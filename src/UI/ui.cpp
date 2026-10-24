#include "bsml/shared/BSML.hpp"
#include "songcore/shared/SongLoader/RuntimeSongLoader.hpp"
#include "../config.hpp"
#include "../utils/requests.hpp"
#include "../utils/zip/extract.hpp"

#include <filesystem>

#include "main.hpp"

#include <cstdlib>

void PullBeatmapsButton(UnityEngine::UI::Button* button) {
    if (button) {
        button->set_interactable(false);
    }

    std::future<WebUtils::JsonResponse> responseFuture = CreateRequest("/api/beatmaps/json");

    WebUtils::JsonResponse json = responseFuture.get();

    for (auto& beatmap : json.GetParsedData()["version"]["customLevels"].GetArray()) {
        auto bytes = DownloadZipBytes(std::string("/api/beatmaps/download/") + beatmap["name"].GetString());

        logger.info("Downloaded {} bytes", bytes.size());

        if (json.GetParsedData()["version"]["source"].GetString() == std::string("CustomLevels")) {
            ExtractZip(bytes, std::filesystem::path("/sdcard/ModData/com.beatgames.beatsaber/Mods/SongCore/CustomLevels"));
        } else if (json.GetParsedData()["version"]["source"].GetString() == std::string("CustomWIPLevels")) {
            ExtractZip(bytes, std::filesystem::path("/sdcard/ModData/com.beatgames.beatsaber/Mods/SongCore/CustomWIPLevels"));
        }
    }

    SongCore::SongLoader::RuntimeSongLoader::get_instance()->RefreshSongs(false);

    if (button) {
        button->set_interactable(true);
    }
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

    UnityEngine::UI::Button* pullButton = nullptr;
    pullButton = BSML::Lite::CreateUIButton(container, "Pull Beatmaps", [&pullButton]() {
        PullBeatmapsButton(pullButton);
    });
}