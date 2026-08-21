/*
 * process.c — Cross-platform subprocess spawn + stdout/stderr capture.
 * Zero external dependencies. POSIX via fork/execvp/pipe. Win32 via CreateProcess.
 * Used by every language driver to invoke compilers, linkers, and package tools.
 */
#include "qs_process.h"
#include "qs_arena.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#  include <sys/wait.h>
#  include <sys/time.h>
#  include <errno.h>
#  include <fcntl.h>
#endif

/* ── Portable wall-clock time ────────────────────────────────────────────── */
static qs_u64 now_us(void) {
#ifdef _WIN32
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    qs_u64 t = ((qs_u64)ft.dwHighDateTime<<32)|ft.dwLowDateTime;
    return t/10; /* 100ns → 1us */
#else
    struct timeval tv; gettimeofday(&tv,NULL);
    return (qs_u64)tv.tv_sec*1000000ULL + (qs_u64)tv.tv_usec;
#endif
}

/* ── Read entire fd into a null-terminated arena string ──────────────────── */
#ifndef _WIN32
static char *read_fd(qs_arena_t *a, int fd) {
    qs_size_t cap=4096, len=0;
    char *buf=(char*)qs_arena_alloc(a,cap,1);
    if (!buf) return (char*)"";
    ssize_t n;
    while ((n=read(fd,buf+len,(size_t)(cap-len-1)))>0) {
        len+=(qs_size_t)n;
        if (len+1>=cap) {
            qs_size_t ncap=cap*2;
            char *nb=(char*)qs_arena_realloc(a,buf,cap,ncap,1);
            if (!nb) break;
            buf=nb; cap=ncap;
        }
    }
    buf[len]='\0';
    return buf;
}
#endif

/* ── Main spawn function ─────────────────────────────────────────────────── */
qs_result_t qs_proc_run(qs_arena_t *a, const char *const *argv,
                          const char *const *env, qs_u64 timeout_us,
                          qs_proc_result_t *out) {
    (void)env; /* env inheritance on both platforms: inherit by default */
    /* timeout enforced below per-platform */
    memset(out,0,sizeof(*out));
    qs_u64 t0=now_us();

#ifdef _WIN32
    /* Build command-line for CreateProcess.
     *
     * IMPORTANT: We use lpApplicationName = argv[0] (the resolved executable
     * path) and pass the full command line as lpCommandLine. This bypasses any
     * shell (cmd.exe / bash) so MSYS/Git Bash cannot mangle MSVC-style flags
     * like /nologo, /Fe:, /O2. The process receives the arguments verbatim.
     *
     * Quoting rules (CommandLineToArgvW / argv parsing in the CRT):
     *   - Each argument is wrapped in double-quotes.
     *   - Backslashes before a double-quote are doubled.
     *   - Other backslashes are left alone.
     * argv[0] is the executable path — include it quoted so the CRT sees it
     * as $0 (required by CommandLineToArgvW convention).
     */
    qs_size_t cmdlen = 0;
    for (int i = 0; argv[i]; i++) {
        cmdlen += strlen(argv[i]) * 2 + 4; /* worst-case quoting */
    }
    char *cmd = (char*)qs_arena_alloc(a, cmdlen + 4, 1);
    if (!cmd) return QS_ERROR_OOM;
    qs_size_t pos = 0;
    for (int i = 0; argv[i]; i++) {
        if (i) cmd[pos++] = ' ';
        cmd[pos++] = '"';
        for (const char *p = argv[i]; *p; p++) {
            if (*p == '"') {
                /* Escape embedded double-quote */
                cmd[pos++] = '\\'; cmd[pos++] = '"';
            } else {
                cmd[pos++] = *p;
            }
        }
        cmd[pos++] = '"';
    }
    cmd[pos] = '\0';

    /* Redirect stdin to NUL so interactive tools (like cl.exe with no args)
     * cannot block waiting for keyboard input. */
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE hNul = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE,
                               &sa, OPEN_EXISTING, 0, NULL);

    HANDLE hStdoutR,hStdoutW,hStderrR,hStderrW;
    if (!CreatePipe(&hStdoutR,&hStdoutW,&sa,0)) {
        CloseHandle(hNul); return QS_ERROR_PROCESS;
    }
    if (!CreatePipe(&hStderrR,&hStderrW,&sa,0)) {
        CloseHandle(hStdoutR);CloseHandle(hStdoutW);CloseHandle(hNul);
        return QS_ERROR_PROCESS;
    }
    SetHandleInformation(hStdoutR, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hStderrR, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si; memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
    si.hStdOutput = hStdoutW;
    si.hStdError  = hStderrW;
    si.hStdInput  = (hNul != INVALID_HANDLE_VALUE) ? hNul : GetStdHandle(STD_INPUT_HANDLE);
    si.dwFlags    = STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi;
    /* Use argv[0] as lpApplicationName so Windows resolves the exact binary
     * without going through PATH or shell expansion. */
    if (!CreateProcessA(argv[0], cmd, NULL, NULL, TRUE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        /* Fallback: let Windows search PATH (argv[0] may be just a name) */
        if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE,
                            CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            CloseHandle(hStdoutR);CloseHandle(hStdoutW);
            CloseHandle(hStderrR);CloseHandle(hStderrW);
            if (hNul != INVALID_HANDLE_VALUE) CloseHandle(hNul);
            return QS_ERROR_PROCESS;
        }
    }
    CloseHandle(hStdoutW); CloseHandle(hStderrW);
    if (hNul != INVALID_HANDLE_VALUE) CloseHandle(hNul);

    /* Read stdout+stderr with timeout.
     * timeout_us==0 means infinite; otherwise kill after timeout. */
    DWORD timeout_ms = (timeout_us == 0)
        ? INFINITE
        : (DWORD)(timeout_us / 1000ULL);

    qs_size_t ocap=4096,olen=0,ecap=4096,elen=0;
    char *obuf=(char*)qs_arena_alloc(a,ocap,1);
    char *ebuf=(char*)qs_arena_alloc(a,ecap,1);
    DWORD nr;
    char tmp[4096];
    DWORD t_start_ms = GetTickCount();
    for(;;){
        /* Check timeout */
        if (timeout_ms != INFINITE) {
            DWORD elapsed = GetTickCount() - t_start_ms;
            if (elapsed >= timeout_ms) {
                TerminateProcess(pi.hProcess, 1);
                out->timed_out = QS_TRUE;
                break;
            }
        }
        /* Non-blocking peek+read for stdout */
        DWORD avail=0;
        if (PeekNamedPipe(hStdoutR,NULL,0,NULL,&avail,NULL) && avail>0) {
            DWORD toread = avail < sizeof(tmp) ? avail : sizeof(tmp);
            if (ReadFile(hStdoutR,tmp,toread,&nr,NULL) && nr>0) {
                if(olen+nr+1>=ocap){qs_size_t nc=ocap*2;char*nb=(char*)qs_arena_realloc(a,obuf,ocap,nc,1);if(nb){obuf=nb;ocap=nc;}}
                memcpy(obuf+olen,tmp,nr);olen+=nr;
            }
        }
        /* Non-blocking peek+read for stderr */
        avail=0;
        if (PeekNamedPipe(hStderrR,NULL,0,NULL,&avail,NULL) && avail>0) {
            DWORD toread = avail < sizeof(tmp) ? avail : sizeof(tmp);
            if (ReadFile(hStderrR,tmp,toread,&nr,NULL) && nr>0) {
                if(elen+nr+1>=ecap){qs_size_t nc=ecap*2;char*nb=(char*)qs_arena_realloc(a,ebuf,ecap,nc,1);if(nb){ebuf=nb;ecap=nc;}}
                memcpy(ebuf+elen,tmp,nr);elen+=nr;
            }
        }
        /* Check if process finished */
        if (WaitForSingleObject(pi.hProcess, 1) == WAIT_OBJECT_0) {
            /* Drain remaining output */
            avail=0;
            while(PeekNamedPipe(hStdoutR,NULL,0,NULL,&avail,NULL)&&avail>0){
                DWORD toread=avail<sizeof(tmp)?avail:sizeof(tmp);
                if(!ReadFile(hStdoutR,tmp,toread,&nr,NULL)||!nr) break;
                if(olen+nr+1>=ocap){qs_size_t nc=ocap*2;char*nb=(char*)qs_arena_realloc(a,obuf,ocap,nc,1);if(nb){obuf=nb;ocap=nc;}}
                memcpy(obuf+olen,tmp,nr);olen+=nr; avail=0;
            }
            avail=0;
            while(PeekNamedPipe(hStderrR,NULL,0,NULL,&avail,NULL)&&avail>0){
                DWORD toread=avail<sizeof(tmp)?avail:sizeof(tmp);
                if(!ReadFile(hStderrR,tmp,toread,&nr,NULL)||!nr) break;
                if(elen+nr+1>=ecap){qs_size_t nc=ecap*2;char*nb=(char*)qs_arena_realloc(a,ebuf,ecap,nc,1);if(nb){ebuf=nb;ecap=nc;}}
                memcpy(ebuf+elen,tmp,nr);elen+=nr; avail=0;
            }
            break;
        }
    }
    if(obuf) obuf[olen]='\0';
    if(ebuf) ebuf[elen]='\0';
    CloseHandle(hStdoutR); CloseHandle(hStderrR);
    DWORD ec=0; GetExitCodeProcess(pi.hProcess,&ec);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    out->exit_code=(qs_i64)ec;
    out->stdout_text=obuf?obuf:(char*)"";
    out->stderr_text=ebuf?ebuf:(char*)"";

#else /* POSIX */
    int pout[2],perr[2];
    if (pipe(pout)<0||pipe(perr)<0) return QS_ERROR_PROCESS;

    pid_t pid=fork();
    if (pid<0) { close(pout[0]);close(pout[1]);close(perr[0]);close(perr[1]); return QS_ERROR_PROCESS; }

    if (pid==0) {
        /* child */
        dup2(pout[1],STDOUT_FILENO); close(pout[0]); close(pout[1]);
        dup2(perr[1],STDERR_FILENO); close(perr[0]); close(perr[1]);
        execvp(argv[0],(char *const*)argv);
        _exit(127);
    }
    /* parent */
    close(pout[1]); close(perr[1]);
    out->stdout_text=read_fd(a,pout[0]);
    out->stderr_text=read_fd(a,perr[0]);
    close(pout[0]); close(perr[0]);
    int status=0; waitpid(pid,&status,0);
    out->exit_code=WIFEXITED(status)?(qs_i64)WEXITSTATUS(status):-1;
#endif

    out->wall_time_us=now_us()-t0;
    out->timed_out=QS_FALSE;
    return QS_OK;
}

void qs_proc_print_argv(const char *const *argv) {
    fprintf(stderr,"[cmd]");
    for (int i=0;argv[i];i++) {
        int needs_quote=argv[i][0]=='\0'||strchr(argv[i],' ')||strchr(argv[i],'\t');
        if (needs_quote) fprintf(stderr," \"%s\"",argv[i]);
        else             fprintf(stderr," %s",argv[i]);
    }
    fprintf(stderr,"\n");
}

char *qs_proc_find_in_path(qs_arena_t *a, const char *exe_name) {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD r=SearchPathA(NULL,exe_name,".exe",MAX_PATH,buf,NULL);
    return r>0?qs_arena_strdup(a,buf):NULL;
#else
    const char *PATH=getenv("PATH");
    if (!PATH) return NULL;
    char *copy=qs_arena_strdup(a,PATH);
    if (!copy) return NULL;
    char *tok=strtok(copy,":");
    while (tok) {
        /* build candidate path */
        qs_size_t tl=strlen(tok), el=strlen(exe_name);
        char *cand=(char*)qs_arena_alloc(a,tl+1+el+1,1);
        if (!cand) return NULL;
        memcpy(cand,tok,tl); cand[tl]='/'; memcpy(cand+tl+1,exe_name,el); cand[tl+1+el]='\0';
        if (access(cand,X_OK)==0) return cand;
        tok=strtok(NULL,":");
    }
    return NULL;
#endif
}
