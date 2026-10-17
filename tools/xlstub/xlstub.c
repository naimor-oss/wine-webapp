/* xlstub: a stand-in for Excel's automation interface (Excel.Application),
 * enough for programs that fill a sheet cell by cell and then hand it to the
 * user. It keeps the values and number formats in memory; when the program
 * shows the workbook (Visible = True), saves, closes or quits, it writes them
 * to a staging file that the gateway turns into an .xlsx download
 * (docs/frontends.md, "Excel output"). Anything it doesn't know is accepted
 * and ignored, and logged once per name to the staging folder's xlstub.log,
 * so a program using more of Excel keeps running and shows what it used.
 *
 * Understood: Application.{Visible, Workbooks, ActiveWorkbook, ActiveSheet,
 * Worksheets, Sheets, Cells, Range, ActiveCell, Selection, Quit, Version},
 * Workbooks.Add, Workbook.{Worksheets, Sheets, ActiveSheet, Close, Save,
 * SaveAs}, Worksheet.{Cells, Range, Name}, and on a cell: Select, Activate,
 * Value, Value2, Formula, FormulaR1C1, Text, NumberFormat, Offset, Cells,
 * Row, Column.
 *
 * Staging file, one cell per line: ROW TAB COL TAB TYPE TAB FORMAT TAB VALUE,
 * TYPE n (number), s (text), b (TRUE/FALSE), d (date as an Excel serial);
 * backslash, tab, CR and LF in FORMAT and VALUE written as \\ \t \r \n.
 * Folder: WEBAPP_EXPORT_DIR, else %USERPROFILE%\.exports (the gateway's). */
#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <oleauto.h>
#include <stdio.h>
#include <string.h>

static const CLSID CLSID_Excel = {0x00024500, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};

/* --- the sheet ------------------------------------------------------------ */
typedef struct { int row, col; VARIANT v; WCHAR *fmt; } Cell;
typedef struct {
    LONG ref;
    Cell *cells; int ncells, cap;
    int arow, acol;                 /* active cell */
    BOOL visible;
} App;

static Cell *cell_at(App *a, int r, int c, BOOL create)
{
    int i;
    for (i = a->ncells - 1; i >= 0; i--)
        if (a->cells[i].row == r && a->cells[i].col == c) return &a->cells[i];
    if (!create) return NULL;
    if (a->ncells == a->cap) {
        a->cap = a->cap ? a->cap * 2 : 256;
        a->cells = realloc(a->cells, a->cap * sizeof(Cell));
    }
    memset(&a->cells[a->ncells], 0, sizeof(Cell));
    a->cells[a->ncells].row = r; a->cells[a->ncells].col = c;
    VariantInit(&a->cells[a->ncells].v);
    return &a->cells[a->ncells++];
}

static void sheet_clear(App *a)
{
    int i;
    for (i = 0; i < a->ncells; i++) { VariantClear(&a->cells[i].v); free(a->cells[i].fmt); }
    a->ncells = 0;
}

static void export_dir(WCHAR *out, DWORD len)
{
    if (GetEnvironmentVariableW(L"WEBAPP_EXPORT_DIR", out, len)) return;
    if (!GetEnvironmentVariableW(L"USERPROFILE", out, len)) lstrcpyW(out, L"C:\\");
    lstrcatW(out, L"\\.exports");
}

static void put_escaped(FILE *f, const WCHAR *s)
{
    char buf[8192]; int n, i;
    if (!s) return;
    n = WideCharToMultiByte(CP_UTF8, 0, s, -1, buf, sizeof buf, NULL, NULL);
    for (i = 0; i < n - 1; i++) {
        switch (buf[i]) {
        case '\\': fputs("\\\\", f); break;
        case '\t': fputs("\\t", f); break;
        case '\r': fputs("\\r", f); break;
        case '\n': fputs("\\n", f); break;
        default: fputc(buf[i], f);
        }
    }
}

static void note_unknown(const char *what)
{
    static char seen[64][64]; static int nseen;
    WCHAR dir[MAX_PATH], path[MAX_PATH]; FILE *f; int i;
    for (i = 0; i < nseen; i++) if (!strcmp(seen[i], what)) return;
    if (nseen < 64) lstrcpynA(seen[nseen++], what, 64);
    export_dir(dir, MAX_PATH);
    CreateDirectoryW(dir, NULL);
    wsprintfW(path, L"%s\\xlstub.log", dir);
    if ((f = _wfopen(path, L"a"))) { fprintf(f, "ignored: %s\n", what); fclose(f); }
}

/* Write the sheet for the gateway and start a new one. */
static void sheet_flush(App *a)
{
    WCHAR dir[MAX_PATH], tmp[MAX_PATH], path[MAX_PATH];
    FILE *f; int i;
    if (!a->ncells) return;
    export_dir(dir, MAX_PATH);
    CreateDirectoryW(dir, NULL);
    wsprintfW(path, L"%s\\%lu-%lu.cells", dir, GetTickCount(), GetCurrentProcessId());
    wsprintfW(tmp, L"%s.part", path);
    if (!(f = _wfopen(tmp, L"wb"))) return;
    for (i = 0; i < a->ncells; i++) {
        Cell *c = &a->cells[i]; VARIANT s; char type = 's';
        VariantInit(&s);
        switch (V_VT(&c->v)) {
        case VT_EMPTY: case VT_NULL: continue;
        case VT_I1: case VT_I2: case VT_I4: case VT_I8: case VT_INT: case VT_UI1: case VT_UI2:
        case VT_UI4: case VT_UI8: case VT_UINT: case VT_R4: case VT_R8: case VT_CY: case VT_DECIMAL:
            type = 'n'; break;
        case VT_BOOL: type = 'b'; break;
        case VT_DATE: type = 'd'; break;
        }
        if (type == 'd') { V_VT(&s) = VT_R8; V_R8(&s) = V_DATE(&c->v); type = 'n'; }
        else VariantCopy(&s, &c->v);
        if (FAILED(VariantChangeTypeEx(&s, &s, MAKELCID(0x0409, SORT_DEFAULT), 0, VT_BSTR))) continue;
        fprintf(f, "%d\t%d\t%c\t", c->row, c->col, type);
        put_escaped(f, c->fmt);
        fputc('\t', f);
        put_escaped(f, V_BSTR(&s));
        fputc('\n', f);
        VariantClear(&s);
    }
    fclose(f);
    MoveFileW(tmp, path);
    sheet_clear(a);
}

/* --- the objects ------------------------------------------------------------ */
enum kind { K_APP, K_WORKBOOKS, K_WORKBOOK, K_SHEET, K_CELL, K_STUB };
typedef struct { IDispatch disp; LONG ref; enum kind kind; int row, col; App *app; } Node;
static IDispatchVtbl vtbl;

static IDispatch *node(App *a, enum kind k, int r, int c)
{
    Node *n = calloc(1, sizeof *n);
    n->disp.lpVtbl = &vtbl; n->ref = 1; n->kind = k; n->row = r; n->col = c; n->app = a;
    InterlockedIncrement(&a->ref);
    return &n->disp;
}

static const WCHAR *NAMES[] = {
    L"Application", L"Parent", L"Visible", L"Workbooks", L"ActiveWorkbook", L"ActiveSheet",
    L"Worksheets", L"Sheets", L"Cells", L"Range", L"ActiveCell", L"Selection", L"Quit", L"Version",
    L"Add", L"Open", L"Item", L"Count", L"Close", L"Save", L"SaveAs", L"Name", L"Select", L"Activate",
    L"Value", L"Value2", L"Formula", L"FormulaR1C1", L"Text", L"NumberFormat", L"Offset", L"Row",
    L"Column", L"ScreenUpdating", L"DisplayAlerts", L"Interactive", L"Columns", L"Rows", L"AutoFit",
};
enum { N_APPLICATION = 1, N_PARENT, N_VISIBLE, N_WORKBOOKS, N_ACTIVEWORKBOOK, N_ACTIVESHEET,
       N_WORKSHEETS, N_SHEETS, N_CELLS, N_RANGE, N_ACTIVECELL, N_SELECTION, N_QUIT, N_VERSION,
       N_ADD, N_OPEN, N_ITEM, N_COUNT, N_CLOSE, N_SAVE, N_SAVEAS, N_NAME, N_SELECT, N_ACTIVATE,
       N_VALUE, N_VALUE2, N_FORMULA, N_FORMULAR1C1, N_TEXT, N_NUMBERFORMAT, N_OFFSET, N_ROW,
       N_COLUMN, N_SCREENUPDATING, N_DISPLAYALERTS, N_INTERACTIVE, N_COLUMNS, N_ROWS, N_AUTOFIT,
       N_KNOWN = N_AUTOFIT };
static char unknown_names[256][64];
static int nunknown;

static HRESULT WINAPI qi(IDispatch *d, REFIID iid, void **out)
{
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IDispatch)) {
        *out = d; IDispatch_AddRef(d); return S_OK;
    }
    *out = NULL; return E_NOINTERFACE;
}
static ULONG WINAPI addref(IDispatch *d) { return InterlockedIncrement(&((Node *)d)->ref); }
static ULONG WINAPI release(IDispatch *d)
{
    Node *n = (Node *)d; LONG r = InterlockedDecrement(&n->ref);
    if (!r) {
        App *a = n->app;
        free(n);
        /* The program let go of Excel without showing it: still deliver. */
        if (!InterlockedDecrement(&a->ref)) { sheet_flush(a); sheet_clear(a); free(a->cells); free(a); }
    }
    return r;
}
static HRESULT WINAPI tic(IDispatch *d, UINT *n) { *n = 0; return S_OK; }
static HRESULT WINAPI ti(IDispatch *d, UINT i, LCID l, ITypeInfo **t) { *t = NULL; return E_NOTIMPL; }

static HRESULT WINAPI ids(IDispatch *d, REFIID iid, LPOLESTR *names, UINT c, LCID l, DISPID *id)
{
    UINT i; int k;
    for (i = 0; i < c; i++) {
        id[i] = DISPID_UNKNOWN;
        if (i) continue;
        for (k = 0; k < N_KNOWN; k++)
            if (!lstrcmpiW(names[0], NAMES[k])) { id[0] = k + 1; break; }
        if (id[0] == DISPID_UNKNOWN) {
            char a[64];
            WideCharToMultiByte(CP_ACP, 0, names[0], -1, a, sizeof a, NULL, NULL);
            for (k = 0; k < nunknown && lstrcmpiA(unknown_names[k], a); k++) ;
            if (k == nunknown && nunknown < 256) lstrcpynA(unknown_names[nunknown++], a, 64);
            id[0] = 1000 + k;
        }
    }
    return S_OK;
}

/* Argument i counted from the first (DISPPARAMS holds them last first),
 * skipping named ones; converted to a VT_I4 or VT_BSTR as asked. */
static BOOL arg(DISPPARAMS *p, UINT i, VARTYPE vt, VARIANT *out)
{
    UINT pos = p->cArgs - p->cNamedArgs;
    VariantInit(out);
    if (i >= pos) return FALSE;
    return SUCCEEDED(VariantChangeType(out, &p->rgvarg[pos - 1 - i], 0, vt));
}

static BOOL parse_a1(const WCHAR *s, int *r, int *c)
{
    int col = 0, row = 0;
    while (*s == '$') s++;
    while ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z'))
        col = col * 26 + ((*s++ | 0x20) - 'a' + 1);
    while (*s == '$') s++;
    while (*s >= '0' && *s <= '9') row = row * 10 + (*s++ - '0');
    if (!col || !row) return FALSE;
    *r = row; *c = col;
    return TRUE;
}

static void ret_obj(VARIANT *res, IDispatch *o)
{
    if (res) { V_VT(res) = VT_DISPATCH; V_DISPATCH(res) = o; } else IDispatch_Release(o);
}

static HRESULT WINAPI inv(IDispatch *d, DISPID id, REFIID iid, LCID lcid, WORD flags, DISPPARAMS *p,
                          VARIANT *res, EXCEPINFO *e, UINT *argerr)
{
    Node *n = (Node *)d; App *a = n->app;
    BOOL put = flags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF);
    VARIANT v1, v2, *newval = put && p->cArgs ? &p->rgvarg[0] : NULL;
    if (res) VariantInit(res);

    if (n->kind == K_STUB) { if (!put) ret_obj(res, node(a, K_STUB, 0, 0)); return S_OK; }

    switch (id) {
    case N_APPLICATION: case N_PARENT:
        ret_obj(res, node(a, K_APP, 0, 0)); return S_OK;
    case N_VISIBLE:
        if (put) {
            VariantInit(&v1);
            if (SUCCEEDED(VariantChangeType(&v1, newval, 0, VT_BOOL))) {
                a->visible = V_BOOL(&v1) != VARIANT_FALSE;
                if (a->visible) sheet_flush(a);  /* the program hands the sheet to the user */
            }
        } else if (res) { V_VT(res) = VT_BOOL; V_BOOL(res) = a->visible ? VARIANT_TRUE : VARIANT_FALSE; }
        return S_OK;
    case N_VERSION:
        if (res) { V_VT(res) = VT_BSTR; V_BSTR(res) = SysAllocString(L"11.0"); }
        return S_OK;
    case N_NAME:
        if (!put && res) { V_VT(res) = VT_BSTR; V_BSTR(res) = SysAllocString(n->kind == K_APP ? L"Microsoft Excel" : L"Sheet1"); }
        return S_OK;
    case N_COUNT:
        if (res) { V_VT(res) = VT_I4; V_I4(res) = 1; }
        return S_OK;
    case N_QUIT: case N_CLOSE: case N_SAVE: case N_SAVEAS:
        sheet_flush(a); return S_OK;
    case N_SCREENUPDATING: case N_DISPLAYALERTS: case N_INTERACTIVE: case N_AUTOFIT:
        return S_OK;
    case N_WORKBOOKS:
        ret_obj(res, node(a, K_WORKBOOKS, 0, 0)); return S_OK;
    case N_ADD: case N_OPEN: case N_ITEM: case N_ACTIVEWORKBOOK:
        if (id == N_ADD || id == N_OPEN) sheet_flush(a);   /* a new workbook: deliver the last */
        ret_obj(res, node(a, n->kind == K_WORKBOOKS || id == N_ACTIVEWORKBOOK ? K_WORKBOOK : K_SHEET, 0, 0));
        return S_OK;
    case N_ACTIVESHEET: case N_WORKSHEETS: case N_SHEETS:
        ret_obj(res, node(a, K_SHEET, 0, 0)); return S_OK;
    case N_COLUMNS: case N_ROWS:
        ret_obj(res, node(a, K_STUB, 0, 0)); return S_OK;
    case N_ACTIVECELL: case N_SELECTION:
        ret_obj(res, node(a, K_CELL, a->arow ? a->arow : 1, a->acol ? a->acol : 1)); return S_OK;
    case N_CELLS: case N_OFFSET:
        if (arg(p, 0, VT_I4, &v1) && arg(p, 1, VT_I4, &v2)) {
            int r = V_I4(&v1), c = V_I4(&v2);
            if (n->kind == K_CELL && id == N_CELLS) { r += n->row - 1; c += n->col - 1; }
            if (id == N_OFFSET) { r += n->row; c += n->col; }
            ret_obj(res, node(a, K_CELL, r, c));
        } else ret_obj(res, node(a, K_STUB, 0, 0));
        return S_OK;
    case N_RANGE:
        if (arg(p, 0, VT_BSTR, &v1)) {
            int r, c;
            if (parse_a1(V_BSTR(&v1), &r, &c)) ret_obj(res, node(a, K_CELL, r, c));
            else ret_obj(res, node(a, K_STUB, 0, 0));
            VariantClear(&v1);
        } else ret_obj(res, node(a, K_STUB, 0, 0));
        return S_OK;
    case N_SELECT: case N_ACTIVATE:
        if (n->kind == K_CELL) { a->arow = n->row; a->acol = n->col; }
        return S_OK;
    case N_ROW: case N_COLUMN:
        if (res) { V_VT(res) = VT_I4; V_I4(res) = id == N_ROW ? n->row : n->col; }
        return S_OK;
    case N_VALUE: case N_VALUE2: case N_FORMULA: case N_FORMULAR1C1: case N_TEXT:
        if (n->kind != K_CELL) return S_OK;
        if (put) {
            Cell *c = cell_at(a, n->row, n->col, TRUE);
            VariantClear(&c->v);
            VariantCopyInd(&c->v, newval);
        } else if (res) {
            Cell *c = cell_at(a, n->row, n->col, FALSE);
            if (c) VariantCopy(res, &c->v);
        }
        return S_OK;
    case N_NUMBERFORMAT:
        if (n->kind != K_CELL) return S_OK;
        if (put) {
            VariantInit(&v1);
            if (SUCCEEDED(VariantChangeType(&v1, newval, 0, VT_BSTR))) {
                Cell *c = cell_at(a, n->row, n->col, TRUE);
                free(c->fmt);
                c->fmt = _wcsdup(V_BSTR(&v1));
                VariantClear(&v1);
            }
        } else if (res) { V_VT(res) = VT_BSTR; V_BSTR(res) = SysAllocString(L"General"); }
        return S_OK;
    }
    if (id >= 1000 && id - 1000 < nunknown) note_unknown(unknown_names[id - 1000]);
    if (!put) ret_obj(res, node(a, K_STUB, 0, 0));
    return S_OK;
}

static IDispatchVtbl vtbl = { qi, addref, release, tic, ti, ids, inv };

/* --- class factory ------------------------------------------------------------ */
static HRESULT WINAPI cf_qi(IClassFactory *f, REFIID iid, void **out)
{
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IClassFactory)) { *out = f; return S_OK; }
    *out = NULL; return E_NOINTERFACE;
}
static ULONG WINAPI cf_ref(IClassFactory *f) { return 2; }
static HRESULT WINAPI cf_create(IClassFactory *f, IUnknown *outer, REFIID iid, void **out)
{
    App *a = calloc(1, sizeof *a); IDispatch *d; HRESULT hr;
    if (outer) return CLASS_E_NOAGGREGATION;
    d = node(a, K_APP, 0, 0);
    hr = IDispatch_QueryInterface(d, iid, out);
    IDispatch_Release(d);
    return hr;
}
static HRESULT WINAPI cf_lock(IClassFactory *f, BOOL l) { return S_OK; }
static IClassFactoryVtbl cfv = { cf_qi, cf_ref, cf_ref, cf_create, cf_lock };
static IClassFactory factory = { &cfv };

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **out)
{
    if (!IsEqualCLSID(clsid, &CLSID_Excel)) return CLASS_E_CLASSNOTAVAILABLE;
    return cf_qi(&factory, iid, out);
}
HRESULT WINAPI DllCanUnloadNow(void) { return S_FALSE; }
