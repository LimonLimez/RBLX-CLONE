#pragma once

#include <string>
#include <deque>
#include "Part.h"
#include "PhysicsWorld.h"

class WorldLoader {
public:
    static void loadWorld(const std::string& filename, std::deque<Part>& parts, PhysicsWorld& physicsWorld);
};

