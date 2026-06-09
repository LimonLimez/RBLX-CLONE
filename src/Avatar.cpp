#include "Avatar.h"
#include <fstream>
#include <sstream>
#include <iostream>

namespace {
bool isKnownFaceId(const std::string& faceId) {
    return faceId == "classic" ||
           faceId == "happy" ||
           faceId == "surprised" ||
           faceId == "smirk" ||
           faceId == "wink";
}
}

void AvatarConfig::saveToFile(const std::string& filename) {
    std::ofstream file(filename);
    if (file.is_open()) {
        file << headColor.x << " " << headColor.y << " " << headColor.z << "\n";
        file << torsoColor.x << " " << torsoColor.y << " " << torsoColor.z << "\n";
        file << leftArmColor.x << " " << leftArmColor.y << " " << leftArmColor.z << "\n";
        file << rightArmColor.x << " " << rightArmColor.y << " " << rightArmColor.z << "\n";
        file << leftLegColor.x << " " << leftLegColor.y << " " << leftLegColor.z << "\n";
        file << rightLegColor.x << " " << rightLegColor.y << " " << rightLegColor.z << "\n";
        file << (isKnownFaceId(faceId) ? faceId : "classic") << "\n";
        file.close();
        std::cout << "Avatar saved to " << filename << std::endl;
    }
}

void AvatarConfig::loadFromFile(const std::string& filename) {
    std::ifstream file(filename);
    if (file.is_open()) {
        file >> headColor.x >> headColor.y >> headColor.z;
        file >> torsoColor.x >> torsoColor.y >> torsoColor.z;
        file >> leftArmColor.x >> leftArmColor.y >> leftArmColor.z;
        file >> rightArmColor.x >> rightArmColor.y >> rightArmColor.z;
        file >> leftLegColor.x >> leftLegColor.y >> leftLegColor.z;
        file >> rightLegColor.x >> rightLegColor.y >> rightLegColor.z;
        std::string loadedFaceId;
        if (file >> loadedFaceId && isKnownFaceId(loadedFaceId)) {
            faceId = loadedFaceId;
        } else {
            faceId = "classic";
        }
        file.close();
        std::cout << "Avatar loaded from " << filename << std::endl;
    }
}


