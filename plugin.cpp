#include <windows.h>
#include <cstdint>
#include <cstring>
#include <intrin.h>
#include "camera_controller.h"
#include "camera_bridge.h"
#include "log.h"

namespace {
// The game's native F8 debug-camera path does not need to be involved in our
// free camera.  The important clue from the supplied executable/logs is that
// F8 reaches the mode-1 setter at RVA 0x003D5300 (the v43 build only blocked
// the mode-4 setter at 0x003D52F0, while the log still showed mode=1).
//
// Both tiny entry points are dedicated mode-selection callbacks:
//   0x003D52F0 -> set mode 4
//   0x003D5300 -> set mode 1
// The free camera already replaces the real render-camera getters, so these
// native debug-camera callbacks must simply be inert.  This leaves the game's
// normal camera/UI pipeline alone instead of forcing camera state from A3DA.
constexpr uintptr_t DEBUG_MODE4_CALLBACK_RVA = 0x003D52F0;
constexpr uintptr_t DEBUG_MODE1_CALLBACK_RVA = 0x003D5300;
constexpr uintptr_t DEBUG_ACTIVE_RVA = 0x00CC2C080;
constexpr uintptr_t DEBUG_MODE_RVA   = 0x00CC2C084;

static bool g_native_debug_guard_installed = false;

static void protect_write(void* address, const void* data, std::size_t size) {
    DWORD oldProtect = 0;
    if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect)) return;
    std::memcpy(address, data, size);
    FlushInstructionCache(GetCurrentProcess(), address, size);
    DWORD dummy = 0;
    VirtualProtect(address, size, oldProtect, &dummy);
}

static void InstallNativeDebugCameraGuard() {
    if (g_native_debug_guard_installed) return;
    const uintptr_t module = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!module) return;

    const unsigned char ret = 0xC3;
    protect_write(reinterpret_cast<void*>(module + DEBUG_MODE4_CALLBACK_RVA), &ret, 1);
    protect_write(reinterpret_cast<void*>(module + DEBUG_MODE1_CALLBACK_RVA), &ret, 1);

    // Start clean in case the native debug camera was already active when the
    // DLL initialized.  We do not touch A3DA camera data.
    *reinterpret_cast<volatile unsigned char*>(module + DEBUG_ACTIVE_RVA) = 0;
    *reinterpret_cast<volatile std::int32_t*>(module + DEBUG_MODE_RVA) = 0;

    g_native_debug_guard_installed = true;
    Log::write("MegaMix FreeCamera v61: native DEBUG CAMERA mode-1 + mode-4 callbacks blocked");
}
}


static FreeCameraController g_controller;
static MegaMixCameraBridge g_camera;
static bool g_started = false;

extern "C" __declspec(dllexport)
void Init() {
    if (g_started)
        return;

    g_started = true;

    Log::init();
    Log::write("MegaMix FreeCamera: Init");

    InstallNativeDebugCameraGuard();

    g_camera.initialize();
    g_controller.initialize(&g_camera);

    Log::write("MegaMix FreeCamera: real camera getter bridge ready");
}

extern "C" __declspec(dllexport)
void OnFrame(void*) {
    if (!g_started)
        return;

    g_controller.update();
}
