# buxn

Implementation of the [uxn virtual machine](https://wiki.xxiivv.com/site/uxn.html) and the [varvara system](https://wiki.xxiivv.com/site/varvara.html).

Current supported platforms:

* Linux
* Android

## Building
### Common

Run `./bootstrap` to make sure all dependencies are up-to-date.

The general syntax for the build script is: `./build <type> <platform>`.

`type` can be one of:

* `Debug`: Defines `_DEBUG` and enable sanitizer.
* `Release`: Enable optimization.

`platform` can be one of:

* `linux`
* `android`

Without arguments, it will default to build with `Debug` and `linux`.
The output will be placed in `bin/$type/$platform`.

[GNU Parallel](https://www.gnu.org/software/parallel/) is required for parallel compilation.

### Linux

This is the argument passed to the linker: `-lGL -lX11 -lXcursor -lXi -lasound`.
Install the respective development packages for your distro.

`clang` and [`mold`](https://github.com/rui314/mold) are required for compilation.
Take note that there is a [performance regression](https://github.com/llvm/llvm-project/issues/106846) in clang 19 when using computed goto.
An earlier version such as 18 is recommended until the bug is fixed.

`buxn-cli` is a terminal version of the emulator.
It only has a few devices: system, console, file, datetime.

`buxn-gui` is a graphical version of the emulator with all devices enabled.
Use it with: `buxn-gui <rom>`.
When a ROM exposes [metadata](https://wiki.xxiivv.com/site/metadata.html) the following will be handled:

* General metadata: The first line will be set as the window's title.
* Icon: The icon will be set as the window's icon.
  Take note that it will be drawn using the [system's theme](https://wiki.xxiivv.com/site/varvara.html#set-theme) with the color 0 being transparent.

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

## Development tips and other notes
### Build on file change

Run `./watch` with the same arguments as `./build` to automatically build on file changes.

### Build system

As it is relatively simple, the project does not make use of any build system.
Simply add the files to be compiled into the `build` script and its object file into the link command.

### Interpreter design and computed goto

The bytecode interpreter makes use of computed goto for an easy speed boost.
Uxn has several "mode" flags in its instructions so a bit of macro programming was used to reduce code duplication.
This is so that the code for each variant of the same opcode (e.g: `JMP`, `JMP2`, `JMP2kr`) are duplicated at compile-time instead of checking the flag at runtime.
The code at [src/vm.c](src/vm.c) uses the suffix `_MONO` to refer to a monomorphic piece of code and `_POLY` for code that expands into multiple `_MONO` variants.
Some macro shenanigans are also used to generate the jump table for all variants of the same opcode.
This looks horrible and I'm not sorry.

Most opcodes interact with the stack.
They are decomposed into micro ops which change their behaviour based on the `2kr` flags.
The `k`(eep) flag, in particular, avoid modifying the stack by writing to a separate register instead of the stack pointer.

### Audio system design

Audio is handled by [sokol_audio](https://github.com/floooh/sokol?tab=readme-ov-file#sokol_audioh) in a separate thread.
Care was taken not to block the audio thread which can produce audible artifacts.

New samples are written into a staging buffer before submission to the audio thread.

The audio thread polls a pointer for incoming submissions and merge them with its own private data.
When it is done processing, it will set this pointer to `NULL`.

The main thread tries to atomically set this pointer to its staging buffer when the pointer is not NULL.
This will notify the audio thread of new submissions.
When this atomic swap is successful, it will use the other buffer for staging.

This ensures that both the main thread and the audio thread cannot block each other, even under heavy usage such as in the game [Oquonie](https://100r.co/site/oquonie.html).
The drawbacks are:

* Latency: Since submission is asynchronous, there can be a delay between writing to the audio port and when the sound is heard.
  In practice, this does not seem noticeable.
* Omission: Since submission is buffered, when sample B is written shortly after sample A *to the same audio device*, it is possible that no part of sample A is heard at all.
