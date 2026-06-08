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

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

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
char authToken[256] = "";
char loginUsername[32] = "";
char loginPassword[64] = "";
char webServerUrl[128] = "http://localhost:3000";
bool showLoginError = false;
char loginErrorMsg[256] = "";
bool showConnectionUI = false; // Track if connection UI should be shown

std::map<int, Character*> remotePlayers;
std::map<int, std::string> playerNames;
std::vector<PlayerInfo> playerList;
std::vector<char> clientReceiveBuffer;

Character* myCharacter = nullptr;
AvatarConfig currentAvatar;

// Interpolation Struct
struct NetworkTransform {
    glm::vec3 position;
    glm::vec3 rotation; // Euler
    float timestamp;
};

std::map<int, NetworkTransform> targetTransforms;
std::map<int, NetworkTransform> startTransforms;
float interpolationTime = 0.1f; // 100ms buffer

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

int main() {
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
        std::ifstream tokenFile("auth_token.txt");
        if (tokenFile.is_open()) {
            std::string token;
            std::getline(tokenFile, token);
            if (!token.empty()) {
                strncpy(authToken, token.c_str(), 255);
                authToken[255] = '\0';
                // If token exists, skip to play choice menu
                currentState = MENU_PLAY_CHOICE;
            }
            tokenFile.close();
        }
        
        // Load saved avatar (will be fetched from server when logged in)
        currentAvatar.loadFromFile("avatar.txt");
        
        faceTextureId = loadTexture("face.png");
        if (faceTextureId > 0) {
            std::cout << "Face Texture Loaded successfully (ID: " << faceTextureId << ")" << std::endl;
        } else {
            std::cout << "WARNING: Face texture failed to load! Check if face.png exists and is a valid image." << std::endl;
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
            // Debug Info Window
            ImGui::Begin("Debug Info");
            ImGui::Text("FPS: %.1f", io.Framerate);
            ImGui::Separator();
            
            // Camera Info
            if (currentState == ONLINE || currentState == OFFLINE) {
                ImGui::Text("=== CAMERA ===");
                ImGui::Text("Position: (%.2f, %.2f, %.2f)", camera.Position.x, camera.Position.y, camera.Position.z);
                ImGui::Text("Yaw: %.2f", camera.Yaw);
                ImGui::Text("Pitch: %.2f", camera.Pitch);
                ImGui::Separator();
                
                // Player Info
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
                    ImGui::Separator();
                    
                    // Remote Players Info
                    if (!remotePlayers.empty()) {
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

            // First Menu: Choose Login/Register/Guest
            if (currentState == MENU_AUTH_CHOICE) {
                ImGui::SetNextWindowPos(ImVec2(SCR_WIDTH/2 - 200, SCR_HEIGHT/2 - 150), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_Always);
                if (ImGui::Begin("Welcome", NULL, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
                    ImGui::Text("Welcome to RBLX Game Engine!");
                    ImGui::Separator();
                    ImGui::Spacing();
                    
                    if (ImGui::Button("Login", ImVec2(360, 50))) {
                        currentState = MENU; // Show login form
                    }
                    
                    ImGui::Spacing();
                    
                    if (ImGui::Button("Sign Up", ImVec2(360, 50))) {
                        std::string url = std::string(webServerUrl) + "/signup";
                        Auth::openBrowser(url);
                    }
                    
                    ImGui::Spacing();
                    
                    if (ImGui::Button("Play as Guest", ImVec2(360, 50))) {
                        // Skip to play choice menu
                        currentState = MENU_PLAY_CHOICE;
                    }
                }
                ImGui::End();
            }
            
            // Login Form
            if (currentState == MENU) {
                ImGui::SetNextWindowPos(ImVec2(SCR_WIDTH/2 - 200, SCR_HEIGHT/2 - 200), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiCond_Always);
                if (ImGui::Begin("Login", NULL, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
                    ImGui::Text("Login to Your Account");
                    ImGui::Separator();
                    
                    ImGui::InputText("Username", loginUsername, 32);
                    ImGui::InputText("Password", loginPassword, 64, ImGuiInputTextFlags_Password);
                    ImGui::InputText("Web Server URL", webServerUrl, 128);
                    
                    if (showLoginError) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
                        ImGui::Text("%s", loginErrorMsg);
                        ImGui::PopStyleColor();
                    }
                    
                    if (ImGui::Button("Login", ImVec2(180, 40))) {
                        showLoginError = false;
                        std::string response = Auth::login(std::string(loginUsername), std::string(loginPassword), std::string(webServerUrl));
                        if (!response.empty() && response.find("\"success\":true") != std::string::npos) {
                            // Parse token from JSON (simple parsing)
                            size_t tokenPos = response.find("\"token\":\"");
                            if (tokenPos != std::string::npos) {
                                tokenPos += 9; // Skip "token":"
                                size_t tokenEnd = response.find("\"", tokenPos);
                                if (tokenEnd != std::string::npos) {
                                    std::string token = response.substr(tokenPos, tokenEnd - tokenPos);
                                    strncpy(authToken, token.c_str(), 255);
                                    authToken[255] = '\0';
                                    strncpy(myUsername, loginUsername, 31);
                                    myUsername[31] = '\0';
                                    
                                    // Save token to file
                                    std::ofstream tokenFile("auth_token.txt");
                                    if (tokenFile.is_open()) {
                                        tokenFile << token;
                                        tokenFile.close();
                                    }
                                    
                                    // Fetch avatar from server
                                    std::string avatarResponse = Auth::getAvatar(token, std::string(webServerUrl));
                                    if (!avatarResponse.empty() && avatarResponse.find("\"success\":true") != std::string::npos) {
                                        // Parse avatar colors from JSON (simple parsing)
                                        // Format: "headColor":[r,g,b], "torsoColor":[r,g,b], etc.
                                        auto parseColorArray = [&avatarResponse](const std::string& name, glm::vec3& color) {
                                            size_t pos = avatarResponse.find("\"" + name + "\":[");
                                            if (pos != std::string::npos) {
                                                pos = avatarResponse.find("[", pos);
                                                if (pos != std::string::npos) {
                                                    pos++;
                                                    size_t end = avatarResponse.find("]", pos);
                                                    if (end != std::string::npos) {
                                                        std::string colorStr = avatarResponse.substr(pos, end - pos);
                                                        // Parse "r, g, b"
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
                                        
                                        parseColorArray("headColor", currentAvatar.headColor);
                                        parseColorArray("torsoColor", currentAvatar.torsoColor);
                                        parseColorArray("leftArmColor", currentAvatar.leftArmColor);
                                        parseColorArray("rightArmColor", currentAvatar.rightArmColor);
                                        parseColorArray("leftLegColor", currentAvatar.leftLegColor);
                                        parseColorArray("rightLegColor", currentAvatar.rightLegColor);
                                        
                                        // Save to local file as backup
                                        currentAvatar.saveToFile("avatar.txt");
                                    }
                                    
                                    // Move to play choice menu
                                    currentState = MENU_PLAY_CHOICE;
                                }
                            }
                        } else {
                            showLoginError = true;
                            strncpy(loginErrorMsg, "Login failed. Check username/password.", 255);
                        }
                    }
                    
                    ImGui::SameLine();
                    if (ImGui::Button("Back", ImVec2(180, 40))) {
                        currentState = MENU_AUTH_CHOICE;
                    }
                }
                ImGui::End();
            }
            
            // Second Menu: Choose Online/Offline
            if (currentState == MENU_PLAY_CHOICE) {
                ImGui::SetNextWindowPos(ImVec2(SCR_WIDTH/2 - 200, SCR_HEIGHT/2 - 100), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_Always);
                if (ImGui::Begin("Play", NULL, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
                    ImGui::Text("How would you like to play?");
                    ImGui::Separator();
                    ImGui::Spacing();
                    
                    if (ImGui::Button("Play Online", ImVec2(360, 50))) {
                        // Show server IP input and connect
                        showConnectionUI = true;
                    }
                    
                    ImGui::Spacing();
                    
                    if (ImGui::Button("Play Offline", ImVec2(360, 50))) {
                        std::string path = openFileDialog(window.getNativeWindow());
                        if (!path.empty()) {
                            WorldLoader::loadWorld(path, parts, physicsWorld);
                            if (myCharacter) delete myCharacter;
                            glm::vec3 spawnPos(0, 10, 0);
                            for(const auto& p : parts) { if(p.isSpawn) spawnPos = p.position + glm::vec3(0, 2, 0); }
                            myCharacter = new Character(spawnPos, &physicsWorld, &parts,
                                currentAvatar.headColor, currentAvatar.torsoColor,
                                currentAvatar.leftArmColor, currentAvatar.rightArmColor,
                                currentAvatar.leftLegColor, currentAvatar.rightLegColor);
                            currentState = OFFLINE;
                            lastFrame = static_cast<float>(glfwGetTime()); 
                        }
                    }
                    
                    ImGui::Spacing();
                    if (ImGui::Button("Back", ImVec2(360, 30))) {
                        currentState = MENU_AUTH_CHOICE;
                    }
                }
                ImGui::End();
            }
            
            // Connection UI (shown when "Play Online" is clicked from MENU_PLAY_CHOICE)
            if (showConnectionUI) {
                ImGui::SetNextWindowPos(ImVec2(SCR_WIDTH/2 - 200, SCR_HEIGHT/2 - 100), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_Always);
                const char* title = (strlen(authToken) > 0) ? "Connect to Server" : "Connect to Server (Guest)";
                if (ImGui::Begin(title, NULL, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
                    ImGui::Text("Server IP:");
                    ImGui::InputText("##ip", serverIP, 32);
                    
                    const char* connectText = (strlen(authToken) > 0) ? "Connect" : "Connect as Guest";
                    if (ImGui::Button(connectText, ImVec2(180, 40))) {
                        // Connect Logic
                        clientSocket = socket(AF_INET, SOCK_STREAM, 0);
                        sockaddr_in serverAddr;
                        serverAddr.sin_family = AF_INET;
                        inet_pton(AF_INET, serverIP, &serverAddr.sin_addr);
                        serverAddr.sin_port = htons(7777);
                        
                        if (connect(clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
                            showLoginError = true;
                            strncpy(loginErrorMsg, "Connection failed!", 255);
                        } else {
                            isConnected = true;
                            currentState = ONLINE;
                            showConnectionUI = false;
                            Network::setNonBlocking(clientSocket);
                            
                            PacketConnect pkt;
                            std::memset(&pkt, 0, sizeof(pkt));
                            
                            if (strlen(authToken) > 0) {
                                // Authenticated user
                                strncpy(pkt.username, myUsername, sizeof(pkt.username) - 1);
                                strncpy(pkt.authToken, authToken, sizeof(pkt.authToken) - 1);
                            } else {
                                // Guest
                                strncpy(pkt.username, "Guest", sizeof(pkt.username) - 1);
                            }
                            
                            if (!Network::sendStructPacket(clientSocket, PacketType::CONNECT, pkt)) {
                                showLoginError = true;
                                strncpy(loginErrorMsg, "Failed to send connect packet!", 255);
                                isConnected = false;
                                closesocket(clientSocket);
                                clientSocket = INVALID_SOCKET;
                                currentState = MENU_PLAY_CHOICE;
                                showConnectionUI = true;
                            } else {
                                std::cout << "Connected! Waiting for world state from server..." << std::endl;
                            }
                        }
                    }
                    
                    ImGui::SameLine();
                    if (ImGui::Button("Back", ImVec2(180, 40))) {
                        showConnectionUI = false;
                        currentState = MENU_PLAY_CHOICE;
                    }
                    
                    if (showLoginError) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
                        ImGui::Text("%s", loginErrorMsg);
                        ImGui::PopStyleColor();
                    }
                }
                ImGui::End();
            }
            
            // Player List Window (when online) - Always show when online, auto-scale
            if (currentState == ONLINE && isConnected) {
                ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
                // Auto-scale window size based on number of players
                int playerCount = (int)playerList.size() + 1; // +1 for local player
                float windowHeight = 60.0f + (playerCount * 25.0f); // Base height + per player
                if (windowHeight > 500.0f) windowHeight = 500.0f; // Max height
                ImGui::SetNextWindowSize(ImVec2(250, windowHeight), ImGuiCond_Always);
                if (ImGui::Begin("Players", NULL)) {
                    ImGui::Text("Players Online: %d", playerCount);
                    ImGui::Separator();
                    
                    // Show local player first
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
                    ImGui::Text("> %s (You)", myUsername);
                    ImGui::PopStyleColor();
                    
                    // Show other players
                    if (playerList.empty()) {
                        // Already showing local player, so no message needed
                    } else {
                        for (const auto& player : playerList) {
                            ImGui::Text("%s (ID: %d)", player.username, player.playerId);
                        }
                    }
                }
                ImGui::End();
            }

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
                                            remoteChar->isRemote = true;
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

            if ((isConnected || currentState == OFFLINE) && myCharacter) {
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
