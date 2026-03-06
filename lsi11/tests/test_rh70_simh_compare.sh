#!/bin/sh
set -eu

TRACE_BIN="$1"
SIMH_BIN="${SIMH_PDP11:-/opt/homebrew/bin/pdp11}"
IMG_PATH="${2:-/Users/sash/Work/PROJECTS/k1801vm1/lsi11/disks/rt11v5.3/system.dsk}"

if [ ! -x "$SIMH_BIN" ]; then
  echo "SKIP: test_rh70_simh_compare (SIMH pdp11 not found at $SIMH_BIN)"
  exit 0
fi

if [ ! -f "$IMG_PATH" ]; then
  echo "SKIP: test_rh70_simh_compare (image not found: $IMG_PATH)"
  exit 0
fi

TMP_SIMH="$(mktemp /tmp/rh70simh.XXXXXX)"
TMP_LOC="$(mktemp /tmp/rh70loc.XXXXXX)"
TMP_SIMH_STEPS="$(mktemp /tmp/rh70simhsteps.XXXXXX)"
TMP_LOC_STEPS="$(mktemp /tmp/rh70locsteps.XXXXXX)"
trap 'rm -f "$TMP_SIMH" "$TMP_LOC" "$TMP_SIMH_STEPS" "$TMP_LOC_STEPS"' EXIT INT TERM

{
  printf "set cpu 11/70\n"
  printf "set cpu 4096k\n"
  printf "set hk enabled\n"
  printf "attach hk0 %s\n" "$IMG_PATH"
  printf "reset\n"

  printf "ex 17777440\n"
  printf "ex 17777450\n"

  printf "dep 17777441 200\n"
  printf "ex 17777440\n"
  printf "ex 17777450\n"

  printf "dep 17777440 100\n"
  printf "ex 17777440\n"
  printf "ex 17777450\n"

  printf "dep 17777442 177777\n"
  printf "dep 17777444 400\n"
  printf "dep 17777446 0\n"
  printf "dep 17777440 121\n"
  printf "ex 17777440\n"
  printf "ex 17777450\n"

  printf "dep 17777440 121\n"
  printf "ex 17777440\n"
  printf "ex 17777450\n"

  printf "quit\n"
} | "$SIMH_BIN" >"$TMP_SIMH" 2>&1

"$TRACE_BIN" >"$TMP_LOC" 2>&1

awk '
  BEGIN { n1 = 0; n2 = 0; }
  $1 ~ /^17777440:/ { n1++; cs1[n1] = $2; }
  $1 ~ /^17777450:/ { n2++; cs2[n2] = $2; }
  END {
    if (n1 < 5 || n2 < 5) {
      print "FAIL: SIMH trace incomplete";
      exit 2;
    }
    print "INIT CS1=" cs1[1] " CS2=" cs2[1];
    print "CCLR CS1=" cs1[2] " CS2=" cs2[2];
    print "IEONLY CS1=" cs1[3] " CS2=" cs2[3];
    print "GO1 CS1=" cs1[4] " CS2=" cs2[4];
    print "GO2 CS1=" cs1[5] " CS2=" cs2[5];
  }
' "$TMP_SIMH" >"$TMP_SIMH_STEPS"

grep -E '^(INIT|CCLR|IEONLY|GO1|GO2) ' "$TMP_LOC" >"$TMP_LOC_STEPS"

if [ "$(wc -l < "$TMP_LOC_STEPS")" -ne 5 ]; then
  echo "FAIL: local RH70 trace incomplete"
  cat "$TMP_LOC"
  exit 1
fi

check_masked_step() {
  step="$1"

  simh_line="$(grep "^$step " "$TMP_SIMH_STEPS")"
  loc_line="$(grep "^$step " "$TMP_LOC_STEPS")"

  simh_cs1="$(printf "%s\n" "$simh_line" | sed -E 's/.*CS1=([0-7]+).*/\1/')"
  simh_cs2="$(printf "%s\n" "$simh_line" | sed -E 's/.*CS2=([0-7]+).*/\1/')"
  loc_cs1="$(printf "%s\n" "$loc_line" | sed -E 's/.*CS1=([0-7]+).*/\1/')"
  loc_cs2="$(printf "%s\n" "$loc_line" | sed -E 's/.*CS2=([0-7]+).*/\1/')"

  simh_m="$(printf '%o' $((8#$simh_cs1 & 8#000301)))"
  loc_m="$(printf '%o' $((8#$loc_cs1 & 8#000301)))"
  if [ "$simh_m" != "$loc_m" ]; then
    echo "FAIL: $step CS1(DONE|IE|GO) mismatch simh=$simh_cs1 local=$loc_cs1"
    exit 1
  fi

  simh_ir="$(printf '%o' $((8#$simh_cs2 & 8#000100)))"
  loc_ir="$(printf '%o' $((8#$loc_cs2 & 8#000100)))"
  if [ "$simh_ir" != "$loc_ir" ]; then
    echo "FAIL: $step CS2(IR) mismatch simh=$simh_cs2 local=$loc_cs2"
    exit 1
  fi

  simh_pge="$(printf '%o' $((8#$simh_cs2 & 8#002000)))"
  loc_pge="$(printf '%o' $((8#$loc_cs2 & 8#002000)))"
  if [ "$simh_pge" != "$loc_pge" ]; then
    echo "FAIL: $step CS2(PGE) mismatch simh=$simh_cs2 local=$loc_cs2"
    exit 1
  fi
}

check_masked_step INIT
check_masked_step CCLR
check_masked_step IEONLY
check_masked_step GO1
check_masked_step GO2

echo "PASS: test_rh70_simh_compare"
