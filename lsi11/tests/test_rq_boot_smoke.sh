#!/bin/sh
set -eu

BIN="${1:-./pdp1184}"
IMG="${2:-disks/rd54.img}"
STEPS="${3:-2000}"

TMP_LOG="$(mktemp /tmp/rq-boot-smoke.XXXXXX.log)"
trap 'rm -f "$TMP_LOG"' EXIT INT TERM

if [ ! -x "$BIN" ]; then
  echo "FAIL: binary not executable: $BIN" >&2
  exit 1
fi

if [ ! -f "$IMG" ]; then
  echo "SKIP: test_rq_boot_smoke (image not found: $IMG)"
  exit 0
fi

"$BIN" -rq "$IMG" -boot rq0 -steps "$STEPS" >"$TMP_LOG" 2>&1 || {
  echo "FAIL: test_rq_boot_smoke (rc=$?)" >&2
  tail -n 120 "$TMP_LOG" >&2 || true
  exit 1
}

echo "PASS: test_rq_boot_smoke"
