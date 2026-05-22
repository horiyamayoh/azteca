# 10. Test Strategy V3

## 1. 目的

この文書は、Azteca自身の品質保証と、Aztecaが生成するテストの品質保証を定義する。

V3では、生成テストの標準ランナーを **Google Test** とする。アステカ自身のテストにもGoogle Testを使用する。独自runnerは、Google Testで実現できない制約が明確になった場合のfallbackであり、標準路線ではない。

## 2. テスト対象の二層構造

アステカの品質は、次の二層で保証する。

```text
A. Azteca implementation tests
   resolver / planner / lowerer / codegen / runtime の品質を検証する。

B. Generated method tests
   アステカが生成したkernel、scenario、deps、effectsがGoogle Testで実行できることを検証する。
```

## 3. Azteca implementation tests

```text
tests/
  unit/
    method_spec_parser/
    method_selector/
    mmir_builder/
    envelope_planner/
    dependency_planner/
    shape_planner/
    lowerer/
    codegen/
    runtime/
  fixtures/
    simple_field/
    helper_call/
    dependency_query/
    dependency_effect/
    operation/
    raw_this_escape/
    object_ref/
    shape_return/
    expression_level_port/
    virtual_dispatch/
  golden/
    inspect_reports/
    generated_files/
    manifests/
  integration/
    build_generated/
    run_generated_gtest/
    missing_observation/
    pathwise_stub_burden/
  negative/
    invalid_method_spec/
    ambiguous_method/
    unsupported_ub/
  fuzz/
    method_spec_parser/
    scenario_runtime/
```

## 4. Google Testを標準にする理由

標準テストランナーとしてGoogle Testを採用する理由は次の通り。

```text
- C++ユニットテストの既存資産として広く使われている。
- TEST/TEST_F/EXPECT/ASSERTなどの記法により、生成テストが読みやすい。
- CMake/CTest連携がしやすい。
- 生成テストを既存CIへ投入しやすい。
- アステカ独自runnerを作るより導入負担が小さい。
```

標準生成コードは以下を含む。

```cpp
#include <gtest/gtest.h>
#include "C_m.scenario.hpp"

TEST(C_m, sample_success_path) {
    auto s = azteca_gen::scenario::C_m{};

    s.self.x = 10;
    s.when.some_query(1).returns(2);

    auto result = s.call(1);

    EXPECT_EQ(result, 12);
    s.effects.some_effect.expect_none();
}
```

## 5. 独自runnerを標準にしない理由

C++のユニットテストは `main` と `assert` だけでも可能である。しかし、アステカが独自runnerを標準にすると、利用者は新しいテスト生態系を学ぶ必要がある。

したがって、標準はGoogle Testにする。

独自runnerを検討する条件は限定する。

```text
- Google Testではmissing observationの診断を十分に表せない。
- sandbox実行やrecord/replay制御にrunner固有制約が必要になる。
- fuzzingやsymbolic executionと統合する際にGoogle Testが支障になる。
- 組込み/クロス環境でGoogle Test自体の導入が不可能である。
```

その場合でも、生成kernelとscenario runtimeはGoogle Test非依存に保ち、runnerだけを差し替えられるようにする。

## 6. Google Test依存境界

`azteca_runtime.hpp` と生成kernelは、Google Testへ直接依存しない。

```text
Google Testに依存してよい:
  - generated sample test
  - generated test skeleton
  - gtest adapter
  - Azteca implementation tests

Google Testに依存しない:
  - kernel
  - self
  - shape
  - ports
  - core scenario runtime
  - MMIR
  - lowerer
```

これにより、将来的に別runnerへ切り替える余地を残す。

## 7. 生成Google Testの標準形

### 7.1 単純ロジック

```cpp
TEST(Account_withdraw, unlocked_subtracts_amount_and_fee) {
    auto s = azteca_gen::scenario::Account_withdraw{};

    s.self.balance_ = 100;
    s.self.locked_ = false;
    s.when.fee(50).returns(5);

    auto result = s.call(50);

    EXPECT_EQ(result, 45);
    EXPECT_EQ(s.self.balance_, 45);
    s.effects.expect_none();
}
```

### 7.2 早期return経路

```cpp
TEST(Account_withdraw, locked_returns_error_without_fee_query) {
    auto s = azteca_gen::scenario::Account_withdraw{};

    s.self.balance_ = 100;
    s.self.locked_ = true;

    auto result = s.call(50);

    EXPECT_EQ(result, -1);
    EXPECT_EQ(s.self.balance_, 100);
    s.when.fee.expect_not_called();
}
```

### 7.3 外部効果

```cpp
TEST(OrderService_approve, success_reserves_marks_and_publishes) {
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

## 8. Missing Observation Tests

未設定queryに到達した場合、生成runtimeはmissing observationを発生させる。

標準では例外型を使う。

```cpp
TEST(OrderService_approve, missing_policy_observation_reports_port) {
    auto s = azteca_gen::scenario::OrderService_approve{};

    s.when.repo_load(OrderId{10}).returns(OrderShape{.amount = Money{5000}});
    s.when.clock_now().returns(Time{900});

    EXPECT_THROW({
        (void)s.call(OrderId{10});
    }, azteca::missing_observation);
}
```

追加で、Google Test adapterを提供する。

```cpp
AZTECA_EXPECT_MISSING_OBSERVATION(s.call(OrderId{10}), "policy_can_approve");
```

ただし、このマクロは補助であり、必須ではない。

## 9. Golden tests

Golden testsは、fixture入力から生成される以下を比較する。

```text
- inspect report
- self.hpp
- shape.hpp
- ports.hpp
- scenario.hpp
- kernel.cpp
- sample_test.cpp
- manifest.json
```

更新コマンド:

```bash
AZTECA_UPDATE_GOLDEN=1 ctest -R golden
```

Golden更新はレビュー必須である。

## 10. Compile and Run tests

生成されたGoogle Testプロジェクトは必ずビルド・実行する。

```bash
azteca extract -p build --method 'C::m(int)' --out tmp/out
cmake -S tmp/out -B tmp/out/build
cmake --build tmp/out/build
ctest --test-dir tmp/out/build --output-on-failure
```

検証:

```text
- 生成CMakeがGoogle Testを見つけられる。
- sample_testがコンパイルできる。
- sample_testが実行できる。
- missing observationが期待通り失敗する。
- effects assertionが期待通り失敗/成功する。
```

## 11. Path-wise Stub Burden tests

依存が多いfixtureでは、経路ごとの必要stub数を検証する。

```cpp
int C::f(Id id) {
    if (!enabled_) return DISABLED;
    if (!repo_.exists(id)) return NOT_FOUND;
    if (!policy_.allow(id)) return DENIED;
    notifier_.send(id);
    return OK;
}
```

期待:

```text
DISABLED path:
  required queries: none

NOT_FOUND path:
  required queries: repo_exists

DENIED path:
  required queries: repo_exists, policy_allow

OK path:
  required queries: repo_exists, policy_allow
  effects: notifier_send
```

この検証により、依存総量ではなく、実際のテスト負担を報告できることを保証する。

## 12. Runtime tests

`azteca_runtime` のテスト項目:

```text
query:
  - returns configured value
  - missing observation throws
  - call count is tracked
  - argument mismatch diagnostics are readable

effect:
  - records calls
  - expect_once succeeds/fails correctly
  - expect_none succeeds/fails correctly
  - ordering assertions work

operation:
  - supplies return value
  - records effect
  - missing return value fails

object_ref:
  - identity equality
  - no pointer conversion

shape:
  - value equality
  - printable diagnostics
```

## 13. Sanitizer/Fuzzer

生成kernelは通常のC++コードなので、ASan/UBSan/coverage/fuzzerを載せられる。

ただし、fuzzerはGoogle Test runnerと分けてもよい。

```text
- generated unit tests: Google Test
- generated fuzz harness: libFuzzer/AFL++ adapter
```

## 14. Definition of Done

V3テスト戦略上、Phase A〜Cの完了条件は次の通り。

```text
Phase A:
  inspectがdependency transcript planとpath-wise stub burdenを出せる。

Phase B:
  最小kernelとGoogle Test sampleが生成・実行できる。

Phase C:
  query/effect/operation/scenario/missing observationがGoogle Testで検証できる。
```
