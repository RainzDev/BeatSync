#pragma once

#include <string>
#include "config-utils/shared/config-utils.hpp"


DECLARE_CONFIG(Config) {
    CONFIG_VALUE(IPAddress, std::string, "IP Address", "");
    CONFIG_VALUE(Port, std::string, "Port", "5050");
};