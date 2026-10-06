/* winlist: list top-level windows on the Wine desktop, or bring one forward.
 *
 *   wine winlist.exe [-d DESKTOP]            list: handle, pid, visible/enabled, rect, class, title
 *   wine winlist.exe [-d DESKTOP] -t         the same, tab-separated, for programs (see README.md)
 *   wine winlist.exe [-d DESKTOP] raise HWND restore (if minimized) and bring to the front
 *   wine winlist.exe [-d DESKTOP] fit W H    make the desktop's screen WxH, after the X
 *                                            screen has been resized to that
 *
 * DESKTOP defaults to "webapp", the Wine desktop wine-webapp-run starts the app on.
 *
 * The Wine virtual desktop has no taskbar, so a window that ends up behind a
 * maximized main window is otherwise unreachable. The vnc front end's window
 * list (docs/frontends.md) runs this per request; it runs fit when the browser
 * window has been resized. Build: see README.md. */
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

/* After a screen size change: a maximized window keeps its old size (Windows
 * itself re-maximizes them, Wine doesn't), so stretch it to the new screen,
 * keeping its maximized state and its restore position. A window left partly
 * off the screen comes back to it, top-left first. */
static BOOL CALLBACK refit(HWND hwnd, LPARAM size)
{
    int w = LOWORD(size), h = HIWORD(size);
    RECT r;

    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;
    GetWindowRect(hwnd, &r);
    if (IsZoomed(hwnd))
    {
        /* maximized: the screen grown by the frame on every side (no taskbar) */
        SetWindowPos(hwnd, NULL, r.left, r.top, w - 2 * r.left, h - 2 * r.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        printf("%08lx maximized to %dx%d\n", (unsigned long)(ULONG_PTR)hwnd, w, h);
    }
    else if (r.right > w || r.bottom > h)
    {
        int x = r.right > w ? max(0, w - (r.right - r.left)) : r.left;
        int y = r.bottom > h ? max(0, h - (r.bottom - r.top)) : r.top;
        if (x != r.left || y != r.top)
        {
            SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            printf("%08lx moved to %d,%d\n", (unsigned long)(ULONG_PTR)hwnd, x, y);
        }
    }
    return TRUE;
}

/* The desktop accepts a size only up to the X screen's (and the size of the
 * desktop window it was started with), so a grown browser window needs the X
 * screen to have caught up first: retry for a few seconds. */
static int fit(HDESK desk, int w, int h)
{
    DEVMODEA dm;
    LONG ret = DISP_CHANGE_FAILED;
    int i;

    for (i = 0; i < 30; i++)
    {
        if (GetSystemMetrics(SM_CXSCREEN) == w && GetSystemMetrics(SM_CYSCREEN) == h) ret = DISP_CHANGE_SUCCESSFUL;
        else
        {
            memset(&dm, 0, sizeof(dm));
            dm.dmSize = sizeof(dm);
            dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
            dm.dmPelsWidth = w;
            dm.dmPelsHeight = h;
            ret = ChangeDisplaySettingsExA(NULL, &dm, NULL, 0, NULL);
        }
        if (ret == DISP_CHANGE_SUCCESSFUL) break;
        Sleep(100);
    }
    if (ret != DISP_CHANGE_SUCCESSFUL)
    {
        fprintf(stderr, "cannot set the screen to %dx%d (error %ld)\n", w, h, ret);
        return 3;
    }
    printf("screen %dx%d\n", w, h);
    EnumDesktopWindows(desk, refit, MAKELPARAM(w, h));
    return 0;
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
    if (argc == 4 && !strcmp(argv[1], "fit"))
    {
        int w = atoi(argv[2]), h = atoi(argv[3]);
        if (w < 320 || h < 200 || w > 16384 || h > 16384)
        {
            fprintf(stderr, "bad size %sx%s\n", argv[2], argv[3]);
            return 2;
        }
        return fit(desk, w, h);
    }
    EnumDesktopWindows(desk, show, tabs);  /* top to bottom in z-order */
    return 0;
}
