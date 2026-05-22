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
