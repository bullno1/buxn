# buxn-gui - The GUI emulator

`buxn-gui` is a graphical version of the emulator with all devices enabled.
Use it with: `buxn-gui <rom>`.
When a ROM exposes [metadata](https://wiki.xxiivv.com/site/metadata.html) the following will be handled:

* General metadata: The first line will be set as the window's title.
* Icon: The icon will be set as the window's icon.
  Take note that it will be drawn using [potato's default theme](https://git.sr.ht/~rabbits/potato/tree/325083af9dbde3c39e27e67e75b026ed4b98c8f0/item/src/potato.tal#L31) with the color 0 being transparent.

Just like [cli](./cli.md), a ROM can also be embedded directly into the emulator to create a standalone GUI application.
See [rom2exe](./rom2exe.md).

## Android notes

On Android, the app automatically loads the `boot.rom` file in its [assets](../src/android/apk/assets/README.md) directory.

Additionally, the app can also opens `.rom` files.
Simply tap any `.rom` file in a file explorer or browser and choose it as the opener.
