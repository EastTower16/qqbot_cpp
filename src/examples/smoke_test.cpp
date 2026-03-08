#include <cassert>
#include <iostream>
#include <memory>
#include <vector>

#include "message/message_models.h"
#include "openapi/v1/openapi_v1.h"
#include "sdk/bot_sdk.h"
#include "transport/http_transport.h"
#include "websocket/websocket_client.h"

int main() {
    qqbot::common::BotConfig config;
    config.app_id = "app_id";
    config.client_secret = "client_secret";
    config.sandbox = true;
    config.max_retry = 2;

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
    websocket->ReceiveHello(30000);
    websocket->ReceiveReady("session-1", 1);
    websocket->ReceiveDispatch("MESSAGE_CREATE", R"({"content":"hello"})", 2);
    websocket->HandleClose(4000, true);

    assert(websocket->session_state().heartbeat_interval_ms == 30000);
    assert(websocket->session_state().session_id == "session-1");
    assert(websocket->session_state().sequence == 2);
    assert(websocket->retry_count() == 1);
    assert(events.size() >= 3);

    std::cout << "qqbot_sdk_smoke passed" << std::endl;
    return 0;
}
