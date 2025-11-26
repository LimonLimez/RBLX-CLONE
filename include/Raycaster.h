#pragma once

#include <glm/glm.hpp>
#include <deque>
#include "Part.h"
#include "Camera.h"

class Raycaster {
public:
    // Returns index of selected part, or -1 if none
    static int GetPartFromMouse(const std::deque<Part>& parts, const Camera& camera, float mouseX, float mouseY, float screenWidth, float screenHeight);

private:
    static bool RayCubeIntersection(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const Part& part, float& distance);
};

