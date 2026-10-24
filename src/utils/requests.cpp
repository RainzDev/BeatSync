#include "../utils/requests.hpp"

#include "web-utils/shared/WebUtils.hpp"

#include "../config.hpp"

#include <string>
#include <cstdint>
#include <future>

std::future<WebUtils::JsonResponse> CreateRequest(std::string const& endpoint) {
    const std::string getIp = getConfig().IPAddress.GetValue();
    const std::string getPort = getConfig().Port.GetValue();
    const std::string URL = "http://" + getIp + ":" + getPort + endpoint;

    WebUtils::URLOptions path{ URL };
    path.noEscape = true;

    return WebUtils::GetAsync<WebUtils::JsonResponse>(path);
}

std::vector<uint8_t> DownloadZipBytes(std::string const& endpoint) {
    const std::string ip = getConfig().IPAddress.GetValue();
    const std::string port = getConfig().Port.GetValue();
    const std::string url = "http://" + ip + ":" + port + endpoint;

    WebUtils::URLOptions path{ url };
    path.noEscape = true;

    return WebUtils::GetAsync<WebUtils::DataResponse>(path).get().GetParsedData();
}
