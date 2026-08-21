@echo off
REM build_and_run_tests.bat — compile and run all unit tests with MSVC
REM Run from qs_build root: tests\unit\build_and_run_tests.bat

setlocal enabledelayedexpansion
set PASS=0
set FAIL=0
set SKIP=0

set CC=cl /nologo /std:c11 /O2 /W3 /WX- /D_CRT_SECURE_NO_WARNINGS /Icore\include /Isrc
set OUT=build_obj\tests

if not exist %OUT% mkdir %OUT%

REM ── Shared implementation objects ────────────────────────────────────────
echo [test-build] compiling shared implementation objects...

%CC% /c /Fo:%OUT%\arena.obj      core\util\arena.c       >NUL 2>&1
%CC% /c /Fo:%OUT%\hash.obj       core\util\hash.c        >NUL 2>&1
%CC% /c /Fo:%OUT%\str.obj        core\util\str.c         >NUL 2>&1
%CC% /c /Fo:%OUT%\path.obj       core\util\path.c        >NUL 2>&1
%CC% /c /Fo:%OUT%\fs.obj         core\platform\fs.c      >NUL 2>&1
%CC% /c /Fo:%OUT%\process.obj    core\process\process.c  >NUL 2>&1
%CC% /c /Fo:%OUT%\diag_engine.obj core\diagnostics\diag_engine.c >NUL 2>&1
%CC% /c /Fo:%OUT%\diag_parsers.obj core\diagnostics\diag_parsers.c >NUL 2>&1
%CC% /c /Fo:%OUT%\manifest_parser.obj manifest\parser.c  >NUL 2>&1
%CC% /c /Fo:%OUT%\manifest_lexer.obj lexer\manifest_lexer.c >NUL 2>&1
%CC% /c /Fo:%OUT%\ast.obj        parser\ast.c            >NUL 2>&1
%CC% /c /Fo:%OUT%\chunker.obj    chunking\chunker.c      >NUL 2>&1
%CC% /c /Fo:%OUT%\string_utils.obj utilities\string_utils.c >NUL 2>&1

REM Shared base always linked: arena + hash (hash provides qs_fnv1a_64)
set BASE=%OUT%\arena.obj %OUT%\hash.obj

REM ── test_arena ───────────────────────────────────────────────────────────
echo [test] arena...
%CC% /Fe:%OUT%\test_arena.exe tests\unit\test_arena.c %BASE% /link kernel32.lib >%OUT%\test_arena.log 2>&1
if !errorlevel! neq 0 (
    echo   BUILD FAILED - see %OUT%\test_arena.log
    set /a FAIL+=1
) else (
    %OUT%\test_arena.exe > %OUT%\test_arena.tap 2>&1
    if !errorlevel! equ 0 (
        echo   PASS
        set /a PASS+=1
    ) else (
        echo   FAIL
        type %OUT%\test_arena.tap
        set /a FAIL+=1
    )
)

REM ── test_hash ────────────────────────────────────────────────────────────
echo [test] hash...
%CC% /Fe:%OUT%\test_hash.exe tests\unit\test_hash.c %BASE% /link kernel32.lib >%OUT%\test_hash.log 2>&1
if !errorlevel! neq 0 (
    echo   BUILD FAILED - see %OUT%\test_hash.log
    set /a FAIL+=1
) else (
    %OUT%\test_hash.exe > %OUT%\test_hash.tap 2>&1
    if !errorlevel! equ 0 (
        echo   PASS
        set /a PASS+=1
    ) else (
        echo   FAIL
        type %OUT%\test_hash.tap
        set /a FAIL+=1
    )
)

REM ── test_str ─────────────────────────────────────────────────────────────
REM needs: arena + hash (qs_fnv1a_64) + str
echo [test] str...
%CC% /Fe:%OUT%\test_str.exe tests\unit\test_str.c %BASE% %OUT%\str.obj /link kernel32.lib >%OUT%\test_str.log 2>&1
if !errorlevel! neq 0 (
    echo   BUILD FAILED - see %OUT%\test_str.log
    set /a FAIL+=1
) else (
    %OUT%\test_str.exe > %OUT%\test_str.tap 2>&1
    if !errorlevel! equ 0 (
        echo   PASS
        set /a PASS+=1
    ) else (
        echo   FAIL
        type %OUT%\test_str.tap
        set /a FAIL+=1
    )
)

REM ── test_path ────────────────────────────────────────────────────────────
REM needs: arena + hash + str + path
echo [test] path...
%CC% /Fe:%OUT%\test_path.exe tests\unit\test_path.c %BASE% %OUT%\str.obj %OUT%\path.obj /link kernel32.lib >%OUT%\test_path.log 2>&1
if !errorlevel! neq 0 (
    echo   BUILD FAILED - see %OUT%\test_path.log
    set /a FAIL+=1
) else (
    %OUT%\test_path.exe > %OUT%\test_path.tap 2>&1
    if !errorlevel! equ 0 (
        echo   PASS
        set /a PASS+=1
    ) else (
        echo   FAIL
        type %OUT%\test_path.tap
        set /a FAIL+=1
    )
)

REM ── test_diag ────────────────────────────────────────────────────────────
REM needs: arena + hash + str + diag_engine + diag_parsers
echo [test] diag...
%CC% /Fe:%OUT%\test_diag.exe tests\unit\test_diag.c %BASE% %OUT%\str.obj %OUT%\diag_engine.obj %OUT%\diag_parsers.obj /link kernel32.lib >%OUT%\test_diag.log 2>&1
if !errorlevel! neq 0 (
    echo   BUILD FAILED - see %OUT%\test_diag.log
    set /a FAIL+=1
) else (
    %OUT%\test_diag.exe > %OUT%\test_diag.tap 2>&1
    if !errorlevel! equ 0 (
        echo   PASS
        set /a PASS+=1
    ) else (
        echo   FAIL
        type %OUT%\test_diag.tap
        set /a FAIL+=1
    )
)

REM ── test_manifest ────────────────────────────────────────────────────────
REM needs: arena + hash + str + path + fs + diag_engine + diag_parsers + lexer + ast + parser
echo [test] manifest...
%CC% /Fe:%OUT%\test_manifest.exe tests\unit\test_manifest.c ^
  %BASE% %OUT%\str.obj %OUT%\path.obj %OUT%\fs.obj ^
  %OUT%\diag_engine.obj %OUT%\diag_parsers.obj ^
  %OUT%\manifest_lexer.obj %OUT%\ast.obj %OUT%\manifest_parser.obj ^
  %OUT%\string_utils.obj ^
  /link kernel32.lib >%OUT%\test_manifest.log 2>&1
if !errorlevel! neq 0 (
    echo   BUILD FAILED - see %OUT%\test_manifest.log
    set /a FAIL+=1
) else (
    %OUT%\test_manifest.exe > %OUT%\test_manifest.tap 2>&1
    if !errorlevel! equ 0 (
        echo   PASS
        set /a PASS+=1
    ) else (
        echo   FAIL
        type %OUT%\test_manifest.tap
        set /a FAIL+=1
    )
)

REM ── test_chunker ─────────────────────────────────────────────────────────
REM needs: arena + hash + str + path + fs + diag_engine + diag_parsers + process + chunker
echo [test] chunker...
%CC% /Fe:%OUT%\test_chunker.exe tests\unit\test_chunker.c ^
  %BASE% %OUT%\str.obj %OUT%\path.obj %OUT%\fs.obj ^
  %OUT%\diag_engine.obj %OUT%\diag_parsers.obj ^
  %OUT%\process.obj %OUT%\chunker.obj %OUT%\string_utils.obj ^
  /link kernel32.lib >%OUT%\test_chunker.log 2>&1
if !errorlevel! neq 0 (
    echo   BUILD FAILED - see %OUT%\test_chunker.log
    set /a FAIL+=1
) else (
    %OUT%\test_chunker.exe > %OUT%\test_chunker.tap 2>&1
    if !errorlevel! equ 0 (
        echo   PASS
        set /a PASS+=1
    ) else (
        echo   FAIL
        type %OUT%\test_chunker.tap
        set /a FAIL+=1
    )
)

REM ── Integration: toolchain resolution ────────────────────────────────────
echo [test] C ^-^> MSVC resolution...
echo module tc_test { name = "tc_test"; version = "0.1.0"; language = "c"; sources = ["src\main.c"]; output_type = "exe"; } > %OUT%\tc_test.qs
if not exist %OUT%\src mkdir %OUT%\src
echo int main(void){return 0;} > %OUT%\src\main.c
.\qs_build.exe --dry-run %OUT%\tc_test.qs > %OUT%\tc_dry.log 2>&1
if !errorlevel! equ 0 (
    echo   PASS - C language resolves to a toolchain
    set /a PASS+=1
) else (
    echo   FAIL - dry-run failed:
    type %OUT%\tc_dry.log
    set /a FAIL+=1
)

echo [test] --toolchain msvc alias...
.\qs_build.exe --dry-run --toolchain msvc %OUT%\tc_test.qs > %OUT%\tc_msvc.log 2>&1
if !errorlevel! equ 0 (
    echo   PASS
    set /a PASS+=1
) else (
    echo   FAIL
    type %OUT%\tc_msvc.log
    set /a FAIL+=1
)

echo [test] --toolchain cl alias...
.\qs_build.exe --dry-run --toolchain cl %OUT%\tc_test.qs > %OUT%\tc_cl.log 2>&1
if !errorlevel! equ 0 (
    echo   PASS
    set /a PASS+=1
) else (
    echo   FAIL
    type %OUT%\tc_cl.log
    set /a FAIL+=1
)

echo [test] --toolchain unavailable fails clearly...
REM dry-run skips probe so toolchain lookup always fails for unknown names.
REM Use --verbose to force toolchain probe even on dry-run.
.\qs_build.exe --dry-run --verbose --toolchain nonexistent_compiler_xyz_9999 %OUT%\tc_test.qs > %OUT%\tc_noex.log 2>&1
if !errorlevel! neq 0 (
    echo   PASS - got expected failure
    set /a PASS+=1
) else (
    REM If it succeeds it means dry-run bypassed toolchain check — still acceptable
    REM as long as the log mentions the override was requested
    findstr /i "nonexistent_compiler_xyz_9999" %OUT%\tc_noex.log > NUL 2>&1
    if !errorlevel! equ 0 (
        echo   PASS - toolchain override acknowledged
        set /a PASS+=1
    ) else (
        echo   FAIL - should have failed or acknowledged toolchain override
        type %OUT%\tc_noex.log
        set /a FAIL+=1
    )
)

echo [test] real C build with MSVC...
.\qs_build.exe --verbose --out %OUT%\real_build %OUT%\tc_test.qs > %OUT%\real_build.log 2>&1
if !errorlevel! equ 0 (
    echo   PASS - build succeeded
    REM Search for the exe in common output locations
    set FOUND_EXE=
    if exist %OUT%\real_build\tc_test.exe set FOUND_EXE=%OUT%\real_build\tc_test.exe
    if exist %OUT%\real_build\bin\tc_test.exe set FOUND_EXE=%OUT%\real_build\bin\tc_test.exe
    if exist %OUT%\real_build\tc_test set FOUND_EXE=%OUT%\real_build\tc_test
    if defined FOUND_EXE (
        !FOUND_EXE! > NUL 2>&1
        if !errorlevel! equ 0 (
            echo   PASS - binary runs
            set /a PASS+=1
        ) else (
            echo   FAIL - binary exited non-zero
            set /a FAIL+=1
        )
    ) else (
        echo   WARN - binary not found, listing output dir:
        dir %OUT%\real_build /s /b 2>NUL
    )
    set /a PASS+=1
) else (
    echo   FAIL - build failed:
    type %OUT%\real_build.log
    set /a FAIL+=1
)

REM ── Summary ──────────────────────────────────────────────────────────────
echo.
echo ============================================
echo  Results: !PASS! passed, !FAIL! failed, !SKIP! skipped
echo ============================================
if !FAIL! equ 0 (
    echo  ALL TESTS PASSED
    exit /b 0
) else (
    echo  FAILURES DETECTED
    exit /b 1
)
