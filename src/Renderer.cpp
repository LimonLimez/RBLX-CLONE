#include "Renderer.h"
#include <glm/gtx/quaternion.hpp> // Include for quaternion to mat4

// ... (rest of includes)
#include <vector>
#include <cmath>

Renderer::Renderer() : shader(nullptr), depthShader(nullptr), cubeVAO(0), cubeVBO(0), sphereVAO(0), sphereVBO(0), sphereEBO(0), wedgeVAO(0), wedgeVBO(0) {}

Renderer::~Renderer() {
    if (shader) delete shader;
    if (depthShader) delete depthShader;
    glDeleteVertexArrays(1, &cubeVAO);
    glDeleteBuffers(1, &cubeVBO);
    glDeleteVertexArrays(1, &sphereVAO);
    glDeleteBuffers(1, &sphereVBO);
    glDeleteBuffers(1, &sphereEBO);
    glDeleteVertexArrays(1, &wedgeVAO);
    glDeleteBuffers(1, &wedgeVBO);
}

void Renderer::init() {
    std::cout << "Renderer::init() starting..." << std::endl;
    // Main Shader
    const char* vPath = "shaders/vertex.glsl";
    const char* fPath = "shaders/fragment.glsl";
    
    FILE* f = fopen(vPath, "r");
    if (!f) {
        std::cout << "Main vertex shader not found at: " << vPath << " trying ../../" << std::endl;
        vPath = "../../shaders/vertex.glsl";
        fPath = "../../shaders/fragment.glsl";
    } else {
        std::cout << "Main vertex shader found at: " << vPath << std::endl;
        fclose(f);
    }
    
    std::cout << "Compiling Main Shader..." << std::endl;
    shader = new Shader(vPath, fPath);
    if (!shader) std::cout << "Main Shader object creation failed!" << std::endl;
    
    // Depth Shader
    const char* vDepthPath = "shaders/shadow_depth_vertex.glsl";
    const char* fDepthPath = "shaders/shadow_depth_fragment.glsl";
    
    f = fopen(vDepthPath, "r");
    if (!f) {
        std::cout << "Shadow vertex shader not found at: " << vDepthPath << " trying ../../" << std::endl;
        vDepthPath = "../../shaders/shadow_depth_vertex.glsl";
        fDepthPath = "../../shaders/shadow_depth_fragment.glsl";
    } else {
        std::cout << "Shadow vertex shader found at: " << vDepthPath << std::endl;
        fclose(f);
    }
    
    std::cout << "Compiling Depth Shader..." << std::endl;
    depthShader = new Shader(vDepthPath, fDepthPath);
    if (!depthShader) std::cout << "Depth Shader object creation failed!" << std::endl;

    std::cout << "Initializing Meshes..." << std::endl;
    initCubeMesh();
    initSphereMesh();
    initWedgeMesh();
    std::cout << "Renderer::init() done." << std::endl;
}

void Renderer::drawPart(const Part& part, const glm::mat4& view, const glm::mat4& projection, const glm::mat4& lightSpaceMatrix, unsigned int shadowMap, unsigned int faceTexture) {
    shader->use();
    shader->setMat4("view", view);
    shader->setMat4("projection", projection);
    shader->setMat4("lightSpaceMatrix", lightSpaceMatrix);
    shader->setVec3("color", part.color);
    shader->setFloat("transparency", part.transparency);
    shader->setFloat("reflectance", part.reflectance);
    // Check if this is a head part (character head) or camera part
    bool isHeadPart = (part.name == "Head");
    unsigned int activeFaceTexture = (isHeadPart && part.textureId > 0) ? part.textureId : faceTexture;
    shader->setFloat("isCamera", (part.isCamera || isHeadPart) ? 1.0f : 0.0f); // Pass isCamera/head flag
    shader->setInt("shadowMap", 1); // Shadow map texture unit 1
    
    // Set face texture for camera parts or head parts
    if ((part.isCamera || isHeadPart) && activeFaceTexture > 0) {
        shader->setFloat("hasTexture", 1.0f);
        shader->setInt("faceTexture", 2); // Texture unit 2
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, activeFaceTexture);
        
        // Debug: Check if texture is valid
        GLint boundTexture = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTexture);
        if (boundTexture != (GLint)activeFaceTexture) {
            static bool warned = false;
            if (!warned) {
                std::cerr << "WARNING: Face texture not bound correctly! Expected " << activeFaceTexture
                          << " but got " << boundTexture << std::endl;
                warned = true;
            }
        }
    } else {
        shader->setFloat("hasTexture", 0.0f);
    }
    
    // Inverse view matrix to get camera position
    glm::vec3 viewPos = glm::vec3(glm::inverse(view)[3]);
    shader->setVec3("viewPos", viewPos);

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, part.position);
    
    // Apply Rotation using Quaternion for consistency with Physics
    glm::quat q = glm::quat(glm::radians(part.rotation)); 
    glm::mat4 rotationMatrix = glm::toMat4(q);
    model = model * rotationMatrix;
    
    model = glm::scale(model, part.size);
    shader->setMat4("model", model);
    shader->setVec3("partSize", part.size); // Pass size for studs

    // Bind shadow map
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadowMap);
    
    // Rebind face texture if needed (ensure it's still bound)
    if ((part.isCamera || isHeadPart) && activeFaceTexture > 0) {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, activeFaceTexture);
    }

    // Enable blending only for transparent parts
    // Head parts don't need blending - texture alpha is handled in shader
    if (part.transparency > 0.0f) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE); // Disable culling for transparent parts to see both sides
    } else {
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE); // Ensure culling is on for opaque
    }

    unsigned int vao = cubeVAO;
    int count = 36;
    bool useElements = false;

    if (part.shape == ShapeType::Sphere) {
        vao = sphereVAO;
        count = sphereIndexCount;
        useElements = true;
    } else if (part.shape == ShapeType::Wedge) {
        vao = wedgeVAO;
        count = 30; // Correct vertex count for 5 faces * 6 verts
    }

    glBindVertexArray(vao);
    if (useElements) {
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, 0);
    } else {
        glDrawArrays(GL_TRIANGLES, 0, count);
    }
    glBindVertexArray(0);
    
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE); // Restore defaults
}

void Renderer::drawPartShadow(const Part& part, const glm::mat4& lightSpaceMatrix) {
    depthShader->use();
    depthShader->setMat4("lightSpaceMatrix", lightSpaceMatrix);
    
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, part.position);
    
    glm::quat q = glm::quat(glm::radians(part.rotation)); 
    glm::mat4 rotationMatrix = glm::toMat4(q);
    model = model * rotationMatrix;

    model = glm::scale(model, part.size);
    depthShader->setMat4("model", model);
    
    unsigned int vao = cubeVAO;
    int count = 36;
    bool useElements = false;

    if (part.shape == ShapeType::Sphere) {
        vao = sphereVAO;
        count = sphereIndexCount;
        useElements = true;
    } else if (part.shape == ShapeType::Wedge) {
        vao = wedgeVAO;
        count = 30;
    }

    glBindVertexArray(vao);
    if (useElements) {
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, 0);
    } else {
        glDrawArrays(GL_TRIANGLES, 0, count);
    }
    glBindVertexArray(0);
}

void Renderer::initCubeMesh() {
    // Added Normals
    float vertices[] = {
        // Back face (-Z)
        -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f, // BL
         0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f, // TR
         0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f, // BR         
         0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f, // TR
        -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f, // BL
        -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f, // TL

        // Front face (+Z)
        -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f, // BL
         0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f, // BR
         0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f, // TR
         0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f, // TR
        -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f, // TL
        -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f, // BL

        // Left face (-X)
        -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f, // TL (Front)
        -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f, // TL (Back)
        -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f, // BL (Back)
        -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f, // BL (Back)
        -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f, // BL (Front)
        -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f, // TL (Front)

        // Right face (+X)
         0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f, // TR (Front)
         0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f, // BR (Back)
         0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f, // TR (Back)         
         0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f, // BR (Back)
         0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f, // TR (Front)
         0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f, // BR (Front)

        // Bottom face (-Y)
        -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f, // BL (Back)
         0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f, // BR (Back)
         0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f, // BR (Front)
         0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f, // BR (Front)
        -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f, // BL (Front)
        -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f, // BL (Back)

        // Top face (+Y)
        -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f, // TL (Back)
         0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f, // TR (Front)
         0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f, // TR (Back)     
         0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f, // TR (Front)
        -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f, // TL (Back)
        -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f  // TL (Front)
    };

    glGenVertexArrays(1, &cubeVAO);
    glGenBuffers(1, &cubeVBO);

    glBindVertexArray(cubeVAO);

    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // Position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // Normal attribute
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0); 
    glBindVertexArray(0); 
}

void Renderer::initSphereMesh() {
    glGenVertexArrays(1, &sphereVAO);
    glGenBuffers(1, &sphereVBO);
    glGenBuffers(1, &sphereEBO);

    std::vector<float> data;
    std::vector<unsigned int> indices;

    const unsigned int X_SEGMENTS = 32;
    const unsigned int Y_SEGMENTS = 32;
    const float PI = 3.14159265359f;

    for (unsigned int x = 0; x <= X_SEGMENTS; ++x) {
        for (unsigned int y = 0; y <= Y_SEGMENTS; ++y) {
            float xSegment = (float)x / (float)X_SEGMENTS;
            float ySegment = (float)y / (float)Y_SEGMENTS;
            float xPos = std::cos(xSegment * 2.0f * PI) * std::sin(ySegment * PI);
            float yPos = std::cos(ySegment * PI);
            float zPos = std::sin(xSegment * 2.0f * PI) * std::sin(ySegment * PI);

            // Position and Normal (same for sphere at origin with radius 1)
            data.push_back(xPos * 0.5f); // Radius 0.5
            data.push_back(yPos * 0.5f);
            data.push_back(zPos * 0.5f);
            data.push_back(xPos); // Normal
            data.push_back(yPos);
            data.push_back(zPos);
        }
    }

    for (unsigned int y = 0; y < Y_SEGMENTS; ++y) {
        for (unsigned int x = 0; x < X_SEGMENTS; ++x) {
            // Standard CCW Winding
            indices.push_back((y + 1) * (X_SEGMENTS + 1) + x);
            indices.push_back(y * (X_SEGMENTS + 1) + x + 1);
            indices.push_back(y * (X_SEGMENTS + 1) + x);

            indices.push_back((y + 1) * (X_SEGMENTS + 1) + x);
            indices.push_back((y + 1) * (X_SEGMENTS + 1) + x + 1);
            indices.push_back(y * (X_SEGMENTS + 1) + x + 1);
        }
    }
    sphereIndexCount = (unsigned int)indices.size();

    glBindVertexArray(sphereVAO);
    glBindBuffer(GL_ARRAY_BUFFER, sphereVBO);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), &data[0], GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphereEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), &indices[0], GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    glBindVertexArray(0);
}

void Renderer::initWedgeMesh() {
    // Wedge: -Z is forward direction of taper?
    // Let's align with physics.
    // Vertices relative to center 0.5 size
    float h = 0.5f;

    float vertices[] = {
        // Bottom (-Y)
        -h, -h, -h,  0, -1, 0, // 3
         h, -h, -h,  0, -1, 0, // 2
         h, -h,  h,  0, -1, 0, // 1
         
         h, -h,  h,  0, -1, 0, // 1
        -h, -h,  h,  0, -1, 0, // 0
        -h, -h, -h,  0, -1, 0, // 3
        
        // Back (-Z)
        -h,  h, -h,  0, 0, -1, // 4
         h,  h, -h,  0, 0, -1, // 5
         h, -h, -h,  0, 0, -1, // 2
         
         h, -h, -h,  0, 0, -1, // 2
        -h, -h, -h,  0, 0, -1, // 3
        -h,  h, -h,  0, 0, -1, // 4
        
        // Left (-X)
        -h,  h, -h, -1, 0, 0, // 4
        -h, -h, -h, -1, 0, 0, // 3
        -h, -h,  h, -1, 0, 0, // 0
        
        // Right (+X)
         h,  h, -h,  1, 0, 0, // 5
         h, -h,  h,  1, 0, 0, // 1
         h, -h, -h,  1, 0, 0, // 2
         
        // Slope (+Z/Y) Normal = (0, 0.707, 0.707)
        // 0, 1, 5, 4
        -h, -h,  h,  0, 0.7071f, 0.7071f, // 0
         h, -h,  h,  0, 0.7071f, 0.7071f, // 1
         h,  h, -h,  0, 0.7071f, 0.7071f, // 5
         
         h,  h, -h,  0, 0.7071f, 0.7071f, // 5
        -h,  h, -h,  0, 0.7071f, 0.7071f, // 4
        -h, -h,  h,  0, 0.7071f, 0.7071f  // 0
    };
    
    glGenVertexArrays(1, &wedgeVAO);
    glGenBuffers(1, &wedgeVBO);
    
    glBindVertexArray(wedgeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, wedgeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    glBindVertexArray(0);
}
