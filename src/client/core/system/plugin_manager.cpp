#include "plugin_manager.hpp"

#include <filesystem>
#include "browser/manager.hpp"
#include "system/logger.hpp"

void PluginManager::Initialize(BrowserManager* browserManager)
{
    browser_manager_ = browserManager;
    initialized_ = true;

    wchar_t exePath[MAX_PATH] = { 0 };
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH))
    {
        LOG_ERROR("[PluginManager] GetModuleFileNameW failed.");
        return;
    }

    std::filesystem::path gameDir(exePath);
    gameDir = gameDir.parent_path();
    std::filesystem::path pluginsDir = gameDir / L"cef" / L"plugins";

    LOG_INFO("[PluginManager] Searching for client plugins in: {}", pluginsDir.string());

    if (!std::filesystem::exists(pluginsDir) || !std::filesystem::is_directory(pluginsDir))
    {
        LOG_INFO("[PluginManager] Plugins directory does not exist: {}", pluginsDir.string());
        return;
    }

    static void* s_ApiTable[4] = {
        reinterpret_cast<void*>(&PluginManager::ApiCreateBrowser),
        reinterpret_cast<void*>(&PluginManager::ApiDestroyBrowser),
        reinterpret_cast<void*>(&PluginManager::ApiHideBrowser),
        reinterpret_cast<void*>(&PluginManager::ApiFocusBrowser)
    };

    for (const auto& entry : std::filesystem::directory_iterator(pluginsDir))
    {
        if (!entry.is_regular_file())
            continue;

        if (entry.path().extension() != L".dll")
            continue;

        if (entry.path().filename() == L"cef_interface.dll")
        {
            LOG_INFO("[PluginManager] Skipping obsolete Laminar bridge: cef_interface.dll");
            continue;
        }

        std::wstring pluginPath = entry.path().wstring();
        LOG_INFO("[PluginManager] Loading plugin: {}", entry.path().string());

        HMODULE hModule = LoadLibraryW(pluginPath.c_str());
        if (!hModule)
        {
            LOG_ERROR("[PluginManager] Failed to load library {}: error {}", entry.path().string(), GetLastError());
            continue;
        }

        Plugin p;
        p.module = hModule;
        p.name = entry.path().filename().wstring();
        p.cef_initialize = reinterpret_cast<void(__cdecl*)(void*)>(GetProcAddress(hModule, "cef_initialize"));
        p.cef_samp_mainloop = reinterpret_cast<void(__cdecl*)()>(GetProcAddress(hModule, "cef_samp_mainloop"));
        p.cef_dxreset = reinterpret_cast<void(__cdecl*)()>(GetProcAddress(hModule, "cef_dxreset"));
        p.cef_browser_created = reinterpret_cast<void(__cdecl*)(uint32_t, int)>(GetProcAddress(hModule, "cef_browser_created"));
        p.cef_connect = reinterpret_cast<void(__cdecl*)()>(GetProcAddress(hModule, "cef_connect"));
        p.cef_disconnect = reinterpret_cast<void(__cdecl*)()>(GetProcAddress(hModule, "cef_disconnect"));
        p.cef_quit = reinterpret_cast<void(__cdecl*)()>(GetProcAddress(hModule, "cef_quit"));

        if (p.cef_initialize)
        {
            LOG_INFO("[PluginManager] Calling cef_initialize for: {}", entry.path().string());
            __try
            {
                p.cef_initialize(s_ApiTable);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                LOG_ERROR("[PluginManager] Exception in cef_initialize for: {}", entry.path().string());
            }
        }

        plugins_.push_back(p);
        LOG_INFO("[PluginManager] Successfully loaded and initialized plugin: {}", entry.path().string());
    }
}

void PluginManager::Shutdown()
{
    LOG_INFO("[PluginManager] Shutting down plugins (count={})...", plugins_.size());
    for (auto& p : plugins_)
    {
        if (p.cef_quit)
        {
            __try
            {
                p.cef_quit();
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }
        if (p.module)
        {
            FreeLibrary(p.module);
        }
    }
    plugins_.clear();
    browser_manager_ = nullptr;
    initialized_ = false;
}

void PluginManager::OnMainLoop()
{
    for (auto& p : plugins_)
    {
        if (p.cef_samp_mainloop)
        {
            __try
            {
                p.cef_samp_mainloop();
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }
    }
}

void PluginManager::OnDxReset()
{
    for (auto& p : plugins_)
    {
        if (p.cef_dxreset)
        {
            __try
            {
                p.cef_dxreset();
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }
    }
}

void PluginManager::OnBrowserCreated(uint32_t id, int status)
{
    for (auto& p : plugins_)
    {
        if (p.cef_browser_created)
        {
            __try
            {
                p.cef_browser_created(id, status);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }
    }
}

void PluginManager::OnConnect()
{
    for (auto& p : plugins_)
    {
        if (p.cef_connect)
        {
            __try
            {
                p.cef_connect();
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }
    }
}

void PluginManager::OnDisconnect()
{
    for (auto& p : plugins_)
    {
        if (p.cef_disconnect)
        {
            __try
            {
                p.cef_disconnect();
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }
    }
}

void __cdecl PluginManager::ApiCreateBrowser(uint32_t id, const char* url, bool hidden, bool focused)
{
    LOG_INFO("[PluginApi] CreateBrowser: id={}, url={}, hidden={}, focused={}", id, url ? url : "null", hidden, focused);
    auto* bm = PluginManager::Instance().browser_manager_;
    if (!bm)
    {
        LOG_WARN("[PluginApi] CreateBrowser: browser_manager_ is null");
        return;
    }

    bm->CreateBrowser(static_cast<int>(id), url ? url : "", focused, true, 0.0f, 0.0f);
    if (hidden)
    {
        bm->SetBrowserVisible(static_cast<int>(id), false);
    }
}

void __cdecl PluginManager::ApiDestroyBrowser(uint32_t id)
{
    LOG_INFO("[PluginApi] DestroyBrowser: id={}", id);
    auto* bm = PluginManager::Instance().browser_manager_;
    if (bm)
    {
        bm->DestroyBrowser(static_cast<int>(id));
    }
}

void __cdecl PluginManager::ApiHideBrowser(uint32_t id, bool hidden)
{
    LOG_INFO("[PluginApi] HideBrowser: id={}, hidden={}", id, hidden);
    auto* bm = PluginManager::Instance().browser_manager_;
    if (bm)
    {
        bm->SetBrowserVisible(static_cast<int>(id), !hidden);
    }
}

void __cdecl PluginManager::ApiFocusBrowser(uint32_t id, bool focused)
{
    LOG_INFO("[PluginApi] FocusBrowser: id={}, focused={}", id, focused);
    auto* bm = PluginManager::Instance().browser_manager_;
    if (bm)
    {
        bm->FocusBrowser(static_cast<int>(id), focused);
    }
}

extern "C"
{
    __declspec(dllexport) void __cdecl CefCreateBrowser(uint32_t id, const char* url, bool hidden, bool focused)
    {
        PluginManager::ApiCreateBrowser(id, url, hidden, focused);
    }

    __declspec(dllexport) void __cdecl CefDestroyBrowser(uint32_t id)
    {
        PluginManager::ApiDestroyBrowser(id);
    }

    __declspec(dllexport) void __cdecl CefHideBrowser(uint32_t id, bool hidden)
    {
        PluginManager::ApiHideBrowser(id, hidden);
    }

    __declspec(dllexport) void __cdecl CefFocusBrowser(uint32_t id, bool focused)
    {
        PluginManager::ApiFocusBrowser(id, focused);
    }
}
