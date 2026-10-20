# CS 238P Project 2

User-level thread scheduler. Build it on Linux, or on Windows with WSL.

## Normal demo

Threads call `scheduler_yield()` after each line.

```bash
make
./cs238
```

Five threads print 100 lines each. New threads are added at the front of the list, so the order is `course!`, `this`, `love`, `world`, `hello`.

## Extra credit

`demo/extra_credit.c` is the same kind of program, but no thread calls `scheduler_yield()`. A one-second timer switches them.

```bash
make extra_credit
```

`gamma` runs first. About once a second the name changes. The run takes about 6 seconds.

## Tests

```bash
make test
```

The last line should be `0 failed`. This takes about 10 seconds.

To run one test:

```bash
./test_sched rr
```

Names: `all`, `empty`, `only`, `rr`, `life`, `stack`, `many`, `sleep`, `cpu`, `twice`.

## Clean

```bash
make clean
```
