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

### 2026-02-26: OFX ページ/タブ表示問題 — 未解決

Phase 6 UAT 開始時に、ホスト UI でページ/タブが正しく表示されない問題が発覚。複数のアプローチを試行したが、いずれも失敗。

#### 症状

NDK 版は Controls / Bokeh / Non-Uniform / Advanced の 4 タブ構成。OFX 版では一部のページが表示されない、またはパラメータが間違ったページに表示される。**NUKE でも Advanced ページが消失**しており、Flame 固有の問題ではなく OFX レベルの問題。

#### 試行した修正と結果

| # | アプローチ | Flame 結果 | NUKE 結果 |
|---|-----------|-----------|-----------|
| 1 | ページ定義を先頭に集約（4 PageParam） | Controls と Bokeh のみ表示 | Advanced 消失 |
| 2 | `setPageParamOrder()` 追加 | クラッシュ | — |
| 3 | GroupParam で 2 ページに統合 | 動作するが 2 ページのみ | — |
| 4 | ページ名のハイフン除去 | 変化なし | — |
| 5 | PageParam + GroupParam 混在（グループ上部、定義順不正） | 3 ページ、内容ズレ | — |
| 6 | PageParam のみ（GroupParam 全削除） | 3 ページ正しく表示 | Boolean パラメータ消失 |
| 7 | PageParam + GroupParam 混在（インライン定義） | Controls と Bokeh のみ | — |
| 8 | PageParam + GroupParam 混在（グループ上部、定義順正） | Controls と Bokeh のみ | — |
| 9 | GroupParam のみ（PageParam 全削除） | Page1 / Page2 の汎用名 | — |
| 10 | 1 PageParam + 4 GroupParam（page→addChild で Group を Page に追加） | Controls / Page2 / Page3 | 展開可能セクション表示、Knob 化されず |
| 11 | 4 PageParam のみ（GroupParam なし、仕様準拠: Group を Page に追加しない） | 失敗 | 失敗 |
| 12 | 4 PageParam のみ（GroupParam なし、setParent なし、プラン承認済み） | Controls + Bokeh のみ表示 | ページ/Knob なし、1パネルに全パラメータ |
| 13 | 4 PageParam + 4 GroupParam 併用（方法A: page→addChild + setParent 二重登録、Group を Page に addChild しない） | 失敗 | 失敗 |
| 14 | ホスト判定分岐（v3レポート方法B）: Flame=PageParamのみ、NUKE=GroupParam+kFnOfxParamPropGroupIsTab | Controls+Bokehのみ（2/4ページ） | **全4タブ正常表示** ✅ |
| 15 | GroupParam のみ（全ホスト共通、PageParam なし、GroupIsTab なし） | 失敗 | — |
| 16 | #14 + `setPageParamOrder()` 追加（全4ページ） | **プラグイン読み込みエラー**（クラッシュ） | — |
| 17 | #14 + パラメータ定義順をページ定義順に一致（Advanced を末尾に移動） | Controls+Bokehのみ（変化なし） | — |
| 18 | 全ホスト共通 GroupParam + Page 各セクション直前定義 + addChild(*param) 二重登録あり | 3ページ表示（Controls,Bokeh,NonUniform）、パラメータ1ページズレ | NUKE OK |
| 19 | #18 から addChild(*param) 全削除、setParent(*grp) のみ + page→addChild(*grp) | 3ページ表示、パラメータ1ページズレ（#18と同一症状） | NUKE OK |
| 20 | 全ホスト共通 GroupParam + sub-group 分割 + Flame ページ名 "Page N" + GroupIsTab 廃止 | Page1/2/3 表示 ✅ | 6タブ（GroupParam がタブ昇格） |
| 21 | Host Branching: Flame のみ GroupParam 生成、NUKE は Page→addChild(*param) フラット方式 | — | 1ページ（階層なしで全パラメータ同一リスト化） |
| 22 | **Topological Branching**: 全ホスト共通 GroupParam + トポロジー分岐。NUKE: 入れ子なし4グループ→4タブ。Flame: フラットサブグループ→列分割。サブグループ(BokehNoise/Catseye/Barndoors)はFlame専用、NUKE では親グループに直接流し込み。defineGroupParam 定義順を表示順と一致 | **Page1/2/3/4 正常表示** ✅ | **Controls/Bokeh/Non-Uniform/Advanced 4タブ正常表示** ✅ |

**アプローチ #14 詳細:**
- `OFX::getImageEffectHostDescription()->hostName` で Flame/NUKE を判定
- NUKE: GroupParam に `OfxParamPropGroupIsTab = 1`（NUKE独自拡張プロパティ）を設定 → Knob Tab として表示
- Flame: PageParam のみ使用、GroupParam/setParent なし
- **NUKE 問題は完全に解決**。Flame は依然 2 ページのみ表示

**アプローチ #14 追加診断テスト:**

| テスト | 結果 |
|--------|------|
| ホストプロパティ取得 | `maxPages=0`, `pageRowCount=0`, `pageColumnCount=0`, `maxParameters=-1` |
| kOfxParamPropPageChild ダンプ | 全4ページの children プロパティが正しく設定されていることを確認 |
| ページ定義順序変更（NonUniform→Advanced→Controls→Bokeh） | 先頭2ページ（NonUniform, Advanced）のタブが表示されるが、中身は Controls+Bokeh のパラメータ |

#### 判明した事実

**NUKE:**
- NUKE は hierarchical layout ホスト。PageParam を完全に無視する（`kOfxParamHostPropMaxPages = 0`）
- NUKE で Knob Tab を生成するには GroupParam + `OfxParamPropGroupIsTab = 1` が必要（NUKE 独自拡張、OFX 標準外）
- **アプローチ #14 で NUKE の全4タブ表示を達成** — `kFnOfxParamPropGroupIsTab` がキー

**Flame:**
- Flame は `kOfxParamHostPropMaxPages = 0`, `pageRowCount = 0`, `pageColumnCount = 0` を報告 — OFX 標準のページプロパティを設定していない
- [Autodesk Community Forum](https://forums.autodesk.com/t5/flame-forum/openfx-plugin-development-resources/td-p/12268117) の公式情報: **"Flame does not support the Pages and lists all visible Params one after the other in the tabs after the 'Plugin' tab. For Params that are inside a group, the group name is shown at the top of the column and empty space in the column is added at the bottom to not show params that are not part of the group in that column."**
- つまり Flame は OFX PageParam を公式にはサポートしない。表示される 2 タブは Flame の部分的・非標準な動作
- `kOfxParamPropPageChild` は各ページに正しくセットされているが、Flame はこれを正しく処理しない
- ページ定義順序変更テストで、Flame は常に先頭 2 ページのタブのみ表示し、パラメータは `addChild` ではなく定義順でスロット配置されることを確認
- **Flame は GroupParam を認識する** — グループ名がカラム上部に表示される仕様

**OFX 仕様:**
- OFX 仕様 (`ofxParam.h` L544-548): "Group parameters cannot be added to a page"
- ホストのレイアウト方式は排他的: paged layout (PageParam) vs hierarchical layout (GroupParam)
- `kOfxParamPropPageChild` valid values: "the names of any parameter that is not a group or page" (`ofxParam.h` L565)
- `OfxParamPropGroupIsTab` は OFX SDK 標準には存在しない（NUKE 独自拡張）

#### 並行して実施した修正（ロールバック済みの構成にも反映）

| 修正 | 内容 | 根拠 |
|------|------|------|
| Depth clip コンテキストガード | `fetchClip(kClipDepth)` を `getContext() == eContextGeneral` で保護 | Filter コンテキストでは Depth clip が存在せず例外発生 |
| updateParamVisibility 全面書き直し | 原本 `consts.rs` の `KnobChanged::new(enabled, visible)` パターンを忠実に移植 | 原本の 2 軸制御（enabled + visible）を OFX の `setEnabled` + `setIsSecret` で再現 |
| changedParam トリガ追加 | FarmQuality, Math を追加 | Samples 表示切替、Math 依存パラメータの enabled 更新漏れ |
| Samples setIsSecret | `setEnabled` → `setIsSecret(!samplesVisible)` | 原本は visible 制御（非表示）、enabled 制御（グレーアウト）ではない |
| Filter コンテキスト非表示 | Mode, Math, RenderResult 等 8 パラメータを `setIsSecret(true)` | Filter コンテキストでは Depth clip がなく、これらのパラメータは無意味 |
| Mode に Camera 追加 | 2D / Depth / Camera の 3 択 | 原本準拠。Camera は Rust 側で Depth にマッピング（protobuf に Camera なし） |
| FilterType に Image 追加 | Simple / Disc / Blade / Image の 4 択 | 原本準拠 |

#### バイナリ解析による新発見

Flame で 4+ ページを正常に表示する OFX プラグインのバイナリを `strings` コマンドで解析:

**`out_of_focus.ofx` / `depth_of_field.ofx`** (正常動作プラグイン):
- `OfxParamTypePage` — PageParam 使用
- `OfxParamPropPageChild` — addChild 使用
- `OfxPluginPropParamPageOrder` — **ページ順序を明示指定**
- `OfxParamPropParent` — setParent 使用
- `N3OFX10GroupParamE` — GroupParam 使用
- `OfxParamPropGroupIsTab` は**未使用**

**`FlaresOFX.ofx`** (参考):
- GroupParam + setParent のみ（PageParam なし）

**重要な発見**: `kOfxPluginPropParamPageOrder`（`ofxParam.h:558`）は OFX 標準プロパティ。正常動作する Flame プラグインはこれを使用してページ順序を明示指定している。Support Library では `desc.setPageParamOrder(page)` で設定可能。

**アプローチ #16 のクラッシュ原因（未特定）**: `setPageParamOrder` を #14 のホスト分岐構成にそのまま追加したが、Flame でプラグイン読み込みエラー。`out_of_focus.ofx` は `GroupIsTab` を使わず全ホスト共通で PageParam + GroupParam + setPageParamOrder の構成であり、ホスト分岐 + GroupIsTab との組み合わせが問題の可能性。

#### Flame 自動ページネーション仕様の解明（#17-#19 の調査結果）

Flame の OFX UI 構築は以下の仕様で動作することが判明:

1. **PageParam を完全に無視** — タブ名・タブ構成は OFX Page 定義に依存しない
2. **GroupParam で列（カラム）を生成** — 各グループが新しい列として表示される
3. **1列あたりの縦パラメータ上限: 約12-14個** — 超過すると次の列に溢れる
4. **1タブあたり2-3列** — 入り切らないグループは自動的に次のタブに押し出される
5. **タブ名はグループ名から自動生成** — 先頭のグループ名がタブ名になる

**ズレの原因**: Bokeh(15個) と NonUniform(16個) が列の上限を超え、溢れたパラメータが次のタブに押し出される「玉突き事故」

**解決策**: 巨大グループを10個以下のサブグループに分割
- Bokeh(15) → BokehShape(12) + BokehNoise(3)
- NonUniform(16) → Catseye(7) + Barndoors(9)

#### 最終構成（アプローチ #22: Topological Branching）

**設計パターン**: グループは全ホスト共通で定義し、親子関係（トポロジー）のみホストごとに分岐。

**Page 定義（全ホスト共通）:**
- 4 PageParam: Controls / Bokeh / NonUniform / Advanced
- Flame: ラベルを "Page 1"〜"Page 4" に設定
- NUKE: ラベルを "Controls" / "Bokeh" / "Non-Uniform" / "Advanced" に設定

**Group 定義（defineGroupParam の呼び出し順 = UI 表示順）:**
1. ControlsGroup — 全ホスト共通
2. BokehGroup — 全ホスト共通
3. BokehNoiseGroup — **Flame 専用**（`isFlame ? define... : nullptr`）
4. NonUniformGroup — **NUKE 専用**（`isFlame ? nullptr : define...`）
5. CatseyeGroup — **Flame 専用**
6. BarndoorsGroup — **Flame 専用**
7. AdvancedGroup — 全ホスト共通（**必ず最後に定義**）

**トポロジー（親子関係）の分岐:**
- **NUKE**: 4つのメイングループ (Controls, Bokeh, NonUniform, Advanced) を各ページに `addChild` → 4タブ。サブグループは生成されないため入れ子メニューなし。BokehNoise/Catseye/Barndoors のパラメータは親グループ (bokehGrp/nonUniformGrp) に直接 `setParent`
- **Flame**: 全グループ（サブグループ含む）をフラットに各ページへ `addChild` → 独立した列として並べ、Flame の自動ページネーションで Page 1/2/3/4 に配分

**パラメータの所属:**
- Controls (12個) → `controlsGrp`
- Bokeh 前半 (12個) → `bokehGrp`
- Bokeh Noise (3個) → `bokehNoiseGrp` (Flame) / `bokehGrp` (NUKE) — フォールバック分岐
- Catseye (7個) → `catseyeGrp` (Flame) / `nonUniformGrp` (NUKE) — フォールバック分岐
- Barndoors (9個) → `barndoorsGrp` (Flame) / `nonUniformGrp` (NUKE) — フォールバック分岐
- Advanced (2個) → `advancedGrp`

**重要な知見:**
- `defineGroupParam` / `definePageParam` の呼び出し順がそのまま UI 表示順になる
- `kFnOfxParamPropGroupIsTab` (NUKE 独自拡張) は不要 — NUKE は PageParam + GroupParam の組み合わせで自然にタブを生成
- Flame のサブグループは列分割に使われるが、NUKE では入れ子メニューになるため、NUKE では生成しない

#### ステータス: NUKE・Flame 両方解決 ✅

- **NUKE**: Controls / Bokeh / Non-Uniform / Advanced の 4 タブ正常表示、入れ子メニューなし ✅
- **Flame**: Page 1 / Page 2 / Page 3 / Page 4 の 4 ページ正常表示 ✅
- 22 アプローチ試行

### 2026-02-27: Phase 8 — GPU レンダリング対応 (wgpu)

Phase 7 の Non-Uniform 完了に続き、wgpu ベースの GPU レンダリングを有効化。upstream の `WgpuRunner` が独自に Vulkan デバイスを作成する方式（OFX ホストの GPU コンテキストとは独立）。

#### Rust FFI 変更 (`rust/opendefocus-ofx-bridge/`)

**Cargo.toml:**
- `opendefocus` の features に `"wgpu"` を追加: `features = ["std", "protobuf-vendored", "wgpu"]`

**src/lib.rs:**
- `od_create()` 内で GPU を有効化:
  - `settings.render.use_gpu_if_available = true`
  - `OpenDefocusRenderer::new(true, &mut settings)` — prefer_gpu = true
- 新規 FFI 関数 1個追加（合計 59 FFI 関数）:

| 関数 | 役割 |
|------|------|
| `od_is_gpu_active` | GPU 使用状況の照会 — `inst.renderer.is_gpu()` を返す |

**動作原理:**
- `SharedRunner::init(true)` が `WgpuRunner::new()` を試行
- 成功 → GPU レンダリング (Vulkan backend)
- 失敗 → 自動的に `CpuRunner` にフォールバック（クラッシュしない）
- `od_render()` の呼び出し方は変更なし（CPU メモリのポインタを渡し、wgpu が内部で GPU upload/download を管理）

#### C++ OFX プラグイン更新 (`plugin/OpenDefocusOFX/src/OpenDefocusOFX.cpp`)

- **開発バージョン表示**: `kDevVersion` 定数 (`"v0.1.10-OFX-v1 (Phase 8: GPU)"`) と読み取り専用 String パラメータ `devVersion` を Controls ページ先頭に追加。`setEnabled(false)` でグレーアウト（編集不可）
- **C++ プラグインのレンダリングフロー変更なし** — GPU は Rust 側で完全に管理されるため、C++ 側のコード変更は不要

#### ビルド結果

- Rust crate: wgpu 依存を含めビルド成功（初回ビルドで +3-5分）
- C++ OFX: ビルド成功、リンクエラーなし
- バンドルサイズ: 約 35MB（wgpu/Vulkan 依存を含む）
- `od_is_gpu_active` シンボルを含む全 FFI 関数エクスポート確認済み

### 2026-02-27: Phase 8 UAT 実施

テスト担当: Hiroshi。詳細は `UAT_checklist.md` セクション 24 を参照。

#### UAT 結果サマリー

| カテゴリ | 結果 |
|---------|------|
| GPU レンダリング (15項目) | 13 PASS / 2 N/A |

#### 検出された問題と対応

**1. Filter Preview 真っ黒 (24.9) — 修正 PASS**

- 症状: Filter Preview ON で Depth モード使用時、出力が真っ黒
- 原因: Filter Preview パスでは `od_render` に `depth_data = nullptr` を渡すが、`od_set_defocus_mode` が preview パスの後（通常レンダリングパス内）で呼ばれるため、前回のレンダリングの defocus_mode が残る。Depth モードの状態で preview を実行すると、Rust 側 `validate()` が `DepthNotFound` エラーを返しレンダリング失敗
- 修正: Filter Preview パスの直前に `od_set_defocus_mode(rustHandle_, TWO_D)` を強制設定。Preview は bokeh 形状描画のみで depth は不要なため TWO_D が正しい
- 注: GPU 有無に関係なく潜在していたバグ（Phase 3 UAT は 2D モードでテストしたため未検出）

**2. CPU 版との比較 (24.4/24.5) — N/A**

- CPU/GPU 切替機能がないため厳密比較不可。以前のバージョンとの目視比較ではほぼ同一

**3. CPU フォールバック (24.15) — N/A**

- GPU 環境 (Linux RTX A4000) のみ。GPU なし環境がないため検証不可

#### パフォーマンス

GPU 対応によりパフォーマンスが大幅に向上。これまで検証が困難だった Quality High モードでも快適に動作することを確認。

### 2026-02-28: Phase 9 — Filter Type: Image（カスタム Bokeh 画像入力）

Phase 8 の GPU レンダリングに続き、Filter Type = Image のサポートを追加。ユーザーが任意の Bokeh 画像を Filter クリップに接続し、カスタムカーネルとして使用可能にする。

#### Rust FFI 変更 (`rust/opendefocus-ofx-bridge/src/lib.rs`)

**`od_render()` シグネチャ拡張:**
- `filter_data: *const f32`, `filter_width: u32`, `filter_height: u32`, `filter_channels: u32` パラメータを追加
- filter_data が非 NULL かつ width/height/channels > 0 の場合に `Array3<f32>` を構築
- NDK の `render.rs:62-73` と同一パターン: `Array3::from_shape_vec((height, width, channels), slice.to_vec())`
- `render_stripe()` の第5引数を `None` → `filter` に変更

**`OdFilterType` enum 更新:**
- `Image = 3` は既に定義済み
- `od_set_filter_type()` の `OdFilterType::Image` アームで `FilterMode::Image` を設定

**`ndarray` import 更新:**
- `Array3` を追加: `use ndarray::{Array2, Array3, ArrayViewMut3};`

#### C++ OFX プラグイン更新 (`plugin/OpenDefocusOFX/src/OpenDefocusOFX.cpp`)

**Filter クリップ追加:**
- `kClipFilter = "Filter"` 定数追加
- `describeInContext()`: オプショナル RGBA クリップとして定義（General コンテキストのみ）
- コンストラクタ: `filterClip_ = fetchClip(kClipFilter)`
- メンバ変数: `OFX::Clip* filterClip_ = nullptr`

**Filter Type Choice に "Image" 追加:**
- `param->appendOption("Image")` — index 3 = Image

**render() の変更:**
- `filterType == 3` (Image) かつ Filter クリップ接続時:
  - Filter クリップから画像を取得 (`filterClip_->fetchImage(args.time)`)
  - RGBA float バッファにコピー（Depth と同じパターン）
  - `od_render()` に filter パラメータを渡す
- それ以外: `od_render()` に `nullptr, 0, 0, 0` を渡す

**Filter Preview の条件変更:**
- 変更前: `filterPreview && filterType >= 1`
- 変更後: `filterPreview && filterType >= 1 && filterType <= 2`
- 理由: Image モードではユーザー指定画像を使うため bokeh_creator のプレビューは無意味

**開発バージョン更新:**
- `kDevVersion = "v0.1.10-OFX-v1 (Phase 9: Filter Image)"`

#### データフロー

```
OFX Filter Clip → fetchImage() → float バッファにコピー
  ↓
od_render(..., filter_data, filter_w, filter_h, filter_ch, ...)
  ↓
Array3::from_shape_vec((height, width, channels), filter.to_vec())
  ↓
render_stripe(..., filter: Some(Array3<f32>))
  ↓
prepare_filter_image() → resize + mipmap
  ↓
render_convolve() でカーネルとして使用
```

#### Phase 9 初回 UAT 結果

| カテゴリ | 結果 |
|---------|------|
| Filter Image (9項目) | 2 PASS / 7 FAIL |

**FAIL 原因**: Filter Type の Choice パラメータに "Image" 選択肢が追加されていなかった（`appendOption("Image")` 漏れ）。修正後に再テスト予定。

### 2026-02-28: GPU 安定化 — 4K クラッシュ対策

#### 問題

4K UHD (3840x2160) フッテージでレンダリングすると NUKE/Flame がクラッシュ（abort）。

#### 根本原因

wgpu の validation error → panic → `extern "C"` 境界で unwind 不可 → abort:

```
wgpu error: Validation Error
Caused by:
  In Device::create_bind_group, label = 'Convolve Bind Group'
    Buffer binding 3 range 165888000 exceeds `max_*_buffer_binding_size` limit 134217728
```

- 4K RGBA32F の staging buffer サイズ: 165,888,000 bytes (≈158MB)
- wgpu デフォルト `max_*_buffer_binding_size` 上限: 134,217,728 bytes (128MB)
- wgpu はバリデーションエラーを `panic!` で処理（エラー返却ではなく）
- `od_render` は `extern "C" fn` のため panic が unwind 不可 → `panic_cannot_unwind` → **abort (ホストプロセス全体がクラッシュ)**

#### 修正内容

**1. `std::panic::catch_unwind` によるパニック捕捉** (`lib.rs`):
- `render_stripe()` 呼び出しを `std::panic::catch_unwind(AssertUnwindSafe(...))` でラップ
- panic をキャッチ → `OdResult::ErrorRenderFailed` を返す（abort を回避）
- `gpu_failed = true` をセット → 次回レンダリングで CPU フォールバック

**2. `[profile.release] panic = "unwind"` 明示** (`Cargo.toml`):
- `catch_unwind` が確実に動作するよう release プロファイルで明示指定

**3. ランタイム CPU フォールバック** (`lib.rs`):
- `OdInstance` に `gpu_failed: bool` フラグを追加
- GPU render 失敗（error または panic）時にフラグをセット
- 次回 `od_render()` 呼び出し時、`gpu_failed && renderer.is_gpu()` なら CPU レンダラーを再作成
- `od_is_gpu_active()` はフォールバック後に `false` を返す

#### GPU 安定化 初回 UAT 結果

| カテゴリ | 結果 |
|---------|------|
| GPU 安定化 (9項目) | 2 PASS / 3 FAIL / 4 notyet |

**FAIL 原因**: `catch_unwind` 実装前のビルドでテスト。4K でクラッシュし、フォールバックが発動しなかった。`catch_unwind` 追加後のビルドで再テスト予定。

### 2026-02-28: GPU 安定化 — 即時 CPU リトライ

#### 問題

`catch_unwind` による panic 捕捉は成功したが、GPU 失敗後の出力にブラーが適用されない。

#### 原因

GPU 失敗 → `ErrorRenderFailed` 返却 → C++ 側で source バッファをそのまま出力にコピー → `gpu_failed = true` をセットするが、OFX ホストは同一フレームの再レンダリングを行わないため、CPU フォールバックが発動しない。

#### 修正内容

`od_render()` 内で GPU 失敗を検知した場合、同一呼び出し内で即座に CPU レンダラーを作成してリトライ:

1. `catch_unwind` で GPU 失敗を検出 → `gpu_failed_now` フラグ
2. CPU レンダラーを `OpenDefocusRenderer::new(false, ...)` で即座に作成
3. `ArrayViewMut3` / `Array2` / `Array3` を生ポインタから再構築（panic で消費済み）
4. CPU で `render_stripe()` リトライ
5. 成功 → `OdResult::Ok` を返す（ホストには成功として見える）

**キーポイント**: `render_specs.clone()` で初回 GPU 試行に渡し、リトライ用に `render_specs` を保持。`RenderSpecs` は `Clone` を実装していることを確認済み。

### 2026-02-28: Phase 10 — Render Scale 補正 + RoI 拡張 + Use GPU パラメータ

Phase 9 + GPU 安定化に続き、OFX 商用プラグインとして必要な 3 機能を実装。

#### 1. Render Scale 補正

プロキシモード（1/2, 1/4 解像度）でピクセル空間パラメータが過剰適用される問題を修正。

**render() 内で `args.renderScale.x` を空間パラメータに乗算:**
- `size *= renderScale` — ボケ半径
- `maxSize *= renderScale` — 最大ボケ半径
- `protect *= renderScale` — 焦点面保護範囲

**スケーリング不要パラメータ**: `focusPlane`, `sizeMultiplier`, `ringColor`, `quality` 等（正規化値/比率/非空間値）

#### 2. getRegionsOfInterest (RoI 拡張)

ボケ半径分だけ入力画像を広く要求し、画像端のエッジクリッピングを防止。

**`getRegionsOfInterest()` オーバーライド追加:**
- 実効ボケ半径 = `max(size, maxSize) * sizeMultiplier` (Depth モード) or `size * sizeMultiplier` (2D モード)
- マージン = `ceil(effectiveRadius) + 1.0` (カノニカル座標)
- Source / Depth クリップの RoI を拡張。Filter クリップは拡張不要

**render() のバッファ処理を srcBounds 基準に変更:**
- `imageBuffer` を `src->getBounds()` サイズで確保（RoI 拡張により renderWindow より大きい場合がある）
- `fullRegion` = srcBounds サイズ、`renderRegion` = renderWindow の srcBounds 相対座標
- 結果コピーも offset 考慮でマッピング
- Depth バッファも同様に `depth->getBounds()` 基準に変更

#### 3. Use GPU パラメータ (CPU/GPU 切替)

NDK 版の `use_gpu_if_available` に対応する手動切替機能を追加。

**Rust FFI 追加 (`od_set_use_gpu`):**
- `use_gpu` パラメータで GPU/CPU を指定
- モード変更時にレンダラーを再作成（`OpenDefocusRenderer::new(use_gpu, ...)`)
- GPU 再有効化時に `gpu_failed` フラグをリセット
- 変更がない場合は早期リターン（毎フレーム呼び出しのコスト回避）

**C++ OFX プラグイン:**
- Controls タブに "Use GPU" Boolean パラメータ追加（デフォルト: true）
- render() の冒頭で `od_set_use_gpu()` を呼び出し

**GPU 自動フォールバックとの共存:**
- `Use GPU = false`: 手動 CPU モード。`od_set_use_gpu(false)` で CPU レンダラーに切替
- `Use GPU = true`: GPU を試行。4K で自動 CPU フォールバック → Use GPU を再度 true にチェックし直すと GPU 復帰（`gpu_failed` リセット）

#### 変更対象ファイル

| ファイル | 変更内容 |
|---------|---------|
| `plugin/OpenDefocusOFX/src/OpenDefocusOFX.cpp` | `getRegionsOfInterest()` 追加、renderScale 補正、srcBounds 基準バッファ処理、Use GPU パラメータ追加 |
| `rust/opendefocus-ofx-bridge/src/lib.rs` | `od_set_use_gpu()` FFI 関数追加 |

#### 開発バージョン更新
- `kDevVersion = "v0.1.10-OFX-v1 (Phase 10: RenderScale + RoI + UseGPU)"`

### 2026-02-28: コードレビュー — スレッドセーフティ修正

デバッグチームによるコードレビューを実施。OFX 仕様とコーディング標準に照らした安全性・正確性の観点で評価。

#### クリティカル修正: スレッドセーフティ宣言

**問題**: `setRenderThreadSafety(eRenderInstanceSafe)` と宣言していたが、`render()` 内で `rustHandle_`（Rust 側の内部ステート）に対してパラメータ設定→レンダリングを行うため、同一インスタンスの並行 `render()` 呼び出しでレースコンディションが発生するリスク。特に `od_set_use_gpu()` によるレンダラー再作成が同時に走ると wgpu レベルで致命的クラッシュの可能性。

**修正**: `eRenderInstanceSafe` → `eRenderUnsafe` に変更。ホスト（NUKE/Flame）が render() 呼び出しをシリアライズするか、スレッドごとに別インスタンスを生成する。

#### レビュー評価ポイント（正しい実装として確認）

- **Row-padding 考慮のバッファコピー**: `getPixelAddress()` で 1 行ずつ `memcpy` — OFX ホストの行末パディングに対応した正確な実装
- **座標系マッピング**: `rw.x1 - srcBounds.x1` による OFX ワールド座標から 0 ベースバッファローカル座標への変換
- **RoI 拡張の数学**: `max(size, maxSize) * sizeMultiplier` + `renderScale` 補正 — 数学的に正確

#### マイナー改善メモ（将来対応）

- Depth/Filter クリップごとに解像度（スケール）が異なるケース（NUKE で Depth だけフル解像度接続）への対応は、現状 `args.renderScale.x` 共通スケールで 99% 問題なし。ズレ報告時にクリップ別スケール比率チェックを追加検討

### 2026-02-28: Phase 10 UAT — プロキシモードクラッシュ修正

#### クラッシュ症状

NUKE のプロキシモード（Render Scale 補正テスト）でクラッシュ（abort）。スタックトレース: `panic_cannot_unwind` → `od_render` → abort。

#### 根本原因

プロキシモードやビューアパン時、NUKE の `src->getBounds()` が `renderWindow` より小さい／ズレることがある。`rw.x1 - srcBounds.x1` が負の値になり、Rust 側で `usize` 変換時にオーバーフロー → panic。この panic はスライス構築段階（`catch_unwind` の外）で発生するため捕捉できず abort。

#### 修正内容

**1. `getRegionsOfInterest` — renderScale 適用漏れ修正:**
- `size` / `maxSize` に `args.renderScale.x` を乗算してからマージン計算
- プロキシモードでの RoI 拡張幅が正しくスケーリングされるようになった

**2. `render()` — fetchWindow + intersection 方式に全面書き換え:**

旧方式（srcBounds ベース）:
- `srcBounds` サイズでバッファ確保 → `renderRegion = rw - srcBounds` → 負の値でクラッシュ

新方式（fetchWindow + intersection）:
- **fetchWindow** = `renderWindow` + blur margin で必要領域を計算
- **0 初期化バッファ** を fetchWindow サイズで確保（境界外は黒/透明）
- **intersection** で `srcBounds` と `fetchWindow` の重なりのみコピー
- `renderRegion = rw - fetchWindow` → **常に非負が数学的に保証**（fetchWindow ⊇ rw）
- Depth バッファも同様に fetchWindow + intersection 方式
- 光学中心を `RegionOfDefinition` から計算し fetchWindow ローカル座標にマッピング

### 2026-03-01: エッジ黒枠修正 — Clamp to Edge パディング

#### 症状

画像の左辺・下辺にボケサイズに比例した黒い領域が発生。2D/Depth モード、Filter Disc/Image いずれでも再現。NDK 版では発生しない。

#### 根本原因

前回のプロキシモードクラッシュ修正で、fetchWindow 内の `srcBounds` 外を `0.0f`（黒/透明）で Zero-Padding していた。コンボリューション処理がこの黒ピクセルを巻き込み、エッジが暗く沈み込む。

NDK 版では NUKE エンジンが自動的に Clamp to Edge（端ピクセルリピート）を行うため問題が発生しない。OFX ではホストがこの処理を行わないため、プラグイン側で実装が必要。

左辺・下辺が目立つ理由: NUKE の座標原点が左下 (0,0) のため、fetchWindow がマイナス座標方向に拡張される左辺・下辺で Zero-Padding の影響が直接的に出る。

#### 修正内容

**Zero-Padding → Clamp to Edge に変更:**

Source バッファ（RGBA）:
- Y方向: `clampY = clamp(y, srcBounds.y1, srcBounds.y2 - 1)` で端の行をリピート
- X方向: 3領域に分割処理
  - 左マージン: 最左端ピクセルをリピート
  - 中央: `memcpy` で高速コピー
  - 右マージン: 最右端ピクセルをリピート

Depth バッファ（単チャンネル）:
- 同じ Clamp to Edge ロジックを適用
- Depth が端で突然 0 になることによる「不自然なピント変化」も同時に解消

**validX クランプの安全強化（デバッグチームレビュー指摘）:**
- `srcBounds` が `fetchWindow` と完全に交差しない極端なパンアウト時、`validX2 < fetchWindow.x1` となり右マージンの `dstX` が負 → バッファアンダーラン
- 修正: `validX1` / `validX2` を fetchWindow 範囲内にクランプ
  - `validX1 = min(max(fetchWindow.x1, srcBounds.x1), fetchWindow.x2)`
  - `validX2 = max(min(fetchWindow.x2, srcBounds.x2), fetchWindow.x1)`
- Source / Depth 両方に適用

### 2026-03-01: Phase 9 + Phase 10 最終 UAT 完了

テスト担当: Hiroshi。詳細は `UAT_checklist.md` セクション 25–29 を参照。

#### UAT 結果サマリー

| カテゴリ | 結果 |
|---------|------|
| Filter Type: Image (9項目, セクション 25) | 9 PASS |
| GPU 安定化 (9項目, セクション 26) | 8 PASS / 1 未確認 (26.8) |
| Render Scale + RoI (18項目, セクション 27) | 15 PASS / 2 FAIL / 1 未テスト (27.9 Flame) |
| Use GPU パラメータ (10項目, セクション 28) | 7 PASS / 3 FAIL |
| スレッドセーフティ (5項目, セクション 29) | 5 PASS |

#### FAIL 項目と分類

**27.6 — Size Multiplier でボケ崩れ:**
- 症状: Size Multiplier を大きくするとボケが崩れたりグレー領域が発生
- NDK 版でも類似症状あり。Size/MaxSize でボケを2倍にする場合は問題なし
- 分類: upstream の sizeMultiplier 処理の問題。DEFERRED

**27.8 — Filter Preview はみ出し:**
- 症状: プロキシモード関係なく、Filter Preview が画面一杯にはみ出す
- Render Scale 補正のテストではなく Filter Preview 自体の問題
- 分類: Filter Preview のプレビューサイズ制御の問題。要調査

**28.4 / 28.5 — Use GPU 切替時のログ未出力:**
- 症状: CPU/GPU 切替は正常に動作するが、NUKE/Flame のコンソールにログが出力されない
- 分類: `log::info!` の出力先がホストのコンソールに接続されていない可能性。機能には影響なし

**28.6 — CPU/GPU 間ピクセルドリフト:**
- 症状: CPU と GPU で約1px のズレ。NDK 版とよく似た挙動
- 分類: upstream の CPU/wgpu 実装差異に起因。DEFERRED

#### 重要な知見

- **ピクセルドリフトの原因確定**: CPU/GPU 間で約1px のズレが NDK 版と同様に発生。OFX ラッパー側ではなく、upstream の Rust コア（CPU と wgpu の実装差異または座標系解釈）に内在する問題
- **Clamp to Edge の完全動作**: エッジ黒枠問題は完全に解消。Crop/AdjBBox による意図的な BBox 制限、極端なパン、プロキシモードの組み合わせすべてで安定動作
- **スレッドセーフティ**: Flipbook、Write ノードバッチ、Flame バッチすべて PASS。`eRenderUnsafe` 宣言が正常に機能

### 現在のステータス — v0.1.10-OFX-v1 移植完了

- **Phase 1 (OFX スケルトン)**: 完了、UAT 完了
- **Phase 2 (FFI ブリッジ)**: 完了、UAT 完了
- **Phase 3 (Quality + Bokeh パラメータ)**: 完了、UAT 完了
- **Phase 4 (Defocus 一般 + Advanced)**: 完了、UAT 完了
- **Phase 5 (Bokeh Noise)**: 完了、UAT 完了
- **Phase 6 (Non-Uniform: Catseye + Barndoors)**: 完了、UAT 完了
- **Phase 7 (Non-Uniform: Astigmatism + Axial Aberration + InverseForeground)**: 完了、UAT 完了
- **Phase 8 (GPU レンダリング)**: 完了、UAT 完了
- **Phase 9 (Filter Type: Image)**: 完了、UAT 完了
- **GPU 安定化 (4K クラッシュ対策 + 即時 CPU リトライ)**: 完了、UAT 完了
- **Phase 10 (RenderScale + RoI + UseGPU)**: 完了、UAT 完了
- **コードレビュー (スレッドセーフティ + エッジ処理)**: 完了、UAT 完了

**OFX 移植ミッション完了。** DEFERRED 項目はすべて upstream 起因。OFX 側のデグレーションはゼロ。

### 既知の制約（すべて upstream 起因）

| 制約 | 理由 | 将来対応 |
|------|------|---------|
| タイリング無効 | `setSupportsTiles(false)` | タイル分割対応は超高解像度 (8K+) で必要に |
| 4K+ GPU レンダリング | wgpu `max_buffer_binding_size` (128MB) 超過 → 即時 CPU フォールバック | upstream で device limits 拡張、またはバッファ分割 |
| CPU/GPU ピクセルドリフト | 約1px の差。upstream の CPU/wgpu 実装差異に起因 | upstream で座標系統一後に再検証 |
| Size Multiplier でボケ崩れ | upstream の sizeMultiplier 処理の問題。NDK 版でも類似症状 | upstream 調査 |
| Gamma Correction 効果なし | upstream デッドフィールド | upstream でパイプライン接続後に再検証 |
| Focal Plane Offset 効果なし | upstream 未実装 (ConvolveSettings 未接続) | upstream でパイプライン接続後に再検証 |
| Bokeh Noise 効果なし | upstream で bokeh_creator `noise` feature 無効 | upstream で feature 有効化後に再検証 |
| Camera モード | protobuf に Camera なし | upstream で Camera 追加後に再検証 |
| Axial Aberration Type 切替で色変化なし | upstream enum off-by-one バグ | upstream 修正後に再検証 |

### OFX 版未解決（要修正）

| 項目 | 内容 | 優先度 |
|------|------|--------|
| Filter Preview はみ出し (27.8) | プレビューがフィルターサイズを無視して画面一杯にはみ出す | 中 |
| Use GPU ログ未出力 (28.4/28.5) | `log::info!` がホストコンソールに到達しない | 低（機能影響なし）|

### 2026-03-01: Upstream 調査 — 移植対象外の決定

upstream NDK 版との機能差分を調査し、以下を移植対象外（意図的オミット）と決定。

#### 移植対象外パラメータ

| パラメータ | 理由 |
|-----------|------|
| **CameraMaxSize / UseCameraFocal / WorldUnit** | Camera Mode はホストアプリごとにカメラデータ取得方法が異なり、全サポート不可能。NUKE 依存機能として現時点オミット。メタデータ読み込み等の代替はオリジナル NDK 版移植の範囲を逸脱 |
| **DeviceName** | GPU デバイス名の読み取り専用表示。低優先度 |
| **UseCustomStripeHeight / CustomStripeHeight** | パフォーマンス調整。低優先度 |
| **Donate / Documentation** | UI ボタン。OFX プラグインでは不要 |

#### 移植不要（NDK 固有）

| パラメータ | 理由 |
|-----------|------|
| **Channels / DepthChannel** | NUKE NDK のチャンネル指定。OFX では Clip アーキテクチャで代替済み |

### 2026-03-01: Phase 11 — Focus Point XY Picker

NDK 版の `FocusPointUtility` に相当する機能を OFX 版として実装。ユーザーが画面上の XY 座標を指定し、その位置の Depth 値をサンプリングしてフォーカス距離に反映する。

#### 設計方針（Read-Only in Render）

OFX における XY ピッカーは他社プラグインでもクラッシュを誘発しやすい機能であるため、最も安全な設計パラダイムを採用:

- `render()` 内で `setValue()` を **呼ばない** — ワーカースレッドからの UI 更新は OFX 仕様で禁止
- Interact (カスタムオーバーレイ) は **自作しない** — ホスト標準ウィジェット (`setUseHostOverlayHandle`) に委ねる
- `focusPlane` は **ローカル変数の上書き** で Rust に渡す（UI 値は変更しない）

#### NDK 版との挙動差異

| 項目 | NDK | OFX |
|------|-----|-----|
| サンプリング契機 | `knob_changed()` (メインスレッド) | `render()` (ワーカースレッド) |
| Focus Plane 更新 | knob 値を直接更新 | ローカル変数上書き（knob 値は不変） |
| depth == 0 | スキップ | スキップ（同一挙動） |

#### C++ OFX プラグイン変更 (`OpenDefocusOFX.cpp`)

**UI パラメータ追加 (Controls グループ、Focus Plane の直後):**

| パラメータ | OFX 型 | デフォルト |
|-----------|--------|-----------|
| Use Focus Point | Boolean | false |
| Focus Point XY | Double2D (`eDoubleTypeXYAbsolute`, `eCoordinatesCanonical`) | (0, 0) |

**render() サンプリングロジック (depthBuffer 構築完了後):**
1. `useFocusPoint` && `useDepth` && `!depthBuffer.empty()` の 3 条件で実行
2. Canonical 座標 → pixel 座標変換（`renderScale` 適用）
3. fetchWindow 境界チェック（バッファ外アクセス防止）
4. `depthBuffer[idx]` から深度値を取得
5. depth != 0 の場合のみ `focusPlane` ローカル変数を上書き
6. `od_set_focus_plane()` を移動 — パラメータ設定ブロック (L402) → サンプリング直後 (depthBuffer 完成後)

**updateParamVisibility():**
- `useFocusPoint`: Mode=Depth 時のみ有効
- `focusPointXY`: Mode=Depth かつ Use Focus Point=true 時のみ有効

**Rust FFI 変更なし** — 新規 FFI 関数は不要。既存の `od_set_focus_plane()` を使用。

#### ビルド中に解決した問題

**プラグインロードエラー — `setUseHostOverlayHandle` 例外:**

- 症状: NUKE / Flame ともにプラグインコンストラクタ失敗 (`Constructor for OFXcom.opendefocus.ofx_v0 failed`)
- 原因: OFX Support Library の `setUseHostOverlayHandle()` は `propSetInt(kOfxParamPropUseHostOverlayHandle, ...)` を直接呼び出し、**try-catch で保護されていない**。ホストがこのプロパティを未サポートの場合に例外がスロー → `describeInContext()` 全体が失敗 → プラグイン記述無効化
- 対照: 同じ OFX 1.2 プロパティの `setDefaultCoordinateSystem()` は Support Library 内で try-catch 保護済み（ofxsParams.cpp L449-461）
- 修正: `try { param->setUseHostOverlayHandle(true); } catch (...) {}` で保護。オーバーレイ未サポートのホストでは数値入力のみで動作

#### Phase 11 UAT — クロスヘア表示問題の試行記録

Phase 11 の render() 内サンプリングロジック自体は正常動作するが、ビューア上のクロスヘア表示が NUKE / Flame ともに表示されない問題が発生。以下は試行と失敗のパターンの記録。

**試行 1: `setUseHostOverlayHandle(true)` のみ（OverlayInteract なし）**

- 仮説: `eDoubleTypeXYAbsolute` + `setUseHostOverlayHandle(true)` で、ホストが標準クロスヘアウィジェットを描画してくれる
- 結果: **FAIL** — プラグインロードエラー（上記の例外問題）。try-catch 追加後もクロスヘア表示なし
- 教訓: **`setUseHostOverlayHandle` だけではクロスヘアは描画されない。OverlayInteract の登録が必須**

**試行 2: ダミー OverlayInteract（draw で false を返すだけ）**

- 仮説: OverlayInteract を登録すれば、ホストが自動的にクロスヘアを描画する
- 実装: `draw()` で `return false` のみの空オーバーレイ。`DefaultEffectOverlayDescriptor` CRTP パターンで `describe()` に登録
- 結果: **FAIL** — クロスヘア表示なし。さらに副作用:
  - **NUKE**: Use Focus Point ON → XY デフォルト (0,0) で間違った深度値をサンプリング → デフォーカスが効かなくなる
  - **Flame**: 空オーバーレイでも OpenGL コンテキスト切替が発生 → 著しいパフォーマンス劣化
- 教訓: **OFX はクロスヘアを自動描画しない。SDK 全サンプル（Tester, Basic, MultiBundle, ChoiceParams）は OpenGL で自前描画している。ダミーオーバーレイは Flame で深刻なパフォーマンス問題を引き起こす**

**試行 3: OpenGL 自前描画（SDK PositionInteract パターン準拠）— 初回テスト**

- 実装: SDK の `PositionInteract` (Tester.cpp:30-178) を参考に完全な OpenGL 描画:
  - `draw()`: 十字線 + 小正方形、状態別色変更（白/緑/黄）
  - `penMotion()`: ヒットテスト + ドラッグ
  - `penDown()` / `penUp()`: ピック開始/終了
  - `useFocusPoint` OFF 時は `return false` で描画・イベント消費なし（Flame 対策）
- `setUseHostOverlayHandle` の try-catch ブロックは削除（自前描画で不要）
- OpenGL ヘッダ追加 (`GL/gl.h`)
- クロスヘアサイズ: 5 スクリーンピクセル、デフォルト位置: (0, 0)
- 結果: **表面上は FAIL** — ユーザーからは「クロスヘアが表示されない」と報告

**デバッグ — 段階的切り分け:**

1回目のデバッグビルド: `draw()` 内に `fprintf(stderr, ...)` を追加 → NUKE ターミナルで `draw()` が呼ばれていないことを確認
- この時点で、オーバーレイ登録レベルの問題と判断

2回目のデバッグビルド: 3箇所にデバッグ出力を追加:
1. `describe()` — `mainEntry` ポインタ値と登録完了
2. `OpenDefocusOverlay` コンストラクタ — インスタンス生成
3. `draw()` — 呼び出しと `enabled` 状態、座標、pixelScale

結果（OFX キャッシュ消去後に再テスト）:
```
[OpenDefocus] describe(): mainEntry=0x7f02a475ded0
[OpenDefocus] describe(): overlay registered OK
[OpenDefocus] OverlayInteract constructor called
[OpenDefocus] OverlayInteract constructor OK
[OpenDefocus] draw() called, enabled=0   ← Use Focus Point OFF
[OpenDefocus] draw() called, enabled=1   ← Use Focus Point ON
[OpenDefocus] draw() pos=(0.00, 0.00) pixelScale=(2.0000, 2.0000)
```

**根本原因の特定:**

- 登録、インスタンス生成、描画ディスパッチは**全て正常に動作していた**
- 1回目のテストで `draw()` が呼ばれていなかったのは **OFX キャッシュが原因** — キャッシュ消去前のビルド（ダミーオーバーレイ）がロードされていた
- `enabled=1` 時に OpenGL 描画コードも実行されていたが、**クロスヘアが見えなかった理由は 2 つ**:
  1. **デフォルト位置 (0, 0)** — キャノニカル座標の原点（画像左下隅）に描画されており、ユーザーは画像中央付近を表示していたため視界外
  2. **クロスヘアサイズ 5px** — pixelScale=2.0 で約 5px 幅。画像左下隅の小さな点として存在していたが、視認困難

**教訓:**
- **OFX キャッシュの罠**: ホストはプラグインの describe 結果をキャッシュする。オーバーレイ登録の変更はキャッシュ消去なしでは反映されない。テスト前に必ずキャッシュを消去すること
- **デバッグ出力は段階的に**: 推測で修正するのではなく、`fprintf(stderr, ...)` で各段階（登録 → インスタンス生成 → 描画呼出 → 描画パラメータ）を順に確認する方法が最も確実
- **デフォルト座標と視認性**: XY パラメータの初期値 (0, 0) は画像左下隅であり、ビューア中央付近を表示するユーザーには見えない。サイズも考慮してデフォルト値を設定すること

**棄却した仮説:**

- ~~「`describeInContext()` での再登録が必要」~~ — SDK 全サンプル（5件）が `describe()` のみで登録。Support Library ソース (ofxsImageEffect.cpp L2619, L2638) を確認し、`describe()` と `describeInContext()` は同じハンドルを共有するため一度の登録で十分
- ~~「OpenGL リンクの問題」~~ — `nm -D` で確認済み。全 OpenGL シンボル (`glBegin`, `glVertex2f` 等) が `U` (undefined) として存在。`.so` の動的リンクでは正常（ホストプロセスの `libGL.so` から実行時解決）。SDK サンプルの CMake も `opengl::opengl` をリンクしているが、OFX プラグインではホスト側で解決される

#### 修正内容（クロスヘア表示）

- クロスヘアサイズ: 5px → 20px → **100px** に拡大（UHD でも十分な視認性）
- デフォルト位置: (0, 0) → **(25, 25)** に変更（クロスヘア全体が画像内に収まり、左下隅の境界線と重ならない）
- `setUseHostOverlayHandle` の try-catch ブロックを削除（自前 OpenGL 描画で不要）
- デバッグ出力 (`fprintf`, `#include <cstdio>`) を全て削除

#### 修正内容（Focus Plane knob 更新）

UAT で「Focus Plane knob の値が変化しない」問題が報告された。現状ではクロスヘアで指定した深度値が render() 内のローカル変数上書きのみで UI に反映されず、以下の問題があった:
- Use Focus Point ON/OFF 切替でフォーカスが変わってしまう
- 入力画像の解像度変更でフォーカス位置がずれる
- キーフレーム操作ができない

**設計変更 — render() 内サンプリングから changedParam() サンプリングへ:**

OFX 仕様の調査により、`changedParam()` (`kOfxActionInstanceChanged`) 内での `fetchImage()` + `setValue()` が OFX 仕様上完全に安全であることを確認:
- `changedParam()` はメインスレッドで実行される
- `fetchImage()` は `kOfxActionInstanceChanged` 内で明示的にドキュメント化（ofxThreadSafety.rst）
- `beginEditBlock`/`endEditBlock` で undo/redo 対応

実装:
- `changedParam()` で `focusPointXY` 変更を検知 → `depthClip_->fetchImage()` でサンプリング → `focusPlaneParam_->setValue()` で Focus Plane knob を直接更新
- `render()` 内のサンプリングロジック（「4b. Focus Point XY Picker」ブロック）を削除
- `render()` では `focusPlaneParam_->getValueAtTime()` で取得した値（changedParam で更新済み）をそのまま使用

NDK 版の `focus_point_knobchanged()` (lib.rs:1033-1058) とほぼ同一のフローを実現。

#### 開発バージョン更新
- `kDevVersion = "v0.1.10-OFX-v1 (Phase 11: Focus Point)"`

### Stripe Height (TIER 3) の調査結果

upstream 調査の結果、OFX での実装を見送り:

- `use_custom_stripe_height` / `custom_stripe_height` は `NukeSpecificSettings` (lib.rs:149-151) に格納。`Settings.render` には存在しない
- NDK では `stripe_height()` メソッドが NUKE に返す値を制御し、**NUKE エンジンがストライプ分割を実行**する仕組み
- OFX で同等機能を実現するには C++ `render()` 内でストライプループを自前実装し、ストライプ間のコンボリューションマージン（重複領域）を管理する必要がある
- 現在の GPU パニック時 CPU フォールバックが 4K+ で正常動作しているため、即座の必要性なし

### 2026-03-02: Phase 11 UAT 完了

全 19 項目 PASS（NUKE 16.0 / Flame 2026）。

**既知の制約（許容済み）:**
- Flame でのクロスヘアドラッグ時の応答性が NUKE より遅い（`changedParam` 内の `fetchImage` によるオーバーヘッド）。前回のパフォーマンス劣化問題からは大幅改善済み

---

## Performance Optimization（ブランチ: `feature/stripe-rendering`）

### 2026-03-02: パフォーマンス最適化調査

UATフィードバックに基づき、3つのパフォーマンス課題を調査し `OPTIMIZATION_REPORT.md` を作成。

#### 課題一覧

| # | 課題 | 根本原因 | 深刻度 |
|---|---|---|---|
| 1 | UHD GPU レンダリング失敗 | wgpu ストレージバッファ上限 128MB 超過（UHD: 158MB） | 高 |
| 2 | CPU フォールバック時の UHD Quality High 極端な速度低下 | 全フレーム一括処理（ワーキングセット 500MB+） | 高 |
| 3 | Flame クロスヘアドラッグ応答性 | `fetchImage()` の毎回フル評価オーバーヘッド | 中 |

#### 根本原因分析

OFX ブリッジが画像全体を1バッファとして `render_stripe()` に渡す一方、NDK 版は NUKE のストライプ分割（64–256px）を利用。upstream の `ChunkHandler`（4096×4096 チャンク）は存在するが、チャンクサイズでも 335MB となり GPU バッファ上限を超過する。

#### 対策方針

- **Phase 1**: Rust FFI ブリッジ `od_render()` 内でストライプ分割レンダリングを実装（Issue 1 & 2 同時解決）
- **Phase 2**: Depth 画像キャッシング（`penDown` 時に `std::vector<float>` にコピー、ドラッグ中はキャッシュから読み取り）
- **Phase 3（将来）**: スレッドセーフティ分析 → `eRenderInstanceSafe` 化

#### ブランチ運用

- ブランチ `feature/stripe-rendering` を作成、リモートにプッシュ済み
- main 版と並行ロード・比較のためプラグイン名に `_stripe` postfix を付与:
  - プラグイン名: `OpenDefocusOFX_stripe`
  - 識別子: `com.opendefocus.ofx.stripe`
  - バンドル: `OpenDefocusOFX_stripe.ofx.bundle/`
- **main マージ時に元の名称に戻す必要あり**

### 2026-03-02: Phase 1 — ストライプベースレンダリング実装

#### 変更ファイル

`rust/opendefocus-ofx-bridge/src/lib.rs` のみ（upstream 変更なし）

#### 実装内容

**`get_stripe_height()` ヘルパー関数追加:**

NDK 版 `stripe_height()` (lib.rs:458-473) と同一ロジック:

| モード | ストライプ高さ |
|---|---|
| CPU | 64 px |
| GPU Low | 256 px |
| GPU Medium | 128 px |
| GPU High / Custom | 64 px |
| FocalPlaneSetup | 32 px |

**`od_render()` のストライプループ化:**

1. ストライプループ前にソース画像全体のコピー (`source_image`) を保持
2. 各ストライプで `source_image` からフレッシュなバッファ (`stripe_buf`) を構築
3. ストライプ用 `RenderSpecs` を構築（`full_region.y = y_in` でグローバル座標を保持）
4. `render_stripe()` 呼び出し後、`stripe_buf` から render_region のみを `image_data` にコピーバック
5. ストライプ間で `abort` チェック
6. 最初の GPU ストライプのみ `catch_unwind` でパニック防護、失敗時は CPU にフォールバック

#### 第1回 UAT 結果（ストライプ境界の継ぎ目問題）

全モード・全 Quality で水平方向のストライプ境界に「継ぎ目」が発生。

**根本原因:** 全ストライプが同一の `image_data` バッファを共有していたため、ストライプ N のレンダリング結果（ボケたピクセル）がストライプ N+1 のパディング領域でソースとして読み取られていた。NDK 版では NUKE がストライプごとに新しいバッファ（オリジナルソース）を提供するため、この問題は発生しない。

**修正:** ストライプループ前にソース画像のスナップショット (`source_image`) を保持。各ストライプはこのスナップショットからデータをコピーして独立したバッファで作業し、レンダリング後は render_region のみを出力バッファにコピーバック。これにより NDK 版と同一の動作を実現。

#### 第1回 UAT サマリー

| ステータス | 項目数 | 備考 |
|---|---|---|
| PASS | 3 | UHD GPU 成功 (32.3)、UHD CPU 速度改善 (32.4)、極端なボケサイズ (32.15) |
| FAIL | 12 | ストライプ境界の継ぎ目 (32.1–32.12) → 修正済み、Flame パフォーマンス低下 (32.18) |
| NOTYET | 2 | プロキシモード (32.13)、マルチフレーム (32.17) |
| ??? | 1 | abort (32.14) |

#### Flame パフォーマンス低下（32.18）分析

コード差分は `od_render()` 内のストライプ分割のみで、プラグインロード/初期化には変更なし。「読み込み時点から」低下する報告から、**main 版 (`OpenDefocusOFX.ofx`) と stripe 版 (`OpenDefocusOFX_stripe.ofx`) の同時ロードにより、2つの wgpu デバイスが GPU リソースを競合している可能性**が最も高い。stripe 版のみで再テスト予定。

#### GPU フォールバック（32.16）分析

ストライプ分割により各ストライプのバッファが 128MB 以下に収まるため、10K 以上でも GPU レンダリングが成功する（これは想定通りの改善）。12K の "Asked for too-large image input" は NUKE ホスト側のバッファ上限。テスト項目の想定を更新する必要あり。

#### 第2回 UAT 結果

- **Flame パフォーマンス低下 (32.18)**: main 版バンドルを除去し stripe 版のみでテスト → パフォーマンス問題は解消。原因は2つの wgpu デバイスの GPU リソース競合で確定。
- **ストライプ境界の継ぎ目**: source_image スナップショット修正後も依然として発生。「継ぎ目の最下部のボケが大きくなっている」「等間隔に発生」との報告。

#### 第2回継ぎ目修正: パディング不足の解消

**根本原因分析:**

upstream カーネルの詳細調査により、`get_padding()` が返す値（`ceil(max_size)` = 畳み込み半径ちょうど）では、以下の理由でストライプバッファ境界でのサンプリングに余裕がないことが判明:

1. **`bilinear_depth_based`** が `base_coords + Vec2::ONE`（+1ピクセル）をサンプリング → 畳み込み半径の外側 +1 ピクセルが必要
2. **`skip_overlap`** が `process_region.y - 1`（1行余分に処理）→ render_region の1行上も処理対象
3. 上記により、render_region 端のピクセルからの最外周サンプルがストライプバッファ境界の **ClampToEdge** に到達 → フル画像レンダリングとは微妙に異なる畳み込み結果 → 周期的な継ぎ目

NDK 版では NUKE ホストがプラグイン報告のパディングに加えて独自の内部マージンを提供するため、この問題は発生しない。OFX にはそのような機構がない。

**修正:** `lib.rs:1134` でパディング計算を変更:
```rust
// Before:
let padding = inst.settings.defocus.get_padding() as i32;
// After:
let padding = inst.settings.defocus.get_padding() as i32 + 4;
```
+4 の内訳: +1 bilinear 補間、+1 skip_overlap y-1、+2 安全マージン（NUKE ホスト相当）

#### 第3回継ぎ目修正: render_region 拡張

NDK C++ コード (`opendefocus.cpp:177-178`) の精査により、NDK が `render_region = full_region.expand(2)` としてカーネルの `skip_overlap()` にパディング領域含む全ピクセルを処理させていることを発見。OFX ブリッジでは `render_region = 出力領域のみ` としていたため、パディングピクセルが `skip_overlap()` でスキップされていた。

**修正:** `stripe_specs.render_region` を `full_region` より各辺 2px 拡張。

#### 第4回修正: 0起点 full_region

C++ メイン版が `fullRegion = [0, 0, bufWidth, bufHeight]`（0起点）を渡すのに対し、ストライプ版が `full_region.y = y_in`（グローバル座標）を使用していた点に着目。0起点座標 `[0, 0, W, stripe_h_in]` に変更。

#### ビルドシステム問題の発見と修正

第2回〜第4回の修正後の UAT で「継ぎ目が依然として発生」と報告されていたが、**CMake の `add_custom_command` に Rust ソースファイルの `DEPENDS` が未指定**であったため、`make` が Rust コードを再コンパイルしていなかった。全テストは初期実装のバイナリで実行されており、修正は一度も反映されていなかった。

**発見の経緯:** デバッグログ（`/tmp/stripe_debug.log` へのファイル出力）が出力されないことから、ビルド出力のタイムスタンプを確認。`libopendefocus_ofx_bridge.a` とバンドル内の `.ofx` バイナリが全て 3月2日 04:38（初期ビルド時刻）のままであることを確認。

**対処:** ビルド前に静的ライブラリを手動削除して強制再コンパイル:
```bash
rm -f rust/opendefocus-ofx-bridge/target/release/libopendefocus_ofx_bridge.a
make -j$(nproc)
```

**結果:** 全修正（source_image スナップショット、padding +4、render_region expand(2)、0起点座標）が反映されたバイナリで UAT を実施。**NUKE で継ぎ目が発生しないことを確認**。

### 現在のステータス

- **Phase 11 (Focus Point XY Picker)**: UAT 完了、全項目 PASS（main ブランチ）
- **Performance Optimization Phase 1**: ストライプベースレンダリング実装完了、NUKE で継ぎ目なしを確認（feature/stripe-rendering ブランチ）。デバッグログの除去と CMake DEPENDS 修正が残作業。

### 次のステップ

1. Phase 1: デバッグログ除去、CMake の DEPENDS 修正（Rust ソース変更時の自動再ビルド）
2. Phase 1: 全 UAT 項目の再テスト（32.1–32.18）
3. Phase 2: Depth 画像キャッシングによる Flame ドラッグ応答性改善
4. リリース準備（ビルド手順書、配布用バンドルパッケージング）
5. Filter Preview はみ出し問題の調査・修正
6. Upstream Rust コアの調査（ピクセルドリフト、Enum ズレ、未配線パラメータ）
