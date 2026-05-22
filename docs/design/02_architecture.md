# 02. Architecture V3

## 1. 目的

この文書は、Aztecaの全体アーキテクチャを定義する。

V3の中核は次である。

```text
Clang AST
  -> Method Meaning IR (MMIR)
  -> Extraction Plan
  -> Semantic Envelope
  -> Generated kernel/scenario
  -> Google Test sample
```

## 2. 全体構成

```text
azteca CLI
  |
  v
Compilation Database Loader
  |
  v
Clang Tooling Frontend
  |
  v
Method Selector
  |
  v
MMIR Builder
  |
  +--> Feature Collector
  +--> Dependency Observation Collector
  +--> Shape Planner
  +--> Effect Planner
  +--> Path-wise Stub Burden Analyzer
  |
  v
Extraction Planner
  |
  v
Lowering Pipeline
  |
  v
Code Generator
  |
  +--> self/shape/ports/kernel/scenario
  +--> Google Test sample/skeleton
  +--> CMake/manifest/report
```

## 3. モジュール責務

### CLI

```text
- scan
- inspect
- extract
- build
- test
- explain
```

通常利用者には `extract` と `test` を中心に見せる。開発初期は `inspect` を先に実装する。

### CompilationDatabaseLoader

`compile_commands.json` を読み、元プロジェクトと同じinclude path、macro、標準バージョン、コンパイルオプションでASTを構築する。

### MethodSelector

`--method 'ns::C::m(int) const &'` を `clang::CXXMethodDecl` に解決する。

### MMIR Builder

Clang ASTを、Aztecaが扱いやすいMethod Meaning IRへ変換する。

MMIRは次を含む。

```text
- receiver access
- local variable
- branch
- return/throw
- dependency call
- object identity operation
- addressability operation
- type/lifetime/byte operation
```

### FeatureCollector

対象メソッドが必要とするSemantic Envelope要素を検出する。

```text
field state
shape
query/effect/operation
object_ref
cell/ref
dispatch
type_tag
lifetime
byte_view
```

### DependencyObservationCollector

依存呼び出しを、fakeクラスではなくDependency Transcript portとして集める。

```text
query
operation
effect
expression-level query
shape-producing query
```

### Path-wise Stub Burden Analyzer

制御フローを概算し、経路ごとに必要なquery/effectをreportする。

### ExtractionPlanner

利用者にmodeを選ばせず、内部で抽出計画を決める。

```text
- 再帰抽出するhelper
- transcript portにする依存
- shape化する戻り値
- object_refが必要な箇所
- Google Test skeletonに出すべきscenario行
```

### Lowering Pipeline

MMIRを生成C++へloweringする。

### Code Generator

次を生成する。

```text
- self.hpp
- shape.hpp
- ports.hpp
- kernel.hpp/cpp
- scenario.hpp
- Google Test sample/skeleton
- CMakeLists.txt
- manifest.json
- azteca_report.md
```

## 4. ディレクトリ構成

```text
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
  fuzz/
```

## 5. 実装順序

```text
Phase A:
  inspect as extraction plan

Phase B:
  minimal kernel + Google Test sample

Phase C:
  Dependency Transcript + Scenario Runtime

Phase D:
  Shape and expression-level ports

Phase E:
  Identity and addressability

Phase F:
  Dispatch and dynamic type

Phase G:
  Lifetime and representation
```

## 6. 公開UX契約

```text
1. 通常利用者にmode選択を要求しない。
2. Google Testを標準生成テストにする。
3. 依存fakeクラスを標準生成しない。
4. reportは次に書くべきscenario行を示す。
5. kernel/scenario runtimeはGoogle Test非依存に保つ。
```
