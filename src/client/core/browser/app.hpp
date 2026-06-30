#pragma once

#include "include/cef_app.h"

class DefaultCefApp : public CefApp {
public:
    DefaultCefApp() = default;

    void OnBeforeCommandLineProcessing(const CefString& process_type, CefRefPtr<CefCommandLine> command_line) override 
    {
        command_line->AppendSwitchWithValue("force-device-scale-factor", "1.0");
        command_line->AppendSwitchWithValue("high-dpi-support", "1");
        command_line->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");
        command_line->AppendSwitchWithValue("allow-browser-signin", "false");
        command_line->AppendSwitch("enable-begin-frame-scheduling");

        // alt+tab fix WITHOUT sacrificing GPU acceleration:
        // run the GPU inside the browser process so there is no separate GPU
        // child process that dies permanently on DirectX9 device loss. Chromium
        // recovers the GPU context in-process; RestoreBrowserTextures() handles
        // the DX9 surface. The watchdog is disabled so it cannot kill the GPU
        // thread while alt+tab briefly stalls it.
        command_line->AppendSwitch("in-process-gpu");
        command_line->AppendSwitch("disable-gpu-watchdog");
    }

private:
    IMPLEMENT_REFCOUNTING(DefaultCefApp);
};
