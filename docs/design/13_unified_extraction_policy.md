# 13. Unified Extraction Policy V3

## 1. 目的

この文書は、アステカを「多くのモードを備えた抽象的なツール」ではなく、利用者にとって単純な **単一の抽出操作** として提供する方針を定義する。

V3では、依存関係の扱いとGoogle Test生成もこの単一方針へ統合する。

## 2. 決定

アステカの公開UXでは、以下を基本方針とする。

```text
1. 公開コマンドは `extract` を中心にする。
2. `--mode heart` や `--mode live` を通常利用者に要求しない。
3. 抽出器は、対象メソッドに必要な意味モデルを自動的に計画する。
4. 依存関係はDependency Transcriptとして表現する。
5. 生成テストはGoogle Testを標準にする。
6. 変換不能性は「利用者が選ぶモード」ではなく「抽出結果の説明」として提示する。
```

標準コマンド:

```bash
azteca extract -p build --method 'ns::Account::withdraw(int)' --out azteca-out
```

## 3. 利用者に見せる概念

公開概念は、最小限にする。

```text
Kernel
  取り出されたメソッドロジック。

Self
  元クラスのうち、対象メソッドのロジックに必要な状態。

Scenario
  self、依存観測、effects、callをまとめたGoogle Test向け入口。

When
  対象メソッドが外界から観測する値を設定するAPI。

Effects
  対象メソッドが外界へ送った要求を検証するAPI。

Report
  何をどう抽出したか、どの観測が必要かを説明する文書。
```

通常利用者に見せない概念:

```text
Heart mode
Live mode
Rich Heart mode
fallback mode
Semantic Envelope internals
MMIR internals
```

## 4. 抽出結果のステータス

モード選択ではなく、結果説明として次を使う。

```text
extracted
  メソッドロジックを直接kernel化できた。

extracted-with-transcript
  主要ロジックをkernel化し、外部依存をquery/effect/operationとして表現した。

extracted-as-effect-model
  this identity、lifetime、dynamic type、byte view などを含む意味モデルとして抽出した。

not-meaningful-for-unit-extraction
  メソッドの本体がほぼ全て外部I/O、未定義動作、inline asm、実ハードウェア操作などで、ロジック単体として取り出す価値が乏しい。
```

## 5. 単純化原則

### 原則1: まず抽出する

分類して終わるのではなく、原則として何らかのテスト可能なkernel/scenarioを生成する。

悪い出力:

```text
this escapes. unsupported.
```

望ましい出力:

```text
this escapes through registry.add(this).
Generated:
  s.self.object_ref()
  s.effects.registry_add.expect_once(s.self.object_ref())
```

### 原則2: 依存fakeを作らせない

悪い出力:

```text
Please implement FakeRepo, FakeClock, FakePolicy, FakeBus.
```

望ましい出力:

```cpp
s.when.repo_load(id).returns(OrderShape{...});
s.when.clock_now().returns(Time{900});
s.when.policy_allow(id).returns(true);
s.effects.bus_publish.expect_once(OrderApproved{id});
```

### 原則3: Google Testで始める

生成テストはGoogle Testで始める。

```cpp
#include <gtest/gtest.h>
#include "generated/account.withdraw.scenario.hpp"

TEST(Account_withdraw, unlocked_path) {
    auto s = azteca_gen::scenario::Account_withdraw{};
    s.self.balance_ = 100;
    s.self.locked_ = false;
    s.when.fee(50).returns(5);

    auto result = s.call(50);

    EXPECT_EQ(result, 45);
    EXPECT_EQ(s.self.balance_, 45);
}
```

### 原則4: 危険な正確性より、安全な抽象化

fake this や ABI hack によって製品コードそのものを呼ぶより、ASTから意味を取り出して安全な抽象化を生成する。

### 原則5: 境界は失敗ではない

外部関数、OS、I/O、未取得の他メソッドは、ユニットテスト上は依存境界である。境界化は逃げではなく、ユニットテストの自然な分離である。

## 6. 生成物の標準形

```text
azteca-out/
  include/
    azteca/
      azteca_runtime.hpp
      azteca_gtest.hpp
    generated/
      target.method.self.hpp
      target.method.shape.hpp
      target.method.ports.hpp
      target.method.scenario.hpp
      target.method.kernel.hpp
  src/
    target.method.kernel.cpp
  tests/
    target.method.sample_test.cpp
    target.method.skeleton_test.cpp
  azteca_report.md
  manifest.json
  CMakeLists.txt
```

## 7. レポート方針

レポートは、利用者に判断を押し付けるものではない。生成されたscenarioで何を書けばよいかを示す。

例:

```text
Extraction result: extracted-with-transcript

Receiver:
  - int balance_
  - bool locked_

Observations:
  - fee(int) -> int

Generated Google Test:
  tests/account.withdraw.sample_test.cpp

Try this:
  s.self.balance_ = 100;
  s.self.locked_ = false;
  s.when.fee(50).returns(5);
  auto result = s.call(50);
  EXPECT_EQ(result, 45);
```

this escapeがある場合:

```text
Extraction result: extracted-as-effect-model

Detected:
  registry.add(this)

Lowered as:
  s.effects.registry_add.expect_once(s.self.object_ref())

Meaning:
  The method's decision to register itself is testable.
  No fake product object was created.
```

## 8. 厳密性の定義

アステカが守る意味は、次の unit-observable semantics とする。

```text
- 戻り値
- 例外
- receiver state
- dependency observations
- external effects
- call order when meaningful
- object identity
- dynamic type decision
- lifetime intent
- byte access intent
```

標準では守らないもの:

```text
- 実アドレス値
- vptr実表現
- padding byteの偶然値
- allocator内部状態
- OS handle実値
- UBの結果
```

## 9. 契約

```text
1. 利用者にmode選択を要求しない。
2. 依存fakeクラス生成を標準にしない。
3. Google Test生成を標準にする。
4. kernel/scenario runtimeはGoogle Test非依存に保つ。
5. 依存はDependency Transcriptとして扱う。
6. 抽出不能を広く認めず、意味モデルを自動拡張する。
7. reportは次に書くべきscenario行を示す。
```
