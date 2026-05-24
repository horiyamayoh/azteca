# Phase A Text Output (frozen contract)

`azteca inspect --format text` の出力構造は本文書で凍結する。golden は
`tests/golden/phase_a/service_handle.inspect.txt` を代表サンプルとして保持
する。Phase A 期間中、セクションの追加削除・並び替え・見出し文字列の変更は
major bump を要する。

## 1. Section order (fixed)

```
Azteca can inspect <qualified_name>.

Azteca phase: <phase>

Extraction result: <result>

Confidence: <confidence>

Generated Google Test preview:
  <sample_test_path>

Receiver state:
  ...

Dependency observations:
  ...

Observable effects:
  ...

Operations:
  ...

Recursive helper candidates:
  ...

Generated shapes:
  ...

Object identity requirements:
  ...

Semantic features:
  ...

Envelope requirements:
  ...

Modeled or boundary constructs:
  ...

Control flow summary:
  if: yes|no
  switch: yes|no
  loop: yes|no
  range-for: yes|no
  try: yes|no
  throw: yes|no
  return: yes|no

Path-wise test burden:
  ...

Rule coverage:
  - <LR-id> [<handling>] observed|not observed[: <note>]

Google Test preview:
  ...

Diagnostics:                    (optional section, only if non-empty)
  - <severity> <code>: <message> (<location>)
```

## 2. `--verbose` 拡張

`--verbose` 指定時、各要素直下に evidence ブロックを追加する。

```
      rule: <rule_id> - <reason>
      certainty: <certainty> [(conservative)]
      source: <file>:<line>:<column>-<file>:<line>:<column>
```

evidence ブロックは無変更で追加 only。Phase A 期間中削除しない。

## 3. `--quiet` 規約

`--quiet` 指定時、stdout は空。終了コードのみで結果を伝える。

## 4. 互換性ルール

1. セクション追加は minor (末尾、Diagnostics の前)
2. セクション削除・並び替え・見出し文字列変更は major
3. 行内 token (`-`, `:`, `->`) の文字種は固定
4. インデント (空白 2 文字) も固定

## 5. 検証

- `tests/golden/phase_a/service_handle.inspect.txt` の golden 一致
- `tests/integration/PhaseAInspect.cmake` の `required_line` リストの存在確認
- 追加 fixture の text golden を入れる場合、本順序に従う
