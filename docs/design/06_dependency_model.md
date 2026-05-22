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

| ID | 種類 | 例 | V3既定方針 |
|---|---|---|---|
| D1 | same-class pure helper | `fee(x)` | 可能なら再帰抽出 |
| D2 | same-class boundary-like helper | `notify(x)` | query/effect/operation port |
| D3 | base-class nonvirtual method | `B::g()` | 再帰抽出またはport |
| D4 | virtual method | `compute(x)` | dispatch query port |
| D5 | static member function | `C::normalize(x)` | pureなら再帰/直接、外部性があればport |
| D6 | free function | `normalize(x)` | pureなら直接、外部性があればport |
| D7 | global read/write | `global_limit` | env portまたはeffect |
| D8 | member object method | `repo_.load(id)` | dependency transcript port |
| D9 | external resource | file/socket/db/time/random | query/effect/operation port |
| D10 | returned dependency object | `repo_.load(id)->amount()` | Shapeまたはexpression-level query |
| D11 | object identity dependency | `return this`, `external(this)` | `object_ref` |
| D12 | template helper | `helper<T>(x)` | specialization単位 |

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
