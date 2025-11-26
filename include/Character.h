#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <deque>
#include "Part.h"
#include "PhysicsWorld.h"
#include "Camera.h"

class Character {
public:
    Character(glm::vec3 startPos, PhysicsWorld* physicsWorld, std::deque<Part>* partsList, 
              const glm::vec3& headColor = glm::vec3(0.8f, 0.6f, 0.4f),
              const glm::vec3& torsoColor = glm::vec3(0.2f, 0.4f, 0.8f),
              const glm::vec3& leftArmColor = glm::vec3(0.8f, 0.6f, 0.4f),
              const glm::vec3& rightArmColor = glm::vec3(0.8f, 0.6f, 0.4f),
              const glm::vec3& leftLegColor = glm::vec3(0.2f, 0.6f, 0.2f),
              const glm::vec3& rightLegColor = glm::vec3(0.2f, 0.6f, 0.2f));
    ~Character();

    void update(float deltaTime, Camera* camera, bool isSprinting); // Camera pointer, optional
    
    // Remote Control
    void setRemoteState(glm::vec3 pos, float yaw, bool walking, bool jumping);
    bool isRemote;
    bool isJumping; // Added member

    void processInput(bool forward, bool backward, bool left, bool right, bool jump, const Camera& camera, float zoomInput, float deltaTime);

    glm::vec3 getPosition() const;

    // Debug
    float debugTargetYaw;
    float debugCurrentYaw;
    glm::vec3 debugMoveDir;

    // Health
    float health;
    float maxHealth;
    void takeDamage(float amount);
    void respawn();
    
    // Get the folder index for this character (for cleanup)
    int getFolderIndex() const;
    
private:
    PhysicsWorld* physicsWorld;
    std::deque<Part>* partsList;
    glm::vec3 spawnPoint; 

    int headIndex;
    int torsoIndex;
    int leftArmIndex;
    int rightArmIndex;
    int leftLegIndex;
    int rightLegIndex;

    // Animation State
    float walkTime;
    bool isWalking;
    float zoomDistance;

    // Rotation State
    float internalYaw;
    bool hasReceivedFirstRotation; // Track if we've received first rotation update for remote characters

    // Physics
    int rootPartIndex;
};
