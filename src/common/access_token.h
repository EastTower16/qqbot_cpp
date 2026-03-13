#pragma once

#include <string>

#include "common/bot_config.h"
#include "transport/http_transport.h"

namespace qqbot {
namespace common {

std::string ResolveAuthorizationString(const BotConfig& config, const transport::HttpTransportPtr& transport);
std::string ResolveAccessToken(const BotConfig& config, const transport::HttpTransportPtr& transport);
void ClearAccessTokenCache(const BotConfig& config);

}  // namespace common
}  // namespace qqbot
