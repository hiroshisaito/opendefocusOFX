# OpenDefocus OFX — Known Issues (v0.1.10-OFX-v1)

## 分類

- **Upstream**: OpenDefocus Rust コア由来。OFX 側で修正するスコープ外
- **OFX**: OFX 移植固有のバグ
- **Flame**: Flame プラットフォーム制約
- **Architecture**: アーキテクチャ上の制約

---

## 一覧

| # | 問題 | 分類 | ステータス | 備考 |
|---|------|------|-----------|------|
| 1 | Gamma Correction 効果なし | Upstream | DEFERRED | protobuf 定義のみ、パイプライン未接続 |
| 2 | Focal Plane Offset 効果なし | Upstream | DEFERRED | NDK knob 作成済みだが ConvolveSettings 未接続 |
| 3 | Bokeh Noise 効果なし | Upstream | DEFERRED | `noise` feature flag 無効 |
| 4 | Axial Aberration Type 切替で色変化なし | Upstream | DEFERRED | protobuf/Rust enum off-by-one |
| 5 | NDK/OFX 間 ~1px ピクセルドリフト | Upstream | DEFERRED | 座標系差異 |
| 6 | CPU/GPU 間 ~1px ピクセルドリフト | Upstream | DEFERRED | レンダリング差異 |
| 7 | Size Multiplier 大値でボケ崩壊 | Upstream | DEFERRED | カーネル相互作用 |
| 8 | ストライプ境界シーム | OFX | FIXED | global coordinates で修正済み |
| 9 | Filter Preview オーバーフロー | OFX | FIXED | バッファサイズ修正 |
| 10 | Bokeh パラメータ grayout 復帰不可 | OFX | FIXED | visibility ロジック修正 |
| 11 | Depth モードで Filter Preview 黒 | OFX | FIXED | 状態初期化修正 |
| 12 | Flame: Filter 解像度不一致エラー | Flame | DEFERRED | OFX API で回避不可 |
| 13 | Flame: Filter 画像アスペクト比歪み | Flame+Upstream | DEFERRED | upstream filter_aspect_ratio 計算 |
| 14 | UHD GPU 失敗 (wgpu 128MB 制限) | Architecture | FIXED | stripe rendering で解決 |
| 15 | UHD CPU 極端な低速 | Architecture | FIXED | stripe rendering で解決 |
| 16 | Flame crosshair ドラッグ応答性 | Architecture | IDENTIFIED | OFX API 制約 |

---

## Upstream 由来 (DEFERRED)

### 1. Gamma Correction 効果なし (UAT 13.12)

- protobuf で定義済み (`gamma_correction = 110`)
- NDK 版で knob 未作成、ConvolveSettings 未接続
- Catseye/Barndoors/Astigmatism 個別の gamma は正常動作

### 2. Focal Plane Offset 効果なし (UAT 15.5)

- protobuf で定義済み (`focal_plane_offset = 80`)
- NDK 版で knob 作成済みだが ConvolveSettings 未接続
- NDK 版でも同一症状を確認

### 3. Bokeh Noise 効果なし (UAT 16.2, 16.4, 16.6, 16.8)

- `bokeh-creator` クレートに実装あり (`apply_noise()`, Fbm Simplex)
- upstream の `opendefocus` クレートが `noise` feature flag を有効にしていない
- `#[cfg(not(feature = "noise"))]` スタブが値をそのまま返す
- Noise Size, Noise Intensity, Noise Seed, Filter Preview 内 Noise すべて無効

### 4. Axial Aberration Type 切替で色変化なし (UAT 21.7)

- protobuf: `RED_BLUE=0, BLUE_YELLOW=1, GREEN_PURPLE=2`
- Rust 内部 enum: `RedBlue=1, BlueYellow=2, GreenPurple=3` (`#[repr(u32)]`)
- 変換 match: `1=>RedBlue, 2=>BlueYellow, 3=>GreenPurple`
- protobuf 値 0,1,2 はすべて `_ => RedBlue` にフォールスルー
- レンダリング関数自体は3色すべて正しく実装済み

### 5. NDK/OFX 間 ~1px ピクセルドリフト (UAT 4.3, 5.4, 6.1, 7.1-7.3)

- OFX 標準座標系準拠による ~1px オフセット
- 2K+ 解像度ではほぼ知覚不可能

### 6. CPU/GPU 間 ~1px ピクセルドリフト (UAT 28.6)

- 同一パラメータで CPU/GPU 出力に ~1px 差異
- NDK 版でも同様の挙動

### 7. Size Multiplier 大値でボケ崩壊 (UAT 27.6)

- Size Multiplier を大きな値にするとボケが崩壊またはグレー領域が出現
- Size/MaxSize パラメータ経由で同等のボケサイズにした場合は正常
- NDK 版でも類似症状

---

## OFX 固有 (FIXED)

### 8. ストライプ境界シーム

- **原因**: ストライプ間パディング領域の汚染、position-dependent 効果でのローカル座標問題
- **修正**: source_image スナップショット + global coordinates in stripe RenderSpecs
- **コミット**: f60776d
- **再テスト**: 32.1-32.18 + 32.12a 待ち

### 9-11. Filter Preview / パラメータ関連

すべて修正済み。詳細は HISTORY_DEV_en.md 参照。

---

## Flame プラットフォーム制約 (DEFERRED)

### 12. Filter Type = Image 解像度不一致エラー

- Flame がグラフ接続レベルで入力クリップ解像度を検証
- `setSupportsMultiResolution(true)` を無視
- BorisFX Sapphire, Frischluft Lenscare でも同一エラー確認
- OFX API では回避不可能
- 詳細: `references/flame_filter_resolution_fix.md`

### 13. Filter Type = Image アスペクト比歪み

- Flame の解像度制約により正方形 Filter 画像を使用不可
- Source と同一解像度 (例: 1920×1080) にリサイズすると 16:9 のアスペクト比になる
- upstream `settings_to_convolve_settings()` が `filter_resolution.x / filter_resolution.y` でアスペクト比を計算
- 非正方形画像ではボケ形状が歪む (upstream の仕様通りの動作)
- Frischluft Lenscare は独自 Filter 処理で歪みなし

**総合判定: Filter Type = Image は Flame では実質使用不可**

---

## アーキテクチャ制約

### 14-15. UHD レンダリング (FIXED)

stripe-based rendering で解決済み。詳細は OPTIMIZATION_REPORT.md 参照。

### 16. Flame crosshair ドラッグ応答性 (IDENTIFIED)

- OFX API は `fetchImage()` のみで入力画像にアクセス → ノードツリー全体の再評価が発生
- NDK 版は NUKE のスキャンラインキャッシュに直接アクセス → ゼロコストサンプリング
- OFX API の制約であり、根本的な解決は不可能
- 現状は実用上許容範囲 (UAT 30.19 PASS)
