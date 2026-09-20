# Contributing

Thank you for improving AL4.20. Keep version 1 small, deterministic, and easy to
inspect.

## Before you start

Open an issue before making a large architecture change. Describe the research
question, the simulation semantics that would change, and the smallest useful
implementation. Do not add arbitrary organism code execution, networking,
topology growth, graphical interfaces, GPU dependencies, or complex ecology to
version 1.

For a security problem, follow [Security](SECURITY.md) instead of opening a
public issue.

## Build and test

You need a C11 compiler, `make`, and a POSIX-like development environment.

```sh
make clean
make
make test
make sanitize
```

If your toolchain does not support the sanitizer target, state that in your
pull request and run the strict default build and test suite.

Run the small end-to-end experiment after tests pass:

```sh
./scripts/run-small.sh
```

Do not commit generated binaries, event logs, or checkpoints.

## Change guidelines

- Preserve the substrate-organism trust boundary.
- Keep all memory ownership in the substrate.
- Validate dimensions, indices, finite values, and size arithmetic before use.
- Use the simulation PRNG for every random decision.
- Preserve stable iteration and tie-break ordering.
- Keep allocation and verbose logging outside the hot path where practical.
- Put experiment policy in validated configuration instead of hidden constants.
- Add a test for every fixed invariant or regression.
- Update architecture, configuration, log, and CLI documentation with behavior
  changes.
- Follow Google developer documentation guidance for English prose, comments,
  help, and errors. Use direct, concise language and present tense.

Format C code with the repository's existing style. Compile without warnings;
do not suppress a warning unless the code documents why it is safe.

## Tests to preserve

Changes must continue to prove that:

- immature or single organisms cannot reproduce;
- parents are distinct and both authorize reproduction;
- offspring dimensions and mutations stay within configured bounds;
- capacity enforcement removes the oldest organism deterministically;
- April 1 prevents all organism execution and cannot be bypassed by state;
- invalid neural values do not corrupt host memory;
- the same seed and configuration reproduce the same run; and
- checkpoint resume preserves the complete continuation state.

## Pull requests

Keep each pull request focused. Include:

- the reason for the change;
- any change to tick ordering or deterministic random-number consumption;
- commands you ran and their results;
- performance measurements for hot-path changes; and
- migration notes for configuration, logs, or checkpoints.

By contributing, you agree that your contribution is licensed under the MIT
License and that you will follow the [Code of Conduct](CODE_OF_CONDUCT.md).
