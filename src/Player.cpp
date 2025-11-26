#include "Window.h"
#include "Renderer.h"
#include "Camera.h"
#include "Part.h"
#include "Raycaster.h"
#include "Skybox.h"
#include "PhysicsWorld.h"
#include "ShadowMap.h"

#include <fstream>
#include <sstream>
#include <deque>
#include <vector>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <iomanip>

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
bool rightMousePressed = false; // In Player mode, usually locked unless ESC?

// State
bool isCursorLocked = true;
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// --- Serialization Helpers (Duplicated for now, should be in Utils.h) ---
void loadWorld(const std::string& filename, std::deque<Part>& parts, PhysicsWorld& physicsWorld) {
    std::ifstream in(filename);
    if (!in.is_open()) {
        std::cerr << "Failed to open world file: " << filename << std::endl;
        return;
    }
    
    physicsWorld.reset();
    parts.clear();
    
    size_t count;
    in >> count;
    
    struct TempWeld {
        int partIndex;
        std::vector<int> targets;
    };
    std::vector<TempWeld> tempWelds;
    
    for (size_t i = 0; i < count; i++) {
        int shapeInt;
        in >> shapeInt;
        
        Part p;
        p.shape = (ShapeType)shapeInt;
        
        in >> std::ws;
        if (in.peek() == '"') {
            in >> std::quoted(p.name);
        } else {
            in >> p.name; 
        }
        
        in >> p.parentIndex >> p.isFolder >> p.isCamera >> p.isSpawn;
        
        in >> p.position.x >> p.position.y >> p.position.z
           >> p.size.x >> p.size.y >> p.size.z
           >> p.color.x >> p.color.y >> p.color.z
           >> p.rotation.x >> p.rotation.y >> p.rotation.z
           >> p.transparency >> p.reflectance
           >> p.anchored >> p.canCollide;
           
        size_t weldCount;
        in >> weldCount;
        
        if (weldCount > 0) {
            TempWeld tw;
            tempWelds.push_back({ (int)i, {} });
            for (size_t j=0; j<weldCount; j++) {
                int targetIdx;
                in >> targetIdx;
                tempWelds.back().targets.push_back(targetIdx);
            }
        }
        
        parts.push_back(p);
        if (!p.isFolder) physicsWorld.addPart(&parts.back());
    }
    
    for (const auto& tw : tempWelds) {
        for (int target : tw.targets) {
            if (target >= 0 && target < parts.size()) {
                Weld w;
                w.part1 = &parts[target];
                parts[tw.partIndex].welds.push_back(w);
            }
        }
    }
    
    in.close();
}

// Windows File Dialog
#include <windows.h>
#include <commdlg.h>

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
    if (!isCursorLocked) return;

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

#include "Character.h" // Include Character

// Global Character
Character* myCharacter = nullptr;

int main() {
    Window window(SCR_WIDTH, SCR_HEIGHT, "RBLX Clone Player");
    glfwSetCursorPosCallback(window.getNativeWindow(), mouse_callback);
    
    // Lock cursor by default
    glfwSetInputMode(window.getNativeWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // Setup ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window.getNativeWindow(), true);
    ImGui_ImplOpenGL3_Init("#version 330");

    Renderer renderer;
    renderer.init();

    Skybox skybox;
    skybox.init();

    PhysicsWorld physicsWorld;
    physicsWorld.init();
    
    ShadowMap shadowMap;
    shadowMap.init();

    std::deque<Part> parts;
    std::string currentWorldFile = "";
    
    // Load World on Start
    currentWorldFile = openFileDialog(window.getNativeWindow());
    // Reset lastFrame after waiting for file dialog to avoid huge deltaTime
    lastFrame = static_cast<float>(glfwGetTime());
    
    if (!currentWorldFile.empty()) {
        loadWorld(currentWorldFile, parts, physicsWorld);
    } else {
        Part baseplate(glm::vec3(0.0f, -2.0f, 0.0f), glm::vec3(100.0f, 1.0f, 100.0f), glm::vec3(0.3f, 0.5f, 0.3f));
        baseplate.name = "Baseplate";
        parts.push_back(baseplate);
        physicsWorld.addPart(&parts.back());
    }
    
    // Helper to reset and setup physics/joints
    auto setupPhysics = [&]() {
        if (myCharacter) { delete myCharacter; myCharacter = nullptr; }
        physicsWorld.reset();
        for (auto& part : parts) {
            if (!part.isFolder) physicsWorld.addPart(&part);
        }
        for (auto& part : parts) {
             for (const auto& weld : part.welds) {
                 if (weld.part1) {
                     physicsWorld.createWeld(&part, weld.part1);
                 }
             }
        }
        // Spawn Location Logic
        Part* spawnPart = nullptr;
        Part* cameraPart = nullptr;
        
        for(auto& p : parts) {
            if (p.isSpawn && !spawnPart) spawnPart = &p;
            if (p.isCamera && !cameraPart) cameraPart = &p;
        }
        
        if (spawnPart) {
             glm::vec3 spawnPos = spawnPart->position + glm::vec3(0, 2, 0);
             myCharacter = new Character(spawnPos, &physicsWorld, &parts);
             return (Part*)nullptr; // Character controls camera
        } else if (cameraPart) {
            camera.Position = cameraPart->position;
            camera.Yaw = cameraPart->rotation.y - 90.0f; 
            camera.Pitch = cameraPart->rotation.x;
            camera.ProcessMouseMovement(0,0);
            return cameraPart;
        }
        return (Part*)nullptr;
    };

    Part* activeCameraPart = setupPhysics();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    while (!window.shouldClose()) {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        if (glfwGetKey(window.getNativeWindow(), GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            isCursorLocked = false;
            glfwSetInputMode(window.getNativeWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
        
        // ImGui Frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        
        // Overlay UI
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        if (ImGui::Begin("Player Controls", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
            if (ImGui::Button("Load (Test)")) {
                std::string newFile = openFileDialog(window.getNativeWindow());
                // Reset timer after dialog
                lastFrame = static_cast<float>(glfwGetTime());
                
                if (!newFile.empty()) {
                    currentWorldFile = newFile;
                    loadWorld(currentWorldFile, parts, physicsWorld);
                    activeCameraPart = setupPhysics();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Restart")) {
                if (!currentWorldFile.empty()) {
                    loadWorld(currentWorldFile, parts, physicsWorld);
                    activeCameraPart = setupPhysics();
                } else {
                    activeCameraPart = setupPhysics();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Stop")) {
                glfwSetWindowShouldClose(window.getNativeWindow(), true);
            }
            ImGui::SameLine();
            if (ImGui::Button(isCursorLocked ? "Unlock Cursor (ESC)" : "Lock Cursor")) {
                isCursorLocked = !isCursorLocked;
                glfwSetInputMode(window.getNativeWindow(), GLFW_CURSOR, isCursorLocked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
            }
        }
        ImGui::End();

        // Health Bar (Copied from main.cpp logic)
        if (myCharacter) {
            ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
            window_flags |= ImGuiWindowFlags_NoMove;
            
            float PAD = 10.0f;
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImVec2 work_pos = viewport->WorkPos; 
            ImVec2 work_size = viewport->WorkSize;
            ImVec2 window_pos, window_pos_pivot;
            window_pos.x = work_pos.x + work_size.x - PAD;
            window_pos.y = work_pos.y + work_size.y - PAD;
            window_pos_pivot.x = 1.0f;
            window_pos_pivot.y = 1.0f;
            ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
            ImGui::SetNextWindowBgAlpha(0.35f);
            
            if (ImGui::Begin("HealthOverlay", NULL, window_flags)) {
                ImGui::Text("Health");
                float health = myCharacter->health;
                float maxHealth = myCharacter->maxHealth;
                
                ImVec4 color = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
                if (health < 30) color = ImVec4(0.8f, 0.2f, 0.2f, 1.0f);
                
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
                char buf[32];
                sprintf(buf, "%.0f / %.0f", health, maxHealth);
                ImGui::ProgressBar(health / maxHealth, ImVec2(200.0f, 20.0f), buf);
                ImGui::PopStyleColor();
            }
            ImGui::End();
        }

        // Health Bar
        if (myCharacter) {
            // ... existing health bar code ...
        }
        
        // Player List (Leaderboard)
        {
            float PAD = 10.0f;
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImVec2 work_pos = viewport->WorkPos; 
            ImVec2 work_size = viewport->WorkSize;
            ImVec2 window_pos, window_pos_pivot;
            window_pos.x = work_pos.x + work_size.x - PAD;
            window_pos.y = work_pos.y + PAD; // Top Right
            window_pos_pivot.x = 1.0f;
            window_pos_pivot.y = 0.0f;
            ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
            ImGui::SetNextWindowBgAlpha(0.5f); // Dark translucent
            
            ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
            
            if (ImGui::Begin("Leaderboard", NULL, window_flags)) {
                ImGui::Text("Players");
                ImGui::Separator();
                ImGui::Text("Player1"); // Placeholder
            }
            ImGui::End();
        }

        if (isCursorLocked) {
            if (myCharacter) {
                 bool fwd = glfwGetKey(window.getNativeWindow(), GLFW_KEY_W) == GLFW_PRESS;
                 bool bwd = glfwGetKey(window.getNativeWindow(), GLFW_KEY_S) == GLFW_PRESS;
                 bool l = glfwGetKey(window.getNativeWindow(), GLFW_KEY_A) == GLFW_PRESS;
                 bool r = glfwGetKey(window.getNativeWindow(), GLFW_KEY_D) == GLFW_PRESS;
                 bool j = glfwGetKey(window.getNativeWindow(), GLFW_KEY_SPACE) == GLFW_PRESS;
                 
                 float zoom = 0.0f;
                 if (glfwGetKey(window.getNativeWindow(), GLFW_KEY_UP) == GLFW_PRESS) zoom = 1.0f;
                 if (glfwGetKey(window.getNativeWindow(), GLFW_KEY_DOWN) == GLFW_PRESS) zoom = -1.0f;
                 
                 myCharacter->processInput(fwd, bwd, l, r, j, camera, zoom, deltaTime);
                 
                 // Camera is updated in update() loop
            }
            // If Camera Part exists, lock position to it
            else if (activeCameraPart) {
                camera.Position = activeCameraPart->position;
            } else {
                // Free cam
                bool isSprinting = glfwGetKey(window.getNativeWindow(), GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
                if (glfwGetKey(window.getNativeWindow(), GLFW_KEY_W) == GLFW_PRESS) camera.ProcessKeyboard(FORWARD, deltaTime, isSprinting);
                if (glfwGetKey(window.getNativeWindow(), GLFW_KEY_S) == GLFW_PRESS) camera.ProcessKeyboard(BACKWARD, deltaTime, isSprinting);
                if (glfwGetKey(window.getNativeWindow(), GLFW_KEY_A) == GLFW_PRESS) camera.ProcessKeyboard(LEFT, deltaTime, isSprinting);
                if (glfwGetKey(window.getNativeWindow(), GLFW_KEY_D) == GLFW_PRESS) camera.ProcessKeyboard(RIGHT, deltaTime, isSprinting);
            }
        }

        // Update Character
        if (myCharacter) {
             bool isSprinting = glfwGetKey(window.getNativeWindow(), GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
             myCharacter->update(deltaTime, &camera, isSprinting);
        }

        // Update Physics
        physicsWorld.update(deltaTime);
        
        // --- Render ---
        glm::vec3 lightPos(20.0f, 50.0f, 20.0f);
        glm::mat4 lightSpaceMatrix = shadowMap.getLightSpaceMatrix(lightPos);
        
        shadowMap.bindForWriting();
        for (const auto& part : parts) {
            // In Player mode, hide camera part?
            // User said "face of the part black showing the fron that camera is what you will see out of"
            // So keep rendering it.
            renderer.drawPartShadow(part, lightSpaceMatrix);
        }
        shadowMap.unbind();
        
        glViewport(0, 0, window.getWidth(), window.getHeight());
        glClearColor(0.53f, 0.81f, 0.92f, 1.0f); 
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 view = camera.GetViewMatrix();
        glm::mat4 projection = camera.GetProjectionMatrix((float)window.getWidth(), (float)window.getHeight());

        renderer.shader->use();
        renderer.shader->setVec3("lightPos", lightPos);

        glDepthMask(GL_FALSE);
        skybox.draw(view, projection);
        glDepthMask(GL_TRUE);

        for (const auto& part : parts) {
            // Camera part logic handled in shader (isCamera uniform)
            // Renderer::drawPart sets isCamera uniform based on part.isCamera
            renderer.drawPart(part, view, projection, lightSpaceMatrix, shadowMap.depthMap);
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        window.swapBuffers();
        window.pollEvents();
    }
    
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    return 0;
}
