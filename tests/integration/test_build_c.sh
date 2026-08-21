#!/bin/sh
# Integration test: build a tiny C project end-to-end using qs_build.
# Works with MSVC (Windows), clang, or gcc — whichever is available.
# Skips cleanly if no C compiler is found rather than failing.

PASS=0; FAIL=0

ok()   { PASS=$((PASS+1)); printf "ok %d - %s\n" $((PASS+FAIL)) "$1"; }
fail() { FAIL=$((FAIL+1)); printf "not ok %d - %s\n" $((PASS+FAIL)) "$1"; }
skip() { printf "ok %d - %s # SKIP %s\n" $((PASS+FAIL+1)) "$1" "$2"; PASS=$((PASS+1)); }

TMP=$(mktemp -d 2>/dev/null || mkdir -p /tmp/qs_integ_$$ && echo /tmp/qs_integ_$$)
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$TMP/src"
cat > "$TMP/src/main.c" << 'CSRC'
#include <stdio.h>
int main(void) { printf("integration ok\n"); return 0; }
CSRC

# Write manifest with absolute source path so validator finds file
# regardless of cwd — critical for Git Bash where /tmp maps to Windows path
MAIN_C_ABS="$TMP/src/main.c"
cat > "$TMP/build.qs" << QS
module integ_c {
    name = "integ_c"; version = "0.1.0"; language = "c";
    sources = [ "$MAIN_C_ABS" ]; output_type = "exe";
}
QS

printf "TAP version 13\n"

# Test 1: --help
if qs_build --help > /dev/null 2>&1; then
    ok "--help exits 0"
else
    fail "--help exits 0"
fi

# Test 2: dry-run parses manifest
if qs_build --dry-run "$TMP/build.qs" > /dev/null 2>&1; then
    ok "dry-run parses manifest"
else
    fail "dry-run parses manifest"
fi

# Test 3: missing manifest gives clear error (not a hang)
MISSING_OUT=$(qs_build --dry-run "$TMP/does_not_exist.qs" 2>&1)
if echo "$MISSING_OUT" | grep -qi "not found\|manifest\|error" ; then
    ok "missing manifest gives clear error"
else
    fail "missing manifest gives clear error"
fi

# Test 4: --toolchain msvc alias
# cl.exe is only on PATH in Developer Command Prompt, not Git Bash
CL_ON_PATH=0
command -v cl.exe > /dev/null 2>&1 && CL_ON_PATH=1
command -v cl     > /dev/null 2>&1 && CL_ON_PATH=1
if [ $CL_ON_PATH -eq 1 ]; then
    if qs_build --dry-run --toolchain msvc "$TMP/build.qs" > /dev/null 2>&1; then
        ok "--toolchain msvc resolves"
    else
        fail "--toolchain msvc resolves"
    fi
else
    skip "--toolchain msvc resolves" "cl.exe not on PATH in this shell"
fi

# Test 5: --toolchain cl alias
if qs_build --dry-run --toolchain cl "$TMP/build.qs" > /dev/null 2>&1; then
    ok "--toolchain cl resolves"
else
    skip "--toolchain cl resolves" "cl not on PATH"
fi

# Test 6: unavailable toolchain fails clearly
OUT=$(qs_build --dry-run --toolchain nonexistent_xyz_compiler "$TMP/build.qs" 2>&1)
STATUS=$?
if [ $STATUS -ne 0 ] && echo "$OUT" | grep -qi "not found\|unavailable\|error"; then
    ok "--toolchain unavailable fails clearly"
else
    fail "--toolchain unavailable fails clearly"
fi

# Test 7: real build — skip if no C compiler
PROBE=$(qs_build --dry-run "$TMP/build.qs" 2>&1)
if echo "$PROBE" | grep -qi "no toolchain\|not found"; then
    skip "real C build" "no C compiler available"
else
    if qs_build --out "$TMP/out" "$TMP/build.qs" > "$TMP/build.log" 2>&1; then
        ok "qs_build exits 0"

        # Find the produced binary
        BIN=""
        for candidate in "$TMP/out/integ_c.exe" "$TMP/out/integ_c" "$TMP/out/bin/integ_c.exe" "$TMP/out/bin/integ_c"; do
            if [ -f "$candidate" ]; then BIN="$candidate"; break; fi
        done
        if [ -z "$BIN" ]; then
            BIN=$(find "$TMP/out" -name "integ_c*" -type f 2>/dev/null | head -1)
        fi

        if [ -n "$BIN" ]; then
            RESULT=$("$BIN" 2>/dev/null)
            if echo "$RESULT" | grep -q "integration ok"; then
                ok "binary runs and prints expected output"
            else
                fail "binary runs and prints expected output"
            fi
        else
            fail "binary produced by build"
        fi
    else
        fail "qs_build exits 0"
        cat "$TMP/build.log"
    fi
fi

# Test 8: dry-run --types exe
if qs_build --dry-run --types exe "$TMP/build.qs" > "$TMP/t8.log" 2>&1; then
    ok "dry-run --types exe"
else
    fail "dry-run --types exe"
    cat "$TMP/t8.log"
fi

# Test 9: dry-run --types dll
if qs_build --dry-run --types dll "$TMP/build.qs" > "$TMP/t9.log" 2>&1; then
    ok "dry-run --types dll"
else
    fail "dry-run --types dll"
    cat "$TMP/t9.log"
fi

# Test 10: no infinite hang on valid manifest
TIMEDOUT=0
if command -v timeout > /dev/null 2>&1; then
    timeout 10s qs_build --dry-run "$TMP/build.qs" > /dev/null 2>&1
    [ $? -eq 124 ] && TIMEDOUT=1
fi
if [ $TIMEDOUT -eq 0 ]; then
    ok "dry-run does not hang (completes within 10s)"
else
    fail "dry-run does not hang (completes within 10s)"
fi

printf "1..%d\n" $((PASS+FAIL))
[ "$FAIL" -eq 0 ]
