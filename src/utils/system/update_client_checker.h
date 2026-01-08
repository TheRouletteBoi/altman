#pragma once

#include <string>
#include <unordered_map>
#include <chrono>
#include <thread>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "network/client_manager.h"
#include "core/logging.hpp"
#include "system/multi_instance.h"
#include "threading.h"
#include "main_thread.h"
#include "update.h"

namespace ClientUpdateChecker {

struct ClientVersionInfo {
    std::string installedVersion;
    std::string latestVersion;
    std::chrono::system_clock::time_point lastChecked;
    bool updateAvailable{false};
};

class UpdateChecker {
private:
    inline static std::unordered_map<std::string, ClientVersionInfo> clientVersions;
    inline static std::atomic<bool> isRunning{false};
    inline static std::atomic<bool> shouldStop{false};
    inline static std::thread checkerThread;
    inline static std::filesystem::path configPath;

    static std::filesystem::path GetConfigPath() {
        std::string appDataDir = MultiInstance::getAppDataDirectory();
        if (appDataDir.empty()) {
            return "";
        }
        
        std::filesystem::path path = std::filesystem::path(appDataDir) / "storage" /"client_versions.json";
        return path;
    }

    static void SaveVersionInfo() {
        if (configPath.empty()) {
            configPath = GetConfigPath();
        }

        nlohmann::json j = nlohmann::json::object();
        
        for (const auto& [clientName, info] : clientVersions) {
            j[clientName] = {
                {"installedVersion", info.installedVersion},
                {"latestVersion", info.latestVersion},
                {"lastChecked", std::chrono::system_clock::to_time_t(info.lastChecked)},
                {"updateAvailable", info.updateAvailable}
            };
        }

        std::ofstream file(configPath);
        if (file.is_open()) {
            file << j.dump(2);
            file.close();
        }
    }

    static void LoadVersionInfo() {
        if (configPath.empty()) {
            configPath = GetConfigPath();
        }

        if (!std::filesystem::exists(configPath)) {
            return;
        }

        std::ifstream file(configPath);
        if (!file.is_open()) {
            return;
        }

        try {
            nlohmann::json j;
            file >> j;
            file.close();

            for (auto& [clientName, data] : j.items()) {
                ClientVersionInfo info;
                info.installedVersion = data.value("installedVersion", "");
                info.latestVersion = data.value("latestVersion", "");
                info.updateAvailable = data.value("updateAvailable", false);
                
                auto lastCheckedTime = data.value("lastChecked", 0LL);
                info.lastChecked = std::chrono::system_clock::from_time_t(lastCheckedTime);
                
                clientVersions[clientName] = info;
            }
        } catch (const std::exception& e) {
            LOG_ERROR(std::format("Failed to load client versions: {}", e.what()));
        }
    }

    static void CheckClientForUpdate(const std::string& clientName) {
        if (!MultiInstance::isBaseClientInstalled(clientName)) {
            return;
        }

        LOG_INFO(std::format("Checking for updates: {}", clientName));

        auto& info = clientVersions[clientName];
        std::string latestVersion = GetClientVersion(clientName);

        if (latestVersion.empty()) {
            LOG_WARN(std::format("Failed to fetch latest version for {}", clientName));
            return;
        }

        info.latestVersion = latestVersion;
        info.lastChecked = std::chrono::system_clock::now();

        if (info.installedVersion.empty()) {
            info.installedVersion = latestVersion;
            info.updateAvailable = false;
            LOG_INFO(std::format("{} version initialized: {}", clientName, latestVersion));
            SaveVersionInfo();
            return;
        }

        if (info.installedVersion != latestVersion) {
            info.updateAvailable = true;
            LOG_INFO(std::format("{} update available: {} -> {}", 
                clientName, info.installedVersion, latestVersion));
            
            SaveVersionInfo();

            MainThread::Post([clientName, info]() {
                NotifyAndUpdate(clientName, info);
            });
        } else {
            info.updateAvailable = false;
            LOG_INFO(std::format("{} is up to date: {}", clientName, latestVersion));
            SaveVersionInfo();
        }
    }

    static void NotifyAndUpdate(const std::string& clientName, const ClientVersionInfo& info) {
        std::string message = std::format("Updating {} from {} to {}...", 
            clientName, info.installedVersion, info.latestVersion);

        UpdateNotification::Show("Client Update", message, 5.0f);
        
        LOG_INFO(std::format("Starting auto-update for {}", clientName));

        std::string clientPath = MultiInstance::getBaseClientPath(clientName);
        if (!clientPath.empty() && std::filesystem::exists(clientPath)) {
            std::error_code ec;
            std::filesystem::remove_all(clientPath, ec);
            if (ec) {
                LOG_ERROR(std::format("Failed to remove old {}: {}", clientName, ec.message()));
                UpdateNotification::Show("Update Failed", 
                    std::format("Failed to remove old {} client", clientName), 5.0f);
                return;
            }
        }

        auto progressCallback = [clientName](float progress, const std::string& msg) {
            // LOG_INFO(std::format("{} update progress: {:.0f}% - {}", clientName, progress * 100.0f, msg));
        };

        auto completionCallback = [clientName](bool success, const std::string& message) {
            if (success) {
                LOG_INFO(std::format("{} updated successfully", clientName));

                auto& info = clientVersions[clientName];
                info.installedVersion = info.latestVersion;
                info.updateAvailable = false;
                SaveVersionInfo();
                
                MainThread::Post([clientName]() {
                    UpdateNotification::Show("Update Complete", 
                        std::format("{} has been updated successfully!", clientName), 5.0f);
                });
            } else {
                LOG_ERROR(std::format("{} update failed: {}", clientName, message));
                
                MainThread::Post([clientName, message]() {
                    UpdateNotification::Show("Update Failed", 
                        std::format("{}: {}", clientName, message), 5.0f);
                });
            }
        };

        ClientManager::InstallClientAsync(clientName, progressCallback, completionCallback);
    }

    static void CheckerLoop() {
        LOG_INFO("Client update checker started");

        while (!shouldStop) {
            std::vector<std::string> clientsToCheck = {"Vanilla", "MacSploit", "Hydrogen", "Delta"};
            
            for (const auto& clientName : clientsToCheck) {
                if (shouldStop) break;

                if (clientName != "Vanilla" && !MultiInstance::isBaseClientInstalled(clientName)) {
                    continue;
                }

                auto& info = clientVersions[clientName];
                auto now = std::chrono::system_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::hours>(
                    now - info.lastChecked).count();

                if (elapsed >= 24 || info.lastChecked == std::chrono::system_clock::time_point{}) {
                    CheckClientForUpdate(clientName);
                }

                std::this_thread::sleep_for(std::chrono::seconds(2));
            }

            // Sleep for 1 hour before next check cycle
            for (int i = 0; i < 3600 && !shouldStop; ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }

        LOG_INFO("Client update checker stopped");
    }

public:
    static void Initialize() {
        if (isRunning) {
            LOG_WARN("Client update checker already running");
            return;
        }

        LoadVersionInfo();
        
        shouldStop = false;
        isRunning = true;
        
        checkerThread = std::thread(CheckerLoop);
        checkerThread.detach();
        
        LOG_INFO("Client update checker initialized");
    }

    static void Shutdown() {
        if (!isRunning) {
            return;
        }

        shouldStop = true;
        isRunning = false;
        
        LOG_INFO("Client update checker shutdown requested");
    }

    static void CheckNow(const std::string& clientName) {
        Threading::newThread([clientName]() {
            CheckClientForUpdate(clientName);
        });
    }

    static void CheckAllNow() {
        Threading::newThread([]() {
            std::vector<std::string> clients = {"Vanilla", "MacSploit", "Hydrogen", "Delta"};
            for (const auto& clientName : clients) {
                if (clientName != "Vanilla" && !MultiInstance::isBaseClientInstalled(clientName)) {
                    continue;
                }
                CheckClientForUpdate(clientName);
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        });
    }

	static std::string GetClientVersion(const std::string& clientName) {
    	try {
    		if (clientName == "MacSploit") {
    			auto version = ClientManager::GetMacsploitVersion();
    			return version.relVersion;
    		} else if (clientName == "Hydrogen") {
    			auto version = ClientManager::GetHydrogenVersion();
    			return version.macos.exploit_version.value_or("");
    		} else if (clientName == "Delta") {
    			return ClientManager::GetDeltaVersion();
    		} else if (clientName == "Vanilla") {
    			return ClientManager::GetLatestRobloxVersion();
    		}
    	} catch (const std::exception& e) {
    		LOG_ERROR(std::format("Failed to get version for {}: {}", clientName, e.what()));
    	}
    	return "";
    }

    static ClientVersionInfo GetVersionInfo(const std::string& clientName) {
        auto it = clientVersions.find(clientName);
        if (it != clientVersions.end()) {
            return it->second;
        }
        return ClientVersionInfo{};
    }

    static void MarkClientAsInstalled(const std::string& clientName, const std::string& version) {
        auto& info = clientVersions[clientName];
        info.installedVersion = version;
        info.latestVersion = version;
        info.updateAvailable = false;
        info.lastChecked = std::chrono::system_clock::now();
        SaveVersionInfo();
        
        LOG_INFO(std::format("Marked {} as installed: {}", clientName, version));
    }
};

}