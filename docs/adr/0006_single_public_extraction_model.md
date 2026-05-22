# ADR-0006: 単一の公開抽出モデルを採用する

## Status

Accepted

## Context

アステカは、C++メソッドのロジックをAST経由でテスト可能な形へ取り出すツールである。以前の設計では、Heart mode、Live mode、fallback、unsupportedといった分類を明示していた。

しかし、利用者に多くのmodeを選ばせると、ツールの利用が難しくなる。元々の目的はテスタビリティの向上であり、難しいコードを試験するために別の難しい概念群を学ばせるのは本末転倒である。

## Decision

アステカの公開UXでは、原則として単一の抽出操作を採用する。

```bash
azteca extract -p build --method 'C::m(int)'
```

内部では、必要に応じてfield state、dependency boundary、object identity、dispatch、lifetime、byte viewなどの意味モデルを自動的に追加する。

## Consequences

### Positive

- 利用者はmode選択を覚えなくてよい。
- 抽出結果が統一されたself/deps/effectsの形になる。
- 対応範囲拡張が、public API複雑化に直結しない。

### Negative

- 内部実装はより複雑になる。
- レポート品質が重要になる。
- 自動判断を誤らないため、MMIRとEnvelope Plannerが必要になる。

## Rejected Alternatives

### Alternative A: 多数のmodeを公開する

却下理由:

```text
利用者に判断負荷を押し付ける。
```

### Alternative B: Heart modeだけを狭く実装する

却下理由:

```text
単純ではあるが、対応範囲が狭くなりすぎる。
```

### Alternative C: Live modeを主軸にする

却下理由:

```text
実オブジェクト構築の問題から逃げられず、アステカの本来価値を失う。
```
