#include "common/bot_config.h"

namespace qqbot {
namespace common {

BotConfig::BotConfig()
    : api_base_url("https://api.sgroup.qq.com"),
      sandbox_api_base_url("https://sandbox.api.sgroup.qq.com"),
      gateway_url("/gateway") {}

bool BotConfig::IsValid() const {
    return !app_id.empty() && (!client_secret.empty() || !token.empty());
}

std::string BotConfig::ResolveApiBaseUrl() const {
    return sandbox ? sandbox_api_base_url : api_base_url;
}

}  // namespace common
}  // namespace qqbot
