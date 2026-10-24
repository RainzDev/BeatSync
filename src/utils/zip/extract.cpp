#include <vector>
#include <fstream>
#include <filesystem>
#include "miniz.h"

bool ExtractZip(const std::vector<uint8_t>& zipBytes, const std::filesystem::path& outputDir, const std::string& folderName) {
    const std::filesystem::path targetDir = outputDir / folderName;
    std::filesystem::create_directories(targetDir);

    mz_zip_archive archive{};
    if (!mz_zip_reader_init_mem(&archive, zipBytes.data(), zipBytes.size(), 0)) {
        return false;
    }

    const auto count = mz_zip_reader_get_num_files(&archive);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&archive, i, &stat)) {
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

        std::vector<uint8_t> buffer(stat.m_uncomp_size);
        if (!mz_zip_reader_extract_to_mem(
                &archive,
                i,
                buffer.data(),
                buffer.size(),
                0)) {
            continue;
        }

        std::ofstream out(outPath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    }

    mz_zip_reader_end(&archive);
    return true;
}