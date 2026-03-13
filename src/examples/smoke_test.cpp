#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "message/message_models.h"
#include "openapi/v1/openapi_v1.h"
#include "sdk/bot_sdk.h"
#include "common/bot_config.h"
#include "transport/http_transport.h"
#include "websocket/websocket_client.h"

namespace {

std::filesystem::path GetSmokeSessionPath(const qqbot::common::BotConfig& config) {
    if (const auto* local_app_data = std::getenv("LOCALAPPDATA"); local_app_data != nullptr && *local_app_data != '\0') {
        return std::filesystem::path(local_app_data) / "qqbot_cpp" / "sessions" / ("session-" + config.app_id + ".json");
    }
    return std::filesystem::temp_directory_path() / "qqbot_cpp" / "sessions" / ("session-" + config.app_id + ".json");
}

void ClearSmokeSession(const qqbot::common::BotConfig& config) {
    std::error_code ec;
    std::filesystem::remove(GetSmokeSessionPath(config), ec);
}

}

int main() {
    qqbot::common::BotConfig config;
    config.app_id = "app_id";
    config.client_secret = "client_secret";
    config.sandbox = true;
    config.max_retry = 2;

    ClearSmokeSession(config);

    auto transport = std::make_shared<qqbot::transport::MockHttpTransport>();

    auto openapi = qqbot::sdk::CreateOpenAPI(config, transport);
    auto openapi_v1 = std::dynamic_pointer_cast<qqbot::openapi::v1::OpenAPIV1Client>(openapi);
    assert(openapi_v1 != nullptr);

    const auto me_response = openapi->Execute("GET", "/users/@me");
    const auto me_v1_response = openapi_v1->Me();
    const auto guilds_response = openapi_v1->MeGuilds({{"before", "1"}, {"limit", "10"}});
    const auto channel_message_response = openapi_v1->PostMessage("channel-1", nlohmann::json{{"content", "hello"}});
    const auto pins_response = openapi_v1->PinsMessage("channel-1");
    const auto c2c_response = openapi_v1->PostC2CMessage("openid-1", nlohmann::json{{"content", "hello"}, {"msg_type", 0}, {"msg_id", "mid-1"}});
    const auto group_response = openapi_v1->PostGroupMessage("group-openid-1", nlohmann::json{{"content", "hello"}, {"msg_type", 0}, {"msg_id", "mid-2"}});
    const auto c2c_file_response = openapi_v1->PostC2CFile("openid-1", nlohmann::json{{"file_type", 1}, {"url", "https://example.com/a.png"}, {"srv_send_msg", false}});
    const auto group_file_response = openapi_v1->PostGroupFile("group-openid-1", nlohmann::json{{"file_type", 1}, {"url", "https://example.com/b.png"}, {"srv_send_msg", false}});

    qqbot::websocket::GatewayEvent c2c_event{qqbot::websocket::EventType::kDispatch, "C2C_MESSAGE_CREATE", R"({"id":"m1","content":"hello","author":{"user_openid":"openid-1"},"attachments":[]})"};
    qqbot::websocket::GatewayEvent group_event{qqbot::websocket::EventType::kDispatch, "GROUP_AT_MESSAGE_CREATE", R"({"id":"m2","content":"hello","group_openid":"group-openid-1","author":{"member_openid":"member-1"},"attachments":[]})"};
    qqbot::websocket::GatewayEvent direct_event{qqbot::websocket::EventType::kDispatch, "DIRECT_MESSAGE_CREATE", R"({"id":"m3","content":"hello","guild_id":"dm-guild-1","channel_id":"dm-channel-1","author":{"id":"u1"},"attachments":[]})"};

    const auto parsed_c2c = qqbot::message::ParseC2CMessage(c2c_event);
    const auto parsed_group = qqbot::message::ParseGroupMessage(group_event);
    const auto parsed_direct = qqbot::message::ParseDirectMessage(direct_event);

    const auto c2c_reply_response = openapi_v1->Reply(parsed_c2c, nlohmann::json{{"content", "reply"}, {"msg_type", 0}});
    const auto group_reply_response = openapi_v1->Reply(parsed_group, nlohmann::json{{"content", "reply"}, {"msg_type", 0}});
    const auto direct_reply_response = openapi_v1->Reply(parsed_direct, nlohmann::json{{"content", "reply"}});

    assert(me_response.status_code == 200);
    assert(me_v1_response.status_code == 200);
    assert(guilds_response.status_code == 200);
    assert(channel_message_response.status_code == 200);
    assert(pins_response.status_code == 200);
    assert(c2c_response.status_code == 200);
    assert(group_response.status_code == 200);
    assert(c2c_file_response.status_code == 200);
    assert(group_file_response.status_code == 200);
    assert(c2c_reply_response.status_code == 200);
    assert(group_reply_response.status_code == 200);
    assert(direct_reply_response.status_code == 200);
    assert(openapi->config().ResolveApiBaseUrl() == "https://sandbox.api.sgroup.qq.com");

    auto websocket = qqbot::sdk::CreateWebSocket(config, transport);
    std::vector<qqbot::websocket::EventType> events;
    websocket->SetEventHandler([&events](const qqbot::websocket::GatewayEvent& event) {
        events.push_back(event.type);
    });

    websocket->Connect();
    assert(!websocket->gateway_url().empty());
    assert(websocket->connection_phase() == qqbot::websocket::ConnectionPhase::kIdentifying);
    websocket->ReceiveHello(30000);
    websocket->ReceiveReady("session-1", 1);
    assert(websocket->connection_phase() == qqbot::websocket::ConnectionPhase::kReady);
    websocket->ReceiveDispatch("MESSAGE_CREATE", R"({"content":"hello"})", 2);
    websocket->HandleClose(4000, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    assert(websocket->session_state().heartbeat_interval_ms == 30000);
    assert(websocket->session_state().session_id == "session-1");
    assert(websocket->session_state().sequence == 2);
    assert(websocket->retry_count() == 1);
    assert(events.size() >= 3);
    assert(websocket->connection_phase() == qqbot::websocket::ConnectionPhase::kIdentifying);
    ClearSmokeSession(config);

    qqbot::common::BotConfig matrix_config = config;
    matrix_config.app_id = "app_id_matrix";

    auto make_client = [&]() {
        ClearSmokeSession(matrix_config);
        auto client = qqbot::sdk::CreateWebSocket(matrix_config, transport);
        client->Connect();
        assert(client->connection_phase() == qqbot::websocket::ConnectionPhase::kIdentifying);
        client->ReceiveHello(30000);
        client->ReceiveReady("matrix-session", 10);
        assert(client->connection_phase() == qqbot::websocket::ConnectionPhase::kReady);
        client->ReceiveDispatch("MESSAGE_CREATE", R"({"content":"matrix"})", 11);
        return client;
    };

    {
        auto client = make_client();
        client->HandleClose(4004, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        const auto state = client->session_state();
        assert(client->retry_count() == 1);
        assert(state.session_id.empty());
        assert(state.sequence == 0);
        assert(state.last_command == "IDENTIFY");
        assert(client->connection_phase() == qqbot::websocket::ConnectionPhase::kIdentifying);
        ClearSmokeSession(matrix_config);
    }

    {
        auto client = make_client();
        client->HandleClose(4007, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        const auto state = client->session_state();
        assert(client->retry_count() == 1);
        assert(state.session_id.empty());
        assert(state.sequence == 0);
        assert(state.last_command == "IDENTIFY");
        assert(client->connection_phase() == qqbot::websocket::ConnectionPhase::kIdentifying);
        ClearSmokeSession(matrix_config);
    }

    {
        auto client = make_client();
        client->HandleClose(4009, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        const auto state = client->session_state();
        assert(client->retry_count() == 1);
        assert(state.session_id.empty());
        assert(state.sequence == 0);
        assert(state.last_command == "IDENTIFY");
        assert(client->connection_phase() == qqbot::websocket::ConnectionPhase::kIdentifying);
        ClearSmokeSession(matrix_config);
    }

    {
        auto client = make_client();
        client->HandleClose(4008, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const auto state = client->session_state();
        assert(client->retry_count() == 1);
        assert(state.session_id == "matrix-session");
        assert(state.sequence == 11);
        assert(state.last_command == "RESUME");
        assert(client->connection_phase() == qqbot::websocket::ConnectionPhase::kReconnecting);
        client->Disconnect();
        assert(client->connection_phase() == qqbot::websocket::ConnectionPhase::kDisconnected);
        ClearSmokeSession(matrix_config);
    }

    {
        auto client = make_client();
        client->HandleClose(4914, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const auto state = client->session_state();
        assert(client->retry_count() == 0);
        assert(state.last_command == "TERMINAL");
        assert(client->connection_phase() == qqbot::websocket::ConnectionPhase::kTerminal);
        client->Disconnect();
        assert(client->connection_phase() == qqbot::websocket::ConnectionPhase::kDisconnected);
        ClearSmokeSession(matrix_config);
    }

    {
        auto client = qqbot::sdk::CreateWebSocket(matrix_config, transport);
        assert(client->connection_phase() == qqbot::websocket::ConnectionPhase::kDisconnected);

        client->ReceiveDispatch("MESSAGE_CREATE", R"({"content":"ignored"})", 99);
        assert(client->session_state().sequence == 0);
        assert(client->session_state().last_command.empty());
        assert(client->connection_phase() == qqbot::websocket::ConnectionPhase::kDisconnected);

        client->ReceiveReconnectSignal();
        assert(client->session_state().last_command.empty());
        assert(client->connection_phase() == qqbot::websocket::ConnectionPhase::kDisconnected);

        client->ReceiveReady("ignored-session", 123);
        assert(client->connection_phase() == qqbot::websocket::ConnectionPhase::kDisconnected);
        assert(client->session_state().session_id.empty());
        assert(client->session_state().sequence == 0);

        client->Disconnect();
        assert(client->connection_phase() == qqbot::websocket::ConnectionPhase::kDisconnected);
        ClearSmokeSession(matrix_config);
    }

    std::cout << "qqbot_sdk_smoke passed" << std::endl;
    return 0;
}
