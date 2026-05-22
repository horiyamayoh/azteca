# 01. Semantic Contract V3

## 1. 目的

この文書は、Aztecaが守る意味論上の契約を定義する。

アステカは、C++非staticメンバ関数をfake `this`で呼ぶツールではない。Clang AST/Sema後の情報から、対象メソッドのunit-observable semanticsを取り出し、`self`、`scenario`、`when`、`effects`を持つテスト可能なkernelへ変換するツールである。

## 2. 絶対禁止事項

```text
1. 未構築storageを `C*` に見せて非staticメンバ関数を呼ばない。
2. fake `this` を作らない。
3. pointer-to-memberをraw function pointerへ偽変換しない。
4. `#define private public` を中核手段にしない。
5. AST/Semaで解決していない構文を安全扱いしない。
6. 未設定依存queryへ暗黙デフォルト値を返さない。
7. 変換不能な意味をコメントだけで埋めて成功扱いしない。
```

## 3. 保存する意味

Aztecaが保存する標準意味は、unit-observable semanticsである。

```text
- 戻り値
- 例外
- receiver stateのread/write
- local stateの制御フロー上の意味
- dependency observations
- external effects
- call order when meaningful
- object identity
- dynamic type decision
- lifetime intent
- byte access intent
```

## 4. 標準では保存しない意味

標準抽出では、以下を実表現として保存しない。

```text
- 実アドレスの数値
- vptrの実表現
- padding byteの偶然値
- allocator内部状態
- OS handleの実値
- 未定義動作の結果
- inline asmの未モデル化機械効果
```

これらがテスト対象の本体である場合、アステカは「unit extractionとして意味が薄い」または「明示境界が必要」と報告する。

## 5. 単一公開抽出契約

利用者に通常要求する操作は1つである。

```bash
azteca extract -p build --method 'C::m(args...)'
```

内部では、必要に応じて次を自動的に追加する。

```text
self
shape
query/effect/operation ports
object_ref
cell/ref
dispatch table
type_tag
lifetime_state
byte_view
```

利用者に `Heart mode`、`Live mode`、`Rich Heart mode` の選択を迫らない。

## 6. Dependency Transcript契約

依存関係は、依存クラスのfakeではなく、対象メソッドが観測する値と外界へ送る効果として扱う。

```text
query:
  戻り値を持つ外界観測。

effect:
  戻り値なし、または戻り値を使わない外界要求。

operation:
  戻り値と副作用の両方が意味を持つ外界操作。
```

未設定queryに到達した場合、`missing_observation` を発生させる。`false`、`0`、`std::nullopt` などを暗黙に返してはならない。

## 7. Google Test契約

生成テストの標準ランナーはGoogle Testである。

ただし、以下はGoogle Test非依存に保つ。

```text
- kernel
- self
- shape
- ports
- scenario core runtime
- MMIR
- lowerer
```

Google Test依存は、sample/skeleton testと薄いadapterに限定する。

## 8. 正しさの主張

Aztecaが生成するkernelは、元メソッドの実バイナリではない。

Aztecaが主張するのは次である。

```text
同じreceiver状態、同じ引数、同じdependency transcriptが与えられたとき、
抽出kernelは元メソッドのunit-observable semanticsを保存する。
```

これは、製品コードの全ABI挙動を再現する主張ではない。

## 9. Live Validationの位置づけ

正規に構築された実オブジェクトと抽出kernelを比較するLive Validationは、抽出器の検証補助である。

```text
- 標準抽出方式ではない。
- fake objectを作らない。
- 実オブジェクトは正規factoryで構築する。
- 比較対象はobserverで取り出せるunit-observable semanticsに限る。
```

## 10. 契約違反時の挙動

契約違反が検出された場合、Aztecaは次のいずれかを行う。

```text
1. 安全な意味モデルへloweringする。
2. dependency transcript境界へ出す。
3. 明示的にnot-meaningful-for-unit-extractionとして報告する。
4. 実装未対応ならunsupportedではなく「未実装だが設計上の扱い」を報告する。
```

黙って危険な近似をしてはならない。
