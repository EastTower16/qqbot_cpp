#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "common/bot_config.h"
#include "transport/http_transport.h"

namespace qqbot {
namespace websocket {

enum class EventType {
    kReady,
    kDispatch,
    kReconnect,
    kDisconnect,
    kError
};

struct SessionState {
    std::string session_id;
    int sequence{0};
    int heartbeat_interval_ms{0};
    int last_heartbeat_sequence{0};
    bool alive{false};
    bool resumable{false};
    bool identified{false};
    std::string last_command;
};

struct GatewayEvent {
    EventType type{EventType::kDispatch};
    std::string event_name;
    std::string payload;
};

using EventHandler = std::function<void(const GatewayEvent&)>;

class WebSocketClient {
public:
    WebSocketClient(common::BotConfig config, transport::HttpTransportPtr transport = nullptr);
    ~WebSocketClient();

    WebSocketClient(const WebSocketClient&) = delete;
    WebSocketClient& operator=(const WebSocketClient&) = delete;

    void SetEventHandler(EventHandler handler);
    void Connect();
    void Disconnect(int close_code = 1000);
    void ReceiveHello(int heartbeat_interval_ms);
    void ReceiveReady(const std::string& session_id, int sequence);
    void ReceiveDispatch(const std::string& event_name, const std::string& payload, int sequence);
    void ReceiveReconnectSignal();
    void HandleClose(int close_code, bool resumable);

    const SessionState& session_state() const noexcept;
    std::size_t retry_count() const noexcept;
    std::string gateway_url() const noexcept;

private:
    struct Impl;

    void BootstrapGateway();
    void Emit(EventType type, const std::string& event_name, const std::string& payload);
    void TryReconnect();

    common::BotConfig config_;
    transport::HttpTransportPtr transport_;
    SessionState session_state_;
    std::size_t retry_count_{0};
    std::string gateway_url_;
    EventHandler event_handler_;
    std::unique_ptr<Impl> impl_;
};

using WebSocketClientPtr = std::shared_ptr<WebSocketClient>;

}  // namespace websocket
}  // namespace qqbot
