#define _CRT_SECURE_NO_WARNINGS
#define GLM_ENABLE_EXPERIMENTAL
#include "Window.h"
#include "Renderer.h"
#include "Camera.h"
#include "Part.h"
#include "Raycaster.h"
#include "Skybox.h"
#include "PhysicsWorld.h"
#include "ShadowMap.h"
#include "Character.h"
#include "Network.h"
#include "WorldLoader.h"
#include "Auth.h"
#include "Avatar.h"
#include "UiScale.h"
#include <glm/gtx/euler_angles.hpp>
#include <fstream>
#include <sstream>

#include <deque>
#include <vector>
#include <map>
#include <iostream>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iomanip>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <commdlg.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// Settings
const unsigned int SCR_WIDTH = 1280;
const unsigned int SCR_HEIGHT = 720;

// Camera
Camera camera(glm::vec3(0.0f, 5.0f, 10.0f));
float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;

#include "stb_image.h"

// Texture Loader
unsigned int loadTexture(const char* path) {
    unsigned int textureID = 0;
    
    FILE* f = fopen(path, "rb");
    if (!f) {
        std::cout << "Texture file not found: " << path << std::endl;
        return 0;
    }
    fclose(f);

    int width, height, nrComponents;
    unsigned char *data = stbi_load(path, &width, &height, &nrComponents, 0);
    if (!data) {
        std::cout << "Texture failed to load at path: " << path << " - stbi_load returned NULL" << std::endl;
        return 0;
    }
    
    if (width == 0 || height == 0) {
        std::cout << "Texture has invalid size: " << width << "x" << height << std::endl;
        stbi_image_free(data);
        return 0;
    }
    
    std::cout << "Loading texture: " << path << " - Size: " << width << "x" << height << ", Components: " << nrComponents << std::endl;

    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    
    GLenum format;
    unsigned char* finalData = data;
    bool converted = false;
    
    // Convert 2-component (grayscale + alpha) to RGBA for easier shader handling
    if (nrComponents == 2) {
        finalData = new unsigned char[width * height * 4];
        for (int i = 0; i < width * height; i++) {
            unsigned char gray = data[i * 2];
            unsigned char alpha = data[i * 2 + 1];
            // Convert grayscale to RGB (all channels same)
            finalData[i * 4] = gray;     // R
            finalData[i * 4 + 1] = gray; // G
            finalData[i * 4 + 2] = gray; // B
            finalData[i * 4 + 3] = alpha; // A
        }
        format = GL_RGBA;
        converted = true;
    } else if (nrComponents == 1)
        format = GL_RED;
    else if (nrComponents == 3)
        format = GL_RGB;
    else if (nrComponents == 4)
        format = GL_RGBA;
    else {
        std::cout << "Unsupported texture format: " << nrComponents << " components" << std::endl;
        stbi_image_free(data);
        glDeleteTextures(1, &textureID);
        return 0;
    }

    // Check for OpenGL errors before creating texture
    GLenum glError = glGetError();
    if (glError != GL_NO_ERROR) {
        std::cout << "OpenGL error before texture creation: " << glError << std::endl;
    }
    
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, finalData);
    
    glError = glGetError();
    if (glError != GL_NO_ERROR) {
        std::cout << "OpenGL error creating texture: " << glError << std::endl;
        stbi_image_free(data);
        if (converted) {
            delete[] finalData;
        }
        glDeleteTextures(1, &textureID);
        return 0;
    }
    
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Free original data
    stbi_image_free(data);
    // Free converted data if we created it
    if (converted) {
        delete[] finalData;
    }
    
    // Verify texture was created correctly
    GLint checkWidth = 0, checkHeight = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &checkWidth);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &checkHeight);
    
    if (checkWidth != width || checkHeight != height) {
        std::cout << "WARNING: Texture size mismatch! Expected " << width << "x" << height 
                  << " but got " << checkWidth << "x" << checkHeight << std::endl;
    }

    return textureID;
}

// State
bool isCursorLocked = false; // Start unlocked for login
float deltaTime = 0.0f;
float lastFrame = 0.0f;
unsigned int faceTextureId = 0;
const std::string DEFAULT_FACE_ID = "classic";
const std::vector<std::pair<std::string, const char*>> FACE_TEXTURE_FILES = {
    {"classic", "face.png"},
    {"happy", "face-happy.png"},
    {"surprised", "face-surprised.png"},
    {"smirk", "face-smirk.png"},
    {"wink", "face-wink.png"}
};
std::map<std::string, unsigned int> faceTextureIds;

enum GameState {
    MENU,
    MENU_AUTH_CHOICE,  // First menu: Login/Register/Guest
    MENU_PLAY_CHOICE,   // Second menu: Online/Offline
    OFFLINE,
    ONLINE
};
GameState currentState = MENU_AUTH_CHOICE;

// Network State
SOCKET clientSocket = INVALID_SOCKET;
int myPlayerId = -1;
bool isConnected = false;
char myUsername[32] = "Guest";
char serverIP[32] = "127.0.0.1";
int serverPort = 7777;
char authToken[256] = "";
char loginUsername[32] = "";
char loginPassword[64] = "";
char webServerUrl[128] = "http://localhost:3000";
const char* DEFAULT_WEB_SERVER_URL = "http://localhost:3000";
const char* DEFAULT_GAME_SERVER_IP = "127.0.0.1";
bool showLoginError = false;
char loginErrorMsg[256] = "";
bool showConnectionUI = false; // Track if connection UI should be shown
bool autoConnectOnStart = false;
int launchGameId = 0;
bool showDebugGui = false;
bool waitingForWebLogin = false;
SOCKET authListenSocket = INVALID_SOCKET;
SOCKET authClientSocket = INVALID_SOCKET;
int authCallbackPort = 0;
std::string authCallbackBuffer;
char chatInput[256] = "";

struct ChatLine {
    std::string username;
    std::string message;
    bool system = false;
};

std::map<int, Character*> remotePlayers;
std::map<int, std::string> playerNames;
std::vector<PlayerInfo> playerList;
std::vector<char> clientReceiveBuffer;
std::vector<ChatLine> chatLines;

Character* myCharacter = nullptr;
AvatarConfig currentAvatar;

bool isKnownFaceId(const std::string& faceId) {
    return faceId == "classic" ||
           faceId == "happy" ||
           faceId == "surprised" ||
           faceId == "smirk" ||
           faceId == "wink";
}

std::string normalizeFaceId(const std::string& faceId) {
    return isKnownFaceId(faceId) ? faceId : DEFAULT_FACE_ID;
}

unsigned int textureForFace(const std::string& faceId) {
    const std::string safeFaceId = normalizeFaceId(faceId);
    auto face = faceTextureIds.find(safeFaceId);
    if (face != faceTextureIds.end()) {
        return face->second;
    }

    auto defaultFace = faceTextureIds.find(DEFAULT_FACE_ID);
    return defaultFace != faceTextureIds.end() ? defaultFace->second : 0;
}

void loadFaceTextures() {
    faceTextureIds.clear();
    for (const auto& face : FACE_TEXTURE_FILES) {
        unsigned int textureId = loadTexture(face.second);
        if (textureId > 0) {
            faceTextureIds[face.first] = textureId;
            std::cout << "Face Texture Loaded: " << face.first << " (ID: " << textureId << ")" << std::endl;
        }
    }

    faceTextureId = textureForFace(DEFAULT_FACE_ID);
    if (faceTextureId == 0) {
        std::cout << "WARNING: Face textures failed to load! Check if face PNG files exist and are valid." << std::endl;
    }
}

std::string jsonStringField(const std::string& json, const std::string& name) {
    size_t pos = json.find("\"" + name + "\"");
    if (pos == std::string::npos) return {};

    pos = json.find(":", pos);
    if (pos == std::string::npos) return {};

    pos = json.find("\"", pos);
    if (pos == std::string::npos) return {};

    size_t end = json.find("\"", pos + 1);
    if (end == std::string::npos || end <= pos + 1) return {};

    return json.substr(pos + 1, end - pos - 1);
}

void copyToBuffer(char* destination, size_t destinationSize, const std::string& value) {
    if (!destination || destinationSize == 0) return;
    std::memset(destination, 0, destinationSize);
    std::memcpy(destination, value.c_str(), std::min(destinationSize - 1, value.size()));
}

std::string automaticWebServerUrl() {
    if (std::strlen(webServerUrl) == 0) {
        copyToBuffer(webServerUrl, sizeof(webServerUrl), DEFAULT_WEB_SERVER_URL);
    }
    return std::string(webServerUrl);
}

const char* automaticGameServerIp() {
    if (std::strlen(serverIP) == 0) {
        copyToBuffer(serverIP, sizeof(serverIP), DEFAULT_GAME_SERVER_IP);
    }
    return serverIP;
}

void addChatLine(const std::string& username, const std::string& message, bool system = false) {
    if (message.empty()) return;
    chatLines.push_back({username, message, system});
    if (chatLines.size() > 80) {
        chatLines.erase(chatLines.begin(), chatLines.begin() + static_cast<std::ptrdiff_t>(chatLines.size() - 80));
    }
}

std::string urlEncode(const std::string& value) {
    std::ostringstream encoded;
    encoded << std::uppercase << std::hex;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << c;
        } else {
            encoded << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
            encoded << std::setfill(' ');
        }
    }
    return encoded.str();
}

std::string urlDecode(const std::string& value) {
    std::string decoded;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+' ) {
            decoded.push_back(' ');
        } else if (value[i] == '%' && i + 2 < value.size()) {
            const std::string hex = value.substr(i + 1, 2);
            char* end = nullptr;
            long character = std::strtol(hex.c_str(), &end, 16);
            if (end && *end == '\0') {
                decoded.push_back(static_cast<char>(character));
                i += 2;
            } else {
                decoded.push_back(value[i]);
            }
        } else {
            decoded.push_back(value[i]);
        }
    }
    return decoded;
}

std::string queryParam(const std::string& request, const std::string& name) {
    const size_t requestStart = request.find("GET ");
    if (requestStart == std::string::npos) return {};
    const size_t pathStart = requestStart + 4;
    const size_t pathEnd = request.find(' ', pathStart);
    if (pathEnd == std::string::npos) return {};

    const std::string path = request.substr(pathStart, pathEnd - pathStart);
    const size_t queryStart = path.find('?');
    if (queryStart == std::string::npos) return {};

    const std::string query = path.substr(queryStart + 1);
    const std::string prefix = name + "=";
    size_t pos = 0;
    while (pos <= query.size()) {
        const size_t next = query.find('&', pos);
        const std::string pair = query.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
        if (pair.rfind(prefix, 0) == 0) {
            return urlDecode(pair.substr(prefix.size()));
        }
        if (next == std::string::npos) break;
        pos = next + 1;
    }
    return {};
}

bool parseJsonColorArray(const std::string& json, const std::string& name, glm::vec3& color) {
    size_t pos = json.find("\"" + name + "\":[");
    if (pos == std::string::npos) return false;

    pos = json.find("[", pos);
    if (pos == std::string::npos) return false;

    size_t end = json.find("]", pos);
    if (end == std::string::npos || end <= pos) return false;

    std::string colorStr = json.substr(pos + 1, end - pos - 1);
    size_t comma1 = colorStr.find(",");
    size_t comma2 = colorStr.find(",", comma1 + 1);
    if (comma1 == std::string::npos || comma2 == std::string::npos) return false;

    try {
        color.r = std::stof(colorStr.substr(0, comma1));
        color.g = std::stof(colorStr.substr(comma1 + 1, comma2 - comma1 - 1));
        color.b = std::stof(colorStr.substr(comma2 + 1));
        return true;
    } catch (...) {
        return false;
    }
}

void fetchAvatarForToken(const std::string& token, const std::string& serverUrl) {
    std::string avatarResponse = Auth::getAvatar(token, serverUrl);
    if (!avatarResponse.empty() && avatarResponse.find("\"success\":true") != std::string::npos) {
        parseJsonColorArray(avatarResponse, "headColor", currentAvatar.headColor);
        parseJsonColorArray(avatarResponse, "torsoColor", currentAvatar.torsoColor);
        parseJsonColorArray(avatarResponse, "leftArmColor", currentAvatar.leftArmColor);
        parseJsonColorArray(avatarResponse, "rightArmColor", currentAvatar.rightArmColor);
        parseJsonColorArray(avatarResponse, "leftLegColor", currentAvatar.leftLegColor);
        parseJsonColorArray(avatarResponse, "rightLegColor", currentAvatar.rightLegColor);
        currentAvatar.faceId = normalizeFaceId(jsonStringField(avatarResponse, "faceId"));
        currentAvatar.saveToFile("avatar.txt");
    }
}

bool applyAuthSession(const std::string& token, const std::string& fallbackUsername, const std::string& serverUrl, bool saveToken, std::string& error) {
    if (token.empty()) {
        error = "No token came back from the website.";
        return false;
    }

    std::string verifyResponse = Auth::verifyToken(token, serverUrl);
    if (verifyResponse.empty() || verifyResponse.find("\"success\":true") == std::string::npos) {
        error = "The website login could not be verified.";
        return false;
    }

    const std::string verifiedUsername = jsonStringField(verifyResponse, "username");
    copyToBuffer(authToken, sizeof(authToken), token);
    copyToBuffer(myUsername, sizeof(myUsername), verifiedUsername.empty() ? fallbackUsername : verifiedUsername);

    if (saveToken) {
        std::ofstream tokenFile("auth_token.txt");
        if (tokenFile.is_open()) {
            tokenFile << token;
        }
    }

    fetchAvatarForToken(token, serverUrl);
    return true;
}

void closeAuthCallback() {
    if (authClientSocket != INVALID_SOCKET) {
        closesocket(authClientSocket);
        authClientSocket = INVALID_SOCKET;
    }
    if (authListenSocket != INVALID_SOCKET) {
        closesocket(authListenSocket);
        authListenSocket = INVALID_SOCKET;
    }
    waitingForWebLogin = false;
    authCallbackPort = 0;
    authCallbackBuffer.clear();
}

bool startAuthCallback(std::string& loginUrl, std::string& error) {
    closeAuthCallback();

    for (int port = 39170; port < 39190; ++port) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) continue;

        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<u_short>(port));
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            closesocket(s);
            continue;
        }

        if (listen(s, 1) == SOCKET_ERROR) {
            closesocket(s);
            continue;
        }

        Network::setNonBlocking(s);
        authListenSocket = s;
        authCallbackPort = port;
        waitingForWebLogin = true;
        const std::string redirect = "http://127.0.0.1:" + std::to_string(port) + "/auth";
        loginUrl = automaticWebServerUrl() + "/login?client=player&redirect=" + urlEncode(redirect);
        return true;
    }

    error = "Could not open a local login callback port.";
    return false;
}

void sendAuthCallbackResponse(SOCKET socket, bool success) {
    const char* body = success
        ? "<!doctype html><title>RBLX Player</title><h1>Signed in</h1><p>You can return to the player.</p>"
        : "<!doctype html><title>RBLX Player</title><h1>Login failed</h1><p>Please return to the player and try again.</p>";
    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: "
             << std::strlen(body) << "\r\nConnection: close\r\n\r\n" << body;
    const std::string payload = response.str();
    send(socket, payload.c_str(), static_cast<int>(payload.size()), 0);
}

void pollAuthCallback() {
    if (!waitingForWebLogin || authListenSocket == INVALID_SOCKET) return;

    if (authClientSocket == INVALID_SOCKET) {
        sockaddr_in clientAddr;
        int clientLen = sizeof(clientAddr);
        SOCKET accepted = accept(authListenSocket, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
        if (accepted == INVALID_SOCKET) return;
        Network::setNonBlocking(accepted);
        authClientSocket = accepted;
    }

    char buffer[2048];
    int received = recv(authClientSocket, buffer, sizeof(buffer), 0);
    if (received == SOCKET_ERROR) {
        const int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK) return;
        closeAuthCallback();
        return;
    }
    if (received <= 0) return;

    authCallbackBuffer.append(buffer, buffer + received);
    if (authCallbackBuffer.find("\r\n\r\n") == std::string::npos) return;

    std::string error;
    const std::string token = queryParam(authCallbackBuffer, "token");
    const std::string username = queryParam(authCallbackBuffer, "username");
    const bool success = applyAuthSession(token, username, automaticWebServerUrl(), true, error);
    sendAuthCallbackResponse(authClientSocket, success);
    if (success) {
        showLoginError = false;
        currentState = MENU_PLAY_CHOICE;
        showConnectionUI = false;
        addChatLine("System", "Signed in as " + std::string(myUsername) + ".", true);
    } else {
        showLoginError = true;
        copyToBuffer(loginErrorMsg, sizeof(loginErrorMsg), error);
    }
    closeAuthCallback();
}

// Interpolation Struct
struct NetworkTransform {
    glm::vec3 position;
    glm::vec3 rotation; // Euler
    float timestamp;
};

std::map<int, NetworkTransform> targetTransforms;
std::map<int, NetworkTransform> startTransforms;
float interpolationTime = 0.1f; // 100ms buffer

std::string openFileDialog(GLFWwindow* window);

void applyModernPlayerStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 18.0f;
    style.ChildRounding = 14.0f;
    style.FrameRounding = 10.0f;
    style.PopupRounding = 12.0f;
    style.ScrollbarRounding = 12.0f;
    style.GrabRounding = 10.0f;
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(18.0f, 18.0f);
    style.FramePadding = ImVec2(12.0f, 10.0f);
    style.ItemSpacing = ImVec2(12.0f, 12.0f);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.045f, 0.052f, 0.070f, 0.92f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.070f, 0.078f, 0.102f, 0.88f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.050f, 0.056f, 0.074f, 0.96f);
    colors[ImGuiCol_Border] = ImVec4(1.0f, 1.0f, 1.0f, 0.10f);
    colors[ImGuiCol_Text] = ImVec4(0.94f, 0.96f, 1.0f, 1.0f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.59f, 0.66f, 1.0f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.105f, 0.118f, 0.150f, 0.96f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.145f, 0.165f, 0.210f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.180f, 0.210f, 0.270f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.135f, 0.325f, 0.850f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.185f, 0.395f, 0.940f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.090f, 0.245f, 0.700f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.135f, 0.325f, 0.850f, 0.65f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.185f, 0.395f, 0.940f, 0.80f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.090f, 0.245f, 0.700f, 0.95f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.105f, 0.850f, 0.505f, 1.0f);
}

ImGuiWindowFlags overlayFlags() {
    return ImGuiWindowFlags_NoDecoration |
           ImGuiWindowFlags_NoSavedSettings |
           ImGuiWindowFlags_NoFocusOnAppearing |
           ImGuiWindowFlags_NoNav |
           ImGuiWindowFlags_NoMove;
}

void drawLabel(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.64f, 0.76f, 1.0f));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

bool modernButton(const char* label, const ImVec2& size, const ImVec4& color) {
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(std::min(color.x + 0.08f, 1.0f), std::min(color.y + 0.08f, 1.0f), std::min(color.z + 0.08f, 1.0f), color.w));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(std::max(color.x - 0.06f, 0.0f), std::max(color.y - 0.06f, 0.0f), std::max(color.z - 0.06f, 0.0f), color.w));
    bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return pressed;
}

void disconnectFromServerOnly() {
    if (clientSocket != INVALID_SOCKET) {
        closesocket(clientSocket);
        clientSocket = INVALID_SOCKET;
    }
    isConnected = false;
    myPlayerId = -1;
    showConnectionUI = false;
    clientReceiveBuffer.clear();
}

void leaveCurrentGame(std::deque<Part>& parts, PhysicsWorld& physicsWorld) {
    disconnectFromServerOnly();
    if (myCharacter) {
        delete myCharacter;
        myCharacter = nullptr;
    }
    for (auto& [id, remoteChar] : remotePlayers) {
        if (remoteChar) delete remoteChar;
    }
    remotePlayers.clear();
    playerNames.clear();
    playerList.clear();
    targetTransforms.clear();
    startTransforms.clear();
    physicsWorld.reset();
    parts.clear();
    currentState = MENU_PLAY_CHOICE;
    addChatLine("System", "Left the game.", true);
}

bool connectToServer(std::string& error) {
    disconnectFromServerOnly();
    clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == INVALID_SOCKET) {
        error = "Could not create network socket.";
        return false;
    }

    sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    if (inet_pton(AF_INET, automaticGameServerIp(), &serverAddr.sin_addr) != 1) {
        error = "Could not find the game server.";
        disconnectFromServerOnly();
        return false;
    }
    serverAddr.sin_port = htons(static_cast<u_short>(serverPort));

    if (connect(clientSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        error = "Could not connect to the game server.";
        disconnectFromServerOnly();
        return false;
    }

    isConnected = true;
    currentState = ONLINE;
    Network::setNonBlocking(clientSocket);

    PacketConnect pkt;
    std::memset(&pkt, 0, sizeof(pkt));
    if (std::strlen(authToken) > 0) {
        copyToBuffer(pkt.username, sizeof(pkt.username), myUsername);
        copyToBuffer(pkt.authToken, sizeof(pkt.authToken), authToken);
    } else {
        copyToBuffer(pkt.username, sizeof(pkt.username), "Guest");
    }

    if (!Network::sendStructPacket(clientSocket, PacketType::CONNECT, pkt)) {
        error = "Connected, but could not send the join packet.";
        disconnectFromServerOnly();
        currentState = MENU_PLAY_CHOICE;
        return false;
    }

    showConnectionUI = false;
    addChatLine("System", "Joining online game...", true);
    return true;
}

void sendChatMessage() {
    std::string message = chatInput;
    message.erase(message.begin(), std::find_if(message.begin(), message.end(), [](unsigned char c) { return !std::isspace(c); }));
    message.erase(std::find_if(message.rbegin(), message.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), message.end());
    if (message.empty()) return;

    if (currentState == ONLINE && isConnected && clientSocket != INVALID_SOCKET) {
        PacketChat chat;
        std::memset(&chat, 0, sizeof(chat));
        chat.playerId = myPlayerId;
        copyToBuffer(chat.username, sizeof(chat.username), myUsername);
        copyToBuffer(chat.message, sizeof(chat.message), message);
        if (!Network::sendStructPacket(clientSocket, PacketType::CHAT, chat)) {
            addChatLine("System", "Could not send chat message.", true);
        }
    } else {
        addChatLine("System", "Chat is available after joining an online game.", true);
    }
    chatInput[0] = '\0';
}

void renderDebugWindow(const ImGuiIO& io) {
    if (!showDebugGui) return;

    ImGui::Begin("Debug Info", &showDebugGui);
    ImGui::Text("FPS: %.1f", io.Framerate);
    ImGui::Separator();

    if (currentState == ONLINE || currentState == OFFLINE) {
        ImGui::Text("=== CAMERA ===");
        ImGui::Text("Position: (%.2f, %.2f, %.2f)", camera.Position.x, camera.Position.y, camera.Position.z);
        ImGui::Text("Yaw: %.2f", camera.Yaw);
        ImGui::Text("Pitch: %.2f", camera.Pitch);
        ImGui::Separator();

        if (myCharacter) {
            ImGui::Text("=== PLAYER (Local) ===");
            glm::vec3 pos = myCharacter->getPosition();
            ImGui::Text("Position: (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);
            ImGui::Text("Internal Yaw: %.2f", myCharacter->debugCurrentYaw);
            ImGui::Text("Target Yaw: %.2f", myCharacter->debugTargetYaw);
            ImGui::Text("Move Dir: (%.2f, %.2f, %.2f)",
                myCharacter->debugMoveDir.x,
                myCharacter->debugMoveDir.y,
                myCharacter->debugMoveDir.z);

            if (currentState == ONLINE && isConnected) {
                ImGui::Separator();
                ImGui::Text("=== NETWORK ===");
                ImGui::Text("Sending RotationY: %.2f", myCharacter->debugCurrentYaw);
                ImGui::Text("Player ID: %d", myPlayerId);
            }

            if (!remotePlayers.empty()) {
                ImGui::Separator();
                ImGui::Text("=== REMOTE PLAYERS ===");
                for (auto& [id, remoteChar] : remotePlayers) {
                    ImGui::Text("Player %d:", id);
                    glm::vec3 remotePos = remoteChar->getPosition();
                    ImGui::Text("  Pos: (%.2f, %.2f, %.2f)", remotePos.x, remotePos.y, remotePos.z);
                    ImGui::Text("  Yaw: %.2f", remoteChar->debugCurrentYaw);
                    ImGui::Text("  Target: %.2f", remoteChar->debugTargetYaw);
                }
            }
        }
    }
    ImGui::End();
}

void renderAuthMenu(float uiScale) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float panelWidth = std::min(display.x - UiScale::Px(40.0f, uiScale), UiScale::Px(520.0f, uiScale));
    const float panelHeight = UiScale::Px(360.0f, uiScale);
    ImGui::SetNextWindowPos(ImVec2((display.x - panelWidth) * 0.5f, (display.y - panelHeight) * 0.5f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight), ImGuiCond_Always);

    if (ImGui::Begin("PlayerAuth", nullptr, overlayFlags())) {
        drawLabel("RBLX CLONE PLAYER");
        ImGui::TextUnformatted("Sign in through the website");
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + panelWidth - UiScale::Px(48.0f, uiScale));
        ImGui::TextWrapped("Sign in to continue with your account.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        if (std::strlen(authToken) > 0) {
            ImGui::Text("Signed in as %s", myUsername);
            if (modernButton("Continue", ImVec2(-1, UiScale::Px(46.0f, uiScale)), ImVec4(0.10f, 0.50f, 0.95f, 1.0f))) {
                currentState = MENU_PLAY_CHOICE;
            }
        }

        if (modernButton(waitingForWebLogin ? "Waiting for login" : "Login with website", ImVec2(-1, UiScale::Px(50.0f, uiScale)), ImVec4(0.10f, 0.50f, 0.95f, 1.0f))) {
            std::string loginUrl;
            std::string error;
            if (startAuthCallback(loginUrl, error)) {
                Auth::openBrowser(loginUrl);
                showLoginError = false;
            } else {
                showLoginError = true;
                copyToBuffer(loginErrorMsg, sizeof(loginErrorMsg), error);
            }
        }

        if (modernButton("Create account", ImVec2(-1, UiScale::Px(42.0f, uiScale)), ImVec4(0.16f, 0.18f, 0.23f, 1.0f))) {
            Auth::openBrowser(automaticWebServerUrl() + "/signup");
        }

        if (modernButton("Play as guest", ImVec2(-1, UiScale::Px(42.0f, uiScale)), ImVec4(0.16f, 0.18f, 0.23f, 1.0f))) {
            authToken[0] = '\0';
            copyToBuffer(myUsername, sizeof(myUsername), "Guest");
            currentState = MENU_PLAY_CHOICE;
            showLoginError = false;
        }

        if (showLoginError) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.36f, 0.36f, 1.0f));
            ImGui::TextWrapped("%s", loginErrorMsg);
            ImGui::PopStyleColor();
        }
    }
    ImGui::End();
}

void renderPlayMenu(GLFWwindow* nativeWindow, std::deque<Part>& parts, PhysicsWorld& physicsWorld, float uiScale) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float panelWidth = std::min(display.x - UiScale::Px(40.0f, uiScale), UiScale::Px(620.0f, uiScale));
    const float panelHeight = UiScale::Px(330.0f, uiScale);
    ImGui::SetNextWindowPos(ImVec2((display.x - panelWidth) * 0.5f, (display.y - panelHeight) * 0.5f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight), ImGuiCond_Always);

    if (ImGui::Begin("PlayMenu", nullptr, overlayFlags())) {
        drawLabel(std::strlen(authToken) > 0 ? "SIGNED IN" : "GUEST SESSION");
        ImGui::Text("Welcome, %s", myUsername);
        ImGui::TextWrapped("Choose how you want to play.");
        ImGui::Spacing();

        if (modernButton("Play online", ImVec2(-1, UiScale::Px(52.0f, uiScale)), ImVec4(0.10f, 0.50f, 0.95f, 1.0f))) {
            std::string error;
            if (!connectToServer(error)) {
                showLoginError = true;
                copyToBuffer(loginErrorMsg, sizeof(loginErrorMsg), error);
            } else {
                showLoginError = false;
            }
        }

        if (modernButton("Play offline", ImVec2(-1, UiScale::Px(48.0f, uiScale)), ImVec4(0.12f, 0.62f, 0.42f, 1.0f))) {
            std::string path = openFileDialog(nativeWindow);
            if (!path.empty()) {
                WorldLoader::loadWorld(path, parts, physicsWorld);
                if (myCharacter) delete myCharacter;
                glm::vec3 spawnPos(0, 10, 0);
                for (const auto& p : parts) {
                    if (p.isSpawn) spawnPos = p.position + glm::vec3(0, 2, 0);
                }
                myCharacter = new Character(spawnPos, &physicsWorld, &parts,
                    currentAvatar.headColor, currentAvatar.torsoColor,
                    currentAvatar.leftArmColor, currentAvatar.rightArmColor,
                    currentAvatar.leftLegColor, currentAvatar.rightLegColor);
                if (myCharacter) {
                    myCharacter->setFaceTexture(textureForFace(currentAvatar.faceId));
                }
                currentState = OFFLINE;
                lastFrame = static_cast<float>(glfwGetTime());
                addChatLine("System", "Offline world loaded.", true);
                showLoginError = false;
            }
        }

        if (modernButton("Switch account", ImVec2(-1, UiScale::Px(42.0f, uiScale)), ImVec4(0.16f, 0.18f, 0.23f, 1.0f))) {
            currentState = MENU_AUTH_CHOICE;
            showLoginError = false;
        }

        if (showLoginError) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.36f, 0.36f, 1.0f));
            ImGui::TextWrapped("%s", loginErrorMsg);
            ImGui::PopStyleColor();
        }
    }
    ImGui::End();
}

void renderTopBar(std::deque<Part>& parts, PhysicsWorld& physicsWorld, float uiScale) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float width = std::min(UiScale::Px(420.0f, uiScale), display.x - UiScale::Px(32.0f, uiScale));
    ImGui::SetNextWindowPos(ImVec2((display.x - width) * 0.5f, UiScale::Px(14.0f, uiScale)), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, UiScale::Px(58.0f, uiScale)), ImGuiCond_Always);
    if (ImGui::Begin("TopBarHud", nullptr, overlayFlags())) {
        ImGui::Text("%s", currentState == ONLINE ? "Online" : "Offline");
        ImGui::SameLine();
        if (modernButton("Leave game", ImVec2(UiScale::Px(130.0f, uiScale), UiScale::Px(34.0f, uiScale)), ImVec4(0.85f, 0.18f, 0.18f, 1.0f))) {
            leaveCurrentGame(parts, physicsWorld);
        }
        ImGui::SameLine();
        if (modernButton(showDebugGui ? "Hide debug" : "Debug", ImVec2(UiScale::Px(100.0f, uiScale), UiScale::Px(34.0f, uiScale)), ImVec4(0.16f, 0.18f, 0.23f, 1.0f))) {
            showDebugGui = !showDebugGui;
        }
    }
    ImGui::End();
}

void renderPlayerList(float uiScale) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float margin = UiScale::Px(18.0f, uiScale);
    const float width = std::clamp(display.x * 0.24f, UiScale::Px(230.0f, uiScale), UiScale::Px(340.0f, uiScale));
    const float height = std::clamp(UiScale::Px(92.0f + playerList.size() * 30.0f, uiScale), UiScale::Px(128.0f, uiScale), display.y * 0.46f);

    ImGui::SetNextWindowPos(ImVec2(display.x - width - margin, margin), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    if (ImGui::Begin("PlayerListHud", nullptr, overlayFlags())) {
        drawLabel("PLAYERS");
        int count = 1;
        for (const auto& player : playerList) {
            if (player.playerId != myPlayerId) ++count;
        }
        ImGui::Text("%d online", count);
        ImGui::Separator();
        ImGui::Text("%s  (You)", myUsername);
        for (const auto& player : playerList) {
            if (player.playerId == myPlayerId) continue;
            const std::string username = PacketProtocol::fixedString(player.username, sizeof(player.username));
            ImGui::Text("%s", username.empty() ? "Player" : username.c_str());
        }
    }
    ImGui::End();
}

void renderChat(float uiScale) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float margin = UiScale::Px(18.0f, uiScale);
    const float width = std::clamp(display.x * 0.34f, UiScale::Px(300.0f, uiScale), UiScale::Px(460.0f, uiScale));
    const float height = std::clamp(display.y * 0.30f, UiScale::Px(210.0f, uiScale), UiScale::Px(320.0f, uiScale));

    ImGui::SetNextWindowPos(ImVec2(margin, margin), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    if (ImGui::Begin("ChatHud", nullptr, overlayFlags())) {
        drawLabel("CHAT");
        ImGui::BeginChild("ChatMessages", ImVec2(0, -UiScale::Px(44.0f, uiScale)), false);
        for (const auto& line : chatLines) {
            if (line.system) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.72f, 0.90f, 1.0f));
                ImGui::TextWrapped("%s", line.message.c_str());
                ImGui::PopStyleColor();
            } else {
                ImGui::TextWrapped("%s: %s", line.username.c_str(), line.message.c_str());
            }
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();

        ImGui::SetNextItemWidth(-UiScale::Px(72.0f, uiScale));
        bool enterPressed = ImGui::InputText("##chatInput", chatInput, sizeof(chatInput), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (modernButton("Send", ImVec2(UiScale::Px(62.0f, uiScale), 0), ImVec4(0.10f, 0.50f, 0.95f, 1.0f)) || enterPressed) {
            sendChatMessage();
        }
    }
    ImGui::End();
}

void renderBottomHud(float uiScale) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float width = std::min(display.x - UiScale::Px(32.0f, uiScale), UiScale::Px(560.0f, uiScale));
    const float height = UiScale::Px(138.0f, uiScale);
    ImGui::SetNextWindowPos(ImVec2((display.x - width) * 0.5f, display.y - height - UiScale::Px(18.0f, uiScale)), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    if (ImGui::Begin("BottomHud", nullptr, overlayFlags())) {
        const float slotSize = std::min(UiScale::Px(62.0f, uiScale), (width - UiScale::Px(72.0f, uiScale)) / 5.0f);
        const float startX = (width - (slotSize * 5.0f + UiScale::Px(8.0f, uiScale) * 4.0f)) * 0.5f;
        ImGui::SetCursorPosX(startX);
        for (int i = 0; i < 5; ++i) {
            ImGui::PushID(i);
            modernButton(std::to_string(i + 1).c_str(), ImVec2(slotSize, slotSize), ImVec4(0.095f, 0.105f, 0.135f, 1.0f));
            ImGui::PopID();
            if (i < 4) ImGui::SameLine();
        }

        ImGui::Spacing();
        float health = myCharacter ? myCharacter->health : 100.0f;
        float maxHealth = myCharacter ? myCharacter->maxHealth : 100.0f;
        const float fraction = maxHealth > 0.0f ? std::clamp(health / maxHealth, 0.0f, 1.0f) : 0.0f;
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, fraction > 0.35f ? ImVec4(0.10f, 0.82f, 0.46f, 1.0f) : ImVec4(0.95f, 0.28f, 0.22f, 1.0f));
        char label[64];
        std::snprintf(label, sizeof(label), "Health %.0f / %.0f", health, maxHealth);
        ImGui::ProgressBar(fraction, ImVec2(-1, UiScale::Px(24.0f, uiScale)), label);
        ImGui::PopStyleColor();
    }
    ImGui::End();
}

void renderGameHud(std::deque<Part>& parts, PhysicsWorld& physicsWorld, float uiScale) {
    if ((currentState != ONLINE && currentState != OFFLINE) || !myCharacter) return;
    renderChat(uiScale);
    if (currentState == ONLINE && isConnected) {
        renderPlayerList(uiScale);
    }
    renderTopBar(parts, physicsWorld, uiScale);
    renderBottomHud(uiScale);
}

// Windows File Dialog
std::string openFileDialog(GLFWwindow* window) {
    OPENFILENAMEA ofn;
    char szFile[260] = {0};
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = glfwGetWin32Window(window);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "World Files\0*.world\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        return std::string(ofn.lpstrFile);
    }
    return "";
}

void copyArgument(char* destination, size_t destinationSize, const char* value) {
    if (!destination || destinationSize == 0 || !value) return;
    std::memset(destination, 0, destinationSize);
    std::strncpy(destination, value, destinationSize - 1);
}

void parseClientArguments(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        if (arg == "--server" && i + 1 < argc) {
            copyArgument(serverIP, sizeof(serverIP), argv[++i]);
        } else if (arg == "--port" && i + 1 < argc) {
            int parsed = std::atoi(argv[++i]);
            if (parsed > 0 && parsed <= 65535) {
                serverPort = parsed;
            }
        } else if (arg == "--token" && i + 1 < argc) {
            copyArgument(authToken, sizeof(authToken), argv[++i]);
            currentState = MENU_PLAY_CHOICE;
        } else if (arg == "--web" && i + 1 < argc) {
            copyArgument(webServerUrl, sizeof(webServerUrl), argv[++i]);
        } else if (arg == "--game" && i + 1 < argc) {
            launchGameId = std::max(0, std::atoi(argv[++i]));
        } else if (arg == "--connect") {
            autoConnectOnStart = true;
            currentState = MENU_PLAY_CHOICE;
        }
    }
}

bool connectToConfiguredServer() {
    clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == INVALID_SOCKET) {
        showLoginError = true;
        std::strncpy(loginErrorMsg, "Could not create network socket.", sizeof(loginErrorMsg) - 1);
        return false;
    }

    sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    if (inet_pton(AF_INET, serverIP, &serverAddr.sin_addr) != 1) {
        showLoginError = true;
        std::strncpy(loginErrorMsg, "Invalid server address.", sizeof(loginErrorMsg) - 1);
        closesocket(clientSocket);
        clientSocket = INVALID_SOCKET;
        return false;
    }
    serverAddr.sin_port = htons(static_cast<u_short>(serverPort));

    if (connect(clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        showLoginError = true;
        std::strncpy(loginErrorMsg, "Connection failed.", sizeof(loginErrorMsg) - 1);
        closesocket(clientSocket);
        clientSocket = INVALID_SOCKET;
        return false;
    }

    isConnected = true;
    currentState = ONLINE;
    showConnectionUI = false;
    Network::setNonBlocking(clientSocket);

    PacketConnect pkt;
    std::memset(&pkt, 0, sizeof(pkt));

    if (std::strlen(authToken) > 0) {
        std::strncpy(pkt.username, myUsername, sizeof(pkt.username) - 1);
        std::strncpy(pkt.authToken, authToken, sizeof(pkt.authToken) - 1);
    } else {
        std::strncpy(pkt.username, "Guest", sizeof(pkt.username) - 1);
    }

    if (!Network::sendStructPacket(clientSocket, PacketType::CONNECT, pkt)) {
        showLoginError = true;
        std::strncpy(loginErrorMsg, "Failed to send connect packet.", sizeof(loginErrorMsg) - 1);
        isConnected = false;
        closesocket(clientSocket);
        clientSocket = INVALID_SOCKET;
        currentState = MENU_PLAY_CHOICE;
        showConnectionUI = true;
        return false;
    }

    std::cout << "Connected to " << serverIP << ":" << serverPort;
    if (launchGameId > 0) {
        std::cout << " for game " << launchGameId;
    }
    std::cout << ". Waiting for world state from server..." << std::endl;
    return true;
}

void mouse_callback(GLFWwindow* window, double xposIn, double yposIn) {
    if (ImGui::GetIO().WantCaptureMouse) return;
    
    // Right Click to Look
    bool isRightClickHeld = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    
    if (isRightClickHeld) {
        if (!isCursorLocked) {
            isCursorLocked = true;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            firstMouse = true; // Reset on re-engage
        }
    } else {
        if (isCursorLocked) {
            isCursorLocked = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
        return; // Don't process mouse move for camera if not locked
    }

    float xpos = static_cast<float>(xposIn);
    float ypos = static_cast<float>(yposIn);

    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos; 

    lastX = xpos;
    lastY = ypos;

    camera.ProcessMouseMovement(xoffset, yoffset);
}

int main(int argc, char** argv) {
    // Redirect stdout/stderr to files for debugging
    freopen("client_log.txt", "w", stdout);
    freopen("client_error_log.txt", "w", stderr);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    try {
        std::cout << "Starting Client..." << std::endl;
        
        FILE* f = fopen("shaders/vertex.glsl", "r");
        if (!f) {
             std::cout << "CRITICAL ERROR: shaders/vertex.glsl not found in current directory!" << std::endl;
             std::cout << "Please ensure the 'shaders' folder is next to the executable." << std::endl;
             system("pause");
             return -1;
        }
        fclose(f);

        Window window(SCR_WIDTH, SCR_HEIGHT, "RBLX Clone Client");
        std::cout << "Window Created." << std::endl;
        glfwSetCursorPosCallback(window.getNativeWindow(), mouse_callback);
        
        // Setup ImGui Standard Init
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        ImGui::StyleColorsDark();
        applyModernPlayerStyle();
        const float uiScale = UiScale::Apply(window.getNativeWindow());
        ImGui_ImplGlfw_InitForOpenGL(window.getNativeWindow(), true);
        ImGui_ImplOpenGL3_Init("#version 330");
        std::cout << "ImGui Initialized." << std::endl;

        Renderer renderer;
        renderer.init();
        std::cout << "Renderer Initialized." << std::endl;

        // Skybox skybox; 
        // skybox.init();
        Skybox skybox;
        skybox.init();
        std::cout << "Skybox Initialized." << std::endl;

        PhysicsWorld physicsWorld;
        physicsWorld.init();
        std::cout << "PhysicsWorld Initialized." << std::endl;
        
        ShadowMap shadowMap;
        shadowMap.init();
        std::cout << "ShadowMap Initialized." << std::endl;

        std::deque<Part> parts;

        Network::init();
        std::cout << "Network Initialized." << std::endl;
        
        // Load saved token on startup
        std::string savedToken;
        std::ifstream tokenFile("auth_token.txt");
        if (tokenFile.is_open()) {
            std::getline(tokenFile, savedToken);
            tokenFile.close();
        }

        parseClientArguments(argc, argv);
        const bool hasLaunchToken = std::strlen(authToken) > 0;
        
        // Load saved avatar (will be fetched from server when logged in)
        currentAvatar.loadFromFile("avatar.txt");
        if (hasLaunchToken) {
            std::string authError;
            if (applyAuthSession(authToken, "Player", automaticWebServerUrl(), false, authError)) {
                currentState = MENU_PLAY_CHOICE;
            } else {
                addChatLine("System", authError, true);
            }
        } else if (!savedToken.empty()) {
            std::string authError;
            if (applyAuthSession(savedToken, "Player", automaticWebServerUrl(), false, authError)) {
                currentState = MENU_PLAY_CHOICE;
            } else {
                authToken[0] = '\0';
                currentState = MENU_AUTH_CHOICE;
                addChatLine("System", authError, true);
            }
        }
        
        loadFaceTextures();

        if (autoConnectOnStart) {
            std::string connectError;
            if (!connectToServer(connectError)) {
                showLoginError = true;
                copyToBuffer(loginErrorMsg, sizeof(loginErrorMsg), connectError);
            }
        }

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        std::cout << "Starting Main Loop..." << std::endl;

        int frameCount = 0;
        while (!window.shouldClose()) {
            // 1. Poll Events
            window.pollEvents();

            float currentFrame = static_cast<float>(glfwGetTime());
            deltaTime = currentFrame - lastFrame;
            lastFrame = currentFrame;

            if (glfwGetKey(window.getNativeWindow(), GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                if (isCursorLocked) {
                    isCursorLocked = false;
                    glfwSetInputMode(window.getNativeWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                }
            }

            // 2. ImGui NewFrame
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            if (frameCount < 5) std::cout << "Frame " << frameCount << " ImGui NewFrame" << std::endl;

            // 3. Logic / UI
            pollAuthCallback();

            static bool wasDebugToggleDown = false;
            const bool debugToggleDown = glfwGetKey(window.getNativeWindow(), GLFW_KEY_F3) == GLFW_PRESS;
            if (debugToggleDown && !wasDebugToggleDown) {
                showDebugGui = !showDebugGui;
            }
            wasDebugToggleDown = debugToggleDown;

            renderDebugWindow(io);
            if (currentState == MENU_AUTH_CHOICE || currentState == MENU) {
                renderAuthMenu(uiScale);
            } else if (currentState == MENU_PLAY_CHOICE) {
                renderPlayMenu(window.getNativeWindow(), parts, physicsWorld, uiScale);
            }
            renderGameHud(parts, physicsWorld, uiScale);

            // ... Network & Physics Logic (Keep logic running)
            if (isConnected && clientSocket != INVALID_SOCKET) {
                char buffer[4096];
                int bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0);
                
                if (bytesReceived > 0) {
                    clientReceiveBuffer.insert(clientReceiveBuffer.end(), buffer, buffer + bytesReceived);

                    std::vector<char> completeFrames;
                    while (true) {
                        PacketProtocol::ParsedPacket frame;
                        PacketProtocol::ParseStatus status = PacketProtocol::tryReadFrame(clientReceiveBuffer, frame);
                        if (status == PacketProtocol::ParseStatus::NeedMoreData) {
                            break;
                        }
                        if (status == PacketProtocol::ParseStatus::Invalid) {
                            std::cerr << "Disconnected after receiving malformed packet from server" << std::endl;
                            isConnected = false;
                            closesocket(clientSocket);
                            clientSocket = INVALID_SOCKET;
                            currentState = MENU_AUTH_CHOICE;
                            showConnectionUI = false;
                            clientReceiveBuffer.clear();
                            break;
                        }

                        completeFrames.insert(
                            completeFrames.end(),
                            clientReceiveBuffer.begin(),
                            clientReceiveBuffer.begin() + static_cast<std::ptrdiff_t>(frame.frameSize)
                        );
                        PacketProtocol::consumeFrame(clientReceiveBuffer, frame.frameSize);
                    }

                    char* packetBuffer = completeFrames.empty() ? nullptr : completeFrames.data();
                    int bytesToProcess = static_cast<int>(completeFrames.size());

                    // Limit logging to prevent spam
                    static int logCounter = 0;
                    if (logCounter++ % 60 == 0) {
                        std::cout << "Recv bytes: " << bytesReceived << std::endl;
                    }
                    int offset = 0;
                    while (offset < bytesToProcess) {
                        if (offset + sizeof(PacketHeader) > bytesToProcess) break; // Safety check
                        
                        PacketHeader* header = (PacketHeader*)(packetBuffer + offset);
                        uint32_t packetSize = sizeof(PacketHeader) + header->size;
                        
                        // Safety check: ensure entire packet fits in buffer
                        if (offset + packetSize > bytesToProcess) {
                            std::cerr << "Warning: Incomplete packet, waiting for more data" << std::endl;
                            break; // Wait for more data
                        }
                        
                        // Limit logging to prevent spam
                        if (logCounter % 60 == 0) {
                            std::cout << "Packet Type: " << (int)header->type << " Size: " << header->size << std::endl;
                        }

                        if (header->type == PacketType::WELCOME) {
                            if (header->size < sizeof(PacketWelcome)) {
                                std::cerr << "ERROR: Invalid WELCOME packet size" << std::endl;
                                offset += packetSize;
                                continue;
                            }
                            PacketWelcome* pkt = (PacketWelcome*)(packetBuffer + offset + sizeof(PacketHeader));
                             myPlayerId = pkt->playerId;
                             std::cout << "Joined with ID: " << myPlayerId << std::endl;
                             
                             // Don't create character yet - wait for WORLD_STATE to load all parts first
                             // Character will be created after WORLD_STATE is received
                             
                             // Don't lock cursor on join, let user right click
                             isCursorLocked = false;
                             glfwSetInputMode(window.getNativeWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                        }
                        // ... (rest of packet handlers)
                        else if (header->type == PacketType::PLAYER_JOIN) {
                            if (header->size < sizeof(PacketPlayerJoin)) {
                                std::cerr << "ERROR: Invalid PLAYER_JOIN packet size" << std::endl;
                                offset += packetSize;
                                continue;
                            }
                            PacketPlayerJoin* pkt = (PacketPlayerJoin*)(packetBuffer + offset + sizeof(PacketHeader));
                            
                            // Validate player ID
                            if (pkt->playerId < 0 || pkt->playerId > 1000) {
                                std::cerr << "ERROR: Invalid player ID in PLAYER_JOIN: " << pkt->playerId << std::endl;
                                offset += packetSize;
                                continue;
                            }
                            
                            if (pkt->playerId != myPlayerId) {
                                std::cout << "Remote Player Joined: " << pkt->username << " (ID: " << pkt->playerId << ")" << std::endl;
                                playerNames[pkt->playerId] = pkt->username;
                                
                                // Only create if doesn't exist (prevent duplicate/crash)
                                if (remotePlayers.count(pkt->playerId) == 0) {
                                    // Create Remote Character with error handling
                                    glm::vec3 spawnPos(0, 10, 0);
                                    for(const auto& p : parts) { 
                                        if(p.isSpawn) {
                                            spawnPos = p.position + glm::vec3(0, 5, 0);
                                            break;
                                        }
                                    }
                                    
                                    std::cout << "Creating remote character for player " << pkt->playerId << std::endl;
                                    
                                    // Safety check: ensure parts list is valid
                                    if (parts.empty()) {
                                        std::cerr << "ERROR: Parts list is empty, cannot create character" << std::endl;
                                        offset += packetSize;
                                        continue;
                                    }
                                    
                                    // Default avatar colors
                                    glm::vec3 headColor(0.8f, 0.6f, 0.4f);
                                    glm::vec3 torsoColor(0.2f, 0.4f, 0.8f);
                                    glm::vec3 leftArmColor(0.8f, 0.6f, 0.4f);
                                    glm::vec3 rightArmColor(0.8f, 0.6f, 0.4f);
                                    glm::vec3 leftLegColor(0.2f, 0.6f, 0.2f);
                                    glm::vec3 rightLegColor(0.2f, 0.6f, 0.2f);
                                    std::string faceId = DEFAULT_FACE_ID;
                                    
                                    // Find player in playerList for avatar colors
                                    bool foundAvatar = false;
                                    for (const auto& playerInfo : playerList) {
                                        if (playerInfo.playerId == pkt->playerId) {
                                            headColor = glm::vec3(playerInfo.headColor[0], playerInfo.headColor[1], playerInfo.headColor[2]);
                                            torsoColor = glm::vec3(playerInfo.torsoColor[0], playerInfo.torsoColor[1], playerInfo.torsoColor[2]);
                                            leftArmColor = glm::vec3(playerInfo.leftArmColor[0], playerInfo.leftArmColor[1], playerInfo.leftArmColor[2]);
                                            rightArmColor = glm::vec3(playerInfo.rightArmColor[0], playerInfo.rightArmColor[1], playerInfo.rightArmColor[2]);
                                            leftLegColor = glm::vec3(playerInfo.leftLegColor[0], playerInfo.leftLegColor[1], playerInfo.leftLegColor[2]);
                                            rightLegColor = glm::vec3(playerInfo.rightLegColor[0], playerInfo.rightLegColor[1], playerInfo.rightLegColor[2]);
                                            faceId = normalizeFaceId(PacketProtocol::fixedString(playerInfo.faceId, sizeof(playerInfo.faceId)));
                                            foundAvatar = true;
                                            break;
                                        }
                                    }
                                    
                                    // If avatar not found in player list, use defaults (will be updated when player list arrives)
                                    if (!foundAvatar) {
                                        std::cout << "Avatar data not found for player " << pkt->playerId << ", using defaults (will update when player list arrives)" << std::endl;
                                    }
                                    
                                    // Double-check parts list is still valid and not empty
                                    if (parts.empty()) {
                                        std::cerr << "ERROR: Parts list became empty before character creation for player " << pkt->playerId << std::endl;
                                        offset += packetSize;
                                        continue;
                                    }
                                    
                                    Character* remoteChar = nullptr;
                                    try {
                                        remoteChar = new Character(spawnPos, &physicsWorld, &parts,
                                            headColor, torsoColor, leftArmColor, rightArmColor, leftLegColor, rightLegColor);
                                        
                                        if (remoteChar) {
                                            remoteChar->setFaceTexture(textureForFace(faceId));
                                            remoteChar->setRemoteControlled(true);
                                            remotePlayers[pkt->playerId] = remoteChar;
                                            std::cout << "Successfully created remote character for player " << pkt->playerId << " with avatar colors" << std::endl;
                                        } else {
                                            std::cerr << "ERROR: Character creation returned null for player " << pkt->playerId << std::endl;
                                        }
                                    } catch (const std::bad_alloc& e) {
                                        std::cerr << "ERROR: Memory allocation failed creating remote character for player " << pkt->playerId << ": " << e.what() << std::endl;
                                    } catch (const std::exception& e) {
                                        std::cerr << "ERROR: Exception creating remote character for player " << pkt->playerId << ": " << e.what() << std::endl;
                                    } catch (...) {
                                        std::cerr << "ERROR: Unknown exception creating remote character for player " << pkt->playerId << std::endl;
                                    }
                                } else {
                                    std::cout << "Warning: Remote character for player " << pkt->playerId << " already exists, skipping creation" << std::endl;
                                }
                            } else {
                                std::cout << "Received PLAYER_JOIN for myself (ID: " << pkt->playerId << "), ignoring" << std::endl;
                            }
                        }
                        else if (header->type == PacketType::PLAYER_LEAVE) {
                            if (header->size < sizeof(PacketPlayerLeave)) {
                                std::cerr << "ERROR: Invalid PLAYER_LEAVE packet size" << std::endl;
                                offset += packetSize;
                                continue;
                            }
                            PacketPlayerLeave* pkt = (PacketPlayerLeave*)(packetBuffer + offset + sizeof(PacketHeader));
                            
                            // Safety check: don't remove local player's character (shouldn't be in remotePlayers anyway)
                            if (pkt->playerId == myPlayerId) {
                                std::cout << "Received PLAYER_LEAVE for myself (ID: " << pkt->playerId << "), ignoring" << std::endl;
                                offset += packetSize;
                                continue;
                            }
                            
                            if (remotePlayers.count(pkt->playerId)) {
                                Character* c = remotePlayers[pkt->playerId];
                                if (c) {
                                    // Use the Character's method to get its folder index
                                    int playerFolderIndex = c->getFolderIndex();
                                    
                                    std::vector<int> partsToRemove;
                                    
                                    // Add the folder itself
                                    if (playerFolderIndex >= 0 && playerFolderIndex < parts.size()) {
                                        // Verify this is actually a Player folder
                                        if (parts[playerFolderIndex].name == "Player" && parts[playerFolderIndex].isFolder) {
                                            partsToRemove.push_back(playerFolderIndex);
                                            
                                            // Find all parts with this folder as parent
                                            for (int j = 0; j < parts.size(); ++j) {
                                                if (parts[j].parentIndex == playerFolderIndex && !parts[j].deleted) {
                                                    partsToRemove.push_back(j);
                                                }
                                            }
                                        } else {
                                            std::cerr << "WARNING: getFolderIndex returned invalid folder for player " << pkt->playerId << std::endl;
                                        }
                                    }
                                    
                                    // Mark parts as deleted instead of removing (prevents index shifts)
                                    for (int idx : partsToRemove) {
                                        if (idx >= 0 && idx < parts.size()) {
                                            Part& p = parts[idx];
                                            if (p.deleted) continue; // Already deleted
                                            
                                            // Remove from physics world if it has a body
                                            if (p.physicsBody) {
                                                physicsWorld.removePart(&p);
                                            }
                                            // Mark as deleted instead of erasing
                                            p.deleted = true;
                                            p.transparency = 1.0f; // Make invisible
                                            p.anchored = true; // Stop physics
                                            p.canCollide = false;
                                        }
                                    }
                                    
                                    delete c; // Properly delete the character
                                }
                                remotePlayers.erase(pkt->playerId);
                                playerNames.erase(pkt->playerId);
                                std::cout << "Player " << pkt->playerId << " left, removed character and parts" << std::endl;
                            } else {
                                std::cout << "Player " << pkt->playerId << " left, but no remote character found (already removed?)" << std::endl;
                            }
                        }
                        else if (header->type == PacketType::PLAYER_STATE) {
                            if (header->size < sizeof(PacketPlayerState)) {
                                std::cerr << "ERROR: Invalid PLAYER_STATE packet size" << std::endl;
                                offset += packetSize;
                                continue;
                            }
                            PacketPlayerState* pkt = (PacketPlayerState*)(packetBuffer + offset + sizeof(PacketHeader));
                            if (pkt->playerId != myPlayerId && remotePlayers.count(pkt->playerId)) {
                                try {
                                    Character* remoteChar = remotePlayers[pkt->playerId];
                                    if (remoteChar) {
                                        remoteChar->setRemoteState(pkt->position, pkt->rotationY, pkt->isWalking, pkt->isJumping);
                                        remoteChar->health = pkt->health;
                                    }
                                } catch (...) {
                                    std::cerr << "ERROR: Exception updating remote character state for player " << pkt->playerId << std::endl;
                                }
                            }
                        }
                        else if (header->type == PacketType::CHAT) {
                            if (header->size < sizeof(PacketChat)) {
                                std::cerr << "ERROR: Invalid CHAT packet size" << std::endl;
                                offset += packetSize;
                                continue;
                            }

                            PacketChat* pkt = (PacketChat*)(packetBuffer + offset + sizeof(PacketHeader));
                            const std::string username = PacketProtocol::fixedString(pkt->username, sizeof(pkt->username));
                            const std::string message = PacketProtocol::fixedString(pkt->message, sizeof(pkt->message));
                            addChatLine(username.empty() ? "Player" : username, message);
                        }
                        else if (header->type == PacketType::PLAYER_LIST) {
                            // Player list from server
                            if (header->size < sizeof(uint32_t)) {
                                std::cerr << "ERROR: Invalid PLAYER_LIST packet size" << std::endl;
                                offset += packetSize;
                                continue;
                            }
                            
                            // Read player count
                            uint32_t playerCount = *(uint32_t*)(packetBuffer + offset + sizeof(PacketHeader));
                            int expectedSize = sizeof(uint32_t) + playerCount * sizeof(PlayerInfo);
                            
                            if (header->size != expectedSize) {
                                std::cerr << "ERROR: PLAYER_LIST size mismatch. Expected " << expectedSize 
                                          << ", got " << header->size << std::endl;
                                offset += packetSize;
                                continue;
                            }
                            
                            playerList.clear();
                            if (playerCount > 0 && playerCount < 100) { // Safety limit
                                PlayerInfo* players = (PlayerInfo*)(packetBuffer + offset + sizeof(PacketHeader) + sizeof(uint32_t));
                                for (uint32_t i = 0; i < playerCount; i++) {
                                    playerList.push_back(players[i]);
                                    playerNames[players[i].playerId] = players[i].username;
                                    const std::string faceId = normalizeFaceId(PacketProtocol::fixedString(players[i].faceId, sizeof(players[i].faceId)));
                                    
                                    // Apply avatar colors to remote character if it exists
                                    if (remotePlayers.count(players[i].playerId) > 0) {
                                        Character* remoteChar = remotePlayers[players[i].playerId];
                                        // Update part colors
                                        if (remoteChar && !parts.empty()) {
                                            try {
                                                int folderIndex = remoteChar->getFolderIndex();
                                                if (folderIndex >= 0 && folderIndex < (int)parts.size()) {
                                                    // Find character parts and update colors
                                                    for (size_t j = 0; j < parts.size(); j++) {
                                                        Part& p = parts[j];
                                                        if (p.parentIndex == folderIndex && !p.deleted) {
                                                            if (p.name == "Head") {
                                                                p.color = glm::vec3(players[i].headColor[0], players[i].headColor[1], players[i].headColor[2]);
                                                                p.textureId = textureForFace(faceId);
                                                            } else if (p.name == "Torso") {
                                                                p.color = glm::vec3(players[i].torsoColor[0], players[i].torsoColor[1], players[i].torsoColor[2]);
                                                            } else if (p.name == "LeftArm") {
                                                                p.color = glm::vec3(players[i].leftArmColor[0], players[i].leftArmColor[1], players[i].leftArmColor[2]);
                                                            } else if (p.name == "RightArm") {
                                                                p.color = glm::vec3(players[i].rightArmColor[0], players[i].rightArmColor[1], players[i].rightArmColor[2]);
                                                            } else if (p.name == "LeftLeg") {
                                                                p.color = glm::vec3(players[i].leftLegColor[0], players[i].leftLegColor[1], players[i].leftLegColor[2]);
                                                            } else if (p.name == "RightLeg") {
                                                                p.color = glm::vec3(players[i].rightLegColor[0], players[i].rightLegColor[1], players[i].rightLegColor[2]);
                                                            }
                                                        }
                                                    }
                                                    std::cout << "Updated avatar colors for player " << players[i].playerId << std::endl;
                                                }
                                            } catch (...) {
                                                std::cerr << "ERROR: Exception updating avatar colors for player " << players[i].playerId << std::endl;
                                            }
                                        }
                                    }
                                }
                                std::cout << "Received player list: " << playerCount << " players" << std::endl;
                            }
                        }
                        else if (header->type == PacketType::WORLD_STATE) {
                            // Complete world state from server - load all parts
                            if (header->size < sizeof(uint32_t)) {
                                std::cerr << "ERROR: Invalid WORLD_STATE packet size" << std::endl;
                                offset += packetSize;
                                continue;
                            }
                            
                            // Read part count
                            uint32_t partCount = *(uint32_t*)(packetBuffer + offset + sizeof(PacketHeader));
                            int expectedSize = sizeof(uint32_t) + partCount * sizeof(PartData);
                            
                            if (header->size != expectedSize) {
                                std::cerr << "ERROR: WORLD_STATE size mismatch. Expected " << expectedSize 
                                          << " but got " << header->size << std::endl;
                                offset += packetSize;
                                continue;
                            }
                            
                            // CRITICAL: Delete all characters BEFORE resetting physics world
                            // Otherwise character physics bodies will be invalid
                            std::cout << "WORLD_STATE received - cleaning up before reset" << std::endl;
                            
                            if (myCharacter) {
                                std::cout << "Deleting my character before physics reset" << std::endl;
                                delete myCharacter;
                                myCharacter = nullptr;
                            }
                            
                            for (auto& [id, remoteChar] : remotePlayers) {
                                if (remoteChar) {
                                    std::cout << "Deleting remote character " << id << " before physics reset" << std::endl;
                                    delete remoteChar;
                                }
                            }
                            remotePlayers.clear();
                            
                            // Clear existing parts
                            std::cout << "Resetting physics world" << std::endl;
                            physicsWorld.reset();
                            parts.clear();
                            std::cout << "Physics world reset complete" << std::endl;
                            
                            // Read all parts
                            PartData* partData = (PartData*)(packetBuffer + offset + sizeof(PacketHeader) + sizeof(uint32_t));
                            
                            for (uint32_t i = 0; i < partCount; ++i) {
                                PartData& data = partData[i];
                                
                                Part p;
                                p.shape = (ShapeType)data.shape;
                                p.name = std::string(data.name);
                                p.parentIndex = data.parentIndex;
                                
                                // Unpack flags
                                p.isFolder = (data.flags & 1) != 0;
                                p.isCamera = (data.flags & 2) != 0;
                                p.isSpawn = (data.flags & 4) != 0;
                                
                                p.position = data.position;
                                p.size = data.size;
                                p.color = data.color;
                                p.rotation = data.rotation;
                                p.transparency = data.transparency;
                                p.reflectance = data.reflectance;
                                
                                // Unpack physics flags
                                p.anchored = (data.physicsFlags & 1) != 0;
                                p.canCollide = (data.physicsFlags & 2) != 0;
                                
                                p.mass = data.mass;
                                p.deleted = false;
                                
                                parts.push_back(p);
                                
                                // Add to physics world if not a folder
                                if (!p.isFolder) {
                                    physicsWorld.addPart(&parts.back());
                                }
                            }
                            
                            std::cout << "Loaded complete world state from server: " << partCount << " parts" << std::endl;
                            
                            // Now that world is loaded, create the character
                            if (myPlayerId != -1 && !myCharacter) {
                                // Find spawn
                                glm::vec3 spawnPos(0, 10, 0);
                                for(const auto& p : parts) { 
                                    if(p.isSpawn) {
                                        spawnPos = p.position + glm::vec3(0, 5, 0);
                                        break;
                                    }
                                }
                                
                                std::cout << "Creating Character at: " << spawnPos.x << ", " << spawnPos.y << ", " << spawnPos.z << std::endl;
                                myCharacter = new Character(spawnPos, &physicsWorld, &parts,
                                    currentAvatar.headColor, currentAvatar.torsoColor,
                                    currentAvatar.leftArmColor, currentAvatar.rightArmColor,
                                    currentAvatar.leftLegColor, currentAvatar.rightLegColor);
                                
                                // Immediately update character to position all parts correctly
                                if (myCharacter) {
                                    myCharacter->setFaceTexture(textureForFace(currentAvatar.faceId));
                                    myCharacter->update(0.0f, nullptr, false);
                                }
                            }
                        }
                        else if (header->type == PacketType::WORLD_UPDATE) {
                            if (header->size % sizeof(PartUpdate) != 0) {
                                std::cerr << "ERROR: Invalid WORLD_UPDATE packet size (not multiple of PartUpdate)" << std::endl;
                                offset += packetSize;
                                continue;
                            }
                            int numUpdates = header->size / sizeof(PartUpdate);
                            PartUpdate* updates = (PartUpdate*)(packetBuffer + offset + sizeof(PacketHeader));
                            float now = (float)glfwGetTime();
                            for(int i=0; i<numUpdates; ++i) {
                                PartUpdate& up = updates[i];
                                if (up.partIndex >= 0 && up.partIndex < parts.size()) {
                                    Part& p = parts[up.partIndex];
                                    
                                    // SAFETY: Validate server position
                                    if (std::isnan(up.position.x) || std::isnan(up.position.y) || std::isnan(up.position.z) ||
                                        std::isinf(up.position.x) || std::isinf(up.position.y) || std::isinf(up.position.z)) {
                                        continue; // Skip invalid updates
                                    }
                                    
                                    // Check if this is a player part (don't apply server updates to player parts)
                                    bool isPlayerPart = false;
                                    if (p.parentIndex >= 0 && p.parentIndex < parts.size()) {
                                        if (parts[p.parentIndex].name == "Player") {
                                            isPlayerPart = true;
                                        }
                                    }
                                    
                                    // Server is fully authoritative for world parts
                                    if (!isPlayerPart && p.physicsBody && !p.anchored) {
                                        rp3d::RigidBody* body = (rp3d::RigidBody*)p.physicsBody;
                                        
                                        // CRITICAL: Make world parts KINEMATIC immediately
                                        // This prevents ANY local physics simulation
                                        body->setType(rp3d::BodyType::KINEMATIC);
                                        
                                        // Apply server position IMMEDIATELY - server is source of truth
                                        rp3d::Vector3 serverPos(up.position.x, up.position.y, up.position.z);
                                        rp3d::Quaternion serverRot = rp3d::Quaternion::fromEulerAngles(
                                            glm::radians(up.rotation.x),
                                            glm::radians(up.rotation.y),
                                            glm::radians(up.rotation.z)
                                        );
                                        
                                        rp3d::Transform serverTransform(serverPos, serverRot);
                                        body->setTransform(serverTransform);
                                        body->setLinearVelocity(rp3d::Vector3(0, 0, 0));
                                        body->setAngularVelocity(rp3d::Vector3(0, 0, 0));
                                        
                                        // Update part position for rendering - server is truth
                                        p.position = up.position;
                                        p.rotation = up.rotation;
                                    }
                                    
                                    // Store for continuous updates (apply every frame)
                                    NetworkTransform target; 
                                    target.position = up.position; 
                                    target.rotation = up.rotation; 
                                    target.timestamp = now;
                                    targetTransforms[up.partIndex] = target;
                                }
                            }
                        }
                        
                        offset += packetSize; // Use calculated packetSize
                    }
                } 
                else if (bytesReceived == 0 || (bytesReceived == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)) {
                    std::cout << "Disconnected. Error: " << (bytesReceived == 0 ? 0 : WSAGetLastError()) << std::endl;
                    isConnected = false; closesocket(clientSocket); clientSocket = INVALID_SOCKET; currentState = MENU_AUTH_CHOICE; showConnectionUI = false;
                    clientReceiveBuffer.clear();
                }
                // Send state with safety checks
                if (isConnected && clientSocket != INVALID_SOCKET && myCharacter && myPlayerId != -1) {
                    try {
                        PacketPlayerState state;
                        state.playerId = myPlayerId;
                        state.position = myCharacter->getPosition();
                        // Send the actual current yaw (internalYaw) which smoothly interpolates to target
                        // This ensures smooth rotation even during diagonal movement
                        state.rotationY = myCharacter->debugCurrentYaw;
                        state.isWalking = (glm::length(myCharacter->debugMoveDir) > 0.1f);
                        state.isJumping = false;
                        state.health = myCharacter->health;
                        if (!Network::sendStructPacket(clientSocket, PacketType::PLAYER_STATE, state)) {
                            std::cout << "Disconnected while sending player state." << std::endl;
                            isConnected = false;
                            closesocket(clientSocket);
                            clientSocket = INVALID_SOCKET;
                            currentState = MENU_AUTH_CHOICE;
                            showConnectionUI = false;
                            clientReceiveBuffer.clear();
                        }
                    } catch (...) {
                        std::cerr << "ERROR: Exception getting character state" << std::endl;
                    }
                }
            }

            if ((isConnected || currentState == OFFLINE) && myCharacter && !io.WantCaptureKeyboard) {
                 try {
                     bool fwd = glfwGetKey(window.getNativeWindow(), GLFW_KEY_W) == GLFW_PRESS;
                     bool bwd = glfwGetKey(window.getNativeWindow(), GLFW_KEY_S) == GLFW_PRESS;
                     bool l = glfwGetKey(window.getNativeWindow(), GLFW_KEY_A) == GLFW_PRESS;
                     bool r = glfwGetKey(window.getNativeWindow(), GLFW_KEY_D) == GLFW_PRESS;
                     bool j = glfwGetKey(window.getNativeWindow(), GLFW_KEY_SPACE) == GLFW_PRESS;
                     float zoom = 0.0f;
                     if (glfwGetKey(window.getNativeWindow(), GLFW_KEY_UP) == GLFW_PRESS) zoom = 1.0f;
                     if (glfwGetKey(window.getNativeWindow(), GLFW_KEY_DOWN) == GLFW_PRESS) zoom = -1.0f;
                     myCharacter->processInput(fwd, bwd, l, r, j, camera, zoom, deltaTime);
                 } catch (...) {
                     std::cerr << "ERROR: Exception processing character input" << std::endl;
                 }
            }
            // Update local character with safety checks
            if (myCharacter) {
                try {
                    myCharacter->update(deltaTime, &camera, false);
                } catch (...) {
                    std::cerr << "ERROR: Exception updating local character" << std::endl;
                    delete myCharacter;
                    myCharacter = nullptr;
                }
            }
            
            // Update remote characters with safety checks
            for (auto it = remotePlayers.begin(); it != remotePlayers.end();) {
                auto& [id, remoteChar] = *it;
                if (remoteChar) {
                    try {
                        remoteChar->update(deltaTime, nullptr, false);
                        ++it;
                    } catch (...) {
                        std::cerr << "ERROR: Exception updating remote character " << id << ", removing" << std::endl;
                        delete remoteChar;
                        it = remotePlayers.erase(it);
                    }
                } else {
                    it = remotePlayers.erase(it);
                }
            }
            
            if (currentState == OFFLINE) {
                // Offline: Full physics simulation
                physicsWorld.update(deltaTime);
            } else if (currentState == ONLINE && isConnected) {
                // Online: Server is fully authoritative
                // FIRST: Ensure ALL world parts are KINEMATIC (even if no recent updates)
                for (auto& p : parts) {
                    if (p.deleted) continue; // Skip deleted parts
                    
                    // Check if this is a player part
                    bool isPlayerPart = false;
                    if (p.parentIndex >= 0 && p.parentIndex < parts.size()) {
                        if (parts[p.parentIndex].name == "Player") {
                            isPlayerPart = true;
                        }
                    }
                    
                    // Force world parts to KINEMATIC - server controls them
                    if (!isPlayerPart && p.physicsBody && !p.anchored) {
                        rp3d::RigidBody* body = (rp3d::RigidBody*)p.physicsBody;
                        if (body->getType() != rp3d::BodyType::KINEMATIC) {
                            body->setType(rp3d::BodyType::KINEMATIC);
                        }
                    }
                }
                
                // SECOND: Apply server updates EVERY FRAME - keep parts in sync
                float currentTime = (float)glfwGetTime();
                for (auto it = targetTransforms.begin(); it != targetTransforms.end();) {
                    int index = it->first;
                    NetworkTransform& target = it->second;
                    
                    float age = currentTime - target.timestamp;
                    if (age > 1.0f) {
                        it = targetTransforms.erase(it);
                        continue;
                    }
                    
                    if (index >= 0 && index < parts.size()) {
                        Part& p = parts[index];
                        if (p.deleted) continue; // Skip deleted parts
                        
                        // Only apply to world parts (not player character parts)
                        bool isPlayerPart = false;
                        if (p.parentIndex >= 0 && p.parentIndex < parts.size()) {
                            if (parts[p.parentIndex].name == "Player") {
                                isPlayerPart = true;
                            }
                        }
                        
                        // Server is fully authoritative for world parts
                        if (!isPlayerPart && p.physicsBody && !p.anchored) {
                            rp3d::RigidBody* body = (rp3d::RigidBody*)p.physicsBody;
                            
                            // FORCE KINEMATIC - server controls this completely
                            body->setType(rp3d::BodyType::KINEMATIC);
                            
                            // Apply server position DIRECTLY every frame
                            rp3d::Vector3 serverPos(target.position.x, target.position.y, target.position.z);
                            rp3d::Quaternion serverRot = rp3d::Quaternion::fromEulerAngles(
                                glm::radians(target.rotation.x),
                                glm::radians(target.rotation.y),
                                glm::radians(target.rotation.z)
                            );
                            
                            rp3d::Transform serverTransform(serverPos, serverRot);
                            body->setTransform(serverTransform);
                            body->setLinearVelocity(rp3d::Vector3(0, 0, 0));
                            body->setAngularVelocity(rp3d::Vector3(0, 0, 0));
                            
                            // Update part position for rendering - server is source of truth
                            p.position = target.position;
                            p.rotation = target.rotation;
                        }
                    }
                    ++it;
                }
                
                // THIRD: Run physics AFTER applying server updates
                // PhysicsWorld::update() will skip KINEMATIC parts, so server positions stay
                physicsWorld.update(deltaTime);
            } else {
                // Menu state - no physics
            }

            // 4. Render Scene
            // Shadow Pass
            glm::vec3 lightPos(20.0f, 50.0f, 20.0f);
            glm::mat4 lightSpaceMatrix = shadowMap.getLightSpaceMatrix(lightPos);

            if (!parts.empty()) {
                shadowMap.bindForWriting();
                for (const auto& part : parts) {
                    if (!part.deleted) {
                        renderer.drawPartShadow(part, lightSpaceMatrix);
                    }
                }
                shadowMap.unbind();
            }

            glViewport(0, 0, window.getWidth(), window.getHeight());
            glClearColor(0.53f, 0.81f, 0.92f, 1.0f); // Sky Blue
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glm::mat4 view = camera.GetViewMatrix();
            glm::mat4 projection = camera.GetProjectionMatrix((float)window.getWidth(), (float)window.getHeight());

            // Draw Skybox
            glDepthMask(GL_FALSE);
            skybox.draw(view, projection);
            glDepthMask(GL_TRUE);

            // Bind Skybox Texture for Parts Reflection (Texture Unit 0)
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_CUBE_MAP, skybox.getTextureID());
            
            if (renderer.shader) {
                 renderer.shader->use();
                 renderer.shader->setInt("skybox", 0); 
                 renderer.shader->setVec3("lightPos", lightPos);
            }

            // Only render if we have parts and camera is valid
            if (!parts.empty() && window.getWidth() > 0 && window.getHeight() > 0) {
                for (const auto& part : parts) {
                    if (!part.deleted) {
                        renderer.drawPart(part, view, projection, lightSpaceMatrix, shadowMap.depthMap, faceTextureId);
                    }
                }
            }
            
            // Cleanup texture state for ImGui
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

            // 5. ImGui Render
            ImGui::Render();
            
            // 6. Render Draw Data
            ImDrawData* drawData = ImGui::GetDrawData();
            if (drawData) {
                ImGui_ImplOpenGL3_RenderDrawData(drawData);
            } else {
                if (frameCount < 5) std::cout << "ImGui DrawData is NULL" << std::endl;
            }

            // 7. Swap Buffers
            window.swapBuffers();
            
            frameCount++;
        }
        
        Network::cleanup();
        return 0;
    } catch (const std::exception& e) {
        std::cout << "CRITICAL ERROR: " << e.what() << std::endl;
        system("pause");
        return -1;
    } catch (...) {
        std::cout << "CRITICAL ERROR: Unknown." << std::endl;
        system("pause");
        return -1;
    }
}
