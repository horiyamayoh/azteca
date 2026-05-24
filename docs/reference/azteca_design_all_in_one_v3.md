# Azteca Design Documents V3 - All in One

This file is generated from `docs/README.md`, `docs/design/`, `docs/planning/`,
`docs/review/`, and `docs/adr/`.

`docs/development.md` is intentionally excluded because it is operational developer
guidance, not design source of truth.

Run `npm run docs:reference` after editing design documents.

---

# File: docs/README.md

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

---

# File: docs/design/00_project_charter.md

# 00. Project Charter

## 1. 目的

Azteca（アステカ）は、C++の非staticメンバ関数を、可能な限り実クラスのインスタンス化なしに単体試験できるようにするテストハーネス生成器である。

C++の通常の呼び出し規則では、非staticメンバ関数は対象オブジェクトを要求する。これは言語仕様として正しいが、ユニットテストの観点では、コンストラクタ、外部資源、巨大な依存グラフ、private状態、継承構造などが邪魔になり、メソッド内部のロジックだけを検証したい場面で過剰な負担になる。

Aztecaの目的は、非staticメンバ関数を「本物の未構築オブジェクトに対して無理に呼ぶ」のではなく、Clang ASTからメソッド本体を抽出し、明示レシーバ関数へ変換することで、メソッド本体のロジックを安定して試験可能にすることである。

## 2. 名前の由来

Aztecaは、クラスからメソッドだけを心臓のように取り出す、という比喩から命名する。

- クラス: 生体
- メソッド: 心臓
- AST lowering: 摘出手術
- self model: 人工循環器
- test driver: 動的解析装置

この比喩は設計の方向性を示すが、実装は比喩に頼らない。すべての変換はAST/Sema後の意味情報と明文化されたlowering ruleに従う。

## 3. 中核思想

Aztecaの中核思想は次である。

```text
非staticメンバ関数を fake this で呼ばない。
メソッド本体をASTから取り出し、明示receiver関数へloweringする。
```

変換前:

```cpp
class C {
    int x_;
public:
    int f(int a) {
        x_ += a;
        return x_;
    }
};
```

変換後:

```cpp
struct C_f_self {
    int x_;
};

int C_f(C_f_self& self, int a) {
    self.x_ += a;
    return self.x_;
}
```

この生成関数は、元の実バイナリ関数そのものではない。元メソッドのASTから、観測可能な制御フロー、状態更新、戻り値計算を移植したテスト用カーネルである。

## 4. 解決したい問題

Aztecaが主に解決する問題は次である。

1. コンストラクタが重すぎて、メソッド単体の試験が難しい。
2. private/protected状態により、任意の内部状態からメソッドを開始できない。
3. 対象メソッドが小さなロジックであっても、巨大な依存グラフ全体を構築させられる。
4. 異常系や境界値を作るために、通常APIでは到達困難な状態を作る必要がある。
5. fuzzerやsanitizerをメソッド単体に集中させたい。

## 5. 非目的

Aztecaは、次を目的にしない。

1. C++標準のオブジェクトモデルを破ること。
2. 未構築ストレージを `C*` として扱い、非staticメンバ関数を呼ぶこと。
3. pointer-to-memberを通常の関数ポインタへ偽変換すること。
4. あらゆるC++構文を初版から完全変換すること。
5. 元製品バイナリの関数そのものを、オブジェクトなしで実行すること。
6. private/protectedを `#define private public` で突破することを中核機能にすること。

ただし、Live modeでは正規に構築された実オブジェクトに対して元メソッドを呼び、Heart modeとの差分検証や実ABI込みのテストを行う。

## 6. モード概要

### 6.1 Heart mode

Heart modeは、Aztecaの本命モードである。

- 元クラスの実インスタンスを作らない。
- `this` を明示的な `self` 引数へ置き換える。
- private/protected状態はself modelとして表現する。
- 依存メソッドは再帰抽出またはstub化する。
- fuzzer、sanitizer、coverageを生成カーネルに適用する。

### 6.2 Live mode

Live modeは、実オブジェクトが本質的に必要な場合の合法モードである。

- 実クラス `C` の正規インスタンスを作る。
- pointer-to-memberを標準的な構文で呼ぶ。
- RTTI、virtual dispatch、実レイアウト、実ABIを含めて検査する。
- Heart modeとの差分検証にも使う。

Live modeは逃げ道ではなく、別の検査対象を持つモードである。

## 7. 利用者像

Aztecaの主要利用者は次である。

- C++製品コードのユニットテストを書きたい開発者
- レガシーコードの内部ロジックを段階的にテストしたい保守担当者
- fuzzing対象を小さく切り出したい検証担当者
- sanitizer/coverageを特定メソッドへ集中適用したい品質担当者
- C++の言語仕様上のUBを避けながら、強いテストハーネスを求める開発チーム

## 8. 成功基準

初期版の成功基準は次である。

1. `compile_commands.json` から対象translation unitを解析できる。
2. `CXXMethodDecl`として対象メソッドを解決できる。
3. 単純なフィールドread/write、if、return、算術式をHeart modeで抽出できる。
4. 同一クラスhelperメソッドを依存として検出できる。
5. `this` escapeなどLive-required条件を正しく分類できる。
6. 生成コードが通常のC++としてコンパイルできる。
7. 最小driverを生成し、sanitizerつきで実行できる。
8. 変換不能な対象を危険な代替で処理せず、明確な診断を出せる。

## 9. 用語

| 用語             | 意味                                          |
| ---------------- | --------------------------------------------- |
| target method    | 抽出対象の非staticメンバ関数                  |
| Heart mode       | ASTから明示レシーバ関数を生成するモード       |
| Live mode        | 正規構築オブジェクトで元メソッドを呼ぶモード  |
| receiver         | 元の暗黙 `this` を置き換える明示引数          |
| self model       | receiverが参照するテスト用状態構造体          |
| dependency model | 他メソッド・外部関数・global等の依存表現      |
| lowering         | AST上の意味構造を生成コードへ変換すること     |
| kernel           | 生成されたテスト用関数本体                    |
| driver           | kernelまたはLive callを実行するテストハーネス |
| manifest         | 抽出結果、分類、生成物を記録するJSON          |
| classification   | Heart可能、Live必須、未対応などの分類         |
| raw this escape  | `this` が外部へ `C*` 等として流出すること     |
| fallback         | Heart modeで扱えない場合の代替策              |

## 10. 初期MVPスコープ

初期MVPでは、以下に集中する。

- 単一translation unit内の通常クラス
- 非templateの通常メンバ関数
- `const` / 非`const` メソッド
- フィールドread/write
- `if` / `return` / 代入 / 算術 / 比較 / 論理演算
- 同一クラスの非virtual helper呼び出しの依存検出
- raw `this` escape検出
- codegen manifest
- smoke test driver生成

初期MVPでは、次は分類だけ行い、完全変換は後続フェーズに送る。

- 継承
- virtual call
- template method
- lambda this capture
- constructor/destructor body
- coroutine
- module
- macro展開領域の複雑な書き換え

## 11. 設計原則

1. **安全性優先**: 迷ったらHeart変換を止め、理由を出す。
2. **意味ベース**: 文字列置換ではなくAST/Sema後の宣言IDに基づく。
3. **分類可能性**: 失敗を単なる失敗にせず、分類とfallbackを提示する。
4. **小さな生成物**: 生成コードは通常のC++として読みやすくする。
5. **差分検証**: 可能ならHeartとLiveの観測結果を比較する。
6. **ルール駆動**: loweringはルール台帳とfixtureで管理する。
7. **過剰約束禁止**: 元製品バイナリそのものをオブジェクトなしで動かせるとは言わない。

## 12. 参照資料

- Clang LibTooling: https://clang.llvm.org/docs/LibTooling.html
- Clang AST Matchers: https://clang.llvm.org/docs/LibASTMatchers.html
- C++ draft non-static member functions: https://eel.is/c++draft/class.mfct.non.static
- C++ draft object lifetime: https://eel.is/c++draft/basic.life

---

# File: docs/design/01_semantic_contract.md

# 01. Semantic Contract V3

## 1. 目的

この文書は、Aztecaが守る意味論上の契約を定義する。

アステカは、C++非staticメンバ関数をfake `this`で呼ぶツールではない。Clang AST/Sema後の情報から、対象メソッドのunit-observable semanticsを取り出し、`self`、`scenario`、`when`、`effects`を持つテスト可能なkernelへ変換するツールである。

## 2. 絶対禁止事項

```text
1. 未構築storageを `C*` に見せて非staticメンバ関数を呼ばない。
2. fake `this` を作らない。
3. pointer-to-memberをraw function pointerへ偽変換しない。
4. `#define private public` を中核手段にしない。
5. AST/Semaで解決していない構文を安全扱いしない。
6. 未設定依存queryへ暗黙デフォルト値を返さない。
7. 変換不能な意味をコメントだけで埋めて成功扱いしない。
```

## 3. 保存する意味

Aztecaが保存する標準意味は、unit-observable semanticsである。

```text
- 戻り値
- 例外
- receiver stateのread/write
- local stateの制御フロー上の意味
- dependency observations
- external effects
- call order when meaningful
- object identity
- dynamic type decision
- lifetime intent
- byte access intent
```

## 4. 標準では保存しない意味

標準抽出では、以下を実表現として保存しない。

```text
- 実アドレスの数値
- vptrの実表現
- padding byteの偶然値
- allocator内部状態
- OS handleの実値
- 未定義動作の結果
- inline asmの未モデル化機械効果
```

これらがテスト対象の本体である場合、アステカは「unit extractionとして意味が薄い」または「明示境界が必要」と報告する。

## 5. 単一公開抽出契約

利用者に通常要求する操作は1つである。

```bash
azteca extract -p build --method 'C::m(args...)'
```

内部では、必要に応じて次を自動的に追加する。

```text
self
shape
query/effect/operation ports
object_ref
cell/ref
dispatch table
type_tag
lifetime_state
byte_view
```

利用者に `Heart mode`、`Live mode`、`Rich Heart mode` の選択を迫らない。

## 6. Dependency Transcript契約

依存関係は、依存クラスのfakeではなく、対象メソッドが観測する値と外界へ送る効果として扱う。

```text
query:
  戻り値を持つ外界観測。

effect:
  戻り値なし、または戻り値を使わない外界要求。

operation:
  戻り値と副作用の両方が意味を持つ外界操作。
```

未設定queryに到達した場合、`missing_observation` を発生させる。`false`、`0`、`std::nullopt` などを暗黙に返してはならない。

## 7. Google Test契約

生成テストの標準ランナーはGoogle Testである。

ただし、以下はGoogle Test非依存に保つ。

```text
- kernel
- self
- shape
- ports
- scenario core runtime
- MMIR
- lowerer
```

Google Test依存は、sample/skeleton testと薄いadapterに限定する。

## 8. 正しさの主張

Aztecaが生成するkernelは、元メソッドの実バイナリではない。

Aztecaが主張するのは次である。

```text
同じreceiver状態、同じ引数、同じdependency transcriptが与えられたとき、
抽出kernelは元メソッドのunit-observable semanticsを保存する。
```

これは、製品コードの全ABI挙動を再現する主張ではない。

## 9. Live Validationの位置づけ

正規に構築された実オブジェクトと抽出kernelを比較するLive Validationは、抽出器の検証補助である。

```text
- 標準抽出方式ではない。
- fake objectを作らない。
- 実オブジェクトは正規factoryで構築する。
- 比較対象はobserverで取り出せるunit-observable semanticsに限る。
```

## 10. 契約違反時の挙動

契約違反が検出された場合、Aztecaは次のいずれかを行う。

```text
1. 安全な意味モデルへloweringする。
2. dependency transcript境界へ出す。
3. 明示的にnot-meaningful-for-unit-extractionとして報告する。
4. 実装未対応ならunsupportedではなく「未実装だが設計上の扱い」を報告する。
```

黙って危険な近似をしてはならない。

---

# File: docs/design/02_architecture.md

# 02. Architecture V3

## 1. 目的

この文書は、Aztecaの全体アーキテクチャを定義する。

V3の中核は次である。

```text
Clang AST
  -> Method Meaning IR (MMIR)
  -> Extraction Plan
  -> Semantic Envelope
  -> Generated kernel/scenario
  -> Google Test sample
```

## 2. 全体構成

```text
azteca CLI
  |
  v
Compilation Database Loader
  |
  v
Clang Tooling Frontend
  |
  v
Method Selector
  |
  v
MMIR Builder
  |
  +--> Feature Collector
  +--> Dependency Observation Collector
  +--> Shape Planner
  +--> Effect Planner
  +--> Path-wise Stub Burden Analyzer
  |
  v
Extraction Planner
  |
  v
Lowering Pipeline
  |
  v
Code Generator
  |
  +--> self/shape/ports/kernel/scenario
  +--> Google Test sample/skeleton
  +--> CMake/manifest/report
```

## 3. モジュール責務

### CLI

```text
- scan
- inspect
- extract
- build
- test
- explain
```

通常利用者には `extract` と `test` を中心に見せる。開発初期は `inspect` を先に実装する。

### CompilationDatabaseLoader

`compile_commands.json` を読み、元プロジェクトと同じinclude path、macro、標準バージョン、コンパイルオプションでASTを構築する。

### MethodSelector

`--method 'ns::C::m(int) const &'` を `clang::CXXMethodDecl` に解決する。

### MMIR Builder

Clang ASTを、Aztecaが扱いやすいMethod Meaning IRへ変換する。

MMIRは次を含む。

```text
- receiver access
- local variable
- branch
- return/throw
- dependency call
- object identity operation
- addressability operation
- type/lifetime/byte operation
```

### FeatureCollector

対象メソッドが必要とするSemantic Envelope要素を検出する。

```text
field state
shape
query/effect/operation
object_ref
cell/ref
dispatch
type_tag
lifetime
byte_view
```

### DependencyObservationCollector

依存呼び出しを、fakeクラスではなくDependency Transcript portとして集める。

```text
query
operation
effect
expression-level query
shape-producing query
```

### Path-wise Stub Burden Analyzer

制御フローを概算し、経路ごとに必要なquery/effectをreportする。

### ExtractionPlanner

利用者にmodeを選ばせず、内部で抽出計画を決める。

```text
- 再帰抽出するhelper
- transcript portにする依存
- shape化する戻り値
- object_refが必要な箇所
- Google Test skeletonに出すべきscenario行
```

### Lowering Pipeline

MMIRを生成C++へloweringする。

### Code Generator

次を生成する。

```text
- self.hpp
- shape.hpp
- ports.hpp
- kernel.hpp/cpp
- scenario.hpp
- Google Test sample/skeleton
- CMakeLists.txt
- manifest.json
- azteca_report.md
```

## 4. ディレクトリ構成

```text
src/
  cli/
  frontend/
  resolve/
  mmir/
  collect/
  plan/
  lower/
  codegen/
  report/
  runtime/
  gtest/

tests/
  unit/
  fixtures/
  golden/
  integration/
  negative/
  fuzz/
```

## 5. 実装順序

```text
Phase A:
  inspect as extraction plan

Phase B:
  minimal kernel + Google Test sample

Phase C:
  Dependency Transcript + Scenario Runtime

Phase D:
  Shape and expression-level ports

Phase E:
  Identity and addressability

Phase F:
  Dispatch and dynamic type

Phase G:
  Lifetime and representation
```

## 6. 公開UX契約

```text
1. 通常利用者にmode選択を要求しない。
2. Google Testを標準生成テストにする。
3. 依存fakeクラスを標準生成しない。
4. reportは次に書くべきscenario行を示す。
5. kernel/scenario runtimeはGoogle Test非依存に保つ。
```

---

# File: docs/design/03_extraction_pipeline.md

# 03. Extraction Pipeline V3

## 1. 目的

この文書は、対象メソッドをASTから抽出し、Google Testで試験可能なkernel/scenarioへ変換する処理順序を定義する。

## 2. Pipeline Overview

```text
1. compile_commands.json を読む
2. 対象translation unitをClangでparseする
3. 対象メソッドをCXXMethodDeclとして解決する
4. Clang ASTからMMIRを構築する
5. receiver stateを収集する
6. dependency observations/effects/operationsを収集する
7. shape候補を生成する
8. object identity/addressability/type/lifetime/byte要素を収集する
9. path-wise stub burdenを概算する
10. Extraction Planを作る
11. kernel/scenario/Google Test/CMake/report/manifestを生成する
```

## 3. Inspect Pipeline

`inspect` はコード生成しない。Extraction Planを表示する。

```bash
azteca inspect -p build --method 'C::m(int)'
```

出力:

```text
- receiver state
- generated shapes
- dependency observations
- observable effects
- object_ref requirements
- path-wise stub burden
- Google Test preview
```

## 4. Extract Pipeline

`extract` は生成物を出す。

```bash
azteca extract -p build --method 'C::m(int)' --out azteca-out
```

生成物:

```text
self.hpp
shape.hpp
ports.hpp
kernel.hpp/cpp
scenario.hpp
sample_test.cpp
skeleton_test.cpp
CMakeLists.txt
manifest.json
azteca_report.md
```

## 5. MMIR構築

MMIRは、Clang ASTを直接文字列置換せず、意味単位に整理する中間表現である。

例:

```cpp
if (locked_) return -1;
balance_ -= amount + fee(amount);
return balance_;
```

MMIR概念:

```text
Branch(ReadField locked_)
Return(Const -1)
WriteField(balance_, Sub(ReadField balance_, Add(Arg amount, DependencyCall fee)))
Return(ReadField balance_)
```

## 6. Dependency Collection

依存呼び出しは次へ分類する。

```text
recursive internal logic
query
operation
effect
expression-level query
shape-producing query
```

例:

```cpp
auto order = repo_.load(id);
if (!order) return ERR_NOT_FOUND;
bus_.publish(OrderApproved{id});
```

Plan:

```text
query repo_load(OrderId) -> optional<OrderShape>
effect bus_publish(OrderApproved)
```

## 7. Shape Planning

依存戻り値の本物を作る代わりに、対象メソッドが観測する部分だけをshape化する。

```cpp
order->deadline()
order->amount()
```

Shape:

```cpp
struct OrderShape {
    Time deadline;
    Money amount;
};
```

## 8. Path-wise Stub Burden

制御フローから、経路ごとに必要なquery/effectを概算する。

```text
not_found:
  required observations: repo_load
  effects: none

success:
  required observations: repo_load, clock_now, policy_allow
  effects: payment_reserve, bus_publish
```

これはreportとskeleton generationに使う。

## 9. Lowering

Loweringでは、MMIRをC++コードへ変換する。

対応例:

| 元意味                     | 生成意味                 |
| -------------------------- | ------------------------ |
| field read/write           | `self.x`                 |
| same-class pure helper     | recursive kernel         |
| external non-void call     | `ports.xxx.call(...)`    |
| external void call         | `ports.xxx.record(...)`  |
| return this                | `self.object_ref()`      |
| virtual call               | dispatch query/operation |
| dependency object property | shape field              |

## 10. Google Test Generation

標準でGoogle Test sampleを生成する。

```cpp
#include <gtest/gtest.h>
#include "generated/C_m.scenario.hpp"

TEST(C_m, sample) {
    auto s = azteca_gen::scenario::C_m{};
    s.when.some_query().returns(/* value */);
    auto result = s.call(/* args */);
    EXPECT_EQ(result, /* expected */);
}
```

## 11. Failure Handling

失敗時は、単にunsupportedで止めず、理由と代替を示す。

```text
- safe lowering possible -> generate model
- dependency boundary possible -> generate transcript port
- implementation missing -> report as not-yet-implemented with intended model
- unit extraction not meaningful -> explain why
```

## 12. 契約

```text
1. AST/Sema後情報を使う。
2. テキスト置換を中核にしない。
3. fake thisを作らない。
4. 依存fakeクラスを標準にしない。
5. Google Test生成を標準にする。
6. inspectで抽出計画を見せる。
7. extractでscenario付きkernelを生成する。
```

---

# File: docs/design/04_receiver_model.md

# 04. Receiver Model

## 1. 目的

この文書は、非staticメンバ関数の暗黙オブジェクト引数、すなわち`this`を、Heart modeでどのような明示receiver/self modelへ変換するかを定義する。

Receiver ModelはAztecaの中核である。fake `this` を作らない代わりに、メソッド本体が必要とする状態をself modelとして定義し、生成kernelの第一引数として渡す。

## 2. 基本形

元コード:

```cpp
class C {
    int x_;
public:
    int f(int a) {
        x_ += a;
        return x_;
    }
};
```

Heart mode生成:

```cpp
struct C_f_self {
    int x_;
};

int C_f(C_f_self& self, int a) {
    self.x_ += a;
    return self.x_;
}
```

## 3. Receiverの意味

`self`は`C`オブジェクトではない。

`self`は、対象メソッドのHeart kernelを実行するために必要な状態を保持するテスト用モデルである。

禁止:

```cpp
C* p = reinterpret_cast<C*>(&self); // 禁止
p->f(1);                            // 禁止
```

許可:

```cpp
C_f_self self{.x_ = 10};
int r = C_f(self, 5);
```

## 4. Receiver型決定

### 4.1 通常メソッド

```cpp
int C::f();
```

生成:

```cpp
int C_f(C_f_self& self);
```

### 4.2 constメソッド

```cpp
int C::f() const;
```

生成:

```cpp
int C_f(C_f_self const& self);
```

### 4.3 volatileメソッド

初期版では`volatile` receiverは`unsupported`または`live_required`に分類する。

理由:

- `volatile`の意味はメモリ観測やデバイスI/Oと関係しうる。
- self modelで安易に再現すると意味が変わる。

後続対応では、`C_f_self volatile&`を生成するオプションを検討する。

### 4.4 ref-qualifiedメソッド

```cpp
int C::f() &;
int C::f() &&;
int C::f() const &;
```

生成方針:

```cpp
int C_f_lvalue(C_f_self& self);
int C_f_rvalue(C_f_self&& self);
int C_f_const_lvalue(C_f_self const& self);
```

`&&`メソッドでは、元メソッドが`*this`をmove元として扱う可能性がある。selfを`&&`で受け、lowererは`std::move(self.field)`が必要な箇所だけを保持する。

## 5. フィールド収集

Receiver Plannerは、対象メソッド本体で使用される非staticデータメンバを収集する。

収集対象:

- 明示的な`this->x_`
- 暗黙の`x_`
- base class由来の`b_`
- member function call内で再帰抽出されるhelperが使うfield
- lambda this capture内で使うfield

例:

```cpp
class C {
    int a_;
    int b_;
    int unused_;
public:
    int f() { return a_ + b_; }
};
```

生成:

```cpp
struct C_f_self {
    int a_;
    int b_;
};
```

`unused_`は含めない。ただし差分検証やsnapshot互換を重視する設定では全fieldを含めてもよい。

## 6. Field Plan

フィールドごとに以下を記録する。

```cpp
struct FieldPlan {
    std::string originalName;
    std::string generatedName;
    QualType type;
    bool isMutable;
    bool isRead;
    bool isWritten;
    bool isReference;
    bool isPointer;
    bool isArray;
    bool isBitField;
    SourceLocation declarationLocation;
};
```

## 7. 命名規則

基本:

```text
元フィールド名を維持する。
```

例:

```cpp
struct Account_withdraw_self {
    int balance_;
    bool locked_;
};
```

名前衝突がある場合:

```text
az_base_<BaseName>_<field>
az_field_<ordinal>_<name>
```

生成コードには対応コメントを付ける。

```cpp
int az_base_B_x; // maps to B::x
```

## 8. private/protected/public

self modelは元クラスのアクセス指定を再現しない。

理由:

- selfは元クラスではない。
- テスト用状態モデルは利用者が直接初期化できる必要がある。
- Heart modeでは元クラスのprivateへアクセスしない。

ただし、manifestには元アクセスレベルを記録してよい。

```json
{
  "field": "balance_",
  "access": "private"
}
```

## 9. mutable

`mutable`属性はself modelに保持する。

元コード:

```cpp
class C {
    mutable int cache_;
public:
    int f() const {
        cache_ += 1;
        return cache_;
    }
};
```

生成:

```cpp
struct C_f_self {
    mutable int cache_;
};

int C_f(C_f_self const& self) {
    self.cache_ += 1;
    return self.cache_;
}
```

## 10. 参照メンバ

元コード:

```cpp
class C {
    int& ref_;
public:
    int f() { return ++ref_; }
};
```

生成方針:

```cpp
struct C_f_self {
    std::reference_wrapper<int> ref_;
};

int C_f(C_f_self& self) {
    return ++self.ref_.get();
}
```

理由:

- 参照メンバはrebindできない。
- selfの代入可能性・初期化しやすさを優先する。
- `T&`をそのままfieldにするとdefault constructionが難しい。

オプション:

```text
--receiver-reference-style native|reference_wrapper|pointer
```

初期既定は`reference_wrapper`。

## 11. ポインタメンバ

ポインタメンバはそのまま保持する。

```cpp
struct C_f_self {
    Node* head_;
};
```

ただし、ポインタ先のライフタイムはテスト側責務である。生成driverではnull/defaultを避け、明示初期化を要求する。

manifest:

```json
{
  "field": "head_",
  "type": "Node*",
  "requires_user_initialization": true
}
```

## 12. 配列メンバ

元コード:

```cpp
class C {
    int xs_[4];
public:
    int f(int i) { return xs_[i]; }
};
```

生成:

```cpp
struct C_f_self {
    int xs_[4];
};
```

配列境界は元型から保持する。生成driverでは初期化例を出す。

## 13. bit-field

初期版ではbit-fieldは`heart_partial_with_modeling`に分類する。

理由:

- bit-fieldの型、幅、符号、allocation unit、packingは処理系依存の要素を含む。
- self modelで通常整数へ変換するとオーバーフローや切り詰めの意味が変わる。

後続対応案:

```cpp
struct C_f_self {
    azteca::bitfield<int, 3> flags;
};
```

または、実際のbit-field宣言をselfにも生成する。

```cpp
struct C_f_self {
    int flags : 3;
};
```

ただし、layout依存の比較はLive modeに委ねる。

## 14. base class

### 14.1 単純base

元コード:

```cpp
struct B { int b_; };
struct C : B { int c_; int f() { return b_ + c_; } };
```

生成方針:

```cpp
struct B_self {
    int b_;
};

struct C_f_self {
    B_self base_B;
    int c_;
};

int C_f(C_f_self& self) {
    return self.base_B.b_ + self.c_;
}
```

### 14.2 複数継承

```cpp
struct B1 { int x_; };
struct B2 { int x_; };
struct C : B1, B2 {
    int f() { return B1::x_ + B2::x_; }
};
```

生成:

```cpp
struct C_f_self {
    B1_self base_B1;
    B2_self base_B2;
};
```

qualified accessを保持する。

### 14.3 virtual base

初期版では、virtual baseへの実レイアウト依存はLive modeを推奨する。

Heart modeで対応する場合は、共有base identityをself modelで明示する。

```cpp
struct C_f_self {
    std::shared_ptr<VBase_self> vbase_V;
};
```

ただし、これは実layoutの再現ではなく、状態共有のモデル化である。

## 15. static member

static data memberはreceiverに含めない。

```cpp
class C {
    static int s_;
    int x_;
public:
    int f() { return s_ + x_; }
};
```

生成:

```cpp
int C_f(C_f_self& self) {
    return C::s_ + self.x_;
}
```

ただしprivate static memberに外部からアクセスできない場合、生成コードがアクセス不能になる。この場合は次を選ぶ。

1. direct access不可としてdependency化
2. accessor injection
3. Live mode
4. friend test hookを明示的に要求

初期MVPではprivate static member direct accessは`extractable_with_transcript`として扱う。

## 16. anonymous union/struct

初期版では`unsupported`または`heart_partial_with_modeling`。

理由:

- active member管理が必要。
- anonymous memberの名前解決と状態表現が複雑。

後続対応では、active tagをself modelに追加する。

```cpp
struct C_f_self {
    enum class active_union_member { none, i, d } active;
    union { int i; double d; } u;
};
```

## 17. `this` の写像

`this`そのものが式として現れる場合、単純なfield accessとは異なる。

### 17.1 `this->field`

```cpp
this->x_
```

生成:

```cpp
self.x_
```

### 17.2 `*this`

`*this`が内部比較や参照としてのみ使われる場合、selfへ写像できる場合がある。

```cpp
return this == &other;
```

ただし`other`が`C&`なら、Heart modeでは`other`もself modelへ写像しない限り扱えない。

### 17.3 `return this`

元コード:

```cpp
C* C::get() { return this; }
```

Heart mode案:

```cpp
C_get_self* C_get(C_get_self& self) {
    return &self;
}
```

この場合、戻り型は`C*`ではなく`C_get_self*`へ変わるため、公開APIの型契約は保存されない。分類は`heart_partial_with_modeling`とする。

### 17.4 `external(this)`

```cpp
external(this);
```

原則`live_required`。

明示設定で、externalがlayout非依存であることを利用者が宣言した場合のみdependency modelへ写像できる。

## 18. Receiver初期化

生成sample testでは、すべての必要フィールドを明示初期化する。

```cpp
C_f_self self{
    .x_ = 10,
    .flag_ = false,
};
```

初期化漏れを避けるため、将来はbuilderを生成する。

```cpp
auto self = C_f_self_builder{}
    .x(10)
    .flag(false)
    .build();
```

## 19. Receiver snapshot

Heart/Live差分検証では、Live objectからselfへ状態を抽出する必要がある。

方法:

1. ユーザー提供observer
2. friend test hook
3. public accessor
4. debug reflection設定

初期版ではユーザー提供observerを基本とする。

```cpp
C_f_self snapshot(C const& obj);
```

## 20. Open questions

1. selfに使用fieldのみ含めるか、全fieldを含めるか。
2. 参照メンバの既定表現を`reference_wrapper`にするかポインタにするか。
3. private static memberをdependency化する際の標準形。
4. bit-fieldを初期版で直接生成するか。

初期判断:

- 使用fieldのみを含める。
- 参照メンバは`std::reference_wrapper`。
- private static memberはdependency化。
- bit-fieldはpartial扱い。

---

# File: docs/design/05_lowering_rules.md

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

| 用語        | 意味                         |
| ----------- | ---------------------------- |
| accept      | Heart modeで変換可能         |
| dependency  | 依存注入または再帰抽出が必要 |
| model       | 明示モデル追加で変換可能     |
| live        | Live modeが必要              |
| unsupported | 現在未対応                   |

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

## LR-022: return \*this

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

## LR-024: typeid(\*this)

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

---

# File: docs/design/06_dependency_model.md

# 06. Dependency Model V3

## 1. 目的

この文書は、抽出対象メソッドが他のメソッド、関数、global状態、外部資源、戻り値オブジェクト、virtual dispatch、`this` 同一性に依存する場合の扱いを定義する。

V3では、依存問題の基本方針を次のように改める。

```text
依存オブジェクトをfakeするのではない。
対象メソッドが外界から観測する値と、外界へ送る効果を、Dependency Transcriptとして扱う。
```

これにより、依存クラスの構築、private constructor、巨大なドメインモデル、DB/時刻/ネットワーク/通知などをユニットテストから切り離す。

## 2. 依存の基本分類

| ID  | 種類                            | 例                              | V3既定方針                            |
| --- | ------------------------------- | ------------------------------- | ------------------------------------- |
| D1  | same-class pure helper          | `fee(x)`                        | 可能なら再帰抽出                      |
| D2  | same-class boundary-like helper | `notify(x)`                     | query/effect/operation port           |
| D3  | base-class nonvirtual method    | `B::g()`                        | 再帰抽出またはport                    |
| D4  | virtual method                  | `compute(x)`                    | dispatch query port                   |
| D5  | static member function          | `C::normalize(x)`               | pureなら再帰/直接、外部性があればport |
| D6  | free function                   | `normalize(x)`                  | pureなら直接、外部性があればport      |
| D7  | global read/write               | `global_limit`                  | env portまたはeffect                  |
| D8  | member object method            | `repo_.load(id)`                | dependency transcript port            |
| D9  | external resource               | file/socket/db/time/random      | query/effect/operation port           |
| D10 | returned dependency object      | `repo_.load(id)->amount()`      | Shapeまたはexpression-level query     |
| D11 | object identity dependency      | `return this`, `external(this)` | `object_ref`                          |
| D12 | template helper                 | `helper<T>(x)`                  | specialization単位                    |

## 3. 依存処理の優先順位

アステカは、次の順で依存を処理する。

```text
1. 対象メソッドの意味を増やさず保てる内部ロジックは再帰抽出する。
2. 外部性のある依存はDependency Transcriptのportにする。
3. 依存が返す巨大オブジェクトは、対象メソッドが観測するshapeへ縮約する。
4. 依存オブジェクトの同一性が意味を持つ場合はobject_refで保存する。
5. 最終的にC++実行環境そのものが必要な箇所だけを明示境界にする。
```

この順序により、心臓ロジックを小さく切りすぎず、それでいて依存関係の構築地獄へ戻らない。

## 4. Dependency Transcript

Dependency Transcriptは、対象メソッドが実行中に外界と交換する情報の列である。

```text
Observation:
  外界から受け取る値。
  例: repo.load(id) -> OrderShape{...}

Effect:
  外界へ送る要求。
  例: bus.publish(OrderApproved{id})

Operation:
  戻り値もあり、副作用としても観測すべき外界操作。
  例: payment.reserve(amount) -> true
```

テストでは、ユーザーは依存クラスではなく、transcriptを記述する。

```cpp
auto s = azteca::scenario<OrderService_approve>();

s.when.repo_load(id).returns(OrderShape{
    .deadline = Time{1000},
    .amount = Money{5000},
});
s.when.clock_now().returns(Time{900});
s.when.policy_can_approve().returns(true);
s.when.risk_score(Money{5000}, UserId{42}).returns(20);

auto result = s.call(id);

EXPECT_EQ(result, OK);
s.effects.payment_reserve.expect_once(Money{5000});
s.effects.repo_mark_approved.expect_once(id);
s.effects.bus_publish.expect_once(OrderApproved{id});
```

## 5. Query / Effect / Operation

### 5.1 Query

戻り値を供給する依存である。

```cpp
auto now = clock_.now();
auto order = repo_.load(id);
```

生成例:

```cpp
struct OrderService_approve_ports {
    azteca::query<Time()> clock_now;
    azteca::query<std::optional<OrderShape>(OrderId)> repo_load;
};
```

未設定queryに到達した場合、デフォルト値を返してはならない。必ずmissing observationとして失敗させる。

### 5.2 Effect

戻り値がない、または戻り値が対象ロジックで使われない外部要求である。

```cpp
logger_.write(message);
bus_.publish(event);
```

生成例:

```cpp
struct OrderService_approve_effects {
    azteca::effect<Message> logger_write;
    azteca::effect<OrderApproved> bus_publish;
};
```

Effectは標準で記録してよい。テスト側は期待するものだけassertする。

### 5.3 Operation

戻り値もあり、副作用としても観測すべき依存である。

```cpp
bool ok = payment_.reserve(amount);
```

生成例:

```cpp
azteca::operation<bool(Money)> payment_reserve;
```

テストでは、戻り値を設定し、効果を検証できる。

```cpp
s.when.payment_reserve(Money{5000}).returns(true);
...
s.effects.payment_reserve.expect_once(Money{5000});
```

## 6. Shape Model

依存が返す型を本物として構築すると、ユニットテスト性を失うことがある。

元コード:

```cpp
auto order = repo_.load(id);
if (order->deadline() < clock_.now()) return ERR_EXPIRED;
return order->amount();
```

対象メソッドが使うのは `deadline()` と `amount()` だけである。この場合、生成するのは本物の `Order` ではなく、次のshapeでよい。

```cpp
struct OrderShape {
    Time deadline;
    Money amount;
};
```

lowering:

```cpp
auto order = deps.repo_load(id);
if (order.deadline < deps.clock_now()) return ERR_EXPIRED;
return order.amount;
```

Shapeは、依存型の代替実装ではない。対象メソッドが観測する意味だけを持つテスト用値である。

## 7. Expression-level Port

依存チェーンを逐一fakeすると破綻する。

```cpp
auto age = repo_.find(id)->profile().birthDate().age(clock_.now());
```

この式の途中の `User`、`Profile`、`BirthDate` が対象メソッド上で独立に意味を持たない場合、アステカは式全体をquery portへ畳める。

```cpp
auto age = deps.user_age_from_repo_and_clock(id);
```

テスト:

```cpp
s.when.user_age_from_repo_and_clock(id).returns(37);
```

畳んではならない条件:

```text
- 中間オブジェクトの同一性が比較される。
- 中間オブジェクトの状態が複数箇所で更新される。
- 中間オブジェクトへの参照/ポインタが外部へ渡る。
- 例外や副作用の順序が中間呼び出し単位で意味を持つ。
```

この場合は、Shape、object_ref、個別portへ展開する。

## 8. Object Ref

依存が返したオブジェクトや `this` の実アドレスではなく、同一性だけが必要な場合、`object_ref<T>` を使う。

```cpp
auto conn = pool_.acquire();
auto user = repo_.load(conn, id);
audit_.write(conn, user);
```

テスト:

```cpp
auto conn = s.objects.new_ref<Connection>("conn1");

s.when.pool_acquire().returns(conn);
s.when.repo_load(conn, id).returns(UserShape{.id = id});

s.call(id);

s.effects.audit_write.expect_once(conn, UserShape{.id = id});
```

`Connection` の実体は作らない。必要なのは「同じものが渡された」という意味である。

## 9. 経路ごとのStub Burden

依存が多いメソッドでも、1つのテスト経路で必要なqueryは少ないことが多い。

```cpp
if (!enabled_) return DISABLED;
if (!repo_.exists(id)) return NOT_FOUND;
if (!policy_.allow(id)) return DENIED;
notifier_.send(id);
return OK;
```

`DISABLED` 経路:

```cpp
s.self.enabled = false;
auto result = s.call(id);
EXPECT_EQ(result, DISABLED);
s.effects.expect_none();
```

必要queryは0個。

`NOT_FOUND` 経路:

```cpp
s.self.enabled = true;
s.when.repo_exists(id).returns(false);
auto result = s.call(id);
EXPECT_EQ(result, NOT_FOUND);
```

必要queryは1個。

`SUCCESS` 経路:

```cpp
s.self.enabled = true;
s.when.repo_exists(id).returns(true);
s.when.policy_allow(id).returns(true);
auto result = s.call(id);
EXPECT_EQ(result, OK);
s.effects.notifier_send.expect_once(id);
```

必要queryは2個。

`inspect` と `report` は、依存総量だけでなく、経路ごとのstub burdenを表示する。

## 10. Missing Observation診断

未設定queryに到達した場合、テストを曖昧に通してはならない。

出力例:

```text
Missing dependency observation

Port:
  policy_can_approve(UserShape, OrderShape) -> bool

Reached from:
  order_service.cpp:31

Expression:
  policy_.canApprove(user_, *order)

Suggested scenario line:
  s.when.policy_can_approve(/* user */, /* order */).returns(true);
```

Google Test統合では、これは例外またはfatal failureとして表現できる。標準生成テストでは、missing observationを発生させないskeletonを出す。

## 11. 再帰抽出とスタブ化の判断

### 11.1 再帰抽出すべきもの

```text
- private pure helper
- fieldだけを見る計算helper
- enum/state machine helper
- 入力検証helper
- 副作用を持たない正規化関数
```

### 11.2 Port化すべきもの

```text
- DB、ファイル、ネットワーク、時刻、乱数
- 通知、ログ、監査、イベント送信
- 他コンポーネントのpublic API
- 依存オブジェクトを構築しないと呼べない処理
- 依存内部の正しさが対象メソッドのunit test目的ではない処理
```

### 11.3 ユーザーに選択を迫らない

標準ではアステカが判断する。
ユーザーは必要な場合だけ設定で上書きできる。

```yaml
boundaries:
  normalize:
    strategy: recursive
  Payment::reserve:
    strategy: operation
  Audit::write:
    strategy: effect
```

## 12. GoogleMockとの関係

Google Testは標準ランナーとするが、GoogleMockをアステカ依存処理の中核にはしない。

理由:

```text
- アステカのportは元C++ interfaceと1対1とは限らない。
- expression-level portやshape化は通常のmock objectと相性が悪い。
- 依存クラスfakeを作ると、C++オブジェクト構築問題へ戻る。
```

ただし、既存プロジェクトがGoogleMockを使っている場合、以下は許容する。

```text
- effects logの内容をMATCHERで検査する。
- scenarioからGoogleMock adapterを呼ぶ。
- record/replayの境界実装で既存mockを利用する。
```

GoogleMockは補助であり、アステカの主要抽象はScenario APIである。

## 13. 生成物

依存がある抽出では、次を生成する。

```text
include/
  C_m.self.hpp
  C_m.shape.hpp
  C_m.ports.hpp
  C_m.scenario.hpp
  C_m.kernel.hpp

tests/
  C_m.sample_test.cpp
  C_m.scenario_skeleton.cpp

azteca_report.md
manifest.json
```

`scenario.hpp` は利用者が読む主ファイルである。

## 14. 契約

Dependency Model V3の契約は次の通り。

```text
1. 依存オブジェクトの完全fakeを標準にしない。
2. 対象メソッドが観測する値だけをqueryとして扱う。
3. 対象メソッドが外界へ送る要求だけをeffectとして扱う。
4. 戻り値と副作用の両方が意味を持つ依存はoperationにする。
5. 未設定queryに到達したら失敗する。
6. effectは標準で記録する。
7. 経路ごとの必要観測をinspect/reportで示す。
8. Google Testで自然にassertできるScenario APIを生成する。
```

---

# File: docs/design/07_live_mode.md

# 07. Live Validation Model

## 1. 目的

この文書は、正規に構築された実オブジェクトと抽出kernelを比較するLive Validationの位置づけを定義する。

V3では、Liveは利用者が通常選ぶ抽出modeではない。抽出器の検証、差分確認、実オブジェクト性が仕様の一部である箇所の補助検査として扱う。

## 2. 契約

```text
1. fake objectを作らない。
2. 未構築storageへメンバ関数を呼ばない。
3. pointer-to-memberをraw function pointerへ変換しない。
4. 実オブジェクトは正規factoryで構築する。
5. 比較対象はobserverで取り出せるunit-observable semanticsに限定する。
```

## 3. 用途

```text
- 抽出kernelと製品メソッドの差分検証
- receiver snapshot生成の確認
- semantic loweringのregression検出
- 実プロジェクト導入時の信頼性確認
```

## 4. 例

元クラス:

```cpp
class Account {
    int balance_;
public:
    explicit Account(int b) : balance_(b) {}
    int withdraw(int amount) {
        balance_ -= amount;
        return balance_;
    }
    int balance() const { return balance_; }
};
```

Live Validation用observer:

```cpp
azteca_gen::generated::Account_withdraw_self snapshot(Account const& a) {
    return {.balance_ = a.balance()};
}
```

Google Test差分検証:

```cpp
TEST(Account_withdraw_diff, sample) {
    Account live{100};

    auto s = azteca_gen::scenario::Account_withdraw{};
    s.self = snapshot(live);

    auto r_kernel = s.call(40);
    auto r_live = live.withdraw(40);

    auto after = snapshot(live);

    EXPECT_EQ(r_kernel, r_live);
    EXPECT_EQ(s.self.balance_, after.balance_);
}
```

## 5. 生成条件

Live Validationは、ユーザーがfactory/observerを提供できる場合のみ生成する。

```bash
azteca extract -p build \
  --method 'Account::withdraw(int)' \
  --validate-with-live-factory make_account_for_azteca \
  --validate-with-live-observer snapshot_account
```

これは抽出方式の選択ではなく、検証補助である。

## 6. Google Test統合

差分検証もGoogle Testで生成する。

```text
tests/account.withdraw.diff_test.cpp
```

`ctest` で通常の生成sampleと一緒に実行できる。

## 7. 契約上の限界

Live Validationは、以下を解決しない。

```text
- private stateをobserverで取り出せない問題
- 正規factoryが作れない型
- 非決定的I/O
- timing/thread scheduling依存
- 元コードのUB
```

これらはDependency Transcript、record/replay、または明示境界で扱う。

---

# File: docs/design/08_codegen_spec.md

# 08. Code Generation Specification V3

## 1. 目的

この文書は、Aztecaが生成するファイル、命名規則、C++コード形状、manifest、CMake構成を定義する。

V3では、標準生成テストをGoogle Testに統一し、依存関係はDependency Transcript / Scenario APIとして生成する。

## 2. 出力ディレクトリ構成

既定出力:

```text
azteca-out/
  include/
    azteca/
      azteca_runtime.hpp
      azteca_gtest.hpp
    generated/
      account.withdraw.self.hpp
      account.withdraw.shape.hpp
      account.withdraw.ports.hpp
      account.withdraw.scenario.hpp
      account.withdraw.kernel.hpp
  src/
    account.withdraw.kernel.cpp
  tests/
    account.withdraw.sample_test.cpp
    account.withdraw.skeleton_test.cpp
  live/
    account.withdraw.live_probe.cpp          optional
    account.withdraw.diff_test.cpp           optional
  manifest.json
  azteca_report.md
  CMakeLists.txt
  README.md
```

不要なファイルは生成しない。依存がなければ `shape.hpp` や `ports.hpp` は省略可能。ただしmanifestには「依存なし」を明記する。

## 3. ファイル命名規則

基本形:

```text
<namespace>.<class>.<method>.<signature-hash>.<kind>.<ext>
```

例:

```text
account.withdraw.self.hpp
account.withdraw.kernel.cpp
order_service.approve.u64.scenario.hpp
```

衝突回避:

```text
- overloadがない場合は短い名前を使う。
- overloadがある場合はsignature hashを追加する。
- template specializationはtemplate args hashを追加する。
- manifestに元宣言との完全対応を記録する。
```

## 4. 生成ヘッダ共通規則

各生成ファイル冒頭に次を入れる。

```cpp
// Generated by Azteca 0.3.0.
// Source: src/account.cpp:17
// Target: Account::withdraw(int)
// Public model: unified extraction
// Do not edit manually unless this file is marked as user-editable.
```

ユーザーが編集するsample/skeleton testには明記する。

```cpp
// User-editable sample generated by Azteca.
// Re-run Azteca with --preserve-user-tests to avoid overwriting.
```

## 5. namespace

生成コードは `azteca_gen` 配下に置く。

```cpp
namespace azteca_gen::generated {
    // generated code
}

namespace azteca_gen::scenario {
    // user-facing scenario wrappers
}
```

プロジェクト別namespaceを指定可能にする。

```bash
--gen-namespace azteca_gen::my_project
```

## 6. self header

例:

```cpp
#pragma once

namespace azteca_gen::generated {

struct Account_withdraw_self {
    int balance_{};
    bool locked_{};
};

} // namespace azteca_gen::generated
```

要件:

```text
- 対象メソッドが読む/書くreceiver状態だけを含める。
- 元フィールド対応をコメントまたはmetadataに残す。
- const/mutable/reference/address-taken fieldの情報を保持する。
- `this` 同一性が必要な場合は object_ref accessorを持つ。
```

object_refが必要な例:

```cpp
struct C_register_self {
    azteca::object_ref<C> self_ref{azteca::object_id::fresh()};

    azteca::object_ref<C> object_ref() const noexcept {
        return self_ref;
    }
};
```

## 7. shape header

依存戻り値や外部オブジェクトをshape化する場合に生成する。

```cpp
#pragma once

namespace azteca_gen::generated {

struct OrderShape {
    Time deadline{};
    Money amount{};
};

} // namespace azteca_gen::generated
```

要件:

```text
- 対象メソッドが観測するfield/propertyのみ含める。
- 本物の依存クラスを構築しない。
- Google Testで比較しやすいoperator==またはprinterを生成可能にする。
```

## 8. ports header

依存観測と外部効果を宣言する。

```cpp
#pragma once

#include <azteca/azteca_runtime.hpp>
#include "account.withdraw.self.hpp"

namespace azteca_gen::generated {

struct Account_withdraw_ports {
    azteca::query<int(int)> fee;
};

struct Account_withdraw_effects {
    void expect_none() const;
    void expect_no_unexpected_calls() const;
};

} // namespace azteca_gen::generated
```

OrderService例:

```cpp
struct OrderService_approve_ports {
    azteca::query<std::optional<OrderShape>(OrderId)> repo_load;
    azteca::query<Time()> clock_now;
    azteca::query<bool(UserId, OrderShape)> policy_can_approve;
    azteca::query<int(Money, UserId)> risk_score;
    azteca::operation<bool(Money)> payment_reserve;
    azteca::effect<OrderId> repo_mark_approved;
    azteca::effect<OrderApproved> bus_publish;
};
```

## 9. kernel header/source

kernelはGoogle Test非依存である。

```cpp
#pragma once

#include "account.withdraw.self.hpp"
#include "account.withdraw.ports.hpp"

namespace azteca_gen::generated {

int Account_withdraw(Account_withdraw_self& self,
                     Account_withdraw_ports& ports,
                     int amount);

} // namespace azteca_gen::generated
```

source:

```cpp
#include "generated/account.withdraw.kernel.hpp"

namespace azteca_gen::generated {

int Account_withdraw(Account_withdraw_self& self,
                     Account_withdraw_ports& ports,
                     int amount) {
    if (self.locked_) return -1;
    self.balance_ -= amount + ports.fee.call(amount);
    return self.balance_;
}

} // namespace azteca_gen::generated
```

要件:

```text
- fake C objectを作らない。
- 元bodyのstatement orderを維持する。
- source mappingをmanifest/reportに残す。
- unsupported部分をコメントで埋めて成功扱いしない。
- dependency callはquery/effect/operationを通す。
```

## 10. scenario header

scenarioは利用者が主に触る生成物である。

```cpp
#pragma once

#include "generated/account.withdraw.kernel.hpp"

namespace azteca_gen::scenario {

struct Account_withdraw {
    generated::Account_withdraw_self self;
    generated::Account_withdraw_ports ports;

    struct when_api {
        generated::Account_withdraw_ports& ports;

        auto fee(int amount) {
            return ports.fee.bind(amount);
        }
    } when{ports};

    auto& effects = ports;

    int call(int amount) {
        return generated::Account_withdraw(self, ports, amount);
    }
};

} // namespace azteca_gen::scenario
```

実際のAPIは、`ports.fee.bind(amount)` ではなく、runtime側の安定APIに合わせる。重要なのは利用者表面が次になること。

```cpp
s.when.fee(50).returns(5);
auto result = s.call(50);
s.effects.expect_none();
```

## 11. Google Test sample

標準sampleはGoogle Testで生成する。

```cpp
#include <gtest/gtest.h>
#include "generated/account.withdraw.scenario.hpp"

TEST(Account_withdraw, unlocked_path) {
    auto s = azteca_gen::scenario::Account_withdraw{};

    s.self.balance_ = 100;
    s.self.locked_ = false;
    s.when.fee(50).returns(5);

    auto result = s.call(50);

    EXPECT_EQ(result, 45);
    EXPECT_EQ(s.self.balance_, 45);
    s.effects.expect_none();
}

TEST(Account_withdraw, locked_path_does_not_call_fee) {
    auto s = azteca_gen::scenario::Account_withdraw{};

    s.self.balance_ = 100;
    s.self.locked_ = true;

    auto result = s.call(50);

    EXPECT_EQ(result, -1);
    EXPECT_EQ(s.self.balance_, 100);
    s.when.fee.expect_not_called();
}
```

## 12. CMakeLists

標準生成CMake:

```cmake
cmake_minimum_required(VERSION 3.20)
project(azteca_generated_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(GTest CONFIG REQUIRED)

add_executable(account_withdraw_test
    tests/account.withdraw.sample_test.cpp
    src/account.withdraw.kernel.cpp
)

target_include_directories(account_withdraw_test PRIVATE include)
target_link_libraries(account_withdraw_test PRIVATE GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(account_withdraw_test)
```

FetchContentは標準では自動挿入しない。`--gtest fetchcontent` など明示オプションで生成する。

## 13. manifest.json

```json
{
  "azteca_version": "0.3.0",
  "public_model": "unified_extraction",
  "test_framework": "googletest",
  "target": {
    "qualified_name": "Account::withdraw",
    "signature": "int(int)",
    "source_file": "src/account.cpp",
    "line": 17
  },
  "generated": {
    "self": "include/generated/account.withdraw.self.hpp",
    "ports": "include/generated/account.withdraw.ports.hpp",
    "scenario": "include/generated/account.withdraw.scenario.hpp",
    "kernel": "src/account.withdraw.kernel.cpp",
    "sample_test": "tests/account.withdraw.sample_test.cpp"
  },
  "dependency_transcript": {
    "queries": ["fee(int) -> int"],
    "effects": [],
    "operations": []
  }
}
```

## 14. 生成コードの編集ポリシー

```text
Generated core files:
  self/shape/ports/kernel/scenarioは原則再生成対象。

User-editable files:
  sample_test/skeleton_testはユーザー編集可能。

Preservation:
  --preserve-user-tests が既定。
```

## 15. 契約

```text
1. kernelはGoogle Test非依存。
2. sample/skeleton testはGoogle Test標準。
3. dependencyはfake classではなくports/scenarioとして生成。
4. missing queryに暗黙デフォルト値を入れない。
5. manifestに生成物と意味モデルを記録。
6. 生成テストはCMake/CTestで実行可能。
```

---

# File: docs/design/09_cli_and_outputs.md

# 09. CLI and Outputs V3

## 1. 目的

この文書は、Azteca CLIのサブコマンド、引数、出力形式、終了コードを定義する。

V3では、公開UXを単純に保つ。

```text
通常利用者が最初に使うコマンド:
  azteca extract -p build --method 'C::m(args...)'

実装初期に作るコマンド:
  azteca inspect -p build --method 'C::m(args...)'
```

Google Testを標準生成テストフレームワークとする。

## 2. 基本形式

```bash
azteca <command> [options]
```

共通オプション:

```text
-p, --build-dir <dir>        compile_commands.jsonのあるbuild directory
--source <file>              対象source fileを明示
--method <spec>              対象メソッド指定
--out <dir>                  出力ディレクトリ。既定: azteca-out
--format <text|json>         診断出力形式
--verbose                    詳細診断
--quiet                      最小出力
```

## 3. scan

プロジェクト内の候補メソッドを列挙する。

```bash
azteca scan -p build
```

出力例:

```text
Methods:
  Account::withdraw(int) at src/account.cpp:17
  Account::deposit(int) at src/account.cpp:25
  Account::fee(int) const at src/account.cpp:40
```

## 4. inspect

対象メソッドを解析し、抽出計画を表示する。コード生成はしない。

```bash
azteca inspect -p build --method 'OrderService::approve(OrderId)'
```

出力例:

```text
Azteca can extract OrderService::approve(OrderId).

Generated Google Test:
  tests/order_service.approve.sample_test.cpp

Receiver state:
  - UserId user_id

Generated shapes:
  OrderShape
    - Time deadline
    - Money amount

Dependency observations:
  query repo_load(OrderId) -> optional<OrderShape>
  query clock_now() -> Time
  query policy_can_approve(UserId, OrderShape) -> bool
  query risk_score(Money, UserId) -> int

Observable effects:
  operation payment_reserve(Money) -> bool
  effect repo_mark_approved(OrderId)
  effect bus_publish(OrderApproved)

Path-wise test burden:
  not_found:
    observations: repo_load
    effects: none
  expired:
    observations: repo_load, clock_now
    effects: none
  success:
    observations: repo_load, clock_now, policy_can_approve, risk_score, payment_reserve
    effects: repo_mark_approved, bus_publish

No fake OrderService object will be created.
No dependency fake class is required.
```

JSON出力は、report生成、IDE連携、Phase B以降のcodegen入力候補に使う。

Phase AのJSON schemaは `schema_version: 2` を安定契約とする。主要な意味は次である。

```text
result:
  extracted                         保守的注記なしでinspectできた
  extracted-with-conservative-notes 保守的に表現した構文や経路がある
  invalid-plan                      MMIR/planの内部検証に失敗した

confidence:
  high    supported/modeled中心
  medium  conservative constructを含む
  low     not_yet_implemented constructを含む

diagnostics:
  userが次に見るべき警告・エラーを保持する。通常のconservative noteはwarningで出す。

unsupported_or_modeled_constructs:
  fake thisや危険な変換を避けるため、boundary/model/conservative/future扱いにした箇所を保持する。
```

## 5. extract

対象メソッドの生成物を作る。

標準形:

```bash
azteca extract -p build \
  --method 'Account::withdraw(int)' \
  --out azteca-out
```

標準で生成するもの:

```text
- self
- shape if needed
- ports
- scenario
- kernel
- Google Test sample
- CMakeLists.txt
- manifest.json
- azteca_report.md
```

通常利用者に `--mode` は要求しない。

高度なオプション:

```text
--strict                         意味抽象化をより保守的にする
--gen-namespace <name>            生成namespace
--test-framework <googletest|none> 既定: googletest
--gtest <find-package|fetchcontent|external> 既定: find-package
--emit-skeleton                   skeleton test生成
--emit-cmake                      CMakeLists生成
--format-code                     clang-format実行
--force                           出力先上書き
--preserve-user-tests             user-editable testsを上書きしない。既定true
```

内部向け・開発者向けオプション:

```text
--explain-envelope                Semantic Envelope詳細表示
--boundary-policy <auto|transcript|direct|recursive>
--validate-with-live-factory <fn> optional差分検証
```

## 6. build

生成物をビルドする。

```bash
azteca build azteca-out
```

実行内容:

```text
cmake -S azteca-out -B azteca-out/build
cmake --build azteca-out/build
```

オプション:

```text
--sanitize address,undefined
--config Debug|Release
--cmake-generator <name>
--clean
```

## 7. test / run

生成Google Testを実行する。

```bash
azteca test azteca-out
```

内部的にはCTestを使う。

```text
ctest --test-dir azteca-out/build --output-on-failure
```

`run` は後方互換として `test` のaliasにできる。

## 8. diff

抽出kernelと正規構築された実オブジェクトの差分検証を生成または実行する。
これは標準抽出方式ではなく、検証補助である。

```bash
azteca diff azteca-out \
  --live-factory 'make_account_for_azteca' \
  --live-observer 'snapshot_account'
```

生成される差分検証もGoogle Testにできる。

```cpp
TEST(Account_withdraw_diff, sample_case) {
    // live object and extracted scenario comparison
}
```

## 9. explain

ルールや診断の理由を表示する。

```bash
azteca explain missing-observation
```

出力例:

```text
Missing observation

A non-void dependency query was reached without a configured return value.
Azteca does not return implicit defaults such as false, 0, or nullopt.
Add a scenario line like:

  s.when.policy_allow(id).returns(true);
```

## 10. record / replay 将来機能

大量依存メソッドのscenario作成を補助する。

```bash
azteca record -p build --method 'OrderService::approve(OrderId)' --case integration_case
azteca replay-transcript transcript.json --emit-gtest --out azteca-out/tests
```

record/replayは補助であり、標準unit testの意図確認を置き換えない。

## 11. method spec syntax

基本:

```text
QualifiedClass::method(type1, type2)
```

cv/ref:

```text
C::f(int) const
C::f(int) &
C::f(int) const &
C::f(int) &&
```

template:

```text
C::f<int>(int)
ns::C::g<std::string>(std::string const&)
```

operator:

```text
C::operator+(C const&) const
C::operator[](int)
```

Phase A `inspect` では、operator overloadそのものをtarget methodにする指定は未対応として拒否する。
一方で、通常メソッド本体内に現れるoverloaded operator callは、解決済みcallee metadataを持つboundary候補としてreportする。

constructor/destructorは将来:

```text
C::C(int)
C::~C()
```

## 12. 終了コード

```text
0  success
1  user input error
2  compile database error
3  method resolution error
4  extraction planning error
5  code generation error
6  build/test failure
```

## 13. 契約

```text
1. `extract` は標準でGoogle Test生成物を出す。
2. `--mode` を通常利用者に要求しない。
3. `inspect` はdependency transcriptとpath-wise stub burdenを表示する。
4. `test` は生成Google Testを実行する。
5. 独自runnerは標準CLI体験に出さない。
```

---

# File: docs/design/10_test_strategy.md

# 10. Test Strategy V3

## 1. 目的

この文書は、Azteca自身の品質保証と、Aztecaが生成するテストの品質保証を定義する。

V3では、生成テストの標準ランナーを **Google Test** とする。アステカ自身のテストにもGoogle Testを使用する。独自runnerは、Google Testで実現できない制約が明確になった場合のfallbackであり、標準路線ではない。

## 2. テスト対象の二層構造

アステカの品質は、次の二層で保証する。

```text
A. Azteca implementation tests
   resolver / planner / lowerer / codegen / runtime の品質を検証する。

B. Generated method tests
   アステカが生成したkernel、scenario、deps、effectsがGoogle Testで実行できることを検証する。
```

## 3. Azteca implementation tests

```text
tests/
  unit/
    method_spec_parser/
    method_selector/
    mmir_builder/
    envelope_planner/
    dependency_planner/
    shape_planner/
    lowerer/
    codegen/
    runtime/
  fixtures/
    simple_field/
    helper_call/
    dependency_query/
    dependency_effect/
    operation/
    raw_this_escape/
    object_ref/
    shape_return/
    expression_level_port/
    virtual_dispatch/
  golden/
    inspect_reports/
    generated_files/
    manifests/
  integration/
    build_generated/
    run_generated_gtest/
    missing_observation/
    pathwise_stub_burden/
  negative/
    invalid_method_spec/
    ambiguous_method/
    unsupported_ub/
  fuzz/
    method_spec_parser/
    scenario_runtime/
```

## 4. Google Testを標準にする理由

標準テストランナーとしてGoogle Testを採用する理由は次の通り。

```text
- C++ユニットテストの既存資産として広く使われている。
- TEST/TEST_F/EXPECT/ASSERTなどの記法により、生成テストが読みやすい。
- CMake/CTest連携がしやすい。
- 生成テストを既存CIへ投入しやすい。
- アステカ独自runnerを作るより導入負担が小さい。
```

標準生成コードは以下を含む。

```cpp
#include <gtest/gtest.h>
#include "C_m.scenario.hpp"

TEST(C_m, sample_success_path) {
    auto s = azteca_gen::scenario::C_m{};

    s.self.x = 10;
    s.when.some_query(1).returns(2);

    auto result = s.call(1);

    EXPECT_EQ(result, 12);
    s.effects.some_effect.expect_none();
}
```

## 5. 独自runnerを標準にしない理由

C++のユニットテストは `main` と `assert` だけでも可能である。しかし、アステカが独自runnerを標準にすると、利用者は新しいテスト生態系を学ぶ必要がある。

したがって、標準はGoogle Testにする。

独自runnerを検討する条件は限定する。

```text
- Google Testではmissing observationの診断を十分に表せない。
- sandbox実行やrecord/replay制御にrunner固有制約が必要になる。
- fuzzingやsymbolic executionと統合する際にGoogle Testが支障になる。
- 組込み/クロス環境でGoogle Test自体の導入が不可能である。
```

その場合でも、生成kernelとscenario runtimeはGoogle Test非依存に保ち、runnerだけを差し替えられるようにする。

## 6. Google Test依存境界

`azteca_runtime.hpp` と生成kernelは、Google Testへ直接依存しない。

```text
Google Testに依存してよい:
  - generated sample test
  - generated test skeleton
  - gtest adapter
  - Azteca implementation tests

Google Testに依存しない:
  - kernel
  - self
  - shape
  - ports
  - core scenario runtime
  - MMIR
  - lowerer
```

これにより、将来的に別runnerへ切り替える余地を残す。

## 7. 生成Google Testの標準形

### 7.1 単純ロジック

```cpp
TEST(Account_withdraw, unlocked_subtracts_amount_and_fee) {
    auto s = azteca_gen::scenario::Account_withdraw{};

    s.self.balance_ = 100;
    s.self.locked_ = false;
    s.when.fee(50).returns(5);

    auto result = s.call(50);

    EXPECT_EQ(result, 45);
    EXPECT_EQ(s.self.balance_, 45);
    s.effects.expect_none();
}
```

### 7.2 早期return経路

```cpp
TEST(Account_withdraw, locked_returns_error_without_fee_query) {
    auto s = azteca_gen::scenario::Account_withdraw{};

    s.self.balance_ = 100;
    s.self.locked_ = true;

    auto result = s.call(50);

    EXPECT_EQ(result, -1);
    EXPECT_EQ(s.self.balance_, 100);
    s.when.fee.expect_not_called();
}
```

### 7.3 外部効果

```cpp
TEST(OrderService_approve, success_reserves_marks_and_publishes) {
    auto s = azteca_gen::scenario::OrderService_approve{};

    auto id = OrderId{10};
    s.self.user_id = UserId{42};
    s.when.repo_load(id).returns(OrderShape{
        .deadline = Time{1000},
        .amount = Money{5000},
    });
    s.when.clock_now().returns(Time{900});
    s.when.policy_can_approve().returns(true);
    s.when.risk_score(Money{5000}, UserId{42}).returns(20);

    auto result = s.call(id);

    EXPECT_EQ(result, OK);
    s.effects.payment_reserve.expect_once(Money{5000});
    s.effects.repo_mark_approved.expect_once(id);
    s.effects.bus_publish.expect_once(OrderApproved{id});
    s.effects.expect_no_unexpected_calls();
}
```

## 8. Missing Observation Tests

未設定queryに到達した場合、生成runtimeはmissing observationを発生させる。

標準では例外型を使う。

```cpp
TEST(OrderService_approve, missing_policy_observation_reports_port) {
    auto s = azteca_gen::scenario::OrderService_approve{};

    s.when.repo_load(OrderId{10}).returns(OrderShape{.amount = Money{5000}});
    s.when.clock_now().returns(Time{900});

    EXPECT_THROW({
        (void)s.call(OrderId{10});
    }, azteca::missing_observation);
}
```

追加で、Google Test adapterを提供する。

```cpp
AZTECA_EXPECT_MISSING_OBSERVATION(s.call(OrderId{10}), "policy_can_approve");
```

ただし、このマクロは補助であり、必須ではない。

## 9. Golden tests

Golden testsは、fixture入力から生成される以下を比較する。

```text
- inspect report
- self.hpp
- shape.hpp
- ports.hpp
- scenario.hpp
- kernel.cpp
- sample_test.cpp
- manifest.json
```

更新コマンド:

```bash
AZTECA_ACCEPT_GOLDEN=1 ctest -R golden
```

Golden更新はレビュー必須である。

## 10. Compile and Run tests

生成されたGoogle Testプロジェクトは必ずビルド・実行する。

```bash
azteca extract -p build --method 'C::m(int)' --out tmp/out
cmake -S tmp/out -B tmp/out/build
cmake --build tmp/out/build
ctest --test-dir tmp/out/build --output-on-failure
```

検証:

```text
- 生成CMakeがGoogle Testを見つけられる。
- sample_testがコンパイルできる。
- sample_testが実行できる。
- missing observationが期待通り失敗する。
- effects assertionが期待通り失敗/成功する。
```

## 11. Path-wise Stub Burden tests

依存が多いfixtureでは、経路ごとの必要stub数を検証する。

```cpp
int C::f(Id id) {
    if (!enabled_) return DISABLED;
    if (!repo_.exists(id)) return NOT_FOUND;
    if (!policy_.allow(id)) return DENIED;
    notifier_.send(id);
    return OK;
}
```

期待:

```text
DISABLED path:
  required queries: none

NOT_FOUND path:
  required queries: repo_exists

DENIED path:
  required queries: repo_exists, policy_allow

OK path:
  required queries: repo_exists, policy_allow
  effects: notifier_send
```

この検証により、依存総量ではなく、実際のテスト負担を報告できることを保証する。

## 12. Runtime tests

`azteca_runtime` のテスト項目:

```text
query:
  - returns configured value
  - missing observation throws
  - call count is tracked
  - argument mismatch diagnostics are readable

effect:
  - records calls
  - expect_once succeeds/fails correctly
  - expect_none succeeds/fails correctly
  - ordering assertions work

operation:
  - supplies return value
  - records effect
  - missing return value fails

object_ref:
  - identity equality
  - no pointer conversion

shape:
  - value equality
  - printable diagnostics
```

## 13. Sanitizer/Fuzzer

生成kernelは通常のC++コードなので、ASan/UBSan/coverage/fuzzerを載せられる。

ただし、fuzzerはGoogle Test runnerと分けてもよい。

```text
- generated unit tests: Google Test
- generated fuzz harness: libFuzzer/AFL++ adapter
```

## 14. Definition of Done

V3テスト戦略上、Phase A〜Cの完了条件は次の通り。

```text
Phase A:
  inspectがdependency transcript planとpath-wise stub burdenを出せる。

Phase B:
  最小kernelとGoogle Test sampleが生成・実行できる。

Phase C:
  query/effect/operation/scenario/missing observationがGoogle Testで検証できる。
```

---

# File: docs/design/11_unsupported_and_fallbacks.md

# 11. Unsupported and Fallbacks

## 1. 目的

この文書は、Heart modeで扱えない、または初期版で未対応の構文・意味と、それぞれのfallbackを定義する。

Aztecaは「できない」で終わらせない。変換不能な理由を分類し、可能な代替策を提示する。ただし、危険なfake `this`やABIハックには逃げない。

## 2. 分類表

| 構文・意味                      |          Heart mode | Live mode | 備考                       |
| ------------------------------- | ------------------: | --------: | -------------------------- |
| field read/write                |                 yes |       yes | 基本対応                   |
| private field                   |                 yes |       yes | Heartではselfへ写像        |
| same-class nonvirtual call      |                 yes |       yes | 再帰抽出またはstub         |
| free function call              |                 yes |       yes | direct or inject           |
| global read                     |           yes, warn |       yes | 再現性注意                 |
| global write                    |           yes, warn |       yes | テスト干渉注意             |
| base field                      |             partial |       yes | base self model            |
| virtual call                    |             partial |       yes | dispatch table             |
| `return this`                   |             partial |       yes | self pointerへ型写像       |
| `return *this`                  |             partial |       yes | self referenceへ型写像     |
| `external(this)`                |       no by default |       yes | raw this escape            |
| `reinterpret_cast<char*>(this)` |                  no |       yes | layout依存                 |
| `dynamic_cast` involving this   |       no by default |       yes | RTTI依存                   |
| `typeid(*this)` polymorphic     |       no by default |       yes | 動的型依存                 |
| `delete this`                   |                  no |       yes | lifetime/storage ownership |
| `this->~C()`                    |                  no |       yes | lifetime終了               |
| placement new into `this`       |                  no |       yes | lifetime再開始             |
| bit-field                       |             partial |       yes | 初期版は部分対応外         |
| anonymous union                 |             partial |       yes | active member model必要    |
| constructor                     |      future partial |       yes | init kernel設計が必要      |
| destructor                      |      future partial |       yes | resource意味論注意         |
| template method                 | specialization only |       yes | 具体化単位                 |
| coroutine                       |          no initial |       yes | frame/lifetime複雑         |
| module                          |          no initial |       yes | build integration課題      |

## 3. Fallback vocabulary

| Fallback             | 意味                                         |
| -------------------- | -------------------------------------------- |
| dependency injection | 外部呼び出しをstub/function object化する     |
| recursive extraction | 依存メソッドもHeart化する                    |
| explicit model       | RTTI/identity等をself modelへ明示する        |
| Live mode            | 正規実オブジェクトで元メソッドを呼ぶ         |
| refactor target      | 対象コードを純粋ロジックと外部依存へ分離する |
| test hook            | friend observer/factory等を明示的に追加する  |
| unsupported          | 現在は扱わない                               |

## 4. raw this escape

例:

```cpp
void C::f() {
    registry.add(this);
}
```

分類:

```text
live_required
```

理由:

- external関数が`C*`のidentityを保存するかもしれない。
- 実layoutやvptrにアクセスするかもしれない。
- self pointerを渡しても`C*`ではない。

Fallback:

1. registryをdependency injectionする。
2. self identity modelを使う。
3. Live modeを使う。

dependency化例:

```cpp
struct C_f_deps {
    required_function<void(C_f_self&)> registry_add;
};

void C_f(C_f_self& self, C_f_deps& deps) {
    deps.registry_add(self);
}
```

## 5. dynamic_cast

例:

```cpp
bool C::is_d() const {
    return dynamic_cast<D const*>(this) != nullptr;
}
```

分類:

```text
live_required
```

Fallback:

1. explicit runtime type model
2. Live mode

model例:

```cpp
struct C_is_d_self {
    enum class dynamic_type { C, D, Other } type;
};

bool C_is_d(C_is_d_self const& self) {
    return self.type == C_is_d_self::dynamic_type::D;
}
```

これはRTTIそのものではなく、テスト用モデルである。APIがRTTIを要求するならLive mode。

## 6. typeid(\*this)

polymorphic型の`typeid(*this)`は実動的型に依存する。

Fallback:

- explicit runtime type model
- Live mode

非polymorphic型で静的型だけが問題ならHeart化可能な余地はあるが、初期版では安全側に倒す。

## 7. reinterpret_cast involving this

例:

```cpp
auto bytes = reinterpret_cast<std::byte*>(this);
```

分類:

```text
live_required
```

理由:

- self modelは`C`のobject representationではない。
- padding、alignment、base layout、vptr、ABIが絡む。

Fallback:

- Live mode
- ロジック部分を別メソッドへ切り出す

## 8. delete this

例:

```cpp
void C::release() { delete this; }
```

分類:

```text
live_required
```

理由:

- storage ownershipを操作する。
- self modelは`new C`された実オブジェクトではない。

Fallback:

- Live mode with ownership-aware factory
- deleter dependency model

## 9. placement new into this

例:

```cpp
void C::reset() {
    this->~C();
    new (this) C();
}
```

分類:

```text
live_required
```

理由:

- object lifetimeを明示的に終了・再開始する。
- Heart modeで模倣するにはlifetime state machineが必要。

Fallback:

- Live mode
- 状態リセットロジックをpure functionへ分離

## 10. bit-field

例:

```cpp
class C {
    unsigned flags_ : 3;
public:
    void f(unsigned x) { flags_ = x; }
};
```

分類:

```text
heart_partial_with_modeling
```

理由:

- 代入時の切り詰めや符号が重要。
- layoutは処理系依存要素を含む。

Fallback:

1. selfにも同じbit-field宣言を生成する。
2. explicit bitfield wrapperを使う。
3. Live mode。

初期版ではpartial扱いとし、明示オプションなしでは生成しない。

## 11. anonymous union

例:

```cpp
class C {
    union { int i_; double d_; };
public:
    int f() { return i_; }
};
```

分類:

```text
heart_partial_with_modeling
```

理由:

- active memberを管理する必要がある。
- 読んでよいmemberかどうかが状態依存。

Fallback:

- active tagをselfへ追加
- Live mode

## 12. virtual base

virtual baseは、複数経路で同じbase subobjectを共有する。

Heart modelではshared base identityを明示する必要がある。

Fallback:

- shared pointer/referenceでbase selfを表現
- Live mode

初期版では、単純なvirtual base field readはpartial、layout依存はLive-required。

## 13. template dependent body

テンプレートは具体化されないとbody内の名前解決が完了しない場合がある。

例:

```cpp
template<class T>
auto C::f(T t) { return t.value() + x_; }
```

分類:

```text
unsupported if unspecialized
extractable if concrete specialization resolved
```

Fallback:

- `C::f<int>(int)`のように具体特殊化を指定する。
- buildで観測済みspecializationを列挙する。

## 14. macro complex expression

macroで生成された複雑式は、source spellingとAST生成の対応が難しい場合がある。

分類:

```text
unsupported or extractable if AST regeneration is safe
```

Fallback:

- macro展開後ASTから再生成
- 手動fixture化
- Live mode

## 15. coroutine

例:

```cpp
Task<int> C::f() { co_return x_; }
```

分類:

```text
unsupported initial
```

理由:

- coroutine frameに`this`やlocalが保存される。
- lifetimeとsuspend/resumeが絡む。

Fallback:

- Live mode
- coroutine bodyのpure helperを抽出対象にする

## 16. constructor

constructorは通常メソッドと違い、member initializerとobject lifetime開始が絡む。

初期版分類:

```text
unsupported as target method
```

将来fallback:

- init kernel生成
- constructor bodyだけHeart化
- Live mode

## 17. destructor

destructorは資源解放とlifetime終了が絡む。

初期版分類:

```text
unsupported as target method
```

将来fallback:

- pure state cleanupならHeart destructor kernel
- resource releaseはLive mode推奨

## 18. private nested type

元メソッドがprivate nested typeを使う場合、生成コードから型名にアクセスできないことがある。

Fallback:

1. type erasure
2. dependency化
3. generated codeを元クラスfriendにする明示hook
4. Live mode

## 19. unsupported診断テンプレート

```text
FAIL unsupported
Target: <method>
Location: <file:line:col>
Construct: <construct>
Reason: <why Heart cannot safely lower it>
Fallbacks:
  - <fallback 1>
  - <fallback 2>
Rule: <LR/ADR>
```

## 20. Refactoring suggestions

Aztecaは、可能なら対象コードの小さなリファクタ案も出す。

例:

```cpp
void C::register_me() {
    registry.add(this);
    score_ = compute_score();
}
```

提案:

```cpp
int C::compute_score_only() const { ... }
void C::register_me() {
    registry.add(this);
    score_ = compute_score_only();
}
```

Aztecaは`compute_score_only`をHeart化できる。

## 21. 優先対応表

| 項目                    | 優先度 | 理由                 |
| ----------------------- | -----: | -------------------- |
| base class member       |     高 | 一般的               |
| loops/range-for         |     高 | 一般的               |
| overloaded operator     |     中 | 型依存ロジックで必要 |
| lambda this capture     |     中 | 現代C++で一般的      |
| virtual call dispatch   |     中 | 抽象設計で必要       |
| template specialization |     中 | 必須だが範囲制御必要 |
| bit-field               | 低〜中 | 組込み系で重要       |
| constructor/destructor  |     中 | 別pipelineが必要     |
| coroutine               |     低 | 初期スコープ外       |

## 22. Open questions

1. partial modelingをどの程度自動提案するか。
2. Live-requiredとunsupportedの境界をどれだけ厳密に分けるか。
3. private nested typeへのfriend hook自動生成を許すか。

初期判断:

- partial modelingは診断のみ、明示オプションが必要。
- 実オブジェクトなら正確に試験できるものはLive-required。
- friend hookは自動挿入しない。

---

# File: docs/design/13_unified_extraction_policy.md

# 13. Unified Extraction Policy V3

## 1. 目的

この文書は、アステカを「多くのモードを備えた抽象的なツール」ではなく、利用者にとって単純な **単一の抽出操作** として提供する方針を定義する。

V3では、依存関係の扱いとGoogle Test生成もこの単一方針へ統合する。

## 2. 決定

アステカの公開UXでは、以下を基本方針とする。

```text
1. 公開コマンドは `extract` を中心にする。
2. `--mode heart` や `--mode live` を通常利用者に要求しない。
3. 抽出器は、対象メソッドに必要な意味モデルを自動的に計画する。
4. 依存関係はDependency Transcriptとして表現する。
5. 生成テストはGoogle Testを標準にする。
6. 変換不能性は「利用者が選ぶモード」ではなく「抽出結果の説明」として提示する。
```

標準コマンド:

```bash
azteca extract -p build --method 'ns::Account::withdraw(int)' --out azteca-out
```

## 3. 利用者に見せる概念

公開概念は、最小限にする。

```text
Kernel
  取り出されたメソッドロジック。

Self
  元クラスのうち、対象メソッドのロジックに必要な状態。

Scenario
  self、依存観測、effects、callをまとめたGoogle Test向け入口。

When
  対象メソッドが外界から観測する値を設定するAPI。

Effects
  対象メソッドが外界へ送った要求を検証するAPI。

Report
  何をどう抽出したか、どの観測が必要かを説明する文書。
```

通常利用者に見せない概念:

```text
Heart mode
Live mode
Rich Heart mode
fallback mode
Semantic Envelope internals
MMIR internals
```

## 4. 抽出結果のステータス

モード選択ではなく、結果説明として次を使う。

```text
extracted
  メソッドロジックを直接kernel化できた。

extracted-with-transcript
  主要ロジックをkernel化し、外部依存をquery/effect/operationとして表現した。

extracted-as-effect-model
  this identity、lifetime、dynamic type、byte view などを含む意味モデルとして抽出した。

not-meaningful-for-unit-extraction
  メソッドの本体がほぼ全て外部I/O、未定義動作、inline asm、実ハードウェア操作などで、ロジック単体として取り出す価値が乏しい。
```

## 5. 単純化原則

### 原則1: まず抽出する

分類して終わるのではなく、原則として何らかのテスト可能なkernel/scenarioを生成する。

悪い出力:

```text
this escapes. unsupported.
```

望ましい出力:

```text
this escapes through registry.add(this).
Generated:
  s.self.object_ref()
  s.effects.registry_add.expect_once(s.self.object_ref())
```

### 原則2: 依存fakeを作らせない

悪い出力:

```text
Please implement FakeRepo, FakeClock, FakePolicy, FakeBus.
```

望ましい出力:

```cpp
s.when.repo_load(id).returns(OrderShape{...});
s.when.clock_now().returns(Time{900});
s.when.policy_allow(id).returns(true);
s.effects.bus_publish.expect_once(OrderApproved{id});
```

### 原則3: Google Testで始める

生成テストはGoogle Testで始める。

```cpp
#include <gtest/gtest.h>
#include "generated/account.withdraw.scenario.hpp"

TEST(Account_withdraw, unlocked_path) {
    auto s = azteca_gen::scenario::Account_withdraw{};
    s.self.balance_ = 100;
    s.self.locked_ = false;
    s.when.fee(50).returns(5);

    auto result = s.call(50);

    EXPECT_EQ(result, 45);
    EXPECT_EQ(s.self.balance_, 45);
}
```

### 原則4: 危険な正確性より、安全な抽象化

fake this や ABI hack によって製品コードそのものを呼ぶより、ASTから意味を取り出して安全な抽象化を生成する。

### 原則5: 境界は失敗ではない

外部関数、OS、I/O、未取得の他メソッドは、ユニットテスト上は依存境界である。境界化は逃げではなく、ユニットテストの自然な分離である。

## 6. 生成物の標準形

```text
azteca-out/
  include/
    azteca/
      azteca_runtime.hpp
      azteca_gtest.hpp
    generated/
      target.method.self.hpp
      target.method.shape.hpp
      target.method.ports.hpp
      target.method.scenario.hpp
      target.method.kernel.hpp
  src/
    target.method.kernel.cpp
  tests/
    target.method.sample_test.cpp
    target.method.skeleton_test.cpp
  azteca_report.md
  manifest.json
  CMakeLists.txt
```

## 7. レポート方針

レポートは、利用者に判断を押し付けるものではない。生成されたscenarioで何を書けばよいかを示す。

例:

```text
Extraction result: extracted-with-transcript

Receiver:
  - int balance_
  - bool locked_

Observations:
  - fee(int) -> int

Generated Google Test:
  tests/account.withdraw.sample_test.cpp

Try this:
  s.self.balance_ = 100;
  s.self.locked_ = false;
  s.when.fee(50).returns(5);
  auto result = s.call(50);
  EXPECT_EQ(result, 45);
```

this escapeがある場合:

```text
Extraction result: extracted-as-effect-model

Detected:
  registry.add(this)

Lowered as:
  s.effects.registry_add.expect_once(s.self.object_ref())

Meaning:
  The method's decision to register itself is testable.
  No fake product object was created.
```

## 8. 厳密性の定義

アステカが守る意味は、次の unit-observable semantics とする。

```text
- 戻り値
- 例外
- receiver state
- dependency observations
- external effects
- call order when meaningful
- object identity
- dynamic type decision
- lifetime intent
- byte access intent
```

標準では守らないもの:

```text
- 実アドレス値
- vptr実表現
- padding byteの偶然値
- allocator内部状態
- OS handle実値
- UBの結果
```

## 9. 契約

```text
1. 利用者にmode選択を要求しない。
2. 依存fakeクラス生成を標準にしない。
3. Google Test生成を標準にする。
4. kernel/scenario runtimeはGoogle Test非依存に保つ。
5. 依存はDependency Transcriptとして扱う。
6. 抽出不能を広く認めず、意味モデルを自動拡張する。
7. reportは次に書くべきscenario行を示す。
```

---

# File: docs/design/14_semantic_envelope.md

# 14. Semantic Envelope

## 目的

この文書は、アステカが「より多くのメソッドを、利用者に複雑なモード選択を強いずに取り出す」ための中核概念である **Semantic Envelope** を定義する。

Semantic Envelopeとは、対象メソッドの意味をユニットテスト可能な形で保持するため、`self`、`deps`、`effects`、`object_ref`、`dispatch`、`lifetime`、`byte_view` などを必要最小限だけ自動的に追加する仕組みである。

```text
単純なメソッド:
  self fields だけで足りる。

this のアドレスを使うメソッド:
  object_ref を追加する。

virtual call を含むメソッド:
  dispatch table を追加する。

外部関数を呼ぶメソッド:
  dependency boundary と effect log を追加する。

delete this を含むメソッド:
  lifetime state を追加する。

raw memory view を使うメソッド:
  byte_view または byte boundary を追加する。
```

利用者から見ると、どれも単に「抽出されたkernel」である。

## 基本思想

アステカは、対象メソッドを次のように考える。

```text
method body = receiver state + arguments + dependencies + effects の状態遷移
```

したがって、`this` を実オブジェクトとして偽造する必要はない。必要なのは、対象メソッドが観測・変更する意味を表す受け皿である。

Semantic Envelopeは、その受け皿を自動的に設計する。

## Envelope Components

### 1. Field State

最小の状態モデル。

```cpp
class C {
    int x;
public:
    int f() { return x + 1; }
};
```

生成:

```cpp
struct C_f_self {
    int x;
};

int C_f(C_f_self const& self) {
    return self.x + 1;
}
```

### 2. Base State

基底クラスの状態。

```cpp
struct B { int b; };
struct C : B { int c; };
```

生成:

```cpp
struct C_f_self {
    B_self base_B;
    int c;
};
```

複数継承では、基底ごとに独立したbase slotを持つ。

### 3. Addressable Cells

メンバのアドレス、参照、ポインタ、aliasが必要な場合、単純な値フィールドではなく addressable cell に昇格する。

元コード:

```cpp
int* p = &x;
*p += 1;
return x;
```

生成概念:

```cpp
struct C_f_self {
    azteca::cell<int> x;
};

int C_f(C_f_self& self) {
    auto p = self.x.ref();
    p.get() += 1;
    return self.x.get();
}
```

この昇格は自動で行う。利用者は意識しない。

### 4. Object Identity

`this` の同一性が意味になる場合、実 `C*` ではなく `azteca::object_ref<C>` を使う。

元コード:

```cpp
C* C::self() { return this; }
```

生成:

```cpp
azteca::object_ref<C> C_self(C_self_self& self) {
    return self.object_ref();
}
```

`object_ref<C>` は、実C++オブジェクトを指すポインタではない。テスト世界でのオブジェクト同一性を表す安全な値である。

### 5. Effect Log

外部に何かを登録する、通知する、送信する、といった副作用は effect log に記録できる。

元コード:

```cpp
registry.add(this);
```

生成:

```cpp
deps.registry_add(self.object_ref());
effects.record("registry.add", self.object_ref());
```

ユニットテストでは、実registryを動かさなくても「登録しようとした」ことを検証できる。

### 6. Dependency Boundary

外部関数や未抽出メソッドは依存境界にする。

元コード:

```cpp
return fee(amount) + external_rate();
```

生成:

```cpp
return C_fee(self, deps, effects, amount)
     + deps.external_rate();
```

`external_rate` の戻り値はテストで指定できる。

### 7. Dynamic Type

`dynamic_cast`、polymorphic `typeid`、virtual dispatchに必要な動的型情報。

```cpp
struct C_f_self {
    azteca::type_tag dynamic_type;
    azteca::dispatch_table dispatch;
    // fields...
};
```

単純な型ではこの情報は生成しない。必要になったときだけ追加する。

### 8. Virtual Dispatch

virtual callは、実vtableを偽造せず、明示dispatch tableに変換する。

元コード:

```cpp
return compute(x);
```

`compute` がvirtualなら:

```cpp
return self.dispatch.compute(self.object_ref(), self, deps, effects, x);
```

または、対象動的型が既知なら派生kernelへ静的に解決する。

### 9. Lifetime State

`delete this`、明示デストラクタ呼び出し、placement new on this など、ライフタイム操作の意図を表す。

```cpp
struct C_f_self {
    azteca::lifetime_state lifetime;
};
```

元コード:

```cpp
delete this;
```

生成:

```cpp
C_destructor_kernel(self, deps, effects);
self.lifetime.mark_destroyed();
effects.record_delete(self.object_ref());
```

これは実メモリ解放ではない。ユニットテスト対象としての「自分を破棄する意図」を意味保存する。

### 10. Byte View

オブジェクトのバイト表現を読む処理は、標準では `byte_view` へ抽象化する。

元コード:

```cpp
auto* p = reinterpret_cast<unsigned char*>(this);
return checksum(p, sizeof(C));
```

生成候補:

```cpp
return deps.checksum(self.byte_view(), self.byte_size());
```

または、対象が安全に表現できる場合:

```cpp
return azteca::checksum(self.representation_bytes());
```

実C++オブジェクトのpadding、vptr、ABIレイアウトを偽造しない。

### 11. Global State Model

グローバル変数を読む・書く場合、標準では依存環境に移す。

元コード:

```cpp
return x + global_rate;
```

生成:

```cpp
return self.x + env.global_rate;
```

ただし、既存グローバルをそのまま読む設定も可能にする。標準では再現性を優先する。

## 自動昇格規則

Semantic Envelopeは、構文に応じて自動的に拡張される。

| 検出された構文・意味               | 追加されるEnvelope                             | 生成方針                           |
| ---------------------------------- | ---------------------------------------------- | ---------------------------------- |
| `this->x` / implicit member access | field state                                    | `self.x`                           |
| `&this->x`                         | addressable cell                               | `self.x.ref()`                     |
| reference member access            | addressable cell                               | alias preserving ref               |
| `return this`                      | object identity                                | `self.object_ref()`                |
| `this == other`                    | object identity                                | object_ref comparison              |
| `external(this)`                   | object identity + dependency boundary + effect | `deps.external(self.object_ref())` |
| virtual call                       | dynamic type + dispatch                        | explicit dispatch table            |
| `dynamic_cast`                     | dynamic type                                   | generated type test                |
| `typeid(*this)`                    | dynamic type                                   | generated type info                |
| `delete this`                      | lifetime + destructor kernel + effect          | mark destroyed                     |
| `this->~C()`                       | lifetime + destructor kernel                   | mark destroyed                     |
| placement new on `this`            | lifetime + constructor kernel                  | reinitialize self                  |
| `reinterpret_cast<char*>(this)`    | byte view                                      | representation boundary            |
| `memcpy(this, ...)`                | byte view or lifetime boundary                 | representation mutation            |
| global read/write                  | env/global model                               | `env.name`                         |
| external call                      | deps/effect                                    | generated dependency               |
| unmodeled inline asm               | boundary or not-meaningful                     | report                             |

## 例: this escapeを即unsupportedにしない

元コード:

```cpp
class Node {
    Registry& registry_;
    bool enabled_;

public:
    void activate() {
        if (!enabled_) return;
        registry_.add(this);
    }
};
```

生成:

```cpp
struct Node_activate_self {
    azteca::object_id id;
    bool enabled_;
};

struct Node_activate_deps {
    std::function<void(azteca::object_ref<Node>)> registry_add;
};

void Node_activate(
    Node_activate_self& self,
    Node_activate_deps& deps,
    azteca::effects& effects
) {
    if (!self.enabled_) return;

    auto ref = self.object_ref();
    deps.registry_add(ref);
    effects.record_call("Registry::add", ref);
}
```

Google Test例:

```cpp
TEST(Node_activate, registers_self_when_enabled) {
    auto s = azteca_gen::scenario::Node_activate{};

    s.self.enabled_ = true;
    auto ref = s.self.object_ref();

    s.call();

    s.effects.registry_add.expect_once(ref);
}
```

これは製品の `Registry` 実装を試験するものではない。`Node::activate` が正しい条件で自身を登録しようとするロジックを試験する。

## 例: virtual call

元コード:

```cpp
struct Shape {
    int scale_;
    virtual int area() const = 0;

    int scaled_area() const {
        return area() * scale_;
    }
};
```

生成:

```cpp
struct Shape_scaled_area_self {
    int scale_;
    azteca::object_id id;
    azteca::dispatch_table<Shape> dispatch;
};

int Shape_scaled_area(Shape_scaled_area_self const& self) {
    return self.dispatch.area(self.object_ref()) * self.scale_;
}
```

テストでは `area` を直接与える。

```cpp
TEST(Shape_scaled_area, uses_dispatch_observation) {
    auto s = azteca_gen::scenario::Shape_scaled_area{};

    s.self.scale_ = 3;
    s.when.area(s.self.object_ref()).returns(10);

    EXPECT_EQ(s.call(), 30);
}
```

抽象クラスでも実オブジェクトを作らずに、ロジックを試験できる。

## 例: delete this

元コード:

```cpp
void RefCounted::release() {
    --count_;
    if (count_ == 0) delete this;
}
```

生成:

```cpp
void RefCounted_release(
    RefCounted_release_self& self,
    RefCounted_release_deps& deps,
    azteca::effects& effects
) {
    --self.count_;
    if (self.count_ == 0) {
        RefCounted_destructor(self, deps, effects);
        self.lifetime.mark_destroyed();
        effects.record_delete(self.object_ref());
    }
}
```

Google Test例:

```cpp
TEST(RefCounted_release, marks_destroyed_at_zero) {
    auto s = azteca_gen::scenario::RefCounted_release{};

    s.self.count_ = 1;
    auto ref = s.self.object_ref();

    s.call();

    EXPECT_EQ(s.self.count_, 0);
    EXPECT_TRUE(s.self.lifetime.destroyed());
    s.effects.delete_this.expect_once(ref);
}
```

このテストは、allocatorや実delete演算子ではなく、`release` の分岐ロジックと破棄意図を試験する。

## 意味保存の範囲

Semantic Envelopeによる抽出は、次の意味を保存する。

```text
- 制御フロー
- 値計算
- receiver state更新
- dependency呼び出しの発生、順序、引数、戻り値利用
- object identityに関する比較・返却・外部渡し
- dynamic typeによる分岐
- virtual dispatchの選択点
- lifetime操作の意図
- byte accessの意図、または安全に表現できるbyte値
```

次の意味は標準では保存しない。

```text
- 実メモリアドレスの数値
- 実vtableアドレス
- ABI固有レイアウト
- padding byteの偶然値
- OS資源の実副作用
- 未定義動作の結果
```

## なぜこれが単純化なのか

一見するとSemantic Envelopeは複雑である。しかし、これは利用者に見せる複雑さではない。

```text
悪い単純化:
  対応範囲を狭くし、少し難しい構文でunsupportedにする。

良い単純化:
  利用者には `extract` だけを見せ、内部で必要な意味モデルを自動拡張する。
```

アステカは後者を選ぶ。

## 実装上のInvariant

```text
INV-SE-001:
  Semantic Envelopeは、fake C objectを作ってはならない。

INV-SE-002:
  object_ref<C> は C* へ暗黙変換できてはならない。

INV-SE-003:
  addressable cellは、対象フィールドのC++値カテゴリとconst性を保存する。

INV-SE-004:
  dependency boundaryは、必ずsource locationと元callee情報を持つ。

INV-SE-005:
  effect logは、依存呼び出しの順序を保存する。

INV-SE-006:
  lifetime.mark_destroyed() 後のfield accessは、生成kernel内で診断またはテスト時assert対象にする。

INV-SE-007:
  byte_viewは、実C++オブジェクトレイアウトを偽造しない。
```

## Definition of Done

Semantic Envelopeが設計として成立したと言える条件:

```text
1. this escapeをobject_ref/effect/dependencyへ落とせる。
2. address-taking fieldをaddressable cellへ自動昇格できる。
3. virtual callをdispatch tableへ落とせる。
4. delete thisをlifetime effectへ落とせる。
5. external callを標準でdependency boundaryへ落とせる。
6. どの昇格が起きたかをreportに表示できる。
7. 利用者はそれでも `azteca extract` だけで始められる。
```

---

# File: docs/design/15_method_meaning_ir.md

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

---

# File: docs/design/16_universal_lowering_strategy.md

# 16. Universal Lowering Strategy

## 目的

この文書は、アステカが「例外を狭く保ち、ほとんどのメソッドを何らかの有意義なユニットテスト対象として取り出す」ためのlowering戦略を定義する。

ここでの重要な設計変更は次である。

```text
以前:
  ある構文を検出したら Heart不可 / Live必要 / Unsupported と分類する。

改訂:
  ある構文を検出したら、必要なSemantic Envelopeを追加して抽出を継続する。
  それでもユニットテストとして意味が薄いものだけを例外とする。
```

## 基本アルゴリズム

```text
Input:
  CXXMethodDecl target

Algorithm:
  1. Clang ASTからMMIRを生成する。
  2. MMIRを走査して必要なsemantic featuresを収集する。
  3. 最小Envelopeを計画する。
  4. 変換可能な依存は再帰抽出する。
  5. 変換しない依存はBoundaryCallにする。
  6. receiver modelを生成する。
  7. kernelを生成する。
  8. effect/deps/test scaffold/reportを生成する。

Output:
  Generated unit-testable kernel
```

このアルゴリズムに「モード選択」はない。

## Feature収集

MMIRから以下を収集する。

```text
features:
  uses_fields
  writes_fields
  takes_field_address
  uses_this_identity
  returns_this
  passes_this_to_dependency
  uses_virtual_dispatch
  uses_dynamic_type
  uses_lifetime_operation
  uses_byte_representation
  uses_global_state
  uses_external_call
  uses_exception
  uses_template_dependent_construct
  uses_inline_asm
  uses_coroutine
```

各featureは、Envelope Plannerへ渡される。

## Lowering方針一覧

| Feature             | Lowering                             | 例外条件                                           |
| ------------------- | ------------------------------------ | -------------------------------------------------- |
| field read/write    | `self.field`                         | 型が生成不能な場合はopaque field                   |
| field address       | `cell<T>`                            | pointer arithmeticが任意メモリへ出る場合はboundary |
| this identity       | `object_ref<C>`                      | 実C\*数値そのものが要求される場合はboundary        |
| return this         | `object_ref<C>`戻り                  | 元シグネチャ維持が必須ならadapter生成              |
| pass this           | dependencyに`object_ref<C>`を渡す    | calleeも抽出可能ならcalleeも変換                   |
| virtual call        | dispatch table                       | dispatch先が不明でもstub dispatch                  |
| dynamic_cast        | type_tag test                        | private inheritance等は型グラフに基づき診断        |
| delete this         | lifetime effect                      | 実allocator検証は対象外                            |
| destructor call     | destructor kernel                    | 未抽出資源解放はboundary                           |
| placement new       | constructor kernel + lifetime reinit | raw storage layout依存はboundary                   |
| global read/write   | env model                            | 実global使用も設定可能                             |
| external call       | dependency boundary                  | 戻り値はtest sideで供給                            |
| byte representation | byte_view/boundary                   | ABI正確性は標準対象外                              |
| inline asm          | boundary                             | 制御支配ならnot-meaningful                         |
| coroutine           | coroutine state model or boundary    | 初期実装ではboundary                               |

## Direct Field Lowering

元コード:

```cpp
int C::f(int a) {
    x += a;
    return x;
}
```

MMIR:

```text
Assign(FieldRef(x), BinaryOp(+, FieldRef(x), Arg(a)))
Return(FieldRef(x))
```

生成:

```cpp
int C_f(C_f_self& self, int a) {
    self.x += a;
    return self.x;
}
```

## Addressable Field Lowering

元コード:

```cpp
int C::f() {
    int& r = x;
    r += 1;
    return x;
}
```

Envelope:

```text
x is addressable
```

生成:

```cpp
struct C_f_self {
    azteca::cell<int> x;
};

int C_f(C_f_self& self) {
    auto r = self.x.ref();
    r.get() += 1;
    return self.x.get();
}
```

最適化として、addressを取らないフィールドは通常値のままでよい。

## this Identity Lowering

元コード:

```cpp
bool C::is_same(C* other) const {
    return this == other;
}
```

生成シグネチャ:

```cpp
bool C_is_same(C_is_same_self const& self, azteca::object_ref<C> other) {
    return self.object_ref() == other;
}
```

元引数 `C*` はテスト世界では `object_ref<C>` に写像する。

重要:

```text
object_ref<C> は C* ではない。
C* を偽造しない。
```

## this Escape Lowering

元コード:

```cpp
void C::register_me() {
    registry.add(this);
}
```

callee `Registry::add(C*)` が抽出可能な場合:

```cpp
Registry_add(registry_self, deps, effects, self.object_ref());
```

calleeが抽出不能または外部の場合:

```cpp
deps.registry_add(self.object_ref());
effects.record_call("Registry::add", self.object_ref());
```

これにより、`this` escapeは原則として抽出継続可能になる。

## Return this Lowering

元コード:

```cpp
C* C::owner() { return this; }
```

標準生成:

```cpp
azteca::object_ref<C> C_owner(C_owner_self& self) {
    return self.object_ref();
}
```

互換adapter:

```cpp
// 生成しないのが標準。必要な場合のみ明示設定。
C* C_owner_live_adapter(C& obj) {
    return obj.owner();
}
```

ユニットテストでは、object_refで同一性を検査する。

## Virtual Call Lowering

元コード:

```cpp
int Base::score() const {
    return raw_score() * weight_;
}
```

`raw_score` がvirtualなら:

```cpp
int Base_score(Base_score_self const& self) {
    return self.dispatch.raw_score(self.object_ref()) * self.weight_;
}
```

Google Test例:

```cpp
TEST(Base_score, uses_dispatch_observation) {
    auto s = azteca_gen::scenario::Base_score{};

    s.self.weight_ = 2;
    s.when.raw_score(s.self.object_ref()).returns(10);

    EXPECT_EQ(s.call(), 20);
}
```

この方式により、抽象クラスでも単純に試験できる。

## Dynamic Cast Lowering

元コード:

```cpp
if (auto* d = dynamic_cast<Derived*>(this)) {
    return d->special_;
}
return 0;
```

生成概念:

```cpp
if (self.dynamic_type.is_a<Derived>()) {
    auto d = self.as<Derived>();
    return d.special_;
}
return 0;
```

必要なEnvelope:

```text
- dynamic_type
- type graph
- derived self view
```

初期実装では、同一抽出対象内で必要な派生フィールドをselfに含める。

```cpp
struct Base_f_self {
    azteca::type_tag dynamic_type;
    std::optional<Derived_view> derived_Derived;
};
```

## Lifetime Lowering

### delete this

元コード:

```cpp
void C::release() {
    if (--refs_ == 0) delete this;
}
```

生成:

```cpp
void C_release(C_release_self& self, C_release_deps& deps, azteca::effects& effects) {
    if (--self.refs_ == 0) {
        C_destructor(self, deps, effects);
        self.lifetime.mark_destroyed();
        effects.record_delete(self.object_ref());
    }
}
```

### placement new on this

元コード:

```cpp
new (this) C(42);
```

生成:

```cpp
C_constructor_from_int(self, deps, effects, 42);
self.lifetime.mark_live();
effects.record_reinitialize(self.object_ref());
```

実ストレージへのplacement newではなく、self状態の再初期化として扱う。

## External Call Lowering

外部関数呼び出しは、次の優先順で扱う。

```text
1. 関数本体があり、抽出対象として安全なら再帰抽出する。
2. 本体がない、または境界として扱うべきならdependency boundaryにする。
3. 呼び出しの発生と引数をeffect logへ記録する。
4. 戻り値が必要ならdepsから受け取る。
```

元コード:

```cpp
int C::f(int x) {
    return normalize(x) + 1;
}
```

生成:

```cpp
int C_f(C_f_self& self, C_f_deps& deps, azteca::effects& effects, int x) {
    auto v = deps.normalize(x);
    effects.record_call("normalize", x, v);
    return v + 1;
}
```

## Global State Lowering

元コード:

```cpp
extern int threshold;

bool C::ok() const {
    return value_ >= threshold;
}
```

生成:

```cpp
struct C_ok_env {
    int threshold;
};

bool C_ok(C_ok_self const& self, C_ok_env const& env) {
    return self.value_ >= env.threshold;
}
```

これにより、ユニットテストでglobal stateを明示できる。

## Byte Representation Lowering

元コード:

```cpp
int C::hash() const {
    return hash_bytes(reinterpret_cast<unsigned char const*>(this), sizeof(C));
}
```

標準生成:

```cpp
int C_hash(C_hash_self const& self, C_hash_deps& deps) {
    return deps.hash_bytes(self.byte_view());
}
```

`byte_view`の生成方針:

```text
- field-based deterministic representation
- user-supplied representation provider
- dependency boundary
```

標準ではABI実レイアウトを偽造しない。

## Lambda Lowering

元コード:

```cpp
auto f = [this](int x) { return x + bias_; };
return f(10);
```

生成:

```cpp
auto f = [&](int x) { return x + self.bias_; };
return f(10);
```

this captureはself captureへ変換する。lambdaが外部へ逃げる場合は、closureをdependency/effectとして表す。

## Template Lowering

テンプレートは、具体化単位で扱う。

```cpp
template<class T>
T C::f(T x) { return x + bias_; }
```

抽出対象:

```text
C::f<int>(int)
C::f<double>(double)
```

未具体化テンプレートを一つの生成kernelとして万能に扱うことは標準範囲外とする。ただし、将来的にはtemplate-preserving codegenを検討できる。

## Coroutine Lowering

初期実装では、coroutineはdependency boundaryまたはnot-meaningful候補とする。ただし設計上は次の方向を持つ。

```text
co_await / co_yield / co_return
  -> coroutine state self
  -> scheduler dependency
  -> suspension/resumption effect log
```

これは大きな拡張なので、v1以降に回す。

## Inline Assembly

inline asmは原則としてboundaryにする。

```cpp
asm volatile("...");
```

生成:

```cpp
deps.inline_asm_boundary("source-span-id");
effects.record_inline_asm("source-span-id");
```

ただし、asmが出力値を生成し、その値が制御フローを支配する場合、ユニットテストではdepsから値を供給する。

## Not Meaningful Criteria

次の場合のみ、抽出を停止して `not-meaningful-for-unit-extraction` とする。

```text
1. メソッド本体の実質すべてが未モデル化外部環境の操作であり、ロジックが存在しない。
2. 元コードがUBに依存している。
3. AST上に対象メソッド本体が存在しない。
4. すべての出力がinline asmまたは実ハードウェア状態だけにより決まる。
5. 生成kernelが元メソッドのユニットテスト価値を誤認させる危険が高い。
```

この基準は狭く運用する。

## Loweringの正しさ検査

各loweringは次のテストを持つ。

```text
1. MMIR期待値テスト
2. Envelope Plan期待値テスト
3. 生成コードsnapshotテスト
4. 生成コードコンパイルテスト
5. 実行テスト
6. 可能なら製品オブジェクトとの差分検証
```

## Definition of Done

```text
1. this escapeが標準で抽出継続される。
2. external callが標準でdeps/effectsに変換される。
3. virtual callがdispatch tableに変換される。
4. address-taken fieldsがcellへ昇格する。
5. lifetime操作がlifetime effectへ変換される。
6. byte representationは安全なbyte_viewまたはboundaryへ変換される。
7. 利用者はこれらをモード選択なしに使える。
```

---

# File: docs/design/17_product_experience_and_governance.md

# 17. Product Experience and Governance V3

## 1. 目的

この文書は、アステカを「理論的には面白いが実務で扱いづらいツール」にしないためのUX・運用・保守方針を定義する。

V3では、特に次を強化する。

```text
- 利用者にmode選択を要求しない。
- 依存fakeクラスを大量に書かせない。
- 生成テストはGoogle Testでそのまま動く。
- テスト経路ごとに必要な観測だけを書く。
```

## 2. プロダクト原則

### PX-001: One Command First

最初の体験は必ず1コマンドである。

```bash
azteca extract -p build --method 'C::m(int)'
```

出力:

```text
- kernel
- self
- shape
- scenario
- Google Test sample
- report
- manifest
```

### PX-002: No Mode Tax

利用者に抽象的なmode選択を要求しない。

悪いUX:

```bash
azteca extract --mode rich-heart --identity-model object --dispatch stub --lifetime symbolic
```

良いUX:

```bash
azteca extract -p build --method 'C::m(int)'
```

内部で必要なSemantic Envelopeを追加する。

### PX-003: No Fake Class Tax

依存クラスのfakeを大量に作らせない。

悪いUX:

```cpp
class FakeRepo : public Repo { ... };
class FakeClock : public Clock { ... };
class FakePayment : public Payment { ... };
```

良いUX:

```cpp
s.when.repo_load(id).returns(OrderShape{...});
s.when.clock_now().returns(Time{900});
s.effects.payment_reserve.expect_once(Money{5000});
```

### PX-004: Google Test First

標準生成テストはGoogle Testである。

```cpp
#include <gtest/gtest.h>
#include "C_m.scenario.hpp"

TEST(C_m, sample) {
    auto s = azteca_gen::scenario::C_m{};
    auto result = s.call(/* args */);
    EXPECT_EQ(result, /* expected */);
}
```

独自runnerは標準にしない。

### PX-005: Boundary is Normal

外部依存境界は失敗ではない。生成テストの自然な一部である。

```text
Azteca extracted the method logic.
It generated:
  - receiver state for 3 fields
  - 2 dependency observations
  - 1 observable effect
  - a Google Test sample
```

### PX-006: Reports Explain, They Do Not Demand

レポートは、利用者に内部設計用語を押し付けない。

悪いレポート:

```text
Classification: rich-heart-with-identity-and-boundary-and-transcript
```

良いレポート:

```text
Azteca extracted the method logic.
To test the success path, provide these observations:
  - repo_load(id) -> OrderShape
  - policy_allow(id) -> bool

The method may produce these effects:
  - notifier_send(id)
```

### PX-007: Missing Query Must Be Helpful

未設定queryに到達したら、単に例外を投げるのではなく、次に書くべきテスト行を示す。

```text
Missing observation: policy_allow(Id) -> bool
Suggested:
  s.when.policy_allow(id).returns(true);
```

### PX-008: Generated Tests Must Compile

生成サンプルテストは、そのままCMakeでビルドできることを原則とする。

```text
- void effectは標準で記録する。
- non-void queryはsample内にplaceholderまたは代表値を置く。
- placeholderが不可能な型はコメント付きskeletonとして生成する。
```

## 3. 利用者が覚える概念

V3で利用者が覚える概念は、最大でも次に抑える。

```text
self:
  対象メソッドが読む/書くreceiver状態。

scenario:
  self、依存観測、effects、callをまとめたテスト用オブジェクト。

when:
  外界から対象メソッドへ与える観測値。

effects:
  対象メソッドが外界へ送った要求の記録。
```

内部概念:

```text
MMIR
Semantic Envelope
object_ref
addressable cell
dispatch table
type_tag
lifetime_state
byte_view
Dependency Transcript
```

内部概念は必要なときだけreportに出す。

## 4. 標準生成テストの形

```cpp
#include <gtest/gtest.h>
#include "OrderService_approve.scenario.hpp"

TEST(OrderService_approve, success_path) {
    auto s = azteca_gen::scenario::OrderService_approve{};

    auto id = OrderId{10};
    s.self.user_id = UserId{42};

    s.when.repo_load(id).returns(OrderShape{
        .deadline = Time{1000},
        .amount = Money{5000},
    });
    s.when.clock_now().returns(Time{900});
    s.when.policy_can_approve().returns(true);
    s.when.risk_score(Money{5000}, UserId{42}).returns(20);

    auto result = s.call(id);

    EXPECT_EQ(result, OK);
    s.effects.payment_reserve.expect_once(Money{5000});
    s.effects.repo_mark_approved.expect_once(id);
    s.effects.bus_publish.expect_once(OrderApproved{id});
    s.effects.expect_no_unexpected_calls();
}
```

## 5. Report Layout

`azteca_report.md` は次の構成にする。

```text
1. Summary
2. Generated Google Test entry point
3. What was extracted
4. How to test this method
5. Receiver state
6. Required observations by path
7. Observable effects
8. Generated shapes
9. Object identity notes
10. Source mapping
11. Limitations
12. Suggested next tests
```

## 6. Inspect出力例

```text
Azteca can extract OrderService::approve(OrderId).

Generated Google Test:
  tests/OrderService_approve.sample_test.cpp

Receiver state:
  - UserId user_id

Generated shapes:
  OrderShape
    - Time deadline
    - Money amount

Dependency observations:
  query repo_load(OrderId) -> optional<OrderShape>
  query clock_now() -> Time
  query policy_can_approve(UserId, OrderShape) -> bool
  query risk_score(Money, UserId) -> int

Observable effects:
  payment_reserve(Money)
  repo_mark_approved(OrderId)
  bus_publish(OrderApproved)

Path-wise test burden:
  not_found:
    queries: repo_load
    effects: none
  expired:
    queries: repo_load, clock_now
    effects: none
  success:
    queries: repo_load, clock_now, policy_can_approve, risk_score
    effects: payment_reserve, repo_mark_approved, bus_publish
```

## 7. Configuration Philosophy

設定ファイルは小さく保つ。

```yaml
methods:
  - ns::OrderService::approve(OrderId)

test:
  framework: googletest

boundaries:
  default: transcript

naming:
  namespace: azteca_gen
```

詳細設定は可能だが、標準では不要にする。

```yaml
boundaries:
  Repo::load:
    strategy: query
    shape: OrderShape
  Payment::reserve:
    strategy: operation
  Audit::write:
    strategy: effect
  Math::clamp:
    strategy: recursive
```

## 8. Governance Rules

```text
1. 新機能は、利用者概念を増やさないことを第一に検討する。
2. mode名を公開CLIへ増やす場合はADR必須。
3. 依存fakeクラス生成を標準にする提案はADR必須。
4. Google Testから独自runnerへ標準変更する場合はADR必須。
5. 未設定queryの暗黙デフォルト値は禁止。
6. 生成テストがコンパイルできない変更は禁止。
7. reportは内部分類名ではなく、次に何を書けばよいかを示す。
```

---

# File: docs/design/19_correctness_model.md

# 19. Correctness Model

## 目的

この文書は、アステカが生成するkernelの正しさをどのように定義し、検証するかを定める。

アステカは、製品メソッドの実バイナリをそのまま呼ぶツールではない。ASTからメソッド本体の意味を取り出し、ユニットテスト可能な形に変換するツールである。そのため、正しさの定義を曖昧にしてはならない。

## 正しさの基本定義

アステカの正しさは、次の対応で定義する。

```text
Concrete world:
  正規に構築された C++ オブジェクトと実行環境

Azteca world:
  self + deps + effects + env + runtime abstractions

Abstraction:
  concrete object state を self に写像する

Unit-observable equivalence:
  対象メソッドのユニットテスト上観測すべき振る舞いが一致する
```

形式的には、対象メソッド `C::m(args...)` と生成kernel `C_m(self, deps, effects, args'...)` について、次を目指す。

```text
alpha(before_concrete_state) = before_self
argument_mapping(args) = args'

after executing C::m:
  result_concrete
  after_concrete_state
  observable_effects_concrete

after executing generated kernel:
  result_azteca
  after_self
  effects_azteca

Then:
  result_mapping(result_concrete) = result_azteca
  alpha(after_concrete_state) = after_self
  effect_mapping(observable_effects_concrete) = effects_azteca
```

ただし、外部依存境界は `deps` の契約に従う。

## Unit-Observable Semantics

アステカが標準で保存する意味:

```text
1. 戻り値
2. 例外の発生と種類
3. receiver field state の更新
4. base subobject state の更新
5. local logic の制御フロー
6. dependency call の発生、順序、引数、戻り値の利用
7. object identity の比較、返却、外部渡し
8. virtual dispatch の選択点
9. dynamic type による分岐
10. lifetime operation の意図
11. global/env state の読み書き
12. byte representation access の意図、または安全な抽象表現
```

標準で保存しない意味:

```text
1. 実メモリアドレスの数値
2. vptrの実表現
3. padding byteの偶然値
4. ABI固有のpointer-to-member表現
5. allocator内部状態
6. OS handle実値
7. 未定義動作の結果
```

## Soundness Principle

アステカは、変換の正確性を過剰に主張しない。

```text
SP-001:
  意味を保存できる場合はkernelへloweringする。

SP-002:
  意味を安全な抽象化で保存できる場合はSemantic Envelopeへloweringする。

SP-003:
  自前で意味を保存できないが、ユニットテスト上依存として扱える場合はBoundaryCallにする。

SP-004:
  BoundaryCallも不適切な場合はnot-meaningful-for-unit-extractionとする。

SP-005:
  どの場合もfake thisや未開始ライフタイム呼び出しを使わない。
```

## Rule Proof Obligation

各lowering ruleは、以下の証明責務を持つ。

```text
1. 対象AST/MMIR条件
2. 生成されるMMIR/Envelope
3. 保存する意味
4. 保存しない意味
5. boundaryが発生する場合の契約
6. negative case
7. テストfixture
```

例: `this escape` lowering

```text
対象:
  C* 型として this が外部依存に渡される。

生成:
  object_ref<C> をdepsへ渡す。

保存する意味:
  対象メソッドが自分自身を依存へ渡したこと。
  呼び出し順序。
  他引数。

保存しない意味:
  実C*の数値。
  依存側がCの実レイアウトを読む挙動。

契約:
  depsはobject_ref<C>を受け取り、テスト側が期待を検証する。
```

## Boundary Contract

BoundaryCallは、次の契約を持つ。

```text
- 元calleeの名前、型、source locationを保持する。
- 引数はAzteca worldに写像される。
- 戻り値はテスト側またはdefault stubから供給される。
- 呼び出しはeffectsに記録できる。
- 境界化されたcalleeの内部挙動は、対象kernelの正しさ範囲外である。
```

BoundaryCallは、ユニットテストにおけるmock/stubと同じ役割を持つ。ただし、アステカはsource mapとeffect logにより、どの依存が境界になったかを透明にする。

## Object Identity Contract

`object_ref<C>` は、次を保証する。

```text
- 同じselfから得たobject_refは等しい。
- 異なるobject_idから得たobject_refは異なる。
- object_ref<C>はC*へ暗黙変換されない。
- object_ref<C>は実C++オブジェクトのライフタイムを意味しない。
- object_refはユニットテスト世界の同一性を表す。
```

これにより、`return this`、`this == other`、`external(this)` を安全に試験できる。

## Lifetime Contract

`lifetime_state` は、実メモリ管理ではなく、メソッドのライフタイム意図を表す。

```text
- live
- destroying
- destroyed
- reinitialized
```

`delete this` loweringは、次を行う。

```text
1. destructor kernelを呼ぶ。可能な場合。
2. lifetimeをdestroyedへ変更する。
3. effectsにdelete意図を記録する。
```

保存する意味:

```text
- どの条件で破棄を意図したか
- 破棄前にどの状態更新が起きたか
- destructor logicが抽出されていればそのロジック
```

保存しない意味:

```text
- 実operator deleteのallocator挙動
- 実メモリ解放
```

## Byte View Contract

`byte_view` は、実ABIレイアウトの偽造ではない。

方針:

```text
- フィールドから決定的な表現を作れる場合はrepresentation_bytesを生成する。
- ABI正確性が必要な場合はboundaryにする。
- paddingやvptrに依存する意味は標準kernelでは保存しない。
```

この契約を破ると、アステカはfake objectに近づいてしまうため禁止する。

## Differential Validation

可能な場合、生成kernelと実オブジェクト呼び出しの差分検証を行う。

ただし、差分検証は抽出方式ではなく、検証補助である。

```text
1. テスト用factoryで正規Cオブジェクトを作る。
2. alphaでselfへ写像する。
3. 同じ入力で実メソッドとkernelを実行する。
4. result/state/effectsを比較する。
```

比較できないもの:

```text
- external dependencyの実副作用
- allocator内部状態
- OS handle
- timing
- thread scheduling
```

## Regression Strategy

各ruleに対して以下を保つ。

```text
- MMIR snapshot
- Envelope Plan snapshot
- generated code snapshot
- compile test
- runtime test
- report snapshot
```

snapshotは過剰に細かくしすぎない。意味のある単位で固定する。

## No Silent Unsoundness

アステカ最大の失敗は、抽出不能なものを抽出できたように見せることである。

禁止:

```text
- 実C*が必要な依存にobject_refを渡したことを隠す
- byte_viewがABI正確であるように見せる
- dynamic dispatchを固定したのにreportしない
- dependency戻り値の仮定をreportしない
- lifetime effectを実deleteとして説明する
```

## Correctness Checklist

生成時に次をチェックする。

```text
1. 裸のthisが生成コードに残っていないか。
2. reinterpret_cast<C*>が生成されていないか。
3. object_refがC*へ変換されていないか。
4. BoundaryCallがmanifestに記録されているか。
5. effectsが呼び出し順序を保存しているか。
6. dynamic dispatchが固定化された場合、reportされているか。
7. lifetime操作が実メモリ操作として実装されていないか。
8. byte_viewがABI正確性を主張していないか。
```

## Definition of Done

```text
1. unit-observable semanticsが文書化されている。
2. 各lowering ruleにproof obligationがある。
3. Boundary/Object/Lifetime/Byteの契約が明確である。
4. no silent unsoundness checklistが実装されている。
5. 差分検証は任意の検証補助として位置づけられている。
```

---

# File: docs/design/20_runtime_contract.md

# 20. Runtime Contract V3

## 1. 目的

この文書は、生成kernelと生成Google Testを支える `azteca_runtime.hpp` の契約を定義する。

ランタイムの目的は、C++オブジェクトを偽造することではない。対象メソッドのユニットテスト可能な意味を安全に表現することである。

## 2. Runtime Components

```text
azteca::scenario_base
azteca::query<Sig>
azteca::effect<Args...>
azteca::operation<Sig>
azteca::missing_observation
azteca::object_id
azteca::object_ref<T>
azteca::cell<T>
azteca::ref<T>
azteca::dispatch_table<T>
azteca::type_tag
azteca::lifetime_state
azteca::byte_view
azteca::result<T>
```

Google Test依存のadapterは別ヘッダに分ける。

```text
azteca_gtest.hpp
```

`azteca_runtime.hpp` 自体はGoogle Testに依存しない。

## 3. scenario

生成scenarioは、対象メソッドのテスト入口である。

```cpp
namespace azteca_gen::scenario {

struct C_m {
    C_m_self self;
    C_m_when when;
    C_m_effects effects;

    R call(Args... args);
};

}
```

契約:

```text
- scenarioはself、when、effectsを1つにまとめる。
- call()は生成kernelを呼ぶ。
- call()はfake C objectを作らない。
- queryが未設定ならmissing_observationを発生させる。
- effectは標準で記録する。
```

## 4. query

`query<R(Args...)>` は、外界から対象メソッドが観測する戻り値を供給する。

```cpp
namespace azteca {

template<class Sig>
class query;

template<class R, class... Args>
class query<R(Args...)> {
public:
    auto returns_for(Args... args, R value) -> void;
    auto returns(R value) -> void; // 引数を区別しない簡易設定
    R call(Args... args);

    void expect_not_called() const;
    std::size_t call_count() const noexcept;
};

}
```

生成Scenario APIでは、より読みやすい形を出す。

```cpp
s.when.repo_load(id).returns(OrderShape{...});
s.when.clock_now().returns(Time{900});
```

未設定queryに到達した場合:

```cpp
throw azteca::missing_observation{...};
```

暗黙の `false`、`0`、`std::nullopt` などは返してはならない。

## 5. effect

`effect<Args...>` は、外界へ送る要求を記録する。

```cpp
namespace azteca {

template<class... Args>
class effect {
public:
    void record(Args... args);
    void expect_once(Args... args) const;
    void expect_none() const;
    std::size_t call_count() const noexcept;
};

}
```

Google Test非依存runtimeでは、`expect_once` は例外またはruntime assertionを使える。
`azteca_gtest.hpp` をincludeした生成テストでは、Google Test failureとして報告できるadapterを使う。

## 6. operation

`operation<R(Args...)>` は、戻り値供給と効果記録の両方を行う。

```cpp
namespace azteca {

template<class Sig>
class operation;

template<class R, class... Args>
class operation<R(Args...)> {
public:
    auto returns_for(Args... args, R value) -> void;
    R call(Args... args);

    void expect_once(Args... args) const;
    std::size_t call_count() const noexcept;
};

}
```

## 7. missing_observation

```cpp
namespace azteca {

class missing_observation : public std::exception {
public:
    std::string_view port_name() const noexcept;
    std::string_view source_file() const noexcept;
    int source_line() const noexcept;
    std::string_view suggested_scenario_line() const noexcept;
};

}
```

診断例:

```text
Missing observation for policy_can_approve(UserShape, OrderShape) -> bool
Source: order_service.cpp:31
Suggested: s.when.policy_can_approve(/* user */, /* order */).returns(true);
```

## 8. object_id

`object_id` は、テスト世界におけるオブジェクト同一性を表す。

```cpp
namespace azteca {

class object_id {
public:
    static object_id fresh();

    bool operator==(object_id const&) const noexcept;
    bool operator!=(object_id const&) const noexcept;
};

}
```

契約:

```text
- fresh()は一意なidを返す。
- object_idは実アドレスではない。
- object_idはC++オブジェクトライフタイムを持たない。
```

## 9. object_ref<T>

`object_ref<T>` は、`this` や依存戻り値の同一性をテスト世界で表す。

```cpp
namespace azteca {

template<class T>
class object_ref {
    object_id id_;
public:
    explicit object_ref(object_id id) noexcept;
    object_id id() const noexcept;

    bool operator==(object_ref const&) const noexcept;
    bool operator!=(object_ref const&) const noexcept;
};

}
```

禁止:

```cpp
operator T*();
operator void*();
T& get();
```

`object_ref` は実オブジェクトを指さない。

## 10. cell<T> and ref<T>

`cell<T>` は、フィールドやローカル変数がaddressableになる場合の値格納である。

```cpp
namespace azteca {

template<class T>
class cell {
public:
    cell();
    explicit cell(T value);

    T& get();
    T const& get() const;
    ref<T> ref();
    ref<T const> cref() const;
    void set(T value);
};

template<class T>
class ref {
public:
    T& get() const;
};

}
```

未構築の `T` を偽装しない。

## 11. effects aggregation

生成scenarioは個別effectを持つが、全体検証のため集約APIも提供する。

```cpp
s.effects.expect_no_unexpected_calls();
s.effects.expect_none();
s.effects.trace();
```

traceはrecord/replay、diagnostics、golden comparisonに使える。

## 12. dispatch_table and type_tag

virtual callやdynamic typeは、必要に応じてruntimeモデルを使う。

```cpp
struct Shape_area_dispatch {
    azteca::query<int(azteca::object_ref<Shape>)> area;
};
```

`type_tag` はテスト世界の型タグであり、実C++ RTTIを標準では要求しない。

## 13. Google Test Adapter

`azteca_gtest.hpp` は、runtime診断をGoogle Test failureへ変換する。

```cpp
#include <gtest/gtest.h>
#include <azteca/azteca_gtest.hpp>

AZTECA_EXPECT_EFFECT_ONCE(s.effects.bus_publish, OrderApproved{id});
AZTECA_EXPECT_NO_UNEXPECTED_EFFECTS(s.effects);
AZTECA_EXPECT_MISSING_OBSERVATION(s.call(id), "policy_can_approve");
```

ただし、標準生成テストは可能な限り通常のGoogle Test assertionを使う。

```cpp
EXPECT_EQ(result, OK);
s.effects.bus_publish.expect_once(OrderApproved{id});
```

## 14. 契約

```text
1. runtimeはfake C++ objectを作らない。
2. kernelはGoogle Test非依存にする。
3. scenario runtimeも原則Google Test非依存にする。
4. Google Test adapterは薄く保つ。
5. queryの暗黙デフォルト値は禁止。
6. effectは記録され、assert可能である。
7. object_refは実ポインタへ変換できない。
8. generated Google Testはscenario APIを通じてkernelを試験する。
```

---

# File: docs/design/21_end_to_end_examples.md

# 21. End-to-End Examples V3

## 1. 目的

この文書は、Azteca V3がどのようにメソッドの心臓ロジックを抽出し、Google Testでユニットテスト可能にするかを例で示す。

## 2. 単純な状態更新

元コード:

```cpp
class Account {
    int balance_;
    bool locked_;

public:
    int withdraw(int amount) {
        if (locked_) return -1;
        balance_ -= amount;
        return balance_;
    }
};
```

生成self:

```cpp
struct Account_withdraw_self {
    int balance_{};
    bool locked_{};
};
```

生成kernel:

```cpp
int Account_withdraw(Account_withdraw_self& self, int amount) {
    if (self.locked_) return -1;
    self.balance_ -= amount;
    return self.balance_;
}
```

生成Google Test:

```cpp
TEST(Account_withdraw, unlocked_subtracts_amount) {
    auto s = azteca_gen::scenario::Account_withdraw{};

    s.self.balance_ = 100;
    s.self.locked_ = false;

    auto result = s.call(40);

    EXPECT_EQ(result, 60);
    EXPECT_EQ(s.self.balance_, 60);
    s.effects.expect_none();
}
```

## 3. helper依存

元コード:

```cpp
class Account {
    int balance_;
    int fee(int amount) const { return amount / 10; }
public:
    int withdraw(int amount) {
        balance_ -= amount + fee(amount);
        return balance_;
    }
};
```

private pure helperは標準で再帰抽出される。

```cpp
int Account_fee(Account_withdraw_self const& self, int amount) {
    return amount / 10;
}

int Account_withdraw(Account_withdraw_self& self, int amount) {
    self.balance_ -= amount + Account_fee(self, amount);
    return self.balance_;
}
```

Google Test:

```cpp
TEST(Account_withdraw, includes_fee_logic) {
    auto s = azteca_gen::scenario::Account_withdraw{};

    s.self.balance_ = 100;

    auto result = s.call(50);

    EXPECT_EQ(result, 45);
    EXPECT_EQ(s.self.balance_, 45);
}
```

## 4. 外部query依存

元コード:

```cpp
int PriceService::discounted(ItemId id) {
    auto price = repo_.price(id);
    auto discount = campaign_.discount(id);
    return price - discount;
}
```

生成scenario:

```cpp
TEST(PriceService_discounted, computes_from_observations) {
    auto s = azteca_gen::scenario::PriceService_discounted{};

    auto id = ItemId{10};
    s.when.repo_price(id).returns(1000);
    s.when.campaign_discount(id).returns(150);

    auto result = s.call(id);

    EXPECT_EQ(result, 850);
}
```

`Repo` や `Campaign` のfake classは不要である。

## 5. 早期returnと経路ごとの依存

元コード:

```cpp
int Service::handle(Id id) {
    if (!enabled_) return DISABLED;
    if (!repo_.exists(id)) return NOT_FOUND;
    notifier_.send(id);
    return OK;
}
```

`DISABLED` 経路:

```cpp
TEST(Service_handle, disabled_does_not_touch_dependencies) {
    auto s = azteca_gen::scenario::Service_handle{};

    s.self.enabled_ = false;

    auto result = s.call(Id{1});

    EXPECT_EQ(result, DISABLED);
    s.when.repo_exists.expect_not_called();
    s.effects.notifier_send.expect_none();
}
```

成功経路:

```cpp
TEST(Service_handle, success_sends_notification) {
    auto s = azteca_gen::scenario::Service_handle{};

    auto id = Id{1};
    s.self.enabled_ = true;
    s.when.repo_exists(id).returns(true);

    auto result = s.call(id);

    EXPECT_EQ(result, OK);
    s.effects.notifier_send.expect_once(id);
}
```

## 6. Shape生成

元コード:

```cpp
int OrderService::check(OrderId id) {
    auto order = repo_.load(id);
    if (!order) return NOT_FOUND;
    if (order->deadline() < clock_.now()) return EXPIRED;
    return order->amount().value();
}
```

生成shape:

```cpp
struct OrderShape {
    Time deadline;
    Money amount;
};
```

Google Test:

```cpp
TEST(OrderService_check, returns_amount_before_deadline) {
    auto s = azteca_gen::scenario::OrderService_check{};

    auto id = OrderId{10};
    s.when.repo_load(id).returns(OrderShape{
        .deadline = Time{1000},
        .amount = Money{5000},
    });
    s.when.clock_now().returns(Time{900});

    auto result = s.call(id);

    EXPECT_EQ(result, 5000);
}
```

本物の `Order` は構築しない。

## 7. this escape

元コード:

```cpp
void Component::registerSelf() {
    registry_.add(this);
}
```

生成kernelは実 `Component*` を作らない。

```cpp
void Component_registerSelf(Component_registerSelf_self& self,
                            Component_registerSelf_ports& ports) {
    ports.registry_add.record(self.object_ref());
}
```

Google Test:

```cpp
TEST(Component_registerSelf, registers_self_identity) {
    auto s = azteca_gen::scenario::Component_registerSelf{};

    auto self_ref = s.self.object_ref();
    s.call();

    s.effects.registry_add.expect_once(self_ref);
}
```

## 8. operation依存

元コード:

```cpp
int PaymentService::approve(Money amount) {
    if (!payment_.reserve(amount)) return RESERVE_FAILED;
    audit_.write("reserved");
    return OK;
}
```

Google Test:

```cpp
TEST(PaymentService_approve, success_records_reserve_and_audit) {
    auto s = azteca_gen::scenario::PaymentService_approve{};

    auto amount = Money{5000};
    s.when.payment_reserve(amount).returns(true);

    auto result = s.call(amount);

    EXPECT_EQ(result, OK);
    s.effects.payment_reserve.expect_once(amount);
    s.effects.audit_write.expect_once("reserved");
}
```

`payment_reserve` は戻り値を供給し、効果としても記録される。

## 9. virtual dispatch

元コード:

```cpp
int Shape::scaledArea() const {
    return area() * scale_;
}
```

生成scenario:

```cpp
TEST(Shape_scaledArea, uses_dispatch_observation) {
    auto s = azteca_gen::scenario::Shape_scaledArea{};

    s.self.scale_ = 3;
    s.when.area(s.self.object_ref()).returns(10);

    auto result = s.call();

    EXPECT_EQ(result, 30);
}
```

実vtableは使わない。

## 10. missing observation

```cpp
TEST(Service_handle, missing_repo_observation_is_reported) {
    auto s = azteca_gen::scenario::Service_handle{};

    s.self.enabled_ = true;

    EXPECT_THROW({
        (void)s.call(Id{1});
    }, azteca::missing_observation);
}
```

未設定queryは暗黙デフォルトを返さない。

## 11. まとめ

V3の例が示す方針:

```text
- selfでreceiver状態を与える。
- whenで外界観測を与える。
- callで抽出kernelを実行する。
- EXPECTで戻り値/stateを検査する。
- effectsで外界への要求を検査する。
- fake thisもfake dependency classも作らない。
```

---

# File: docs/design/22_dependency_transcript_and_stubbing.md

# 22. Dependency Transcript and Stubbing

## 1. 目的

この文書は、依存関係が多いメソッドから抽出した心臓ロジックを、実務上扱いやすくユニットテストするための設計を定義する。

アステカの目的は、依存クラスのfakeを書くことではない。対象メソッドが外界から受け取る観測値と外界へ送る要求を、最小のテスト記述で制御・検証できるようにすることである。

## 2. 問題

以下のようなメソッドを考える。

```cpp
int OrderService::approve(OrderId id) {
    auto order = repo_.load(id);

    if (!order) return ERR_NOT_FOUND;
    if (clock_.now() > order->deadline()) return ERR_EXPIRED;
    if (!policy_.canApprove(user_, *order)) return ERR_DENIED;

    auto risk = risk_.score(order->amount(), user_.id());
    if (risk > 80) {
        audit_.write("high risk");
        return ERR_RISK;
    }

    payment_.reserve(order->amount());
    repo_.markApproved(id);
    bus_.publish(OrderApproved{id});

    return OK;
}
```

通常のテストでは、次が必要に見える。

```text
repo fake
clock fake
policy fake
risk fake
audit fake
payment fake
bus fake
user fake
order fake
```

これはユニットテスト性を破壊する。

## 3. 方針

アステカは依存オブジェクトを作らない。

```text
依存オブジェクトのfakeではなく、対象メソッドから見える依存観測をscenarioとして与える。
```

テストはこうなる。

```cpp
TEST(OrderService_approve, success_path) {
    auto s = azteca_gen::scenario::OrderService_approve{};

    auto id = OrderId{10};
    s.self.user_id = UserId{42};

    s.when.repo_load(id).returns(OrderShape{
        .deadline = Time{1000},
        .amount = Money{5000},
    });
    s.when.clock_now().returns(Time{900});
    s.when.policy_can_approve(UserId{42}, s.objects.order("order1")).returns(true);
    s.when.risk_score(Money{5000}, UserId{42}).returns(20);

    auto result = s.call(id);

    EXPECT_EQ(result, OK);
    s.effects.payment_reserve.expect_once(Money{5000});
    s.effects.repo_mark_approved.expect_once(id);
    s.effects.bus_publish.expect_once(OrderApproved{id});
}
```

実際の生成APIでは、`policy_can_approve` の引数はshape/value/object_refのどれが必要かに応じて最適化する。

## 4. Dependency Transcriptとは何か

Dependency Transcriptは、対象メソッドの外界入出力を記録・再生する構造である。

```text
Input side:
  Query observations

Output side:
  Effects

Both sides:
  Operations
```

例:

```text
repo_load(OrderId{10}) -> OrderShape{deadline=1000, amount=5000}
clock_now() -> Time{900}
policy_can_approve(...) -> true
risk_score(Money{5000}, UserId{42}) -> 20
payment_reserve(Money{5000})
repo_mark_approved(OrderId{10})
bus_publish(OrderApproved{10})
```

このtranscriptがあれば、本物のrepo/clock/policy/risk/payment/busは不要である。

## 5. ユーザー表面API

利用者が覚えるAPIは次に絞る。

```cpp
s.self               // receiver state
s.when.xxx(...).returns(...)
s.call(...)
s.effects.xxx.expect_once(...)
s.effects.expect_none()
s.effects.expect_no_unexpected_calls()
```

### 5.1 query

```cpp
s.when.repo_exists(id).returns(true);
```

### 5.2 effect

```cpp
s.effects.notifier_send.expect_once(id);
```

### 5.3 operation

```cpp
s.when.payment_reserve(amount).returns(true);
...
s.effects.payment_reserve.expect_once(amount);
```

## 6. Path-wise Stub Burden

依存総数ではなく、テスト経路ごとの必要観測を最小化する。

```cpp
if (!enabled_) return DISABLED;
if (!repo_.exists(id)) return NOT_FOUND;
if (!policy_.allow(id)) return DENIED;
notifier_.send(id);
return OK;
```

生成report:

```text
Path: disabled
  required observations: none
  effects: none

Path: not_found
  required observations:
    repo_exists(id) -> bool
  effects: none

Path: denied
  required observations:
    repo_exists(id) -> bool
    policy_allow(id) -> bool
  effects: none

Path: success
  required observations:
    repo_exists(id) -> bool
    policy_allow(id) -> bool
  effects:
    notifier_send(id)
```

この情報は、ユーザーが最初に書くべきテストを選ぶために重要である。

## 7. Shape生成

依存戻り値を本物のクラスとして構築しない。

```cpp
auto user = repo_.load(id);
return user->profile().age();
```

対象メソッドが `age` だけ使うなら、生成は次のどちらかでよい。

### 7.1 Shape

```cpp
struct UserShape {
    int age;
};

s.when.repo_load(id).returns(UserShape{.age = 37});
```

### 7.2 Expression-level query

```cpp
s.when.user_age_from_repo(id).returns(37);
```

どちらを選ぶかは、対象メソッド内で中間値が複数回使われるか、同一性が必要か、副作用順序が意味を持つかで決める。

## 8. Missing Observation

未設定queryは暗黙値を返さない。

悪い挙動:

```text
bool query -> false
int query -> 0
optional<T> query -> nullopt
```

これはテストを偶然通してしまう。

良い挙動:

```text
missing_observationを発生させ、次に書くべきscenario行を示す。
```

例:

```text
Missing observation
  policy_allow(Id) -> bool

Suggested:
  s.when.policy_allow(id).returns(true);
```

Google Testでは以下のように検査できる。

```cpp
EXPECT_THROW((void)s.call(id), azteca::missing_observation);
```

## 9. 依存順序

効果の順序が意味を持つ場合、trace順序を検証できる。

```cpp
s.effects.expect_sequence({
    s.effects.payment_reserve.called_with(amount),
    s.effects.repo_mark_approved.called_with(id),
    s.effects.bus_publish.called_with(OrderApproved{id}),
});
```

順序が意味を持たない場合は個別 `expect_once` だけでよい。

標準reportは、元コード上の順序を表示するが、テストで順序assertを強制しない。

## 10. Stateful Dependency

状態を持つ依存は、まずtranscriptで扱う。

```cpp
auto v = cache_.get(key);
if (!v) {
    v = compute();
    cache_.put(key, *v);
}
return *v;
```

テスト:

```cpp
s.when.cache_get(key).returns(std::nullopt);
s.when.compute().returns(Value{123});

auto result = s.call(key);

EXPECT_EQ(result, Value{123});
s.effects.cache_put.expect_once(key, Value{123});
```

複数回呼び出しが必要なら、call sequenceを使う。

```cpp
s.when.cache_get(key).on_call(1).returns(std::nullopt);
s.when.cache_get(key).on_call(2).returns(Value{123});
```

Stateful fake生成は補助機能であり、標準ではない。

## 11. Record/Replay

依存が多いメソッドの初期scenario作成を補助するため、将来的にrecord/replayを導入する。

流れ:

```text
1. 既存統合テストまたは手動実行で本物を動かす。
2. アステカが依存観測と効果を記録する。
3. transcriptからGoogle Test scenarioを生成する。
4. 人間が意図を確認し、境界値や異常系を追加する。
```

record/replayはテスト作成補助であり、自動生成されたscenarioを無批判に正とみなしてはならない。

## 12. アンチパターン

### 12.1 fake class explosion

```cpp
class FakeRepo : public Repo { ... };
class FakeClock : public Clock { ... };
class FakePayment : public Payment { ... };
```

標準設計では避ける。

### 12.2 default returns

```cpp
query<bool> q; // 未設定ならfalse
```

禁止。

### 12.3 over-mocking internal helpers

private pure helperを安易にstub化すると、心臓ロジックが失われる。

標準では再帰抽出する。

## 13. 受け入れ基準

Dependency Transcript機能のMVPは、次を満たす。

```text
1. query/effect/operationを生成できる。
2. scenario.whenで戻り値を設定できる。
3. scenario.effectsで効果をassertできる。
4. 未設定queryがmissing observationを出す。
5. Google Test sampleが生成される。
6. path-wise stub burdenがreportに出る。
7. 依存fakeクラスを書かずにテストできる。
```

---

# File: docs/design/23_gtest_integration.md

# 23. Google Test Integration

## 1. 目的

この文書は、アステカが生成するテストの標準ランナーをGoogle Testにする設計を定義する。

方針:

```text
Google Testを標準ランナーにする。
GoogleMockは中核にしない。
アステカruntimeとkernelはGoogle Test非依存に保つ。
```

## 2. 採用理由

Google Testを標準にする理由:

```text
- 既存C++プロジェクトで導入しやすい。
- TEST/EXPECT/ASSERTの読みやすい記法がある。
- CMake/CTest連携がしやすい。
- CI/CDで扱いやすい。
- アステカ独自runnerの学習コストを避けられる。
```

## 3. 非目標

```text
- GoogleMockを必須にしない。
- 生成kernelをGoogle Testに依存させない。
- アステカ独自assert言語を標準にしない。
- 既存プロジェクトのテストフレームワークを強制的に置換しない。
```

## 4. 生成CMake

標準生成CMakeは、次の選択肢を持つ。

```text
1. find_package(GTest CONFIG REQUIRED)
2. FetchContentでGoogleTestを取得
3. 親プロジェクトが提供するGTest::gtest_mainを利用
```

標準出力では、利用者環境への侵襲を避けるため、次の順にする。

```text
- 既存GTest targetがあれば使う。
- find_packageできれば使う。
- 明示オプションがある場合だけFetchContentを生成する。
```

例:

```cmake
cmake_minimum_required(VERSION 3.20)
project(azteca_generated_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(GTest CONFIG REQUIRED)

add_executable(Account_withdraw_test
    tests/Account_withdraw.sample_test.cpp
    src/Account_withdraw.kernel.cpp
)

target_include_directories(Account_withdraw_test PRIVATE include)
target_link_libraries(Account_withdraw_test PRIVATE GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(Account_withdraw_test)
```

## 5. 生成テスト形式

### 5.1 基本形

```cpp
#include <gtest/gtest.h>
#include "Account_withdraw.scenario.hpp"

TEST(Account_withdraw, unlocked_path) {
    auto s = azteca_gen::scenario::Account_withdraw{};

    s.self.balance_ = 100;
    s.self.locked_ = false;
    s.when.fee(50).returns(5);

    auto result = s.call(50);

    EXPECT_EQ(result, 45);
    EXPECT_EQ(s.self.balance_, 45);
    s.effects.expect_none();
}
```

### 5.2 Missing observation

```cpp
TEST(Account_withdraw, missing_fee_observation) {
    auto s = azteca_gen::scenario::Account_withdraw{};

    s.self.balance_ = 100;
    s.self.locked_ = false;

    EXPECT_THROW({
        (void)s.call(50);
    }, azteca::missing_observation);
}
```

### 5.3 Effects

```cpp
TEST(C_register, registers_self) {
    auto s = azteca_gen::scenario::C_register{};

    auto ref = s.self.object_ref();
    s.call();

    s.effects.registry_add.expect_once(ref);
}
```

## 6. Google Test Adapter

通常は `EXPECT_EQ` とscenario APIで足りる。

ただし、diagnosticsを改善するために薄いadapterを提供する。

```cpp
#include <azteca/azteca_gtest.hpp>

AZTECA_EXPECT_NO_UNEXPECTED_EFFECTS(s.effects);
AZTECA_EXPECT_EFFECT_ONCE(s.effects.bus_publish, OrderApproved{id});
AZTECA_EXPECT_MISSING_OBSERVATION(s.call(id), "policy_allow");
```

adapterはGoogle Testの非fatal/fatal failure semanticsに従う。

## 7. GoogleMockとの関係

GoogleMockは使ってよいが、標準依存モデルではない。

理由:

```text
- アステカのportは元interfaceと一致しないことがある。
- shape化やexpression-level portはmock objectより自然である。
- 依存クラスfakeを作ると、テスタビリティ問題へ戻る。
```

許容される用途:

```text
- 既存プロジェクトがGoogleMock matchersを使いたい場合。
- effects traceをmatcherで検査したい場合。
- record/replay境界で既存mockを利用したい場合。
```

## 8. 独自runner fallback

独自runnerを導入する可能性は残すが、標準ではない。

導入条件:

```text
- Google Testで表せない実行制御が必要。
- fuzzing専用harness。
- cross compile環境でGoogle Test discoveryが使えない。
- 組込みターゲットでGoogle Testが過重。
```

その場合でも、kernel/scenario runtimeはそのまま再利用する。

## 9. 生成物

```text
azteca-out/
  CMakeLists.txt
  include/
    azteca/
      azteca_runtime.hpp
      azteca_gtest.hpp
    generated/
      Account_withdraw.self.hpp
      Account_withdraw.shape.hpp
      Account_withdraw.ports.hpp
      Account_withdraw.scenario.hpp
      Account_withdraw.kernel.hpp
  src/
    Account_withdraw.kernel.cpp
  tests/
    Account_withdraw.sample_test.cpp
    Account_withdraw.skeleton_test.cpp
  manifest.json
  azteca_report.md
```

## 10. 成功基準

```text
1. `cmake -S azteca-out -B azteca-out/build` が通る。
2. `cmake --build azteca-out/build` が通る。
3. `ctest --test-dir azteca-out/build --output-on-failure` が通る。
4. Google Test sampleがself、when、effectsを使っている。
5. 生成kernelはGoogle Test非依存である。
6. 独自runnerなしで標準利用できる。
```

---

# File: docs/planning/12_implementation_plan.md

# 12. Implementation Plan V3

## 1. 目的

この文書は、Azteca V3の実装着手時に使う具体的な開発計画を定義する。

設計は実装開始に十分である。最初の成果物は完全な `extract` ではなく、`inspect` によるExtraction Plan表示である。

## 2. Repository Layout

```text
azteca/
  CMakeLists.txt
  include/
    azteca/
      Diagnostics.hpp
      MethodSpec.hpp
      MMIR.hpp
      ExtractionPlan.hpp
      RuntimeContracts.hpp
  src/
    cli/
    frontend/
    resolve/
    mmir/
    collect/
    plan/
    lower/
    codegen/
    report/
    runtime/
    gtest/
  tests/
    unit/
    fixtures/
    golden/
    integration/
    negative/
  docs/
```

## 3. Phase A: Inspect MVP

### Goal

対象メソッドをASTから見つけ、抽出計画を表示する。

### Scope

```text
- compile_commands.json loading
- MethodSelector
- MethodInfo extraction
- MMIR MVP
- receiver field collection
- dependency observation collection
- query/effect/operation classification MVP
- shape candidate MVP
- path-wise stub burden MVP
- Google Test preview report
```

### Non-scope

```text
- kernel generation
- scenario runtime implementation
- CMake generation
- Google Test execution
```

### Example output

```text
Azteca can extract Account::withdraw(int).

Generated Google Test:
  tests/account.withdraw.sample_test.cpp

Receiver state:
  - int balance_ read/write
  - bool locked_ read

Dependency observations:
  query fee(int) -> int

Path-wise test burden:
  locked:
    observations: none
  unlocked:
    observations: fee
```

## 4. Phase B: Minimal Kernel + Google Test

### Scope

```text
- self.hpp
- kernel.hpp/cpp
- scenario.hpp minimal
- Google Test sample
- CMakeLists.txt
- manifest.json
- report.md
```

### Supported syntax

```text
field read/write
local variable
argument
if/else
return
arithmetic/logical expression
simple private helper recursive extraction
simple query dependency
```

### Done

```text
cmake -S azteca-out -B azteca-out/build
cmake --build azteca-out/build
ctest --test-dir azteca-out/build --output-on-failure
```

## 5. Phase C: Dependency Transcript Runtime

### Scope

```text
azteca::query
azteca::effect
azteca::operation
azteca::missing_observation
scenario.when API
scenario.effects API
Google Test adapter
```

### Done

```text
- non-void dependency returns configured value
- missing dependency throws missing_observation
- void dependency records effect
- operation returns value and records effect
- generated Google Test verifies effects
```

## 6. Phase D: Shape and Expression-level Ports

### Scope

```text
- returned dependency object shape
- optional-like shape wrapping
- expression-level query port
- object_ref for identity-preserving cases
```

## 7. Phase E onward

```text
E: Identity and addressability
F: Dispatch and dynamic type
G: Lifetime and representation
H: Record/replay
I: UX hardening and regression
```

## 8. First Issues

```text
Issue 1: Project bootstrap
Issue 2: Compilation database loading
Issue 3: Method selector
Issue 4: MMIR MVP
Issue 5: Receiver field collector
Issue 6: Dependency observation collector
Issue 7: Inspect report text/json
Issue 8: Basic fixtures and Google Test unit tests
```

## 9. Definition of Done for Implementation Start

```text
- azteca --help works
- azteca inspect can find CXXMethodDecl
- inspect displays receiver fields
- inspect displays dependency observations
- inspect displays Google Test preview
- unit tests run with Google Test
```

---

# File: docs/planning/18_implementation_roadmap.md

# 18. V3 Implementation Roadmap

## 1. 目的

この文書は、アステカを「単純な利用感を保ったまま、より多くのメソッドを抽出できるツール」へ進める実装ロードマップを定義する。

V3では、依存関係問題とGoogle Test統合を正式にロードマップへ組み込む。

```text
Phase A: Inspect as Extraction Plan
Phase B: Minimal Google Test Kernel Extraction
Phase C: Dependency Transcript and Scenario Runtime
Phase D: Shape and Expression-level Ports
Phase E: Identity and Addressability
Phase F: Dispatch and Dynamic Type
Phase G: Lifetime and Representation
Phase H: Record/Replay and Scale
Phase I: UX Hardening and Regression
```

## 2. Phase A: Inspect as Extraction Plan

### 目的

コード生成前に、対象メソッドがどのように抽出されるかを説明できるようにする。

### コマンド

```bash
azteca inspect -p build --method 'C::m(int)'
```

### 出力に含めるもの

```text
- receiver state
- generated shapes候補
- dependency observations
- observable effects
- object_ref要求
- path-wise stub burden
- generated Google Test preview
```

### 完了条件

```text
1. field read/writeを検出できる。
2. same-class helperの再帰抽出候補を出せる。
3. member object method callをquery/effect/operation候補に分類できる。
4. this escapeをobject_ref要求として表示できる。
5. 早期return経路ごとの必要queryを概算できる。
```

## 3. Phase B: Minimal Google Test Kernel Extraction

### 目的

単純なメソッドをkernelとして生成し、Google Test sampleで実行できるようにする。

### 対応範囲

```text
- field read/write
- local variable
- argument
- arithmetic/logical expression
- if/else
- return
- const receiver
- simple helper recursive extraction
```

### 生成物

```text
include/
  C_m.self.hpp
  C_m.kernel.hpp

tests/
  C_m.sample_test.cpp

CMakeLists.txt
azteca_report.md
manifest.json
```

### 完了条件

```text
1. 生成コードがCMakeでビルドできる。
2. Google Test sampleが実行できる。
3. fake thisを使わない。
4. self更新と戻り値をEXPECTで検証できる。
```

## 4. Phase C: Dependency Transcript and Scenario Runtime

### 目的

大量依存メソッドを、依存fakeクラスなしにテストできるようにする。

### 対応範囲

```text
- query port
- effect log
- operation port
- scenario.when.xxx(...).returns(...)
- scenario.effects.xxx.expect_once(...)
- missing observation diagnostics
- generated scenario skeleton
```

### 生成例

```cpp
TEST(C_m, success_path) {
    auto s = azteca_gen::scenario::C_m{};

    s.self.enabled = true;
    s.when.repo_exists(Id{1}).returns(true);
    s.when.policy_allow(Id{1}).returns(true);

    auto result = s.call(Id{1});

    EXPECT_EQ(result, OK);
    s.effects.notifier_send.expect_once(Id{1});
}
```

### 完了条件

```text
1. non-void dependency callをqueryとして扱える。
2. void dependency callをeffectとして記録できる。
3. operationが戻り値供給と効果記録の両方を行える。
4. 未設定queryに到達するとmissing observationが出る。
5. 生成Google Testがscenario APIを使って通る。
```

## 5. Phase D: Shape and Expression-level Ports

### 目的

依存が返す巨大オブジェクトやメソッドチェーンを、本物の依存構築なしに扱う。

### 対応範囲

```text
- returned object shape generation
- optional/unique_ptr/shared_ptr-like wrapperのshape化
- expression-level query port
- shape equality / print support
- inspectでshape field表示
```

### 完了条件

```text
1. repo.load(id)->amount() を OrderShape.amount にloweringできる。
2. repo.find(id)->profile().age(now) を単一query portに畳める。
3. 中間同一性が必要な場合は畳まずobject_refへ展開できる。
```

## 6. Phase E: Identity and Addressability

### 目的

`this` の同一性、`return this`、`external(this)`、メンバアドレス取得を抽出できるようにする。

### 対応範囲

```text
- object_ref<C>
- object_id generation
- return this
- this comparison
- pass this to dependency
- address-taken field
- reference aliasing
- simple pointer to field
```

### 完了条件

```text
1. return thisをobject_ref戻りにできる。
2. external(this)をdeps.external(object_ref)にできる。
3. &fieldをcell/refにできる。
4. aliasによるfield更新が保存される。
```

## 7. Phase F: Dispatch and Dynamic Type

### 目的

virtual call、dynamic_cast、typeidを意味モデルとして抽出する。

### 対応範囲

```text
- virtual method call -> dispatch query/operation
- pure virtual call -> required dispatch observation
- dynamic_cast<this> -> type_tag test
- typeid(*this) -> type_tag info
- derived shape view
```

## 8. Phase G: Lifetime and Representation

### 目的

delete this、explicit destructor、placement new、byte accessなどを、可能な限り意味モデルとして抽出する。

### 対応範囲

```text
- lifetime_state
- destructor kernel
- delete effect
- placement-new intent
- byte_view for representation observation
```

完全なABI再現はしない。ユニットテスト上意味のある観測へ落とす。

## 9. Phase H: Record/Replay and Scale

### 目的

大量依存メソッドのscenario作成負担をさらに下げる。

### 対応範囲

```text
- dependency transcript recording
- transcript to Google Test scenario generation
- path seed generation
- scenario minimization
```

record/replayは補助機能である。標準のunit testは人間が意図を確認して編集する。

## 10. Phase I: UX Hardening and Regression

### 目的

実プロジェクトで継続使用できる品質にする。

### 対応範囲

```text
- diagnostics polish
- large fixture corpus
- generated code style
- CI integration
- CMake package integration
- regression minimization
- reporting quality
```

## 11. 実装開始判断

現時点で実装開始してよい。

ただし、最初に作るものは `extract` の完全版ではなく、`inspect` である。

```text
Phase A first:
  - MethodSelector
  - FeatureCollector
  - MMIR MVP
  - Dependency observation collector
  - Path-wise stub burden reporter
  - Google Test preview reporter
```

この順であれば、設計が現実のASTに耐えるかを早期に検証できる。

---

# File: docs/planning/25_phase_a_inspect_coverage.md

# 25. Phase A Inspect Coverage

このメモは Phase A `azteca inspect` の close 判定に使う構文カバレッジ表である。Phase A は kernel/codegen を実装せず、Clang AST/Sema 後情報から Extraction Plan、診断、Google Test preview を安定表示する。

Phase A の完了条件は、実行可能な kernel や Google Test を生成することではない。実インスタンス化なしの kernel/scenario 化に必要な receiver state、dependency transcript、shape 候補、path-wise stub burden、未対応/保守的理由を inspect report と JSON で安定説明できることを完了条件とする。

handling の意味:

```text
supported:
  inspect が Clang/Sema 後の意味を安定して計画へ反映できる。

modeled:
  Phase A では report-only model として意味要求を示す。lowering/codegen 対応を意味しない。

boundary:
  対象メソッドの外側の観測点として dependency transcript port へ分離する。

conservative:
  壊れた plan/preview を出さず、診断、confidence、control_flow_summary、path burden で近似理由を示す。

not_yet_implemented:
  Phase A では future/diagnostic として明示し、危険な fake this や ABI hack へ進まない。
```

| 構文・意味                             | Phase A handling                                   | 実装状態     | inspect 出力                                   |
| -------------------------------------- | -------------------------------------------------- | ------------ | ---------------------------------------------- |
| implicit `this` field access           | supported                                          | pass         | `receiver_state`, `self_state`, `LR-001`       |
| explicit `this->field` access          | supported                                          | pass         | `receiver_state`, `self_state`, `LR-002`       |
| member field write / compound write    | supported                                          | pass         | `read/write`, `LR-003`                         |
| field address taking                   | modeled                                            | pass         | `addressable_cell`, `LR-029`                   |
| same-class nonvirtual member call      | supported as recursive candidate                   | pass         | `recursive_helper_candidates`, `LR-007`        |
| dependency member call                 | boundary                                           | pass         | query/effect/operation port                    |
| virtual member call                    | modeled                                            | pass         | `dispatch_table`, `object_ref`, `LR-012`       |
| overload                               | supported for inspect naming                       | pass         | overload-disambiguated port names              |
| operator target method                 | not_yet_implemented                                | future       | `operator methods are not supported`           |
| const/volatile/ref-qualified target    | supported                                          | pass         | `method_qualifier`, `LR-047`                   |
| default argument                       | supported                                          | pass         | `default_argument`, `LR-042`                   |
| default member initializer             | not_yet_implemented                                | future       | `default_member_initializer`, `LR-040`         |
| constructor call in body               | conservative                                       | conservative | `constructor_call`, `lifetime_state`, `LR-049` |
| destructor target                      | not_yet_implemented                                | future       | target diagnostic                              |
| explicit destructor call               | modeled                                            | pass         | `lifetime_state`, `LR-027`                     |
| local variable                         | supported                                          | pass         | `local_variable`, `LR-014`                     |
| parameter reference                    | supported                                          | pass         | MMIR `ArgRef`                                  |
| lambda without `this` capture          | supported                                          | pass         | `lambda`, `LR-017`                             |
| lambda with `this` capture             | modeled                                            | pass         | `self_state`, `LR-018`                         |
| nested class / namespace target        | supported                                          | pass         | fully qualified method matching                |
| function template specialization       | supported                                          | pass         | `template_specialization`, `LR-033`            |
| class template specialization          | conservative                                       | conservative | `template_specialization`, `LR-033`            |
| uninstantiated template/dependent name | not_yet_implemented                                | future       | `dependent_name`, template diagnostic          |
| `auto`                                 | supported                                          | pass         | resolved semantic type, `LR-043`               |
| range-for                              | conservative                                       | conservative | `loop_control_flow`, `LR-016`                  |
| if                                     | supported                                          | pass         | path split, `LR-005`                           |
| switch                                 | supported for inspect path segmentation            | pass         | case/default path labels, `LR-015`             |
| for/while/do                           | conservative                                       | conservative | conservative path summary, `LR-015`            |
| return                                 | supported                                          | pass         | path terminal, `LR-004`                        |
| break/continue                         | supported for inspect                              | pass         | `break_continue`, `LR-046`                     |
| ternary `?:`                           | supported                                          | pass         | `conditional_operator`, `LR-041`               |
| unary/binary operators                 | supported for built-ins                            | pass         | `LR-006`                                       |
| overloaded operator in body            | boundary                                           | pass         | operation/query/effect port, `LR-013`          |
| references/pointers                    | supported when local; modeled for identity/address | pass         | `object_ref` or `addressable_cell` as needed   |
| `std::move` / `std::forward`           | supported as cast, not dependency                  | pass         | `value_category_cast`, `LR-044`                |
| static/functional/C-style cast         | supported                                          | pass         | `cast_expression`, `LR-044`                    |
| `const_cast`                           | boundary                                           | pass         | `const_cast`, `LR-045`                         |
| `dynamic_cast` / `typeid`              | modeled report-only                                | pass         | `type_tag`, `LR-023` / `LR-024`                |
| `reinterpret_cast`                     | boundary                                           | pass         | `byte_view`, `LR-025`                          |
| throw                                  | supported                                          | pass         | `exception_model`, `LR-035`                    |
| try/catch                              | conservative                                       | conservative | conservative exception control flow, `LR-036`  |
| macro expansion                        | conservative                                       | conservative | `macro_source_map`, `LR-034`                   |
| private/protected access               | modeled report-only                                | pass         | `access_control`, `LR-048`                     |
| coroutine                              | not_yet_implemented                                | future       | `coroutine`, `LR-038`                          |

未対応または保守的な項目は、壊れた plan/preview を出さず、`unsupported_or_modeled_constructs`、`diagnostics`、`confidence`、`control_flow_summary` のいずれかで理由を示す。

identity、addressability、dispatch、dynamic type、lifetime、byte representation は Phase A では report-only model である。Phase A は必要な Semantic Envelope 要素を表示するが、それらの lowering/codegen 対応は後続 Phase の責務である。

Phase A close時点の安定化ゲートは次である。

```text
- dev-clang check
- asan-clang build
- asan-clang ctest
- Phase A golden text/json
- JSON parse validation for inspect --format json
```

## Close score

2026-05-24 時点の Phase A 自己採点は 92/100 である。release candidate 水準では
あるが、100 点へ近づけるために次を追加の close hardening とする。

- project-wide inspect で、対象と無関係な TU parse failure を `AZT-W0002` として
  report し、matching plan が得られた場合は継続する。
- `paths[].ordered_events` で query / operation / effect の到達順を JSON に追加する。
- Google Test preview は ordered events を使って stub setup と effect assertion の
  順序を安定化する。
- conservative path は preview 内で短い comment として明示する。

## Fixture evidence

上表の "pass" 行は、`tests/integration/PhaseACoverageObservations.cmake` が以下のメソッドを inspect し、各 `rule_coverage[].observed=true` をアサートしつつ golden 比較することで証明する。golden は `tests/golden/phase_a/coverage_observations/` に置く。

| LR rule | observed via                               | golden                                             |
| ------- | ------------------------------------------ | -------------------------------------------------- |
| LR-001  | `OuterContainer::Inner::run() const`       | `outer_container_inner_run.inspect.json`           |
| LR-002  | `EdgeCases::explicit_this_read() const`    | `edge_cases_explicit_this_read.inspect.json`       |
| LR-005  | `EdgeCases::branch_controls(int)`          | `edge_cases_branch_controls.inspect.json`          |
| LR-006  | `SyntaxMatrix::first_byte() const`         | `syntax_matrix_first_byte.inspect.json`            |
| LR-009  | `SyntaxMatrix::base_global_static(int)`    | `syntax_matrix_base_global_static.inspect.json`    |
| LR-012  | `SyntaxMatrix::dispatch()`                 | `syntax_matrix_dispatch.inspect.json`              |
| LR-013  | `SyntaxMatrix::operator_path()`            | `syntax_matrix_operator_path.inspect.json`         |
| LR-014  | `SyntaxMatrix::reset_in_place()`           | `syntax_matrix_reset_in_place.inspect.json`        |
| LR-015  | `SyntaxMatrix::switch_loop(int)`           | `syntax_matrix_switch_loop.inspect.json`           |
| LR-017  | `SyntaxMatrix::lambda_run()`               | `syntax_matrix_lambda_run.inspect.json`            |
| LR-019  | `EdgeCases::noexcept_read() const`         | `edge_cases_noexcept_read.inspect.json`            |
| LR-021  | `EdgeCases::return_self_reference()`       | `edge_cases_return_self_reference.inspect.json`    |
| LR-022  | `EdgeCases::return_self_reference()`       | `edge_cases_return_self_reference.inspect.json`    |
| LR-023  | `SyntaxMatrix::identity_and_type() const`  | `syntax_matrix_identity_and_type.inspect.json`     |
| LR-024  | `SyntaxMatrix::identity_and_type() const`  | `syntax_matrix_identity_and_type.inspect.json`     |
| LR-025  | `SyntaxMatrix::first_byte() const`         | `syntax_matrix_first_byte.inspect.json`            |
| LR-027  | `SyntaxMatrix::release()`                  | `syntax_matrix_release.inspect.json`               |
| LR-029  | `EdgeCases::field_address()`               | `edge_cases_field_address.inspect.json`            |
| LR-033  | `TemplateExample::target(int)`             | `template_example_target.inspect.json`             |
| LR-034  | `SyntaxMatrix::base_global_static(int)`    | `syntax_matrix_base_global_static.inspect.json`    |
| LR-035  | `SyntaxMatrix::exception_run()`            | `syntax_matrix_exception_run.inspect.json`         |
| LR-036  | `SyntaxMatrix::exception_run()`            | `syntax_matrix_exception_run.inspect.json`         |
| LR-037  | `SyntaxMatrix::structured()`               | `syntax_matrix_structured.inspect.json`            |
| LR-038  | `CoroHost::run(int)`                       | `coro_host_run.inspect.json`                       |
| LR-039  | `SyntaxMatrix::unevaluated()`              | `syntax_matrix_unevaluated.inspect.json`           |
| LR-040  | `EdgeCases::default_initialized_read()`    | `edge_cases_default_initialized_read.inspect.json` |
| LR-041  | `EdgeCases::ternary(int)`                  | `edge_cases_ternary.inspect.json`                  |
| LR-042  | `EdgeCases::default_argument()`            | `edge_cases_default_argument.inspect.json`         |
| LR-043  | `EdgeCases::value_category_and_casts(int)` | `edge_cases_value_category_and_casts.inspect.json` |
| LR-044  | `EdgeCases::value_category_and_casts(int)` | `edge_cases_value_category_and_casts.inspect.json` |
| LR-048  | `EdgeCases::explicit_this_read() const`    | `edge_cases_explicit_this_read.inspect.json`       |

LR-031/LR-032 は plan JSON を生成しない target-level rejection として
`PhaseACoverageMatrix.cmake` が `AZT-E0010` と exit code 3 を検証する。

その他の "pass" 行 (LR-003/004/007/018/043/045/046/047/049 ほか) は既存
`PhaseAInspect.cmake` の goldens で観測される。

---

# File: docs/review/24_total_review_and_self_verification.md

# 24. Total Review and Self Verification

## 1. 目的

この文書は、ここまでのアステカ設計がユーザー要求を満たすか、内部齟齬がないか、実装開始に足るかを総点検する。

## 2. ユーザー要求の再整理

主要要求:

```text
1. C++メソッドをインスタンス化要求から解放したい。
2. UBにならない限り強い手法を使ってよいが、壊れやすいhackは禁止。
3. ASTを使い、本来ロジックの意味を守ったままテスト可能な形へ抽出する。
4. モードや分類を増やし、利用者の学習コストを増やしてはならない。
5. よほどの例外を除き、ほとんどすべてのメソッドを取り出せる方向へ原理を拡大する。
6. 抽出した心臓ロジックを有意義にユニットテストできなければならない。
7. 依存関係が多いメソッドでも、fake地獄に戻してはならない。
8. テストランナーは基本Google Testを使う。
9. Google Testで実現不能な問題が明確になった場合のみ独自runnerを検討する。
```

## 3. 要求充足チェック

| 要求                                             | V3での回答                                                                             | 判定 |
| ------------------------------------------------ | -------------------------------------------------------------------------------------- | ---- |
| インスタンス化なしにメソッドロジックを試験したい | ASTからMMIRへ落とし、self付きkernelを生成する                                          | OK   |
| fake this禁止                                    | Semantic Contractで禁止。object_refは実ポインタではない                                | OK   |
| 壊れやすいABI hack禁止                           | pointer-to-member偽変換、未構築storage呼び出しを禁止                                   | OK   |
| AST/Semaベース                                   | Clang AST/Sema後情報から変換する                                                       | OK   |
| 複雑なmode選択を避ける                           | 公開入口は原則 `azteca extract` のみ                                                   | OK   |
| ほとんどすべてのメソッドへ拡張                   | Semantic Envelopeにfield/object_ref/deps/effects/dispatch/type/lifetime/byteを追加可能 | OK   |
| 依存が多いメソッドを試験可能にする               | Dependency Transcript、Scenario API、path-wise stub burden                             | OK   |
| fakeクラス地獄を避ける                           | `s.when...returns` と `s.effects...expect` を標準化                                    | OK   |
| Google Test標準                                  | V3で標準runnerに決定                                                                   | OK   |
| 独自runner乱立を避ける                           | runtime/kernelは独立、runner fallbackは限定条件のみ                                    | OK   |

## 4. 齟齬検査

### 4.1 「Google Test標準」と「GoogleMock非中核」は矛盾しないか

矛盾しない。

Google Testはテスト実行・assertion・reportingの標準である。一方、依存制御の中核はGoogleMockではなくScenario APIである。

理由:

```text
- アステカのdependency portは元C++ interfaceと一致しない場合がある。
- shape化、expression-level port、object_refはmock objectより自然である。
- GoogleMockを中核にすると、依存fakeクラス問題へ戻る。
```

したがって、方針は次で整合する。

```text
Runner/assertion: Google Test
Dependency control: Azteca Scenario API
Optional matcher/mock integration: GoogleMock可
```

### 4.2 「ほとんどすべてのメソッド抽出」と「意味の正しさ」は矛盾しないか

矛盾しない。ただし、保存する意味をunit-observable semanticsに限定する。

保存するもの:

```text
- 戻り値
- 例外
- receiver state
- dependency observations
- external effects
- call order when meaningful
- object identity
- dynamic type decision
- lifetime intent
- byte access intent
```

標準では保存しないもの:

```text
- 実アドレス値
- vptr実表現
- padding byteの偶然値
- allocator内部状態
- OS handle実値
- UBの結果
```

この線引きにより、広い抽出範囲と安全性を両立する。

### 4.3 「単純な利用体験」と「Semantic Envelopeの豊富さ」は矛盾しないか

矛盾しない。

利用者が覚える概念を制限する。

```text
self
scenario
when
effects
```

内部では複雑なEnvelopeを使うが、通常reportでは「何を設定すればよいか」だけを示す。

### 4.4 「再帰抽出」と「スタブ化」は衝突しないか

衝突しない。

標準判断:

```text
内部pure helper: 再帰抽出
外部性のある依存: transcript port
```

これにより、心臓ロジックを保ちながら依存爆発を抑える。

### 4.5 「生成テストはコンパイル可能」と「未設定query禁止」は矛盾しないか

矛盾しない。

生成sampleは、代表値またはplaceholder付きskeletonを出す。
未設定queryの暗黙デフォルトは禁止だが、sample内で設定例を生成すればコンパイル・実行できる。

## 5. 残る本質的限界

V3でも、次は完全自動で意味保存できない場合がある。

```text
- 元コード自体がUBに依存する。
- inline asmの意味がC++レベルで解釈不能。
- 実ハードウェア状態そのものがテスト対象。
- 暗号/乱数/時刻などで外界観測のモデル化が不十分。
- padding byteやABI固有表現が仕様として意味を持つ。
```

ただし、これらは例外であり、通常のビジネスロジック、状態遷移、依存調停、プロトコル処理、入力検証、イベント発行などはV3設計で広く扱える。

## 6. 実装開始可否

判断:

```text
実装開始してよい。
```

理由:

```text
- 安全契約は十分明確。
- 公開UXは単純に保たれている。
- 依存問題への中核回答が定義された。
- Google Test標準方針が決まった。
- これ以上抽象設計だけを進めると、現実のAST検証が遅れる。
```

ただし、最初の成果物は完全な `extract` ではなく、`inspect` である。

## 7. 最初に実装すべきもの

Phase A MVP:

```text
1. CLI skeleton
2. compile_commands.json loading
3. MethodSelector
4. MMIR MVP
5. FeatureCollector
6. DependencyObservationCollector
7. EnvelopePlanner
8. Path-wise Stub Burden Reporter
9. Google Test Preview Reporter
```

出力例:

```text
Azteca can extract OrderService::approve(OrderId).

Google Test will be generated.

To test the success path, provide:
  s.when.repo_load(id).returns(OrderShape{...});
  s.when.clock_now().returns(Time{...});
  s.when.policy_can_approve(...).returns(true);
  s.when.risk_score(...).returns(20);

Then assert:
  s.effects.payment_reserve.expect_once(...);
  s.effects.repo_mark_approved.expect_once(id);
  s.effects.bus_publish.expect_once(...);
```

## 8. 設計上の最終方針

アステカのV3最終方針は次である。

```text
メソッドをfake thisで呼ばない。
メソッドの意味をASTから抽出する。
実オブジェクトの代わりにselfを使う。
依存オブジェクトのfakeではなくDependency Transcriptを使う。
テストはGoogle Testで実行する。
利用者にはscenario/when/effectsだけを見せる。
```

この方針は、ユーザー要求である「有意義なユニットテスタビリティの向上」を満たす見込みが高い。

## 9. Go Decision

```text
GO: Phase A implementation may start.
```

実装で現実のASTにぶつかった場合、設計書へADRとして反映する。
ただし、公開UXを複雑化する方向の変更は慎重に扱う。

---

# File: docs/adr/0001_no_fake_this.md

# ADR-0001: fake `this` を使わない

## Status

Accepted

## Context

Aztecaの目的は、C++非staticメンバ関数を単体試験しやすくすることである。最も安易な案は、未初期化ストレージを`C*`に見せかけ、pointer-to-memberで呼ぶ方式である。

しかし、この方式はC++オブジェクトライフタイム規則に反し、ハーネス自身が未定義動作を持ち込む。

```cpp
void* storage = std::malloc(sizeof(C));
C* fake = reinterpret_cast<C*>(storage);
fake->m(); // 禁止
```

## Decision

Azteca中核ではfake `this` を使わない。

Heart modeでは、メソッド本体をASTから抽出し、明示receiver関数へloweringする。

Live modeでは、正規に構築された実オブジェクトだけを使う。

## Consequences

良い点:

- ハーネス由来UBを避けられる。
- 生成コードが通常C++として読める。
- sanitizer/fuzzerの結果が汚染されにくい。
- 多重継承やvirtual ABIに依存しない中核を作れる。

悪い点:

- Heart modeは元製品バイナリそのものの呼び出しではない。
- 実layout/RTTI/vptrを検査するにはLive modeが必要。
- AST lowering実装が必要になる。

## Alternatives considered

### ABI hack

pointer-to-memberを関数ポインタへ変換する案。

却下理由:

- ABI依存。
- 多重継承やvirtualで壊れやすい。
- C++標準上の堅牢な契約にできない。

### `#define private public`

private突破で実オブジェクトを作りやすくする案。

却下理由:

- クラス定義を変えてしまう。
- Aztecaの中核安全契約を弱める。

## References

- `../design/01_semantic_contract.md`
- `../design/07_live_mode.md`

---

# File: docs/adr/0002_use_clang_ast_not_text_rewrite.md

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

---

# File: docs/adr/0003_heart_mode_and_live_mode.md

# ADR-0003: Heart mode と Live mode を分離する

## Status

Accepted

## Context

Aztecaには2つの異なる要求がある。

1. 実オブジェクトなしにメソッド本体ロジックを単体試験したい。
2. 実オブジェクト、実ABI、実RTTI、実virtual dispatch込みで元メソッドを試験したい。

これらを単一モードに混ぜると、Heart modeが実layoutを暗黙に仮定したり、Live modeがfake objectに逃げたりする危険がある。

## Decision

AztecaはHeart modeとLive modeを明確に分離する。

Heart mode:

- ASTから明示receiver関数を生成する。
- 元クラスの実インスタンスを作らない。
- ロジック単体試験とfuzzingに向く。

Live mode:

- 正規構築された実オブジェクトに対して元メソッドを呼ぶ。
- 実ABI/RTTI/layout/constructor invariantを含める。
- Heart/Live差分検証にも使う。

## Consequences

良い点:

- それぞれの意味契約が明確になる。
- 危険な中間案を避けられる。
- 変換不能時のfallbackが説明しやすい。

悪い点:

- 利用者に2モードの違いを説明する必要がある。
- diff検証にはobserver/factoryが必要。

## References

- `../design/01_semantic_contract.md`
- `../design/07_live_mode.md`

---

# File: docs/adr/0004_generate_new_code_do_not_modify_product_code.md

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

---

# File: docs/adr/0005_compile_database_as_primary_input.md

# ADR-0005: compile_commands.json を主要入力にする

## Status

Accepted

## Context

C++のAST解析には、include path、macro definition、language standard、target triple、コンパイルオプションが必要である。これらが元プロジェクトと異なると、名前解決やtemplate instantiationが変わりうる。

## Decision

Aztecaは`compile_commands.json`を主要入力にする。

CLIは`-p <build-dir>`を受け取り、Clang Toolingを使って対象translation unitを解析する。

## Consequences

良い点:

- 元プロジェクトに近い条件でASTを構築できる。
- Clang Toolingと相性が良い。
- CIで再現しやすい。

悪い点:

- build directoryが必要。
- compile databaseを生成しないビルドシステムでは前処理が必要。

## Alternatives considered

### Header-only parse

却下。macro/include/standard設定が不足しやすい。

### 手動include path指定

補助としては可能だが、既定にはしない。

## References

- `../design/02_architecture.md`
- `../design/03_extraction_pipeline.md`

---

# File: docs/adr/0006_single_public_extraction_model.md

# ADR-0006: 単一の公開抽出モデルを採用する

## Status

Accepted

## Context

アステカは、C++メソッドのロジックをAST経由でテスト可能な形へ取り出すツールである。以前の設計では、Heart mode、Live mode、fallback、unsupportedといった分類を明示していた。

しかし、利用者に多くのmodeを選ばせると、ツールの利用が難しくなる。元々の目的はテスタビリティの向上であり、難しいコードを試験するために別の難しい概念群を学ばせるのは本末転倒である。

## Decision

アステカの公開UXでは、原則として単一の抽出操作を採用する。

```bash
azteca extract -p build --method 'C::m(int)'
```

内部では、必要に応じてfield state、dependency boundary、object identity、dispatch、lifetime、byte viewなどの意味モデルを自動的に追加する。

## Consequences

### Positive

- 利用者はmode選択を覚えなくてよい。
- 抽出結果が統一されたself/deps/effectsの形になる。
- 対応範囲拡張が、public API複雑化に直結しない。

### Negative

- 内部実装はより複雑になる。
- レポート品質が重要になる。
- 自動判断を誤らないため、MMIRとEnvelope Plannerが必要になる。

## Rejected Alternatives

### Alternative A: 多数のmodeを公開する

却下理由:

```text
利用者に判断負荷を押し付ける。
```

### Alternative B: Heart modeだけを狭く実装する

却下理由:

```text
単純ではあるが、対応範囲が狭くなりすぎる。
```

### Alternative C: Live modeを主軸にする

却下理由:

```text
実オブジェクト構築の問題から逃げられず、アステカの本来価値を失う。
```

---

# File: docs/adr/0007_semantic_envelope_over_modes.md

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

---

# File: docs/adr/0008_boundary_shims_as_valid_unit_extraction.md

# ADR-0008: dependency boundaryを有効な抽出結果として扱う

## Status

Accepted

## Context

多くのメソッドは、外部関数、他オブジェクト、registry、logger、OS、I/Oなどを呼び出す。これらをすべて再帰抽出しようとすると、ユニットテストではなく統合テストに近づく。

一方で、外部呼び出しがあるたびにunsupportedにすると、実用性が失われる。

## Decision

外部依存は、標準でdependency boundaryとして扱う。

```cpp
external(this, x)
```

は、例えば次のようにloweringする。

```cpp
deps.external(self.object_ref(), x);
effects.record_call("external", self.object_ref(), x);
```

## Consequences

### Positive

- 抽出可能範囲が大きく広がる。
- ユニットテストとして自然な依存注入になる。
- 呼び出し発生、順序、引数を検証できる。

### Negative

- 外部依存の実装そのものは試験対象外になる。
- 戻り値が必要な依存では、テスト側が値を与える必要がある。
- reportで境界化を明示しないと、過剰な正確性を期待させる。

## Rule

Boundary化は失敗ではない。ただし、境界化された依存は必ずreportとmanifestに記録する。

---

# File: docs/adr/0009_mmir_between_clang_ast_and_codegen.md

# ADR-0009: Clang ASTとCodegenの間にMMIRを置く

## Status

Accepted

## Context

Clang ASTから直接生成コードを出すと、意味解析、Envelope計画、コード生成が混ざる。対応構文が増えるほど、lowering ruleが複雑化し、保守性が落ちる。

## Decision

Clang ASTから一度 Method Meaning IR、すなわちMMIRへ変換する。

```text
Clang AST -> MMIR -> Semantic Envelope Plan -> C++ Codegen
```

MMIRは、field access、object identity、boundary call、dispatch call、lifetime operation、byte viewなど、ユニットテストに必要な意味を表す。

## Consequences

### Positive

- 意味抽出とコード生成を分離できる。
- Envelope Plannerを独立してテストできる。
- report/source mapを作りやすい。
- 新しい生成方針を追加しやすい。

### Negative

- 実装量は増える。
- MMIR validationが必要になる。
- ASTとMMIRの二重テストが必要になる。

## Notes

MMIRはコンパイラIRのような最適化用IRではない。目的は、対象メソッドのユニットテスト可能な意味を失わずに正規化することである。

---

# File: docs/adr/0010_gtest_as_default_runner.md

# ADR-0010: Google Testを標準テストランナーにする

## Status

Accepted

## Context

アステカが生成するkernelは、何らかのテストランナーで実行される必要がある。C++では `main` と `assert` だけでもユニットテストは可能だが、利用者に独自runnerを学ばせると導入障壁が上がる。

Google TestはC++テストフレームワークとして広く使われ、CMake/CTest連携もしやすい。

## Decision

アステカの標準生成テストはGoogle Testとする。

```cpp
#include <gtest/gtest.h>
#include "C_m.scenario.hpp"

TEST(C_m, sample) {
    auto s = azteca_gen::scenario::C_m{};
    auto result = s.call(/* args */);
    EXPECT_EQ(result, /* expected */);
}
```

ただし、生成kernel、self、shape、scenario core runtimeはGoogle Testに直接依存しない。

## Consequences

### Positive

- 既存C++プロジェクトに導入しやすい。
- CI/CTest連携が容易。
- 生成テストが読みやすい。
- 独自runner開発を遅らせられる。

### Negative

- Google Testが使えない組込み/クロス環境ではfallbackが必要になる。
- Google TestのdiscoveryやCMake統合に環境差が出る可能性がある。

### Mitigation

- kernel/scenario runtimeをGoogle Test非依存に保つ。
- `azteca_gtest.hpp` を薄いadapterにする。
- 必要時のみstandalone runnerを生成できる余地を残す。

---

# File: docs/adr/0011_dependency_transcript_over_handwritten_fakes.md

# ADR-0011: 手書きfakeではなくDependency Transcriptを標準にする

## Status

Accepted

## Context

抽出したメソッドが大量の依存関係を持つ場合、従来のmock/fake方式では、依存クラスの偽物を大量に作る必要がある。これは、アステカが解決しようとしている「テスタビリティがないコードを苦労して試験する」問題へ逆戻りする。

## Decision

アステカは、依存クラスのfake生成を標準としない。

代わりに、対象メソッドが依存から観測する値と、依存へ送る効果をDependency Transcriptとして扱う。

```cpp
s.when.repo_load(id).returns(OrderShape{...});
s.when.clock_now().returns(Time{900});
s.effects.bus_publish.expect_once(OrderApproved{id});
```

## Consequences

### Positive

- 依存クラスを構築しなくてよい。
- private constructorや巨大ドメインモデルの影響を受けにくい。
- 経路ごとに必要な観測だけを書ける。
- Google Testで読みやすいscenarioが生成できる。

### Negative

- 依存実装そのものの正しさは検証しない。
- 依存interfaceとportが1対1に対応しない場合、利用者が最初に理解する必要がある。
- record/replayなしでは、依存が非常に多い成功経路のscenario作成はまだ手間がかかる。

### Mitigation

- reportにpath-wise stub burdenを出す。
- scenario skeletonを生成する。
- missing observation診断に次に書くべき行を出す。
- 将来的にrecord/replayを導入する。
