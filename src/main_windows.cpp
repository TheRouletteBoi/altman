#define IDI_ICON_32 102
#define STB_IMAGE_IMPLEMENTATION

#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <objbase.h>
#include <shellscalingapi.h>
#include <shellapi.h>

#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "utils/stb_image.h"

#include "main_common.h"
#include "system/multi_instance.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

    ID3D11Device *g_pd3dDevice = nullptr;
    ID3D11DeviceContext *g_pd3dDeviceContext = nullptr;
    IDXGISwapChain *g_pSwapChain = nullptr;
    ID3D11RenderTargetView *g_mainRenderTargetView = nullptr;
    bool g_swapChainOccluded = false;
    UINT g_resizeWidth = 0;
    UINT g_resizeHeight = 0;

    float g_currentDPIScale = 1.0f;

    [[nodiscard]]
    float GetDPIScale(HWND hwnd) {
        UINT dpi = GetDpiForWindow(hwnd);
        return static_cast<float>(dpi) / 96.0f;
    }

    void CreateRenderTarget() {
        ID3D11Texture2D *pBackBuffer = nullptr;
        g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
        if (pBackBuffer) {
            g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
            pBackBuffer->Release();
        }
    }

    void CleanupRenderTarget() {
        if (g_mainRenderTargetView) {
            g_mainRenderTargetView->Release();
            g_mainRenderTargetView = nullptr;
        }
    }

    bool CreateDeviceD3D(HWND hWnd) {
        DXGI_SWAP_CHAIN_DESC sd {};
        sd.BufferCount = 2;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hWnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        constexpr D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_0,
        };

        D3D_FEATURE_LEVEL featureLevel;
        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            featureLevels,
            2,
            D3D11_SDK_VERSION,
            &sd,
            &g_pSwapChain,
            &g_pd3dDevice,
            &featureLevel,
            &g_pd3dDeviceContext
        );

        if (hr == DXGI_ERROR_UNSUPPORTED) {
            hr = D3D11CreateDeviceAndSwapChain(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                0,
                featureLevels,
                2,
                D3D11_SDK_VERSION,
                &sd,
                &g_pSwapChain,
                &g_pd3dDevice,
                &featureLevel,
                &g_pd3dDeviceContext
            );
        }

        if (FAILED(hr)) {
            return false;
        }

        CreateRenderTarget();
        return true;
    }

    void CleanupDeviceD3D() {
        CleanupRenderTarget();
        if (g_pSwapChain) {
            g_pSwapChain->Release();
            g_pSwapChain = nullptr;
        }
        if (g_pd3dDeviceContext) {
            g_pd3dDeviceContext->Release();
            g_pd3dDeviceContext = nullptr;
        }
        if (g_pd3dDevice) {
            g_pd3dDevice->Release();
            g_pd3dDevice = nullptr;
        }
    }

    void ReloadFonts(float dpiScale) {
        const float scaledFontSize = BASE_FONT_SIZE * dpiScale;
        LoadImGuiFonts(scaledFontSize);

        ImGuiStyle &style = ImGui::GetStyle();
        style = ImGuiStyle();
        ImGui::StyleColorsDark();
        style.ScaleAllSizes(dpiScale);

        ImGui::GetIO().Fonts->Build();

        if (g_pd3dDevice) {
            ImGui_ImplDX11_InvalidateDeviceObjects();
            ImGui_ImplDX11_CreateDeviceObjects();
        }
    }

    LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
            return true;
        }

        switch (msg) {
            case WM_DPICHANGED: {
                float newDPIScale = GetDPIScale(hWnd);
                if (std::abs(newDPIScale - g_currentDPIScale) > 0.01f) {
                    g_currentDPIScale = newDPIScale;
                    ReloadFonts(g_currentDPIScale);
                }
                auto *rect = reinterpret_cast<RECT *>(lParam);
                SetWindowPos(
                    hWnd,
                    nullptr,
                    rect->left,
                    rect->top,
                    rect->right - rect->left,
                    rect->bottom - rect->top,
                    SWP_NOZORDER | SWP_NOACTIVATE
                );
                return 0;
            }

            case WM_SIZE:
                if (wParam == SIZE_MINIMIZED) {
                    return 0;
                }
                g_resizeWidth = LOWORD(lParam);
                g_resizeHeight = HIWORD(lParam);
                return 0;

            case WM_SYSCOMMAND:
                if ((wParam & 0xfff0) == SC_KEYMENU) {
                    return 0;
                }
                break;

            case WM_DESTROY:
                g_running = false;
                PostQuitMessage(0);
                return 0;
        }

        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    [[nodiscard]]
    HWND createMainWindow(HINSTANCE hInstance) {
        constexpr wchar_t CLASS_NAME[] = L"AltMan_WindowClass";

        WNDCLASSEXW wc {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_CLASSDC;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hInstance;
        wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON_32));
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = CLASS_NAME;
        wc.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON_32));
        RegisterClassExW(&wc);

        UINT dpi = GetDpiForSystem();
        int width = MulDiv(1000, dpi, 96);
        int height = MulDiv(560, dpi, 96);

        HWND hwnd = CreateWindowExW(
            0,
            CLASS_NAME,
            L"AltMan",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            width,
            height,
            nullptr,
            nullptr,
            hInstance,
            nullptr
        );

        BOOL useDarkMode = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));

        return hwnd;
    }

} // namespace

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

    D3D11_TEXTURE2D_DESC desc {};
    desc.Width = static_cast<UINT>(imageWidth);
    desc.Height = static_cast<UINT>(imageHeight);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA subResource {};
    subResource.pSysMem = imageData;
    subResource.SysMemPitch = static_cast<UINT>(imageWidth) * 4;

    ID3D11Texture2D *pTexture = nullptr;
    HRESULT hr = g_pd3dDevice->CreateTexture2D(&desc, &subResource, &pTexture);
    if (FAILED(hr) || !pTexture) {
        return std::unexpected("Failed to create D3D11 texture");
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc {};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView *srv = nullptr;
    hr = g_pd3dDevice->CreateShaderResourceView(pTexture, &srvDesc, &srv);
    pTexture->Release();

    if (FAILED(hr) || !srv) {
        return std::unexpected("Failed to create shader resource view");
    }

    TextureLoadResult result;
    result.texture.reset(srv);
    result.width = imageWidth;
    result.height = imageHeight;

    return result;
}

void OpenURL(const char* url) {
    ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HRESULT hrCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hrCom)) {
        std::println("Failed to initialize COM library. Error code = {:#x}", static_cast<unsigned long>(hrCom));
        return 1;
    }

    if (!initializeApp()) {
        CoUninitialize();
        return 1;
    }

    if (g_multiRobloxEnabled) {
        MultiInstance::Enable();
    }

    HWND hwnd = createMainWindow(hInstance);
    g_currentDPIScale = GetDPIScale(hwnd);

    if (!CreateDeviceD3D(hwnd)) {
        LOG_ERROR("Failed to create D3D device.");
        MessageBoxA(
            hwnd,
            "Failed to create D3D device. The application will now exit.",
            "D3D Error",
            MB_OK | MB_ICONERROR
        );
        CleanupDeviceD3D();
        CoUninitialize();
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    ReloadFonts(g_currentDPIScale);

    constexpr ImVec4 clearColor {0.45f, 0.55f, 0.60f, 1.00f};

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) {
                done = true;
            }
        }

        if (done) {
            break;
        }

        WorkerThreads::RunOnMainUpdate();

        if (g_swapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            Sleep(10);
            continue;
        }
        g_swapChainOccluded = false;

        if (g_resizeWidth != 0 && g_resizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_resizeWidth, g_resizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeWidth = g_resizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
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

        ImGui::PopStyleVar(1);
        ImGui::Render();

        const float clearColorWithAlpha[4]
            = {clearColor.x * clearColor.w, clearColor.y * clearColor.w, clearColor.z * clearColor.w, clearColor.w};

        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clearColorWithAlpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        HRESULT hrPresent = g_pSwapChain->Present(1, 0);
        g_swapChainOccluded = (hrPresent == DXGI_STATUS_OCCLUDED);

        if (shouldQuit) {
            done = true;
        }
    }

    g_running = false;

    ShutdownManager::instance().requestShutdown();
    ShutdownManager::instance().waitForShutdown();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    CoUninitialize();

    return 0;
}
