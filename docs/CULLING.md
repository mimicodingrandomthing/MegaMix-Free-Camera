# Frustum Culling

Free camera movement can place the camera outside the normal PV view volume. The game can then reject objects using its frustum test.

The relevant functions found during the investigation were:

```text
FUN_1402FBCB0
FUN_14045DFC0
```

`FUN_1402FBCB0` performs the frustum-plane checks using the camera/frustum data. `FUN_14045DFC0` was identified as the relevant caller used by the mod's culling bypass.

The free camera bypass returns a visible/accepted result so the normal frustum test does not remove objects simply because the free camera moved away from the original view.
