# 25. Phase A Inspect Coverage

このメモは Phase A `azteca inspect` の構文カバレッジ表である。Phase A は kernel/codegen を実装せず、Clang AST/Sema 後情報から Extraction Plan、診断、Google Test preview を安定表示する。

| 構文・意味                          | Phase A handling                                   | 実装状態     | inspect 出力                                   |
| ----------------------------------- | -------------------------------------------------- | ------------ | ---------------------------------------------- |
| implicit `this` field access        | supported                                          | pass         | `receiver_state`, `self_state`, `LR-001`       |
| explicit `this->field` access       | supported                                          | pass         | `receiver_state`, `self_state`, `LR-002`       |
| member field write / compound write | supported                                          | pass         | `read/write`, `LR-003`                         |
| field address taking                | modeled                                            | pass         | `addressable_cell`, `LR-029`                   |
| same-class nonvirtual member call   | supported as recursive candidate                   | pass         | `recursive_helper_candidates`, `LR-007`        |
| dependency member call              | boundary                                           | pass         | query/effect/operation port                    |
| virtual member call                 | modeled                                            | pass         | `dispatch_table`, `object_ref`, `LR-012`       |
| overload                            | supported for inspect naming                       | pass         | overload-disambiguated port names              |
| operator target method              | not_yet_implemented                                | future       | `operator methods are not supported`           |
| const/volatile/ref-qualified target | supported                                          | pass         | `method_qualifier`, `LR-047`                   |
| default argument                    | supported                                          | pass         | `default_argument`, `LR-042`                   |
| constructor call in body            | conservative                                       | conservative | `constructor_call`, `lifetime_state`, `LR-049` |
| destructor target                   | not_yet_implemented                                | future       | target diagnostic                              |
| explicit destructor call            | modeled                                            | pass         | `lifetime_state`, `LR-027`                     |
| local variable                      | supported                                          | pass         | `local_variable`, `LR-014`                     |
| parameter reference                 | supported                                          | pass         | MMIR `ArgRef`                                  |
| lambda without `this` capture       | supported                                          | pass         | `lambda`, `LR-017`                             |
| lambda with `this` capture          | modeled                                            | pass         | `self_state`, `LR-018`                         |
| nested class / namespace target     | supported                                          | pass         | fully qualified method matching                |
| template member target              | not_yet_implemented                                | future       | `AZTECA_TEMPLATE_METHOD`                       |
| class template specialization       | conservative                                       | conservative | `template_specialization`, `LR-033`            |
| dependent type/name                 | not_yet_implemented                                | future       | `dependent_name`, `LR-033`                     |
| `auto`                              | supported                                          | pass         | resolved semantic type, `LR-043`               |
| range-for                           | conservative                                       | conservative | `loop_control_flow`, `LR-016`                  |
| if                                  | supported                                          | pass         | path split, `LR-005`                           |
| switch                              | conservative                                       | conservative | conservative path summary, `LR-015`            |
| for/while/do                        | conservative                                       | conservative | conservative path summary, `LR-015`            |
| return                              | supported                                          | pass         | path terminal, `LR-004`                        |
| break/continue                      | supported for inspect                              | pass         | `break_continue`, `LR-046`                     |
| ternary `?:`                        | supported                                          | pass         | `conditional_operator`, `LR-041`               |
| unary/binary operators              | supported for built-ins                            | pass         | `LR-006`                                       |
| overloaded operator in body         | boundary                                           | pass         | operation/query/effect port, `LR-013`          |
| references/pointers                 | supported when local; modeled for identity/address | pass         | `object_ref` or `addressable_cell` as needed   |
| `std::move` / `std::forward`        | supported as cast, not dependency                  | pass         | `value_category_cast`, `LR-044`                |
| static/functional/C-style cast      | supported                                          | pass         | `cast_expression`, `LR-044`                    |
| `const_cast`                        | boundary                                           | pass         | `const_cast`, `LR-045`                         |
| `dynamic_cast` / `typeid`           | modeled                                            | pass         | `type_tag`, `LR-023` / `LR-024`                |
| `reinterpret_cast`                  | boundary                                           | pass         | `byte_view`, `LR-025`                          |
| throw                               | supported                                          | pass         | `exception_model`, `LR-035`                    |
| try/catch                           | conservative                                       | conservative | conservative exception control flow, `LR-036`  |
| macro expansion                     | conservative                                       | conservative | `macro_source_map`, `LR-034`                   |
| private/protected access            | modeled                                            | pass         | `access_control`, `LR-048`                     |
| coroutine                           | not_yet_implemented                                | future       | `coroutine`, `LR-038`                          |

未対応または保守的な項目は、壊れた plan/preview を出さず、`unsupported_or_modeled_constructs`、`diagnostics`、`confidence`、`control_flow_summary` のいずれかで理由を示す。

Phase A close時点の安定化ゲートは次である。

```text
- dev-clang check
- asan-clang build
- asan-clang ctest
- Phase A golden text/json
- JSON parse validation for inspect --format json
```
