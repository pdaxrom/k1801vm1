#!/bin/sh
set -eu

BIN="$1"

TMP1="$(mktemp /tmp/pdp1184cfg.XXXXXX)"
TMP2="$(mktemp /tmp/pdp1184cfg.XXXXXX)"
TMP3="$(mktemp /tmp/pdp1184cfg.XXXXXX)"
TMP4="$(mktemp /tmp/pdp1184cfg.XXXXXX)"
trap 'rm -f "$TMP1" "$TMP2" "$TMP3" "$TMP4"' EXIT INT TERM

"$BIN" -check-config >/dev/null 2>"$TMP1"

grep -q "machine=pdp1184" "$TMP1" || {
  echo "FAIL: expected machine=pdp1184" >&2
  exit 1
}

grep -q "cpu=dcj11" "$TMP1" || {
  echo "FAIL: expected default cpu=dcj11" >&2
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

grep -q "rh11=1" "$TMP1" || {
  echo "FAIL: expected rh11=1 on pdp1184" >&2
  exit 1
}

grep -q "rh_mode=rh11" "$TMP1" || {
  echo "FAIL: expected default rh_mode=rh11 on pdp1184" >&2
  exit 1
}

grep -q "dev_xp=0" "$TMP1" || {
  echo "FAIL: expected dev_xp=0 by default on pdp1184" >&2
  exit 1
}

if "$BIN" --mem-kb 4101 -check-config >/dev/null 2>"$TMP2"; then
  echo "FAIL: expected --mem-kb 4101 to be rejected" >&2
  exit 1
fi

grep -q "multiple of 4 KB" "$TMP2" || {
  echo "FAIL: missing multiple-of-4KB validation message" >&2
  exit 1
}

"$BIN" --mem-kb 4104 -check-config >/dev/null 2>"$TMP3"

grep -q "ram_kb=4104" "$TMP3" || {
  echo "FAIL: expected ram_kb=4104" >&2
  exit 1
}

"$BIN" -rh-mode rh70 -check-config >/dev/null 2>"$TMP4"

grep -q "rh_mode=rh70" "$TMP4" || {
  echo "FAIL: expected rh_mode=rh70 with -rh-mode rh70" >&2
  exit 1
}

if "$BIN" -rh-mode bad -check-config >/dev/null 2>"$TMP4"; then
  echo "FAIL: invalid -rh-mode value must be rejected" >&2
  exit 1
fi

grep -q "Invalid -rh-mode:" "$TMP4" || {
  echo "FAIL: missing invalid -rh-mode error message" >&2
  exit 1
}

"$BIN" -xp /tmp/rm05.img -check-config >/dev/null 2>"$TMP4"

grep -q "dev_xp=1" "$TMP4" || {
  echo "FAIL: expected dev_xp=1 with -xp" >&2
  exit 1
}

if "$BIN" -disable-xp -xp /tmp/rm05.img -check-config >/dev/null 2>"$TMP4"; then
  echo "FAIL: -xp must be rejected with -disable-xp" >&2
  exit 1
fi

grep -q -- "-xp/-rp is not allowed with -disable-xp" "$TMP4" || {
  echo "FAIL: missing -disable-xp validation message" >&2
  exit 1
}

echo "PASS: test_pdp1184_ram"
