# dbg - Uxntal debugging support libraries

Due to its complexity, the bulk of the debugger is a different project: https://github.com/bullno1/buxn-dbg.
However, certain facilities have to be provided within the VM to make it debuggable.

This consists of:

* The debugger core:
  * [core.h](../include/buxn/dbg/core.h)
  * [core.c](../src/dbg/core.c)
* The protocol message encoder/decoder:
  * [protocol.h](../include/buxn/dbg/protocol.h)
  * [protocol.c](../src/dbg/protocol.c)
* The glue code that ties the core to the protocol encoder/decoder:
  * [wire.h](../include/buxn/dbg/wire.h)
  * [wire.c](../src/dbg/wire.c)
* The transport layer:
  * Arbitrary file descriptor:
    * [fd.h](../include/buxn/dbg/transports/fd.h)
    * [fd.c](../src/dbg/transports/fd.c)
  * FIFO file or serial device:
    * [file.h](../include/buxn/dbg/transports/file.h)
    * [file.c](../src/dbg/transports/file.c)
  * Stream socket (e.g: TCP):
    * [stream.h](../include/buxn/dbg/transports/stream.h)
    * [stream.c](../src/dbg/transports/stream.c)
  * Declarative transport:
    * [from_str.h](../include/buxn/dbg/transports/from_str.h)
    * [from_str.c](../src/dbg/transports/from_str.c)
* The symbol reader/writer:
  * [symtab.h](../include/buxn/dbg/symtab.h)
  * [symtab.c](../src/dbg/symtab.c)

This is more complicated than it should be.
There are various transport types but they all fold back into file descriptor.
However, this does not translate well to Windows where socket has a different API from regular files.

## The debugger core (buxn-dbg-core)

At its heart, the debugger integration is simple.
It is an execution hook to be attached to the VM.
In turn, it makes several callbacks to the host to inform it of the VM states.
The most important one is: `buxn_dbg_next_command` where the host is supposed to return a debugger command to be executed.
The range of commands include: stepping, setting breakpoints, inspecting memory and stack...
One of the command is "continue" which resumes execution until a breakpoint is hit.

A breakpoint can have a combination of any of the following 3 bits:

* Read: pause execution when a certain memory address is read from.
* Write: pause execution when a certain memory address is written to.
* Execute: pause execution when a certain memory address is executed.
  This happens when the PC reaches the address and before the opcode is executed.

A breakpoint can also be set on either main memory or device memory.

On main memory, the breakpoints are tripped on program execution and normal load/store.
Uxn has a single address space for both program and data.

On device memory, `DEI` is considered a load and `DEO` is a store.
An execution breakpoint would trip on a vector invocation at that port address.
For example, setting an execution breakpoint on `0x80` in the device memory space would pause execution every time a key is pressed.

The host is supposed to get debug commands from "some source" which is not the concern of the debugger core.

## The debug wire protocol

The intention was always to support a remote debugger.
This also includes an external program running on the same physical machine.
It allows developing the debugger independently from the host program emebedding the uxn virtual machine.

For this to happen, the command needs to be serialized.
[bserial](https://github.com/bullno1/libs/blob/master/bserial.h) is used for this purpose.
The code for this is packaged into `buxn-dbg-protocol`.

Next, it has to be transported.
The debugger core assumes a serial connection.
There is only a single command being served at a time.
Bytes can be sent and received on both ends and they are reliable and ordered.
This is what the `buxn-dbg-wire` library does: read and write bytes on an abstract serial connection.

`buxn-dbg-transport` implements various concrete transport methods.
It also allows a transport to be picked dynamically as a CLI argument.
For example: `tcp-connect:<address>:<port>` will make a connection from the host program to a listening debugger at `<address>:<port>`.

## The debug symbol format

All of the above would allow a debugger to step through code at bytecode level.
However, to be more user-friendly, we need to map the bytecode back into source files.
This is where `buxn-dbg-symtab` comes in.

It also uses [bserial](https://github.com/bullno1/libs/blob/master/bserial.h) to define an on-disk debug symbol format.
Every byte in the rom is mapped to a symbol which can be anything from an opcode, a literal or an address reference.
The [debugger frontend](https://github.com/bullno1/buxn-dbg) would load this file and display a source view where the position of the PC (program counter) is highlighted.

The [assembler frontend](./asm-frontend.md) will generate this file along side the ROM with the extension: `.rom.dbg`.
It will also generate the beetbug-compatible [`.rom.sym` format](https://wiki.xxiivv.com/site/symbols.html).

In order to visualize the ROM using the `.rom.dbg` format, [romviz](./romviz.md) can be used.
