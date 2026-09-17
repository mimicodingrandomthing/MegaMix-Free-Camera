# Building

## Requirements

- Windows
- Visual Studio with the MSVC C++ toolchain
- CMake 3.20 or newer
- x64 build tools

The project uses C++17 and MASM assembly.

## CMake

From a Visual Studio developer command prompt:

```text
cmake -S . -B build -A x64
cmake --build build --config Release
```

For a debug build:

```text
cmake --build build --config Debug
```

The exact output location depends on the CMake generator/configuration.

## Notes

This project is intended to be built as a Windows x64 DLL and loaded by the mod/plugin environment used with the compatible MegaMix executable.

The repository does not include the game executable or game assets.

The camera hooks are version-specific. If the game executable changes, the addresses and call sites should be verified again with Ghidra before using the DLL.
