/* lockmatrix: one side of a two-process Windows file-sharing test.
 *
 * Run as two Windows processes ("A" holds, "B" tries) to check whether the
 * file semantics a file-share database relies on hold between them: share
 * modes, byte-range lock conflicts, mandatory locking of reads and writes,
 * and coherence of written data. lockmatrix.sh runs the cases between
 * processes in different places (containers, a network mount, one wineserver).
 *
 *   lockmatrix hold-open PATH SHARE SECONDS     SHARE: none|read|write|rw
 *   lockmatrix try-open  PATH ACCESS SHARE      ACCESS: r|w|rw
 *   lockmatrix hold-lock PATH OFFSET LEN SECONDS
 *   lockmatrix try-lock  PATH OFFSET LEN
 *   lockmatrix try-read  PATH OFFSET LEN
 *   lockmatrix try-write PATH OFFSET TEXT
 *   lockmatrix read      PATH OFFSET LEN        prints the bytes as text
 *
 * "hold-*" prints READY once it holds, then sleeps. "try-*" prints one word:
 * OK, SHARING_VIOLATION, LOCK_VIOLATION or ERROR_<code>. Exit status 0.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static DWORD share_of(const char *s)
{
    if (!strcmp(s, "none")) return 0;
    if (!strcmp(s, "read")) return FILE_SHARE_READ;
    if (!strcmp(s, "write")) return FILE_SHARE_WRITE;
    return FILE_SHARE_READ | FILE_SHARE_WRITE;
}

static DWORD access_of(const char *s)
{
    if (!strcmp(s, "r")) return GENERIC_READ;
    if (!strcmp(s, "w")) return GENERIC_WRITE;
    return GENERIC_READ | GENERIC_WRITE;
}

static void result(DWORD err)
{
    if (err == ERROR_SUCCESS) puts("OK");
    else if (err == ERROR_SHARING_VIOLATION) puts("SHARING_VIOLATION");
    else if (err == ERROR_LOCK_VIOLATION) puts("LOCK_VIOLATION");
    else printf("ERROR_%lu\n", (unsigned long)err);
    fflush(stdout);
}

/* Shared opens, as a database engine opens its data files. */
static HANDLE open_shared(const char *path)
{
    return CreateFileA(path, GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, NULL);
}

static void seek(HANDLE h, unsigned long long off)
{
    LONG hi = (LONG)(off >> 32);
    SetFilePointer(h, (LONG)(off & 0xffffffffu), &hi, FILE_BEGIN);
}

static void ready_and_sleep(int seconds)
{
    puts("READY");
    fflush(stdout);
    Sleep((DWORD)seconds * 1000);
}

int main(int argc, char **argv)
{
    const char *cmd = argc > 1 ? argv[1] : "";
    HANDLE h;
    unsigned long long off;
    DWORD len, n;

    if (!strcmp(cmd, "hold-open") && argc == 5) {
        h = CreateFileA(argv[2], GENERIC_READ | GENERIC_WRITE, share_of(argv[3]),
                        NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) { result(GetLastError()); return 0; }
        ready_and_sleep(atoi(argv[4]));
        return 0;
    }
    if (!strcmp(cmd, "try-open") && argc == 5) {
        h = CreateFileA(argv[2], access_of(argv[3]), share_of(argv[4]), NULL,
                        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        result(h == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS);
        return 0;
    }
    if (argc < 5) {
        fputs("usage: see the comment at the top of lockmatrix.c\n", stderr);
        return 2;
    }
    h = open_shared(argv[2]);
    if (h == INVALID_HANDLE_VALUE) { result(GetLastError()); return 0; }
    off = _strtoui64(argv[3], NULL, 0);

    if (!strcmp(cmd, "hold-lock") && argc == 6) {
        len = (DWORD)strtoul(argv[4], NULL, 0);
        if (!LockFile(h, (DWORD)(off & 0xffffffffu), (DWORD)(off >> 32), len, 0)) {
            result(GetLastError());
            return 0;
        }
        ready_and_sleep(atoi(argv[5]));
        return 0;
    }
    if (!strcmp(cmd, "try-lock")) {
        len = (DWORD)strtoul(argv[4], NULL, 0);
        if (LockFile(h, (DWORD)(off & 0xffffffffu), (DWORD)(off >> 32), len, 0)) {
            result(ERROR_SUCCESS);
            UnlockFile(h, (DWORD)(off & 0xffffffffu), (DWORD)(off >> 32), len, 0);
        } else {
            result(GetLastError());
        }
        return 0;
    }
    if (!strcmp(cmd, "try-read") || !strcmp(cmd, "read")) {
        char buf[4096];
        len = (DWORD)strtoul(argv[4], NULL, 0);
        if (len > sizeof buf - 1) len = sizeof buf - 1;
        seek(h, off);
        if (!ReadFile(h, buf, len, &n, NULL)) { result(GetLastError()); return 0; }
        if (!strcmp(cmd, "read")) { buf[n] = 0; printf("%s\n", buf); }
        else result(ERROR_SUCCESS);
        return 0;
    }
    if (!strcmp(cmd, "try-write")) {
        seek(h, off);
        if (!WriteFile(h, argv[4], (DWORD)strlen(argv[4]), &n, NULL)) {
            result(GetLastError());
            return 0;
        }
        FlushFileBuffers(h);
        result(ERROR_SUCCESS);
        return 0;
    }
    fputs("usage: see the comment at the top of lockmatrix.c\n", stderr);
    return 2;
}
