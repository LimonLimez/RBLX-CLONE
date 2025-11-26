#pragma once

#include "Shader.h"
#include "Part.h"
#include <vector>
#include <glm/glm.hpp>

class Renderer {
public:
    Renderer();
    ~Renderer();

    void init();
    void drawPart(const Part& part, const glm::mat4& view, const glm::mat4& projection, const glm::mat4& lightSpaceMatrix, unsigned int shadowMap, unsigned int faceTexture = 0);
    void drawPartShadow(const Part& part, const glm::mat4& lightSpaceMatrix);
    
    Shader* shader; // Make public for main access to set lightPos
    Shader* depthShader;

private:
    unsigned int cubeVAO, cubeVBO;
    unsigned int sphereVAO, sphereVBO, sphereEBO;
    unsigned int wedgeVAO, wedgeVBO;
    unsigned int sphereIndexCount;
    
    void initCubeMesh();
    void initSphereMesh();
    void initWedgeMesh();
};
