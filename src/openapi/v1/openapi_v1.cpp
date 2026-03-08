#include "openapi/v1/openapi_v1.h"
#include "openapi/v1/resource.h"

namespace qqbot {
namespace openapi {
namespace v1 {

OpenAPIV1Client::OpenAPIV1Client(const common::BotConfig& config, const transport::HttpTransportPtr& transport)
    : OpenAPIClient(config, transport) {
    set_version("v1");
}

transport::HttpResponse OpenAPIV1Client::Request(const std::string& method,
                                                 const std::string& resource_name,
                                                 const std::map<std::string, std::string>& path_params,
                                                 const Query& query,
                                                 const Json& body,
                                                 const Headers& headers) const {
    const auto path = BuildResourcePath(resource_name, path_params);
    return Execute(method, path, headers, query, body.is_null() ? std::string{} : body.dump());
}

transport::HttpResponse OpenAPIV1Client::GetGateway() const {
    return Request("GET", "gatewayURI");
}

transport::HttpResponse OpenAPIV1Client::GetGatewayBot() const { return Request("GET", "gatewayBotURI"); }
transport::HttpResponse OpenAPIV1Client::Me() const { return Request("GET", "userMeURI"); }
transport::HttpResponse OpenAPIV1Client::MeGuilds(const Query& query) const { return Request("GET", "userMeGuildsURI", {}, query); }
transport::HttpResponse OpenAPIV1Client::Guild(const std::string& guild_id) const { return Request("GET", "guildURI", {{"guildID", guild_id}}); }
transport::HttpResponse OpenAPIV1Client::GuildMember(const std::string& guild_id, const std::string& user_id) const { return Request("GET", "guildMemberURI", {{"guildID", guild_id}, {"userID", user_id}}); }
transport::HttpResponse OpenAPIV1Client::GuildMembers(const std::string& guild_id, const Query& query) const { return Request("GET", "guildMembersURI", {{"guildID", guild_id}}, query); }
transport::HttpResponse OpenAPIV1Client::DeleteGuildMember(const std::string& guild_id, const std::string& user_id) const { return Request("DELETE", "guildMemberURI", {{"guildID", guild_id}, {"userID", user_id}}); }
transport::HttpResponse OpenAPIV1Client::GuildVoiceMembers(const std::string& channel_id) const { return Request("GET", "guildVoiceMembersURI", {{"channelID", channel_id}}); }
transport::HttpResponse OpenAPIV1Client::Channel(const std::string& channel_id) const { return Request("GET", "channelURI", {{"channelID", channel_id}}); }
transport::HttpResponse OpenAPIV1Client::Channels(const std::string& guild_id) const { return Request("GET", "channelsURI", {{"guildID", guild_id}}); }
transport::HttpResponse OpenAPIV1Client::PostChannel(const std::string& guild_id, const Json& body) const { return Request("POST", "channelsURI", {{"guildID", guild_id}}, {}, body); }
transport::HttpResponse OpenAPIV1Client::PatchChannel(const std::string& channel_id, const Json& body) const { return Request("PATCH", "channelURI", {{"channelID", channel_id}}, {}, body); }
transport::HttpResponse OpenAPIV1Client::DeleteChannel(const std::string& channel_id) const { return Request("DELETE", "channelURI", {{"channelID", channel_id}}); }
transport::HttpResponse OpenAPIV1Client::Message(const std::string& channel_id, const std::string& message_id) const { return Request("GET", "messageURI", {{"channelID", channel_id}, {"messageID", message_id}}); }
transport::HttpResponse OpenAPIV1Client::Messages(const std::string& channel_id, const Query& query) const { return Request("GET", "messagesURI", {{"channelID", channel_id}}, query); }
transport::HttpResponse OpenAPIV1Client::PostMessage(const std::string& channel_id, const Json& body) const { return Request("POST", "messagesURI", {{"channelID", channel_id}}, {}, body); }
transport::HttpResponse OpenAPIV1Client::DeleteMessage(const std::string& channel_id, const std::string& message_id, const Query& query) const { return Request("DELETE", "messageURI", {{"channelID", channel_id}, {"messageID", message_id}}, query); }
transport::HttpResponse OpenAPIV1Client::Roles(const std::string& guild_id) const { return Request("GET", "rolesURI", {{"guildID", guild_id}}); }
transport::HttpResponse OpenAPIV1Client::PostRole(const std::string& guild_id, const Json& body) const { return Request("POST", "rolesURI", {{"guildID", guild_id}}, {}, body); }
transport::HttpResponse OpenAPIV1Client::PatchRole(const std::string& guild_id, const std::string& role_id, const Json& body) const { return Request("PATCH", "roleURI", {{"guildID", guild_id}, {"roleID", role_id}}, {}, body); }
transport::HttpResponse OpenAPIV1Client::DeleteRole(const std::string& guild_id, const std::string& role_id) const { return Request("DELETE", "roleURI", {{"guildID", guild_id}, {"roleID", role_id}}); }
transport::HttpResponse OpenAPIV1Client::MemberAddRole(const std::string& guild_id, const std::string& user_id, const std::string& role_id, const Json& body) const { return Request("PUT", "memberRoleURI", {{"guildID", guild_id}, {"userID", user_id}, {"roleID", role_id}}, {}, body); }
transport::HttpResponse OpenAPIV1Client::MemberDeleteRole(const std::string& guild_id, const std::string& user_id, const std::string& role_id, const Json& body) const { return Request("DELETE", "memberRoleURI", {{"guildID", guild_id}, {"userID", user_id}, {"roleID", role_id}}, {}, body); }
transport::HttpResponse OpenAPIV1Client::CreateDirectMessage(const Json& body) const { return Request("POST", "userMeDMURI", {}, {}, body); }
transport::HttpResponse OpenAPIV1Client::PostDirectMessage(const std::string& guild_id, const Json& body) const { return Request("POST", "dmsURI", {{"guildID", guild_id}}, {}, body); }
transport::HttpResponse OpenAPIV1Client::PostGroupMessage(const std::string& group_openid, const Json& body) const { return Request("POST", "groupMessagesURI", {{"groupOpenID", group_openid}}, {}, body); }
transport::HttpResponse OpenAPIV1Client::PostC2CMessage(const std::string& openid, const Json& body) const { return Request("POST", "c2cMessagesURI", {{"openid", openid}}, {}, body); }
transport::HttpResponse OpenAPIV1Client::PostGroupFile(const std::string& group_openid, const Json& body) const { return Request("POST", "groupFilesURI", {{"groupOpenID", group_openid}}, {}, body); }
transport::HttpResponse OpenAPIV1Client::PostC2CFile(const std::string& openid, const Json& body) const { return Request("POST", "c2cFilesURI", {{"openid", openid}}, {}, body); }
transport::HttpResponse OpenAPIV1Client::ChannelPermissions(const std::string& channel_id, const std::string& user_id) const { return Request("GET", "channelPermissionsURI", {{"channelID", channel_id}, {"userID", user_id}}); }
transport::HttpResponse OpenAPIV1Client::PutChannelPermissions(const std::string& channel_id, const std::string& user_id, const Json& body) const { return Request("PUT", "channelPermissionsURI", {{"channelID", channel_id}, {"userID", user_id}}, {}, body); }
transport::HttpResponse OpenAPIV1Client::ChannelRolePermissions(const std::string& channel_id, const std::string& role_id) const { return Request("GET", "channelRolePermissionsURI", {{"channelID", channel_id}, {"roleID", role_id}}); }
transport::HttpResponse OpenAPIV1Client::PutChannelRolePermissions(const std::string& channel_id, const std::string& role_id, const Json& body) const { return Request("PUT", "channelRolePermissionsURI", {{"channelID", channel_id}, {"roleID", role_id}}, {}, body); }
transport::HttpResponse OpenAPIV1Client::MuteMember(const std::string& guild_id, const std::string& user_id, const Json& body) const { return Request("PATCH", "muteMemberURI", {{"guildID", guild_id}, {"userID", user_id}}, {}, body); }
transport::HttpResponse OpenAPIV1Client::MuteAll(const std::string& guild_id, const Json& body) const { return Request("PATCH", "muteURI", {{"guildID", guild_id}}, {}, body); }
transport::HttpResponse OpenAPIV1Client::MuteMembers(const std::string& guild_id, const Json& body) const { return Request("PATCH", "muteMembersURI", {{"guildID", guild_id}}, {}, body); }
transport::HttpResponse OpenAPIV1Client::PostGuildAnnounce(const std::string& guild_id, const Json& body) const { return Request("POST", "guildAnnouncesURI", {{"guildID", guild_id}}, {}, body); }
transport::HttpResponse OpenAPIV1Client::DeleteGuildAnnounce(const std::string& guild_id, const std::string& message_id) const { return Request("DELETE", "guildAnnounceURI", {{"guildID", guild_id}, {"messageID", message_id}}); }
transport::HttpResponse OpenAPIV1Client::PostChannelAnnounce(const std::string& channel_id, const Json& body) const { return Request("POST", "channelAnnouncesURI", {{"channelID", channel_id}}, {}, body); }
transport::HttpResponse OpenAPIV1Client::DeleteChannelAnnounce(const std::string& channel_id, const std::string& message_id) const { return Request("DELETE", "channelAnnounceURI", {{"channelID", channel_id}, {"messageID", message_id}}); }
transport::HttpResponse OpenAPIV1Client::Schedules(const std::string& channel_id, const Query& query) const { return Request("GET", "schedulesURI", {{"channelID", channel_id}}, query); }
transport::HttpResponse OpenAPIV1Client::Schedule(const std::string& channel_id, const std::string& schedule_id) const { return Request("GET", "scheduleURI", {{"channelID", channel_id}, {"scheduleID", schedule_id}}); }
transport::HttpResponse OpenAPIV1Client::PostSchedule(const std::string& channel_id, const Json& body) const { return Request("POST", "schedulesURI", {{"channelID", channel_id}}, {}, body); }
transport::HttpResponse OpenAPIV1Client::PatchSchedule(const std::string& channel_id, const std::string& schedule_id, const Json& body) const { return Request("PATCH", "scheduleURI", {{"channelID", channel_id}, {"scheduleID", schedule_id}}, {}, body); }
transport::HttpResponse OpenAPIV1Client::DeleteSchedule(const std::string& channel_id, const std::string& schedule_id) const { return Request("DELETE", "scheduleURI", {{"channelID", channel_id}, {"scheduleID", schedule_id}}); }
transport::HttpResponse OpenAPIV1Client::Permissions(const std::string& guild_id) const { return Request("GET", "guildPermissionURI", {{"guildID", guild_id}}); }
transport::HttpResponse OpenAPIV1Client::PostPermissionDemand(const std::string& guild_id, const Json& body) const { return Request("POST", "guildPermissionDemandURI", {{"guildID", guild_id}}, {}, body); }
transport::HttpResponse OpenAPIV1Client::PostReaction(const std::string& channel_id, const std::string& message_id, const std::string& emoji_type, const std::string& emoji_id) const { return Request("PUT", "reactionURI", {{"channelID", channel_id}, {"messageID", message_id}, {"emojiType", emoji_type}, {"emojiID", emoji_id}}); }
transport::HttpResponse OpenAPIV1Client::DeleteReaction(const std::string& channel_id, const std::string& message_id, const std::string& emoji_type, const std::string& emoji_id) const { return Request("DELETE", "reactionURI", {{"channelID", channel_id}, {"messageID", message_id}, {"emojiType", emoji_type}, {"emojiID", emoji_id}}); }
transport::HttpResponse OpenAPIV1Client::GetReactionUserList(const std::string& channel_id, const std::string& message_id, const std::string& emoji_type, const std::string& emoji_id, const Query& query) const { return Request("GET", "reactionURI", {{"channelID", channel_id}, {"messageID", message_id}, {"emojiType", emoji_type}, {"emojiID", emoji_id}}, query); }
transport::HttpResponse OpenAPIV1Client::PinsMessage(const std::string& channel_id) const { return Request("GET", "pinsMessageURI", {{"channelID", channel_id}}); }
transport::HttpResponse OpenAPIV1Client::PutPinsMessage(const std::string& channel_id, const std::string& message_id) const { return Request("PUT", "pinsMessageIdURI", {{"channelID", channel_id}, {"messageID", message_id}}, {}, Json(), {{"Content-Type", "application/json"}}); }
transport::HttpResponse OpenAPIV1Client::DeletePinsMessage(const std::string& channel_id, const std::string& message_id) const { return Request("DELETE", "pinsMessageIdURI", {{"channelID", channel_id}, {"messageID", message_id}}); }
transport::HttpResponse OpenAPIV1Client::PutInteraction(const std::string& interaction_id, const Json& body) const { return Request("PUT", "interactionURI", {{"interactionID", interaction_id}}, {}, body, {{"Content-Type", "none"}}); }
transport::HttpResponse OpenAPIV1Client::PostAudio(const std::string& channel_id, const Json& body) const { return Request("POST", "audioControlURI", {{"channelID", channel_id}}, {}, body); }
transport::HttpResponse OpenAPIV1Client::BotOnMic(const std::string& channel_id) const { return Request("PUT", "botMic", {{"channelID", channel_id}}, {}, Json::object()); }
transport::HttpResponse OpenAPIV1Client::BotOffMic(const std::string& channel_id) const { return Request("DELETE", "botMic", {{"channelID", channel_id}}, {}, Json::object()); }

transport::HttpResponse OpenAPIV1Client::Reply(const message::ChannelMessage& message, const Json& body) const {
    Json payload = body;
    payload["msg_id"] = message.id;
    return PostMessage(message.channel_id, payload);
}

transport::HttpResponse OpenAPIV1Client::Reply(const message::DirectMessage& message, const Json& body) const {
    Json payload = body;
    payload["msg_id"] = message.id;
    return PostDirectMessage(message.guild_id, payload);
}

transport::HttpResponse OpenAPIV1Client::Reply(const message::GroupMessage& message, const Json& body) const {
    Json payload = body;
    payload["msg_id"] = message.id;
    if (!payload.contains("msg_type")) {
        payload["msg_type"] = 0;
    }
    if (!payload.contains("msg_seq")) {
        payload["msg_seq"] = 1;
    }
    return PostGroupMessage(message.group_openid, payload);
}

transport::HttpResponse OpenAPIV1Client::Reply(const message::C2CMessage& message, const Json& body) const {
    Json payload = body;
    payload["msg_id"] = message.id;
    if (!payload.contains("msg_type")) {
        payload["msg_type"] = 0;
    }
    if (!payload.contains("msg_seq")) {
        payload["msg_seq"] = 1;
    }
    return PostC2CMessage(message.author.user_openid, payload);
}

void SetupV1() {
    RegisterOpenAPIVersion("v1", [](const common::BotConfig& config, const transport::HttpTransportPtr& transport) {
        return std::make_shared<OpenAPIV1Client>(config, transport);
    });
}

}  // namespace v1
}  // namespace openapi
}  // namespace qqbot
