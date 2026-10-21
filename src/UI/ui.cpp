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

            buttonGUI->set_text("Connecting...");
            buttonGUI->set_enabled(false);

            IPInput->set_enabled(false);
            PortInput->set_enabled(false);
            UserInput->set_enabled(false);
            PasswordInput->set_enabled(false);

            SMB::ConnectAsync(ip, port, user, pass, [buttonGUI, IPInput, PortInput, UserInput, PasswordInput](bool success) {
                if (success) {
                    buttonGUI->set_text("Disconnect");
                } else {
                    buttonGUI->set_text("Connect");
                    buttonGUI->set_enabled(true);

                    IPInput->set_enabled(true);
                    PortInput->set_enabled(true);
                    UserInput->set_enabled(true);
                    PasswordInput->set_enabled(true);
                }
            });
        } else if (buttonGUI->get_text() == "Disconnect") {
            SMB::Disconnect();
            buttonGUI->set_text("Connect");
            IPInput->set_enabled(true);
            PortInput->set_enabled(true);
            UserInput->set_enabled(true);
            PasswordInput->set_enabled(true);
        }
    });
}