# Pull Request

## Summary

<!-- 1-2 sentences describing what this PR changes. -->

## Phase A contract impact

- [ ] No change to the Phase A public contract (`docs/spec/phase_a_*.md`,
      `include/azteca/DiagnosticCatalog.hpp`, exit codes).
- [ ] Additive only (new optional JSON key, new diagnostic id, new optional
      CLI flag). Updated `CHANGELOG.md` under `[Unreleased]`.
- [ ] Breaking change. CHANGELOG entry justifies the major bump and
      `docs/spec/` was updated accordingly.

## Golden updates

If this PR regenerates files under `tests/golden/`, fill in below.
Goldens are normally frozen and must only be regenerated with
`AZTECA_ACCEPT_GOLDEN=1`. CI does not regenerate goldens.

- [ ] No golden changes.
- [ ] Goldens regenerated. Reason:

<!-- Explain why the expected output legitimately changed (e.g. new
     LR-* rule, new construct fixture). Reference the relevant
     coverage row in docs/planning/25_phase_a_inspect_coverage.md. -->

## Verification

- [ ] `cmake --build --preset dev-clang --target check` passes locally.
- [ ] `cmake --build --preset dev-clang --target release-check` passes locally.
- [ ] ASan/UBSan build (`ctest --preset asan-clang`) passes locally for
      anything touching the runtime/CLI surface.
