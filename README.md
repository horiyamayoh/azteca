# Azteca

Aztecaは、C++非staticメンバ関数向けのテストハーネス生成器です。
Clang ASTからメソッドロジックを取り出し、明示receiverを持つkernelへloweringすることで、元の巨大なオブジェクトグラフを構築せずにユニットテストできる形へ変換します。

## 現在の実装対象

最初の実装対象はPhase Aのinspect MVPです。

```bash
azteca inspect -p build --method 'C::m(args...)'
```

最初の成果物は完全なcodegenではなく、Extraction Planの表示です。
[docs/planning/12_implementation_plan.md](docs/planning/12_implementation_plan.md) と
[docs/planning/18_implementation_roadmap.md](docs/planning/18_implementation_roadmap.md) を参照してください。

## リポジトリ構成

```text
include/azteca/      公開C++ヘッダ
src/                 実装モジュール
tests/               unit / fixture / golden / integration / negative tests
docs/                設計書、ADR、実装計画、レビュー
```

## ドキュメント

- [docs/README.md](docs/README.md): 設計書の入口
- [docs/design/00_project_charter.md](docs/design/00_project_charter.md): プロジェクト憲章
- [docs/design/02_architecture.md](docs/design/02_architecture.md): モジュール構成
- [docs/adr](docs/adr): 採択済みArchitecture Decision Records

## Bootstrap

```bash
npm ci
cmake --preset dev-clang
cmake --build --preset dev-clang
cmake --build --preset dev-clang --target check
```

Azteca本体はClang 18 + C++23をbaselineにします。`build/` 配下は生成物であり、既存artifactや `compile_commands.json` はsource of truthではありません。

`check` は日常用の品質ゲートで、format、docs/reference 同期、CTest を実行します。clang-tidy を含む CI 相当の full gate は `cmake --build --preset dev-clang --target strict-check` です。

開発基盤と品質ゲートは [AGENTS.md](AGENTS.md) と [docs/development.md](docs/development.md) を参照してください。
