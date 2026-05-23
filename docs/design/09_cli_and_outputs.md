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
