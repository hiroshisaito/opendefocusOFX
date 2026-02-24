# OpenDefocus OFX移植セットアップ（2章分離版）

このドキュメントは、手順混在を防ぐために以下を明確に分離します。

- 第1章: 現状（OpenDefocus原本）
- 第2章: 移植先（OFXプラグイン）


## 第1章: 現状（OpenDefocus原本）

### 1.1 対象

- リポジトリ: `upstream/opendefocus`
- 主目的: Nuke向けプラグイン提供（OFX実装は未同梱）

### 1.2 現状の事実（この章の前提）

- ビルドは `cargo xtask` ベース（CMake/Conanベースではない）
- Nuke向けビルドは `DDImage` ヘッダ/ライブラリ依存
- 出力は `package/opendefocus_plugin/bin/<nuke_version>/<os>/<arch>/` 配下
- ランタイム配布は `.nuke` / `NUKE_PATH` 前提
- GPUは `wgpu` ベース（Vulkan/Metal）

### 1.3 現状セットアップ手順（原本開発・検証用）

1. 原本のルートへ移動
   - `cd /Volumes/RAID/develop/ofx/oepndefocus_ofx/upstream/opendefocus`
2. Rustツールチェインを準備
   - stable（1.92+）
   - `crates/spirv-cli-build/rust-toolchain.toml` で指定された nightly
3. Nuke向けバイナリをビルド
   - 例:
     - `cargo xtask --compile --nuke-versions 15.1,15.2 --target-platform linux --output-to-package`
4. Nuke側で読み込み確認
   - `NUKE_PATH` または `.nuke/init.py` による読み込み

### 1.4 この章で「やらないこと」

- `/usr/OFX/Plugins` への配置
- `.ofx.bundle` の作成
- OFX SDK（`openfx/include`, `openfx/Support`）を使ったエントリポイント実装


## 第2章: 移植先（OFXプラグイン）

### 2.1 対象

- 新規OFX実装: `plugin/OpenDefocusOFX`
- OFX SDK参照元: `upstream/openfx/include`, `upstream/openfx/Support`
- バンドル作業領域: `bundle/OpenDefocus.ofx.bundle/Contents/Linux-x86-64`

### 2.2 ゴール

- OFXホストが読み込めるバンドルを生成する
- 目標配置:
  - `/usr/OFX/Plugins/OpenDefocus.ofx.bundle/Contents/Linux-x86-64/OpenDefocus.ofx`

### 2.3 移植先セットアップ手順（OFX実装用）

1. OFX実装の作業場所を使用する
   - `plugin/OpenDefocusOFX`
2. OFX SDKを参照する設計に固定する
   - ヘッダ参照元: `upstream/openfx/include`
   - C++ Support参照元: `upstream/openfx/Support`
3. OFXエントリポイントを実装する
   - `OfxGetNumberOfPlugins`
   - `OfxGetPlugin`
   - `kOfxActionDescribe`
   - `kOfxImageEffectActionDescribeInContext`
   - `kOfxImageEffectActionRender`
4. 入出力設計をNuke依存からOFX依存へ切り替える
   - `Source` / `Depth` / `Output` クリップ定義
   - 32-bit float中心の処理
5. バンドルを組み立てる
   - `OpenDefocus.ofx.bundle/Contents/Linux-x86-64/OpenDefocus.ofx`
6. ホストで検証する
   - 必要に応じてホスト側キャッシュを削除して再スキャン

### 2.4 この章で「やらないこと」

- `cargo xtask --compile --nuke-versions ...` によるNukeバイナリ生成
- `.nuke` / `NUKE_PATH` 前提の配布確認


## 第3章: 混在防止ルール（運用）

1. コマンド実行前に、対象章（第1章/第2章）を明示する。
2. 第1章の成果物は `package/opendefocus_plugin/...`、第2章の成果物は `.ofx.bundle` として区別する。
3. `references` の既存PDFは背景資料として扱い、実作業手順は本ドキュメントを優先する。
4. Linuxアーキテクチャ表記は `Linux-x86-64` を採用する（OFX Packaging準拠）。


## 付録: 章別の最短チェック

### 第1章チェック（現状）

- `upstream/opendefocus` で `cargo xtask` 実行を前提にしている
- Nuke向け出力先が `package/opendefocus_plugin/bin/...` である

### 第2章チェック（移植先）

- `plugin/OpenDefocusOFX` でOFXエントリポイントを実装している
- 出力が `OpenDefocus.ofx.bundle/Contents/Linux-x86-64/OpenDefocus.ofx` である
