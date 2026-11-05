#include "../utils/requests.hpp"

#include "web-utils/shared/WebUtils.hpp"

#include "../config.hpp"

#include <string>
#include <cstdint>
#include <future>
#include <iomanip>
#include <sstream>

std::string UrlEncode(std::string_view value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex << std::uppercase;

    for (unsigned char c : value) {
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << static_cast<int>(c);
        }
    }

    return escaped.str();
}

std::future<WebUtils::JsonResponse> CreateRequest(
    std::string const& endpoint,
    std::string const& hostOverride,
    std::string const& portOverride
) {
    const std::string host = hostOverride.empty() ? getConfig().IPAddress.GetValue() : hostOverride;
    const std::string port = portOverride.empty() ? getConfig().Port.GetValue() : portOverride;
    const std::string URL = "http://" + host + ":" + port + UrlEncode(endpoint);

    WebUtils::URLOptions path{ URL };
    path.noEscape = true;

    return WebUtils::GetAsync<WebUtils::JsonResponse>(path);
}

std::future<WebUtils::StringResponse> CreateStringRequest(
    std::string const& endpoint,
    std::string const& hostOverride,
    std::string const& portOverride
) {
    const std::string host = hostOverride.empty() ? getConfig().IPAddress.GetValue() : hostOverride;
    const std::string port = portOverride.empty() ? getConfig().Port.GetValue() : portOverride;
    const std::string URL = "http://" + host + ":" + port + UrlEncode(endpoint);

    WebUtils::URLOptions path{ URL };
    path.noEscape = true;

    return WebUtils::GetAsync<WebUtils::StringResponse>(path);
}

bool CheckServerHealth(std::string const& hostOverride, std::string const& portOverride) {
    auto response = CreateStringRequest("/health", hostOverride, portOverride).get();
    return response && response.get_HttpCode() == 200;
}

std::vector<uint8_t> DownloadZipBytes(
    std::string const& endpoint,
    std::string const& hostOverride,
    std::string const& portOverride
) {
    const std::string host = hostOverride.empty() ? getConfig().IPAddress.GetValue() : hostOverride;
    const std::string port = portOverride.empty() ? getConfig().Port.GetValue() : portOverride;
    const std::string url = "http://" + host + ":" + port + UrlEncode(endpoint);

    WebUtils::URLOptions path{ url };
    path.noEscape = true;

    auto future = WebUtils::GetAsync<WebUtils::DataResponse>(path);
    auto response = future.get();

    if (!response || !response.DataParsedSuccessful()) {
        return {};
    }

    return response.GetParsedData();
}
