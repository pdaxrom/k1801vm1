#!/bin/sh
set -eu

BIN="${1:-./pdp1184}"
IMG="${2:-disks/ultrix/sys.dsk}"
RUN_SECS="${SMOKE_SECS:-24}"

TMP_RH11="$(mktemp /tmp/ultrix-rh11.XXXXXX)"
TMP_RH70="$(mktemp /tmp/ultrix-rh70.XXXXXX)"
trap 'rm -f "$TMP_RH11" "$TMP_RH70"' EXIT INT TERM

if [ ! -x "$BIN" ]; then
  echo "FAIL: binary not executable: $BIN" >&2
  exit 1
fi

if [ ! -f "$IMG" ]; then
  echo "SKIP: test_ultrix_rh_smoke (image not found: $IMG)"
  exit 0
fi

run_mode() {
  mode="$1"
  out="$2"

  (
    "$BIN" -rh "$IMG" -bootrt11 -rh-mode "$mode" >"$out" 2>&1 &
    pid=$!
    sleep "$RUN_SECS"
    kill -TERM "$pid" >/dev/null 2>&1 || true
    wait "$pid" >/dev/null 2>&1 || true
  )
}

assert_boot_ok() {
  mode="$1"
  out="$2"

  grep -q "ULTRIX-11 Kernel V3.1" "$out" || {
    echo "FAIL: $mode did not reach ULTRIX kernel banner" >&2
    tail -n 80 "$out" >&2
    exit 1
  }

  grep -q "ULTRIX-11 Setup Program" "$out" || {
    echo "FAIL: $mode did not reach setup phase" >&2
    tail -n 80 "$out" >&2
    exit 1
  }

  if grep -q "panic:" "$out"; then
    echo "FAIL: $mode hit kernel panic during smoke boot" >&2
    tail -n 80 "$out" >&2
    exit 1
  fi
}

run_mode rh11 "$TMP_RH11"
run_mode rh70 "$TMP_RH70"

assert_boot_ok rh11 "$TMP_RH11"
assert_boot_ok rh70 "$TMP_RH70"

echo "PASS: test_ultrix_rh_smoke"
