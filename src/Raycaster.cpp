#include "Raycaster.h"
#include <limits>

int Raycaster::GetPartFromMouse(const std::deque<Part>& parts, const Camera& camera, float mouseX, float mouseY, float screenWidth, float screenHeight) {
    // 1. Convert Mouse (Screen) -> NDC
    float x = (2.0f * mouseX) / screenWidth - 1.0f;
    float y = 1.0f - (2.0f * mouseY) / screenHeight;
    float z = 1.0f;
    glm::vec3 ray_nds = glm::vec3(x, y, z);

    // 2. NDC -> Homogeneous Clip
    glm::vec4 ray_clip = glm::vec4(ray_nds.x, ray_nds.y, -1.0, 1.0);

    // 3. Clip -> Camera (Eye)
    // Need projection matrix to inverse
    glm::mat4 projection = ((Camera&)camera).GetProjectionMatrix(screenWidth, screenHeight); // Hack: cast const away or make GetProjection const
    glm::vec4 ray_eye = glm::inverse(projection) * ray_clip;
    ray_eye = glm::vec4(ray_eye.x, ray_eye.y, -1.0, 0.0);

    // 4. Eye -> World
    glm::mat4 view = ((Camera&)camera).GetViewMatrix();
    glm::vec3 ray_wor = glm::vec3(glm::inverse(view) * ray_eye);
    ray_wor = glm::normalize(ray_wor);

    // 5. Raycast against all parts
    int closestPart = -1;
    float minDist = std::numeric_limits<float>::max();

    for (int i = 0; i < parts.size(); i++) {
        float dist;
        if (RayCubeIntersection(camera.Position, ray_wor, parts[i], dist)) {
            if (dist < minDist) {
                minDist = dist;
                closestPart = i;
            }
        }
    }

    return closestPart;
}

bool Raycaster::RayCubeIntersection(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const Part& part, float& distance) {
    // Simple AABB slab method
    glm::vec3 minBound = part.position - (part.size * 0.5f);
    glm::vec3 maxBound = part.position + (part.size * 0.5f);

    float t1 = (minBound.x - rayOrigin.x) / rayDir.x;
    float t2 = (maxBound.x - rayOrigin.x) / rayDir.x;
    float t3 = (minBound.y - rayOrigin.y) / rayDir.y;
    float t4 = (maxBound.y - rayOrigin.y) / rayDir.y;
    float t5 = (minBound.z - rayOrigin.z) / rayDir.z;
    float t6 = (maxBound.z - rayOrigin.z) / rayDir.z;

    float tmin = std::max(std::max(std::min(t1, t2), std::min(t3, t4)), std::min(t5, t6));
    float tmax = std::min(std::min(std::max(t1, t2), std::max(t3, t4)), std::max(t5, t6));

    // if tmax < 0, ray (line) is intersecting AABB, but whole AABB is behind us
    if (tmax < 0) {
        distance = tmax;
        return false;
    }

    // if tmin > tmax, ray doesn't intersect AABB
    if (tmin > tmax) {
        distance = tmax;
        return false;
    }

    distance = tmin;
    return true;
}

