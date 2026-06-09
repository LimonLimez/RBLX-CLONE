#pragma once

#include <string>

struct VerifiedUser {
    bool success = false;
    int userId = -1;
    std::string username;
};

class ServerAuth {
public:
    static VerifiedUser verifyToken(const std::string& token, const std::string& serverUrl = "http://localhost:3000");
    static std::string getAvatar(const std::string& token, const std::string& serverUrl = "http://localhost:3000");
    static bool recordPlaytime(const std::string& token, int seconds, const std::string& serverUrl = "http://localhost:3000");
    static bool reportInstanceHeartbeat(const std::string& instanceId, const std::string& managerToken, int playerCount, const std::string& serverUrl = "http://localhost:3000");
};
