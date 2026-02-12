#include "context_menus.h"

#include <cstdio>
#include <format>

#include <imgui.h>

void RenderStandardJoinMenu(const StandardJoinMenuParams &p) {
    if (ImGui::BeginMenu("Copy", p.placeId != 0)) {
        if (ImGui::MenuItem("Game ID")) {
            ImGui::SetClipboardText(std::format("{}", p.placeId).c_str());
        }

        if (p.universeId != 0 && ImGui::MenuItem("Universe ID")) {
            ImGui::SetClipboardText(std::format("{}", p.universeId).c_str());
        }

        if (!p.jobId.empty()) {
            if (ImGui::MenuItem("Instance ID")) {
                ImGui::SetClipboardText(p.jobId.c_str());
            }
        }

        if (p.placeId != 0) {
            ImGui::Separator();
            if (ImGui::MenuItem("Browser Link (Game)##game")) {
                ImGui::SetClipboardText(
                    std::format("https://www.roblox.com/games/start?placeId={}", p.placeId).c_str()
                );
            }
            if (ImGui::MenuItem("Deep Link (Game)##game")) {
                ImGui::SetClipboardText(std::format("roblox://placeId={}", p.placeId).c_str());
            }
            if (ImGui::MenuItem("JavaScript (Game)##game")) {
                ImGui::SetClipboardText(std::format("Roblox.GameLauncher.joinGameInstance({})", p.placeId).c_str());
            }
            if (ImGui::MenuItem("Roblox Luau (Game)##game")) {
                ImGui::SetClipboardText(
                    std::format("game:GetService(\"TeleportService\"):Teleport({})", p.placeId).c_str()
                );
            }

            if (!p.jobId.empty()) {
                ImGui::Separator();

                if (ImGui::MenuItem("Browser Link (Instance)##instance")) {
                    ImGui::SetClipboardText(
                        std::format(
                            "https://www.roblox.com/games/start?placeId={}&gameInstanceId={}",
                            p.placeId,
                            p.jobId
                        )
                            .c_str()
                    );
                }
                if (ImGui::MenuItem("Deep Link (Instance)##instance")) {
                    ImGui::SetClipboardText(
                        std::format("roblox://placeId={}&gameInstanceId={}", p.placeId, p.jobId).c_str()
                    );
                }
                if (ImGui::MenuItem("JavaScript (Instance)##instance")) {
                    ImGui::SetClipboardText(
                        std::format("Roblox.GameLauncher.joinGameInstance({}, \"{}\")", p.placeId, p.jobId).c_str()
                    );
                }
                if (ImGui::MenuItem("Roblox Luau (Instance)##instance")) {
                    ImGui::SetClipboardText(
                        std::format(
                            "game:GetService(\"TeleportService\"):TeleportToPlaceInstance({}, \"{}\")",
                            p.placeId,
                            p.jobId
                        )
                            .c_str()
                    );
                }
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Fill \"Join Options\"", p.placeId != 0)) {
        if (ImGui::MenuItem("Game")) {
            if (p.onFillGame) {
                p.onFillGame();
            }
        }
        if (!p.jobId.empty()) {
            if (ImGui::MenuItem("Game Server")) {
                if (p.onFillInstance) {
                    p.onFillInstance();
                }
            }
        }
        ImGui::EndMenu();
    }

    ImGui::Separator();

    ImGui::TextDisabled("Launch options");

    const std::string gameLbl = (p.launchGameLabel.empty() ? "Launch Game" : p.launchGameLabel) + "##game";
    const std::string instLbl
        = (p.launchInstanceLabel.empty() ? "Launch Game Server" : p.launchInstanceLabel) + "##instance";

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.18f, 0.80f, 0.44f, 1.0f));

    if (ImGui::MenuItem(gameLbl.c_str(), nullptr, false, p.enableLaunchGame && p.placeId != 0)) {
        if (p.onLaunchGame) {
            p.onLaunchGame();
        }
    }

    if (!p.jobId.empty()) {
        if (ImGui::MenuItem(instLbl.c_str(), nullptr, false, p.enableLaunchInstance && p.placeId != 0)) {
            if (p.onLaunchInstance) {
                p.onLaunchInstance();
            }
        }
    }

    ImGui::PopStyleColor();
}
