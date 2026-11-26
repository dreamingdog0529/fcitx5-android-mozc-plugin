# fcitx5-android-mozc-plugin

Fcitx5 for Android 用の Mozc 入力メソッドプラグインです。
Google 日本語入力の変換エンジン（Mozc）を、fcitx5-android のプラグインとして動かします。

## 特徴

- ローマ字入力 → ひらがな変換、スペースでの漢字変換、候補タップでの確定に対応
- 入力メソッドを切り替えたとき、未確定の文字を確定してから切り替えます
- Mozc 本体とは JSON 境界の C API（`libmozc_engine.so`）で接続し、アドオン本体（`libmozc.so`）はエンジンを絶対パスで dlopen します
- 辞書データ `mozc.data` を同梱

## 動作環境

- arm64-v8a の Android 端末
- Fcitx5 for Android 本体（プラグインと同じ署名でビルドされたもの）
  プラグインの仕組み上、本体と署名が一致しないと読み込まれません

## 構成

- `src/main/cpp/mozc.cpp` — fcitx アドオン本体（InputMethodEngineV3）
- `src/main/cpp/mozc_engine_capi.h` — エンジン C API のヘッダ
- `src/main/jniLibs/arm64-v8a/libmozc_engine.so` — Mozc エンジン（ビルド済み、arm64-v8a のみ）
- `src/main/cpp/prebuilt/mozc.data` — Mozc 辞書データ
- `engine/mozc_engine_capi.cc` — エンジン側ラッパのソース（Mozc 本体ツリーの `android/jni/` に置いて bazel でビルド）
- `scripts/build-engine.sh` — エンジンと辞書データの再ビルド手順
- `licenses/libraries/` — 依存ライブラリのライセンス情報（JSON 形式）

## ビルド方法

1. [fcitx5-android](https://github.com/fcitx5-android/fcitx5-android) のツリーで `plugin/mozc` にこのリポジトリの内容を置きます
2. `settings.gradle.kts` に `include(":plugin:mozc")` を追加します
3. `./gradlew :plugin:mozc:assembleDebug` でプラグイン apk ができます

`src/main/assets/`（アドオン設定・入力メソッド設定・辞書データの配置先）はビルド時に自動生成されるため、リポジトリには含めていません。

## エンジンの再ビルド

エンジン（`libmozc_engine.so`）と辞書データ（`mozc.data`）は Mozc（commit `3f235b4eb6fcff7d14ef5f0fb8ee56de7ee4c732`）から bazel でビルドしたものです。再ビルドの手順は `scripts/build-engine.sh` にまとめています。

## インストール

1. 本体アプリを入れたあと、プラグイン apk をインストールします
2. 本体アプリの設定 → 入力メソッド で Mozc を有効化します

## ライセンス

- `src/main/cpp/mozc.cpp`、`src/main/cpp/mozc_engine_capi.h`、`engine/mozc_engine_capi.cc` など、このリポジトリで新規に書いたコードは MIT License です（`LICENSE` ファイル参照）
- Mozc 本体・辞書データ（`libmozc_engine.so`、`mozc.data`）は BSD 3-Clause License（Google）です。ライセンス全文は `licenses/Mozc-LICENSE.txt` に同梱しています
- `build.gradle.kts`、`src/main/res/` など fcitx5-android の anthy プラグインから流用した部分は、fcitx5-android のライセンス（LGPL-2.1-or-later）に従います。全文は `licenses/fcitx5-android-LICENSE.txt` に同梱しています
- nlohmann/json（`src/main/cpp/nlohmann/`、バージョン 3.12.0）は MIT License で、各ヘッダファイル内にライセンス文が含まれています
- 各依存のライセンス情報は `licenses/libraries/` にも JSON 形式でまとめています
