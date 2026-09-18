#pragma once
#include <windows.h>
#include <string>
#include "camera_bridge.h"

class FreeCameraController {
public:
    enum class KeyAction : int {
        ToggleFreeCam = 0,
        ToggleMouseLook,
        ToggleAllLock,
        ToggleUI,
        ResetPose,
        GetGameCamera,
        DefaultCamera,
        ApplyUI,

        MoveForward,
        MoveBackward,
        MoveLeft,
        MoveRight,
        MoveUp,
        MoveDown,

        RollLeft,
        RollRight,
        LookAtDecrease,
        LookAtIncrease,
        FovDecrease,
        FovIncrease,
        SensitivityDecrease,
        SensitivityIncrease,
        FastModifier,
        SlowModifier,

        LoadSlot1,
        LoadSlot2,
        LoadSlot3,
        LoadSlot4,
        LoadSlot5,
        LoadSlot6,
        LoadSlot7,
        LoadSlot8,
        LoadSlot9,

        SaveSlot1,
        SaveSlot2,
        SaveSlot3,
        SaveSlot4,
        SaveSlot5,
        SaveSlot6,
        SaveSlot7,
        SaveSlot8,
        SaveSlot9,

        Count
    };

    struct KeyChord {
        int vk = 0;
        bool ctrl = false;
        bool alt = false;
        bool shift = false;
    };

    struct KeyBinding {
        KeyChord chords[4]{};
        int count = 0;
    };

    static constexpr int KEY_ACTION_COUNT = static_cast<int>(KeyAction::Count);

    void initialize(MegaMixCameraBridge* bridge);
    void update();
    void apply_ui();
    void refresh_ui();
    void refresh_ui_live();
    void save_slot(int slot);
    void load_slot(int slot);
    void default_position();
    void reset_to_game_position();

public:
    void toggle();
    void update_freecam();
    void update_mouse(CameraPose& pose);
    void update_roll(CameraPose& pose);
    void update_focal(CameraPose& pose);
    void update_fov(CameraPose& pose);
    void update_sensitivity_hotkeys();
    void open_ui();
    void close_ui();
    void reset_pose();
    void handle_state_hotkeys();
    void save_to_disk(int slot, const CameraPose& p);
    bool load_from_disk(int slot, CameraPose& p);
    void apply_orientation(CameraPose& pose);
    void apply_slider(int idx);
    bool game_input_available();
    void load_keybinds();
    void update_binding_states();
    bool action_down(KeyAction action) const;
    bool action_pressed(KeyAction action) const;
    std::string binding_text(KeyAction action) const;
    void update_ui_key_labels();

    MegaMixCameraBridge* m_bridge = nullptr;
    bool m_enabled = false;
    bool m_input_locked = false;
    bool m_mouse_locked = false;
    bool m_have_center = false;
    POINT m_center{};
    CameraPose m_home{};
    HWND m_ui = nullptr;
    HWND m_edits[11]{};
    HWND m_focal_edit = nullptr;
    HWND m_sliders[6]{}; // yaw, pitch, roll, FOV, near, focal
    HWND m_sensitivity_slider = nullptr;
    HWND m_sensitivity_label = nullptr;
    HWND m_status = nullptr;
    HWND m_hotkey_summary = nullptr;

    // UI controls whose text contains configurable key names.
    HWND m_value_labels[12]{}; // 0..10 camera fields, 11 focal distance
    HWND m_sensitivity_title = nullptr;
    HWND m_apply_button = nullptr;
    HWND m_reset_game_button = nullptr;
    HWND m_default_button = nullptr;
    HWND m_load_label = nullptr;
    HWND m_save_label = nullptr;
    HWND m_load_buttons[9]{};
    HWND m_save_buttons[9]{};

    CameraPose m_slots[9]{};
    bool m_slot_valid[9]{};
    float m_focal_distance = 1.0f;
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
    float m_roll = 0.0f;
    float m_game_roll_capture = 0.0f;
    float m_sensitivity = 1.0f;
    float m_base_fov = 32.267342f;
    float m_rotation_x = 0.0f;
    float m_rotation_y = 0.0f;
    float m_rotation_z = 0.0f;
    float m_rotation_w = 0.0f;
    HWND m_game_window = nullptr;
    bool m_all_locked = false;
    bool m_sync_fov_near = false;

    KeyBinding m_keybinds[KEY_ACTION_COUNT]{};
    bool m_binding_down[KEY_ACTION_COUNT]{};
    bool m_binding_pressed[KEY_ACTION_COUNT]{};
};
