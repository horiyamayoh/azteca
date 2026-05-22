# ADR-0003: Heart mode と Live mode を分離する

## Status

Accepted

## Context

Aztecaには2つの異なる要求がある。

1. 実オブジェクトなしにメソッド本体ロジックを単体試験したい。
2. 実オブジェクト、実ABI、実RTTI、実virtual dispatch込みで元メソッドを試験したい。

これらを単一モードに混ぜると、Heart modeが実layoutを暗黙に仮定したり、Live modeがfake objectに逃げたりする危険がある。

## Decision

AztecaはHeart modeとLive modeを明確に分離する。

Heart mode:

- ASTから明示receiver関数を生成する。
- 元クラスの実インスタンスを作らない。
- ロジック単体試験とfuzzingに向く。

Live mode:

- 正規構築された実オブジェクトに対して元メソッドを呼ぶ。
- 実ABI/RTTI/layout/constructor invariantを含める。
- Heart/Live差分検証にも使う。

## Consequences

良い点:

- それぞれの意味契約が明確になる。
- 危険な中間案を避けられる。
- 変換不能時のfallbackが説明しやすい。

悪い点:

- 利用者に2モードの違いを説明する必要がある。
- diff検証にはobserver/factoryが必要。

## References

- `../design/01_semantic_contract.md`
- `../design/07_live_mode.md`
