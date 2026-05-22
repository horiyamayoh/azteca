# 19. Correctness Model

## 目的

この文書は、アステカが生成するkernelの正しさをどのように定義し、検証するかを定める。

アステカは、製品メソッドの実バイナリをそのまま呼ぶツールではない。ASTからメソッド本体の意味を取り出し、ユニットテスト可能な形に変換するツールである。そのため、正しさの定義を曖昧にしてはならない。

## 正しさの基本定義

アステカの正しさは、次の対応で定義する。

```text
Concrete world:
  正規に構築された C++ オブジェクトと実行環境

Azteca world:
  self + deps + effects + env + runtime abstractions

Abstraction:
  concrete object state を self に写像する

Unit-observable equivalence:
  対象メソッドのユニットテスト上観測すべき振る舞いが一致する
```

形式的には、対象メソッド `C::m(args...)` と生成kernel `C_m(self, deps, effects, args'...)` について、次を目指す。

```text
alpha(before_concrete_state) = before_self
argument_mapping(args) = args'

after executing C::m:
  result_concrete
  after_concrete_state
  observable_effects_concrete

after executing generated kernel:
  result_azteca
  after_self
  effects_azteca

Then:
  result_mapping(result_concrete) = result_azteca
  alpha(after_concrete_state) = after_self
  effect_mapping(observable_effects_concrete) = effects_azteca
```

ただし、外部依存境界は `deps` の契約に従う。

## Unit-Observable Semantics

アステカが標準で保存する意味:

```text
1. 戻り値
2. 例外の発生と種類
3. receiver field state の更新
4. base subobject state の更新
5. local logic の制御フロー
6. dependency call の発生、順序、引数、戻り値の利用
7. object identity の比較、返却、外部渡し
8. virtual dispatch の選択点
9. dynamic type による分岐
10. lifetime operation の意図
11. global/env state の読み書き
12. byte representation access の意図、または安全な抽象表現
```

標準で保存しない意味:

```text
1. 実メモリアドレスの数値
2. vptrの実表現
3. padding byteの偶然値
4. ABI固有のpointer-to-member表現
5. allocator内部状態
6. OS handle実値
7. 未定義動作の結果
```

## Soundness Principle

アステカは、変換の正確性を過剰に主張しない。

```text
SP-001:
  意味を保存できる場合はkernelへloweringする。

SP-002:
  意味を安全な抽象化で保存できる場合はSemantic Envelopeへloweringする。

SP-003:
  自前で意味を保存できないが、ユニットテスト上依存として扱える場合はBoundaryCallにする。

SP-004:
  BoundaryCallも不適切な場合はnot-meaningful-for-unit-extractionとする。

SP-005:
  どの場合もfake thisや未開始ライフタイム呼び出しを使わない。
```

## Rule Proof Obligation

各lowering ruleは、以下の証明責務を持つ。

```text
1. 対象AST/MMIR条件
2. 生成されるMMIR/Envelope
3. 保存する意味
4. 保存しない意味
5. boundaryが発生する場合の契約
6. negative case
7. テストfixture
```

例: `this escape` lowering

```text
対象:
  C* 型として this が外部依存に渡される。

生成:
  object_ref<C> をdepsへ渡す。

保存する意味:
  対象メソッドが自分自身を依存へ渡したこと。
  呼び出し順序。
  他引数。

保存しない意味:
  実C*の数値。
  依存側がCの実レイアウトを読む挙動。

契約:
  depsはobject_ref<C>を受け取り、テスト側が期待を検証する。
```

## Boundary Contract

BoundaryCallは、次の契約を持つ。

```text
- 元calleeの名前、型、source locationを保持する。
- 引数はAzteca worldに写像される。
- 戻り値はテスト側またはdefault stubから供給される。
- 呼び出しはeffectsに記録できる。
- 境界化されたcalleeの内部挙動は、対象kernelの正しさ範囲外である。
```

BoundaryCallは、ユニットテストにおけるmock/stubと同じ役割を持つ。ただし、アステカはsource mapとeffect logにより、どの依存が境界になったかを透明にする。

## Object Identity Contract

`object_ref<C>` は、次を保証する。

```text
- 同じselfから得たobject_refは等しい。
- 異なるobject_idから得たobject_refは異なる。
- object_ref<C>はC*へ暗黙変換されない。
- object_ref<C>は実C++オブジェクトのライフタイムを意味しない。
- object_refはユニットテスト世界の同一性を表す。
```

これにより、`return this`、`this == other`、`external(this)` を安全に試験できる。

## Lifetime Contract

`lifetime_state` は、実メモリ管理ではなく、メソッドのライフタイム意図を表す。

```text
- live
- destroying
- destroyed
- reinitialized
```

`delete this` loweringは、次を行う。

```text
1. destructor kernelを呼ぶ。可能な場合。
2. lifetimeをdestroyedへ変更する。
3. effectsにdelete意図を記録する。
```

保存する意味:

```text
- どの条件で破棄を意図したか
- 破棄前にどの状態更新が起きたか
- destructor logicが抽出されていればそのロジック
```

保存しない意味:

```text
- 実operator deleteのallocator挙動
- 実メモリ解放
```

## Byte View Contract

`byte_view` は、実ABIレイアウトの偽造ではない。

方針:

```text
- フィールドから決定的な表現を作れる場合はrepresentation_bytesを生成する。
- ABI正確性が必要な場合はboundaryにする。
- paddingやvptrに依存する意味は標準kernelでは保存しない。
```

この契約を破ると、アステカはfake objectに近づいてしまうため禁止する。

## Differential Validation

可能な場合、生成kernelと実オブジェクト呼び出しの差分検証を行う。

ただし、差分検証は抽出方式ではなく、検証補助である。

```text
1. テスト用factoryで正規Cオブジェクトを作る。
2. alphaでselfへ写像する。
3. 同じ入力で実メソッドとkernelを実行する。
4. result/state/effectsを比較する。
```

比較できないもの:

```text
- external dependencyの実副作用
- allocator内部状態
- OS handle
- timing
- thread scheduling
```

## Regression Strategy

各ruleに対して以下を保つ。

```text
- MMIR snapshot
- Envelope Plan snapshot
- generated code snapshot
- compile test
- runtime test
- report snapshot
```

snapshotは過剰に細かくしすぎない。意味のある単位で固定する。

## No Silent Unsoundness

アステカ最大の失敗は、抽出不能なものを抽出できたように見せることである。

禁止:

```text
- 実C*が必要な依存にobject_refを渡したことを隠す
- byte_viewがABI正確であるように見せる
- dynamic dispatchを固定したのにreportしない
- dependency戻り値の仮定をreportしない
- lifetime effectを実deleteとして説明する
```

## Correctness Checklist

生成時に次をチェックする。

```text
1. 裸のthisが生成コードに残っていないか。
2. reinterpret_cast<C*>が生成されていないか。
3. object_refがC*へ変換されていないか。
4. BoundaryCallがmanifestに記録されているか。
5. effectsが呼び出し順序を保存しているか。
6. dynamic dispatchが固定化された場合、reportされているか。
7. lifetime操作が実メモリ操作として実装されていないか。
8. byte_viewがABI正確性を主張していないか。
```

## Definition of Done

```text
1. unit-observable semanticsが文書化されている。
2. 各lowering ruleにproof obligationがある。
3. Boundary/Object/Lifetime/Byteの契約が明確である。
4. no silent unsoundness checklistが実装されている。
5. 差分検証は任意の検証補助として位置づけられている。
```
