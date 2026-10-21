#pragma once

#include <string>
#include "config-utils/shared/config-utils.hpp"


DECLARE_CONFIG(Config) {
    CONFIG_VALUE(IPInput, std::string, "IP Address", "");
    CONFIG_VALUE(PortInput, std::string, "Port", "445");
    CONFIG_VALUE(UserInput, std::string, "Username", "");
    CONFIG_VALUE(PasswordInput, std::string, "Password", "");
};