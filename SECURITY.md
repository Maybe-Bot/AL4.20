# Security policy

## Supported versions

AL4.20 is pre-release research software. Security fixes are applied to the
current `main` branch. Older commits and generated checkpoints are not supported
as separate release lines.

## Report a vulnerability

Do not open a public issue for a suspected vulnerability. Use GitHub's private
vulnerability reporting for this repository. If private reporting is not
enabled, ask the repository owner for a private contact channel without
including vulnerability details in the request. Include:

- the affected commit and build options;
- the operating system and compiler;
- the smallest configuration or checkpoint that reproduces the problem;
- sanitizer or debugger output; and
- the expected and observed boundary violation.

Do not include unrelated private data. Allow maintainers time to reproduce and
fix the issue before public disclosure.

## Security boundary

An evolved organism is numerical data interpreted by the host. It cannot
execute native code and has no direct access to allocation, files, commands,
process creation, networking, function pointers, dynamic libraries, arbitrary
addresses, or substrate state. The host owns all memory, time, communication,
reproduction, and validation.

The intended boundary protects the substrate from ordinary evolved organism
state. Tests and runtime validation should prevent invalid values or dimensions
from crossing that boundary.

## Threat model and limitations

AL4.20 is not hardened for hostile configuration or checkpoint files. Both are
parsed by a native C program. A maliciously modified file may exploit an
unknown parser or arithmetic bug before normal organism validation applies.
Run the program only with inputs you trust.

The checkpoint encoding is versioned for compatibility checks but remains an
exact-build research format. A recognized header does not authenticate a file.
The event log is also untrusted text when viewed by other tools. Do not render
log values as HTML or pass them to a shell without appropriate escaping.

The project does not claim containment against a compromised compiler,
operating system, dependency, or executable. For defense in depth when testing
fuzzed files, run a sanitizer build as an unprivileged user inside an operating
system sandbox with no sensitive files or network access.

## In-scope examples

- Out-of-bounds reads or writes caused by neural state or parsed input.
- Integer overflow in organism, population, log, or checkpoint sizing.
- An April 1 bypass caused by mutable organism state.
- A path from neural output to native code, filesystem, process, or network
  access.
- Use of invalid floating-point state that leads to memory corruption.

Unexpected evolutionary behavior, poor population viability, and differences
across compilers are research or reproducibility issues unless they cross a
memory-safety or privilege boundary.
