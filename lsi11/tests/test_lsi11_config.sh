#!/bin/sh
set -eu

BIN="$1"

TMP1="$(mktemp /tmp/lsi11cfg.XXXXXX)"
TMP2="$(mktemp /tmp/lsi11cfg.XXXXXX)"
TMP3="$(mktemp /tmp/lsi11cfg.XXXXXX)"
TMP4="$(mktemp /tmp/lsi11cfg.XXXXXX)"
trap 'rm -f "$TMP1" "$TMP2" "$TMP3" "$TMP4"' EXIT INT TERM

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

grep -q "dev_kw11_l=1" "$TMP1" || {
  echo "FAIL: expected dev_kw11_l=1 on lsi11 profile" >&2
  exit 1
}

grep -q "dev_kw11_p=0" "$TMP1" || {
  echo "FAIL: expected dev_kw11_p=0 on lsi11 profile" >&2
  exit 1
}

grep -q "rh11=1" "$TMP1" || {
  echo "FAIL: expected rh11=1 on lsi11 profile" >&2
  exit 1
}

grep -q "rh_mode=rh11" "$TMP1" || {
  echo "FAIL: expected default rh_mode=rh11 on lsi11 profile" >&2
  exit 1
}

grep -q "dev_tq=0" "$TMP1" || {
  echo "FAIL: expected dev_tq=0 by default on lsi11 profile" >&2
  exit 1
}

grep -q "dev_xp=0" "$TMP1" || {
  echo "FAIL: expected dev_xp=0 by default on lsi11 profile" >&2
  exit 1
}

"$BIN" -enable-kw11-p -check-config >/dev/null 2>"$TMP3"

grep -q "dev_kw11_p=1" "$TMP3" || {
  echo "FAIL: expected dev_kw11_p=1 with -enable-kw11-p" >&2
  exit 1
}

"$BIN" -tq /tmp/test.tap -check-config >/dev/null 2>"$TMP3"

grep -q "dev_tq=1" "$TMP3" || {
  echo "FAIL: expected dev_tq=1 with -tq" >&2
  exit 1
}

"$BIN" -xp /tmp/rm05.img -check-config >/dev/null 2>"$TMP3"

grep -q "dev_xp=1" "$TMP3" || {
  echo "FAIL: expected dev_xp=1 with -xp" >&2
  exit 1
}

"$BIN" -rh-mode rh70 -check-config >/dev/null 2>"$TMP4"

grep -q "rh_mode=rh70" "$TMP4" || {
  echo "FAIL: expected rh_mode=rh70 with -rh-mode rh70" >&2
  exit 1
}

if "$BIN" -rh-mode invalid -check-config >/dev/null 2>"$TMP4"; then
  echo "FAIL: invalid -rh-mode value must be rejected" >&2
  exit 1
fi

grep -q "Invalid -rh-mode:" "$TMP4" || {
  echo "FAIL: missing invalid -rh-mode error message" >&2
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
