#include "common/url.h"

#include <stdexcept>

namespace qqbot {
namespace common {

bool ParsedUrl::IsSecure() const {
    return scheme == "https" || scheme == "wss";
}

ParsedUrl ParseUrl(const std::string& url) {
    const auto scheme_pos = url.find("://");
    if (scheme_pos == std::string::npos) {
        throw std::invalid_argument("url missing scheme");
    }

    ParsedUrl parsed;
    parsed.scheme = url.substr(0, scheme_pos);

    const auto authority_start = scheme_pos + 3;
    const auto path_start = url.find('/', authority_start);
    std::string authority = path_start == std::string::npos ? url.substr(authority_start) : url.substr(authority_start, path_start - authority_start);
    parsed.target = path_start == std::string::npos ? "/" : url.substr(path_start);

    const auto colon_pos = authority.find(':');
    if (colon_pos == std::string::npos) {
        parsed.host = authority;
        parsed.port = parsed.IsSecure() ? (parsed.scheme == "wss" ? "443" : "443") : "80";
    } else {
        parsed.host = authority.substr(0, colon_pos);
        parsed.port = authority.substr(colon_pos + 1);
    }

    if (parsed.host.empty()) {
        throw std::invalid_argument("url missing host");
    }

    return parsed;
}

}  // namespace common
}  // namespace qqbot
