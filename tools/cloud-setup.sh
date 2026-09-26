#!/usr/bin/env bash
# Setup script for a Claude Code cloud environment (Ubuntu 24.04), pasted into the environment's
# "Setup script" field. Installs what CI uses: LLVM and Clang from apt.llvm.org, CMake, Ninja and rsync,
# plus the tree-sitter CLI so that `ninja grammar` also checks the editor grammar.
#
# Network access: Custom, with "Also include default list of common package managers" checked and
# these allowed domains:
#   apt.llvm.org
#   ppa.launchpadcontent.net   (PPAs that the base image already lists)
set -euo pipefail

LLVM_VERSION=23
export DEBIAN_FRONTEND=noninteractive

install -d -m 755 /etc/apt/keyrings
curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key -o /etc/apt/keyrings/apt.llvm.org.asc
echo "deb [signed-by=/etc/apt/keyrings/apt.llvm.org.asc] https://apt.llvm.org/noble/ llvm-toolchain-noble-${LLVM_VERSION} main" \
  > /etc/apt/sources.list.d/llvm.list

# An unreachable source that we don't need shouldn't stop the session; if the LLVM index is missing,
# the install below fails anyway.
apt-get update || true
apt-get install -y --no-install-recommends \
  "clang-${LLVM_VERSION}" \
  "clang-format-${LLVM_VERSION}" \
  "clangd-${LLVM_VERSION}" \
  "lld-${LLVM_VERSION}" \
  "llvm-${LLVM_VERSION}-dev" \
  cmake \
  libzstd-dev \
  ninja-build \
  python3 \
  rsync \
  zlib1g-dev

# The image ships an older LLVM; make the unversioned names point to this one.
for tool in clang clang++ clang-format clangd ld.lld llvm-config; do
  ln -sf "/usr/lib/llvm-${LLVM_VERSION}/bin/${tool}" "/usr/local/bin/${tool}"
done

npm install -g tree-sitter-cli || echo "Couldn't install tree-sitter-cli; ninja grammar will skip the tree-sitter check."

clang --version | head -1
