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

        // alt+tab fix while keeping full GPU acceleration:
        // the GPU watchdog kills the GPU process when its thread briefly stalls
        // during the exclusive-fullscreen -> minimized transition on alt+tab,
        // which permanently took CEF down. Disabling the watchdog lets the GPU
        // process survive the stall and resume; RestoreBrowserTextures() on
        // focus regain recovers the DX9 surface. The GPU stays out-of-process
        // (in-process-gpu crashes this multi-threaded windowless setup).
        command_line->AppendSwitch("disable-gpu-watchdog");
    }

private:
    IMPLEMENT_REFCOUNTING(DefaultCefApp);
};
