#pragma once

#include <map>
#include <string>

namespace qqbot {
namespace openapi {
namespace v1 {

std::string GetResource(const std::string& name);
std::string BuildResourcePath(const std::string& name, const std::map<std::string, std::string>& params);

}  // namespace v1
}  // namespace openapi
}  // namespace qqbot
