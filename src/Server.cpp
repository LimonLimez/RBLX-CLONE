#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <cstring> // for memset
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <limits>
#include "Network.h"
#include "Part.h"
#include "PhysicsWorld.h"
#include "WorldLoader.h"
#include "ServerAuth.h"

// GLM Includes
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

struct ConnectedPlayer {
    SOCKET socket;
    int id;
    char username[32];
    PacketPlayerState state;
    bool active;
    std::vector<char> receiveBuffer; // Buffer for sticky packets
    rp3d::RigidBody* shadowBody; // Server-side physics body for player
    glm::vec3 headColor = glm::vec3(0.8f, 0.6f, 0.4f);
    glm::vec3 torsoColor = glm::vec3(0.2f, 0.4f, 0.8f);
    glm::vec3 leftArmColor = glm::vec3(0.8f, 0.6f, 0.4f);
    glm::vec3 rightArmColor = glm::vec3(0.8f, 0.6f, 0.4f);
    glm::vec3 leftLegColor = glm::vec3(0.2f, 0.6f, 0.2f);
    glm::vec3 rightLegColor = glm::vec3(0.2f, 0.6f, 0.2f);
};

std::map<int, ConnectedPlayer> players;
int nextPlayerId = 1;
const char* WEB_SERVER_URL = "http://localhost:3000"; // Web server URL for token verification
const float MAX_PLAYER_COORDINATE = 10000.0f;

// Server World State
std::deque<Part> serverParts;
PhysicsWorld serverPhysicsWorld;

bool isFiniteVec3(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool isSafePlayerState(const PacketPlayerState& state) {
    if (!isFiniteVec3(state.position) || !std::isfinite(state.rotationY) || !std::isfinite(state.health)) {
        return false;
    }

    return std::abs(state.position.x) <= MAX_PLAYER_COORDINATE &&
           std::abs(state.position.y) <= MAX_PLAYER_COORDINATE &&
           std::abs(state.position.z) <= MAX_PLAYER_COORDINATE;
}

float clamp01(float value) {
    if (!std::isfinite(value)) return 0.0f;
    return std::max(0.0f, std::min(1.0f, value));
}

bool parseAvatarColor(const std::string& avatarJson, const std::string& name, glm::vec3& color) {
    size_t pos = avatarJson.find("\"" + name + "\":[");
    if (pos == std::string::npos) return false;

    pos = avatarJson.find("[", pos);
    size_t end = avatarJson.find("]", pos);
    if (pos == std::string::npos || end == std::string::npos || end <= pos) return false;

    std::string colorStr = avatarJson.substr(pos + 1, end - pos - 1);
    size_t comma1 = colorStr.find(",");
    size_t comma2 = colorStr.find(",", comma1 + 1);
    if (comma1 == std::string::npos || comma2 == std::string::npos) return false;

    try {
        color.r = clamp01(std::stof(colorStr.substr(0, comma1)));
        color.g = clamp01(std::stof(colorStr.substr(comma1 + 1, comma2 - comma1 - 1)));
        color.b = clamp01(std::stof(colorStr.substr(comma2 + 1)));
        return true;
    } catch (...) {
        return false;
    }
}

void copyFixedString(char* destination, size_t destinationSize, const std::string& value) {
    if (!destination || destinationSize == 0) return;
    std::memset(destination, 0, destinationSize);
    const size_t bytesToCopy = std::min(destinationSize - 1, value.size());
    std::memcpy(destination, value.data(), bytesToCopy);
}

void broadcast(const void* data, int size, int excludeId = -1) {
    for (auto& [id, player] : players) {
        if (id == excludeId) continue;
        if (player.active && !Network::sendAll(player.socket, data, static_cast<size_t>(size))) {
            std::cerr << "Failed to send packet to player " << id << std::endl;
        }
    }
}

void broadcastPacket(const std::vector<char>& packet, int excludeId = -1) {
    if (!packet.empty()) {
        broadcast(packet.data(), static_cast<int>(packet.size()), excludeId);
    }
}

std::vector<char> buildPlayerListPacket() {
    std::vector<PlayerInfo> playerInfoList;
    for (auto& [id, player] : players) {
        if (player.active) {
            PlayerInfo info;
            std::memset(&info, 0, sizeof(info));
            info.playerId = id;
            copyFixedString(info.username, sizeof(info.username), PacketProtocol::fixedString(player.username, sizeof(player.username)));
            info.headColor[0] = player.headColor.r;
            info.headColor[1] = player.headColor.g;
            info.headColor[2] = player.headColor.b;
            info.torsoColor[0] = player.torsoColor.r;
            info.torsoColor[1] = player.torsoColor.g;
            info.torsoColor[2] = player.torsoColor.b;
            info.leftArmColor[0] = player.leftArmColor.r;
            info.leftArmColor[1] = player.leftArmColor.g;
            info.leftArmColor[2] = player.leftArmColor.b;
            info.rightArmColor[0] = player.rightArmColor.r;
            info.rightArmColor[1] = player.rightArmColor.g;
            info.rightArmColor[2] = player.rightArmColor.b;
            info.leftLegColor[0] = player.leftLegColor.r;
            info.leftLegColor[1] = player.leftLegColor.g;
            info.leftLegColor[2] = player.leftLegColor.b;
            info.rightLegColor[0] = player.rightLegColor.r;
            info.rightLegColor[1] = player.rightLegColor.g;
            info.rightLegColor[2] = player.rightLegColor.b;
            playerInfoList.push_back(info);
        }
    }

    uint32_t playerCount = (uint32_t)playerInfoList.size();
    uint32_t payloadSize = sizeof(uint32_t) + static_cast<uint32_t>(playerInfoList.size() * sizeof(PlayerInfo));

    std::vector<char> payload(payloadSize);
    std::memcpy(payload.data(), &playerCount, sizeof(uint32_t));

    if (!playerInfoList.empty()) {
        std::memcpy(payload.data() + sizeof(uint32_t),
                    playerInfoList.data(),
                    playerInfoList.size() * sizeof(PlayerInfo));
    }

    return PacketProtocol::buildPacket(PacketType::PLAYER_LIST, payload.data(), payloadSize);
}

void sendPlayerList(SOCKET socket) {
    std::vector<char> packet = buildPlayerListPacket();
    Network::sendAll(socket, packet.data(), packet.size());
}

void broadcastPlayerList(int excludeId = -1) {
    broadcastPacket(buildPlayerListPacket(), excludeId);
}

void updatePlayerShadowBody(ConnectedPlayer& player) {
    if (!player.active) return;

    if (!player.shadowBody) {
        // Create shadow body if not exists
        rp3d::Vector3 pos(player.state.position.x, player.state.position.y, player.state.position.z);
        rp3d::Quaternion rot = rp3d::Quaternion::identity();
        rp3d::Transform transform(pos, rot);

        player.shadowBody = serverPhysicsWorld.world->createRigidBody(transform);
        player.shadowBody->setType(rp3d::BodyType::KINEMATIC); // Moved by code (player input)

        // Add a capsule or box shape for collision
        rp3d::Vector3 halfExtents(1.0f, 2.5f, 1.0f); // Roughly character size
        rp3d::BoxShape* shape = serverPhysicsWorld.physicsCommon.createBoxShape(halfExtents);
        player.shadowBody->addCollider(shape, rp3d::Transform::identity());

        // Set friction to 0 like character
        if (player.shadowBody->getNbColliders() > 0) {
            player.shadowBody->getCollider(0)->getMaterial().setFrictionCoefficient(0.0f);
        }
    }

    // Move shadow body smoothly using velocity (KINEMATIC bodies can push DYNAMIC parts when moving)
    rp3d::Transform currentTransform = player.shadowBody->getTransform();
    rp3d::Vector3 currentPos = currentTransform.getPosition();
    rp3d::Vector3 targetPos(player.state.position.x, player.state.position.y, player.state.position.z);

    rp3d::Vector3 diff = targetPos - currentPos;
    float distance = diff.length();

    // Set velocity to move towards target (KINEMATIC bodies with velocity can push parts)
    if (distance > 0.01f) {
        // Calculate velocity needed to reach target in one physics step (1/60s)
        float speed = distance * 60.0f; // Move distance in one frame
        rp3d::Vector3 direction = diff.getUnit();
        rp3d::Vector3 velocity = direction * speed;
        player.shadowBody->setLinearVelocity(velocity);
    } else {
        // Close enough, just set position directly
        player.shadowBody->setLinearVelocity(rp3d::Vector3(0, 0, 0));
        rp3d::Transform newTransform(targetPos, currentTransform.getOrientation());
        player.shadowBody->setTransform(newTransform);
    }

    // Update rotation smoothly
    rp3d::Quaternion currentRot = currentTransform.getOrientation();
    rp3d::Quaternion targetRot = rp3d::Quaternion::fromEulerAngles(0, glm::radians(player.state.rotationY), 0);
    rp3d::Quaternion newRot = rp3d::Quaternion::slerp(currentRot, targetRot, 0.3f);
    rp3d::Transform newTransform(currentPos, newRot);
    player.shadowBody->setTransform(newTransform);

    // Zero angular velocity (we control rotation directly)
    player.shadowBody->setAngularVelocity(rp3d::Vector3(0, 0, 0));
}

void broadcastWorldUpdate() {
    // Send updates for ALL dynamic parts (server is authoritative)
    // This ensures all clients stay in sync, even if parts are stationary
    std::vector<PartUpdate> updates;
    for (int i = 0; i < serverParts.size(); ++i) {
        Part& p = serverParts[i];
        if (p.physicsBody) {
            rp3d::RigidBody* body = (rp3d::RigidBody*)p.physicsBody;
            // Send updates for all dynamic parts (active or not) to keep clients in sync
            if (body->getType() == rp3d::BodyType::DYNAMIC) {
                rp3d::Transform t = body->getTransform();
                rp3d::Vector3 pos = t.getPosition();
                rp3d::Quaternion rot = t.getOrientation();

                // Server runs physics exactly like singleplayer - simple and clean
                p.position = glm::vec3(pos.x, pos.y, pos.z);

                PartUpdate up;
                up.partIndex = i;
                up.position = p.position;

                // Convert quaternion to euler for network transmission
                glm::quat q(rot.w, rot.x, rot.y, rot.z);
                up.rotation = glm::degrees(glm::eulerAngles(q));

                updates.push_back(up);
            }
        }
    }

    if (updates.empty()) return;

    if (updates.size() > PacketProtocol::MAX_WORLD_UPDATES) {
        updates.resize(PacketProtocol::MAX_WORLD_UPDATES);
    }

    uint32_t payloadSize = static_cast<uint32_t>(updates.size() * sizeof(PartUpdate));
    broadcastPacket(PacketProtocol::buildPacket(PacketType::WORLD_UPDATE, updates.data(), payloadSize));
}

int main() {
    // Initialize random seed for guest usernames
    srand((unsigned int)time(NULL));

    // Initialize Physics World
    serverPhysicsWorld.init();

    if (!Network::init()) {
        std::cerr << "Failed to init winsock" << std::endl;
        return 1;
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "Failed to create socket" << std::endl;
        return 1;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(7777);

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed" << std::endl;
        return 1;
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed" << std::endl;
        return 1;
    }

    Network::setNonBlocking(serverSocket);

    std::cout << "RBLX Clone Server started on port 7777" << std::endl;

    // Load Server World
    WorldLoader::loadWorld("ServerWorld.world", serverParts, serverPhysicsWorld);
    std::cout << "Loaded ServerWorld with " << serverParts.size() << " parts." << std::endl;

    auto lastTime = std::chrono::high_resolution_clock::now();
    float physicsAccumulator = 0.0f;
    float networkAccumulator = 0.0f;

    while (true) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;

        physicsAccumulator += deltaTime;
        networkAccumulator += deltaTime;

        // Physics Step (60Hz) - with safety checks
        while (physicsAccumulator >= 1.0f/60.0f) {
            try {
                serverPhysicsWorld.update(1.0f/60.0f);
            } catch (...) {
                std::cerr << "ERROR: Physics update crashed, continuing..." << std::endl;
            }
            physicsAccumulator -= 1.0f/60.0f;
        }

        // Update all player shadow bodies every frame (so they can push parts)
        for (auto& [id, player] : players) {
            if (player.active) {
                updatePlayerShadowBody(player);
            }
        }

        // Network Broadcast (60Hz - match physics rate for smooth updates)
        if (networkAccumulator >= 1.0f/60.0f) {
            broadcastWorldUpdate();
            networkAccumulator = 0.0f;
        }

        // 1. Accept New Connections
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket != INVALID_SOCKET) {
            Network::setNonBlocking(clientSocket);

            ConnectedPlayer newPlayer;
            newPlayer.socket = clientSocket;
            newPlayer.id = nextPlayerId++;
            newPlayer.active = false;
            newPlayer.shadowBody = nullptr; // Init to null
            memset(newPlayer.username, 0, 32);

            players[newPlayer.id] = newPlayer;
            std::cout << "Client connected, assigned ID " << newPlayer.id << std::endl;
        }

        // 2. Process Clients
        std::vector<int> disconnectedIds;

        for (auto& [id, player] : players) {
            bool shouldDisconnect = false;
            char buffer[4096];
            int bytesReceived = recv(player.socket, buffer, sizeof(buffer), 0);

            if (bytesReceived > 0) {
                player.receiveBuffer.insert(player.receiveBuffer.end(), buffer, buffer + bytesReceived);

                while (!shouldDisconnect) {
                    PacketProtocol::ParsedPacket frame;
                    PacketProtocol::ParseStatus status = PacketProtocol::tryReadFrame(player.receiveBuffer, frame);
                    if (status == PacketProtocol::ParseStatus::NeedMoreData) {
                        break;
                    }
                    if (status == PacketProtocol::ParseStatus::Invalid) {
                        std::cerr << "Disconnecting player " << id << " after malformed packet" << std::endl;
                        shouldDisconnect = true;
                        break;
                    }

                    if (frame.type == PacketType::CONNECT) {
                        PacketConnect pkt;
                        std::memcpy(&pkt, frame.payload, sizeof(pkt));

                        const std::string token = PacketProtocol::fixedString(pkt.authToken, sizeof(pkt.authToken));
                        const bool isGuest = token.empty();

                        player.headColor = glm::vec3(0.8f, 0.6f, 0.4f);
                        player.torsoColor = glm::vec3(0.2f, 0.4f, 0.8f);
                        player.leftArmColor = glm::vec3(0.8f, 0.6f, 0.4f);
                        player.rightArmColor = glm::vec3(0.8f, 0.6f, 0.4f);
                        player.leftLegColor = glm::vec3(0.2f, 0.6f, 0.2f);
                        player.rightLegColor = glm::vec3(0.2f, 0.6f, 0.2f);

                        if (!isGuest) {
                            VerifiedUser verified = ServerAuth::verifyToken(token, WEB_SERVER_URL);
                            if (!verified.success) {
                                std::cout << "Player " << id << " failed authentication" << std::endl;
                                shouldDisconnect = true;
                                break;
                            }

                            copyFixedString(player.username, sizeof(player.username), verified.username);

                            std::string avatarJson = ServerAuth::getAvatar(token, WEB_SERVER_URL);
                            if (!avatarJson.empty() && avatarJson.find("\"success\":true") != std::string::npos) {
                                parseAvatarColor(avatarJson, "headColor", player.headColor);
                                parseAvatarColor(avatarJson, "torsoColor", player.torsoColor);
                                parseAvatarColor(avatarJson, "leftArmColor", player.leftArmColor);
                                parseAvatarColor(avatarJson, "rightArmColor", player.rightArmColor);
                                parseAvatarColor(avatarJson, "leftLegColor", player.leftLegColor);
                                parseAvatarColor(avatarJson, "rightLegColor", player.rightLegColor);
                            }

                            std::cout << "Player " << id << " joined as " << player.username << " (authenticated)" << std::endl;
                        } else {
                            int guestNumber = (rand() % 9999) + 1;
                            snprintf(player.username, sizeof(player.username), "guest_%d", guestNumber);
                            std::cout << "Player " << id << " joined as " << player.username << " (guest)" << std::endl;
                        }

                        player.active = true;

                        PacketWelcome welcome = { id };
                        if (!Network::sendStructPacket(player.socket, PacketType::WELCOME, welcome)) {
                            shouldDisconnect = true;
                            break;
                        }

                        std::vector<PartData> worldData;
                        worldData.reserve(std::min<size_t>(serverParts.size(), PacketProtocol::MAX_WORLD_PARTS));
                        for (size_t i = 0; i < serverParts.size() && worldData.size() < PacketProtocol::MAX_WORLD_PARTS; ++i) {
                            Part& p = serverParts[i];

                            PartData data;
                            std::memset(&data, 0, sizeof(data));
                            data.shape = (int)p.shape;
                            copyFixedString(data.name, sizeof(data.name), p.name);
                            data.parentIndex = p.parentIndex;

                            data.flags = 0;
                            if (p.isFolder) data.flags |= 1;
                            if (p.isCamera) data.flags |= 2;
                            if (p.isSpawn) data.flags |= 4;

                            if (p.physicsBody) {
                                rp3d::RigidBody* body = (rp3d::RigidBody*)p.physicsBody;
                                rp3d::Transform t = body->getTransform();
                                rp3d::Vector3 pos = t.getPosition();
                                rp3d::Quaternion rot = t.getOrientation();

                                data.position = glm::vec3(pos.x, pos.y, pos.z);

                                glm::quat q(rot.w, rot.x, rot.y, rot.z);
                                data.rotation = glm::degrees(glm::eulerAngles(q));
                            } else {
                                data.position = p.position;
                                data.rotation = p.rotation;
                            }

                            data.size = p.size;
                            data.color = p.color;
                            data.transparency = p.transparency;
                            data.reflectance = p.reflectance;

                            data.physicsFlags = 0;
                            if (p.anchored) data.physicsFlags |= 1;
                            if (p.canCollide) data.physicsFlags |= 2;

                            data.mass = p.mass;
                            worldData.push_back(data);
                        }

                        uint32_t partCount = static_cast<uint32_t>(worldData.size());
                        uint32_t payloadSize = sizeof(uint32_t) + static_cast<uint32_t>(worldData.size() * sizeof(PartData));
                        std::vector<char> payload(payloadSize);
                        std::memcpy(payload.data(), &partCount, sizeof(uint32_t));
                        if (!worldData.empty()) {
                            std::memcpy(payload.data() + sizeof(uint32_t), worldData.data(), worldData.size() * sizeof(PartData));
                        }

                        if (!Network::sendPacket(player.socket, PacketType::WORLD_STATE, payload.data(), payloadSize)) {
                            shouldDisconnect = true;
                            break;
                        }
                        std::cout << "Sent complete world state (" << partCount << " parts) to player " << id << std::endl;

                        sendPlayerList(player.socket);
                        broadcastPlayerList(id);

                        PacketPlayerJoin joinPkt;
                        std::memset(&joinPkt, 0, sizeof(joinPkt));
                        joinPkt.playerId = id;
                        copyFixedString(joinPkt.username, sizeof(joinPkt.username), PacketProtocol::fixedString(player.username, sizeof(player.username)));
                        broadcastPacket(PacketProtocol::buildStructPacket(PacketType::PLAYER_JOIN, joinPkt), id);

                        for (auto& [otherId, otherPlayer] : players) {
                            if (otherId != id && otherPlayer.active) {
                                PacketPlayerJoin otherJoinPkt;
                                std::memset(&otherJoinPkt, 0, sizeof(otherJoinPkt));
                                otherJoinPkt.playerId = otherId;
                                copyFixedString(otherJoinPkt.username, sizeof(otherJoinPkt.username), PacketProtocol::fixedString(otherPlayer.username, sizeof(otherPlayer.username)));
                                if (!Network::sendStructPacket(player.socket, PacketType::PLAYER_JOIN, otherJoinPkt)) {
                                    shouldDisconnect = true;
                                    break;
                                }
                            }
                        }
                    } else if (frame.type == PacketType::PLAYER_STATE) {
                        if (!player.active) {
                            PacketProtocol::consumeFrame(player.receiveBuffer, frame.frameSize);
                            continue;
                        }

                        PacketPlayerState pkt;
                        std::memcpy(&pkt, frame.payload, sizeof(pkt));
                        if (!isSafePlayerState(pkt)) {
                            std::cerr << "Ignoring invalid player state from player " << id << std::endl;
                            PacketProtocol::consumeFrame(player.receiveBuffer, frame.frameSize);
                            continue;
                        }

                        player.state = pkt;
                        player.state.playerId = id;
                        player.state.health = std::max(0.0f, std::min(100.0f, player.state.health));

                        updatePlayerShadowBody(player);
                        broadcastPacket(PacketProtocol::buildStructPacket(PacketType::PLAYER_STATE, player.state), id);
                    }

                    PacketProtocol::consumeFrame(player.receiveBuffer, frame.frameSize);
                }

            } else if (bytesReceived == 0 || (bytesReceived == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)) {
                shouldDisconnect = true;
            }

            if (shouldDisconnect && std::find(disconnectedIds.begin(), disconnectedIds.end(), id) == disconnectedIds.end()) {
                disconnectedIds.push_back(id);
            }
        }

        // 3. Handle Disconnects
        for (int id : disconnectedIds) {
            if (players.find(id) == players.end()) {
                continue;
            }
            std::cout << "Player " << id << " disconnected." << std::endl;
            closesocket(players[id].socket);

            // Cleanup Shadow Body
            if (players[id].shadowBody) {
                serverPhysicsWorld.world->destroyRigidBody(players[id].shadowBody);
            }

            if (players[id].active) {
                players[id].active = false;
                PacketPlayerLeave leavePkt = { id };
                broadcastPacket(PacketProtocol::buildStructPacket(PacketType::PLAYER_LEAVE, leavePkt), id);
                broadcastPlayerList();
            }

            players.erase(id);
        }

        Sleep(1);
    }

    Network::cleanup();
    return 0;
}
