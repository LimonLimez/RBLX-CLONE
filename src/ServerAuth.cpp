#include "ServerAuth.h"

#include <windows.h>
#include <winhttp.h>

#include <cstdlib>
#include <sstream>

#pragma comment(lib, "winhttp.lib")

namespace {
    struct WinHttpHandle {
        HINTERNET handle = nullptr;

        explicit WinHttpHandle(HINTERNET value = nullptr) : handle(value) {}
        ~WinHttpHandle() {
            if (handle) {
                WinHttpCloseHandle(handle);
            }
        }

        WinHttpHandle(const WinHttpHandle&) = delete;
        WinHttpHandle& operator=(const WinHttpHandle&) = delete;

        operator HINTERNET() const {
            return handle;
        }
    };

    struct ParsedUrl {
        std::wstring host;
        INTERNET_PORT port = INTERNET_DEFAULT_HTTP_PORT;
        bool secure = false;
    };

    bool parseServerUrl(const std::string& serverUrl, ParsedUrl& out) {
        std::wstring wurl(serverUrl.begin(), serverUrl.end());
        URL_COMPONENTS urlComp;
        ZeroMemory(&urlComp, sizeof(urlComp));
        urlComp.dwStructSize = sizeof(urlComp);
        urlComp.dwSchemeLength = static_cast<DWORD>(-1);
        urlComp.dwHostNameLength = static_cast<DWORD>(-1);
        urlComp.dwUrlPathLength = static_cast<DWORD>(-1);
        urlComp.dwExtraInfoLength = static_cast<DWORD>(-1);

        if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &urlComp)) {
            return false;
        }

        out.host.assign(urlComp.lpszHostName, urlComp.dwHostNameLength);
        out.secure = urlComp.nScheme == INTERNET_SCHEME_HTTPS;
        out.port = urlComp.nPort ? urlComp.nPort : (out.secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT);
        return !out.host.empty();
    }

    std::string httpRequest(
        const std::string& method,
        const std::string& serverUrl,
        const std::wstring& path,
        const std::string& body,
        const std::string& bearerToken
    ) {
        ParsedUrl parsed;
        if (!parseServerUrl(serverUrl, parsed)) {
            return "";
        }

        WinHttpHandle session(WinHttpOpen(
            L"RBLX Server/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0
        ));
        if (!session.handle) return "";

        WinHttpSetTimeouts(session, 3000, 3000, 5000, 5000);

        WinHttpHandle connect(WinHttpConnect(session, parsed.host.c_str(), parsed.port, 0));
        if (!connect.handle) return "";

        const std::wstring wmethod(method.begin(), method.end());
        WinHttpHandle request(WinHttpOpenRequest(
            connect,
            wmethod.c_str(),
            path.c_str(),
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            parsed.secure ? WINHTTP_FLAG_SECURE : 0
        ));
        if (!request.handle) return "";

        std::wstring headers = L"Content-Type: application/json\r\n";
        if (!bearerToken.empty()) {
            std::wstring token(bearerToken.begin(), bearerToken.end());
            headers += L"Authorization: Bearer " + token + L"\r\n";
        }
        WinHttpAddRequestHeaders(request, headers.c_str(), static_cast<DWORD>(headers.length()), WINHTTP_ADDREQ_FLAG_ADD);

        void* requestData = body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data());
        DWORD requestSize = static_cast<DWORD>(body.size());
        if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, requestData, requestSize, requestSize, 0)) {
            return "";
        }

        if (!WinHttpReceiveResponse(request, nullptr)) {
            return "";
        }

        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &statusCode,
            &statusCodeSize,
            WINHTTP_NO_HEADER_INDEX
        );
        if (statusCode < 200 || statusCode >= 300) {
            return "";
        }

        std::string response;
        while (true) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available) || available == 0) {
                break;
            }

            std::string chunk(available, '\0');
            DWORD downloaded = 0;
            if (!WinHttpReadData(request, chunk.data(), available, &downloaded)) {
                break;
            }
            chunk.resize(downloaded);
            response += chunk;
        }

        return response;
    }

    bool jsonBoolTrue(const std::string& json, const std::string& field) {
        const std::string needle = "\"" + field + "\":true";
        return json.find(needle) != std::string::npos;
    }

    int jsonInt(const std::string& json, const std::string& field) {
        const std::string needle = "\"" + field + "\":";
        size_t pos = json.find(needle);
        if (pos == std::string::npos) return -1;
        pos += needle.size();
        return std::atoi(json.c_str() + pos);
    }

    std::string jsonString(const std::string& json, const std::string& field) {
        const std::string needle = "\"" + field + "\":\"";
        size_t pos = json.find(needle);
        if (pos == std::string::npos) return "";
        pos += needle.size();
        size_t end = json.find('"', pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    }
}

VerifiedUser ServerAuth::verifyToken(const std::string& token, const std::string& serverUrl) {
    VerifiedUser user;
    if (token.empty()) return user;

    const std::string response = httpRequest("POST", serverUrl, L"/api/verify", "{}", token);
    if (response.empty() || !jsonBoolTrue(response, "success")) {
        return user;
    }

    user.userId = jsonInt(response, "userId");
    user.username = jsonString(response, "username");
    user.success = user.userId > 0 && !user.username.empty() && user.username.size() < 32;
    return user;
}

std::string ServerAuth::getAvatar(const std::string& token, const std::string& serverUrl) {
    if (token.empty()) return "";
    return httpRequest("GET", serverUrl, L"/api/avatar", "", token);
}

bool ServerAuth::recordPlaytime(const std::string& token, int seconds, const std::string& serverUrl) {
    if (token.empty() || seconds <= 0) return false;

    std::ostringstream body;
    body << "{\"seconds\":" << seconds << "}";

    const std::string response = httpRequest("POST", serverUrl, L"/api/me/playtime", body.str(), token);
    return !response.empty() && jsonBoolTrue(response, "success");
}
