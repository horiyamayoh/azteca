# 20. Runtime Contract V3

## 1. 目的

この文書は、生成kernelと生成Google Testを支える `azteca_runtime.hpp` の契約を定義する。

ランタイムの目的は、C++オブジェクトを偽造することではない。対象メソッドのユニットテスト可能な意味を安全に表現することである。

## 2. Runtime Components

```text
azteca::scenario_base
azteca::query<Sig>
azteca::effect<Args...>
azteca::operation<Sig>
azteca::missing_observation
azteca::object_id
azteca::object_ref<T>
azteca::cell<T>
azteca::ref<T>
azteca::dispatch_table<T>
azteca::type_tag
azteca::lifetime_state
azteca::byte_view
azteca::result<T>
```

Google Test依存のadapterは別ヘッダに分ける。

```text
azteca_gtest.hpp
```

`azteca_runtime.hpp` 自体はGoogle Testに依存しない。

## 3. scenario

生成scenarioは、対象メソッドのテスト入口である。

```cpp
namespace azteca_gen::scenario {

struct C_m {
    C_m_self self;
    C_m_when when;
    C_m_effects effects;

    R call(Args... args);
};

}
```

契約:

```text
- scenarioはself、when、effectsを1つにまとめる。
- call()は生成kernelを呼ぶ。
- call()はfake C objectを作らない。
- queryが未設定ならmissing_observationを発生させる。
- effectは標準で記録する。
```

## 4. query

`query<R(Args...)>` は、外界から対象メソッドが観測する戻り値を供給する。

```cpp
namespace azteca {

template<class Sig>
class query;

template<class R, class... Args>
class query<R(Args...)> {
public:
    auto returns_for(Args... args, R value) -> void;
    auto returns(R value) -> void; // 引数を区別しない簡易設定
    R call(Args... args);

    void expect_not_called() const;
    std::size_t call_count() const noexcept;
};

}
```

生成Scenario APIでは、より読みやすい形を出す。

```cpp
s.when.repo_load(id).returns(OrderShape{...});
s.when.clock_now().returns(Time{900});
```

未設定queryに到達した場合:

```cpp
throw azteca::missing_observation{...};
```

暗黙の `false`、`0`、`std::nullopt` などは返してはならない。

## 5. effect

`effect<Args...>` は、外界へ送る要求を記録する。

```cpp
namespace azteca {

template<class... Args>
class effect {
public:
    void record(Args... args);
    void expect_once(Args... args) const;
    void expect_none() const;
    std::size_t call_count() const noexcept;
};

}
```

Google Test非依存runtimeでは、`expect_once` は例外またはruntime assertionを使える。
`azteca_gtest.hpp` をincludeした生成テストでは、Google Test failureとして報告できるadapterを使う。

## 6. operation

`operation<R(Args...)>` は、戻り値供給と効果記録の両方を行う。

```cpp
namespace azteca {

template<class Sig>
class operation;

template<class R, class... Args>
class operation<R(Args...)> {
public:
    auto returns_for(Args... args, R value) -> void;
    R call(Args... args);

    void expect_once(Args... args) const;
    std::size_t call_count() const noexcept;
};

}
```

## 7. missing_observation

```cpp
namespace azteca {

class missing_observation : public std::exception {
public:
    std::string_view port_name() const noexcept;
    std::string_view source_file() const noexcept;
    int source_line() const noexcept;
    std::string_view suggested_scenario_line() const noexcept;
};

}
```

診断例:

```text
Missing observation for policy_can_approve(UserShape, OrderShape) -> bool
Source: order_service.cpp:31
Suggested: s.when.policy_can_approve(/* user */, /* order */).returns(true);
```

## 8. object_id

`object_id` は、テスト世界におけるオブジェクト同一性を表す。

```cpp
namespace azteca {

class object_id {
public:
    static object_id fresh();

    bool operator==(object_id const&) const noexcept;
    bool operator!=(object_id const&) const noexcept;
};

}
```

契約:

```text
- fresh()は一意なidを返す。
- object_idは実アドレスではない。
- object_idはC++オブジェクトライフタイムを持たない。
```

## 9. object_ref<T>

`object_ref<T>` は、`this` や依存戻り値の同一性をテスト世界で表す。

```cpp
namespace azteca {

template<class T>
class object_ref {
    object_id id_;
public:
    explicit object_ref(object_id id) noexcept;
    object_id id() const noexcept;

    bool operator==(object_ref const&) const noexcept;
    bool operator!=(object_ref const&) const noexcept;
};

}
```

禁止:

```cpp
operator T*();
operator void*();
T& get();
```

`object_ref` は実オブジェクトを指さない。

## 10. cell<T> and ref<T>

`cell<T>` は、フィールドやローカル変数がaddressableになる場合の値格納である。

```cpp
namespace azteca {

template<class T>
class cell {
public:
    cell();
    explicit cell(T value);

    T& get();
    T const& get() const;
    ref<T> ref();
    ref<T const> cref() const;
    void set(T value);
};

template<class T>
class ref {
public:
    T& get() const;
};

}
```

未構築の `T` を偽装しない。

## 11. effects aggregation

生成scenarioは個別effectを持つが、全体検証のため集約APIも提供する。

```cpp
s.effects.expect_no_unexpected_calls();
s.effects.expect_none();
s.effects.trace();
```

traceはrecord/replay、diagnostics、golden comparisonに使える。

## 12. dispatch_table and type_tag

virtual callやdynamic typeは、必要に応じてruntimeモデルを使う。

```cpp
struct Shape_area_dispatch {
    azteca::query<int(azteca::object_ref<Shape>)> area;
};
```

`type_tag` はテスト世界の型タグであり、実C++ RTTIを標準では要求しない。

## 13. Google Test Adapter

`azteca_gtest.hpp` は、runtime診断をGoogle Test failureへ変換する。

```cpp
#include <gtest/gtest.h>
#include <azteca/azteca_gtest.hpp>

AZTECA_EXPECT_EFFECT_ONCE(s.effects.bus_publish, OrderApproved{id});
AZTECA_EXPECT_NO_UNEXPECTED_EFFECTS(s.effects);
AZTECA_EXPECT_MISSING_OBSERVATION(s.call(id), "policy_can_approve");
```

ただし、標準生成テストは可能な限り通常のGoogle Test assertionを使う。

```cpp
EXPECT_EQ(result, OK);
s.effects.bus_publish.expect_once(OrderApproved{id});
```

## 14. 契約

```text
1. runtimeはfake C++ objectを作らない。
2. kernelはGoogle Test非依存にする。
3. scenario runtimeも原則Google Test非依存にする。
4. Google Test adapterは薄く保つ。
5. queryの暗黙デフォルト値は禁止。
6. effectは記録され、assert可能である。
7. object_refは実ポインタへ変換できない。
8. generated Google Testはscenario APIを通じてkernelを試験する。
```
