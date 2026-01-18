#define STB_IMAGE_IMPLEMENTATION
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <TargetConditionals.h>

#include "imgui.h"
#include "imgui_impl_metal.h"
#include "imgui_impl_osx.h"
#include "utils/stb_image.h"

#include "ui/ui.h"
#include "components/data.h"
#include "network/roblox/common.h"
#include "network/roblox/auth.h"
#include "network/roblox/games.h"
#include "network/roblox/session.h"
#include "network/roblox/social.h"
#include "ui/widgets/notifications.h"
#include "ui/widgets/modal_popup.h"
#include "utils/thread_task.h"
#include "system/auto_updater.h"
#include "system/client_update_checker.h"
#include "console/console.h"
#include "image.h"

#include "assets/fonts/embedded_rubik.h"
#include "assets/fonts/embedded_fa_solid.h"

#include <cstdio>
#include <thread>
#include <chrono>
#include <algorithm>
#include <string>
#include <format>
#include <print>
#include <expected>
#include <ranges>
#include <mutex>
#include <shared_mutex>
#include <memory>

namespace {
    id<MTLDevice> g_metalDevice = nil;
    id<MTLCommandQueue> g_commandQueue = nil;
    ImFont* g_rubikFont = nullptr;
    ImFont* g_iconFont = nullptr;

    constexpr ImWchar ICON_MIN_FA = 0xf000;
    constexpr ImWchar ICON_MAX_16_FA = 0xf3ff;
    constexpr float BASE_FONT_SIZE = 16.0f;

    std::shared_mutex g_accountsMutex;
}

void ReloadFonts(float dpiScale) {
    IM_ASSERT([NSThread isMainThread]);

    ImGuiIO& io = ImGui::GetIO();

    ImGui_ImplMetal_DestroyDeviceObjects();

    io.Fonts->Clear();

    const float scaledFontSize = BASE_FONT_SIZE * dpiScale;

    ImFontConfig rubikCfg{};
    rubikCfg.FontDataOwnedByAtlas = false;
    g_rubikFont = io.Fonts->AddFontFromMemoryTTF(
        const_cast<void*>(static_cast<const void*>(EmbeddedFonts::rubik_regular_ttf)),
        sizeof(EmbeddedFonts::rubik_regular_ttf),
        scaledFontSize,
        &rubikCfg
    );

    if (!g_rubikFont) {
        LOG_ERROR("Failed to load rubik-regular.ttf font.");
        g_rubikFont = io.Fonts->AddFontDefault();
    }

    ImFontConfig iconCfg{};
    iconCfg.MergeMode = true;
    iconCfg.PixelSnapH = true;
    iconCfg.FontDataOwnedByAtlas = false;
    iconCfg.GlyphMinAdvanceX = scaledFontSize;

    static constexpr ImWchar fa_solid_ranges[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};
    g_iconFont = io.Fonts->AddFontFromMemoryTTF(
        const_cast<void*>(static_cast<const void*>(EmbeddedFonts::fa_solid_ttf)),
        sizeof(EmbeddedFonts::fa_solid_ttf),
        scaledFontSize,
        &iconCfg,
        fa_solid_ranges
    );

    if (!g_iconFont && g_rubikFont) {
        LOG_ERROR("Failed to load fa-solid.ttf font for icons.");
    }

    io.FontDefault = g_rubikFont;

    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(dpiScale);

    ImGui_ImplMetal_CreateDeviceObjects(g_metalDevice);
}

[[nodiscard]]
auto LoadTextureFromMemory(const void* data, size_t data_size) -> std::expected<TextureLoadResult, std::string> {
    int image_width = 0;
    int image_height = 0;
    unsigned char* image_data = stbi_load_from_memory(
        static_cast<const unsigned char*>(data),
        static_cast<int>(data_size),
        &image_width,
        &image_height,
        nullptr,
        4
    );

    if (!image_data) {
        return std::unexpected("Failed to decode image data");
    }

    auto imageDataCleanup = std::unique_ptr<unsigned char, decltype(&stbi_image_free)>(
        image_data, stbi_image_free
    );

    MTLTextureDescriptor* textureDescriptor = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
        width:static_cast<NSUInteger>(image_width)
        height:static_cast<NSUInteger>(image_height)
        mipmapped:NO];
    textureDescriptor.usage = MTLTextureUsageShaderRead;
    textureDescriptor.storageMode = MTLStorageModeShared;

    id<MTLTexture> texture = [g_metalDevice newTextureWithDescriptor:textureDescriptor];
    if (!texture) {
        return std::unexpected("Failed to create Metal texture");
    }

    [texture replaceRegion:MTLRegionMake2D(0, 0,
                                           static_cast<NSUInteger>(image_width),
                                           static_cast<NSUInteger>(image_height))
                mipmapLevel:0
                  withBytes:image_data
                bytesPerRow:4 * static_cast<NSUInteger>(image_width)];

    TextureLoadResult result;
    result.texture.reset(texture);
    result.width = image_width;
    result.height = image_height;

    return result;
}

[[nodiscard]]
auto LoadTextureFromFile(const char* file_name) -> std::expected<TextureLoadResult, std::string> {
    FILE* f = std::fopen(file_name, "rb");
    if (!f) {
        return std::unexpected(std::format("Failed to open file: {}", file_name));
    }

    auto fileCleanup = std::unique_ptr<FILE, decltype(&std::fclose)>(f, std::fclose);

    std::fseek(f, 0, SEEK_END);
    const long file_pos = std::ftell(f);
    if (file_pos < 0) {
        return std::unexpected("Failed to determine file size");
    }

    const size_t file_size = static_cast<size_t>(file_pos);
    std::fseek(f, 0, SEEK_SET);

    std::vector<char> file_data(file_size);
    const size_t read_size = std::fread(file_data.data(), 1, file_size, f);

    if (read_size != file_size) {
        return std::unexpected(std::format("Failed to read file: expected {} bytes, got {}",
                                           file_size, read_size));
    }

    return LoadTextureFromMemory(file_data.data(), file_size);
}

namespace AccountProcessor {
    // Reuse AccountData as the snapshot - it's already a value type
    using AccountSnapshot = AccountData;

    // Only the fields that get updated during processing
    struct ProcessResult {
        int id;
        std::string userId;
        std::string username;
        std::string displayName;
        std::string status;
        std::string lastLocation;
        uint64_t placeId = 0;
        std::string jobId;
        std::string voiceStatus;
        time_t banExpiry = 0;
        time_t voiceBanExpiry = 0;
        bool shouldDeselect = false;
        bool isInvalid = false;
    };

    [[nodiscard]]
    auto takeAccountSnapshots() -> std::vector<AccountSnapshot> {
        std::shared_lock lock(g_accountsMutex);
        return {g_accounts.begin(), g_accounts.end()};
    }

    [[nodiscard]]
    auto processAccount(const AccountSnapshot& account) -> ProcessResult {
        ProcessResult result{
            .id = account.id,
            .userId = account.userId,
            .username = account.username,
            .displayName = account.displayName,
            .status = "Unknown"
        };

        if (account.cookie.empty()) {
            return result;
        }

        auto userJson = Roblox::getAuthenticatedUser(account.cookie);
        if (userJson.empty()) {
            result.status = "Network Error";
            result.shouldDeselect = true;
            result.isInvalid = true;
            return result;
        }

        const uint64_t userId = userJson.value("id", 0ULL);
        result.userId = std::to_string(userId);
        result.username = userJson.value("name", std::string{});
        result.displayName = userJson.value("displayName", std::string{});

        if (userId == 0) {
            result.status = "Error";
            return result;
        }

        auto banInfo = Roblox::checkBanStatus(account.cookie);
        switch (banInfo.status) {
                case Roblox::BanCheckResult::InvalidCookie:
                    result.isInvalid = true;
                    result.status = "Invalid Cookie";
                    return result;

                case Roblox::BanCheckResult::Banned:
                    result.status = "Banned";
                    result.banExpiry = banInfo.endDate;
                    result.voiceStatus = "N/A";
                    result.shouldDeselect = true;
                    return result;

                case Roblox::BanCheckResult::Warned:
                    result.status = "Warned";
                    result.voiceStatus = "N/A";
                    result.shouldDeselect = true;
                    return result;

                case Roblox::BanCheckResult::Terminated:
                    result.status = "Terminated";
                    result.voiceStatus = "N/A";
                    result.shouldDeselect = true;
                    return result;

                case Roblox::BanCheckResult::Unbanned: {

                    /*auto userJson = Roblox::getAuthenticatedUser(snapshot.cookie);
                    if (!userJson.empty()) {
                        result.userId = std::format("{}", userJson.value("id", 0ULL));
                        result.username = userJson.value("name", "");
                        result.displayName = userJson.value("displayName", "");
                        needsUserInfoUpdate = false;

                        auto uidResult = parseUserId(result.userId);
                        if (uidResult) {
                            uint64_t uid = *uidResult;

                            auto presences = Roblox::getPresences({uid}, snapshot.cookie);
                            if (!presences.empty()) {
                                if (auto it = presences.find(uid); it != presences.end()) {
                                    result.status = it->second.presence;
                                    result.lastLocation = it->second.lastLocation;
                                    result.placeId = it->second.placeId;
                                    result.jobId = it->second.jobId;
                                } else {
                                    result.status = "Offline";
                                }
                            } else {
                                result.status = Roblox::getPresence(snapshot.cookie, uid);
                            }

                            auto vs = Roblox::getVoiceChatStatus(snapshot.cookie);
                            result.voiceStatus = vs.status;
                            result.voiceBanExpiry = vs.bannedUntil;
                        } else {
                            result.status = "Error";
                        }
                    }*/
                    break;
                }

                case Roblox::BanCheckResult::NetworkError:
                    result.status = "Network Error";
                    return result;
            }

        auto voiceStatus = Roblox::getVoiceChatStatus(account.cookie);
        result.voiceStatus = voiceStatus.status;
        result.voiceBanExpiry = voiceStatus.bannedUntil;

        auto presences = Roblox::getPresences({userId}, account.cookie);
        if (auto it = presences.find(userId); it != presences.end()) {
            const auto& presence = it->second;
            result.status = presence.presence;
            result.lastLocation = presence.lastLocation;
            result.placeId = presence.placeId;
            result.jobId = presence.jobId;
        } else {
            result.status = "Offline";
        }

        return result;
    }

    void applyResults(const std::vector<ProcessResult>& results) {
        std::unique_lock lock(g_accountsMutex);

        for (const auto& result : results) {
            auto it = std::ranges::find_if(g_accounts, [&result](const AccountData& a) {
                return a.id == result.id;
            });

            if (it == g_accounts.end()) {
                continue;
            }

            it->userId = result.userId;
            it->username = result.username;
            it->displayName = result.displayName;
            it->status = result.status;
            it->lastLocation = result.lastLocation;
            it->placeId = result.placeId;
            it->jobId = result.jobId;
            it->voiceStatus = result.voiceStatus;
            it->banExpiry = result.banExpiry;
            it->voiceBanExpiry = result.voiceBanExpiry;

            if (result.shouldDeselect) {
                g_selectedAccountIds.erase(result.id);
            }

            invalidateAccountIndex();
        }
    }

    void showInvalidCookieModal(std::vector<int> invalidIds, std::string invalidNames) {
        if (invalidIds.empty()) {
            return;
        }

        ThreadTask::RunOnMain([ids = std::move(invalidIds),
                               names = std::move(invalidNames)]() {
            auto message = std::format("Invalid cookies for: {}. Remove them?", names);

            ModalPopup::AddYesNo(message.c_str(), [ids]() {
                std::unique_lock lock(g_accountsMutex);

                std::erase_if(g_accounts, [&ids](const AccountData& a) {
                    return std::ranges::find(ids, a.id) != ids.end();
                });

                invalidateAccountIndex();

                for (int id : ids) {
                    g_selectedAccountIds.erase(id);
                }

                Data::SaveAccounts();
            });
        });
    }
}

void refreshAccounts() {
    auto snapshots = AccountProcessor::takeAccountSnapshots();

    std::vector<AccountProcessor::ProcessResult> results;
    results.reserve(snapshots.size());

    std::vector<int> invalidIds;
    std::string invalidNames;

    for (const auto& snapshot : snapshots) {
        auto result = AccountProcessor::processAccount(snapshot);

        if (result.isInvalid) {
            invalidIds.push_back(result.id);
            if (!invalidNames.empty()) {
                invalidNames.append(", ");
            }
            invalidNames.append(
                snapshot.displayName.empty() ? snapshot.username : snapshot.displayName
            );
        }

        results.push_back(std::move(result));
    }

    ThreadTask::RunOnMain([results = std::move(results),
                           invalidIds = std::move(invalidIds),
                           invalidNames = std::move(invalidNames)]() mutable {
        AccountProcessor::applyResults(results);
        Data::SaveAccounts();
        LOG_INFO("Loaded accounts and refreshed statuses");

        AccountProcessor::showInvalidCookieModal(std::move(invalidIds), std::move(invalidNames));
    });
}

void startAccountRefreshLoop() {
    ThreadTask::fireAndForget([] {
        refreshAccounts();

        while (true) {
            std::this_thread::sleep_for(std::chrono::minutes(g_statusRefreshInterval));
            LOG_INFO("Refreshing account statuses...");
            refreshAccounts();
        }
    });
}

void initializeAutoUpdater() {
    AutoUpdater::Initialize();
    AutoUpdater::SetBandwidthLimit(5_MB);
    AutoUpdater::SetShowNotifications(true);
    AutoUpdater::SetUpdateChannel(UpdateChannel::Stable);
    AutoUpdater::SetAutoUpdate(true, true, false);
    ClientUpdateChecker::UpdateChecker::Initialize();
}

@interface AppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation AppDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    return YES;
}
@end

@interface AppViewController : NSViewController <MTKViewDelegate>
@end

@implementation AppViewController {
    MTKView* _view;
    id<MTLCommandQueue> _commandQueue;
    ImGuiContext* _imguiContext;
    float _lastDPIScale;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        g_metalDevice = MTLCreateSystemDefaultDevice();
        _commandQueue = [g_metalDevice newCommandQueue];
        g_commandQueue = _commandQueue;
        _lastDPIScale = 1.0f;

        IMGUI_CHECKVERSION();
        _imguiContext = ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

        ImGui::StyleColorsDark();
    }
    return self;
}

- (void)dealloc {
    ImGui_ImplMetal_Shutdown();
    ImGui_ImplOSX_Shutdown();
    ImGui::DestroyContext();

#if !__has_feature(objc_arc)
    [super dealloc];
#endif
}

- (void)loadView {
    constexpr CGFloat windowWidth = 1000.0;
    constexpr CGFloat windowHeight = 560.0;

    NSRect frame = NSMakeRect(0, 0, windowWidth, windowHeight);
    _view = [[MTKView alloc] initWithFrame:frame device:g_metalDevice];
    _view.delegate = self;
    _view.clearColor = MTLClearColorMake(0.45, 0.55, 0.60, 1.00);
    self.view = _view;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    ImGui_ImplOSX_Init(self.view);
    ImGui_ImplMetal_Init(g_metalDevice);

    CGFloat scale = self.view.window.screen.backingScaleFactor ?: NSScreen.mainScreen.backingScaleFactor;
    _lastDPIScale = static_cast<float>(scale);
    ReloadFonts(_lastDPIScale);
}

- (void)drawInMTKView:(MTKView*)view {
    IM_ASSERT([NSThread isMainThread]);

    ThreadTask::RunOnMainUpdate();

    const CGFloat framebufferScale = view.window.screen.backingScaleFactor
                                   ?: NSScreen.mainScreen.backingScaleFactor;

    const float currentScale = static_cast<float>(framebufferScale);
    if (std::abs(currentScale - _lastDPIScale) > 0.01f) {
        _lastDPIScale = currentScale;
        ReloadFonts(_lastDPIScale);
    }

    ImGuiIO& io = ImGui::GetIO();

    io.DisplaySize = ImVec2(
        static_cast<float>(view.drawableSize.width) / currentScale,
        static_cast<float>(view.drawableSize.height) / currentScale
    );
    io.DisplayFramebufferScale = ImVec2(currentScale, currentScale);

    id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
    MTLRenderPassDescriptor* renderPassDescriptor = view.currentRenderPassDescriptor;

    if (!renderPassDescriptor) {
        return;
    }

    ImGui_ImplMetal_NewFrame(renderPassDescriptor);
    ImGui_ImplOSX_NewFrame(view);
    ImGui::NewFrame();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    bool shouldQuit = false;
    {
        std::shared_lock lock(g_accountsMutex);
        shouldQuit = RenderUI();
    }

    ImGui::PopStyleVar(1);
    ImGui::Render();

    ImDrawData* drawData = ImGui::GetDrawData();
    id<MTLRenderCommandEncoder> renderEncoder =
        [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];

    [renderEncoder pushDebugGroup:@"Dear ImGui rendering"];
    ImGui_ImplMetal_RenderDrawData(drawData, commandBuffer, renderEncoder);
    [renderEncoder popDebugGroup];
    [renderEncoder endEncoding];

    [commandBuffer presentDrawable:view.currentDrawable];
    [commandBuffer commit];

    if (shouldQuit) {
        [[NSApplication sharedApplication] terminate:nil];
    }
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size {

}

@end

auto createMainWindow() -> NSWindow* {
    constexpr CGFloat windowWidth = 1000.0;
    constexpr CGFloat windowHeight = 560.0;

    NSRect frame = NSMakeRect(0, 0, windowWidth, windowHeight);

    NSWindow* window = [[NSWindow alloc]
        initWithContentRect:frame
        styleMask:NSWindowStyleMaskTitled |
                  NSWindowStyleMaskClosable |
                  NSWindowStyleMaskResizable |
                  NSWindowStyleMaskMiniaturizable
        backing:NSBackingStoreBuffered
        defer:NO];

    [window setTitle:@"AltMan"];
    [window center];

    return window;
}

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        Data::LoadSettings("settings.json");

        if (g_checkUpdatesOnStartup) {
            initializeAutoUpdater();
        }

        Data::LoadAccounts("accounts.json");
        Data::LoadFriends("friends.json");

        startAccountRefreshLoop();

        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];

        AppDelegate* appDelegate = [[AppDelegate alloc] init];
        [app setDelegate:appDelegate];

        NSWindow* window = createMainWindow();
        AppViewController* viewController = [[AppViewController alloc] init];
        [window setContentViewController:viewController];

        [window makeKeyAndOrderFront:nil];
        [app activateIgnoringOtherApps:YES];
        [app run];
    }
    return 0;
}
