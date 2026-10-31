#include "bsml/shared/BSML.hpp"
#include "bsml/shared/BSML/Components/Settings/SliderSetting.hpp"
#include "songcore/shared/SongCore.hpp"
#include "songcore/shared/SongLoader/RuntimeSongLoader.hpp"
#include "../config.hpp"
#include "../utils/requests.hpp"
#include "../utils/zip/extract.hpp"
#include "../utils/Sprite/convertsprite.hpp"

#include "playlistcore/shared/PlaylistCore.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "main.hpp"

#include "../assets/logo_bytes.inc"

#include <cstdlib>

static bool g_isPullingBeatmaps = false;
static UnityEngine::UI::Button* g_pullBeatmapsButton = nullptr;
static BSML::SliderSetting* g_pullProgressBar = nullptr;

static std::filesystem::path GetPrimaryLogoPath() {
    return std::filesystem::path("/sdcard/ModData/com.beatgames.beatsaber/Mods") / MOD_ID / "logo.png";
}

static std::filesystem::path GetLegacyLogoPath() {
    return "/sdcard/ModData/com.beatgames.beatsaber/Mods/BeatSync/logo.png";
}

void EnsureLogoPathOnLateLoad() {
    try {
        const std::filesystem::path primaryLogoPath = GetPrimaryLogoPath();
        std::error_code ec;

        std::filesystem::create_directories(primaryLogoPath.parent_path(), ec);
        if (ec) {
            logger.warn("Failed creating logo directory '{}': {}", primaryLogoPath.parent_path().string(), ec.message());
            ec.clear();
        }

        if (std::filesystem::exists(primaryLogoPath)) {
            auto* existingSprite = ConvertImageFileToSprite(primaryLogoPath.string());
            if (existingSprite) {
                return;
            }

            logger.warn("Existing logo at '{}' is invalid, rebuilding it", primaryLogoPath.string());
            std::filesystem::remove(primaryLogoPath, ec);
            if (ec) {
                logger.warn("Failed removing invalid logo '{}': {}", primaryLogoPath.string(), ec.message());
                ec.clear();
            }
        }

        const std::filesystem::path legacyLogoPath = GetLegacyLogoPath();
        if (std::filesystem::exists(legacyLogoPath)) {
            std::filesystem::copy_file(legacyLogoPath, primaryLogoPath, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                logger.warn("Failed copying legacy logo '{}' to '{}': {}", legacyLogoPath.string(), primaryLogoPath.string(), ec.message());
            } else {
                if (ConvertImageFileToSprite(primaryLogoPath.string())) {
                    logger.info("Copied logo to '{}' from legacy BeatSync path", primaryLogoPath.string());
                    return;
                }

                logger.warn("Copied legacy logo to '{}' but sprite decode failed, writing fallback", primaryLogoPath.string());
            }
        }

        std::ofstream out(primaryLogoPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            logger.warn("Failed creating fallback logo at '{}'", primaryLogoPath.string());
            return;
        }

        out.write(reinterpret_cast<const char*>(src_assets_logo_png), static_cast<std::streamsize>(src_assets_logo_png_len));
        out.close();

        if (!out) {
            logger.warn("Failed writing fallback logo at '{}'", primaryLogoPath.string());
            return;
        }

        logger.info("Created fallback logo at '{}'", primaryLogoPath.string());
    } catch (const std::exception& ex) {
        logger.error("EnsureLogoPathOnLateLoad failed: {}", ex.what());
    } catch (...) {
        logger.error("EnsureLogoPathOnLateLoad failed with unknown exception");
    }
}

static void SetPullButtonState(bool isPulling) {
    BSML::MainThreadScheduler::Schedule([isPulling]() {
        if (!g_pullBeatmapsButton) {
            return;
        }

        g_pullBeatmapsButton->set_interactable(!isPulling);
        BSML::Lite::SetButtonText(g_pullBeatmapsButton, isPulling ? "Pulling..." : "Pull Beatmaps");
    });
}

static bool IsRemapperFolderLevel(SongCore::SongLoader::CustomBeatmapLevel* level) {
    if (!level) {
        return false;
    }

    const auto customLevelPath = level->get_customLevelPath();
    if (customLevelPath.empty()) {
        return false;
    }

    std::string folderName;
    try {
        const std::filesystem::path levelPath(customLevelPath);
        folderName = levelPath.filename().string();
    } catch (const std::exception& ex) {
        logger.warn("Skipping level with invalid custom path while syncing remapper playlist: {}", ex.what());
        return false;
    }

    static constexpr std::string_view suffix = "_Remapper";

    return folderName.size() >= suffix.size() &&
           folderName.compare(folderName.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static void SetProgressValue(float progress) {
    BSML::MainThreadScheduler::Schedule([progress]() {
        if (g_pullProgressBar) {
            g_pullProgressBar->set_Value(progress);
        }
    });
}

static void AddRemapperLevelsToPlaylistWithProgress() {
    std::string playlistPath = getConfig().PlaylistPath.GetValue();
    PlaylistCore::Playlist* playlist = nullptr;

    if (!playlistPath.empty()) {
        playlist = PlaylistCore::GetPlaylist(playlistPath);
    }

    if (!playlist) {
        const std::string modLogoPath = GetPrimaryLogoPath().string();
        UnityEngine::Sprite* playlistCover = ConvertImageFileToSprite(modLogoPath);
        if (!playlistCover) {
            playlistCover = ConvertImageFileToSprite(GetLegacyLogoPath().string());
        }

        auto createdPlaylist = PlaylistCore::AddPlaylist("Remapper Output", "BeatSync Mod", playlistCover);
        playlistPath = createdPlaylist.first;
        playlist = createdPlaylist.second;

        if (!playlistPath.empty()) {
            getConfig().PlaylistPath.SetValue(playlistPath);
        }

        if (!playlist && !playlistPath.empty()) {
            playlist = PlaylistCore::GetPlaylist(playlistPath);
        }
    }

    if (!playlist) {
        logger.error("Unable to resolve Remapper Output playlist");
        return;
    }

    std::vector<SongCore::SongLoader::CustomBeatmapLevel*> remapperLevels;
    for (auto* level : SongCore::API::Loading::GetAllLevels()) {
        if (IsRemapperFolderLevel(level)) {
            remapperLevels.push_back(level);
        }
    }

    if (remapperLevels.empty()) {
        SetProgressValue(100.0f);
        return;
    }

    for (size_t i = 0; i < remapperLevels.size(); ++i) {
        auto* remapperLevel = remapperLevels[i];
        if (!remapperLevel) {
            logger.warn("Skipping null remapper level while syncing playlist");
            continue;
        }

        try {
            PlaylistCore::AddSongToPlaylist(playlist, remapperLevel);
        } catch (const std::exception& ex) {
            logger.error("Failed adding remapper level to playlist: {}", ex.what());
        } catch (...) {
            logger.error("Failed adding remapper level to playlist with unknown exception");
        }

        const float progress = 90.0f + (10.0f * static_cast<float>(i + 1) / static_cast<float>(remapperLevels.size()));
        SetProgressValue(progress);
    }
}

void PullBeatmapsButton(UnityEngine::UI::Button* button) {
    if (g_isPullingBeatmaps) {
        return;
    }

    g_isPullingBeatmaps = true;

    if (button) {
        button->set_interactable(false);
        BSML::Lite::SetButtonText(button, "Pulling...");
    } else {
        SetPullButtonState(true);
    }

    if (g_pullProgressBar) {
        g_pullProgressBar->set_Value(0.0f);
    }

    std::thread([]() {
        try {
            auto responseFuture = CreateRequest("/api/beatmaps/json");
            WebUtils::JsonResponse json = responseFuture.get();

            const auto& source = json.GetParsedData()["version"]["source"].GetString();
            const std::string sourceStr = source;
            const bool shouldSyncRemapperPlaylist = sourceStr == "RemapperOutput";
            const auto& customLevels = json.GetParsedData()["version"]["customLevels"];
            const auto totalBeatmaps = customLevels.Size();
            int processedBeatmaps = 0;

            for (auto& beatmap : customLevels.GetArray()) {
                auto bytes = DownloadZipBytes(std::string("/api/beatmaps/download/") + beatmap["name"].GetString());

                logger.info("Downloaded {} bytes", bytes.size());

                if (sourceStr == "CustomLevels") {
                    if (std::filesystem::exists(SongCore::API::Loading::GetPreferredCustomLevelPath() / beatmap["name"].GetString())) {
                        std::filesystem::remove_all(SongCore::API::Loading::GetPreferredCustomLevelPath() / beatmap["name"].GetString());
                    }
                    ExtractZip(bytes, SongCore::API::Loading::GetPreferredCustomLevelPath(), beatmap["name"].GetString());
                } else if (sourceStr == "CustomWIPLevels") {
                    if (std::filesystem::exists(SongCore::API::Loading::GetPreferredCustomWIPLevelPath() / beatmap["name"].GetString())) {
                        std::filesystem::remove_all(SongCore::API::Loading::GetPreferredCustomWIPLevelPath() / beatmap["name"].GetString());
                    }
                    ExtractZip(bytes, SongCore::API::Loading::GetPreferredCustomWIPLevelPath(), beatmap["name"].GetString());
                } else if (sourceStr == "RemapperOutput") {
                    if (std::filesystem::exists(SongCore::API::Loading::GetPreferredCustomWIPLevelPath() / (beatmap["name"].GetString() + std::string("_Remapper")))) {
                        std::filesystem::remove_all(SongCore::API::Loading::GetPreferredCustomWIPLevelPath() / (beatmap["name"].GetString() + std::string("_Remapper")));
                    }
                    ExtractZip(bytes, SongCore::API::Loading::GetPreferredCustomWIPLevelPath(), beatmap["name"].GetString() + std::string("_Remapper"));
                }

                ++processedBeatmaps;
                const float progress = totalBeatmaps > 0
                    ? (static_cast<float>(processedBeatmaps) / static_cast<float>(totalBeatmaps)) * 80.0f
                    : 80.0f;

                SetProgressValue(progress);
            }

            auto refreshFuture = SongCore::API::Loading::RefreshSongs(true);
            if (refreshFuture.valid()) {
                refreshFuture.wait();
            }

            SetProgressValue(90.0f);

            SetPullButtonState(false);

            BSML::MainThreadScheduler::Schedule([shouldSyncRemapperPlaylist]() {
                if (shouldSyncRemapperPlaylist) {
                    AddRemapperLevelsToPlaylistWithProgress();
                } else {
                    SetProgressValue(100.0f);
                }
            });

            g_isPullingBeatmaps = false;
        } catch (const std::exception& ex) {
            logger.error("Pull beatmaps failed: {}", ex.what());
            SetPullButtonState(false);

            BSML::MainThreadScheduler::Schedule([]() {
                if (g_pullProgressBar) {
                    g_pullProgressBar->set_Value(0.0f);
                }
            });

            g_isPullingBeatmaps = false;
        } catch (...) {
            logger.error("Pull beatmaps failed with unknown exception");
            SetPullButtonState(false);

            BSML::MainThreadScheduler::Schedule([]() {
                if (g_pullProgressBar) {
                    g_pullProgressBar->set_Value(0.0f);
                }
            });

            g_isPullingBeatmaps = false;
        }
    }).detach();
}

void TriggerAutomaticPullOnLateLoad() {
    if (!getConfig().AutomaticPull.GetValue()) {
        return;
    }

    PullBeatmapsButton(nullptr);
}


void DidActivate(HMUI::ViewController* self, bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling) {
    // Only create the elements once per menu
    if (!firstActivation)
        return;

    auto container = BSML::Lite::CreateScrollableSettingsContainer(self);

    BSML::Lite::CreateStringSetting(container, "IP Address", getConfig().IPAddress.GetValue(), [](std::string value) {
        getConfig().IPAddress.SetValue(value);
    });

    BSML::Lite::CreateStringSetting(container, "Port", getConfig().Port.GetValue(), [](std::string value) {
        getConfig().Port.SetValue(value);
    });

    g_pullProgressBar = BSML::Lite::CreateSliderSetting(
        container,
        "Pull Progress",
        1.0f,
        0.0f,
        0.0f,
        100.0f,
        0.0f,
        false,
        {0, 0},
        nullptr
    );

    if (g_pullProgressBar) {
        g_pullProgressBar->set_interactable(false);
        g_pullProgressBar->set_Value(0.0f);
    }

    g_pullBeatmapsButton = BSML::Lite::CreateUIButton(container, "Pull Beatmaps", []() {
        if (g_isPullingBeatmaps) {
            return;
        }

        PullBeatmapsButton(g_pullBeatmapsButton);
    });

    if (g_isPullingBeatmaps) {
        g_pullBeatmapsButton->set_interactable(false);
        BSML::Lite::SetButtonText(g_pullBeatmapsButton, "Pulling...");
    }

    BSML::Lite::CreateToggle(container, "Automatic Pull", getConfig().AutomaticPull.GetValue(), [](bool value) {
        getConfig().AutomaticPull.SetValue(value);
    });
}