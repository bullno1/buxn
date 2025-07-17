# Overview

buxn has evolved from being just an emulator implementation to a suite of tools for uxn.
I have also started using uxntal as an embedded scripting language and developed other supporting tools such as a [language server](https://github.com/bullno1/buxn-ls) or a [step debugger](https://github.com/bullno1/buxn-dbg).
A map of the project is needed:

* Core libraries:
  * [asm](./asm.md): The uxntal assembler
  * [vm](./vm.md): The uxn virtual machine
  * [dbg](./dbg.md): Debugging support.
    To be used along side [buxn-dbg](https://github.com/bullno1/buxn-dbg).
  * [devices](./devices.md): Varvara implementation
* Frontend programs:
  * [asm](./asm-frontend.md): The assembler frontend
  * [cli](./cli.md): Terminal version of the emulator
  * [gui](./gui.md): GUI version of the emulator
  * [rom2exe](./rom2exe.md): Create a standalone executable from a ROM
  * [romviz](./romviz.md): ROM visualization tool
  * [repl](./repl.md): Example of a REPL
  * [bindgen](./bindgen.md): Binding generator

## On link-time polymorphism

Several headers in the project declares `extern` functions after the following comment:

```c
// Must be provided by the host program
```

This means the library will call into those functions and the host program must provide them.
Otherwise, a link error will happen.

This is similar to passing function pointers to the library but:

* The code is a lot simpler on both the library and the host sides.
* In the typical case, there is only a single implementation.
  Passing function pointers is redundant.
* Link-time polymorphism can still be converted to runtime polymorphism by the host.

All libraries will make no assumptions about the following:

* File system: This allows the host to virtualize file access.
* Memory allocation: This allows the host to plug in its own allocator.
  All libraries will assume the existence of an arena allocator.
  There is only an "alloc" callback and no "free" one.
  In addition, small allocations are also reused to reduce peak memory usage.

  Some libraries such as [vm](./vm.md) does not allocate memory at all.
  The user is expected to provide a correctly sized buffer.

# On external dependencies

Care was taken to reduce external dependencies.
The following libraries do not depend on any external libraries:

* [asm](./asm.md)
* [vm](./vm.md)
* [devices](./devices.md): I/O devices such as file or screen require the host to provide callbacks.
  These are implemented in the emulator programs instead of the library.
* [metadata](./metadata.md)

[Debug libraries](./dbg.md) use [bserial](https://github.com/bullno1/libs/tree/master/tests/bserial) for the debug wire protocol.
If you want to implement an alternate protocol, only buxn-dbg-core is needed and the only external dependency can be removed.
However, the current debugger frontend ([buxn-dbg](https://github.com/bullno1/buxn-dbg)) requires this protocol so that has to be reimplemented too.

Frontend programs, in general, do not have that restriction.
But all dependencies are vendored in `deps`.
Only graphics, input and windowing libraries are taken from the system.
