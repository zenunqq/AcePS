# AcePS Architecture Notes

AcePS is organized as a layered emulator rather than a mirror of any existing emulator repository. The application layer owns presentation and user actions. The core layer owns lifecycle and service composition. Subsystems communicate through narrow interfaces so platform-specific code remains isolated.

The first implementation milestone intentionally separates project wiring from emulation correctness. A clean build proves that the foundation is stable, but the current project makes no claim to execute commercial PS4 software.

## Boundary rules

- UI code must not call host OS APIs directly when an emulator service can own the behavior.
- Shared memory and address types should be explicit-width and validated at subsystem boundaries.
- Stubs must log their status and return documented placeholder values.
- External behavioral references inform design decisions but are not copied into the repository.

## Concrete foundations

The memory layer now owns host mappings with page rounding, allocation tracking, protection changes, and exact-release validation. The OS layer exposes a syscall registry that separates registration from dispatch and returns `-ENOSYS` for unknown calls. The graphics layer parses PM4 packet headers and payload bounds without executing them. These are intentionally narrow foundations: execution semantics can be added without weakening ownership, validation, or error-reporting guarantees.
