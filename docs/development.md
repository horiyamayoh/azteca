# Development Guide

この文書は Azteca の開発基盤と品質ゲートを定義します。設計判断は `docs/design/` と `docs/adr/` を優先し、この文書は実装時の運用ルールを扱います。

## Toolchain

Azteca 本体は Clang + C++23 を baseline にします。

```bash
npm ci
cmake --preset dev-clang
cmake --build --preset dev-clang
cmake --build --preset dev-clang --target quick-check
```

`build/` 配下は生成物です。既存 artifact や `compile_commands.json` は再生成可能なものとして扱い、source of truth にしません。

## Quality Gates

開発中の反復は、ビルド済み target に依存して CTest だけを実行する `quick-check` を使います。

```bash
cmake --build --preset dev-clang --target quick-check
```

CI 相当の確認は `check` を使います。

```bash
cmake --build --preset dev-clang --target check
```

個別ゲート:

```bash
cmake --build --preset dev-clang --target format-check
cmake --build --preset dev-clang --target prettier-check
cmake --build --preset dev-clang --target lint-fast
cmake --build --preset dev-clang --target lint
ctest --preset dev-clang
```

`lint-fast` は `clang-analyzer-*` を除いたローカル向け clang-tidy profile です。`lint` は analyzer を含む full profile で、`check` は format、Prettier、reference、full clang-tidy、CTest をまとめて実行します。通常の build は CI で `check` の前に実行します。

`asan-clang` の test preset は `ASAN_OPTIONS=allow_user_poisoning=0` を設定します。Azteca 側の ASan/UBSan instrumentation は有効なまま、Clang Tooling が内部で使う LLVM allocator poison による外部ライブラリ由来の false positive を避けるためです。

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
cmake --build --preset dev-clang --target phase-a-accept-goldens
```

`phase-a-accept-goldens` は Phase A inspect golden を再生成し、JSON golden を
Prettier で整形します。通常の `check` が golden を更新してはいけません。

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
