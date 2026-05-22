# ADR-0002: テキスト置換ではなくClang ASTを使う

## Status

Accepted

## Context

メソッド本体から`this->x`を`self.x`へ置換するだけなら、文字列処理でも一見可能に見える。

しかしC++では、次の要素により文字列置換は堅牢ではない。

- implicit `this`
- overload resolution
- operator overload
- template specialization
- macro expansion
- ADL
- base class member lookup
- cv/ref qualifier
- access control
- type aliases

## Decision

AztecaはClang AST/Sema後の情報に基づいて変換する。

具体的には、`CXXThisExpr`、`MemberExpr`、`CXXMemberCallExpr`、`DeclRefExpr`、`FieldDecl`、`CXXMethodDecl`等を使い、宣言IDと型情報に基づいてloweringする。

## Consequences

良い点:

- implicit accessを扱える。
- overload済みcalleeを正確に扱える。
- template specialization単位で扱える。
- 危険構文検出が正確になる。

悪い点:

- 実装が重くなる。
- Clang dependencyが必須になる。
- AST pretty printing/codegen方針が必要になる。

## Alternatives considered

### Regex replacement

却下。C++構文と意味情報を扱えない。

### Source-to-source Rewriter only

一部利用は可能だが、Aztecaは元ソースを書き換えず生成物を作るため、AST lowering + codegenを中核にする。

## References

- `../design/02_architecture.md`
- `../design/05_lowering_rules.md`
