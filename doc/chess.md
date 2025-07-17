# chess - The uxntal type checker

> It's named chess because chess is better than checker

chess is a type checker for uxntal using [symbolic execution](https://en.wikipedia.org/wiki/Symbolic_execution).

It supports the [standard stack notation](https://wiki.xxiivv.com/site/uxntal_notation.html) and some other extensions described later.
All annotations reside entirely in comments.
Thus, they do not affect code generation and can be ignored by the assembler.

Beside checking stack annotation against implementation, it can also do the following:

* [Nominal typing](#nominal-typing)
* [Annotation-less checking](#annotation-less-checking)
* [Static stack printing](#stack-printing)
* [Escape hatch](#escape-hatch): For when the checker is not smart enough
* Detect common errors such as:
  * [Load and store using non-address values](#address-semantics): A sign of wrong argument order
  * Load and store to code region
  * Execution of data region
  * Wrong termination type: `JMP2r` from a vector or `BRK` from a regular subroutine
  * Infinite self-recursion

It is integrated into the [frontend](./asm-frontend) and enabled using `--chess`.

## Nominal typing
### Introduction

Since uxntal only has a number type, it is easy to make mistakes with argument orders.
A [nominal type system](https://en.wikipedia.org/wiki/Nominal_type_system) is introduced to alleviate the problem.
Simply put, one can give names to those numbers.
Numbers with different names are not compatible and will result in a type error.

In more concrete term, consider the following declaration:

```
@make-card ( Suit value -- Card )
    ( implementation redacted )
```

This declares a subroutine which accepts 2 arguments: the first is a byte of the nominal type `Suit` and the second is an arbitrary byte.
It returns a byte of the type `Card`.

Nominal types must start with an uppercased letter to distinguish them from generic numbers.
This takes inspiration from many written languages where a proper name must be capitalized.

Now, assuming that we also have the following:

```
(doc Return the value of a card )
@value-of ( Card -- value )
    ( implementation redacted )

(doc Return the suit of a card )
@suit-of ( Card -- Suit )
    ( implementation redacted )
```

We can now define a subroutine that increase the value of a card:

```
@inc-value ( Card -- Card )
    DUP value-of INC ( get value of card and increase )
    SWP suit-of ( get suit )
    make-card ( rebuild the card )
    JMP2r
```

There was an error in the above snippet.
Namely, the order of the `Suit` and `value` arguments are swapped.
A regular arity checker would not catch this.
But `chess` will report at the `make-card` line:

> Input working stack #0: A value of type "Suit" (Suit from card.tal:1:14:13) cannot be constructed from a value of type "" (value′ from card.tal:16:9:317)

### Constructing a nominally-typed value

A generic number is simply promoted to a nominally-typed value upon return:

```
@make-card ( Suit value -- Card )
    #20 SFT ( use 2 bits to represent 4 suits )
    ORA ( Or in the suit )
    JMP2r
```

This makes it easy to write "constructor".
However, a nominally-typed value will not be converted to a different type:

```
@make-card ( Suit value -- Card )
    POP
    JMP2r
```

The above snippet will receive the following report:

> Output working stack #0: A value of type "Card" (Card from card.tal:1:28:27) cannot be constructed from a value of type "Suit" (Suit from card.tal:1:14:13)

A number can also be casted into a nominally typed value with the `( type ! )` annotation:

```
%spade { #01 ( Suit ! ) }

spade #03 make-card ( 3 of spade )
```

This syntax will be explained further below.

### Manipulating a nominally-typed value

If a nominally-typed value is modified in anyway (`INC`, `SUB`, `MUL`...), it immediately loses its nominal type:

```
%spade #03 make-card INC ( this is now just a byte )
suit-of ( This is now an error )
```

The real meaning of a nominal type is user-defined and thus, chess takes the conservative approach when it comes to manipulation.
In practice, a nominally-typed value should always be manipulated using properly designed functions that accept or return them instead of directly with low level primitives.

### Enumeration

As a special case, all labels in the zero page will be promoted to a nominally typed value of the same name when used as a literal:

```
|00 @Suit
    &spade $1
    &club $1
    &heart $1
    &diamond $1

|0100
.Suit/spade #03 make-card ( 3 of spade )
POP BRK

@make-card ( Suit/ value -- card )
    ( implementation redacted )
```

The trailing slash just means the routine accepts any value whose type starts with `Suit/`.
For example, `.Suit/spade` has the value `0` and nominal type: `Suit/spade`, the same as its label name.
This matches the `Suit/` requirement.

"Nominal subtyping" using prefix matching is an experimental concept.
Strictly speaking, `Suit-yourself` is a subtype of `Suit` under the current implementation.
This can be unexpected.
Subtyping might be restricted to just segments of a slash separated string in the future, should this become problematic.

## Address semantics

All load and store instruction (e.g: `LDR`, `STA`) requires the address argument to have address semantics.
That is, it must satisfy one of the following conditions:

1. Be a literal constant (e.g: `#1337`)
2. Be a literal label (e.g: `=write-addr`)
3. Be annotated with address semantics
4. Be an offset of one of the above

The last condition is a special notation used in signature: `[name]`.
For example:

```
@read-one-past-zero-page-addr ( [addr] -- value )
    INC LDZ
    JMP2r
```

Without it, there would be a warning on `LDZ`:

> Load address (addr′ from card.tal:21:33:443) is not a constant or an offset of one

For a 16 bit address, combine it with the `*` suffix: `[addr]*`.

`DEO` and `DEI` are also treated as load and store (to devices).
As of now, there is no safe guard for misusing address values from the 2 separate address spaces.
e.g: Using a device address in `LDA` or using a memory address in `DEI`.

## Escape hatch

There are times when the checker is not smart enough.
In that case, the user can make a type assertion with `!`.
There are two types of assertion: stack and signature.

### Stack assertion

Stack assertion has been shown earlier in the nominal typing section.
However, the general syntax allows asserting any number of top stack items.
For example: `( a* Color . [addr]* ! )` asserts that:

* The top 3 bytes of the working stack are:
  * a generic short
  * a byte of the type `Color`
* The top of the return stack contains a short address

The user can freely assign nominal type or address semantic to the stack items as they wish.
"Over assertion" is still an error:

```
@routine ( a b -- )
    POP ( one two ! )
    JMP2r
```

The above will result in this error:

> Working stack size mismatch: Expecting at least 2 ( one two ), got 1 ( a )

#### Naming the stack items

Stack assertion is also useful in giving more meaningful names to stack items to help with debugging or error messages.
You might have noticed that the name `addr′` is mentioned in several places.
chess generates display name for stack items:

* A literal number has its value as the name.
  e.g: `#03` will show up as `0x03` in error messages.
* When a value is modified in-place (i.e: `INC`), a prime ('′') character is appended to its name.
  e.g: `addr* INC2` creates `addr′*`
* When a short value is split into 2, the 2 halves will the the original name suffixed with `-hi` and `-lo`.
  e.g: `short* SWP` will create `short-lo short-hi`.
* When 2 values are combined (e.g: `ADD`), their names are joined with a dot (·)
  e.g: `a b ADD` will create `a·b`

Moreover, the origin of each value is tracked throughout the symbolic execution so chess can report which opcode or routine created a value.
But the generated names can become hard to read.
This is where the user can use stack assertion to simplify the error messages.

### Signature assertion

A signature ending with `!` is trusted:

```
@trust-me-bro ( a b -- ! )
```

This will not be checked by chess.
However, its stack effect will still be used to check the call site:

```
@stack-underflow ( a -- )
    trust-me-bro
    JMP2r
```

The above will generate an error at: `trust-me-bro`:

> Input working stack size mismatch: Expecting at least 2 ( a b ), got 1 ( a )

This can be useful for runtime generated code or highly dynamic code (e.g: indirect jump).

### Assertion and macro

Macros are inlined to the application side.
As such, any annotation on the macro itself has no effect and serves purely as documentation:

```
%SWP-POP ( a b -- b ) { SWP POP }
```

However, annotations within the macro's body will be applied accordingly:

```
%as-card { ( Card ! ) }

#15 as-card ( This number will now be considered to have the nominal type: Card )
```

Thus, macros can be used to create shortcut for nominal casting or constants.

## Annotation-less checking

Inherently, chess does not require any annotation at all to do arity checking.

Without annotation, chess will by default, assume that the address `0x0100` is a vector with the signature: `( -> )`.
It will perform type checking as usual against all directly reachable code.
Zero page address will still automatically have nominal types (albeit useless without signature annotation).
Address of labels will still have address semantic for load/store checking.

Getting started examples such as "Hello World" can be immediately verified without further actions.
This could be useful for newcomers who just started learning.

This is just a side effect of using symbolic execution under the hood.
However, user annotation helps in the following ways:

* User annotation can give more meaningful names to error messages.
* Nominal type can help to reduce errors.
* Vectors other than the reset vector cannot be discovered through symbolic execution.
  chess makes no assumption about device layout or vector signature so writes to devices (`DEO`) are largely ignored.
  The only verification done is stack effect and address semantics mentioned above.
* Whenever an annotated routine is called, chess will just apply the signature directly to the stack and then verify the routine's body separately later.
  This will greatly reduce verification time especially if the routine is called in many places.

  Furthermore, it will also catch errors much earlier: at the call site.
  Without signature annotation, stack errors will only be reported when an overflow or underflow happens, or when the reset vector terminates (`BRK`) with a non-empty stack.

Thus, annotation is still recommended, especially at the "border".
However, internal helpers or loop labels do not have to be annotated.
chess also does not require any special syntax for loop labels to be verified.

## Stack printing

Finally, to help with debugging stack errors, chess can print out the (statically inferred) stack at a certain point in the program.
This is done by writing the special value: [2b](https://nier.fandom.com/wiki/YoRHa_No.2_Type_B) to the `System/debug` port (`0e`):

```
@test-print ( one two* -- )
    #2b0e DEO
    JMP2r ( yes, there is also an error here )
```

This will print:

```
Stack:
WST(3): one two*
RST(2): [RETURN]*
```

Using the [language server](https://github.com/bullno1/buxn-ls), this message can be viewed directly in the editor as an informational diagnostic message without running the assembler.

### Stack printing and macro

Due to the implementation of macro, putting the above sequence into a macro will not have the desired effect:

```
%show-stack { #2b0e DEO }

@test-print ( one two* -- )
    show-stack
    JMP2r ( yes, there is also an error here )
```

The message will still be printed, however, it is attached to the `DEO` opcode at the macro definition site instead of the `show-stack` application site.
Internally, a macro is a sequence of tokens, spliced into the token stream at the application site while still retaining the original location at the definition site.
This helps tools such as the debugger to be able to step through a macro's body just like with a function.
However, the unintended effect is that location info can be unintuitive.
