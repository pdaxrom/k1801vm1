#!/bin/sh

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
BIN="$ROOT_DIR/.build/mk90"
TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/mk90-smoke.XXXXXX")

cleanup() {
    rm -rf "$TMP_DIR"
}

trap cleanup EXIT INT TERM

hash_file() {
    shasum -a 1 "$1" | awk '{print $1}'
}

run_case() {
    name=$1
    expected=$2
    shift 2

    output="$TMP_DIR/$name.pgm"
    "$BIN" --headless --dump-pgm "$output" "$@" >/dev/null 2>&1
    actual=$(hash_file "$output")

    if [ "$actual" != "$expected" ]; then
        echo "smoke: $name hash mismatch" >&2
        echo "expected: $expected" >&2
        echo "actual:   $actual" >&2
        return 1
    fi

    echo "smoke: $name ok ($actual)"
}

run_case menu      4f40178f6e7c3f6a4888680504dc235d95bddae1 --frames 200
run_case test_menu 32a2d1838c27f4511dc2ffe4e42baedd962a92fc --frames 260 --tap 210:123
run_case smp1_menu 4645b8c39006fc50a5eb44efcec6e1d9d98432a4 --frames 260 --tap 210:133
run_case smp0_menu 40f31e4923a68ec706f5fc3b6a907537f3be3a1f --frames 260 --tap 210:273
run_case basic     51651fff3cf2dc3e3498f8da2ab6562b9091c40d --frames 260 --tap 210:373
run_case test_sub  9a8cfce60f53cbce53c02b9cb2f55b277d39687b --frames 280 --tap 210:123 --tap 214:373
run_case smp_no_loader 2116009d920099b2fc3b745b9373de59e01986dd \
    --smp0 "$ROOT_DIR/media/smp1.bin" --frames 280 --tap 210:273 --tap 214:373

if [ -f "$ROOT_DIR/media/trex.bin" ]; then
    run_case trex_boot dc3d7a39bc45a83e3c2092a095bd4f3bb58fc627 \
        --smp0 "$ROOT_DIR/media/trex.bin" --frames 320 --tap 210:273 --tap 214:373
fi
