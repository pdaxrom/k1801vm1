#!/bin/sh
set -e

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
RUN_FLAGS="-exit-on-abort"

make -C "$ROOT_DIR/demo"

IMG="$(mktemp "/tmp/lsi11-rk11-XXXXXX")"
cleanup() {
  rm -f "$IMG"
}
trap cleanup EXIT INT TERM

dd if=/dev/zero of="$IMG" bs=512 count=4872 >/dev/null 2>&1

echo "[KW11]"
"$ROOT_DIR/lsi11" $RUN_FLAGS -load "$ROOT_DIR/demo/kw11_test.bin" -addr 02000 -pc 02000

echo "[LP11]"
"$ROOT_DIR/lsi11" $RUN_FLAGS -load "$ROOT_DIR/demo/lp11_test.bin" -addr 02000 -pc 02000

echo "[RK11 WRITE/READ]"
"$ROOT_DIR/lsi11" $RUN_FLAGS -rk "$IMG" -load "$ROOT_DIR/demo/rk11_wr_test.bin" -addr 02000 -pc 02000

echo "[RK11 READ]"
"$ROOT_DIR/lsi11" $RUN_FLAGS -rk "$IMG" -load "$ROOT_DIR/demo/rk11_test.bin" -addr 02000 -pc 02000

echo "[SR]"
"$ROOT_DIR/lsi11" $RUN_FLAGS -load "$ROOT_DIR/demo/sr_test.bin" -addr 02000 -pc 02000

echo
echo "DL11 test requires keyboard input; run manually:"
echo "  $ROOT_DIR/lsi11 -load $ROOT_DIR/demo/dl11_test.bin -addr 02000 -pc 02000"
