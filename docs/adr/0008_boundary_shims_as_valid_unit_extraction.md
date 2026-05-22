# ADR-0008: dependency boundaryを有効な抽出結果として扱う

## Status

Accepted

## Context

多くのメソッドは、外部関数、他オブジェクト、registry、logger、OS、I/Oなどを呼び出す。これらをすべて再帰抽出しようとすると、ユニットテストではなく統合テストに近づく。

一方で、外部呼び出しがあるたびにunsupportedにすると、実用性が失われる。

## Decision

外部依存は、標準でdependency boundaryとして扱う。

```cpp
external(this, x)
```

は、例えば次のようにloweringする。

```cpp
deps.external(self.object_ref(), x);
effects.record_call("external", self.object_ref(), x);
```

## Consequences

### Positive

- 抽出可能範囲が大きく広がる。
- ユニットテストとして自然な依存注入になる。
- 呼び出し発生、順序、引数を検証できる。

### Negative

- 外部依存の実装そのものは試験対象外になる。
- 戻り値が必要な依存では、テスト側が値を与える必要がある。
- reportで境界化を明示しないと、過剰な正確性を期待させる。

## Rule

Boundary化は失敗ではない。ただし、境界化された依存は必ずreportとmanifestに記録する。
