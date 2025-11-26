#pragma once

#include <reactphysics3d/reactphysics3d.h>
#include <vector>
#include <map>
#include "Part.h"

class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();

    void init();
    void update(float deltaTime);
    
    void addPart(Part* part);
    void removePart(Part* part);
    void updatePartBody(Part* part);
    void reset();

    // Raycasting
    struct RaycastResult {
        Part* part;
        glm::vec3 hitPoint;
        glm::vec3 hitNormal;
        float hitFraction;
    };
    
    bool raycast(const glm::vec3& start, const glm::vec3& end, RaycastResult& result, Part* ignorePart = nullptr);

    // Welding
    void createWeld(Part* partA, Part* partB);

// Make these public or provide accessors, but for now public for Server usage
    rp3d::PhysicsCommon physicsCommon;
    rp3d::PhysicsWorld* world;

private:
    std::map<Part*, rp3d::RigidBody*> bodyMap;
    std::map<Part*, rp3d::CollisionShape*> shapeMap;
    std::map<Part*, rp3d::PolyhedronMesh*> meshMap; // For Polyhedron shapes (Wedge)
    std::map<Part*, rp3d::PolygonVertexArray*> vertexArrayMap; // For Polyhedron shapes
    std::map<Part*, float*> vertexDataMap; // For Polyhedron shapes
    std::map<Part*, int*> indicesDataMap; // For Polyhedron shapes
    std::map<Part*, rp3d::PolygonVertexArray::PolygonFace*> facesDataMap; // For Polyhedron shapes
};
