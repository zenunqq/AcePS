<!-- AcePS project guide: explains the current scaffold and the deliberately incremental emulator-development plan. -->
# AcePS

AcePS is an original, experimental PlayStation 4 emulator project written in C++20. It currently provides a Qt6 desktop shell plus a tested, intentionally limited guest bootstrap path; it does **not** run commercial games yet. Use only games, firmware, and content you are legally entitled to use.

## Project layout

```text
AcePS/
├── app/
│   └── desktop/                 # Process startup and application wiring
│       └── main.cpp
├── interface/
│   └── shell/                   # Minimal Qt desktop experience
│       ├── main_window.cpp
│       └── main_window.h
├── runtime/                     # Future launch orchestration and session lifecycle
├── engine/
│   ├── processor/               # Future Jaguar CPU decoding, execution, and scheduling
│   ├── memory_space/            # Future unified-memory map and protection tracking
│   ├── kernel/                  # Future PS4 kernel services, events, and module APIs
│   ├── content/                 # Future package, PFS, ELF/PRX, and virtual-file handling
│   ├── graphics/                # Future GCN command processing and Vulkan presentation
│   ├── sound/                   # Future audio API facade and output mixer
│   └── controls/                # Future controller and keyboard mapping
├── support/
│   ├── logging/                 # Future structured runtime logging
│   └── settings/                # Future persistent user configuration
├── CMakeLists.txt
├── vcpkg.json
├── setup.bat
└── setup.sh
```

The names and boundaries above are AcePS-specific: `engine` models guest subsystems, `runtime` coordinates a game session, `interface` owns Qt-only user interactions, and `support` contains host services shared by them.

## Requirements

### Windows (primary target)

- Git
- CMake 3.21 or newer
- Visual Studio 2022 Build Tools with the **Desktop development with C++** workload
- Vulkan SDK (needed once the graphics system is added)

### Linux (secondary target)

- Git, CMake 3.21+, and a C++20 compiler
- A Qt-compatible Linux desktop environment
- Vulkan development drivers/SDK (needed for future graphics work)

The setup script downloads a project-local copy of vcpkg and installs Qt6 Widgets automatically. It does not need a global vcpkg installation.

## Build

```bash
git clone <repo>
cd AcePS
./setup.sh
cmake -B build
cmake --build build --config Release
```

On Windows, use a Developer Command Prompt and run:

```bat
git clone <repo>
cd AcePS
setup.bat
cmake -B build
cmake --build build --config Release
```

Run `build/AcePS` on Linux or `build/Release/AcePS.exe` with a multi-config Visual Studio generator on Windows.

## First launch and adding games

On its first launch, the current shell asks where your PS4 games are stored. Choose a folder with **Browse for games**. The **Add Game** control currently accepts a PKG path as a library placeholder; mounting, decryption, metadata extraction, and execution are future work. The **Play** control intentionally reports that the runtime is not implemented instead of failing silently.

If a future build requires firmware files, AcePS will show a friendly guide. Obtain firmware and game data only from lawful sources you are authorized to use.

## FAQ

**Does AcePS run games today?** No. The current milestone validates a mapped-memory CPU bootstrap, package recognition, safe host-service facades, and the user-interface foundation only.

**Why does the FPS counter show zero?** It is an explicit placeholder until a game-session timing loop exists.

**Why is Vulkan listed before a renderer exists?** Vulkan is the planned primary graphics backend, so it is a development prerequisite; renderer integration will arrive in the graphics milestone.

## Troubleshooting

- **CMake cannot find Qt6:** rerun the platform setup script. It installs dependencies into `.tools/vcpkg`, which the top-level CMake file detects automatically.
- **The setup script cannot run:** ensure Git and CMake are on `PATH`; on Linux, make the script executable with `chmod +x setup.sh`.
- **Windows compiler errors:** launch `setup.bat` and CMake from a Visual Studio Developer Command Prompt.
- **Vulkan errors later in development:** install the Vulkan SDK and update your graphics driver.

## Development Guide

When building or extending this emulator, implement one system at a time. Recommended order: CPU → Memory → Kernel/OS Layer → File System → GPU/Graphics → Audio → Input. Do not move to the next system until the current one compiles and runs basic stubs successfully. This keeps the project stable at every stage and makes debugging much easier.

The current core target contains original, safe stubs for virtual memory, an eight-core CPU step interface, kernel synchronization primitives, VFS/package recognition, graphics-device initialization reporting, audio, input, and session lifecycle. Every implementation file should begin with a concise plain-English header comment, keep a narrow responsibility, use descriptive names, log user-visible failures clearly, and leave explicit TODOs for unfinished emulation behavior.
