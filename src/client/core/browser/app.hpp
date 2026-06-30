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

        // === alt+tab survival while keeping full GPU acceleration ===
        //
        // 1) GPU watchdog kills the GPU process when its thread briefly stalls
        //    during the exclusive-fullscreen <-> minimize transition on a SHORT
        //    alt+tab. Disabling it lets the GPU process survive the stall.
        command_line->AppendSwitch("disable-gpu-watchdog");

        // 2) Renderer/compositor backgrounding is what kills the UI on a LONG
        //    alt+tab. When the game window loses focus / is occluded / minimized,
        //    Chromium "backgrounds" the renderer and throttles or suspends its
        //    compositor, so offscreen (OSR) painting stops and does not cleanly
        //    resume. Keeping the renderer foregrounded makes CEF keep its
        //    compositor alive while tabbed out, so the UI is alive on return.
        command_line->AppendSwitch("disable-renderer-backgrounding");
        command_line->AppendSwitch("disable-backgrounding-occluded-windows");
        command_line->AppendSwitch("disable-background-timer-throttling");

        // 3) If the GPU process does die for real, don't give up after a few
        //    restarts - keep respawning it so the compositor can come back.
        command_line->AppendSwitch("disable-gpu-process-crash-limit");

        // GPU stays OUT-of-process on purpose: in-process-gpu crashes this
        // multi_threaded_message_loop + windowless setup on startup.
    }

private:
    IMPLEMENT_REFCOUNTING(DefaultCefApp);
};
