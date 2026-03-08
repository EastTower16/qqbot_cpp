#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#include "common/sdk_error.h"
#include "openapi/v1/openapi_v1.h"
#include "sdk/bot_sdk.h"

namespace {

std::string GetEnvOrEmpty(const char* name) {
#ifdef _WIN32
    int wide_name_size = MultiByteToWideChar(CP_UTF8, 0, name, -1, nullptr, 0);
    if (wide_name_size <= 0) {
        return {};
    }
    std::wstring wide_name(static_cast<std::size_t>(wide_name_size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wide_name.data(), wide_name_size);
    const wchar_t* wide_value = _wgetenv(wide_name.c_str());
    if (wide_value == nullptr) {
        return {};
    }
    int utf8_size = WideCharToMultiByte(CP_UTF8, 0, wide_value, -1, nullptr, 0, nullptr, nullptr);
    if (utf8_size <= 0) {
        return {};
    }
    std::string value(static_cast<std::size_t>(utf8_size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide_value, -1, value.data(), utf8_size, nullptr, nullptr);
    return value;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
#endif
}

bool ParseBoolEnv(const std::string& value) {
    return value == "1" || value == "true" || value == "TRUE" || value == "True";
}

void PrintResponse(const std::string& title, const qqbot::transport::HttpResponse& response) {
    std::cout << "==== " << title << " ====" << std::endl;
    std::cout << "status: " << response.status_code << std::endl;
    std::cout << response.body << std::endl;
    std::cout << std::endl;
}

}  // namespace

int main() {
    qqbot::common::BotConfig config;
    config.app_id = GetEnvOrEmpty("QQBOT_APP_ID");
    config.token = GetEnvOrEmpty("QQBOT_TOKEN");
    config.client_secret = GetEnvOrEmpty("QQBOT_CLIENT_SECRET");
    config.sandbox = ParseBoolEnv(GetEnvOrEmpty("QQBOT_SANDBOX"));
    config.skip_tls_verify = ParseBoolEnv(GetEnvOrEmpty("QQBOT_SKIP_TLS_VERIFY"));
    const auto dm_user_id = GetEnvOrEmpty("QQBOT_DM_USER_ID");
    const auto dm_guild_id = GetEnvOrEmpty("QQBOT_DM_GUILD_ID");

    if (config.app_id.empty() || (config.client_secret.empty() && config.token.empty())) {
        std::cerr << "Please set QQBOT_APP_ID and QQBOT_CLIENT_SECRET before running this example." << std::endl;
        return 1;
    }

    try {
        auto client = qqbot::sdk::CreateOpenAPI(config);
        auto v1 = std::dynamic_pointer_cast<qqbot::openapi::v1::OpenAPIV1Client>(client);
        if (!v1) {
            std::cerr << "Failed to create OpenAPIV1Client." << std::endl;
            return 1;
        }

        PrintResponse("Me", v1->Me());
        PrintResponse("Gateway Bot", v1->GetGatewayBot());
        PrintResponse("My Guilds", v1->MeGuilds({{"limit", "10"}}));

        if (!dm_user_id.empty()) {
            const auto dm = v1->CreateDirectMessage({{"recipient_id", dm_user_id}});
            PrintResponse("Create Direct Message", dm);
        }

        if (!dm_guild_id.empty()) {
            PrintResponse("Post Direct Message",
                          v1->PostDirectMessage(dm_guild_id,
                                                {{"content", "hello from qqbot_cpp example"}}));
        }
    } catch (const qqbot::common::SDKError& error) {
        std::cerr << "SDKError: " << error.what() << std::endl;
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << std::endl;
        return 1;
    }

    return 0;
}
