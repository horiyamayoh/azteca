# ADR-0009: Clang ASTとCodegenの間にMMIRを置く

## Status

Accepted

## Context

Clang ASTから直接生成コードを出すと、意味解析、Envelope計画、コード生成が混ざる。対応構文が増えるほど、lowering ruleが複雑化し、保守性が落ちる。

## Decision

Clang ASTから一度 Method Meaning IR、すなわちMMIRへ変換する。

```text
Clang AST -> MMIR -> Semantic Envelope Plan -> C++ Codegen
```

MMIRは、field access、object identity、boundary call、dispatch call、lifetime operation、byte viewなど、ユニットテストに必要な意味を表す。

## Consequences

### Positive

- 意味抽出とコード生成を分離できる。
- Envelope Plannerを独立してテストできる。
- report/source mapを作りやすい。
- 新しい生成方針を追加しやすい。

### Negative

- 実装量は増える。
- MMIR validationが必要になる。
- ASTとMMIRの二重テストが必要になる。

## Notes

MMIRはコンパイラIRのような最適化用IRではない。目的は、対象メソッドのユニットテスト可能な意味を失わずに正規化することである。
