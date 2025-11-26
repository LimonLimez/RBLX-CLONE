#pragma once

#include <glad/glad.h>
#include <vector>
#include <string>
#include <iostream>
#include "Shader.h"
#include <stb_image.h> // We need stb_image for loading textures

class Skybox {
public:
    Skybox();
    ~Skybox();
    void init();
    void draw(const glm::mat4& view, const glm::mat4& projection);
    unsigned int getTextureID() const { return cubemapTexture; }

private:
    unsigned int skyboxVAO, skyboxVBO;
    unsigned int cubemapTexture;
    Shader* shader;
    
    unsigned int loadCubemap(std::vector<std::string> faces);
};

