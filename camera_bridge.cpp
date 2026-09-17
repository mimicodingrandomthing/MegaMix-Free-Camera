#include "camera_bridge.h"
#include "log.h"
#include <windows.h>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace {
    constexpr uintptr_t GET_POSITION_RVA = 0x002FB960;
    constexpr uintptr_t GET_INTEREST_RVA = 0x002FB980;
    constexpr uintptr_t GET_FOV_RVA      = 0x002FB9D0;
    constexpr uintptr_t GET_NEAR_RVA     = 0x002FB9E0;
    constexpr uintptr_t GET_ROTATION_RVA = 0x002FB860;
    constexpr uintptr_t ROTATION_STATE_RVA = 0x00CC2B5C8;
    constexpr uintptr_t SET_UP_RVA      = 0x002FB7B0;
    constexpr uintptr_t SET_ROLL_RVA    = 0x002FB7A0;
    constexpr uintptr_t PV_ROLL_CALL_RVA = 0x002FAE3A;
    constexpr uintptr_t UP_STATE_RVA   = 0x00CC2B5DC;
    constexpr uintptr_t UP_VALID_RVA   = 0x00CC2B5D8;

    // Camera-state routines in the supplied MegaMix executable.
    // 0x2FB7A0 stores the native PV/A3DA roll angle in degrees.
    // 0x2FB7B0 stores the optional camera up-vector state used by the debug/basis path.


    void protect_write(void* address, const void* bytes, std::size_t size) {
        DWORD oldProtect = 0;
        VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect);
        std::memcpy(address, bytes, size);
        FlushInstructionCache(GetCurrentProcess(), address, size);
        VirtualProtect(address, size, oldProtect, &oldProtect);
    }

    void write_abs_jump(void* address, const void* destination, std::size_t patch_size) {
        unsigned char patch[32]{};
        patch[0] = 0x48; patch[1] = 0xB8;
        const std::uint64_t p = reinterpret_cast<std::uint64_t>(destination);
        std::memcpy(patch + 2, &p, sizeof(p));
        patch[10] = 0xFF; patch[11] = 0xE0; // jmp rax
        for (std::size_t i = 12; i < patch_size; ++i) patch[i] = 0x90;
        protect_write(address, patch, patch_size);
    }

    void write_abs_jump_raw(void* address, const void* destination) {
        unsigned char patch[12]{};
        patch[0] = 0x48; patch[1] = 0xB8;
        const std::uint64_t p = reinterpret_cast<std::uint64_t>(destination);
        std::memcpy(patch + 2, &p, sizeof(p));
        patch[10] = 0xFF; patch[11] = 0xE0;
        protect_write(address, patch, sizeof(patch));
    }

    void* alloc_near(void* target) {
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        const uintptr_t t = reinterpret_cast<uintptr_t>(target);
        const uintptr_t gran = si.dwAllocationGranularity ? si.dwAllocationGranularity : 0x10000;
        const uintptr_t limit = 0x7fffffffULL;
        for (uintptr_t delta = 0; delta <= limit; delta += gran) {
            uintptr_t candidates[2] = {
                (t > delta ? t - delta : 0),
                t + delta
            };
            for (uintptr_t raw : candidates) {
                if (!raw) continue;
                uintptr_t hint = raw & ~(gran - 1);
                void* p = VirtualAlloc(reinterpret_cast<void*>(hint), 0x1000,
                                       MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
                if (!p) continue;
                const uintptr_t got = reinterpret_cast<uintptr_t>(p);
                const std::int64_t rel = static_cast<std::int64_t>(got) - static_cast<std::int64_t>(t);
                if (rel >= INT32_MIN && rel <= INT32_MAX) return p;
                VirtualFree(p, 0, MEM_RELEASE);
            }
            if (delta > limit - gran) break;
        }
        return nullptr;
    }

    bool write_rel_call(void* address, const void* destination) {
        const uintptr_t src = reinterpret_cast<uintptr_t>(address);
        const uintptr_t dst = reinterpret_cast<uintptr_t>(destination);
        const std::int64_t rel = static_cast<std::int64_t>(dst) - static_cast<std::int64_t>(src + 5);
        if (rel < INT32_MIN || rel > INT32_MAX) return false;
        unsigned char patch[5] = {0xE8, 0, 0, 0, 0};
        const std::int32_t disp = static_cast<std::int32_t>(rel);
        std::memcpy(patch + 1, &disp, sizeof(disp));
        protect_write(address, patch, sizeof(patch));
        return true;
    }

    void write_abs_jump_into_relay(void* relay, const void* destination) {
        unsigned char patch[12]{};
        patch[0] = 0x48; patch[1] = 0xB8;
        const std::uint64_t p = reinterpret_cast<std::uint64_t>(destination);
        std::memcpy(patch + 2, &p, sizeof(p));
        patch[10] = 0xFF; patch[11] = 0xE0;
        protect_write(relay, patch, sizeof(patch));
    }

    struct V3 { float x,y,z; };
    static V3 normalize(V3 v) {
        float l = std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
        if (l < 1e-6f) return {0.0f,1.0f,0.0f};
        float s = 1.0f/l; return {v.x*s,v.y*s,v.z*s};
    }
    static V3 cross(V3 a,V3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
    static float dot(V3 a,V3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
    static V3 rotate_axis(V3 v,V3 axis,float angle) {
        axis=normalize(axis);
        float c=std::cos(angle), s=std::sin(angle);
        V3 axv=cross(axis,v);
        float d=dot(axis,v);
        return {v.x*c+axv.x*s+axis.x*d*(1.0f-c),
                v.y*c+axv.y*s+axis.y*d*(1.0f-c),
                v.z*c+axv.z*s+axis.z*d*(1.0f-c)};
    }
}

MegaMixCameraBridge* MegaMixCameraBridge::s_active = nullptr;

extern "C" void MegaMixCameraBridge_HookedUpStub();
extern "C" void MegaMixCameraBridge_HookedPvRollStub();

extern "C" void MegaMixCameraBridge_HookedPvRollImpl(float incoming_degrees) {
    MegaMixCameraBridge* self = MegaMixCameraBridge::s_active;
    float degrees = incoming_degrees;
    if (self && self->m_enabled)
        degrees = self->m_roll_offset * 57.29577951308232f;

    const uintptr_t module = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!module) return;
    *reinterpret_cast<volatile float*>(module + 0x00CC2B5A8) = degrees;
}

extern "C" int MegaMixCameraBridge_HookedCull() {
    // FUN_14045DFC0 is the game's object-frustum visibility wrapper.
    // While freecam is active, bypass the original-camera frustum rejection.
    // The native function's normal results are:
    //   0 = outside, 1 = inside, 2 = intersecting.
    return 1;
}

extern "C" void MegaMixCameraBridge_HookedUpImpl(bool enabled, const float* up) {
    MegaMixCameraBridge* self = MegaMixCameraBridge::s_active;
    if (!up) return;

    // 0x2FB7B0 is a tiny native setter:
    //   [5D8] = CL;
    //   [5DC..5E4] = 3 floats from RDX;
    // There is no matrix work in the function itself. Preserve the v59
    // behavior for enabled up-vector updates: these are the native basis
    // updates that the working free-camera path expects.
    //
    // The important exception is an enabled == false update. During PV/A3DA
    // playback the game can issue one of these as part of its camera teardown
    // / transition. Letting that write [5D8] = 0 and the native PV vector into
    // the shared camera state reintroduces default-camera influence while
    // freecam is active. Ignore that update instead of forcing the up state
    // back to the v60 frozen baseline. This keeps the native 2D/basis path in
    // the same state as v59, while blocking the stray PV reset.
    if (self && self->m_enabled) {
        if (!enabled)
            return;

        self->m_last_up[0] = up[0];
        self->m_last_up[1] = up[1];
        self->m_last_up[2] = up[2];
        self->m_have_up = true;
        self->apply_roll_to_up(up);
        return;
    }

    self ? self->write_up_state(up, enabled) : [&]() {
        const uintptr_t module = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if (!module) return;
        *reinterpret_cast<volatile unsigned char*>(module + UP_VALID_RVA) = enabled ? 1 : 0;
        *reinterpret_cast<volatile std::uint64_t*>(module + UP_STATE_RVA) =
            *reinterpret_cast<const std::uint64_t*>(up);
        *reinterpret_cast<volatile float*>(module + UP_STATE_RVA + 8) = up[2];
    }();
}

float MegaMixCameraBridge::hooked_fov() {
    MegaMixCameraBridge* self = s_active;
    if (self && self->m_enabled)
        return self->m_free_fov;
    return 0.0f;
}

void MegaMixCameraBridge::hooked_rotation(float* x, float* y, float* z, float* roll) {
    // Kept only for ABI/source compatibility with older builds. The rotation
    // getter is deliberately NOT patched: feeding freecam roll through the
    // fourth rotation value caused projection stretching.
    const uintptr_t module = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!module) return;
    const volatile float* r = reinterpret_cast<const volatile float*>(module + ROTATION_STATE_RVA);
    if (x) *x = r[0];
    if (y) *y = r[1];
    if (z) *z = r[2];
    if (roll) *roll = r[3];
}

float MegaMixCameraBridge::hooked_near() {
    MegaMixCameraBridge* self = s_active;
    if (self && self->m_enabled)
        return self->m_free_near;
    return 0.0f;
}

// The A3DA camera-up setter is deliberately NOT hooked in this build.
// Auth3D/A3DA owns the camera basis completely until F8 explicitly enables
// the free camera.

bool MegaMixCameraBridge::install_detour(Detour& d) {
    if(d.installed || !d.target || !d.hook || d.stolen<12 || d.stolen>sizeof(d.original)) return d.installed;
    std::memcpy(d.original,d.target,d.stolen);
    write_abs_jump(d.target,d.hook,d.stolen);
    d.installed=true;
    return true;
}

void MegaMixCameraBridge::remove_detour(Detour& d) {
    if(!d.installed)return;
    protect_write(d.target,d.original,d.stolen);
    d.installed=false;
}

bool MegaMixCameraBridge::patch_scalar_getters() {
    if (!m_valid) return false;
    const uintptr_t module = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!module) return false;

    // The scalar hooks return the freecam values while enabled. They never
    // call the patched entry points, so there is no recursive getter call.
    if (!m_fov_patched) {
        std::memcpy(m_fov_original,
                    reinterpret_cast<const void*>(module + GET_FOV_RVA),
                    sizeof(m_fov_original));
        write_abs_jump(reinterpret_cast<void*>(module + GET_FOV_RVA),
                       reinterpret_cast<const void*>(&MegaMixCameraBridge::hooked_fov),
                       sizeof(m_fov_original));
        m_fov_patched = true;
    }

    if (!m_near_patched) {
        std::memcpy(m_near_original,
                    reinterpret_cast<const void*>(module + GET_NEAR_RVA),
                    sizeof(m_near_original));
        write_abs_jump(reinterpret_cast<void*>(module + GET_NEAR_RVA),
                       reinterpret_cast<const void*>(&MegaMixCameraBridge::hooked_near),
                       sizeof(m_near_original));
        m_near_patched = true;
    }
    return true;
}

void MegaMixCameraBridge::restore_scalar_getters() {
    const uintptr_t module = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!module) return;

    if (m_fov_patched) {
        protect_write(reinterpret_cast<void*>(module + GET_FOV_RVA),
                      m_fov_original, sizeof(m_fov_original));
        m_fov_patched = false;
    }
    if (m_near_patched) {
        protect_write(reinterpret_cast<void*>(module + GET_NEAR_RVA),
                      m_near_original, sizeof(m_near_original));
        m_near_patched = false;
    }
}

bool MegaMixCameraBridge::patch_rotation_getter() {
    // Do not patch 0x2FB860. Its fourth rotation value is not the render-roll
    // channel; using it for Q/E makes the projection stretch.
    return true;
}

void MegaMixCameraBridge::restore_rotation_getter() {
    // Rotation getter intentionally remains native.
}

bool MegaMixCameraBridge::install_detours() {
    if (!m_valid) return false;
    const uintptr_t module = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!module) return false;

    // IMPORTANT: do not detour 0x2FB7B0.
    // This is the native camera-up/basis setter. Earlier builds that
    // intercepted it caused the AET/2D layer to disappear or flicker at
    // particular free-camera angles. The native up-vector path must remain
    // completely game-owned. Q/E roll is handled only through the native
    // PV roll value at 0x2FAE3A below.

    // 0x45DFC0 is the direct object visibility/frustum wrapper.
    // While freecam is active, bypass this test so objects are not rejected
    // using the original game's camera frustum.
    if (!m_cull_detour.installed) {
        m_cull_detour.target = reinterpret_cast<void*>(module + CULL_RVA);
        m_cull_detour.hook = reinterpret_cast<void*>(&MegaMixCameraBridge_HookedCull);
        m_cull_detour.stolen = 12;
        if (!install_detour(m_cull_detour)) {
            return false;
        }
    }

    // A3DA applies its PV roll every time FUN_1402FAE00 runs. The exact
    // CALL at 0x1402FAE3A is only 5 bytes, so patch that call rather than
    // detouring the 8-byte 0x2FB7A0 setter. A nearby relay makes the patch
    // safe even when the DLL itself is more than +/-2 GB from the game.
    if (!m_pv_roll_patched) {
        m_pv_roll_callsite = reinterpret_cast<void*>(module + PV_ROLL_CALL_RVA);
        std::memcpy(m_pv_roll_original, m_pv_roll_callsite, sizeof(m_pv_roll_original));
        const unsigned char expected_call[5] = {0xE8, 0x61, 0x09, 0x00, 0x00};
        if (std::memcmp(m_pv_roll_original, expected_call, sizeof(expected_call)) != 0) {
            Log::write("CameraBridge v61: PV roll callsite signature mismatch at %p", m_pv_roll_callsite);
            remove_detour(m_cull_detour);
            return false;
        }
        m_pv_roll_relay = alloc_near(m_pv_roll_callsite);
        if (!m_pv_roll_relay) {
            remove_detour(m_cull_detour);
            return false;
        }
        write_abs_jump_into_relay(m_pv_roll_relay,
                                   reinterpret_cast<const void*>(&MegaMixCameraBridge_HookedPvRollStub));
        if (!write_rel_call(m_pv_roll_callsite, m_pv_roll_relay)) {
            protect_write(m_pv_roll_callsite, m_pv_roll_original, sizeof(m_pv_roll_original));
            VirtualFree(m_pv_roll_relay, 0, MEM_RELEASE);
            m_pv_roll_relay = nullptr;
            m_pv_roll_callsite = nullptr;
            remove_detour(m_cull_detour);
            return false;
        }
        m_pv_roll_patched = true;
    }
    return true;
}

void MegaMixCameraBridge::restore_pv_roll_callsite() {
    if (!m_pv_roll_patched) return;
    if (m_pv_roll_callsite)
        protect_write(m_pv_roll_callsite, m_pv_roll_original, sizeof(m_pv_roll_original));
    if (m_pv_roll_relay)
        VirtualFree(m_pv_roll_relay, 0, MEM_RELEASE);
    m_pv_roll_relay = nullptr;
    m_pv_roll_callsite = nullptr;
    m_pv_roll_patched = false;
}

bool MegaMixCameraBridge::initialize() {
    const uintptr_t module=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if(!module)return false;
    m_get_position=reinterpret_cast<GetVecFn>(module+GET_POSITION_RVA);
    m_get_interest=reinterpret_cast<GetVecFn>(module+GET_INTEREST_RVA);
    m_get_fov=reinterpret_cast<GetFloatFn>(module+GET_FOV_RVA);
    m_get_near=reinterpret_cast<GetFloatFn>(module+GET_NEAR_RVA);
    // Keep the native rotation getter address available for compatibility.
    m_get_rotation=reinterpret_cast<GetRotationFn>(module+GET_ROTATION_RVA);
    m_set_roll=reinterpret_cast<SetFloatFn>(module+SET_ROLL_RVA);
    std::memcpy(m_pos_original,reinterpret_cast<const void*>(module+GET_POSITION_RVA),sizeof(m_pos_original));
    std::memcpy(m_intr_original,reinterpret_cast<const void*>(module+GET_INTEREST_RVA),sizeof(m_intr_original));
    std::memcpy(m_rotation_original,reinterpret_cast<const void*>(module+GET_ROTATION_RVA),sizeof(m_rotation_original));
    if(!m_get_position||!m_get_interest||!m_get_fov||!m_get_near||!m_get_rotation||!m_set_roll)return false;

    const float* p=m_get_position(); const float* i=m_get_interest();
    if(!p||!i)return false;
    Log::write("CameraBridge v61: pos=%p interest=%p fov=%p near=%p up_hook=disabled",
        (void*)m_get_position,(void*)m_get_interest,(void*)m_get_fov,(void*)m_get_near);
    Log::write("CameraBridge v61: POS=(%.3f,%.3f,%.3f) INTR=(%.3f,%.3f,%.3f) FOV=%.3f NEAR=%.3f",
        p[0],p[1],p[2],i[0],i[1],i[2],m_get_fov(),m_get_near());
    m_valid=true;
    return true;
}

bool MegaMixCameraBridge::patch_getters() {
    if(!m_valid)return false;
    const uintptr_t module=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if(!module)return false;
    if(m_getters_patched)return true;
    // 12-byte mov rax,imm64 / ret, padded with NOPs to the known function sizes.
    unsigned char patch[16]{};
    patch[0]=0x48;patch[1]=0xB8;
    std::uint64_t p=reinterpret_cast<std::uint64_t>(m_free_position);std::memcpy(patch+2,&p,8);patch[10]=0xC3;
    protect_write(reinterpret_cast<void*>(module+GET_POSITION_RVA),patch,sizeof(m_pos_original));
    std::memset(patch,0x90,sizeof(patch));patch[0]=0x48;patch[1]=0xB8;p=reinterpret_cast<std::uint64_t>(m_free_interest);std::memcpy(patch+2,&p,8);patch[10]=0xC3;
    protect_write(reinterpret_cast<void*>(module+GET_INTEREST_RVA),patch,sizeof(m_intr_original));
    m_getters_patched=true;
    s_active=this;
    return true;
}

void MegaMixCameraBridge::restore_getters() {
    if(!m_valid)return;
    const uintptr_t module=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if(!module)return;
    if(m_getters_patched){
        protect_write(reinterpret_cast<void*>(module+GET_POSITION_RVA),m_pos_original,sizeof(m_pos_original));
        protect_write(reinterpret_cast<void*>(module+GET_INTEREST_RVA),m_intr_original,sizeof(m_intr_original));
        m_getters_patched=false;
    }
    if(s_active==this)s_active=nullptr;
}

void MegaMixCameraBridge::write_up_state(const float* up, bool enabled) {
    const uintptr_t module=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if(!module||!up)return;
    volatile float* dst=reinterpret_cast<volatile float*>(module+UP_STATE_RVA);
    dst[0]=up[0];dst[1]=up[1];dst[2]=up[2];
    *reinterpret_cast<volatile unsigned char*>(module+UP_VALID_RVA)=enabled ? 1 : 0;
}

void MegaMixCameraBridge::apply_roll_to_up(const float* incoming) {
    if (!incoming) return;

    V3 f{m_free_interest[0]-m_free_position[0],
         m_free_interest[1]-m_free_position[1],
         m_free_interest[2]-m_free_position[2]};
    f = normalize(f);

    // The native basis builder assumes its reference vector is transverse to
    // forward. Remove any forward component before applying roll so pitch/
    // yaw changes cannot turn the supplied vector into a skewed basis.
    V3 u{incoming[0], incoming[1], incoming[2]};
    u = normalize(u);
    u = normalize({u.x - f.x * dot(u, f),
                   u.y - f.y * dot(u, f),
                   u.z - f.z * dot(u, f)});

    if (std::fabs(m_roll_offset) > 1e-7f)
        u = rotate_axis(u, f, m_roll_offset);
    u = normalize(u);

    float out[3]{u.x, u.y, u.z};
    write_up_state(out, true);
}

void MegaMixCameraBridge::set_enabled(bool enabled) {
    if(!m_valid || enabled==m_enabled) return;
    if(enabled){
        const float* p=m_get_position(); const float* i=m_get_interest();
        if(!p||!i) return;
        std::memcpy(m_free_position,p,sizeof(float)*3);
        std::memcpy(m_free_interest,i,sizeof(float)*3);
        m_free_position[3]=1.0f; m_free_interest[3]=1.0f;
        m_free_fov=m_get_fov(); m_free_near=m_get_near();

        // Leave the native up-vector state completely untouched.
        // It is shared with the AET/2D camera path.
        m_have_up = false;
        m_roll_offset=0.0f;
        s_active=this;
        m_enabled=true;
        m_getters_patched=false;
       if(!patch_getters() || !patch_scalar_getters() || !patch_rotation_getter() || !install_detours()){
            m_enabled=false;
            restore_pv_roll_callsite();
            remove_detour(m_cull_detour);
            restore_rotation_getter();
            restore_scalar_getters();
            restore_getters();
            s_active=nullptr;
            return;
        }
        write(CameraPose{{m_free_position[0],m_free_position[1],m_free_position[2]},
                         {m_free_interest[0],m_free_interest[1],m_free_interest[2]},0,m_free_fov,m_free_near});
        Log::write("CameraBridge v61: FREE CAMERA ENABLED (native up-vector untouched; PV roll callsite override)");
    } else {
        restore_pv_roll_callsite();
        remove_detour(m_cull_detour);
        restore_rotation_getter();
        restore_scalar_getters();
        restore_getters();
        m_enabled=false;
        m_roll_offset=0.0f;
        m_have_up=false;
        Log::write("CameraBridge v61: FREE CAMERA DISABLED (native up-vector never modified)");
    }
}

bool MegaMixCameraBridge::get_current_game_roll(float& roll) const {
    roll=0.0f;
    if(!m_valid) return false;
    if(m_enabled){ roll=m_roll_offset; return true; }
    const uintptr_t module=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if(!module) return false;
    const float* p=m_get_position ? m_get_position() : nullptr;
    const float* i=m_get_interest ? m_get_interest() : nullptr;
    if(!p || !i) return false;
    V3 f{ i[0]-p[0], i[1]-p[1], i[2]-p[2] };
    f=normalize(f);
    V3 worldUp{0.0f,1.0f,0.0f};
    worldUp=normalize({worldUp.x-f.x*dot(worldUp,f), worldUp.y-f.y*dot(worldUp,f), worldUp.z-f.z*dot(worldUp,f)});
    V3 right=normalize(cross(f,worldUp));
    const volatile float* u=reinterpret_cast<const volatile float*>(module+UP_STATE_RVA);
    V3 up{u[0],u[1],u[2]}; up=normalize(up);
    roll=std::atan2(dot(right,up),dot(worldUp,up));
    return true;
}

bool MegaMixCameraBridge::read(CameraPose& out) const {
    if(!m_valid)return false;
    if(m_enabled){out.position={m_free_position[0],m_free_position[1],m_free_position[2]};out.interest={m_free_interest[0],m_free_interest[1],m_free_interest[2]};out.perspective=m_free_fov;out.near_clip=m_free_near;return true;}
    const float*p=m_get_position();const float*i=m_get_interest();if(!p||!i)return false;out.position={p[0],p[1],p[2]};out.interest={i[0],i[1],i[2]};out.perspective=m_get_fov();out.near_clip=m_get_near();return true;
}

bool MegaMixCameraBridge::get_rotation(float&x,float&y,float&z,float&roll) const {x=y=z=0;roll=m_roll_offset;return true;}
void MegaMixCameraBridge::set_rotation(float x,float y,float z,float roll){m_free_rotation[0]=x;m_free_rotation[1]=y;m_free_rotation[2]=z;m_roll_offset=roll;}
void MegaMixCameraBridge::set_roll(float roll){
    if(!m_valid)return;

    m_roll_offset=roll;

    // PV/A3DA path: 0x2FB7A0 stores the roll angle in DEGREES.
    // FUN_1402FC3A0 later converts this value to radians and feeds the
    // native Z-axis rotation helper. Do not patch this function; it is a
    // tiny native setter and is safest to call directly.
    if(m_enabled && m_set_roll)
        m_set_roll(roll * 57.29577951308232f);

    // Do not modify the native up-vector/basis state here. That state is
    // also consumed by the AET/2D camera path, and changing it from the
    // free-camera roll causes 2D elements to flicker/disappear.
    // The PV roll channel above is sufficient for Q/E.
}

bool MegaMixCameraBridge::write(const CameraPose& pose){
    if(!m_valid||!m_enabled)return false;
    m_free_position[0]=pose.position.x;m_free_position[1]=pose.position.y;m_free_position[2]=pose.position.z;
    m_free_interest[0]=pose.interest.x;m_free_interest[1]=pose.interest.y;m_free_interest[2]=pose.interest.z;
    m_free_fov=pose.perspective;m_free_near=pose.near_clip;
    // Do NOT touch 0x2FB830 / 0x14CC2B5C8..5D4. That state is not render roll;
    // writing its fourth value caused the projection stretch observed in v22/v24.
    // PV/A3DA render roll is overridden at the exact A3DA call site
    // 0x2FAE3A; the debug-camera basis path uses the 0x2FB7B0 up-vector state.
    // FOV is supplied through the scalar getter hook while freecam is enabled.
    // Do not write any engine camera globals here. Position/interest are
    // supplied through the freecam getter hooks while enabled; all A3DA-owned
    // camera state remains untouched.
    return true;
}
