#!/usr/bin/env bash
# Runs the integration tests in VS Code with a fresh profile. The path to VS Code can be given as the argument.
set -e
cd "$(dirname "$0")/../.."
code="${1:-/Applications/Visual Studio Code.app/Contents/MacOS/Code}"
profile="$(mktemp -d)"
trap 'rm -rf "$profile"' EXIT
"$code" --user-data-dir="$profile/data" --extensions-dir="$profile/extensions" --disable-workspace-trust \
    --extensionDevelopmentPath="$PWD" --extensionTestsPath="$PWD/test/integration/index.js"
