/* winlist: list top-level windows on the Wine desktop, or bring one forward.
 *
 *   wine winlist.exe [-d DESKTOP]            list: handle, pid, visible/enabled, rect, class, title
 *   wine winlist.exe [-d DESKTOP] raise HWND restore (if minimized) and bring to the front
 *
 * DESKTOP defaults to "webapp", the Wine desktop wine-webapp-run starts the app on.
 *
 * For diagnosing windows that ended up hidden behind others: the Wine virtual
 * desktop has no taskbar to find them with. Build: see README.md. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static BOOL CALLBACK show(HWND hwnd, LPARAM unused)
{
    char title[256], cls[128];
    RECT r;
    DWORD pid = 0;

    (void)unused;
    GetWindowTextA(hwnd, title, sizeof(title));
    GetClassNameA(hwnd, cls, sizeof(cls));
    GetWindowRect(hwnd, &r);
    GetWindowThreadProcessId(hwnd, &pid);
    if (!IsWindowVisible(hwnd) && !title[0]) return TRUE;  /* skip hidden helper windows */
    printf("%08lx pid=%-5lu %s%s (%ld,%ld)-(%ld,%ld) [%s] \"%s\"\n",
           (unsigned long)(ULONG_PTR)hwnd, (unsigned long)pid,
           IsWindowVisible(hwnd) ? "V" : "-", IsWindowEnabled(hwnd) ? "E" : "-",
           r.left, r.top, r.right, r.bottom, cls, title);
    return TRUE;
}

int main(int argc, char **argv)
{
    const char *name = "webapp";
    HDESK desk;

    if (argc >= 3 && !strcmp(argv[1], "-d")) { name = argv[2]; argc -= 2; argv += 2; }
    desk = OpenDesktopA(name, 0, FALSE, GENERIC_ALL);
    if (!desk || !SetThreadDesktop(desk))
    {
        fprintf(stderr, "cannot open desktop \"%s\" (error %lu)\n", name, GetLastError());
        return 1;
    }

    if (argc == 3 && !strcmp(argv[1], "raise"))
    {
        HWND hwnd = (HWND)(ULONG_PTR)strtoul(argv[2], NULL, 16);
        if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
        SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(hwnd);
        return 0;
    }
    EnumDesktopWindows(desk, show, 0);  /* top to bottom in z-order */
    return 0;
}
