#ifndef WINDOW_SELECTION_MODULE_H
#define WINDOW_SELECTION_MODULE_H

#include <winrt/Windows.Graphics.Capture.h>
#include <shobjidl.h> // For IInitializeWithWindow

// Function declaration to show the picker UI.
winrt::Windows::Graphics::Capture::GraphicsCaptureItem SelectCaptureTarget(HWND parentWindow);

#endif // WINDOW_SELECTION_MODULE_H
