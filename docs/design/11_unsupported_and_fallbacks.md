# 11. Unsupported and Fallbacks

## 1. 目的

この文書は、Heart modeで扱えない、または初期版で未対応の構文・意味と、それぞれのfallbackを定義する。

Aztecaは「できない」で終わらせない。変換不能な理由を分類し、可能な代替策を提示する。ただし、危険なfake `this`やABIハックには逃げない。

## 2. 分類表

| 構文・意味 | Heart mode | Live mode | 備考 |
|---|---:|---:|---|
| field read/write | yes | yes | 基本対応 |
| private field | yes | yes | Heartではselfへ写像 |
| same-class nonvirtual call | yes | yes | 再帰抽出またはstub |
| free function call | yes | yes | direct or inject |
| global read | yes, warn | yes | 再現性注意 |
| global write | yes, warn | yes | テスト干渉注意 |
| base field | partial | yes | base self model |
| virtual call | partial | yes | dispatch table |
| `return this` | partial | yes | self pointerへ型写像 |
| `return *this` | partial | yes | self referenceへ型写像 |
| `external(this)` | no by default | yes | raw this escape |
| `reinterpret_cast<char*>(this)` | no | yes | layout依存 |
| `dynamic_cast` involving this | no by default | yes | RTTI依存 |
| `typeid(*this)` polymorphic | no by default | yes | 動的型依存 |
| `delete this` | no | yes | lifetime/storage ownership |
| `this->~C()` | no | yes | lifetime終了 |
| placement new into `this` | no | yes | lifetime再開始 |
| bit-field | partial | yes | 初期版は部分対応外 |
| anonymous union | partial | yes | active member model必要 |
| constructor | future partial | yes | init kernel設計が必要 |
| destructor | future partial | yes | resource意味論注意 |
| template method | specialization only | yes | 具体化単位 |
| coroutine | no initial | yes | frame/lifetime複雑 |
| module | no initial | yes | build integration課題 |

## 3. Fallback vocabulary

| Fallback | 意味 |
|---|---|
| dependency injection | 外部呼び出しをstub/function object化する |
| recursive extraction | 依存メソッドもHeart化する |
| explicit model | RTTI/identity等をself modelへ明示する |
| Live mode | 正規実オブジェクトで元メソッドを呼ぶ |
| refactor target | 対象コードを純粋ロジックと外部依存へ分離する |
| test hook | friend observer/factory等を明示的に追加する |
| unsupported | 現在は扱わない |

## 4. raw this escape

例:

```cpp
void C::f() {
    registry.add(this);
}
```

分類:

```text
live_required
```

理由:

- external関数が`C*`のidentityを保存するかもしれない。
- 実layoutやvptrにアクセスするかもしれない。
- self pointerを渡しても`C*`ではない。

Fallback:

1. registryをdependency injectionする。
2. self identity modelを使う。
3. Live modeを使う。

dependency化例:

```cpp
struct C_f_deps {
    required_function<void(C_f_self&)> registry_add;
};

void C_f(C_f_self& self, C_f_deps& deps) {
    deps.registry_add(self);
}
```

## 5. dynamic_cast

例:

```cpp
bool C::is_d() const {
    return dynamic_cast<D const*>(this) != nullptr;
}
```

分類:

```text
live_required
```

Fallback:

1. explicit runtime type model
2. Live mode

model例:

```cpp
struct C_is_d_self {
    enum class dynamic_type { C, D, Other } type;
};

bool C_is_d(C_is_d_self const& self) {
    return self.type == C_is_d_self::dynamic_type::D;
}
```

これはRTTIそのものではなく、テスト用モデルである。APIがRTTIを要求するならLive mode。

## 6. typeid(*this)

polymorphic型の`typeid(*this)`は実動的型に依存する。

Fallback:

- explicit runtime type model
- Live mode

非polymorphic型で静的型だけが問題ならHeart化可能な余地はあるが、初期版では安全側に倒す。

## 7. reinterpret_cast involving this

例:

```cpp
auto bytes = reinterpret_cast<std::byte*>(this);
```

分類:

```text
live_required
```

理由:

- self modelは`C`のobject representationではない。
- padding、alignment、base layout、vptr、ABIが絡む。

Fallback:

- Live mode
- ロジック部分を別メソッドへ切り出す

## 8. delete this

例:

```cpp
void C::release() { delete this; }
```

分類:

```text
live_required
```

理由:

- storage ownershipを操作する。
- self modelは`new C`された実オブジェクトではない。

Fallback:

- Live mode with ownership-aware factory
- deleter dependency model

## 9. placement new into this

例:

```cpp
void C::reset() {
    this->~C();
    new (this) C();
}
```

分類:

```text
live_required
```

理由:

- object lifetimeを明示的に終了・再開始する。
- Heart modeで模倣するにはlifetime state machineが必要。

Fallback:

- Live mode
- 状態リセットロジックをpure functionへ分離

## 10. bit-field

例:

```cpp
class C {
    unsigned flags_ : 3;
public:
    void f(unsigned x) { flags_ = x; }
};
```

分類:

```text
heart_partial_with_modeling
```

理由:

- 代入時の切り詰めや符号が重要。
- layoutは処理系依存要素を含む。

Fallback:

1. selfにも同じbit-field宣言を生成する。
2. explicit bitfield wrapperを使う。
3. Live mode。

初期版ではpartial扱いとし、明示オプションなしでは生成しない。

## 11. anonymous union

例:

```cpp
class C {
    union { int i_; double d_; };
public:
    int f() { return i_; }
};
```

分類:

```text
heart_partial_with_modeling
```

理由:

- active memberを管理する必要がある。
- 読んでよいmemberかどうかが状態依存。

Fallback:

- active tagをselfへ追加
- Live mode

## 12. virtual base

virtual baseは、複数経路で同じbase subobjectを共有する。

Heart modelではshared base identityを明示する必要がある。

Fallback:

- shared pointer/referenceでbase selfを表現
- Live mode

初期版では、単純なvirtual base field readはpartial、layout依存はLive-required。

## 13. template dependent body

テンプレートは具体化されないとbody内の名前解決が完了しない場合がある。

例:

```cpp
template<class T>
auto C::f(T t) { return t.value() + x_; }
```

分類:

```text
unsupported if unspecialized
extractable if concrete specialization resolved
```

Fallback:

- `C::f<int>(int)`のように具体特殊化を指定する。
- buildで観測済みspecializationを列挙する。

## 14. macro complex expression

macroで生成された複雑式は、source spellingとAST生成の対応が難しい場合がある。

分類:

```text
unsupported or extractable if AST regeneration is safe
```

Fallback:

- macro展開後ASTから再生成
- 手動fixture化
- Live mode

## 15. coroutine

例:

```cpp
Task<int> C::f() { co_return x_; }
```

分類:

```text
unsupported initial
```

理由:

- coroutine frameに`this`やlocalが保存される。
- lifetimeとsuspend/resumeが絡む。

Fallback:

- Live mode
- coroutine bodyのpure helperを抽出対象にする

## 16. constructor

constructorは通常メソッドと違い、member initializerとobject lifetime開始が絡む。

初期版分類:

```text
unsupported as target method
```

将来fallback:

- init kernel生成
- constructor bodyだけHeart化
- Live mode

## 17. destructor

destructorは資源解放とlifetime終了が絡む。

初期版分類:

```text
unsupported as target method
```

将来fallback:

- pure state cleanupならHeart destructor kernel
- resource releaseはLive mode推奨

## 18. private nested type

元メソッドがprivate nested typeを使う場合、生成コードから型名にアクセスできないことがある。

Fallback:

1. type erasure
2. dependency化
3. generated codeを元クラスfriendにする明示hook
4. Live mode

## 19. unsupported診断テンプレート

```text
FAIL unsupported
Target: <method>
Location: <file:line:col>
Construct: <construct>
Reason: <why Heart cannot safely lower it>
Fallbacks:
  - <fallback 1>
  - <fallback 2>
Rule: <LR/ADR>
```

## 20. Refactoring suggestions

Aztecaは、可能なら対象コードの小さなリファクタ案も出す。

例:

```cpp
void C::register_me() {
    registry.add(this);
    score_ = compute_score();
}
```

提案:

```cpp
int C::compute_score_only() const { ... }
void C::register_me() {
    registry.add(this);
    score_ = compute_score_only();
}
```

Aztecaは`compute_score_only`をHeart化できる。

## 21. 優先対応表

| 項目 | 優先度 | 理由 |
|---|---:|---|
| base class member | 高 | 一般的 |
| loops/range-for | 高 | 一般的 |
| overloaded operator | 中 | 型依存ロジックで必要 |
| lambda this capture | 中 | 現代C++で一般的 |
| virtual call dispatch | 中 | 抽象設計で必要 |
| template specialization | 中 | 必須だが範囲制御必要 |
| bit-field | 低〜中 | 組込み系で重要 |
| constructor/destructor | 中 | 別pipelineが必要 |
| coroutine | 低 | 初期スコープ外 |

## 22. Open questions

1. partial modelingをどの程度自動提案するか。
2. Live-requiredとunsupportedの境界をどれだけ厳密に分けるか。
3. private nested typeへのfriend hook自動生成を許すか。

初期判断:

- partial modelingは診断のみ、明示オプションが必要。
- 実オブジェクトなら正確に試験できるものはLive-required。
- friend hookは自動挿入しない。
