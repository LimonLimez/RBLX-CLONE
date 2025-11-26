#include "WorldLoader.h"
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
    if (!(in >> count)) return;
    
    struct TempWeld {
        int partIndex;
        std::vector<int> targets;
    };
    std::vector<TempWeld> tempWelds;
    
    for (size_t i = 0; i < count; i++) {
        int shapeInt;
        in >> shapeInt;
        
        Part p;
        p.shape = (ShapeType)shapeInt;
        
        in >> std::ws;
        if (in.peek() == '"') {
            in >> std::quoted(p.name);
        } else {
            in >> p.name; 
        }
        
        in >> p.parentIndex >> p.isFolder >> p.isCamera >> p.isSpawn;
        
        in >> p.position.x >> p.position.y >> p.position.z
           >> p.size.x >> p.size.y >> p.size.z
           >> p.color.x >> p.color.y >> p.color.z
           >> p.rotation.x >> p.rotation.y >> p.rotation.z
           >> p.transparency >> p.reflectance
           >> p.anchored >> p.canCollide;
           
        size_t weldCount;
        in >> weldCount;
        
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

