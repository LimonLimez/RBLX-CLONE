#pragma once

#include <glm/glm.hpp>
#include <string>

struct AvatarConfig {
    glm::vec3 headColor = glm::vec3(0.8f, 0.6f, 0.4f);      // Skin color
    glm::vec3 torsoColor = glm::vec3(0.2f, 0.4f, 0.8f);    // Blue shirt
    glm::vec3 leftArmColor = glm::vec3(0.8f, 0.6f, 0.4f);  // Skin color
    glm::vec3 rightArmColor = glm::vec3(0.8f, 0.6f, 0.4f); // Skin color
    glm::vec3 leftLegColor = glm::vec3(0.2f, 0.6f, 0.2f);  // Green pants
    glm::vec3 rightLegColor = glm::vec3(0.2f, 0.6f, 0.2f); // Green pants
    std::string faceId = "classic";
    
    void saveToFile(const std::string& filename = "avatar.txt");
    void loadFromFile(const std::string& filename = "avatar.txt");
};


