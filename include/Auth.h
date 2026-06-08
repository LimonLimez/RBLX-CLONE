#pragma once

#include <string>
#include <windows.h>
#include <winhttp.h>
#include <sstream>
#pragma comment(lib, "winhttp.lib")

class Auth {
public:
    static std::string jsonEscape(const std::string& value) {
        std::ostringstream escaped;
        for (char c : value) {
            unsigned char uc = static_cast<unsigned char>(c);
            switch (c) {
                case '\\': escaped << "\\\\"; break;
                case '"': escaped << "\\\""; break;
                case '\b': escaped << "\\b"; break;
                case '\f': escaped << "\\f"; break;
                case '\n': escaped << "\\n"; break;
                case '\r': escaped << "\\r"; break;
                case '\t': escaped << "\\t"; break;
                default:
                    if (uc < 0x20) {
                        escaped << "\\u00";
                        const char* hex = "0123456789abcdef";
                        escaped << hex[(uc >> 4) & 0x0f] << hex[uc & 0x0f];
                    } else {
                        escaped << c;
                    }
            }
        }
        return escaped.str();
    }

    static std::string httpPost(const std::string& url, const std::string& jsonData, const std::string& bearerToken = "") {
        std::string result;
        
        // Convert URL to wide string
        int urlLen = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, NULL, 0);
        wchar_t* wurl = new wchar_t[urlLen];
        MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wurl, urlLen);
        
        // Parse URL
        URL_COMPONENTS urlComp;
        ZeroMemory(&urlComp, sizeof(urlComp));
        urlComp.dwStructSize = sizeof(urlComp);
        urlComp.dwSchemeLength = (DWORD)-1;
        urlComp.dwHostNameLength = (DWORD)-1;
        urlComp.dwUrlPathLength = (DWORD)-1;
        urlComp.dwExtraInfoLength = (DWORD)-1;
        
        if (!WinHttpCrackUrl(wurl, 0, 0, &urlComp)) {
            delete[] wurl;
            return "";
        }
        
        std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
        std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
        if (urlComp.lpszExtraInfo) {
            path += std::wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
        }
        
        delete[] wurl;
        
        // Initialize WinHTTP
        HINTERNET hSession = WinHttpOpen(L"RBLX Client/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, 
                                         WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return "";
        
        HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), 
                                            urlComp.nPort ? urlComp.nPort : INTERNET_DEFAULT_HTTP_PORT, 0);
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            return "";
        }
        
        // Create request
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(), NULL, 
                                               WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 
                                               urlComp.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return "";
        }
        
        // Set headers
        std::wstring headers = L"Content-Type: application/json\r\n";
        if (!bearerToken.empty()) {
            std::wstring wtoken(bearerToken.begin(), bearerToken.end());
            headers += L"Authorization: Bearer " + wtoken + L"\r\n";
        }
        WinHttpAddRequestHeaders(hRequest, headers.c_str(), (DWORD)headers.length(), WINHTTP_ADDREQ_FLAG_ADD);
        
        // Send request
        std::string data = jsonData;
        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, 
                               (LPVOID)data.c_str(), (DWORD)data.length(), (DWORD)data.length(), 0)) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return "";
        }
        
        // Receive response
        if (!WinHttpReceiveResponse(hRequest, NULL)) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return "";
        }
        
        // Read response
        DWORD dwSize = 0;
        DWORD dwDownloaded = 0;
        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
            
            if (dwSize == 0) break;
            
            char* pszOutBuffer = new char[dwSize + 1];
            ZeroMemory(pszOutBuffer, dwSize + 1);
            
            if (WinHttpReadData(hRequest, (LPVOID)pszOutBuffer, dwSize, &dwDownloaded)) {
                result += std::string(pszOutBuffer, dwDownloaded);
            }
            
            delete[] pszOutBuffer;
        } while (dwSize > 0);
        
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        
        return result;
    }
    
    static std::string login(const std::string& username, const std::string& password, const std::string& serverUrl = "http://localhost:3000") {
        std::ostringstream json;
        json << "{\"username\":\"" << jsonEscape(username) << "\",\"password\":\"" << jsonEscape(password) << "\"}";
        return httpPost(serverUrl + "/api/login", json.str());
    }
    
    static std::string verifyToken(const std::string& token, const std::string& serverUrl = "http://localhost:3000") {
        return httpPost(serverUrl + "/api/verify", "{}", token);
    }
    
    static void openBrowser(const std::string& url) {
        std::wstring wurl(url.begin(), url.end());
        ShellExecuteW(NULL, L"open", wurl.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
    
    static std::string httpGet(const std::string& url, const std::string& bearerToken = "") {
        std::string result;
        
        // Convert URL to wide string
        int urlLen = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, NULL, 0);
        wchar_t* wurl = new wchar_t[urlLen];
        MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wurl, urlLen);
        
        // Parse URL
        URL_COMPONENTS urlComp;
        ZeroMemory(&urlComp, sizeof(urlComp));
        urlComp.dwStructSize = sizeof(urlComp);
        urlComp.dwSchemeLength = (DWORD)-1;
        urlComp.dwHostNameLength = (DWORD)-1;
        urlComp.dwUrlPathLength = (DWORD)-1;
        urlComp.dwExtraInfoLength = (DWORD)-1;
        
        if (!WinHttpCrackUrl(wurl, 0, 0, &urlComp)) {
            delete[] wurl;
            return "";
        }
        
        std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
        std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
        if (urlComp.lpszExtraInfo) {
            path += std::wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
        }
        
        delete[] wurl;
        
        // Initialize WinHTTP
        HINTERNET hSession = WinHttpOpen(L"RBLX Client/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, 
                                         WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return "";
        
        HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), 
                                            urlComp.nPort ? urlComp.nPort : INTERNET_DEFAULT_HTTP_PORT, 0);
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            return "";
        }
        
        // Create GET request
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL, 
                                               WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 
                                               urlComp.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return "";
        }
        
        if (!bearerToken.empty()) {
            std::wstring wtoken(bearerToken.begin(), bearerToken.end());
            std::wstring headers = L"Authorization: Bearer " + wtoken + L"\r\n";
            WinHttpAddRequestHeaders(hRequest, headers.c_str(), (DWORD)headers.length(), WINHTTP_ADDREQ_FLAG_ADD);
        }

        // Send request
        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, 
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return "";
        }
        
        // Receive response
        if (!WinHttpReceiveResponse(hRequest, NULL)) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return "";
        }
        
        // Read response
        DWORD dwSize = 0;
        DWORD dwDownloaded = 0;
        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
            
            if (dwSize == 0) break;
            
            char* pszOutBuffer = new char[dwSize + 1];
            ZeroMemory(pszOutBuffer, dwSize + 1);
            
            if (WinHttpReadData(hRequest, (LPVOID)pszOutBuffer, dwSize, &dwDownloaded)) {
                result += std::string(pszOutBuffer, dwDownloaded);
            }
            
            delete[] pszOutBuffer;
        } while (dwSize > 0);
        
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        
        return result;
    }
    
    static std::string getAvatar(const std::string& token, const std::string& serverUrl = "http://localhost:3000") {
        return httpGet(serverUrl + "/api/avatar", token);
    }
};

