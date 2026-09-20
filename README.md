# AL4.20

AL4.20 is a small artificial-life substrate for evolving fixed-topology,
self-modifying neural organisms. It is written in C and designed for long,
deterministic experiments on laptop-class CPUs.

The first experiment asks one question: Can tiny self-modifying neural
organisms remain viable and evolve when age is the only explicit reward?

> [!WARNING]
> This project is experimental research software. Do not use untrusted
> configuration or checkpoint files. See [Security](SECURITY.md).

## What the simulation includes

- A fixed-topology recurrent `tanh` network for each organism.
- Substrate-provided age, reward, calendar, self-state, and communication
  inputs.
- A small communication vector delivered by the host.
- Bounded Hebbian changes to recurrent weights during an organism's life.
- Sexual reproduction through per-gene recombination and bounded mutation.
- A byte-based ecosystem capacity instead of a fixed population count.
- An immutable rule that prevents execution and kills all organisms on
  April 1.
- JSON Lines event logs, terminal summaries, and binary checkpoints.

AL4.20 does not include arbitrary organism code, topology evolution,
backpropagation, task rewards, a graphical world, GPU support, or networking.

## Build

You need a C11 compiler, `make`, and a POSIX-like system.

```sh
make
make test
```

The build creates `build/alife`. The default build uses strict warnings. During
development, also run the sanitizer target when your compiler supports it:

```sh
make sanitize
```

## Run the first experiment

Run the included small experiment from the repository root:

```sh
./scripts/run-small.sh
```

The script builds the program if needed and uses `configs/small.conf`. You can
also invoke the CLI directly:

```sh
build/alife run --config configs/small.conf
```

The small configuration writes events to `small-events.jsonl` and periodically
replaces `small.chk`.

To periodically replace a checkpoint file while a run is active, set a
positive checkpoint interval and checkpoint path in the configuration. You can
also provide or override the checkpoint destination on the command line:

```sh
build/alife run --config configs/small.conf --checkpoint run.chk
```

Resume and inspect a checkpoint with the same executable build that created it:

```sh
build/alife resume --config configs/small.conf --checkpoint run.chk
build/alife inspect --checkpoint run.chk
```

`inspect` reads the checkpoint header and prints one JSON object with the
checkpoint version, configuration fingerprint, tick, simulated date,
population, births, and deaths. It does not load or execute organisms.

Configuration files use one `key=value` entry per line. Blank lines are
ignored, and `#` starts a comment. Unspecified keys retain documented defaults.
The program rejects duplicate, unknown, or out-of-range values before starting
the simulation. See the files in
[`configs/`](configs/) for runnable examples. `configs/small.conf` is a quick
smoke experiment. `configs/long-24h.conf` is the starting point for an
approximately 24-hour run on the target machine; benchmark it locally and
adjust `tick_count` before relying on its wall-clock duration. See
[Performance notes](docs/performance.md) for the baseline profile and
calibration guidance.

### Configuration keys

| Key | Meaning |
| --- | --- |
| `seed` | Deterministic simulation seed. |
| `input_size` | Non-communication input width from 6 through 32. |
| `hidden_size` | Recurrent hidden-state width from 4 through 64. |
| `communication_size` | Message input and output width from 1 through 8. |
| `initial_population` | Number of related seed organisms; version 1 requires `2`. |
| `capacity_bytes` | Maximum total byte cost of living organisms after capacity enforcement. |
| `maturity_age` | Minimum age in ticks for reproduction. |
| `reproduction_ramp_ticks` | Ticks over which opportunity rises after maturity. |
| `reproduction_base_probability` | Opportunity probability at maturity. |
| `reproduction_max_probability` | Maximum opportunity probability at the end of the ramp. |
| `mutation_probability` | Independent probability of mutating a recombined gene. |
| `mutation_magnitude` | Maximum absolute mutation step. |
| `plasticity_limit` | Absolute bound for a lifetime recurrent-weight delta. |
| `plasticity_decay` | Baseline per-tick retention factor for lifetime plasticity. |
| `max_abs_weight` | Absolute bound for neural weights. |
| `calendar_start_year` | Initial simulated Gregorian year. |
| `calendar_start_month` | Initial simulated month. |
| `calendar_start_day` | Initial simulated day of month. |
| `ticks_per_day` | Number of simulation ticks per calendar day. |
| `tick_count` | Terminal tick for a new or resumed experiment. |
| `summary_interval` | Positive tick interval for terminal and summary records. |
| `checkpoint_interval` | Positive tick interval for scheduled checkpoints. |
| `checkpoint_path` | Destination for scheduled binary checkpoints. |
| `event_log_path` | Destination for JSONL events. |
| `logging_level` | `error`, `summary`, or `events`. |
| `april_1_behavior` | `terminate` to stop after extinction or `reseed` to create two seeds on the next permitted date. |

Probabilities range from `0` through `1`. Sizes, paths, date fields, and numeric
bounds are validated before any organism storage is allocated.

Event records go to the configured JSONL log. Periodic concise summaries go to
standard error so you can monitor a run without parsing the event stream. See
[Log format](docs/log-format.md) before writing analysis tools.
Use `summary` or `events` for research runs so the log preserves every birth
and death cause. The `error` level is intended for tests and diagnostics that
do not require a lifecycle record.

At normal completion, the CLI writes `final_tick`, final living `population`,
and a hexadecimal `state_hash` to standard output. Use the state hash as a quick
same-build reproducibility check.

## How the model works

1. **Organism representation.** An organism is host-owned data: a genome,
   recurrent neural state, mutable recurrent weights, age and reward counters,
   a communication vector, and lineage metadata. The topology is fixed for a
   run.
2. **Self-modification.** After network execution, a local Hebbian rule updates
   eligible recurrent weights. Heritable coefficients control the rule. The
   host clamps every update and weight to configured limits.
3. **Inheritance and mutation.** Each offspring needs two parents. The host
   independently selects or blends each gene from the parents, then applies
   probability-controlled, magnitude-bounded mutation and validates the result.
4. **Reproduction authorization.** Neural outputs can request and accept a
   pairing. Only the host can choose a distinct eligible partner, validate both
   decisions, create an offspring, and allocate its storage.
5. **Age reward.** A living organism gains reward as simulation ticks advance.
   Its age and accumulated reward are neural inputs. No task reward exists in
   version 1.
6. **Maturity and opportunity.** Organisms below the configured maturity age
   cannot reproduce. At and above maturity, a configurable monotonic age curve
   increases reproductive opportunity.
7. **Capacity-driven death.** Capacity is measured in bytes used by living
   organisms. After births, the host repeatedly removes the oldest organism
   until the population fits. Version 1 has no random old-age death.
8. **April 1 enforcement.** The host advances the simulated calendar before
   neural execution. On April 1, it kills all living organisms and executes
   none. If configured, it seeds a new population after the date advances.
9. **Sandbox boundaries.** Organisms never run native code. They cannot access
   allocation functions, files, processes, networks, function pointers,
   arbitrary addresses, or host state. The C interpreter is the boundary; it
   is not a hardened parser for hostile checkpoint or configuration files.
10. **Running and inspecting.** Start with `./scripts/run-small.sh`, watch the
    summaries on standard error, analyze the JSONL event file, and use
    `alife inspect` to view checkpoint metadata and state totals.

## Tick ordering

Each tick uses a stable host-controlled order:

1. Advance simulated time and derive the calendar date.
2. Apply the April 1 gate. On April 1, kill all living organisms before any
   network executes and skip the organism phases.
3. Build each eligible organism's inputs from host state and its previously
   delivered inbox.
4. Execute its network once in deterministic population-array order.
5. Apply and validate its bounded lifetime plasticity before moving to the next
   organism.
6. Deliver host-validated communication for the next input snapshot.
7. Resolve reproduction requests and acceptances.
8. Recombine, mutate, validate, and create approved offspring.
9. Displace the oldest organisms until the byte capacity is satisfied, and
   record all deaths and births.
10. Advance organism age and age reward, update statistics, emit scheduled
    records, and write a scheduled checkpoint.

New offspring do not execute until the next tick. This ordering is part of the
experiment definition. See [Architecture](docs/architecture.md) for details.
The substrate also applies the April 1 gate to the configured starting date
before the first calendar advance.

## Determinism and checkpoints

Given the same executable build, configuration, seed, and checkpoint input,
AL4.20 consumes random values in a defined order and reproduces a run. Floating
point and binary-layout differences can prevent bit-for-bit reproduction across
compilers or platforms.

Checkpoints have a magic value and format version and are intended for resume
with the exact build that wrote them. The reader rejects incompatible metadata
and invalid dimensions. Checkpoints are not a stable interchange format.

## Project status and scope

Version 1 favors a clear single-process implementation, contiguous state, and
low-overhead buffered logs. Profile representative runs before adding threads,
SIMD, or accelerators. Likely future work includes alternative reward providers,
new mating policies, topology evolution, and platform-independent checkpoint
encoding.

Read [Contributing](CONTRIBUTING.md) before sending a change. By participating,
you agree to follow the [Code of Conduct](CODE_OF_CONDUCT.md).

## License

AL4.20 is available under the [MIT License](LICENSE).
