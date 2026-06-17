/*
 * CEF public C++ API for open.mp components.
 *
 * Exposes the CefApi functionality as an IExtension so that other components
 * (e.g. SampSharp.Cef bridge) can call into the CEF server without going through
 * the PAWN AMX native table.
 *
 * Consumer pattern:
 *     auto* cefComp = list->queryComponent(UID{0xD9607C6728B33464ULL});
 *     if (cefComp) {
 *         auto* cef = queryExtension<ICefComponent>(cefComp);
 *         cef->createBrowser(playerid, 1, "http://...", true, true);
 *     }
 *
 * All strings are UTF-8 (const char*). For EmitEvent/RegisterEvent we expose a
 * plain tagged-union CefArg[] to keep the FFI flat.
 */

#pragma once

#include <component.hpp>
#include <cstdint>
#include <vector>

#include "common/cef_event_handlers.hpp"

constexpr UID kCefComponentUID = UID(0xD9607C6728B33464ULL);
constexpr UID kCefExtensionUID = UID(0xD9607C6728B33465ULL);

struct ICefComponent : public IExtension
{
    PROVIDE_EXT_UID(kCefExtensionUID)

    // Session status.
    virtual bool playerHasPlugin(int playerid) = 0;

    // Resource management (call during onInit / OnGameModeInit).
    virtual void addResource(const char* resourceName) = 0;

    // Browser lifecycle.
    virtual void createBrowser(int playerid, int browserid, const char* url,
        bool focused, bool controlsChat) = 0;
    virtual void createWorldBrowser(int playerid, int browserid, const char* url,
        const char* textureName, float width, float height) = 0;
    virtual void createWorld2DBrowser(int playerid, int browserid, const char* url,
        float worldX, float worldY, float worldZ, float width, float height,
        float offsetZ, float pivotX, float pivotY) = 0;
    virtual void setWorld2DBrowserPos(int playerid, int browserid,
        float worldX, float worldY, float worldZ) = 0;
    virtual void setBrowserVisible(int playerid, int browserid, bool visible) = 0;
    virtual void destroyBrowser(int playerid, int browserid) = 0;

    // Events (server → JS and JS → server).
    // types[] is a list of CefArgType, one per expected argument in the event.
    virtual void registerEvent(const char* name, const char* callback,
        int typeCount, const CefArgType* types) = 0;
    virtual void emitEvent(int playerid, int browserid, const char* name,
        int argCount, const CefArg* args) = 0;

    // Browser utilities.
    virtual void reloadBrowser(int playerid, int browserid, bool ignoreCache) = 0;
    virtual void focusBrowser(int playerid, int browserid, bool focused) = 0;
    virtual void loadUrl(int playerid, int browserid, const char* url) = 0;
    virtual void enableDevTools(int playerid, int browserid, bool enabled) = 0;

    // World-browser attachment.
    virtual void attachBrowserToObject(int playerid, int browserid, int objectid) = 0;
    virtual void detachBrowserFromObject(int playerid, int browserid, int objectid) = 0;

    // Audio control.
    virtual void setBrowserMuted(int playerid, int browserid, bool muted) = 0;
    virtual void setBrowserAudioMode(int playerid, int browserid, int mode) = 0;
    virtual void setBrowserAudioSettings(int playerid, int browserid,
        float maxDistance, float referenceDistance) = 0;

    // HUD / spawn-screen / chat.
    virtual void toggleHudComponent(int playerid, int componentid, bool toggle) = 0;
    virtual void toggleSpawnScreen(int playerid, bool toggle) = 0;
    virtual void clearChat(int playerid) = 0;
    virtual void toggleChatInput(int playerid, bool toggle) = 0;
    virtual bool isChatInputOpen(int playerid) = 0;

    // Keyboard capture.
    virtual void setKeyCapture(int playerid, bool enabled) = 0;
    virtual void enableKey(int playerid, int key, bool enabled) = 0;

    // Miscellaneous.
    virtual void exitGame(int playerid) = 0;

    // Native GTA ESC/pause menu.
    virtual void setEscapeMenuMode(int playerid, int mode) = 0;

    // Event-handler registry.
    virtual void addEventHandler(ICefEventHandler* handler) = 0;
    virtual void removeEventHandler(ICefEventHandler* handler) = 0;

    void reset() override { /* CEF plugin has its own reset path */ }
};

// Defined in cef_extension_api.cpp. Returns the process-wide singleton that
// CefOmpComponent::getExtension() hands out when asked for ICefComponent.
ICefComponent* GetCefExtension();

// Flat list of currently-registered C++ event handlers (used by plugin.cpp to
// forward events alongside the AMX-public dispatch).
const std::vector<ICefEventHandler*>& GetCefEventHandlers();
