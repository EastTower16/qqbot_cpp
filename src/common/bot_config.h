#pragma once

#include <cstddef>
#include <initializer_list>
#include <string>

namespace qqbot {
namespace common {

namespace intents {

inline constexpr int kGuilds = 1 << 0;
inline constexpr int kGuildMembers = 1 << 1;
inline constexpr int kOpenForumEvent = 1 << 18;
inline constexpr int kAudioOrLiveChannelMember = 1 << 19;
inline constexpr int kPublicMessages = 1 << 25;
inline constexpr int kInteraction = 1 << 26;
inline constexpr int kMessageAudit = 1 << 27;
inline constexpr int kForumsEvent = 1 << 28;
inline constexpr int kAudioAction = 1 << 29;
inline constexpr int kPublicGuildMessages = 1 << 30;
inline constexpr int kGuildMessages = 1 << 9;
inline constexpr int kGuildMessageReactions = 1 << 10;
inline constexpr int kDirectMessage = 1 << 12;

inline constexpr int kNone = 0;
inline constexpr int kDefault = kGuilds | kGuildMembers | kGuildMessageReactions | kDirectMessage | kInteraction | kMessageAudit
                                | kAudioAction | kPublicGuildMessages | kPublicMessages | kAudioOrLiveChannelMember | kOpenForumEvent;
inline constexpr int kAll = kDefault | kGuildMessages | kForumsEvent;
inline constexpr int kMessageReplyExample = kDirectMessage | kPublicMessages | kPublicGuildMessages;

inline constexpr int Combine(const std::initializer_list<int> values) {
    int result = 0;
    for (const int value : values) {
        result |= value;
    }
    return result;
}

inline constexpr bool Has(int value, int flag) {
    return (value & flag) == flag;
}

}  // namespace intents

struct BotConfig {
    std::string app_id;
    std::string token;
    std::string client_secret;
    bool sandbox{false};
    bool skip_tls_verify{false};
    std::size_t max_retry{10};
    int intents{0};
    int shard_id{0};
    int shard_count{1};
    std::string api_base_url;
    std::string sandbox_api_base_url;
    std::string gateway_url;

    BotConfig();

    bool IsValid() const;
    std::string ResolveApiBaseUrl() const;
};

}  // namespace common
}  // namespace qqbot
