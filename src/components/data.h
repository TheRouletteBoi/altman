#pragma once

#include <array>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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
        //bool isUsingCustomClient = false;
        //std::string clientName;
        std::string customClientBase;
        bool cookieAutoRefresh = false;
        time_t cookieLastUse = 0;
        time_t cookieLastRefreshAttempt = 0;
        std::string hbaPublicKey; // HBA keypair: persisted so Roblox sees the same device across restarts
        std::string hbaPrivateKey;
};

struct AccountGroup {
    int id = 0;
    std::string name;
    std::vector<int> accountIds; // Ordered list of account IDs in this group
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

inline std::mutex g_selectionMutex;
inline std::shared_mutex g_accountsMutex;

inline std::vector<AccountData> g_accounts;
inline std::set<int> g_selectedAccountIds;

inline std::vector<FavoriteGame> g_favorites;
inline std::vector<FriendInfo> g_friends;
inline std::unordered_map<int, std::vector<FriendInfo>> g_accountFriends;
inline std::unordered_map<int, std::vector<FriendInfo>> g_unfriendedFriends;

inline std::vector<std::string> g_privateServerHistory;
constexpr int k_privateServerHistoryMax = 20;

inline std::vector<AccountGroup> g_accountGroups;
inline int g_activeGroupTab = -1;

inline int g_defaultAccountId = -1;
inline std::array<char, 128> s_jobIdBuffer = {};
inline std::array<char, 128> s_playerBuffer = {};
inline int g_rateLimitWindow = 30; // seconds
inline int g_statusRefreshInterval = 2; // minutes
inline bool g_checkUpdatesOnStartup = true;
inline bool g_killRobloxOnLaunch = false;
inline bool g_clearCacheOnLaunch = false;
inline bool g_multiRobloxEnabled = false;
inline std::unordered_map<std::string, std::string> g_clientKeys;
inline bool g_forceLatestRobloxVersion = false;
inline std::vector<std::string> g_availableClientsNames = {"Default", "MacSploit", "Hydrogen", "Delta"};
inline bool g_privacyModeEnabled = false;

void invalidateAccountIndex();
AccountData *getAccountById(int id);
std::vector<AccountData *> getUsableSelectedAccounts();
std::vector<const AccountData *> getSelectedAccountsOrdered();
std::vector<AccountData *> getSelectedAccountsOrderedMutable();
int getAccountIndexById(int id);
std::string getPrimaryAccountCookie();

AccountGroup *getGroupById(int groupId);
int generateGroupId();
void removeAccountFromAllGroups(int accountId);
std::vector<AccountData> getAccountsForGroup(int groupId);
std::vector<AccountData *> getAccountsForGroupMutable(int groupId);

namespace Data {

    std::optional<std::string> encryptLocalData(std::string_view plaintext);
    std::string decryptLocalData(std::string_view base64Encrypted);

    void LoadSettings(std::string_view filename = "settings.json");
    void SaveSettings(std::string_view filename = "settings.json");

    void LoadAccounts(std::string_view filename = "accounts.json");
    void SaveAccounts(std::string_view filename = "accounts.json");

    void LoadFavorites(std::string_view filename = "favorites.json");
    void SaveFavorites(std::string_view filename = "favorites.json");

    void LoadFriends(std::string_view filename = "friends.json");
    void SaveFriends(std::string_view filename = "friends.json");

    void LoadPrivateServerHistory(std::string_view filename = "private_server_history.json");
    void SavePrivateServerHistory(std::string_view filename = "private_server_history.json");

    void LoadAccountGroups(std::string_view filename = "account_groups.json");
    void SaveAccountGroups(std::string_view filename = "account_groups.json");
} // namespace Data
