#include "auth.h"
#include "network/roblox/hba.h"

#include <format>

#include "common.h"
#include "console/console.h"
#include "network/http.h"
#include "session.h"
#include "utils/time_utils.h"

namespace Roblox {

    namespace {
        // TTL Cache for ban status (30 minutes)
        TtlCache<std::string, BanInfo> g_banCache {std::chrono::minutes(30)};

        TtlCache<std::string, RestrictionInfo> g_restrictionCache {std::chrono::minutes(30)};

        // TTL Cache for authenticated user info (1 hour)
        TtlCache<std::string, AuthenticatedUserInfo> g_userInfoCache {std::chrono::hours(1)};
    } // namespace

    BanInfo checkBanStatus(const std::string &cookie) {
        LOG_INFO("Checking moderation status");

        HttpClient::Response response = HttpClient::rateLimitedGet(
            "https://usermoderation.roblox.com/v1/not-approved",
            {
                {"Cookie", ".ROBLOSECURITY=" + cookie}
            }
        );

        if (response.status_code < 200 || response.status_code >= 300) {
            LOG_ERROR("Failed moderation check: HTTP {}", response.status_code);

            if (response.status_code == 401 || response.status_code == 403) {
                return {BanCheckResult::InvalidCookie, 0, 0};
            }

            if (response.status_code == 429) {
                HttpClient::RateLimiter::instance().backoff(std::chrono::seconds(2));
                return {BanCheckResult::NetworkError, 0, 0};
            }

            return {BanCheckResult::NetworkError, 0, 0};
        }

        auto j = HttpClient::decode(response);

        if (j.is_object() && j.contains("punishmentTypeDescription")) {
            std::string punishmentType = j["punishmentTypeDescription"].get<std::string>();
            time_t end = 0;
            uint64_t punishedUserId = j.value("punishedUserId", 0ULL);
            bool hasEndDate = j.contains("endDate") && j["endDate"].is_string()
                              && !j["endDate"].get<std::string>().empty();

            if (hasEndDate) {
                end = parseIsoTimestamp(j["endDate"].get<std::string>());
                return {BanCheckResult::Banned, end, punishedUserId};
            }

            if (punishmentType == "Delete") {
                return {BanCheckResult::Terminated, 0, punishedUserId};
            }

            if (punishmentType == "Warn") {
                return {BanCheckResult::Warned, 0, punishedUserId};
            }

            // Default to banned for other punishment types without end date
            return {BanCheckResult::Banned, 0, punishedUserId};
        }

        return {BanCheckResult::Unbanned, 0, 0};
    }

    RestrictionInfo checkRestrictionStatus(const std::string &cookie) {
        LOG_INFO("Checking restriction status");

        HttpClient::Response response = HttpClient::rateLimitedGet(
            "https://usermoderation.roblox.com/v2/not-approved",
            {{"Cookie", ".ROBLOSECURITY=" + cookie}}
        );

        if (response.status_code < 200 || response.status_code >= 300) {
            LOG_ERROR("Failed restriction check: HTTP {}", response.status_code);

            if (response.status_code == 401 || response.status_code == 403) {
                return {RestrictionCheckResult::InvalidCookie, 0, 0, 0, 0};
            }
            if (response.status_code == 429) {
                HttpClient::RateLimiter::instance().backoff(std::chrono::seconds(2));
                return {RestrictionCheckResult::NetworkError, 0, 0, 0, 0};
            }

            return {RestrictionCheckResult::NetworkError, 0, 0, 0, 0};
        }

        auto j = HttpClient::decode(response);

        if (!j.is_object() || !j.contains("restriction") || j["restriction"].is_null()) {
            return {RestrictionCheckResult::Unknown0, 0, 0, 0, 0};
        }

        const auto &restriction = j["restriction"];

        int sourceStatus     = restriction.value("source", 0);
        int moderationStatus = restriction.value("moderationStatus", 0);

        time_t startTime = 0;
        if (restriction.contains("startTime") && restriction["startTime"].is_string()
            && !restriction["startTime"].get<std::string>().empty()) {
            startTime = parseIsoTimestamp(restriction["startTime"].get<std::string>());
            }

        time_t endTime = 0;
        if (restriction.contains("endTime") && restriction["endTime"].is_string()
            && !restriction["endTime"].get<std::string>().empty()) {
            endTime = parseIsoTimestamp(restriction["endTime"].get<std::string>());
            }

        uint64_t durationSeconds = 0;
        if (restriction.contains("durationSeconds") && restriction["durationSeconds"].is_number()) {
            durationSeconds = restriction["durationSeconds"].get<uint64_t>();
        }

        switch (sourceStatus) {
            case 1:
                return {RestrictionCheckResult::Banned, moderationStatus, startTime, endTime, durationSeconds};
            case 2:
                return {RestrictionCheckResult::ScreenTimeLimit, moderationStatus, startTime, endTime, durationSeconds};
            case 5:
                return {RestrictionCheckResult::AccountLocked, moderationStatus, startTime, endTime, durationSeconds};
            default:
                break;
        }

        if (moderationStatus == 3) {
            return {RestrictionCheckResult::Banned, moderationStatus, startTime, endTime, durationSeconds};
        }

        return {RestrictionCheckResult::Unknown0, moderationStatus, startTime, endTime, durationSeconds};
    }

    BanInfo cachedBanInfo(const std::string &cookie) {
        if (auto cached = g_banCache.get(cookie)) {
            return *cached;
        }

        BanInfo info = checkBanStatus(cookie);
        g_banCache.set(cookie, info);

        return info;
    }

    RestrictionInfo cachedRestrictionInfo(const std::string &cookie) {
        if (auto cached = g_restrictionCache.get(cookie)) {
            return *cached;
        }

        RestrictionInfo info = checkRestrictionStatus(cookie);
        g_restrictionCache.set(cookie, info);
        return info;
    }

    BanInfo refreshBanInfo(const std::string &cookie) {
        g_banCache.invalidate(cookie);
        BanInfo info = checkBanStatus(cookie);
        g_banCache.set(cookie, info);
        return info;
    }

    bool isCookieValid(const std::string &cookie) {
        return cachedBanInfo(cookie).status != BanCheckResult::InvalidCookie;
    }

    bool canUseCookie(const std::string &cookie) {
        return validateCookieForRequest(cookie) == ApiError::Success;
    }

    nlohmann::json getAuthenticatedUser(const std::string &cookie) {
        if (!canUseCookie(cookie)) {
            return nlohmann::json::object();
        }

        if (auto cached = g_userInfoCache.get(cookie)) {
            return nlohmann::json {
                {"id",          cached->userId     },
                {"name",        cached->username   },
                {"displayName", cached->displayName}
            };
        }

        LOG_INFO("Fetching profile info");

        HttpClient::Response response = HttpClient::rateLimitedGet(
            "https://users.roblox.com/v1/users/authenticated",
            {
                {"Cookie", ".ROBLOSECURITY=" + cookie}
        }
        );

        if (response.status_code < 200 || response.status_code >= 300) {
            LOG_ERROR("Failed to fetch user info: HTTP {}", response.status_code);
            return nlohmann::json::object();
        }

        auto j = HttpClient::decode(response);

        if (j.is_object() && j.contains("id")) {
            AuthenticatedUserInfo info;
            info.userId = j.value("id", 0ULL);
            info.username = j.value("name", "");
            info.displayName = j.value("displayName", "");
            g_userInfoCache.set(cookie, info);
        }

        return j;
    }

    ApiResult<AuthenticatedUserInfo> getAuthenticatedUserInfo(const std::string &cookie) {
        ApiError validationError = validateCookieForRequest(cookie);
        if (validationError != ApiError::Success) {
            return std::unexpected(validationError);
        }

        if (auto cached = g_userInfoCache.get(cookie)) {
            return *cached;
        }

        LOG_INFO("Fetching profile info");

        HttpClient::Response response = HttpClient::rateLimitedGet(
            "https://users.roblox.com/v1/users/authenticated",
            {
                {"Cookie", ".ROBLOSECURITY=" + cookie}
        }
        );

        if (response.status_code < 200 || response.status_code >= 300) {
            LOG_ERROR("Failed to fetch user info: HTTP {}", response.status_code);
            return std::unexpected(httpStatusToError(response.status_code));
        }

        auto j = HttpClient::decode(response);

        if (!j.is_object() || !j.contains("id")) {
            return std::unexpected(ApiError::InvalidResponse);
        }

        AuthenticatedUserInfo info;
        info.userId = j.value("id", 0ULL);
        info.username = j.value("name", "");
        info.displayName = j.value("displayName", "");

        g_userInfoCache.set(cookie, info);

        return info;
    }

    ApiResult<FullAccountInfo> fetchFullAccountInfo(const std::string &cookie) {
        FullAccountInfo result;
        result.banInfo = cachedBanInfo(cookie);
        result.restrictionInfo = cachedRestrictionInfo(cookie);

        if (result.banInfo.status == BanCheckResult::InvalidCookie) {
            if (result.restrictionInfo.status != RestrictionCheckResult::AccountLocked) {
                result.voiceSettings = {"N/A", 0};
                return result;
            }
        }

        const bool shouldFetchUserInfo =
            result.banInfo.status == BanCheckResult::Unbanned ||
            result.restrictionInfo.status == RestrictionCheckResult::AccountLocked;

        if (shouldFetchUserInfo) {
            auto userInfo = getAuthenticatedUserInfo(cookie);
            if (userInfo) {
                result.userId = userInfo->userId;
                result.username = userInfo->username;
                result.displayName = userInfo->displayName;
            }
        }

        const bool shouldFetchVoice =
            result.banInfo.status == BanCheckResult::Unbanned &&
            result.restrictionInfo.status != RestrictionCheckResult::AccountLocked;

        if (shouldFetchVoice) {
            result.voiceSettings = getVoiceChatStatus(cookie);
        }
        else {
            result.voiceSettings = {"N/A", 0};
        }

        //result.ageGroup = getAgeGroup(cookie);

        return result;
    }

    uint64_t getUserId(const std::string &cookie) {
        auto result = getAuthenticatedUserInfo(cookie);
        return result ? result->userId : 0;
    }

    std::string getUsername(const std::string &cookie) {
        auto result = getAuthenticatedUserInfo(cookie);
        return result ? result->username : "";
    }

    std::string getDisplayName(const std::string &cookie) {
        auto result = getAuthenticatedUserInfo(cookie);
        return result ? result->displayName : "";
    }

    std::string fetchAuthTicket(const std::string &cookie) {
        if (!canUseCookie(cookie)) {
            return "";
        }

        LOG_INFO("Fetching authentication ticket");

        auto response = authenticatedPost("https://auth.roblox.com/v1/authentication-ticket", cookie);

        if (response.status_code < 200 || response.status_code >= 300) {
            LOG_ERROR("Failed to fetch auth ticket: HTTP {}", response.status_code);
            return "";
        }

        auto ticket = response.headers.find("rbx-authentication-ticket");
        if (ticket == response.headers.end()) {
            LOG_ERROR("Failed to get authentication ticket from response headers");
            return "";
        }

        return ticket->second;
    }

    std::string fetchAuthTicketSecure(const std::string &cookie) {
        if (!canUseCookie(cookie)) {
            return "";
        }

        auto assertionResult = Hba::fetchClientAssertion(cookie);
        if (!assertionResult) {
            LOG_ERROR("Failed to fetch client assertion: {}", apiErrorToString(assertionResult.error()));
            return "";
        }

        const std::string url = "https://auth.roblox.com/v1/authentication-ticket/";
        const std::string body = nlohmann::json{{"clientAssertion", *assertionResult}}.dump();

        auto tokenResult = Hba::buildBoundAuthToken(cookie, url, body);
        if (!tokenResult) {
            LOG_ERROR("Failed to build bound auth token for auth ticket");
            return "";
        }

        auto response = authenticatedPost(
            url,
            cookie,
            body,
            {
                {"x-bound-auth-token", *tokenResult}
            }
        );

        if (response.status_code < 200 || response.status_code >= 300) {
            LOG_ERROR("Failed to fetch auth ticket: HTTP {}", response.status_code);
            return "";
        }

        auto it = response.headers.find("rbx-authentication-ticket");
        if (it == response.headers.end()) {
            LOG_ERROR("Failed to get authentication ticket from response headers");
            return "";
        }

        return it->second;
    }

    void clearAuthCaches() {
        g_banCache.clear();
        g_userInfoCache.clear();
        g_restrictionCache.clear();
        CsrfManager::instance().clear();
    }

    void invalidateCacheForCookie(const std::string &cookie) {
        g_banCache.invalidate(cookie);
        g_userInfoCache.invalidate(cookie);
        g_restrictionCache.invalidate(cookie);
        CsrfManager::instance().invalidateToken(cookie);
    }

    ApiResult<std::string> refreshCookie(const std::string &cookie) {
        ApiError validationError = validateCookieForRequest(cookie);
        if (validationError != ApiError::Success) {
            return std::unexpected(validationError);
        }

        LOG_INFO("Refreshing cookie");

        auto intentResult = Hba::buildSecureAuthIntent(cookie);
        if (!intentResult) {
            LOG_ERROR("Failed to build secure auth intent");
            return std::unexpected(intentResult.error());
        }

        nlohmann::json body = {
            {"secureAuthenticationIntent", nlohmann::json::parse(*intentResult)}
        };
        std::string bodyStr = body.dump();

        auto tokenResult = Hba::buildBoundAuthToken(
            cookie,
            "https://auth.roblox.com/v1/logoutfromallsessionsandreauthenticate",
            bodyStr
        );

        auto response = authenticatedPost(
            "https://auth.roblox.com/v1/logoutfromallsessionsandreauthenticate",
            cookie,
            bodyStr,
            {
                {"x-bound-auth-token", tokenResult ? *tokenResult : ""}
        }
        );

        if (response.status_code < 200 || response.status_code >= 300) {
            LOG_ERROR("Cookie refresh failed: HTTP {}", response.status_code);
            return std::unexpected(httpStatusToError(response.status_code));
        }

        auto it = response.headers.find("set-cookie");
        if (it == response.headers.end()) {
            return std::unexpected(ApiError::InvalidResponse);
        }

        const std::string prefix = ".ROBLOSECURITY=";
        auto pos = it->second.find(prefix);
        if (pos == std::string::npos) {
            return std::unexpected(ApiError::InvalidResponse);
        }

        pos += prefix.size();
        auto end = it->second.find(';', pos);
        std::string newCookie = it->second.substr(pos, end == std::string::npos ? end : end - pos);

        if (newCookie.empty()) {
            return std::unexpected(ApiError::InvalidResponse);
        }

        LOG_INFO("Cookie refreshed successfully");
        invalidateCacheForCookie(cookie);
        return newCookie;
    }

} // namespace Roblox
