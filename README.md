# MegaMix Free Camera

A free-camera mod for **Hatsune Miku: Project DIVA MegaMix+**.

## Introduction

I'm a coding student, and I was introduced to MegaMix as a learning experience. I wanted to understand how a real game works internally, so I started exploring its camera system and reverse engineering it with Ghidra.

I used projects like **reDIVA, PD Loader, and AFTMods/DivaGL** as references, then traced MegaMix's own functions and data until I was able to build a working free camera.

This project was mainly a learning experience. A lot of it involved trial and error, debugging, and figuring out what the game was actually doing internally.

## Features

- Free camera movement
- Position and look-at/interest controls
- Yaw and pitch control
- Roll control
- FOV control
- Near clip control
- Mouse camera control
- Frustum culling bypass
- Original camera behavior restored when disabled
- UI for editing camera values

## How It Works

The main challenge was finding where MegaMix actually builds its camera transform.

Using Ghidra, I followed camera-related functions and their XREFs. The important call chain ended up being:

```text
FUN_1402FB0F0
    └── FUN_1402FC3A0
```

`FUN_1402FC3A0` builds the camera basis and view transform. It directly reads the native camera position and interest globals:

```text
14CC2B590..14CC2B598 = position
14CC2B59C..14CC2B5A4 = interest
```

This was important because hooking the camera getter functions alone was not enough. The render code was still using the native camera globals directly.

The solution was to temporarily replace those values while the original camera builder runs:

```text
save original camera
write free camera values
run original camera builder
restore original camera
```

This lets MegaMix's original camera code calculate the view transform while using the free-camera position and interest.

## Roll

Roll was investigated separately.

The relevant call site is:

```text
1402FAE3A
    CALL 1402FB7A0
```

`FUN_1402FB7A0` is only a few bytes long and writes the native roll value to `14CC2B5A8`. Because the function is too small to safely detour directly, the mod hooks the 5-byte CALL at `1402FAE3A` instead.

This keeps the original behavior when free camera is disabled while allowing Q/E to provide the free-camera roll when enabled.

## Culling

Once the camera could move freely, objects outside the normal camera view could be removed by the game's frustum culling.

The relevant functions were found through XREFs:

```text
FUN_1402FBCB0    frustum test
FUN_14045DFC0    caller
```

The free camera bypasses this culling check so objects can remain visible when the camera moves outside the normal PV view.

## Important Camera Functions

```text
FUN_1402FB0F0    Camera update
FUN_1402FB410    Projection/frustum related calculations
FUN_1402FC3A0    Camera basis/view transform
FUN_1402FB760    Position setter
FUN_1402FB780    Interest setter
FUN_1402FB7A0    Roll setter
FUN_1402FB7B0    Camera state/up-vector related setter
FUN_1402FB960    Position getter
FUN_1402FB980    Interest getter
FUN_1402FB9D0    FOV getter
FUN_1402FB9E0    Near clip getter
FUN_1402FB860    Rotation getter
FUN_14045DFC0    Frustum culling caller
FUN_1402FBCB0    Frustum culling test
```

These addresses are tied to the MegaMix executable version used during development and may change between game versions.

## Camera Data

The main native camera values identified were:

```text
14CC2B590    Position X
14CC2B594    Position Y
14CC2B598    Position Z

14CC2B59C    Interest X
14CC2B5A0    Interest Y
14CC2B5A4    Interest Z

14CC2B5A8    Roll
```

Other relevant state identified during the investigation includes the native rotation/up-vector state around `14CC2B5C8` and `14CC2B5DC`.

## Look-at Distance

The UI calls this value **Look-at Distance**. It is not an optical camera focal length.

It represents the distance between the camera position and its interest point. When using yaw/pitch movement, the interest point is generated along the current viewing direction at that distance.

## Development and Debugging

There were several bugs during development:

- PV camera overriding the free camera
- 2D/AET elements flickering or disappearing at certain angles
- 3D rendering becoming stretched or corrupted after incorrect camera changes
- Q/E roll controls breaking after hook changes
- UI values reverting after pressing Apply
- UI font rendering problems
- UI flickering
- Live camera updates overwriting manually entered values

A lot of the debugging process was basically:

```text
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
check XREFs / assembly
    ↓
change it again
    ↓
repeat
```

The hardest part was usually finding which part of the game's camera system was actually responsible. For example, the obvious camera getters were not enough because the camera transform code was reading the native globals directly.

The final solution was much simpler once that data flow was understood.

## Architecture

```text
Free Camera Controller
        ↓
Camera Bridge
 ├─ position / interest
 ├─ FOV / near clip
 ├─ roll hook
 ├─ temporary position/interest override
 └─ culling bypass
        ↓
Original MegaMix camera
        ↓
Renderer
```

## Build

See [BUILDING.md](BUILDING.md).

## Reverse Engineering Notes

More details are in the `docs/` directory:

- [Reverse Engineering](docs/REVERSE_ENGINEERING.md)
- [Camera Functions](docs/CAMERA_FUNCTIONS.md)
- [Camera Memory](docs/CAMERA_MEMORY.md)
- [Culling](docs/CULLING.md)
- [Debugging Notes](docs/DEBUGGING.md)

## Credits and References

This project builds on the existing DIVA reverse-engineering and modding community's work. In particular, **reDIVA, PD Loader, and AFTMods/DivaGL** were useful references while learning the engine and deciding what to investigate in MegaMix.

See [CREDITS.md](CREDITS.md).

## Compatibility

The hooks in this project are version-specific. Game updates can change function addresses, global addresses, call sites, and assembly layout.

Do not assume these addresses will work with another MegaMix executable version without checking it first.

## Disclaimer

This is an unofficial modification for educational and research purposes. It is not affiliated with or endorsed by SEGA, Crypton Future Media, or the developers of Project DIVA.

Use the mod at your own risk and keep backups of your game files.
