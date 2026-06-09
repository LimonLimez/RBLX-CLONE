#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

#include <glm/glm.hpp>

// Packet Types
enum class PacketType : uint8_t {
    CONNECT = 0,
    WELCOME = 1,
    PLAYER_STATE = 2,
    PLAYER_JOIN = 3,
    PLAYER_LEAVE = 4,
    WORLD_UPDATE = 5,
    CHAT = 6,
    WORLD_STATE = 7,
    PLAYER_LIST = 8
};

#pragma pack(push, 1)

struct PacketHeader {
    PacketType type;
    uint32_t size; // Payload size in bytes. Current protocol is little-endian.
};

// 1. Connect (Client -> Server)
struct PacketConnect {
    char username[32];
    char authToken[256]; // JWT token for authentication
};

// 2. Welcome (Server -> Client)
struct PacketWelcome {
    int playerId;
};

// 3. Player State (Client <-> Server)
struct PacketPlayerState {
    int playerId;
    glm::vec3 position;
    float rotationY;
    bool isWalking;
    bool isJumping;
    float health;
};

// 4. Player Join (Server -> Client)
struct PacketPlayerJoin {
    int playerId;
    char username[32];
};

// 5. Player Leave (Server -> Client)
struct PacketPlayerLeave {
    int playerId;
};

// 6. Chat (Client <-> Server)
struct PacketChat {
    int playerId;
    char username[32];
    char message[256];
};

// 8. Player List (Server -> Client)
struct PlayerInfo {
    int playerId;
    char username[32];
    float headColor[3];
    float torsoColor[3];
    float leftArmColor[3];
    float rightArmColor[3];
    float leftLegColor[3];
    float rightLegColor[3];
    char faceId[16];
};

// 6. World Update (Server -> Client)
struct PartUpdate {
    int partIndex;
    glm::vec3 position;
    glm::vec3 rotation;
};

// 7. World State (Server -> Client)
struct PartData {
    int shape;
    char name[64];
    int parentIndex;
    uint8_t flags; // isFolder (bit 0), isCamera (bit 1), isSpawn (bit 2)
    glm::vec3 position;
    glm::vec3 size;
    glm::vec3 color;
    glm::vec3 rotation;
    float transparency;
    float reflectance;
    uint8_t physicsFlags; // anchored (bit 0), canCollide (bit 1)
    float mass;
};

#pragma pack(pop)

namespace PacketProtocol {
    constexpr uint32_t MAX_PAYLOAD_SIZE = 64 * 1024;
    constexpr uint32_t MAX_PLAYERS = 128;
    constexpr uint32_t MAX_WORLD_PARTS = 4096;
    constexpr uint32_t MAX_WORLD_UPDATES = 4096;

    enum class ParseStatus {
        Complete,
        NeedMoreData,
        Invalid
    };

    struct ParsedPacket {
        PacketType type = PacketType::CONNECT;
        const char* payload = nullptr;
        uint32_t payloadSize = 0;
        size_t frameSize = 0;
    };

    inline bool isValidType(PacketType type) {
        switch (type) {
            case PacketType::CONNECT:
            case PacketType::WELCOME:
            case PacketType::PLAYER_STATE:
            case PacketType::PLAYER_JOIN:
            case PacketType::PLAYER_LEAVE:
            case PacketType::WORLD_UPDATE:
            case PacketType::CHAT:
            case PacketType::WORLD_STATE:
            case PacketType::PLAYER_LIST:
                return true;
            default:
                return false;
        }
    }

    inline bool validatePayload(PacketType type, const char* payload, uint32_t payloadSize) {
        if (payloadSize > MAX_PAYLOAD_SIZE) {
            return false;
        }

        switch (type) {
            case PacketType::CONNECT:
                return payloadSize == sizeof(PacketConnect);
            case PacketType::WELCOME:
                return payloadSize == sizeof(PacketWelcome);
            case PacketType::PLAYER_STATE:
                return payloadSize == sizeof(PacketPlayerState);
            case PacketType::PLAYER_JOIN:
                return payloadSize == sizeof(PacketPlayerJoin);
            case PacketType::PLAYER_LEAVE:
                return payloadSize == sizeof(PacketPlayerLeave);
            case PacketType::WORLD_UPDATE:
                return payloadSize % sizeof(PartUpdate) == 0 &&
                       payloadSize / sizeof(PartUpdate) <= MAX_WORLD_UPDATES;
            case PacketType::CHAT:
                return payloadSize == sizeof(PacketChat);
            case PacketType::WORLD_STATE: {
                if (payloadSize < sizeof(uint32_t) || !payload) return false;
                uint32_t count = 0;
                std::memcpy(&count, payload, sizeof(count));
                return count <= MAX_WORLD_PARTS &&
                       payloadSize == sizeof(uint32_t) + count * sizeof(PartData);
            }
            case PacketType::PLAYER_LIST: {
                if (payloadSize < sizeof(uint32_t) || !payload) return false;
                uint32_t count = 0;
                std::memcpy(&count, payload, sizeof(count));
                return count <= MAX_PLAYERS &&
                       payloadSize == sizeof(uint32_t) + count * sizeof(PlayerInfo);
            }
            default:
                return false;
        }
    }

    inline ParseStatus tryReadFrame(const std::vector<char>& buffer, ParsedPacket& packet) {
        if (buffer.size() < sizeof(PacketHeader)) {
            return ParseStatus::NeedMoreData;
        }

        PacketHeader header;
        std::memcpy(&header, buffer.data(), sizeof(header));

        if (!isValidType(header.type) || header.size > MAX_PAYLOAD_SIZE) {
            return ParseStatus::Invalid;
        }

        const size_t frameSize = sizeof(PacketHeader) + static_cast<size_t>(header.size);
        if (buffer.size() < frameSize) {
            return ParseStatus::NeedMoreData;
        }

        const char* payload = buffer.data() + sizeof(PacketHeader);
        if (!validatePayload(header.type, payload, header.size)) {
            return ParseStatus::Invalid;
        }

        packet.type = header.type;
        packet.payload = payload;
        packet.payloadSize = header.size;
        packet.frameSize = frameSize;
        return ParseStatus::Complete;
    }

    inline void consumeFrame(std::vector<char>& buffer, size_t frameSize) {
        buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(frameSize));
    }

    inline std::vector<char> buildPacket(PacketType type, const void* payload, uint32_t payloadSize) {
        std::vector<char> packet(sizeof(PacketHeader) + payloadSize);
        PacketHeader header{ type, payloadSize };
        std::memcpy(packet.data(), &header, sizeof(header));
        if (payload && payloadSize > 0) {
            std::memcpy(packet.data() + sizeof(PacketHeader), payload, payloadSize);
        }
        return packet;
    }

    template <typename T>
    inline std::vector<char> buildStructPacket(PacketType type, const T& payload) {
        return buildPacket(type, &payload, static_cast<uint32_t>(sizeof(T)));
    }

    inline std::string fixedString(const char* value, size_t capacity) {
        if (!value || capacity == 0) {
            return {};
        }
        size_t length = 0;
        while (length < capacity && value[length] != '\0') {
            ++length;
        }
        return std::string(value, length);
    }
}

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")

class Network {
public:
    static bool init() {
        WSADATA wsaData;
        return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
    }

    static void cleanup() {
        WSACleanup();
    }

    static bool setNonBlocking(SOCKET s) {
        u_long mode = 1;
        return ioctlsocket(s, FIONBIO, &mode) == 0;
    }

    static bool sendAll(SOCKET socket, const void* data, size_t size) {
        const char* bytes = static_cast<const char*>(data);
        size_t sentTotal = 0;

        while (sentTotal < size) {
            const int chunkSize = static_cast<int>(std::min<size_t>(size - sentTotal, 16384));
            const int sent = send(socket, bytes + sentTotal, chunkSize, 0);
            if (sent == SOCKET_ERROR) {
                const int error = WSAGetLastError();
                if (error == WSAEWOULDBLOCK) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                return false;
            }
            if (sent == 0) {
                return false;
            }
            sentTotal += static_cast<size_t>(sent);
        }

        return true;
    }

    static bool sendPacket(SOCKET socket, PacketType type, const void* payload, uint32_t payloadSize) {
        if (!PacketProtocol::validatePayload(type, static_cast<const char*>(payload), payloadSize)) {
            return false;
        }
        const std::vector<char> packet = PacketProtocol::buildPacket(type, payload, payloadSize);
        return sendAll(socket, packet.data(), packet.size());
    }

    template <typename T>
    static bool sendStructPacket(SOCKET socket, PacketType type, const T& payload) {
        return sendPacket(socket, type, &payload, static_cast<uint32_t>(sizeof(T)));
    }
};
#else
using SOCKET = int;
constexpr SOCKET INVALID_SOCKET = -1;

class Network {
public:
    static bool init() { return false; }
    static void cleanup() {}
    static bool setNonBlocking(SOCKET) { return false; }
    static bool sendAll(SOCKET, const void*, size_t) { return false; }
    static bool sendPacket(SOCKET, PacketType, const void*, uint32_t) { return false; }

    template <typename T>
    static bool sendStructPacket(SOCKET, PacketType, const T&) {
        return false;
    }
};
#endif
