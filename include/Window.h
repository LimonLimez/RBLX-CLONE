#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <string>

class Window {
public:
    Window(int width, int height, const std::string& title);
    ~Window();

    bool shouldClose() const;
    void swapBuffers();
    void pollEvents();
    
    int getWidth() const { 
        if (window) {
            int w, h;
            glfwGetFramebufferSize(window, &w, &h);
            return w;
        }
        return width; 
    }
    int getHeight() const { 
        if (window) {
            int w, h;
            glfwGetFramebufferSize(window, &w, &h);
            return h;
        }
        return height; 
    }
    GLFWwindow* getNativeWindow() const { return window; }

private:
    GLFWwindow* window;
    int width;
    int height;
    std::string title;

    void init();
};



