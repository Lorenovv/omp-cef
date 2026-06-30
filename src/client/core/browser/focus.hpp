#pragma once

#include <atomic>

class BrowserManager;

// Manages input focus transitions between game and CEF browsers
// Handles cursor visibility, camera control, and chat input blocking
class FocusManager
{
public:
    explicit FocusManager(BrowserManager& mgr) : manager_(mgr) {}
    ~FocusManager() = default;

    FocusManager(const FocusManager&) = delete;
    FocusManager& operator=(const FocusManager&) = delete;

    void Update();
    // void RestoreGameControls();
    void SetInputFocus(int browserId, bool has_focus);
    bool ShouldBlockChat() const;

    void SetChatInputEnabled(bool enabled) { chat_input_enabled_.store(enabled); }
    void SetChatInputOpen(bool open) { chat_input_open_.store(open); }
    bool IsChatInputOpen() const { return chat_input_open_.load(); }

    int GetInputFocusedBrowserId() const { return input_focused_browser_id_.load(); }
    bool IsTextInputFocused(int browserId) const { return input_focused_browser_id_.load() == browserId; }

    void RequestResync() { force_resync_.store(true); }
private:
    BrowserManager& manager_;

    // Focus state to prevent redundant ShowCursor/HideCursor calls
    bool was_cef_focused_last_frame_ = false;

    // Latches true while we are forcing/showing the CEF cursor. Lets us always
    // hide the cursor again once focus leaves CEF, even if the focus-loss edge
    // was missed (e.g. the focused AUTH browser is destroyed on the same frame
    // focus is released on auth completion) - otherwise the cursor stays stuck.
    bool cursor_shown_ = false;

    // Set true once any CEF browser has been focused. Gates the per-frame stray
    // cursor correction in Update() so it never calls into SA-MP game functions
    // before the first focus (GTA/SA-MP menu, loading screen, server browser),
    // where those calls dereference uninitialized samp.dll state and crash at
    // startup. By the post-login spawn (the case we correct) SA-MP is up.
    bool had_cef_focus_session_ = false;

    // Browser ID currently holding input focus (-1 = none)
    std::atomic<int> input_focused_browser_id_{ -1 };

    std::atomic<bool> chat_input_enabled_{ true };
    std::atomic<bool> chat_input_open_{ false };

    std::atomic<bool> force_resync_{ false };
};