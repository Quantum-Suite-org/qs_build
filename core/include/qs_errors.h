/* qs_errors.h — Complete error catalogue for qs_build (C11, zero deps)
 *
 * STRUCTURE
 * ─────────
 * Every qs_build diagnostic code lives here, organised into families by
 * prefix:
 *
 *   QSB-ARG   CLI argument / option errors
 *   QSB-AST   AST / parser internal errors
 *   QSB-CHK   Unity-chunker errors
 *   QSB-CFG   Config-loader / .qsbuildrc errors
 *   QSB-CMP   Generic compilation errors (emitted by drivers)
 *   QSB-CSH   C# / dotnet driver errors
 *   QSB-DEP   Dependency-graph / topological-sort errors
 *   QSB-DRV   Generic driver dispatch errors
 *   QSB-EXT   Extension (JSON-RPC subprocess) errors
 *   QSB-FS    Filesystem / path errors
 *   QSB-GO    Go driver errors
 *   QSB-INC   Incremental / .d depfile errors
 *   QSB-INT   qs_build internal / assertion errors
 *   QSB-JAV   Java driver errors
 *   QSB-LEX   Manifest lexer errors
 *   QSB-LNK   Linker errors
 *   QSB-LTO   LTO helper errors
 *   QSB-MAN   Manifest parser errors
 *   QSB-MIG   Migrator (CMake/Cargo/go.mod) errors
 *   QSB-MON   Phase-monitor / telemetry errors
 *   QSB-NET   Remote-cache / HTTP errors
 *   QSB-OOM   Out-of-memory / arena errors
 *   QSB-PCH   Precompiled-header errors
 *   QSB-PIP   Pipeline orchestrator errors
 *   QSB-PKG   Packager (.qpkg) errors
 *   QSB-PLG   Plugin (dlopen) errors
 *   QSB-PRB   Toolchain-probe errors
 *   QSB-PRC   Subprocess / process errors
 *   QSB-PYT   Python driver errors
 *   QSB-RB    Remote-build / remote-cache errors
 *   QSB-REF   Reflection extractor errors
 *   QSB-RST   Rust driver errors
 *   QSB-RUB   Ruby driver errors
 *   QSB-SCH   Scheduler / DAG errors
 *   QSB-SCN   Source scanner errors
 *   QSB-SCR   Scripting / hook errors
 *   QSB-SER   Serialisation (.qpkg v2) errors
 *   QSB-SHD   Shader compiler errors
 *   QSB-SYS   System / OS capability errors
 *   QSB-THR   Thread-pool errors
 *   QSB-TST   Test-runner errors
 *   QSB-VAL   Manifest validator errors
 *   QSB-WRK   Workspace / multi-module errors
 *   QSB-ZIG   Zig driver errors
 *
 * USAGE
 * ─────
 * 1.  Every error code is a string literal that can be assigned to
 *     qs_diag_t.code and passed to qs_diag_emit_simple().
 *
 * 2.  qs_error_describe(code) returns a human-readable name, a hint,
 *     and a URL for any code in the catalogue.
 *
 * 3.  qs_error_emit() is a convenience wrapper that looks up the
 *     catalogue entry and calls qs_diag_emit() with fully-populated
 *     metadata.
 *
 * 4.  QSB_ASSERT() is a diagnostic-backed assertion for internal checks.
 *
 * ADDING NEW CODES
 * ────────────────
 *  a) Add a #define in the appropriate family block below.
 *  b) Add a row to the qs_error_catalog[] table in errors/errors.c.
 *  c) Bump QS_ERROR_CATALOG_VERSION.
 */
#ifndef QS_ERRORS_H
#define QS_ERRORS_H
#include "qs_types.h"
#include "qs_arena.h"
#include "qs_diag.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * Catalogue version — increment on every addition or rename.
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QS_ERROR_CATALOG_VERSION 1

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-ARG  CLI argument / option errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_ARG001  "QSB-ARG001"   /* unknown flag or option                  */
#define QSB_ARG002  "QSB-ARG002"   /* flag requires a value but none given     */
#define QSB_ARG003  "QSB-ARG003"   /* invalid value for flag                   */
#define QSB_ARG004  "QSB-ARG004"   /* conflicting flags (e.g. --pch --no-pch) */
#define QSB_ARG005  "QSB-ARG005"   /* --jobs value out of range (< 1)          */
#define QSB_ARG006  "QSB-ARG006"   /* --error-limit value out of range         */
#define QSB_ARG007  "QSB-ARG007"   /* --chunk-size value out of range          */
#define QSB_ARG008  "QSB-ARG008"   /* manifest path not provided               */
#define QSB_ARG009  "QSB-ARG009"   /* unrecognised sub-command                 */
#define QSB_ARG010  "QSB-ARG010"   /* --target triple is malformed             */
#define QSB_ARG011  "QSB-ARG011"   /* --profile value not recognised           */
#define QSB_ARG012  "QSB-ARG012"   /* --types value not recognised             */
#define QSB_ARG013  "QSB-ARG013"   /* too many positional arguments            */
#define QSB_ARG014  "QSB-ARG014"   /* duplicate flag                           */
#define QSB_ARG015  "QSB-ARG015"   /* --out directory is a regular file        */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-AST  AST / parser internal errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_AST001  "QSB-AST001"   /* unexpected token in expression           */
#define QSB_AST002  "QSB-AST002"   /* unexpected end-of-file                   */
#define QSB_AST003  "QSB-AST003"   /* string literal not terminated            */
#define QSB_AST004  "QSB-AST004"   /* list not closed (missing ']')            */
#define QSB_AST005  "QSB-AST005"   /* block not closed (missing '}')           */
#define QSB_AST006  "QSB-AST006"   /* expected '=' after key                   */
#define QSB_AST007  "QSB-AST007"   /* expected ';' after statement             */
#define QSB_AST008  "QSB-AST008"   /* duplicate key in block                   */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-CHK  Unity-chunker errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_CHK001  "QSB-CHK001"   /* chunk I/O write failed                   */
#define QSB_CHK002  "QSB-CHK002"   /* source file not readable during chunking */
#define QSB_CHK003  "QSB-CHK003"   /* chunk plan produced zero chunks          */
#define QSB_CHK004  "QSB-CHK004"   /* chunk size is zero or negative           */
#define QSB_CHK005  "QSB-CHK005"   /* unity file creation failed (permissions) */
#define QSB_CHK006  "QSB-CHK006"   /* #pragma NO_UNITY override not honoured   */
#define QSB_CHK007  "QSB-CHK007"   /* header frequency scan I/O error          */
#define QSB_CHK010  "QSB-CHK010"   /* chunk output dir cannot be created       */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-CFG  Config-loader / .qsbuildrc errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_CFG001  "QSB-CFG001"   /* .qsbuildrc parse error                   */
#define QSB_CFG002  "QSB-CFG002"   /* unknown key in .qsbuildrc                */
#define QSB_CFG003  "QSB-CFG003"   /* invalid value type in .qsbuildrc         */
#define QSB_CFG004  "QSB-CFG004"   /* env var value is malformed               */
#define QSB_CFG005  "QSB-CFG005"   /* config file could not be opened          */
#define QSB_CFG006  "QSB-CFG006"   /* conflicting config from env and file     */
#define QSB_CFG007  "QSB-CFG007"   /* config value out of range                */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-CMP  Generic compile-phase errors (emitted by every driver)
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_CMP001  "QSB-CMP001"   /* compiler process spawn failed            */
#define QSB_CMP002  "QSB-CMP002"   /* compiler exited with non-zero status     */
#define QSB_CMP003  "QSB-CMP003"   /* compilation timed out                    */
#define QSB_CMP004  "QSB-CMP004"   /* no sources to compile                    */
#define QSB_CMP005  "QSB-CMP005"   /* output object file not produced          */
#define QSB_CMP006  "QSB-CMP006"   /* stderr capture truncated (too large)     */
#define QSB_CMP007  "QSB-CMP007"   /* compile command line too long            */
#define QSB_CMP008  "QSB-CMP008"   /* incompatible standard version for driver */
#define QSB_CMP009  "QSB-CMP009"   /* define string contains invalid character */
#define QSB_CMP010  "QSB-CMP010"   /* include path contains invalid character  */
#define QSB_CMP011  "QSB-CMP011"   /* error limit reached; build halted        */
#define QSB_CMP012  "QSB-CMP012"   /* cross-compile flags incompatible         */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-CSH  C# / dotnet driver errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_CSH001  "QSB-CSH001"   /* dotnet SDK not found on PATH             */
#define QSB_CSH002  "QSB-CSH002"   /* csc / mcs not found (legacy fallback)    */
#define QSB_CSH003  "QSB-CSH003"   /* dotnet build failed                      */
#define QSB_CSH004  "QSB-CSH004"   /* nuget pack failed                        */
#define QSB_CSH005  "QSB-CSH005"   /* nuget publish failed                     */
#define QSB_CSH006  "QSB-CSH006"   /* invalid dotnet_sdk value in manifest     */
#define QSB_CSH007  "QSB-CSH007"   /* project file generation failed           */
#define QSB_CSH008  "QSB-CSH008"   /* assembly reference not found             */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-DEP  Dependency-graph errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_DEP001  "QSB-DEP001"   /* dependency cycle detected                */
#define QSB_DEP002  "QSB-DEP002"   /* declared dependency module not found     */
#define QSB_DEP003  "QSB-DEP003"   /* self-dependency (module depends on self) */
#define QSB_DEP004  "QSB-DEP004"   /* topological sort overflowed arena        */
#define QSB_DEP005  "QSB-DEP005"   /* dependency graph has too many nodes      */
#define QSB_DEP006  "QSB-DEP006"   /* diamond dependency version conflict      */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-DRV  Driver dispatch errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_DRV001  "QSB-DRV001"   /* no driver registered for language        */
#define QSB_DRV002  "QSB-DRV002"   /* driver returned unexpected result code   */
#define QSB_DRV003  "QSB-DRV003"   /* cross-compile not supported by driver    */
#define QSB_DRV004  "QSB-DRV004"   /* output type not supported by driver      */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-EXT  Extension (JSON-RPC subprocess) errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_EXT001  "QSB-EXT001"   /* extension executable not found           */
#define QSB_EXT002  "QSB-EXT002"   /* extension JSON-RPC handshake failed      */
#define QSB_EXT003  "QSB-EXT003"   /* extension responded with error           */
#define QSB_EXT004  "QSB-EXT004"   /* extension timed out                      */
#define QSB_EXT005  "QSB-EXT005"   /* extension response JSON malformed        */
#define QSB_EXT006  "QSB-EXT006"   /* extension protocol version mismatch      */
#define QSB_EXT007  "QSB-EXT007"   /* extension crashed (non-zero exit)        */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-FS   Filesystem / path errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_FS001   "QSB-FS001"    /* path too long for platform               */
#define QSB_FS002   "QSB-FS002"    /* file not found                           */
#define QSB_FS003   "QSB-FS003"    /* directory not found                      */
#define QSB_FS004   "QSB-FS004"    /* permission denied                        */
#define QSB_FS005   "QSB-FS005"    /* file read error                          */
#define QSB_FS006   "QSB-FS006"    /* file write error                         */
#define QSB_FS007   "QSB-FS007"    /* directory create failed                  */
#define QSB_FS008   "QSB-FS008"    /* directory delete failed                  */
#define QSB_FS009   "QSB-FS009"    /* file copy failed                         */
#define QSB_FS010   "QSB-FS010"    /* file move / rename failed                */
#define QSB_FS011   "QSB-FS011"    /* symlink creation failed                  */
#define QSB_FS012   "QSB-FS012"    /* path traversal detected (security)       */
#define QSB_FS013   "QSB-FS013"    /* disk full or quota exceeded              */
#define QSB_FS014   "QSB-FS014"    /* recursive scan hit depth limit           */
#define QSB_FS015   "QSB-FS015"    /* file is empty                            */
#define QSB_FS016   "QSB-FS016"    /* file is a directory (expected file)      */
#define QSB_FS017   "QSB-FS017"    /* file is not a directory                  */
#define QSB_FS018   "QSB-FS018"    /* glob pattern produced no matches         */
#define QSB_FS019   "QSB-FS019"    /* stat() failed                            */
#define QSB_FS020   "QSB-FS020"    /* temp file creation failed                */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-GO   Go driver errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_GO001   "QSB-GO001"    /* go binary not found on PATH              */
#define QSB_GO002   "QSB-GO002"    /* go build failed                          */
#define QSB_GO003   "QSB-GO003"    /* go test failed                           */
#define QSB_GO004   "QSB-GO004"    /* GOPATH / GOROOT misconfigured            */
#define QSB_GO005   "QSB-GO005"    /* go.mod module path not set               */
#define QSB_GO006   "QSB-GO006"    /* GOOS / GOARCH pair unsupported           */
#define QSB_GO007   "QSB-GO007"    /* go mod tidy failed                       */
#define QSB_GO008   "QSB-GO008"    /* CGO_ENABLED=1 but C compiler not found   */
#define QSB_GO009   "QSB-GO009"    /* go install failed                        */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-INC  Incremental build / depfile errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_INC001  "QSB-INC001"   /* .d depfile not found for source          */
#define QSB_INC002  "QSB-INC002"   /* .d depfile parse error                   */
#define QSB_INC003  "QSB-INC003"   /* fingerprint file corrupted               */
#define QSB_INC004  "QSB-INC004"   /* fingerprint store write failed           */
#define QSB_INC005  "QSB-INC005"   /* cache directory not accessible           */
#define QSB_INC006  "QSB-INC006"   /* hash mismatch; forcing rebuild           */
#define QSB_INC007  "QSB-INC007"   /* flag set changed; invalidating cache     */
#define QSB_INC008  "QSB-INC008"   /* incremental cache version mismatch       */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-INT  Internal / assertion errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_INT001  "QSB-INT001"   /* internal assertion failed                */
#define QSB_INT002  "QSB-INT002"   /* null pointer dereference guard           */
#define QSB_INT003  "QSB-INT003"   /* arena overflow — scratch arena too small */
#define QSB_INT004  "QSB-INT004"   /* unreachable code path reached            */
#define QSB_INT005  "QSB-INT005"   /* vector out-of-bounds access              */
#define QSB_INT006  "QSB-INT006"   /* state machine entered invalid state      */
#define QSB_INT007  "QSB-INT007"   /* unimplemented code path invoked          */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-JAV  Java driver errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_JAV001  "QSB-JAV001"   /* javac not found on PATH                  */
#define QSB_JAV002  "QSB-JAV002"   /* java not found on PATH                   */
#define QSB_JAV003  "QSB-JAV003"   /* javac compilation failed                 */
#define QSB_JAV004  "QSB-JAV004"   /* jar packaging failed                     */
#define QSB_JAV005  "QSB-JAV005"   /* manifest Class-Path invalid              */
#define QSB_JAV006  "QSB-JAV006"   /* java_version value not recognised        */
#define QSB_JAV007  "QSB-JAV007"   /* classpath entry not found                */
#define QSB_JAV008  "QSB-JAV008"   /* maven deploy failed                      */
#define QSB_JAV009  "QSB-JAV009"   /* module-info.java parse error             */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-LEX  Manifest lexer errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_LEX001  "QSB-LEX001"   /* unexpected character                     */
#define QSB_LEX002  "QSB-LEX002"   /* unterminated string literal              */
#define QSB_LEX003  "QSB-LEX003"   /* invalid escape sequence in string        */
#define QSB_LEX004  "QSB-LEX004"   /* number literal overflow                  */
#define QSB_LEX005  "QSB-LEX005"   /* invalid identifier character             */
#define QSB_LEX006  "QSB-LEX006"   /* file could not be read for lexing        */
#define QSB_LEX007  "QSB-LEX007"   /* source file exceeds maximum lex size     */
#define QSB_LEX008  "QSB-LEX008"   /* invalid UTF-8 byte sequence              */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-LNK  Linker errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_LNK001  "QSB-LNK001"   /* linker not found (ar / link.exe / ld)   */
#define QSB_LNK002  "QSB-LNK002"   /* linker exited with non-zero status       */
#define QSB_LNK003  "QSB-LNK003"   /* no object files to link                  */
#define QSB_LNK004  "QSB-LNK004"   /* linker command line too long             */
#define QSB_LNK005  "QSB-LNK005"   /* library directory not found              */
#define QSB_LNK006  "QSB-LNK006"   /* unresolved external symbol               */
#define QSB_LNK007  "QSB-LNK007"   /* multiple definition of symbol            */
#define QSB_LNK008  "QSB-LNK008"   /* output binary not produced               */
#define QSB_LNK009  "QSB-LNK009"   /* mixed debug/release objects (MSVC)       */
#define QSB_LNK010  "QSB-LNK010"   /* LTO link step failed                     */
#define QSB_LNK011  "QSB-LNK011"   /* shared library missing soname            */
#define QSB_LNK012  "QSB-LNK012"   /* link timed out                           */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-LTO  Link-time optimisation errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_LTO001  "QSB-LTO001"   /* LTO not supported by toolchain           */
#define QSB_LTO002  "QSB-LTO002"   /* LTO flags incompatible with debug info   */
#define QSB_LTO003  "QSB-LTO003"   /* thin-LTO cache directory inaccessible    */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-MAN  Manifest (build.qs) parser errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_MAN001  "QSB-MAN001"   /* manifest file not found                  */
#define QSB_MAN002  "QSB-MAN002"   /* manifest file is empty                   */
#define QSB_MAN003  "QSB-MAN003"   /* 'module' keyword missing                 */
#define QSB_MAN004  "QSB-MAN004"   /* module name missing or empty             */
#define QSB_MAN005  "QSB-MAN005"   /* language field missing                   */
#define QSB_MAN006  "QSB-MAN006"   /* language value not recognised            */
#define QSB_MAN007  "QSB-MAN007"   /* output_type value not recognised         */
#define QSB_MAN008  "QSB-MAN008"   /* version string malformed                 */
#define QSB_MAN009  "QSB-MAN009"   /* sources list is empty                    */
#define QSB_MAN010  "QSB-MAN010"   /* sources field missing                    */
#define QSB_MAN011  "QSB-MAN011"   /* standard version not recognised          */
#define QSB_MAN012  "QSB-MAN012"   /* chunk_size value out of range            */
#define QSB_MAN013  "QSB-MAN013"   /* duplicate module block in file           */
#define QSB_MAN014  "QSB-MAN014"   /* field value is wrong type                */
#define QSB_MAN015  "QSB-MAN015"   /* unrecognised field key                   */
#define QSB_MAN016  "QSB-MAN016"   /* pch_header path is invalid               */
#define QSB_MAN017  "QSB-MAN017"   /* go_module path is not a valid Go module  */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-MIG  Migrator errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_MIG001  "QSB-MIG001"   /* source build file not found (CMakeLists) */
#define QSB_MIG002  "QSB-MIG002"   /* unsupported build system for migration   */
#define QSB_MIG003  "QSB-MIG003"   /* migration produced an empty build.qs     */
#define QSB_MIG004  "QSB-MIG004"   /* CMake variable expansion not supported   */
#define QSB_MIG005  "QSB-MIG005"   /* Cargo.toml parse failed                  */
#define QSB_MIG006  "QSB-MIG006"   /* go.mod parse failed                      */
#define QSB_MIG007  "QSB-MIG007"   /* migrator write of build.qs failed        */
#define QSB_MIG008  "QSB-MIG008"   /* ambiguous project structure              */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-MON  Phase-monitor / telemetry errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_MON001  "QSB-MON001"   /* telemetry output file could not be opened*/
#define QSB_MON002  "QSB-MON002"   /* phase name is NULL or empty              */
#define QSB_MON003  "QSB-MON003"   /* phase ended before it was started        */
#define QSB_MON004  "QSB-MON004"   /* too many concurrent phases (overflow)    */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-NET  Remote-cache / HTTP errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_NET001  "QSB-NET001"   /* remote cache URL not configured          */
#define QSB_NET002  "QSB-NET002"   /* DNS resolution failed                    */
#define QSB_NET003  "QSB-NET003"   /* TCP connection refused                   */
#define QSB_NET004  "QSB-NET004"   /* HTTP request timed out                   */
#define QSB_NET005  "QSB-NET005"   /* HTTP response status not 200/204         */
#define QSB_NET006  "QSB-NET006"   /* socket send / recv error                 */
#define QSB_NET007  "QSB-NET007"   /* remote cache response body truncated     */
#define QSB_NET008  "QSB-NET008"   /* remote cache authentication failed (401) */
#define QSB_NET009  "QSB-NET009"   /* remote cache object not found (404)      */
#define QSB_NET010  "QSB-NET010"   /* remote cache server error (5xx)          */
#define QSB_NET011  "QSB-NET011"   /* socket() or bind() failed                */
#define QSB_NET012  "QSB-NET012"   /* SSL/TLS not supported (plain HTTP only)  */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-OOM  Out-of-memory / arena errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_OOM001  "QSB-OOM001"   /* arena alloc failed — system OOM          */
#define QSB_OOM002  "QSB-OOM002"   /* scratch arena overflow                   */
#define QSB_OOM003  "QSB-OOM003"   /* vector growth failed                     */
#define QSB_OOM004  "QSB-OOM004"   /* string intern table full                 */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-PCH  Precompiled header errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_PCH001  "QSB-PCH001"   /* PCH compilation failed                   */
#define QSB_PCH002  "QSB-PCH002"   /* PCH header not found                     */
#define QSB_PCH003  "QSB-PCH003"   /* PCH output not produced                  */
#define QSB_PCH004  "QSB-PCH004"   /* PCH not supported by toolchain version   */
#define QSB_PCH005  "QSB-PCH005"   /* PCH invalidated by flag change           */
#define QSB_PCH006  "QSB-PCH006"   /* PCH header contains #pragma once error   */
#define QSB_PCH007  "QSB-PCH007"   /* auto-generated PCH write failed          */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-PIP  Pipeline orchestrator errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_PIP001  "QSB-PIP001"   /* pipeline phase failed                    */
#define QSB_PIP002  "QSB-PIP002"   /* output directory cannot be created       */
#define QSB_PIP003  "QSB-PIP003"   /* clean failed (could not remove out dir)  */
#define QSB_PIP004  "QSB-PIP004"   /* hook phase returned non-zero exit        */
#define QSB_PIP005  "QSB-PIP005"   /* workspace mode: no modules found         */
#define QSB_PIP006  "QSB-PIP006"   /* module build order could not be resolved */
#define QSB_PIP007  "QSB-PIP007"   /* dry-run requested but manifest invalid   */
#define QSB_PIP008  "QSB-PIP008"   /* asset pipeline phase failed              */
#define QSB_PIP009  "QSB-PIP009"   /* test phase failed                        */
#define QSB_PIP010  "QSB-PIP010"   /* publish phase failed                     */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-PKG  Packager / .qpkg errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_PKG001  "QSB-PKG001"   /* .qpkg header write failed                */
#define QSB_PKG002  "QSB-PKG002"   /* .qpkg file entry write failed            */
#define QSB_PKG003  "QSB-PKG003"   /* .qpkg output path could not be opened    */
#define QSB_PKG004  "QSB-PKG004"   /* .qpkg index block write failed           */
#define QSB_PKG005  "QSB-PKG005"   /* .qpkg checksum mismatch on verify        */
#define QSB_PKG006  "QSB-PKG006"   /* .qpkg magic bytes invalid                */
#define QSB_PKG007  "QSB-PKG007"   /* .qpkg version not supported              */
#define QSB_PKG008  "QSB-PKG008"   /* .qpkg index offset corrupt               */
#define QSB_PKG009  "QSB-PKG009"   /* .qpkg entry count exceeds limit          */
#define QSB_PKG010  "QSB-PKG010"   /* .qpkg v2 index write failed              */
#define QSB_PKG020  "QSB-PKG020"   /* .qpkg verify: file not found             */
#define QSB_PKG021  "QSB-PKG021"   /* .qpkg verify: read error                 */
#define QSB_PKG022  "QSB-PKG022"   /* .qpkg entry name is malformed            */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-PLG  Plugin (dlopen) errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_PLG001  "QSB-PLG001"   /* plugin file not found                    */
#define QSB_PLG002  "QSB-PLG002"   /* dlopen() / LoadLibrary() failed          */
#define QSB_PLG003  "QSB-PLG003"   /* qs_plugin_init symbol not exported       */
#define QSB_PLG004  "QSB-PLG004"   /* plugin api_version mismatch              */
#define QSB_PLG005  "QSB-PLG005"   /* plugin init() returned NULL              */
#define QSB_PLG006  "QSB-PLG006"   /* plugin name or version field empty       */
#define QSB_PLG007  "QSB-PLG007"   /* plugin hook registration failed          */
#define QSB_PLG008  "QSB-PLG008"   /* plugin raised exception during hook      */
#define QSB_PLG009  "QSB-PLG009"   /* plugin directory not found               */
#define QSB_PLG010  "QSB-PLG010"   /* too many plugins loaded (cap reached)    */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-PRB  Toolchain-probe errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_PRB001  "QSB-PRB001"   /* required compiler not found on PATH      */
#define QSB_PRB002  "QSB-PRB002"   /* compiler version query failed            */
#define QSB_PRB003  "QSB-PRB003"   /* compiler version too old for standard    */
#define QSB_PRB004  "QSB-PRB004"   /* multiple conflicting compilers found     */
#define QSB_PRB005  "QSB-PRB005"   /* sysroot not found                        */
#define QSB_PRB006  "QSB-PRB006"   /* cross-compile toolchain not found        */
#define QSB_PRB007  "QSB-PRB007"   /* toolchain probe timed out                */
#define QSB_PRB008  "QSB-PRB008"   /* linker probe failed                      */
#define QSB_PRB009  "QSB-PRB009"   /* assembler not found                      */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-PRC  Subprocess / process errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_PRC001  "QSB-PRC001"   /* fork() / CreateProcess() failed          */
#define QSB_PRC002  "QSB-PRC002"   /* pipe() creation failed                   */
#define QSB_PRC003  "QSB-PRC003"   /* waitpid() / GetExitCodeProcess() failed  */
#define QSB_PRC004  "QSB-PRC004"   /* process killed by signal                 */
#define QSB_PRC005  "QSB-PRC005"   /* stdout capture overflowed buffer         */
#define QSB_PRC006  "QSB-PRC006"   /* stderr capture overflowed buffer         */
#define QSB_PRC007  "QSB-PRC007"   /* process timed out and was killed         */
#define QSB_PRC008  "QSB-PRC008"   /* exec*() failed — bad executable          */
#define QSB_PRC009  "QSB-PRC009"   /* command string too long for exec         */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-PYT  Python driver errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_PYT001  "QSB-PYT001"   /* python3 / python not found on PATH       */
#define QSB_PYT002  "QSB-PYT002"   /* pip not found or inaccessible            */
#define QSB_PYT003  "QSB-PYT003"   /* setup.py / pyproject.toml missing        */
#define QSB_PYT004  "QSB-PYT004"   /* wheel build failed                       */
#define QSB_PYT005  "QSB-PYT005"   /* pip install failed                       */
#define QSB_PYT006  "QSB-PYT006"   /* twine upload (publish) failed            */
#define QSB_PYT007  "QSB-PYT007"   /* python_version format not recognised     */
#define QSB_PYT008  "QSB-PYT008"   /* virtualenv creation failed               */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-RB   Remote-build / remote-cache client errors  (also see QSB-NET)
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_RB001   "QSB-RB001"    /* cache PUT body larger than server limit  */
#define QSB_RB002   "QSB-RB002"    /* cache token expired or invalid           */
#define QSB_RB003   "QSB-RB003"    /* cache key collision (hash conflict)      */
#define QSB_RB004   "QSB-RB004"    /* fetched object failed integrity check    */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-REF  Reflection extractor errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_REF001  "QSB-REF001"   /* reflect.json write failed                */
#define QSB_REF002  "QSB-REF002"   /* QS_REFLECT macro malformed               */
#define QSB_REF003  "QSB-REF003"   /* duplicate reflected symbol name          */
#define QSB_REF004  "QSB-REF004"   /* source scan for reflection failed        */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-RST  Rust driver errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_RST001  "QSB-RST001"   /* cargo not found on PATH                  */
#define QSB_RST002  "QSB-RST002"   /* rustc not found on PATH (fallback mode)  */
#define QSB_RST003  "QSB-RST003"   /* cargo build failed                       */
#define QSB_RST004  "QSB-RST004"   /* Cargo.toml not found                     */
#define QSB_RST005  "QSB-RST005"   /* rust edition not recognised              */
#define QSB_RST006  "QSB-RST006"   /* cross-compile target not installed       */
#define QSB_RST007  "QSB-RST007"   /* cargo publish failed                     */
#define QSB_RST008  "QSB-RST008"   /* rustfmt / clippy subprocess failed       */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-RUB  Ruby driver errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_RUB001  "QSB-RUB001"   /* ruby not found on PATH                   */
#define QSB_RUB002  "QSB-RUB002"   /* gem not found on PATH                    */
#define QSB_RUB003  "QSB-RUB003"   /* ruby compilation (syntax check) failed   */
#define QSB_RUB004  "QSB-RUB004"   /* gem build failed                         */
#define QSB_RUB005  "QSB-RUB005"   /* gem push (publish) failed                */
#define QSB_RUB006  "QSB-RUB006"   /* gemspec file not found                   */
#define QSB_RUB007  "QSB-RUB007"   /* bundle install failed                    */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-SCH  Scheduler / DAG errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_SCH001  "QSB-SCH001"   /* scheduler ran out of work unexpectedly   */
#define QSB_SCH002  "QSB-SCH002"   /* job failed with unexpected error code    */
#define QSB_SCH003  "QSB-SCH003"   /* job node has no associated compile unit  */
#define QSB_SCH004  "QSB-SCH004"   /* scheduler dependency not yet satisfied   */
#define QSB_SCH005  "QSB-SCH005"   /* scheduler deadlock detected              */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-SCN  Source scanner errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_SCN001  "QSB-SCN001"   /* source directory not found               */
#define QSB_SCN002  "QSB-SCN002"   /* source scan returned no files            */
#define QSB_SCN003  "QSB-SCN003"   /* source file extension not recognised     */
#define QSB_SCN004  "QSB-SCN004"   /* source scan recursion limit reached      */
#define QSB_SCN005  "QSB-SCN005"   /* source list too large for arena          */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-SCR  Scripting / hook errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_SCR001  "QSB-SCR001"   /* hook script not found                    */
#define QSB_SCR002  "QSB-SCR002"   /* hook script exited with non-zero status  */
#define QSB_SCR003  "QSB-SCR003"   /* hook script timed out                    */
#define QSB_SCR004  "QSB-SCR004"   /* hook script interpreter not found        */
#define QSB_SCR005  "QSB-SCR005"   /* hook environment variable injection fail */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-SER  Serialisation / .qpkg v2 errors  (companion to QSB-PKG)
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_SER001  "QSB-SER001"   /* checksum algorithm not supported         */
#define QSB_SER002  "QSB-SER002"   /* index serialisation buffer overflow      */
#define QSB_SER003  "QSB-SER003"   /* entry count field overflow (> UINT32_MAX)*/
#define QSB_SER004  "QSB-SER004"   /* padding or alignment constraint violated */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-SHD  Shader compiler errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_SHD001  "QSB-SHD001"   /* glslc / dxc / metal not found on PATH   */
#define QSB_SHD002  "QSB-SHD002"   /* shader compilation failed                */
#define QSB_SHD003  "QSB-SHD003"   /* shader source file not found             */
#define QSB_SHD004  "QSB-SHD004"   /* shader target stage not recognised       */
#define QSB_SHD005  "QSB-SHD005"   /* shader output not produced               */
#define QSB_SHD006  "QSB-SHD006"   /* shader include path not found            */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-SYS  System / OS capability errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_SYS001  "QSB-SYS001"   /* required system call not available       */
#define QSB_SYS002  "QSB-SYS002"   /* CPU core count could not be determined   */
#define QSB_SYS003  "QSB-SYS003"   /* available memory too low to build        */
#define QSB_SYS004  "QSB-SYS004"   /* POSIX-only feature on non-POSIX OS       */
#define QSB_SYS005  "QSB-SYS005"   /* Win32-only feature on non-Windows OS     */
#define QSB_SYS006  "QSB-SYS006"   /* clock_gettime / QueryPerformanceCounter fail */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-THR  Thread-pool errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_THR001  "QSB-THR001"   /* pthread_create / CreateThread failed     */
#define QSB_THR002  "QSB-THR002"   /* mutex init failed                        */
#define QSB_THR003  "QSB-THR003"   /* condition variable init failed           */
#define QSB_THR004  "QSB-THR004"   /* thread join timed out                    */
#define QSB_THR005  "QSB-THR005"   /* worker thread crashed                    */
#define QSB_THR006  "QSB-THR006"   /* job queue overflow (too many pending)    */
#define QSB_THR007  "QSB-THR007"   /* thread pool not initialised              */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-TST  Test-runner errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_TST001  "QSB-TST001"   /* test binary not found                    */
#define QSB_TST002  "QSB-TST002"   /* test returned non-zero exit              */
#define QSB_TST003  "QSB-TST003"   /* TAP output malformed                     */
#define QSB_TST004  "QSB-TST004"   /* test timed out                           */
#define QSB_TST005  "QSB-TST005"   /* test count mismatch (plan != actual)     */
#define QSB_TST006  "QSB-TST006"   /* no test sources declared in manifest     */
#define QSB_TST007  "QSB-TST007"   /* test binary could not be executed        */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-VAL  Manifest validator errors / warnings (existing + extended)
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_VAL001  "QSB-VAL001"   /* source file not found on disk            */
#define QSB_VAL002  "QSB-VAL002"   /* duplicate source file entry              */
#define QSB_VAL003  "QSB-VAL003"   /* output type not valid for language       */
#define QSB_VAL004  "QSB-VAL004"   /* pch_header path not found                */
#define QSB_VAL005  "QSB-VAL005"   /* include directory not found              */
#define QSB_VAL006  "QSB-VAL006"   /* LTO + debug-info combination warning     */
#define QSB_VAL007  "QSB-VAL007"   /* dependency name contains whitespace      */
#define QSB_VAL008  "QSB-VAL008"   /* define string contains '=' without value */
#define QSB_VAL009  "QSB-VAL009"   /* chunk_size exceeds source count          */
#define QSB_VAL010  "QSB-VAL010"   /* module name contains invalid characters  */
#define QSB_VAL011  "QSB-VAL011"   /* lib_dirs entry not found on disk         */
#define QSB_VAL012  "QSB-VAL012"   /* version string does not follow semver    */
#define QSB_VAL013  "QSB-VAL013"   /* test source not found on disk            */
#define QSB_VAL014  "QSB-VAL014"   /* out_dir path is absolute (must be relative) */
#define QSB_VAL015  "QSB-VAL015"   /* extra_flags contains shell-injection risk */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-WRK  Workspace / multi-module errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_WRK001  "QSB-WRK001"   /* workspace root not found                 */
#define QSB_WRK002  "QSB-WRK002"   /* no build.qs files found in workspace     */
#define QSB_WRK003  "QSB-WRK003"   /* workspace scan hit max module limit      */
#define QSB_WRK004  "QSB-WRK004"   /* duplicate module name in workspace       */
#define QSB_WRK005  "QSB-WRK005"   /* workspace build order could not be resolved */

/* ═══════════════════════════════════════════════════════════════════════════
 * QSB-ZIG  Zig driver errors
 * ═══════════════════════════════════════════════════════════════════════════ */
#define QSB_ZIG001  "QSB-ZIG001"   /* zig binary not found on PATH             */
#define QSB_ZIG002  "QSB-ZIG002"   /* zig build failed                         */
#define QSB_ZIG003  "QSB-ZIG003"   /* build.zig generation failed              */
#define QSB_ZIG004  "QSB-ZIG004"   /* zig cc fallback failed                   */
#define QSB_ZIG005  "QSB-ZIG005"   /* zig target triple not recognised         */
#define QSB_ZIG006  "QSB-ZIG006"   /* zig version too old (< 0.12)             */
#define QSB_ZIG007  "QSB-ZIG007"   /* zig translate-c failed                   */

/* ═══════════════════════════════════════════════════════════════════════════
 * Catalogue entry — one per code
 * ═══════════════════════════════════════════════════════════════════════════ */
typedef struct {
    const char    *code;          /* e.g. "QSB-VAL001"                        */
    qs_severity_t  default_sev;   /* severity when not overridden by context  */
    const char    *name;          /* short human name                         */
    const char    *hint;          /* actionable fix suggestion                */
    const char    *url;           /* documentation URL (may be NULL)          */
} qs_error_entry_t;

/* ── Public API ─────────────────────────────────────────────────────────── */

/**
 * qs_error_lookup() — find a catalogue entry by code string.
 * Returns NULL if the code is not registered.
 */
const qs_error_entry_t *qs_error_lookup(const char *code);

/**
 * qs_error_emit() — convenience: look up the catalogue entry for @code,
 * populate a qs_diag_t with its default severity / hint / url, override
 * the message with @message, override severity with @sev if != (qs_severity_t)-1,
 * and call qs_diag_emit() on @engine.
 *
 * If @code is not in the catalogue the diagnostic is still emitted with
 * whatever @sev / @message you pass (degraded mode).
 */
void qs_error_emit(qs_diag_engine_t *engine,
                   qs_arena_t       *arena,
                   const char       *code,
                   qs_severity_t     sev,        /* (qs_severity_t)-1 → use catalogue default */
                   const char       *tool,
                   qs_loc_t          loc,
                   const char       *message);   /* NULL → use catalogue name as message */

/**
 * qs_error_count() — total number of codes in the catalogue.
 */
qs_size_t qs_error_count(void);

/**
 * qs_error_catalog_ptr() — pointer to the first entry of the catalogue
 * array (terminated by an entry with code==NULL).  Useful for tooling
 * that wants to iterate all codes (e.g. the doc generator).
 */
const qs_error_entry_t *qs_error_catalog_ptr(void);

/* ── Diagnostic-backed assertion ─────────────────────────────────────────── */

/**
 * QSB_ASSERT(engine, arena, cond, message)
 * If @cond is false, emits QSB-INT001 as a fatal diagnostic and returns
 * QS_ERROR_INTERNAL from the enclosing function.
 * Only active in debug builds (NDEBUG not defined) unless QSB_ASSERT_ALWAYS
 * is defined.
 */
#if !defined(NDEBUG) || defined(QSB_ASSERT_ALWAYS)
#  define QSB_ASSERT(engine, arena, cond, msg)                              \
     do {                                                                    \
         if (QS_UNLIKELY(!(cond))) {                                         \
             qs_error_emit((engine),(arena),QSB_INT001,QS_SEV_FATAL,        \
                 "qs_build",QS_LOC_UNKNOWN,                                  \
                 qs_arena_sprintf((arena),                                   \
                     "assertion failed: %s  (%s:%d)",                       \
                     (msg), __FILE__, __LINE__));                            \
             return QS_ERROR_INTERNAL;                                       \
         }                                                                   \
     } while(0)
#else
#  define QSB_ASSERT(engine, arena, cond, msg)  QS_UNUSED(cond)
#endif

/**
 * QSB_ASSERT_NORETURN — same but calls abort() instead of returning, for
 * use in void functions or truly unrecoverable states.
 */
#if !defined(NDEBUG) || defined(QSB_ASSERT_ALWAYS)
#  include <stdlib.h>
#  define QSB_ASSERT_NORETURN(engine, arena, cond, msg)                     \
     do {                                                                    \
         if (QS_UNLIKELY(!(cond))) {                                         \
             qs_error_emit((engine),(arena),QSB_INT001,QS_SEV_FATAL,        \
                 "qs_build",QS_LOC_UNKNOWN,                                  \
                 qs_arena_sprintf((arena),                                   \
                     "assertion failed: %s  (%s:%d)",                       \
                     (msg), __FILE__, __LINE__));                            \
             qs_diag_print_all(engine);                                      \
             abort();                                                        \
         }                                                                   \
     } while(0)
#else
#  define QSB_ASSERT_NORETURN(engine, arena, cond, msg)  QS_UNUSED(cond)
#endif

#endif /* QS_ERRORS_H */
