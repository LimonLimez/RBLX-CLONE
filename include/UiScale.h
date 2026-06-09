#pragma once

#include "imgui.h"
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

namespace UiScale {
    inline float Compute(GLFWwindow* window) {
        int framebufferWidth = 1280;
        int framebufferHeight = 720;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);

        float contentScaleX = 1.0f;
        float contentScaleY = 1.0f;
        glfwGetWindowContentScale(window, &contentScaleX, &contentScaleY);

        const float resolutionScale = std::sqrt(std::max(
            static_cast<float>(std::max(framebufferWidth, 1)) / 1280.0f,
            static_cast<float>(std::max(framebufferHeight, 1)) / 720.0f
        ));
        const float dpiScale = std::max(contentScaleX, contentScaleY);
        return std::clamp(std::max(resolutionScale, dpiScale), 1.0f, 1.85f);
    }

    inline float Apply(GLFWwindow* window) {
        const float scale = Compute(window);
        ImGuiIO& io = ImGui::GetIO();
        ImGuiStyle& style = ImGui::GetStyle();
        style.ScaleAllSizes(scale);
        io.FontGlobalScale = scale;
        return scale;
    }

    inline float Px(float value, float scale) {
        return value * scale;
    }

    inline ImVec2 Size(float width, float height, float scale) {
        return ImVec2(Px(width, scale), Px(height, scale));
    }

    inline ImVec2 CenteredWindowPos(float width, float height, float scale, float yOffset = 0.0f) {
        const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        const float scaledWidth = Px(width, scale);
        const float scaledHeight = Px(height, scale);
        return ImVec2(
            std::max(0.0f, (displaySize.x - scaledWidth) * 0.5f),
            std::max(0.0f, (displaySize.y - scaledHeight) * 0.5f + Px(yOffset, scale))
        );
    }
}
