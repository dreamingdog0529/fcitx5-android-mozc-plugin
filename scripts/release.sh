#!/usr/bin/env bash
# Mozc プラグインのリリース作成スクリプト
#
# 使い方: scripts/release.sh <tag> [変更点メモのファイル]
#   例:   scripts/release.sh v0.2.0
#
# やること:
#   1. 本体アプリと Mozc プラグインを debug / release でビルドする
#   2. release 版に専用キーストアで署名する（debug 版はそのまま）
#   3. GitHub リリースを作成し、apk を添付する
#      - fcitx5-plugin-mozc-<tag>-debug.apk    (デバッグ署名)
#      - fcitx5-plugin-mozc-<tag>-release.apk  (専用キーストア署名)
#      - fcitx5-base-<tag>-release.apk         (同じキーストアで署名した本体)
# 変更点メモのファイルを渡さない場合、前回のタグ以降のコミット件名から自動生成する。
set -euo pipefail

TAG="${1:?usage: release.sh <tag> [notes-file]}"
NOTES_FILE="${2:-}"

FCITX_TREE="${FCITX_TREE:-$HOME/work/fcitx5-android-stock}"
REPO_DIR="${REPO_DIR:-$HOME/work/fcitx5-android-mozc-plugin}"
GH_REPO="${GH_REPO:-dreamingdog0529/fcitx5-android-mozc-plugin}"
KEYSTORE="${KEYSTORE:-$HOME/android-dev/fcitx-release.jks}"
KEY_ALIAS="${KEY_ALIAS:-fcitx-release}"
PASS_FILE="${PASS_FILE:-$HOME/android-dev/keystore-password.txt}"
BUILD_TOOLS="${BUILD_TOOLS:-$HOME/android-dev/sdk/build-tools/34.0.0}"
OUT="${OUT:-$HOME/release-out/$TAG}"

export PATH="$HOME/.local/bin:$PATH"
export JAVA_HOME="$HOME/android-dev/jdk"
export ANDROID_HOME="$HOME/android-dev/sdk"
export ECM_DIR="$HOME/android-dev/ecm/share/ECM/cmake"
export LD_LIBRARY_PATH="$HOME/android-dev/gettext/root/usr/lib/x86_64-linux-gnu"
export BUILD_ABI="arm64-v8a"
export PATH="$JAVA_HOME/bin:$HOME/android-dev/gettext/root/usr/bin:$HOME/.local/bin:/usr/local/bin:/usr/bin:/bin"

if gh -R "$GH_REPO" release view "$TAG" >/dev/null 2>&1; then
    echo "error: release $TAG already exists" >&2
    exit 1
fi

echo "== build (tree: $FCITX_TREE) =="
cd "$FCITX_TREE"
./gradlew :app:assembleDebug :app:assembleRelease :plugin:mozc:assembleDebug :plugin:mozc:assembleRelease --console=plain

mkdir -p "$OUT"
PLUGIN_DBG=$(ls plugin/mozc/build/outputs/apk/debug/*mozc*debug.apk | head -1)
PLUGIN_REL=$(ls plugin/mozc/build/outputs/apk/release/*release-unsigned.apk | head -1)
BASE_REL=$(ls app/build/outputs/apk/release/*release-unsigned.apk | head -1)
BASE_DBG=$(ls app/build/outputs/apk/debug/*debug.apk | head -1)

cp "$PLUGIN_DBG" "$OUT/fcitx5-plugin-mozc-$TAG-debug.apk"
cp "$BASE_DBG" "$OUT/fcitx5-base-$TAG-debug.apk"

sign_apk() {
    # $1: unsigned apk, $2: output apk
    "$BUILD_TOOLS/zipalign" -f -p 4 "$1" "$OUT/.aligned.apk"
    "$BUILD_TOOLS/apksigner" sign \
        --ks "$KEYSTORE" --ks-key-alias "$KEY_ALIAS" \
        --ks-pass "file:$PASS_FILE" --key-pass "file:$PASS_FILE" \
        --out "$2" "$OUT/.aligned.apk"
    rm -f "$OUT/.aligned.apk"
    "$BUILD_TOOLS/apksigner" verify "$2"
}

echo "== sign =="
sign_apk "$PLUGIN_REL" "$OUT/fcitx5-plugin-mozc-$TAG-release.apk"
sign_apk "$BASE_REL" "$OUT/fcitx5-base-$TAG-release.apk"
ls -la "$OUT"

if [ -z "$NOTES_FILE" ]; then
    NOTES_FILE="$OUT/notes.md"
    LAST_TAG=$(git -C "$REPO_DIR" describe --tags --abbrev=0 2>/dev/null || echo "")
    {
        echo "Mozcプラグイン $TAG"
        echo
        echo "## 変更点"
        if [ -n "$LAST_TAG" ]; then
            git -C "$REPO_DIR" log --format="- %s" "$LAST_TAG..HEAD"
        else
            git -C "$REPO_DIR" log --format="- %s"
        fi
        echo
        echo "## インストールについて"
        echo "- プラグインは本体アプリと同じ署名でないと読み込まれません。"
        echo "- debug版プラグインは、debug版の本体（fcitx5-base-$TAG-debug.apk）とセットで使ってください。"
        echo "- release版プラグインは、同じキーストアで署名した本体（fcitx5-base-$TAG-release.apk）とセットで使ってください。"
        echo "- 署名の異なる本体・プラグインへ切り替えるときは、先にアンインストールが必要です（設定とユーザー辞書は消えます）。"
    } > "$NOTES_FILE"
fi

echo "== create release $TAG =="
cd "$REPO_DIR"
gh -R "$GH_REPO" release create "$TAG" \
    "$OUT/fcitx5-plugin-mozc-$TAG-debug.apk" \
    "$OUT/fcitx5-base-$TAG-debug.apk" \
    "$OUT/fcitx5-plugin-mozc-$TAG-release.apk" \
    "$OUT/fcitx5-base-$TAG-release.apk" \
    --target "$(git rev-parse HEAD)" \
    --title "$TAG" \
    --notes-file "$NOTES_FILE"

echo "RELEASE_DONE $TAG"
