#!/bin/sh
set -eu

BIN="${1:-./pdp1184}"
IMG="${2:-disks/bsd2.9/2.9BSD-usr.rm05}"
STEPS="${3:-2000}"

TMP_LOG="$(mktemp /tmp/xp-boot-smoke.XXXXXX.log)"
trap 'rm -f "$TMP_LOG"' EXIT INT TERM

if [ ! -x "$BIN" ]; then
  echo "FAIL: binary not executable: $BIN" >&2
  exit 1
fi

if [ ! -f "$IMG" ]; then
  echo "SKIP: test_xp_boot_smoke (image not found: $IMG)"
  exit 0
fi

"$BIN" -xp "$IMG" -boot xp0 -steps "$STEPS" >"$TMP_LOG" 2>&1 || {
  echo "FAIL: test_xp_boot_smoke (rc=$?)" >&2
  tail -n 120 "$TMP_LOG" >&2 || true
  exit 1
}

echo "PASS: test_xp_boot_smoke"
