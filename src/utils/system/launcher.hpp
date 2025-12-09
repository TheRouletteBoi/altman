#pragma once

#include "network/http.hpp"
#include <iostream>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <vector>
#include <utility>

#ifdef _WIN32
    #include <windows.h>
#elif __APPLE__
    #include <unistd.h>
    #include <spawn.h>
    #include <sys/wait.h>
#endif

#include "core/logging.hpp"
#include "ui/notifications.h"
#include "../../components/data.h"
#include "roblox_control.h"

using namespace std;
using namespace std::chrono;

static string urlEncode(const string &s) {
    ostringstream out;
    out << hex << uppercase;
    for (unsigned char c: s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out << c;
        else
            out << '%' << setw(2) << setfill('0') << static_cast<int>(c);
    }
    return out.str();
}

#ifdef _WIN32
inline HANDLE startRoblox(uint64_t placeId, const string &jobId, const string &cookie) {
    LOG_INFO("Fetching x-csrf token");
    auto csrfResponse = HttpClient::post(
        "https://auth.roblox.com/v1/authentication-ticket",
        {{"Cookie", ".ROBLOSECURITY=" + cookie}});

    auto csrfToken = csrfResponse.headers.find("x-csrf-token");
    if (csrfToken == csrfResponse.headers.end()) {
        cerr << "failed to get CSRF token\n";
        LOG_ERROR("Failed to get CSRF token");
        return nullptr;
    }

    LOG_INFO("Fetching authentication ticket");
    auto ticketResponse = HttpClient::post(
        "https://auth.roblox.com/v1/authentication-ticket",
        {
            {"Cookie", ".ROBLOSECURITY=" + cookie},
            {"Origin", "https://www.roblox.com"},
            {"Referer", "https://www.roblox.com/"},
            {"X-CSRF-TOKEN", csrfToken->second}
        });

    auto ticket = ticketResponse.headers.find("rbx-authentication-ticket");
    if (ticket == ticketResponse.headers.end()) {
        cerr << "failed to get authentication ticket\n";
        LOG_ERROR("Failed to get authentication ticket");
        return nullptr;
    }

    auto nowMs = duration_cast<milliseconds>(
                system_clock::now().time_since_epoch())
            .count();
    ostringstream ts;
    ts << nowMs;

    string placeLauncherUrl =
            "https://assetgame.roblox.com/game/PlaceLauncher.ashx?"
            "request=RequestGameJob"
            "&browserTrackerId=147062882894"
            "&placeId=" +
            to_string(placeId) +
            "&gameId=" + jobId +
            "&isPlayTogetherGame=false"
            "+browsertrackerid:147062882894"
            "+robloxLocale:en_us"
            "+gameLocale:en_us"
            "+channel:";

    string protocolLaunchCommand =
            "roblox-player:1+launchmode:play"
            "+gameinfo:" +
            ticket->second +
            "+launchtime:" + ts.str() +
            "+placelauncherurl:" + urlEncode(placeLauncherUrl);

    string logMessage = "Attempting to launch Roblox for place ID: " + to_string(placeId) + (
                            jobId.empty() ? "" : " with Job ID: " + jobId);
    LOG_INFO(logMessage);

    wstring notificationTitle = L"Launching";
    wostringstream notificationMessageStream;
    notificationMessageStream << L"Attempting to launch Roblox for place ID: " << placeId;
    if (!jobId.empty()) {
        notificationMessageStream << L" with Job ID: " << jobId.c_str();
    }
    Notifications::showNotification(notificationTitle.c_str(), notificationMessageStream.str().c_str());

    SHELLEXECUTEINFOA executionInfo{sizeof(executionInfo)};
    executionInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    executionInfo.lpVerb = "open";
    executionInfo.lpFile = protocolLaunchCommand.c_str();
    executionInfo.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExA(&executionInfo)) {
        LOG_ERROR("ShellExecuteExA failed for Roblox launch. Error: " + to_string(GetLastError()));
        cerr << "ShellExecuteEx failed: " << GetLastError() << "\n";
        return nullptr;
    }

    LOG_INFO("Roblox process started successfully for place ID: " + to_string(placeId));
    return executionInfo.hProcess;
}

#elif __APPLE__
#include <random>
#include <regex>

inline bool patchRobloxBinary(const std::string& appPath) {
    std::string binaryPath = appPath + "/Contents/MacOS/RobloxPlayer";
    
    // Read the binary
    /*std::ifstream binary(binaryPath, std::ios::binary);
    if (!binary) {
        LOG_ERROR("Failed to open RobloxPlayer binary");
        return false;
    }
    
    std::vector<uint8_t> data(
        (std::istreambuf_iterator<char>(binary)),
        std::istreambuf_iterator<char>()
    );
    binary.close();
    
    // Find and replace semaphore names
    const char* oldName1 = "/RobloxPlayerUniq";
    const char* oldName2 = "/robloxPlayerStartedEvent";
    
    // Unique names
    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    std::string newName1 = "/RobloxPlayer" + std::to_string(nowMs);
    std::string newName2 = "/robloxStarted" + std::to_string(nowMs);
    
    // Pad to same length
    while (newName1.length() < std::strlen(oldName1)) newName1 += "\0";
    while (newName2.length() < std::strlen(oldName2)) newName2 += "\0";
    
    bool found1 = false, found2 = false;
    
    for (size_t i = 0; i < data.size() - std::strlen(oldName1); i++) {
        if (std::memcmp(&data[i], oldName1, std::strlen(oldName1)) == 0) {
            std::memcpy(&data[i], newName1.c_str(), newName1.length());
            found1 = true;
            LOG_INFO("Patched semaphore name 1");
        }
        if (std::memcmp(&data[i], oldName2, std::strlen(oldName2)) == 0) {
            std::memcpy(&data[i], newName2.c_str(), newName2.length());
            found2 = true;
            LOG_INFO("Patched semaphore name 2");
        }
    }
    
    if (!found1 || !found2) {
        LOG_ERROR("Failed to find semaphore names in binary");
        return false;
    }
    
    // Write back
    std::ofstream output(binaryPath, std::ios::binary);
    output.write(reinterpret_cast<char*>(data.data()), data.size());
    output.close();*/
    
    // Re-sign
    std::string codesignCmd = "codesign --force --deep --sign - \"" + appPath + "\" 2>/dev/null";
    std::system(codesignCmd.c_str());
    
    return true;
}

inline bool createMultiInstanceRoblox(const std::string& instanceNumText) {

    std::string originalPath = "/Applications/Roblox.app";
    std::string modifiedPath = "/Applications/Roblox" + instanceNumText + ".app";
    
    if (std::filesystem::exists(modifiedPath)) {
        return true;
    }
    
    LOG_INFO("Creating Roblox" + instanceNumText + ".app...");
    
    // Copy the app
    std::filesystem::copy(
        originalPath, 
        modifiedPath,
        std::filesystem::copy_options::recursive |
        std::filesystem::copy_options::overwrite_existing
    );
    
    // Modify Info.plist
    std::string plistPath = modifiedPath + "/Contents/Info.plist";
    std::ifstream plistIn(plistPath);
    std::stringstream buffer;
    buffer << plistIn.rdbuf();
    std::string plistContent = buffer.str();
    plistIn.close();
    
    // Change bundle ID
    size_t pos = plistContent.find("<string>com.roblox.RobloxPlayer</string>");
    if (pos != std::string::npos) {
        plistContent.replace(
            pos,
            40,
            "<string>com.roblox.RobloxPlayer" + instanceNumText + "</string>"
        );
    }
    
    // Change LSMultipleInstancesProhibited
    pos = plistContent.find("<key>LSMultipleInstancesProhibited</key>");
    if (pos != std::string::npos) {
        size_t truePos = plistContent.find("<true/>", pos);
        if (truePos != std::string::npos && truePos - pos < 100) {
            plistContent.replace(truePos, 7, "<false/>");
        }
    }
    
    std::ofstream plistOut(plistPath);
    plistOut << plistContent;
    plistOut.close();
    
    // Patch binary
    if (!patchRobloxBinary(modifiedPath)) {
        LOG_ERROR("Failed to patch binary");
        return false;
    }
    
    LOG_INFO("Successfully created multi-instance Roblox");
    return true;
}

// Launch mode enum
enum class LaunchMode {
    Standard,        // Join any server
    GameJob,         // Join specific server by JobID
    PrivateServer,   // Join private server via share link
    FollowUser       // Follow a user into their game
};

// Launch parameters struct - holds all possible launch options
struct LaunchParams {
    LaunchMode mode = LaunchMode::Standard;
    uint64_t placeId = 0;
    std::string value;  // Multi-purpose: jobId, shareLink, or userId depending on mode
    
    static LaunchParams standard(uint64_t placeId) {
        return {LaunchMode::Standard, placeId, ""};
    }
    
    static LaunchParams gameJob(uint64_t placeId, const std::string& jobId) {
        return {LaunchMode::GameJob, placeId, jobId};
    }
    
    static LaunchParams privateServer(const std::string& shareLink) {
        return {LaunchMode::PrivateServer, 0, shareLink};
    }
    
    static LaunchParams followUser(const std::string& userId) {
        return {LaunchMode::FollowUser, 0, userId};
    }
};

inline bool resolvePrivateServer(const std::string& shareLink, const std::string& cookie, 
                                 const std::string& csrfToken, uint64_t& placeId, 
                                 std::string& linkCode, std::string& accessCode) {
    std::regex shareLinkRegex(R"(roblox\.com/share\?code=([^&]+)&type=Server)");
    std::smatch match;
    
    if (!std::regex_search(shareLink, match, shareLinkRegex) || match.size() <= 1) {
        LOG_ERROR("Invalid share link format");
        return false;
    }
    
    std::string shareCode = match[1].str();
    LOG_INFO("Resolving share code: " + shareCode);
    
    // Call API to resolve share link
    std::string apiUrl = "https://apis.roblox.com/sharelinks/v1/resolve-link";
    std::string jsonBody = "{\"linkId\":\"" + shareCode + "\",\"linkType\":\"Server\"}";
    
    auto apiResponse = HttpClient::post(
        apiUrl,
        {
            {"Cookie", ".ROBLOSECURITY=" + cookie},
            {"X-CSRF-TOKEN", csrfToken},
            {"Content-Type", "application/json;charset=UTF-8"},
            {"Accept", "application/json, text/plain, */*"},
            {"Origin", "https://www.roblox.com"},
            {"Referer", "https://www.roblox.com/"},
            {"User-Agent", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36"}
        },
        jsonBody
    );
    
    if (apiResponse.status_code != 200) {
        LOG_ERROR("API request failed with status: " + std::to_string(apiResponse.status_code));
        return false;
    }
    
    try {
        auto jsonResponse = HttpClient::decode(apiResponse);
        
        if (!jsonResponse.contains("privateServerInviteData") || 
            jsonResponse["privateServerInviteData"].is_null()) {
            LOG_ERROR("API response missing privateServerInviteData");
            return false;
        }
        
        auto invite = jsonResponse["privateServerInviteData"];
        
        if (!invite.contains("status") || invite["status"] != "Valid") {
            std::string status = invite.contains("status") ? invite["status"].get<std::string>() : "Unknown";
            LOG_ERROR("Private server status: " + status);
            return false;
        }
        
        placeId = invite["placeId"].get<uint64_t>();
        linkCode = invite["linkCode"].get<std::string>();
        
        LOG_INFO("Got placeId: " + std::to_string(placeId) + ", linkCode: " + linkCode);
        
    } catch (const exception &e) {
        LOG_ERROR("Failed to parse API response: " + std::string(e.what()));
        return false;
    }
    
    // Fetch game page to get access code
    std::string gameUrl = "https://www.roblox.com/games/" + std::to_string(placeId) + 
                    "/?privateServerLinkCode=" + linkCode;
    
    auto pageResponse = HttpClient::get(
        gameUrl,
        {
            {"Cookie", ".ROBLOSECURITY=" + cookie},
            {"X-CSRF-TOKEN", csrfToken},
            {"User-Agent", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36"}
        },
        {}, true, 10
    );
    
    std::regex accessCodeRegex(R"(Roblox\.GameLauncher\.joinPrivateGame\(\d+,\s*'([a-f0-9\-]+)',\s*'(\d+)')");
    std::smatch accessMatch;
    
    if (std::regex_search(pageResponse.text, accessMatch, accessCodeRegex) && 
        accessMatch.size() == 3) {
        accessCode = accessMatch[1].str();
        LOG_INFO("Retrieved access code: " + accessCode);
    } else {
        LOG_ERROR("Could not find access code");
        return false;
    }
    
    return true;
}

inline bool startRoblox(const LaunchParams& params, const std::string& cookie) {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    uint64_t rndNum = gen();
    std::stringstream ss;
    ss << std::hex << rndNum;
    std::string instanceNumText = ss.str();
    
    if (!createMultiInstanceRoblox(instanceNumText)) {
        return false;
    }
    
    LOG_INFO("Fetching x-csrf token");
    auto csrfResponse = HttpClient::post(
        "https://auth.roblox.com/v1/authentication-ticket",
        {{"Cookie", ".ROBLOSECURITY=" + cookie}});
    
    auto csrfToken = csrfResponse.headers.find("x-csrf-token");
    if (csrfToken == csrfResponse.headers.end()) {
        cerr << "failed to get CSRF token\n";
        LOG_ERROR("Failed to get CSRF token");
        return false;
    }
    
    LOG_INFO("Fetching authentication ticket");
    auto ticketResponse = HttpClient::post(
        "https://auth.roblox.com/v1/authentication-ticket",
        {
            {"Cookie", ".ROBLOSECURITY=" + cookie},
            {"Origin", "https://www.roblox.com"},
            {"Referer", "https://www.roblox.com/"},
            {"X-CSRF-TOKEN", csrfToken->second}
        });
    
    auto ticket = ticketResponse.headers.find("rbx-authentication-ticket");
    if (ticket == ticketResponse.headers.end()) {
        cerr << "failed to get authentication ticket\n";
        LOG_ERROR("Failed to get authentication ticket");
        return false;
    }
    
    // Generate browser tracker ID
    std::random_device trackerRd;
    std::mt19937 trackerGen(trackerRd());
    std::uniform_int_distribution<> dis1(100000, 175000);
    std::uniform_int_distribution<> dis2(100000, 900000);
    std::string browserTrackerId = std::to_string(dis1(trackerGen)) + std::to_string(dis2(trackerGen));
    
    auto nowMs = duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
    std::ostringstream ts;
    ts << nowMs;
    
    // Build place launcher URL based on mode
    std::string placeLauncherUrl;
    uint64_t placeId = params.placeId;
    
    switch (params.mode) {
        case LaunchMode::PrivateServer: {
            // Resolve share link to get private server details
            std::string linkCode;
            std::string accessCode;
            
            if (!resolvePrivateServer(params.value, cookie, csrfToken->second, 
                                     placeId, linkCode, accessCode)) {
                return false;
            }
            
            if (!accessCode.empty()) {
                placeLauncherUrl = 
                    "https://assetgame.roblox.com/game/PlaceLauncher.ashx?"
                    "request=RequestPrivateGame"
                    "&placeId=" + std::to_string(placeId) +
                    "&accessCode=" + accessCode +
                    "&linkCode=" + linkCode;
            }

            LOG_INFO("Launching private server for place " + std::to_string(placeId));
            break;
        }
        
        case LaunchMode::FollowUser: {
            placeLauncherUrl = 
                "https://assetgame.roblox.com/game/PlaceLauncher.ashx?"
                "request=RequestFollowUser"
                "&userId=" + params.value;
            
            LOG_INFO("Following user " + params.value);
            break;
        }
        
        case LaunchMode::GameJob: {
            placeLauncherUrl = 
                "https://assetgame.roblox.com/game/PlaceLauncher.ashx?"
                "request=RequestGameJob"
                "&browserTrackerId=" + browserTrackerId +
                "&placeId=" + std::to_string(placeId) +
                "&gameId=" + params.value +
                "&isPlayTogetherGame=false"
                "&isTeleport=true";

            LOG_INFO("Joining specific server for place " + std::to_string(placeId));
            break;
        }
        
        case LaunchMode::Standard:
        default: {
            placeLauncherUrl = 
                "https://assetgame.roblox.com/game/PlaceLauncher.ashx?"
                "request=RequestGame"
                "&browserTrackerId=" + browserTrackerId +
                "&placeId=" + std::to_string(placeId) +
                "&isPlayTogetherGame=false";
            
            LOG_INFO("Launching standard join for place " + std::to_string(placeId));
            break;
        }
    }
    
    string protocolLaunchCommand =
        "roblox-player:1+launchmode:play"
        "+gameinfo:" + ticket->second +
        "+launchtime:" + ts.str() +
        "+placelauncherurl:" + urlEncode(placeLauncherUrl) +
        "+browsertrackerid:" + browserTrackerId +
        "+robloxLocale:en_us"
        "+gameLocale:en_us"
        "+channel:"
        "+LaunchExp:InApp";
    
    std::string appPath = "/Applications/Roblox" + instanceNumText + ".app";
    std::string command = "open -a \"" + appPath + "\" \"" + protocolLaunchCommand + "\"";
    
    int result = system(command.c_str());
    if (result != 0) {
        LOG_ERROR("Failed to launch Roblox. system() returned: " + std::to_string(result));
        return false;
    }
    
    LOG_INFO("Roblox process started successfully");
    usleep(500000); // 500ms
    
    return true;
}

#endif

inline void launchRobloxSequential(const LaunchParams& params, const std::vector<std::pair<int, std::string>> &accounts) {
    if (g_killRobloxOnLaunch)
        RobloxControl::KillRobloxProcesses();

    if (g_clearCacheOnLaunch)
        RobloxControl::ClearRobloxCache();

    for (const auto &[accountId, cookie]: accounts) {
        LOG_INFO("Launching Roblox for account ID: " + std::to_string(accountId) +
            " PlaceID: " + std::to_string(params.placeId) +
            (params.value.empty() ? "" : " JobID: " + params.value));
        
#ifdef _WIN32
        HANDLE proc = startRoblox(placeId, jobId, cookie);
        if (proc) {
            WaitForInputIdle(proc, INFINITE);
            CloseHandle(proc);
            LOG_INFO("Roblox launched successfully for account ID: " +
                std::to_string(accountId));
        } else {
            LOG_ERROR("Failed to start Roblox for account ID: " +
                std::to_string(accountId));
        }
#elif __APPLE__
        bool success = startRoblox(params, cookie);
        if (success) {
            LOG_INFO("Roblox launched successfully for account ID: " +
                std::to_string(accountId));
        } else {
            LOG_ERROR("Failed to start Roblox for account ID: " +
                std::to_string(accountId));
        }
#endif
    }
}