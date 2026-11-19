#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# POLY5 in a browser: firmware/src/poly5.c as WebAssembly (web/web.c in place of the hardware) -> ../site/play/poly5.wasm
# Needs LLVM's clang with the wasm32 target and wasm-ld (macOS: brew install llvm lld).
set -e
cd "$(dirname "$0")/.."
CLANG=${CLANG:-$(command -v /opt/homebrew/opt/llvm/bin/clang || command -v clang)}
LLD=${WASM_LD:-$(command -v /opt/homebrew/opt/lld/bin/wasm-ld || command -v wasm-ld)}
OUT=../site/play
mkdir -p "$OUT" build/web
"$CLANG" --target=wasm32 -O2 -ffreestanding -fno-builtin -nostdlib -Wall -Wno-unused-function \
    -DDINGHY_WEB -DDINGHY_HOST -DAPP_VER='"web"' \
    -Ifirmware/hal -Ifirmware/src -Ifirmware/gen -c -o build/web/poly5.o firmware/src/poly5.c
"$LLD" --no-entry --export-dynamic --initial-memory=4194304 --stack-first -z stack-size=262144 \
    -o "$OUT/poly5.wasm" build/web/poly5.o
ls -l "$OUT/poly5.wasm"
