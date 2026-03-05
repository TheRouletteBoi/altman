#include "context_menus.h"

#include <cstdio>
#include <format>

#include <imgui.h>

void RenderStandardJoinMenu(const StandardJoinMenuParams &params) {
    if (ImGui::BeginMenu("Copy", params.placeId != 0)) {
        if (ImGui::MenuItem("Game ID")) {
            ImGui::SetClipboardText(std::format("{}", params.placeId).c_str());
        }

        if (params.universeId != 0 && ImGui::MenuItem("Universe ID")) {
            ImGui::SetClipboardText(std::format("{}", params.universeId).c_str());
        }

        if (!params.jobId.empty()) {
            if (ImGui::MenuItem("Instance ID")) {
                ImGui::SetClipboardText(params.jobId.c_str());
            }
        }

        if (params.placeId != 0) {
            ImGui::Separator();
            if (ImGui::MenuItem("Browser Link (Game)##game")) {
                ImGui::SetClipboardText(
                    std::format("https://www.roblox.com/games/start?placeId={}", params.placeId).c_str()
                );
            }
            if (ImGui::MenuItem("Deep Link (Game)##game")) {
                ImGui::SetClipboardText(std::format("roblox://placeId={}", params.placeId).c_str());
            }
            if (ImGui::MenuItem("JavaScript (Game)##game")) {
                ImGui::SetClipboardText(std::format("Roblox.GameLauncher.joinGameInstance({})", params.placeId).c_str());
            }
            if (ImGui::MenuItem("Roblox Luau (Game)##game")) {
                ImGui::SetClipboardText(
                    std::format("game:GetService(\"TeleportService\"):Teleport({})", params.placeId).c_str()
                );
            }

            if (!params.jobId.empty()) {
                ImGui::Separator();

                if (ImGui::MenuItem("Browser Link (Instance)##instance")) {
                    ImGui::SetClipboardText(
                        std::format(
                            "https://www.roblox.com/games/start?placeId={}&gameInstanceId={}",
                            params.placeId,
                            params.jobId
                        )
                            .c_str()
                    );
                }
                if (ImGui::MenuItem("Deep Link (Instance)##instance")) {
                    ImGui::SetClipboardText(
                        std::format("roblox://placeId={}&gameInstanceId={}", params.placeId, params.jobId).c_str()
                    );
                }
                if (ImGui::MenuItem("JavaScript (Instance)##instance")) {
                    ImGui::SetClipboardText(
                        std::format("Roblox.GameLauncher.joinGameInstance({}, \"{}\")", params.placeId, params.jobId).c_str()
                    );
                }
                if (ImGui::MenuItem("Roblox Luau (Instance)##instance")) {
                    ImGui::SetClipboardText(
                        std::format(
                            "game:GetService(\"TeleportService\"):TeleportToPlaceInstance({}, \"{}\")",
                            params.placeId,
                            params.jobId
                        )
                            .c_str()
                    );
                }
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Fill \"Join Options\"", params.placeId != 0)) {
        if (ImGui::MenuItem("Game")) {
            if (params.onFillGame) {
                params.onFillGame();
            }
        }
        if (!params.jobId.empty()) {
            if (ImGui::MenuItem("Game Server")) {
                if (params.onFillInstance) {
                    params.onFillInstance();
                }
            }
        }
        ImGui::EndMenu();
    }

    ImGui::Separator();

    ImGui::TextDisabled("Launch options");

    const std::string gameLbl = (params.launchGameLabel.empty() ? "Launch Game" : params.launchGameLabel) + "##game";
    const std::string instLbl
        = (params.launchInstanceLabel.empty() ? "Launch Game Server" : params.launchInstanceLabel) + "##instance";

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.18f, 0.80f, 0.44f, 1.0f));

    if (ImGui::MenuItem(gameLbl.c_str(), nullptr, false, params.enableLaunchGame && params.placeId != 0)) {
        if (params.onLaunchGame) {
            params.onLaunchGame();
        }
    }

    if (!params.jobId.empty()) {
        if (ImGui::MenuItem(instLbl.c_str(), nullptr, false, params.enableLaunchInstance && params.placeId != 0)) {
            if (params.onLaunchInstance) {
                params.onLaunchInstance();
            }
        }
    }

    ImGui::PopStyleColor();
}

void RenderPrivateServerJoinMenu(const PrivateServerMenuParams& params) {
    if (ImGui::BeginMenu("Copy")) {
        if (ImGui::MenuItem("Share Link")) {
            if (params.onCopyShareLink) {
                params.onCopyShareLink();
            }
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Fill \"Join Options\"")) {
        if (ImGui::MenuItem("Private Server")) {
            if (params.onFillJoinOption) {
                params.onFillJoinOption();
            }

        }
        ImGui::EndMenu();
    }

    ImGui::Separator();

    ImGui::TextDisabled("Server settings");

    if (ImGui::MenuItem("Regenerate Share Link", nullptr, false)) {
        if (params.onRegenerateShareLink) {
            params.onRegenerateShareLink();
        }
    }
}
