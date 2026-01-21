#include "modal_popup.h"
#include <format>
#include <imgui.h>

namespace ModalPopup {

void AddYesNo(const std::string& msg, std::function<void()> onYes, std::function<void()> onNo) {
    queue.push_back({
        std::format("##ConfirmPopup{}", nextId++),
        msg,
        std::move(onYes),
        std::move(onNo),
        PopupType::YesNo,
        true,
        true,
        true
    });
}

void AddOk(const std::string& msg, std::function<void()> onOk) {
    queue.push_back({
        std::format("##ConfirmPopup{}", nextId++),
        msg,
        std::move(onOk),
        nullptr,
        PopupType::Ok,
        true,
        true,
        true
    });
}

void AddInfo(const std::string& msg) {
    queue.push_back({
        std::format("##ConfirmPopup{}", nextId++),
        msg,
        nullptr,
        nullptr,
        PopupType::Info,
        true,
        true,
        true
    });
}

void Clear() {
    queue.clear();
}

void Render() {
    if (queue.empty()) {
        return;
    }

    Item& cur = queue.front();

    if (cur.shouldOpen) {
        ImGui::OpenPopup(cur.id.c_str());
        cur.shouldOpen = false;
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse;

    if (!cur.closeable) {
        flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar;
    }

    bool* p_open = cur.closeable ? &cur.isOpen : nullptr;

    if (ImGui::BeginPopupModal(cur.id.c_str(), p_open, flags)) {
        if (cur.closeable && !cur.isOpen) {
            ImGui::CloseCurrentPopup();
            queue.pop_front();
            ImGui::EndPopup();
            return;
        }

        ImGui::TextWrapped("%s", cur.message.c_str());
        ImGui::Spacing();

        switch (cur.type) {
            case PopupType::YesNo: {
                if (ImGui::Button("Yes", ImVec2(120, 0))) {
                    if (cur.onYes) {
                        cur.onYes();
                    }
                    ImGui::CloseCurrentPopup();
                    queue.pop_front();
                }
                ImGui::SameLine();
                if (ImGui::Button("No", ImVec2(120, 0))) {
                    if (cur.onNo) {
                        cur.onNo();
                    }
                    ImGui::CloseCurrentPopup();
                    queue.pop_front();
                }
                break;
            }

            case PopupType::Ok: {
                if (ImGui::Button("OK", ImVec2(120, 0))) {
                    if (cur.onYes) {
                        cur.onYes();
                    }
                    ImGui::CloseCurrentPopup();
                    queue.pop_front();
                }
                break;
            }

            case PopupType::Info: {
                if (ImGui::Button("OK", ImVec2(120, 0))) {
                    ImGui::CloseCurrentPopup();
                    queue.pop_front();
                }
                break;
            }
        }

        ImGui::EndPopup();
    }
}

}