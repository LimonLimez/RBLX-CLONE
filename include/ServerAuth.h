#pragma once

#include <string>

class ServerAuth {
public:
    static bool verifyToken(const std::string& token, const std::string& serverUrl = "http://localhost:3000");
    static std::string getAvatar(const std::string& token, const std::string& serverUrl = "http://localhost:3000");
};

