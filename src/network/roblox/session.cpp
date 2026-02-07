#include "session.h"

#include <format>

#include <nlohmann/json.hpp>

#include "common.h"
#include "auth.h"
#include "common.h"
#include "console/console.h"
#include "network/http.h"

namespace Roblox {

    namespace {
        // TTL Cache for presence data (1 minute expiry)
        TtlCache<uint64_t, PresenceData> g_presenceCache {std::chrono::minutes(1)};

        // TTL Cache for age group (48 hour expiry)
        TtlCache<std::string, std::string> g_ageGroupCache {std::chrono::hours(48)};

        // TTL Cache for user settings fields (48 hour expiry)
        TtlCache<std::string, std::unordered_map<std::string, std::string>> g_userSettingsCache {std::chrono::hours(48)};

        std::string parseAgeGroupKey(const std::string &translationKey) {
            constexpr std::string_view prefix = "Label.AgeGroup";
            if (translationKey.starts_with(prefix)) {
                return translationKey.substr(prefix.size());
            }
            return translationKey;
        }
    } // namespace

    std::string getPresence(const std::string &cookie, uint64_t userId) {
        BanCheckResult status = cachedBanStatus(cookie);
        if (status == BanCheckResult::InvalidCookie) {
            return "InvalidCookie";
        }
        if (!canUseCookie(cookie)) {
            return std::string(banResultToString(status));
        }

        if (auto cached = g_presenceCache.get(userId)) {
            return cached->presence;
        }

        LOG_INFO("Fetching user presence for {}", userId);

        nlohmann::json payload = {
            {"userIds", {userId}}
        };

        HttpClient::Response response = HttpClient::post(
            "https://presence.roblox.com/v1/presence/users",
            {
                {"Cookie",       ".ROBLOSECURITY=" + cookie},
                {"Content-Type", "application/json"        }
            },
            payload.dump()
        );

        if (response.status_code < 200 || response.status_code >= 300) {
            LOG_ERROR("Presence lookup failed: HTTP {}", response.status_code);

            if (response.status_code == 403) {
                return "Banned";
            }
            return "Offline";
        }

        auto json = HttpClient::decode(response);

        if (json.contains("userPresences") && json["userPresences"].is_array() && !json["userPresences"].empty()) {
            const auto &jsonData = json["userPresences"][0];
            int typeInt = jsonData.value("userPresenceType", 0);
            std::string presenceStr = presenceTypeToString(typeInt);

            PresenceData data;
            data.presence = presenceStr;
            data.lastLocation = jsonData.value("lastLocation", "");
            if (jsonData.contains("placeId") && jsonData["placeId"].is_number_unsigned()) {
                data.placeId = jsonData["placeId"].get<uint64_t>();
            }
            if (jsonData.contains("gameId") && !jsonData["gameId"].is_null()) {
                data.jobId = jsonData["gameId"].get<std::string>();
            }

            g_presenceCache.set(userId, data);

            LOG_INFO("Got user presence for {}: {}", userId, presenceStr);
            return presenceStr;
        }

        return "Offline";
    }

    ApiResult<PresenceData> getPresenceData(const std::string &cookie, uint64_t userId) {
        ApiError validationError = validateCookieForRequest(cookie);
        if (validationError != ApiError::Success) {
            return std::unexpected(validationError);
        }

        if (auto cached = g_presenceCache.get(userId)) {
            return *cached;
        }

        LOG_INFO("Fetching user presence for {}", userId);

        nlohmann::json payload = {
            {"userIds", {userId}}
        };

        HttpClient::Response response = HttpClient::post(
            "https://presence.roblox.com/v1/presence/users",
            {
                {"Cookie",       ".ROBLOSECURITY=" + cookie},
                {"Content-Type", "application/json"        }
            },
            payload.dump()
        );

        if (response.status_code < 200 || response.status_code >= 300) {
            LOG_ERROR("Presence lookup failed: HTTP {}", response.status_code);
            return std::unexpected(httpStatusToError(response.status_code));
        }

        auto json = HttpClient::decode(response);

        if (!json.contains("userPresences") || !json["userPresences"].is_array()
            || json["userPresences"].empty()) {
            return std::unexpected(ApiError::InvalidResponse);
        }

        const auto &jsonData = json["userPresences"][0];

        PresenceData data;
        data.presence = presenceTypeToString(jsonData.value("userPresenceType", 0));
        data.lastLocation = jsonData.value("lastLocation", "");

        if (jsonData.contains("placeId") && jsonData["placeId"].is_number_unsigned()) {
            data.placeId = jsonData["placeId"].get<uint64_t>();
        }
        if (jsonData.contains("gameId") && !jsonData["gameId"].is_null()) {
            data.jobId = jsonData["gameId"].get<std::string>();
        }

        // Cache the result
        g_presenceCache.set(userId, data);

        return data;
    }

    std::unordered_map<uint64_t, PresenceData>
    getPresences(const std::vector<uint64_t> &userIds, const std::string &cookie) {
        if (!canUseCookie(cookie)) {
            return {};
        }

        if (userIds.empty()) {
            return {};
        }

        // Check which users we need to fetch (not in cache)
        std::vector<uint64_t> uncachedIds;
        std::unordered_map<uint64_t, PresenceData> result;

        for (uint64_t id: userIds) {
            if (auto cached = g_presenceCache.get(id)) {
                result[id] = *cached;
            } else {
                uncachedIds.push_back(id);
            }
        }

        // If all cached, return early
        if (uncachedIds.empty()) {
            return result;
        }

        LOG_INFO("Fetching batch presence for {} users ({} cached)", userIds.size(), result.size());

        nlohmann::json payload = {
            {"userIds", uncachedIds}
        };

        auto resp = HttpClient::post(
            "https://presence.roblox.com/v1/presence/users",
            {
                {"Cookie",       ".ROBLOSECURITY=" + cookie},
                {"Content-Type", "application/json"        }
            },
            payload.dump()
        );

        if (resp.status_code < 200 || resp.status_code >= 300) {
            LOG_ERROR("Batch presence failed: HTTP {}", resp.status_code);
            return result; // Return what we have from cache
        }

        nlohmann::json j = HttpClient::decode(resp);

        if (j.contains("userPresences") && j["userPresences"].is_array()) {
            for (auto &up: j["userPresences"]) {
                if (!up.contains("userId")) {
                    continue;
                }

                uint64_t userId = up["userId"].get<uint64_t>();

                PresenceData d;
                d.presence = presenceTypeToString(up.value("userPresenceType", 0));
                d.lastLocation = up.value("lastLocation", "");

                if (up.contains("placeId") && up["placeId"].is_number_unsigned()) {
                    d.placeId = up["placeId"].get<uint64_t>();
                }

                // API uses field name 'gameId' for jobId
                if (up.contains("gameId") && !up["gameId"].is_null()) {
                    d.jobId = up["gameId"].get<std::string>();
                }

                g_presenceCache.set(userId, d);
                result[userId] = std::move(d);
            }
        }

        return result;
    }

    ApiResult<std::unordered_map<uint64_t, PresenceData>>
    getPresencesBatch(const std::vector<uint64_t> &userIds, const std::string &cookie) {
        ApiError validationError = validateCookieForRequest(cookie);
        if (validationError != ApiError::Success) {
            return std::unexpected(validationError);
        }

        if (userIds.empty()) {
            return std::unordered_map<uint64_t, PresenceData> {};
        }

        return getPresences(userIds, cookie);
    }

    VoiceSettings getVoiceChatStatus(const std::string &cookie) {
        // First check if account is banned/warned/terminated
        BanCheckResult status = cachedBanStatus(cookie);
        if (status == BanCheckResult::Banned || status == BanCheckResult::Warned
            || status == BanCheckResult::Terminated || status == BanCheckResult::InvalidCookie) {
            return {"N/A", 0};
        }

        LOG_INFO("Fetching voice chat settings");

        auto resp = HttpClient::get(
            "https://voice.roblox.com/v1/settings",
            {
                {"Cookie", ".ROBLOSECURITY=" + cookie}
            }
        );

        if (resp.status_code < 200 || resp.status_code >= 300) {
            LOG_INFO("Failed to fetch voice settings: HTTP {}", resp.status_code);
            return {"Unknown", 0};
        }

        auto j = HttpClient::decode(resp);

        bool banned = j.value("isBanned", false);
        bool enabled = j.value("isVoiceEnabled", false);
        bool eligible = j.value("isUserEligible", false);
        bool opted = j.value("isUserOptIn", false);

        time_t bannedUntil = 0;
        if (j.contains("bannedUntil") && !j["bannedUntil"].is_null()) {
            if (j["bannedUntil"].contains("Seconds")) {
                bannedUntil = j["bannedUntil"]["Seconds"].get<int64_t>();
            }
        }

        if (banned) {
            return {"Banned", bannedUntil};
        }
        if (enabled || opted) {
            return {"Enabled", 0};
        }
        if (eligible) {
            return {"Disabled", 0};
        }

        return {"Disabled", 0};
    }

    ApiResult<std::string> getAgeGroup(const std::string &cookie) {
        ApiError validationError = validateCookieForRequest(cookie);
        if (validationError != ApiError::Success) {
            return std::unexpected(validationError);
        }

        if (auto cached = g_ageGroupCache.get(cookie)) {
            return *cached;
        }

        LOG_INFO("Fetching account age group");

        auto resp = HttpClient::get(
            "https://apis.roblox.com/user-settings-api/v1/account-insights/age-group",
            {
                {"Cookie", ".ROBLOSECURITY=" + cookie}
            }
        );

        if (resp.status_code < 200 || resp.status_code >= 300) {
            LOG_ERROR("Age group fetch failed: HTTP {}", resp.status_code);
            return std::unexpected(httpStatusToError(resp.status_code));
        }

        auto j = HttpClient::decode(resp);

        std::string ageGroup = parseAgeGroupKey(j.value("ageGroupTranslationKey", ""));
        if (ageGroup.empty()) {
            return std::unexpected(ApiError::InvalidResponse);
        }

        g_ageGroupCache.set(cookie, ageGroup);
        return ageGroup;
    }

    ApiResult<std::string> getUserSetting(const std::string &cookie, const std::string &key) {
        ApiError validationError = validateCookieForRequest(cookie);
        if (validationError != ApiError::Success) {
            return std::unexpected(validationError);
        }

        if (auto cached = g_userSettingsCache.get(cookie)) {
            auto it = cached->find(key);
            if (it != cached->end()) {
                return it->second;
            }
            return std::unexpected(ApiError::NotFound);
        }

        LOG_INFO("Fetching user settings");

        auto resp = HttpClient::get(
            "https://apis.roblox.com/user-settings-api/v1/user-settings/settings-and-options",
            {{"Cookie", ".ROBLOSECURITY=" + cookie}}
        );

        if (resp.status_code < 200 || resp.status_code >= 300) {
            LOG_ERROR("User settings fetch failed: HTTP {}", resp.status_code);
            return std::unexpected(ApiError::NetworkError);
        }

        auto j = HttpClient::decode(resp);

        std::unordered_map<std::string, std::string> settings;

        for (auto &[settingKey, val] : j.items()) {
            if (val.is_object() && val.contains("currentValue")) {
                const auto &cv = val["currentValue"];
                if (cv.is_string()) {
                    settings[settingKey] = cv.get<std::string>();
                } else if (cv.is_boolean()) {
                    settings[settingKey] = cv.get<bool>() ? "true" : "false";
                } else if (cv.is_number_integer()) {
                    settings[settingKey] = std::to_string(cv.get<int64_t>());
                }
            }
        }

        g_userSettingsCache.set(cookie, settings);

        auto it = settings.find(key);
        if (it == settings.end()) {
            return std::unexpected(ApiError::NotFound);
        }

        return it->second;
    }

    ApiResult<std::string> getOnlineStatusVisibility(const std::string &cookie) {
        return getUserSetting(cookie, "whoCanSeeMyOnlineStatus");
    }

    ApiResult<std::string> getJoinRestriction(const std::string &cookie) {
        return getUserSetting(cookie, "whoCanJoinMeInExperiences");
    }

    ApiResult<void> setUserSetting(const std::string &cookie, const std::string &key, const std::string &value) {
        ApiError validationError = validateCookieForRequest(cookie);
        if (validationError != ApiError::Success) {
            return std::unexpected(validationError);
        }

        nlohmann::json payload = {{key, value}};
        auto resp = authenticatedPost(
            "https://apis.roblox.com/user-settings-api/v1/user-settings",
            cookie,
            payload.dump()
        );

        if (resp.status_code < 200 || resp.status_code >= 300) {
            LOG_ERROR("Failed to set user setting '{}': HTTP {}", key, resp.status_code);
            return std::unexpected(httpStatusToError(resp.status_code));
        }

        g_userSettingsCache.invalidate(cookie);
        return {};
    }

    ApiResult<void> setOnlineStatusVisibility(const std::string &cookie, const std::string &value) {
        return setUserSetting(cookie, "whoCanSeeMyOnlineStatus", value);
    }

    ApiResult<void> setJoinRestriction(const std::string &cookie, const std::string &value) {
        return setUserSetting(cookie, "whoCanJoinMeInExperiences", value);
    }

    void clearPresenceCache() {
        g_presenceCache.clear();
    }

    void invalidatePresenceCache(uint64_t userId) {
        g_presenceCache.invalidate(userId);
    }

} // namespace Roblox
