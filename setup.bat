@echo off
REM AcePS Windows setup helper: downloads vcpkg locally and installs the declared C++ dependencies.
setlocal
set "PROJECT_ROOT=%~dp0"
set "VCPKG_ROOT=%PROJECT_ROOT%.tools\vcpkg"

where git >nul 2>nul || (
  echo Missing required command: git. Install Git, then run this script again.
  exit /b 1
)
where cmake >nul 2>nul || (
  echo Missing required command: cmake. Install CMake, then run this script again.
  exit /b 1
)

if not exist "%VCPKG_ROOT%\.git" (
  if not exist "%PROJECT_ROOT%.tools" mkdir "%PROJECT_ROOT%.tools"
  git clone https://github.com/microsoft/vcpkg.git "%VCPKG_ROOT%" || exit /b 1
)

call "%VCPKG_ROOT%\bootstrap-vcpkg.bat" -disableMetrics || exit /b 1
"%VCPKG_ROOT%\vcpkg.exe" install --triplet x64-windows || exit /b 1

echo Setup complete. Build with: cmake -B build ^&^& cmake --build build --config Release
endlocal
