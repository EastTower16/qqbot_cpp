#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

#include "common/bot_config.h"
#include "common/sdk_error.h"
#include "message/message_models.h"
#include "openapi/v1/openapi_v1.h"
#include "sdk/bot_sdk.h"
#include "websocket/websocket_client.h"

namespace {

std::string GetEnvOrEmpty(const char* name) {
#ifdef _WIN32
    int wide_name_size = MultiByteToWideChar(CP_UTF8, 0, name, -1, nullptr, 0);
    if (wide_name_size <= 0) {
        return {};
    }
    std::wstring wide_name(static_cast<std::size_t>(wide_name_size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wide_name.data(), wide_name_size);
    const wchar_t* wide_value = _wgetenv(wide_name.c_str());
    if (wide_value == nullptr) {
        return {};
    }
    int utf8_size = WideCharToMultiByte(CP_UTF8, 0, wide_value, -1, nullptr, 0, nullptr, nullptr);
    if (utf8_size <= 0) {
        return {};
    }
    std::string value(static_cast<std::size_t>(utf8_size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide_value, -1, value.data(), utf8_size, nullptr, nullptr);
    return value;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
#endif
}

bool ParseBoolEnv(const std::string& value) {
    return value == "1" || value == "true" || value == "TRUE" || value == "True";
}

int ParseIntEnv(const std::string& value, int fallback) {
    if (value.empty()) {
        return fallback;
    }
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

void PrintAttachments(const std::vector<qqbot::message::Attachment>& attachments) {
    if (attachments.empty()) {
        return;
    }
    for (const auto& attachment : attachments) {
        std::cout << "attachment filename=" << attachment.filename
                  << " content_type=" << attachment.content_type
                  << " url=" << attachment.url << std::endl;
    }
}

}  // namespace

int main() {
    qqbot::common::BotConfig config;
    config.app_id = GetEnvOrEmpty("QQBOT_APP_ID");
    config.token = GetEnvOrEmpty("QQBOT_TOKEN");
    config.client_secret = GetEnvOrEmpty("QQBOT_CLIENT_SECRET");
    config.sandbox = ParseBoolEnv(GetEnvOrEmpty("QQBOT_SANDBOX"));
    config.skip_tls_verify = ParseBoolEnv(GetEnvOrEmpty("QQBOT_SKIP_TLS_VERIFY"));
    config.intents = ParseIntEnv(GetEnvOrEmpty("QQBOT_INTENTS"), qqbot::common::intents::kMessageReplyExample);

    const auto trigger = GetEnvOrEmpty("QQBOT_REPLY_TRIGGER").empty()
                             ? std::string("/ping")
                             : GetEnvOrEmpty("QQBOT_REPLY_TRIGGER");
    const auto reply_text = GetEnvOrEmpty("QQBOT_REPLY_TEXT").empty()
                                ? std::string("pong")
                                : GetEnvOrEmpty("QQBOT_REPLY_TEXT");
    const auto media_url = GetEnvOrEmpty("QQBOT_MEDIA_URL");

    if (config.app_id.empty() || (config.client_secret.empty() && config.token.empty())) {
        std::cerr << "Please set QQBOT_APP_ID and QQBOT_CLIENT_SECRET before running this example." << std::endl;
        return 1;
    }

    try {
        auto openapi = qqbot::sdk::CreateOpenAPI(config);
        auto v1 = std::dynamic_pointer_cast<qqbot::openapi::v1::OpenAPIV1Client>(openapi);
        if (!v1) {
            std::cerr << "Failed to create OpenAPIV1Client." << std::endl;
            return 1;
        }

        auto websocket = qqbot::sdk::CreateWebSocket(config);
        auto running = std::make_shared<std::atomic<bool>>(true);

        websocket->SetEventHandler([v1, trigger, reply_text, media_url](const qqbot::websocket::GatewayEvent& event) {
            if (event.type == qqbot::websocket::EventType::kReady) {
                std::cout << "Gateway ready. session_id=" << event.payload << std::endl;
                return;
            }

            if (event.type != qqbot::websocket::EventType::kDispatch) {
                return;
            }

            try {
                if (qqbot::message::IsDirectMessageEvent(event)) {
                    const auto message = qqbot::message::ParseDirectMessage(event);
                    std::cout << "DIRECT_MESSAGE_CREATE guild=" << message.guild_id
                              << " channel=" << message.channel_id
                              << " content=" << message.content << std::endl;
                    PrintAttachments(message.attachments);
                    if (message.content != trigger) {
                        return;
                    }
                    const auto response = v1->Reply(message, {{"content", reply_text}});
                    std::cout << "Direct reply sent. status=" << response.status_code << std::endl;
                    return;
                }

                if (qqbot::message::IsC2CMessageEvent(event)) {
                    const auto message = qqbot::message::ParseC2CMessage(event);
                    std::cout << "C2C_MESSAGE_CREATE openid=" << message.author.user_openid
                              << " content=" << message.content << std::endl;
                    PrintAttachments(message.attachments);
                    if (message.content == trigger) {
                        const auto response = v1->Reply(message, {{"content", reply_text}, {"msg_type", 0}});
                        std::cout << "C2C reply sent. status=" << response.status_code << std::endl;
                    }
                    if (!media_url.empty()) {
                        const auto media = v1->PostC2CFile(message.author.user_openid,
                                                           {{"file_type", 1}, {"url", media_url}, {"srv_send_msg", false}});
                        std::cout << "C2C file uploaded. status=" << media.status_code << std::endl;
                        const auto media_payload = nlohmann::json::parse(media.body.empty() ? std::string{"{}"} : media.body);
                        const auto media_response = v1->Reply(message,
                                                              {{"msg_type", 7},
                                                               {"content", reply_text},
                                                               {"media", media_payload}});
                        std::cout << "C2C media sent. status=" << media_response.status_code << std::endl;
                    }
                    return;
                }

                if (qqbot::message::IsGroupMessageEvent(event)) {
                    const auto message = qqbot::message::ParseGroupMessage(event);
                    std::cout << message.event_name << " group_openid=" << message.group_openid
                              << " content=" << message.content << std::endl;
                    PrintAttachments(message.attachments);
                    if (message.content == trigger) {
                        const auto response = v1->Reply(message, {{"content", reply_text}, {"msg_type", 0}});
                        std::cout << "Group reply sent. status=" << response.status_code << std::endl;
                    }
                    if (!media_url.empty()) {
                        const auto media = v1->PostGroupFile(message.group_openid,
                                                             {{"file_type", 1}, {"url", media_url}, {"srv_send_msg", false}});
                        std::cout << "Group file uploaded. status=" << media.status_code << std::endl;
                        const auto media_payload = nlohmann::json::parse(media.body.empty() ? std::string{"{}"} : media.body);
                        const auto media_response = v1->Reply(message,
                                                              {{"msg_type", 7},
                                                               {"content", reply_text},
                                                               {"media", media_payload}});
                        std::cout << "Group media sent. status=" << media_response.status_code << std::endl;
                    }
                    return;
                }

                if (qqbot::message::IsChannelMessageEvent(event)) {
                    const auto message = qqbot::message::ParseChannelMessage(event);
                    if (message.author_is_bot || message.channel_id.empty()) {
                        return;
                    }
                    std::cout << message.event_name << " channel=" << message.channel_id
                              << " guild=" << message.guild_id
                              << " content=" << message.content << std::endl;
                    PrintAttachments(message.attachments);
                    if (message.content == trigger) {
                        const auto response = v1->Reply(message, {{"content", reply_text}});
                        std::cout << "Channel reply sent. status=" << response.status_code << std::endl;
                    }
                }
            } catch (const std::exception& error) {
                std::cerr << "Failed to process gateway event: " << error.what() << std::endl;
            }
        });

        std::cout << "Starting bot..." << std::endl;
        std::cout << "trigger: " << trigger << std::endl;
        std::cout << "reply: " << reply_text << std::endl;
        std::cout << "intents: " << config.intents << std::endl;
        if (!media_url.empty()) {
            std::cout << "media_url: " << media_url << std::endl;
        }

        websocket->Connect();

        std::cout << "Connected. Press Ctrl+C to stop." << std::endl;
        while (running->load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    } catch (const qqbot::common::SDKError& error) {
        std::cerr << "SDKError: " << error.what() << std::endl;
        if (!error.diagnostics().empty()) {
            std::cerr << error.diagnostics() << std::endl;
        }
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << std::endl;
        return 1;
    }

    return 0;
}
