#include "bsml/shared/BSML.hpp"
#include "bsml/shared/BSML/Components/Settings/SliderSetting.hpp"
#include "songcore/shared/SongCore.hpp"
#include "songcore/shared/SongLoader/RuntimeSongLoader.hpp"
#include "../config.hpp"
#include "../utils/requests.hpp"
#include "../utils/zip/extract.hpp"
#include "../utils/Sprite/convertsprite.hpp"

#include "playlistcore/shared/PlaylistCore.hpp"

#include "mdns_cpp/mdns.hpp"

#include <filesystem>
#include <atomic>
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>

#include "main.hpp"

#include "../assets/logo_bytes.inc"

#include <cstdlib>

static std::atomic_bool g_isPullingBeatmaps = false;
static UnityEngine::UI::Button* g_pullBeatmapsButton = nullptr;
static BSML::SliderSetting* g_pullProgressBar = nullptr;
static BSML::CustomListTableData* g_hostList = nullptr;
static UnityEngine::UI::Button* g_refreshHostButton = nullptr;
static UnityEngine::UI::Button* g_connectHostButton = nullptr;
static HMUI::CurvedTextMeshPro* g_hostStatusText = nullptr;
static std::string g_selectedHost;
static std::string g_selectedPort;
static std::atomic_bool g_isRefreshingHosts = false;

static void SetPullButtonState(bool isPulling);
static void SetHostSelectionState();

static std::string FormatHostDisplayName(const std::string& host) {
    if (host.empty()) {
        return "Unknown Host";
    }

    return host;
}

static void ClearHostSelectionState() {
    g_selectedHost.clear();
    g_selectedPort.clear();
    if (g_hostList && g_hostList->tableView) {
        g_hostList->tableView->ClearSelection();
    }
    SetHostSelectionState();
}

static bool IsDigitsOnly(const std::string& value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return c >= '0' && c <= '9';
    });
}

static std::pair<std::string, std::string> ExtractHostAndPortFromDisplay(const std::string& displayText) {
    const auto openParen = displayText.rfind('(');
    const auto closeParen = displayText.rfind(')');

    if (openParen != std::string::npos && closeParen != std::string::npos && closeParen > openParen) {
        std::string endpoint = displayText.substr(openParen + 1, closeParen - openParen - 1);
        const auto colon = endpoint.rfind(':');
        if (colon != std::string::npos) {
            const std::string host = endpoint.substr(0, colon);
            const std::string port = endpoint.substr(colon + 1);
            if (!host.empty() && IsDigitsOnly(port)) {
                return {host, port};
            }
        }

        return {endpoint, ""};
    }

    return {displayText, ""};
}

static void UpdateHostStatusText(const std::string& text) {
    BSML::MainThreadScheduler::Schedule([text]() {
        if (g_hostStatusText) {
            g_hostStatusText->set_text(StringW(text));
        }
    });
}

static void RefreshHostList() {
    if (!g_hostList) {
        return;
    }

    g_isRefreshingHosts.store(true);
    UpdateHostStatusText("Scanning for hosts...");

    if (g_refreshHostButton) {
        g_refreshHostButton->set_interactable(false);
    }

    ClearHostSelectionState();
    if (g_connectHostButton) {
        g_connectHostButton->set_interactable(false);
    }

    BSML::MainThreadScheduler::Schedule([]() {
        if (!g_hostList) {
            return;
        }

        if (g_hostList->tableView) {
            g_hostList->tableView->ClearSelection();
        }

        g_hostList->data.clear();
        if (g_hostList->tableView) {
            g_hostList->tableView->ReloadData();
        }
        ClearHostSelectionState();
    });

    std::thread([]() {
        std::vector<std::string> discoveredHosts;

        auto addHost = [&](const std::string& host) {
            if (host.empty()) {
                return;
            }

            if (std::find(discoveredHosts.begin(), discoveredHosts.end(), host) == discoveredHosts.end()) {
                discoveredHosts.push_back(host);
            }
        };

        try {
            mdns_cpp::mDNS mdns;
            const auto resolvedHosts = mdns.executeQueryAndGetHosts("_http._tcp.local.");
            for (const auto& host : resolvedHosts) {
                addHost(host);
            }
        } catch (const std::exception& ex) {
            logger.warn("mDNS host refresh failed: {}", ex.what());
        } catch (...) {
            logger.warn("mDNS host refresh failed with an unknown exception");
        }

        if (discoveredHosts.empty()) {
            UpdateHostStatusText("No hosts found");
        }

        BSML::MainThreadScheduler::Schedule([discoveredHosts]() {
            g_isRefreshingHosts.store(false);
            if (g_refreshHostButton) {
                g_refreshHostButton->set_interactable(true);
            }

            if (!g_hostList) {
                return;
            }

            g_hostList->data.clear();
            for (const auto& host : discoveredHosts) {
                g_hostList->data.push_back(BSML::CustomCellInfo::construct(FormatHostDisplayName(host)));
            }

            if (g_hostList->tableView) {
                g_hostList->tableView->ClearSelection();
                g_hostList->tableView->ReloadData();
            }

            ClearHostSelectionState();
            if (g_hostList && !g_hostList->data.empty()) {
                UpdateHostStatusText("Select a host to connect");
            } else {
                UpdateHostStatusText("No hosts found");
            }
        });
    }).detach();
}

static void SetHostSelectionState() {
    if (!g_connectHostButton) {
        return;
    }

    g_connectHostButton->set_interactable(!g_selectedHost.empty());

    if (g_hostStatusText) {
        if (g_selectedHost.empty()) {
            g_hostStatusText->set_text(StringW(g_isRefreshingHosts.load() ? "Scanning for hosts..." : "Select a host to connect"));
        } else {
            const std::string selectedLabel = g_selectedPort.empty()
                ? ("Selected: " + g_selectedHost)
                : ("Selected: " + g_selectedHost + ":" + g_selectedPort);
            g_hostStatusText->set_text(StringW(selectedLabel));
        }
    }
}

static void HandlePullFailureAndResetState(const std::string& reason) {
    logger.info("Pull beatmaps failed: {}", reason);
    SetPullButtonState(false);

    BSML::MainThreadScheduler::Schedule([]() {
        if (g_pullProgressBar) {
            g_pullProgressBar->set_Value(0.0f);
        }
    });

    g_isPullingBeatmaps.store(false);
}

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
        logger.info("EnsureLogoPathOnLateLoad failed: {}", ex.what());
    } catch (...) {
        logger.info("EnsureLogoPathOnLateLoad failed with unknown exception");
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
        logger.info("Unable to resolve Remapper Output playlist");
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
            logger.info("Failed adding remapper level to playlist: {}", ex.what());
        } catch (...) {
            logger.info("Failed adding remapper level to playlist with unknown exception");
        }

        const float progress = 90.0f + (10.0f * static_cast<float>(i + 1) / static_cast<float>(remapperLevels.size()));
        SetProgressValue(progress);
    }
}

void PullBeatmapsButton(UnityEngine::UI::Button* button) {
    if (g_isPullingBeatmaps.exchange(true)) {
        return;
    }

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

            const auto& parsedData = json.GetParsedData();
            if (!parsedData.IsObject()) {
                throw std::runtime_error("server response is not a JSON object");
            }

            if (!parsedData.HasMember("version") || !parsedData["version"].IsObject()) {
                throw std::runtime_error("missing or invalid 'version' in server response");
            }

            const auto& version = parsedData["version"];
            if (!version.HasMember("source") || !version["source"].IsString()) {
                throw std::runtime_error("missing or invalid 'source' in server response");
            }

            if (!version.HasMember("customLevels") || !version["customLevels"].IsArray()) {
                throw std::runtime_error("missing or invalid 'customLevels' in server response");
            }

            const auto& source = version["source"].GetString();
            const std::string sourceStr = source;
            const bool shouldSyncRemapperPlaylist = sourceStr == "RemapperOutput";
            const auto& customLevels = version["customLevels"];
            const auto totalBeatmaps = customLevels.Size();
            int processedBeatmaps = 0;

            for (auto& beatmap : customLevels.GetArray()) {
                if (!beatmap.IsObject() || !beatmap.HasMember("name") || !beatmap["name"].IsString()) {
                    logger.warn("Skipping beatmap entry with missing/invalid name field");
                    continue;
                }

                const std::string beatmapName = beatmap["name"].GetString();
                auto bytes = DownloadZipBytes(std::string("/api/beatmaps/download/") + beatmapName);

                logger.info("Downloaded {} bytes", bytes.size());

                if (sourceStr == "CustomLevels") {
                    if (std::filesystem::exists(SongCore::API::Loading::GetPreferredCustomLevelPath() / beatmapName)) {
                        std::filesystem::remove_all(SongCore::API::Loading::GetPreferredCustomLevelPath() / beatmapName);
                    }
                    ExtractZip(bytes, SongCore::API::Loading::GetPreferredCustomLevelPath(), beatmapName);
                } else if (sourceStr == "CustomWIPLevels") {
                    if (std::filesystem::exists(SongCore::API::Loading::GetPreferredCustomWIPLevelPath() / beatmapName)) {
                        std::filesystem::remove_all(SongCore::API::Loading::GetPreferredCustomWIPLevelPath() / beatmapName);
                    }
                    ExtractZip(bytes, SongCore::API::Loading::GetPreferredCustomWIPLevelPath(), beatmapName);
                } else if (sourceStr == "RemapperOutput") {
                    if (std::filesystem::exists(SongCore::API::Loading::GetPreferredCustomWIPLevelPath() / (beatmapName + std::string("_Remapper")))) {
                        std::filesystem::remove_all(SongCore::API::Loading::GetPreferredCustomWIPLevelPath() / (beatmapName + std::string("_Remapper")));
                    }
                    ExtractZip(bytes, SongCore::API::Loading::GetPreferredCustomWIPLevelPath(), beatmapName + std::string("_Remapper"));
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

            g_isPullingBeatmaps.store(false);
        } catch (const std::exception& ex) {
            HandlePullFailureAndResetState(ex.what());
        } catch (...) {
            HandlePullFailureAndResetState("unknown exception");
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

    g_hostStatusText = BSML::Lite::CreateText(container, StringW("Select a host to connect"), TMPro::FontStyles::Normal, 3.5f, {0.0f, -2.0f}, {35.0f, 4.0f});

    g_hostList = BSML::Lite::CreateScrollableList(container, {0.0f, 0.0f}, {60.0f, 50.0f}, [](int idx) {
        if (!g_hostList || idx < 0 || idx >= g_hostList->data.size()) {
            ClearHostSelectionState();
            return;
        }

        auto* cell = g_hostList->data[idx];
        if (!cell || !cell->text) {
            ClearHostSelectionState();
            return;
        }

        const auto selectedEndpoint = ExtractHostAndPortFromDisplay(std::string(cell->text));
        g_selectedHost = selectedEndpoint.first;
        g_selectedPort = selectedEndpoint.second;
        SetHostSelectionState();
    });

    g_refreshHostButton = BSML::Lite::CreateUIButton(container, "Refresh Hosts", []() {
        RefreshHostList();
    });

    g_connectHostButton = BSML::Lite::CreateUIButton(container, "Connect", []() {
        if (g_selectedHost.empty()) {
            logger.info("No host selected");
            return;
        }

        if (g_connectHostButton->GetComponentInChildren<TMPro::TextMeshProUGUI*>()->get_text() == "Connect") {
            BSML::Lite::SetButtonText(g_connectHostButton, "Connecting...");
            g_connectHostButton->set_interactable(false);
        } else {
            getConfig().Port.SetValue("");
            getConfig().IPAddress.SetValue("");
            BSML::Lite::SetButtonText(g_connectHostButton, "Connect");
            return;
        }

        if (!g_selectedPort.empty()) {
            getConfig().Port.SetValue(g_selectedPort);
        }

        if (!CheckServerHealth(g_selectedHost)) {
            logger.info("Selected host '{}' did not respond successfully to /health on port '{}'", g_selectedHost, getConfig().Port.GetValue());
            BSML::Lite::SetButtonText(g_connectHostButton, "Connect");
            g_connectHostButton->set_interactable(true);
            return;
        }

        getConfig().IPAddress.SetValue(g_selectedHost);

        BSML::Lite::SetButtonText(g_connectHostButton, "Disconnect");
        g_connectHostButton->set_interactable(true);
    });

    RefreshHostList();

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
        if (g_isPullingBeatmaps.load()) {
            return;
        }

        PullBeatmapsButton(g_pullBeatmapsButton);
    });

    if (g_isPullingBeatmaps.load()) {
        g_pullBeatmapsButton->set_interactable(false);
        BSML::Lite::SetButtonText(g_pullBeatmapsButton, "Pulling...");
    }

    BSML::Lite::CreateToggle(container, "Automatic Pull", getConfig().AutomaticPull.GetValue(), [](bool value) {
        getConfig().AutomaticPull.SetValue(value);
    });
}