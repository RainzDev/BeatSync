#include "bsml/shared/BSML.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "../config.hpp"
#include "../utils/QuestConnection.hpp"

#include <cstdlib>

namespace {
    UnityEngine::UI::Button* g_connectButton = nullptr;
}

void DidActivate(HMUI::ViewController* self, bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling) {
    // Only create the elements once per menu
    if (!firstActivation)
        return;

    auto container = BSML::Lite::CreateScrollableSettingsContainer(self);

    g_connectButton = BSML::Lite::CreateUIButton(container, "Connect", []() {
        if (g_connectButton == nullptr)
            return;

        auto buttonGUI = g_connectButton->GetComponentInChildren<TMPro::TextMeshProUGUI*>();
        if (buttonGUI == nullptr)
            return;

        if (buttonGUI->get_text() == "Connect") {
            buttonGUI->set_text("Listening...");
            buttonGUI->set_enabled(false);

            QuestConnection::StartQuestHandshakeListener([buttonGUI](bool success) {
                if (success) {
                    buttonGUI->set_text("Disconnect");
                    buttonGUI->set_enabled(true);
                } else {
                    buttonGUI->set_text("Connect");
                    buttonGUI->set_enabled(true);
                }
            });
        } else if (buttonGUI->get_text() == "Disconnect") {
            QuestConnection::Disconnect();
            QuestConnection::StopQuestHandshakeListener();
            buttonGUI->set_text("Connect");
            buttonGUI->set_enabled(true);
        }
    });
}