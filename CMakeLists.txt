cmake_minimum_required(VERSION 3.25)

project(AcePS VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ACEPS_BUILD_GUI "Build the Qt6 desktop application" ON)
option(ACEPS_BUILD_TESTS "Build AcePS tests" OFF)
option(ACEPS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(ACEPS_BUILD_BENCHMARKS "Build lightweight local performance benchmarks" OFF)

include(GNUInstallDirs)
include(CTest)
include(cmake/CompilerWarnings.cmake)

find_package(spdlog CONFIG QUIET)
if(NOT spdlog_FOUND)
  message(WARNING "spdlog was not found; configure with vcpkg or install spdlog to build AcePS.")
endif()

add_library(aceps_core STATIC
  src/app/GameLibrary.cpp
  src/common/Logging.cpp
  src/common/Profiler.cpp
  src/config/ApplicationConfig.cpp
  src/config/PlatformPaths.cpp
  src/core/BootSequence.cpp
  src/core/Emulator.cpp
  src/core/FrameClock.cpp
  src/core/WorkScheduler.cpp
  src/memory/VirtualMemoryManager.cpp
  src/os/Kernel.cpp
  src/os/SyscallRegistry.cpp
  src/loader/SelfLoader.cpp
  src/loader/ModuleRegistry.cpp
  src/filesystem/VirtualFileSystem.cpp
  src/filesystem/AsyncFileService.cpp
  src/gpu/CommandProcessor.cpp
  src/gpu/Pm4Parser.cpp
  src/gpu/GpuQueue.cpp
  src/shader/ShaderTranslator.cpp
  src/audio/AudioSystem.cpp
  src/input/InputSystem.cpp
)

add_library(AcePS::Core ALIAS aceps_core)
target_include_directories(aceps_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_definitions(aceps_core PUBLIC ACEPS_VERSION=\"${PROJECT_VERSION}\")
if(spdlog_FOUND)
  target_link_libraries(aceps_core PUBLIC spdlog::spdlog)
else()
  target_compile_definitions(aceps_core PUBLIC ACEPS_NO_SPDLOG=1)
endif()
aceps_enable_warnings(aceps_core)

if(ACEPS_BUILD_GUI)
  find_package(Qt6 COMPONENTS Widgets QUIET)
  if(Qt6_FOUND)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTOUIC ON)
    set(CMAKE_AUTORCC ON)
    add_executable(aceps
      src/main.cpp
      src/app/MainWindow.cpp
      include/aceps/app/MainWindow.h
    )
    target_link_libraries(aceps PRIVATE AcePS::Core Qt6::Widgets)
    target_include_directories(aceps PRIVATE include)
    aceps_enable_warnings(aceps)
  else()
    message(WARNING "Qt6 Widgets was not found; GUI target 'aceps' will not be generated.")
  endif()
endif()

if(ACEPS_BUILD_TESTS)
  add_executable(aceps_smoke_tests tests/SmokeTests.cpp)
  target_link_libraries(aceps_smoke_tests PRIVATE AcePS::Core)
  aceps_enable_warnings(aceps_smoke_tests)
  add_test(NAME aceps_smoke_tests COMMAND aceps_smoke_tests)
endif()

if(ACEPS_BUILD_BENCHMARKS)
  add_executable(aceps_pm4_benchmark benchmarks/Pm4Benchmark.cpp)
  target_link_libraries(aceps_pm4_benchmark PRIVATE AcePS::Core)
  aceps_enable_warnings(aceps_pm4_benchmark)
  add_executable(aceps_compatibility_benchmark benchmarks/CompatibilityBenchmark.cpp)
  target_link_libraries(aceps_compatibility_benchmark PRIVATE AcePS::Core)
  aceps_enable_warnings(aceps_compatibility_benchmark)
endif()

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS aceps_core EXPORT AcePSTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
if(TARGET aceps)
  install(TARGETS aceps RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(EXPORT AcePSTargets NAMESPACE AcePS:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AcePS)
