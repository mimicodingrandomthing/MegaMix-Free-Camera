#pragma once
#include <windows.h>
#include "camera_bridge.h"

class FreeCameraController {
public:
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
    void open_ui();
    void close_ui();
    void reset_pose();
    void handle_state_hotkeys();
    void save_to_disk(int slot, const CameraPose& p);
    bool load_from_disk(int slot, CameraPose& p);
    void apply_orientation(CameraPose& pose);
    void apply_slider(int idx);
    bool game_input_available();
    static bool down(int vk);

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
};
