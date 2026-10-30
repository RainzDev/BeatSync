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
#include <string>
#include <thread>

#include "main.hpp"

#include <cstdlib>

static bool g_isPullingBeatmaps = false;
static UnityEngine::UI::Button* g_pullBeatmapsButton = nullptr;
static BSML::SliderSetting* g_pullProgressBar = nullptr;

static bool IsRemapperFolderLevel(SongCore::SongLoader::CustomBeatmapLevel* level) {
    if (!level) {
        return false;
    }

    const std::filesystem::path levelPath(level->get_customLevelPath());
    const std::string folderName = levelPath.filename().string();
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
        auto createdPlaylist = PlaylistCore::AddPlaylist("Remapper Output", "BeatSync Mod", ConvertImageFileToSprite("/sdcard/ModData/com.beatgames.beatsaber/Mods/BeatSync/logo.png"));
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
        PlaylistCore::AddSongToPlaylist(playlist, remapperLevels[i]);
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
    }

    if (g_pullProgressBar) {
        g_pullProgressBar->set_Value(0.0f);
    }

    std::thread([button]() {
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
                    if (std::filesystem::exists(SongCore::API::Loading::GetPreferredCustomWIPLevelPath() / beatmap["name"].GetString())) {
                        std::filesystem::remove_all(SongCore::API::Loading::GetPreferredCustomWIPLevelPath() / beatmap["name"].GetString());
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

            BSML::MainThreadScheduler::Schedule([button, shouldSyncRemapperPlaylist]() {
                if (button) {
                    button->set_interactable(true);
                    BSML::Lite::SetButtonText(button, "Pull Beatmaps");
                }

                if (shouldSyncRemapperPlaylist) {
                    AddRemapperLevelsToPlaylistWithProgress();
                } else {
                    SetProgressValue(100.0f);
                }
            });

            g_isPullingBeatmaps = false;
        } catch (...) {
            BSML::MainThreadScheduler::Schedule([button]() {
                if (button) {
                    button->set_interactable(true);
                    BSML::Lite::SetButtonText(button, "Pull Beatmaps");
                }

                if (g_pullProgressBar) {
                    g_pullProgressBar->set_Value(0.0f);
                }
            });

            g_isPullingBeatmaps = false;
        }
    }).detach();
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

    BSML::Lite::CreateToggle(container, "Automatic Pull", getConfig().AutomaticPull.GetValue(), [](bool value) {
        getConfig().AutomaticPull.SetValue(value);
    });
}