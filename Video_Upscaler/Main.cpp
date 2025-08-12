#include <windows.h>

#include <d3d11.h>

#include <dxgi1_6.h>

#include <d3dcompiler.h>

#include <winrt/base.h>

#include <winrt/Windows.Foundation.h>

#include <winrt/Windows.Graphics.Capture.h>

#include <windows.graphics.capture.interop.h>

// Globals
HWND g_hWnd = nullptr;
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swapChain = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;

// Forward declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
bool CreateDeviceAndSwapChain();
void RenderFrame();
void Cleanup();

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    // Register window class
    WNDCLASS wc = {};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"VideoScalerWindow";
    RegisterClass(&wc);

    // Create borderless topmost window (full screen size)
    RECT desktop;
    GetClientRect(GetDesktopWindow(), &desktop);
    g_hWnd = CreateWindow(L"VideoScalerWindow", L"Video Scaler Overlay",
        WS_POPUP, 0, 0, desktop.right, desktop.bottom,
        nullptr, nullptr, hInstance, nullptr);

    if (!g_hWnd) return -1;

    // Make topmost and show
    SetWindowPos(g_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    ShowWindow(g_hWnd, SW_SHOW);

    // Initialize D3D11
    if (!CreateDeviceAndSwapChain()) {
        Cleanup();
        return -1;
    }

    // Message loop
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        RenderFrame();  // Render each frame
    }

    Cleanup();
    return 0;
}

// Single implementation for WindowProc
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) DestroyWindow(hwnd);  // Escape to close
        return 0;
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

// Single implementation for CreateDeviceAndSwapChain
bool CreateDeviceAndSwapChain() {
    // Create DXGI factory
    IDXGIFactory1* factory = nullptr;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return false;

    // Create D3D11 device and context
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION,
        &g_device, nullptr, &g_context);
    if (FAILED(hr)) {
        factory->Release();
        return false;
    }

    // Create swap chain
    DXGI_SWAP_CHAIN_DESC scDesc = {};
    scDesc.BufferCount = 2;
    scDesc.BufferDesc.Width = GetSystemMetrics(SM_CXSCREEN);
    scDesc.BufferDesc.Height = GetSystemMetrics(SM_CYSCREEN);
    scDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.OutputWindow = g_hWnd;
    scDesc.SampleDesc.Count = 1;
    scDesc.Windowed = TRUE;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    hr = factory->CreateSwapChain(g_device, &scDesc, &g_swapChain);
    factory->Release();
    if (FAILED(hr)) return false;

    // Create render target view
    ID3D11Texture2D* backBuffer = nullptr;
    hr = g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) return false;
    hr = g_device->CreateRenderTargetView(backBuffer, nullptr, &g_rtv);
    backBuffer->Release();
    if (FAILED(hr)) return false;

    // Set viewport
    D3D11_VIEWPORT vp = {};
    vp.Width = (float)scDesc.BufferDesc.Width;
    vp.Height = (float)scDesc.BufferDesc.Height;
    g_context->RSSetViewports(1, &vp);

    return true;
}

void RenderFrame() {
    float clearColor[4] = { 0.0f, 0.2f, 0.4f, 1.0f };  // Dark blue
    g_context->ClearRenderTargetView(g_rtv, clearColor);
    g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_swapChain->Present(1, 0);  // VSync
}

void Cleanup() {
    if (g_rtv) g_rtv->Release();
    if (g_swapChain) g_swapChain->Release();
    if (g_context) g_context->Release();
    if (g_device) g_device->Release();
}
