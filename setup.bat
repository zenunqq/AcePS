@echo off
setlocal enabledelayedexpansion

where git >nul 2>nul || (echo Git is required.& exit /b 1)
where cmake >nul 2>nul || (echo CMake is required.& exit /b 1)

if not exist "%~dp0vcpkg" (
  git clone https://github.com/microsoft/vcpkg.git "%~dp0vcpkg" || exit /b 1
  call "%~dp0vcpkg\bootstrap-vcpkg.bat" -disableMetrics || exit /b 1
)

cmake -S "%~dp0" -B "%~dp0build" -G Ninja -DCMAKE_TOOLCHAIN_FILE="%~dp0vcpkg\scripts\buildsystems\vcpkg.cmake" -DACEPS_BUILD_GUI=ON
if errorlevel 1 exit /b 1

echo AcePS configured. Build with: cmake --build build
