# repl - A REPL example

```
% bin/Debug/buxn-repl
> #01 #02 ( a b ! )
WST:| a| b|
    |01|02|
RST:|
    |
> SWP
WST:| b| a|
    |02|01|
RST:|
    |
>
```

This is an example of how the [symbolic stack](./chess.md) can be combined with actual stack value for a REPL.
It works by repeatedly assembling and executing [repl.tal](../src/repl.tal).

The REPL is rather limited.
