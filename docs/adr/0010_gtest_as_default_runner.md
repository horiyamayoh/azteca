# ADR-0010: Google Testを標準テストランナーにする

## Status

Accepted

## Context

アステカが生成するkernelは、何らかのテストランナーで実行される必要がある。C++では `main` と `assert` だけでもユニットテストは可能だが、利用者に独自runnerを学ばせると導入障壁が上がる。

Google TestはC++テストフレームワークとして広く使われ、CMake/CTest連携もしやすい。

## Decision

アステカの標準生成テストはGoogle Testとする。

```cpp
#include <gtest/gtest.h>
#include "C_m.scenario.hpp"

TEST(C_m, sample) {
    auto s = azteca_gen::scenario::C_m{};
    auto result = s.call(/* args */);
    EXPECT_EQ(result, /* expected */);
}
```

ただし、生成kernel、self、shape、scenario core runtimeはGoogle Testに直接依存しない。

## Consequences

### Positive

- 既存C++プロジェクトに導入しやすい。
- CI/CTest連携が容易。
- 生成テストが読みやすい。
- 独自runner開発を遅らせられる。

### Negative

- Google Testが使えない組込み/クロス環境ではfallbackが必要になる。
- Google TestのdiscoveryやCMake統合に環境差が出る可能性がある。

### Mitigation

- kernel/scenario runtimeをGoogle Test非依存に保つ。
- `azteca_gtest.hpp` を薄いadapterにする。
- 必要時のみstandalone runnerを生成できる余地を残す。
