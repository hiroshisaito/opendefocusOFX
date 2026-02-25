# 開発履歴 (HISTORY_DEV.md)

## プロジェクト概要

OpenDefocus（Rust製コンボリューションライブラリ）を、Nuke NDKプラグインからOpenFXプラグインへ移植するプロジェクト。

- プロジェクトルート: `/Volumes/RAID/develop/ofx/oepndefocus_ofx`
- プラットフォーム: Rocky Linux 8.10 (x86_64)

## バージョン管理ルール

OpenDefocus原本のバージョンと移植版のバージョンは同期させる。
移植版には `-OFX-v<リビジョン>` サフィックスを付与する。

- 書式: `v<原本バージョン>-OFX-v<リビジョン>`
- 例: OpenDefocus v0.1.10 に対する移植版初回リリース → `v0.1.10-OFX-v1`
- 原本がバージョンアップした場合、移植版も追従し OFX リビジョンは v1 にリセットする
  - 例: 原本が v0.1.11 に上がった場合 → `v0.1.11-OFX-v1`
- 同一原本バージョンに対してOFX側のみ修正が入った場合はリビジョンをインクリメントする
  - 例: OFX側バグ修正 → `v0.1.10-OFX-v2`

現在の対象: **OpenDefocus v0.1.10** → 移植版 **v0.1.10-OFX-v1** (開発中)

## ディレクトリ構成

```
oepndefocus_ofx/
├── upstream/
│   ├── opendefocus/    # OpenDefocus原本 (v0.1.10, Rust/wgpu)
│   └── openfx/         # OFX SDK (include/, Support/)
├── plugin/
│   └── OpenDefocusOFX/ # OFX移植先（C++実装）
│       ├── src/
│       ├── include/
│       └── cmake/
├── build/               # ビルド出力用
├── bundle/
│   └── OpenDefocus.ofx.bundle/
│       └── Contents/
│           └── Linux-x86-64/   # OFXバンドル配置先
└── references/          # 設計ドキュメント・背景資料
```

## 時系列

### 2026-02-23: プロジェクト初期セットアップ

#### 背景資料の整備

以下のPDFドキュメントを作成し `references/` に配置:

1. **Rocky Linux 8.10 OFXプラグイン開発ガイド.pdf** — 開発環境の構築手順
2. **OpenDefocus OFX 移植ガイド.pdf** — 移植の全体方針
3. **NDKからOFXへの移植とOpenDefocus.pdf** — NDK→OFX移植の技術的背景
4. **OFXプラグイン移植作業セットアップ.pdf** — 作業環境セットアップ

#### 運用ドキュメントの作成

- **OpenDefocus_OFX_移植セットアップ_2章分離.md** — 手順混在防止のため、現状（第1章: Nuke原本）と移植先（第2章: OFX実装）を明確に分離した一次手順書
- **references/README.md** — references利用ガイド（一次手順は2章分離版を優先する旨を明記）

#### プロジェクトツリーの作成

以下のディレクトリ構成を構築:

- `upstream/opendefocus` — OpenDefocus原本を配置
- `upstream/openfx` — OFX SDKを配置
- `plugin/OpenDefocusOFX/{src,include,cmake}` — OFX移植先の空テンプレート
- `build/` — ビルド出力用（空）
- `bundle/OpenDefocus.ofx.bundle/Contents/Linux-x86-64/` — OFXバンドル構造を事前作成

### 2026-02-23: Phase 1 — OFX スケルトン実装

- OFX C++ Support Library を使用した OFX プラグインスケルトンを作成
- `OpenDefocusOFX.cpp`: ImageEffect / PluginFactory の実装（パススルー動作）
- `CMakeLists.txt`: OFX SDK 参照、バンドル生成の自動化
- クリップ定義: Source (必須, RGBA), Depth (オプション, RGBA/Alpha), Output (RGBA)
- パラメータ: Size (defocus radius), Focus Plane
- コンテキスト: General (多入力), Filter (単入力)
- GCC Toolset 13 でビルド成功、OFX エントリポイント (`OfxGetNumberOfPlugins`, `OfxGetPlugin`) のエクスポート確認

### 2026-02-23: Phase 2 — Rust FFI ブリッジ実装

OpenDefocus Rust コアライブラリと C++ OFX プラグインを extern "C" FFI で接続。

#### Rust FFI クレート新設 (`rust/opendefocus-ofx-bridge/`)

- `crate-type = ["staticlib"]` で `.a` ファイルを生成
- cbindgen による C ヘッダ自動生成 (`include/opendefocus_ofx_bridge.h`)
- CPU のみ（`default-features = false, features = ["std", "protobuf-vendored"]`）
- tokio runtime `block_on()` で async → sync ブリッジ

**extern "C" API (9関数):**

| 関数 | 役割 |
|------|------|
| `od_create` / `od_destroy` | インスタンスのライフサイクル管理 |
| `od_set_size` | defocus サイズ設定 |
| `od_set_focus_plane` | 焦点面設定 |
| `od_set_defocus_mode` | 2D / Depth モード切替 |
| `od_set_quality` | レンダリング品質プリセット |
| `od_set_resolution` | 解像度設定 |
| `od_set_aborted` | レンダリング中断シグナル |
| `od_render` | メインレンダリング（in-place バッファ更新） |

#### C++ OFX プラグイン更新

- `OdHandle rustHandle_` メンバ追加（opaque ポインタ / `Box<OdInstance>` ↔ `*mut c_void`）
- コンストラクタで `od_create()`、デストラクタで `od_destroy()`
- `render()`: OFX 画像 → 連続 f32 バッファ → `od_render()` → OFX 出力にコピー
- Depth クリップ: RGBA/Alpha 両対応、単チャンネル抽出
- フォールバック: size ≤ 0 またはハンドル不正時はパススルー

#### CMake 更新

- `add_custom_command` で `cargo build` を実行
- Rust staticlib (`libopendefocus_ofx_bridge.a`) をリンク
- システムライブラリ: `pthread`, `dl`, `m`

#### ビルド結果

- `OpenDefocus.ofx`: 15MB（Rust コア含む）
- OFX エントリポイント + FFI 関数 9個すべてエクスポート確認済み
- 動的依存: libpthread, libdl, libstdc++, libm, libc（すべて標準）

#### ビルド中に解決した問題

- **protoc 未インストール**: `protobuf-vendored` feature で回避
- **Rust 型不一致 (4件)**: prost 生成コードの命名規則差異を修正
  - `Option<bool>` → `bool`, `TwoD` → `Twod`, `Quality` enum 型, `Option<UVector2>` → `UVector2`

### 2026-02-25: UAT 実施 — NUKE 16.0 / Flame 2026

テスト担当: Hiroshi。詳細は `UAT_checklist.md` を参照。

#### UAT 前の修正

- プラグイン名を `OpenDefocus` → `OpenDefocusOFX` に変更（NUKE NDK版との名称衝突回避）
- バンドル名を `OpenDefocusOFX.ofx.bundle` / `OpenDefocusOFX.ofx` に変更
- プラグインID (`com.opendefocus.ofx`) は変更なし

#### UAT 結果サマリー（最終）

| カテゴリ | 結果 |
|---------|------|
| プラグイン読み込み (4項目) | 3 PASS / 1 N/A |
| クリップ接続 (4項目) | 4 PASS |
| パラメータ動作 (6項目) | 6 PASS |
| 2D モード (3項目) | 2 PASS / 1 DEFERRED |
| Depth モード (5項目) | 3 PASS / 1 DEFERRED / 1 N/A |
| レンダリング品質 (5項目) | 4 PASS / 1 DEFERRED |
| NUKE版比較 (4項目) | 1 PASS / 3 DEFERRED |
| 安定性 (4項目) | 4 PASS |

FAIL 項目はすべて解消済み（修正 PASS、対応不要 N/A、または upstream 依存 DEFERRED に分類）。

#### 検出された問題と最終対応

**1. Depth Alpha 入力 (5.5) — N/A**

- 修正: `depthClip_->getPixelComponents()` → `depth->getPixelComponents()` に変更
- 結論: Flame/NUKE ともに Depth は RGBA で入力されるため、Alpha 単独入力は運用上発生しない。N/A に分類

**2. Flame ノード名表示 (1.1, 1.2) — 修正完了 PASS**

- 経緯: `"Filter"` → `"Filter/OpenDefocusOFX"` → 最終的に `"OpenDefocusOFX"` に変更
- 結果: Flame / NUKE ともに正しいノード名で表示されることを確認

**3. NUKE NDK版とのピクセルドリフト (4.3, 5.4, 6.1, 7.1-7.3) — DEFERRED**

- 症状: NUKE NDK版と OFX 版で約1pxのオフセット差
- 分析: OFX 版は OFX 標準座標系で正しく動作。NDK 版は NUKE の pixel center 0.5 オフセットに起因する可能性
- 2K以上では目視で判別困難。OFX 版のリリースブロッカーではない
- upstream 座標系の調査後に再検証予定

**4. プラグイン説明文 (1.4) — N/A**

- `setPluginDescription()` はホスト依存の表示。OFX 側の実装は正しい。対応不要

## ディレクトリ構成（更新）

```
oepndefocus_ofx/
├── upstream/
│   ├── opendefocus/    # OpenDefocus原本 (v0.1.10, Rust/wgpu)
│   └── openfx/         # OFX SDK (include/, Support/)
├── rust/
│   └── opendefocus-ofx-bridge/  # Rust FFI クレート (staticlib)
│       ├── src/lib.rs           # extern "C" 関数群
│       ├── build.rs             # cbindgen ヘッダ生成
│       ├── include/             # 自動生成 C ヘッダ
│       ├── Cargo.toml
│       └── cbindgen.toml
├── plugin/
│   └── OpenDefocusOFX/
│       ├── src/OpenDefocusOFX.cpp  # OFX プラグイン本体
│       └── CMakeLists.txt
├── build/
├── bundle/
│   └── OpenDefocusOFX.ofx.bundle/
│       └── Contents/Linux-x86-64/OpenDefocusOFX.ofx  # 15MB
├── UAT_checklist.md             # UAT チェックリスト
└── references/
```

### 2026-02-25: Phase 3 — Quality + Bokeh パラメータ追加

Phase 2 の Size / FocusPlane のみの最小構成から、Quality と Bokeh 形状パラメータ（計14個）を追加。

#### Rust FFI 追加 (`rust/opendefocus-ofx-bridge/src/lib.rs`)

- `OdFilterType` enum 追加 (`Simple=0, Disc=1, Blade=2`)
- 新規 FFI 関数 12個追加（合計 21 FFI 関数）:

| 関数 | 役割 |
|------|------|
| `od_set_samples` | レンダリングサンプル数 (Quality=Custom 時) |
| `od_set_filter_type` | フィルタタイプ (Simple/Disc/Blade) — 内部で FilterMode + bokeh FilterType を一括設定 |
| `od_set_filter_preview` | フィルタプレビューモード |
| `od_set_filter_resolution` | Bokeh フィルタ解像度 |
| `od_set_ring_color` | Bokeh リングカラー |
| `od_set_inner_color` | Bokeh 内側カラー |
| `od_set_ring_size` | Bokeh リングサイズ |
| `od_set_outer_blur` | Bokeh 外側ブラー |
| `od_set_inner_blur` | Bokeh 内側ブラー |
| `od_set_aspect_ratio` | Bokeh アスペクト比 |
| `od_set_blades` | 絞り羽根枚数 |
| `od_set_angle` | 絞り羽根角度 |
| `od_set_curvature` | 絞り羽根曲率 |

**Filter Type の内部マッピング** (Nuke NDK版と同一パターン):
- Simple → `render.filter.mode = Simple`
- Disc → `render.filter.mode = BokehCreator` + `bokeh.filter_type = Disc`
- Blade → `render.filter.mode = BokehCreator` + `bokeh.filter_type = Blade`

#### C++ OFX プラグイン更新 (`plugin/OpenDefocusOFX/src/OpenDefocusOFX.cpp`)

**Controls ページ (既存ページに追加):**

| パラメータ | OFX 型 | デフォルト | 範囲 |
|-----------|--------|-----------|------|
| Quality | Choice (Low/Medium/High/Custom) | 0 (Low) | — |
| Samples | Int | 20 | 1–256 |

**Bokeh ページ (新規):**

| パラメータ | OFX 型 | デフォルト | 範囲 |
|-----------|--------|-----------|------|
| Filter Type | Choice (Simple/Disc/Blade) | 0 (Simple) | — |
| Filter Preview | Boolean | false | — |
| Filter Resolution | Int | 256 | 32–1024 |
| Ring Color | Double | 1.0 | 0–1 |
| Inner Color | Double | 0.4 | 0.001–1 |
| Ring Size | Double | 0.1 | 0–1 |
| Outer Blur | Double | 0.1 | 0–1 |
| Inner Blur | Double | 0.05 | 0–1 |
| Aspect Ratio | Double | 1.0 | 0–2 |
| Blades | Int | 5 | 3–16 |
| Angle | Double | 0.0 | -180–180 |
| Curvature | Double | 0.0 | -1–1 |

**条件付き有効/無効 (`changedParam` + `updateParamVisibility`):**
- Samples: Quality=Custom の時のみ有効
- Bokeh パラメータ: 常に有効（NUKE NDK 版準拠）

#### ビルド中に解決した問題

- **`blades` フィールド型不一致**: protobuf 生成コードでは `u32` だが plan では `i32` と想定。FFI 関数の引数を `u32` に修正

#### ビルド結果

- Rust crate: ビルド成功、C ヘッダ (`opendefocus_ofx_bridge.h`) に `OdFilterType` + 12関数を正常生成
- C++ OFX: ビルド成功
- FFI 関数 21個 + OFX エントリポイント 2個すべてエクスポート確認済み

### 2026-02-25: Phase 3 UAT 実施

テスト担当: Hiroshi。詳細は `UAT_checklist.md` セクション 9–12 を参照。

#### UAT 結果サマリー

| カテゴリ | 結果 |
|---------|------|
| Quality パラメータ (4項目) | 4 PASS |
| Filter Type (6項目) | 4 PASS / 2 FAIL |
| Bokeh Shape (7項目) | 7 PASS |
| Blade 専用 (4項目) | 3 PASS / 1 N/A |

#### 検出された問題と対応

**1. Filter Preview はみ出し (10.5) — 修正 PASS**

- 症状: Filter Preview 有効時、Bokeh シェイプが画面全体に広がりはみ出す
- 原因: Rust コアの `render_preview_bokeh` は渡されたバッファのフルサイズに描画する仕様。OFX 版はフル出力解像度（2K/4K）のバッファを渡していた
- 修正: Filter Preview 時は `filter_resolution` サイズ（デフォルト 256px）の小バッファで Bokeh をレンダリングし、出力画像の中央にコピー。Filter Resolution パラメータでプレビューサイズを調整可能

**2. グレーアウトで復帰不可 (10.6) — 修正 PASS**

- 症状: Filter Preview 有効状態（Disc/Blade）から Simple に切り替えると、Filter Preview がグレーアウトして元に戻せない
- 原因: `updateParamVisibility()` が Filter Type=Simple 時に Bokeh パラメータ群をグレーアウトしていた
- 修正: NUKE NDK オリジナル版に合わせて Bokeh パラメータのグレーアウトを廃止。Quality=Custom の Samples のみグレーアウト制御を維持

**3. Disc 時の Blade パラメータグレーアウト (12.4) — N/A**

- 10.6 の修正に伴いグレーアウト廃止。パラメータは常に有効（NUKE NDK 準拠）

#### ピクセルドリフト追加知見

NUKE NDK オリジナル版ではデフォルトで GPU レンダリングが有効であり、GPU 有効時にピクセルドリフトが発生することが判明。OFX 版（CPU のみ）では正しい座標で動作しているため、ドリフトは NDK 版の GPU レンダリングに起因する可能性が高い。

### 2026-02-25: Phase 4 — Defocus 一般 + Advanced パラメータ追加

Phase 3 の Quality + Bokeh に続き、Defocus 一般パラメータ（8個）と Advanced パラメータ（2個）、計10個を追加。

#### Rust FFI 追加 (`rust/opendefocus-ofx-bridge/src/lib.rs`)

- `OdMath` enum 追加 (`Direct=0, OneDividedByZ=1, Real=2`)
- `OdResultMode` enum 追加 (`Result=0, FocalPlaneSetup=1`)
- 新規 FFI 関数 9個追加（合計 30 FFI 関数）:

| 関数 | 役割 |
|------|------|
| `od_set_math` | 深度計算モード — 内部で `use_direct_math` + `circle_of_confusion.math` を一括設定 |
| `od_set_result_mode` | レンダリング結果モード (Result/FocalPlaneSetup) |
| `od_set_show_image` | ソース画像オーバーレイ表示 |
| `od_set_protect` | 焦点面保護範囲 |
| `od_set_max_size` | 最大 defocus 半径 |
| `od_set_gamma_correction` | Bokeh 強度のガンマ補正 |
| `od_set_farm_quality` | Farm/バッチレンダリング品質プリセット |
| `od_set_size_multiplier` | defocus 半径の倍率 |
| `od_set_focal_plane_offset` | 焦点面オフセット |

**Math の内部マッピング** (`od_set_filter_type` と同一の compound setter パターン):
- Direct → `defocus.use_direct_math = true`
- 1÷Z → `defocus.use_direct_math = false` + `circle_of_confusion.math = OneDividedByZ`
- Real → `defocus.use_direct_math = false` + `circle_of_confusion.math = Real`

#### C++ OFX プラグイン更新 (`plugin/OpenDefocusOFX/src/OpenDefocusOFX.cpp`)

**Controls ページ (既存ページに追加):**

| パラメータ | OFX 型 | デフォルト | 範囲 |
|-----------|--------|-----------|------|
| Mode | Choice (2D/Depth) | 0 (2D) | — |
| Math | Choice (Direct/1÷Z/Real) | 1 (1/Z) | — |
| Render Result | Choice (Result/Focal Plane Setup) | 0 (Result) | — |
| Show Image | Boolean | false | — |
| Protect | Double | 0.0 | 0–10000 |
| Maximum Size | Double | 10.0 | 0–500 |
| Gamma Correction | Double | 1.0 | 0.2–5.0 |
| Farm Quality | Choice (Low/Medium/High/Custom) | 2 (High) | — |

**Advanced ページ (新規):**

| パラメータ | OFX 型 | デフォルト | 範囲 |
|-----------|--------|-----------|------|
| Size Multiplier | Double | 1.0 | 0–2 |
| Focal Plane Offset | Double | 0.0 | -5–5 |

**Mode パラメータのロジック変更:**
- 従来: Depth クリップ接続の有無で自動判定
- Phase 4: Mode パラメータ (2D/Depth) で明示的に選択
  - Mode=2D: 常に 2D（Depth 接続の有無に関わらず）
  - Mode=Depth + Depth 接続: DEPTH モード
  - Mode=Depth + Depth 未接続: 2D にフォールバック（エラーなし）

**条件付き有効/無効 (`changedParam` + `updateParamVisibility`):**
- Math, RenderResult, Protect, MaxSize, FocalPlaneOffset: Mode=Depth の時のみ有効
- ShowImage: Mode=Depth かつ RenderResult=FocalPlaneSetup の時のみ有効
- GammaCorrection, FarmQuality, SizeMultiplier: 常に有効

#### ビルド結果

- Rust crate: ビルド成功、C ヘッダに `OdMath` + `OdResultMode` + 9関数を正常生成
- C++ OFX: ビルド成功
- FFI 関数 30個 + OFX エントリポイント 2個すべてエクスポート確認済み

### 2026-02-26: Phase 4 UAT 実施

テスト担当: Hiroshi。詳細は `UAT_checklist.md` セクション 13–15 を参照。

#### UAT 結果サマリー

| カテゴリ | 結果 |
|---------|------|
| Defocus 一般パラメータ (13項目) | 11 PASS / 1 DEFERRED (13.12) |
| 条件付き有効/無効 (5項目) | 5 PASS |
| Advanced パラメータ (5項目) | 4 PASS / 1 DEFERRED (15.5) |

#### 検出された問題と対応

**1. Gamma Correction 効果なし (13.12) — DEFERRED (upstream 未実装)**

- 症状: 2D/Depth ともに値を変更しても描画に変化なし
- 調査結果: upstream Rust コアの `gamma_correction` フィールドは protobuf 定義のみのデッドフィールド
  - NDK 版でも `create_knob_with_value()` が呼ばれておらず UI knob として作成されていない
  - `ConvolveSettings` 構造体に含まれておらずレンダリングパイプラインに一切渡されていない
  - 注: `catseye.gamma` / `barndoors.gamma` / `astigmatism.gamma` は別フィールドで正常に使用されている（Non-uniform エフェクト用）
- OFX 版の問題ではない。upstream でパイプライン接続されるまで DEFERRED

**2. Focal Plane Offset 効果なし (15.5) — DEFERRED (upstream 未実装)**

- 症状: Mode=Depth 時に値を変更しても描画に変化なし。NDK 版でも同一症状（ユーザー報告で確認）
- 調査結果: upstream Rust コアの `focal_plane_offset` フィールドは protobuf 定義・NDK knob 作成済みだが `ConvolveSettings` に未接続
  - NDK 版では Advanced タブに knob が作成されている（lib.rs line 621-625）
  - しかしレンダリングパイプラインに値が渡されておらず、変更しても効果なし
- OFX 版の問題ではない。upstream でパイプライン接続されるまで DEFERRED

### 2026-02-26: Phase 5 — Bokeh Noise パラメータ追加

Phase 4 の Defocus 一般 + Advanced に続き、Bokeh Noise パラメータ（3個）を追加。

#### Rust FFI 追加 (`rust/opendefocus-ofx-bridge/src/lib.rs`)

- 新規 FFI 関数 3個追加（合計 34 FFI 関数）:

| 関数 | 役割 |
|------|------|
| `od_set_noise_size` | Bokeh ノイズサイズ — `inst.settings.bokeh.noise.size` に設定 |
| `od_set_noise_intensity` | Bokeh ノイズ強度 — `inst.settings.bokeh.noise.intensity` に設定 |
| `od_set_noise_seed` | Bokeh ノイズシード — `inst.settings.bokeh.noise.seed` に設定 (u32) |

**データフロー**: Bokeh Noise は `ConvolveSettings` ではなく `Settings.bokeh.noise` に格納される。`bokeh_creator::Renderer::render_to_array(settings.bokeh, ...)` でフィルタ生成時に使用される（Bokeh シェイプパラメータと同一パス）。

#### C++ OFX プラグイン更新 (`plugin/OpenDefocusOFX/src/OpenDefocusOFX.cpp`)

**Bokeh ページ (既存ページに追加、Curvature の後):**

| パラメータ | OFX 型 | デフォルト | 範囲 |
|-----------|--------|-----------|------|
| Noise Size | Double | 0.1 | 0–1 |
| Noise Intensity | Double | 0.25 | 0–1 |
| Noise Seed | Int | 0 | 0–10000 |

- 条件付き有効/無効: なし（Bokeh パラメータは常に有効、NUKE NDK 準拠）
- OFX パラメータ合計: 29個

#### ビルド結果

- Rust crate: ビルド成功、C ヘッダに 3関数を正常生成
- C++ OFX: ビルド成功
- FFI 関数 34個 + OFX エントリポイント 2個すべてエクスポート確認済み

### 2026-02-26: Phase 5 UAT 実施

テスト担当: Hiroshi。詳細は `UAT_checklist.md` セクション 16 を参照。

#### UAT 結果サマリー

| カテゴリ | 結果 |
|---------|------|
| Bokeh Noise パラメータ (8項目) | 4 PASS / 4 DEFERRED |

#### 検出された問題と対応

**1. Noise パラメータ効果なし (16.2/16.4/16.6/16.8) — DEFERRED (upstream feature フラグ無効)**

- 症状: Disc/Blade 時に Noise Size/Intensity/Seed を変更しても描画に変化なし。Filter Preview にも反映されない。NDK 版でも同一症状
- 調査結果: bokeh_creator クレート (v0.1.17) には Noise の完全な実装が存在する
  - `Renderer::apply_noise()` で Fbm Simplex ノイズを生成・適用（`#[cfg(feature = "noise")]`）
  - `Renderer::render_pixel()` で noise.intensity > 0 かつ noise.size > 0 の場合に `apply_noise()` を呼び出し
- 原因: upstream の `opendefocus` クレートが `bokeh-creator = { default-features = false, features = ["image"] }` で依存
  - `"noise"` feature が含まれていないため、`#[cfg(not(feature = "noise"))]` のスタブ関数がコンパイルされる
  - スタブは `fn apply_noise(&self, _, _, bokeh: f32) -> f32 { bokeh }` — 何もせずそのまま返す
- OFX 版の問題ではない。upstream で `noise` feature が有効化されれば自動的に動作する

### 2026-02-26: Phase 6 — Non-Uniform エフェクト: Catseye + Barndoors

Phase 5 の Bokeh Noise に続き、Non-uniform エフェクトのうち Catseye（7個）と Barndoors（9個）、計16パラメータを追加。

#### Rust FFI 追加 (`rust/opendefocus-ofx-bridge/src/lib.rs`)

- 新規 FFI 関数 16個追加（合計 50 FFI 関数）:

**Catseye (7関数):**

| 関数 | 役割 |
|------|------|
| `od_set_catseye_enable` | Catseye 有効/無効 — `non_uniform.catseye.enable` |
| `od_set_catseye_amount` | Catseye 強度 — `non_uniform.catseye.amount` |
| `od_set_catseye_inverse` | Catseye 反転 — `non_uniform.catseye.inverse` |
| `od_set_catseye_inverse_foreground` | Catseye 前景反転 — `non_uniform.catseye.inverse_foreground` |
| `od_set_catseye_gamma` | Catseye ガンマ — `non_uniform.catseye.gamma` |
| `od_set_catseye_softness` | Catseye ソフトネス — `non_uniform.catseye.softness` |
| `od_set_catseye_dimension_based` | Catseye 画面サイズ基準 — `non_uniform.catseye.relative_to_screen` |

**Barndoors (9関数):**

| 関数 | 役割 |
|------|------|
| `od_set_barndoors_enable` | Barndoors 有効/無効 — `non_uniform.barndoors.enable` |
| `od_set_barndoors_amount` | Barndoors 強度 — `non_uniform.barndoors.amount` |
| `od_set_barndoors_inverse` | Barndoors 反転 — `non_uniform.barndoors.inverse` |
| `od_set_barndoors_inverse_foreground` | Barndoors 前景反転 — `non_uniform.barndoors.inverse_foreground` |
| `od_set_barndoors_gamma` | Barndoors ガンマ — `non_uniform.barndoors.gamma` |
| `od_set_barndoors_top` | Barndoors 上端位置 — `non_uniform.barndoors.top` |
| `od_set_barndoors_bottom` | Barndoors 下端位置 — `non_uniform.barndoors.bottom` |
| `od_set_barndoors_left` | Barndoors 左端位置 — `non_uniform.barndoors.left` |
| `od_set_barndoors_right` | Barndoors 右端位置 — `non_uniform.barndoors.right` |

**データフロー**: Non-uniform 設定は `Settings.non_uniform` に格納される。`settings_to_convolve_settings()` で `NonUniformFlags` / `GlobalFlags` ビットマスクと個別値が `ConvolveSettings` に自動的にマッピングされる。FFI setter は `inst.settings.non_uniform.catseye.*` / `inst.settings.non_uniform.barndoors.*` に直接設定するだけで、フラグ計算は Rust 側が自動実行する。

#### C++ OFX プラグイン更新 (`plugin/OpenDefocusOFX/src/OpenDefocusOFX.cpp`)

**Non-Uniform ページ (新規) — Catseye セクション:**

| パラメータ | OFX 型 | デフォルト | 範囲 |
|-----------|--------|-----------|------|
| Catseye Enable | Boolean | false | — |
| Catseye Amount | Double | 0.5 | 0–2 |
| Catseye Inverse | Boolean | false | — |
| Catseye Inverse Foreground | Boolean | true | — |
| Catseye Gamma | Double | 1.0 | 0.2–4.0 |
| Catseye Softness | Double | 0.2 | 0.01–1.0 |
| Catseye Dimension Based | Boolean | false | — |

**Non-Uniform ページ — Barndoors セクション:**

| パラメータ | OFX 型 | デフォルト | 範囲 |
|-----------|--------|-----------|------|
| Barndoors Enable | Boolean | false | — |
| Barndoors Amount | Double | 0.5 | 0–2 |
| Barndoors Inverse | Boolean | false | — |
| Barndoors Inverse Foreground | Boolean | true | — |
| Barndoors Gamma | Double | 1.0 | 0.2–4.0 |
| Barndoors Top | Double | 100.0 | 0–100 |
| Barndoors Bottom | Double | 100.0 | 0–100 |
| Barndoors Left | Double | 100.0 | 0–100 |
| Barndoors Right | Double | 100.0 | 0–100 |

**条件付き有効/無効 (`changedParam` + `updateParamVisibility`):**
- Catseye: Enable 以外の 6 パラメータは `CatseyeEnable = true` の時のみ有効
- Barndoors: Enable 以外の 8 パラメータは `BarndoorsEnable = true` の時のみ有効

- OFX パラメータ合計: 45個（29 + 16）

#### ビルド結果

- Rust crate: ビルド成功、C ヘッダに 16関数を正常生成
- C++ OFX: ビルド成功
- FFI 関数 50個 + OFX エントリポイント 2個すべてエクスポート確認済み

### 現在のステータス

- **Phase 1 (OFX スケルトン)**: 完了
- **Phase 2 (FFI ブリッジ)**: 完了
- **Phase 2 UAT**: 完了 — FAIL 項目なし
- **Phase 3 (Quality + Bokeh パラメータ)**: 完了
- **Phase 3 UAT**: 完了 — FAIL 項目なし（10.5, 10.6 修正後再テスト PASS）
- **Phase 4 (Defocus 一般 + Advanced)**: 完了
- **Phase 4 UAT**: 完了 — OFX 側の FAIL 項目なし（13.12, 15.5 は upstream 未実装で DEFERRED）
- **Phase 5 (Bokeh Noise)**: 完了
- **Phase 5 UAT**: 完了 — OFX 側の FAIL 項目なし（16.2/16.4/16.6/16.8 は upstream `noise` feature 無効で DEFERRED）
- **Phase 6 (Non-Uniform: Catseye + Barndoors)**: 完了
- **Phase 6 UAT**: 未実施

### 既知の制約

| 制約 | 理由 | 将来対応 |
|------|------|---------|
| CPU のみ | GPU (wgpu) 未統合 | wgpu feature 有効化 |
| タイリング無効 | ROI 拡張未実装 | getRegionsOfInterest 実装 |
| カスタム Bokeh 画像未対応 | Filter 入力クリップ未統合 | 第3クリップ追加 (Image フィルタタイプ) |
| NDK版とのピクセルドリフト | NDK 版 GPU レンダリングに起因 | upstream 調査 |
| Gamma Correction 効果なし | upstream デッドフィールド | upstream でパイプライン接続後に再検証 |
| Focal Plane Offset 効果なし | upstream 未実装 (ConvolveSettings 未接続) | upstream でパイプライン接続後に再検証 |
| Bokeh Noise 効果なし | upstream で bokeh_creator `noise` feature 無効 | upstream で feature 有効化後に再検証 |
| Camera モード未実装 | 段階的追加方針 | 次フェーズで追加 |
| Non-uniform 残り未実装 | Astigmatism (3) + Axial Aberration (3) + InverseForeground (1) | Phase 7 で追加 |
| 残りパラメータ未実装 | 段階的追加方針 | 次フェーズで追加 |

### 次のステップ

1. Phase 6 UAT 実施
2. Phase 7: Non-uniform 残り（Astigmatism + Axial Aberration + InverseForeground）
3. 追加パラメータ（Camera モード等）
4. getRegionsOfInterest 実装（タイリング対応）
5. GPU レンダリング対応
6. ピクセルドリフト調査（upstream NDK版 GPU レンダリング）
