#pragma once

#include "app_state.h"
#include "network/http.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>
#include <vector>

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


// Helper to convert a string pattern ("? ? ? 34") to a byte array and mask
inline void parsePattern(const std::string& patternStr, std::vector<uint8_t>& patternBytes, std::vector<uint8_t>& mask) {
    std::stringstream ss(patternStr);
    std::string byteStr;

    while (ss >> byteStr) {
        if (byteStr == "?") {
            patternBytes.push_back(0x00);
            mask.push_back(0x00);
        } else {
            // Convert hex string to uint8_t
            patternBytes.push_back(static_cast<uint8_t>(std::stoul(byteStr, nullptr, 16)));
            mask.push_back(0xFF);
        }
    }
}

// Function to compare data against pattern using a mask
inline bool comparePattern(const uint8_t* data, const std::vector<uint8_t>& pattern, const std::vector<uint8_t>& mask) {
    for (size_t i = 0; i < pattern.size(); ++i) {
        if ((data[i] & mask[i]) != pattern[i]) {
            return false;
        }
    }
    return true;
}

struct PatchTarget {
    std::string patternStr;
    std::vector<uint8_t> patchBytes;
    uint32_t offset{0};
};

inline bool patchRobloxBinary(const std::string& appPath) {

    std::string binaryPath = appPath + "/Contents/MacOS/RobloxPlayer";

    std::vector<PatchTarget> targets = {
        {
            // RobloxTerminationRoutine -> CBZ check
            "F3 03 00 AA F3 03 00 F9 ? ? 02 ? ? ? ? 91 ? ? ? ?", 
            {0x07, 0x00, 0x00, 0x14},
            0x18
        },
        {
            // RobloxTerminationRoutine -> _objc_msgSend$terminate_
            "00 01 40 F9 02 00 80 D2 FD 7B 42 A9 F4 4F 41 A9 FF C3 00 91", 
            {0x1F, 0x20, 0x03, 0xD5},
            0x14
        },
        {
            // signalShutdownSemaphore -> sem_post
            "? ? ? 91 ? ? ? ? ? ? 02 ? 1F 61 30 39 ? ? ? F9 FD 7B 42 A9 F4 4F 41 A9", 
            {0x1F, 0x20, 0x03, 0xD5},
            0x24
        }
    };
    
    std::ifstream binaryFile(binaryPath, std::ios::binary | std::ios::ate);
    if (!binaryFile.is_open()) {
        LOG_ERROR("Failed to open RobloxPlayer binary");
        return false;
    }
    
    std::streamsize size = binaryFile.tellg();
    std::vector<uint8_t> data(size);
    binaryFile.seekg(0, std::ios::beg);
    binaryFile.read(reinterpret_cast<char*>(data.data()), size);
    binaryFile.close();
    
    int totalPatchesApplied = 0;
    
    for (size_t targetIndex = 0; targetIndex < targets.size(); targetIndex++) {
        PatchTarget& target = targets[targetIndex];
        
        std::vector<uint8_t> patternBytes;
        std::vector<uint8_t> mask;
        parsePattern(target.patternStr, patternBytes, mask);
        
        size_t patchAddress = std::string::npos; 

        if (data.size() >= patternBytes.size()) {
            for (size_t i = 0; i <= data.size() - patternBytes.size(); ++i) {
                if (comparePattern(&data[i], patternBytes, mask)) {
                    patchAddress = i;
                    break;
                }
            }
        }

        patchAddress += target.offset;
        
        if (patchAddress == std::string::npos) {
            LOG_INFO(std::format("Target {}: Pattern not found. Skipping.", targetIndex + 1));
            continue;
        }

        if (std::memcmp(&data[patchAddress], target.patchBytes.data(), target.patchBytes.size()) == 0) {
            LOG_INFO(std::format("Target {}: Already patched. Skipping.", targetIndex + 1));
            continue;
        }

        std::memcpy(&data[patchAddress], target.patchBytes.data(), target.patchBytes.size());
        totalPatchesApplied++;
        LOG_INFO(std::format("Target {}: Successfully patched instruction at 0x{:x}", 
                              targetIndex + 1, patchAddress));
    }

    if (totalPatchesApplied == 0) {
        LOG_INFO("Patches already applied or no patterns found.");
        return true; 
    }

    std::ofstream output(binaryPath, std::ios::binary);
    if (!output.is_open()) {
        LOG_ERROR("Failed to open binary for writing.");
        return false;
    }

    output.write(reinterpret_cast<const char*>(data.data()), data.size());
    output.close();
    
    LOG_INFO(std::format("Binary successfully processed. Total patches applied: {}", totalPatchesApplied));
    return true;
}

inline bool createMultiInstanceRoblox(AccountData& acc) {

    std::string originalPath = "/Applications/Roblox.app";

	acc.instanceName = "Roblox_" + acc.username;
	const std::string modifiedPath = "/Applications/" + acc.instanceName + ".app";

	if (std::filesystem::exists(modifiedPath)) {
		acc.usesCustomInstance = true;
		Data::SaveAccounts();
		return true;
	}
    
    LOG_INFO("Creating " + acc.instanceName + ".app...");
    
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
            "<string>com.roblox.RobloxPlayer" + acc.instanceName + "</string>"
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
    /*if (!patchRobloxBinary(modifiedPath)) {
        LOG_ERROR("Failed to patch binary");
        return false;
    }*/

	std::string codesignCmd = "codesign --force --deep --sign - \"" + modifiedPath + "\" 2>/dev/null";
	int result = std::system(codesignCmd.c_str());
	if (result != 0) {
		LOG_ERROR("Codesign failed. Check system logs for details.");
		return false;
	}

	acc.usesCustomInstance = true;
	Data::SaveAccounts();

    LOG_INFO("Successfully created multi-instance Roblox");
    return true;
}

enum class LaunchMode {
    Job,        // Join any server
    GameJob,         // Join specific server by JobID
    PrivateServer,   // Join private server via share link
	PrivateServerDirect,
    FollowUser       // Follow a user into their game
};

struct LaunchParams {
    LaunchMode mode = LaunchMode::Job;
    uint64_t placeId = 0;
    std::string value;  // Multi-purpose: jobId, shareLink, or userId depending on mode
    
    static LaunchParams standard(uint64_t placeId) {
        return {LaunchMode::Job, placeId, ""};
    }
    
    static LaunchParams gameJob(uint64_t placeId, const std::string& jobId) {
        return {LaunchMode::GameJob, placeId, jobId};
    }
    
    static LaunchParams privateServer(const std::string& shareLink) {
        return {LaunchMode::PrivateServer, 0, shareLink};
    }

	static LaunchParams privateServerDirect(uint64_t placeId, const std::string& accessCode) {
    	return {LaunchMode::PrivateServerDirect, placeId, accessCode};
    }
    
    static LaunchParams followUser(const std::string& userId) {
        return {LaunchMode::FollowUser, 0, userId};
    }
};

inline bool resolvePrivateServer(const std::string& link, const std::string& cookie,
                                 const std::string& csrfToken, uint64_t& placeId,
                                 std::string& linkCode, std::string& accessCode) {
	std::smatch match;

	std::regex shareLinkRegex(R"(roblox\.com/share\?code=([^&]+)&type=Server)");
	std::regex directLinkRegex(R"(roblox\.com/games/(\d+)[^?]*\?privateServerLinkCode=([0-9]+))");

	bool isShareLink = std::regex_search(link, match, shareLinkRegex);
	bool isDirectLink = !isShareLink && std::regex_search(link, match, directLinkRegex);

	if (!isShareLink && !isDirectLink) {
		LOG_ERROR("Invalid private server link.");
		return false;
	}

	if (isShareLink)
	{
		std::string shareCode = match[1];

		LOG_INFO("Resolving share link code: " + shareCode);

		std::string apiUrl = "https://apis.roblox.com/sharelinks/v1/resolve-link";
		const std::string jsonBody = std::format(R"({{"linkId":"{}","linkType":"Server"}})", shareCode);

		auto apiResponse = HttpClient::post(
			apiUrl,
			{
				{"Cookie", ".ROBLOSECURITY=" + cookie},
				{"X-CSRF-TOKEN", csrfToken},
				{"Content-Type", "application/json;charset=UTF-8"},
				{"Accept", "application/json, text/plain, */*"},
				{"Origin", "https://www.roblox.com"},
				{"Referer", "https://www.roblox.com/"},
			},
			jsonBody
		);

		if (apiResponse.status_code != 200) {
			LOG_ERROR("Share resolve failed: HTTP " + std::to_string(apiResponse.status_code));
			return false;
		}

		try {
			auto jsonResponse = HttpClient::decode(apiResponse);

			if (jsonResponse.contains("status") && jsonResponse["status"].is_string())
			{
				const auto& status = jsonResponse["status"].get<std::string>();

				if (status == "Expired")
				{
					LOG_ERROR("This private server link is no longer valid.");
					return false;
				}
			}

			if (!jsonResponse.contains("privateServerInviteData")) {
				LOG_ERROR("Missing invite data.");
				return false;
			}

			auto invite = jsonResponse["privateServerInviteData"];

			placeId = invite["placeId"].get<uint64_t>();
			linkCode = invite["linkCode"].get<std::string>();

			LOG_INFO("Resolved placeId=" + std::to_string(placeId) +
					 " linkCode=" + linkCode);

		} catch (std::exception& e) {
			LOG_ERROR("JSON parse error: " + std::string(e.what()));
			return false;
		}
	}

	if (isDirectLink)
	{
		placeId = std::stoull(match[1].str());
		linkCode = match[2].str();

		LOG_INFO("Parsed direct-link placeId=" + std::to_string(placeId) +
				 " linkCode=" + linkCode);
	}

	std::string gameUrl = std::format("https://www.roblox.com/games/{}/?privateServerLinkCode={}", placeId, linkCode);

	auto pageResponse = HttpClient::get(
		gameUrl,
		{
			{"Cookie", ".ROBLOSECURITY=" + cookie},
			{"X-CSRF-TOKEN", csrfToken},
			{"User-Agent", "Mozilla/5.0"}
		},
		{}, true, 10
	);

	std::regex accessCodeRegex(R"(Roblox\.GameLauncher\.joinPrivateGame\(\d+,\s*'([a-f0-9\-]+)',\s*'(\d+)')");

	std::smatch accessMatch;
	if (std::regex_search(pageResponse.text, accessMatch, accessCodeRegex) && accessMatch.size() == 3)
	{
		accessCode = accessMatch[1].str();
		LOG_INFO("Retrieved access code: " + accessCode);
	}
	else
	{
		LOG_ERROR("This private server link is no longer valid.");
		return false;
	}

	return true;
}

inline bool startRoblox(const LaunchParams& params, AccountData& acc) {
    LOG_INFO("Fetching x-csrf token");
    auto csrfResponse = HttpClient::post(
        "https://auth.roblox.com/v1/authentication-ticket",
        {{"Cookie", ".ROBLOSECURITY=" + acc.cookie}});
    
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
            {"Cookie", ".ROBLOSECURITY=" + acc.cookie},
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
            std::string linkCode;
            std::string accessCode;
            
            if (!resolvePrivateServer(params.value, acc.cookie, csrfToken->second,
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

	    case LaunchMode::PrivateServerDirect: {
        		placeLauncherUrl =
					"https://assetgame.roblox.com/game/PlaceLauncher.ashx?"
					"request=RequestPrivateGame"
					"&placeId=" + std::to_string(placeId) +
					"&accessCode=" + params.value;

        		LOG_INFO("Launching private server for place " + std::to_string(placeId));
        		break;
	    }
        
        case LaunchMode::FollowUser: {
            placeLauncherUrl = 
                "https://assetgame.roblox.com/game/PlaceLauncher.ashx?"
                "request=RequestFollowUser"
                "&userId=" + params.value;

        	/*
        	`https://www.roblox.com/Game/PlaceLauncher.ashx?` +
		  `request=RequestFollowUser` +
		  `&browserTrackerId=${browserTrackerId}` +
		  `&userId=${friendId}` +
		  `&isPlayTogetherGame=false` +
		  `&joinAttemptId=${joinAttemptId}` +
		  `&joinAttemptOrigin=followUser`
        	*/
            
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

        	/*
        	`https://www.roblox.com/Game/PlaceLauncher.ashx?` +
		  `request=RequestGameJob` +
		  `&browserTrackerId=${browserTrackerId}` +
		  `&placeId=${placeId}` +
		  `&gameId=${jobId}` +
		  `&isPlayTogetherGame=false` +
		  `&joinAttemptId=${joinAttemptId}` +
		  `&joinAttemptOrigin=publicServerListJoin`
		  */

            LOG_INFO("Joining specific server for place " + std::to_string(placeId));
            break;
        }
        
        case LaunchMode::Job:
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


	if (acc.usesCustomInstance) {
		const std::string appPath = std::format("/Applications/{}.app", acc.instanceName);

		if (!std::filesystem::exists(appPath)) {
			LOG_WARN("Instance missing, falling back to default Roblox");
			acc.usesCustomInstance = false;
			Data::SaveAccounts();
		}
	}

	std::string command;
	if (acc.usesCustomInstance && !acc.instanceName.empty()) {
		const std::string appPath = std::format("/Applications/{}.app", acc.instanceName);

		command = std::format("open -a \"{}\" \"{}\"", appPath, protocolLaunchCommand);
		std::println("opening custom roblox");
	}
	else {

		// TODO(Roulette): Implement sandbox applications
		const std::string defaultAppPath = "/Applications/Roblox.app";

		command = std::format("open -a \"{}\" \"{}\"", defaultAppPath, protocolLaunchCommand);
		std::println("opening normal roblox");
	}

    int result = system(command.c_str());
    if (result != 0) {
    	LOG_ERROR(std::format("Failed to launch Roblox for {} (exit code {})", acc.username, result));
        return false;
    }
    
    LOG_INFO("Roblox process started successfully");
    usleep(500000); // 500ms
    
    return true;
}

#endif

#include "utils/core/account_utils.h"

inline std::vector<AccountData*> getUsableSelectedAccounts2() {
	std::vector<AccountData*> result;
	result.reserve(g_selectedAccountIds.size());

	for (int id : g_selectedAccountIds) {
		auto idxIt = g_accountIndexById.find(id);
		if (idxIt == g_accountIndexById.end())
			continue;

		AccountData& acc = g_accounts[idxIt->second];

		if (AccountFilters::IsAccountUsable(acc)) {
			result.push_back(&acc);
		}
	}

	return result;
}

inline void launchRobloxSequential(const LaunchParams& params, const std::vector<std::pair<int, std::string>> &accounts) {

	if (g_killRobloxOnLaunch)
		RobloxControl::KillRobloxProcesses();

	if (g_clearCacheOnLaunch)
		RobloxControl::ClearRobloxCache();

	for (AccountData* acc : getUsableSelectedAccounts2()) {

		if (!AccountFilters::IsAccountUsable(*acc))
			continue;

		if (g_bCreateSeparateRobloxInstance && !acc->usesCustomInstance) {
			if (!createMultiInstanceRoblox(*acc)) {
				LOG_ERROR(std::format("Failed to create instance for account ID: {}",acc->id));
				continue;
			}
		}

		const bool success = startRoblox(params, *acc);

		if (success) {
			LOG_INFO(std::format("Roblox launched successfully for account ID: {}", acc->id));
		} else {
			LOG_ERROR(std::format("Failed to start Roblox for account ID: {}", acc->id));
		}
	}
}