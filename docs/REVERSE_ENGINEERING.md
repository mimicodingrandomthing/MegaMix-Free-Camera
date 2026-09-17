# Reverse Engineering Notes

The camera investigation was done with Ghidra by following XREFs from camera globals and then checking the decompiled functions against the assembly.

## Investigation path

1. Start from existing DIVA research and camera-related code in reDIVA/PD Loader/AFTMods/DivaGL.
2. Locate MegaMix camera position, interest, roll, FOV, and near values.
3. Follow XREFs to determine where those values are written and read.
4. Find the per-frame camera update path:

```text
FUN_1402FB0F0
    -> FUN_1402FB410
    -> FUN_1402FC3A0
```

5. Inspect `FUN_1402FC3A0` in assembly.
6. Confirm that the render camera reads the native position and interest globals directly.
7. Test getter/setter hooks and observe that they do not prevent the camera transform from using the native globals.
8. Temporarily substitute the free-camera position and interest before the original camera basis builder runs.
9. Restore the native values immediately afterward.
10. Trace the PV roll path and hook the `0x2FAE3A` CALL instead of the tiny `FUN_1402FB7A0` function.
11. Trace frustum culling through `FUN_1402FBCB0` and its caller and bypass it for free-camera use.
12. Test 2D/AET rendering and remove changes that modify shared camera-up/basis state.

## Important lesson

The obvious camera getter functions were not the final solution. The important part was finding where the renderer actually consumed the camera state.

The final implementation keeps the game's original camera transform code and temporarily feeds it the free-camera values instead of replacing the whole camera calculation.
