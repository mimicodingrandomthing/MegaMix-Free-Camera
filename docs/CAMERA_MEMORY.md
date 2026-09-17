# Camera Memory

Main native camera globals identified during the investigation:

```text
14CC2B590    Position X
14CC2B594    Position Y
14CC2B598    Position Z

14CC2B59C    Interest X
14CC2B5A0    Interest Y
14CC2B5A4    Interest Z

14CC2B5A8    Roll
```

Other camera state identified during the investigation includes:

```text
14CC2B5C8    Rotation-related state
14CC2B5DC    Up-vector-related state
```

## Camera basis

When the native camera state is enabled, `FUN_1402FC3A0` builds a basis from position, interest, and the native up vector.

The other camera path derives pitch/yaw from the position-interest vector and applies the native roll value before building the view matrix.

The free camera does not permanently replace these globals every frame. It temporarily substitutes position and interest while the original basis builder executes, then restores the original values.
