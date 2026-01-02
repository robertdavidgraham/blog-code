fastip - sample IPv4 parsers
===

This project includes some sample "parsers" for IPv4 addresses
and benchmarks them.

They are mostly vibe coded and not optimized. The intent is to 
show different parsing techniques for parsers in general, not
optimize specific ones.

This project is specifically about using benchmark tools
to investigate such things as "branch misses", to figure
out where performance losses come from.

On *macOS* and *Linux*, just type:
```
make
```

This will build the `fastip` program. To run the benchmarks,
do this following:

```
sudo fastip
```

It needs root priveledges to get CPU performance counters.
If you run without root, the counters will be zero.

Running it produces output like the following. This is on
a MacBook Air M3, with a 4.096-GHz p-core.

```
[warmup]   3.8-GHz  12.1-ns   45  153  3.3   40  1.4  0.1    [0x00000000]
==[p-cores]============
[      ]  freq      time    cycl inst  ipc brch miss  l1d      checksum
[   ai ]   4.0-GHz   5.3-ns   21  153  7.1    0  0.0  0.0    [0x00000000]
[   ai+]   4.0-GHz  11.2-ns   45  153  3.4    0  0.0  0.0    [0x00000000]
[ swar ]   4.0-GHz  12.3-ns   49  272  5.5    0  0.0  0.0    [0x00000000]
[ swar+]   4.0-GHz  12.3-ns   49  272  5.5    0  0.0  0.0    [0x00000000]
[ from ]   3.9-GHz  20.5-ns   79  500  6.3    0  0.0  0.0    [0x00000000]
[ from+]   3.9-GHz  26.7-ns  104  500  4.8    0  0.0  0.0    [0x00000000]
[  dfa ]   4.0-GHz  27.5-ns  110  233  2.1    4  0.0  0.0    [0x00000000]
[  dfa+]   4.0-GHz  28.2-ns  113  233  2.1   22  0.7  0.0    [0x00000000]
[  fsm ]   4.0-GHz  16.5-ns   66  341  5.1    0  0.0  0.0    [0x00000000]
[  fsm+]   4.0-GHz  22.4-ns   90  340  3.8    0  0.0  0.0    [0x00000000]
[ fsm2 ]   4.0-GHz  13.5-ns   54  292  5.4   76  0.2  0.0    [0x00000000]
[ fsm2+]   4.0-GHz  18.3-ns   74  292  3.9    0  0.0  0.0    [0x00000000]
[ neon ]   4.0-GHz   9.9-ns   38  237  6.1    0  0.0  0.0    [0x00000000]
[ neon+]   4.0-GHz  17.9-ns   72  237  3.3    0  0.0  0.0    [0x00000000]

[warmup]   1.1-GHz  53.2-ns   60  153  2.5   40  1.6  0.0    [0x00000000]
**[e-cores]************
[      ]  freq      time    cycl inst  ipc brch miss  l1d 
[   ai ]   1.1-GHz  31.8-ns   34  153  4.4   40  0.3  0.0    [0x00000000]
[   ai+]   1.1-GHz  54.6-ns   59  153  2.6   40  1.6  0.0    [0x00000000]
[ swar ]   1.1-GHz  77.6-ns   84  272  3.2    2  0.0  0.0    [0x00000000]
[ swar+]   1.1-GHz  78.4-ns   84  272  3.2    2  0.0  0.0    [0x00000000]
[ from ]   1.2-GHz 113.5-ns  134  501  3.7   94  1.5  0.0    [0x00000000]
[ from+]   1.1-GHz 126.2-ns  136  501  3.7   94  1.7  0.0    [0x00000000]
[  dfa ]   1.1-GHz 116.4-ns  124  234  1.9   22  0.7  0.0    [0x00000000]
[  dfa+]   1.1-GHz 112.3-ns  124  234  1.9   22  0.7  0.0    [0x00000000]
[  fsm ]   1.1-GHz 107.7-ns  117  341  2.9  109  1.4  0.0    [0x00000000]
[  fsm+]   1.1-GHz 112.2-ns  122  341  2.8  109  1.7  0.0    [0x00000000]
[ fsm2 ]   1.1-GHz  80.8-ns   91  292  3.2   75  1.4  0.0    [0x00000000]
[ fsm2+]   1.1-GHz  87.0-ns   95  292  3.1   75  1.7  0.0    [0x00000000]
[ neon ]   1.1-GHz  77.5-ns   83  238  2.9   41  1.2  0.0    [0x00000000]
[ neon+]   1.1-GHz  83.1-ns   90  238  2.6   41  1.7  0.0    [0x00000000]

```

The thing to notice is how the `[ai ]` algorithm on the *p-core* has
an IPC of *7 instructions-per-clock*. This is a surprisingly large
number, which I created this project to investigate.

My conclusion is that it's due to tricks within the *branch-prediction*
logic, and that as test-data sizes increase, this advantage goes away.
The `+` version of the benchmarks increases the test data by 100x,
which causes that algorithm to drop back down in speed to a normal
expected amount.

It's run twice, once for the *performance-cores* and again for the
*efficiency-cores*.

I run a *warmup* benchmark for each core in order to get them up
to speed.

The columns are:

    - `freq` - This is the calculated frequence the core is running
        at. You see that the p-cores are running at full 4.1-GHz.
        The e-cores are rated at 2.4-GHz, but the scheduler is
        keeping them at 1.1-GHz. This is calculated from the next
        two numbers (time and cycles).
    - `time` - This is the number of nanoseconds it takes to parse
        a single IPv4 address.
    - `cycle` - This is the number of *clock cycles* it takes to parse
       a single IPv4 address.
    - `inst` - This is the number of *instructions* executed per
        IP address.
    - `ipc` - This is the number of *instructions-per-clock-cycle*,
       calculated from the previous two numbers. That an algorithm
       can have a 7 IPC is pretty surprising.
    - `brch` - Numbr of *branches*, of all types (conditional, indirect,
       unconditional, function calls/returns), in the code. **These are
       flaky numbers**, they sometimes work, and sometimes report
       zeroes on the p-core. I don't know why.
    - `miss` - Number of *branch misses* while running the algorithm,
       which I suspect is causing a slowdown in these algorithms.
    - `l1d` - Number of *level-1 cache misses*, which is commonly
       troublesome for algorithms, but irrelevent here.
       
The algorithms are run twice, with different input sizes. The
algorithsm are:

    - `ai` - A vibe-coded parser on Daniel Lemire's blog.
    - `swar` - A parser with no branches, also vibe coced.
    - `from` - A C++ parser using `from_chars`, from the
       same Daniel Lemire post.
    - `dfa` - Shows the trick of using a regex-style DFA
       to control parsing.
    - `fsm` - A vibe coded parser using the *state machine*
       approach.
    - `fsm2` - A hand-coded parser using the *state machine*
       approach that matches the same states as in the `dfa`
       parser. This'll make sense if you study it.
    - `neon` - A vibe coded parser using the SIMD NEON
       intrinsics.
       
There are three targers for the `Makefile`:

    - `fastip`, the program that benchmarks all the algorithms.
    - `fastai`, which only runs the algorithms examining
      the ones mentioned on the Lemire blog.
    - `perfip`, which builds a binary using profiled optimizations.
      This makes some algorithms slower, others faster.
      
   
