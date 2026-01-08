#pragma once
#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <regex>
#include <array>
#include <sys/wait.h>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
#include <signal.h>
#include <print>
#include <spawn.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>


extern char **environ;

#ifdef _WIN32
#include <windows.h>

namespace MultiInstance {
inline HANDLE g_mutex = nullptr;

inline void Enable() {
    if (!g_mutex)
        g_mutex = CreateMutexW(nullptr, FALSE, L"ROBLOX_singletonEvent");
}

inline void Disable() {
    if (g_mutex) {
        CloseHandle(g_mutex);
        g_mutex = nullptr;
    }
}
}
#elif __APPLE__

namespace MultiInstance {
	struct RobloxInstance {
		pid_t pid;
		std::string profileId;
		std::string profilePath;
		std::string clientName;
		time_t launchTime;
	};

	namespace {
		inline sem_t* g_semaphore = SEM_FAILED;
		inline const char* SEMAPHORE_NAME = "/RobloxPlayerUniq";
		inline std::map<pid_t, RobloxInstance> g_activeInstances;
		inline std::mutex g_instanceMutex;
	}

	void Enable();
	void Disable();
	/*void addInstance(pid_t pid, const std::string& profileId, const std::string& profilePath, const std::string& clientName);
	void removeInstance(pid_t pid);
	size_t getInstanceCount();
	bool hasInstances();
	std::vector<RobloxInstance> getAllInstances();
	bool isProfileRunning(const std::string& profileId);
	std::vector<RobloxInstance> getRunningInstances();
	bool isRunning(const std::string& profileId);*/
	std::pair<int, std::string> executeCommand(const std::string& cmd);
	std::string getAppDataDirectory();
	std::string getUserClientPath(const std::string& username, const std::string& clientName);
	std::string getBaseClientPath(const std::string& clientName);
	void saveSourceHash(const std::string& destPath);
	bool needsClientUpdate(const std::string& sourcePath, const std::string& destPath);
	bool cleanupUserEnvironment(const std::string& username);
	bool isClientInstalled(const std::string& username, const std::string& clientName);
	bool isBaseClientInstalled(const std::string& clientName);
	const std::vector<std::string>& getAvailableClients(bool forceRefresh);
	const std::vector<std::string>& getAvailableClientsForUI(bool refresh);
	std::string getClientPath(const std::string& username, const std::string& clientName);
	bool createKeychain(const std::string& profileId);
	bool unlockKeychain(const std::string& profileId);
	bool createProfileEnvironment(const std::string& profileId, std::string& profilePath);
	bool modifyBundleIdentifier(const std::string& username, const std::string& clientName, const std::string& profileId, bool isInitialSetup);
	bool needsBundleIdModification(const std::string& username, const std::string& clientName, const std::string& expectedProfileId);
	bool ensureClientKey(const std::string& username, const std::string& clientName, const std::string& key);
	bool launchSandboxedClient(const std::string& username, const std::string& clientName, const std::string& profileId, const std::string& profilePath, const std::string& protocolURL);
	void monitorRobloxProcess(pid_t pid, const std::string& profileId, const std::string& clientName);
	bool stopSandboxedRoblox(pid_t pid);

}
#endif