# Farcal Engine

Farcal Engine is a C++20 desktop memory analysis tool with a Qt6 UI and Lua scripting support.
It is designed for attaching to a running process, scanning memory, inspecting values, and applying value writes in real time.

## Features

- Process attach flow (including attach last process)
- Value scanning with first/next scan workflow
- Scan conditions: exact, increased, decreased, changed, unchanged
- Supported value types: `int8`, `int16`, `int32`, `int64`, `float`, `double`, `string`
- Memory viewer window
- RTTI scanner
- String scanner (ASCII/UTF-16)
- Structure dissector
- Loop value manager (repeated write entries)
- Lua IDE/VM integration
- Configurable keybinds and settings persistence

## Tech Stack

- C++20
- CMake (3.21+)
- Qt6 Widgets
- Lua 5.4.6
- sol2 v3.3.0
- GLM 1.0.1

## Requirements

- Windows (current memory reading/writing paths are Windows-first)
- Visual Studio 2022 (MSVC toolchain)
- CMake 3.21 or newer
- vcpkg with Qt6 (`qtbase`)

The included `CMakePresets.json` assumes vcpkg is installed at:

`C:/vcpkg/scripts/buildsystems/vcpkg.cmake`

If your vcpkg location is different, update `CMakePresets.json`.

## Build

### Debug

```powershell
cmake --preset msvc-debug
cmake --build --preset build-msvc-debug
```

### Release

```powershell
cmake --preset msvc-release
cmake --build --preset build-msvc-release
```

### Size-optimized build

```powershell
cmake --preset msvc-size
cmake --build --preset build-msvc-size
```

## Run

After building, launch one of:

- `build/Debug/FarcalEngineV2.exe`
- `build/Release/FarcalEngineV2.exe`

A post-build step also mirrors runtime files into `bin/`.

## Main Build Options

- `FARCAL_SINGLE_EXE`: Build as a single executable (requires static Qt)
- `FARCAL_EXTREME_SIZE_OPT`: Extra size-focused optimization for release configs
- `FARCAL_PARALLEL_BUILD`: Enables MSVC parallel compile flags
- `FARCAL_DISABLE_RTTI`: Disables C++ RTTI metadata generation

## Project Layout

- `src/`: application, UI, memory scanner, and Lua VM source files
- `include/`: public headers
- `build/`: generated build files/artifacts
- `bin/`: copied runtime output and DLLs

## Notes

- The tool interacts with external process memory. Run with appropriate permissions.
- Use only on software and systems you are authorized to inspect.
