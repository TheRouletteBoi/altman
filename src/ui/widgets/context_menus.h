#pragma once

#include <cstdint>
#include <functional>
#include <string>

struct StandardJoinMenuParams {
        uint64_t placeId = 0;
        uint64_t universeId = 0;
        std::string jobId;
        bool enableLaunchGame = true;
        bool enableLaunchInstance = true; // only applies if jobId not empty
        std::string launchGameLabel;
        std::string launchInstanceLabel;
        std::function<void()> onLaunchGame; // optional
        std::function<void()> onLaunchInstance; // optional
        std::function<void()> onFillGame; // optional
        std::function<void()> onFillInstance; // optional
};

struct PrivateServerMenuParams {
    uint64_t vipServerId{};
    //std::string accessCode;
    uint64_t placeId{};
    std::string serverName;

    std::function<void()> onCopyShareLink;
    std::function<void()> onRegenerateShareLink;
    std::function<void()> onFillJoinOption;
    std::function<void()> onJoinServer;
};

void RenderStandardJoinMenu(const StandardJoinMenuParams &params);
void RenderPrivateServerJoinMenu(const PrivateServerMenuParams& params);
