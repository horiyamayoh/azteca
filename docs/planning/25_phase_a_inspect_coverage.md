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
