#include "main_common.h"
#include "assets/fonts/embedded_fa_solid.h"
#include "assets/fonts/embedded_rubik.h"

#include <algorithm>
#include <fstream>

void LoadImGuiFonts(float scaledFontSize) {
    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->Clear();

    ImFontConfig rubikCfg {};
    rubikCfg.FontDataOwnedByAtlas = false;
    g_rubikFont = io.Fonts->AddFontFromMemoryTTF(
        const_cast<void *>(static_cast<const void *>(EmbeddedFonts::rubik_regular_ttf)),
        sizeof(EmbeddedFonts::rubik_regular_ttf),
        scaledFontSize,
        &rubikCfg
    );

    if (!g_rubikFont) {
        LOG_ERROR("Failed to load rubik-regular.ttf font.");
        g_rubikFont = io.Fonts->AddFontDefault();
    }

    ImFontConfig iconCfg {};
    iconCfg.MergeMode = true;
    iconCfg.PixelSnapH = true;
    iconCfg.FontDataOwnedByAtlas = false;
    iconCfg.GlyphMinAdvanceX = scaledFontSize;

    static constexpr ImWchar fa_solid_ranges[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};
    g_iconFont = io.Fonts->AddFontFromMemoryTTF(
        const_cast<void *>(static_cast<const void *>(EmbeddedFonts::fa_solid_ttf)),
        sizeof(EmbeddedFonts::fa_solid_ttf),
        scaledFontSize,
        &iconCfg,
        fa_solid_ranges
    );

    if (!g_iconFont && g_rubikFont) {
        LOG_ERROR("Failed to load fa-solid.ttf font for icons.");
    }

    io.FontDefault = g_rubikFont;
}

[[nodiscard]]
std::expected<TextureLoadResult, std::string> LoadTextureFromFile(const char *fileName) {
    std::ifstream file(fileName, std::ios::binary | std::ios::ate);
    if (!file) {
        return std::unexpected(std::format("Failed to open file: {}", fileName));
    }

    const std::streamsize fileSize = file.tellg();
    if (fileSize <= 0) {
        return std::unexpected("Failed to determine file size");
    }

    file.seekg(0, std::ios::beg);

    std::vector<char> fileData(static_cast<size_t>(fileSize));
    if (!file.read(fileData.data(), fileSize)) {
        return std::unexpected(std::format("Failed to read file: expected {} bytes", fileSize));
    }

    return LoadTextureFromMemory(fileData.data(), static_cast<size_t>(fileSize));
}

namespace AccountProcessor {

    [[nodiscard]]
    std::vector<AccountSnapshot> takeAccountSnapshots() {
        std::shared_lock lock(g_accountsMutex);
        return {g_accounts.begin(), g_accounts.end()};
    }

    [[nodiscard]]
    ProcessResult processAccount(const AccountSnapshot &account) {
        ProcessResult result {
            .id = account.id,
            .userId = account.userId,
            .username = account.username,
            .displayName = account.displayName,
            .status = "Unknown"
        };

        if (account.cookie.empty()) {
            return result;
        }

        auto accountInfo = Roblox::fetchFullAccountInfo(account.cookie);

        if (!accountInfo) {
            switch (accountInfo.error()) {
                case Roblox::ApiError::InvalidCookie:
                    result.isInvalid = true;
                    result.status = "InvalidCookie";
                    result.voiceStatus = "N/A";
                    result.shouldDeselect = true;
                    return result;

                case Roblox::ApiError::NetworkError:
                case Roblox::ApiError::Timeout:
                case Roblox::ApiError::ConnectionFailed:
                    result.status = "Network Error";
                    result.voiceStatus = "N/A";
                    result.shouldDeselect = false;
                    return result;

                default:
                    result.status = "Error";
                    result.voiceStatus = "N/A";
                    return result;
            }
        }

        const auto &info = *accountInfo;

        result.userId = std::to_string(info.userId);
        result.username = info.username;
        result.displayName = info.displayName;

        switch (info.banInfo.status) {
            case Roblox::BanCheckResult::InvalidCookie:
                result.isInvalid = true;
                result.status = "InvalidCookie";
                result.voiceStatus = "N/A";
                result.shouldDeselect = true;
                return result;

            case Roblox::BanCheckResult::Banned:
                result.status = "Banned";
                result.banExpiry = info.banInfo.endDate;
                result.voiceStatus = "N/A";
                result.shouldDeselect = true;
                return result;

            case Roblox::BanCheckResult::Warned:
                result.status = "Warned";
                result.shouldDeselect = true;
                break;

            case Roblox::BanCheckResult::Terminated:
                result.status = "Terminated";
                result.voiceStatus = "N/A";
                result.shouldDeselect = true;
                return result;

            case Roblox::BanCheckResult::NetworkError:
                result.status = "Network Error";
                result.voiceStatus = "N/A";
                return result;

            case Roblox::BanCheckResult::Unbanned:
                break;
        }

        result.voiceStatus = info.voiceSettings.status;
        result.voiceBanExpiry = info.voiceSettings.bannedUntil;

        if (info.userId != 0 && info.banInfo.status == Roblox::BanCheckResult::Unbanned) {
            auto presenceData = Roblox::getPresenceData(account.cookie, info.userId);
            if (presenceData) {
                result.status = presenceData->presence;
                result.lastLocation = presenceData->lastLocation;
                result.placeId = presenceData->placeId;
                result.jobId = presenceData->jobId;
            } else {
                result.status = info.presence;
            }
        } else {
            result.status = info.presence;
        }

        return result;
    }

    void applyResults(const std::vector<ProcessResult> &results) {
        std::unique_lock lock(g_accountsMutex);

        for (const auto &result : results) {
            auto it = std::ranges::find_if(g_accounts, [&result](const AccountData &a) {
                return a.id == result.id;
            });

            if (it == g_accounts.end()) {
                continue;
            }

            it->userId = result.userId;
            it->username = result.username;
            it->displayName = result.displayName;
            it->status = result.status;
            it->lastLocation = result.lastLocation;
            it->placeId = result.placeId;
            it->jobId = result.jobId;
            it->voiceStatus = result.voiceStatus;
            it->banExpiry = result.banExpiry;
            it->voiceBanExpiry = result.voiceBanExpiry;

            if (result.shouldDeselect) {
                std::lock_guard selLock(g_selectionMutex);
                g_selectedAccountIds.erase(result.id);
            }
        }

        invalidateAccountIndex();
    }

    void showInvalidCookieModal(std::vector<int> invalidIds, std::string invalidNames) {
        if (invalidIds.empty()) {
            return;
        }

        WorkerThreads::RunOnMain([ids = std::move(invalidIds), names = std::move(invalidNames)]() {
            auto message = std::format("Invalid cookies for: {}. Remove them?", names);

            ModalPopup::AddYesNo(message.c_str(), [ids]() {
                std::unique_lock lock(g_accountsMutex);

                std::erase_if(g_accounts, [&ids](const AccountData &a) {
                    return std::ranges::find(ids, a.id) != ids.end();
                });

                invalidateAccountIndex();

                for (int id : ids) {
                    g_selectedAccountIds.erase(id);
                }

                Data::SaveAccounts();
            });
        });
    }

} // namespace AccountProcessor

void refreshAccounts() {
    auto snapshots = AccountProcessor::takeAccountSnapshots();

    if (snapshots.empty()) {
        return;
    }

    std::vector<std::future<AccountProcessor::ProcessResult>> futures;
    futures.reserve(snapshots.size());

    for (const auto &snapshot : snapshots) {
        futures.push_back(std::async(std::launch::async, [snapshot]() {
            return AccountProcessor::processAccount(snapshot);
        }));
    }

    std::vector<AccountProcessor::ProcessResult> results;
    results.reserve(futures.size());

    std::vector<int> invalidIds;
    std::string invalidNames;

    for (size_t i = 0; i < futures.size(); ++i) {
        auto result = futures[i].get();

        if (result.isInvalid) {
            invalidIds.push_back(result.id);
            if (!invalidNames.empty()) {
                invalidNames.append(", ");
            }
            const auto &snapshot = snapshots[i];
            invalidNames.append(snapshot.displayName.empty() ? snapshot.username : snapshot.displayName);
        }

        results.push_back(std::move(result));
    }

    WorkerThreads::RunOnMain([results = std::move(results),
                             invalidIds = std::move(invalidIds),
                             invalidNames = std::move(invalidNames)]() mutable {
        AccountProcessor::applyResults(results);
        Data::SaveAccounts();
        LOG_INFO("Loaded accounts and refreshed statuses");

        AccountProcessor::showInvalidCookieModal(std::move(invalidIds), std::move(invalidNames));
    });
}

void startAccountRefreshLoop() {
    WorkerThreads::runBackground([] {
        refreshAccounts();

        while (g_running.load(std::memory_order_relaxed)) {
            if (ShutdownManager::instance().sleepFor(std::chrono::minutes(g_statusRefreshInterval))) {
                break;
            }

            if (!g_running.load()) {
                break;
            }

            refreshAccounts();
        }

        LOG_INFO("Account refresh loop exiting");
    });
}

void initializeAutoUpdater() {
    AutoUpdater::Initialize();
    AutoUpdater::SetBandwidthLimit(5_MB);
    AutoUpdater::SetUpdateChannel(UpdateChannel::Stable);
    AutoUpdater::SetAutoUpdate(true, true, false);
}

[[nodiscard]]
bool initializeApp() {
    if (auto result = Crypto::initialize(); !result) {
        std::println("Failed to initialize crypto library {}", Crypto::errorToString(result.error()));
        return false;
    }

    HttpClient::RateLimiter::instance().configure(50, std::chrono::milliseconds(1000));

    Data::LoadSettings("settings.json");

    if (g_checkUpdatesOnStartup) {
        initializeAutoUpdater();
    }

    Data::LoadAccounts("accounts.json");
    Data::LoadFriends("friends.json");

    startAccountRefreshLoop();

    return true;
}
