# MegaMix-Free-Camera
Free camera mod for Hatsune Miku: Project DIVA MegaMix+, developed as a reverse-engineering and C++ learning project.
MegaMix Free Camera

Introduction

I’m a coding student, and I was introduced to MegaMix as a learning experience. I wanted to understand how a real game works internally, so I started exploring its camera system and reverse engineering it with Ghidra.

I used projects like reDIVA, PD Loader, and AFTMods/DivaGL as references, then traced MegaMix's own functions and data until I was able to build a working free camera.

This project was mainly a learning experience. A lot of it involved trial and error, debugging, and figuring out what the game was actually doing internally.

Features
Free camera movement
Camera position control
Camera look-at / interest control
Yaw and pitch control
Roll control with Q / E
FOV control
Near clip control
Mouse camera control
Frustum culling bypass
PV camera override while free camera is enabled
Original camera behavior restored when disabled
Debug camera controls
Camera values can be edited through the UI
How It Works

The main challenge was finding where MegaMix actually builds its camera transform.

Using Ghidra, I followed camera-related functions and their XREFs. The important call chain ended up being:

FUN_1402FB0F0
    └── FUN_1402FC3A0

FUN_1402FC3A0 builds the camera basis and view transform.

During the investigation, I found that it directly reads the native camera position and interest values:

14CC2B590 = Position X
14CC2B594 = Position Y
14CC2B598 = Position Z

14CC2B59C = Interest X
14CC2B5A0 = Interest Y
14CC2B5A4 = Interest Z

This was important because hooking the camera getter functions alone was not enough. The renderer was still using the native camera globals directly.

The solution was to temporarily replace those values while the original camera builder runs:

save original camera
        ↓
write free camera position/interest
        ↓
run original camera builder
        ↓
restore original camera

This allows MegaMix's original camera code to calculate the camera normally while using the free-camera values.

The native camera state also contains values for things such as:

position
interest
roll
up vector

The free camera keeps its own state and only injects the necessary values into the original camera system.

Roll

Roll was investigated separately.

The relevant code contains a call at:

1402FAE3A

which calls the small roll-setting function at:

1402FB7A0

The function itself is extremely small:

1402FB7A0  MOVSS [14CC2B5A8], XMM0
1402FB7A8  RET

Because the function is only a few bytes long, hooking the function directly would not be safe.

Instead, the call site was hooked:

1402FAE3A
CALL 1402FB7A0

This allows the original game behavior to remain intact when free camera is disabled while allowing the free camera to provide its own roll when enabled.

Culling

Once the camera could move freely, another problem appeared: objects outside the normal camera view were being removed by the game's frustum culling.

The relevant culling code was found in:

FUN_1402FBCB0

and its caller:

FUN_14045DFC0

The free camera bypasses this culling check so that objects can still be rendered when the camera moves outside the normal PV view.

Important Camera Functions

Some of the functions identified during the investigation:

FUN_1402FB0F0    Camera update
FUN_1402FB410    Projection/frustum related calculations
FUN_1402FC3A0    Camera basis/view transform
FUN_1402FB760    Position setter
FUN_1402FB780    Interest setter
FUN_1402FB7A0    Roll setter
FUN_1402FB7B0    Camera state / up-vector related setter
FUN_1402FB960    Position getter
FUN_1402FB980    Interest getter
FUN_1402FB9D0    FOV getter
FUN_1402FB9E0    Near clip getter
FUN_1402FB860    Rotation getter
FUN_14045DFC0    Frustum culling caller
FUN_1402FBCB0    Frustum culling test

These addresses are specific to the MegaMix executable version used during development and may change between game versions.

Camera Data

The main native camera values identified were:

14CC2B590    Position X
14CC2B594    Position Y
14CC2B598    Position Z

14CC2B59C    Interest X
14CC2B5A0    Interest Y
14CC2B5A4    Interest Z

14CC2B5A8    Roll

Other relevant state:

14CC2B5C8    Rotation state
14CC2B5DC    Up-vector state
Look-at Distance

The UI calls this value Look-at Distance.

It is not an optical camera focal length.

It represents the distance between the camera position and its interest point.

Conceptually:

direction = camera direction
interest = position + direction * distance

Changing the value therefore moves the look-at point farther or closer along the current viewing direction.

Development

This project involved a lot of debugging and reverse engineering.

Some of the problems encountered included:

PV camera overriding the free camera
2D/AET elements flickering
2D elements disappearing at certain camera angles
3D rendering becoming stretched or corrupted after incorrect camera hooks
Q/E roll controls breaking after changes
UI values reverting after pressing Apply
UI font rendering problems
UI flickering
Live camera updates overwriting manually entered values

A lot of the debugging process was basically:

change one thing
    ↓
build
    ↓
test in game
    ↓
something else breaks
    ↓
go back to Ghidra
    ↓
check XREFs
    ↓
check assembly
    ↓
change it again

The hardest part was often not writing the hook itself, but finding out which part of the game's camera system was actually responsible for the problem.

For example, the camera initially appeared to be controlled through the obvious getter/setter functions. It took further tracing to discover that the actual camera transform code was directly reading the native camera globals.

The final solution was much simpler once that data flow was understood.

Credits / References

This project would not have been possible without the existing DIVA reverse-engineering and modding work.

Special thanks to the developers and contributors of:

reDIVA
PD Loader
AFTMods / DivaGL

These projects were used as references for understanding the DIVA engine, camera systems, function patterns, and reverse-engineering approaches.

The MegaMix addresses and behavior in this project were independently investigated and confirmed using Ghidra.

Tools

Main tools used during development:

Ghidra
Visual Studio / MSVC
CMake
C++
x64 assembly
Git
Disclaimer

This project is an unofficial modification for educational and research purposes.

It is not affiliated with or endorsed by SEGA, Crypton Future Media, or the developers of Project DIVA.

Use the mod at your own risk and make backups of your game files.

Version Compatibility

The camera addresses used by this project are tied to a specific MegaMix executable version.

Game updates may change:

Function addresses
Global addresses
Call sites
Assembly layout
Camera structures

If the game is updated, the hooks may need to be located again using the reverse-engineering approach described above.

Status

The free camera is currently functional, including camera movement, rotation, roll, FOV, culling bypass, and the camera override system.

The project is primarily intended as a learning/reverse-engineering project and may require adjustments for other versions of MegaMix.
