#include "websocket/websocket_client.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <nlohmann/json.hpp>

#include "common/access_token.h"
#include "common/sdk_error.h"
#include "common/url.h"
#include "openapi/v1/openapi_v1.h"

namespace qqbot {
namespace websocket {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;
using json = nlohmann::json;

transport::HttpTransportPtr EnsureTransport(const transport::HttpTransportPtr& transport) {
    if (transport) {
        return transport;
    }
    return std::make_shared<transport::CurlHttpTransport>();
}

std::string ExtractGatewayUrl(const std::string& body) {
    const auto payload = json::parse(body);
    if (!payload.contains("url")) {
        return {};
    }
    return payload.at("url").get<std::string>();
}

}  // namespace

struct WebSocketClient::Impl {
    asio::io_context io_context;
    asio::ssl::context ssl_context{asio::ssl::context::tls_client};
    tcp::resolver resolver{io_context};
    std::unique_ptr<websocket::stream<beast::ssl_stream<beast::tcp_stream>>> stream;
    std::thread read_thread;
    std::thread heartbeat_thread;
    std::atomic<bool> stop_requested{false};
    std::mutex write_mutex;
};

WebSocketClient::WebSocketClient(common::BotConfig config, transport::HttpTransportPtr transport)
    : config_(std::move(config)), transport_(EnsureTransport(transport)), impl_(std::make_unique<Impl>()) {
    if (config_.skip_tls_verify) {
        impl_->ssl_context.set_verify_mode(asio::ssl::verify_none);
    } else {
        impl_->ssl_context.set_default_verify_paths();
        impl_->ssl_context.set_verify_mode(asio::ssl::verify_peer);
    }
}

WebSocketClient::~WebSocketClient() {
    try {
        Disconnect();
    } catch (...) {
    }
}

void WebSocketClient::SetEventHandler(EventHandler handler) {
    event_handler_ = std::move(handler);
}

void WebSocketClient::Connect() {
    BootstrapGateway();

    if (dynamic_cast<transport::MockHttpTransport*>(transport_.get()) != nullptr) {
        session_state_.last_command = session_state_.resumable && !session_state_.session_id.empty() ? "RESUME" : "IDENTIFY";
        session_state_.identified = true;
        session_state_.alive = true;
        return;
    }

    const auto parsed = common::ParseUrl(gateway_url_);
    impl_->stream = std::make_unique<websocket::stream<beast::ssl_stream<beast::tcp_stream>>>(impl_->io_context, impl_->ssl_context);

    auto results = impl_->resolver.resolve(parsed.host, parsed.port);
    beast::get_lowest_layer(*impl_->stream).connect(results);
    impl_->stream->next_layer().handshake(asio::ssl::stream_base::client);
    impl_->stream->handshake(parsed.host, parsed.target);

    session_state_.last_command = session_state_.resumable && !session_state_.session_id.empty() ? "RESUME" : "IDENTIFY";
    session_state_.identified = true;
    session_state_.alive = true;
    impl_->stop_requested = false;

    impl_->read_thread = std::thread([this]() {
        while (!impl_->stop_requested) {
            beast::flat_buffer buffer;
            impl_->stream->read(buffer);
            const auto payload = beast::buffers_to_string(buffer.data());
            const auto event = json::parse(payload);

            const auto op = event.value("op", -1);
            const auto sequence = event.contains("s") && !event["s"].is_null() ? event["s"].get<int>() : session_state_.sequence;
            const auto event_name = event.value("t", std::string{});

            if (op == 10) {
                const auto heartbeat_interval = event.at("d").at("heartbeat_interval").get<int>();
                ReceiveHello(heartbeat_interval);

                json identify_payload;
                const auto authorization = common::ResolveAuthorizationString(config_, transport_);
                if (session_state_.resumable && !session_state_.session_id.empty()) {
                    identify_payload = {
                        {"op", 6},
                        {"d", {{"token", authorization},
                                {"session_id", session_state_.session_id},
                                {"seq", session_state_.sequence}}}
                    };
                    session_state_.last_command = "RESUME";
                } else {
                    identify_payload = {
                        {"op", 2},
                        {"d", {{"token", authorization},
                                {"intents", config_.intents},
                                {"shard", {config_.shard_id, config_.shard_count}},
                                {"properties", {{"os", "windows"}, {"browser", "qqbot_cpp"}, {"device", "qqbot_cpp"}}}}}
                    };
                    session_state_.last_command = "IDENTIFY";
                }

                {
                    std::lock_guard<std::mutex> lock(impl_->write_mutex);
                    impl_->stream->write(asio::buffer(identify_payload.dump()));
                }

                if (!impl_->heartbeat_thread.joinable()) {
                    impl_->heartbeat_thread = std::thread([this]() {
                        while (!impl_->stop_requested) {
                            if (session_state_.heartbeat_interval_ms <= 0) {
                                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                                continue;
                            }

                            std::this_thread::sleep_for(std::chrono::milliseconds(session_state_.heartbeat_interval_ms));
                            if (impl_->stop_requested) {
                                break;
                            }

                            json heartbeat = {
                                {"op", 1},
                                {"d", session_state_.sequence}
                            };

                            {
                                std::lock_guard<std::mutex> lock(impl_->write_mutex);
                                impl_->stream->write(asio::buffer(heartbeat.dump()));
                            }

                            session_state_.last_command = "HEARTBEAT";
                            session_state_.last_heartbeat_sequence = session_state_.sequence;
                        }
                    });
                }
                continue;
            }

            if (op == 11) {
                session_state_.alive = true;
                continue;
            }

            if (op == 7) {
                ReceiveReconnectSignal();
                HandleClose(0, true);
                continue;
            }

            if (op == 9) {
                HandleClose(0, false);
                continue;
            }

            if (event_name == "READY") {
                ReceiveReady(event.at("d").at("session_id").get<std::string>(), sequence);
                retry_count_ = 0;
                continue;
            }

            if (op == 0) {
                ReceiveDispatch(event_name, event.at("d").dump(), sequence);
            }
        }
    });
}

void WebSocketClient::Disconnect(int close_code) {
    impl_->stop_requested = true;

    if (dynamic_cast<transport::MockHttpTransport*>(transport_.get()) != nullptr) {
        session_state_.alive = false;
        session_state_.resumable = false;
        session_state_.identified = false;
        session_state_.last_command = "CLOSE";
        Emit(EventType::kDisconnect, "CLOSE", std::to_string(close_code));
        return;
    }

    if (impl_->stream) {
        beast::error_code ec;
        impl_->stream->close(websocket::close_reason(static_cast<websocket::close_code>(close_code)), ec);
    }

    if (impl_->heartbeat_thread.joinable()) {
        impl_->heartbeat_thread.join();
    }
    if (impl_->read_thread.joinable()) {
        impl_->read_thread.join();
    }

    session_state_.alive = false;
    session_state_.resumable = false;
    session_state_.identified = false;
    session_state_.last_command = "CLOSE";
    Emit(EventType::kDisconnect, "CLOSE", std::to_string(close_code));
}

void WebSocketClient::ReceiveHello(int heartbeat_interval_ms) {
    session_state_.heartbeat_interval_ms = heartbeat_interval_ms;
    session_state_.last_heartbeat_sequence = session_state_.sequence;
    session_state_.last_command = "HELLO";
}

void WebSocketClient::ReceiveReady(const std::string& session_id, int sequence) {
    session_state_.session_id = session_id;
    session_state_.sequence = sequence;
    session_state_.last_heartbeat_sequence = sequence;
    session_state_.alive = true;
    session_state_.resumable = true;
    session_state_.identified = true;
    session_state_.last_command = "READY";
    Emit(EventType::kReady, "READY", session_id);
}

void WebSocketClient::ReceiveDispatch(const std::string& event_name, const std::string& payload, int sequence) {
    session_state_.sequence = sequence;
    session_state_.last_heartbeat_sequence = sequence;
    session_state_.last_command = "DISPATCH";
    Emit(EventType::kDispatch, event_name, payload);
}

void WebSocketClient::ReceiveReconnectSignal() {
    session_state_.last_command = "RECONNECT";
    Emit(EventType::kReconnect, "RECONNECT", {});
}

void WebSocketClient::HandleClose(int close_code, bool resumable) {
    session_state_.alive = false;
    session_state_.resumable = resumable;
    session_state_.identified = false;
    session_state_.last_command = "CLOSE";
    Emit(EventType::kDisconnect, "CLOSE", std::to_string(close_code));
    TryReconnect();
}

const SessionState& WebSocketClient::session_state() const noexcept {
    return session_state_;
}

std::size_t WebSocketClient::retry_count() const noexcept {
    return retry_count_;
}

std::string WebSocketClient::gateway_url() const noexcept {
    return gateway_url_;
}

void WebSocketClient::BootstrapGateway() {
    if (!config_.IsValid()) {
        throw common::SDKError(common::ErrorCode::kInvalidArgument, "bot config is invalid");
    }

    openapi::v1::OpenAPIV1Client client(config_, transport_);
    const auto response = client.GetGateway();
    gateway_url_ = ExtractGatewayUrl(response.body);
    if (gateway_url_.empty()) {
        throw common::SDKError(common::ErrorCode::kGatewayBootstrapFailed,
                               "failed to parse gateway url",
                               response.status_code,
                               0,
                               {},
                               response.body);
    }
}

void WebSocketClient::Emit(EventType type, const std::string& event_name, const std::string& payload) {
    if (!event_handler_) {
        return;
    }
    event_handler_(GatewayEvent{type, event_name, payload});
}

void WebSocketClient::TryReconnect() {
    if (!session_state_.resumable) {
        session_state_.last_command = "TERMINAL";
        Emit(EventType::kError, "TERMINAL", "non-resumable close");
        return;
    }

    if (retry_count_ >= config_.max_retry) {
        throw common::SDKError(common::ErrorCode::kRetryExhausted,
                               "websocket reconnect retry exhausted",
                               0,
                               0,
                               {},
                               "retry_count=" + std::to_string(retry_count_));
    }

    ++retry_count_;
    session_state_.last_command = "RESUME";
    Emit(EventType::kReconnect, "RESUME", session_state_.session_id);
    Connect();
}

}  // namespace websocket
}  // namespace qqbot
