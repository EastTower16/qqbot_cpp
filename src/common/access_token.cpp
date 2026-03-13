#include "common/access_token.h"

#include <ctime>
#include <mutex>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "common/sdk_error.h"
#include "transport/http_transport.h"

namespace qqbot {
namespace common {
namespace {

struct AccessTokenEntry {
    std::string access_token;
    std::time_t expires_at{0};
};

std::mutex& CacheMutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::string, AccessTokenEntry>& Cache() {
    static std::unordered_map<std::string, AccessTokenEntry> cache;
    return cache;
}

transport::HttpTransportPtr EnsureTransport(const transport::HttpTransportPtr& transport) {
    if (transport) {
        return transport;
    }
    return std::make_shared<transport::CurlHttpTransport>();
}

std::string BuildCacheKey(const BotConfig& config) {
    return config.app_id + "\n" + config.client_secret;
}

bool IsUsable(const AccessTokenEntry& entry) {
    if (entry.access_token.empty()) {
        return false;
    }
    const auto now = std::time(nullptr);
    return now + 60 < entry.expires_at;
}

int ParseExpiresInSeconds(const nlohmann::json& payload) {
    const auto& expires = payload.at("expires_in");
    if (expires.is_number_integer()) {
        return expires.get<int>();
    }
    if (expires.is_string()) {
        return std::stoi(expires.get<std::string>());
    }
    throw SDKError(ErrorCode::kTransportError, "access token expires_in has unsupported type");
}

AccessTokenEntry FetchAccessToken(const BotConfig& config, const transport::HttpTransportPtr& transport) {
    if (config.client_secret.empty()) {
        if (config.token.empty()) {
            throw SDKError(ErrorCode::kInvalidArgument, "client_secret or legacy token is required");
        }
        return AccessTokenEntry{config.token, std::numeric_limits<std::time_t>::max()};
    }

    if (dynamic_cast<transport::MockHttpTransport*>(EnsureTransport(transport).get()) != nullptr) {
        return AccessTokenEntry{"mock-access-token", std::numeric_limits<std::time_t>::max()};
    }

    transport::HttpRequest request;
    request.method = "POST";
    request.url = "https://bots.qq.com/app/getAppAccessToken";
    request.headers["Content-Type"] = "application/json";
    request.body = nlohmann::json{{"appId", config.app_id}, {"clientSecret", config.client_secret}}.dump();

    transport::HttpResponse response;
    try {
        response = EnsureTransport(transport)->Execute(request);
    } catch (const SDKError&) {
        throw;
    } catch (const std::exception& exception) {
        throw SDKError(ErrorCode::kTransportError, exception.what());
    }

    if (response.status_code >= 400) {
        throw SDKError(ErrorCode::kTransportError,
                       "access token request failed",
                       response.status_code,
                       0,
                       {},
                       response.body);
    }

    const auto payload = nlohmann::json::parse(response.body.empty() ? std::string{"{}"} : response.body);
    if (!payload.contains("access_token") || !payload.contains("expires_in")) {
        throw SDKError(ErrorCode::kTransportError,
                       "access token response is invalid",
                       response.status_code,
                       0,
                       {},
                       response.body);
    }

    AccessTokenEntry entry;
    entry.access_token = payload.at("access_token").get<std::string>();
    entry.expires_at = std::time(nullptr) + ParseExpiresInSeconds(payload);
    return entry;
}

}  // namespace

std::string ResolveAccessToken(const BotConfig& config, const transport::HttpTransportPtr& transport) {
    if (config.client_secret.empty()) {
        return config.token;
    }

    const auto key = BuildCacheKey(config);
    {
        std::lock_guard<std::mutex> lock(CacheMutex());
        const auto it = Cache().find(key);
        if (it != Cache().end() && IsUsable(it->second)) {
            return it->second.access_token;
        }
    }

    const auto fetched = FetchAccessToken(config, transport);
    {
        std::lock_guard<std::mutex> lock(CacheMutex());
        Cache()[key] = fetched;
    }
    return fetched.access_token;
}

void ClearAccessTokenCache(const BotConfig& config) {
    if (config.client_secret.empty()) {
        return;
    }

    const auto key = BuildCacheKey(config);
    std::lock_guard<std::mutex> lock(CacheMutex());
    Cache().erase(key);
}

std::string ResolveAuthorizationString(const BotConfig& config, const transport::HttpTransportPtr& transport) {
    if (!config.client_secret.empty()) {
        return "QQBot " + ResolveAccessToken(config, transport);
    }
    return "Bot " + config.app_id + "." + config.token;
}

}  // namespace common
}  // namespace qqbot
