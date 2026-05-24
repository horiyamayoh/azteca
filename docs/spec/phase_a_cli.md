# Phase A CLI Surface (frozen contract)

Phase A の `azteca` CLI で公開する surface は本文書で凍結する。破壊的変更は
Azteca semver の major bump を要する。

本文書は実装の真実 (source of truth) であり、`docs/design/09_cli_and_outputs.md`
の将来 vision とは独立に Phase A の互換性ラインを定義する。

## 1. Subcommands (Phase A)

| Subcommand | 状態 | 目的                                  |
| ---------- | ---- | ------------------------------------- |
| `inspect`  | 安定 | 対象メソッドの Extraction Plan を表示 |
| `explain`  | 安定 | 診断 ID (`AZT-E*` / `AZT-W*`) の説明  |

Phase A では他の subcommand (`extract`, `scan`, `build`, `test`, `diff`,
`record`, `replay-transcript`) は提供しない。指定された場合は
`AZT-E0002 unknown command` を返す。

## 2. `azteca --help`

`--help` または `-h` を指定すると help を stdout に出す。出力テキストは
`tests/golden/phase_a/cli/help.txt` を golden として凍結する。Phase B 以降で
help を変更する場合、必ず schema/CLI surface 変更とセットで semver bump する。

## 3. `azteca inspect`

```text
azteca inspect -p <build-dir> --method 'C::m(args...)' \
    [--template-args 'T,...'] \
    [--source <file>] \
    [--format text|json] \
    [--verbose|--quiet]
```

オプション意味:

- `-p, --build-dir <dir>` (必須): `compile_commands.json` のあるディレクトリ
- `--method <spec>` (必須): `MethodSpec` 文法に従う対象指定
- `--template-args <csv>` (任意): `--method` 内テンプレート引数の冗長指定
- `--source <file>` (任意): TU の明示。曖昧解消に使用
- `--format <text|json>` (既定 `text`): 出力形式
- `--verbose`: 詳細 evidence 表示。`--format text` のみ意味あり
- `--quiet`: 成功時の stdout/stderr を抑制し、終了コードのみ返す

stdout / stderr 規約:

- `--format text` 時: 出力は stdout、診断は stdout の末尾セクション
- `--format json` 時: stdout は pure JSON のみ。診断は JSON `diagnostics` 配列に集約
- `--quiet` 成功時: stdout/stderr は空。終了コードのみで成功を示す
- `--quiet` 失敗時: stdout は空。stderr に `AZT-E*` 診断を出す
- ユーザ向け CLI エラー (引数不正等) は stderr、`AZT-E*` で識別

## 4. `azteca explain`

```text
azteca explain <diagnostic-id>
```

引数:

- `<diagnostic-id>` (必須): `AZT-E0001` などの公開 ID。既知の内部
  `AZTECA_*` code も alias として受け付け、対応する公開 ID の説明を返す。
  lowering rule の `LR-xxx` は本コマンドの対象外 (将来別コマンドで提供する
  可能性あり)

stdout に説明テキストを返す。未知 ID は `AZT-E0003 unknown diagnostic id` を
stderr に出して exit 1。

## 5. Exit Codes (frozen)

| Code | Symbol                     | 意味                                               |
| ---- | -------------------------- | -------------------------------------------------- |
| 0    | `kSuccess`                 | 正常終了 (warning は許容)                          |
| 1    | `kUserInputError`          | CLI 引数・spec 解析失敗、未知 subcommand 等        |
| 2    | `kCompileDatabaseError`    | `compile_commands.json` 欠落・破損・読込失敗       |
| 3    | `kMethodResolutionError`   | method 未検出 / 曖昧 / 未対応 target (operator 等) |
| 4    | `kExtractionPlanningError` | MMIR/plan 内部検証失敗                             |

Phase A で 5 番以降は予約。追加しない。

## 6. 非対象 (Phase A explicit non-goals)

- kernel/scenario/codegen の出力
- CMakeLists.json/manifest.json 生成
- Google Test 実行
- live mode / diff 検証

これらを要求されたら `AZT-E0002 unknown command` または該当 subcommand の
拒否診断を返す。

## 7. 互換性ルール

1. 既存オプションの削除、意味変更は major bump
2. 既存オプションへの追加 alias は minor
3. 新オプション追加 (既定で off) は minor
4. exit code 数値の再割当は major
5. `--quiet` 時の終了コード意味は変更しない
