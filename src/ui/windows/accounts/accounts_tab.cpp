#include <algorithm>
#include <ctime>
#include <format>
#include <random>
#include <ranges>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <imgui.h>
#include "imgui_internal.h"

#include "accounts.h"
#include "accounts_context_menu.h"
#include "accounts_join_ui.h"
#include "components/data.h"
#include "console/console.h"
#include "network/roblox/auth.h"
#include "network/roblox/common.h"
#include "network/roblox/games.h"
#include "network/roblox/session.h"
#include "network/roblox/social.h"
#include "ui/ui.h"
#include "ui/webview/webview.h"
#include "ui/widgets/bottom_right_status.h"
#include "ui/widgets/modal_popup.h"
#include "ui/windows/components.h"
#include "utils/worker_thread.h"
#include "utils/time_utils.h"

namespace {
    constexpr int COLUMN_COUNT = 6;
    constexpr float HOLD_THRESHOLD_SECONDS = 0.65f;
    constexpr float DEFAULT_ROW_HEIGHT = 19.0f;
    constexpr float MIN_INPUT_WIDTH = 100.0f;

    constexpr float COL_DISPLAY_NAME_WEIGHT = 1.0000f;
    constexpr float COL_USERNAME_WEIGHT = 1.0000f;
    constexpr float COL_USERID_WEIGHT = 0.7000f;
    constexpr float COL_STATUS_WEIGHT = 0.5000f;
    constexpr float COL_VOICE_WEIGHT = 0.4500f;
    constexpr float COL_NOTE_WEIGHT = 2.0000f;

    constexpr ImVec4 COLOR_VOICE_ENABLED {0.7f, 1.0f, 0.7f, 1.0f};
    constexpr ImVec4 COLOR_VOICE_DISABLED {1.0f, 1.0f, 0.7f, 1.0f};
    constexpr ImVec4 COLOR_VOICE_BANNED {1.0f, 0.7f, 0.7f, 1.0f};
    constexpr ImVec4 COLOR_VOICE_NA {0.7f, 0.7f, 0.7f, 1.0f};

    constexpr const char *PAYLOAD_ROW_REORDER = "ACCOUNT_ROW_REORDER";

    struct DragDropState {
            int draggedIndex = -1;
            int draggedAccountId = -1;
            bool isDragging = false;
            ImVec4 dragIndicatorColor = ImVec4(0.4f, 0.6f, 1.0f, 0.8f);
    };

    DragDropState g_dragDropState;

    struct UrlPopupState {
            bool open {false};
            int accountId {-1};
            char buffer[256] {};
    };

    struct GroupPopupState {
            bool openCreate = false;
            bool openRename = false;
            int editingGroupId = -1;
            char nameBuffer[128] {};
    };

    UrlPopupState g_urlPopup;
    GroupPopupState g_groupPopup;
    std::unordered_set<int> g_voiceUpdateInProgress;
    std::unordered_map<int, double> g_holdStartTimes;

    ImVec4 getVoiceStatusColor(std::string_view status) noexcept {
        if (status == "Enabled") {
            return COLOR_VOICE_ENABLED;
        }
        if (status == "Disabled") {
            return COLOR_VOICE_DISABLED;
        }
        if (status == "Banned") {
            return COLOR_VOICE_BANNED;
        }
        if (status == "N/A") {
            return COLOR_VOICE_NA;
        }
        return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    void handleAccountSelection(int accountId, bool isCurrentlySelected) {
        if (ImGui::GetIO().KeyCtrl) {
            if (isCurrentlySelected) {
                g_selectedAccountIds.erase(accountId);
            } else {
                g_selectedAccountIds.insert(accountId);
            }
        } else {
            const bool wasSolelySelected = isCurrentlySelected && g_selectedAccountIds.size() == 1;
            g_selectedAccountIds.clear();
            if (!wasSolelySelected) {
                g_selectedAccountIds.insert(accountId);
            }
        }
    }

    void handleDoubleClick(const AccountData &account) {
        if (account.cookie.empty()) {
            LOG_WARN("Cannot open browser - cookie is empty for account: {}", account.displayName);
            return;
        }

        LOG_INFO("Opening browser for account: {} (ID: {})", account.displayName, account.id);
        WorkerThreads::runBackground([account]() {
            LaunchBrowserWithCookie(account);
        });
    }

    void handleHoldAction(const AccountData &account) {
        if (account.cookie.empty()) {
            LOG_WARN("Cannot open browser - cookie is empty for account: {}", account.displayName);
            return;
        }

        g_urlPopup.open = true;
        g_urlPopup.accountId = account.id;
        g_urlPopup.buffer[0] = '\0';
    }

    void checkVoiceBanExpiry(AccountData &account) {
        if (account.voiceStatus != "Banned" || account.voiceBanExpiry <= 0) {
            return;
        }
        if (account.cookie.empty()) {
            return;
        }
        if (g_voiceUpdateInProgress.contains(account.id)) {
            return;
        }

        const auto now = std::time(nullptr);
        if (now < account.voiceBanExpiry) {
            return;
        }

        g_voiceUpdateInProgress.insert(account.id);
        const int accountId = account.id;
        const std::string cookie = account.cookie;

        WorkerThreads::runBackground([accountId, cookie]() {
            const auto voiceStatus = Roblox::getVoiceChatStatus(cookie);
            WorkerThreads::RunOnMain([accountId, voiceStatus]() {
                if (AccountData *acc = getAccountById(accountId)) {
                    acc->voiceStatus = voiceStatus.status;
                    acc->voiceBanExpiry = voiceStatus.bannedUntil;
                }
                g_voiceUpdateInProgress.erase(accountId);
                Data::SaveAccounts();
            });
        });
    }

    struct RowMetrics {
            float height;
            float verticalPadding;
    };

    RowMetrics calculateRowMetrics() {
        float height = ImGui::GetFrameHeight();
        if (height <= 0.0f) {
            height = ImGui::GetTextLineHeightWithSpacing();
        }
        if (height <= 0.0f) {
            height = DEFAULT_ROW_HEIGHT;
        }

        const float textHeight = ImGui::GetTextLineHeight();
        const float padding = std::max(0.0f, (height - textHeight) * 0.5f);

        return {height, padding};
    }

    void renderCenteredTextCell(
        std::string_view text,
        float cellStartY,
        float rowHeight,
        float verticalPadding,
        const ImVec4 *color = nullptr
    ) {
        ImGui::TableNextColumn();
        const float currentY = ImGui::GetCursorPosY();

        ImGui::SetCursorPosY(currentY + verticalPadding);
        if (color) {
            ImGui::TextColored(*color, "%s", text.data());
        } else {
            ImGui::TextUnformatted(text.data());
        }

        ImGui::SetCursorPosY(currentY + rowHeight);
    }

    void renderRedactedCenteredTextCell(
        std::string_view text,
        float cellStartY,
        float rowHeight,
        float verticalPadding
    ) {
        ImGui::TableNextColumn();
        const float currentY = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(currentY + verticalPadding);

        if (g_privacyModeEnabled) {
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float textWidth = ImGui::CalcTextSize(text.data()).x;
            const float barHeight = ImGui::GetTextLineHeight();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
            ImGui::TextUnformatted(text.data());
            ImGui::PopStyleColor();

            ImDrawList *drawList = ImGui::GetWindowDrawList();
            const ImVec2 barMin(pos.x - 2.0f, pos.y - 1.0f);
            const ImVec2 barMax(pos.x + textWidth + 2.0f, pos.y + barHeight + 1.0f);
            const ImVec2 clipMin = ImGui::GetWindowPos();
            const ImVec2 clipMax = ImVec2(clipMin.x + ImGui::GetWindowSize().x, clipMin.y + ImGui::GetWindowSize().y);

            drawList->PushClipRect(clipMin, clipMax, true);
            const ImVec4 col = ImGui::GetStyleColorVec4(ImGuiCol_TableHeaderBg);
            drawList->AddRectFilled(barMin, barMax, IM_COL32(col.x * 255, col.y * 255, col.z * 255, 220), 3.0f);
            drawList->PopClipRect();
        } else {
            ImGui::TextUnformatted(text.data());
        }

        ImGui::SetCursorPosY(currentY + rowHeight);
    }

    void renderStatusCell(const AccountData &account, float startY, float rowHeight, float verticalPadding) {
        ImGui::TableNextColumn();
        const float currentY = ImGui::GetCursorPosY();

        ImGui::SetCursorPosY(currentY + verticalPadding);
        const ImVec4 statusColor = getStatusColor(account.status);
        ImGui::TextColored(statusColor, "%s", account.status.c_str());

        if (ImGui::IsItemHovered()) {
            if (account.status == "Banned" && account.banExpiry > 0) {
                ImGui::BeginTooltip();
                const auto timeStr = formatCountdown(account.banExpiry);
                ImGui::TextUnformatted(timeStr.c_str());
                ImGui::EndTooltip();
            } else if (account.status == "InGame" && !account.lastLocation.empty()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(account.lastLocation.c_str());
                ImGui::EndTooltip();
            } else if (account.status == "Locked") {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted("Account locked: suspicious activity detected,");
                ImGui::TextUnformatted("Human verification required to unlock.");
                ImGui::EndTooltip();
            } else if (account.status == "Screen Time Limit") {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted("Account has an active parental screen time restriction.");
                ImGui::EndTooltip();
            }
        }

        ImGui::SetCursorPosY(currentY + rowHeight);
    }

    void renderVoiceCell(AccountData &account, float startY, float rowHeight, float verticalPadding) {
        ImGui::TableNextColumn();
        const float currentY = ImGui::GetCursorPosY();

        checkVoiceBanExpiry(account);

        ImGui::SetCursorPosY(currentY + verticalPadding);
        const ImVec4 voiceColor = getVoiceStatusColor(account.voiceStatus);
        ImGui::TextColored(voiceColor, "%s", account.voiceStatus.c_str());

        if (ImGui::IsItemHovered()) {
            if (account.voiceStatus == "Banned" && account.voiceBanExpiry > 0) {
                ImGui::BeginTooltip();
                const auto timeStr = formatCountdown(account.voiceBanExpiry);
                ImGui::TextUnformatted(timeStr.c_str());
                ImGui::EndTooltip();
            } else if (account.voiceStatus == "Unknown") {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted("HTTP request returned an error");
                ImGui::EndTooltip();
            } else if (account.voiceStatus == "N/A") {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted("HTTP request unavailable");
                ImGui::EndTooltip();
            }
        }

        ImGui::SetCursorPosY(currentY + rowHeight);
    }

    void renderDragDropTarget(int targetIndex) {
        if (!ImGui::BeginDragDropTarget()) {
            return;
        }

        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(PAYLOAD_ROW_REORDER)) {
            const int sourceIndex = *(const int *) payload->Data;

            if (sourceIndex == targetIndex) {
                ImGui::EndDragDropTarget();
                return;
            }

            if (g_activeGroupTab == -1) {
                if (sourceIndex >= 0 && sourceIndex < static_cast<int>(g_accounts.size()) && targetIndex >= 0
                    && targetIndex < static_cast<int>(g_accounts.size())) {

                    auto accountCopy = g_accounts[sourceIndex];
                    g_accounts.erase(g_accounts.begin() + sourceIndex);

                    int insertIndex = targetIndex;
                    if (sourceIndex < targetIndex) {
                        insertIndex--;
                    }

                    g_accounts.insert(g_accounts.begin() + insertIndex, accountCopy);
                    invalidateAccountIndex();
                    Data::SaveAccounts();
                    LOG_INFO("Reordered account from index {} to {}", sourceIndex, insertIndex);
                }
            } else {
                if (AccountGroup *group = getGroupById(g_activeGroupTab)) {
                    if (sourceIndex >= 0 && sourceIndex < static_cast<int>(group->accountIds.size()) && targetIndex >= 0
                        && targetIndex < static_cast<int>(group->accountIds.size())) {

                        const int movedId = group->accountIds[sourceIndex];
                        group->accountIds.erase(group->accountIds.begin() + sourceIndex);

                        int insertIndex = targetIndex;
                        if (sourceIndex < targetIndex) {
                            insertIndex--;
                        }
                        group->accountIds.insert(group->accountIds.begin() + insertIndex, movedId);
                        Data::SaveAccountGroups();
                        LOG_INFO("Reordered account in group from index {} to {}", sourceIndex, insertIndex);
                    }
                }
            }
        }

        ImGui::EndDragDropTarget();
    }

    void renderDragDropIndicator(int currentIndex, int draggedIndex) {
        if (!g_dragDropState.isDragging || draggedIndex < 0) {
            return;
        }

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();

            ImDrawList *drawList = ImGui::GetWindowDrawList();
            const ImU32 color = ImGui::GetColorU32(g_dragDropState.dragIndicatorColor);

            const float lineThickness = 3.0f;
            const float y = (currentIndex < draggedIndex) ? min.y : max.y;

            drawList->AddLine(ImVec2(min.x, y), ImVec2(max.x, y), color, lineThickness);
        }
    }

    int getDisplayIndex(int accountId) {
        if (g_activeGroupTab == -1) {
            return getAccountIndexById(accountId);
        }

        if (const AccountGroup *group = getGroupById(g_activeGroupTab)) {
            for (int i = 0; i < static_cast<int>(group->accountIds.size()); ++i) {
                if (group->accountIds[i] == accountId) {
                    return i;
                }
            }
        }
        return -1;
    }

    void renderAccountRow(AccountData &account, const RowMetrics &metrics) {
        ImGui::TableNextRow();
        ImGui::PushID(account.id);

        const int currentIndex = getDisplayIndex(account.id);

        const bool isSelected = g_selectedAccountIds.contains(account.id);
        if (isSelected) {
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImGuiCol_Header));
        }

        if (g_dragDropState.isDragging && currentIndex == g_dragDropState.draggedIndex) {
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImVec4(0.3f, 0.5f, 0.8f, 0.3f)));
        }

        ImGui::TableNextColumn();
        const float cellStartY = ImGui::GetCursorPosY();

        const auto selectableLabel = std::format("##row_selectable_{}", account.id);
        if (ImGui::Selectable(
                selectableLabel.c_str(),
                isSelected,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                ImVec2(0, metrics.height)
            )) {
            handleAccountSelection(account.id, isSelected);
        }

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover)) {
            ImGui::SetDragDropPayload(PAYLOAD_ROW_REORDER, &currentIndex, sizeof(int));
            g_dragDropState.isDragging = true;
            g_dragDropState.draggedIndex = currentIndex;
            g_dragDropState.draggedAccountId = account.id;

            ImGui::Text("Moving: %s", account.displayName.c_str());
            ImGui::EndDragDropSource();
        }

        if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            g_dragDropState.isDragging = false;
            g_dragDropState.draggedIndex = -1;
            g_dragDropState.draggedAccountId = -1;
        }

        if (currentIndex >= 0) {
            renderDragDropTarget(currentIndex);
            renderDragDropIndicator(currentIndex, g_dragDropState.draggedIndex);
        }

        if (ImGui::IsItemActivated() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            g_holdStartTimes[account.id] = ImGui::GetTime();
        }

        bool holdTriggered = false;
        if (ImGui::IsItemActive()) {
            if (const auto holdIt = g_holdStartTimes.find(account.id);
                holdIt != g_holdStartTimes.end() && (ImGui::GetTime() - holdIt->second) >= HOLD_THRESHOLD_SECONDS) {
                g_holdStartTimes.erase(holdIt);
                holdTriggered = true;
            }
        } else {
            g_holdStartTimes.erase(account.id);
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            handleDoubleClick(account);
        }

        if (holdTriggered) {
            handleHoldAction(account);
        }

        const auto contextMenuId = std::format("AccountsTable_ContextMenu_{}", account.id);
        RenderAccountContextMenu(account, contextMenuId);

        ImGui::SetNextItemAllowOverlap();
        ImGui::SetCursorPosY(cellStartY + metrics.verticalPadding);
        ImGui::TextUnformatted(account.displayName.c_str());
        ImGui::SetCursorPosY(cellStartY + metrics.height);

        renderRedactedCenteredTextCell(account.username, cellStartY, metrics.height, metrics.verticalPadding);
        renderRedactedCenteredTextCell(account.userId, cellStartY, metrics.height, metrics.verticalPadding);
        renderStatusCell(account, cellStartY, metrics.height, metrics.verticalPadding);
        renderVoiceCell(account, cellStartY, metrics.height, metrics.verticalPadding);
        renderCenteredTextCell(account.note, cellStartY, metrics.height, metrics.verticalPadding);

        ImGui::PopID();
    }

    void renderUrlPopup() {
        if (g_urlPopup.open) {
            ImGui::OpenPopup("Open URL");
            g_urlPopup.open = false;
        }

        if (!ImGui::BeginPopupModal("Open URL", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        const auto &style = ImGui::GetStyle();
        const float openWidth = ImGui::CalcTextSize("Open").x + style.FramePadding.x * 2.0f;
        const float cancelWidth = ImGui::CalcTextSize("Cancel").x + style.FramePadding.x * 2.0f;
        const float inputWidth = std::max(
            MIN_INPUT_WIDTH,
            ImGui::GetContentRegionAvail().x - openWidth - cancelWidth - style.ItemSpacing.x
        );

        ImGui::PushItemWidth(inputWidth);
        ImGui::InputTextWithHint("##WebviewUrl", "Enter URL", g_urlPopup.buffer, sizeof(g_urlPopup.buffer));
        ImGui::PopItemWidth();
        ImGui::Spacing();

        if (ImGui::Button("Open", ImVec2(openWidth, 0)) && g_urlPopup.buffer[0] != '\0') {
            if (const AccountData *acc = getAccountById(g_urlPopup.accountId)) {
                WorkerThreads::runBackground([account = *acc, url = std::string(g_urlPopup.buffer)]() {
                    LaunchWebview(url, account);
                });
            }
            g_urlPopup.buffer[0] = '\0';
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine(0, style.ItemSpacing.x);
        if (ImGui::Button("Cancel", ImVec2(cancelWidth, 0))) {
            g_urlPopup.buffer[0] = '\0';
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void renderCreateGroupPopup() {
        if (g_groupPopup.openCreate) {
            ImGui::OpenPopup("Create Group");
            g_groupPopup.openCreate = false;
        }

        if (!ImGui::BeginPopupModal("Create Group", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        ImGui::Text("Group Name:");
        ImGui::PushItemWidth(250.0f);
        ImGui::InputText("##GroupName", g_groupPopup.nameBuffer, sizeof(g_groupPopup.nameBuffer));
        ImGui::PopItemWidth();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Create") && g_groupPopup.nameBuffer[0] != '\0') {
            AccountGroup newGroup {};
            newGroup.id = generateGroupId();
            newGroup.name = g_groupPopup.nameBuffer;

            g_accountGroups.push_back(std::move(newGroup));
            Data::SaveAccountGroups();
            LOG_INFO("Created group '{}'", g_groupPopup.nameBuffer);

            g_groupPopup.nameBuffer[0] = '\0';
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            g_groupPopup.nameBuffer[0] = '\0';
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void renderRenameGroupPopup() {
        if (g_groupPopup.openRename) {
            ImGui::OpenPopup("Rename Group");
            g_groupPopup.openRename = false;
        }

        if (!ImGui::BeginPopupModal("Rename Group", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        ImGui::Text("New Name:");
        ImGui::PushItemWidth(250.0f);
        ImGui::InputText("##RenameGroupName", g_groupPopup.nameBuffer, sizeof(g_groupPopup.nameBuffer));
        ImGui::PopItemWidth();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Save") && g_groupPopup.nameBuffer[0] != '\0') {
            if (AccountGroup *group = getGroupById(g_groupPopup.editingGroupId)) {
                group->name = g_groupPopup.nameBuffer;
                Data::SaveAccountGroups();
                LOG_INFO("Renamed group {} to '{}'", group->id, group->name);
            }
            g_groupPopup.nameBuffer[0] = '\0';
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            g_groupPopup.nameBuffer[0] = '\0';
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void renderGroupTabBar() {
        constexpr ImGuiTabBarFlags tabBarFlags = ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll;

        if (!ImGui::BeginTabBar("AccountGroupTabs", tabBarFlags)) {
            return;
        }

        if (ImGui::BeginTabItem("All")) {
            g_activeGroupTab = -1;
            ImGui::EndTabItem();
        }

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(PAYLOAD_ROW_REORDER)) {
                const int accountId = g_dragDropState.draggedAccountId;
                if (accountId >= 0) {
                    if (g_selectedAccountIds.contains(accountId) && g_selectedAccountIds.size() > 1) {
                        for (int id: g_selectedAccountIds) {
                            removeAccountFromAllGroups(id);
                        }
                        Data::SaveAccountGroups();
                        LOG_INFO("Removed {} selected accounts from all groups", g_selectedAccountIds.size());
                    } else {
                        removeAccountFromAllGroups(accountId);
                        Data::SaveAccountGroups();
                        LOG_INFO("Removed account {} from all groups", accountId);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        for (int i = 0; i < static_cast<int>(g_accountGroups.size()); ++i) {
            auto &group = g_accountGroups[i];
            ImGui::PushID(group.id);

            bool tabOpen = true;
            if (ImGui::BeginTabItem(group.name.c_str(), &tabOpen)) {
                g_activeGroupTab = group.id;
                ImGui::EndTabItem();
            }

            if (!tabOpen) {
                tabOpen = true;

                const int groupId = group.id;
                const std::string groupName = group.name;
                ModalPopup::AddYesNo(
                    std::format("Are you sure you want to delete the group \"{}\"?", groupName),
                    [groupId]() {
                        for (int i = 0; i < static_cast<int>(g_accountGroups.size()); ++i) {
                            if (g_accountGroups[i].id == groupId) {
                                LOG_INFO("Deleted group '{}' (id={})", g_accountGroups[i].name, groupId);
                                if (g_activeGroupTab == groupId) {
                                    g_activeGroupTab = -1;
                                }
                                g_accountGroups.erase(g_accountGroups.begin() + i);
                                Data::SaveAccountGroups();
                                break;
                            }
                        }
                    }
                );
            }

            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                ImGui::OpenPopup("GroupTabContextMenu");
            }

            if (ImGui::BeginPopup("GroupTabContextMenu")) {
                if (ImGui::MenuItem("Rename")) {
                    g_groupPopup.openRename = true;
                    g_groupPopup.editingGroupId = group.id;
                    std::strncpy(g_groupPopup.nameBuffer, group.name.c_str(), sizeof(g_groupPopup.nameBuffer) - 1);
                    g_groupPopup.nameBuffer[sizeof(g_groupPopup.nameBuffer) - 1] = '\0';
                }

                if (ImGui::MenuItem("Delete")) {
                    const int groupId = group.id;
                    const std::string groupName = group.name;
                    ModalPopup::AddYesNo(
                        std::format("Are you sure you want to delete the group \"{}\"?", groupName),
                        [groupId]() {
                            for (int i = 0; i < static_cast<int>(g_accountGroups.size()); ++i) {
                                if (g_accountGroups[i].id == groupId) {
                                    LOG_INFO("Deleted group '{}' (id={})", g_accountGroups[i].name, groupId);
                                    if (g_activeGroupTab == groupId) {
                                        g_activeGroupTab = -1;
                                    }
                                    g_accountGroups.erase(g_accountGroups.begin() + i);
                                    Data::SaveAccountGroups();
                                    break;
                                }
                            }
                        }
                    );
                }

                ImGui::EndPopup();
            }

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(PAYLOAD_ROW_REORDER)) {
                    const int accountId = g_dragDropState.draggedAccountId;
                    if (accountId >= 0) {
                        std::vector<int> accountsToAdd;
                        if (g_selectedAccountIds.contains(accountId) && g_selectedAccountIds.size() > 1) {
                            accountsToAdd.assign(g_selectedAccountIds.begin(), g_selectedAccountIds.end());
                        } else {
                            accountsToAdd.push_back(accountId);
                        }

                        int addedCount = 0;
                        for (int id: accountsToAdd) {
                            const bool alreadyInGroup
                                = std::ranges::find(group.accountIds, id) != group.accountIds.end();
                            if (!alreadyInGroup) {
                                group.accountIds.push_back(id);
                                ++addedCount;
                            }
                        }

                        if (addedCount > 0) {
                            Data::SaveAccountGroups();
                            LOG_INFO("Added {} account(s) to group '{}'", addedCount, group.name);
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::PopID();
        }

        if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip)) {
            g_groupPopup.openCreate = true;
            g_groupPopup.nameBuffer[0] = '\0';
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Create new group");
        }

        ImGui::EndTabBar();
    }

    float calculateJoinOptionsHeight(JoinType joinType) {
        const auto &style = ImGui::GetStyle();
        float height = 0.0f;

        height += ImGui::GetTextLineHeight() + style.ItemSpacing.y;
        height += ImGui::GetFrameHeight() + style.ItemSpacing.y;

        if (joinType == JoinType::GameServer) {
            height += ImGui::GetFrameHeight() + style.ItemSpacing.y;
            height += ImGui::GetFrameHeight() + style.ItemSpacing.y;
        } else {
            height += ImGui::GetFrameHeight() + style.ItemSpacing.y;
        }

        height += 1.0f + style.ItemSpacing.y;
        height += ImGui::GetFrameHeight() + style.ItemSpacing.y;
        height += style.ItemSpacing.y;

        return height;
    }

} // namespace

void RenderAccountsTable(std::vector<AccountData *> &accountsToDisplay, const char *tableId, float tableHeight) {
    // Auto-select default account if nothing selected
    if (g_selectedAccountIds.empty() && g_defaultAccountId != -1) {
        g_selectedAccountIds.insert(g_defaultAccountId);
    }

    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable
                                           | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Hideable
                                           | ImGuiTableFlags_Reorderable | ImGuiTableFlags_ContextMenuInBody;

    const ImVec2 tableSize(0.0f, tableHeight > 0.0f ? tableHeight - 2.0f : 0.0f);
    if (!ImGui::BeginTable(tableId, COLUMN_COUNT, tableFlags, tableSize)) {
        return;
    }

    ImGui::TableSetupColumn("Display Name", ImGuiTableColumnFlags_WidthStretch, COL_DISPLAY_NAME_WEIGHT);
    ImGui::TableSetupColumn("Username", ImGuiTableColumnFlags_WidthStretch, COL_USERNAME_WEIGHT);
    ImGui::TableSetupColumn("UserID", ImGuiTableColumnFlags_WidthStretch, COL_USERID_WEIGHT);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, COL_STATUS_WEIGHT);
    ImGui::TableSetupColumn("Voice", ImGuiTableColumnFlags_WidthStretch, COL_VOICE_WEIGHT);
    ImGui::TableSetupColumn("Note", ImGuiTableColumnFlags_WidthStretch, COL_NOTE_WEIGHT);
    ImGui::TableSetupScrollFreeze(0, 1);

    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Display Name");
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Username");
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("UserID");
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Status");
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Voice");
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Note");

    const auto metrics = calculateRowMetrics();
    for (auto *account: accountsToDisplay) {
        renderAccountRow(*account, metrics);
    }

    ImGui::EndTable();
}

void RenderFullAccountsTabContent() {
    const float availHeight = ImGui::GetContentRegionAvail().y;
    const auto &style = ImGui::GetStyle();

    const float tabBarHeight = ImGui::GetFrameHeight() + style.ItemSpacing.y;
    const float joinOptionsHeight = calculateJoinOptionsHeight(static_cast<JoinType>(join_type_combo_index));
    const float separatorHeight = 1.0f + style.ItemSpacing.y;
    const float totalReserved = tabBarHeight + separatorHeight + joinOptionsHeight;

    constexpr float MIN_TABLE_HEIGHT_MULTIPLIER = 3.0f;
    const float minTableHeight = ImGui::GetFrameHeight() * MIN_TABLE_HEIGHT_MULTIPLIER;

    float tableHeight = std::max(minTableHeight, availHeight - totalReserved);
    if (availHeight <= totalReserved) {
        tableHeight = minTableHeight;
    }

    renderGroupTabBar();

    if (g_activeGroupTab == -1) {
        std::vector<AccountData *> ptrs;
        ptrs.reserve(g_accounts.size());
        for (auto &acc: g_accounts) {
            ptrs.push_back(&acc);
        }
        RenderAccountsTable(ptrs, "AccountsTable", tableHeight);
    } else {
        auto ptrs = getAccountsForGroupMutable(g_activeGroupTab);
        const auto tableId = std::format("GroupTable_{}", g_activeGroupTab);
        RenderAccountsTable(ptrs, tableId.c_str(), tableHeight);
    }

    ImGui::Separator();
    RenderJoinOptions();

    //renderUrlPopup();
    renderCreateGroupPopup();
    renderRenameGroupPopup();
}
