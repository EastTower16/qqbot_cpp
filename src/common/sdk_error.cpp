#include "common/sdk_error.h"

namespace qqbot {
namespace common {

SDKError::SDKError(ErrorCode code,
                   std::string message,
                   int http_status,
                   int gateway_close_code,
                   std::string trace_id,
                   std::string diagnostics)
    : std::runtime_error(std::move(message)),
      code_(code),
      http_status_(http_status),
      gateway_close_code_(gateway_close_code),
      trace_id_(std::move(trace_id)),
      diagnostics_(std::move(diagnostics)) {}

ErrorCode SDKError::code() const noexcept {
    return code_;
}

int SDKError::http_status() const noexcept {
    return http_status_;
}

int SDKError::gateway_close_code() const noexcept {
    return gateway_close_code_;
}

const std::string& SDKError::trace_id() const noexcept {
    return trace_id_;
}

const std::string& SDKError::diagnostics() const noexcept {
    return diagnostics_;
}

}  // namespace common
}  // namespace qqbot
