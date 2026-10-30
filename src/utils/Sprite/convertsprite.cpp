#include "bsml/shared/Helpers/utilities.hpp"
#include "UnityEngine/Texture2D.hpp"
#include "UnityEngine/Sprite.hpp"

#include <fstream>
#include <string>
#include <vector>

UnityEngine::Sprite* ConvertImageFileToSprite(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    
    const auto len = file.tellg();

    file.seekg(0, std::ios::beg);
    ArrayW<uint8_t> fileBytes((len));
    file.read((char*)fileBytes.begin(), len);

    return BSML::Utilities::LoadSpriteRaw(fileBytes);
}