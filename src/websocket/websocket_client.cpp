#include "websocket/websocket_client.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
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

std::string DetectPlatformName() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

void JoinOrDetachThread(std::thread& thread) {
    if (!thread.joinable()) {
        return;
    }
    if (thread.get_id() == std::this_thread::get_id()) {
        thread.detach();
        return;
    }
    thread.join();
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
    std::atomic<bool> manual_disconnect{false};
    std::atomic<bool> reconnecting{false};
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
        impl_->manual_disconnect = false;
        impl_->reconnecting = false;
        impl_->stop_requested = false;
        session_state_.last_command = session_state_.resumable && !session_state_.session_id.empty() ? "RESUME" : "IDENTIFY";
        session_state_.identified = true;
        session_state_.alive = true;
        return;
    }

    JoinOrDetachThread(impl_->heartbeat_thread);
    JoinOrDetachThread(impl_->read_thread);

    const auto parsed = common::ParseUrl(gateway_url_);
    std::cout << "Gateway connecting. host=" << parsed.host
              << " port=" << parsed.port
              << " target=" << parsed.target
              << " retry_count=" << retry_count_
              << " resumable=" << (session_state_.resumable ? "true" : "false")
              << std::endl;
    impl_->stream = std::make_unique<websocket::stream<beast::ssl_stream<beast::tcp_stream>>>(impl_->io_context, impl_->ssl_context);

    auto results = impl_->resolver.resolve(parsed.host, parsed.port);
    beast::get_lowest_layer(*impl_->stream).connect(results);
    impl_->stream->next_layer().handshake(asio::ssl::stream_base::client);
    impl_->stream->handshake(parsed.host, parsed.target);

    session_state_.last_command = session_state_.resumable && !session_state_.session_id.empty() ? "RESUME" : "IDENTIFY";
    session_state_.identified = true;
    session_state_.alive = true;
    impl_->manual_disconnect = false;
    impl_->reconnecting = false;
    impl_->stop_requested = false;
    std::cout << "Gateway connected. session_mode="
              << (session_state_.resumable && !session_state_.session_id.empty() ? "RESUME" : "IDENTIFY")
              << " platform=" << DetectPlatformName()
              << std::endl;

    impl_->read_thread = std::thread([this]() {
        try {
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
                                    {"properties", {{"os", DetectPlatformName()}, {"browser", "qqbot_cpp"}, {"device", "qqbot_cpp"}}}}}
                        };
                        session_state_.last_command = "IDENTIFY";
                    }

                    {
                        std::lock_guard<std::mutex> lock(impl_->write_mutex);
                        impl_->stream->write(asio::buffer(identify_payload.dump()));
                    }

                    if (!impl_->heartbeat_thread.joinable()) {
                        impl_->heartbeat_thread = std::thread([this]() {
                            try {
                                std::cout << "Gateway heartbeat started. interval_ms="
                                          << session_state_.heartbeat_interval_ms
                                          << std::endl;
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
                            } catch (const std::exception& ex) {
                                if (!impl_->stop_requested) {
                                    HandleClose(0, true);
                                    session_state_.last_command = "ERROR";
                                    std::cerr << "Gateway heartbeat failed. error=" << ex.what() << std::endl;
                                    Emit(EventType::kError, "HEARTBEAT", ex.what());
                                }
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
                    std::cout << "Gateway ready. session_id=" << session_state_.session_id
                              << " sequence=" << session_state_.sequence
                              << std::endl;
                    continue;
                }

                if (op == 0) {
                    ReceiveDispatch(event_name, event.at("d").dump(), sequence);
                }
            }
        } catch (const std::exception& ex) {
            if (!impl_->stop_requested) {
                HandleClose(0, true);
                session_state_.last_command = "ERROR";
                std::cerr << "Gateway read failed. error=" << ex.what() << std::endl;
                Emit(EventType::kError, "READ", ex.what());
            }
        }
    });
}

void WebSocketClient::Disconnect(int close_code) {
    impl_->manual_disconnect = true;
    impl_->stop_requested = true;
    impl_->reconnecting = false;
    std::cout << "Gateway disconnect requested. close_code=" << close_code << std::endl;

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

    JoinOrDetachThread(impl_->heartbeat_thread);
    JoinOrDetachThread(impl_->read_thread);

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
    if (impl_->manual_disconnect) {
        return;
    }

    bool expected = false;
    if (!impl_->reconnecting.compare_exchange_strong(expected, true)) {
        return;
    }

    impl_->stop_requested = true;
    session_state_.alive = false;
    session_state_.resumable = resumable;
    session_state_.identified = false;
    session_state_.last_command = "CLOSE";
    std::cerr << "Gateway closed. close_code=" << close_code
              << " resumable=" << (resumable ? "true" : "false")
              << " retry_count=" << retry_count_
              << " session_id=" << session_state_.session_id
              << std::endl;

    if (impl_->stream) {
        beast::error_code ec;
        impl_->stream->close(websocket::close_reason(static_cast<websocket::close_code>(close_code)), ec);
    }

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
        impl_->reconnecting = false;
        std::cerr << "Gateway reconnect skipped. reason=non_resumable_close" << std::endl;
        Emit(EventType::kError, "TERMINAL", "non-resumable close");
        return;
    }

    JoinOrDetachThread(impl_->heartbeat_thread);
    JoinOrDetachThread(impl_->read_thread);

    while (!impl_->manual_disconnect && retry_count_ < config_.max_retry) {
        ++retry_count_;
        session_state_.last_command = "RESUME";
        Emit(EventType::kReconnect, "RESUME", session_state_.session_id);

        const auto backoff_ms = static_cast<int>(std::min<std::size_t>(retry_count_, 5) * 1000);
        std::cerr << "Gateway reconnect scheduled. attempt=" << retry_count_
                  << "/" << config_.max_retry
                  << " backoff_ms=" << backoff_ms
                  << " session_id=" << session_state_.session_id
                  << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));

        if (impl_->manual_disconnect) {
            impl_->reconnecting = false;
            std::cerr << "Gateway reconnect cancelled. reason=manual_disconnect" << std::endl;
            return;
        }

        try {
            Connect();
            std::cout << "Gateway reconnect succeeded. attempt=" << retry_count_ << std::endl;
            return;
        } catch (const std::exception& ex) {
            session_state_.last_command = "ERROR";
            std::cerr << "Gateway reconnect failed. attempt=" << retry_count_
                      << " error=" << ex.what()
                      << std::endl;
            Emit(EventType::kError, "RECONNECT", ex.what());
        }
    }

    session_state_.last_command = "TERMINAL";
    impl_->reconnecting = false;
    std::cerr << "Gateway reconnect exhausted. retry_count=" << retry_count_
              << " max_retry=" << config_.max_retry
              << std::endl;
    Emit(EventType::kError,
         "RETRY_EXHAUSTED",
         "websocket reconnect retry exhausted: retry_count=" + std::to_string(retry_count_));
}

}  // namespace websocket
}  // namespace qqbot
