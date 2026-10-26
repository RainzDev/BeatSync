#pragma once

#include "UnityEngine/Texture2D.hpp"
#include "UnityEngine/Sprite.hpp"
#include <string>

UnityEngine::Sprite* ConvertImageFileToSprite(const std::string& filePath);