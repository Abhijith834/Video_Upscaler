#include "WindowSelectionModule.h"
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>

winrt::Windows::Graphics::Capture::GraphicsCapturePicker picker;
winrt::Windows::Graphics::Capture::GraphicsCaptureItem SelectCaptureTarget(HWND parentWindow) {
    winrt::Windows::Graphics::Capture::GraphicsCapturePicker picker;
    auto init = picker.as<IInitializeWithWindow>();
    init->Initialize(parentWindow);
    auto item = picker.PickSingleItemAsync().get();
    if (item) {
        OutputDebugString(L"User selected a capture target.\n");
    }
    else {
        OutputDebugString(L"Capture selection canceled.\n");
    }
    return item;
}
