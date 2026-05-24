# Phase A Inspect JSON Schema v2 (frozen contract)

`azteca inspect --format json` の出力は本文書で凍結する。`schema_version: 2`
かつ `azteca_phase: "A"` の組合せが Phase A 公開契約である。

破壊的変更 (key の削除、enum 値の意味変更、必須化) は schema_version を 3
へ bump する。Phase A 維持期間中は schema_version: 2 を変えない。

JSON Schema 定義本体は `tests/contract/schema_v2/azteca_phase_a.schema.json`
を参照する。本文書はその意味と凍結ルールを宣言する。

## 1. Top-level keys (required, fixed order)

1. `schema_version: 2`
2. `azteca_phase: "A"`
3. `target`
4. `result`
5. `confidence`
6. `receiver_state`
7. `dependency_observations`
8. `observable_effects`
9. `operations`
10. `recursive_helper_candidates`
11. `shape_candidates`
12. `object_ref_requirements`
13. `semantic_features`
14. `unsupported_or_modeled_constructs`
15. `control_flow_summary`
16. `envelope_requirements`
17. `rule_coverage`
18. `paths`
19. `gtest_preview`
20. `diagnostics`

key 順序は安定とする (生成順=表示順)。クライアントは順序に依存してよい。

## 2. Enum 値 (frozen)

- `result`: `extracted` | `extracted-with-conservative-notes` | `invalid-plan`
- `confidence`: `high` | `medium` | `low`
- `azteca_phase`: `A` (Phase A 期間中固定)
- `schema_version`: `2`
- `FieldAccess` (`receiver_state[].access`): `read` | `write` | `read-write` | `address`
- `DependencyKind` (`*[].kind`): `query` | `effect` | `operation` | `recursive-candidate`
- `ConstructHandling`: `supported` | `modeled` | `boundary` | `conservative` | `not-yet-implemented` | `not-meaningful`
- `EnvelopeRequirementKind`: 14 値。`src/plan/ExtractionPlan.cpp` の `to_string`
  と完全に一致
- `DiagnosticSeverity`: `info` | `warning` | `error`

Phase A で enum 値の追加は minor、削除/意味変更は major。

## 3. Evidence object

各要素は `rule_id`, `reason`, `certainty`, `conservative`, `source_range` を
含む `evidence` フィールドを持つ (フラット展開)。

- `rule_id`: `LR-xxx` 形式 (lowering rule の意味分類)
- `certainty`: `certain` | `heuristic` | `conservative`
- `conservative`: bool
- `source_range`: `{begin: {file,line,column}, end: {file,line,column}}`

## 4. stdout 規約

- `--format json` 時、stdout は厳密に JSON 1 オブジェクトのみ
- ANSI escape、log メッセージ、空行などは出力しない
- 全 diagnostic は JSON `diagnostics` 配列に集約 (stderr へは出さない)
- 終端は `}\n`

## 5. 互換性ルール

1. 新規 key 追加は minor (クライアントは未知 key を無視すべき)
2. 既存 key 削除は major
3. 既存 key の型/意味変更は major
4. enum 値の追加は minor、削除は major
5. `schema_version` を変えずに 1-4 の破壊的変更を行ってはならない

## 6. 検証

- `tests/contract/schema_v2/` 配下の JSON Schema validator が CI 必須
- すべての `tests/golden/phase_a/*.inspect.json` は schema validator を pass
  しなければならない
- 追加の fixture を入れる場合も同じ要件
