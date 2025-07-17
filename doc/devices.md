# devices - Varvara devices implementation

The various devices can be found in:

* [Headers](../include/buxn/devices)
* [Sources](../src/devices)

A lot of the are lifted from the reference uxn implementation.
The few modifications are mentioned below.

## Screen

Software scaling and blending is removed.
Instead, this is pushed to the GPU.
This greatly simplfied the code.

## Audio

The audio device(s) now use renders float samples instead of short.
This maps it closer to modern audi APIs.

### Audio system in emulator

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

## File

At the emulator level, the filesystem access is virtualized using [PhysFS](https://github.com/icculus/physfs).

The root of the file system is the same directory as the executable.

On Android, PhysFS also allows reading of the files embedded in the [assets](../src/android/apk/assets/README.md) directory.
