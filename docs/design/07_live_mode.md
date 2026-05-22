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
