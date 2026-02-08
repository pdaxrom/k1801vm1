#!/bin/sh
set -eu

BIN="$1"

TMP1="$(mktemp /tmp/pdp1134cfg.XXXXXX)"
TMP2="$(mktemp /tmp/pdp1134cfg.XXXXXX)"
TMP3="$(mktemp /tmp/pdp1134cfg.XXXXXX)"
trap 'rm -f "$TMP1" "$TMP2" "$TMP3"' EXIT INT TERM

"$BIN" -check-config >/dev/null 2>"$TMP1"

grep -q "machine=pdp1134" "$TMP1" || {
  echo "FAIL: expected machine=pdp1134" >&2
  exit 1
}

grep -q "ram_kb=4096" "$TMP1" || {
  echo "FAIL: expected default ram_kb=4096" >&2
  exit 1
}

grep -q "dl11_alias=0" "$TMP1" || {
  echo "FAIL: expected default dl11_alias=0" >&2
  exit 1
}

if "$BIN" -ram 4100 -check-config >/dev/null 2>"$TMP2"; then
  echo "FAIL: expected -ram 4100 to be rejected" >&2
  exit 1
fi

grep -q "multiple of 8 KB" "$TMP2" || {
  echo "FAIL: missing multiple-of-8KB validation message" >&2
  exit 1
}

"$BIN" -ram 4104 -check-config >/dev/null 2>"$TMP3"

grep -q "ram_kb=4104" "$TMP3" || {
  echo "FAIL: expected ram_kb=4104" >&2
  exit 1
}

echo "PASS: test_pdp1134_ram"
