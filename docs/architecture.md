# AcePS Architecture Notes

AcePS is organized as a layered emulator rather than a mirror of any existing emulator repository. The application layer owns presentation and user actions. The core layer owns lifecycle and service composition. Subsystems communicate through narrow interfaces so platform-specific code remains isolated.

The first implementation milestone intentionally separates **project wiring** from **emulation correctness**. A clean build proves that future work has a stable home, but the current skeleton makes no claim to execute PS4 software.

## Boundary rules

- UI code must not call host OS APIs directly when an emulator service can own the behavior.
- Shared memory and address types should be explicit-width and validated at subsystem boundaries.
- Stubs must log their status and return documented placeholder values.
- External behavioral references inform design decisions but are not copied into the repository.
