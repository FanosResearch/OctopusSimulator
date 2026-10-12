# Contributing

Contributions are welcome in the form of bug reports, documentation fixes,
configuration examples, protocol FSMs, tests, and C++ changes.

## Before opening a change

- Search existing issues and pull requests.
- Keep changes focused and explain the experiment or behavior they affect.
- Run the environment check and the narrowest relevant test.
- Do not commit benchmark traces or generated result directories.
- Document new configuration keys, command-line options, or output files.

## Pull requests

Include the motivation, the exact validation command, expected output, and any
platform or benchmark requirements. For a protocol change, include a small trace
or Debugger output that demonstrates the intended transitions. For performance
changes, include before-and-after summary metrics on the same workload.

Use the repository's [GitHub issues and pull requests](https://github.com/FanosResearch/OctopusSimulator/issues)
for discussion. Publications can be added through a focused pull request to
`PUBLICATIONS.md`.

The project currently does not ship a root-level `CONTRIBUTING.md`; this page is
the documentation site's concise contribution guide until the repository adopts a
formal policy.