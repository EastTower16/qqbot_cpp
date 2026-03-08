#pragma once

#include <stdexcept>
#include <string>

namespace qqbot {
namespace common {

enum class ErrorCode {
    kInvalidArgument,
    kUnknownApiVersion,
    kTransportError,
    kGatewayBootstrapFailed,
    kGatewayDisconnected,
    kRetryExhausted,
    kProtocolError
};

class SDKError : public std::runtime_error {
public:
    SDKError(ErrorCode code,
             std::string message,
             int http_status = 0,
             int gateway_close_code = 0,
             std::string trace_id = {},
             std::string diagnostics = {});

    ErrorCode code() const noexcept;
    int http_status() const noexcept;
    int gateway_close_code() const noexcept;
    const std::string& trace_id() const noexcept;
    const std::string& diagnostics() const noexcept;

private:
    ErrorCode code_;
    int http_status_;
    int gateway_close_code_;
    std::string trace_id_;
    std::string diagnostics_;
};

}  // namespace common
}  // namespace qqbot
