# 05. Lowering Rules

## 1. 目的

この文書は、Azteca Heart modeにおけるAST lowering ruleを定義する。各ルールは、元メソッド本体の意味構造を、生成kernelのC++コードへ変換するための仕様である。

この文書は実装者が最も頻繁に参照する台帳である。新しい構文をサポートする場合は、対応するLowering Ruleを追加し、fixtureとテストを追加する。

## 2. ルール記述形式

各ルールは以下の形式で記述する。

```text
ID: LR-xxx
Name: ルール名
Before: 元コード例
After: 生成コード例
AST nodes: 主な対象Clang ASTノード
Conditions: 適用条件
Reject: 適用禁止条件
Dependencies: 生成または要求する依存
Tests: 必須fixture
```

## 3. 分類語彙

| 用語 | 意味 |
|---|---|
| accept | Heart modeで変換可能 |
| dependency | 依存注入または再帰抽出が必要 |
| model | 明示モデル追加で変換可能 |
| live | Live modeが必要 |
| unsupported | 現在未対応 |

## LR-001: implicit data member read

### Before

```cpp
int C::f() { return x_; }
```

### After

```cpp
int C_f(C_f_self& self) { return self.x_; }
```

### AST nodes

- `MemberExpr`
- implicit `CXXThisExpr`
- `FieldDecl`

### Conditions

- 対象が非static data memberである。
- `MemberExpr`のbaseがimplicit `this`である。
- field型がself modelで表現可能である。

### Reject

- fieldがbit-fieldで初期版未対応の場合。
- fieldがanonymous union memberでactive member不明の場合。

### Tests

- `simple_field_read.cpp`
- `private_field_read.cpp`
- `const_field_read.cpp`

## LR-002: explicit this data member read

### Before

```cpp
int C::f() { return this->x_; }
```

### After

```cpp
int C_f(C_f_self& self) { return self.x_; }
```

### AST nodes

- `CXXThisExpr`
- `MemberExpr`
- `FieldDecl`

### Conditions

- `this->x_`のmemberが非static data memberである。

### Reject

- `this`がfield access以外の形で外部へ流れる場合はLR-020へ。

### Tests

- `explicit_this_field_read.cpp`

## LR-003: data member write

### Before

```cpp
void C::f(int v) { x_ = v; }
```

### After

```cpp
void C_f(C_f_self& self, int v) { self.x_ = v; }
```

### AST nodes

- `BinaryOperator` assignment
- `CompoundAssignOperator`
- `UnaryOperator` increment/decrement
- `MemberExpr`

### Conditions

- LHSが非static data memberである。
- receiverが書き込み可能である。
- `const`メソッドの場合はfieldが`mutable`である。

### Reject

- non-mutable fieldを`const self`へ書く形になる場合。
- bit-field切り詰め意味を保持できない場合。

### Tests

- `field_assignment.cpp`
- `field_compound_assignment.cpp`
- `mutable_field_write_in_const_method.cpp`

## LR-004: simple return

### Before

```cpp
int C::f() { return x_ + 1; }
```

### After

```cpp
int C_f(C_f_self& self) { return self.x_ + 1; }
```

### AST nodes

- `ReturnStmt`

### Conditions

- return expressionがlowering可能である。
- 戻り型が生成kernelの戻り型として表現可能である。

### Reject

- `return this;`はLR-021へ。
- `return *this;`はLR-022へ。
- 戻り型が`C&`/`C*`でself写像が必要な場合。

### Tests

- `simple_return.cpp`
- `void_return.cpp`

## LR-005: if statement

### Before

```cpp
int C::f() {
    if (locked_) return -1;
    return value_;
}
```

### After

```cpp
int C_f(C_f_self& self) {
    if (self.locked_) return -1;
    return self.value_;
}
```

### AST nodes

- `IfStmt`

### Conditions

- condition、then、elseがlowering可能である。
- init statementがある場合もlowering可能である。

### Reject

- condition内でraw this escapeがある場合。
- structured binding conditionが未対応の場合。

### Tests

- `if_statement.cpp`
- `if_else_statement.cpp`
- `if_with_initializer.cpp`

## LR-006: arithmetic/comparison/logical expression

### Before

```cpp
return (x_ + y_) * 2 > limit_ && enabled_;
```

### After

```cpp
return (self.x_ + self.y_) * 2 > self.limit_ && self.enabled_;
```

### AST nodes

- `BinaryOperator`
- `UnaryOperator`
- `ParenExpr`
- `ImplicitCastExpr`

### Conditions

- operandがlowering可能である。
- operatorがbuilt-inまたは解決済みoverloaded operatorとして扱える。

### Reject

- overloaded operatorが非staticメンバ関数でdependency化不能の場合。

### Tests

- `arithmetic_expression.cpp`
- `comparison_expression.cpp`
- `logical_expression.cpp`

## LR-007: same-class nonvirtual member call

### Before

```cpp
int C::f(int x) { return fee(x) + 1; }
```

### After: recursive extraction

```cpp
int C_f(C_f_self& self, int x) {
    return C_fee(self, x) + 1;
}
```

### After: dependency injection

```cpp
int C_f(C_f_self& self, C_f_deps& deps, int x) {
    return deps.fee(self, x) + 1;
}
```

### AST nodes

- `CXXMemberCallExpr`
- `MemberExpr`
- `CXXMethodDecl`

### Conditions

- calleeが同一クラスの非static member functionである。
- virtual dispatchが不要、または明示的に非virtual呼び出しである。

### Reject

- calleeが`delete this`等を含みLive-requiredの場合、依存として扱うか全体をLive-requiredへ引き上げる。

### Tests

- `same_class_helper_recursive.cpp`
- `same_class_helper_stub.cpp`

## LR-008: static member function call

### Before

```cpp
int C::f(int x) { return normalize(x); }
```

ここで`normalize`が`static int C::normalize(int)`の場合。

### After

```cpp
int C_f(C_f_self& self, int x) {
    return C::normalize(x);
}
```

またはdependency化:

```cpp
return deps.normalize(x);
```

### AST nodes

- `CallExpr`
- `CXXMemberCallExpr`
- `DeclRefExpr`
- `FunctionDecl`

### Conditions

- static member functionがアクセス可能である。

### Reject

- private static member functionで生成コードからアクセス不能な場合はdependency化。

### Tests

- `static_member_call.cpp`
- `private_static_member_dependency.cpp`

## LR-009: free function call

### Before

```cpp
int C::f(int x) { return normalize(x); }
```

### After default

```cpp
int C_f(C_f_self& self, int x) {
    return normalize(x);
}
```

### After injected

```cpp
return deps.normalize(x);
```

### AST nodes

- `CallExpr`
- `DeclRefExpr`
- `FunctionDecl`

### Conditions

- calleeがfree functionである。
- direct callが生成コードのinclude/accessで可能である。

### Reject

- calleeが`this`を必要とするwrapperである場合。
- ADLで解決され、生成側で同じlookupが保証できない場合はfully qualified化またはdependency化。

### Tests

- `free_function_direct.cpp`
- `free_function_dependency.cpp`

## LR-010: global variable read/write

### Before

```cpp
int C::f() { return global_limit + x_; }
```

### After default

```cpp
return global_limit + self.x_;
```

### Conditions

- global symbolが生成コードから参照可能である。

### Warnings

- 再現性が低くなる可能性がある。
- 並列テストで干渉する可能性がある。

### Alternative

```cpp
return deps.global_limit.get() + self.x_;
```

### Tests

- `global_read.cpp`
- `global_write_warning.cpp`

## LR-011: base class member access

### Before

```cpp
int C::f() { return b_ + c_; }
```

`b_`はbase `B`のfield。

### After

```cpp
return self.base_B.b_ + self.c_;
```

### AST nodes

- `MemberExpr`
- `CXXBaseSpecifier`
- `FieldDecl`

### Conditions

- base subobjectがreceiver modelで表現可能である。

### Reject

- virtual base identityが重要な場合はmodelまたはLive。

### Tests

- `single_base_field.cpp`
- `multiple_base_field_qualified.cpp`

## LR-012: virtual call

### Before

```cpp
int C::f(int x) { return compute(x); }
```

`compute`がvirtualの場合。

### After

```cpp
return deps.vtable.compute(self, x);
```

### AST nodes

- `CXXMemberCallExpr`
- `CXXMethodDecl::isVirtual`

### Conditions

- virtual dispatchを明示dependencyとしてモデル化する。

### Reject

- 実RTTIや実派生オブジェクト状態が必要な場合はLive mode。

### Tests

- `virtual_call_dispatch_table.cpp`

## LR-013: overloaded operator call

### Before

```cpp
return value_ + other;
```

`operator+`がoverloadされている場合。

### After

解決済みcalleeに応じる。

- free operatorならdirect/dependency call
- member operatorならsame-class dependency
- built-inなら通常演算

### AST nodes

- `CXXOperatorCallExpr`
- `FunctionDecl`
- `CXXMethodDecl`

### Conditions

- Clang Semaでcalleeが解決済みである。

### Reject

- dependent operatorで未解決の場合。

### Tests

- `overloaded_free_operator.cpp`
- `overloaded_member_operator.cpp`

## LR-014: local variable declaration

### Before

```cpp
int C::f() {
    int y = x_ + 1;
    return y;
}
```

### After

```cpp
int C_f(C_f_self& self) {
    int y = self.x_ + 1;
    return y;
}
```

### AST nodes

- `DeclStmt`
- `VarDecl`

### Conditions

- initializerがlowering可能である。
- local typeが生成コードで参照可能である。

### Reject

- local class/lambda等が未対応の場合。

### Tests

- `local_variable.cpp`

## LR-015: loops

### Before

```cpp
for (int i = 0; i < n_; ++i) sum += xs_[i];
```

### After

```cpp
for (int i = 0; i < self.n_; ++i) sum += self.xs_[i];
```

### AST nodes

- `ForStmt`
- `WhileStmt`
- `DoStmt`

### Conditions

- init/condition/increment/bodyがlowering可能である。

### Tests

- `for_loop.cpp`
- `while_loop.cpp`

## LR-016: range-for

### Before

```cpp
for (auto& x : xs_) sum += x;
```

### After

```cpp
for (auto& x : self.xs_) sum += x;
```

### AST nodes

- `CXXForRangeStmt`

### Conditions

- range expressionがlowering可能である。

### Reject

- begin/end lookupが生成側で変わる可能性がある場合は注意診断。

### Tests

- `range_for_array.cpp`
- `range_for_vector_field.cpp`

## LR-017: lambda without this capture

### Before

```cpp
auto twice = [](int x) { return x * 2; };
return twice(n_);
```

### After

```cpp
auto twice = [](int x) { return x * 2; };
return twice(self.n_);
```

### Conditions

- lambda bodyが`this`をcaptureしない。

### Tests

- `lambda_no_this.cpp`

## LR-018: lambda with this capture

### Before

```cpp
auto f = [this](int x) { return x + value_; };
return f(1);
```

### After

```cpp
auto f = [&self](int x) { return x + self.value_; };
return f(1);
```

### AST nodes

- `LambdaExpr`
- `CXXThisExpr`

### Conditions

- lambdaが同期的に使われ、self lifetimeを超えて保存されない。
- captureを`&self`または`self`へ安全に写像できる。

### Reject

- lambdaが外部へ返される。
- lambdaが非同期実行される。
- lambdaが`C*`として`this`を保存する。

### Tests

- `lambda_this_capture_immediate.cpp`
- `lambda_this_capture_escape_live.cpp`

## LR-019: noexcept propagation

### Before

```cpp
int C::f() noexcept { return x_; }
```

### After

```cpp
int C_f(C_f_self& self) noexcept { return self.x_; }
```

### Conditions

- 依存呼び出しも`noexcept`互換である。

### Reject/Warning

- dependency injectionにより例外仕様が不明な場合、`noexcept`を外すか、required noexcept function wrapperを使う。

### Tests

- `noexcept_simple.cpp`
- `noexcept_dependency.cpp`

## LR-020: raw this escape

### Before

```cpp
registry.add(this);
```

### Classification

```text
live_required
```

### AST nodes

- `CXXThisExpr`
- `CallExpr`
- `ImplicitCastExpr`

### Reason

`this`が`C*`として外部へ渡ると、外部関数が実レイアウト、RTTI、アドレス同一性、vptr、lifetimeに依存する可能性がある。

### Fallbacks

1. dependency injection
2. self identity model
3. Live mode

### Tests

- `raw_this_escape_external_call.cpp`

## LR-021: return this

### Before

```cpp
C* C::f() { return this; }
```

### Heart partial model

```cpp
C_f_self* C_f(C_f_self& self) { return &self; }
```

### Classification

```text
heart_partial_with_modeling
```

### Reject

- 戻り型`C*`を保存する必要があるAPI契約をHeartで保持しようとする場合。

### Fallback

- Live mode

### Tests

- `return_this_partial.cpp`

## LR-022: return *this

### Before

```cpp
C& C::f() { return *this; }
```

### Heart partial model

```cpp
C_f_self& C_f(C_f_self& self) { return self; }
```

### Classification

```text
heart_partial_with_modeling
```

### Tests

- `return_deref_this_partial.cpp`

## LR-023: dynamic_cast involving this

### Before

```cpp
return dynamic_cast<D*>(this) != nullptr;
```

### Classification

```text
live_required
```

### Reason

RTTIと実オブジェクトの動的型が必要。

### Fallback

- explicit runtime type model
- Live mode

### Tests

- `dynamic_cast_this_live.cpp`

## LR-024: typeid(*this)

### Before

```cpp
return typeid(*this) == typeid(D);
```

### Classification

```text
live_required
```

polymorphic型では実動的型が必要。非polymorphicで静的型だけならモデル化可能だが、初期版ではLive-required寄りに分類する。

### Tests

- `typeid_this_live.cpp`

## LR-025: reinterpret_cast involving this

### Before

```cpp
auto p = reinterpret_cast<unsigned char*>(this);
```

### Classification

```text
live_required
```

### Reason

実object representationとlayout依存。

### Tests

- `reinterpret_this_live.cpp`

## LR-026: delete this

### Before

```cpp
delete this;
```

### Classification

```text
live_required
```

### Reason

実オブジェクトのlifetimeとstorage ownershipを操作する。

### Tests

- `delete_this_live.cpp`

## LR-027: explicit destructor call on this

### Before

```cpp
this->~C();
```

### Classification

```text
live_required
```

### Reason

object lifetimeを終了する。self modelで通常field破棄に落とすと意味が変わる。

### Tests

- `destructor_call_this_live.cpp`

## LR-028: placement new into this

### Before

```cpp
new (this) C(args...);
```

### Classification

```text
live_required
```

### Reason

同一storage上でlifetimeを再開始する。Heart kernelで安全に再現するには専用lifetime modelが必要。

### Tests

- `placement_new_this_live.cpp`

## LR-029: member address taking

### Before

```cpp
return &x_;
```

### After

```cpp
return &self.x_;
```

### Classification

```text
extractable
```

ただし戻り型が`int*`であり、field型が同じなら可能。

### Reject

- pointerが`C`オブジェクト内offsetとして外部で使われる場合。
- `uintptr_t`へ変換される場合はlayout依存としてLive寄り。

### Tests

- `return_field_pointer.cpp`

## LR-030: taking address of member function

### Before

```cpp
auto p = &C::helper;
```

### Classification

```text
unsupported初期版
```

### Fallback

- dependency化
- Live mode

### Tests

- `address_of_member_function_unsupported.cpp`

## LR-031: constructor body

### Scope

初期版では通常メソッド対象外。

将来方針:

- member initializerをself field初期化へlowering
- constructor bodyをinit kernelへlowering

```cpp
C::C(int x) : x_(x) { normalize(); }
```

生成案:

```cpp
C_ctor_self C_ctor(int x, deps& d) {
    C_ctor_self self{.x_ = x};
    C_normalize(self, d);
    return self;
}
```

## LR-032: destructor body

### Scope

初期版では通常メソッド対象外。

将来方針:

- resource release semanticsはLive mode推奨
- pure state cleanupはHeart destructor kernel化可能

## LR-033: template method specialization

### Before

```cpp
template<class T>
T C::f(T x) { return x + value_; }
```

### Rule

テンプレート宣言そのものではなく、具体化されたspecializationを対象にする。

```bash
azteca extract --method 'C::f<int>(int)'
```

### Classification

```text
extractable if instantiated and body resolved
unsupported if dependent unresolved
```

### Tests

- `template_method_int_specialization.cpp`

## LR-034: macro-expanded expressions

### Rule

macro展開領域にある式は、AST上で意味が解決できても、source textの再生成が難しい場合がある。

方針:

- ASTから生成可能な単純式は許可。
- source spellingに依存する複雑macroはunsupported。
- manifestにmacro locationを記録する。

### Tests

- `macro_simple_field.cpp`
- `macro_complex_unsupported.cpp`

## LR-035: throw expression

### Before

```cpp
if (x_ < 0) throw std::runtime_error("bad");
```

### After

```cpp
if (self.x_ < 0) throw std::runtime_error("bad");
```

### AST nodes

- `CXXThrowExpr`

### Conditions

- thrown expressionがlowering可能である。

### Tests

- `throw_expression.cpp`

## LR-036: try/catch

### Before

```cpp
try { return helper(); }
catch (...) { return -1; }
```

### After

```cpp
try { return deps.helper(self); }
catch (...) { return -1; }
```

### AST nodes

- `CXXTryStmt`
- `CXXCatchStmt`

### Conditions

- try body/catch bodyがlowering可能である。

### Tests

- `try_catch.cpp`

## LR-037: structured binding

### Before

```cpp
auto [a, b] = pair_;
return a + b;
```

### Classification

初期版では`unsupported`または限定対応。

### Future

- `DecompositionDecl`のlowering
- field expressionのself化

## LR-038: coroutine

### Classification

初期版では`unsupported`。

理由:

- coroutine frameと`this` capture/lifetimeが絡む。
- メソッド単体ロジックの抽出に専用設計が必要。

## LR-039: unevaluated contexts

### Before

```cpp
return sizeof(x_);
```

### After

```cpp
return sizeof(self.x_);
```

または型だけが必要なら元型から生成。

### Notes

`decltype(x_)`、`sizeof`、`noexcept(expr)`などは評価されないが、名前解決と型に影響する。lowererは評価式と同じ扱いで副作用を作ってはならない。

### Tests

- `sizeof_member.cpp`
- `decltype_member.cpp`

## LR-040: default member initializer dependency

通常メソッド抽出では、default member initializerは直接関係しない。

ただしreceiver builderを生成する場合、default値として使うかどうかを設定可能にする。

初期版ではself fieldは明示初期化を要求する。

## 4. ルール追加手順

新しいlowering ruleを追加する場合:

1. この文書にルールを追加する。
2. classifier findingを追加する。
3. lowerer実装を追加する。
4. fixtureを追加する。
5. inspect JSON期待値を追加する。
6. generated code goldenを追加する。
7. compile testを追加する。
8. 必要ならADRを追加する。

## 5. 優先実装順

MVPで実装するルール:

```text
LR-001 implicit data member read
LR-002 explicit this data member read
LR-003 data member write
LR-004 simple return
LR-005 if statement
LR-006 arithmetic/comparison/logical expression
LR-007 same-class nonvirtual member call
LR-009 free function call
LR-014 local variable declaration
LR-019 noexcept propagation
LR-020 raw this escape classification
```

Phase 2:

```text
LR-008 static member call
LR-010 global variable
LR-011 base class member access
LR-015 loops
LR-016 range-for
LR-035 throw
LR-036 try/catch
```

Phase 3:

```text
LR-012 virtual call
LR-013 overloaded operator
LR-018 lambda with this capture
LR-021 return this
LR-022 return *this
LR-033 template specialization
```

Phase 4:

```text
constructor/destructor
bit-field
anonymous union
coroutine
advanced template/dependent constructs
```
