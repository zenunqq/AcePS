# AcePS Implementation Roadmap

AcePS should advance from validated infrastructure toward incremental compatibility work. The roadmap below converts the proposed emulator milestones into repository-sized deliverables with explicit boundaries, dependencies, and acceptance criteria. It does not promise compatibility with any commercial title, and it does not include or distribute proprietary firmware keys, copyrighted game data, or decrypted software.

## Current position

The repository already provides the safety-oriented foundation needed for future emulation work. This includes a validated ELF64 load plan, host-backed virtual memory allocation, a duplicate-safe syscall registry, traversal-safe virtual filesystem resolution, bounded asynchronous file services, PM4 packet validation, GPU queue and fence primitives, and lifecycle-managed application services. The remaining work is to connect these boundaries without weakening their validation guarantees.

The first implementation target is a **legal, reproducible homebrew or test workload** that can exercise loading, memory, syscalls, file access, and graphics independently. Commercial-game compatibility is not an acceptance criterion for the early stages.

## Delivery principles

1. **Keep boundaries narrow.** Each subsystem must expose a typed interface and must not reach across layers through global state.
2. **Make every milestone testable.** New behavior requires headless unit tests or deterministic integration fixtures before it is considered complete.
3. **Separate parsing from execution.** Container, ELF, PM4, and shader readers must validate untrusted input without performing host-side effects.
4. **Use supplied firmware material only.** Any cryptographic or firmware-dependent behavior must consume user-provided files and must never generate, embed, or redistribute console keys.
5. **Prefer incremental compatibility.** A minimal correct implementation is more valuable than broad stubs that silently return plausible but incorrect data.
6. **Preserve host safety.** Guest paths, addresses, handles, threads, and packet data must remain validated at every host boundary.

## Milestones

| Milestone | Goal | Primary deliverables | Exit criteria |
| --- | --- | --- | --- |
| 0. Foundation hardening | Make the existing seams measurable and diagnosable | Error taxonomy, subsystem metrics, fixture format, logging categories, deterministic test harness | All existing tests pass; each later milestone has a fixture and failure mode |
| 1. Load a legal test executable | Move from an ELF load plan to mapped guest memory | Segment mapper, zero-fill handling, protection translation, entry-point validation, module ownership | A synthetic ELF fixture maps and unloads correctly; invalid overlap, alignment, and range cases are rejected |
| 2. File and module boot path | Make a test program able to resolve modules and read files | Read-only package/container abstraction, VFS-backed file handles, module load lifecycle, bounded reads | A fixture opens an application image through `/app0`, reads deterministic bytes, and closes all handles without leaks |
| 3. Core OS services | Provide the minimum deterministic HLE surface | Kernel service object, thread lifecycle, memory mapping, file descriptors, mutexes, semaphores, and exit paths | A legal test workload can create, start, synchronize, and exit threads; unknown calls remain explicit errors |
| 4. Guest syscall boundary | Connect guest execution to the registry safely | Platform-specific trap/exception abstraction, register-frame decoding, syscall argument validation, dispatch tracing | Synthetic trap frames dispatch to the registry on supported hosts without terminating the host process |
| 5. First graphics submission | Turn validated PM4 into observable host work | Command processor state machine, queue submission, fence completion, device abstraction, render-pass test backend | A fixture submits a minimal command stream and receives deterministic completion or a structured rejection |
| 6. Shader translation baseline | Parse shader input and emit valid host IR | GCN instruction reader, operand model, SPIR-V module builder, capability checks, compiler diagnostics | Known test shaders produce structurally valid SPIR-V; unsupported instructions fail loudly and reproducibly |
| 7. Render correctness | Translate state and resources into visible output | Context-register tracker, pipeline state, texture format conversion, blend/depth mapping, cache invalidation | Reference render fixtures match expected images within documented tolerances |
| 8. Audio and input services | Add timing-sensitive peripheral boundaries | Audio ring buffer, SDL2 or equivalent backend, pad/input abstraction, clock integration | Synthetic playback and input fixtures remain synchronized under load and shut down cleanly |
| 9. Persistence and compatibility expansion | Support durable application behavior | Save-data mount abstraction, user-slot model, background save operations, expanded syscall coverage | Save/load fixtures survive restart, reject path escapes, and do not block emulator shutdown |

## Work packages

### 1. Segment mapping

`Elf64Loader::inspect` should remain a pure parser. A separate loader service should consume its `ElfLoadPlan` and own the lifetime of each mapped segment. It should allocate page-rounded memory through `VirtualMemoryManager`, copy only the file-backed bytes, zero the remaining memory-backed range, and apply the translated `SegmentFlags` protection after initialization. Cleanup must be transactional: if any segment fails, previously mapped segments are released in reverse order.

The mapper must reject integer overflow, overlapping guest ranges, unsupported machine types, invalid alignment, and an entry point outside an executable segment. Tests should use synthetic byte arrays rather than real game binaries.

### 2. Container and filesystem access

The VFS should continue to own namespace and traversal policy. Package parsing should be introduced behind a read-only `PackageReader` interface that exposes validated entries and bounded range reads. The parser must not extract files into arbitrary host paths by default. A streaming implementation is preferable for large assets, while the existing asynchronous file service can provide bounded scheduling and caching.

The initial format support should be limited to a documented fixture format or a format that can be implemented without distributing proprietary content. Package encryption and firmware-derived decryption must remain an optional boundary that reads user-supplied material and reports unavailable keys clearly.

### 3. Orbis HLE services

`SyscallRegistry` should remain a dispatch mechanism rather than a store for subsystem state. A `Kernel` service should own handles, threads, synchronization objects, and memory mappings, and register handlers during initialization. Each handler needs a documented argument contract, error mapping, and lifecycle rule.

The first syscall group should cover process exit, thread creation and start, anonymous mapping and unmapping, open/read/close against the VFS, and mutex/semaphore operations. The implementation should use host primitives only behind an Orbis-compatible semantic layer so that timeout, ownership, and destruction behavior can be tested independently of the host scheduler.

### 4. Guest execution boundary

Trap handling is inherently platform-specific and should not be embedded in `SyscallRegistry`. Introduce a small `GuestTrapDispatcher` interface with platform adapters for supported hosts. The adapter should decode a captured register frame, validate the syscall number and argument count, invoke the registry, and write the result back to the frame.

The first version should support a synthetic register-frame test path before real signal or exception integration. Real handlers must use an explicitly documented recovery strategy. They must never treat arbitrary host faults as guest syscalls.

### 5. GPU and shader path

`Pm4Parser` already provides a safe packet boundary. `CommandProcessor` should consume packet views and translate only recognized operations into an abstract GPU command stream. Vulkan calls should live behind a device/queue interface so tests can use a deterministic fake backend without requiring a display server or physical GPU.

The shader path should begin with a versioned GCN bytecode reader and an instruction model. The translator should emit valid SPIR-V for a deliberately small instruction subset, report unsupported operations with instruction offsets, and validate the generated module before caching it. A correct subset with clear diagnostics is the required baseline; silently generated garbage is not.

### 6. Audio, input, and persistence

Audio and input should be scheduled against the emulator clock rather than wall-clock callbacks owned by the UI. The audio service should expose a bounded ring buffer and explicit underrun/overrun counters. The input service should expose timestamped state snapshots.

Save data should be modeled as a controlled namespace under the configured save root. It should provide atomic replacement, bounded background writes, cancellation during shutdown, and explicit user-slot selection. No service should write outside the configured root.

## Suggested repository additions

| Path | Responsibility |
| --- | --- |
| `docs/roadmap.md` | This staged implementation plan and its acceptance criteria |
| `include/aceps/loader/SegmentMapper.h` | Transactional mapping of validated ELF segments |
| `include/aceps/filesystem/PackageReader.h` | Read-only package entry and range-read contract |
| `include/aceps/os/Kernel.h` | Orbis service ownership and syscall registration |
| `include/aceps/os/GuestTrapDispatcher.h` | Host-independent guest trap boundary |
| `include/aceps/gpu/CommandProcessor.h` | PM4-to-device command translation contract |
| `include/aceps/shader/GcnReader.h` | Validated GCN instruction stream reader |
| `include/aceps/shader/ShaderTranslator.h` | GCN-to-SPIR-V translation contract |
| `tests/fixtures/` | Synthetic ELF, package, PM4, shader, and save-data fixtures |

These paths are targets for future implementation. They should not be created as empty placeholders; each addition should arrive with behavior, tests, and documentation.

## Definition of done for every milestone

A milestone is complete only when the implementation has a public contract, a failure model, deterministic tests, bounded resource ownership, and an updated architecture note. The build must pass with warnings treated as errors when the required dependencies are available. The test suite must cover malformed input and cleanup after partial failure. Logs must identify the subsystem and operation without exposing secrets or user data.

## Explicit non-goals

This roadmap does not authorize acquiring or distributing PS4 firmware keys. It does not provide decrypted SELF or PKG content. It does not claim that AcePS can run God of War, Red Dead Redemption 2, or any other commercial title. Those titles may be used only as long-term compatibility references after the legal, technical, and reproducibility requirements of the earlier milestones have been met.

## References

[1]: https://github.com/shadps4-emu/shadPS4 "shadPS4 emulator repository"

[2]: https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html "Khronos SPIR-V specification"

[3]: ../README.md "AcePS project overview and current architecture guarantees"

[4]: ./architecture.md "AcePS architecture notes"
