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
#include "multi_instance.h"

namespace MultiInstance {
	void Enable() {
		if (g_semaphore == SEM_FAILED) {
			g_semaphore = sem_open(SEMAPHORE_NAME, O_CREAT, 0644, 1);
			if (g_semaphore == SEM_FAILED) {
				return;
			}
		}
	}

	void Disable() {
		if (g_semaphore != SEM_FAILED) {
			sem_close(g_semaphore);
			sem_unlink(SEMAPHORE_NAME);
			g_semaphore = SEM_FAILED;
		}
	}

	std::pair<int, std::string> executeCommand(const std::string& cmd) {
		std::array<char, 128> buffer;
		std::string result;

		FILE* pipe = popen((cmd + " 2>&1").c_str(), "r");
		if (!pipe) return {-1, "popen failed"};

		while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
			result += buffer.data();
		}

		int exitCode = pclose(pipe);
		return {WEXITSTATUS(exitCode), result};
	}

	std::string getAppDataDirectory() {
		const char* home = getenv("HOME");
		if (!home) {
			std::println("Failed to get HOME directory");
			return "";
		}

		std::filesystem::path appDataDir = std::filesystem::path(home) / "Library" / "Application Support" / "Altman";

		std::error_code ec;
		std::filesystem::create_directories(appDataDir, ec);
		if (ec) {
			std::println("Failed to create app data directory: {}", ec.message());
			return "";
		}

		return appDataDir.string();
	}

	std::string getUserClientPath(const std::string& username, const std::string& clientName) {
		std::string appDataDir = getAppDataDirectory();
		if (appDataDir.empty()) return "";

		return std::format("{}/environments/{}/Applications/{}.app",
			appDataDir, username, clientName);
	}

	std::optional<std::string> getBundleIdentifier(const std::filesystem::path& appBundle) {
		std::filesystem::path plistPath = appBundle / "Contents" / "Info.plist";

		if (!std::filesystem::exists(plistPath)) {
			return std::nullopt;
		}

		try {
			std::ifstream file(plistPath);
			std::string contents((std::istreambuf_iterator<char>(file)),
								std::istreambuf_iterator<char>());

			std::regex bundleIdRegex("<key>CFBundleIdentifier</key>\\s*<string>([^<]+)</string>");
			std::smatch match;

			if (std::regex_search(contents, match, bundleIdRegex) && match.size() > 1) {
				return match[1].str();
			}
		} catch (const std::exception& e) {
			std::println("Error reading bundle identifier: {}", e.what());
		}

		return std::nullopt;
	}

	std::optional<std::filesystem::path> findMainExecutable(const std::filesystem::path& appBundle) {
		std::filesystem::path macosDir = appBundle / "Contents" / "MacOS";

		if (!std::filesystem::exists(macosDir)) {
			return std::nullopt;
		}

		const std::vector<std::string> commonNames = {"Roblox", "RobloxPlayer"};
		for (const auto& name : commonNames) {
			std::filesystem::path candidate = macosDir / name;
			if (std::filesystem::exists(candidate)) {
				return candidate;
			}
		}

		try {
			for (const auto& entry : std::filesystem::directory_iterator(macosDir)) {
				if (!entry.is_regular_file()) {
					continue;
				}

				auto perms = entry.status().permissions();
				if ((perms & std::filesystem::perms::owner_exec) != std::filesystem::perms::none) {
					return entry.path();
				}
			}
		} catch (const std::exception& e) {
			std::println("Error finding executable: {}", e.what());
		}

		return std::nullopt;
	}

	// Note: This checks the UNMODIFIED source against destination to avoid false positives
	// from codesigning changes. Use this BEFORE any bundle modifications.
	bool needsClientUpdate(const std::string& sourcePath, const std::string& destPath) {
		if (!std::filesystem::exists(destPath)) {
			std::println("Destination does not exist, needs update");
			return true;
		}

		try {
			// Check bundle identifiers first - this uniquely identifies the client type
			auto sourceBundleId = getBundleIdentifier(sourcePath);
			auto destBundleId = getBundleIdentifier(destPath);

			if (sourceBundleId && destBundleId) {
				// Extract the base identifier (before any profile-specific modifications)
				// e.g., "com.roblox.RobloxPlayer" or "com.hydrogen.client"
				std::string sourceBase = *sourceBundleId;
				std::string destBase = *destBundleId;

				// Remove any profile suffixes (e.g., ".cooker_0039")
				auto sourceDot = sourceBase.find_last_of('.');
				auto destDot = destBase.find_last_of('.');

				// Only compare if they look like modified identifiers
				if (sourceDot != std::string::npos && destDot != std::string::npos) {
					std::string sourcePrefix = sourceBase.substr(0, sourceDot);
					std::string destPrefix = destBase.substr(0, destDot);

					if (sourcePrefix != destPrefix) {
						std::println("Bundle identifiers differ (source: {}, dest: {}), needs update",
									sourcePrefix, destPrefix);
						return true;
					}
				}
			}

			// Find the main executable dynamically
			auto sourceExecOpt = findMainExecutable(sourcePath);
			if (!sourceExecOpt) {
				std::println("Source executable not found in {}", sourcePath);
				return false;
			}
			std::filesystem::path sourceExec = *sourceExecOpt;

			auto destExecOpt = findMainExecutable(destPath);
			if (!destExecOpt) {
				std::println("Destination executable does not exist, needs update");
				return true;
			}
			std::filesystem::path destExec = *destExecOpt;

			std::println("Comparing executables:");
			std::println("  Source: {}", sourceExec.string());
			std::println("  Dest:   {}", destExec.string());

			// Check if executable names are different (for clients like Delta with different names)
			if (sourceExec.filename() != destExec.filename()) {
				std::println("Executable names differ (source: {}, dest: {}), needs update",
							sourceExec.filename().string(), destExec.filename().string());
				return true;
			}

			// Check modification time - if source is newer, definitely need update
			auto sourceTime = std::filesystem::last_write_time(sourceExec);
			auto destTime = std::filesystem::last_write_time(destExec);

			if (sourceTime > destTime) {
				std::println("Source is newer, needs update");
				return true;
			}

			// If destination is same age or newer, check Info.plist version as tiebreaker
			// This handles cases where codesigning changed the binary but the base client hasn't changed
			std::filesystem::path sourcePlist = std::filesystem::path(sourcePath) / "Contents" / "Info.plist";
			std::filesystem::path destPlist = std::filesystem::path(destPath) / "Contents" / "Info.plist";

			if (std::filesystem::exists(sourcePlist) && std::filesystem::exists(destPlist)) {
				auto sourcePlistTime = std::filesystem::last_write_time(sourcePlist);
				auto destPlistTime = std::filesystem::last_write_time(destPlist);

				if (sourcePlistTime > destPlistTime) {
					std::println("Source Info.plist is newer, needs update");
					return true;
				}
			}

			std::println("Destination is up to date");
			return false;

		} catch (const std::exception& e) {
			std::println("Error checking client update: {}", e.what());
			// On error, safer to return false to avoid unnecessary copies
			return false;
		}
	}

	bool cleanupUserEnvironment(const std::string& username) {
		std::string appDataDir = getAppDataDirectory();
		if (appDataDir.empty()) return false;

		std::filesystem::path userEnvPath = std::filesystem::path(appDataDir) / "environments" / username;

		if (!std::filesystem::exists(userEnvPath)) {
			std::println("User environment doesn't exist: {}", username);
			return true;
		}

		std::println("Cleaning up environment for user: {}", username);

		std::error_code ec;
		std::filesystem::remove_all(userEnvPath, ec);

		if (ec) {
			std::println("Failed to cleanup user environment: {}", ec.message());
			return false;
		}

		std::println("User environment cleaned up successfully");
		return true;
	}

	bool isClientInstalled(const std::string& username, const std::string& clientName) {
		std::string clientPath = getUserClientPath(username, clientName);
		return !clientPath.empty() && std::filesystem::exists(clientPath);
	}

	bool isBaseClientInstalled(const std::string& clientName) {
		std::string appDataDir = getAppDataDirectory();
		if (appDataDir.empty()) return false;

		std::filesystem::path clientPath = std::filesystem::path(appDataDir) / "clients" / (clientName + ".app");
		return std::filesystem::exists(clientPath);
	}

	const std::vector<std::string>& getAvailableClients(bool forceRefresh) {
		static std::vector<std::string> cached;
		static std::filesystem::file_time_type lastScan{};
		static bool initialized = false;

		std::string appDataDir = getAppDataDirectory();
		if (appDataDir.empty()) return cached;

		std::filesystem::path clientsDir = std::filesystem::path(appDataDir) / "clients";

		std::error_code ec;
		auto currentWriteTime =
			std::filesystem::exists(clientsDir, ec)
				? std::filesystem::last_write_time(clientsDir, ec)
				: std::filesystem::file_time_type{};

		if (!initialized || forceRefresh || currentWriteTime != lastScan) {
			cached.clear();

			if (std::filesystem::exists(clientsDir)) {
				for (const auto& entry : std::filesystem::directory_iterator(clientsDir, ec)) {
					if (entry.is_directory() && entry.path().extension() == ".app") {
						cached.push_back(entry.path().stem().string());
					}
				}
			}

			std::sort(cached.begin(), cached.end());
			lastScan = currentWriteTime;
			initialized = true;
		}

		return cached;
	}

	const std::vector<std::string>& getAvailableClientsForUI(bool refresh) {
		static std::vector<std::string> cached;

		if (refresh || cached.empty()) {
			cached.clear();

			cached.push_back("Vanilla");

			auto diskClients = getAvailableClients(false);
			for (const auto& c : diskClients) {
				if (c != "Vanilla") {
					cached.push_back(c);
				}
			}
		}

		return cached;
	}

	std::string getClientPath(const std::string& username, const std::string& clientName) {
		return getUserClientPath(username, clientName);
	}

	std::string getBaseClientPath(const std::string& clientName) {
		std::string appDataDir = getAppDataDirectory();
		if (appDataDir.empty()) return "";

		std::string baseClientName = clientName;
		if (clientName.find("Roblox_") == 0) {
			baseClientName = "Vanilla";
		}

		return std::format("{}/clients/{}.app", appDataDir, baseClientName);
	}

	struct SpawnOptions {
		std::map<std::string, std::string> env;
		std::optional<std::string> stdoutPath;
		std::optional<std::string> stderrPath;
		bool waitForCompletion = true;
	};

	// Returns true if spawn succeeded, false otherwise
	// Note: When waitForCompletion is false, this only indicates the process started,
	// not whether the application successfully launched
	bool spawnProcessWithEnv(
			    const char* program,
			    const std::vector<const char*>& args,
			    const SpawnOptions& opts) {
		std::vector<std::string> envStorage;
		std::vector<char*> envp;

		for (char** env = environ; *env; ++env) {
			std::string envStr(*env);
			size_t eqPos = envStr.find('=');
			if (eqPos != std::string::npos) {
				std::string key = envStr.substr(0, eqPos);
				if (opts.env.find(key) == opts.env.end()) {
					envStorage.emplace_back(envStr);
				}
			}
		}

		// Add custom environment variables
		for (const auto& [key, value] : opts.env) {
			envStorage.emplace_back(key + "=" + value);
		}

		for (auto& s : envStorage) {
			envp.push_back(s.data());
		}
		envp.push_back(nullptr);

		std::vector<char*> argv;
		for (auto* arg : args) {
			argv.push_back(const_cast<char*>(arg));
		}
		argv.push_back(nullptr);

		posix_spawn_file_actions_t actions;
		posix_spawn_file_actions_t* actionsPtr = nullptr;

		if (opts.stdoutPath || opts.stderrPath) {
			posix_spawn_file_actions_init(&actions);
			actionsPtr = &actions;

			if (opts.stdoutPath) {
				int outFd = open(opts.stdoutPath->c_str(),
							O_CREAT | O_WRONLY | O_APPEND, 0644);
				if (outFd != -1) {
					posix_spawn_file_actions_adddup2(&actions, outFd, STDOUT_FILENO);
					posix_spawn_file_actions_addclose(&actions, outFd);
				}
			}

			if (opts.stderrPath) {
				int errFd = open(opts.stderrPath->c_str(),
							O_CREAT | O_WRONLY | O_APPEND, 0644);
				if (errFd != -1) {
					posix_spawn_file_actions_adddup2(&actions, errFd, STDERR_FILENO);
					posix_spawn_file_actions_addclose(&actions, errFd);
				}
			}
		}

		pid_t pid;
		int status = posix_spawn(&pid, program, actionsPtr, nullptr,
							argv.data(), envp.data());

		if (actionsPtr) {
			posix_spawn_file_actions_destroy(&actions);
		}

		if (status != 0) {
			std::println("posix_spawn failed: {}", strerror(status));
			return false;
		}

		if (opts.waitForCompletion) {
			int wstatus;
			waitpid(pid, &wstatus, 0);
			if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0) {
				return false;
			}
		}

		return true;
	}

	bool spawnWithEnv(const char* program,
                const std::vector<const char*>& args,
                const std::string& customHome) {
		SpawnOptions opts;
		opts.env = {{"HOME", customHome}};
		opts.waitForCompletion = true;
		return spawnProcessWithEnv(program, args, opts);
	}

	bool createKeychain(const std::string& profileId) {
		std::string appDataDir = getAppDataDirectory();
		if (appDataDir.empty()) return false;

		auto profileDir = std::filesystem::path(appDataDir) / "environments" / profileId;

		std::error_code ec;
		std::filesystem::create_directories(profileDir, ec);
		if (ec) {
			std::println("Failed to create profile directory: {}", ec.message());
			return false;
		}

		std::println("Creating keychain for profile: {}", profileId);

		return spawnWithEnv("/usr/bin/security",
			{"security", "create-keychain", "-p", "", "login.keychain", nullptr},
			profileDir.string());
	}

	bool unlockKeychain(const std::string& profileId) {
		std::string appDataDir = getAppDataDirectory();
		if (appDataDir.empty()) return false;

		auto profileDir = std::filesystem::path(appDataDir) / "environments" / profileId;

		if (!std::filesystem::exists(profileDir)) {
			std::println("Profile directory does not exist: {}", profileDir.string());
			return false;
		}

		std::println("Unlocking keychain for profile: {}", profileId);

		return spawnWithEnv("/usr/bin/security",
		{"security", "unlock-keychain", "-p", "", "login.keychain", nullptr},
		profileDir.string());
	}

	bool createProfileEnvironment(const std::string& profileId, std::string& profilePath) {
		std::string appDataDir = getAppDataDirectory();
		if (appDataDir.empty()) return false;

		std::filesystem::path environmentsDir = std::filesystem::path(appDataDir) / "environments" / profileId;

		std::error_code ec;
		std::filesystem::create_directories(environmentsDir, ec);
		if (ec) {
			std::println("Failed to create profile environment: {}", ec.message());
			return false;
		}

		std::filesystem::create_directories(environmentsDir / "Documents", ec);
		std::filesystem::create_directories(environmentsDir / "Applications", ec);
		std::filesystem::create_directories(environmentsDir / "Library", ec);
		std::filesystem::create_directories(environmentsDir / "Library" / "Preferences", ec);
		std::filesystem::create_directories(environmentsDir / "Library" / "Keychains", ec);
		std::filesystem::create_directories(environmentsDir / "Library" / "Application Support", ec);
		std::filesystem::create_directories(environmentsDir / "Library" / "Caches", ec);
		std::filesystem::create_directories(environmentsDir / "Library" / "Delta" / "Cache", ec);

		std::filesystem::create_directories(environmentsDir / "Documents" / "Delta" / "Autoexecute", ec);
		std::filesystem::create_directories(environmentsDir / "Documents" / "Delta" / "Scripts", ec);
		std::filesystem::create_directories(environmentsDir / "Documents" / "Delta" / "Workspace", ec);

		std::filesystem::create_directories(environmentsDir / "Hydrogen", ec);
		std::filesystem::create_directories(environmentsDir / "Hydrogen" / "autoexecute", ec);
		std::filesystem::create_directories(environmentsDir / "Hydrogen" / "workspace", ec);

		std::filesystem::create_directories(environmentsDir / "Documents" / "Macsploit Automatic Execution", ec);
		std::filesystem::create_directories(environmentsDir / "Documents" / "Macsploit Workspace", ec);

		if (ec) {
			std::println("Failed to create profile subdirectories: {}", ec.message());
			return false;
		}

		profilePath = environmentsDir.string();
		std::println("Profile environment created: {}", profilePath);
		return true;
	}

	bool modifyBundleIdentifier(const std::string& username,
							const std::string& clientName,
							const std::string& profileId,
							bool isInitialSetup) {
		const std::string clientPath = getUserClientPath(username, clientName);
		if (clientPath.empty() || !std::filesystem::exists(clientPath)) {
			std::println("Client not found: {}", clientName);
			return false;
		}

		std::println("Modifying bundle identifier for client: {}", clientName);
		std::println("Profile ID: {}", profileId);

		std::string plistPath = std::format("{}/Contents/Info.plist", clientPath);
		if (!std::filesystem::exists(plistPath)) {
			std::println("Info.plist not found");
			return false;
		}

		std::println("Using plist at: {}", plistPath);

		std::ifstream plistIn(plistPath);
		if (!plistIn) {
			std::println("Failed to open Info.plist for reading");
			return false;
		}

		std::string plistContent((std::istreambuf_iterator<char>(plistIn)),
								 std::istreambuf_iterator<char>());
		plistIn.close();

		const std::regex bundleIdRegex(R"(<string>com\.roblox\.RobloxPlayer\.?\w*</string>)");
		const std::string newBundleId = profileId.empty()
			? "<string>com.roblox.RobloxPlayer</string>"
			: std::format("<string>com.roblox.RobloxPlayer.{}</string>", profileId);

		plistContent = std::regex_replace(plistContent, bundleIdRegex, newBundleId);

		{
			std::ofstream plistOut(plistPath, std::ios::out | std::ios::trunc);
			if (!plistOut) {
				std::println("Failed to open Info.plist for writing");
				return false;
			}
			plistOut << plistContent;
			plistOut.flush();
			plistOut.close();
		}

		std::string codesignCmd = std::format(
			"codesign --force --deep -s - \"{}\" 2>&1",
			clientPath
		);

		auto [result, output] = executeCommand(codesignCmd);

		if (result != 0) {
			std::println("Codesign failed with code {}: {}", result, output);
			return false;
		}

		std::println("Codesigning completed successfully");
		return true;
	}

	bool needsBundleIdModification(const std::string& username,
								  const std::string& clientName,
								  const std::string& expectedProfileId) {
		const std::string clientPath = getUserClientPath(username, clientName);
		if (clientPath.empty() || !std::filesystem::exists(clientPath)) {
			return false;
		}

		// Try both possible plist locations
		std::string plistPath = std::format("{}/Contents/Info.plist", clientPath);
		if (!std::filesystem::exists(plistPath)) {

			// For Delta
			plistPath = std::format("{}/Info.plist", clientPath);
			if (!std::filesystem::exists(plistPath)) {
				return true;
			}
		}

		std::ifstream plistIn(plistPath);
		if (!plistIn) {
			return true;
		}

		std::string plistContent((std::istreambuf_iterator<char>(plistIn)),
								 std::istreambuf_iterator<char>());
		plistIn.close();

		const std::string expectedBundleId = std::format(
			"<string>com.roblox.RobloxPlayer.{}</string>", expectedProfileId);

		return plistContent.find(expectedBundleId) == std::string::npos;
	}

	bool ensureClientKey(const std::string& username, const std::string& clientName, const std::string& key) {
		if (key.empty()) {
			std::println("Key required for {} but not provided", clientName);
			return false;
		}

		std::string appDataDir = getAppDataDirectory();
		if (appDataDir.empty()) return false;

		std::string keyPath;
		if (clientName == "Hydrogen") {
			keyPath = std::format("{}/environments/{}/Library/Application Support/Hydrogen/Key.txt",
				appDataDir, username);
		} else if (clientName == "Delta") {
			keyPath = std::format("{}/environments/{}/Library/Delta/Cache/license",
				appDataDir, username);
		} else {
			// Client doesn't require a key
			return true;
		}

		std::filesystem::path keyDir = std::filesystem::path(keyPath).parent_path();
		std::error_code ec;
		std::filesystem::create_directories(keyDir, ec);
		if (ec) {
			std::println("Failed to create key directory for {}: {}", clientName, ec.message());
			return false;
		}

		// Check if key needs updating
		if (std::filesystem::exists(keyPath)) {
			std::ifstream existingKeyFile(keyPath);
			if (existingKeyFile) {
				std::string existingKey((std::istreambuf_iterator<char>(existingKeyFile)),
									   std::istreambuf_iterator<char>());
				existingKeyFile.close();

				if (existingKey == key) {
					std::println("Key already up to date for {}", clientName);
					return true;
				}
			}
		}

		std::ofstream keyFile(keyPath);
		if (!keyFile) {
			std::println("Failed to write key file for {}", clientName);
			return false;
		}

		keyFile << key;
		keyFile.close();

		std::println("Key written successfully for {}", clientName);
		return true;
	}

	bool isMobileClient2(std::string_view clientPath) {
		std::filesystem::path libgloopMarker =
			std::filesystem::path(clientPath) / "Contents" / "Frameworks" / "libgloop.dylib";

		return std::filesystem::exists(libgloopMarker);
	}

	bool launchSandboxedClient(const std::string& username,
							const std::string& clientName,
							const std::string& profileId,
							const std::string& profilePath,
							const std::string& protocolURL) {
		std::string clientPath = getUserClientPath(username, clientName);
		if (clientPath.empty() || !std::filesystem::exists(clientPath)) {
			std::println("Client not installed: {}", clientName);
			return false;
		}

		std::println("playerPath: {}", clientPath);
		std::println("protocolURL: {}", protocolURL);
		std::println("Launching sandboxed client: {}", clientName);
		std::println("Profile: {}", profileId);
		std::println("Profile path: {}", profilePath);

		bool mobileClient = isMobileClient2(clientPath);
		std::println("mobileClient: {}", mobileClient);

		std::string logDir = std::format("{}/Logs", profilePath);
		std::error_code ec;
		std::filesystem::create_directories(logDir, ec);

		std::vector<const char*> argv = {"open", "-a", clientPath.c_str()};
		if (!protocolURL.empty()) {
			argv.push_back(protocolURL.c_str());
		}
		argv.push_back(nullptr);

		SpawnOptions opts;
		opts.env = {
		    {"HOME", profilePath},
			//{"LANG", "en_US.UTF-8"},
			//{"TMPDIR", profilePath + "/tmp"}
		};

		if (mobileClient) {
			opts.env["CFFIXED_USER_HOME"] = profilePath;
			opts.env["XDG_DATA_HOME"]     = profilePath + "/Documents";
		}

		for (const auto& [key, value] : opts.env) {
			std::println("{} = {}", key, value);
		}

		opts.stdoutPath = std::format("{}/roblox_stdout.log", logDir);
		opts.stderrPath = std::format("{}/roblox_stderr.log", logDir);
		opts.waitForCompletion = false;

		bool result = spawnProcessWithEnv("/usr/bin/open", argv, opts);

		if (!result) {
			std::println("Failed to launch client");
			return false;
		}

		std::println("Client launched successfully");
		return true;
	}

	bool stopSandboxedRoblox(pid_t pid) {
		if (pid <= 0) {
			std::println("Invalid PID");
			return false;
		}

		std::println("Stopping client instance with PID: {}", pid);

		if (kill(pid, SIGTERM) == 0) {
			std::println("Sent SIGTERM to process {}", pid);

			for (int i = 0; i < 50; i++) {
				if (kill(pid, 0) != 0) {
					std::println("Process terminated gracefully");
					return true;
				}
				usleep(100000);
			}

			std::println("Process didn't terminate gracefully, sending SIGKILL");
			if (kill(pid, SIGKILL) == 0) {
				std::println("Sent SIGKILL to process {}", pid);
				return true;
			}
		}

		std::println("Failed to stop process: {}", strerror(errno));
		return false;
	}

}
#endif