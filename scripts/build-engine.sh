#!/bin/bash
# libmozc_engine.so と mozc.data を Mozc から再ビルドする手順スクリプト。
# このリポジトリに同梱のビルド済みファイルは、以下の手順で生成した。
#
# 前提: bazelisk、Android NDK r29、ホスト用 gcc が使えること。
set -e

MOZC_COMMIT=3f235b4eb6fcff7d14ef5f0fb8ee56de7ee4c732

# 1. Mozc を固定コミットで取得する
#    git clone https://github.com/google/mozc.git
#    cd mozc && git checkout $MOZC_COMMIT
#    git submodule update --init src/data/dictionary_oss
#    src/third_party/ndk/android-ndk-r29 に NDK への symlink を張る

# 2. エンジン側ラッパを Mozc ツリーに入れる
#    cp engine/mozc_engine_capi.cc src/android/jni/
#    src/android/jni/BUILD.bazel に mozc_engine ターゲット
#    （cc_binary、linkshared、-Wl,-soname,libmozc_engine.so）を追加する

# 3. bazel でビルドする（src/ で実行）
#    bazelisk build --config=oss_android //android/jni:mozc_engine
#    bazelisk build --config=oss_android //data_manager/oss:mozc.data

# 4. 成果物をこのリポジトリへ配置する
#    bazel-bin/android/jni/libmozc_engine.so -> src/main/jniLibs/arm64-v8a/
#    bazel-bin/data_manager/oss/mozc.data    -> src/main/cpp/prebuilt/
echo "手順はこのファイル内のコメントを参照してください。"
