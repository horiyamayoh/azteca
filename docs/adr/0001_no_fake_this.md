# ADR-0001: fake `this` を使わない

## Status

Accepted

## Context

Aztecaの目的は、C++非staticメンバ関数を単体試験しやすくすることである。最も安易な案は、未初期化ストレージを`C*`に見せかけ、pointer-to-memberで呼ぶ方式である。

しかし、この方式はC++オブジェクトライフタイム規則に反し、ハーネス自身が未定義動作を持ち込む。

```cpp
void* storage = std::malloc(sizeof(C));
C* fake = reinterpret_cast<C*>(storage);
fake->m(); // 禁止
```

## Decision

Azteca中核ではfake `this` を使わない。

Heart modeでは、メソッド本体をASTから抽出し、明示receiver関数へloweringする。

Live modeでは、正規に構築された実オブジェクトだけを使う。

## Consequences

良い点:

- ハーネス由来UBを避けられる。
- 生成コードが通常C++として読める。
- sanitizer/fuzzerの結果が汚染されにくい。
- 多重継承やvirtual ABIに依存しない中核を作れる。

悪い点:

- Heart modeは元製品バイナリそのものの呼び出しではない。
- 実layout/RTTI/vptrを検査するにはLive modeが必要。
- AST lowering実装が必要になる。

## Alternatives considered

### ABI hack

pointer-to-memberを関数ポインタへ変換する案。

却下理由:

- ABI依存。
- 多重継承やvirtualで壊れやすい。
- C++標準上の堅牢な契約にできない。

### `#define private public`

private突破で実オブジェクトを作りやすくする案。

却下理由:

- クラス定義を変えてしまう。
- Aztecaの中核安全契約を弱める。

## References

- `../design/01_semantic_contract.md`
- `../design/07_live_mode.md`
