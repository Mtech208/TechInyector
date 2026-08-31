#include <windows.h>
#include <commdlg.h>
#include <vector>
#include <string>

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
constexpr UINT ID_LABEL_MADE      = 0x3F0; // 1008 - pie de pagina "Made by Mtech08"

// Colores
constexpr COLORREF TEXT_COLOR     = 0x00932693; // morado
constexpr COLORREF BG_COLOR       = 0x00000000; // negro

} // namespace

// Una entrada de la lista de procesos: PID + titulo de la ventana.
struct ProcEntry {
    DWORD        pid;
    std::wstring title;
};

// Variables globales
std::vector<ProcEntry> g_procs;      // rellenada por EnumWindows
HWND   g_hMain     = nullptr;
HWND   g_listMain  = nullptr;
HWND   g_listSmall = nullptr;
HWND   g_editDll   = nullptr;
HFONT  g_hFont     = nullptr;
HBRUSH g_hBgBrush  = nullptr;        // pincel de fondo negro

// ---------------------------------------------------------------------------
// Procedimiento de ventana
// ---------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

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

    procs->push_back(ProcEntry{ pid, std::wstring(title) });
    return TRUE;
}

// ---------------------------------------------------------------------------
// Inyecta una DLL en un proceso remoto:
//   OpenProcess -> VirtualAllocEx -> WriteProcessMemory(ruta) ->
//   WriteProcessMemory(L"\0") -> GetModuleHandleW("kernel32.dll") ->
//   GetProcAddress("LoadLibraryW") -> CreateRemoteThread ->
//   WaitForSingleObject -> VirtualFreeEx + CloseHandle.
// Devuelve true si tiene exito.
// ---------------------------------------------------------------------------
bool InjectDll(DWORD pid, const char* dllPath)
{
    // Convierte la ruta multibyte que llega como argumento a UTF-16.
    int wlen = MultiByteToWideChar(CP_ACP, 0, dllPath, -1, nullptr, 0);
    std::wstring wpath;
    if (wlen > 0) {
        wpath.resize(static_cast<size_t>(wlen) - 1);
        MultiByteToWideChar(CP_ACP, 0, dllPath, -1, &wpath[0], wlen);
    }

    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) {
        ShowErrorMessage(L"OpenProcess failed");
        return false;
    }

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

    // Terminador nulo (2 bytes) justo despues del texto.
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
// Procedimiento de ventana
// ---------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_hMain = hWnd;

        g_hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, L"Consolas");
        g_hBgBrush = CreateSolidBrush(BG_COLOR);
        HINSTANCE hInst = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hWnd, GWLP_HINSTANCE));

        // Lista de procesos (ID 1001) en (10,10) tamano 250x250.
        g_listMain = CreateWindowExW(
            0, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_EXTENDEDSEL,
            10, 10, 250, 250, hWnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_LISTBOX_MAIN)), hInst, nullptr);
        if (!g_listMain) ShowErrorMessage(L"CreateWindowExW (ListBox) failed");
        if (g_listMain) SendMessageW(g_listMain, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // Cuadro de ruta de la DLL (ID 1002) en (270,10) tamano 250x25.
        g_editDll = CreateWindowExW(
            0, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            270, 10, 250, 25, hWnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_EDIT_DLLPATH)), hInst, nullptr);
        if (!g_editDll) ShowErrorMessage(L"CreateWindowExW (Edit) failed");
        if (g_editDll) SendMessageW(g_editDll, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // Boton "Browse" (ID 1003) en (530,10) tamano 80x25.
        CreateWindowExW(
            0, L"BUTTON", L"Browse",
            WS_CHILD | WS_VISIBLE,
            530, 10, 80, 25, hWnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_BTN_BROWSE)), hInst, nullptr);

        // Boton "Inject" (ID 1004) en (270,45) tamano 100x25.
        CreateWindowExW(
            0, L"BUTTON", L"Inject",
            WS_CHILD | WS_VISIBLE,
            270, 45, 100, 25, hWnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_BTN_INJECT)), hInst, nullptr);

        // Boton "Refresh" (ID 1006) en (380,45) tamano 100x25.
        CreateWindowExW(
            0, L"BUTTON", L"Refresh",
            WS_CHILD | WS_VISIBLE,
            380, 45, 100, 25, hWnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_BTN_REFRESH)), hInst, nullptr);

        // Lista secundaria (ID 1005), sin contenido visible.
        g_listSmall = CreateWindowExW(
            0, L"LISTBOX", nullptr,
            WS_CHILD | WS_BORDER,
            470, 290, 150, 20, hWnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_LISTBOX_STATUS)), hInst, nullptr);

        // Pie de pagina "Made by Mtech08" abajo a la derecha.
        HWND hMadeLabel = CreateWindowExW(
            0, L"STATIC", L"Made by Mtech08",
            WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | SS_RIGHT,
            460, 285, 170, 20, hWnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_LABEL_MADE)), hInst, nullptr);
        if (hMadeLabel) SendMessageW(hMadeLabel, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        return 0;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX:
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, TEXT_COLOR);
        SetBkColor(hdc, BG_COLOR);

        UINT id = GetDlgCtrlID(reinterpret_cast<HWND>(lParam));
        if (id == ID_LISTBOX_MAIN) {
            SetBkMode(hdc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
        }
        // Demas controles (cuadro de texto, etiquetas, botones):
        // texto morado sobre fondo negro.
        SetBkMode(hdc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(g_hBgBrush ? g_hBgBrush : GetStockObject(BLACK_BRUSH));
    }

    case WM_COMMAND:
    {
        WORD id = LOWORD(wParam);
        WORD code = HIWORD(wParam);

        // Fuerza seleccion unica en la lista de procesos: al marcar uno,
        // deselecciona el resto y solo deja marcado el elegido.
        if (id == ID_LISTBOX_MAIN && code == LBN_SELCHANGE) {
            const int count = static_cast<int>(SendMessageW(g_listMain, LB_GETCOUNT, 0, 0));
            int selCount = static_cast<int>(SendMessageW(g_listMain, LB_GETSELCOUNT, 0, 0));
            if (selCount > 0 && selCount < count) {
                int caret = static_cast<int>(SendMessageW(g_listMain, LB_GETCARETINDEX, 0, 0));
                if (caret != LB_ERR && caret < count) {
                    for (int i = 0; i < count; i++)
                        SendMessageW(g_listMain, LB_SETSEL, i == caret ? TRUE : FALSE, i);
                }
            }
            return 0;
        }

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
            LRESULT sel = SendMessageW(g_listMain, LB_GETCARETINDEX, 0, 0);
            if (sel == LB_ERR || sel < 0) {
                MessageBoxW(hWnd, L"Please select an application.",
                            L"Warning", MB_ICONWARNING);
                return 0;
            }
            DWORD pid = static_cast<DWORD>(SendMessageW(g_listMain, LB_GETITEMDATA, sel, 0));
            if (pid == 0) {
                MessageBoxW(hWnd, L"Please select an application.",
                            L"Warning", MB_ICONWARNING);
                return 0;
            }

            wchar_t dllPath[0x104];
            GetWindowTextW(g_editDll, dllPath, 0x104);
            if (dllPath[0] == L'\0') {
                MessageBoxW(hWnd, L"Please select a DLL file.",
                            L"Warning", MB_ICONWARNING);
                return 0;
            }

            // Convierte de wide a narrow e inyecta.
            char bufN[0x104];
            WideCharToMultiByte(CP_ACP, 0, dllPath, -1, bufN, 0x104, nullptr, nullptr);
            InjectDll(pid, bufN);
            return 0;
        }

        case ID_BTN_REFRESH:
        {
            SendMessageW(g_listMain, LB_RESETCONTENT, 0, 0);
            g_procs.clear();
            EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&g_procs));

            for (std::size_t i = 0; i < g_procs.size(); i++) {
                LRESULT idx = SendMessageW(g_listMain, LB_ADDSTRING, 0,
                                           reinterpret_cast<LPARAM>(g_procs[i].title.c_str()));
                SendMessageW(g_listMain, LB_SETITEMDATA, idx, g_procs[i].pid);
            }
            return 0;
        }
        }
        break;
    }

    case WM_DESTROY:
        if (g_hBgBrush) { DeleteObject(g_hBgBrush); g_hBgBrush = nullptr; }
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
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    wc.hCursor       = LoadCursorW(nullptr, MAKEINTRESOURCEW(IDC_ARROW));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(CreateSolidBrush(BG_COLOR));
    wc.lpszClassName = L"SimpleDLLInjectorClass";

    if (!RegisterClassExW(&wc)) {
        ShowErrorMessage(L"RegisterClassExW failed");
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        0, L"SimpleDLLInjectorClass", L"TechInyect",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 650, 350,
        nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) {
        ShowErrorMessage(L"CreateWindowExW failed");
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG m{};
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    return static_cast<int>(m.wParam);
}
