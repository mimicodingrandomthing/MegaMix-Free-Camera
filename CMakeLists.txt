cmake_minimum_required(VERSION 3.20)
project(MegaMixFreeCam LANGUAGES CXX ASM_MASM)

add_library(MegaMixFreeCam SHARED
    plugin.cpp
    log.cpp
    sigscan.cpp
    camera_bridge.cpp
    camera_controller.cpp
    camera_bridge_hook.asm
)

target_compile_features(MegaMixFreeCam PRIVATE cxx_std_17)

target_compile_definitions(MegaMixFreeCam PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    set_property(TARGET MegaMixFreeCam PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()


add_custom_command(TARGET MegaMixFreeCam POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/MegaMixFreeCam.ini"
        "$<TARGET_FILE_DIR:MegaMixFreeCam>/MegaMixFreeCam.ini"
)
