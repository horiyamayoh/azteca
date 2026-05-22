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
