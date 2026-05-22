# Development Guide

この文書は Azteca の開発基盤と品質ゲートを定義します。設計判断は `docs/design/` と `docs/adr/` を優先し、この文書は実装時の運用ルールを扱います。

## Toolchain

Azteca 本体は Clang + C++23 を baseline にします。

```bash
npm ci
cmake --preset dev-clang
cmake --build --preset dev-clang
cmake --build --preset dev-clang --target check
```

`build/` 配下は生成物です。既存 artifact や `compile_commands.json` は再生成可能なものとして扱い、source of truth にしません。

## Quality Gates

ローカルと CI は同じ CMake target を使います。

```bash
cmake --build --preset dev-clang --target format-check
cmake --build --preset dev-clang --target prettier-check
cmake --build --preset dev-clang --target lint
ctest --preset dev-clang
```

`check` は format、Prettier、clang-tidy、CTest をまとめて実行します。通常の build は CI で `check` の前に実行します。

現在のbootstrap段階では、production C++ translation unit と Google Test unit test が未追加のため、clang-tidyやCTestは対象なしでスキップされることがあります。Issue 8でGoogle Test unit testを追加した時点で、`check` は実テストも実行します。

## Formatting

C++ は clang-format 18、Markdown/JSON/YAML は Prettier で整形します。

```bash
cmake --build --preset dev-clang --target format
cmake --build --preset dev-clang --target prettier-format
```

C++ style は Google を土台に、Allman braces、tab indentation、4 幅、連続行の記号揃え禁止を固定します。

## Testing

Azteca 自身の標準テスト runner は Google Test です。GoogleMock は中核依存ではありません。

新しい production module を追加する場合は、原則として対応する unit test を `tests/unit/<module>/` に追加します。Clang AST 解析、CLI 出力、診断、report を変更する場合は、fixture、golden、negative test のいずれかを追加します。

Golden の更新は明示 opt-in のみ許可します。

```bash
AZTECA_ACCEPT_GOLDEN=1 cmake --build --preset dev-clang --target check
```

通常の `check` が golden を更新してはいけません。

## Generated Reference

`docs/reference/azteca_design_all_in_one_v3.md` は設計文書から生成する派生物です。設計文書を変更したら、次のコマンドで同期します。

```bash
npm run docs:reference
npm run docs:reference:check
```

`docs/development.md` は運用文書なので、all-in-one design reference には含めません。

## Generated Code Tests

Phase B 以降、生成コードの検証は `build/test-work/` 配下で行います。生成された project は CMake configure、build、CTest まで通します。

生成物を tracked source に混ぜないでください。
