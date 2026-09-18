#pragma once
#include <cstdint>
#include <cstddef>

extern "C" void MegaMixCameraBridge_HookedUpImpl(bool enabled, const float* up);
extern "C" void MegaMixCameraBridge_HookedPvRollImpl(float incoming_degrees);
extern "C" int MegaMixCameraBridge_HookedCull();
extern "C" void MegaMixCameraBridge_HookedCameraBasisImpl();

struct Vec3 { float x{}, y{}, z{}; };
struct CameraPose {
    Vec3 position{};
    Vec3 interest{};
    float rot_y{};
    float perspective{};
    float near_clip{};
};

class MegaMixCameraBridge {
public:
    bool initialize();
    bool valid() const { return m_valid; }
    bool read(CameraPose& out) const;
    bool get_current_game_roll(float& roll) const;
    bool write(const CameraPose& pose);
    // DIVA stores camera rotation as rot.x, rot.y, rot.z and a separate roll value.
    void set_rotation(float x, float y, float z, float roll);
    void set_roll(float roll);
    bool get_rotation(float& x, float& y, float& z, float& roll) const;
    bool enabled() const { return m_enabled; }
    void set_enabled(bool enabled);

private:
    friend void MegaMixCameraBridge_HookedUpImpl(bool enabled, const float* up);
    friend void MegaMixCameraBridge_HookedPvRollImpl(float incoming_degrees);
    friend void MegaMixCameraBridge_HookedCameraBasisImpl();
    friend int MegaMixCameraBridge_HookedCull();

    using GetVecFn = const float* (*)();
    using GetFloatFn = float (*)();
    using GetRotationFn = void (*)(float*, float*, float*, float*);
    using SetRotationFn = void (*)(float, float, float, float);
    using SetFloatFn = void (*)(float);

    struct Detour {
        void* target = nullptr;
        void* hook = nullptr;
        void* trampoline = nullptr;
        std::size_t stolen = 0;
        unsigned char original[32]{};
        bool installed = false;
    };

    static constexpr uintptr_t CULL_RVA = 0x0045DFC0;
    static constexpr uintptr_t CAMERA_BASIS_CALL_RVA = 0x002FB10E;
    static constexpr uintptr_t CAMERA_BASIS_TARGET_RVA = 0x002FC3A0;

    bool patch_getters();
    void restore_getters();

    bool m_valid = false;
    bool m_enabled = false;
    bool m_getters_patched = false;
    GetVecFn m_get_position = nullptr;
    GetVecFn m_get_interest = nullptr;
    GetFloatFn m_get_fov = nullptr;
    GetFloatFn m_get_near = nullptr;
    GetRotationFn m_get_rotation = nullptr;
    SetRotationFn m_set_rotation = nullptr;
    SetFloatFn m_set_roll = nullptr;

    unsigned char m_pos_original[16]{};
    unsigned char m_intr_original[16]{};
    unsigned char m_fov_original[12]{};
    unsigned char m_near_original[12]{};
    unsigned char m_rotation_original[16]{};

    float m_free_position[4]{};
    float m_free_interest[4]{};
    float m_free_fov = 50.0f;
    float m_free_near = 0.05f;
    // [rot.x, rot.y, rot.z, roll] -- the fourth value is NOT quaternion W.
    float m_free_rotation[4]{0.0f, 0.0f, 0.0f, 0.0f};
    float m_roll_offset = 0.0f;
    float m_last_up[3]{0.0f,1.0f,0.0f};
    bool m_have_up = false;
    Detour m_up_detour{};
    Detour m_cull_detour{};
    void* m_pv_roll_callsite = nullptr;
    void* m_pv_roll_relay = nullptr;
    unsigned char m_pv_roll_original[5]{};
    bool m_pv_roll_patched = false;
    void* m_camera_basis_callsite = nullptr;
    void* m_camera_basis_relay = nullptr;
    unsigned char m_camera_basis_original[5]{};
    bool m_camera_basis_patched = false;

    static MegaMixCameraBridge* s_active;
    static void hooked_rotation(float* x, float* y, float* z, float* roll);
    static float hooked_fov();
    static float hooked_near();

    bool m_fov_patched = false;
    bool m_near_patched = false;
    bool m_rotation_patched = false;
    bool install_detours();
    void restore_pv_roll_callsite();
    bool install_camera_basis_callsite();
    void restore_camera_basis_callsite();
    bool patch_scalar_getters();
    bool patch_rotation_getter();
    void restore_rotation_getter();
    void restore_scalar_getters();
    static bool install_detour(Detour& d);
    static void remove_detour(Detour& d);
    void apply_roll_to_up(const float* incoming);
    void write_up_state(const float* up, bool enabled);
};
