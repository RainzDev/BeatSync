#pragma once

#include "web-utils/shared/WebUtils.hpp"

#include <functional>
#include <future>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

std::string UrlEncode(std::string_view value);
std::future<WebUtils::JsonResponse> CreateRequest(
    std::string const& endpoint,
    std::string const& hostOverride = "",
    std::string const& portOverride = ""
);
std::future<WebUtils::StringResponse> CreateStringRequest(
    std::string const& endpoint,
    std::string const& hostOverride = "",
    std::string const& portOverride = ""
);
bool CheckServerHealth(std::string const& hostOverride = "", std::string const& portOverride = "");
std::vector<uint8_t> DownloadZipBytes(
    std::string const& endpoint,
    std::string const& hostOverride = "",
    std::string const& portOverride = ""
);