#include <vector>
#include <fstream>
#include <filesystem>
#include "miniz.h"

bool ExtractZip(const std::vector<uint8_t>& zipBytes, const std::filesystem::path& outputDir, const std::string& folderName) {
    const std::filesystem::path targetDir = outputDir / folderName;
    std::filesystem::create_directories(targetDir);

    const std::filesystem::path tempZipPath = outputDir / (folderName + ".tmpdownload.zip");
    {
        std::ofstream zipOut(tempZipPath, std::ios::binary | std::ios::trunc);
        if (!zipOut.is_open()) {
            return false;
        }

        zipOut.write(reinterpret_cast<const char*>(zipBytes.data()), static_cast<std::streamsize>(zipBytes.size()));
        if (!zipOut.good()) {
            zipOut.close();
            std::error_code removeErr;
            std::filesystem::remove(tempZipPath, removeErr);
            return false;
        }
    }

    mz_zip_archive archive{};
    if (!mz_zip_reader_init_file(&archive, tempZipPath.string().c_str(), 0)) {
        std::error_code removeErr;
        std::filesystem::remove(tempZipPath, removeErr);
        return false;
    }

    bool allExtracted = true;

    const auto count = mz_zip_reader_get_num_files(&archive);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&archive, i, &stat)) {
            allExtracted = false;
            continue;
        }

        std::filesystem::path relativePath = stat.m_filename;
        if (relativePath.is_absolute()) {
            relativePath = relativePath.lexically_relative(relativePath.root_path());
        }

        if (relativePath.empty()) {
            continue;
        }

        std::filesystem::path outPath = targetDir / relativePath;

        if (std::string(stat.m_filename).ends_with("/")) {
            std::filesystem::create_directories(outPath);
            continue;
        }

        std::filesystem::create_directories(outPath.parent_path());

        if (!mz_zip_reader_extract_to_file(
                &archive,
                i,
                outPath.string().c_str(),
                0)) {
            allExtracted = false;
            continue;
        }
    }

    mz_zip_reader_end(&archive);

    std::error_code removeErr;
    std::filesystem::remove(tempZipPath, removeErr);

    return allExtracted;
}