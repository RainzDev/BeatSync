#include "bsml/shared/BSML.hpp"
#include "songcore/shared/SongLoader/RuntimeSongLoader.hpp"
#include "../config.hpp"
#include "../utils/requests.hpp"
#include "../utils/zip/extract.hpp"

#include <filesystem>

#include "main.hpp"

#include <cstdlib>

static bool g_isPullingBeatmaps = false;
static UnityEngine::UI::Button* g_pullBeatmapsButton = nullptr;

void PullBeatmapsButton(UnityEngine::UI::Button* button) {
    if (g_isPullingBeatmaps) {
        return;
    }

    g_isPullingBeatmaps = true;

    if (button) {
        button->set_interactable(false);
        BSML::Lite::SetButtonText(button, "Pulling...");
    }

    std::thread([button]() {
        try {
            auto responseFuture = CreateRequest("/api/beatmaps/json");
            WebUtils::JsonResponse json = responseFuture.get();

            const auto& source = json.GetParsedData()["version"]["source"].GetString();
            const auto& customLevels = json.GetParsedData()["version"]["customLevels"];

            for (auto& beatmap : customLevels.GetArray()) {
                auto bytes = DownloadZipBytes(std::string("/api/beatmaps/download/") + beatmap["name"].GetString());

                logger.info("Downloaded {} bytes", bytes.size());

                if (std::string(source) == std::string("CustomLevels")) {
                    ExtractZip(bytes, std::filesystem::path("/sdcard/ModData/com.beatgames.beatsaber/Mods/SongCore/CustomLevels"), beatmap["name"].GetString());
                } else if (std::string(source) == std::string("CustomWIPLevels")) {
                    ExtractZip(bytes, std::filesystem::path("/sdcard/ModData/com.beatgames.beatsaber/Mods/SongCore/CustomWIPLevels"), beatmap["name"].GetString());
                }
            }

            BSML::MainThreadScheduler::Schedule([button]() {
                if (button) {
                    button->set_interactable(true);
                    BSML::Lite::SetButtonText(button, "Pull Beatmaps");
                }

                SongCore::SongLoader::RuntimeSongLoader::get_instance()->RefreshSongs(false);
            });

            g_isPullingBeatmaps = false;
        } catch (...) {
            BSML::MainThreadScheduler::Schedule([button]() {
                if (button) {
                    button->set_interactable(true);
                    BSML::Lite::SetButtonText(button, "Pull Beatmaps");
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

    g_pullBeatmapsButton = BSML::Lite::CreateUIButton(container, "Pull Beatmaps", []() {
        if (g_isPullingBeatmaps) {
            return;
        }

        PullBeatmapsButton(g_pullBeatmapsButton);
    });
}