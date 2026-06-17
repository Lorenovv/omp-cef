/*
 * ICefComponent implementation. All methods proxy to CefApi::Instance().
 *
 * CefApi is created during CefPlugin::Initialize (inside CefOmpComponent::onInit),
 * so this extension only works after the plugin is initialized. Calling any
 * method before that is a no-op.
 */

#include "common/api.hpp"
#include "cef_extension_api.hpp"

#include <algorithm>
#include <cstring>
#include <shared/packet.hpp>

namespace
{
    inline Argument ConvertArg(const CefArg& a)
    {
        switch (a.type)
        {
            case CefArgType::String:
                return Argument(std::string(a.stringValue ? a.stringValue : ""));
            case CefArgType::Integer:
                return Argument(a.intValue);
            case CefArgType::Float:
                return Argument(a.floatValue);
            case CefArgType::Bool:
                return Argument(a.boolValue);
        }
        return Argument(0);
    }

    inline std::string Str(const char* s) { return s ? std::string(s) : std::string(); }
}

class CefExtension : public ICefComponent
{
public:
    bool playerHasPlugin(int playerid) override
    {
        auto* api = CefApi::Instance();
        return api ? api->PlayerHasPlugin(playerid) : false;
    }

    void addResource(const char* resourceName) override
    {
        auto* api = CefApi::Instance();
        if (api) api->AddResource(Str(resourceName));
    }

    void createBrowser(int playerid, int browserid, const char* url,
        bool focused, bool controlsChat) override
    {
        auto* api = CefApi::Instance();
        if (api) api->CreateBrowser(playerid, browserid, Str(url), focused, controlsChat);
    }

    void createWorldBrowser(int playerid, int browserid, const char* url,
        const char* textureName, float width, float height) override
    {
        auto* api = CefApi::Instance();
        if (api) api->CreateWorldBrowser(playerid, browserid, Str(url), Str(textureName), width, height);
    }

    void createWorld2DBrowser(int playerid, int browserid, const char* url,
        float worldX, float worldY, float worldZ, float width, float height,
        float offsetZ, float pivotX, float pivotY) override
    {
        auto* api = CefApi::Instance();
        if (api) api->CreateWorld2DBrowser(playerid, browserid, Str(url),
            worldX, worldY, worldZ, width, height, offsetZ, pivotX, pivotY);
    }

    void setWorld2DBrowserPos(int playerid, int browserid, float x, float y, float z) override
    {
        auto* api = CefApi::Instance();
        if (api) api->SetWorld2DBrowserPos(playerid, browserid, x, y, z);
    }

    void setBrowserVisible(int playerid, int browserid, bool visible) override
    {
        auto* api = CefApi::Instance();
        if (api) api->SetBrowserVisible(playerid, browserid, visible);
    }

    void destroyBrowser(int playerid, int browserid) override
    {
        auto* api = CefApi::Instance();
        if (api) api->DestroyBrowser(playerid, browserid);
    }

    void registerEvent(const char* name, const char* callback,
        int typeCount, const CefArgType* types) override
    {
        auto* api = CefApi::Instance();
        if (!api) return;

        std::vector<ArgumentType> sig;
        sig.reserve(typeCount);
        for (int i = 0; i < typeCount; ++i)
        {
            sig.push_back(static_cast<ArgumentType>(types[i]));
        }
        api->RegisterEvent(Str(name), Str(callback), sig);
    }

    void emitEvent(int playerid, int browserid, const char* name,
        int argCount, const CefArg* args) override
    {
        auto* api = CefApi::Instance();
        if (!api) return;

        std::vector<Argument> converted;
        converted.reserve(argCount);
        for (int i = 0; i < argCount; ++i)
        {
            converted.push_back(ConvertArg(args[i]));
        }
        api->EmitEvent(playerid, browserid, Str(name), converted);
    }

    void reloadBrowser(int playerid, int browserid, bool ignoreCache) override
    {
        auto* api = CefApi::Instance();
        if (api) api->ReloadBrowser(playerid, browserid, ignoreCache);
    }

    void focusBrowser(int playerid, int browserid, bool focused) override
    {
        auto* api = CefApi::Instance();
        if (api) api->FocusBrowser(playerid, browserid, focused);
    }

    void loadUrl(int playerid, int browserid, const char* url) override
    {
        auto* api = CefApi::Instance();
        if (api) api->LoadUrl(playerid, browserid, Str(url));
    }

    void enableDevTools(int playerid, int browserid, bool enabled) override
    {
        auto* api = CefApi::Instance();
        if (api) api->EnableDevTools(playerid, browserid, enabled);
    }

    void attachBrowserToObject(int playerid, int browserid, int objectid) override
    {
        auto* api = CefApi::Instance();
        if (api) api->AttachBrowserToObject(playerid, browserid, objectid);
    }

    void detachBrowserFromObject(int playerid, int browserid, int objectid) override
    {
        auto* api = CefApi::Instance();
        if (api) api->DetachBrowserFromObject(playerid, browserid, objectid);
    }

    void setBrowserMuted(int playerid, int browserid, bool muted) override
    {
        auto* api = CefApi::Instance();
        if (api) api->SetBrowserMuted(playerid, browserid, muted);
    }

    void setBrowserAudioMode(int playerid, int browserid, int mode) override
    {
        auto* api = CefApi::Instance();
        if (api) api->SetBrowserAudioMode(playerid, browserid, mode);
    }

    void setBrowserAudioSettings(int playerid, int browserid,
        float maxDistance, float referenceDistance) override
    {
        auto* api = CefApi::Instance();
        if (api) api->SetBrowserAudioSettings(playerid, browserid, maxDistance, referenceDistance);
    }

    void toggleHudComponent(int playerid, int componentid, bool toggle) override
    {
        auto* api = CefApi::Instance();
        if (api) api->ToggleHudComponent(playerid, componentid, toggle);
    }

    void toggleSpawnScreen(int playerid, bool toggle) override
    {
        auto* api = CefApi::Instance();
        if (api) api->ToggleSpawnScreen(playerid, toggle);
    }

    void clearChat(int playerid) override
    {
        auto* api = CefApi::Instance();
        if (api) api->ClearChat(playerid);
    }

    void toggleChatInput(int playerid, bool toggle) override
    {
        auto* api = CefApi::Instance();
        if (api) api->ToggleChatInput(playerid, toggle);
    }

    bool isChatInputOpen(int playerid) override
    {
        auto* api = CefApi::Instance();
        return api ? api->IsChatInputOpen(playerid) : false;
    }

    void setKeyCapture(int playerid, bool enabled) override
    {
        auto* api = CefApi::Instance();
        if (api) api->SetKeyCapture(playerid, enabled);
    }

    void enableKey(int playerid, int key, bool enabled) override
    {
        auto* api = CefApi::Instance();
        if (api) api->EnableKey(playerid, key, enabled);
    }

    void exitGame(int playerid) override
    {
        auto* api = CefApi::Instance();
        if (api) api->ExitGame(playerid);
    }

    void addEventHandler(ICefEventHandler* h) override
    {
        if (!h) return;
        auto& list = CefHandlerList();
        if (std::find(list.begin(), list.end(), h) == list.end())
        {
            list.push_back(h);
        }
    }

    void removeEventHandler(ICefEventHandler* h) override
    {
        auto& list = CefHandlerList();
        list.erase(std::remove(list.begin(), list.end(), h), list.end());
    }

    void setEscapeMenuMode(int playerid, int mode) override
    {
        auto* api = CefApi::Instance();
        if (api) 
            api->SetEscapeMenuMode(playerid, mode);
    }

    void setPlayerListMode(int playerid, int mode) override
    {
        auto* api = CefApi::Instance();
        if (api)
            api->SetPlayerListMode(playerid, mode);
    }
};

static CefExtension g_cefExtension;

ICefComponent* GetCefExtension()
{
    return &g_cefExtension;
}
