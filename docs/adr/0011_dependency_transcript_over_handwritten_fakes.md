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
