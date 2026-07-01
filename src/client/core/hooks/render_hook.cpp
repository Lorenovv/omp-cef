#include "render_hook.hpp"
#include "hook_manager.hpp"
#include "browser/manager.hpp"
#include "system/logger.hpp"

#include <game_sa/CEntity.h>

namespace {
    static constexpr uintptr_t kCEntity_Render_Addr = 0x534310;
}

void __fastcall RenderHook::hkCEntity_Render(CEntity* pThis, void* /*_edx*/)
{
    // [DIAG 2026-07-01] Per-entity CEF texture-swap disabled to test the driving-freeze.
    // This hook runs on EVERY world entity EVERY frame; while driving the entity count
    // explodes and this path is the prime suspect for the ~2 min freeze. No browser is
    // rendered onto an in-world 3D surface, so OnBeforeEntityRender/OnAfterEntityRender
    // are not needed. To restore the feature, re-add the swap/restore calls:
    //     auto* self = s_self_;
    //     if (self) {
    //         self->browser_.OnBeforeEntityRender(pThis);
    //         s_original_(pThis);
    //         self->browser_.OnAfterEntityRender(pThis);
    //     } else {
    //         s_original_(pThis);
    //     }
    s_original_(pThis);
}

bool RenderHook::Initialize()
{
    s_self_ = this;

    void* target = reinterpret_cast<void*>(kCEntity_Render_Addr);
    if (!hooks_.Install("RenderHook::CEntity_Render", target, reinterpret_cast<void*>(&hkCEntity_Render))) {
        LOG_FATAL("Failed to install CEntity::Render hook.");
        return false;
    }

    s_original_ = reinterpret_cast<CEntity_Render_t>(hooks_.GetOriginal("RenderHook::CEntity_Render"));
    if (!s_original_) {
        LOG_FATAL("Failed to get original CEntity::Render pointer.");
        return false;
    }

    LOG_DEBUG("CEntity::Render hook installed successfully.");
    return true;
}

void RenderHook::Shutdown()
{
    hooks_.Uninstall("RenderHook::CEntity_Render");
    s_original_ = nullptr;
    s_self_ = nullptr;
    LOG_DEBUG("CEntity::Render hook uninstalled.");
}
