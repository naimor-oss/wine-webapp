/* locktest <file> <offsetHex> <holdSeconds>
   Tries LockFileEx(exclusive, fail-immediately) on 1 byte at offset; if it
   succeeds, holds the lock for holdSeconds. Prints LOCKED or BUSY. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    if (argc < 4) { printf("usage\n"); return 2; }
    unsigned long long off = strtoull(argv[2], NULL, 16);
    int hold = atoi(argv[3]);
    HANDLE h = CreateFileA(argv[1], GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("OPENFAIL %lu\n", GetLastError()); return 3; }
    OVERLAPPED ov = {0}; ov.Offset = (DWORD)off; ov.OffsetHigh = (DWORD)(off >> 32);
    if (LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov)) {
        printf("LOCKED %s @%llx\n", argv[1], off); fflush(stdout);
        Sleep(hold * 1000);
        UnlockFileEx(h, 0, 1, 0, &ov);
    } else printf("BUSY %s @%llx err=%lu\n", argv[1], off, GetLastError());
    CloseHandle(h); return 0;
}
