#!/bin/sh
set -eu

BIN="$1"

TMP1="$(mktemp /tmp/lsi11cfg.XXXXXX)"
TMP2="$(mktemp /tmp/lsi11cfg.XXXXXX)"
trap 'rm -f "$TMP1" "$TMP2"' EXIT INT TERM

"$BIN" -check-config >/dev/null 2>"$TMP1"

grep -q "machine=lsi11" "$TMP1" || {
  echo "FAIL: expected machine=lsi11" >&2
  exit 1
}

grep -q "ram_kb=56" "$TMP1" || {
  echo "FAIL: expected ram_kb=56" >&2
  exit 1
}

grep -q "dl11_alias=1" "$TMP1" || {
  echo "FAIL: expected dl11_alias=1" >&2
  exit 1
}

grep -q "rh11=0" "$TMP1" || {
  echo "FAIL: expected rh11=0 on lsi11 profile" >&2
  exit 1
}

if "$BIN" --mem-kb 60 -check-config >/dev/null 2>"$TMP2"; then
  echo "FAIL: --mem-kb must be rejected for lsi11 target" >&2
  exit 1
fi

grep -q "fixed 56KB RAM" "$TMP2" || {
  echo "FAIL: missing fixed-56KB error" >&2
  exit 1
}

echo "PASS: test_lsi11_config"
