# 00. Project Charter

## 1. 目的

Azteca（アステカ）は、C++の非staticメンバ関数を、可能な限り実クラスのインスタンス化なしに単体試験できるようにするテストハーネス生成器である。

C++の通常の呼び出し規則では、非staticメンバ関数は対象オブジェクトを要求する。これは言語仕様として正しいが、ユニットテストの観点では、コンストラクタ、外部資源、巨大な依存グラフ、private状態、継承構造などが邪魔になり、メソッド内部のロジックだけを検証したい場面で過剰な負担になる。

Aztecaの目的は、非staticメンバ関数を「本物の未構築オブジェクトに対して無理に呼ぶ」のではなく、Clang ASTからメソッド本体を抽出し、明示レシーバ関数へ変換することで、メソッド本体のロジックを安定して試験可能にすることである。

## 2. 名前の由来

Aztecaは、クラスからメソッドだけを心臓のように取り出す、という比喩から命名する。

- クラス: 生体
- メソッド: 心臓
- AST lowering: 摘出手術
- self model: 人工循環器
- test driver: 動的解析装置

この比喩は設計の方向性を示すが、実装は比喩に頼らない。すべての変換はAST/Sema後の意味情報と明文化されたlowering ruleに従う。

## 3. 中核思想

Aztecaの中核思想は次である。

```text
非staticメンバ関数を fake this で呼ばない。
メソッド本体をASTから取り出し、明示receiver関数へloweringする。
```

変換前:

```cpp
class C {
    int x_;
public:
    int f(int a) {
        x_ += a;
        return x_;
    }
};
```

変換後:

```cpp
struct C_f_self {
    int x_;
};

int C_f(C_f_self& self, int a) {
    self.x_ += a;
    return self.x_;
}
```

この生成関数は、元の実バイナリ関数そのものではない。元メソッドのASTから、観測可能な制御フロー、状態更新、戻り値計算を移植したテスト用カーネルである。

## 4. 解決したい問題

Aztecaが主に解決する問題は次である。

1. コンストラクタが重すぎて、メソッド単体の試験が難しい。
2. private/protected状態により、任意の内部状態からメソッドを開始できない。
3. 対象メソッドが小さなロジックであっても、巨大な依存グラフ全体を構築させられる。
4. 異常系や境界値を作るために、通常APIでは到達困難な状態を作る必要がある。
5. fuzzerやsanitizerをメソッド単体に集中させたい。

## 5. 非目的

Aztecaは、次を目的にしない。

1. C++標準のオブジェクトモデルを破ること。
2. 未構築ストレージを `C*` として扱い、非staticメンバ関数を呼ぶこと。
3. pointer-to-memberを通常の関数ポインタへ偽変換すること。
4. あらゆるC++構文を初版から完全変換すること。
5. 元製品バイナリの関数そのものを、オブジェクトなしで実行すること。
6. private/protectedを `#define private public` で突破することを中核機能にすること。

ただし、Live modeでは正規に構築された実オブジェクトに対して元メソッドを呼び、Heart modeとの差分検証や実ABI込みのテストを行う。

## 6. モード概要

### 6.1 Heart mode

Heart modeは、Aztecaの本命モードである。

- 元クラスの実インスタンスを作らない。
- `this` を明示的な `self` 引数へ置き換える。
- private/protected状態はself modelとして表現する。
- 依存メソッドは再帰抽出またはstub化する。
- fuzzer、sanitizer、coverageを生成カーネルに適用する。

### 6.2 Live mode

Live modeは、実オブジェクトが本質的に必要な場合の合法モードである。

- 実クラス `C` の正規インスタンスを作る。
- pointer-to-memberを標準的な構文で呼ぶ。
- RTTI、virtual dispatch、実レイアウト、実ABIを含めて検査する。
- Heart modeとの差分検証にも使う。

Live modeは逃げ道ではなく、別の検査対象を持つモードである。

## 7. 利用者像

Aztecaの主要利用者は次である。

- C++製品コードのユニットテストを書きたい開発者
- レガシーコードの内部ロジックを段階的にテストしたい保守担当者
- fuzzing対象を小さく切り出したい検証担当者
- sanitizer/coverageを特定メソッドへ集中適用したい品質担当者
- C++の言語仕様上のUBを避けながら、強いテストハーネスを求める開発チーム

## 8. 成功基準

初期版の成功基準は次である。

1. `compile_commands.json` から対象translation unitを解析できる。
2. `CXXMethodDecl`として対象メソッドを解決できる。
3. 単純なフィールドread/write、if、return、算術式をHeart modeで抽出できる。
4. 同一クラスhelperメソッドを依存として検出できる。
5. `this` escapeなどLive-required条件を正しく分類できる。
6. 生成コードが通常のC++としてコンパイルできる。
7. 最小driverを生成し、sanitizerつきで実行できる。
8. 変換不能な対象を危険な代替で処理せず、明確な診断を出せる。

## 9. 用語

| 用語 | 意味 |
|---|---|
| target method | 抽出対象の非staticメンバ関数 |
| Heart mode | ASTから明示レシーバ関数を生成するモード |
| Live mode | 正規構築オブジェクトで元メソッドを呼ぶモード |
| receiver | 元の暗黙 `this` を置き換える明示引数 |
| self model | receiverが参照するテスト用状態構造体 |
| dependency model | 他メソッド・外部関数・global等の依存表現 |
| lowering | AST上の意味構造を生成コードへ変換すること |
| kernel | 生成されたテスト用関数本体 |
| driver | kernelまたはLive callを実行するテストハーネス |
| manifest | 抽出結果、分類、生成物を記録するJSON |
| classification | Heart可能、Live必須、未対応などの分類 |
| raw this escape | `this` が外部へ `C*` 等として流出すること |
| fallback | Heart modeで扱えない場合の代替策 |

## 10. 初期MVPスコープ

初期MVPでは、以下に集中する。

- 単一translation unit内の通常クラス
- 非templateの通常メンバ関数
- `const` / 非`const` メソッド
- フィールドread/write
- `if` / `return` / 代入 / 算術 / 比較 / 論理演算
- 同一クラスの非virtual helper呼び出しの依存検出
- raw `this` escape検出
- codegen manifest
- smoke test driver生成

初期MVPでは、次は分類だけ行い、完全変換は後続フェーズに送る。

- 継承
- virtual call
- template method
- lambda this capture
- constructor/destructor body
- coroutine
- module
- macro展開領域の複雑な書き換え

## 11. 設計原則

1. **安全性優先**: 迷ったらHeart変換を止め、理由を出す。
2. **意味ベース**: 文字列置換ではなくAST/Sema後の宣言IDに基づく。
3. **分類可能性**: 失敗を単なる失敗にせず、分類とfallbackを提示する。
4. **小さな生成物**: 生成コードは通常のC++として読みやすくする。
5. **差分検証**: 可能ならHeartとLiveの観測結果を比較する。
6. **ルール駆動**: loweringはルール台帳とfixtureで管理する。
7. **過剰約束禁止**: 元製品バイナリそのものをオブジェクトなしで動かせるとは言わない。

## 12. 参照資料

- Clang LibTooling: https://clang.llvm.org/docs/LibTooling.html
- Clang AST Matchers: https://clang.llvm.org/docs/LibASTMatchers.html
- C++ draft non-static member functions: https://eel.is/c++draft/class.mfct.non.static
- C++ draft object lifetime: https://eel.is/c++draft/basic.life
