#pragma once

#include <string>

namespace qqbot {
namespace common {

struct ParsedUrl {
    std::string scheme;
    std::string host;
    std::string port;
    std::string target;

    bool IsSecure() const;
};

ParsedUrl ParseUrl(const std::string& url);

}  // namespace common
}  // namespace qqbot
