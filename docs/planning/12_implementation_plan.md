# 12. Implementation Plan V3

## 1. 目的

この文書は、Azteca V3の実装着手時に使う具体的な開発計画を定義する。

設計は実装開始に十分である。最初の成果物は完全な `extract` ではなく、`inspect` によるExtraction Plan表示である。

## 2. Repository Layout

```text
azteca/
  CMakeLists.txt
  include/
    azteca/
      Diagnostics.hpp
      MethodSpec.hpp
      MMIR.hpp
      ExtractionPlan.hpp
      RuntimeContracts.hpp
  src/
    cli/
    frontend/
    resolve/
    mmir/
    collect/
    plan/
    lower/
    codegen/
    report/
    runtime/
    gtest/
  tests/
    unit/
    fixtures/
    golden/
    integration/
    negative/
  docs/
```

## 3. Phase A: Inspect MVP

### Goal

対象メソッドをASTから見つけ、抽出計画を表示する。

### Scope

```text
- compile_commands.json loading
- MethodSelector
- MethodInfo extraction
- MMIR MVP
- receiver field collection
- dependency observation collection
- query/effect/operation classification MVP
- shape candidate MVP
- path-wise stub burden MVP
- Google Test preview report
```

### Non-scope

```text
- kernel generation
- scenario runtime implementation
- CMake generation
- Google Test execution
```

### Example output

```text
Azteca can extract Account::withdraw(int).

Generated Google Test:
  tests/account.withdraw.sample_test.cpp

Receiver state:
  - int balance_ read/write
  - bool locked_ read

Dependency observations:
  query fee(int) -> int

Path-wise test burden:
  locked:
    observations: none
  unlocked:
    observations: fee
```

## 4. Phase B: Minimal Kernel + Google Test

### Scope

```text
- self.hpp
- kernel.hpp/cpp
- scenario.hpp minimal
- Google Test sample
- CMakeLists.txt
- manifest.json
- report.md
```

### Supported syntax

```text
field read/write
local variable
argument
if/else
return
arithmetic/logical expression
simple private helper recursive extraction
simple query dependency
```

### Done

```text
cmake -S azteca-out -B azteca-out/build
cmake --build azteca-out/build
ctest --test-dir azteca-out/build --output-on-failure
```

## 5. Phase C: Dependency Transcript Runtime

### Scope

```text
azteca::query
azteca::effect
azteca::operation
azteca::missing_observation
scenario.when API
scenario.effects API
Google Test adapter
```

### Done

```text
- non-void dependency returns configured value
- missing dependency throws missing_observation
- void dependency records effect
- operation returns value and records effect
- generated Google Test verifies effects
```

## 6. Phase D: Shape and Expression-level Ports

### Scope

```text
- returned dependency object shape
- optional-like shape wrapping
- expression-level query port
- object_ref for identity-preserving cases
```

## 7. Phase E onward

```text
E: Identity and addressability
F: Dispatch and dynamic type
G: Lifetime and representation
H: Record/replay
I: UX hardening and regression
```

## 8. First Issues

```text
Issue 1: Project bootstrap
Issue 2: Compilation database loading
Issue 3: Method selector
Issue 4: MMIR MVP
Issue 5: Receiver field collector
Issue 6: Dependency observation collector
Issue 7: Inspect report text/json
Issue 8: Basic fixtures and Google Test unit tests
```

## 9. Definition of Done for Implementation Start

```text
- azteca --help works
- azteca inspect can find CXXMethodDecl
- inspect displays receiver fields
- inspect displays dependency observations
- inspect displays Google Test preview
- unit tests run with Google Test
```
