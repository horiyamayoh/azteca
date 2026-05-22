# Azteca Design Documents V3

アステカは、C++非staticメンバ関数のロジックをAST経由で取り出し、実オブジェクト構築なしにユニットテスト可能なkernelへ変換するツールである。

V3では、V2の「単一公開抽出モデル」「Semantic Envelope」「MMIR」を維持したうえで、実務上もっとも大きな障害になる依存関係問題を、**Dependency Transcript** と **Scenario API** によって解く方針を追加した。また、生成テストの標準ランナーを **Google Test** として明確化した。

## V3の中心方針

```text
利用者に見せる入口は原則として1つ。

  azteca extract -p build --method 'C::m(args...)'

生成される標準成果物はGoogle Testで実行できる。
内部では、self / scenario / when / effects を中心に、依存観測と外部効果をテスト可能な形へ自動変換する。
```

## V2からの主な変更

- 依存オブジェクトのfakeを作るのではなく、対象メソッドが依存から受け取る観測値と依存へ送る効果を **Dependency Transcript** として扱う。
- スタブは「手書きfakeクラス」ではなく、`scenario.when.xxx(...).returns(...)` と `scenario.effects.xxx.expect_once(...)` の形で記述する。
- 依存が大量にあるメソッドでも、テスト経路ごとに必要な観測だけを書けばよい設計にする。
- Google Testを標準テストランナーにする。
- GoogleMockは中核には置かない。必要に応じて連携可能だが、アステカの本流はScenario APIである。
- `azteca inspect` は、抽出可能性だけでなく、必要なscenario入力、生成shape、observable effects、経路ごとのstub burdenを表示する。

## フォルダ構成

```text
docs/
  design/      V3の設計仕様
  planning/    実装計画とロードマップ
  adr/         採択済みArchitecture Decision Records
  review/      総点検・自己検証
  reference/   結合済み通読版
```

## 主要文書

- `development.md`: 開発基盤、ローカルコマンド、品質ゲート、テスト追加ルール。

- `design/00_project_charter.md`  
  プロジェクト憲章。

- `design/01_semantic_contract.md`  
  fake this禁止、AST/Semaベース、意味保存範囲、安全契約。

- `design/06_dependency_model.md`  
  依存処理のV3版。recursive extraction / boundary port / transcript / shape / object_ref を統合。

- `design/10_test_strategy.md`  
  アステカ自身のテスト戦略と、生成Google Testの検証戦略。

- `design/20_runtime_contract.md`  
  scenario、query、effect、operation、object_ref、shape、missing observation診断を含むランタイム契約。

- `design/22_dependency_transcript_and_stubbing.md`  
  大量依存問題を解く中心文書。

- `design/23_gtest_integration.md`  
  Google Testを標準ランナーにする設計。

- `review/24_total_review_and_self_verification.md`  
  ユーザー要求に対する総点検、齟齬検査、実装開始可否判断。

## 実装計画

- `planning/12_implementation_plan.md`
- `planning/18_implementation_roadmap.md`

## ADR追加

- `adr/0010_gtest_as_default_runner.md`
- `adr/0011_dependency_transcript_over_handwritten_fakes.md`

## 通読用

`reference/azteca_design_all_in_one_v3.md` に全設計書を結合している。
