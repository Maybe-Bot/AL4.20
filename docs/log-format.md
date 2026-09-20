# Event log format

AL4.20 writes experiment events as JSON Lines (JSONL): one complete JSON object
per UTF-8 line. This format supports streaming analysis and recovery of all
complete records if a run stops during a later write.

Periodic human-readable summaries go to standard error. Do not parse them as a
stable data format.

The configured logging level controls volume:

- `error` writes only diagnostics to standard error.
- `summary` writes lifecycle, Fool's Day roll, checkpoint, periodic summary,
  and run-boundary records. Lifecycle records preserve every birth, state
  transition, and death cause.
- `events` adds reproduction-attempt and sampled plasticity records.

## Common fields

Each event record contains these fields when applicable:

| Field | Type | Meaning |
| --- | --- | --- |
| `event` | string | Record type. |
| `tick` | integer | Simulation tick at which the event occurred. |
| `year` | integer | Simulated calendar year. |
| `month` | integer | Simulated calendar month, from 1 through 12. |
| `day` | integer | Simulated day of month, starting at 1. |
| `organism_id` | integer | Subject organism ID; omitted when not applicable. |

IDs and tick counters are unsigned logical values. JSON parsers that store all
numbers as binary floating point can lose precision in very long experiments;
choose an integer-capable parser.

Writers may add fields in a compatible release. Readers should ignore unknown
fields and use `event` to select required fields. A change in field meaning or
removal of a documented field requires a new `log_version` in `run_start`.

## Events

### `run_start`

Marks a new run and records the inputs needed to identify it.

Fields include `log_version`, `seed`, `config_fingerprint`, and `resumed`.
Keep the configuration file with the log; the fingerprint detects accidental
mismatches but does not encode the configuration. A resumed segment appends a
new `run_start` record with `resumed` set to `true`.

### `birth`

Records a committed organism birth. Fields include `organism_id`, `parent_a`,
`parent_b`, `generation`, `organism_bytes`, `mutations`, `tick`, and the
simulated date. `genome_parameters` gives the heritable parameter count. Seed
organisms use zero parent IDs.
The `state` field is `awake` for every seed and offspring birth.

### `state_transition`

Records a substrate-approved lifecycle transition. Fields include
`organism_id`, `previous_state`, `new_state`, `requested_off_duration`, and
`back_on_tick`, in addition to the common tick and date. A duration and back-on
tick are nonzero only when entering off. The states are `awake`, `asleep`, and
`off`.

### `foolsday_sleep_roll`

Records the single sleep-risk evaluation for an organism on April 1. Fields
include `organism_id`, `roll`, `death_probability`, and `outcome`. Outcome is
`survived` or `died`. A death outcome is followed by a distinct death record.

### `reproduction_attempt`

Records a host-evaluated attempt. Fields include `parent_a`, `parent_b`, the
Boolean `approved` result, and one of these reason values:

- `parents_not_distinct`
- `parent_not_living`
- `opportunity_not_granted`
- `parent_not_eligible`
- `insufficient_capacity`
- `invalid_offspring`
- `allocation_failed`
- `approved`

Aggregate-only logging levels omit individual attempts.

### `plasticity`

Records a significant lifetime weight change when detailed plasticity logging
is enabled. It includes `organism_id` and `absolute_change`. The organism's
counter advances for every significant update, but the event stream reports
only power-of-two counter milestones. This logarithmic sampling avoids dumping
individual weights. Use `events` for short diagnostic experiments and
`summary` for long experiments. Death records contain the final counter, and
checkpoints contain the full current state.

### `death`

Records one terminal transition for an organism. Fields include
`organism_id`, `generation`, `birth_tick`, `age`, `reward`, reproduction and
mutation counters, significant weight changes, and `cause`.

`cause` is one of:

- `capacity`: The organism was oldest when a birth caused excess byte use.
- `foolsday_awake`: The organism was awake, or woke, on April 1.
- `foolsday_sleep`: The organism failed its once-per-day April 1 sleep roll.
- `invalid_state`: Neural or organism state failed host validation.
- `shutdown`: The experiment ended explicitly.

### `summary`

Records periodic population statistics. Fields include `population`, `awake`,
`asleep`, `off`, `population_bytes`, `population_genome_parameters`, and
cumulative births, deaths, reproduction attempts, and mutations.

### `checkpoint`

Records a successfully committed checkpoint. Fields include `path`,
`checkpoint_version`, `config_fingerprint`, `checkpoint_tick`,
`checkpoint_bytes` (the serialized file size), `population_bytes`, the common
simulated date, and `population`. Treat paths as local metadata, not portable
identifiers.

### `run_end`

Records a normal completion or controlled shutdown. It includes the final tick,
population, cumulative births, and cumulative deaths.

## Ordering guarantees

Records follow simulation commit order. Within one tick:

1. Off-timer transitions and Fool's Day sleep rolls appear before ordinary
   execution events. Awake deaths also occur before awake execution.
2. State transitions appear after the neural step that requested them.
3. Reproduction attempts appear before their successful birth.
4. A birth appears before any capacity death it causes.
5. A death is the final event for that organism ID.
6. The periodic summary appears after births, deaths, and accounting for the
   tick.
7. A checkpoint event appears only after the checkpoint has been written
   successfully.

Buffered output can remain in process memory briefly. A crash can lose the most
recent buffer, but every flushed line is a complete record. Use unique event-log
and checkpoint paths for concurrent experiments. Writers do not coordinate
access to a shared path, and overlapping runs can corrupt or replace one
another's output.

## Example

The exact optional fields depend on the configured logging level. A minimal
event stream resembles:

```json
{"event":"run_start","log_version":2,"tick":0,"year":2000,"month":1,"day":1,"seed":420,"config_fingerprint":6759519941994754753,"resumed":false}
{"event":"birth","tick":0,"year":2000,"month":1,"day":1,"organism_id":1,"parent_a":0,"parent_b":0,"generation":0,"organism_bytes":8192,"mutations":0,"state":"awake"}
{"event":"summary","tick":1000,"year":2000,"month":1,"day":2,"population":17,"awake":10,"asleep":5,"off":2,"population_bytes":139264,"births":17,"deaths":0,"reproduction_attempts":31,"mutations":6}
{"event":"death","tick":1001,"year":2000,"month":1,"day":2,"organism_id":2,"cause":"capacity","birth_tick":0,"age":1001,"reward":1001,"generation":0,"reproduction_attempts":4,"successful_reproduction":1,"mutations":0,"significant_weight_changes":3}
```

## Analysis guidance

- Read the file incrementally instead of loading a long run into memory.
- Check `log_version` before interpreting events.
- Group records by run, then by `organism_id`; IDs are unique only within one
  experiment state lineage.
- Derive living population from committed births and deaths, or use `summary`
  for periodic monitoring.
- Use checkpoint inspection for current neural state. Event logs intentionally
  avoid dumping every weight on every tick.
- Preserve the original configuration, executable build identifier, event log,
  and final checkpoint together when archiving an experiment.
