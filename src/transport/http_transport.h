#pragma once

#include <map>
#include <memory>
#include <string>

namespace qqbot {
namespace transport {

struct HttpRequest {
    std::string method;
    std::string url;
    std::map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponse {
    int status_code{200};
    std::map<std::string, std::string> headers;
    std::string body;
};

class HttpTransport {
public:
    virtual ~HttpTransport() = default;
    virtual HttpResponse Execute(const HttpRequest& request) = 0;
};

class CurlHttpTransport : public HttpTransport {
public:
    CurlHttpTransport();
    HttpResponse Execute(const HttpRequest& request) override;
};

class MockHttpTransport : public HttpTransport {
public:
    HttpResponse Execute(const HttpRequest& request) override;
};

using HttpTransportPtr = std::shared_ptr<HttpTransport>;

}  // namespace transport
}  // namespace qqbot
