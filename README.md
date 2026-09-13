# AcePS

AcePS is an original C++20 PS4 emulator research project. The repository now provides a disciplined foundation for incremental emulator work rather than a collection of empty files.

## What is implemented now

The current foundation includes a typed application configuration model with validation, a top-level emulator service container, explicit subsystem lifecycle contracts, transactional startup with reverse-order rollback, typed initialization errors, a thread-safe logging adapter, a Qt6 desktop shell, deterministic game discovery, a traversal-safe virtual filesystem, a host-backed page-granular virtual memory manager, a duplicate-safe syscall registry, a bounds-checked PM4 packet parser, CMake install/export metadata, vcpkg manifest setup, and strict core smoke tests.

For demanding workloads, the core also includes a bounded multi-worker scheduler with backpressure and exception-isolated futures, drift-resistant monotonic frame pacing, and zero-copy PM4 packet views. These foundations improve scalability but do not by themselves provide compatibility with a specific commercial game.

The emulation subsystems themselves are still intentionally non-functional seams. The project does **not** currently load or run commercial PS4 software. That separation is deliberate: lifecycle, error handling, configuration, and build quality are stabilized before hardware behavior is introduced.

## Build and test

On Windows, run `setup.bat`. On Linux, run:

```bash
./setup.sh
cmake --build build
```

For a dependency-light headless validation build:

```bash
cmake -S . -B build -G Ninja \
  -DACEPS_BUILD_GUI=OFF \
  -DACEPS_BUILD_TESTS=ON \
  -DACEPS_WARNINGS_AS_ERRORS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

For a local PM4 throughput measurement, configure with `-DACEPS_BUILD_BENCHMARKS=ON` and run `build/aceps_pm4_benchmark`. The parser exposes zero-copy packet views for command-processing hot paths while retaining an owning compatibility API for callers that need independent packet storage.

Qt6, Vulkan, spdlog, SDL2, compression, cryptography, and hashing dependencies are declared in `vcpkg.json`. The setup scripts bootstrap vcpkg and configure the full GUI build.

## Architecture guarantees

- **No partial startup:** a failed service rolls back all services already started.
- **Typed failures:** startup errors use `EmulatorError` rather than generic strings at the public boundary.
- **Explicit configuration:** invalid graphics settings and missing paths are rejected before service startup.
- **Deterministic shutdown:** services stop in reverse startup order and shutdown is idempotent.
- **Headless testability:** core configuration and lifecycle behavior can be tested without Qt, Vulkan, or game assets.
- **Clean boundaries:** UI code depends on the emulator API, not on individual future OS or GPU internals.
- **Memory safety:** host allocations are page-rounded, tracked, protected, and released only through validated ownership.
- **Command safety:** malformed PM4 buffers are rejected before any future GPU execution layer sees them.
- **HLE safety:** unknown syscalls return `-ENOSYS`; duplicate or empty handlers are rejected.

## Repository layout

| Path | Purpose |
| --- | --- |
| `include/aceps/app` | Qt application interfaces |
| `include/aceps/common` | Shared logging and utility interfaces |
| `include/aceps/config` | Typed user configuration |
| `include/aceps/core` | Emulator lifecycle and service contracts |
| `src/app` | Desktop UI implementation |
| `src/common` | Shared implementation code |
| `src/core` | Emulator orchestration |
| `src/memory` | Future virtual and physical memory services |
| `src/os` | Future Orbis OS HLE and syscall services |
| `src/loader` | Future SELF/ELF loading and relocation |
| `src/filesystem` | Future PKG/PFS/VFS services |
| `src/gpu` | Future PM4 command processor and render state |
| `src/shader` | Future GCN-to-SPIR-V translator |
| `src/audio` | Future audio output services |
| `src/input` | Future controller and pad services |
| `tests` | Headless smoke and subsystem tests |
| `docs` | Design notes and research records |

## Planned implementation order

1. Memory manager and host protection abstraction
2. SELF/ELF loader and module metadata
3. Orbis syscall registry and synchronization primitives
4. VFS, PKG, and PFS read-only support
5. CPU syscall/exception patching
6. PM4 command processor and Vulkan device layer
7. Shader translator and pipeline cache
8. Audio and input backends
