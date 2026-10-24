#pragma once

#include "web-utils/shared/WebUtils.hpp"

#include <functional>
#include <future>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

std::future<WebUtils::JsonResponse> CreateRequest(std::string const& endpoint);
std::vector<uint8_t> DownloadZipBytes(std::string const& endpoint);