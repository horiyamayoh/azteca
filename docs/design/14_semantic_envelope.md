# 14. Semantic Envelope

## 目的

この文書は、アステカが「より多くのメソッドを、利用者に複雑なモード選択を強いずに取り出す」ための中核概念である **Semantic Envelope** を定義する。

Semantic Envelopeとは、対象メソッドの意味をユニットテスト可能な形で保持するため、`self`、`deps`、`effects`、`object_ref`、`dispatch`、`lifetime`、`byte_view` などを必要最小限だけ自動的に追加する仕組みである。

```text
単純なメソッド:
  self fields だけで足りる。

this のアドレスを使うメソッド:
  object_ref を追加する。

virtual call を含むメソッド:
  dispatch table を追加する。

外部関数を呼ぶメソッド:
  dependency boundary と effect log を追加する。

delete this を含むメソッド:
  lifetime state を追加する。

raw memory view を使うメソッド:
  byte_view または byte boundary を追加する。
```

利用者から見ると、どれも単に「抽出されたkernel」である。

## 基本思想

アステカは、対象メソッドを次のように考える。

```text
method body = receiver state + arguments + dependencies + effects の状態遷移
```

したがって、`this` を実オブジェクトとして偽造する必要はない。必要なのは、対象メソッドが観測・変更する意味を表す受け皿である。

Semantic Envelopeは、その受け皿を自動的に設計する。

## Envelope Components

### 1. Field State

最小の状態モデル。

```cpp
class C {
    int x;
public:
    int f() { return x + 1; }
};
```

生成:

```cpp
struct C_f_self {
    int x;
};

int C_f(C_f_self const& self) {
    return self.x + 1;
}
```

### 2. Base State

基底クラスの状態。

```cpp
struct B { int b; };
struct C : B { int c; };
```

生成:

```cpp
struct C_f_self {
    B_self base_B;
    int c;
};
```

複数継承では、基底ごとに独立したbase slotを持つ。

### 3. Addressable Cells

メンバのアドレス、参照、ポインタ、aliasが必要な場合、単純な値フィールドではなく addressable cell に昇格する。

元コード:

```cpp
int* p = &x;
*p += 1;
return x;
```

生成概念:

```cpp
struct C_f_self {
    azteca::cell<int> x;
};

int C_f(C_f_self& self) {
    auto p = self.x.ref();
    p.get() += 1;
    return self.x.get();
}
```

この昇格は自動で行う。利用者は意識しない。

### 4. Object Identity

`this` の同一性が意味になる場合、実 `C*` ではなく `azteca::object_ref<C>` を使う。

元コード:

```cpp
C* C::self() { return this; }
```

生成:

```cpp
azteca::object_ref<C> C_self(C_self_self& self) {
    return self.object_ref();
}
```

`object_ref<C>` は、実C++オブジェクトを指すポインタではない。テスト世界でのオブジェクト同一性を表す安全な値である。

### 5. Effect Log

外部に何かを登録する、通知する、送信する、といった副作用は effect log に記録できる。

元コード:

```cpp
registry.add(this);
```

生成:

```cpp
deps.registry_add(self.object_ref());
effects.record("registry.add", self.object_ref());
```

ユニットテストでは、実registryを動かさなくても「登録しようとした」ことを検証できる。

### 6. Dependency Boundary

外部関数や未抽出メソッドは依存境界にする。

元コード:

```cpp
return fee(amount) + external_rate();
```

生成:

```cpp
return C_fee(self, deps, effects, amount)
     + deps.external_rate();
```

`external_rate` の戻り値はテストで指定できる。

### 7. Dynamic Type

`dynamic_cast`、polymorphic `typeid`、virtual dispatchに必要な動的型情報。

```cpp
struct C_f_self {
    azteca::type_tag dynamic_type;
    azteca::dispatch_table dispatch;
    // fields...
};
```

単純な型ではこの情報は生成しない。必要になったときだけ追加する。

### 8. Virtual Dispatch

virtual callは、実vtableを偽造せず、明示dispatch tableに変換する。

元コード:

```cpp
return compute(x);
```

`compute` がvirtualなら:

```cpp
return self.dispatch.compute(self.object_ref(), self, deps, effects, x);
```

または、対象動的型が既知なら派生kernelへ静的に解決する。

### 9. Lifetime State

`delete this`、明示デストラクタ呼び出し、placement new on this など、ライフタイム操作の意図を表す。

```cpp
struct C_f_self {
    azteca::lifetime_state lifetime;
};
```

元コード:

```cpp
delete this;
```

生成:

```cpp
C_destructor_kernel(self, deps, effects);
self.lifetime.mark_destroyed();
effects.record_delete(self.object_ref());
```

これは実メモリ解放ではない。ユニットテスト対象としての「自分を破棄する意図」を意味保存する。

### 10. Byte View

オブジェクトのバイト表現を読む処理は、標準では `byte_view` へ抽象化する。

元コード:

```cpp
auto* p = reinterpret_cast<unsigned char*>(this);
return checksum(p, sizeof(C));
```

生成候補:

```cpp
return deps.checksum(self.byte_view(), self.byte_size());
```

または、対象が安全に表現できる場合:

```cpp
return azteca::checksum(self.representation_bytes());
```

実C++オブジェクトのpadding、vptr、ABIレイアウトを偽造しない。

### 11. Global State Model

グローバル変数を読む・書く場合、標準では依存環境に移す。

元コード:

```cpp
return x + global_rate;
```

生成:

```cpp
return self.x + env.global_rate;
```

ただし、既存グローバルをそのまま読む設定も可能にする。標準では再現性を優先する。

## 自動昇格規則

Semantic Envelopeは、構文に応じて自動的に拡張される。

| 検出された構文・意味 | 追加されるEnvelope | 生成方針 |
|---|---|---|
| `this->x` / implicit member access | field state | `self.x` |
| `&this->x` | addressable cell | `self.x.ref()` |
| reference member access | addressable cell | alias preserving ref |
| `return this` | object identity | `self.object_ref()` |
| `this == other` | object identity | object_ref comparison |
| `external(this)` | object identity + dependency boundary + effect | `deps.external(self.object_ref())` |
| virtual call | dynamic type + dispatch | explicit dispatch table |
| `dynamic_cast` | dynamic type | generated type test |
| `typeid(*this)` | dynamic type | generated type info |
| `delete this` | lifetime + destructor kernel + effect | mark destroyed |
| `this->~C()` | lifetime + destructor kernel | mark destroyed |
| placement new on `this` | lifetime + constructor kernel | reinitialize self |
| `reinterpret_cast<char*>(this)` | byte view | representation boundary |
| `memcpy(this, ...)` | byte view or lifetime boundary | representation mutation |
| global read/write | env/global model | `env.name` |
| external call | deps/effect | generated dependency |
| unmodeled inline asm | boundary or not-meaningful | report |

## 例: this escapeを即unsupportedにしない

元コード:

```cpp
class Node {
    Registry& registry_;
    bool enabled_;

public:
    void activate() {
        if (!enabled_) return;
        registry_.add(this);
    }
};
```

生成:

```cpp
struct Node_activate_self {
    azteca::object_id id;
    bool enabled_;
};

struct Node_activate_deps {
    std::function<void(azteca::object_ref<Node>)> registry_add;
};

void Node_activate(
    Node_activate_self& self,
    Node_activate_deps& deps,
    azteca::effects& effects
) {
    if (!self.enabled_) return;

    auto ref = self.object_ref();
    deps.registry_add(ref);
    effects.record_call("Registry::add", ref);
}
```

Google Test例:

```cpp
TEST(Node_activate, registers_self_when_enabled) {
    auto s = azteca_gen::scenario::Node_activate{};

    s.self.enabled_ = true;
    auto ref = s.self.object_ref();

    s.call();

    s.effects.registry_add.expect_once(ref);
}
```

これは製品の `Registry` 実装を試験するものではない。`Node::activate` が正しい条件で自身を登録しようとするロジックを試験する。

## 例: virtual call

元コード:

```cpp
struct Shape {
    int scale_;
    virtual int area() const = 0;

    int scaled_area() const {
        return area() * scale_;
    }
};
```

生成:

```cpp
struct Shape_scaled_area_self {
    int scale_;
    azteca::object_id id;
    azteca::dispatch_table<Shape> dispatch;
};

int Shape_scaled_area(Shape_scaled_area_self const& self) {
    return self.dispatch.area(self.object_ref()) * self.scale_;
}
```

テストでは `area` を直接与える。

```cpp
TEST(Shape_scaled_area, uses_dispatch_observation) {
    auto s = azteca_gen::scenario::Shape_scaled_area{};

    s.self.scale_ = 3;
    s.when.area(s.self.object_ref()).returns(10);

    EXPECT_EQ(s.call(), 30);
}
```

抽象クラスでも実オブジェクトを作らずに、ロジックを試験できる。

## 例: delete this

元コード:

```cpp
void RefCounted::release() {
    --count_;
    if (count_ == 0) delete this;
}
```

生成:

```cpp
void RefCounted_release(
    RefCounted_release_self& self,
    RefCounted_release_deps& deps,
    azteca::effects& effects
) {
    --self.count_;
    if (self.count_ == 0) {
        RefCounted_destructor(self, deps, effects);
        self.lifetime.mark_destroyed();
        effects.record_delete(self.object_ref());
    }
}
```

Google Test例:

```cpp
TEST(RefCounted_release, marks_destroyed_at_zero) {
    auto s = azteca_gen::scenario::RefCounted_release{};

    s.self.count_ = 1;
    auto ref = s.self.object_ref();

    s.call();

    EXPECT_EQ(s.self.count_, 0);
    EXPECT_TRUE(s.self.lifetime.destroyed());
    s.effects.delete_this.expect_once(ref);
}
```

このテストは、allocatorや実delete演算子ではなく、`release` の分岐ロジックと破棄意図を試験する。

## 意味保存の範囲

Semantic Envelopeによる抽出は、次の意味を保存する。

```text
- 制御フロー
- 値計算
- receiver state更新
- dependency呼び出しの発生、順序、引数、戻り値利用
- object identityに関する比較・返却・外部渡し
- dynamic typeによる分岐
- virtual dispatchの選択点
- lifetime操作の意図
- byte accessの意図、または安全に表現できるbyte値
```

次の意味は標準では保存しない。

```text
- 実メモリアドレスの数値
- 実vtableアドレス
- ABI固有レイアウト
- padding byteの偶然値
- OS資源の実副作用
- 未定義動作の結果
```

## なぜこれが単純化なのか

一見するとSemantic Envelopeは複雑である。しかし、これは利用者に見せる複雑さではない。

```text
悪い単純化:
  対応範囲を狭くし、少し難しい構文でunsupportedにする。

良い単純化:
  利用者には `extract` だけを見せ、内部で必要な意味モデルを自動拡張する。
```

アステカは後者を選ぶ。

## 実装上のInvariant

```text
INV-SE-001:
  Semantic Envelopeは、fake C objectを作ってはならない。

INV-SE-002:
  object_ref<C> は C* へ暗黙変換できてはならない。

INV-SE-003:
  addressable cellは、対象フィールドのC++値カテゴリとconst性を保存する。

INV-SE-004:
  dependency boundaryは、必ずsource locationと元callee情報を持つ。

INV-SE-005:
  effect logは、依存呼び出しの順序を保存する。

INV-SE-006:
  lifetime.mark_destroyed() 後のfield accessは、生成kernel内で診断またはテスト時assert対象にする。

INV-SE-007:
  byte_viewは、実C++オブジェクトレイアウトを偽造しない。
```

## Definition of Done

Semantic Envelopeが設計として成立したと言える条件:

```text
1. this escapeをobject_ref/effect/dependencyへ落とせる。
2. address-taking fieldをaddressable cellへ自動昇格できる。
3. virtual callをdispatch tableへ落とせる。
4. delete thisをlifetime effectへ落とせる。
5. external callを標準でdependency boundaryへ落とせる。
6. どの昇格が起きたかをreportに表示できる。
7. 利用者はそれでも `azteca extract` だけで始められる。
```
