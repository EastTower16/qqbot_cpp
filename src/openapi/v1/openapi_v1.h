#pragma once

#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "message/message_models.h"
#include "openapi/openapi.h"

namespace qqbot {
namespace openapi {
namespace v1 {

class OpenAPIV1Client : public OpenAPIClient {
public:
    using Json = nlohmann::json;

    OpenAPIV1Client(const common::BotConfig& config, const transport::HttpTransportPtr& transport);

    transport::HttpResponse GetGateway() const;
    transport::HttpResponse GetGatewayBot() const;

    transport::HttpResponse Me() const;
    transport::HttpResponse MeGuilds(const Query& query = {}) const;

    transport::HttpResponse Guild(const std::string& guild_id) const;
    transport::HttpResponse GuildMember(const std::string& guild_id, const std::string& user_id) const;
    transport::HttpResponse GuildMembers(const std::string& guild_id, const Query& query = {}) const;
    transport::HttpResponse DeleteGuildMember(const std::string& guild_id, const std::string& user_id) const;
    transport::HttpResponse GuildVoiceMembers(const std::string& channel_id) const;

    transport::HttpResponse Channel(const std::string& channel_id) const;
    transport::HttpResponse Channels(const std::string& guild_id) const;
    transport::HttpResponse PostChannel(const std::string& guild_id, const Json& body) const;
    transport::HttpResponse PatchChannel(const std::string& channel_id, const Json& body) const;
    transport::HttpResponse DeleteChannel(const std::string& channel_id) const;

    transport::HttpResponse Message(const std::string& channel_id, const std::string& message_id) const;
    transport::HttpResponse Messages(const std::string& channel_id, const Query& query = {}) const;
    transport::HttpResponse PostMessage(const std::string& channel_id, const Json& body) const;
    transport::HttpResponse DeleteMessage(const std::string& channel_id, const std::string& message_id, const Query& query = {}) const;

    transport::HttpResponse Roles(const std::string& guild_id) const;
    transport::HttpResponse PostRole(const std::string& guild_id, const Json& body) const;
    transport::HttpResponse PatchRole(const std::string& guild_id, const std::string& role_id, const Json& body) const;
    transport::HttpResponse DeleteRole(const std::string& guild_id, const std::string& role_id) const;

    transport::HttpResponse MemberAddRole(const std::string& guild_id, const std::string& user_id, const std::string& role_id, const Json& body = Json::object()) const;
    transport::HttpResponse MemberDeleteRole(const std::string& guild_id, const std::string& user_id, const std::string& role_id, const Json& body = Json::object()) const;

    transport::HttpResponse CreateDirectMessage(const Json& body) const;
    transport::HttpResponse PostDirectMessage(const std::string& guild_id, const Json& body) const;
    transport::HttpResponse PostGroupMessage(const std::string& group_openid, const Json& body) const;
    transport::HttpResponse PostC2CMessage(const std::string& openid, const Json& body) const;
    transport::HttpResponse PostGroupFile(const std::string& group_openid, const Json& body) const;
    transport::HttpResponse PostC2CFile(const std::string& openid, const Json& body) const;

    transport::HttpResponse ChannelPermissions(const std::string& channel_id, const std::string& user_id) const;
    transport::HttpResponse PutChannelPermissions(const std::string& channel_id, const std::string& user_id, const Json& body) const;
    transport::HttpResponse ChannelRolePermissions(const std::string& channel_id, const std::string& role_id) const;
    transport::HttpResponse PutChannelRolePermissions(const std::string& channel_id, const std::string& role_id, const Json& body) const;

    transport::HttpResponse MuteMember(const std::string& guild_id, const std::string& user_id, const Json& body) const;
    transport::HttpResponse MuteAll(const std::string& guild_id, const Json& body) const;
    transport::HttpResponse MuteMembers(const std::string& guild_id, const Json& body) const;

    transport::HttpResponse PostGuildAnnounce(const std::string& guild_id, const Json& body) const;
    transport::HttpResponse DeleteGuildAnnounce(const std::string& guild_id, const std::string& message_id) const;
    transport::HttpResponse PostChannelAnnounce(const std::string& channel_id, const Json& body) const;
    transport::HttpResponse DeleteChannelAnnounce(const std::string& channel_id, const std::string& message_id) const;

    transport::HttpResponse Schedules(const std::string& channel_id, const Query& query = {}) const;
    transport::HttpResponse Schedule(const std::string& channel_id, const std::string& schedule_id) const;
    transport::HttpResponse PostSchedule(const std::string& channel_id, const Json& body) const;
    transport::HttpResponse PatchSchedule(const std::string& channel_id, const std::string& schedule_id, const Json& body) const;
    transport::HttpResponse DeleteSchedule(const std::string& channel_id, const std::string& schedule_id) const;

    transport::HttpResponse Permissions(const std::string& guild_id) const;
    transport::HttpResponse PostPermissionDemand(const std::string& guild_id, const Json& body) const;

    transport::HttpResponse PostReaction(const std::string& channel_id, const std::string& message_id, const std::string& emoji_type, const std::string& emoji_id) const;
    transport::HttpResponse DeleteReaction(const std::string& channel_id, const std::string& message_id, const std::string& emoji_type, const std::string& emoji_id) const;
    transport::HttpResponse GetReactionUserList(const std::string& channel_id, const std::string& message_id, const std::string& emoji_type, const std::string& emoji_id, const Query& query = {}) const;

    transport::HttpResponse PinsMessage(const std::string& channel_id) const;
    transport::HttpResponse PutPinsMessage(const std::string& channel_id, const std::string& message_id) const;
    transport::HttpResponse DeletePinsMessage(const std::string& channel_id, const std::string& message_id) const;

    transport::HttpResponse PutInteraction(const std::string& interaction_id, const Json& body) const;

    transport::HttpResponse PostAudio(const std::string& channel_id, const Json& body) const;
    transport::HttpResponse BotOnMic(const std::string& channel_id) const;
    transport::HttpResponse BotOffMic(const std::string& channel_id) const;

    transport::HttpResponse Reply(const message::ChannelMessage& message, const Json& body) const;
    transport::HttpResponse Reply(const message::DirectMessage& message, const Json& body) const;
    transport::HttpResponse Reply(const message::GroupMessage& message, const Json& body) const;
    transport::HttpResponse Reply(const message::C2CMessage& message, const Json& body) const;

private:
    transport::HttpResponse Request(const std::string& method,
                                    const std::string& resource_name,
                                    const std::map<std::string, std::string>& path_params = {},
                                    const Query& query = {},
                                    const Json& body = Json(),
                                    const Headers& headers = {}) const;
};

void SetupV1();

}  // namespace v1
}  // namespace openapi
}  // namespace qqbot
