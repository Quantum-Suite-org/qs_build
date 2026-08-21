#!/bin/sh
# Integration test: verify --types flag produces correct output files.
# Works with MSVC, clang, or gcc. Skips if no C compiler available.

PASS=0; FAIL=0

ok()   { PASS=$((PASS+1)); printf "ok %d - %s\n" $((PASS+FAIL)) "$1"; }
fail() { FAIL=$((FAIL+1)); printf "not ok %d - %s\n" $((PASS+FAIL)) "$1"; }
skip() { printf "ok %d - %s # SKIP %s\n" $((PASS+FAIL+1)) "$1" "$2"; PASS=$((PASS+1)); }

TMP=$(mktemp -d 2>/dev/null || mkdir -p /tmp/qs_types_$$ && echo /tmp/qs_types_$$)
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$TMP/src"
cat > "$TMP/src/lib.c" << 'CSRC'
int qs_lib_add(int a, int b) { return a + b; }
CSRC
cat > "$TMP/src/main.c" << 'CSRC'
#include <stdio.h>
int main(void) { printf("exe ok\n"); return 0; }
CSRC

# Use absolute paths so validator finds files from any cwd
LIB_C="$TMP/src/lib.c"
MAIN_C="$TMP/src/main.c"
cat > "$TMP/lib.qs" << QS
module integ_lib {
    name="integ_lib"; version="0.1.0"; language="c";
    sources=["$LIB_C"]; output_type="lib";
}
QS
cat > "$TMP/exe.qs" << QS
module integ_exe {
    name="integ_exe"; version="0.1.0"; language="c";
    sources=["$MAIN_C"]; output_type="exe";
}
QS
cat > "$TMP/dll.qs" << QS
module integ_dll {
    name="integ_dll"; version="0.1.0"; language="c";
    sources=["$LIB_C"]; output_type="dll";
}
QS

printf "TAP version 13\n"

# Check if any C compiler is available
PROBE=$(qs_build --dry-run "$TMP/exe.qs" 2>&1)
NO_COMPILER=0
if echo "$PROBE" | grep -qi "no toolchain\|not found"; then
    NO_COMPILER=1
fi

# Dry-run tests always work regardless of compiler
for TYPE in exe dll lib; do
    QS="$TMP/${TYPE}.qs"
    if qs_build --dry-run "$QS" > /dev/null 2>&1; then
        ok "dry-run --types $TYPE"
    else
        fail "dry-run --types $TYPE"
    fi
done

# Real build tests — skip if no compiler
if [ $NO_COMPILER -eq 1 ]; then
    for TYPE in exe dll lib; do
        skip "real build --types $TYPE" "no C compiler available"
    done
else
    # exe
    OUT="$TMP/out_exe"
    if qs_build --out "$OUT" "$TMP/exe.qs" > "$TMP/exe.log" 2>&1; then
        ok "--types exe build exits 0"
        BIN=$(find "$OUT" -name "integ_exe*" -not -name "*.lib" -not -name "*.exp" -type f 2>/dev/null | head -1)
        if [ -n "$BIN" ]; then
            ok "--types exe produces binary"
            OUT2=$("$BIN" 2>/dev/null)
            if echo "$OUT2" | grep -q "exe ok"; then
                ok "--types exe binary runs"
            else
                fail "--types exe binary runs"
            fi
        else
            fail "--types exe produces binary"
            fail "--types exe binary runs"
        fi
    else
        fail "--types exe build exits 0"
        cat "$TMP/exe.log"
        fail "--types exe produces binary"
        fail "--types exe binary runs"
    fi

    # lib
    OUT="$TMP/out_lib"
    if qs_build --out "$OUT" "$TMP/lib.qs" > "$TMP/lib.log" 2>&1; then
        ok "--types lib build exits 0"
        LIB=$(find "$OUT" -name "*.lib" -o -name "*.a" 2>/dev/null | head -1)
        if [ -n "$LIB" ]; then
            ok "--types lib produces archive"
        else
            fail "--types lib produces archive"
        fi
    else
        fail "--types lib build exits 0"
        cat "$TMP/lib.log"
        fail "--types lib produces archive"
    fi

    # dll
    OUT="$TMP/out_dll"
    if qs_build --out "$OUT" "$TMP/dll.qs" > "$TMP/dll.log" 2>&1; then
        ok "--types dll build exits 0"
        DLL=$(find "$OUT" -name "*.dll" -o -name "*.so" -o -name "*.dylib" 2>/dev/null | head -1)
        if [ -n "$DLL" ]; then
            ok "--types dll produces shared library"
        else
            fail "--types dll produces shared library"
        fi
    else
        fail "--types dll build exits 0"
        cat "$TMP/dll.log"
        fail "--types dll produces shared library"
    fi
fi

# Paths with spaces
SPACED="$TMP/path with spaces"
mkdir -p "$SPACED/src"
cp "$TMP/src/main.c" "$SPACED/src/main.c"
cat > "$SPACED/build.qs" << 'QS'
module spaced {
    name="spaced"; version="0.1.0"; language="c";
    sources=["src/main.c"]; output_type="exe";
}
QS
SPACED_MAIN="$SPACED/src/main.c"
cat > "$SPACED/build.qs" << QS2
module spaced {
    name="spaced"; version="0.1.0"; language="c";
    sources=["$SPACED_MAIN"]; output_type="exe";
}
QS2
if qs_build --dry-run "$SPACED/build.qs" > /dev/null 2>&1; then
    ok "manifest path with spaces works (dry-run)"
else
    fail "manifest path with spaces works (dry-run)"
    qs_build --dry-run "$SPACED/build.qs" 2>&1
fi

printf "1..%d\n" $((PASS+FAIL))
[ "$FAIL" -eq 0 ]
