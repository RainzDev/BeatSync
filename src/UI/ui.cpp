#include "bsml/shared/BSML.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "../src/config.hpp"
#include "../src/utils/smb.hpp"

#include <cstdlib>

namespace {
    UnityEngine::UI::Button* g_connectButton = nullptr;
}

void DidActivate(HMUI::ViewController* self, bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling) {
    // Only create the elements once per menu
    if (!firstActivation)
        return;

    auto container = BSML::Lite::CreateScrollableSettingsContainer(self);

    auto IPInput = AddConfigValueInputString(container, getConfig().IPInput);
    auto PortInput = AddConfigValueInputString(container, getConfig().PortInput);
    auto UserInput = AddConfigValueInputString(container, getConfig().UserInput);
    auto PasswordInput = AddConfigValueInputString(container, getConfig().PasswordInput);

    g_connectButton = BSML::Lite::CreateUIButton(container, "Connect", [IPInput, PortInput, UserInput, PasswordInput]() {
        if (g_connectButton == nullptr)
            return;

        auto buttonGUI = g_connectButton->GetComponentInChildren<TMPro::TextMeshProUGUI*>();
        if (buttonGUI == nullptr)
            return;

        if (buttonGUI->get_text() == "Connect") {
            const std::string ip = getConfig().IPInput.GetValue();
            const int port = std::atoi(std::string(getConfig().PortInput.GetValue()).c_str());
            const std::string user = getConfig().UserInput.GetValue();
            const std::string pass = getConfig().PasswordInput.GetValue();

            if (SMB::Connect(ip, port, user, pass)) {
                buttonGUI->set_text("Disconnect");
                IPInput->enabled = false;
                PortInput->enabled = false;
                UserInput->enabled = false;
                PasswordInput->enabled = false;
            }
        } else if (buttonGUI->get_text() == "Disconnect") {
            SMB::Disconnect();
            buttonGUI->set_text("Connect");
            IPInput->enabled = true;
            PortInput->enabled = true;
            UserInput->enabled = true;
            PasswordInput->enabled = true;
        }
    });
}