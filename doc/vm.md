# vm - The uxntal virtual machine

The sources are:

* [vm.h](../include/buxn/vm/vm.h): VM API declaration.
* [opcode.h](../include/buxn/vm/opcode.h): Opcode listing for other tools like debugger.
* [vm.c](../src/vm/vm.c): VM implementation.

There are other headers in `src/vm/` which are only included by `vm.c`.
If you are extracting the VM as a standalone component, make sure to also copy those.

## Mode handling and macro meta programming

The bulk of the opcode implementation is actually at [src/vm/exec.h](../src/vm/exec.h).

Uxn has several "mode" flags in its instructions so a bit of macro programming was used to reduce tedium.
This is so that the code for each variant of the same base opcode (e.g: `JMP`, `JMP2`, `JMP2kr`) are duplicated at compile-time instead of checking the flag at runtime.
The suffix `_MONO` is used to refer to a monomorphic piece of code and `_POLY` is used for code that expands into multiple `_MONO` variants.

Most opcodes interact with the stack.
They are decomposed into micro ops which change their behaviour based on the `2kr` flags.
The `k`(eep) flag, in particular, avoid modifying the stack by writing to a separate register instead of the stack pointer.

The result is a rather straightforward implementation of the 32 base opcodes, macro-expanded into 256 different variants.

## Interpreter

The bytecode interpreter will use [computed goto](https://gcc.gnu.org/onlinedocs/gcc/Labels-as-Values.html) when the compiler supports it.
This gives a nice speed boost without a lot of work.
The macro in [opcode.h](../include/buxn/vm/opcode.h) is a higher order [X Macro](https://en.wikipedia.org/wiki/X_macro) that can expand into all 256 opcode names and values.
It is used to generate the dispatch table.

In case computed goto is not supported (MSVC), [switched goto](https://bullno1.com/blog/switched-goto) will be used.

## Hook

To support [debugging](./dbg.md) a hook can be attached to the VM.
The hook will be invoked before each opcode.

This creates a significant performance penalty even when the hook is noop (an empty function).
Merely checking whether the hook is set (not `NULL`) on every opcode also incurs a significant performance hit.

Thus, the existence of the hook is only checked once: at vector entrance.
The VM will then choose to either execute with or without a hook.
There are 2 variants of the same interpreter loop, one for each case.
That is why the implementation is put in a header file ([src/vm/exec.h](../src/vm/exec.h)).
[vm.c](../src/vm/vm.c) redefines the macro to enable/disable the hook and includes the header twice.

## DEO2 quirks

The reference implementation of uxn handle a `DEO2` by trapping the write a the high address.
This requires handling it at the label address plus 1 which can be awkward.
The uxntal code writes to `0x02` (`System/expansion`).
Searching for `0x02` inside the system device implementation would yield no result and only `0x03` is present.

buxn-vm does the following:

1. Write the entire short to the device memory buffer.
2. Trigger the `DEO` handler at both the low and the high address in that order.

This lets the device handling code trap the write at either address, preferably the low address since that's the one declared in the uxntal code.

In practice, this makes little difference from the reference implementation.
buxn-vm passes the official [opctest](https://git.sr.ht/~rabbits/uxn-utils/blob/main/cli/opctest/src/opctest.tal) and runs several ROMs without issues.

Strictly speaking, compared to the reference implementation, this sequence "leaks" the high byte to the low port, making it visible early.
In practice, short ports work as a whole unit and different byte ports are independent so this has not caused any issues.
