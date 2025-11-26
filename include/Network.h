#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

// Packet Types
enum class PacketType : uint8_t {
    CONNECT = 0,
    WELCOME = 1,
    PLAYER_STATE = 2,
    PLAYER_JOIN = 3,
    PLAYER_LEAVE = 4,
    WORLD_UPDATE = 5, // Changed from WORLD_STATE
    WORLD_STATE = 7, // Complete world state (all parts) sent on connect
    CHAT = 6,
    PLAYER_LIST = 8  // Server sends list of all players
};

#pragma pack(push, 1)

struct PacketHeader {
    PacketType type;
    uint32_t size; // Payload size
};

// 1. Connect (Client -> Server)
struct PacketConnect {
    char username[32];
    char authToken[256]; // JWT token for authentication
};

// 2. Welcome (Server -> Client)
struct PacketWelcome {
    int playerId;
    // Followed by World State?
};

// 3. Player State (Client <-> Server)
// Client sends its own state. Server broadcasts others' states.
struct PacketPlayerState {
    int playerId;
    glm::vec3 position;
    float rotationY; // Yaw
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
};
// PacketPlayerList payload: uint32_t count, then array of PlayerInfo

// 6. World Update (Server -> Client)
// Contains a variable number of PartUpdates
struct PartUpdate {
    int partIndex;
    glm::vec3 position;
    glm::vec3 rotation;
};
// No main struct for WorldUpdate payload, it's just an array of PartUpdate

// 7. World State (Server -> Client)
// Complete world state sent when client connects
// Contains a variable number of PartData
struct PartData {
    int shape; // ShapeType as int
    char name[64]; // Part name
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
// No main struct for WorldState payload, it's just: uint32_t count, then array of PartData

#pragma pack(pop)

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
};
