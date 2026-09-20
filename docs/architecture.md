# Architecture

AL4.20 separates immutable substrate policy from mutable organism data. Keep
that separation intact when you extend the system.

## Trust boundary

The substrate is ordinary compiled C code. It owns the simulation clock,
random-number generator, population storage, execution loop, validation,
reproduction, communication, logging, and checkpoint I/O. An organism is a
record interpreted by that code. It is never a process or dynamically loaded
program.

The organism can affect the simulation only through fixed numerical outputs.
It cannot call `malloc`, `free`, the shell, operating-system process APIs,
filesystem APIs, networking APIs, dynamic libraries, or function pointers. It
cannot select arbitrary memory addresses. Array bounds and dimensions come from
validated substrate configuration, not neural output.

This model contains evolved data safely when the host is correct. It is not an
adversarial sandbox for hostile files. A deliberately modified configuration or
checkpoint may exercise bugs in the native parser. Run only trusted inputs and
see [Security](../SECURITY.md).

## Organism representation

Every living organism contains:

- a unique ID and two parent IDs;
- birth tick and calendar date;
- age, accumulated age reward, and genome generation;
- initial/feed-forward neural genes and plasticity genes;
- lifetime-mutable recurrent weights;
- recurrent activations and the previous delivered message;
- reproduction request and acceptance outputs; and
- counters used for observability.

The network has a fixed topology for one run. Let `I` be `input_size`, `H` be
`hidden_size`, and `C` be `communication_size`. The input vector has `I`
substrate values and `C` delivered message values. Substrate values include
normalized age and reward, simulated calendar signals, and bounded self and
population signals. Hidden and output activations use `tanh`.

The genome stores these arrays:

| Array | Gene count |
| --- | ---: |
| Input-to-hidden weights | `H * (I + C)` |
| Base recurrent weights | `H * H` |
| Hidden biases | `H` |
| Hidden-to-output weights | `(C + 2) * H` |
| Output biases | `C + 2` |
| Plasticity rates | `H` |
| Plasticity decays | `H` |

The total heritable parameter count is therefore
`H*(I+C) + H*H + H + (C+2)*H + (C+2) + 2*H`. Configuration validation requires
500 through 2,000 parameters. The default dimensions, `I=8`, `H=24`, and `C=2`,
produce 988 heritable parameters.

The first substrate inputs have fixed meanings:

| Index | Value |
| ---: | --- |
| 0 | Age, normalized against the maturity and opportunity ramp. |
| 1 | Accumulated age reward, normalized on the same scale. |
| 2 | `1` at or above sexual maturity; otherwise, `0`. |
| 3 | Current age-dependent reproduction opportunity. |
| 4 | Living population bytes divided by configured capacity. |
| 5 | Fractional position in the simulated year. |
| 6 | Simulated month divided by 12, when `I` is at least 7. |
| 7 | Simulated day divided by 31, when `I` is at least 8. |

Additional substrate inputs are reserved and start at zero. The `C` inbox
values follow the `I` substrate values.

Runtime state adds `H` hidden activations, `C` inbox values, `C` outbox values,
and `H * H` lifetime recurrent deltas. Network evaluation uses the base
recurrent weights plus those bounded deltas.

The output vector contains the next outgoing message plus reproduction request
and acceptance channels. An output is a proposal only. The substrate interprets
and validates it.

The topology is represented separately from genome values so a future format
can describe other topologies. Version 1 does not grow or rewire a network.

## Initial population

The version 1 experiment starts with exactly two organisms. The substrate uses
the configured seed to generate related but non-identical valid genomes. Both
organisms use the same representation and mating rules; neither has a symbolic
name or a fixed sex. Seed births have parent IDs of zero.

## Lifetime plasticity

Organisms do not use backpropagation. After an execution step, the substrate
applies a local Hebbian update to recurrent lifetime deltas. For connection
`j -> i`, the update is conceptually:

```text
rate[i] = 0.05 * tanh(rate_gene[i])
decay[i] = clamp(configured_decay + 0.01 * tanh(decay_gene[i]), 0, 1)
delta[i,j] = clamp(
    decay[i] * delta[i,j] + rate[i] * old_hidden[j] * new_hidden[i])
```

The rate and decay genes are heritable per hidden unit. Configuration provides
the decay baseline, lifetime-delta limit, and absolute weight limit.
The substrate rejects non-finite state and clamps permitted changes. Lifetime
deltas affect only the organism that learned them. The genome retains the base
weights, plasticity rates, and plasticity decays used to construct descendants.

## Reward interface

The version 1 reward provider returns survived age only. Each completed living
tick advances both age and accumulated reward, and both values return as bounded
network inputs on a later tick.

Reward calculation is a substrate service, not reproduction code. A later
experiment can add an external computational reward without changing organism
allocation, validation, communication, or population management.

## Reproduction and inheritance

The neural outputs expose request and acceptance signals. The substrate applies
all authorization rules. A birth requires:

- two distinct living organisms;
- request and acceptance signals that satisfy the mating policy;
- both organisms at or above the configured maturity age;
- both organisms passing the monotonic age-opportunity gate;
- available, valid dimensions for an offspring; and
- a child genome that passes finite-value and bounds validation.

The host grants each eligible organism an opportunity with its age-dependent
probability, shuffles the resulting candidates with the simulation PRNG, and
pairs adjacent candidates. This randomized order remains reproducible for the
same run inputs. The host never treats an organism output as an allocation or
spawn instruction.

For every gene, the recombination routine selects either parent with 45%
probability each or averages both values with 10% probability. It then
independently considers a bounded mutation according to the configured
probability and magnitude. The host clamps or rejects values that violate the
genome contract. The child records both parent IDs and a generation one greater
than the greater parent generation.

Before maturity, every request is rejected. After maturity, a simple
linear function increases reproductive opportunity with age:

```text
progress = clamp((age - maturity_age) / reproduction_ramp_ticks, 0, 1)
opportunity = base_probability
              + progress * (max_probability - base_probability)
```

The random draw comes from the simulation's deterministic PRNG.

## Communication

Each organism produces a small fixed-size numerical message. The substrate
copies validated values into host-owned delivery storage. For each channel, an
organism receives the mean output of all other living organisms from the
previous execution phase. A lone organism receives zero. Organisms never share
pointers or directly mutate one another's state.

Version 1 communication is deliberately low bandwidth and has no language,
addresses, network transport, or persistent external channel.

## Population storage and capacity

Capacity is a byte limit, not an organism-count limit. The substrate calculates
each organism's storage cost from checked dimensions and tracks the total for
living organisms. A birth can temporarily cross the limit. The capacity phase
then repeatedly removes the oldest living organism and recalculates the total
until it fits.

Stable ID order breaks equal-age ties. This makes displacement deterministic.
The death record identifies `capacity` as the cause. There is no random or
maximum-age death in version 1.

Population metadata and topology-sized neural slots live in separate contiguous
arrays that grow geometrically. Normal neural execution allocates nothing. A
birth uses host-owned temporary genome storage, and only the substrate can grow
the population arrays.

## Calendar and April 1

Simulation time advances by a configured calendar rate rather than wall time.
The substrate derives date inputs and owns all date transitions. Organism state
cannot write the clock.

The April 1 check runs before input construction or network execution. It
checks the configured starting date before the first advance and checks every
date produced by later calendar advances. If the date is April 1, the
substrate:

1. marks every living organism dead with cause `april_1`;
2. emits the death records;
3. skips execution, plasticity, communication, and reproduction; and
4. optionally waits for the next simulated date and reseeds two related,
   non-identical organisms if reseeding is enabled.

This gate is outside the mutable state and cannot be disabled by neural output.

## Tick transaction

The following order defines a tick:

1. **Time:** Advance the tick clock and simulated calendar.
2. **Calendar policy:** Enforce April 1 before any organism executes.
3. **Inputs:** Build age, reward, date, self, population, and prior-message
   inputs for the next eligible organism.
4. **Execution:** Evaluate each eligible recurrent network once in deterministic
   population-array order.
5. **Plasticity:** Validate activations and apply bounded recurrent updates.
6. **Communication:** Validate and stage output messages for later inputs.
7. **Pairing:** Evaluate request, acceptance, maturity, and age opportunity.
8. **Birth:** Recombine and mutate genes, validate offspring, and allocate
   approved births.
9. **Capacity and events:** Remove oldest organisms until the byte total fits;
   record births, deaths, and reproduction results.
10. **Accounting:** Advance age and reward for survivors, update summaries, and
    perform scheduled logging and checkpoint work.

The loop performs input, execution, and plasticity phases per organism. It can
remove invalid state during that pass, but it does not admit births until the
pass finishes. A newborn starts with age zero and first executes on the next
tick. A death has one substrate-assigned cause.

## Validation and failure handling

Validate configuration before allocation. At runtime, validate organism
dimensions, finite neural values, checked size calculations, bounded indices,
and offspring genomes before committing a state change. Invalid or corrupt
organism state causes a recorded `invalid_state` death. A condition that could
indicate substrate corruption stops the experiment instead of continuing with
undefined state.

On normal CLI completion or controlled shutdown, record every remaining
organism with cause `shutdown` before the terminal `run_end` record.

## Determinism

The simulation uses one explicit-state PRNG and stable iteration and tie-break
orders. A random decision must consume values only from this PRNG. Do not use
wall-clock state, process IDs, address values, or unordered parallel reduction
in simulation decisions.

The reproducibility contract covers the same executable build, configuration,
seed, and input checkpoint. It does not promise identical floating-point output
across architectures, compilers, math libraries, or optimization modes.

## Checkpoints

Checkpoints are versioned binary snapshots optimized for efficient local
resume. A header identifies the file, format version, configuration fingerprint,
compatibility metadata, and configured dimensions. The body includes the clock
and calendar state, PRNG state, ID allocator, statistics, living organism
records, genomes, neural state, lifetime weights, and pending communication
required for exact continuation.

The reader checks all counts and sizes before using them and rejects incompatible
versions or dimensions. Even with those checks, treat checkpoint files as
trusted input. Use the exact executable build to resume. The current encoding
is portable enough for ordinary local storage but is not a cross-version,
cross-ABI interchange standard.

## Extension points

Keep these policies behind narrow host interfaces:

- reward calculation;
- mate eligibility and pairing;
- recombination and mutation;
- communication aggregation and delivery;
- logging sinks; and
- topology descriptors.

Profile a realistic run before changing the single-process design. Useful
measurements include network evaluation time, plasticity time, log volume,
checkpoint latency, bytes per organism, and cache behavior.

Likely optimization opportunities are the dense recurrent and plasticity
loops, the linear oldest-organism scan after each birth, and checkpoint write
latency at large capacities. Consider loop blocking or compiler-assisted
vectorization first, then an age index for capacity displacement. Preserve PRNG
consumption, reduction order, and tie-breaking when optimizing; naive
threading can break deterministic replay. Keep buffered lifecycle logging so
formatting does not add a flush to the hot path.
