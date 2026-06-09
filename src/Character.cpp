#include "Character.h"
#include <cmath>
#include <iostream>

namespace {
    float normalizeDegrees(float angle) {
        if (!std::isfinite(angle)) {
            return 0.0f;
        }

        angle = std::fmod(angle, 360.0f);
        if (angle > 180.0f) angle -= 360.0f;
        if (angle < -180.0f) angle += 360.0f;
        return angle;
    }
}

Character::Character(glm::vec3 startPos, PhysicsWorld* physicsWorld, std::deque<Part>* partsList,
                     const glm::vec3& headColor, const glm::vec3& torsoColor,
                     const glm::vec3& leftArmColor, const glm::vec3& rightArmColor,
                     const glm::vec3& leftLegColor, const glm::vec3& rightLegColor)
    : isRemote(false), isJumping(false), debugTargetYaw(0.0f), debugCurrentYaw(0.0f),
      debugMoveDir(0.0f), health(100.0f), maxHealth(100.0f),
      physicsWorld(physicsWorld), partsList(partsList), spawnPoint(startPos),
      headIndex(-1), torsoIndex(-1), leftArmIndex(-1), rightArmIndex(-1),
      leftLegIndex(-1), rightLegIndex(-1), walkTime(0.0f), isWalking(false),
      zoomDistance(15.0f), internalYaw(0.0f), hasReceivedFirstRotation(false),
      rootPartIndex(-1) {
    
    // Create Folder "Player"
    Part playerFolder;
    playerFolder.name = "Player";
    playerFolder.isFolder = true;
    playerFolder.anchored = true;
    playerFolder.canCollide = false;
    playerFolder.transparency = 1.0f;
    partsList->push_back(playerFolder);
    int folderIndex = partsList->size() - 1;

    // 1. RootPart / Torso (Main Physics Body)
    glm::vec3 rootSize(2.0f, 5.0f, 1.0f);
    Part rootPart(startPos + glm::vec3(0, 2.5f, 0), rootSize, glm::vec3(1,0,0)); 
    rootPart.name = "HumanoidRootPart";
    rootPart.parentIndex = folderIndex; 
    rootPart.anchored = false;
    rootPart.canCollide = true;
    rootPart.mass = 10.0f;
    rootPart.transparency = 1.0f; // Invisible
    
    partsList->push_back(rootPart);
    rootPartIndex = partsList->size() - 1;
    physicsWorld->addPart(&partsList->back());
    
    if (partsList->back().physicsBody) {
        rp3d::RigidBody* body = (rp3d::RigidBody*)partsList->back().physicsBody;
        body->setAngularLockAxisFactor(rp3d::Vector3(0, 1, 0));
        if (body->getNbColliders() > 0) {
            body->getCollider(0)->getMaterial().setFrictionCoefficient(0.0f);
        }
    }
    
    // Torso (Visual)
    Part torso(startPos, glm::vec3(2.0f, 2.0f, 1.0f), torsoColor);
    torso.name = "Torso";
    torso.parentIndex = folderIndex; 
    torso.anchored = true;
    torso.canCollide = false;
    partsList->push_back(torso);
    torsoIndex = partsList->size() - 1;

    auto createLimb = [&](std::string name, glm::vec3 size, glm::vec3 col) -> int {
        Part p(startPos, size, col); 
        p.name = name;
        p.parentIndex = folderIndex; 
        p.anchored = true; 
        p.canCollide = false; 
        partsList->push_back(p);
        return partsList->size() - 1;
    };

    headIndex = createLimb("Head", glm::vec3(1.2f, 1.2f, 1.2f), headColor);
    leftArmIndex = createLimb("LeftArm", glm::vec3(1.0f, 2.0f, 1.0f), leftArmColor);
    rightArmIndex = createLimb("RightArm", glm::vec3(1.0f, 2.0f, 1.0f), rightArmColor);
    leftLegIndex = createLimb("LeftLeg", glm::vec3(1.0f, 2.0f, 1.0f), leftLegColor);
    rightLegIndex = createLimb("RightLeg", glm::vec3(1.0f, 2.0f, 1.0f), rightLegColor);
}

Character::~Character() {
}

int Character::getFolderIndex() const {
    if (!partsList || rootPartIndex < 0 || rootPartIndex >= partsList->size()) {
        return -1;
    }
    const Part& root = (*partsList)[rootPartIndex];
    return root.parentIndex; // The folder is the parent of the root part
}

void Character::setFaceTexture(unsigned int textureId) {
    if (!partsList || headIndex < 0 || headIndex >= static_cast<int>(partsList->size())) {
        return;
    }
    (*partsList)[headIndex].textureId = textureId;
}

void Character::setRemoteControlled(bool remote) {
    isRemote = remote;
    if (!partsList || rootPartIndex < 0 || rootPartIndex >= static_cast<int>(partsList->size())) {
        return;
    }

    Part& root = (*partsList)[rootPartIndex];
    if (root.deleted || !root.physicsBody) {
        return;
    }

    rp3d::RigidBody* body = static_cast<rp3d::RigidBody*>(root.physicsBody);
    if (remote) {
        root.canCollide = false;
        body->setType(rp3d::BodyType::KINEMATIC);
        body->setLinearVelocity(rp3d::Vector3(0, 0, 0));
        body->setAngularVelocity(rp3d::Vector3(0, 0, 0));
        if (body->getNbColliders() > 0) {
            body->getCollider(0)->setCollideWithMaskBits(0);
        }
        return;
    }

    root.canCollide = true;
    body->setType(root.anchored ? rp3d::BodyType::STATIC : rp3d::BodyType::DYNAMIC);
    if (body->getNbColliders() > 0) {
        body->getCollider(0)->setCollideWithMaskBits(0xFFFF);
    }
}

void Character::setRemoteState(glm::vec3 pos, float yaw, bool walking, bool jumping) {
    if (!isRemote) return;
    if (partsList && rootPartIndex >= 0 && rootPartIndex < partsList->size()) {
        Part& root = (*partsList)[rootPartIndex];
        if (root.deleted) return; // Character is deleted, don't update
        
        // Very fast interpolation for local testing (low latency)
        float lerpFactor = 0.8f; // 80% per update for smooth, responsive movement
        root.position = glm::mix(root.position, pos, lerpFactor);
        
        float targetYaw = normalizeDegrees(yaw);
        
        // On first rotation update, snap immediately to avoid wrong angle
        if (!hasReceivedFirstRotation) {
            internalYaw = targetYaw;
            root.rotation.y = targetYaw;
            hasReceivedFirstRotation = true;
        } else {
            // Use internalYaw for smooth interpolation (matches local character behavior)
            float currentYaw = normalizeDegrees(internalYaw);
            float diff = normalizeDegrees(targetYaw - currentYaw);
            
            // Update internalYaw (matches local character)
            internalYaw = normalizeDegrees(currentYaw + diff * lerpFactor);
            
            // Set root rotation to match internalYaw (matches local character)
            root.rotation.y = internalYaw;
        }
        
        // Update debug values for remote characters
        debugCurrentYaw = internalYaw;
        debugTargetYaw = targetYaw;
        debugMoveDir = glm::vec3(0); // Remote characters don't have move dir
        
        isWalking = walking;
        isJumping = jumping;
        
        // Update Physics Body Transform smoothly
        rp3d::RigidBody* body = (rp3d::RigidBody*)root.physicsBody;
        if (body) {
            rp3d::Transform t = body->getTransform();
            rp3d::Vector3 currentPos = t.getPosition();
            rp3d::Vector3 targetPos(pos.x, pos.y, pos.z);
            rp3d::Vector3 lerpedPos = currentPos + (targetPos - currentPos) * lerpFactor;
            t.setPosition(lerpedPos);
            
            // Smooth rotation with quaternion slerp
            rp3d::Quaternion currentQ = t.getOrientation();
            rp3d::Quaternion targetQ = rp3d::Quaternion::fromEulerAngles(0, glm::radians(root.rotation.y), 0);
            rp3d::Quaternion lerpedQ = rp3d::Quaternion::slerp(currentQ, targetQ, lerpFactor);
            t.setOrientation(lerpedQ);
            
            body->setTransform(t);
            // Lightly dampen velocity to allow some physics feel
            rp3d::Vector3 vel = body->getLinearVelocity();
            body->setLinearVelocity(vel * 0.5f);
            body->setAngularVelocity(body->getAngularVelocity() * 0.5f);
        }
    }
}

void Character::update(float deltaTime, Camera* camera, bool isSprinting) {
    if (!partsList || rootPartIndex < 0 || rootPartIndex >= partsList->size()) return;
    Part& root = (*partsList)[rootPartIndex];
    if (root.deleted) return; // Character is deleted
    rp3d::RigidBody* body = (rp3d::RigidBody*)root.physicsBody;
    if (!body) return; // Safety check: body might be invalid

    // For remote characters, ensure root.rotation.y matches internalYaw (like local characters)
    if (isRemote) {
        root.rotation.y = internalYaw;
    }

    // 1. Local Physics Logic
    if (!isRemote && body) {
        rp3d::Vector3 vel = body->getLinearVelocity();
        if (vel.y < -0.1f) {
            body->applyWorldForceAtCenterOfMass(rp3d::Vector3(0, -2000.0f * deltaTime, 0)); 
        }
        
        if (root.position.y < -185.0f) {
            takeDamage(100.0f); 
            return;
        }
        
        // Determine Animation State from Physics
        float speed = glm::length(glm::vec2(vel.x, vel.z));
        isWalking = speed > 0.1f;
        isJumping = std::abs(vel.y) > 0.5f;
    }

    // 2. Animation
    glm::vec3 rootPos = root.position;
    glm::vec3 rootRot = root.rotation; 

    if (isWalking) {
        float speed = 10.0f;
        if (!isRemote && body) {
             rp3d::Vector3 v = body->getLinearVelocity();
             speed = glm::length(glm::vec2(v.x, v.z));
        }
        walkTime += deltaTime * speed * 0.5f; 
    } else {
        walkTime = 0; 
    }
    
    float armAngle = std::sin(walkTime) * 45.0f;
    float legAngle = std::sin(walkTime) * 45.0f;
    
    if (isJumping) {
        armAngle = 180.0f; 
        legAngle = 0.0f; 
    }

    auto updateLimb = [&](int index, glm::vec3 offset, float rotX, float rotY, float rotZ) {
        if (index < 0 || index >= partsList->size()) return;
        Part& p = (*partsList)[index];
        if (p.deleted) return; // Part is deleted
        float yaw = glm::radians(rootRot.y);
        float c = cos(yaw);
        float s = sin(yaw);
        
        glm::vec3 rotOffset;
        rotOffset.x = offset.x * c + offset.z * s;
        rotOffset.y = offset.y;
        rotOffset.z = -offset.x * s + offset.z * c;
        
        p.position = rootPos + rotOffset;
        p.rotation = glm::vec3(rotX, rotY + rootRot.y, rotZ);
    };
    
    updateLimb(torsoIndex, glm::vec3(0, 0.5f, 0), 0, 0, 0);
    updateLimb(headIndex, glm::vec3(0, 2.1f, 0), 0, 0, 0);
    
    float armAmp = isJumping ? 180.0f : (isWalking ? 45.0f : 0.0f);
    float legAmp = isJumping ? 0.0f : (isWalking ? 45.0f : 0.0f);
    
    auto getRotatedOffset = [&](glm::vec3 offset, float rotX) -> glm::vec3 {
        float rad = glm::radians(rotX);
        float c = cos(rad);
        float s = sin(rad);
        glm::vec3 newOffset = offset;
        newOffset.y = offset.y * c - offset.z * s;
        newOffset.z = offset.y * s + offset.z * c;
        return newOffset;
    };

    glm::vec3 leftArmOffset = glm::vec3(-1.5f, 0.5f, 0); 
    float leftArmAngle = isJumping ? armAmp : (isWalking ? armAmp * std::sin(walkTime) : 0);
    glm::vec3 lArmRel = getRotatedOffset(glm::vec3(0, -1.0f, 0), leftArmAngle);
    updateLimb(leftArmIndex, leftArmOffset - glm::vec3(0, -1.0f, 0) + lArmRel, leftArmAngle, 0, 0);

    glm::vec3 rightArmOffset = glm::vec3(1.5f, 0.5f, 0); 
    float rightArmAngle = isJumping ? armAmp : (isWalking ? -armAmp * std::sin(walkTime) : 0);
    glm::vec3 rArmRel = getRotatedOffset(glm::vec3(0, -1.0f, 0), rightArmAngle);
    updateLimb(rightArmIndex, rightArmOffset - glm::vec3(0, -1.0f, 0) + rArmRel, rightArmAngle, 0, 0);
    
    glm::vec3 leftLegOffset = glm::vec3(-0.5f, -1.5f, 0); 
    float leftLegAngle = isJumping ? 0 : (isWalking ? -legAmp * std::sin(walkTime) : 0);
    glm::vec3 lLegRel = getRotatedOffset(glm::vec3(0, -1.0f, 0), leftLegAngle);
    updateLimb(leftLegIndex, leftLegOffset - glm::vec3(0, -1.0f, 0) + lLegRel, leftLegAngle, 0, 0);

    glm::vec3 rightLegOffset = glm::vec3(0.5f, -1.5f, 0);
    float rightLegAngle = isJumping ? 0 : (isWalking ? legAmp * std::sin(walkTime) : 0);
    glm::vec3 rLegRel = getRotatedOffset(glm::vec3(0, -1.0f, 0), rightLegAngle);
    updateLimb(rightLegIndex, rightLegOffset - glm::vec3(0, -1.0f, 0) + rLegRel, rightLegAngle, 0, 0);
    
    // Update Camera (Local only)
    if (!isRemote && camera) {
        float distance = zoomDistance;
        float height = 3.0f; 
        // Calculate camera position behind character
        float yawRad = glm::radians(camera->Yaw);
        float pitchRad = glm::radians(camera->Pitch);
        
        glm::vec3 offset;
        offset.x = cos(yawRad) * cos(pitchRad);
        offset.y = sin(pitchRad);
        offset.z = sin(yawRad) * cos(pitchRad);
        offset = glm::normalize(offset) * distance; // Direction from character to camera if reversed, or camera to character?
        // Camera Front points TO character usually? No, Front points AWAY from viewer.
        // Let's stick to standard orbital logic:
        // Position = Target - (Front * dist)
        
        // Ensure Camera Front is updated by Mouse Input (it is)
        // So just set position relative to Root
        
        camera->Position = rootPos - (camera->Front * distance);
        camera->Position.y += height; 
    }
}

void Character::processInput(bool forward, bool backward, bool left, bool right, bool jump, const Camera& camera, float zoomInput, float deltaTime) {
    if (isRemote) return; // Remote characters don't process input
    if (!partsList || rootPartIndex < 0 || rootPartIndex >= partsList->size()) return;
    Part& root = (*partsList)[rootPartIndex];
    if (root.deleted) return; // Character is deleted
    rp3d::RigidBody* body = (rp3d::RigidBody*)root.physicsBody;
    if (!body) return;
    
    zoomDistance -= zoomInput * 2.0f;
    if (zoomDistance < 2.0f) zoomDistance = 2.0f;
    if (zoomDistance > 50.0f) zoomDistance = 50.0f;

    float moveSpeed = 20.0f;
    
    float yawRad = glm::radians(camera.Yaw);
    glm::vec3 camFront;
    camFront.x = cos(yawRad) * cos(glm::radians(0.0f)); 
    camFront.y = 0.0f;
    camFront.z = sin(yawRad) * cos(glm::radians(0.0f));
    camFront = glm::normalize(camFront);
    
    glm::vec3 camRight = glm::normalize(glm::cross(camFront, glm::vec3(0, 1, 0)));

    glm::vec3 moveDir(0.0f);
    if (forward) moveDir += camFront;
    if (backward) moveDir -= camFront;
    if (right) moveDir += camRight;
    if (left) moveDir -= camRight;
    
    if (glm::length(moveDir) > 0.1f) {
        moveDir = glm::normalize(moveDir);
        body->setIsActive(true);

        rp3d::Vector3 currentVel = body->getLinearVelocity();
        rp3d::Vector3 targetVel(moveDir.x * moveSpeed, currentVel.y, moveDir.z * moveSpeed);
        body->setLinearVelocity(targetVel);
        
        // Calculate target yaw from movement direction
        // atan2(x, z) gives angle in XZ plane, convert to degrees
        float angleRad = atan2(moveDir.x, moveDir.z);
        float targetYaw = normalizeDegrees(glm::degrees(angleRad));
        float currentYaw = normalizeDegrees(internalYaw);
        
        debugTargetYaw = targetYaw;
        debugCurrentYaw = currentYaw;
        debugMoveDir = moveDir;

        // Fast rotation towards movement direction
        float diff = normalizeDegrees(targetYaw - currentYaw);
        
        // Faster rotation for more responsive feel
        float lerpFactor = 15.0f; 
        float step = diff * lerpFactor * deltaTime; 
        
        internalYaw = normalizeDegrees(internalYaw + step);

        body->setAngularVelocity(rp3d::Vector3(0, 0, 0));

    } else {
        rp3d::Vector3 currentVel = body->getLinearVelocity();
        body->setLinearVelocity(rp3d::Vector3(0, currentVel.y, 0));
        body->setAngularVelocity(rp3d::Vector3(0, 0, 0));
        debugCurrentYaw = internalYaw;
        debugTargetYaw = internalYaw;
        debugMoveDir = glm::vec3(0.0f); // Clear move dir to stop animation state
    }
    
    // Always use internalYaw (which smoothly interpolates to target when moving)
    root.rotation.y = internalYaw;
    
    rp3d::Transform t = body->getTransform();
    rp3d::Quaternion q = rp3d::Quaternion::fromEulerAngles(0, glm::radians(internalYaw), 0); 
    t.setOrientation(q);
    body->setTransform(t);
    
    if (jump) {
        rp3d::Vector3 vel = body->getLinearVelocity();
        if (std::abs(vel.y) < 0.1f) {
            body->setIsActive(true);
            body->setLinearVelocity(rp3d::Vector3(vel.x, 12.0f, vel.z)); 
        }
    }
}

glm::vec3 Character::getPosition() const {
    if (partsList && rootPartIndex >= 0 && rootPartIndex < partsList->size()) {
        const Part& root = (*partsList)[rootPartIndex];
        if (!root.deleted) {
            return root.position;
        }
    }
    return glm::vec3(0);
}

void Character::takeDamage(float amount) {
    health -= amount;
    if (health <= 0.0f) {
        respawn();
    }
}

void Character::respawn() {
    health = maxHealth;
    if (partsList && rootPartIndex >= 0 && rootPartIndex < partsList->size()) {
        Part& root = (*partsList)[rootPartIndex];
        if (root.deleted) return; // Character is deleted
        
        // Reset Remote State as well
        if (isRemote) {
            root.position = spawnPoint;
            return;
        }
        
        rp3d::RigidBody* body = (rp3d::RigidBody*)root.physicsBody;
        if (body) {
            rp3d::Transform t = body->getTransform();
            t.setPosition(rp3d::Vector3(spawnPoint.x, spawnPoint.y + 2.5f, spawnPoint.z)); 
            body->setTransform(t);
            body->setLinearVelocity(rp3d::Vector3(0, 0, 0));
            body->setAngularVelocity(rp3d::Vector3(0, 0, 0));
        }
    }
}
