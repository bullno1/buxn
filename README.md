# =uxn

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Build status](https://github.com/bullno1/buxn/actions/workflows/build.yml/badge.svg)](https://github.com/bullno1/buxn/actions/workflows/build.yml)

\> be me

\> be uxn

This project contains:

* An implementation of the [uxn virtual machine](https://wiki.xxiivv.com/site/uxn.html).
* The [varvara system](https://wiki.xxiivv.com/site/varvara.html).
* Libraries for embedding uxn into a host program.
* Tools such as type checker and binding generator.

Current supported platforms:

* Linux
* Windows
* Android
* [Cosmopolitan](https://github.com/jart/cosmopolitan/)

There are many components in this project.
Checkout the [detailed documentation](doc/README.md) for more info.

## Building
### Windows

Run msvc.bat to generate a Visual Studio solution.

### Unix

Run `./bootstrap` to make sure all dependencies are up-to-date.

The general syntax for the build script is: `./build <type> <platform>`.

`type` can be one of:

* `Debug`: Defines `_DEBUG` and enable sanitizers (`-fsanitize=undefined,address`).
* `Release`: Defines `NDEBUG` and enable optimizations (`-O3 -flto`).

`platform` can be one of:

* `linux`
* `android`
* `cosmo`

Without arguments, it will default to build with `Debug` and `linux`.
The output will be placed in `bin/$type/$platform`.

[GNU Parallel](https://www.gnu.org/software/parallel/) is required for parallel compilation.

### Linux

This is the argument passed to the linker: `-lGL -lX11 -lXcursor -lXi -lasound`.
Install the respective development packages for your distro.

`clang` and [`mold`](https://github.com/rui314/mold) are required for compilation.
Take note that there is a [performance regression](https://github.com/llvm/llvm-project/issues/106846) in clang 19 when using computed goto.
An earlier version such as 18 is recommended until the bug is fixed.

### Android

Make sure that Android SDK & NDK are installed.
Typically, this can be downloaded using Android Studio.

The `./build` script assumes the SDK & NDK reside at the default location: `~/Android/Sdk`.
This can be overridden by setting the environment variables: `ANDROID_HOME` and `ANDROID_NDK_ROOT` respectively.

Building for Android is similarly done using: `./build <type> android`.
It will automatically produce the `.so` binary for all supported ABIs and package them into a signed apk.

When started, the app will open the bundled `boot.rom`.
For more details, see [src/android/apk/assets](src/android/apk/assets).

Additionally, the app can also opens `.rom` files.
Simply tap any `.rom` file in a file explorer or browser and choose it as the opener.

#### Using Java code

You can't.
This project is C only.

However, it should not be too hard to add.
The build pipeline was created based on: https://www.hanshq.net/command-line-android.html.

### Cosmopolitan

The [cosmopolitan toolchain](https://github.com/jart/cosmopolitan) has to be in your PATH, in particular `cosmocc` is needed.
Use `./build <Release|Debug> cosmo` to build like other platform.

### CMake

A CMakeLists.txt file is provided to ease integration into other projects.

However, it is simpler to just pick and choose only the relevant parts.
Both [buxn-ls](https://github.com/bullno1/buxn-ls) and [buxn-dbg](https://github.com/bullno1/buxn-dbg) take this approach.
In general, there is only a single header (.h) and a single source (.c) file for each component.
Check the [detailed documentation](doc/README.md) for more info.
