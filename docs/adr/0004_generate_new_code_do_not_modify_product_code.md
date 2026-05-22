# ADR-0004: 製品コードを書き換えず、別ディレクトリへ生成する

## Status

Accepted

## Context

Aztecaは元メソッドをテスト可能な形へ変換する。元ソースへtest hookやfriend、access変更を挿入する案も考えられる。

しかし、元製品コードを書き換えると、レビュー負担、ODR、ビルド差分、CI汚染、生成ミスによる破壊が起きる。

## Decision

Aztecaは既定で元製品コードを変更しない。

生成物は`azteca-out/`へ出力する。

## Consequences

良い点:

- 元製品コードを汚染しない。
- 生成物をartifactとして扱える。
- regeneration/diffが簡単。

悪い点:

- private staticやprivate nested typeへのアクセスが難しい。
- Live observer/factoryにはユーザーhookが必要な場合がある。

## Future

明示オプションでtest hook patchを生成する可能性はある。ただし、その場合は別ADRと明示的な利用者承認が必要。

## References

- `../design/08_codegen_spec.md`
