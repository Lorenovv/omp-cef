#include "render_manager.hpp"

#include <windows.h>
#include <new>
#include <string>
#include <vector>

#include "system/logger.hpp"
#include "hooks/hook_manager.hpp"
#include "hooks/d3d9.hpp"
#include "samp/addresses.hpp"

namespace
{
    void LogLoadedD3D9Module()
    {
        HMODULE mod = ::GetModuleHandleA("d3d9.dll");
        if (!mod)
            mod = ::LoadLibraryA("d3d9.dll");

        if (!mod)
            return;

        char path[MAX_PATH] = {};
        ::GetModuleFileNameA(mod, path, MAX_PATH);
        LOG_INFO("[D3D9] d3d9.dll loaded from: {}", path);
    }

    std::string ModulePathForAddress(const void* address)
    {
        MEMORY_BASIC_INFORMATION memory{};
        if (!address || ::VirtualQuery(address, &memory, sizeof(memory)) == 0 || !memory.AllocationBase)
            return "<unknown>";

        char path[MAX_PATH]{};
        if (::GetModuleFileNameA(static_cast<HMODULE>(memory.AllocationBase), path, MAX_PATH) == 0)
            return "<non-module>";

        return path;
    }

    void* ResolveD3D9Export(const char* name)
    {
        HMODULE module = ::GetModuleHandleA("d3d9.dll");
        if (!module)
            module = ::LoadLibraryA("d3d9.dll");

        return module ? reinterpret_cast<void*>(::GetProcAddress(module, name)) : nullptr;
    }

    bool TryGetPresentParameters(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS& out) noexcept
    {
        if (!device)
            return false;

        IDirect3DSwapChain9* swapChain = nullptr;
        HRESULT hr = device->GetSwapChain(0, &swapChain);
        if (FAILED(hr) || !swapChain)
            return false;

        D3DPRESENT_PARAMETERS pp{};
        hr = swapChain->GetPresentParameters(&pp);
        swapChain->Release();

        if (FAILED(hr))
            return false;

        out = pp;
        return true;
    }

    bool IsSystemD3D9Address(void* fn) noexcept
    {
        if (!fn)
            return false;
        HMODULE hD3D9 = ::GetModuleHandleA("d3d9.dll");
        if (!hD3D9)
            return false;
        HMODULE hMod = nullptr;
        if (!::GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(fn),
                &hMod))
            return false;
        return (hMod == hD3D9);
    }



    HWND CreateDummyWindow()
    {
        constexpr const char* className = "omp_cef_d3d9_deferred";

        WNDCLASSEXA wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ::DefWindowProcA;
        wc.hInstance = ::GetModuleHandleA(nullptr);
        wc.lpszClassName = className;
        ::RegisterClassExA(&wc);

        return ::CreateWindowExA(
            WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            className,
            "",
            WS_POPUP,
            -32000, -32000, 1, 1,
            nullptr, nullptr, wc.hInstance, nullptr);
    }

    bool CreateDummyDevice(IDirect3DDevice9** outDevice) noexcept
    {
        if (!outDevice)
            return false;

        *outDevice = nullptr;

        const HWND hwnd = CreateDummyWindow();
        if (!hwnd)
            return false;

        D3DPRESENT_PARAMETERS params{};
        params.Windowed = TRUE;
        params.SwapEffect = D3DSWAPEFFECT_DISCARD;
        params.hDeviceWindow = hwnd;
        params.BackBufferFormat = D3DFMT_UNKNOWN;
        params.BackBufferWidth = 16;
        params.BackBufferHeight = 16;
        params.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

        // 1. Try Direct3DCreate9Ex first - it is never hooked by ENB
        using Create9ExFn = HRESULT(WINAPI*)(UINT, IDirect3D9Ex**);
        const auto create9Ex = reinterpret_cast<Create9ExFn>(ResolveD3D9Export("Direct3DCreate9Ex"));
        if (create9Ex)
        {
            IDirect3D9Ex* directEx = nullptr;
            if (SUCCEEDED(create9Ex(D3D_SDK_VERSION, &directEx)) && directEx)
            {
                IDirect3DDevice9Ex* devEx = nullptr;
                HRESULT hr = directEx->CreateDeviceEx(
                    D3DADAPTER_DEFAULT,
                    D3DDEVTYPE_NULLREF,
                    hwnd,
                    D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                    &params,
                    nullptr,
                    &devEx);
                if (FAILED(hr))
                {
                    hr = directEx->CreateDeviceEx(
                        D3DADAPTER_DEFAULT,
                        D3DDEVTYPE_HAL,
                        hwnd,
                        D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                        &params,
                        nullptr,
                        &devEx);
                }
                directEx->Release();

                if (SUCCEEDED(hr) && devEx)
                {
                    ::DestroyWindow(hwnd);
                    *outDevice = devEx;
                    return true;
                }
            }
        }

        // 2. Fallback to standard Direct3DCreate9
        using Create9Fn = IDirect3D9* (WINAPI*)(UINT);
        const auto create9 = reinterpret_cast<Create9Fn>(ResolveD3D9Export("Direct3DCreate9"));
        if (!create9)
        {
            ::DestroyWindow(hwnd);
            return false;
        }

        IDirect3D9* direct = create9(D3D_SDK_VERSION);
        if (!direct)
        {
            ::DestroyWindow(hwnd);
            return false;
        }

        // Try D3DDEVTYPE_NULLREF first to avoid triggering ENB's HAL hook
        HRESULT result = direct->CreateDevice(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_NULLREF,
            hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            &params,
            outDevice);

        if (FAILED(result) || !*outDevice)
        {
            // Only use HAL if enbseries.asi is NOT loaded to prevent breaking ENB singleton
            if (::GetModuleHandleA("enbseries.asi") == nullptr)
            {
                result = direct->CreateDevice(
                    D3DADAPTER_DEFAULT,
                    D3DDEVTYPE_HAL,
                    hwnd,
                    D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                    &params,
                    outDevice);
            }
        }

        ::DestroyWindow(hwnd);
        direct->Release();
        return SUCCEEDED(result) && *outDevice;
    }

    static void SafeInvokeDraw(uintptr_t fnAddr, void* pThis) noexcept
    {
        if (!fnAddr || !pThis)
            return;

        __try
        {
            using DrawFn = void(__thiscall*)(void*);
            reinterpret_cast<DrawFn>(fnAddr)(pThis);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static bool IsEnbActive() noexcept
    {
        return (::GetModuleHandleA("enbseries.asi") != nullptr ||
                ::GetModuleHandleA("enb.asi") != nullptr ||
                ::GetModuleHandleA("d3d9_enb.dll") != nullptr);
    }
}

bool RenderManager::IsReadablePointer(const void* pointer, size_t size) noexcept
{
    if (!pointer || size == 0)
        return false;

    MEMORY_BASIC_INFORMATION mbi{};
    if (::VirtualQuery(pointer, &mbi, sizeof(mbi)) == 0)
        return false;

    if (mbi.State != MEM_COMMIT)
        return false;

    const DWORD protect = mbi.Protect & 0xFF;
    if (protect == PAGE_NOACCESS || protect == PAGE_GUARD)
        return false;

    const uintptr_t start = reinterpret_cast<uintptr_t>(pointer);
    const uintptr_t end = start + static_cast<uintptr_t>(size);
    const uintptr_t regionStart = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    const uintptr_t regionEnd = regionStart + static_cast<uintptr_t>(mbi.RegionSize);

    return end <= regionEnd;
}

bool RenderManager::Initialize()
{
    if (initialized_)
        return true;

    if (!hooks_)
    {
        LOG_FATAL("RenderManager::Initialize called without HookManager set. Call SetHookManager() first.");
        return false;
    }

    s_self_ = this;

    LogLoadedD3D9Module();

    // Resolving these methods during ASI startup races SA-MP's render proxy.
    // Samp::OnLoaded calls PollD3D after that proxy chain is complete instead.
    LOG_INFO("[D3D9] Waiting for deferred system hook installation.");

    initialized_ = true;
    return true;
}

void RenderManager::Shutdown()
{
    if (!hooks_)
        return;

    hooks_->Uninstall(hookNameReset);
    hooks_->Uninstall(hookNameBeginScene);
    hooks_->Uninstall(hookNameEndScene);
    hooks_->Uninstall(hookNamePresent);

    device_hooks_installed_ = false;
    hooked_reset_addr_ = nullptr;
    hooked_begin_addr_ = nullptr;
    hooked_end_addr_ = nullptr;
    hooked_present_addr_ = nullptr;

    orig_reset_ = nullptr;
    orig_begin_scene_ = nullptr;
    orig_end_scene_ = nullptr;
    orig_present_ = nullptr;

    DetachDevice();

    initialized_ = false;
}

void RenderManager::PollD3D() noexcept
{
    if (!initialized_)
        return;

    const uint64_t now = ::GetTickCount64();
    if (now - last_poll_ms_ < 250)
        return;

    last_poll_ms_ = now;

    // Always render at the lowest Present in the chain. Hooking the live
    // gGameDevice object instead targets samp.dll's proxy method; our callback
    // then runs before SA-MP draws chat and 3D labels. Resolving system methods
    // from a temporary device makes CEF the final backbuffer overlay.
    if (!device_hooks_installed_)
    {
        if (!TryInstallSystemHooks())
            LOG_WARN("[D3D9] Deferred system hook installation failed; will retry.");
        return;
    }

    // The system Present hook captures the underlying real device. Never
    // replace it with SA-MP's outer proxy from gGameDevice.
}

IDirect3DDevice9* RenderManager::UnwrapToSystemD3D9Device(IDirect3DDevice9* dev) noexcept
{
    if (!dev || !IsReadablePointer(dev, sizeof(void*)))
        return nullptr;

    void** vt = *reinterpret_cast<void***>(dev);
    if (vt && IsReadablePointer(vt, sizeof(void*) * 64))
    {
        if (IsSystemD3D9Address(vt[idxPresent]))
            return dev;
    }

    std::vector<IDirect3DDevice9*> to_scan = { dev };

    // Also inspect samp.dll's underlying device pointer at offset 0x26E888
    HMODULE hSamp = ::GetModuleHandleA("samp.dll");
    if (hSamp)
    {
        const uintptr_t sampBase = reinterpret_cast<uintptr_t>(hSamp);
        auto* pSampDev = reinterpret_cast<IDirect3DDevice9**>(sampBase + 0x26E888);
        if (IsReadablePointer(pSampDev, sizeof(void*)) && *pSampDev)
        {
            to_scan.push_back(*pSampDev);
        }
    }

    for (size_t step = 0; step < to_scan.size() && step < 16; ++step)
    {
        IDirect3DDevice9* current = to_scan[step];
        if (!current || !IsReadablePointer(current, 256))
            continue;

        uintptr_t* fields = reinterpret_cast<uintptr_t*>(current);
        // Scan first 64 DWORDs of the object for child pointers
        for (size_t i = 1; i < 64; ++i)
        {
            const uintptr_t candidate = fields[i];
            if (candidate > 0x10000 && candidate < 0x7FFF0000 &&
                IsReadablePointer(reinterpret_cast<void*>(candidate), sizeof(void*)))
            {
                void** candidateVt = *reinterpret_cast<void***>(candidate);
                if (candidateVt && IsReadablePointer(candidateVt, sizeof(void*) * 64))
                {
                    void* presentFn = candidateVt[idxPresent];
                    if (IsSystemD3D9Address(presentFn))
                    {
                        LOG_INFO("[D3D9] Unwrapped device from proxy {} to real d3d9.dll device at {}",
                            static_cast<void*>(current), reinterpret_cast<void*>(candidate));
                        return reinterpret_cast<IDirect3DDevice9*>(candidate);
                    }

                    bool already_queued = false;
                    for (auto* q : to_scan)
                    {
                        if (q == reinterpret_cast<IDirect3DDevice9*>(candidate))
                        {
                            already_queued = true;
                            break;
                        }
                    }
                    if (!already_queued && to_scan.size() < 16)
                    {
                        to_scan.push_back(reinterpret_cast<IDirect3DDevice9*>(candidate));
                    }
                }
            }
        }
    }

    return nullptr;
}

bool RenderManager::TryInstallSystemHooks() noexcept
{
    // 1. Try to unwrap from live game device if it already exists (0xC97C28 in GTA SA)
    constexpr uintptr_t kRwD3D9DeviceAddress = 0xC97C28;
    IDirect3DDevice9* gameDevice = *reinterpret_cast<IDirect3DDevice9**>(kRwD3D9DeviceAddress);
    if (gameDevice && IsReadablePointer(gameDevice, sizeof(void*)))
    {
        IDirect3DDevice9* realDevice = UnwrapToSystemD3D9Device(gameDevice);
        if (realDevice)
        {
            EnsureDeviceHooksInstalled(realDevice);
            if (device_hooks_installed_)
            {
                LOG_INFO("[D3D9] Deferred system hooks installed from unwrapped real d3d9.dll device.");
                return true;
            }
        }
        else if (::GetModuleHandleA("enbseries.asi") == nullptr)
        {
            // Without ENB, gameDevice can be hooked directly as fallback
            EnsureDeviceHooksInstalled(gameDevice);
            if (device_hooks_installed_)
            {
                LOG_INFO("[D3D9] Deferred system hooks installed from live game device (0xC97C28).");
                return true;
            }
        }
    }

    // 2. Try creating dummy device (safe NULLREF or Direct3DCreate9Ex)
    IDirect3DDevice9* dummy{};
    if (CreateDummyDevice(&dummy))
    {
        EnsureDeviceHooksInstalled(dummy);
        dummy->Release();

        if (device_hooks_installed_)
        {
            LOG_INFO("[D3D9] Deferred system hooks installed from dummy device.");
            return true;
        }
    }

    return false;
}

void RenderManager::AttachDevice(IDirect3D9* direct, IDirect3DDevice9* device, const D3DPRESENT_PARAMETERS& params) noexcept
{
    std::scoped_lock lock(device_lock_);

    direct_ = direct;
    device_ = device;
    device_parameters_ = params;

    fast_device_.store(device, std::memory_order_release);
}

void RenderManager::DetachDevice() noexcept
{
    std::scoped_lock lock(device_lock_);

    direct_ = nullptr;
    device_ = nullptr;
    device_parameters_ = {};

    fast_device_.store(nullptr, std::memory_order_release);
    capture_rank_.store(0, std::memory_order_release);
    initialized_device_.store(nullptr, std::memory_order_release);

    reset_status_ = false;
    scene_status_ = false;
}

IDirect3DDevice9* RenderManager::GetDevice() const noexcept
{
    return fast_device_.load(std::memory_order_acquire);
}

bool RenderManager::GetScreenSize(float& width, float& height) const noexcept
{
    std::scoped_lock lock(device_lock_);
    if (!device_)
        return false;

    if (device_parameters_.BackBufferWidth == 0 || device_parameters_.BackBufferHeight == 0)
        return false;

    width = static_cast<float>(device_parameters_.BackBufferWidth);
    height = static_cast<float>(device_parameters_.BackBufferHeight);
    return true;
}

bool RenderManager::ConvertBaseXValueToScreenXValue(float base, float& screen) const noexcept
{
    std::scoped_lock lock(device_lock_);
    if (!device_ || device_parameters_.BackBufferWidth == 0)
        return false;

    screen = (static_cast<float>(device_parameters_.BackBufferWidth) / kBaseWidth) * base;
    return true;
}

bool RenderManager::ConvertBaseYValueToScreenYValue(float base, float& screen) const noexcept
{
    std::scoped_lock lock(device_lock_);
    if (!device_ || device_parameters_.BackBufferHeight == 0)
        return false;

    screen = (static_cast<float>(device_parameters_.BackBufferHeight) / kBaseHeight) * base;
    return true;
}

bool RenderManager::ConvertScreenXValueToBaseXValue(float screen, float& base) const noexcept
{
    std::scoped_lock lock(device_lock_);
    if (!device_ || device_parameters_.BackBufferWidth == 0)
        return false;

    base = (kBaseWidth / static_cast<float>(device_parameters_.BackBufferWidth)) * screen;
    return true;
}

bool RenderManager::ConvertScreenYValueToBaseYValue(float screen, float& base) const noexcept
{
    std::scoped_lock lock(device_lock_);
    if (!device_ || device_parameters_.BackBufferHeight == 0)
        return false;

    base = (kBaseHeight / static_cast<float>(device_parameters_.BackBufferHeight)) * screen;
    return true;
}

bool RenderManager::IsGameDeviceCandidate(const D3DPRESENT_PARAMETERS* pp, HWND presentHwnd) const noexcept
{
    // If we know the game hwnd, we filter strictly to avoid capturing overlay/helper devices.
    if (!game_hwnd_)
        return true;

    DWORD wpid = 0;
    ::GetWindowThreadProcessId(game_hwnd_, &wpid);
    if (wpid != ::GetCurrentProcessId())
        return true;

    if (presentHwnd && presentHwnd == game_hwnd_)
        return true;

    if (pp && pp->hDeviceWindow && pp->hDeviceWindow == game_hwnd_)
        return true;

    return false;
}

bool RenderManager::TryCaptureDeviceFromPresent(IDirect3DDevice9* device, HWND presentHwnd) noexcept
{
    if (!device)
        return false;

    IDirect3DDevice9* current = fast_device_.load(std::memory_order_acquire);
    const int current_rank = capture_rank_.load(std::memory_order_acquire);

    // Already the confirmed game device
    if (current == device && current_rank >= 2)
        return false;

    D3DPRESENT_PARAMETERS pp{};
    (void)TryGetPresentParameters(device, pp);

    if (!IsGameDeviceCandidate(&pp, presentHwnd))
        return false;

    // Upgrade / switch to this device
    if (current && current != device)
    {
        if (OnDeviceDestroy)
            OnDeviceDestroy();
    }

    AttachDevice(nullptr, device, pp);
    capture_rank_.store(2, std::memory_order_release);

    IDirect3DDevice9* prevInit = initialized_device_.exchange(device, std::memory_order_acq_rel);
    if (prevInit != device)
    {
        if (OnDeviceInitialize)
            OnDeviceInitialize(nullptr, device, pp);
    }

    if (current && current != device)
        LOG_WARN("[D3D9] Game device upgraded via Present: old={}, new={}, hwnd={}", (void*)current, (void*)device, (void*)presentHwnd);

    LOG_INFO("[D3D9] Captured device via Present: device={}, hwnd={}", (void*)device, (void*)presentHwnd);
    return true;
}

void RenderManager::EnsureDeviceHooksInstalled(IDirect3DDevice9* device) noexcept
{
    if (!device || !hooks_)
        return;

    void** vtable = *reinterpret_cast<void***>(device);
    if (!vtable || !IsReadablePointer(vtable, sizeof(void*) * 64))
        return;

    void* addrReset = vtable[idxReset];
    void* addrPresent = vtable[idxPresent];
    void* addrBegin = vtable[idxBeginScene];
    void* addrEnd = vtable[idxEndScene];

    if (!addrReset || !addrPresent || !addrBegin || !addrEnd)
        return;

    std::scoped_lock lock(hook_lock_);

    // Same targets already hooked
    if (device_hooks_installed_ &&
        addrReset == hooked_reset_addr_ &&
        addrBegin == hooked_begin_addr_ &&
        addrEnd == hooked_end_addr_ &&
        addrPresent == hooked_present_addr_)
    {
        return;
    }

    device_hooks_installed_ = false;

    if (!hooks_->Install(hookNameReset, addrReset, reinterpret_cast<void*>(&RenderManager::hkReset)))
        return;

    if (!hooks_->Install(hookNameBeginScene, addrBegin, reinterpret_cast<void*>(&RenderManager::hkBeginScene)))
    {
        hooks_->Uninstall(hookNameReset);
        return;
    }

    if (!hooks_->Install(hookNameEndScene, addrEnd, reinterpret_cast<void*>(&RenderManager::hkEndScene)))
    {
        hooks_->Uninstall(hookNameBeginScene);
        hooks_->Uninstall(hookNameReset);
        return;
    }

    if (!hooks_->Install(hookNamePresent, addrPresent, reinterpret_cast<void*>(&RenderManager::hkPresent)))
    {
        hooks_->Uninstall(hookNameEndScene);
        hooks_->Uninstall(hookNameBeginScene);
        hooks_->Uninstall(hookNameReset);
        return;
    }

    hooked_reset_addr_ = addrReset;
    hooked_begin_addr_ = addrBegin;
    hooked_end_addr_ = addrEnd;
    hooked_present_addr_ = addrPresent;

    orig_reset_ = reinterpret_cast<Reset_t>(hooks_->GetOriginal(hookNameReset));
    orig_begin_scene_ = reinterpret_cast<BeginScene_t>(hooks_->GetOriginal(hookNameBeginScene));
    orig_end_scene_ = reinterpret_cast<EndScene_t>(hooks_->GetOriginal(hookNameEndScene));
    orig_present_ = reinterpret_cast<Present_t>(hooks_->GetOriginal(hookNamePresent));

    device_hooks_installed_ = true;

    LOG_INFO("[D3D9] Hooks installed: Reset={}, BeginScene={}, EndScene={}, Present={}",
        addrReset, addrBegin, addrEnd, addrPresent);
    LOG_INFO("[D3D9] Present target owner: {}", ModulePathForAddress(addrPresent));
}

HRESULT __stdcall RenderManager::hkReset(IDirect3DDevice9* self, D3DPRESENT_PARAMETERS* pp)
{
    auto* rm = s_self_;
    if (!rm || !rm->orig_reset_)
        return D3DERR_INVALIDCALL;

    const bool isGameDevice = (self == rm->fast_device_.load(std::memory_order_acquire));

    if (isGameDevice)
    {
        rm->reset_status_ = true;
        if (rm->OnBeforeReset)
            rm->OnBeforeReset();
    }

    HRESULT hr = rm->orig_reset_(self, pp);

    if (isGameDevice)
    {
        rm->reset_status_ = false;

        if (SUCCEEDED(hr) && pp)
        {
            std::scoped_lock lock(rm->device_lock_);
            rm->device_parameters_ = *pp;
        }

        if (SUCCEEDED(hr) && rm->OnAfterReset)
        {
            D3DPRESENT_PARAMETERS copy{};
            {
                std::scoped_lock lock(rm->device_lock_);
                copy = rm->device_parameters_;
            }
            rm->OnAfterReset(self, copy);
        }
    }

    return hr;
}

HRESULT __stdcall RenderManager::hkBeginScene(IDirect3DDevice9* self)
{
    auto* rm = s_self_;
    if (!rm || !rm->orig_begin_scene_)
        return D3DERR_INVALIDCALL;

    const bool isGameDevice = (self == rm->fast_device_.load(std::memory_order_acquire));

    if (isGameDevice)
    {
        rm->scene_status_ = true;
        if (rm->OnBeforeBeginScene)
            rm->OnBeforeBeginScene();
    }

    HRESULT hr = rm->orig_begin_scene_(self);

    if (isGameDevice)
    {
        if (rm->OnAfterBeginScene)
            rm->OnAfterBeginScene();
    }

    return hr;
}

HRESULT __stdcall RenderManager::hkEndScene(IDirect3DDevice9* self)
{
    auto* rm = s_self_;
    if (!rm || !rm->orig_end_scene_)
        return D3DERR_INVALIDCALL;

    const bool isGameDevice = (self == rm->fast_device_.load(std::memory_order_acquire));

    if (isGameDevice)
    {
        if (rm->OnBeforeEndScene)
            rm->OnBeforeEndScene();
    }

    HRESULT hr = rm->orig_end_scene_(self);

    if (isGameDevice)
    {
        rm->scene_status_ = false;
        if (rm->OnAfterEndScene)
            rm->OnAfterEndScene();
    }

    return hr;
}

void RenderManager::RedrawSampUI(IDirect3DDevice9* device) noexcept
{
    if (!device)
        return;

    // Only redraw when Boris Vorontsov ENB is active and overwrites the backbuffer in Present
    if (!IsEnbActive())
        return;

    HMODULE hSamp = ::GetModuleHandleA("samp.dll");
    if (!hSamp)
        return;

    const uintptr_t sampBase = reinterpret_cast<uintptr_t>(hSamp);

    static bool loggedEnb = false;
    if (!loggedEnb)
    {
        loggedEnb = true;
        LOG_INFO("[D3D9] ENB active: SA-MP UI redraw enabled in Present (chat, input, dialogs)");
    }

    // Determine offsets based on SA-MP version
    auto version = SampAddresses::Instance().Version();
    uintptr_t pChatOffset = 0x26E8C8;
    uintptr_t fnChatDrawOffset = 0x67680;
    uintptr_t pInputOffset = 0x26E8CC;
    uintptr_t fnInputDrawOffset = 0x68F80;
    uintptr_t pDialogOffset = 0x26E898;
    uintptr_t fnDialogDrawOffset = 0x6F140;
    uintptr_t pDeathOffset = 0x26E8D0;
    uintptr_t fnDeathDrawOffset = 0x69B70;
    uintptr_t pScoreboardOffset = 0x26E89C;
    uintptr_t fnScoreboardDrawOffset = 0x713D0;

    switch (version)
    {
        case SampVersion::V037: // 0.3.7-R1
            pChatOffset = 0x21A0E4;
            fnChatDrawOffset = 0x64230;
            pInputOffset = 0x21A0E8;
            fnInputDrawOffset = 0x65A50;
            pDialogOffset = 0x21A0B8;
            fnDialogDrawOffset = 0x6B240;
            pDeathOffset = 0x21A0EC;
            fnDeathDrawOffset = 0x66640;
            pScoreboardOffset = 0x21A0B4;
            fnScoreboardDrawOffset = 0x6AA10;
            break;

        case SampVersion::V037R3: // 0.3.7-R3
            pChatOffset = 0x26E8C8;
            fnChatDrawOffset = 0x67680;
            pInputOffset = 0x26E8CC;
            fnInputDrawOffset = 0x68F80;
            pDialogOffset = 0x26E898;
            fnDialogDrawOffset = 0x6F140;
            pDeathOffset = 0x26E8D0;
            fnDeathDrawOffset = 0x69B70;
            pScoreboardOffset = 0x26E89C;
            fnScoreboardDrawOffset = 0x713D0;
            break;

        case SampVersion::V037R5: // 0.3.7-R5
            pChatOffset = 0x26EB80;
            fnChatDrawOffset = 0x67E00;
            pInputOffset = 0x26EB84;
            fnInputDrawOffset = 0x696F0;
            pDialogOffset = 0x26EB50;
            fnDialogDrawOffset = 0x6F890;
            pDeathOffset = 0x26EB88;
            fnDeathDrawOffset = 0x6A2E0;
            pScoreboardOffset = 0x26EB4C;
            fnScoreboardDrawOffset = 0x6F0B0;
            break;

        case SampVersion::V03DLR1: // 0.3.DL
            pChatOffset = 0x2ACA10;
            fnChatDrawOffset = 0x67870;
            pInputOffset = 0x2ACA14;
            fnInputDrawOffset = 0x69130;
            pDialogOffset = 0x2AC9E0;
            fnDialogDrawOffset = 0x6F2D0;
            pDeathOffset = 0x26EB88;
            fnDeathDrawOffset = 0x6A2E0;
            pScoreboardOffset = 0x2AC9DC;
            fnScoreboardDrawOffset = 0x6EAF0;
            break;

        default:
            break;
    }

    // Save full D3D9 state
    IDirect3DStateBlock9* stateBlock = nullptr;
    device->CreateStateBlock(D3DSBT_ALL, &stateBlock);

    // Ensure BackBuffer is RenderTarget 0
    IDirect3DSurface9* backBuffer = nullptr;
    if (SUCCEEDED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer)))
    {
        device->SetRenderTarget(0, backBuffer);

        D3DSURFACE_DESC desc{};
        if (SUCCEEDED(backBuffer->GetDesc(&desc)))
        {
            D3DVIEWPORT9 vp{ 0, 0, desc.Width, desc.Height, 0.0f, 1.0f };
            device->SetViewport(&vp);
        }

        backBuffer->Release();
    }

    // Clear shaders left behind by ENB's post-processing quad
    device->SetPixelShader(nullptr);
    device->SetVertexShader(nullptr);

    // In D3D9, sprite and primitive rendering requires an active scene
    bool beganScene = false;
    if (SUCCEEDED(device->BeginScene()))
    {
        beganScene = true;
    }

    // 1. Draw Chat
    if (pChatOffset && fnChatDrawOffset)
    {
        void** ppChat = reinterpret_cast<void**>(sampBase + pChatOffset);
        if (IsReadablePointer(ppChat, sizeof(void*)) && *ppChat && IsReadablePointer(*ppChat, sizeof(void*)))
        {
            SafeInvokeDraw(sampBase + fnChatDrawOffset, *ppChat);
        }
    }

    // 2. Draw Input Box
    if (pInputOffset && fnInputDrawOffset)
    {
        void** ppInput = reinterpret_cast<void**>(sampBase + pInputOffset);
        if (IsReadablePointer(ppInput, sizeof(void*)) && *ppInput && IsReadablePointer(*ppInput, sizeof(void*)))
        {
            SafeInvokeDraw(sampBase + fnInputDrawOffset, *ppInput);
        }
    }

    // 3. Draw Death Window (killfeed)
    if (pDeathOffset && fnDeathDrawOffset)
    {
        void** ppDeath = reinterpret_cast<void**>(sampBase + pDeathOffset);
        if (IsReadablePointer(ppDeath, sizeof(void*)) && *ppDeath && IsReadablePointer(*ppDeath, sizeof(void*)))
        {
            SafeInvokeDraw(sampBase + fnDeathDrawOffset, *ppDeath);
        }
    }

    // 4. Draw Dialog
    if (pDialogOffset && fnDialogDrawOffset)
    {
        void** ppDialog = reinterpret_cast<void**>(sampBase + pDialogOffset);
        if (IsReadablePointer(ppDialog, sizeof(void*)) && *ppDialog && IsReadablePointer(*ppDialog, sizeof(void*)))
        {
            SafeInvokeDraw(sampBase + fnDialogDrawOffset, *ppDialog);
        }
    }

    // 5. Draw Scoreboard
    if (pScoreboardOffset && fnScoreboardDrawOffset)
    {
        void** ppScoreboard = reinterpret_cast<void**>(sampBase + pScoreboardOffset);
        if (IsReadablePointer(ppScoreboard, sizeof(void*)) && *ppScoreboard && IsReadablePointer(*ppScoreboard, sizeof(void*)))
        {
            SafeInvokeDraw(sampBase + fnScoreboardDrawOffset, *ppScoreboard);
        }
    }

    if (beganScene)
    {
        device->EndScene();
    }

    if (stateBlock)
    {
        stateBlock->Apply();
        stateBlock->Release();
    }
}

HRESULT __stdcall RenderManager::hkPresent(IDirect3DDevice9* self, const RECT* src, const RECT* dst, HWND wnd, const RGNDATA* dirty)
{
    auto* rm = s_self_;
    if (!rm || !rm->orig_present_)
        return D3DERR_INVALIDCALL;

    // Capture game device on first valid Present()
    rm->TryCaptureDeviceFromPresent(self, wnd);

    const bool isGameDevice = (self == rm->fast_device_.load(std::memory_order_acquire));

    if (isGameDevice)
    {
        rm->RedrawSampUI(self);

        if (rm->OnPresent)
            rm->OnPresent();
    }

    return rm->orig_present_(self, src, dst, wnd, dirty);
}
