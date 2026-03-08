#include "transport/http_transport.h"

#include <curl/curl.h>

#include <stdexcept>
#include <string>

namespace qqbot {
namespace transport {
namespace {

size_t WriteBodyCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    const auto total = size * nmemb;
    auto* body = static_cast<std::string*>(userdata);
    body->append(ptr, total);
    return total;
}

size_t WriteHeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
    const auto total = size * nitems;
    auto* headers = static_cast<std::map<std::string, std::string>*>(userdata);
    std::string line(buffer, total);

    const auto colon = line.find(':');
    if (colon != std::string::npos) {
        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
            value.erase(value.begin());
        }
        while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) {
            value.pop_back();
        }
        if (!key.empty()) {
            (*headers)[key] = value;
        }
    }

    return total;
}

void ApplyMethod(CURL* curl, const HttpRequest& request) {
    if (request.method == "GET") {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        return;
    }
    if (request.method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
        return;
    }

    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.c_str());
    if (!request.body.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
    }
}

}  // namespace

CurlHttpTransport::CurlHttpTransport() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

HttpResponse CurlHttpTransport::Execute(const HttpRequest& request) {
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        throw std::runtime_error("failed to initialize curl");
    }

    HttpResponse response;
    struct curl_slist* header_list = nullptr;

    try {
        curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteBodyCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, WriteHeaderCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "QQBotCppSDK/0.1.0");

        for (const auto& header : request.headers) {
            const auto line = header.first + ": " + header.second;
            header_list = curl_slist_append(header_list, line.c_str());
        }
        if (header_list != nullptr) {
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
        }

        ApplyMethod(curl, request);

        const auto result = curl_easy_perform(curl);
        if (result != CURLE_OK) {
            throw std::runtime_error(curl_easy_strerror(result));
        }

        long status_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
        response.status_code = static_cast<int>(status_code);
    } catch (...) {
        if (header_list != nullptr) {
            curl_slist_free_all(header_list);
        }
        curl_easy_cleanup(curl);
        throw;
    }

    if (header_list != nullptr) {
        curl_slist_free_all(header_list);
    }
    curl_easy_cleanup(curl);
    return response;
}

HttpResponse MockHttpTransport::Execute(const HttpRequest& request) {
    HttpResponse response;
    response.headers["x-mock-transport"] = "true";

    if (request.url.find("/app/getAppAccessToken") != std::string::npos) {
        response.body = R"({"access_token":"mock-access-token","expires_in":7200})";
        return response;
    }

    if (request.url.find("/gateway") != std::string::npos) {
        response.body = R"({"url":"wss://api.sgroup.qq.com/websocket"})";
        return response;
    }

    response.body = R"({"ok":true})";
    return response;
}

}  // namespace transport
}  // namespace qqbot
