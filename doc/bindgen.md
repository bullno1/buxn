# bindgen - Binding generator

Since uxntal is now being as an embedded scripting language, the devices are now used as a Foreign Function Interface (FFI).
To make writing binding less tedious, bindgen is created.
From a `.tal` file such as [bindgen-example.tal](./bindgen-example.tal), it can generate a C header.
Special annotations are needed:

* When `(device )` is put in front of a group of labels, an enum is generated.
  The values are all the device's ports.
  The values are formatted as hex.
* When `(enum )` is put in front of a group of labels, an enum is generated.
  The values are the labels' addresses.
  The values are formatted as decimal.
* When `(command )` is put in front of a group of labels, an enum is generated.
  Several items are generated:

  * An enum whose members are the offset of fields in the command buffer.
  * A struct that represent the command buffer.
    Members are either `uint8_t` or `uint16_t`.
  * A couple of functions to read or write the struct to/from VM's memory.

For an example, refer to [bindgen-example.tal](./bindgen-example.tal) and try running bindgen on it.
It shows how to handle the `.System/expansion` port.

To see all options, use `--help`.
