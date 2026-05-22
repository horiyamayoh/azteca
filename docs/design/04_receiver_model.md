# 04. Receiver Model

## 1. 目的

この文書は、非staticメンバ関数の暗黙オブジェクト引数、すなわち`this`を、Heart modeでどのような明示receiver/self modelへ変換するかを定義する。

Receiver ModelはAztecaの中核である。fake `this` を作らない代わりに、メソッド本体が必要とする状態をself modelとして定義し、生成kernelの第一引数として渡す。

## 2. 基本形

元コード:

```cpp
class C {
    int x_;
public:
    int f(int a) {
        x_ += a;
        return x_;
    }
};
```

Heart mode生成:

```cpp
struct C_f_self {
    int x_;
};

int C_f(C_f_self& self, int a) {
    self.x_ += a;
    return self.x_;
}
```

## 3. Receiverの意味

`self`は`C`オブジェクトではない。

`self`は、対象メソッドのHeart kernelを実行するために必要な状態を保持するテスト用モデルである。

禁止:

```cpp
C* p = reinterpret_cast<C*>(&self); // 禁止
p->f(1);                            // 禁止
```

許可:

```cpp
C_f_self self{.x_ = 10};
int r = C_f(self, 5);
```

## 4. Receiver型決定

### 4.1 通常メソッド

```cpp
int C::f();
```

生成:

```cpp
int C_f(C_f_self& self);
```

### 4.2 constメソッド

```cpp
int C::f() const;
```

生成:

```cpp
int C_f(C_f_self const& self);
```

### 4.3 volatileメソッド

初期版では`volatile` receiverは`unsupported`または`live_required`に分類する。

理由:

- `volatile`の意味はメモリ観測やデバイスI/Oと関係しうる。
- self modelで安易に再現すると意味が変わる。

後続対応では、`C_f_self volatile&`を生成するオプションを検討する。

### 4.4 ref-qualifiedメソッド

```cpp
int C::f() &;
int C::f() &&;
int C::f() const &;
```

生成方針:

```cpp
int C_f_lvalue(C_f_self& self);
int C_f_rvalue(C_f_self&& self);
int C_f_const_lvalue(C_f_self const& self);
```

`&&`メソッドでは、元メソッドが`*this`をmove元として扱う可能性がある。selfを`&&`で受け、lowererは`std::move(self.field)`が必要な箇所だけを保持する。

## 5. フィールド収集

Receiver Plannerは、対象メソッド本体で使用される非staticデータメンバを収集する。

収集対象:

- 明示的な`this->x_`
- 暗黙の`x_`
- base class由来の`b_`
- member function call内で再帰抽出されるhelperが使うfield
- lambda this capture内で使うfield

例:

```cpp
class C {
    int a_;
    int b_;
    int unused_;
public:
    int f() { return a_ + b_; }
};
```

生成:

```cpp
struct C_f_self {
    int a_;
    int b_;
};
```

`unused_`は含めない。ただし差分検証やsnapshot互換を重視する設定では全fieldを含めてもよい。

## 6. Field Plan

フィールドごとに以下を記録する。

```cpp
struct FieldPlan {
    std::string originalName;
    std::string generatedName;
    QualType type;
    bool isMutable;
    bool isRead;
    bool isWritten;
    bool isReference;
    bool isPointer;
    bool isArray;
    bool isBitField;
    SourceLocation declarationLocation;
};
```

## 7. 命名規則

基本:

```text
元フィールド名を維持する。
```

例:

```cpp
struct Account_withdraw_self {
    int balance_;
    bool locked_;
};
```

名前衝突がある場合:

```text
az_base_<BaseName>_<field>
az_field_<ordinal>_<name>
```

生成コードには対応コメントを付ける。

```cpp
int az_base_B_x; // maps to B::x
```

## 8. private/protected/public

self modelは元クラスのアクセス指定を再現しない。

理由:

- selfは元クラスではない。
- テスト用状態モデルは利用者が直接初期化できる必要がある。
- Heart modeでは元クラスのprivateへアクセスしない。

ただし、manifestには元アクセスレベルを記録してよい。

```json
{
  "field": "balance_",
  "access": "private"
}
```

## 9. mutable

`mutable`属性はself modelに保持する。

元コード:

```cpp
class C {
    mutable int cache_;
public:
    int f() const {
        cache_ += 1;
        return cache_;
    }
};
```

生成:

```cpp
struct C_f_self {
    mutable int cache_;
};

int C_f(C_f_self const& self) {
    self.cache_ += 1;
    return self.cache_;
}
```

## 10. 参照メンバ

元コード:

```cpp
class C {
    int& ref_;
public:
    int f() { return ++ref_; }
};
```

生成方針:

```cpp
struct C_f_self {
    std::reference_wrapper<int> ref_;
};

int C_f(C_f_self& self) {
    return ++self.ref_.get();
}
```

理由:

- 参照メンバはrebindできない。
- selfの代入可能性・初期化しやすさを優先する。
- `T&`をそのままfieldにするとdefault constructionが難しい。

オプション:

```text
--receiver-reference-style native|reference_wrapper|pointer
```

初期既定は`reference_wrapper`。

## 11. ポインタメンバ

ポインタメンバはそのまま保持する。

```cpp
struct C_f_self {
    Node* head_;
};
```

ただし、ポインタ先のライフタイムはテスト側責務である。生成driverではnull/defaultを避け、明示初期化を要求する。

manifest:

```json
{
  "field": "head_",
  "type": "Node*",
  "requires_user_initialization": true
}
```

## 12. 配列メンバ

元コード:

```cpp
class C {
    int xs_[4];
public:
    int f(int i) { return xs_[i]; }
};
```

生成:

```cpp
struct C_f_self {
    int xs_[4];
};
```

配列境界は元型から保持する。生成driverでは初期化例を出す。

## 13. bit-field

初期版ではbit-fieldは`heart_partial_with_modeling`に分類する。

理由:

- bit-fieldの型、幅、符号、allocation unit、packingは処理系依存の要素を含む。
- self modelで通常整数へ変換するとオーバーフローや切り詰めの意味が変わる。

後続対応案:

```cpp
struct C_f_self {
    azteca::bitfield<int, 3> flags;
};
```

または、実際のbit-field宣言をselfにも生成する。

```cpp
struct C_f_self {
    int flags : 3;
};
```

ただし、layout依存の比較はLive modeに委ねる。

## 14. base class

### 14.1 単純base

元コード:

```cpp
struct B { int b_; };
struct C : B { int c_; int f() { return b_ + c_; } };
```

生成方針:

```cpp
struct B_self {
    int b_;
};

struct C_f_self {
    B_self base_B;
    int c_;
};

int C_f(C_f_self& self) {
    return self.base_B.b_ + self.c_;
}
```

### 14.2 複数継承

```cpp
struct B1 { int x_; };
struct B2 { int x_; };
struct C : B1, B2 {
    int f() { return B1::x_ + B2::x_; }
};
```

生成:

```cpp
struct C_f_self {
    B1_self base_B1;
    B2_self base_B2;
};
```

qualified accessを保持する。

### 14.3 virtual base

初期版では、virtual baseへの実レイアウト依存はLive modeを推奨する。

Heart modeで対応する場合は、共有base identityをself modelで明示する。

```cpp
struct C_f_self {
    std::shared_ptr<VBase_self> vbase_V;
};
```

ただし、これは実layoutの再現ではなく、状態共有のモデル化である。

## 15. static member

static data memberはreceiverに含めない。

```cpp
class C {
    static int s_;
    int x_;
public:
    int f() { return s_ + x_; }
};
```

生成:

```cpp
int C_f(C_f_self& self) {
    return C::s_ + self.x_;
}
```

ただしprivate static memberに外部からアクセスできない場合、生成コードがアクセス不能になる。この場合は次を選ぶ。

1. direct access不可としてdependency化
2. accessor injection
3. Live mode
4. friend test hookを明示的に要求

初期MVPではprivate static member direct accessは`extractable_with_transcript`として扱う。

## 16. anonymous union/struct

初期版では`unsupported`または`heart_partial_with_modeling`。

理由:

- active member管理が必要。
- anonymous memberの名前解決と状態表現が複雑。

後続対応では、active tagをself modelに追加する。

```cpp
struct C_f_self {
    enum class active_union_member { none, i, d } active;
    union { int i; double d; } u;
};
```

## 17. `this` の写像

`this`そのものが式として現れる場合、単純なfield accessとは異なる。

### 17.1 `this->field`

```cpp
this->x_
```

生成:

```cpp
self.x_
```

### 17.2 `*this`

`*this`が内部比較や参照としてのみ使われる場合、selfへ写像できる場合がある。

```cpp
return this == &other;
```

ただし`other`が`C&`なら、Heart modeでは`other`もself modelへ写像しない限り扱えない。

### 17.3 `return this`

元コード:

```cpp
C* C::get() { return this; }
```

Heart mode案:

```cpp
C_get_self* C_get(C_get_self& self) {
    return &self;
}
```

この場合、戻り型は`C*`ではなく`C_get_self*`へ変わるため、公開APIの型契約は保存されない。分類は`heart_partial_with_modeling`とする。

### 17.4 `external(this)`

```cpp
external(this);
```

原則`live_required`。

明示設定で、externalがlayout非依存であることを利用者が宣言した場合のみdependency modelへ写像できる。

## 18. Receiver初期化

生成sample testでは、すべての必要フィールドを明示初期化する。

```cpp
C_f_self self{
    .x_ = 10,
    .flag_ = false,
};
```

初期化漏れを避けるため、将来はbuilderを生成する。

```cpp
auto self = C_f_self_builder{}
    .x(10)
    .flag(false)
    .build();
```

## 19. Receiver snapshot

Heart/Live差分検証では、Live objectからselfへ状態を抽出する必要がある。

方法:

1. ユーザー提供observer
2. friend test hook
3. public accessor
4. debug reflection設定

初期版ではユーザー提供observerを基本とする。

```cpp
C_f_self snapshot(C const& obj);
```

## 20. Open questions

1. selfに使用fieldのみ含めるか、全fieldを含めるか。
2. 参照メンバの既定表現を`reference_wrapper`にするかポインタにするか。
3. private static memberをdependency化する際の標準形。
4. bit-fieldを初期版で直接生成するか。

初期判断:

- 使用fieldのみを含める。
- 参照メンバは`std::reference_wrapper`。
- private static memberはdependency化。
- bit-fieldはpartial扱い。
