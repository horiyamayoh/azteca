# ADR-0007: mode追加ではなくSemantic Envelope拡張で対応範囲を広げる

## Status

Accepted

## Context

C++メソッドは、field accessだけでなく、this identity、virtual dispatch、dynamic type、lifetime、byte representation、external dependencyなどに依存しうる。

これらを個別modeとして公開すると、ツールは複雑になり、利用者はどのmodeを選べばよいか判断しなければならない。

## Decision

対応範囲の拡大は、公開modeの追加ではなく、Semantic Envelopeの自動拡張で行う。

```text
field access       -> field state
this identity      -> object_ref
external call      -> dependency boundary + effect
virtual call       -> dispatch table
dynamic_cast       -> type_tag
lifetime operation -> lifetime_state
byte access        -> byte_view
```

## Consequences

### Positive

- 公開UXを単純に保てる。
- this escapeなどを即unsupportedにせず、抽出継続できる。
- ユニットテストとして意味のある抽象化を生成できる。

### Negative

- 自動拡張の判断が必要になる。
- 生成レポートで抽象化を正直に説明する必要がある。
- object_refなどのruntime部品が増える。

## Notes

Semantic Envelopeは、実C++オブジェクトを偽造するものではない。あくまでユニットテストで観測可能な意味を表す安全なモデルである。
