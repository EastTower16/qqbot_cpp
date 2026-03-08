#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "websocket/websocket_client.h"

namespace qqbot {
namespace message {

inline constexpr const char kMessageCreateEvent[] = "MESSAGE_CREATE";
inline constexpr const char kMessageDeleteEvent[] = "MESSAGE_DELETE";
inline constexpr const char kAtMessageCreateEvent[] = "AT_MESSAGE_CREATE";
inline constexpr const char kPublicMessageDeleteEvent[] = "PUBLIC_MESSAGE_DELETE";
inline constexpr const char kDirectMessageCreateEvent[] = "DIRECT_MESSAGE_CREATE";
inline constexpr const char kDirectMessageDeleteEvent[] = "DIRECT_MESSAGE_DELETE";
inline constexpr const char kC2CMessageCreateEvent[] = "C2C_MESSAGE_CREATE";
inline constexpr const char kC2CMessageRejectEvent[] = "C2C_MSG_REJECT";
inline constexpr const char kC2CMessageReceiveEvent[] = "C2C_MSG_RECEIVE";
inline constexpr const char kFriendAddEvent[] = "FRIEND_ADD";
inline constexpr const char kFriendDeleteEvent[] = "FRIEND_DEL";
inline constexpr const char kGroupAtMessageCreateEvent[] = "GROUP_AT_MESSAGE_CREATE";
inline constexpr const char kGroupAddRobotEvent[] = "GROUP_ADD_ROBOT";
inline constexpr const char kGroupDeleteRobotEvent[] = "GROUP_DEL_ROBOT";
inline constexpr const char kGroupMessageRejectEvent[] = "GROUP_MSG_REJECT";
inline constexpr const char kGroupMessageReceiveEvent[] = "GROUP_MSG_RECEIVE";

struct Attachment {
    std::string content_type;
    std::string filename;
    int height{0};
    int width{0};
    std::string id;
    int size{0};
    std::string url;
};

struct MessageReference {
    std::string message_id;
};

struct ChannelAuthor {
    std::string id;
    std::string username;
    bool bot{false};
    std::string avatar;
};

struct DirectAuthor {
    std::string id;
    std::string username;
    std::string avatar;
};

struct C2CAuthor {
    std::string user_openid;
};

struct GroupAuthor {
    std::string member_openid;
};

struct MemberInfo {
    std::string nick;
    std::vector<std::string> roles;
    std::string joined_at;
};

struct BaseMessage {
    std::string id;
    std::string content;
    std::string event_name;
    std::string event_id;
    std::string timestamp;
    std::string raw_payload;
    std::vector<Attachment> attachments;
    MessageReference message_reference;
};

struct ChannelMessage : BaseMessage {
    std::string channel_id;
    std::string guild_id;
    ChannelAuthor author;
    MemberInfo member;
    bool author_is_bot{false};
};

struct DirectMessage : BaseMessage {
    std::string channel_id;
    std::string guild_id;
    std::string src_guild_id;
    bool direct_message{false};
    DirectAuthor author;
};

struct C2CMessage : BaseMessage {
    C2CAuthor author;
};

struct GroupMessage : BaseMessage {
    GroupAuthor author;
    std::string group_openid;
};

inline Attachment ParseAttachment(const nlohmann::json& payload) {
    Attachment attachment;
    attachment.content_type = payload.value("content_type", std::string{});
    attachment.filename = payload.value("filename", std::string{});
    attachment.height = payload.value("height", 0);
    attachment.width = payload.value("width", 0);
    attachment.id = payload.value("id", std::string{});
    attachment.size = payload.value("size", 0);
    attachment.url = payload.value("url", std::string{});
    return attachment;
}

inline MessageReference ParseMessageReference(const nlohmann::json& payload) {
    MessageReference reference;
    if (payload.is_object()) {
        reference.message_id = payload.value("message_id", std::string{});
    }
    return reference;
}

inline std::vector<Attachment> ParseAttachments(const nlohmann::json& payload) {
    std::vector<Attachment> attachments;
    if (!payload.is_array()) {
        return attachments;
    }
    for (const auto& item : payload) {
        attachments.push_back(ParseAttachment(item));
    }
    return attachments;
}

inline ChannelMessage ParseChannelMessage(const websocket::GatewayEvent& event) {
    const auto payload = nlohmann::json::parse(event.payload);
    ChannelMessage message;
    message.event_name = event.event_name;
    message.raw_payload = event.payload;
    message.id = payload.value("id", std::string{});
    message.content = payload.value("content", std::string{});
    message.channel_id = payload.value("channel_id", std::string{});
    message.guild_id = payload.value("guild_id", std::string{});
    message.timestamp = payload.value("timestamp", std::string{});
    message.attachments = ParseAttachments(payload.value("attachments", nlohmann::json::array()));
    message.message_reference = ParseMessageReference(payload.value("message_reference", nlohmann::json::object()));
    if (payload.contains("author") && payload.at("author").is_object()) {
        const auto& author = payload.at("author");
        message.author.id = author.value("id", std::string{});
        message.author.username = author.value("username", std::string{});
        message.author.bot = author.value("bot", false);
        message.author.avatar = author.value("avatar", std::string{});
        message.author_is_bot = message.author.bot;
    }
    if (payload.contains("member") && payload.at("member").is_object()) {
        const auto& member = payload.at("member");
        message.member.nick = member.value("nick", std::string{});
        message.member.joined_at = member.value("joined_at", std::string{});
        if (member.contains("roles") && member.at("roles").is_array()) {
            for (const auto& role : member.at("roles")) {
                message.member.roles.push_back(role.get<std::string>());
            }
        }
    }
    return message;
}

inline DirectMessage ParseDirectMessage(const websocket::GatewayEvent& event) {
    const auto payload = nlohmann::json::parse(event.payload);
    DirectMessage message;
    message.event_name = event.event_name;
    message.raw_payload = event.payload;
    message.id = payload.value("id", std::string{});
    message.content = payload.value("content", std::string{});
    message.channel_id = payload.value("channel_id", std::string{});
    message.guild_id = payload.value("guild_id", std::string{});
    message.src_guild_id = payload.value("src_guild_id", std::string{});
    message.direct_message = payload.value("direct_message", false);
    message.timestamp = payload.value("timestamp", std::string{});
    message.attachments = ParseAttachments(payload.value("attachments", nlohmann::json::array()));
    message.message_reference = ParseMessageReference(payload.value("message_reference", nlohmann::json::object()));
    if (payload.contains("author") && payload.at("author").is_object()) {
        const auto& author = payload.at("author");
        message.author.id = author.value("id", std::string{});
        message.author.username = author.value("username", std::string{});
        message.author.avatar = author.value("avatar", std::string{});
    }
    return message;
}

inline C2CMessage ParseC2CMessage(const websocket::GatewayEvent& event) {
    const auto payload = nlohmann::json::parse(event.payload);
    C2CMessage message;
    message.event_name = event.event_name;
    message.raw_payload = event.payload;
    message.id = payload.value("id", std::string{});
    message.content = payload.value("content", std::string{});
    message.timestamp = payload.value("timestamp", std::string{});
    message.attachments = ParseAttachments(payload.value("attachments", nlohmann::json::array()));
    message.message_reference = ParseMessageReference(payload.value("message_reference", nlohmann::json::object()));
    if (payload.contains("author") && payload.at("author").is_object()) {
        message.author.user_openid = payload.at("author").value("user_openid", std::string{});
    }
    return message;
}

inline GroupMessage ParseGroupMessage(const websocket::GatewayEvent& event) {
    const auto payload = nlohmann::json::parse(event.payload);
    GroupMessage message;
    message.event_name = event.event_name;
    message.raw_payload = event.payload;
    message.id = payload.value("id", std::string{});
    message.content = payload.value("content", std::string{});
    message.group_openid = payload.value("group_openid", std::string{});
    message.timestamp = payload.value("timestamp", std::string{});
    message.attachments = ParseAttachments(payload.value("attachments", nlohmann::json::array()));
    message.message_reference = ParseMessageReference(payload.value("message_reference", nlohmann::json::object()));
    if (payload.contains("author") && payload.at("author").is_object()) {
        message.author.member_openid = payload.at("author").value("member_openid", std::string{});
    }
    return message;
}

inline bool IsChannelMessageEvent(const websocket::GatewayEvent& event) {
    return event.type == websocket::EventType::kDispatch
           && (event.event_name == kMessageCreateEvent || event.event_name == kAtMessageCreateEvent);
}

inline bool IsDirectMessageEvent(const websocket::GatewayEvent& event) {
    return event.type == websocket::EventType::kDispatch && event.event_name == kDirectMessageCreateEvent;
}

inline bool IsC2CMessageEvent(const websocket::GatewayEvent& event) {
    return event.type == websocket::EventType::kDispatch && event.event_name == kC2CMessageCreateEvent;
}

inline bool IsGroupMessageEvent(const websocket::GatewayEvent& event) {
    return event.type == websocket::EventType::kDispatch
           && (event.event_name == kGroupAtMessageCreateEvent || event.event_name == kGroupMessageReceiveEvent);
}

}  // namespace message
}  // namespace qqbot
