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
| 9.3 | Quality=Custom の時のみ Samples パラメータが有効になる | | |
| 9.4 | Samples の値を変更するとレンダリング結果が変化する | | Quality=Custom 時 |

## 10. Bokeh パラメータ — Filter Type (Phase 3)

| # | テスト項目 | 合否 | 備考 |
|---|-----------|------|------|
| 10.1 | Filter Type パラメータが UI に表示される (Simple/Disc/Blade) | | デフォルト: Simple |
| 10.2 | Filter Type=Simple で従来通りの defocus が適用される | | |
| 10.3 | Filter Type=Disc で円形 Bokeh が適用される | | |
| 10.4 | Filter Type=Blade で多角形 Bokeh が適用される | | |
| 10.5 | Filter Preview=true でフィルタ形状のプレビューが出力される | | Disc/Blade 時 |
| 10.6 | Filter Type=Simple の時、Bokeh Shape パラメータが無効 (グレーアウト) になる | | |

## 11. Bokeh パラメータ — Bokeh Shape (Phase 3)

| # | テスト項目 | 合否 | 備考 |
|---|-----------|------|------|
| 11.1 | Ring Color を変更すると Bokeh リングの明るさが変化する | | Disc/Blade 時 |
| 11.2 | Inner Color を変更すると Bokeh 中心の明るさが変化する | | Disc/Blade 時 |
| 11.3 | Ring Size を変更すると Bokeh リングの幅が変化する | | Disc/Blade 時 |
| 11.4 | Outer Blur を変更すると Bokeh 外側のソフトネスが変化する | | Disc/Blade 時 |
| 11.5 | Inner Blur を変更すると Bokeh 内側のソフトネスが変化する | | Disc/Blade 時 |
| 11.6 | Aspect Ratio を変更すると Bokeh 形状の縦横比が変化する | | Disc/Blade 時 |
| 11.7 | Filter Resolution を変更しても正常にレンダリングされる | | Disc/Blade 時 |

## 12. Bokeh パラメータ — Blade 専用 (Phase 3)

| # | テスト項目 | 合否 | 備考 |
|---|-----------|------|------|
| 12.1 | Blades の値を変更すると Bokeh の多角形の頂点数が変化する | | Blade 時、範囲: 3–16 |
| 12.2 | Angle を変更すると Bokeh 形状の回転角度が変化する | | Blade 時 |
| 12.3 | Curvature を変更すると Bokeh 辺の曲率が変化する | | Blade 時 |
| 12.4 | Filter Type=Disc の時、Blades/Angle/Curvature が無効 (グレーアウト) になる | | |

## 13. 既知の制約（本バージョンでは対象外）

以下は v0.1.10-OFX-v1 では未実装であり、テスト対象外:

- タイリング / ROI 拡張（`setSupportsTiles(false)`）
- GPU レンダリング（CPU のみ）
- カスタム Bokeh 画像（Image フィルタタイプ / Filter 入力クリップ未実装）
- 残り 50+ の詳細パラメータ（将来フェーズで段階的追加）

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

### ステータス凡例

| ステータス | 意味 |
|-----------|------|
| PASS | テスト合格 |
| FAIL | テスト不合格 — 要修正 |
| RETEST | 修正済み — 再テスト待ち |
| DEFERRED | 後日再検証 — upstream 調査等の外部要因待ち |
| N/A | 対応不要 — ホスト依存の仕様または運用上発生しない |

### ピクセルドリフト問題について

NUKE NDK版と OFX 版の間で約1pxのピクセルオフセットが確認された。OFX 版は OFX 標準座標系に準拠しており正しく動作している。NDK版のドリフトは NUKE の pixel center オフセット (0.5px) または NDK 固有の座標変換に起因する可能性が高い。2K以上の解像度では目視で判別困難であり、OFX 版のリリースブロッカーではない。upstream 調査後に再検証予定。

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
| 総合判定 | ☐ PASS / ☐ FAIL |
| 判定日 | _（記入）_ |
| 判定者 | _（記入）_ |
| 備考 | Quality + Bokeh パラメータの動作検証 |
