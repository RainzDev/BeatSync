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

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>

#include "main.hpp"

#include "../assets/logo_bytes.inc"

#include <cstdlib>

static std::atomic_bool g_isPullingBeatmaps = false;
static UnityEngine::UI::Button* g_pullBeatmapsButton = nullptr;
static BSML::SliderSetting* g_pullProgressBar = nullptr;
static UnityEngine::RectTransform* g_pullProgressFillRect = nullptr;
static float g_pullProgressFillBaseWidth = 0.0f;
static UnityEngine::Vector2 g_pullProgressFillBaseOffsetMax = {0.0f, 0.0f};
static BSML::CustomListTableData* g_hostList = nullptr;
static UnityEngine::UI::Button* g_refreshHostButton = nullptr;
static UnityEngine::UI::Button* g_connectHostButton = nullptr;
static HMUI::CurvedTextMeshPro* g_hostStatusText = nullptr;
static std::string g_selectedHost;
static std::string g_selectedPort;
static std::atomic_bool g_isRefreshingHosts = false;
static std::atomic_bool g_isConnectingHost = false;
static constexpr const char* kDefaultServerPort = "5050";

static void SetPullButtonState(bool isPulling);
static void SetHostSelectionState();
static void SetProgressValue(float progress);

static void EnsurePullProgressFill() {
    if (!g_pullProgressBar || !g_pullProgressBar->slider || g_pullProgressFillRect) {
        return;
    }

    auto* slider = g_pullProgressBar->slider;
    auto sliderTransform = slider->get_transform();
    if (!sliderTransform) {
        logger.warn("Unable to resolve BSML slider transform while creating pull progress fill");
        return;
    }

    auto bgTransform = sliderTransform->Find("BG");
    if (!bgTransform) {
        auto sliderSettingTransform = g_pullProgressBar->get_transform();
        if (sliderSettingTransform) {
            auto bsmlSliderTransform = sliderSettingTransform->Find("BSMLSlider");
            if (bsmlSliderTransform) {
                bgTransform = bsmlSliderTransform->Find("BG");
            }
        }
    }

    if (!bgTransform) {
        logger.warn("Unable to find BSMLSlider/BG while creating pull progress fill");
        return;
    }

    auto clonedBgObject = UnityEngine::Object::Instantiate(
        reinterpret_cast<UnityEngine::Object*>(bgTransform.ptr()),
        bgTransform->get_parent(),
        false
    );

    auto* clonedBgRect = clonedBgObject ? reinterpret_cast<UnityEngine::RectTransform*>(clonedBgObject.ptr()) : nullptr;
    if (!clonedBgRect) {
        logger.warn("Unable to clone BG transform for pull progress fill");
        return;
    }

    clonedBgRect->set_name("BeatSyncProgressFillBG");

    auto* clonedBgImage = clonedBgRect->GetComponent<UnityEngine::UI::Image*>();
    if (clonedBgImage) {
        clonedBgImage->set_color({1.0f, 1.0f, 1.0f, 1.0f});
    }

    const float clonedBgWidth = clonedBgRect->get_rect().get_width();
    if (clonedBgWidth <= 0.0f) {
        logger.warn("Cloned BG width is invalid; progress fill may not render correctly");
    }

    g_pullProgressFillRect = clonedBgRect;
    g_pullProgressFillBaseWidth = clonedBgWidth;
    g_pullProgressFillBaseOffsetMax = clonedBgRect->get_offsetMax();

    if (!g_pullProgressFillRect) {
        logger.warn("Unable to create pull progress fill visual; slider fill will not render");
        return;
    }
}

static std::string ResolveHostnameFromIp(const std::string& ip) {
    if (ip.empty()) {
        return "";
    }

    char hostBuffer[NI_MAXHOST] = {};

    sockaddr_in addr4{};
    addr4.sin_family = AF_INET;
    if (inet_pton(AF_INET, ip.c_str(), &addr4.sin_addr) == 1) {
        const int status = getnameinfo(
            reinterpret_cast<const sockaddr*>(&addr4),
            sizeof(addr4),
            hostBuffer,
            sizeof(hostBuffer),
            nullptr,
            0,
            NI_NAMEREQD
        );

        if (status == 0) {
            return hostBuffer;
        }
    }

    sockaddr_in6 addr6{};
    addr6.sin6_family = AF_INET6;
    if (inet_pton(AF_INET6, ip.c_str(), &addr6.sin6_addr) == 1) {
        const int status = getnameinfo(
            reinterpret_cast<const sockaddr*>(&addr6),
            sizeof(addr6),
            hostBuffer,
            sizeof(hostBuffer),
            nullptr,
            0,
            NI_NAMEREQD
        );

        if (status == 0) {
            return hostBuffer;
        }
    }

    return "";
}

static std::string FormatHostDisplayName(const std::string& host) {
    if (host.empty()) {
        return "Unknown Host";
    }

    const std::string resolvedHostname = ResolveHostnameFromIp(host);
    if (resolvedHostname.empty() || resolvedHostname == host) {
        return host;
    }

    return resolvedHostname + " (" + host + ")";
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

static std::string TrimCopy(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\n\r");
    if (begin == std::string::npos) {
        return "";
    }

    const auto end = value.find_last_not_of(" \t\n\r");
    return value.substr(begin, end - begin + 1);
}

static bool IsValidPortNumber(const std::string& value) {
    if (!IsDigitsOnly(value) || value.size() > 5) {
        return false;
    }

    unsigned int port = 0;
    for (const char c : value) {
        port = (port * 10U) + static_cast<unsigned int>(c - '0');
        if (port > 65535U) {
            return false;
        }
    }

    return port > 0U;
}

static std::pair<std::string, std::string> ExtractHostAndPortFromDisplay(const std::string& displayText) {
    const std::string normalizedDisplay = TrimCopy(displayText);
    if (normalizedDisplay.empty()) {
        return {"", ""};
    }

    std::string endpoint = normalizedDisplay;
    const auto openParen = normalizedDisplay.rfind('(');
    const auto closeParen = normalizedDisplay.rfind(')');
    if (openParen != std::string::npos && closeParen == (normalizedDisplay.size() - 1) && closeParen > openParen) {
        endpoint = TrimCopy(normalizedDisplay.substr(openParen + 1, closeParen - openParen - 1));
    }

    if (endpoint.empty()) {
        return {"", ""};
    }

    if (endpoint.front() == '[') {
        const auto closeBracket = endpoint.find(']');
        if (closeBracket == std::string::npos) {
            return {endpoint, ""};
        }

        const std::string host = endpoint.substr(0, closeBracket + 1);
        if (closeBracket + 1 >= endpoint.size() || endpoint[closeBracket + 1] != ':') {
            return {host, ""};
        }

        const std::string port = TrimCopy(endpoint.substr(closeBracket + 2));
        if (IsValidPortNumber(port)) {
            return {host, port};
        }

        return {host, ""};
    }

    const auto colonCount = static_cast<size_t>(std::count(endpoint.begin(), endpoint.end(), ':'));
    if (colonCount == 1) {
        const auto colon = endpoint.rfind(':');
        const std::string host = TrimCopy(endpoint.substr(0, colon));
        const std::string port = TrimCopy(endpoint.substr(colon + 1));
        if (!host.empty() && IsValidPortNumber(port)) {
            return {host, port};
        }
    }

    return {endpoint, ""};
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

    SetProgressValue(0.0f);

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

        if (g_pullProgressFillRect) {
            const float normalizedProgress = std::clamp(progress / 100.0f, 0.0f, 1.0f);
            auto offsetMax = g_pullProgressFillBaseOffsetMax;
            offsetMax.x = g_pullProgressFillBaseOffsetMax.x - (g_pullProgressFillBaseWidth * (1.0f - normalizedProgress));
            g_pullProgressFillRect->set_offsetMax(offsetMax);
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

    SetProgressValue(0.0f);

    std::thread([]() {
        try {
            const std::string host = getConfig().IPAddress.GetValue();
            const std::string port = getConfig().Port.GetValue();
            auto responseFuture = CreateRequest("/api/beatmaps/json", host, port);
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
                auto bytes = DownloadZipBytes(
                    std::string("/api/beatmaps/download/") + beatmapName,
                    getConfig().IPAddress.GetValue(),
                    getConfig().Port.GetValue()
                );

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
            if (g_isConnectingHost.exchange(true)) {
                return;
            }

            const std::string hostToConnect = g_selectedHost;
            std::string portToConnect = g_selectedPort;
            if (portToConnect.empty()) {
                portToConnect = getConfig().Port.GetValue();
            }
            if (portToConnect.empty()) {
                portToConnect = kDefaultServerPort;
            }

            BSML::Lite::SetButtonText(g_connectHostButton, "Connecting...");
            g_connectHostButton->set_interactable(false);

            std::thread([hostToConnect, portToConnect]() {
                bool isHealthy = false;

                try {
                    isHealthy = CheckServerHealth(hostToConnect, portToConnect);
                } catch (const std::exception& ex) {
                    logger.info("Health check failed for '{}' on port '{}': {}", hostToConnect, portToConnect, ex.what());
                } catch (...) {
                    logger.info("Health check failed for '{}' on port '{}' with unknown exception", hostToConnect, portToConnect);
                }

                BSML::MainThreadScheduler::Schedule([hostToConnect, portToConnect, isHealthy]() {
                    if (!g_connectHostButton) {
                        g_isConnectingHost.store(false);
                        return;
                    }

                    if (!isHealthy) {
                        logger.info("Selected host '{}' did not respond successfully to /health on port '{}'", hostToConnect, portToConnect);
                        BSML::Lite::SetButtonText(g_connectHostButton, "Connect");
                        g_connectHostButton->set_interactable(true);
                        g_isConnectingHost.store(false);
                        return;
                    }

                    getConfig().Port.SetValue(portToConnect);
                    getConfig().IPAddress.SetValue(hostToConnect);

                    BSML::Lite::SetButtonText(g_connectHostButton, "Disconnect");
                    g_connectHostButton->set_interactable(true);
                    g_isConnectingHost.store(false);
                });
            }).detach();
        } else {
            getConfig().IPAddress.SetValue("");
            getConfig().Port.SetValue("");
            BSML::Lite::SetButtonText(g_connectHostButton, "Connect");
            g_connectHostButton->set_interactable(true);
            g_isConnectingHost.store(false);
        }
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

    EnsurePullProgressFill();

    auto progressBarRectTransform = g_pullProgressBar->get_gameObject()->GetComponent<UnityEngine::RectTransform*>();
    if (progressBarRectTransform) {
        auto maxOffset = progressBarRectTransform->get_offsetMax();
        maxOffset.y = 25.0f;
        progressBarRectTransform->set_offsetMax(maxOffset);

        auto handleProgressBar = g_pullProgressBar->slider->get_handleRect();

        auto valueText = g_pullProgressBar->transform->Find("BSMLSlider/SlidingArea/Value");
        
        if (handleProgressBar) {
            handleProgressBar->get_gameObject()->SetActive(false);
        }

        if (valueText) {
            valueText->get_gameObject()->SetActive(false);
        }

    }

    if (g_pullProgressBar) {
        g_pullProgressBar->set_interactable(false);
        SetProgressValue(0.0f);
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

    auto automaticPullToggle = BSML::Lite::CreateToggle(container, "Automatic Pull", getConfig().AutomaticPull.GetValue(), [](bool value) {
        getConfig().AutomaticPull.SetValue(value);
    });
}