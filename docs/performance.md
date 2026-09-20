# Performance notes

Version 1 uses one process and one deterministic execution thread. Measure the
release build on the intended host before starting a long experiment.

## Create a baseline profile

Profile the bundled small configuration with GCC instrumentation:

```sh
make clean
make CFLAGS='-O2 -g -pg' LDFLAGS='-pg'
build/alife run --config configs/small.conf
gprof build/alife gmon.out
```

Record the commit, compiler, flags, final state counts, and state hash with the
profile. Lifecycle behavior changes the mix of awake neural execution, asleep
plasticity, and low-cost off scheduling, so a result from an earlier lifecycle
or configuration is not a comparable baseline. Instrumentation can also change
floating-point code generation. Compare deterministic state hashes only
between identical builds.

The initial rhythm reuses two neurons in the existing recurrent network. It
adds no per-tick allocation, second network, search, thread, or GPU dependency.

## Calibrate a long run

Run the small configuration first and record wall time, final population, and
birth rate. The cost of a tick grows roughly with the living population and
with `hidden_size * hidden_size`. Checkpoint cost grows with population bytes,
and detailed log volume grows with births and deaths.

`configs/long-24h.conf` is a starting point for a laptop-class CPU. Its one
million ticks span 250 simulated days, but its wall time depends strongly on
whether and when the population reaches capacity. Adjust `tick_count`,
capacity, summary interval, and checkpoint interval after measuring the target
machine.

## Likely optimization work

Profile before changing deterministic semantics. Promising areas include:

- reducing duplicate validation scans while preserving corrupt-state checks;
- blocking or vectorizing the dense recurrent and plasticity loops;
- replacing the linear oldest-organism scan if displacement becomes hot; and
- moving large checkpoint writes off the critical path without changing the
  snapshot tick.

Any parallel or vector implementation must preserve PRNG consumption,
floating-point reduction order, ID ordering, and equal-age tie-breaking if it
claims bit-for-bit replay compatibility.
