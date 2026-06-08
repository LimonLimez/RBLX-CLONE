#include "WorldLoader.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iomanip>

void WorldLoader::loadWorld(const std::string& filename, std::deque<Part>& parts, PhysicsWorld& physicsWorld) {
    std::ifstream in(filename);
    if (!in.is_open()) {
        std::cerr << "Failed to open world file: " << filename << std::endl;
        return;
    }
    
    physicsWorld.reset();
    parts.clear();
    
    size_t count;
    if (!(in >> count)) {
        std::cerr << "Malformed world file header: " << filename << std::endl;
        return;
    }

    const size_t maxParts = 4096;
    if (count > maxParts) {
        std::cerr << "World file has too many parts: " << count << std::endl;
        return;
    }
    
    struct TempWeld {
        int partIndex;
        std::vector<int> targets;
    };
    std::vector<TempWeld> tempWelds;
    
    for (size_t i = 0; i < count; i++) {
        int shapeInt;
        if (!(in >> shapeInt) || shapeInt < 0 || shapeInt > static_cast<int>(ShapeType::Cylinder)) {
            std::cerr << "Malformed or unsupported part shape at index " << i << std::endl;
            parts.clear();
            physicsWorld.reset();
            return;
        }
        
        Part p;
        p.shape = (ShapeType)shapeInt;
        
        in >> std::ws;
        if (in.peek() == '"') {
            in >> std::quoted(p.name);
        } else {
            in >> p.name; 
        }
        
        if (!(in >> p.parentIndex >> p.isFolder >> p.isCamera >> p.isSpawn)) {
            std::cerr << "Malformed hierarchy flags at part " << i << std::endl;
            parts.clear();
            physicsWorld.reset();
            return;
        }
        
        if (!(in >> p.position.x >> p.position.y >> p.position.z
                 >> p.size.x >> p.size.y >> p.size.z
                 >> p.color.x >> p.color.y >> p.color.z
                 >> p.rotation.x >> p.rotation.y >> p.rotation.z
                 >> p.transparency >> p.reflectance
                 >> p.anchored >> p.canCollide)) {
            std::cerr << "Malformed transform/material data at part " << i << std::endl;
            parts.clear();
            physicsWorld.reset();
            return;
        }

        auto finiteVec3 = [](const glm::vec3& value) {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        };

        if (!finiteVec3(p.position) || !finiteVec3(p.size) || !finiteVec3(p.color) || !finiteVec3(p.rotation)) {
            std::cerr << "Non-finite values in world part " << i << std::endl;
            parts.clear();
            physicsWorld.reset();
            return;
        }

        p.size.x = std::clamp(p.size.x, 0.05f, 512.0f);
        p.size.y = std::clamp(p.size.y, 0.05f, 512.0f);
        p.size.z = std::clamp(p.size.z, 0.05f, 512.0f);
        p.color.x = std::clamp(p.color.x, 0.0f, 1.0f);
        p.color.y = std::clamp(p.color.y, 0.0f, 1.0f);
        p.color.z = std::clamp(p.color.z, 0.0f, 1.0f);
        p.transparency = std::clamp(p.transparency, 0.0f, 1.0f);
        p.reflectance = std::clamp(p.reflectance, 0.0f, 1.0f);
           
        size_t weldCount;
        if (!(in >> weldCount) || weldCount > maxParts) {
            std::cerr << "Malformed weld data at part " << i << std::endl;
            parts.clear();
            physicsWorld.reset();
            return;
        }
        
        if (weldCount > 0) {
            TempWeld tw;
            tw.partIndex = (int)i;
            for (size_t j=0; j<weldCount; j++) {
                int targetIdx;
                in >> targetIdx;
                tw.targets.push_back(targetIdx);
            }
            tempWelds.push_back(tw);
        }
        
        parts.push_back(p);
        if (!p.isFolder) physicsWorld.addPart(&parts.back());
    }
    
    for (const auto& tw : tempWelds) {
        for (int target : tw.targets) {
            if (target >= 0 && target < parts.size()) {
                Weld w;
                w.part1 = &parts[target];
                parts[tw.partIndex].welds.push_back(w);
                if (parts[tw.partIndex].physicsBody && w.part1->physicsBody) {
                     physicsWorld.createWeld(&parts[tw.partIndex], w.part1);
                }
            }
        }
    }
    
    in.close();
}

