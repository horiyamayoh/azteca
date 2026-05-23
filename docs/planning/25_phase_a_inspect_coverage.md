# 25. Phase A Inspect Coverage

このメモは Phase A `azteca inspect` の構文カバレッジ表である。Phase A は kernel/codegen を実装せず、Clang AST/Sema 後情報から Extraction Plan、診断、Google Test preview を安定表示する。

| 構文・意味                          | Phase A handling                                   | inspect 出力                                   |
| ----------------------------------- | -------------------------------------------------- | ---------------------------------------------- |
| implicit `this` field access        | supported                                          | `receiver_state`, `self_state`, `LR-001`       |
| explicit `this->field` access       | supported                                          | `receiver_state`, `self_state`, `LR-002`       |
| member field write / compound write | supported                                          | `read/write`, `LR-003`                         |
| field address taking                | modeled                                            | `addressable_cell`, `LR-029`                   |
| same-class nonvirtual member call   | supported as recursive candidate                   | `recursive_helper_candidates`, `LR-007`        |
| dependency member call              | boundary                                           | query/effect/operation port                    |
| virtual member call                 | modeled                                            | `dispatch_table`, `object_ref`, `LR-012`       |
| overload                            | supported for inspect naming                       | overload-disambiguated port names              |
| const/volatile/ref-qualified target | supported                                          | `method_qualifier`, `LR-047`                   |
| default argument                    | supported                                          | `default_argument`, `LR-042`                   |
| constructor call in body            | conservative                                       | `constructor_call`, `lifetime_state`, `LR-049` |
| destructor target                   | not_yet_implemented                                | target diagnostic                              |
| explicit destructor call            | modeled                                            | `lifetime_state`, `LR-027`                     |
| local variable                      | supported                                          | `local_variable`, `LR-014`                     |
| parameter reference                 | supported                                          | MMIR `ArgRef`                                  |
| lambda without `this` capture       | supported                                          | `lambda`, `LR-017`                             |
| lambda with `this` capture          | modeled                                            | `self_state`, `LR-018`                         |
| nested class / namespace target     | supported                                          | fully qualified method matching                |
| template member target              | not_yet_implemented                                | `AZTECA_TEMPLATE_METHOD`                       |
| class template specialization       | conservative                                       | `template_specialization`, `LR-033`            |
| dependent type/name                 | not_yet_implemented                                | `dependent_name`, `LR-033`                     |
| `auto`                              | supported                                          | resolved semantic type, `LR-043`               |
| range-for                           | conservative                                       | `loop_control_flow`, `LR-016`                  |
| if                                  | supported                                          | path split, `LR-005`                           |
| switch                              | conservative                                       | conservative path summary, `LR-015`            |
| for/while/do                        | conservative                                       | conservative path summary, `LR-015`            |
| return                              | supported                                          | path terminal, `LR-004`                        |
| break/continue                      | supported for inspect                              | `break_continue`, `LR-046`                     |
| ternary `?:`                        | supported                                          | `conditional_operator`, `LR-041`               |
| unary/binary operators              | supported for built-ins                            | `LR-006`                                       |
| overloaded operator                 | boundary                                           | operation/query/effect port, `LR-013`          |
| references/pointers                 | supported when local; modeled for identity/address | `object_ref` or `addressable_cell` as needed   |
| `std::move` / `std::forward`        | supported as cast, not dependency                  | `value_category_cast`, `LR-044`                |
| static/functional/C-style cast      | supported                                          | `cast_expression`, `LR-044`                    |
| `const_cast`                        | boundary                                           | `const_cast`, `LR-045`                         |
| `dynamic_cast` / `typeid`           | modeled                                            | `type_tag`, `LR-023` / `LR-024`                |
| `reinterpret_cast`                  | boundary                                           | `byte_view`, `LR-025`                          |
| throw                               | supported                                          | `exception_model`, `LR-035`                    |
| try/catch                           | conservative                                       | conservative exception control flow, `LR-036`  |
| macro expansion                     | conservative                                       | `macro_source_map`, `LR-034`                   |
| private/protected access            | modeled                                            | `access_control`, `LR-048`                     |
| coroutine                           | not_yet_implemented                                | `coroutine`, `LR-038`                          |

未対応または保守的な項目は、壊れた plan/preview を出さず、`unsupported_or_modeled_constructs`、`diagnostics`、`confidence`、`control_flow_summary` のいずれかで理由を示す。
