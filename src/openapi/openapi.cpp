#include "openapi/openapi.h"

#include <sstream>
#include <utility>

#include "common/access_token.h"
#include "common/sdk_error.h"
#include "common/sdk_info.h"

namespace qqbot {
namespace openapi {
namespace {

std::map<std::string, OpenAPIFactory>& Registry() {
    static std::map<std::string, OpenAPIFactory> registry;
    return registry;
}

std::string& SelectedVersion() {
    static std::string version = "v1";
    return version;
}

transport::HttpTransportPtr EnsureTransport(const transport::HttpTransportPtr& transport) {
    if (transport) {
        return transport;
    }
    return std::make_shared<transport::CurlHttpTransport>();
}

}  // namespace

OpenAPIClient::OpenAPIClient(common::BotConfig config, transport::HttpTransportPtr transport)
    : config_(std::move(config)), transport_(EnsureTransport(transport)) {}

const common::BotConfig& OpenAPIClient::config() const noexcept {
    return config_;
}

std::string OpenAPIClient::version() const noexcept {
    return version_;
}

void OpenAPIClient::set_version(std::string version) {
    version_ = std::move(version);
}

transport::HttpResponse OpenAPIClient::Execute(std::string method,
                                               const std::string& path,
                                               std::string body) const {
    return Execute(std::move(method), path, {}, {}, std::move(body));
}

transport::HttpResponse OpenAPIClient::Execute(std::string method,
                                               const std::string& path,
                                               Headers headers,
                                               Query query,
                                               std::string body) const {
    if (!config_.IsValid()) {
        throw common::SDKError(common::ErrorCode::kInvalidArgument, "bot config is invalid");
    }

    std::string final_path = path;
    if (!query.empty()) {
        std::ostringstream stream;
        stream << final_path;
        stream << (final_path.find('?') == std::string::npos ? '?' : '&');
        for (std::size_t index = 0; index < query.size(); ++index) {
            if (index > 0) {
                stream << '&';
            }
            stream << query[index].first << '=' << query[index].second;
        }
        final_path = stream.str();
    }

    transport::HttpRequest request;
    request.method = std::move(method);
    request.url = config_.ResolveApiBaseUrl() + final_path;
    request.body = std::move(body);
    request.headers = std::move(headers);
    request.headers["Authorization"] = common::ResolveAuthorizationString(config_, transport_);
    request.headers["User-Agent"] = common::GetUserAgent();
    if (!request.body.empty() && request.headers.find("Content-Type") == request.headers.end()) {
        request.headers["Content-Type"] = "application/json";
    }

    transport::HttpResponse response;
    try {
        response = transport_->Execute(request);
    } catch (const common::SDKError&) {
        throw;
    } catch (const std::exception& exception) {
        throw common::SDKError(common::ErrorCode::kTransportError, exception.what());
    }

    if (response.status_code >= 400) {
        std::string trace_id;
        const auto trace_it = response.headers.find("x-tps-trace-id");
        if (trace_it != response.headers.end()) {
            trace_id = trace_it->second;
        }
        throw common::SDKError(common::ErrorCode::kTransportError,
                               "http request failed",
                               response.status_code,
                               0,
                               trace_id,
                               response.body);
    }
    return response;
}

bool RegisterOpenAPIVersion(const std::string& version, OpenAPIFactory factory) {
    return Registry().emplace(version, std::move(factory)).second;
}

bool SelectOpenAPIVersion(const std::string& version) {
    if (Registry().find(version) == Registry().end()) {
        return false;
    }
    SelectedVersion() = version;
    return true;
}

OpenAPIClientPtr CreateOpenAPI(const common::BotConfig& config, const transport::HttpTransportPtr& transport) {
    const auto selected = SelectedVersion();
    const auto it = Registry().find(selected);
    if (it == Registry().end()) {
        throw common::SDKError(common::ErrorCode::kUnknownApiVersion, "unknown api version: " + selected);
    }
    return it->second(config, EnsureTransport(transport));
}

std::string GetSelectedOpenAPIVersion() {
    return SelectedVersion();
}

}  // namespace openapi
}  // namespace qqbot
