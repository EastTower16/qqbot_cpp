#pragma once

#include <string>

#include "common/bot_config.h"
#include "transport/http_transport.h"

namespace qqbot {
namespace common {

std::string ResolveAuthorizationString(const BotConfig& config, const transport::HttpTransportPtr& transport);
std::string ResolveAccessToken(const BotConfig& config, const transport::HttpTransportPtr& transport);

}  // namespace common
}  // namespace qqbot
