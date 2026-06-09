
#define GLM_ENABLE_EXPERIMENTAL

#include "Window.h"
#include "Renderer.h"
#include "Camera.h"
#include "Editor.h"
#include "Part.h"
#include "PhysicsWorld.h"
#include "Raycaster.h"
#include "ShadowMap.h"
#include "WorldLoader.h"
#ifdef _WIN32
#include "Auth.h"
#endif

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cstdio>
#include <deque>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>

namespace {
    constexpr int WINDOW_WIDTH = 1280;
    constexpr int WINDOW_HEIGHT = 720;
    constexpr float MIN_PART_SCALE = 0.05f;
    constexpr float MAX_PART_SCALE = 512.0f;

    void addDefaultWorld(std::deque<Part>& parts, PhysicsWorld& physicsWorld) {
        parts.clear();
        physicsWorld.reset();

        parts.emplace_back(glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(40.0f, 1.0f, 40.0f), glm::vec3(0.35f, 0.8f, 0.35f), ShapeType::Cube, "Baseplate");
        parts.back().anchored = true;
        physicsWorld.addPart(&parts.back());

        parts.emplace_back(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(4.0f, 4.0f, 4.0f), glm::vec3(0.8f, 0.25f, 0.2f), ShapeType::Cube, "Part");
        parts.back().anchored = true;
        physicsWorld.addPart(&parts.back());
    }

    bool saveWorld(const std::string& filename, const std::deque<Part>& parts) {
        std::ofstream out(filename);
        if (!out.is_open()) {
            return false;
        }

        out << parts.size() << '\n';
        for (const Part& part : parts) {
            out << static_cast<int>(part.shape) << ' '
                << std::quoted(part.name) << ' '
                << part.parentIndex << ' '
                << part.isFolder << ' '
                << part.isCamera << ' '
                << part.isSpawn << ' '
                << part.position.x << ' ' << part.position.y << ' ' << part.position.z << ' '
                << part.size.x << ' ' << part.size.y << ' ' << part.size.z << ' '
                << part.color.x << ' ' << part.color.y << ' ' << part.color.z << ' '
                << part.rotation.x << ' ' << part.rotation.y << ' ' << part.rotation.z << ' '
                << part.transparency << ' '
                << part.reflectance << ' '
                << part.anchored << ' '
                << part.canCollide << ' '
                << part.welds.size();

            for (const Weld& weld : part.welds) {
                int targetIndex = -1;
                for (size_t i = 0; i < parts.size(); ++i) {
                    if (&parts[i] == weld.part1) {
                        targetIndex = static_cast<int>(i);
                        break;
                    }
                }
                out << ' ' << targetIndex;
            }

            out << '\n';
        }

        return true;
    }

    std::string readTextFile(const std::string& filename) {
        std::ifstream in(filename, std::ios::binary);
        if (!in.is_open()) {
            return "";
        }

        return std::string(
            std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>()
        );
    }

    std::string readTokenFile() {
        std::ifstream in("auth_token.txt");
        std::string token;
        if (in.is_open()) {
            std::getline(in, token);
        }
        return token;
    }

    void writeTokenFile(const std::string& token) {
        std::ofstream out("auth_token.txt");
        if (out.is_open()) {
            out << token;
        }
    }

    std::string jsonStringField(const std::string& json, const std::string& field) {
        const std::string needle = "\"" + field + "\":\"";
        size_t pos = json.find(needle);
        if (pos == std::string::npos) return "";
        pos += needle.size();
        size_t end = json.find('"', pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    }

    std::string loginForPublish(const std::string& webUrl, const std::string& username, const std::string& password) {
#ifdef _WIN32
        const std::string response = Auth::login(username, password, webUrl);
        if (response.empty() || response.find("\"success\":true") == std::string::npos) {
            return "";
        }

        return jsonStringField(response, "token");
#else
        (void)webUrl;
        (void)username;
        (void)password;
        return "";
#endif
    }

    bool publishWorld(
        const std::string& webUrl,
        const std::string& token,
        const std::string& title,
        const std::string& description,
        bool isPublic,
        const std::string& worldText,
        std::string& response
    ) {
#ifdef _WIN32
        if (token.empty() || worldText.empty()) {
            return false;
        }

        std::ostringstream body;
        body << "{\"title\":\"" << Auth::jsonEscape(title) << "\","
             << "\"description\":\"" << Auth::jsonEscape(description) << "\","
             << "\"isPublic\":" << (isPublic ? "true" : "false") << ","
             << "\"worldText\":\"" << Auth::jsonEscape(worldText) << "\"}";

        response = Auth::httpPost(webUrl + "/api/games/publish", body.str(), token);
        return !response.empty() && response.find("\"success\":true") != std::string::npos;
#else
        (void)webUrl;
        (void)token;
        (void)title;
        (void)description;
        (void)isPublic;
        (void)worldText;
        response = "";
        return false;
#endif
    }

    glm::mat4 partToMatrix(const Part& part) {
        glm::mat4 model(1.0f);
        model = glm::translate(model, part.position);
        model *= glm::toMat4(glm::quat(glm::radians(part.rotation)));
        model = glm::scale(model, part.size);
        return model;
    }

    glm::vec3 clampScale(const glm::vec3& value) {
        return glm::vec3(
            std::clamp(value.x, MIN_PART_SCALE, MAX_PART_SCALE),
            std::clamp(value.y, MIN_PART_SCALE, MAX_PART_SCALE),
            std::clamp(value.z, MIN_PART_SCALE, MAX_PART_SCALE)
        );
    }

    void applyMatrixToPart(const glm::mat4& model, Part& part, PhysicsWorld& physicsWorld, bool rebuildShape) {
        float translation[3];
        float rotation[3];
        float scale[3];
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(model), translation, rotation, scale);

        part.position = glm::vec3(translation[0], translation[1], translation[2]);
        part.rotation = glm::vec3(rotation[0], rotation[1], rotation[2]);

        if (rebuildShape) {
            part.size = clampScale(glm::vec3(scale[0], scale[1], scale[2]));
            if (part.physicsBody) {
                physicsWorld.removePart(&part);
                physicsWorld.addPart(&part);
            }
        } else {
            physicsWorld.updatePartBody(&part);
        }
    }

    void processCameraInput(Window& window, Camera& camera, float deltaTime) {
        GLFWwindow* nativeWindow = window.getNativeWindow();
        const bool sprint = glfwGetKey(nativeWindow, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
        if (glfwGetKey(nativeWindow, GLFW_KEY_W) == GLFW_PRESS) camera.ProcessKeyboard(FORWARD, deltaTime, sprint);
        if (glfwGetKey(nativeWindow, GLFW_KEY_S) == GLFW_PRESS) camera.ProcessKeyboard(BACKWARD, deltaTime, sprint);
        if (glfwGetKey(nativeWindow, GLFW_KEY_A) == GLFW_PRESS) camera.ProcessKeyboard(LEFT, deltaTime, sprint);
        if (glfwGetKey(nativeWindow, GLFW_KEY_D) == GLFW_PRESS) camera.ProcessKeyboard(RIGHT, deltaTime, sprint);
        if (glfwGetKey(nativeWindow, GLFW_KEY_E) == GLFW_PRESS) camera.ProcessKeyboard(UP, deltaTime, sprint);
        if (glfwGetKey(nativeWindow, GLFW_KEY_Q) == GLFW_PRESS) camera.ProcessKeyboard(DOWN, deltaTime, sprint);
    }
}

int main() {
    try {
        Window window(WINDOW_WIDTH, WINDOW_HEIGHT, "RBLX Clone Studio");
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);

        Camera camera(glm::vec3(8.0f, 8.0f, 12.0f));
        camera.LookAt(glm::vec3(0.0f, 0.0f, 0.0f));

        PhysicsWorld physicsWorld;
        physicsWorld.init();

        std::deque<Part> parts;
        WorldLoader::loadWorld("ServerWorld.world", parts, physicsWorld);
        if (parts.empty()) {
            addDefaultWorld(parts, physicsWorld);
        }

        Renderer renderer;
        renderer.init();

        ShadowMap shadowMap;
        shadowMap.init();

        Editor editor(window.getNativeWindow());
        editor.init();

        int selectedPart = parts.size() > 1 ? 1 : 0;
        ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
        ImGuizmo::MODE mode = ImGuizmo::LOCAL;
        bool snapEnabled = false;
        float snap[3] = { 1.0f, 1.0f, 1.0f };
        char worldPath[260] = "ServerWorld.world";
        std::string statusMessage = "Studio ready";
        char publishWebUrl[128] = "http://localhost:3000";
        char publishTitle[64] = "Starter Baseplate";
        char publishDescription[241] = "";
        char publishUsername[32] = "";
        char publishPassword[64] = "";
        bool publishPublic = true;
        std::string publishStatus = "Sign in to publish";

        float lastFrame = static_cast<float>(glfwGetTime());
        double lastMouseX = WINDOW_WIDTH / 2.0;
        double lastMouseY = WINDOW_HEIGHT / 2.0;
        bool firstMouse = true;
        bool wasLeftPressed = false;

        while (!window.shouldClose()) {
            float currentFrame = static_cast<float>(glfwGetTime());
            float deltaTime = currentFrame - lastFrame;
            lastFrame = currentFrame;

            window.pollEvents();

            editor.newFrame();
            GLFWwindow* nativeWindow = window.getNativeWindow();

            if (ImGui::BeginMainMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("New")) {
                        addDefaultWorld(parts, physicsWorld);
                        selectedPart = parts.size() > 1 ? 1 : 0;
                        statusMessage = "Created a new default world";
                    }
                    if (ImGui::MenuItem("Load")) {
                        WorldLoader::loadWorld(worldPath, parts, physicsWorld);
                        if (parts.empty()) {
                            addDefaultWorld(parts, physicsWorld);
                            statusMessage = "Load failed; restored default world";
                        } else {
                            selectedPart = 0;
                            statusMessage = std::string("Loaded ") + worldPath;
                        }
                    }
                    if (ImGui::MenuItem("Save")) {
                        statusMessage = saveWorld(worldPath, parts) ? std::string("Saved ") + worldPath : std::string("Failed to save ") + worldPath;
                    }
                    if (ImGui::MenuItem("Exit")) {
                        glfwSetWindowShouldClose(nativeWindow, true);
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMainMenuBar();
            }

            ImGui::Begin("Toolbox");
            ImGui::InputText("World file", worldPath, sizeof(worldPath));
            if (ImGui::Button("Add Part")) {
                parts.emplace_back(glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(2.0f), glm::vec3(0.7f, 0.7f, 0.9f), ShapeType::Cube, "Part");
                parts.back().anchored = true;
                physicsWorld.addPart(&parts.back());
                selectedPart = static_cast<int>(parts.size() - 1);
            }
            ImGui::Separator();
            if (ImGui::RadioButton("Move", operation == ImGuizmo::TRANSLATE)) operation = ImGuizmo::TRANSLATE;
            if (ImGui::RadioButton("Rotate", operation == ImGuizmo::ROTATE)) operation = ImGuizmo::ROTATE;
            if (ImGui::RadioButton("Scale", operation == ImGuizmo::SCALE)) operation = ImGuizmo::SCALE;
            ImGui::Checkbox("Snap", &snapEnabled);
            ImGui::InputFloat3("Snap values", snap);
            ImGui::TextWrapped("%s", statusMessage.c_str());
            ImGui::End();

            ImGui::Begin("Publish");
            ImGui::InputText("Web URL", publishWebUrl, sizeof(publishWebUrl));
            ImGui::InputText("Username", publishUsername, sizeof(publishUsername));
            ImGui::InputText("Password", publishPassword, sizeof(publishPassword), ImGuiInputTextFlags_Password);
            if (ImGui::Button("Login")) {
                const std::string token = loginForPublish(publishWebUrl, publishUsername, publishPassword);
                if (token.empty()) {
                    publishStatus = "Login failed";
                } else {
                    writeTokenFile(token);
                    publishStatus = "Logged in for publishing";
                }
            }
            ImGui::Separator();
            ImGui::InputText("Game title", publishTitle, sizeof(publishTitle));
            ImGui::InputTextMultiline("Description", publishDescription, sizeof(publishDescription), ImVec2(0.0f, 72.0f));
            ImGui::Checkbox("Public", &publishPublic);
            if (ImGui::Button("Publish current world")) {
                if (!saveWorld(worldPath, parts)) {
                    publishStatus = std::string("Could not save ") + worldPath;
                } else {
                    const std::string token = readTokenFile();
                    const std::string worldText = readTextFile(worldPath);
                    std::string response;
                    if (publishWorld(publishWebUrl, token, publishTitle, publishDescription, publishPublic, worldText, response)) {
                        publishStatus = "Published to site";
                    } else if (token.empty()) {
                        publishStatus = "Login before publishing";
                    } else {
                        publishStatus = "Publish failed";
                    }
                }
            }
#ifdef _WIN32
            ImGui::SameLine();
            if (ImGui::Button("Open site")) {
                Auth::openBrowser(std::string(publishWebUrl) + "/create");
            }
#endif
            ImGui::TextWrapped("%s", publishStatus.c_str());
            ImGui::End();

            ImGui::Begin("Explorer");
            for (size_t i = 0; i < parts.size(); ++i) {
                if (parts[i].deleted) continue;
                if (ImGui::Selectable(parts[i].name.c_str(), selectedPart == static_cast<int>(i))) {
                    selectedPart = static_cast<int>(i);
                }
            }
            ImGui::End();

            ImGui::Begin("Properties");
            if (selectedPart >= 0 && selectedPart < static_cast<int>(parts.size())) {
                Part& part = parts[selectedPart];
                char name[64] = {};
                std::snprintf(name, sizeof(name), "%s", part.name.c_str());
                if (ImGui::InputText("Name", name, sizeof(name))) {
                    part.name = name;
                }
                bool transformChanged = false;
                transformChanged |= ImGui::DragFloat3("Position", glm::value_ptr(part.position), 0.1f);
                transformChanged |= ImGui::DragFloat3("Rotation", glm::value_ptr(part.rotation), 0.5f);
                if (ImGui::DragFloat3("Size", glm::value_ptr(part.size), 0.1f, MIN_PART_SCALE, MAX_PART_SCALE)) {
                    part.size = clampScale(part.size);
                    if (part.physicsBody) {
                        physicsWorld.removePart(&part);
                        physicsWorld.addPart(&part);
                    }
                } else if (transformChanged) {
                    physicsWorld.updatePartBody(&part);
                }
                ImGui::ColorEdit3("Color", glm::value_ptr(part.color));
                ImGui::Checkbox("Anchored", &part.anchored);
                ImGui::Checkbox("Can collide", &part.canCollide);
            } else {
                ImGui::Text("No part selected");
            }
            ImGui::End();

            ImGuiIO& io = ImGui::GetIO();
            const bool gizmoInUse = ImGuizmo::IsUsing();
            if (!gizmoInUse && !io.WantCaptureKeyboard) {
                processCameraInput(window, camera, deltaTime);
            }

            if (!gizmoInUse && !io.WantCaptureMouse && glfwGetMouseButton(nativeWindow, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
                double mouseX = 0.0;
                double mouseY = 0.0;
                glfwGetCursorPos(nativeWindow, &mouseX, &mouseY);
                if (firstMouse) {
                    lastMouseX = mouseX;
                    lastMouseY = mouseY;
                    firstMouse = false;
                }
                camera.ProcessMouseMovement(static_cast<float>(mouseX - lastMouseX), static_cast<float>(lastMouseY - mouseY));
                lastMouseX = mouseX;
                lastMouseY = mouseY;
            } else {
                firstMouse = true;
            }

            bool leftPressed = glfwGetMouseButton(nativeWindow, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

            glm::vec3 lightPos(20.0f, 50.0f, 20.0f);
            glm::mat4 lightSpaceMatrix = shadowMap.getLightSpaceMatrix(lightPos);

            shadowMap.bindForWriting();
            for (const Part& part : parts) {
                if (!part.deleted && !part.isFolder) {
                    renderer.drawPartShadow(part, lightSpaceMatrix);
                }
            }
            shadowMap.unbind();

            glViewport(0, 0, window.getWidth(), window.getHeight());
            glClearColor(0.53f, 0.81f, 0.92f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glm::mat4 view = camera.GetViewMatrix();
            glm::mat4 projection = camera.GetProjectionMatrix(static_cast<float>(window.getWidth()), static_cast<float>(window.getHeight()));

            if (renderer.shader) {
                renderer.shader->use();
                renderer.shader->setVec3("lightPos", lightPos);
            }

            for (const Part& part : parts) {
                if (!part.deleted && !part.isFolder) {
                    renderer.drawPart(part, view, projection, lightSpaceMatrix, shadowMap.depthMap, 0);
                }
            }

            if (selectedPart >= 0 && selectedPart < static_cast<int>(parts.size()) && !parts[selectedPart].isFolder) {
                Part& part = parts[selectedPart];
                glm::mat4 model = partToMatrix(part);

                ImGuizmo::SetOrthographic(false);
                const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
                ImGuizmo::SetRect(mainViewport->Pos.x, mainViewport->Pos.y, mainViewport->Size.x, mainViewport->Size.y);

                const float* snapValues = snapEnabled ? snap : nullptr;
                if (ImGuizmo::Manipulate(
                        glm::value_ptr(view),
                        glm::value_ptr(projection),
                        operation,
                        mode,
                        glm::value_ptr(model),
                        nullptr,
                        snapValues)) {
                    // ImGuizmo returns the full model matrix. Assign the decomposed size directly;
                    // multiplying by the returned scale each frame causes runaway scale growth.
                    applyMatrixToPart(model, part, physicsWorld, operation == ImGuizmo::SCALE);
                }
            }

            if (leftPressed && !wasLeftPressed && !io.WantCaptureMouse && !ImGuizmo::IsUsing() && !ImGuizmo::IsOver()) {
                double mouseX = 0.0;
                double mouseY = 0.0;
                glfwGetCursorPos(nativeWindow, &mouseX, &mouseY);
                int picked = Raycaster::GetPartFromMouse(parts, camera, static_cast<float>(mouseX), static_cast<float>(mouseY), static_cast<float>(window.getWidth()), static_cast<float>(window.getHeight()));
                if (picked >= 0 && picked < static_cast<int>(parts.size())) {
                    selectedPart = picked;
                }
            }
            wasLeftPressed = leftPressed;

            editor.render();
            window.swapBuffers();
        }

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Studio failed: " << error.what() << std::endl;
        return 1;
    }
}
