/* winlist: list top-level windows on the Wine desktop, or bring one forward.
 *
 *   wine winlist.exe [-d DESKTOP]            list: handle, pid, visible/enabled, rect, class, title
 *   wine winlist.exe [-d DESKTOP] -t         the same, tab-separated, for programs (see README.md)
 *   wine winlist.exe [-d DESKTOP] raise HWND restore (if minimized) and bring to the front
 *
 * DESKTOP defaults to "webapp", the Wine desktop wine-webapp-run starts the app on.
 *
 * The Wine virtual desktop has no taskbar, so a window that ends up behind a
 * maximized main window is otherwise unreachable. The vnc front end's window
 * list (docs/frontends.md) runs this per request. Build: see README.md. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

/* Tabs and line breaks in a title would break the tab-separated format. */
static void clean(char *s)
{
    for (; *s; s++) if (*s == '\t' || *s == '\r' || *s == '\n') *s = ' ';
}

static BOOL CALLBACK show(HWND hwnd, LPARAM tabs)
{
    char title[256], cls[128];
    RECT r;
    DWORD pid = 0;

    GetWindowTextA(hwnd, title, sizeof(title));
    GetClassNameA(hwnd, cls, sizeof(cls));
    GetWindowRect(hwnd, &r);
    GetWindowThreadProcessId(hwnd, &pid);
    if (!IsWindowVisible(hwnd) && !title[0]) return TRUE;  /* skip hidden helper windows */
    if (tabs)
    {
        LONG ex = GetWindowLongA(hwnd, GWL_EXSTYLE);

        clean(title);
        clean(cls);
        /* flags: V visible, E enabled, I minimized, Z maximized, T tool window, P topmost */
        printf("%08lx\t%08lx\t%lu\t%s%s%s%s%s%s\t%ld\t%ld\t%ld\t%ld\t%s\t%s\n",
               (unsigned long)(ULONG_PTR)hwnd,
               (unsigned long)(ULONG_PTR)GetWindow(hwnd, GW_OWNER), (unsigned long)pid,
               IsWindowVisible(hwnd) ? "V" : "", IsWindowEnabled(hwnd) ? "E" : "",
               IsIconic(hwnd) ? "I" : "", IsZoomed(hwnd) ? "Z" : "",
               (ex & WS_EX_TOOLWINDOW) ? "T" : "", (ex & WS_EX_TOPMOST) ? "P" : "",
               r.left, r.top, r.right - r.left, r.bottom - r.top, cls, title);
        return TRUE;
    }
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
    LPARAM tabs = 0;

    if (argc >= 3 && !strcmp(argv[1], "-d")) { name = argv[2]; argc -= 2; argv += 2; }
    if (argc >= 2 && !strcmp(argv[1], "-t")) { tabs = 1; argc--; argv++; }
    desk = OpenDesktopA(name, 0, FALSE, GENERIC_ALL);
    if (!desk || !SetThreadDesktop(desk))
    {
        fprintf(stderr, "cannot open desktop \"%s\" (error %lu)\n", name, GetLastError());
        return 1;
    }

    if (argc == 3 && !strcmp(argv[1], "raise"))
    {
        HWND hwnd = (HWND)(ULONG_PTR)strtoul(argv[2], NULL, 16);
        if (!IsWindow(hwnd))
        {
            fprintf(stderr, "no window %s\n", argv[2]);
            return 2;
        }
        if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
        SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(hwnd);
        return 0;
    }
    EnumDesktopWindows(desk, show, tabs);  /* top to bottom in z-order */
    return 0;
}
