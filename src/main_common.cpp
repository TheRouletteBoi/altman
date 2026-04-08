#include "main_common.h"
#include "assets/fonts/embedded_fa_solid.h"
#include "assets/fonts/embedded_rubik.h"

#include <algorithm>
#include <fstream>
#include <semaphore>

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

    constexpr int UNUSED_DAYS_THRESHOLD = 20;
    constexpr int RETRY_DAYS_THRESHOLD  = 7;
    constexpr double SECONDS_PER_DAY = 86400.0;

    [[nodiscard]]
    std::vector<AccountSnapshot> takeAccountSnapshots() {
        std::shared_lock lock(g_accountsMutex);
        return {g_accounts.begin(), g_accounts.end()};
    }

    [[nodiscard]]
    ProcessResult processAccount(const AccountSnapshot &account, const Roblox::FullAccountInfo &info) {
        ProcessResult result {
            .id = account.id,
            .userId = account.userId,
            .username = account.username,
            .displayName = account.displayName,
            .status = "Unknown"
        };

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
                result.voiceStatus = "N/A";
                result.shouldDeselect = true;
                return result;

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

        switch (info.restrictionInfo.status) {
            case Roblox::RestrictionCheckResult::Banned:
                result.status = "Banned";
                result.banExpiry = info.restrictionInfo.endDate;
                result.voiceStatus = "N/A";
                result.shouldDeselect = true;
                return result;

            case Roblox::RestrictionCheckResult::AccountLocked:
                result.status = "Locked";
                result.voiceStatus = "N/A";
                result.shouldDeselect = true;
                return result;

            case Roblox::RestrictionCheckResult::ScreenTimeLimit:
                result.status = "Screen Time Limit";
                result.voiceStatus = "N/A";
                result.shouldDeselect = true;
                return result;

            default:
                break;
        }

        result.voiceStatus = info.voiceSettings.status;
        result.voiceBanExpiry = info.voiceSettings.bannedUntil;

        if (info.presenceData) {
            result.status = info.presenceData->presence;
            result.lastLocation = info.presenceData->lastLocation;
            result.placeId = info.presenceData->placeId;
            result.jobId = info.presenceData->jobId;
        } else {
            result.status = "Offline";
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
            if (it->status == "Online")
                it->cookieLastUse = std::time(nullptr);

            if (result.shouldDeselect) {
                std::lock_guard selLock(g_selectionMutex);
                g_selectedAccountIds.erase(result.id);
            }
        }

        invalidateAccountIndex();
    }

    void showInvalidCookieModal(std::vector < int > invalidIds, std::string invalidNames) {
        if (invalidIds.empty()) {
            return;
        }

        WorkerThreads::RunOnMain([ids = std::move(invalidIds), names = std::move(invalidNames)]() {
            auto message = std::format("Invalid cookies for: {}. Remove them?", names);

            ModalPopup::AddYesNo(message.c_str(), [ids]() {
                WorkerThreads::runBackground([ids]() {
                    {
                        std::unique_lock lock(g_accountsMutex);

                        std::erase_if(g_accounts, [ & ids](const AccountData & a) {
                            return std::ranges::find(ids, a.id) != ids.end();
                        });
                        invalidateAccountIndex();
                    }

                    {
                        std::lock_guard selLock(g_selectionMutex);
                        for (int id: ids) {
                            g_selectedAccountIds.erase(id);
                        }
                    }

                    Data::SaveAccounts();
                });
            });
        });
    }

    bool shouldRefreshCookies(const AccountData& account) {
        if (!account.cookieAutoRefresh || account.cookie.empty())
            return false;

        auto now = std::time(nullptr);
        double daysSinceUse     = std::difftime(now, account.cookieLastUse)            / SECONDS_PER_DAY;
        double daysSinceAttempt = std::difftime(now, account.cookieLastRefreshAttempt) / SECONDS_PER_DAY;

        return daysSinceUse > UNUSED_DAYS_THRESHOLD && daysSinceAttempt >= RETRY_DAYS_THRESHOLD;
    }

} // namespace AccountProcessor

void refreshAccounts() {
    auto snapshots = AccountProcessor::takeAccountSnapshots();

    if (snapshots.empty()) {
        return;
    }

    // Phase 1: fetch per-account info in parallel (ban/restriction/user/voice, no presence)
    using InfoEntry = std::pair<size_t, Roblox::FullAccountInfo>;
    std::vector<std::future<std::optional<InfoEntry>>> futures;
    futures.reserve(snapshots.size());

    // Dynamic concurrency limit based on current account count
    const int concurrencyLimit = std::clamp(static_cast<int>(snapshots.size()) / 5, 4, 30);

    std::mutex semMutex;
    std::condition_variable semCv;
    int semCount = 0;

    for (size_t i = 0; i < snapshots.size(); ++i) {
        futures.push_back(std::async(std::launch::async, [&, i]() -> std::optional<InfoEntry> {
            const auto &snapshot = snapshots[i];
            if (snapshot.cookie.empty())
                return std::nullopt;

            {
                std::unique_lock lock(semMutex);
                semCv.wait(lock, [&] { return semCount < concurrencyLimit; });
                ++semCount;
            }

            struct SemGuard {
                std::mutex &m;
                std::condition_variable &cv;
                int &count;
                ~SemGuard() {
                    std::unique_lock lock(m);
                    --count;
                    cv.notify_one();
                }
            } guard { semMutex, semCv, semCount };

            auto result = Roblox::fetchFullAccountInfo(snapshot.cookie);
            if (!result)
                return std::nullopt;
            return InfoEntry { i, std::move(*result) };
        }));
    }

    std::vector<std::optional<InfoEntry>> infoResults;
    infoResults.reserve(futures.size());
    for (auto &f : futures) {
        infoResults.push_back(f.get());
    }

    // Phase 2: collect userIds for unbanned accounts and batch presence
    std::vector<uint64_t> presenceUserIds;
    std::string presenceCookie;

    for (const auto &entry : infoResults) {
        if (!entry) continue;
        const auto &info = entry->second;
        if (info.userId != 0 && info.banInfo.status == Roblox::BanCheckResult::Unbanned) {
            presenceUserIds.push_back(info.userId);
            if (presenceCookie.empty()) {
                presenceCookie = snapshots[entry->first].cookie;
            }
        }
    }

    std::unordered_map<uint64_t, Roblox::PresenceData> presences;
    if (!presenceUserIds.empty() && !presenceCookie.empty()) {
        if (auto batch = Roblox::getPresencesBatch(presenceUserIds, presenceCookie)) {
            presences = std::move(*batch);
        }
    }

    // Phase 3: distribute presence into FullAccountInfo, then build ProcessResults
    std::vector<AccountProcessor::ProcessResult> results;
    results.reserve(snapshots.size());

    std::vector<int> invalidIds;
    std::string invalidNames;

    for (size_t i = 0; i < snapshots.size(); ++i) {
        const auto &snapshot = snapshots[i];
        const auto &entry = infoResults[i];

        if (snapshot.cookie.empty()) {
            AccountProcessor::ProcessResult r {};
            r.id = snapshot.id;
            r.userId = snapshot.userId;
            r.username = snapshot.username;
            r.displayName = snapshot.displayName;
            r.status = "Unknown";
            results.push_back(std::move(r));
            continue;
        }

        if (!entry) {
            // fetchFullAccountInfo failed, treat as network/invalid error
            // Recheck the error by looking at a cached ban status if available
            AccountProcessor::ProcessResult r {};
            r.id = snapshot.id;
            r.userId = snapshot.userId;
            r.username = snapshot.username;
            r.displayName = snapshot.displayName;
            r.status = "Error";
            r.voiceStatus = "N/A";
            results.push_back(std::move(r));
            continue;
        }

        Roblox::FullAccountInfo info = entry->second;

        if (info.userId != 0 && info.banInfo.status == Roblox::BanCheckResult::Unbanned) {
            auto it = presences.find(info.userId);
            if (it != presences.end()) {
                info.presenceData = it->second;
            }
        }

        auto result = AccountProcessor::processAccount(snapshot, info);

        if (result.isInvalid) {
            invalidIds.push_back(result.id);
            if (!invalidNames.empty()) {
                invalidNames.append(", ");
            }
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

void refreshAccountsCookies() {
    std::vector<AccountProcessor::AccountSnapshot> snapshots;
    {
        std::shared_lock lock(g_accountsMutex);
        snapshots = {g_accounts.begin(), g_accounts.end()};
    }

    for (const auto& snapshot : snapshots) {
        if (!AccountProcessor::shouldRefreshCookies(snapshot))
            continue;

        LOG_INFO("Attempting cookie refresh for {} | Last use: {}", snapshot.username, snapshot.cookieLastUse);

        {
            std::unique_lock lock(g_accountsMutex);

            auto it = std::ranges::find_if(g_accounts, [&](const AccountData& a) {
                return a.id == snapshot.id;
            });

            if (it != g_accounts.end()) {
                it->cookieLastRefreshAttempt = std::time(nullptr);
            }
            Data::SaveAccounts();
        }

        auto result = Roblox::refreshCookie(snapshot.cookie);

        if (result) {
            std::string newCookie = std::move(*result);
            WorkerThreads::RunOnMain([id = snapshot.id, newCookie = std::move(newCookie)]() {
                std::unique_lock lock(g_accountsMutex);
                auto it = std::ranges::find_if(g_accounts, [id](const AccountData& a) {
                    return a.id == id;
                });
                if (it != g_accounts.end()) {
                    it->cookie = newCookie;
                    Roblox::invalidateCacheForCookie(it->cookie);
                }
                Data::SaveAccounts();
                LOG_INFO("Cookie refreshed and saved");
            });
        }
        else {
            LOG_ERROR("Cookie refresh failed for {}: {}", snapshot.username, Roblox::apiErrorToString(result.error()));
        }

        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

void checkAndRefreshCookiesOnce() {
    WorkerThreads::runBackground([] {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        refreshAccountsCookies();
    });
}

void initializeAutoUpdater() {
    AutoUpdater::Initialize();
    AutoUpdater::SetBandwidthLimit(50_MB);
    AutoUpdater::SetUpdateChannel(UpdateChannel::Stable);
    AutoUpdater::SetAutoUpdate(true, true, false);
}

void configureRefreshConcurrency(size_t accountCount) {
    // Target: peak in flight ≈ concurrency × 3 requests (ban → restriction → voice), at ~60% of rate limit budget
    // Formula: rateLimit = clamp(accountCount * 1.8, 30, 150) concurrency = rateLimit / 5 (keeps peak at 60% of budget)

    const int rateLimit = static_cast<int>(std::clamp(static_cast<double>(accountCount) * 1.8, 30.0, 150.0));
    HttpClient::RateLimiter::instance().configure(rateLimit, std::chrono::seconds(g_rateLimitWindow));
    LOG_INFO("Refresh config: {} accounts with rate limit {}/{}s, concurrency {}, refresh interval {}min",
        accountCount, rateLimit, g_rateLimitWindow, rateLimit / 5, g_statusRefreshInterval);
}

[[nodiscard]]
bool initializeApp() {
    if (auto result = Crypto::initialize(); !result) {
        std::println("Failed to initialize crypto library {}", Crypto::errorToString(result.error()));
        return false;
    }

    Data::LoadSettings("settings.json");

    if (g_checkUpdatesOnStartup) {
        initializeAutoUpdater();
    }

    Data::LoadAccounts("accounts.json");
    Data::LoadFriends("friends.json");
    Data::LoadFavorites("favorites.json");
    Data::LoadPrivateServerHistory("private_server_history.json");
    Data::LoadAccountGroups("account_groups.json");

    configureRefreshConcurrency(g_accounts.size());
    startAccountRefreshLoop();
    checkAndRefreshCookiesOnce();

    return true;
}
