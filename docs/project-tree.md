# AcePS Project Tree

```text
AcePS/
├── CMakeLists.txt                 # Top-level build and target wiring
├── vcpkg.json                     # Manifest-mode dependencies
├── setup.bat / setup.sh           # Platform bootstrap scripts
├── cmake/                         # Reusable CMake modules
├── docs/                          # Architecture notes and project maps
├── include/aceps/                 # Public C++ headers by subsystem
├── src/                           # C++ implementations by subsystem
├── tests/                         # Optional smoke tests
└── third_party/                   # Vendored dependencies, kept empty initially
```
