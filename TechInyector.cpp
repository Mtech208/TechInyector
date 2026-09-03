#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <psapi.h>
#include <vector>
#include <string>
#include <cmath>
#include <gdiplus.h>
using namespace Gdiplus;
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "comctl32.lib")

// ---------------------------------------------------------------------------
// Constantes privadas
// ---------------------------------------------------------------------------
namespace {

// IDs de los controles de la ventana
constexpr UINT ID_LISTBOX_MAIN    = 0x3E9; // 1001 - lista de procesos
constexpr UINT ID_EDIT_DLLPATH    = 0x3EA; // 1002 - cuadro de ruta de la DLL
constexpr UINT ID_BTN_BROWSE      = 0x3EB; // 1003 - boton "Browse"
constexpr UINT ID_BTN_INJECT      = 0x3EC; // 1004 - boton "Inject"
constexpr UINT ID_LISTBOX_STATUS  = 0x3ED; // 1005 - lista secundaria (estado)
constexpr UINT ID_BTN_REFRESH     = 0x3EE; // 1006 - boton "Refresh"
constexpr UINT ID_LABEL_MADE      = 0x3F0; // 1008 - pie de pagina autor
constexpr UINT ID_LABEL_TITLE     = 0x3F1; // 1009 - titulo del panel
constexpr UINT ID_LABEL_PROCS     = 0x3F2; // 1010 - etiqueta "PROCESOS"
constexpr UINT ID_LABEL_DLL       = 0x3F3; // 1011 - etiqueta "DLL"
constexpr UINT ID_LABEL_STATUS    = 0x3F4; // 1012 - etiqueta "ESTADO"
constexpr UINT ID_ICON_TITLE      = 0x3F5; // 1013 - icono del titulo
constexpr UINT ID_IMAGE_LOGO      = 0x3F6; // 1014 - imagen logo

// Paleta estilo "Gengar / liquid crystal" (purpura elegante + violeta refinado)
constexpr COLORREF COL_BG_TOP     = RGB(30, 10, 61);   // purpura oscuro elegante
constexpr COLORREF COL_BG_BOTTOM  = RGB(13, 3, 26);    // casi negro
constexpr COLORREF COL_PANEL      = RGB(45, 25, 85);   // panel purpura
constexpr COLORREF COL_PANEL_HI   = RGB(60, 40, 115);  // panel hover
constexpr COLORREF COL_EDGE       = RGB(106, 79, 211); // borde violeta
constexpr COLORREF COL_EDGE_HI    = RGB(135, 100, 230); // borde violeta hover
constexpr COLORREF COL_TEXT_BG    = RGB(25, 10, 40);   // fondo de cajas de texto
constexpr COLORREF COL_TEXT       = RGB(224, 195, 254); // texto violeta claro
constexpr COLORREF COL_TEXT_TITLE = RGB(240, 210, 255); // titulo brillante
constexpr COLORREF COL_ACCENT     = RGB(115, 80, 200); // acento purpura
constexpr COLORREF COL_NEON       = RGB(180, 140, 240); // etiquetas de seccion
constexpr COLORREF COL_LINE       = RGB(90, 60, 160);   // linea divisoria

// Estado de hover de los botones (para el efecto cristal)
LRESULT g_hoverBrowse  = 0;
LRESULT g_hoverInject  = 0;
LRESULT g_hoverRefresh = 0;

} // namespace

// Una entrada de la lista de procesos: PID + titulo de la ventana.
struct ProcEntry {
    DWORD        pid;
    std::wstring exeName;
    std::wstring title;
};

// Variables globales
std::vector<ProcEntry> g_procs;      // rellenada por EnumWindows
HWND   g_hMain     = nullptr;
HWND   g_listMain  = nullptr;
HWND   g_listHeader = nullptr;
HWND   g_listSmall = nullptr;
HWND   g_editDll   = nullptr;
HFONT  g_hFont     = nullptr;
HFONT  g_hTitleFont = nullptr;
HFONT  g_hSectionFont = nullptr;
HBRUSH g_hBgBrush  = nullptr;        // pincel de fondo principal
HBRUSH g_hFieldBrush = nullptr;      // pincel de fondo de los campos

// ---------------------------------------------------------------------------
// Procedimiento de ventana
// ---------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ---------------------------------------------------------------------------
// Pinta un gradiente vertical de color en un rectangulo.
// ---------------------------------------------------------------------------
void PaintGradient(HDC hdc, const RECT& rc, COLORREF top, COLORREF bottom)
{
    int h = rc.bottom - rc.top;
    for (int y = 0; y < h; y++) {
        double t = h > 1 ? (double)y / (h - 1) : 0.0;
        int r = (int)(GetRValue(top) + (GetRValue(bottom) - GetRValue(top)) * t);
        int g = (int)(GetGValue(top) + (GetGValue(bottom) - GetGValue(top)) * t);
        int b = (int)(GetBValue(top) + (GetBValue(bottom) - GetBValue(top)) * t);
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(r, g, b));
        HPEN old = (HPEN)SelectObject(hdc, pen);
        MoveToEx(hdc, rc.left, rc.top + y, nullptr);
        LineTo(hdc, rc.right, rc.top + y);
        SelectObject(hdc, old);
        DeleteObject(pen);
    }
}

// ---------------------------------------------------------------------------
// Dibuja un rectangulo redondeado relleno.
// ---------------------------------------------------------------------------
void FillRoundRect(HDC hdc, int x, int y, int w, int h, int cx, int cy, COLORREF color)
{
    HBRUSH br = CreateSolidBrush(color);
    HBRUSH old = (HBRUSH)SelectObject(hdc, br);
    RoundRect(hdc, x, y, x + w, y + h, cx, cy);
    SelectObject(hdc, old);
    DeleteObject(br);
}

// ---------------------------------------------------------------------------
// Dibuja el borde redondeado de un control.
// ---------------------------------------------------------------------------
void FrameRoundRect(HDC hdc, int x, int y, int w, int h, int cx, int cy, COLORREF edge)
{
    HPEN pen = CreatePen(PS_SOLID, 1, edge);
    HPEN old = (HPEN)SelectObject(hdc, pen);
    HBRUSH nullbr = (HBRUSH)GetStockObject(NULL_BRUSH);
    HBRUSH oldbr = (HBRUSH)SelectObject(hdc, nullbr);
    RoundRect(hdc, x, y, x + w, y + h, cx, cy);
    SelectObject(hdc, old);
    SelectObject(hdc, oldbr);
    DeleteObject(pen);
}

// ---------------------------------------------------------------------------
// Dibuja una linea horizontal de acento degradada (para separar secciones).
// ---------------------------------------------------------------------------
void DrawAccentLine(HDC hdc, int x, int y, int w)
{
    HPEN pen = CreatePen(PS_SOLID, 1, COL_LINE);
    HPEN old = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, x, y, nullptr);
    LineTo(hdc, x + w, y);
    SelectObject(hdc, old);
    DeleteObject(pen);
}

// ---------------------------------------------------------------------------
// Dibuja un boton con efecto "cristal liquido" (relleno gradiente + borde sutil).
// ---------------------------------------------------------------------------
void DrawCrystalButton(HDC hdc, const RECT& rc, const wchar_t* text, bool hover)
{
    // relleno del boton: gradiente suave
    RECT rr = rc;
    PaintGradient(hdc, rr, hover ? COL_PANEL_HI : COL_PANEL,
                  hover ? COL_PANEL : COL_BG_BOTTOM);
    // borde cristal sutil
    FrameRoundRect(hdc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                   6, 6, hover ? COL_EDGE_HI : COL_EDGE);
    // texto centrado
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, hover ? RGB(255, 255, 255) : COL_TEXT_TITLE);
    RECT tr = rc;
    DrawTextW(hdc, text, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// ---------------------------------------------------------------------------
// Subclase de botones: rastrea el hover para el efecto cristal.
// ---------------------------------------------------------------------------
LRESULT CALLBACK CrystalButtonProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto orig = (WNDPROC)GetPropW(hwnd, L"ORIGPROC");

    switch (msg)
    {
    case WM_MOUSEMOVE:
    {
        LRESULT res = CallWindowProcW(orig, hwnd, msg, wParam, lParam);
        LRESULT* flag = (LRESULT*)GetPropW(hwnd, L"HOVERFLAG");
        if (flag && !*flag) {
            *flag = 1;
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        TRACKMOUSEEVENT tme{};
        tme.cbSize    = sizeof(tme);
        tme.dwFlags   = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);
        return res;
    }
    case WM_MOUSELEAVE:
    {
        LRESULT* flag = (LRESULT*)GetPropW(hwnd, L"HOVERFLAG");
        if (flag && *flag) {
            *flag = 0;
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    }
    case WM_NCDESTROY:
        RemovePropW(hwnd, L"ORIGPROC");
        RemovePropW(hwnd, L"HOVERFLAG");
        break;
    }
    return CallWindowProcW(orig, hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Subclase para el control de imagen PNG (dibuja con GDI+).
// ---------------------------------------------------------------------------
LRESULT CALLBACK ImageControlProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto orig = (WNDPROC)GetPropW(hwnd, L"ORIGPROC");

    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        Image* pImg = (Image*)GetPropW(hwnd, L"GDIIMAGE");
        if (pImg && pImg->GetLastStatus() == Ok) {
            Graphics g(hdc);
            g.DrawImage(pImg, 0, 0, ps.rcPaint.right, ps.rcPaint.bottom);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_NCDESTROY:
        {
            Image* pImg = (Image*)GetPropW(hwnd, L"GDIIMAGE");
            if (pImg) delete pImg;
            RemovePropW(hwnd, L"ORIGPROC");
            RemovePropW(hwnd, L"GDIIMAGE");
        }
        break;
    }
    return CallWindowProcW(orig, hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Subclase del header del ListView: fondo negro solido + texto blanco.
// Garantiza que la cabecera (Proceso/Exe/PID) NUNCA se pinte blanca.
// ---------------------------------------------------------------------------
LRESULT CALLBACK HeaderProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto orig = (WNDPROC)GetPropW(hwnd, L"ORIGPROC");

    switch (msg)
    {
    case WM_ERASEBKGND:
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        return 1;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

        int count = static_cast<int>(SendMessageW(hwnd, HDM_GETITEMCOUNT, 0, 0));
        HFONT hFont = static_cast<HFONT>(GetPropW(hwnd, L"HFONT"));
        HFONT old = static_cast<HFONT>(SelectObject(hdc, hFont ? hFont : GetStockObject(DEFAULT_GUI_FONT)));
        SetTextColor(hdc, RGB(255, 255, 255));
        SetBkMode(hdc, TRANSPARENT);

        for (int i = 0; i < count; i++) {
            RECT ir;
            SendMessageW(hwnd, HDM_GETITEMRECT, i, reinterpret_cast<LPARAM>(&ir));
            wchar_t buf[128];
            HDITEMW hdi{};
            hdi.mask     = HDI_TEXT;
            hdi.pszText  = buf;
            hdi.cchTextMax = 128;
            if (SendMessageW(hwnd, HDM_GETITEMW, i, reinterpret_cast<LPARAM>(&hdi))) {
                RECT tr = ir;
                tr.left += 6;
                tr.right -= 6;
                DrawTextW(hdc, buf, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
        }

        SelectObject(hdc, old);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_NCDESTROY:
        RemovePropW(hwnd, L"ORIGPROC");
        RemovePropW(hwnd, L"HFONT");
        break;
    }
    return CallWindowProcW(orig, hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Aplica la subclase de hover a un boton.
// ---------------------------------------------------------------------------
void MakeCrystalButton(HWND hwnd, LRESULT* hoverFlag)
{
    if (!hwnd) return;
    SetPropW(hwnd, L"HOVERFLAG", (HANDLE)hoverFlag);
    WNDPROC orig = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                                              (LONG_PTR)CrystalButtonProc);
    SetPropW(hwnd, L"ORIGPROC", (HANDLE)orig);
}

// ---------------------------------------------------------------------------
// Crea un control STATIC para mostrar un PNG con GDI+.
// ---------------------------------------------------------------------------
HWND CreatePngStatic(HWND hParent, HINSTANCE hInst, int x, int y, int w, int h, UINT id, const wchar_t* path)
{
    HWND hCtrl = CreateWindowExW(
        0, L"STATIC", nullptr,
        WS_CHILD | WS_VISIBLE,
        x, y, w, h, hParent,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), hInst, nullptr);

    if (hCtrl) {
        Image* pImg = Image::FromFile(path);
        if (pImg && pImg->GetLastStatus() == Ok) {
            SetPropW(hCtrl, L"GDIIMAGE", (HANDLE)pImg);
            WNDPROC orig = (WNDPROC)SetWindowLongPtrW(hCtrl, GWLP_WNDPROC,
                                                      (LONG_PTR)ImageControlProc);
            SetPropW(hCtrl, L"ORIGPROC", (HANDLE)orig);
        }
    }
    return hCtrl;
}

// ---------------------------------------------------------------------------
// Muestra un error de Win32 en un MessageBox (GetLastError + FormatMessageW).
// ---------------------------------------------------------------------------
void ShowErrorMessage(LPCWSTR context)
{
    DWORD err = GetLastError();
    LPWSTR buf = nullptr;

    DWORD n = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        nullptr, err, 0,
        reinterpret_cast<LPWSTR>(&buf), 0, nullptr);

    std::wstring message = context;
    if (n && buf) {
        message += L" (";
        message += std::wstring(buf, n);
        message += L")";
    }
    if (buf) LocalFree(buf);

    MessageBoxW(nullptr, message.c_str(), L"Error", MB_ICONERROR);
}

// ---------------------------------------------------------------------------
// Callback de EnumWindows: recolecta las ventanas visibles de nivel superior.
// ---------------------------------------------------------------------------
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
    auto* procs = reinterpret_cast<std::vector<ProcEntry>*>(lParam);

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    if (!pid || !IsWindowVisible(hwnd))
        return TRUE;

    int len = GetWindowTextLengthW(hwnd);
    if (len <= 0)
        return TRUE;

    wchar_t title[0x104];
    GetWindowTextW(hwnd, title, 0x104);

    std::wstring exeName;
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProc) {
        wchar_t exePath[MAX_PATH];
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(hProc, 0, exePath, &size)) {
            wchar_t* slash = wcsrchr(exePath, L'\\');
            exeName = slash ? slash + 1 : exePath;
        }
        CloseHandle(hProc);
    }

    procs->push_back(ProcEntry{ pid, exeName, std::wstring(title) });
    return TRUE;
}

// ---------------------------------------------------------------------------
// Inyecta una DLL en un proceso remoto
// ---------------------------------------------------------------------------
bool InjectDll(DWORD pid, const char* dllPath)
{
    int wlen = MultiByteToWideChar(CP_ACP, 0, dllPath, -1, nullptr, 0);
    std::wstring wpath;
    if (wlen > 0) {
        wpath.resize(static_cast<size_t>(wlen) - 1);
        MultiByteToWideChar(CP_ACP, 0, dllPath, -1, &wpath[0], wlen);
    }

    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) { ShowErrorMessage(L"OpenProcess failed"); return false; }

    size_t bytes = wpath.size() * sizeof(wchar_t);
    LPVOID remote = VirtualAllocEx(hProcess, nullptr, bytes + 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        ShowErrorMessage(L"VirtualAllocEx failed");
        CloseHandle(hProcess);
        return false;
    }

    SIZE_T written = 0;
    if (!WriteProcessMemory(hProcess, remote, wpath.c_str(), bytes, &written)) {
        ShowErrorMessage(L"WriteProcessMemory failed");
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    wchar_t nul = L'\0';
    WriteProcessMemory(hProcess, (char*)remote + bytes, &nul, sizeof(nul), nullptr);

    HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
    if (!hKernel) {
        ShowErrorMessage(L"GetModuleHandleW failed");
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    FARPROC loadLib = GetProcAddress(hKernel, "LoadLibraryW");
    if (!loadLib) {
        ShowErrorMessage(L"GetProcAddress failed for LoadLibraryW");
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0,
                                        reinterpret_cast<LPTHREAD_START_ROUTINE>(loadLib),
                                        remote, 0, nullptr);
    if (!hThread) {
        ShowErrorMessage(L"CreateRemoteThread failed");
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    return true;
}

// ---------------------------------------------------------------------------
// Enumera de nuevo las ventanas y refresca el ListView de procesos.
// ---------------------------------------------------------------------------
void RefreshProcessList()
{
    if (!g_listMain) return;

    SendMessageW(g_listMain, LVM_DELETEALLITEMS, 0, 0);
    g_procs.clear();
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&g_procs));

    for (std::size_t i = 0; i < g_procs.size(); i++) {
        LVITEMW it{};
        it.mask    = LVIF_TEXT | LVIF_PARAM;
        it.iItem   = static_cast<int>(i);
        it.iSubItem = 0;
        it.pszText = const_cast<LPWSTR>(g_procs[i].title.c_str());
        it.lParam  = g_procs[i].pid;
        LRESULT idx = SendMessageW(g_listMain, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&it));

        if (!g_procs[i].exeName.empty()) {
            it.mask = LVIF_TEXT;
            it.iSubItem = 1;
            it.pszText = const_cast<LPWSTR>(g_procs[i].exeName.c_str());
            SendMessageW(g_listMain, LVM_SETITEMW, 0, reinterpret_cast<LPARAM>(&it));
        }

        std::wstring pidStr = std::to_wstring(g_procs[i].pid);
        it.mask = LVIF_TEXT;
        it.iSubItem = 2;
        it.pszText = const_cast<LPWSTR>(pidStr.c_str());
        SendMessageW(g_listMain, LVM_SETITEMW, 0, reinterpret_cast<LPARAM>(&it));
        (void)idx;
    }
}

// ---------------------------------------------------------------------------
// Procedimiento de ventana
// ---------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_hMain = hWnd;

        g_hFont = CreateFontW(15, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        g_hTitleFont = CreateFontW(22, 0, 0, 0, FW_BOLD, 0, 0, 0,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                   CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                   DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        g_hSectionFont = CreateFontW(13, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                     CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                     DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        g_hBgBrush = CreateSolidBrush(COL_BG_TOP);
        g_hFieldBrush = CreateSolidBrush(COL_TEXT_BG);
        HINSTANCE hInst = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hWnd, GWLP_HINSTANCE));

        // Icono del titulo (un poco a la izquierda)
        HWND hIconTitle = CreateWindowExW(
            0, L"STATIC", nullptr,
            WS_CHILD | WS_VISIBLE | SS_ICON,
            25, 15, 24, 24, hWnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_ICON_TITLE)), hInst, nullptr);
        if (hIconTitle) {
            HICON hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
            if (hIcon) {
                SendMessageW(hIconTitle, STM_SETICON, (WPARAM)hIcon, 0);
            }
        }

        // Titulo de la app (mas a la derecha)
        HWND hTitle = CreateWindowExW(
            0, L"STATIC", L"TechInyector",
            WS_CHILD | WS_VISIBLE,
            65, 17, 615, 30, hWnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_LABEL_TITLE)), hInst, nullptr);
        if (hTitle) SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_hTitleFont, TRUE);

        // Seccion PROCESOS
        HWND hProcLabel = CreateWindowExW(
            0, L"STATIC", L"PROCESOS",
            WS_CHILD | WS_VISIBLE,
            20, 55, 390, 18, hWnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_LABEL_PROCS)), hInst, nullptr);
        if (hProcLabel) SendMessageW(hProcLabel, WM_SETFONT, (WPARAM)g_hSectionFont, TRUE);

        // Lista de procesos (ListView con columnas)
        g_listMain = CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr,
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            20, 78, 390, 190, hWnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_LISTBOX_MAIN)), hInst, nullptr);
        if (!g_listMain) ShowErrorMessage(L"CreateWindowExW (ListView) failed");

        if (g_listMain) {
            SendMessageW(g_listMain, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            SendMessageW(g_listMain, LVM_SETBKCOLOR, 0, (LPARAM)RGB(0, 0, 0));
            SendMessageW(g_listMain, LVM_SETTEXTBKCOLOR, 0, (LPARAM)RGB(0, 0, 0));
            SendMessageW(g_listMain, LVM_SETTEXTCOLOR, 0, (LPARAM)RGB(255, 255, 255));
            g_listHeader = reinterpret_cast<HWND>(SendMessageW(g_listMain, LVM_GETHEADER, 0, 0));
            if (g_listHeader) {
                SetPropW(g_listHeader, L"HFONT", (HANDLE)g_hFont);
                WNDPROC origHdr = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
                    g_listHeader, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(HeaderProc)));
                SetPropW(g_listHeader, L"ORIGPROC", (HANDLE)origHdr);
                InvalidateRect(g_listHeader, nullptr, TRUE);
            }
            LVCOLUMNW col{};
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT;
            col.fmt  = LVCFMT_LEFT;

            col.cx = 200; col.iSubItem = 0; col.pszText = const_cast<LPWSTR>(L"Proceso");
            SendMessageW(g_listMain, LVM_INSERTCOLUMNW, 0, reinterpret_cast<LPARAM>(&col));
            col.cx = 130; col.iSubItem = 1; col.pszText = const_cast<LPWSTR>(L"Exe");
            SendMessageW(g_listMain, LVM_INSERTCOLUMNW, 1, reinterpret_cast<LPARAM>(&col));
            col.cx = 60;  col.iSubItem = 2; col.pszText = const_cast<LPWSTR>(L"PID");
            SendMessageW(g_listMain, LVM_INSERTCOLUMNW, 2, reinterpret_cast<LPARAM>(&col));
        }

        // Seccion DLL
        HWND hDllLabel = CreateWindowExW(
            0, L"STATIC", L"DLL A INYECTAR",
            WS_CHILD | WS_VISIBLE,
            425, 55, 260, 18, hWnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_LABEL_DLL)), hInst, nullptr);
        if (hDllLabel) SendMessageW(hDllLabel, WM_SETFONT, (WPARAM)g_hSectionFont, TRUE);

        // Cuadro de ruta de la DLL
        g_editDll = CreateWindowExW(
            0, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            425, 78, 260, 28, hWnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_EDIT_DLLPATH)), hInst, nullptr);
        if (!g_editDll) ShowErrorMessage(L"CreateWindowExW (Edit) failed");
        if (g_editDll) SendMessageW(g_editDll, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // Botones Browse / Inject en la misma fila
        HWND hBrowse = CreateWindowExW(
            0, L"BUTTON", L"Browse DLL",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            425, 115, 120, 32, hWnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_BTN_BROWSE)), hInst, nullptr);
        MakeCrystalButton(hBrowse, &g_hoverBrowse);

        HWND hInject = CreateWindowExW(
            0, L"BUTTON", L"Inject",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            550, 115, 135, 32, hWnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_BTN_INJECT)), hInst, nullptr);
        MakeCrystalButton(hInject, &g_hoverInject);

        // Boton Refresh debajo
        HWND hRefresh = CreateWindowExW(
            0, L"BUTTON", L"Refresh",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            425, 155, 260, 32, hWnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_BTN_REFRESH)), hInst, nullptr);
        MakeCrystalButton(hRefresh, &g_hoverRefresh);

        // Imagen logo (centrada debajo de Refresh)
        CreatePngStatic(hWnd, hInst, 498, 200, 114, 100, ID_IMAGE_LOGO, L"assets/image.png");

        // Seccion ESTADO
        HWND hStatusLabel = CreateWindowExW(
            0, L"STATIC", L"ESTADO",
            WS_CHILD | WS_VISIBLE,
            20, 285, 390, 18, hWnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_LABEL_STATUS)), hInst, nullptr);
        if (hStatusLabel) SendMessageW(hStatusLabel, WM_SETFONT, (WPARAM)g_hSectionFont, TRUE);

        // Lista de estado
        g_listSmall = CreateWindowExW(
            0, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | LBS_NOINTEGRALHEIGHT,
            20, 305, 660, 40, hWnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_LISTBOX_STATUS)), hInst, nullptr);

        // Pie de pagina
        HWND hMade = CreateWindowExW(
            0, L"STATIC", L"TechInyector  |  Made by Mtech08",
            WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
            20, 355, 660, 18, hWnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_LABEL_MADE)), hInst, nullptr);
        if (hMade) SendMessageW(hMade, WM_SETFONT, (WPARAM)g_hSectionFont, TRUE);

        // Cargar los procesos automaticamente al abrir
        RefreshProcessList();

        return 0;
    }

    case WM_ERASEBKGND:
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        RECT rc;
        GetClientRect(hWnd, &rc);
        PaintGradient(hdc, rc, COL_BG_TOP, COL_BG_BOTTOM);
        DrawAccentLine(hdc, 20, 48, 660);
        return 1;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        UINT id = GetDlgCtrlID(reinterpret_cast<HWND>(lParam));
        if (id == ID_LABEL_TITLE) {
            SetTextColor(hdc, COL_TEXT_TITLE);
        } else if (id == ID_LABEL_PROCS || id == ID_LABEL_DLL || id == ID_LABEL_STATUS) {
            SetTextColor(hdc, COL_NEON);
        } else if (id == ID_LABEL_MADE) {
            SetTextColor(hdc, RGB(150, 120, 190));
        } else {
            SetTextColor(hdc, COL_TEXT);
        }
        SetBkMode(hdc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(g_hBgBrush);
    }
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLOREDIT:
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, COL_TEXT);
        SetBkColor(hdc, COL_TEXT_BG);
        SetBkMode(hdc, OPAQUE);
        return reinterpret_cast<LRESULT>(g_hFieldBrush);
    }

    case WM_NOTIFY:
    {
        LPNMHDR nm = reinterpret_cast<LPNMHDR>(lParam);
        if (nm->code == NM_CUSTOMDRAW) {
            LPNMLVCUSTOMDRAW cd = reinterpret_cast<LPNMLVCUSTOMDRAW>(lParam);
            if (nm->hwndFrom == g_listMain) {
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) {
                    return CDRF_NOTIFYITEMDRAW;
                } else if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    cd->clrTextBk = COL_TEXT_BG;
                    cd->clrText   = COL_TEXT;
                    if ((cd->nmcd.uItemState & CDIS_SELECTED)) {
                        cd->clrTextBk = COL_PANEL_HI;
                        cd->clrText   = RGB(255, 255, 255);
                    }
                    return CDRF_NEWFONT;
                }
            }
        }
        break;
    }

    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (dis->CtlType == ODT_BUTTON) {
            bool hover = false;
            if (dis->hwndItem == GetDlgItem(hWnd, ID_BTN_BROWSE))      hover = g_hoverBrowse;
            else if (dis->hwndItem == GetDlgItem(hWnd, ID_BTN_INJECT)) hover = g_hoverInject;
            else if (dis->hwndItem == GetDlgItem(hWnd, ID_BTN_REFRESH)) hover = g_hoverRefresh;

            wchar_t label[64];
            GetWindowTextW(dis->hwndItem, label, 64);
            DrawCrystalButton(dis->hDC, dis->rcItem, label, hover);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND:
    {
        WORD id = LOWORD(wParam);
        WORD code = HIWORD(wParam);

        switch (id)
        {
        case ID_BTN_BROWSE:
        {
            OPENFILENAMEW ofn{};
            wchar_t fileBuf[0x104] = L"";
            ofn.lStructSize       = sizeof(ofn);
            ofn.hwndOwner         = hWnd;
            ofn.lpstrFilter       = L"DLL Files (*.dll)\0*.dll\0All Files (*.*)\0*.*\0\0";
            ofn.lpstrFile         = fileBuf;
            ofn.nMaxFile          = 0x104;
            ofn.Flags             = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) {
                SetWindowTextW(g_editDll, ofn.lpstrFile);
            }
            return 0;
        }

        case ID_BTN_INJECT:
        {
            int sel = static_cast<int>(SendMessageW(g_listMain, LVM_GETNEXTITEM, -1, LVNI_SELECTED));
            if (sel == -1) {
                MessageBoxW(hWnd, L"Selecciona una aplicacion.", L"Aviso", MB_ICONWARNING);
                return 0;
            }
            LVITEMW it{};
            it.mask = LVIF_PARAM;
            it.iItem = sel;
            it.iSubItem = 0;
            if (SendMessageW(g_listMain, LVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&it))) {
                DWORD pid = static_cast<DWORD>(it.lParam);
                if (pid == 0) {
                    MessageBoxW(hWnd, L"Selecciona una aplicacion.", L"Aviso", MB_ICONWARNING);
                    return 0;
                }

                wchar_t dllPath[0x104];
                GetWindowTextW(g_editDll, dllPath, 0x104);
                if (dllPath[0] == L'\0') {
                    MessageBoxW(hWnd, L"Selecciona un archivo DLL.", L"Aviso", MB_ICONWARNING);
                    return 0;
                }

                char bufN[0x104];
                WideCharToMultiByte(CP_ACP, 0, dllPath, -1, bufN, 0x104, nullptr, nullptr);
                if (InjectDll(pid, bufN)) {
                    SendMessageW(g_listSmall, LB_ADDSTRING, 0, (LPARAM)L"Inyeccion realizada con exito.");
                } else {
                    SendMessageW(g_listSmall, LB_ADDSTRING, 0, (LPARAM)L"La inyeccion fallo. Revisa permisos y ruta.");
                }
            }
            return 0;
        }

        case ID_BTN_REFRESH:
        {
            RefreshProcessList();
            return 0;
        }
        }
        break;
    }

    case WM_DESTROY:
        if (g_hBgBrush) { DeleteObject(g_hBgBrush); g_hBgBrush = nullptr; }
        if (g_hFieldBrush) { DeleteObject(g_hFieldBrush); g_hFieldBrush = nullptr; }
        if (g_hFont) { DeleteObject(g_hFont); g_hFont = nullptr; }
        if (g_hTitleFont) { DeleteObject(g_hTitleFont); g_hTitleFont = nullptr; }
        if (g_hSectionFont) { DeleteObject(g_hSectionFont); g_hSectionFont = nullptr; }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// WinMain: registra la clase, crea la ventana y ejecuta el bucle de mensajes.
// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    // Inicializar controles comunes (ListView, etc.)
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icc);

    // Inicializar GDI+
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    wc.hCursor       = LoadCursorW(nullptr, MAKEINTRESOURCEW(IDC_ARROW));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(CreateSolidBrush(COL_BG_TOP));
    wc.lpszClassName = L"SimpleDLLInjectorClass";

    if (!RegisterClassExW(&wc)) {
        ShowErrorMessage(L"RegisterClassExW failed");
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        0, L"SimpleDLLInjectorClass", L"TechInyector",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_DLGFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 720, 450,
        nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) {
        ShowErrorMessage(L"CreateWindowExW failed");
        return 1;
    }

    // Deshabilitar el boton de maximizar y redimensionar
    HMENU hSysMenu = GetSystemMenu(hwnd, FALSE);
    if (hSysMenu) {
        DeleteMenu(hSysMenu, SC_MAXIMIZE, MF_BYCOMMAND);
        DeleteMenu(hSysMenu, SC_SIZE, MF_BYCOMMAND);
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG m{};
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    // Desinicializar GDI+
    GdiplusShutdown(gdiplusToken);

    return static_cast<int>(m.wParam);
}
