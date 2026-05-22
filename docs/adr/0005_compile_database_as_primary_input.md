# ADR-0005: compile_commands.json を主要入力にする

## Status

Accepted

## Context

C++のAST解析には、include path、macro definition、language standard、target triple、コンパイルオプションが必要である。これらが元プロジェクトと異なると、名前解決やtemplate instantiationが変わりうる。

## Decision

Aztecaは`compile_commands.json`を主要入力にする。

CLIは`-p <build-dir>`を受け取り、Clang Toolingを使って対象translation unitを解析する。

## Consequences

良い点:

- 元プロジェクトに近い条件でASTを構築できる。
- Clang Toolingと相性が良い。
- CIで再現しやすい。

悪い点:

- build directoryが必要。
- compile databaseを生成しないビルドシステムでは前処理が必要。

## Alternatives considered

### Header-only parse

却下。macro/include/standard設定が不足しやすい。

### 手動include path指定

補助としては可能だが、既定にはしない。

## References

- `../design/02_architecture.md`
- `../design/03_extraction_pipeline.md`
