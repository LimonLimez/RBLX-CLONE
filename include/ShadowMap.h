#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

class ShadowMap {
public:
    unsigned int depthMapFBO;
    unsigned int depthMap;
    const unsigned int SHADOW_WIDTH = 2048, SHADOW_HEIGHT = 2048;

    ShadowMap();
    ~ShadowMap();
    void init();
    void bindForWriting();
    void unbind();
    void bindForReading(unsigned int textureUnit);
    
    glm::mat4 getLightSpaceMatrix(glm::vec3 lightPos);
};
