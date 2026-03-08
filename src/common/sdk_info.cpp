#include "common/sdk_info.h"

namespace qqbot {
namespace common {

std::string GetSDKVersion() {
    return "0.1.0";
}

std::string GetUserAgent() {
    return "QQBotCppSDK/" + GetSDKVersion();
}

}  // namespace common
}  // namespace qqbot
