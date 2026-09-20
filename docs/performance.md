# Performance notes

Version 1 uses one process and one deterministic execution thread. Measure the
release build on the intended host before starting a long experiment.

## Baseline profile

The bundled small configuration was profiled with GCC instrumentation:

```sh
make clean
make CFLAGS='-O2 -g -pg' LDFLAGS='-pg'
build/alife run --config configs/small.conf
gprof build/alife gmon.out
```

In the development environment, the 2,000-tick run finished in less than one
second. It reached 40 living organisms, produced 450 total births, and wrote an
event log smaller than 1 MiB. Instrumentation can change floating-point code
generation, so compare deterministic state hashes only between identical
builds.

The sampled CPU profile attributed about 47% of self time to the tick loop and
its inlined neural and plasticity work. Full organism-state validation and
genome-bound validation accounted for the remaining sampled time. Logging,
checkpoint calls, capacity enforcement, and allocation did not register
measurable self time at this small population. These percentages are a
baseline, not a promise for a different compiler, population, or processor.

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
