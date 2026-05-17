# Contributing

Thanks for your interest in the VDI C++ Wrapper!

## Guidelines

- **Formatting**: Use the project's `.clang-format` (if present) or match the
  existing code style (Allman braces, 4-space indent, 100-char lines).
- **Hot path awareness**: The command loop (`process_commands`) is latency-
  sensitive. Avoid heap allocations, virtual dispatch, locking, or I/O in the
  per-chunk path.
- **Benchmark reproducibility**: When adding features that affect throughput
  or latency, re-run the benchmark scripts and update `benchmarks/results/`.
  Document the environment exactly (VM type, SQL version, storage, DB size).
- **No unnecessary dependencies**: The project intentionally depends only on
  the Windows SDK and C++ standard library. Do not add external libraries
  without strong justification.
- **API surface**: Keep the public headers minimal. Protocol internals
  (`VDC_Command`, `VDConfig`, COM interface pointers) belong in `sqlvdi.h`
  and `sqlvdi_guids.h`, not in public wrappers.

## Before Submitting

1. Build clean: `cmake --build build --config Release`
2. Run the C++ benchmark (NullSink mode) to verify no regression.
3. Ensure `Metrics::to_json()` output is valid JSON.

## License

By contributing, you agree that your contributions will be licensed under the
MIT License (see `LICENSE`).