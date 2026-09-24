#!/bin/sh
# Builds the sqlite package and the sample program, runs the program and compares its output with expected.txt.
#
# Usage: ./build.sh [emojicodec options]
#   e.g. EMOJICODEC=../../build/Compiler/emojicodec ./build.sh -S ../../build
set -e
cd "$(dirname "$0")"
EMOJICODEC=${EMOJICODEC:-emojicodec}

mkdir -p packages/sqlite
"$EMOJICODEC" "$@" -p sqlite -o packages/sqlite/libsqlite.a sqlite.🍇 -O
"$EMOJICODEC" "$@" main.🍇 -O
./main | tee output.txt
diff -u expected.txt output.txt
echo "✅ The output matches expected.txt."
