# buxn-asm - The assembler frontend

Based on the [asm](./asm.md) library, this provides the assembler as a CLI program.
Invoking it with `--help` will show the documentation for every flag.

`--chess` will enable [type checking](./chess.md).

`--trace <number> --focus` can be used to show only a single trace, reducing noise from the output.
The trace id is the number at the beginning of every error or warning message.
E.g: "[3] Stack underflow".
This number is deterministically generated.
Therefore, given the same source code, when a number is seen in an editor through the use of the [language server](https://github.com/bullno1/buxn-ls), it can be plugged into the assembler to view the full trace of how an that error was detected.
