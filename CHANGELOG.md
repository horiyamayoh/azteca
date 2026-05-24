# Changelog

All notable changes to Azteca are documented here. The Phase A `inspect`
schema and CLI surface follow [Semantic Versioning](https://semver.org/).

The public contract covered by semver is:

- `azteca inspect --format json` document shape (`schema_version`, top-level
  keys, enum values) — see [docs/spec/phase_a_inspect_schema_v2.md](docs/spec/phase_a_inspect_schema_v2.md)
- `azteca inspect --format text` section order and headings — see
  [docs/spec/phase_a_text_output.md](docs/spec/phase_a_text_output.md)
- CLI subcommands, options, exit codes — see
  [docs/spec/phase_a_cli.md](docs/spec/phase_a_cli.md)
- Diagnostic id registry (`AZT-Exxxx` / `AZT-Wxxxx`) — see
  [include/azteca/DiagnosticCatalog.hpp](include/azteca/DiagnosticCatalog.hpp)

Removing or semantically changing any of the above requires a major bump.
Additive changes (new optional JSON keys, new diagnostic ids, new optional
CLI flags) are minor bumps. Bug fixes and text-detail improvements are
patch bumps.

## [Unreleased]

### Added

- Inspect JSON diagnostics now include optional `public_id` for known internal
  `AZTECA_*` diagnostics, and `azteca explain` accepts those internal codes as
  aliases for their public `AZT-*` explanations.
- Google Test preview is now path-aware: each inspected path gets a `TEST(...)`
  skeleton with path-specific `when` observations and `effects` expectations.
- Real-project-style Phase A fixture coverage now exercises include paths,
  typedef/using aliases, nested namespaces, macro expansion, and source-file
  compile database validation.
- `azteca_phase: "A"` top-level field in the inspect JSON report.
- `azteca explain <diagnostic-id>` subcommand backed by a frozen 12-entry
  diagnostic catalog (`AZT-E0001` … `AZT-E0011`, `AZT-W0001`).
- CLI error messages now carry stable `AZT-E*` ids on stderr.
- Internal `AZTECA_*` diagnostic codes are mapped to public `AZT-E*` ids
  in `inspect` failure output.
- `AZTECA_ASSERT` macro for tool-bug invariants (always-on; emits
  `AZT-E0011` and aborts).
- New contract tests: `phase_a_cli_surface` (help golden, AZT-\* id
  coverage, JSON stdout-purity on failure) and `phase_a_schema_v2_check`.
- New robustness tests: `phase_a_encoding_robustness` (BOM / CRLF / UTF-8
  identifier source files) and `phase_a_coverage_matrix` (gap-fillers for
  `operator` / destructor / coroutine targets from
  `docs/planning/25_phase_a_inspect_coverage.md`).
- New baseline test: `phase_a_perf_smoke` (50 filler TUs, warns above a
  generous wall-time threshold; non-blocking baseline for H5).
- Perf baseline file `tests/perf/phase_a/baseline.json` (`baseline_seconds`
  and `warn_ratio_percent`) drives the `phase_a_perf_smoke` warn threshold
  and is logged on every run for trend tracking.
- New contract test `phase_a_schema_v2_validate` runs `ajv` against
  `tests/contract/schema_v2/azteca_phase_a.schema.json` for every Phase A
  golden under `tests/golden/phase_a/**/*.inspect.json`. Wired into
  `release-check`; backed by `npm run validate:phase-a-json` and a new
  `scripts/validate-phase-a-json.mjs` driver.
- New negative integration suite `phase_a_negative_suite` covering
  AZT-E0007 (empty compile-commands), AZT-E0009 (ambiguous target without
  `--source`, plus disambiguation), and AZT-E0010 (declaration-only
  method) in both text and JSON output; fixture under
  `tests/negative/phase_a/`.
- New coverage observation harness `phase_a_coverage_observations` maps
  24 LR-\* rules to specific fixture methods and golden files under
  `tests/golden/phase_a/coverage_observations/` to evidence the coverage
  matrix in `docs/planning/25_phase_a_inspect_coverage.md`.
- New unit tests in `tests/unit/ExtractionPlanTests.cpp` for invalid-plan
  JSON rendering (`result: "invalid-plan"` + diagnostics + schema keys) and
  text rendering of diagnostics and per-path `conservative reason`.
- Strict `.clang-tidy`: `cert-*` and `cppcoreguidelines-*` are now
  warnings-as-errors; documented per-check exclusions only for known
  noise (visitor-pattern reference members, libTooling const_cast,
  GoogleTest static initializers, magic numbers, c-arrays).
- CI: `release-check` job now runs each Phase A contract test as a
  named step; new non-blocking `perf-smoke` job tracks H5 baseline.
- New Phase A specs: `docs/spec/phase_a_cli.md`,
  `docs/spec/phase_a_inspect_schema_v2.md`,
  `docs/spec/phase_a_text_output.md`.

### Changed

- Quality gates now separate daily local checks from clang-tidy: `check`
  runs formatting, generated-reference validation, and CTest, while
  `strict-check` preserves the full CI gate with clang-tidy.
- Inspect JSON `schema_version` is now `2`; the document layout is frozen
  per the schema spec above.
