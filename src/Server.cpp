#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <cstring> // for memset
#include <chrono>
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

// Server World State
std::deque<Part> serverParts;
PhysicsWorld serverPhysicsWorld;

void broadcast(const void* data, int size, int excludeId = -1) {
    for (auto& [id, player] : players) {
        if (id == excludeId) continue;
        send(player.socket, (const char*)data, size, 0);
    }
}

void sendPlayerList(SOCKET socket) {
    std::vector<PlayerInfo> playerInfoList;
    for (auto& [id, player] : players) {
        if (player.active) {
            PlayerInfo info;
            info.playerId = id;
            strncpy(info.username, player.username, 32);
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
    int payloadSize = sizeof(uint32_t) + playerInfoList.size() * sizeof(PlayerInfo);
    int packetSize = sizeof(PacketHeader) + payloadSize;
    
    std::vector<char> packet(packetSize);
    PacketHeader* header = (PacketHeader*)packet.data();
    header->type = PacketType::PLAYER_LIST;
    header->size = payloadSize;
    
    memcpy(packet.data() + sizeof(PacketHeader), &playerCount, sizeof(uint32_t));
    if (!playerInfoList.empty()) {
        memcpy(packet.data() + sizeof(PacketHeader) + sizeof(uint32_t), 
               playerInfoList.data(), playerInfoList.size() * sizeof(PlayerInfo));
    }
    
    send(socket, packet.data(), packetSize, 0);
}

void broadcastPlayerList(int excludeId = -1) {
    std::vector<PlayerInfo> playerInfoList;
    for (auto& [id, player] : players) {
        if (player.active) {
            PlayerInfo info;
            info.playerId = id;
            strncpy(info.username, player.username, 32);
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
    int payloadSize = sizeof(uint32_t) + playerInfoList.size() * sizeof(PlayerInfo);
    int packetSize = sizeof(PacketHeader) + payloadSize;
    
    std::vector<char> packet(packetSize);
    PacketHeader* header = (PacketHeader*)packet.data();
    header->type = PacketType::PLAYER_LIST;
    header->size = payloadSize;
    
    memcpy(packet.data() + sizeof(PacketHeader), &playerCount, sizeof(uint32_t));
    if (!playerInfoList.empty()) {
        memcpy(packet.data() + sizeof(PacketHeader) + sizeof(uint32_t), 
               playerInfoList.data(), playerInfoList.size() * sizeof(PlayerInfo));
    }
    
    broadcast(packet.data(), packetSize, excludeId);
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
    
    int payloadSize = updates.size() * sizeof(PartUpdate);
    int packetSize = sizeof(PacketHeader) + payloadSize;
    
    std::vector<char> packet(packetSize);
    PacketHeader* header = (PacketHeader*)packet.data();
    header->type = PacketType::WORLD_UPDATE;
    header->size = payloadSize;
    
    memcpy(packet.data() + sizeof(PacketHeader), updates.data(), payloadSize);
    
    broadcast(packet.data(), packetSize);
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
            char buffer[4096];
            int bytesReceived = recv(player.socket, buffer, sizeof(buffer), 0);
            
            if (bytesReceived > 0) {
                // Append to player's buffer
                player.receiveBuffer.insert(player.receiveBuffer.end(), buffer, buffer + bytesReceived);
                
                // Process Loop
                while (true) {
                    if (player.receiveBuffer.size() < sizeof(PacketHeader)) break; 
                    
                    PacketHeader* header = (PacketHeader*)player.receiveBuffer.data();
                    uint32_t packetSize = sizeof(PacketHeader) + header->size;
                    
                    if (player.receiveBuffer.size() < packetSize) break; 
                    
                    // Handle Packet
                    char* packetData = player.receiveBuffer.data();
                    
                    if (header->type == PacketType::CONNECT) {
                        if (header->size >= sizeof(PacketConnect)) {
                            PacketConnect* pkt = (PacketConnect*)(packetData + sizeof(PacketHeader));
                            
                            // Check if token is provided
                            std::string token(pkt->authToken);
                            bool isGuest = token.empty() || token.length() == 0;
                            
                            // Reset avatar colors to defaults first (important for reconnections)
                            player.headColor = glm::vec3(0.8f, 0.6f, 0.4f);
                            player.torsoColor = glm::vec3(0.2f, 0.4f, 0.8f);
                            player.leftArmColor = glm::vec3(0.8f, 0.6f, 0.4f);
                            player.rightArmColor = glm::vec3(0.8f, 0.6f, 0.4f);
                            player.leftLegColor = glm::vec3(0.2f, 0.6f, 0.2f);
                            player.rightLegColor = glm::vec3(0.2f, 0.6f, 0.2f);
                            
                            if (!isGuest) {
                                // Verify authentication token for logged-in users
                                if (!ServerAuth::verifyToken(token, WEB_SERVER_URL)) {
                                    std::cout << "Player " << id << " failed authentication" << std::endl;
                                    closesocket(player.socket);
                                    players.erase(id);
                                    continue;
                                }
                                // Fetch avatar from web server
                                std::string avatarJson = ServerAuth::getAvatar(token, WEB_SERVER_URL);
                                if (!avatarJson.empty() && avatarJson.find("\"success\":true") != std::string::npos) {
                                    // Parse avatar colors (simple JSON parsing)
                                    auto parseColor = [&avatarJson](const std::string& name, glm::vec3& color) {
                                        size_t pos = avatarJson.find("\"" + name + "\":[");
                                        if (pos != std::string::npos) {
                                            pos = avatarJson.find("[", pos);
                                            if (pos != std::string::npos) {
                                                pos++;
                                                size_t end = avatarJson.find("]", pos);
                                                if (end != std::string::npos) {
                                                    std::string colorStr = avatarJson.substr(pos, end - pos);
                                                    size_t comma1 = colorStr.find(",");
                                                    size_t comma2 = colorStr.find(",", comma1 + 1);
                                                    if (comma1 != std::string::npos && comma2 != std::string::npos) {
                                                        color.r = std::stof(colorStr.substr(0, comma1));
                                                        color.g = std::stof(colorStr.substr(comma1 + 1, comma2 - comma1 - 1));
                                                        color.b = std::stof(colorStr.substr(comma2 + 1));
                                                    }
                                                }
                                            }
                                        }
                                    };
                                    
                                    parseColor("headColor", player.headColor);
                                    parseColor("torsoColor", player.torsoColor);
                                    parseColor("leftArmColor", player.leftArmColor);
                                    parseColor("rightArmColor", player.rightArmColor);
                                    parseColor("leftLegColor", player.leftLegColor);
                                    parseColor("rightLegColor", player.rightLegColor);
                                }
                                
                                // Use username from packet for authenticated users
                                memcpy(player.username, pkt->username, 31);
                                player.username[31] = '\0';
                                std::cout << "Player " << id << " joined as " << player.username << " (authenticated)" << std::endl;
                            } else {
                                // Generate guest username
                                int guestNumber = (rand() % 9999) + 1;
                                snprintf(player.username, 32, "guest_%d", guestNumber);
                                std::cout << "Player " << id << " joined as " << player.username << " (guest)" << std::endl;
                            }
                            
                            player.active = true;
                            
                            // Send Welcome
                            PacketHeader welcomeHeader = { PacketType::WELCOME, sizeof(PacketWelcome) };
                            PacketWelcome welcome = { id };
                            
                            send(player.socket, (char*)&welcomeHeader, sizeof(welcomeHeader), 0);
                            send(player.socket, (char*)&welcome, sizeof(welcome), 0);
                            
                            // Send complete world state to client (all parts, not just dynamic)
                            // This allows client to work without needing the world file
                            std::vector<PartData> worldData;
                            for (int i = 0; i < serverParts.size(); ++i) {
                                Part& p = serverParts[i];
                                
                                PartData data;
                                data.shape = (int)p.shape;
                                strncpy(data.name, p.name.c_str(), 63);
                                data.name[63] = '\0';
                                data.parentIndex = p.parentIndex;
                                
                                // Pack flags
                                data.flags = 0;
                                if (p.isFolder) data.flags |= 1;
                                if (p.isCamera) data.flags |= 2;
                                if (p.isSpawn) data.flags |= 4;
                                
                                // Get position/rotation from physics body if it exists, otherwise use stored values
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
                                
                                // Pack physics flags
                                data.physicsFlags = 0;
                                if (p.anchored) data.physicsFlags |= 1;
                                if (p.canCollide) data.physicsFlags |= 2;
                                
                                data.mass = p.mass;
                                
                                worldData.push_back(data);
                            }
                            
                            // Send world state packet: header + count + data array
                            uint32_t partCount = (uint32_t)worldData.size();
                            int payloadSize = sizeof(uint32_t) + worldData.size() * sizeof(PartData);
                            int packetSize = sizeof(PacketHeader) + payloadSize;
                            
                            std::vector<char> packet(packetSize);
                            PacketHeader* worldHeader = (PacketHeader*)packet.data();
                            worldHeader->type = PacketType::WORLD_STATE;
                            worldHeader->size = payloadSize;
                            
                            // Write count
                            memcpy(packet.data() + sizeof(PacketHeader), &partCount, sizeof(uint32_t));
                            // Write data array
                            if (!worldData.empty()) {
                                memcpy(packet.data() + sizeof(PacketHeader) + sizeof(uint32_t), 
                                       worldData.data(), worldData.size() * sizeof(PartData));
                            }
                            
                            send(player.socket, packet.data(), packetSize, 0);
                            std::cout << "Sent complete world state (" << partCount << " parts) to player " << id << std::endl;
                            
                            // Send player list to new player FIRST (so they have avatar data)
                            sendPlayerList(player.socket);
                            
                            // Broadcast updated player list to ALL existing players (so they have new player's avatar data)
                            broadcastPlayerList(id);
                            
                            // THEN broadcast new player join to existing players
                            PacketHeader joinHeader = { PacketType::PLAYER_JOIN, sizeof(PacketPlayerJoin) };
                            PacketPlayerJoin joinPkt;
                            joinPkt.playerId = id;
                            memcpy(joinPkt.username, player.username, 32);
                            broadcast(&joinHeader, sizeof(joinHeader), id);
                            broadcast(&joinPkt, sizeof(joinPkt), id);
                            
                            // Send existing players to new player
                            for (auto& [otherId, otherPlayer] : players) {
                                if (otherId != id && otherPlayer.active) {
                                    PacketHeader otherJoinHeader = { PacketType::PLAYER_JOIN, sizeof(PacketPlayerJoin) };
                                    PacketPlayerJoin otherJoinPkt;
                                    otherJoinPkt.playerId = otherId;
                                    memcpy(otherJoinPkt.username, otherPlayer.username, 32);
                                    
                                    send(player.socket, (char*)&otherJoinHeader, sizeof(otherJoinHeader), 0);
                                    send(player.socket, (char*)&otherJoinPkt, sizeof(otherJoinPkt), 0);
                                }
                            }
                        }
                    }
                    else if (header->type == PacketType::PLAYER_STATE) {
                        if (header->size >= sizeof(PacketPlayerState)) {
                            PacketPlayerState* pkt = (PacketPlayerState*)(packetData + sizeof(PacketHeader));
                            player.state = *pkt;
                            player.state.playerId = id; 
                            
                            // Update Server Shadow Body
                            updatePlayerShadowBody(player);
                            
                            PacketHeader updateHeader = { PacketType::PLAYER_STATE, sizeof(PacketPlayerState) };
                            broadcast(&updateHeader, sizeof(updateHeader), id);
                            broadcast(&player.state, sizeof(player.state), id);
                        }
                    }
                    
                    // Remove processed packet
                    player.receiveBuffer.erase(player.receiveBuffer.begin(), player.receiveBuffer.begin() + packetSize);
                }
                
            } else if (bytesReceived == 0 || (bytesReceived == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)) {
                disconnectedIds.push_back(id);
            }
        }
        
        // 3. Handle Disconnects
        for (int id : disconnectedIds) {
            std::cout << "Player " << id << " disconnected." << std::endl;
            closesocket(players[id].socket);
            
            // Cleanup Shadow Body
            if (players[id].shadowBody) {
                serverPhysicsWorld.world->destroyRigidBody(players[id].shadowBody);
            }
            
            if (players[id].active) {
                PacketHeader leaveHeader = { PacketType::PLAYER_LEAVE, sizeof(PacketPlayerLeave) };
                PacketPlayerLeave leavePkt = { id };
                
                broadcast(&leaveHeader, sizeof(leaveHeader), id);
                broadcast(&leavePkt, sizeof(leavePkt), id);
                
                // Broadcast updated player list
                broadcastPlayerList();
            }
            
            players.erase(id);
        }
        
        Sleep(1);
    }

    Network::cleanup();
    return 0;
}
