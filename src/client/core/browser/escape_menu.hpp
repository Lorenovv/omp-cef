#pragma once

#include <atomic>
#include <cstdint>

#include <windows.h>

// Defines how the native GTA SA ESC/pause menu is handled
enum class EscapeMenuMode : std::uint8_t
{
    Gta = 0,
    Disabled = 1,
    Custom = 2
};

class EscapeMenuController
{
public:
    static const char* GetModeName(EscapeMenuMode mode);
    static EscapeMenuMode NormalizeMode(EscapeMenuMode mode);
    static bool ShouldSuppressNativeMenu(EscapeMenuMode mode);

    EscapeMenuMode SetMode(EscapeMenuMode mode);
    EscapeMenuMode GetMode() const;

    bool IsNativePauseMenuVisible() const;
    bool UpdateInput();
    bool ConsumeWndProcMessage(UINT message, WPARAM w_param, LPARAM l_param);
    void SuppressNativeMenu();

    bool IsCustomMenuOpen() const;
    void SetCustomMenuOpen(bool open);
    void ToggleCustomMenu();
    bool HasPendingCustomMenuVisibilityChange();

private:
    bool ProcessInputState(bool is_down);

    std::atomic<EscapeMenuMode> mode_{ EscapeMenuMode::Custom };
    std::atomic<bool> input_down_{ false };
    std::atomic<bool> custom_menu_open_{ false };
    std::atomic<bool> custom_menu_visibility_changed_{ false };
};
