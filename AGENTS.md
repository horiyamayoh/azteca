# AGENTS.md

このリポジトリで作業する人間とエージェント向けの開発入口です。詳細設計は `docs/` を source of truth とし、このファイルは日々の作業規約をまとめます。

## Project Intent

Azteca は C++ 非 static メンバ関数のロジックを Clang AST から抽出し、実オブジェクト構築なしに Google Test で検証できる kernel/scenario へ変換するツールです。

最初の実装対象は Phase A の `azteca inspect` です。完全な codegen ではなく、Extraction Plan と Google Test preview を出すことを優先します。

## Read First

- `docs/README.md`: 設計文書の入口
- `docs/design/00_project_charter.md`: プロジェクト憲章
- `docs/design/02_architecture.md`: モジュール構成
- `docs/design/10_test_strategy.md`: テスト戦略
- `docs/design/23_gtest_integration.md`: Google Test 統合方針
- `docs/planning/12_implementation_plan.md`: 実装計画

## Required Toolchain

- Clang/Clang++ 18
- clang-format 18
- clang-tidy 18
- CMake 3.24 以上
- Ninja
- Node.js と npm

Azteca 本体は Clang + C++23 専用です。GCC/MSVC 対応は初期スコープ外です。

## Common Commands

```bash
npm ci
cmake --preset dev-clang
cmake --build --preset dev-clang
cmake --build --preset dev-clang --target check
```

個別ゲート:

```bash
cmake --build --preset dev-clang --target format-check
cmake --build --preset dev-clang --target prettier-check
cmake --build --preset dev-clang --target lint
ctest --preset dev-clang
```

整形:

```bash
cmake --build --preset dev-clang --target format
cmake --build --preset dev-clang --target prettier-format
```

## Coding Rules

- C++ は `.clang-format` と `.clang-tidy` に従います。
- Allman braces、インデントはタブ文字、幅は 4。
- 記号・代入・コメントの前後行揃えはしません。
- 型名は `CamelCase`、関数・変数は `lower_case`。
- private/protected member は末尾 `_`。
- 定数と enum constant は `kCamelCase`。
- 生成 codegen も原則として同じ style に寄せます。

## Test Rules

- 標準テスト runner は Google Test です。
- GoogleMock は中核依存にしません。必要な場合だけ局所的に使います。
- production module を追加するときは、原則として `tests/unit/<module>/` の unit test を同時に追加します。
- Clang AST、CLI 出力、診断、report を変更する場合は、fixture、golden、negative test のいずれかを追加します。
- golden 更新は `AZTECA_ACCEPT_GOLDEN=1` のような明示 opt-in に限定します。通常の `check` で golden を自動更新してはいけません。
- Phase B 以降の生成コード検証は `build/test-work/` 配下で行い、生成先を repo 直下や tracked source に混ぜません。

## Do Not

- fake `this`、未構築 object 呼び出し、pointer-to-member の偽変換を導入しない。
- private/protected を `#define private public` で突破する実装を中核にしない。
- 文字列置換で C++ 構文を変換しない。Clang AST/Sema 後情報を使う。
- `build/` 配下の artifact を source of truth として扱わない。
- 無関係な refactor や既存設計文書の大規模整形を、機能変更 PR に混ぜない。

## CI Contract

CI は GitHub Actions で次を必須にします。

- clang-format dry-run
- Prettier check
- clang-tidy warnings-as-errors
- Clang C++23 build warnings-as-errors
- CTest
- ASan/UBSan build and test
