#include <vector>
#include <fstream>
#include <filesystem>

bool ExtractZip(const std::vector<uint8_t>& zipBytes, const std::filesystem::path& outputDir, const std::string& folderName);