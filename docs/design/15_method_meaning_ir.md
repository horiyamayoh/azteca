# 15. Method Meaning IR

## 目的

この文書は、Clang ASTから直接C++コードを生成するのではなく、中間表現 **Method Meaning IR**、略称 **MMIR** を導入する設計を定義する。

MMIRの目的は、アステカの変換を次のように分離することにある。

```text
Clang AST
  ↓
意味抽出・名前解決・型解決
  ↓
MMIR
  ↓
Semantic Envelope Planner
  ↓
C++ kernel codegen
```

これにより、アステカは「ASTノードごとの場当たり的なコード生成」ではなく、「メソッドの意味を一度正規化してから生成する」構成になる。

## なぜMMIRが必要か

C++のASTは、元コードの構文、名前解決、型情報、暗黙変換、テンプレート特殊化などを含む。しかし、テスト用kernel生成に必要なのは、元構文そのものではなく次の情報である。

```text
- どのreceiver状態を読むか
- どのreceiver状態を書くか
- どの依存を呼ぶか
- this identity が必要か
- dynamic type が必要か
- lifetime 操作があるか
- byte representation が必要か
- どの値が戻るか
- どの例外が出るか
- どの順序で副作用が起きるか
```

ASTから直接コード生成すると、これらの意味が各lowering ruleに分散する。MMIRを置けば、意味の正規化、Envelope計画、コード生成を分離できる。

## 基本構造

MMIRは、次の層からなる。

```text
MMIRModule
  - target method metadata
  - type table
  - symbol table
  - receiver model candidates
  - dependency candidates
  - functions

MMIRFunction
  - receiver parameter
  - arguments
  - return model
  - body block
  - effects
  - source map

MMIRBlock
  - ordered statements

MMIRStmt
  - let
  - assign
  - if
  - switch
  - loop
  - return
  - throw
  - call
  - boundary_call
  - lifetime_op
  - effect_record

MMIRExpr
  - constant
  - local_ref
  - arg_ref
  - field_ref
  - base_ref
  - object_ref
  - cell_ref
  - unary/binary op
  - call_expr
  - type_test
  - dispatch_call
  - byte_view
```

## 型モデル

MMIRの型は、C++型そのものと、アステカ抽象型の両方を持つ。

```text
CppType
  int
  C const&
  std::string
  T*

AztecaType
  object_ref<C>
  cell<int>
  ref<int>
  dispatch_table<C>
  lifetime_state
  byte_view<C>
  effect_token
```

型変換は `TypeMapper` で一元管理する。

```text
C* as this identity      -> object_ref<C>
T& as aliasable field    -> ref<T>
field with address taken -> cell<T>
virtual receiver         -> object_ref<C> + dispatch_table<C>
raw byte access          -> byte_view<C>
```

## MMIRノード仕様

### FieldRef

receiverのフィールド参照。

```text
FieldRef {
  receiver: ReceiverId,
  field_decl: CXXFieldDecl,
  access: read | write | readwrite | address
  cv: const | mutable
  source_range: SourceRange
}
```

### ObjectRef

`this` の同一性を表す。

```text
ObjectRef {
  static_type: CppClassType,
  source: this | base_this | external_object
  source_range: SourceRange
}
```

### BoundaryCall

元コード内の呼び出しを、生成kernel内で依存境界として表す。

```text
BoundaryCall {
  original_callee: SymbolId,
  lowered_name: string,
  arguments: [MMIRExpr],
  return_type: Type,
  effect_policy: record | silent | strict,
  source_range: SourceRange
}
```

BoundaryCallは失敗ではない。ユニットテスト上の依存注入点である。

### DispatchCall

virtual callの意味を表す。

```text
DispatchCall {
  virtual_method: CXXMethodDecl,
  receiver: ObjectRef,
  arguments: [MMIRExpr],
  result_type: Type,
  source_range: SourceRange
}
```

### LifetimeOp

ライフタイム操作。

```text
LifetimeOp {
  op: destroy | delete_self | placement_new | construct_base | destroy_base
  receiver: ObjectRef,
  destructor_or_constructor: optional SymbolId,
  source_range: SourceRange
}
```

### ByteView

オブジェクト表現へのアクセス。

```text
ByteView {
  receiver: ObjectRef,
  requested_size: Expr,
  access: read | write | readwrite,
  policy: representation | boundary,
  source_range: SourceRange
}
```

## MMIR生成手順

```text
1. 対象CXXMethodDeclを受け取る。
2. 宣言情報をMMIRFunction metadataへ写す。
3. body ASTを走査する。
4. CXXThisExpr、MemberExpr、CXXMemberCallExprなどを意味ノードへ変換する。
5. 暗黙thisを明示Receiver参照に正規化する。
6. 暗黙変換を必要に応じて明示MMIR Castとして保持する。
7. unresolved/dependentな箇所は未具体化情報として残す。
8. MMIR validationを行う。
9. Envelope Plannerへ渡す。
```

## Source Map

すべてのMMIRノードは元ソース位置を持つ。

```text
理由:
  - レポートに使う
  - 診断に使う
  - 生成コードのコメントに使う
  - fixtureの期待値と照合する
```

例:

```text
BoundaryCall registry.add(this)
  source: node.cpp:42:9-42:27
```

## Envelope Plannerとの関係

MMIRは、まず意味をそのまま表す。Envelope Plannerは、MMIRを見て必要なself/deps/effects/runtime部品を決める。

例:

```text
MMIR:
  ObjectRef(this)
  BoundaryCall(Registry::add, args=[ObjectRef(this)])

Planner result:
  - self needs object_id
  - deps needs registry_add(object_ref<Node>)
  - effects needs call recording
```

この分離により、同じMMIRから異なる生成方針を選べる。

```text
標準生成:
  deps + effects

strict生成:
  dependency must be explicitly supplied

trace生成:
  call order recordingを強化
```

## Codegenとの関係

Codegenは、MMIRとEnvelope Planを入力にしてC++を出す。

```text
MMIR FieldRef + field state
  -> self.x

MMIR FieldRef + addressable cell
  -> self.x.get()

MMIR ObjectRef
  -> self.object_ref()

MMIR BoundaryCall
  -> deps.name(args...); effects.record(...)

MMIR DispatchCall
  -> self.dispatch.method(self.object_ref(), args...)

MMIR LifetimeOp(delete_self)
  -> destructor kernel + lifetime.mark_destroyed() + effects.record_delete(...)
```

## Validation

MMIR生成後に、以下を検査する。

```text
VAL-MMIR-001:
  すべてのDeclRefは解決済みである。ただしdependent templateは例外として明示タグを持つ。

VAL-MMIR-002:
  CXXThisExprは、最終MMIRでは裸で残らない。ObjectRefまたはReceiverRefへ変換される。

VAL-MMIR-003:
  すべてのBoundaryCallはsource locationと元callee情報を持つ。

VAL-MMIR-004:
  すべてのFieldRefはfield_declを持つ。

VAL-MMIR-005:
  virtual callはCallではなくDispatchCallとして表現される。

VAL-MMIR-006:
  lifetime操作は通常のCallに紛れない。

VAL-MMIR-007:
  byte accessはByteViewまたはBoundaryCallとして表現される。
```

## MMIRによる単純化

MMIRは内部実装を増やすが、利用者の複雑さを減らす。

```text
MMIRなし:
  各AST nodeが直接self/deps/effectsへ変換され、規則が散る。

MMIRあり:
  AST -> 意味 -> envelope -> codegen という段階が明確になる。
```

結果として、アステカは「モードを増やして対応する」のではなく、「意味表現を豊かにして単一抽出を広げる」設計になる。

## 最小MMIRから始める

最初から全ノードを作る必要はない。

MVP:

```text
- Function
- Block
- Return
- If
- Assign
- Local
- ArgRef
- FieldRef
- Literal
- BinaryOp
- Call
- BoundaryCall
- ObjectRef
```

次段階:

```text
- AddressableCell
- DispatchCall
- TypeTest
- LifetimeOp
- ByteView
- Loop
- Throw
- Switch
- Lambda
```

## Definition of Done

```text
1. Clang ASTからMMIRを生成できる。
2. MMIR validationがあり、裸のthisや未分類callが残らない。
3. MMIRからEnvelope Planを生成できる。
4. MMIRから生成コードのsource mapを出せる。
5. lowering ruleのテストが、AST直接ではなくMMIR期待値でも検証できる。
```
