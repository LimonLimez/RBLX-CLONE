#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "ImGuizmo.h"

class Editor {
public:
    Editor(GLFWwindow* window);
    ~Editor();

    void init();
    void newFrame();
    void render();
    void cleanup();

    void setupDockSpace();

private:
    GLFWwindow* window;
};

