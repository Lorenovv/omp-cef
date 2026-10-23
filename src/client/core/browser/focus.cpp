#include "focus.hpp"
#include "browser/manager.hpp"
#include <samp/components/game.hpp>
#include <samp/common.hpp>
#include <hooks/cursor_hook.hpp>

namespace {
    // How long after focus leaves CEF we keep re-hiding a stray OS cursor.
    // Covers the post-login spawn re-show (which happens within a few frames of
    // focus loss) while staying short enough not to fight cursors that appear
    // later during normal gameplay (scoreboard/TAB, chat input).
    constexpr uint64_t kSpawnCursorCorrectionMs = 1500;
}

void FocusManager::Update()
{
    auto* game = GetComponent<GameComponent>();
    if (!game)
        return;

    const bool is_cef_focused_now = manager_.IsAnyBrowserFocused();
    const bool force_resync = force_resync_.exchange(false);
    const bool entered_cef_focus = is_cef_focused_now && !was_cef_focused_last_frame_;
    const bool left_cef_focus = !is_cef_focused_now && was_cef_focused_last_frame_;

    if (is_cef_focused_now)
    {
        had_cef_focus_session_ = true;

        game->SetCursorMode(CMODE_LOCKCAMANDCONTROL, FALSE);
        game->ProcessInputEnabling();

        const cef_cursor_type_t type = manager_.GetCursorType();
        HCURSOR hCursor = nullptr;
        switch (type) {
            case CT_POINTER:
                hCursor = LoadCursor(nullptr, IDC_ARROW); 
                break;
            case CT_IBEAM:
                hCursor = LoadCursor(nullptr, IDC_IBEAM); 
                break;
            case CT_HAND:
                hCursor = LoadCursor(nullptr, IDC_HAND);  
                break;
            default:
                hCursor = LoadCursor(nullptr, IDC_ARROW); 
                break;
        }

        CursorHook::Instance().SetForcedCursor(hCursor);
        CursorHook::Instance().SetForced(true);
        ::SetCursor(hCursor);

        if (entered_cef_focus || force_resync)
        {
            while (::ShowCursor(TRUE) < 0) {}
        }

        cursor_shown_ = true;
    }
    else 
    {
        // Teardown on the focus-loss edge, on an explicit resync, or while the
        // shown-latch is still set. The latch covers the focused browser being
        // destroyed the same frame focus is released (e.g. AUTH on login).
        // This path is unchanged from the known-good 1.0.4 build.
        const bool edge_teardown = left_cef_focus || force_resync || cursor_shown_;

        if (edge_teardown)
        {
            CursorHook::Instance().SetForced(false);

            while (::ShowCursor(FALSE) >= 0) {}
            ::SetCursor(nullptr);

            game->SetCursorMode(CMODE_NONE, TRUE);
            game->ProcessInputEnabling();

            cursor_shown_ = false;
        }
    }

    was_cef_focused_last_frame_ = is_cef_focused_now;
}

void FocusManager::SetInputFocus(int browserId, bool has_focus)
{
    if (has_focus) { 
        input_focused_browser_id_.store(browserId); 
    }
    else {
        int expected_id = browserId; 
        input_focused_browser_id_.compare_exchange_strong(expected_id, -1);
    }
}

bool FocusManager::ShouldBlockChat() const
{
    if (!chat_input_enabled_.load())
        return true;

    const int focused_id = input_focused_browser_id_.load();
    if (focused_id == -1) 
        return false;

    // Block chat if focused browser is configured to control chat input
    auto* instance = const_cast<BrowserManager&>(manager_).GetBrowserInstance(focused_id);
    if (instance) { 
        return instance->controls_chat_input; 
    }

    return false;
}