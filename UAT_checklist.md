# UAT チェックリスト — OpenDefocus OFX v0.1.10-OFX-v1

## テスト環境

| 項目 | 内容 |
|------|------|
| OFX プラグイン | `bundle/OpenDefocusOFX.ofx.bundle/Contents/Linux-x86-64/OpenDefocusOFX.ofx` |
| 比較対象 | OpenDefocus Nuke NDK v0.1.10 |
| OS | Rocky Linux 8.10 (x86_64) |
| OFX ホスト | NUKE16.0, Flame 2026 |
| テスト日 | Feb 25 2026 |
| テスト担当 | Hiroshi |

---

## 1. プラグイン読み込み

| # | テスト項目 | 合否 | 備考 |
|---|-----------|------|------|
| 1.1 | OFX ホストがプラグインを認識する | PASS | Flameで確認済み、NUKEでもFiterカテゴリから分離され、修正が確認されました。 |
| 1.2 | プラグインをノードとして追加できる | PASS | |
| 1.3 | プラグイン追加時にクラッシュしない | PASS | |
| 1.4 | プラグイン説明文が正しく表示される | N/A | ホスト依存の UI 仕様。OFX 側の実装 (`setPluginDescription`) は正しい。対応不要 |

## 2. クリップ接続

| # | テスト項目 | 合否 | 備考 |
|---|-----------|------|------|
| 2.1 | Source 入力に画像を接続できる | PASS | RGBA 32-bit float |
| 2.2 | Depth 入力に深度マップを接続できる（オプション） | PASS | RGBA または Alpha |
| 2.3 | Output に結果が出力される | PASS | |
| 2.4 | Depth 未接続でもエラーにならない | PASS | 2D モードで動作すること |

## 3. パラメータ動作

| # | テスト項目 | 合否 | 備考 |
|---|-----------|------|------|
| 3.1 | Size パラメータが UI に表示される | PASS | デフォルト: 10.0, 範囲: 0–500 |
| 3.2 | Focus Plane パラメータが UI に表示される | PASS | デフォルト: 1.0, 範囲: 0–10000 |
| 3.3 | Size = 0 でパススルー（入力がそのまま出力） | PASS | isIdentity 動作確認 |
| 3.4 | Size を変更すると defocus 量が変化する | PASS | |
| 3.5 | Focus Plane を変更するとピント面が変化する | PASS | Depth 接続時のみ効果あり |
| 3.6 | パラメータがキーフレーム対応している | PASS | 時間変化するアニメーション |

## 4. 2D モード（Depth 未接続）

| # | テスト項目 | 合否 | 備考 |
|---|-----------|------|------|
| 4.1 | Depth 未接続で均一な defocus が適用される | PASS | |
| 4.2 | Size 増加に応じてぼけ量が増加する | PASS | |
| 4.3 | NUKE 版 2D モードと視覚的に同等の結果 | DEFERRED | NUKE NDK版で約1pxのピクセルドリフト発生。upstream 座標系の調査が必要 |

> **4.3 分析**: OFX 版と NDK 版で約1ピクセルのオフセット差が確認された。OFX 版は OFX 標準座標系で正しく動作。NDK 版は NUKE の pixel center 0.5 オフセット座標系の影響を受けている可能性。**OFX 版のブロッカーではない — upstream 調査後に再検証**

## 5. Depth モード（Depth 接続時）

| # | テスト項目 | 合否 | 備考 |
|---|-----------|------|------|
| 5.1 | Depth 接続時にデプスベースの defocus が適用される | PASS | |
| 5.2 | Focus Plane 付近がシャープ、遠方がぼける | PASS | |
| 5.3 | Focus Plane の値を変えるとピント面が移動する | PASS | |
| 5.4 | NUKE 版 Depth モードと視覚的に同等の結果 | DEFERRED | NUKE NDK版でピクセルドリフト発生。4.3 と同一原因 |
| 5.5 | Depth が Alpha チャンネルのみの場合も動作する | N/A | Flame では RGB と Matte(A) が分離入力されるため Alpha 単独の Depth 入力は運用上発生しない。対応不要 |

> **5.5 補足**: コード上は `depth->getPixelComponents()` で実画像のコンポーネントを取得するよう修正済み。ただし NUKE/Flame いずれの運用でも Depth は RGBA で入力されるため、Alpha 単独入力のテストは対象外とする。

## 6. レンダリング品質・正確性

| # | テスト項目 | 合否 | 備考 |
|---|-----------|------|------|
| 6.1 | 出力画像にアーティファクトがない | DEFERRED | OFX 版単体にはアーティファクトなし。NDK 版との比較でピクセルドリフトあり。4.3 と同一原因 |
| 6.2 | 出力画像が黒やゼロにならない | PASS | バッファ受け渡しエラーの確認 |
| 6.3 | アルファチャンネルが正しく処理される | PASS | 入力の Alpha が保持/処理される |
| 6.4 | 高解像度 (4K+) でレンダリングが完了する | PASS | メモリ不足やクラッシュがないこと |
| 6.5 | 小さい解像度 (例: 256x256) でも動作する | PASS | 256x256でテスト済み |

## 7. NUKE 版との比較

| # | テスト項目 | 合否 | 備考 |
|---|-----------|------|------|
| 7.1 | 同一パラメータで NUKE 版と OFX 版の出力を比較 | DEFERRED | ピクセルドリフトの原因が upstream 座標系に起因するため、upstream 調査後に再検証 |
| 7.2 | 2D モードの比較 | DEFERRED | 同上 |
| 7.3 | Depth モードの比較 | DEFERRED | 同上 |
| 7.4 | 目視で大きな差異がないことを確認 | PASS | ドリフトはわずかで2K以上ではほとんど差が分からない。オーバーレイやスイッチの比較テストでは確認可能 |

> **7.1–7.3 分析**: すべて同一原因 — NUKE NDK版と OFX 版の間で約1pxのピクセルドリフトが発生。OFX 版は OFX 標準座標系で正しく動作。NDK 版のドリフトは upstream の座標変換に起因する可能性が高く、OFX 版のリリースブロッカーではない。**upstream 調査後に再検証**

## 8. 安定性

| # | テスト項目 | 合否 | 備考 |
|---|-----------|------|------|
| 8.1 | 複数フレームの連続レンダリングでクラッシュしない | PASS | |
| 8.2 | パラメータを素早く変更してもクラッシュしない | PASS | |
| 8.3 | プラグインの追加・削除を繰り返してもメモリリークしない | PASS | |
| 8.4 | ホストアプリケーション終了時にクラッシュしない | PASS | od_destroy 正常動作 |

## 9. Quality パラメータ (Phase 3)

| # | テスト項目 | 合否 | 備考 |
|---|-----------|------|------|
| 9.1 | Quality パラメータが UI に表示される (Low/Medium/High/Custom) | PASS | デフォルト: Low |
| 9.2 | Quality を変更するとレンダリング結果が変化する | PASS | |
| 9.3 | Quality=Custom の時のみ Samples パラメータが有効になる | PASS | |
| 9.4 | Samples の値を変更するとレンダリング結果が変化する | PASS | Quality=Custom 時 |

## 10. Bokeh パラメータ — Filter Type (Phase 3)

| # | テスト項目 | 合否 | 備考 |
|---|-----------|------|------|
| 10.1 | Filter Type パラメータが UI に表示される (Simple/Disc/Blade) | PASS | デフォルト: Simple |
| 10.2 | Filter Type=Simple で従来通りの defocus が適用される | PASS | |
| 10.3 | Filter Type=Disc で円形 Bokeh が適用される | PASS | |
| 10.4 | Filter Type=Blade で多角形 Bokeh が適用される | PASS | |
| 10.5 | Filter Preview=true でフィルタ形状のプレビューが出力される | PASS | Disc/Blade 時。修正: filter_resolution サイズの小バッファで Bokeh をレンダリングし、出力画像の中央にコピーするよう変更 | 動作確認、Filter Sizeに応じてサイズが変化することも確認できました。
| 10.6 | Filter Type=Simple の時、Bokeh Shape パラメータが無効 (グレーアウト) になる | PASS | 修正: NUKE NDK 版に合わせてグレーアウトを廃止。Bokeh パラメータは常に有効 | 修正確認、常にオンオフできます。

> **10.5 分析**: Rust コアの `render_preview_bokeh` は渡されたバッファのフルサイズに Bokeh を描画する仕様。OFX 版ではフル出力解像度（2K/4K）のバッファを渡していたため画面からはみ出していた。修正: Filter Preview 時は `filter_resolution` サイズ（デフォルト 256px）の小バッファで Bokeh をレンダリングし、出力画像の中央にコピー。Filter Resolution パラメータでプレビューサイズを調整可能。

> **10.6 分析**: Filter Preview 有効状態で Simple に切り替えると、Filter Preview がグレーアウトして元に戻せなくなる問題。NUKE NDK オリジナル版ではグレーアウトしない仕様。修正: Bokeh パラメータのグレーアウトを廃止し、NUKE NDK 版と同一の挙動に変更。Quality=Custom の Samples のみグレーアウト制御を維持。

## 11. Bokeh パラメータ — Bokeh Shape (Phase 3)

| # | テスト項目 | 合否 | 備考 |
|---|-----------|------|------|
| 11.1 | Ring Color を変更すると Bokeh リングの明るさが変化する | PASS | Disc/Blade 時 | 
| 11.2 | Inner Color を変更すると Bokeh 中心の明るさが変化する | PASS | Disc/Blade 時 |
| 11.3 | Ring Size を変更すると Bokeh リングの幅が変化する | PASS | Disc/Blade 時 |
| 11.4 | Outer Blur を変更すると Bokeh 外側のソフトネスが変化する | PASS | Disc/Blade 時 |
| 11.5 | Inner Blur を変更すると Bokeh 内側のソフトネスが変化する | PASS | Disc/Blade 時 |
| 11.6 | Aspect Ratio を変更すると Bokeh 形状の縦横比が変化する | PASS | Disc/Blade 時 |
| 11.7 | Filter Resolution を変更しても正常にレンダリングされる | PASS | Disc/Blade 時 |

## 12. Bokeh パラメータ — Blade 専用 (Phase 3)

| # | テスト項目 | 合否 | 備考 |
|---|-----------|------|------|
| 12.1 | Blades の値を変更すると Bokeh の多角形の頂点数が変化する | PASS | Blade 時、範囲: 3–16 |
| 12.2 | Angle を変更すると Bokeh 形状の回転角度が変化する | PASS | Blade 時 |
| 12.3 | Curvature を変更すると Bokeh 辺の曲率が変化する | PASS | Blade 時 |
| 12.4 | Filter Type=Disc の時、Blades/Angle/Curvature が無効 (グレーアウト) になる | N/A | 10.6 の修正に伴いグレーアウト廃止 (NUKE NDK 準拠)。パラメータは常に有効 |

## 13. Defocus 一般パラメータ (Phase 4)

| # | テスト項目 | 合否 | 備考 |
|---|-----------|------|------|
| 13.1 | Mode パラメータが UI に表示される (2D/Depth) | PASS | デフォルト: 2D |
| 13.2 | Mode=2D で Depth 未接続と同等の均一 defocus が適用される | PASS | Depth 接続の有無に関わらず |
| 13.3 | Mode=Depth + Depth 接続で深度ベースの defocus が適用される | PASS | |
| 13.4 | Mode=Depth + Depth 未接続で 2D フォールバックされる | PASS | エラーにならないこと |
| 13.5 | Math パラメータが UI に表示される (Direct/1÷Z/Real) | PASS | デフォルト: 1/Z |
| 13.6 | Math を変更するとレンダリング結果が変化する | PASS | Mode=Depth 時 |
| 13.7 | Render Result パラメータが UI に表示される (Result/Focal Plane Setup) | PASS | デフォルト: Result |
| 13.8 | Render Result=Focal Plane Setup で焦点面の視覚化が出力される | PASS | Mode=Depth 時 |
| 13.9 | Show Image=true でソース画像がオーバーレイ表示される | PASS | RenderResult=Focal Plane Setup 時 |
| 13.10 | Protect を変更すると焦点面の保護範囲が変化する | PASS | Mode=Depth 時 |
| 13.11 | Maximum Size を変更すると最大 defocus 半径が制限される | PASS | Mode=Depth 時 |
| 13.12 | Gamma Correction を変更すると Bokeh の明暗バランスが変化する | DEFERRED | upstream 未実装 — protobuf 定義のみでレンダリングパイプラインに未接続。NDK 版でも knob 未作成。OFX 版の問題ではない |
| 13.13 | Farm Quality パラメータが UI に表示される (Low/Medium/High/Custom) | PASS | デフォルト: High |

> **13.12 分析**: upstream Rust コアの調査結果: `gamma_correction` は protobuf (`opendefocus.proto` line 68) に `required float gamma_correction = 110 [default = 1.0]` として定義されているが、(1) NDK 版で `create_knob_with_value()` が呼ばれておらず UI knob として作成されていない、(2) `ConvolveSettings` 構造体に含まれておらずレンダリングパイプラインに渡されていない。完全なデッドフィールド。なお、`catseye.gamma` / `barndoors.gamma` / `astigmatism.gamma` は別フィールドでありこちらは正常に使用されている（Non-uniform エフェクト用）。

## 14. Defocus — 条件付き有効/無効 (Phase 4)

| # | テスト項目 | 合否 | 備考 |
|---|-----------|------|------|
| 14.1 | Mode=2D の時、Math/RenderResult/Protect/MaxSize/FocalPlaneOffset がグレーアウトする | PASS | |
| 14.2 | Mode=Depth の時、上記パラメータが有効になる | PASS | |
| 14.3 | Mode=Depth + RenderResult=Result の時、ShowImage がグレーアウトする | PASS | |
| 14.4 | Mode=Depth + RenderResult=Focal Plane Setup の時、ShowImage が有効になる | PASS | |
| 14.5 | GammaCorrection, FarmQuality, SizeMultiplier は Mode に関わらず常に有効 | PASS | |

## 15. Advanced パラメータ (Phase 4)

| # | テスト項目 | 合否 | 備考 |
|---|-----------|------|------|
| 15.1 | Advanced ページが UI に表示される | PASS | Controls, Bokeh と別ページ |
| 15.2 | Size Multiplier パラメータが UI に表示される | PASS | デフォルト: 1.0, 範囲: 0–2 |
| 15.3 | Size Multiplier を変更すると defocus サイズが倍率変化する | PASS | |
| 15.4 | Focal Plane Offset パラメータが UI に表示される | PASS | デフォルト: 0.0, 範囲: -5–5 |
| 15.5 | Focal Plane Offset を変更すると焦点面がオフセットされる | DEFERRED | upstream 未実装 — protobuf 定義・NDK knob 作成済みだが `ConvolveSettings` に未接続でレンダリングに反映されない。NDK 版でも同様に効果なし（ユーザー報告と一致）。OFX 版の問題ではない |

> **15.5 分析**: upstream Rust コアの調査結果: `focal_plane_offset` は protobuf (`opendefocus.proto` line 63) に `required float focal_plane_offset = 80 [default = 0.0]` として定義。NDK 版では `create_knob_with_value()` で Advanced タブに knob が作成されている（lib.rs line 621-625）。しかし `ConvolveSettings` 構造体に含まれておらず、レンダリングパイプラインに一切渡されていない。値を変更しても効果がないのは upstream のバグ/未実装であり、NDK 版でも同一症状（ユーザー報告で確認済み）。

## 16. Bokeh Noise パラメータ (Phase 5)

| # | テスト項目 | 合否 | 備考 |
|---|-----------|------|------|
| 16.1 | Noise Size パラメータが Bokeh ページに表示される | PASS | デフォルト: 0.1, 範囲: 0–1 |
| 16.2 | Noise Size を変更すると Bokeh ノイズのサイズが変化する | DEFERRED | upstream で `noise` feature フラグが無効。bokeh_creator クレートに実装は存在するが `default-features = false` + `features = ["image"]` のため noise 処理がコンパイル除外。NDK 版も同一条件で効果なし |
| 16.3 | Noise Intensity パラメータが Bokeh ページに表示される | PASS | デフォルト: 0.25, 範囲: 0–1 |
| 16.4 | Noise Intensity を変更すると Bokeh ノイズの強度が変化する | DEFERRED | 16.2 と同一原因 — upstream `noise` feature 無効 |
| 16.5 | Noise Seed パラメータが Bokeh ページに表示される | PASS | デフォルト: 0, 範囲: 0–10000 |
| 16.6 | Noise Seed を変更するとノイズパターンが変化する | DEFERRED | 16.2 と同一原因 — upstream `noise` feature 無効 |
| 16.7 | Noise パラメータは Filter Type に関わらず常に有効（グレーアウトしない） | PASS | NUKE NDK 準拠 |
| 16.8 | Filter Preview 有効時に Noise の効果がプレビューに反映される | DEFERRED | 16.2 と同一原因 — upstream `noise` feature 無効 |

> **16.2/16.4/16.6/16.8 分析**: upstream Rust コアの調査結果: bokeh_creator クレート (v0.1.17) には Noise の完全な実装が存在する（`Renderer::apply_noise()` で Fbm Simplex ノイズを生成・適用）。しかし upstream の `opendefocus` クレートが `bokeh-creator = { default-features = false, features = ["image"] }` で依存しており、`"noise"` feature が含まれていない。これにより `#[cfg(not(feature = "noise"))]` のスタブ関数（何もせずそのまま返す）がコンパイルされる。NDK 版も同一条件のため効果なし。OFX 版の問題ではない — upstream で `noise` feature が有効化されれば自動的に動作する。

## 17. Non-Uniform: Catseye パラメータ (Phase 6)

| # | テスト項目 | 合否 | 備考 |
|---|-----------|------|------|
| 17.1 | Non-Uniform ページが UI に表示される | | Controls, Bokeh, Advanced と別ページ |
| 17.2 | Catseye Enable パラメータが UI に表示される | | デフォルト: false |
| 17.3 | Catseye Enable=false の時、Catseye サブパラメータ (6個) がグレーアウトする | | Amount, Inverse, InverseForeground, Gamma, Softness, DimensionBased |
| 17.4 | Catseye Enable=true の時、Catseye サブパラメータ (6個) が有効になる | | |
| 17.5 | Catseye Amount パラメータが UI に表示される | | デフォルト: 0.5, 範囲: 0–2 |
| 17.6 | Catseye Amount を変更すると Catseye エフェクトの強度が変化する | | Enable=true 時 |
| 17.7 | Catseye Inverse パラメータが UI に表示される | | デフォルト: false |
| 17.8 | Catseye Inverse Foreground パラメータが UI に表示される | | デフォルト: true |
| 17.9 | Catseye Gamma パラメータが UI に表示される | | デフォルト: 1.0, 範囲: 0.2–4.0 |
| 17.10 | Catseye Gamma を変更すると Catseye のフォールオフカーブが変化する | | Enable=true 時 |
| 17.11 | Catseye Softness パラメータが UI に表示される | | デフォルト: 0.2, 範囲: 0.01–1.0 |
| 17.12 | Catseye Softness を変更すると Catseye のトランジション幅が変化する | | Enable=true 時 |
| 17.13 | Catseye Dimension Based パラメータが UI に表示される | | デフォルト: false |

## 18. Non-Uniform: Barndoors パラメータ (Phase 6)

| # | テスト項目 | 合否 | 備考 |
|---|-----------|------|------|
| 18.1 | Barndoors Enable パラメータが UI に表示される | | デフォルト: false |
| 18.2 | Barndoors Enable=false の時、Barndoors サブパラメータ (8個) がグレーアウトする | | Amount, Inverse, InverseForeground, Gamma, Top, Bottom, Left, Right |
| 18.3 | Barndoors Enable=true の時、Barndoors サブパラメータ (8個) が有効になる | | |
| 18.4 | Barndoors Amount パラメータが UI に表示される | | デフォルト: 0.5, 範囲: 0–2 |
| 18.5 | Barndoors Amount を変更すると Barndoors エフェクトの強度が変化する | | Enable=true 時 |
| 18.6 | Barndoors Inverse パラメータが UI に表示される | | デフォルト: false |
| 18.7 | Barndoors Inverse Foreground パラメータが UI に表示される | | デフォルト: true |
| 18.8 | Barndoors Gamma パラメータが UI に表示される | | デフォルト: 1.0, 範囲: 0.2–4.0 |
| 18.9 | Barndoors Gamma を変更すると Barndoors のフォールオフカーブが変化する | | Enable=true 時 |
| 18.10 | Barndoors Top パラメータが UI に表示される | | デフォルト: 100.0, 範囲: 0–100 |
| 18.11 | Barndoors Bottom パラメータが UI に表示される | | デフォルト: 100.0, 範囲: 0–100 |
| 18.12 | Barndoors Left パラメータが UI に表示される | | デフォルト: 100.0, 範囲: 0–100 |
| 18.13 | Barndoors Right パラメータが UI に表示される | | デフォルト: 100.0, 範囲: 0–100 |
| 18.14 | Barndoors Top/Bottom/Left/Right を変更するとエッジ defocus の範囲が変化する | | Enable=true 時 |

## 19. 既知の制約（本バージョンでは対象外）

以下は v0.1.10-OFX-v1 では未実装であり、テスト対象外:

- タイリング / ROI 拡張（`setSupportsTiles(false)`）
- GPU レンダリング（CPU のみ）
- カスタム Bokeh 画像（Image フィルタタイプ / Filter 入力クリップ未実装）
- Camera モード（Camera 入力クリップ + camera_data パラメータ群）
- Non-uniform エフェクト（Astigmatism, Axial Aberration, InverseForeground）
- 残りの詳細パラメータ（将来フェーズで段階的追加）

---

## FAIL 項目サマリー (Phase 2 UAT)

| # | 項目 | 分類 | ステータス |
|---|------|------|-----------|
| 1.1 | Flame ノード名表示 | グルーピング設定 | PASS — `"OpenDefocusOFX"` に変更済み |
| 1.4 | 説明文表示 | ホスト依存 | N/A — 対応不要 |
| 4.3 | 2D モード NDK比較 | ピクセルドリフト | DEFERRED — upstream 調査後に再検証 |
| 5.4 | Depth モード NDK比較 | ピクセルドリフト | DEFERRED — upstream 調査後に再検証 |
| 5.5 | Depth Alpha 入力 | 運用上不要 | N/A — Flame/NUKE ともに RGBA で Depth 入力 |
| 6.1 | アーティファクト | ピクセルドリフト | DEFERRED — upstream 調査後に再検証 |
| 7.1–7.3 | NDK比較 | ピクセルドリフト | DEFERRED — upstream 調査後に再検証 |

## FAIL 項目サマリー (Phase 3 UAT)

| # | 項目 | 分類 | ステータス |
|---|------|------|-----------|
| 10.5 | Filter Preview はみ出し | レンダリングバッファサイズ | PASS — filter_resolution サイズで中央配置に修正。再テスト合格 |
| 10.6 | グレーアウトで復帰不可 | パラメータ visibility | PASS — グレーアウト廃止 (NUKE NDK 準拠)。再テスト合格 |

## FAIL 項目サマリー (Phase 4 UAT)

| # | 項目 | 分類 | ステータス |
|---|------|------|-----------|
| 13.12 | Gamma Correction 効果なし | upstream デッドフィールド | DEFERRED — protobuf 定義のみ。NDK 版でも knob 未作成・パイプライン未接続 |
| 15.5 | Focal Plane Offset 効果なし | upstream 未実装 | DEFERRED — NDK 版で knob 作成済みだがパイプライン未接続。NDK 版でも同一症状 |

## FAIL 項目サマリー (Phase 5 UAT)

| # | 項目 | 分類 | ステータス |
|---|------|------|-----------|
| 16.2 | Noise Size 効果なし | upstream `noise` feature 無効 | DEFERRED — bokeh_creator に実装あるが feature フラグでコンパイル除外。NDK 版も同一 |
| 16.4 | Noise Intensity 効果なし | 同上 | DEFERRED |
| 16.6 | Noise Seed 効果なし | 同上 | DEFERRED |
| 16.8 | Filter Preview に Noise 未反映 | 同上 | DEFERRED |

### ステータス凡例

| ステータス | 意味 |
|-----------|------|
| PASS | テスト合格 |
| FAIL | テスト不合格 — 要修正 |
| RETEST | 修正済み — 再テスト待ち |
| DEFERRED | 後日再検証 — upstream 調査等の外部要因待ち |
| N/A | 対応不要 — ホスト依存の仕様または運用上発生しない |

### ピクセルドリフト問題について

NUKE NDK版と OFX 版の間で約1pxのピクセルオフセットが確認された。OFX 版は OFX 標準座標系に準拠しており正しく動作している。

**追加知見**: NUKE NDK オリジナル版ではデフォルトで GPU レンダリングが有効であり、GPU 有効時にピクセルドリフトが発生することが判明。OFX 版（CPU のみ）では正しい座標で動作しているため、ドリフトは NDK 版の GPU レンダリングに起因する可能性が高い。OFX 版のリリースブロッカーではない。


---

## 判定

### Phase 2 UAT

| 項目 | 結果 |
|------|------|
| 総合判定 | PASS |
| 判定日 | Feb 25 2026 |
| 判定者 | Hiroshi |
| 備考 | FAIL 項目なし（修正 PASS / N/A / DEFERRED に解決済み）。ピクセルドリフトは非ブロッカー |

### Phase 3 UAT

| 項目 | 結果 |
|------|------|
| 総合判定 | PASS |
| 判定日 | Feb 25 2026 |
| 判定者 | Hiroshi |
| 備考 | Quality + Bokeh パラメータの動作検証完了。10.5, 10.6 は修正後再テスト合格。FAIL 項目なし |

### Phase 4 UAT

| 項目 | 結果 |
|------|------|
| 総合判定 | PASS |
| 判定日 | Feb 26 2026 |
| 判定者 | Hiroshi |
| 備考 | Defocus 一般 + Advanced パラメータの動作検証完了。13.12 (Gamma Correction), 15.5 (Focal Plane Offset) は upstream Rust コアのデッドフィールド/未実装であり OFX 版のブロッカーではない。DEFERRED に分類。OFX 側の FAIL 項目なし |

### Phase 5 UAT

| 項目 | 結果 |
|------|------|
| 総合判定 | PASS |
| 判定日 | Feb 26 2026 |
| 判定者 | Hiroshi |
| 備考 | Bokeh Noise パラメータの動作検証完了。16.2/16.4/16.6/16.8 (Noise 効果なし) は upstream の bokeh_creator `noise` feature フラグ無効に起因。実装は存在するがコンパイル除外されている。NDK 版も同一条件で効果なし。OFX 版のブロッカーではない。DEFERRED に分類。OFX 側の FAIL 項目なし |
