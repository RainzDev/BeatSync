#include "bsml/shared/Helpers/utilities.hpp"
#include "UnityEngine/Texture2D.hpp"
#include "UnityEngine/Sprite.hpp"

#include <fstream>
#include <string>
#include <vector>

UnityEngine::Sprite* ConvertImageFileToSprite(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return nullptr;
    }

    const auto endPos = file.tellg();
    if (endPos <= 0) {
        return nullptr;
    }

    std::vector<uint8_t> fileBytes(static_cast<size_t>(endPos));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(fileBytes.data()), static_cast<std::streamsize>(fileBytes.size()));
    if (!file.good() && !file.eof()) {
        return nullptr;
    }

    ArrayW<uint8_t> fileBytesArr(static_cast<il2cpp_array_size_t>(fileBytes.size()));
    std::copy(fileBytes.begin(), fileBytes.end(), fileBytesArr.begin());

    auto texture = BSML::Utilities::LoadTextureRaw(fileBytesArr);
    if (!texture) {
        return nullptr;
    }

    return BSML::Utilities::LoadSpriteFromTexture(texture);
}