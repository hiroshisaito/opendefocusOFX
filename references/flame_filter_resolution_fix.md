# Flame: "Unsupported input resolution mix in node" — 調査記録

## 現象

- **エラーメッセージ**: `Unsupported input resolution mix in node`
- **発生条件**: Filter Type = Image に設定し、Source クリップと異なる解像度のボケ画像（例: 256×256）を Filter クリップに接続した場合
- **影響ホスト**: Autodesk Flame
- **NUKE**: 同一条件で正常動作（エラーなし）

## 結論: Flame プラットフォーム制約

### 他社製プラグインでの同一エラー確認

以下の商用 OFX デフォーカスプラグインでも同じ `Unsupported input resolution mix in node` エラーが発生することを確認:

- **BorisFX Sapphire**
- **Frischluft Lenscare**

これにより、本エラーは **Flame のプラットフォームレベルの制約** であり、OFX API では回避不可能と結論づける。

### 技術的根拠

Flame は OFX アクション（`getClipPreferences`, `getRegionOfDefinition`）が呼ばれる**前に**、グラフ接続レベルで全入力クリップの解像度を検証する。`setSupportsMultiResolution(true)` の宣言は NUKE では有効だが、Flame はこれを尊重しない。

### 試行した OFX レベルの修正（すべて効果なし）

1. `getClipPreferences()` — 出力フォーマットを Source に合わせて明示宣言
2. `getRegionOfDefinition()` — 出力 RoD を Source クリップのみに限定
3. `setClipBitDepth` / `setPixelAspectRatio` のホスト機能チェック付き条件呼び出し

## Flame での Filter Type = Image 使用方法（制約付き）

### 動作条件

- Filter 画像を **Source と同じ解像度** にリサイズしてから接続する
- 例: Source が 1920×1080 なら、Filter 画像も 1920×1080 にリサイズ

### 同一解像度時の既知の問題

1. **フィルター形状の歪み（原因特定済み・upstream 由来）**: NUKE では正方形の Filter 画像（例: 256×256）を使用するため正常だが、Flame では Source と同じ解像度（例: 1920×1080, 16:9）にリサイズが必要。upstream の Rust コアが `filter_resolution` からアスペクト比を計算してカーネル形状に反映するため（`opendefocus-datastructure/src/lib.rs:265`: `filter_aspect_ratio = filter_resolution.x / filter_resolution.y`）、非正方形の Filter 画像は歪む。これは upstream の仕様通りの動作であり、OFX 側で修正するスコープ外。Frischluft Lenscare ではこの歪みは発生しない（独自の Filter 処理を実装しているため）。
2. **"No filter provided" エラー**: Filter クリップの接続を一時的に解除した際に発生する正常な挙動。Filter Type = Image が選択された状態で Filter 入力が切断されると `filterClip_->isConnected()` が false になり、Rust 側に filter データが渡されないため発生する。対処不要。

## 実装状況（コードに残っている変更）

以下の変更はコードに残す。Flame では解決しないが、OFX 仕様準拠として正しく、他のホストとの互換性向上に寄与する。

- `getClipPreferences()` — 出力フォーマット宣言（ホスト機能チェック付き）
- `getRegionOfDefinition()` — 出力 RoD を Source に限定

## 総合判定

**Filter Type = Image は Flame では実質使用不可。DEFERRED に分類。**

- 解像度不一致 → Flame プラットフォーム制約（OFX API で回避不可）
- 同一解像度に合わせても → upstream の filter_aspect_ratio 計算により非正方形画像は歪む
- upstream 修正なしでは正常動作は不可能

## 今後の選択肢

1. **Flame 制約として DEFERRED（現時点の判定）** — 商用プラグインでも同一制約。upstream の設計変更なしでは対応不可
2. **ファイルパスパラメータによる代替** — Filter 画像をクリップではなくファイルパスから読み込む方式。Flame の解像度検証を回避し、正方形画像を直接読み込める。設計変更が大きいため将来的な検討項目

## 参考

- OFX Programming Guide: Clip Preferences Action
- `setSupportsMultiResolution(true)` — OpenDefocusOFX.cpp line 1231
- Filter クリップ定義 — OpenDefocusOFX.cpp lines 1282-1290
- 商用プラグイン調査: BorisFX Sapphire、Frischluft Lenscare で同一エラー確認済み
