#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include "WindowSelectionModule.h"  // Include the new module
#include <winrt/base.h>

// Manual declaration for IDirect3DDxgiInterfaceAccess
struct __declspec(uuid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1")) IDirect3DDxgiInterfaceAccess : IUnknown {
    virtual HRESULT __stdcall GetInterface(REFIID iid, void** p) = 0;
};

// Globals
HWND g_hWnd = nullptr;
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swapChain = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;

// WGC Globals
winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool g_framePool{ nullptr };
winrt::Windows::Graphics::Capture::GraphicsCaptureSession g_session{ nullptr };
winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice g_winrtDevice{ nullptr };
winrt::com_ptr<ID3D11Texture2D> g_lastCapturedTexture{ nullptr };  // To hold the latest frame

// Forward declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
bool CreateDeviceAndSwapChain();
void RenderFrame();
void Cleanup();
// WGC Forwards
bool InitializeWGC(const winrt::Windows::Graphics::Capture::GraphicsCaptureItem& item);
void OnFrameArrived(const winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool& sender, const winrt::Windows::Foundation::IInspectable&);
void StopWGC();
// Helper to create a WinRT Direct3D device from a D3D11 device
winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice CreateWinrtDevice(ID3D11Device* d3dDevice) {
    winrt::com_ptr<IDXGIDevice> dxgiDevice;
    d3dDevice->QueryInterface(__uuidof(IDXGIDevice), dxgiDevice.put_void());
    winrt::com_ptr<IInspectable> inspectable;
    winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectable.put()));
    return inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
}

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

    // Initialize D3D11 FIRST
    if (!CreateDeviceAndSwapChain()) {
        Cleanup();
        return -1;
    }

    // Select capture target using the separate module
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem item = SelectCaptureTarget(g_hWnd);
    if (!item) {
        OutputDebugString(L"Capture target selection failed or canceled.\n");
        Cleanup();
        return -1;
    }

    // Initialize WGC with selected item
    if (!InitializeWGC(item)) {
        OutputDebugString(L"WGC init failed!\n");
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

// WindowProc (unchanged)
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

// CreateDeviceAndSwapChain (unchanged)
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

// RenderFrame (corrected: single path, copy if texture available)
void RenderFrame() {
    ID3D11Texture2D* backBuffer = nullptr;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (g_lastCapturedTexture) {
        g_context->CopyResource(backBuffer, g_lastCapturedTexture.get());
    }
    else {
        float clearColor[4] = { 0.0f, 0.2f, 0.4f, 1.0f };  // Fallback to blue
        g_context->ClearRenderTargetView(g_rtv, clearColor);
    }
    backBuffer->Release();
    g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_swapChain->Present(1, 0);  // VSync
}

// Cleanup (call StopWGC first)
void Cleanup() {
    StopWGC();
    if (g_rtv) g_rtv->Release();
    if (g_swapChain) g_swapChain->Release();
    if (g_context) g_context->Release();
    if (g_device) g_device->Release();
}

// Initialize WGC with selected item
bool InitializeWGC(const winrt::Windows::Graphics::Capture::GraphicsCaptureItem& item) {
    // Wrap D3D11 device for WinRT
    g_winrtDevice = CreateWinrtDevice(g_device);

    // Create free-threaded frame pool
    auto size = item.Size();
    g_framePool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
        g_winrtDevice,
        winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
        2, size);

    // Subscribe to frames
    g_framePool.FrameArrived(winrt::auto_revoke, &OnFrameArrived);

    // Create and start session
    g_session = g_framePool.CreateCaptureSession(item);
    g_session.StartCapture();

    // Pre-allocate staging texture
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = size.Width;
    desc.Height = size.Height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = 0;
    HRESULT hr = g_device->CreateTexture2D(&desc, nullptr, g_lastCapturedTexture.put());
    if (FAILED(hr)) {
        OutputDebugString(L"Failed to create staging texture!\n");
        return false;
    }

    return true;
}

// Frame arrived handler
void OnFrameArrived(const winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool& sender, const winrt::Windows::Foundation::IInspectable&) {
    auto frame = sender.TryGetNextFrame();
    if (!frame) return;

    OutputDebugString(L"Frame arrived!\n");

    // Extract ID3D11Texture2D
    auto surface = frame.Surface();
    winrt::com_ptr<IDirect3DDxgiInterfaceAccess> dxgiAccess = surface.as<IDirect3DDxgiInterfaceAccess>();
    winrt::com_ptr<ID3D11Texture2D> capturedTexture;
    HRESULT hr = dxgiAccess->GetInterface(__uuidof(ID3D11Texture2D), capturedTexture.put_void());
    if (FAILED(hr)) return;

    // Copy to our texture
    if (capturedTexture) {
        g_context->CopyResource(g_lastCapturedTexture.get(), capturedTexture.get());
    }

    // Handle size change
    auto contentSize = frame.ContentSize();
    D3D11_TEXTURE2D_DESC currentDesc;
    g_lastCapturedTexture->GetDesc(&currentDesc);
    if (contentSize.Width != currentDesc.Width || contentSize.Height != currentDesc.Height) {
        sender.Recreate(g_winrtDevice, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, contentSize);
        currentDesc.Width = contentSize.Width;
        currentDesc.Height = contentSize.Height;
        g_lastCapturedTexture = nullptr;
        g_device->CreateTexture2D(&currentDesc, nullptr, g_lastCapturedTexture.put());
        OutputDebugString(L"Recreated pool and texture for new size\n");
    }
}

void StopWGC() {
    if (g_session) g_session.Close();
    if (g_framePool) g_framePool.Close();
    g_winrtDevice = nullptr;
    g_lastCapturedTexture = nullptr;
}
