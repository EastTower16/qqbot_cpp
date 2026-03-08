#include "sdk/bot_sdk.h"

#include "openapi/v1/openapi_v1.h"
#include "websocket/websocket_client.h"

namespace qqbot {
namespace sdk {
namespace {

void EnsureSetup() {
    static bool initialized = false;
    if (!initialized) {
        openapi::v1::SetupV1();
        initialized = true;
    }
}

}  // namespace

openapi::OpenAPIClientPtr CreateOpenAPI(const common::BotConfig& config, const transport::HttpTransportPtr& transport) {
    EnsureSetup();
    return openapi::CreateOpenAPI(config, transport);
}

websocket::WebSocketClientPtr CreateWebSocket(const common::BotConfig& config,
                                              const transport::HttpTransportPtr& transport) {
    EnsureSetup();
    return std::make_shared<websocket::WebSocketClient>(config, transport);
}

}  // namespace sdk
}  // namespace qqbot
