#include "websocket/websocket_client.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <condition_variable>

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
namespace fs = std::filesystem;

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

std::int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string SanitizeFileName(std::string value) {
    std::replace_if(value.begin(), value.end(), [](unsigned char ch) {
        return !(std::isalnum(ch) || ch == '-' || ch == '_');
    }, '_');
    return value;
}

fs::path GetSessionDirectory() {
    if (const auto* local_app_data = std::getenv("LOCALAPPDATA"); local_app_data != nullptr && *local_app_data != '\0') {
        return fs::path(local_app_data) / "qqbot_cpp" / "sessions";
    }
    return fs::temp_directory_path() / "qqbot_cpp" / "sessions";
}

fs::path GetSessionFilePath(const common::BotConfig& config) {
    return GetSessionDirectory() / ("session-" + SanitizeFileName(config.app_id) + ".json");
}

struct PersistedSessionState {
    std::string session_id;
    int sequence{0};
    std::int64_t saved_at_ms{0};
};

PersistedSessionState LoadPersistedSession(const common::BotConfig& config) {
    PersistedSessionState state;
    const auto path = GetSessionFilePath(config);
    if (!fs::exists(path)) {
        return state;
    }

    try {
        std::ifstream input(path);
        const auto payload = json::parse(input);
        state.session_id = payload.value("session_id", std::string{});
        state.sequence = payload.value("sequence", 0);
        state.saved_at_ms = payload.value("saved_at_ms", static_cast<std::int64_t>(0));

        constexpr std::int64_t kSessionExpireMs = 5 * 60 * 1000;
        if (state.session_id.empty() || state.sequence <= 0 || NowMs() - state.saved_at_ms > kSessionExpireMs) {
            fs::remove(path);
            return PersistedSessionState{};
        }
    } catch (...) {
        fs::remove(path);
        return PersistedSessionState{};
    }

    return state;
}

void SavePersistedSession(const common::BotConfig& config, const SessionState& session_state) {
    if (session_state.session_id.empty() || session_state.sequence <= 0) {
        return;
    }

    const auto path = GetSessionFilePath(config);
    fs::create_directories(path.parent_path());

    std::ofstream output(path, std::ios::trunc);
    output << json{{"session_id", session_state.session_id},
                   {"sequence", session_state.sequence},
                   {"saved_at_ms", NowMs()}}
                  .dump();
}

void ClearPersistedSession(const common::BotConfig& config) {
    std::error_code ec;
    fs::remove(GetSessionFilePath(config), ec);
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
    std::thread reconnect_thread;
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> manual_disconnect{false};
    std::atomic<bool> reconnecting{false};
    ConnectionPhase phase{ConnectionPhase::kDisconnected};
    std::int64_t last_connect_time_ms{0};
    std::size_t quick_disconnect_count{0};
    int pending_close_code{0};
    bool pending_close_resumable{false};
    bool reconnect_request_pending{false};
    bool control_loop_running{true};
    std::condition_variable reconnect_cv;
    std::mutex state_mutex;
    std::mutex stream_mutex;
    std::mutex write_mutex;
};

namespace {

SessionState SnapshotSessionState(const std::unique_ptr<WebSocketClient::Impl>& impl, const SessionState& state) {
    std::lock_guard<std::mutex> lock(impl->state_mutex);
    return state;
}

template <typename Fn>
auto WithSessionState(const std::unique_ptr<WebSocketClient::Impl>& impl, SessionState& state, Fn&& fn) -> decltype(fn(state)) {
    std::lock_guard<std::mutex> lock(impl->state_mutex);
    return fn(state);
}

template <typename Fn>
auto WithStream(const std::unique_ptr<WebSocketClient::Impl>& impl, Fn&& fn) -> decltype(fn(*impl->stream)) {
    std::lock_guard<std::mutex> lock(impl->stream_mutex);
    return fn(*impl->stream);
}

constexpr std::int64_t kQuickDisconnectThresholdMs = 15 * 1000;
constexpr std::size_t kMaxQuickDisconnectCount = 3;

struct ReconnectDecision {
    std::string action;
    bool terminal{false};
    bool refresh_session{false};
    bool clear_access_token_cache{false};
};

struct ReconnectRequest {
    int close_code{0};
    bool resumable{false};
};

std::string DescribeCloseCode(int close_code) {
    switch (close_code) {
    case 4000:
        return "unknown gateway error";
    case 4001:
        return "unknown opcode";
    case 4002:
        return "decode error";
    case 4003:
        return "not authenticated";
    case 1000:
        return "normal closure";
    case 1006:
        return "abnormal closure";
    case 1012:
        return "service restart / reconnect requested";
    case 4004:
        return "invalid token";
    case 4006:
        return "session no longer valid";
    case 4007:
        return "invalid seq on resume";
    case 4008:
        return "rate limited";
    case 4009:
        return "session timed out / invalid session";
    case 4010:
        return "invalid shard";
    case 4011:
        return "sharding required";
    case 4012:
        return "invalid api version";
    case 4013:
        return "invalid intents";
    case 4014:
        return "disallowed intents";
    case 4914:
        return "bot offline or sandbox only";
    case 4915:
        return "bot banned";
    default:
        if (close_code >= 4900 && close_code <= 4913) {
            return "internal server error";
        }
        return "unknown";
    }
}

std::string DescribeReconnectAction(int close_code, bool resumable) {
    if (close_code == 4004) {
        return "REFRESH_TOKEN_IDENTIFY";
    }
    if (close_code == 4006 || close_code == 4007 || close_code == 4009 || (close_code >= 4900 && close_code <= 4913)) {
        return "RESET_SESSION_IDENTIFY";
    }
    if (close_code == 4008) {
        return "RATE_LIMIT_BACKOFF";
    }
    if (close_code == 4914 || close_code == 4915) {
        return "TERMINAL";
    }
    return resumable ? "RESUME" : "TERMINAL";
}

ReconnectDecision BuildReconnectDecision(int close_code, bool resumable) {
    ReconnectDecision decision;
    decision.action = DescribeReconnectAction(close_code, resumable);
    decision.terminal = close_code == 4914 || close_code == 4915 || !resumable;
    decision.refresh_session = close_code == 4004 || close_code == 4006 || close_code == 4007 || close_code == 4009 ||
                               (close_code >= 4900 && close_code <= 4913);
    decision.clear_access_token_cache = close_code == 4004;
    return decision;
}

void StoreReconnectRequest(const std::unique_ptr<WebSocketClient::Impl>& impl, int close_code, bool resumable) {
    std::lock_guard<std::mutex> lock(impl->state_mutex);
    impl->pending_close_code = close_code;
    impl->pending_close_resumable = resumable;
    impl->reconnect_request_pending = true;
}

ReconnectRequest TakeReconnectRequest(const std::unique_ptr<WebSocketClient::Impl>& impl) {
    std::lock_guard<std::mutex> lock(impl->state_mutex);
    impl->reconnect_request_pending = false;
    return ReconnectRequest{impl->pending_close_code, impl->pending_close_resumable};
}

void SetLastCommand(const std::unique_ptr<WebSocketClient::Impl>& impl, SessionState& state, const char* command) {
    WithSessionState(impl, state, [&](SessionState& current) {
        current.last_command = command;
    });
}

void MarkIdentifyMode(const std::unique_ptr<WebSocketClient::Impl>& impl, SessionState& state, bool resumable_mode) {
    WithSessionState(impl, state, [&](SessionState& current) {
        current.last_command = resumable_mode ? "RESUME" : "IDENTIFY";
    });
}

void MarkHeartbeatSentState(const std::unique_ptr<WebSocketClient::Impl>& impl, SessionState& state) {
    WithSessionState(impl, state, [&](SessionState& current) {
        current.last_command = "HEARTBEAT";
        current.last_heartbeat_sequence = current.sequence;
    });
}

void MarkHeartbeatTimeoutState(const std::unique_ptr<WebSocketClient::Impl>& impl, SessionState& state) {
    WithSessionState(impl, state, [&](SessionState& current) {
        current.last_command = "HEARTBEAT_TIMEOUT";
    });
}

void MarkHeartbeatAckState(const std::unique_ptr<WebSocketClient::Impl>& impl, SessionState& state) {
    WithSessionState(impl, state, [&](SessionState& current) {
        current.alive = true;
        current.last_heartbeat_ack_at_ms = NowMs();
        current.heartbeat_ack_pending = false;
        current.consecutive_missed_acks = 0;
    });
}

SessionState MarkResumedState(const std::unique_ptr<WebSocketClient::Impl>& impl, SessionState& state) {
    SessionState snapshot;
    WithSessionState(impl, state, [&](SessionState& current) {
        current.alive = true;
        current.resumable = true;
        current.identified = true;
        current.last_command = "RESUMED";
        current.last_heartbeat_ack_at_ms = NowMs();
        current.heartbeat_ack_pending = false;
        current.consecutive_missed_acks = 0;
        snapshot = current;
    });
    return snapshot;
}

SessionState MarkConnectedState(const std::unique_ptr<WebSocketClient::Impl>& impl, SessionState& state) {
    SessionState snapshot;
    WithSessionState(impl, state, [&](SessionState& current) {
        current.last_command = current.resumable && !current.session_id.empty() ? "RESUME" : "IDENTIFY";
        current.identified = true;
        current.alive = true;
        current.last_close_code = 0;
        current.heartbeat_ack_pending = false;
        current.consecutive_missed_acks = 0;
        current.last_heartbeat_sent_at_ms = 0;
        current.last_heartbeat_ack_at_ms = NowMs();
        snapshot = current;
    });
    return snapshot;
}

SessionState MarkClosedState(const std::unique_ptr<WebSocketClient::Impl>& impl,
                             SessionState& state,
                             int close_code,
                             bool resumable,
                             const char* command) {
    SessionState snapshot;
    WithSessionState(impl, state, [&](SessionState& current) {
        current.last_close_code = close_code;
        current.alive = false;
        current.resumable = resumable;
        current.identified = false;
        current.last_command = command;
        snapshot = current;
    });
    return snapshot;
}

ConnectionPhase SnapshotConnectionPhase(const std::unique_ptr<WebSocketClient::Impl>& impl) {
    std::lock_guard<std::mutex> lock(impl->state_mutex);
    return impl->phase;
}

bool CanHandleClose(ConnectionPhase phase) {
    return phase != ConnectionPhase::kClosing && phase != ConnectionPhase::kDisconnected && phase != ConnectionPhase::kTerminal;
}

bool CanScheduleReconnect(ConnectionPhase phase) {
    return phase == ConnectionPhase::kClosing || phase == ConnectionPhase::kReconnecting;
}

bool CanSendHeartbeat(ConnectionPhase phase) {
    return phase == ConnectionPhase::kIdentifying || phase == ConnectionPhase::kReady || phase == ConnectionPhase::kReconnecting;
}

bool CanAcceptHeartbeatAck(ConnectionPhase phase) {
    return phase == ConnectionPhase::kIdentifying || phase == ConnectionPhase::kReady || phase == ConnectionPhase::kReconnecting;
}

bool CanReceiveReconnectSignal(ConnectionPhase phase) {
    return phase == ConnectionPhase::kIdentifying || phase == ConnectionPhase::kReady || phase == ConnectionPhase::kReconnecting;
}

bool CanReceiveReadyLike(ConnectionPhase phase) {
    return phase == ConnectionPhase::kIdentifying || phase == ConnectionPhase::kConnecting || phase == ConnectionPhase::kReconnecting;
}

bool CanReceiveDispatchInPhase(ConnectionPhase phase) {
    return phase == ConnectionPhase::kReady || phase == ConnectionPhase::kIdentifying;
}

const char* ToString(ConnectionPhase phase) {
    switch (phase) {
    case ConnectionPhase::kDisconnected:
        return "DISCONNECTED";
    case ConnectionPhase::kConnecting:
        return "CONNECTING";
    case ConnectionPhase::kIdentifying:
        return "IDENTIFYING";
    case ConnectionPhase::kReady:
        return "READY";
    case ConnectionPhase::kClosing:
        return "CLOSING";
    case ConnectionPhase::kReconnecting:
        return "RECONNECTING";
    case ConnectionPhase::kTerminal:
        return "TERMINAL";
    }
    return "UNKNOWN";
}

}  // namespace

WebSocketClient::WebSocketClient(common::BotConfig config, transport::HttpTransportPtr transport)
    : config_(std::move(config)), transport_(EnsureTransport(transport)), impl_(std::make_unique<Impl>()) {
    if (config_.skip_tls_verify) {
        impl_->ssl_context.set_verify_mode(asio::ssl::verify_none);
    } else {
        impl_->ssl_context.set_default_verify_paths();
        impl_->ssl_context.set_verify_mode(asio::ssl::verify_peer);
    }
    impl_->reconnect_thread = std::thread([this]() {
        ControlLoop();
    });
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
    SetConnectionPhase(ConnectionPhase::kConnecting, "Connect");
    BootstrapGateway();

    if (WithSessionState(impl_, session_state_, [](SessionState& state) { return state.session_id.empty(); })) {
        const auto persisted = LoadPersistedSession(config_);
        if (!persisted.session_id.empty()) {
            WithSessionState(impl_, session_state_, [&](SessionState& state) {
                state.session_id = persisted.session_id;
                state.sequence = persisted.sequence;
                state.last_heartbeat_sequence = persisted.sequence;
                state.resumable = true;
            });
            const auto snapshot = SnapshotSessionState(impl_, session_state_);
            std::cout << "Gateway session restored from disk. session_id=" << snapshot.session_id
                      << " sequence=" << snapshot.sequence << std::endl;
        }
    }

    if (dynamic_cast<transport::MockHttpTransport*>(transport_.get()) != nullptr) {
        impl_->manual_disconnect = false;
        impl_->reconnecting = false;
        impl_->stop_requested = false;
        const auto snapshot = MarkConnectedState(impl_, session_state_);
        if (!snapshot.session_id.empty() && snapshot.sequence > 0) {
            SavePersistedSession(config_, snapshot);
        }
        SetConnectionPhase(ConnectionPhase::kIdentifying, "ConnectMock");
        return;
    }

    JoinOrDetachThread(impl_->heartbeat_thread);
    JoinOrDetachThread(impl_->read_thread);
    if (impl_->reconnect_thread.joinable() && impl_->reconnect_thread.get_id() != std::this_thread::get_id()) {
        impl_->reconnect_thread.join();
    }

    const auto parsed = common::ParseUrl(gateway_url_);
    const auto pre_connect_snapshot = SnapshotSessionState(impl_, session_state_);
    std::cout << "Gateway connecting. host=" << parsed.host
              << " port=" << parsed.port
              << " target=" << parsed.target
              << " retry_count=" << retry_count_
              << " resumable=" << (pre_connect_snapshot.resumable ? "true" : "false")
              << std::endl;
    {
        std::lock_guard<std::mutex> lock(impl_->stream_mutex);
        impl_->stream = std::make_unique<websocket::stream<beast::ssl_stream<beast::tcp_stream>>>(impl_->io_context, impl_->ssl_context);
    }

    auto results = impl_->resolver.resolve(parsed.host, parsed.port);
    WithStream(impl_, [&](auto& stream) {
        beast::get_lowest_layer(stream).connect(results);
        stream.next_layer().handshake(asio::ssl::stream_base::client);
        stream.handshake(parsed.host, parsed.target);
    });

    const auto connected_snapshot = MarkConnectedState(impl_, session_state_);
    impl_->manual_disconnect = false;
    impl_->reconnecting = false;
    impl_->stop_requested = false;
    impl_->last_connect_time_ms = NowMs();
    SetConnectionPhase(ConnectionPhase::kIdentifying, "ConnectEstablished");
    std::cout << "Gateway connected. session_mode="
              << (connected_snapshot.resumable && !connected_snapshot.session_id.empty() ? "RESUME" : "IDENTIFY")
              << " platform=" << DetectPlatformName()
              << std::endl;

    impl_->read_thread = std::thread([this]() {
        try {
            while (!impl_->stop_requested) {
                beast::flat_buffer buffer;
                WithStream(impl_, [&](auto& stream) {
                    stream.read(buffer);
                });
                const auto payload = beast::buffers_to_string(buffer.data());
                const auto event = json::parse(payload);

                const auto op = event.value("op", -1);
                const auto state_snapshot = SnapshotSessionState(impl_, session_state_);
                const auto sequence = event.contains("s") && !event["s"].is_null() ? event["s"].get<int>() : state_snapshot.sequence;
                const auto event_name = event.value("t", std::string{});

                if (op == 10) {
                    const auto heartbeat_interval = event.at("d").at("heartbeat_interval").get<int>();
                    ReceiveHello(heartbeat_interval);

                    json identify_payload;
                    const auto authorization = common::ResolveAuthorizationString(config_, transport_);
                    const auto identify_state = SnapshotSessionState(impl_, session_state_);
                    if (identify_state.resumable && !identify_state.session_id.empty()) {
                        identify_payload = {
                            {"op", 6},
                            {"d", {{"token", authorization},
                                    {"session_id", identify_state.session_id},
                                    {"seq", identify_state.sequence}}}
                        };
                    } else {
                        identify_payload = {
                            {"op", 2},
                            {"d", {{"token", authorization},
                                    {"intents", config_.intents},
                                    {"shard", {config_.shard_id, config_.shard_count}},
                                    {"properties", {{"os", DetectPlatformName()}, {"browser", "qqbot_cpp"}, {"device", "qqbot_cpp"}}}}}
                        };
                    }
                    MarkIdentifyMode(impl_, session_state_, identify_state.resumable && !identify_state.session_id.empty());

                    {
                        std::lock_guard<std::mutex> lock(impl_->write_mutex);
                        WithStream(impl_, [&](auto& stream) {
                            stream.write(asio::buffer(identify_payload.dump()));
                        });
                    }

                    const auto after_identify = SnapshotSessionState(impl_, session_state_);
                    std::cout << "Gateway identify sent. mode=" << after_identify.last_command
                              << " session_id=" << after_identify.session_id
                              << " sequence=" << after_identify.sequence << std::endl;

                    if (!impl_->heartbeat_thread.joinable()) {
                        impl_->heartbeat_thread = std::thread([this]() {
                            try {
                                std::cout << "Gateway heartbeat started. interval_ms="
                                          << SnapshotSessionState(impl_, session_state_).heartbeat_interval_ms
                                          << std::endl;
                                while (!impl_->stop_requested) {
                                    const auto heartbeat_state = SnapshotSessionState(impl_, session_state_);
                                    const auto heartbeat_phase = SnapshotConnectionPhase(impl_);
                                    if (heartbeat_state.heartbeat_interval_ms <= 0) {
                                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                                        continue;
                                    }

                                    std::this_thread::sleep_for(std::chrono::milliseconds(heartbeat_state.heartbeat_interval_ms));
                                    if (impl_->stop_requested) {
                                        break;
                                    }

                                    if (!CanSendHeartbeat(heartbeat_phase)) {
                                        continue;
                                    }

                                    if (IsHeartbeatTimedOut()) {
                                        MarkHeartbeatTimeoutState(impl_, session_state_);
                                        const auto timeout_state = SnapshotSessionState(impl_, session_state_);
                                        std::cerr << "Gateway heartbeat timed out. missed_acks="
                                                  << timeout_state.consecutive_missed_acks
                                                  << " session_id=" << timeout_state.session_id
                                                  << std::endl;
                                        HandleClose(4009, false);
                                        break;
                                    }

                                    json heartbeat = {
                                        {"op", 1},
                                        {"d", heartbeat_state.sequence}
                                    };

                                    {
                                        std::lock_guard<std::mutex> lock(impl_->write_mutex);
                                        WithStream(impl_, [&](auto& stream) {
                                            stream.write(asio::buffer(heartbeat.dump()));
                                        });
                                    }

                                    MarkHeartbeatSentState(impl_, session_state_);
                                    MarkHeartbeatSent();
                                }
                            } catch (const std::exception& ex) {
                                if (!impl_->stop_requested) {
                                    HandleClose(ExtractCloseCodeFromException(ex), true);
                                    SetLastCommand(impl_, session_state_, "ERROR");
                                    std::cerr << "Gateway heartbeat failed. error=" << ex.what() << std::endl;
                                    Emit(EventType::kError, "HEARTBEAT", ex.what());
                                }
                            }
                        });
                    }
                    continue;
                }

                if (op == 11) {
                    if (!CanAcceptHeartbeatAck(SnapshotConnectionPhase(impl_))) {
                        continue;
                    }
                    MarkHeartbeatAckState(impl_, session_state_);
                    continue;
                }

                if (op == 7) {
                    if (!CanReceiveReconnectSignal(SnapshotConnectionPhase(impl_))) {
                        continue;
                    }
                    ReceiveReconnectSignal();
                    HandleClose(1012, true);
                    continue;
                }

                if (op == 9) {
                    const auto resumable = event.contains("d") && event["d"].is_boolean() ? event["d"].get<bool>() : false;
                    HandleClose(4009, resumable);
                    continue;
                }

                if (event_name == "READY") {
                    if (!CanReceiveReadyLike(SnapshotConnectionPhase(impl_))) {
                        continue;
                    }
                    ReceiveReady(event.at("d").at("session_id").get<std::string>(), sequence);
                    retry_count_ = 0;
                    const auto ready_state = SnapshotSessionState(impl_, session_state_);
                    std::cout << "Gateway ready. session_id=" << ready_state.session_id
                              << " sequence=" << ready_state.sequence
                              << std::endl;
                    continue;
                }

                if (event_name == "RESUMED") {
                    if (!CanReceiveReadyLike(SnapshotConnectionPhase(impl_))) {
                        continue;
                    }
                    const auto resumed_state = MarkResumedState(impl_, session_state_);
                    SetConnectionPhase(ConnectionPhase::kReady, "ReceiveResumed");
                    SavePersistedSession(config_, resumed_state);
                    retry_count_ = 0;
                    Emit(EventType::kReady, "RESUMED", resumed_state.session_id);
                    continue;
                }

                if (op == 0) {
                    if (!CanReceiveDispatchInPhase(SnapshotConnectionPhase(impl_))) {
                        continue;
                    }
                    ReceiveDispatch(event_name, event.at("d").dump(), sequence);
                }
            }
        } catch (const std::exception& ex) {
            if (!impl_->stop_requested) {
                HandleClose(ExtractCloseCodeFromException(ex), true);
                SetLastCommand(impl_, session_state_, "ERROR");
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
    SetConnectionPhase(ConnectionPhase::kClosing, "Disconnect");
    WithSessionState(impl_, session_state_, [&](SessionState& state) { state.last_close_code = close_code; });
    {
        std::lock_guard<std::mutex> lock(impl_->state_mutex);
        impl_->control_loop_running = false;
        impl_->reconnect_request_pending = false;
    }
    impl_->reconnect_cv.notify_all();
    std::cout << "Gateway disconnect requested. close_code=" << close_code << std::endl;

    if (dynamic_cast<transport::MockHttpTransport*>(transport_.get()) != nullptr) {
        if (impl_->reconnect_thread.joinable() && impl_->reconnect_thread.get_id() != std::this_thread::get_id()) {
            impl_->reconnect_thread.join();
        }
        MarkClosedState(impl_, session_state_, close_code, false, "CLOSE");
        SetConnectionPhase(ConnectionPhase::kDisconnected, "DisconnectMockDone");
        Emit(EventType::kDisconnect, "CLOSE", std::to_string(close_code));
        return;
    }

    if (impl_->stream) {
        beast::error_code ec;
        WithStream(impl_, [&](auto& stream) {
            stream.close(websocket::close_reason(static_cast<websocket::close_code>(close_code)), ec);
        });
    }

    JoinOrDetachThread(impl_->heartbeat_thread);
    JoinOrDetachThread(impl_->read_thread);
    if (impl_->reconnect_thread.joinable() && impl_->reconnect_thread.get_id() != std::this_thread::get_id()) {
        impl_->reconnect_thread.join();
    }

    MarkClosedState(impl_, session_state_, close_code, false, "CLOSE");
    SetConnectionPhase(ConnectionPhase::kDisconnected, "DisconnectDone");
    ClearPersistedSession(config_);
    Emit(EventType::kDisconnect, "CLOSE", std::to_string(close_code));
}

void WebSocketClient::ReceiveHello(int heartbeat_interval_ms) {
    WithSessionState(impl_, session_state_, [&](SessionState& state) {
        state.heartbeat_interval_ms = heartbeat_interval_ms;
        state.last_heartbeat_sequence = state.sequence;
        state.last_heartbeat_ack_at_ms = NowMs();
        state.heartbeat_ack_pending = false;
        state.consecutive_missed_acks = 0;
        state.last_command = "HELLO";
    });
}

void WebSocketClient::ReceiveReady(const std::string& session_id, int sequence) {
    if (!CanReceiveReadyLike(SnapshotConnectionPhase(impl_))) {
        return;
    }
    SessionState snapshot;
    WithSessionState(impl_, session_state_, [&](SessionState& state) {
        state.session_id = session_id;
        state.sequence = sequence;
        state.last_heartbeat_sequence = sequence;
        state.alive = true;
        state.resumable = true;
        state.identified = true;
        state.last_heartbeat_ack_at_ms = NowMs();
        state.heartbeat_ack_pending = false;
        state.consecutive_missed_acks = 0;
        state.last_command = "READY";
        snapshot = state;
    });
    SetConnectionPhase(ConnectionPhase::kReady, "ReceiveReady");
    SavePersistedSession(config_, snapshot);
    Emit(EventType::kReady, "READY", session_id);
}

void WebSocketClient::ReceiveDispatch(const std::string& event_name, const std::string& payload, int sequence) {
    if (!CanReceiveDispatchInPhase(SnapshotConnectionPhase(impl_))) {
        return;
    }
    SessionState snapshot;
    WithSessionState(impl_, session_state_, [&](SessionState& state) {
        state.sequence = sequence;
        state.last_heartbeat_sequence = sequence;
        state.last_command = "DISPATCH";
        snapshot = state;
    });
    SavePersistedSession(config_, snapshot);
    Emit(EventType::kDispatch, event_name, payload);
}

void WebSocketClient::ReceiveReconnectSignal() {
    if (!CanReceiveReconnectSignal(SnapshotConnectionPhase(impl_))) {
        return;
    }
    SetLastCommand(impl_, session_state_, "RECONNECT");
    Emit(EventType::kReconnect, "RECONNECT", {});
}

void WebSocketClient::HandleClose(int close_code, bool resumable) {
    if (impl_->manual_disconnect) {
        return;
    }

    if (!CanHandleClose(SnapshotConnectionPhase(impl_))) {
        return;
    }

    bool expected = false;
    if (!impl_->reconnecting.compare_exchange_strong(expected, true)) {
        return;
    }

    impl_->stop_requested = true;
    SetConnectionPhase(ConnectionPhase::kClosing, "HandleClose");
    const auto snapshot = MarkClosedState(impl_, session_state_, close_code, resumable, "CLOSE");
    StoreReconnectRequest(impl_, close_code, resumable);
    std::cerr << "Gateway closed. close_code=" << close_code
              << " reason=" << DescribeCloseCode(close_code)
              << " resumable=" << (resumable ? "true" : "false")
              << " retry_count=" << retry_count_
              << " session_id=" << snapshot.session_id
              << std::endl;

    const auto connection_duration_ms = impl_->last_connect_time_ms > 0 ? NowMs() - impl_->last_connect_time_ms : 0;
    if (impl_->last_connect_time_ms > 0 && connection_duration_ms < kQuickDisconnectThresholdMs) {
        ++impl_->quick_disconnect_count;
        std::cerr << "Gateway quick disconnect detected. duration_ms=" << connection_duration_ms
                  << " count=" << impl_->quick_disconnect_count
                  << std::endl;
        if (impl_->quick_disconnect_count >= kMaxQuickDisconnectCount) {
            std::cerr << "Gateway repeated quick disconnects detected. app_id=" << config_.app_id
                      << " intents=" << config_.intents
                      << " hint=check token, intents, bot permissions, and gateway availability"
                      << std::endl;
        }
    } else if (connection_duration_ms >= kQuickDisconnectThresholdMs) {
        impl_->quick_disconnect_count = 0;
    }

    if (impl_->stream) {
        beast::error_code ec;
        WithStream(impl_, [&](auto& stream) {
            stream.close(websocket::close_reason(static_cast<websocket::close_code>(close_code)), ec);
        });
    }

    Emit(EventType::kDisconnect, "CLOSE", std::to_string(close_code));
    ScheduleReconnect();
}

SessionState WebSocketClient::session_state() const noexcept {
    return SnapshotSessionState(impl_, session_state_);
}

ConnectionPhase WebSocketClient::connection_phase() const noexcept {
    return SnapshotConnectionPhase(impl_);
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

void WebSocketClient::ScheduleReconnect() {
    if (impl_->manual_disconnect) {
        impl_->reconnecting = false;
        return;
    }

    if (!CanScheduleReconnect(SnapshotConnectionPhase(impl_))) {
        impl_->reconnecting = false;
        return;
    }

    impl_->reconnect_cv.notify_one();
}

void WebSocketClient::ControlLoop() {
    while (true) {
        {
            std::unique_lock<std::mutex> lock(impl_->state_mutex);
            impl_->reconnect_cv.wait(lock, [&]() {
                return !impl_->control_loop_running || impl_->reconnect_request_pending;
            });
            if (!impl_->control_loop_running) {
                return;
            }
        }

        SetConnectionPhase(ConnectionPhase::kReconnecting, "ControlLoop");
        JoinOrDetachThread(impl_->heartbeat_thread);
        JoinOrDetachThread(impl_->read_thread);
        const auto request = TakeReconnectRequest(impl_);
        StoreReconnectRequest(impl_, request.close_code, request.resumable);
        TryReconnect();
    }
}

void WebSocketClient::TryReconnect() {
    auto snapshot = SnapshotSessionState(impl_, session_state_);
    const auto request = TakeReconnectRequest(impl_);
    const auto decision = BuildReconnectDecision(request.close_code, request.resumable);
    std::cerr << "Gateway reconnect decision. close_code=" << request.close_code
              << " reason=" << DescribeCloseCode(request.close_code)
              << " action=" << decision.action
              << " resumable=" << (request.resumable ? "true" : "false")
              << " session_id=" << snapshot.session_id
              << " sequence=" << snapshot.sequence
              << std::endl;

    if (decision.terminal && IsTerminalCloseCode(request.close_code)) {
        SetLastCommand(impl_, session_state_, "TERMINAL");
        SetConnectionPhase(ConnectionPhase::kTerminal, "TryReconnectTerminalCode");
        impl_->reconnecting = false;
        std::cerr << "Gateway reconnect skipped. reason=terminal_close_code close_code="
                  << request.close_code << std::endl;
        Emit(EventType::kError, "TERMINAL", "terminal close code: " + std::to_string(request.close_code));
        return;
    }

    if (decision.refresh_session) {
        if (decision.clear_access_token_cache) {
            common::ClearAccessTokenCache(config_);
            std::cerr << "Gateway reconnect session refresh. action=clear_access_token_cache close_code="
                      << request.close_code << std::endl;
        }
        std::cerr << "Gateway reconnect session refresh. action=reset_session_for_identify close_code="
                  << request.close_code
                  << " reason=" << DescribeCloseCode(request.close_code)
                  << std::endl;
        ResetSessionForIdentify();
        snapshot = SnapshotSessionState(impl_, session_state_);
    }

    if (decision.terminal) {
        SetLastCommand(impl_, session_state_, "TERMINAL");
        SetConnectionPhase(ConnectionPhase::kTerminal, "TryReconnectNonResumable");
        impl_->reconnecting = false;
        std::cerr << "Gateway reconnect skipped. reason=non_resumable_close" << std::endl;
        Emit(EventType::kError, "TERMINAL", "non-resumable close");
        return;
    }

    while (!impl_->manual_disconnect && retry_count_ < config_.max_retry) {
        ++retry_count_;
        MarkIdentifyMode(impl_, session_state_, snapshot.resumable && !snapshot.session_id.empty());
        snapshot = SnapshotSessionState(impl_, session_state_);
        Emit(EventType::kReconnect,
             snapshot.resumable && !snapshot.session_id.empty() ? "RESUME" : "IDENTIFY",
             snapshot.session_id);

        const auto backoff_ms = GetReconnectDelayMs(snapshot.last_close_code);
        std::cerr << "Gateway reconnect scheduled. attempt=" << retry_count_
                  << "/" << config_.max_retry
                  << " backoff_ms=" << backoff_ms
                  << " close_code=" << snapshot.last_close_code
                  << " mode=" << (snapshot.resumable && !snapshot.session_id.empty() ? "RESUME" : "IDENTIFY")
                  << " sequence=" << snapshot.sequence
                  << " session_id=" << snapshot.session_id
                  << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));

        if (impl_->manual_disconnect) {
            impl_->reconnecting = false;
            std::cerr << "Gateway reconnect cancelled. reason=manual_disconnect" << std::endl;
            return;
        }

        try {
            impl_->stop_requested = false;
            Connect();
            std::cout << "Gateway reconnect succeeded. attempt=" << retry_count_ << std::endl;
            return;
        } catch (const std::exception& ex) {
            SetLastCommand(impl_, session_state_, "ERROR");
            std::cerr << "Gateway reconnect failed. attempt=" << retry_count_
                      << " error=" << ex.what()
                      << std::endl;
            Emit(EventType::kError, "RECONNECT", ex.what());
        }
    }

    SetLastCommand(impl_, session_state_, "TERMINAL");
    SetConnectionPhase(ConnectionPhase::kTerminal, "TryReconnectExhausted");
    impl_->reconnecting = false;
    std::cerr << "Gateway reconnect exhausted. retry_count=" << retry_count_
              << " max_retry=" << config_.max_retry
              << std::endl;
    Emit(EventType::kError,
         "RETRY_EXHAUSTED",
         "websocket reconnect retry exhausted: retry_count=" + std::to_string(retry_count_));
}

void WebSocketClient::SetConnectionPhase(ConnectionPhase phase, const char* source) {
    ConnectionPhase previous;
    {
        std::lock_guard<std::mutex> lock(impl_->state_mutex);
        previous = impl_->phase;
        impl_->phase = phase;
    }
    if (previous != phase) {
        std::cout << "Gateway phase transition. from=" << ToString(previous)
                  << " to=" << ToString(phase)
                  << " source=" << source
                  << std::endl;
    }
}

void WebSocketClient::ResetSessionForIdentify() {
    ClearPersistedSession(config_);
    WithSessionState(impl_, session_state_, [](SessionState& state) {
        state.session_id.clear();
        state.sequence = 0;
        state.last_heartbeat_sequence = 0;
        state.resumable = true;
        state.identified = false;
        state.heartbeat_ack_pending = false;
        state.consecutive_missed_acks = 0;
    });
}

void WebSocketClient::MarkHeartbeatSent() {
    WithSessionState(impl_, session_state_, [](SessionState& state) {
        state.last_heartbeat_sent_at_ms = NowMs();
        state.heartbeat_ack_pending = true;
        ++state.consecutive_missed_acks;
    });
}

void WebSocketClient::MarkHeartbeatAck() {
    WithSessionState(impl_, session_state_, [](SessionState& state) {
        state.last_heartbeat_ack_at_ms = NowMs();
        state.heartbeat_ack_pending = false;
        state.consecutive_missed_acks = 0;
    });
}

bool WebSocketClient::IsHeartbeatTimedOut() const {
    const auto snapshot = SnapshotSessionState(impl_, session_state_);
    if (!snapshot.heartbeat_ack_pending || snapshot.heartbeat_interval_ms <= 0) {
        return false;
    }

    const auto now = NowMs();
    const auto timeout_ms = static_cast<std::int64_t>(snapshot.heartbeat_interval_ms) * 2;
    return snapshot.last_heartbeat_sent_at_ms > 0 && now - snapshot.last_heartbeat_sent_at_ms >= timeout_ms;
}

int WebSocketClient::ExtractCloseCodeFromException(const std::exception& ex) const {
    if (const auto* beast_error = dynamic_cast<const beast::system_error*>(&ex)) {
        const auto value = beast_error->code().value();
        if (value >= 1000 && value <= 4999) {
            return value;
        }
        if (beast_error->code() == websocket::error::closed) {
            const auto snapshot = SnapshotSessionState(impl_, session_state_);
            return snapshot.last_close_code == 0 ? 1000 : snapshot.last_close_code;
        }
    }

    const auto snapshot = SnapshotSessionState(impl_, session_state_);
    return snapshot.last_close_code == 0 ? 1006 : snapshot.last_close_code;
}

int WebSocketClient::GetReconnectDelayMs(int close_code) const {
    if (close_code == 4008) {
        return 10000;
    }

    const auto retry_delay = static_cast<int>(std::min<std::size_t>(retry_count_, 5) * 1000);
    const auto quick_disconnect_delay = impl_->quick_disconnect_count >= kMaxQuickDisconnectCount ? 15000 : 0;
    return std::max(1000, std::max(retry_delay, quick_disconnect_delay));
}

bool WebSocketClient::IsTerminalCloseCode(int close_code) const {
    return close_code == 4914 || close_code == 4915;
}

bool WebSocketClient::ShouldRefreshSession(int close_code) const {
    return close_code == 4004 || close_code == 4006 || close_code == 4007 || close_code == 4009 ||
           (close_code >= 4900 && close_code <= 4913);
}

}  // namespace websocket
}  // namespace qqbot
