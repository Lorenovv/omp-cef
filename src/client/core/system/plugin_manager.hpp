#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <unordered_set>
#include <cstdint>

class BrowserManager;

class PluginManager
{
public:
    static PluginManager& Instance()
    {
        static PluginManager s_instance;
        return s_instance;
    }

    void Initialize(BrowserManager* browserManager);
    void Shutdown();

    void OnMainLoop();
    void OnDxReset();
    void OnBrowserCreated(uint32_t id, int status);
    void OnConnect();
    void OnDisconnect();

    bool IsPluginBrowser(uint32_t id) const;

    static void __cdecl ApiCreateBrowser(uint32_t id, const char* url, bool hidden, bool focused);
    static void __cdecl ApiDestroyBrowser(uint32_t id);
    static void __cdecl ApiHideBrowser(uint32_t id, bool hidden);
    static void __cdecl ApiFocusBrowser(uint32_t id, bool focused);

private:
    PluginManager() = default;
    ~PluginManager() = default;

    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    struct Plugin
    {
        HMODULE module = nullptr;
        std::wstring name;
        void (__cdecl* cef_initialize)(void* api) = nullptr;
        void (__cdecl* cef_samp_mainloop)() = nullptr;
        void (__cdecl* cef_dxreset)() = nullptr;
        void (__cdecl* cef_browser_created)(uint32_t id, int status) = nullptr;
        void (__cdecl* cef_connect)() = nullptr;
        void (__cdecl* cef_disconnect)() = nullptr;
        void (__cdecl* cef_quit)() = nullptr;
    };

    std::vector<Plugin> plugins_;
    std::unordered_set<uint32_t> plugin_browsers_;
    BrowserManager* browser_manager_ = nullptr;
    bool initialized_ = false;
};
