# asm - The uxntal assembler

This consists of:

* The assembler itself:
  * [asm.h](../include/buxn/asm/asm.h)
  * [asm.c](../src/asm/asm.c)
* An annotation parser:
  * [annotation.h](../include/buxn/asm/annotation.h)
  * [annotation.c](../src/asm/annotation.c)
* A type checker:
  * [chess.h](../include/buxn/asm/chess.h)
  * [chess.c](../src/asm/chess.c)

Only the assembler is required to assemble uxntal into uxn bytecode.
The other components are optional addons.

## The assembler

This is written as a library to support integration into another program.
Several programs in this project, not just the [assembler frontend](./asm-frontend.md) depends on this library.

As such, it contains a callback API to report various types of symbols to the frontend, along with precise source location (line, column and byte offset).
Every single byte in the ROM is mapped to some type of symbols (opcode, number, label reference...).

This enables the development of:

* A language server: https://github.com/bullno1/buxn-ls
* A step debugger: https://github.com/bullno1/buxn-dbg

The assembler library also serves as the entrypoint for addon libraries such as the annotation parser and the type checker.
They contain functions that should be called within the callbacks of the assembler.
For example, `buxn_chess_handle_symbol` should receive the arguments of `buxn_asm_put_symbol`.
Refer to the source code of the [frontend](./asm-frontend.md), for more details.

### Language extensions

Beside the core uxntal language, there are also several language extensions.

#### Long string

A long string can be written as: `" This string is long"`.
It starts with a lone `"` and ends with a `"`, much similar to Forth.

There is no escaping as it is trivial to just put them in between string literals:

```
" This string has " ""quotation" " in between"
```

#### Decimal literal

A decimal literal can be written with a `+` rune.
A single `+` creates a byte literal (e.g: `+69`).
Two `+`-s create a short literal: (e.g: `++420`).

Anytime a hex literal can be used, a decimal literal can also be used.
For example:

* In padding: `|++256`
* In `LIT` short hand: `#++1024`

#### Anonymous backward reference

The label: `@` (defined using `@@`) is an anonymous backward label.
It can be referred to using any runes such as: `|@`, `!@`...

Unlike named labels, `@` can be defined multiple times in the code (by writing `@@`) without triggering a name clash error.
A reference to `@` will always target the latest occurence and "pop" it from the stack.
Thus, unlike named labels, it can only be referred to once.

This allows several idioms such as:

Padding save and restore:

```
@@ ( Save the current code offset )
( Move to 0 page )
|00 @Enum/a $1 &b $1
( Move back )
|@
( This file can be included without messing up the layout )
```

Unnamed loop:

```
@@ ( site A )

    ( nested loop )
    @@ ( site B )
    condition ?@ ( conditionally jump to site B )

!@ ( jump to site A )
```

Everything that can be done with anonymous label can also be done by explicitly naming the label instead.
But naming things a hard problem so it should be avoided unless necessary.

#### Macro with argument

A macro whose name ends with `:` will take a **single** argument which is the following token.
Within its body, the `^` character will be replaced with the argument.

For example, given this macro definition:

```
%as-lit-num: { #^ }
```

The following piece of code: `as-lit-num: 03` will expand into: `#03`.
This particular example is a rather useless macro but macro-with-argument opens the door for some simple syntactical extensions.
For example:

```
%inline-string: {
	;{ ( address of the end of the string )
	;{ ( address of the start of the string )
	SWP2 JMP2k ( jump past the string )
	} ^ ( place the string here )
	}
}

inline-string: " Hello world"
print
BRK

@print ( [start]* [end]* -- )
	SWP2 ( swap start and end )
	@@
		NEQ2k ?{ POP2 POP2 JMP2r } ( if start is equal to end, terminate )
		LDAk #18 DEO ( otherwise, load char and print )
		INC2 !@ ( loop back for next char )
```

## The annotaton parser

[Named comment](https://wiki.xxiivv.com/site/uxntal_notation.html#comments) (`(name this is named )`) is a less known feature in uxntal.
The assembler does not generate any code for comments but it reports the comment tokens to the frontend.
They can be used by external tools.

`buxn-asm-annotation` makes it easier to parse and manage named comments.
[bindgen](./bindgen.md) is a program that makes use of this library to generate a C header from a uxntal file.
This reduces the tedium in writing binding code for devices.

## The type checker

This library has its own [document](./chess.md).
