#pragma once

#include <atomic>
#include <chrono>
#include <expected>
#include <format>
#include <future>
#include <memory>
#include <mutex>
#include <print>
#include <ranges>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

#include "imgui.h"

#include "components/data.h"
#include "console/console.h"
#include "image.h"
#include "network/roblox/auth.h"
#include "network/roblox/common.h"
#include "network/roblox/games.h"
#include "network/roblox/session.h"
#include "network/roblox/social.h"
#include "system/auto_updater.h"
#include "ui/ui.h"
#include "ui/widgets/modal_popup.h"
#include "ui/widgets/notifications.h"
#include "utils/crypto.h"
#include "utils/shutdown_manager.h"
#include "utils/worker_thread.h"

inline constexpr ImWchar ICON_MIN_FA = 0xf000;
inline constexpr ImWchar ICON_MAX_16_FA = 0xf3ff;
inline constexpr float BASE_FONT_SIZE = 16.0f;

inline ImFont *g_rubikFont = nullptr;
inline ImFont *g_iconFont = nullptr;
inline std::atomic<bool> g_running = true;

void LoadImGuiFonts(float scaledFontSize);
void OpenURL(const char* url);

namespace AccountProcessor {

    using AccountSnapshot = AccountData;

    struct ProcessResult {
        int id;
        std::string userId;
        std::string username;
        std::string displayName;
        std::string status;
        std::string lastLocation;
        uint64_t placeId = 0;
        std::string jobId;
        std::string voiceStatus;
        time_t banExpiry = 0;
        time_t voiceBanExpiry = 0;
        bool shouldDeselect = false;
        bool isInvalid = false;
    };

    [[nodiscard]] std::vector<AccountSnapshot> takeAccountSnapshots();
    [[nodiscard]] ProcessResult processAccount(const AccountSnapshot &account);
    void applyResults(const std::vector<ProcessResult> &results);
    void showInvalidCookieModal(std::vector<int> invalidIds, std::string invalidNames);

} // namespace AccountProcessor

void refreshAccounts();
void startAccountRefreshLoop();
void initializeAutoUpdater();

[[nodiscard]] bool initializeApp();
