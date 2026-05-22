# 18. V3 Implementation Roadmap

## 1. 目的

この文書は、アステカを「単純な利用感を保ったまま、より多くのメソッドを抽出できるツール」へ進める実装ロードマップを定義する。

V3では、依存関係問題とGoogle Test統合を正式にロードマップへ組み込む。

```text
Phase A: Inspect as Extraction Plan
Phase B: Minimal Google Test Kernel Extraction
Phase C: Dependency Transcript and Scenario Runtime
Phase D: Shape and Expression-level Ports
Phase E: Identity and Addressability
Phase F: Dispatch and Dynamic Type
Phase G: Lifetime and Representation
Phase H: Record/Replay and Scale
Phase I: UX Hardening and Regression
```

## 2. Phase A: Inspect as Extraction Plan

### 目的

コード生成前に、対象メソッドがどのように抽出されるかを説明できるようにする。

### コマンド

```bash
azteca inspect -p build --method 'C::m(int)'
```

### 出力に含めるもの

```text
- receiver state
- generated shapes候補
- dependency observations
- observable effects
- object_ref要求
- path-wise stub burden
- generated Google Test preview
```

### 完了条件

```text
1. field read/writeを検出できる。
2. same-class helperの再帰抽出候補を出せる。
3. member object method callをquery/effect/operation候補に分類できる。
4. this escapeをobject_ref要求として表示できる。
5. 早期return経路ごとの必要queryを概算できる。
```

## 3. Phase B: Minimal Google Test Kernel Extraction

### 目的

単純なメソッドをkernelとして生成し、Google Test sampleで実行できるようにする。

### 対応範囲

```text
- field read/write
- local variable
- argument
- arithmetic/logical expression
- if/else
- return
- const receiver
- simple helper recursive extraction
```

### 生成物

```text
include/
  C_m.self.hpp
  C_m.kernel.hpp

tests/
  C_m.sample_test.cpp

CMakeLists.txt
azteca_report.md
manifest.json
```

### 完了条件

```text
1. 生成コードがCMakeでビルドできる。
2. Google Test sampleが実行できる。
3. fake thisを使わない。
4. self更新と戻り値をEXPECTで検証できる。
```

## 4. Phase C: Dependency Transcript and Scenario Runtime

### 目的

大量依存メソッドを、依存fakeクラスなしにテストできるようにする。

### 対応範囲

```text
- query port
- effect log
- operation port
- scenario.when.xxx(...).returns(...)
- scenario.effects.xxx.expect_once(...)
- missing observation diagnostics
- generated scenario skeleton
```

### 生成例

```cpp
TEST(C_m, success_path) {
    auto s = azteca_gen::scenario::C_m{};

    s.self.enabled = true;
    s.when.repo_exists(Id{1}).returns(true);
    s.when.policy_allow(Id{1}).returns(true);

    auto result = s.call(Id{1});

    EXPECT_EQ(result, OK);
    s.effects.notifier_send.expect_once(Id{1});
}
```

### 完了条件

```text
1. non-void dependency callをqueryとして扱える。
2. void dependency callをeffectとして記録できる。
3. operationが戻り値供給と効果記録の両方を行える。
4. 未設定queryに到達するとmissing observationが出る。
5. 生成Google Testがscenario APIを使って通る。
```

## 5. Phase D: Shape and Expression-level Ports

### 目的

依存が返す巨大オブジェクトやメソッドチェーンを、本物の依存構築なしに扱う。

### 対応範囲

```text
- returned object shape generation
- optional/unique_ptr/shared_ptr-like wrapperのshape化
- expression-level query port
- shape equality / print support
- inspectでshape field表示
```

### 完了条件

```text
1. repo.load(id)->amount() を OrderShape.amount にloweringできる。
2. repo.find(id)->profile().age(now) を単一query portに畳める。
3. 中間同一性が必要な場合は畳まずobject_refへ展開できる。
```

## 6. Phase E: Identity and Addressability

### 目的

`this` の同一性、`return this`、`external(this)`、メンバアドレス取得を抽出できるようにする。

### 対応範囲

```text
- object_ref<C>
- object_id generation
- return this
- this comparison
- pass this to dependency
- address-taken field
- reference aliasing
- simple pointer to field
```

### 完了条件

```text
1. return thisをobject_ref戻りにできる。
2. external(this)をdeps.external(object_ref)にできる。
3. &fieldをcell/refにできる。
4. aliasによるfield更新が保存される。
```

## 7. Phase F: Dispatch and Dynamic Type

### 目的

virtual call、dynamic_cast、typeidを意味モデルとして抽出する。

### 対応範囲

```text
- virtual method call -> dispatch query/operation
- pure virtual call -> required dispatch observation
- dynamic_cast<this> -> type_tag test
- typeid(*this) -> type_tag info
- derived shape view
```

## 8. Phase G: Lifetime and Representation

### 目的

delete this、explicit destructor、placement new、byte accessなどを、可能な限り意味モデルとして抽出する。

### 対応範囲

```text
- lifetime_state
- destructor kernel
- delete effect
- placement-new intent
- byte_view for representation observation
```

完全なABI再現はしない。ユニットテスト上意味のある観測へ落とす。

## 9. Phase H: Record/Replay and Scale

### 目的

大量依存メソッドのscenario作成負担をさらに下げる。

### 対応範囲

```text
- dependency transcript recording
- transcript to Google Test scenario generation
- path seed generation
- scenario minimization
```

record/replayは補助機能である。標準のunit testは人間が意図を確認して編集する。

## 10. Phase I: UX Hardening and Regression

### 目的

実プロジェクトで継続使用できる品質にする。

### 対応範囲

```text
- diagnostics polish
- large fixture corpus
- generated code style
- CI integration
- CMake package integration
- regression minimization
- reporting quality
```

## 11. 実装開始判断

現時点で実装開始してよい。

ただし、最初に作るものは `extract` の完全版ではなく、`inspect` である。

```text
Phase A first:
  - MethodSelector
  - FeatureCollector
  - MMIR MVP
  - Dependency observation collector
  - Path-wise stub burden reporter
  - Google Test preview reporter
```

この順であれば、設計が現実のASTに耐えるかを早期に検証できる。
