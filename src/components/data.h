#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <set>
#include <array>
#include <ctime>
#include <unordered_map>
#include <cstdint>

struct FriendInfo;
struct AccountData;
struct FavoriteGame;

struct AccountData {
    int id = 0;
    std::string displayName;
    std::string username;
    std::string userId;
    std::string status;
    std::string voiceStatus;
    time_t voiceBanExpiry = 0;
    time_t banExpiry = 0;
    std::string note;
    std::string cookie;
    bool isFavorite = false;
    std::string lastLocation;
    uint64_t placeId = 0;
    std::string jobId;
    bool isUsingCustomClient = false;
    std::string clientName;
    std::string customClientBase;
};

struct FavoriteGame {
    std::string name;
    uint64_t universeId;
    uint64_t placeId;
};

struct FriendInfo {
    uint64_t id = 0;
    std::string username;
    std::string displayName;
    std::string presence;
    std::string lastLocation;
    uint64_t placeId = 0;
    std::string jobId;
};

extern std::vector<AccountData> g_accounts;
extern std::unordered_map<int, size_t> g_accountIndexById;
extern std::set<int> g_selectedAccountIds;
extern std::vector<FavoriteGame> g_favorites;
extern std::vector<FriendInfo> g_friends;
extern std::unordered_map<int, std::vector<FriendInfo>> g_accountFriends;
extern std::unordered_map<int, std::vector<FriendInfo>> g_unfriendedFriends;

extern int g_defaultAccountId;
extern int g_statusRefreshInterval;
extern bool g_checkUpdatesOnStartup;
extern bool g_killRobloxOnLaunch;
extern bool g_clearCacheOnLaunch;
extern std::unordered_map<std::string, std::string> g_clientKeys;
extern std::array<char, 128> s_jobIdBuffer;
extern std::array<char, 128> s_playerBuffer;

#ifdef __APPLE__
std::string GetApplicationDir();
std::string GetApplicationName();
std::string GetExecutablePath();
#endif

namespace Data {
    void LoadSettings(std::string_view filename = "settings.json");
    void SaveSettings(std::string_view filename = "settings.json");

    void LoadAccounts(std::string_view filename = "accounts.json");
    void SaveAccounts(std::string_view filename = "accounts.json");

    void LoadFavorites(std::string_view filename = "favorites.json");
    void SaveFavorites(std::string_view filename = "favorites.json");

    void LoadFriends(std::string_view filename = "friends.json");
    void SaveFriends(std::string_view filename = "friends.json");

    std::string StorageFilePath(std::string_view filename);
    void rebuildAccountIndexCache();
}