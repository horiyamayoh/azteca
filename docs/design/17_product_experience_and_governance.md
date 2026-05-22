# 17. Product Experience and Governance V3

## 1. 目的

この文書は、アステカを「理論的には面白いが実務で扱いづらいツール」にしないためのUX・運用・保守方針を定義する。

V3では、特に次を強化する。

```text
- 利用者にmode選択を要求しない。
- 依存fakeクラスを大量に書かせない。
- 生成テストはGoogle Testでそのまま動く。
- テスト経路ごとに必要な観測だけを書く。
```

## 2. プロダクト原則

### PX-001: One Command First

最初の体験は必ず1コマンドである。

```bash
azteca extract -p build --method 'C::m(int)'
```

出力:

```text
- kernel
- self
- shape
- scenario
- Google Test sample
- report
- manifest
```

### PX-002: No Mode Tax

利用者に抽象的なmode選択を要求しない。

悪いUX:

```bash
azteca extract --mode rich-heart --identity-model object --dispatch stub --lifetime symbolic
```

良いUX:

```bash
azteca extract -p build --method 'C::m(int)'
```

内部で必要なSemantic Envelopeを追加する。

### PX-003: No Fake Class Tax

依存クラスのfakeを大量に作らせない。

悪いUX:

```cpp
class FakeRepo : public Repo { ... };
class FakeClock : public Clock { ... };
class FakePayment : public Payment { ... };
```

良いUX:

```cpp
s.when.repo_load(id).returns(OrderShape{...});
s.when.clock_now().returns(Time{900});
s.effects.payment_reserve.expect_once(Money{5000});
```

### PX-004: Google Test First

標準生成テストはGoogle Testである。

```cpp
#include <gtest/gtest.h>
#include "C_m.scenario.hpp"

TEST(C_m, sample) {
    auto s = azteca_gen::scenario::C_m{};
    auto result = s.call(/* args */);
    EXPECT_EQ(result, /* expected */);
}
```

独自runnerは標準にしない。

### PX-005: Boundary is Normal

外部依存境界は失敗ではない。生成テストの自然な一部である。

```text
Azteca extracted the method logic.
It generated:
  - receiver state for 3 fields
  - 2 dependency observations
  - 1 observable effect
  - a Google Test sample
```

### PX-006: Reports Explain, They Do Not Demand

レポートは、利用者に内部設計用語を押し付けない。

悪いレポート:

```text
Classification: rich-heart-with-identity-and-boundary-and-transcript
```

良いレポート:

```text
Azteca extracted the method logic.
To test the success path, provide these observations:
  - repo_load(id) -> OrderShape
  - policy_allow(id) -> bool

The method may produce these effects:
  - notifier_send(id)
```

### PX-007: Missing Query Must Be Helpful

未設定queryに到達したら、単に例外を投げるのではなく、次に書くべきテスト行を示す。

```text
Missing observation: policy_allow(Id) -> bool
Suggested:
  s.when.policy_allow(id).returns(true);
```

### PX-008: Generated Tests Must Compile

生成サンプルテストは、そのままCMakeでビルドできることを原則とする。

```text
- void effectは標準で記録する。
- non-void queryはsample内にplaceholderまたは代表値を置く。
- placeholderが不可能な型はコメント付きskeletonとして生成する。
```

## 3. 利用者が覚える概念

V3で利用者が覚える概念は、最大でも次に抑える。

```text
self:
  対象メソッドが読む/書くreceiver状態。

scenario:
  self、依存観測、effects、callをまとめたテスト用オブジェクト。

when:
  外界から対象メソッドへ与える観測値。

effects:
  対象メソッドが外界へ送った要求の記録。
```

内部概念:

```text
MMIR
Semantic Envelope
object_ref
addressable cell
dispatch table
type_tag
lifetime_state
byte_view
Dependency Transcript
```

内部概念は必要なときだけreportに出す。

## 4. 標準生成テストの形

```cpp
#include <gtest/gtest.h>
#include "OrderService_approve.scenario.hpp"

TEST(OrderService_approve, success_path) {
    auto s = azteca_gen::scenario::OrderService_approve{};

    auto id = OrderId{10};
    s.self.user_id = UserId{42};

    s.when.repo_load(id).returns(OrderShape{
        .deadline = Time{1000},
        .amount = Money{5000},
    });
    s.when.clock_now().returns(Time{900});
    s.when.policy_can_approve().returns(true);
    s.when.risk_score(Money{5000}, UserId{42}).returns(20);

    auto result = s.call(id);

    EXPECT_EQ(result, OK);
    s.effects.payment_reserve.expect_once(Money{5000});
    s.effects.repo_mark_approved.expect_once(id);
    s.effects.bus_publish.expect_once(OrderApproved{id});
    s.effects.expect_no_unexpected_calls();
}
```

## 5. Report Layout

`azteca_report.md` は次の構成にする。

```text
1. Summary
2. Generated Google Test entry point
3. What was extracted
4. How to test this method
5. Receiver state
6. Required observations by path
7. Observable effects
8. Generated shapes
9. Object identity notes
10. Source mapping
11. Limitations
12. Suggested next tests
```

## 6. Inspect出力例

```text
Azteca can extract OrderService::approve(OrderId).

Generated Google Test:
  tests/OrderService_approve.sample_test.cpp

Receiver state:
  - UserId user_id

Generated shapes:
  OrderShape
    - Time deadline
    - Money amount

Dependency observations:
  query repo_load(OrderId) -> optional<OrderShape>
  query clock_now() -> Time
  query policy_can_approve(UserId, OrderShape) -> bool
  query risk_score(Money, UserId) -> int

Observable effects:
  payment_reserve(Money)
  repo_mark_approved(OrderId)
  bus_publish(OrderApproved)

Path-wise test burden:
  not_found:
    queries: repo_load
    effects: none
  expired:
    queries: repo_load, clock_now
    effects: none
  success:
    queries: repo_load, clock_now, policy_can_approve, risk_score
    effects: payment_reserve, repo_mark_approved, bus_publish
```

## 7. Configuration Philosophy

設定ファイルは小さく保つ。

```yaml
methods:
  - ns::OrderService::approve(OrderId)

test:
  framework: googletest

boundaries:
  default: transcript

naming:
  namespace: azteca_gen
```

詳細設定は可能だが、標準では不要にする。

```yaml
boundaries:
  Repo::load:
    strategy: query
    shape: OrderShape
  Payment::reserve:
    strategy: operation
  Audit::write:
    strategy: effect
  Math::clamp:
    strategy: recursive
```

## 8. Governance Rules

```text
1. 新機能は、利用者概念を増やさないことを第一に検討する。
2. mode名を公開CLIへ増やす場合はADR必須。
3. 依存fakeクラス生成を標準にする提案はADR必須。
4. Google Testから独自runnerへ標準変更する場合はADR必須。
5. 未設定queryの暗黙デフォルト値は禁止。
6. 生成テストがコンパイルできない変更は禁止。
7. reportは内部分類名ではなく、次に何を書けばよいかを示す。
```
