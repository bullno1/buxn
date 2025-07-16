@echo off

git submodule sync
git submodule update --init

for /f "usebackq tokens=*" %%i in (`cmake\vswhere -latest -prerelease -property installationPath`) do (
  set VS_DIR=%%i
)

call "%VS_DIR%\VC\Auxiliary\Build\vcvarsall.bat" x64 10.0.22621.0

mkdir msvc
cd msvc
cmake ^
    -G "Visual Studio 17 2022" ^
    -A x64 ^
    -D CMAKE_SYSTEM_VERSION=10.0.22621.0 ^
    -D CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
    ..
