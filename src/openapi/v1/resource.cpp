#include "openapi/v1/resource.h"

#include <stdexcept>

namespace qqbot {
namespace openapi {
namespace v1 {
namespace {

const std::map<std::string, std::string> kApiMap = {
    {"guildURI", "/guilds/:guildID"},
    {"guildMembersURI", "/guilds/:guildID/members"},
    {"guildMemberURI", "/guilds/:guildID/members/:userID"},
    {"channelsURI", "/guilds/:guildID/channels"},
    {"channelURI", "/channels/:channelID"},
    {"guildAnnouncesURI", "/guilds/:guildID/announces"},
    {"guildAnnounceURI", "/guilds/:guildID/announces/:messageID"},
    {"channelAnnouncesURI", "/channels/:channelID/announces"},
    {"channelAnnounceURI", "/channels/:channelID/announces/:messageID"},
    {"messagesURI", "/channels/:channelID/messages"},
    {"messageURI", "/channels/:channelID/messages/:messageID"},
    {"userMeURI", "/users/@me"},
    {"userMeGuildsURI", "/users/@me/guilds"},
    {"muteURI", "/guilds/:guildID/mute"},
    {"muteMemberURI", "/guilds/:guildID/members/:userID/mute"},
    {"muteMembersURI", "/guilds/:guildID/mute"},
    {"gatewayURI", "/gateway"},
    {"gatewayBotURI", "/gateway/bot"},
    {"audioControlURI", "/channels/:channelID/audio"},
    {"rolesURI", "/guilds/:guildID/roles"},
    {"roleURI", "/guilds/:guildID/roles/:roleID"},
    {"memberRoleURI", "/guilds/:guildID/members/:userID/roles/:roleID"},
    {"userMeDMURI", "/users/@me/dms"},
    {"dmsURI", "/dms/:guildID/messages"},
    {"channelPermissionsURI", "/channels/:channelID/members/:userID/permissions"},
    {"channelRolePermissionsURI", "/channels/:channelID/roles/:roleID/permissions"},
    {"schedulesURI", "/channels/:channelID/schedules"},
    {"scheduleURI", "/channels/:channelID/schedules/:scheduleID"},
    {"guildPermissionURI", "/guilds/:guildID/api_permission"},
    {"guildPermissionDemandURI", "/guilds/:guildID/api_permission/demand"},
    {"reactionURI", "/channels/:channelID/messages/:messageID/reactions/:emojiType/:emojiID"},
    {"pinsMessageIdURI", "/channels/:channelID/pins/:messageID"},
    {"pinsMessageURI", "/channels/:channelID/pins"},
    {"interactionURI", "/interactions/:interactionID"},
    {"guildVoiceMembersURI", "/channels/:channelID/voice/members"},
    {"botMic", "/channels/:channelID/mic"},
    {"groupMessagesURI", "/v2/groups/:groupOpenID/messages"},
    {"c2cMessagesURI", "/v2/users/:openid/messages"},
    {"groupFilesURI", "/v2/groups/:groupOpenID/files"},
    {"c2cFilesURI", "/v2/users/:openid/files"}
};

}  // namespace

std::string GetResource(const std::string& name) {
    const auto it = kApiMap.find(name);
    if (it == kApiMap.end()) {
        throw std::invalid_argument("unknown resource: " + name);
    }
    return it->second;
}

std::string BuildResourcePath(const std::string& name, const std::map<std::string, std::string>& params) {
    std::string path = GetResource(name);
    for (const auto& param : params) {
        const auto needle = ":" + param.first;
        const auto pos = path.find(needle);
        if (pos != std::string::npos) {
            path.replace(pos, needle.size(), param.second);
        }
    }
    return path;
}

}  // namespace v1
}  // namespace openapi
}  // namespace qqbot
