#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "common/bot_config.h"
#include "transport/http_transport.h"

namespace qqbot {
namespace openapi {

class OpenAPIClient {
public:
    using Headers = std::map<std::string, std::string>;
    using Query = std::vector<std::pair<std::string, std::string>>;

    OpenAPIClient(common::BotConfig config, transport::HttpTransportPtr transport);
    virtual ~OpenAPIClient() = default;

    const common::BotConfig& config() const noexcept;
    std::string version() const noexcept;
    void set_version(std::string version);

    transport::HttpResponse Execute(std::string method,
                                    const std::string& path,
                                    std::string body = {}) const;
    transport::HttpResponse Execute(std::string method,
                                    const std::string& path,
                                    Headers headers,
                                    Query query,
                                    std::string body = {}) const;

private:
    common::BotConfig config_;
    std::string version_;
    transport::HttpTransportPtr transport_;
};

using OpenAPIClientPtr = std::shared_ptr<OpenAPIClient>;
using OpenAPIFactory = std::function<OpenAPIClientPtr(const common::BotConfig&, const transport::HttpTransportPtr&)>;

bool RegisterOpenAPIVersion(const std::string& version, OpenAPIFactory factory);
bool SelectOpenAPIVersion(const std::string& version);
OpenAPIClientPtr CreateOpenAPI(const common::BotConfig& config,
                               const transport::HttpTransportPtr& transport = nullptr);
std::string GetSelectedOpenAPIVersion();

}  // namespace openapi
}  // namespace qqbot
