# 03. Extraction Pipeline V3

## 1. 目的

この文書は、対象メソッドをASTから抽出し、Google Testで試験可能なkernel/scenarioへ変換する処理順序を定義する。

## 2. Pipeline Overview

```text
1. compile_commands.json を読む
2. 対象translation unitをClangでparseする
3. 対象メソッドをCXXMethodDeclとして解決する
4. Clang ASTからMMIRを構築する
5. receiver stateを収集する
6. dependency observations/effects/operationsを収集する
7. shape候補を生成する
8. object identity/addressability/type/lifetime/byte要素を収集する
9. path-wise stub burdenを概算する
10. Extraction Planを作る
11. kernel/scenario/Google Test/CMake/report/manifestを生成する
```

## 3. Inspect Pipeline

`inspect` はコード生成しない。Extraction Planを表示する。

```bash
azteca inspect -p build --method 'C::m(int)'
```

出力:

```text
- receiver state
- generated shapes
- dependency observations
- observable effects
- object_ref requirements
- path-wise stub burden
- Google Test preview
```

## 4. Extract Pipeline

`extract` は生成物を出す。

```bash
azteca extract -p build --method 'C::m(int)' --out azteca-out
```

生成物:

```text
self.hpp
shape.hpp
ports.hpp
kernel.hpp/cpp
scenario.hpp
sample_test.cpp
skeleton_test.cpp
CMakeLists.txt
manifest.json
azteca_report.md
```

## 5. MMIR構築

MMIRは、Clang ASTを直接文字列置換せず、意味単位に整理する中間表現である。

例:

```cpp
if (locked_) return -1;
balance_ -= amount + fee(amount);
return balance_;
```

MMIR概念:

```text
Branch(ReadField locked_)
Return(Const -1)
WriteField(balance_, Sub(ReadField balance_, Add(Arg amount, DependencyCall fee)))
Return(ReadField balance_)
```

## 6. Dependency Collection

依存呼び出しは次へ分類する。

```text
recursive internal logic
query
operation
effect
expression-level query
shape-producing query
```

例:

```cpp
auto order = repo_.load(id);
if (!order) return ERR_NOT_FOUND;
bus_.publish(OrderApproved{id});
```

Plan:

```text
query repo_load(OrderId) -> optional<OrderShape>
effect bus_publish(OrderApproved)
```

## 7. Shape Planning

依存戻り値の本物を作る代わりに、対象メソッドが観測する部分だけをshape化する。

```cpp
order->deadline()
order->amount()
```

Shape:

```cpp
struct OrderShape {
    Time deadline;
    Money amount;
};
```

## 8. Path-wise Stub Burden

制御フローから、経路ごとに必要なquery/effectを概算する。

```text
not_found:
  required observations: repo_load
  effects: none

success:
  required observations: repo_load, clock_now, policy_allow
  effects: payment_reserve, bus_publish
```

これはreportとskeleton generationに使う。

## 9. Lowering

Loweringでは、MMIRをC++コードへ変換する。

対応例:

| 元意味                     | 生成意味                 |
| -------------------------- | ------------------------ |
| field read/write           | `self.x`                 |
| same-class pure helper     | recursive kernel         |
| external non-void call     | `ports.xxx.call(...)`    |
| external void call         | `ports.xxx.record(...)`  |
| return this                | `self.object_ref()`      |
| virtual call               | dispatch query/operation |
| dependency object property | shape field              |

## 10. Google Test Generation

標準でGoogle Test sampleを生成する。

```cpp
#include <gtest/gtest.h>
#include "generated/C_m.scenario.hpp"

TEST(C_m, sample) {
    auto s = azteca_gen::scenario::C_m{};
    s.when.some_query().returns(/* value */);
    auto result = s.call(/* args */);
    EXPECT_EQ(result, /* expected */);
}
```

## 11. Failure Handling

失敗時は、単にunsupportedで止めず、理由と代替を示す。

```text
- safe lowering possible -> generate model
- dependency boundary possible -> generate transcript port
- implementation missing -> report as not-yet-implemented with intended model
- unit extraction not meaningful -> explain why
```

## 12. 契約

```text
1. AST/Sema後情報を使う。
2. テキスト置換を中核にしない。
3. fake thisを作らない。
4. 依存fakeクラスを標準にしない。
5. Google Test生成を標準にする。
6. inspectで抽出計画を見せる。
7. extractでscenario付きkernelを生成する。
```
