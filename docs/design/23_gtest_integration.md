# 23. Google Test Integration

## 1. 目的

この文書は、アステカが生成するテストの標準ランナーをGoogle Testにする設計を定義する。

方針:

```text
Google Testを標準ランナーにする。
GoogleMockは中核にしない。
アステカruntimeとkernelはGoogle Test非依存に保つ。
```

## 2. 採用理由

Google Testを標準にする理由:

```text
- 既存C++プロジェクトで導入しやすい。
- TEST/EXPECT/ASSERTの読みやすい記法がある。
- CMake/CTest連携がしやすい。
- CI/CDで扱いやすい。
- アステカ独自runnerの学習コストを避けられる。
```

## 3. 非目標

```text
- GoogleMockを必須にしない。
- 生成kernelをGoogle Testに依存させない。
- アステカ独自assert言語を標準にしない。
- 既存プロジェクトのテストフレームワークを強制的に置換しない。
```

## 4. 生成CMake

標準生成CMakeは、次の選択肢を持つ。

```text
1. find_package(GTest CONFIG REQUIRED)
2. FetchContentでGoogleTestを取得
3. 親プロジェクトが提供するGTest::gtest_mainを利用
```

標準出力では、利用者環境への侵襲を避けるため、次の順にする。

```text
- 既存GTest targetがあれば使う。
- find_packageできれば使う。
- 明示オプションがある場合だけFetchContentを生成する。
```

例:

```cmake
cmake_minimum_required(VERSION 3.20)
project(azteca_generated_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(GTest CONFIG REQUIRED)

add_executable(Account_withdraw_test
    tests/Account_withdraw.sample_test.cpp
    src/Account_withdraw.kernel.cpp
)

target_include_directories(Account_withdraw_test PRIVATE include)
target_link_libraries(Account_withdraw_test PRIVATE GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(Account_withdraw_test)
```

## 5. 生成テスト形式

### 5.1 基本形

```cpp
#include <gtest/gtest.h>
#include "Account_withdraw.scenario.hpp"

TEST(Account_withdraw, unlocked_path) {
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

### 5.2 Missing observation

```cpp
TEST(Account_withdraw, missing_fee_observation) {
    auto s = azteca_gen::scenario::Account_withdraw{};

    s.self.balance_ = 100;
    s.self.locked_ = false;

    EXPECT_THROW({
        (void)s.call(50);
    }, azteca::missing_observation);
}
```

### 5.3 Effects

```cpp
TEST(C_register, registers_self) {
    auto s = azteca_gen::scenario::C_register{};

    auto ref = s.self.object_ref();
    s.call();

    s.effects.registry_add.expect_once(ref);
}
```

## 6. Google Test Adapter

通常は `EXPECT_EQ` とscenario APIで足りる。

ただし、diagnosticsを改善するために薄いadapterを提供する。

```cpp
#include <azteca/azteca_gtest.hpp>

AZTECA_EXPECT_NO_UNEXPECTED_EFFECTS(s.effects);
AZTECA_EXPECT_EFFECT_ONCE(s.effects.bus_publish, OrderApproved{id});
AZTECA_EXPECT_MISSING_OBSERVATION(s.call(id), "policy_allow");
```

adapterはGoogle Testの非fatal/fatal failure semanticsに従う。

## 7. GoogleMockとの関係

GoogleMockは使ってよいが、標準依存モデルではない。

理由:

```text
- アステカのportは元interfaceと一致しないことがある。
- shape化やexpression-level portはmock objectより自然である。
- 依存クラスfakeを作ると、テスタビリティ問題へ戻る。
```

許容される用途:

```text
- 既存プロジェクトがGoogleMock matchersを使いたい場合。
- effects traceをmatcherで検査したい場合。
- record/replay境界で既存mockを利用したい場合。
```

## 8. 独自runner fallback

独自runnerを導入する可能性は残すが、標準ではない。

導入条件:

```text
- Google Testで表せない実行制御が必要。
- fuzzing専用harness。
- cross compile環境でGoogle Test discoveryが使えない。
- 組込みターゲットでGoogle Testが過重。
```

その場合でも、kernel/scenario runtimeはそのまま再利用する。

## 9. 生成物

```text
azteca-out/
  CMakeLists.txt
  include/
    azteca/
      azteca_runtime.hpp
      azteca_gtest.hpp
    generated/
      Account_withdraw.self.hpp
      Account_withdraw.shape.hpp
      Account_withdraw.ports.hpp
      Account_withdraw.scenario.hpp
      Account_withdraw.kernel.hpp
  src/
    Account_withdraw.kernel.cpp
  tests/
    Account_withdraw.sample_test.cpp
    Account_withdraw.skeleton_test.cpp
  manifest.json
  azteca_report.md
```

## 10. 成功基準

```text
1. `cmake -S azteca-out -B azteca-out/build` が通る。
2. `cmake --build azteca-out/build` が通る。
3. `ctest --test-dir azteca-out/build --output-on-failure` が通る。
4. Google Test sampleがself、when、effectsを使っている。
5. 生成kernelはGoogle Test非依存である。
6. 独自runnerなしで標準利用できる。
```
