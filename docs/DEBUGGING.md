# Debugging Notes

This project went through a lot of small test builds. Several bugs looked like camera problems but were actually caused by modifying shared state used by other parts of the renderer.

## PV camera override

The first major issue was the PV camera changing the free camera even after the obvious camera getter hooks were installed.

The important discovery was that `FUN_1402FC3A0` reads the native position and interest globals directly. The fix was to temporarily replace those values only while the original camera basis builder runs.

## 2D/AET flickering

Some attempts to feed free-camera roll through the native up-vector/camera basis state caused 2D/AET elements to flicker or disappear at certain angles. Other experiments caused the 3D scene to stretch or flicker as well.

The final version leaves the native up-vector setter game-owned and keeps Q/E roll through the PV roll call-site hook.

## Q/E roll

The roll setter itself is too small to safely detour. Hooking its caller at `0x2FAE3A` avoids overwriting the tiny function and keeps the native path intact when free camera is disabled.

## UI

The UI also had development regressions. Manual values could be overwritten by the live camera refresh, and some visual revisions caused broken fonts or flickering. The final approach keeps edit values separate while they are being changed and only refreshes fields that are not being edited.

## General debugging process

```text
change one thing
build
run game
observe result
check logs
return to Ghidra
follow XREFs
inspect assembly
change one thing again
repeat
```

A large part of the work was simply narrowing down which native state was actually being consumed by the renderer.
