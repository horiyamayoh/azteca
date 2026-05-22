# 16. Universal Lowering Strategy

## 目的

この文書は、アステカが「例外を狭く保ち、ほとんどのメソッドを何らかの有意義なユニットテスト対象として取り出す」ためのlowering戦略を定義する。

ここでの重要な設計変更は次である。

```text
以前:
  ある構文を検出したら Heart不可 / Live必要 / Unsupported と分類する。

改訂:
  ある構文を検出したら、必要なSemantic Envelopeを追加して抽出を継続する。
  それでもユニットテストとして意味が薄いものだけを例外とする。
```

## 基本アルゴリズム

```text
Input:
  CXXMethodDecl target

Algorithm:
  1. Clang ASTからMMIRを生成する。
  2. MMIRを走査して必要なsemantic featuresを収集する。
  3. 最小Envelopeを計画する。
  4. 変換可能な依存は再帰抽出する。
  5. 変換しない依存はBoundaryCallにする。
  6. receiver modelを生成する。
  7. kernelを生成する。
  8. effect/deps/test scaffold/reportを生成する。

Output:
  Generated unit-testable kernel
```

このアルゴリズムに「モード選択」はない。

## Feature収集

MMIRから以下を収集する。

```text
features:
  uses_fields
  writes_fields
  takes_field_address
  uses_this_identity
  returns_this
  passes_this_to_dependency
  uses_virtual_dispatch
  uses_dynamic_type
  uses_lifetime_operation
  uses_byte_representation
  uses_global_state
  uses_external_call
  uses_exception
  uses_template_dependent_construct
  uses_inline_asm
  uses_coroutine
```

各featureは、Envelope Plannerへ渡される。

## Lowering方針一覧

| Feature             | Lowering                             | 例外条件                                           |
| ------------------- | ------------------------------------ | -------------------------------------------------- |
| field read/write    | `self.field`                         | 型が生成不能な場合はopaque field                   |
| field address       | `cell<T>`                            | pointer arithmeticが任意メモリへ出る場合はboundary |
| this identity       | `object_ref<C>`                      | 実C\*数値そのものが要求される場合はboundary        |
| return this         | `object_ref<C>`戻り                  | 元シグネチャ維持が必須ならadapter生成              |
| pass this           | dependencyに`object_ref<C>`を渡す    | calleeも抽出可能ならcalleeも変換                   |
| virtual call        | dispatch table                       | dispatch先が不明でもstub dispatch                  |
| dynamic_cast        | type_tag test                        | private inheritance等は型グラフに基づき診断        |
| delete this         | lifetime effect                      | 実allocator検証は対象外                            |
| destructor call     | destructor kernel                    | 未抽出資源解放はboundary                           |
| placement new       | constructor kernel + lifetime reinit | raw storage layout依存はboundary                   |
| global read/write   | env model                            | 実global使用も設定可能                             |
| external call       | dependency boundary                  | 戻り値はtest sideで供給                            |
| byte representation | byte_view/boundary                   | ABI正確性は標準対象外                              |
| inline asm          | boundary                             | 制御支配ならnot-meaningful                         |
| coroutine           | coroutine state model or boundary    | 初期実装ではboundary                               |

## Direct Field Lowering

元コード:

```cpp
int C::f(int a) {
    x += a;
    return x;
}
```

MMIR:

```text
Assign(FieldRef(x), BinaryOp(+, FieldRef(x), Arg(a)))
Return(FieldRef(x))
```

生成:

```cpp
int C_f(C_f_self& self, int a) {
    self.x += a;
    return self.x;
}
```

## Addressable Field Lowering

元コード:

```cpp
int C::f() {
    int& r = x;
    r += 1;
    return x;
}
```

Envelope:

```text
x is addressable
```

生成:

```cpp
struct C_f_self {
    azteca::cell<int> x;
};

int C_f(C_f_self& self) {
    auto r = self.x.ref();
    r.get() += 1;
    return self.x.get();
}
```

最適化として、addressを取らないフィールドは通常値のままでよい。

## this Identity Lowering

元コード:

```cpp
bool C::is_same(C* other) const {
    return this == other;
}
```

生成シグネチャ:

```cpp
bool C_is_same(C_is_same_self const& self, azteca::object_ref<C> other) {
    return self.object_ref() == other;
}
```

元引数 `C*` はテスト世界では `object_ref<C>` に写像する。

重要:

```text
object_ref<C> は C* ではない。
C* を偽造しない。
```

## this Escape Lowering

元コード:

```cpp
void C::register_me() {
    registry.add(this);
}
```

callee `Registry::add(C*)` が抽出可能な場合:

```cpp
Registry_add(registry_self, deps, effects, self.object_ref());
```

calleeが抽出不能または外部の場合:

```cpp
deps.registry_add(self.object_ref());
effects.record_call("Registry::add", self.object_ref());
```

これにより、`this` escapeは原則として抽出継続可能になる。

## Return this Lowering

元コード:

```cpp
C* C::owner() { return this; }
```

標準生成:

```cpp
azteca::object_ref<C> C_owner(C_owner_self& self) {
    return self.object_ref();
}
```

互換adapter:

```cpp
// 生成しないのが標準。必要な場合のみ明示設定。
C* C_owner_live_adapter(C& obj) {
    return obj.owner();
}
```

ユニットテストでは、object_refで同一性を検査する。

## Virtual Call Lowering

元コード:

```cpp
int Base::score() const {
    return raw_score() * weight_;
}
```

`raw_score` がvirtualなら:

```cpp
int Base_score(Base_score_self const& self) {
    return self.dispatch.raw_score(self.object_ref()) * self.weight_;
}
```

Google Test例:

```cpp
TEST(Base_score, uses_dispatch_observation) {
    auto s = azteca_gen::scenario::Base_score{};

    s.self.weight_ = 2;
    s.when.raw_score(s.self.object_ref()).returns(10);

    EXPECT_EQ(s.call(), 20);
}
```

この方式により、抽象クラスでも単純に試験できる。

## Dynamic Cast Lowering

元コード:

```cpp
if (auto* d = dynamic_cast<Derived*>(this)) {
    return d->special_;
}
return 0;
```

生成概念:

```cpp
if (self.dynamic_type.is_a<Derived>()) {
    auto d = self.as<Derived>();
    return d.special_;
}
return 0;
```

必要なEnvelope:

```text
- dynamic_type
- type graph
- derived self view
```

初期実装では、同一抽出対象内で必要な派生フィールドをselfに含める。

```cpp
struct Base_f_self {
    azteca::type_tag dynamic_type;
    std::optional<Derived_view> derived_Derived;
};
```

## Lifetime Lowering

### delete this

元コード:

```cpp
void C::release() {
    if (--refs_ == 0) delete this;
}
```

生成:

```cpp
void C_release(C_release_self& self, C_release_deps& deps, azteca::effects& effects) {
    if (--self.refs_ == 0) {
        C_destructor(self, deps, effects);
        self.lifetime.mark_destroyed();
        effects.record_delete(self.object_ref());
    }
}
```

### placement new on this

元コード:

```cpp
new (this) C(42);
```

生成:

```cpp
C_constructor_from_int(self, deps, effects, 42);
self.lifetime.mark_live();
effects.record_reinitialize(self.object_ref());
```

実ストレージへのplacement newではなく、self状態の再初期化として扱う。

## External Call Lowering

外部関数呼び出しは、次の優先順で扱う。

```text
1. 関数本体があり、抽出対象として安全なら再帰抽出する。
2. 本体がない、または境界として扱うべきならdependency boundaryにする。
3. 呼び出しの発生と引数をeffect logへ記録する。
4. 戻り値が必要ならdepsから受け取る。
```

元コード:

```cpp
int C::f(int x) {
    return normalize(x) + 1;
}
```

生成:

```cpp
int C_f(C_f_self& self, C_f_deps& deps, azteca::effects& effects, int x) {
    auto v = deps.normalize(x);
    effects.record_call("normalize", x, v);
    return v + 1;
}
```

## Global State Lowering

元コード:

```cpp
extern int threshold;

bool C::ok() const {
    return value_ >= threshold;
}
```

生成:

```cpp
struct C_ok_env {
    int threshold;
};

bool C_ok(C_ok_self const& self, C_ok_env const& env) {
    return self.value_ >= env.threshold;
}
```

これにより、ユニットテストでglobal stateを明示できる。

## Byte Representation Lowering

元コード:

```cpp
int C::hash() const {
    return hash_bytes(reinterpret_cast<unsigned char const*>(this), sizeof(C));
}
```

標準生成:

```cpp
int C_hash(C_hash_self const& self, C_hash_deps& deps) {
    return deps.hash_bytes(self.byte_view());
}
```

`byte_view`の生成方針:

```text
- field-based deterministic representation
- user-supplied representation provider
- dependency boundary
```

標準ではABI実レイアウトを偽造しない。

## Lambda Lowering

元コード:

```cpp
auto f = [this](int x) { return x + bias_; };
return f(10);
```

生成:

```cpp
auto f = [&](int x) { return x + self.bias_; };
return f(10);
```

this captureはself captureへ変換する。lambdaが外部へ逃げる場合は、closureをdependency/effectとして表す。

## Template Lowering

テンプレートは、具体化単位で扱う。

```cpp
template<class T>
T C::f(T x) { return x + bias_; }
```

抽出対象:

```text
C::f<int>(int)
C::f<double>(double)
```

未具体化テンプレートを一つの生成kernelとして万能に扱うことは標準範囲外とする。ただし、将来的にはtemplate-preserving codegenを検討できる。

## Coroutine Lowering

初期実装では、coroutineはdependency boundaryまたはnot-meaningful候補とする。ただし設計上は次の方向を持つ。

```text
co_await / co_yield / co_return
  -> coroutine state self
  -> scheduler dependency
  -> suspension/resumption effect log
```

これは大きな拡張なので、v1以降に回す。

## Inline Assembly

inline asmは原則としてboundaryにする。

```cpp
asm volatile("...");
```

生成:

```cpp
deps.inline_asm_boundary("source-span-id");
effects.record_inline_asm("source-span-id");
```

ただし、asmが出力値を生成し、その値が制御フローを支配する場合、ユニットテストではdepsから値を供給する。

## Not Meaningful Criteria

次の場合のみ、抽出を停止して `not-meaningful-for-unit-extraction` とする。

```text
1. メソッド本体の実質すべてが未モデル化外部環境の操作であり、ロジックが存在しない。
2. 元コードがUBに依存している。
3. AST上に対象メソッド本体が存在しない。
4. すべての出力がinline asmまたは実ハードウェア状態だけにより決まる。
5. 生成kernelが元メソッドのユニットテスト価値を誤認させる危険が高い。
```

この基準は狭く運用する。

## Loweringの正しさ検査

各loweringは次のテストを持つ。

```text
1. MMIR期待値テスト
2. Envelope Plan期待値テスト
3. 生成コードsnapshotテスト
4. 生成コードコンパイルテスト
5. 実行テスト
6. 可能なら製品オブジェクトとの差分検証
```

## Definition of Done

```text
1. this escapeが標準で抽出継続される。
2. external callが標準でdeps/effectsに変換される。
3. virtual callがdispatch tableに変換される。
4. address-taken fieldsがcellへ昇格する。
5. lifetime操作がlifetime effectへ変換される。
6. byte representationは安全なbyte_viewまたはboundaryへ変換される。
7. 利用者はこれらをモード選択なしに使える。
```
