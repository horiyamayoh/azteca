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
