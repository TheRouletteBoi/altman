#define STB_IMAGE_IMPLEMENTATION

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <TargetConditionals.h>

#include "imgui_impl_metal.h"
#include "imgui_impl_osx.h"
#include "utils/stb_image.h"

#include "app_common.h"
#include "system/client_update_checker_macos.h"

namespace {
    id<MTLDevice> g_metalDevice = nil;
    id<MTLCommandQueue> g_commandQueue = nil;

    std::atomic<bool> g_fontReloadPending = false;
} // namespace

void ReloadFonts(float dpiScale) {
    IM_ASSERT([NSThread isMainThread]);

    const float scaledFontSize = BASE_FONT_SIZE; // macOS handles scaling via framebuffer
    LoadImGuiFonts(scaledFontSize);

    ImGui::StyleColorsDark();

    g_fontReloadPending = true;
}

[[nodiscard]]
std::expected<TextureLoadResult, std::string> LoadTextureFromMemory(const void *data, size_t dataSize) {
    int imageWidth = 0;
    int imageHeight = 0;
    unsigned char *imageData = stbi_load_from_memory(
        static_cast<const unsigned char *>(data),
        static_cast<int>(dataSize),
        &imageWidth,
        &imageHeight,
        nullptr,
        4
    );

    if (!imageData) {
        return std::unexpected("Failed to decode image data");
    }

    auto imageDataCleanup = std::unique_ptr<unsigned char, decltype(&stbi_image_free)>(imageData, stbi_image_free);

    MTLTextureDescriptor *textureDescriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:static_cast<NSUInteger>(imageWidth)
                                                          height:static_cast<NSUInteger>(imageHeight)
                                                       mipmapped:NO];
    textureDescriptor.usage = MTLTextureUsageShaderRead;
    textureDescriptor.storageMode = MTLStorageModeShared;

    id<MTLTexture> texture = [g_metalDevice newTextureWithDescriptor:textureDescriptor];
    if (!texture) {
        return std::unexpected("Failed to create Metal texture");
    }

    [texture
        replaceRegion:MTLRegionMake2D(0, 0, static_cast<NSUInteger>(imageWidth), static_cast<NSUInteger>(imageHeight))
          mipmapLevel:0
            withBytes:imageData
          bytesPerRow:4 * static_cast<NSUInteger>(imageWidth)];

    TextureLoadResult result;
    result.texture.reset(texture);
    result.width = imageWidth;
    result.height = imageHeight;

    return result;
}

void OpenURL(const char* url) {
    @autoreleasepool {
        NSURL* nsurl = [NSURL URLWithString:
            [NSString stringWithUTF8String:url]];
        if (nsurl) {
            [[NSWorkspace sharedWorkspace] openURL:nsurl];
        }
    }
}

@interface AppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation AppDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    return YES;
}

- (void)applicationWillTerminate:(NSNotification *)notification {
    g_running = false;
    ShutdownManager::instance().requestShutdown();
    ClientUpdateChecker::UpdateChecker::Shutdown();
    ShutdownManager::instance().waitForShutdown();
}
@end

@interface AppViewController : NSViewController <MTKViewDelegate>
@end

@implementation AppViewController {
    MTKView *_view;
    id<MTLCommandQueue> _commandQueue;
    ImGuiContext *_imguiContext;
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

        ImGuiIO &io = ImGui::GetIO();
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
    ReloadFonts(1.0f);
}

- (void)drawInMTKView:(MTKView *)view {
    IM_ASSERT([NSThread isMainThread]);

    WorkerThreads::RunOnMainUpdate();

    const CGFloat framebufferScale = view.window.screen.backingScaleFactor ?: NSScreen.mainScreen.backingScaleFactor;

    const float currentScale = static_cast<float>(framebufferScale);
    if (std::abs(currentScale - _lastDPIScale) > 0.01f) {
        _lastDPIScale = currentScale;
        ReloadFonts(1.0f);
    }

    ImGuiIO &io = ImGui::GetIO();

    io.DisplaySize = ImVec2(
        static_cast<float>(view.drawableSize.width),
        static_cast<float>(view.drawableSize.height)
    );
    io.DisplayFramebufferScale = ImVec2(currentScale, currentScale);

    id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
    MTLRenderPassDescriptor *renderPassDescriptor = view.currentRenderPassDescriptor;

    if (!renderPassDescriptor) {
        return;
    }

    ImGui_ImplMetal_NewFrame(renderPassDescriptor);
    ImGui_ImplOSX_NewFrame(view);
    ImGui::NewFrame();

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    bool shouldQuit = false;
    {
        std::shared_lock lock(g_accountsMutex);
        shouldQuit = RenderUI();
    }

    if (g_fontReloadPending.exchange(false)) {
        ImGui_ImplMetal_DestroyDeviceObjects();
        ImGui_ImplMetal_CreateDeviceObjects(g_metalDevice);
    }

    ImGui::PopStyleVar(1);
    ImGui::Render();

    ImDrawData *drawData = ImGui::GetDrawData();
    id<MTLRenderCommandEncoder> renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];

    [renderEncoder pushDebugGroup:@ "Dear ImGui rendering"];
    ImGui_ImplMetal_RenderDrawData(drawData, commandBuffer, renderEncoder);
    [renderEncoder popDebugGroup];
    [renderEncoder endEncoding];

    [commandBuffer presentDrawable:view.currentDrawable];
    [commandBuffer commit];

    if (shouldQuit) {
        [[NSApplication sharedApplication] terminate:nil];
    }
}

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size {
}

@end

NSWindow *createMainWindow() {
    constexpr CGFloat windowWidth = 1000.0;
    constexpr CGFloat windowHeight = 560.0;

    NSRect frame = NSMakeRect(0, 0, windowWidth, windowHeight);

    NSWindow *window =
        [[NSWindow alloc] initWithContentRect:frame
                                    styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                              | NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
                                      backing:NSBackingStoreBuffered
                                        defer:NO];

    [window setTitle:@ "AltMan"];
    [window center];

    return window;
}

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        if (!initializeApp()) {
            return 1;
        }

        if (g_checkUpdatesOnStartup) {
            ClientUpdateChecker::UpdateChecker::Initialize();
        }

        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];

        AppDelegate *appDelegate = [[AppDelegate alloc] init];
        [app setDelegate:appDelegate];

        NSWindow *window = createMainWindow();
        AppViewController *viewController = [[AppViewController alloc] init];
        [window setContentViewController:viewController];

        [window makeKeyAndOrderFront:nil];
        [app activateIgnoringOtherApps:YES];
        [app run];
    }
    return 0;
}
