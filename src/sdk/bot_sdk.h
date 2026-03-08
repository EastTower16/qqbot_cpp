#pragma once

#include <memory>

#include "common/bot_config.h"
#include "openapi/openapi.h"

namespace qqbot {
namespace websocket {
class WebSocketClient;
using WebSocketClientPtr = std::shared_ptr<WebSocketClient>;
}

namespace sdk {

openapi::OpenAPIClientPtr CreateOpenAPI(const common::BotConfig& config,
                                        const transport::HttpTransportPtr& transport = nullptr);
websocket::WebSocketClientPtr CreateWebSocket(const common::BotConfig& config,
                                              const transport::HttpTransportPtr& transport = nullptr);

}  // namespace sdk
}  // namespace qqbot
