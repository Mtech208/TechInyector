#include <windows.h>
#include <commdlg.h>
#include <vector>
#include <string>

// ---------------------------------------------------------------------------
// Private constants (recovered from disassembly)
// ---------------------------------------------------------------------------
namespace {

constexpr UINT ID_LISTBOX_MAIN    = 0x3E9; // 1001  - process list
constexpr UINT ID_EDIT_DLLPATH    = 0x3EA; // 1002  - DLL path text box
constexpr UINT ID_BTN_BROWSE      = 0x3EB; // 1003  - "Browse"
constexpr UINT ID_BTN_INJECT      = 0x3EC; // 1004  - "Inject"
constexpr UINT ID_LISTBOX_STATUS  = 0x3ED; // 1005  - secondary (status) list
constexpr UINT ID_BTN_REFRESH     = 0x3EE; // 1006  - "Refresh"
constexpr UINT ID_LABEL_MADE      = 0x3F0; // 1008  - "Made by Mtech08" footnote

constexpr COLORREF TEXT_COLOR     = 0x00932693; // morado oscuro 15% mas claro
constexpr COLORREF BG_COLOR       = 0x00000000; // negro

} // namespace (private constants)

// Struct pushed into the enum callback; one element per enumerating window.
// Element size 0x28 (40) bytes on the stack/vector: PID + std::wstring title.
struct ProcEntry {
    DWORD          pid;
    std::wstring   title;
};

// Globals (in .data of the binary).
std::vector<ProcEntry> g_procs;      // filled by EnumWindows
HWND  g_hMain     = nullptr;
HWND  g_listMain  = nullptr;
HWND  g_listSmall = nullptr;
HWND  g_editDll   = nullptr;
HFONT g_hFont     = nullptr;
HBRUSH g_hBgBrush = nullptr;         // solid black background brush

// ---------------------------------------------------------------------------
// window procedure
// ---------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ---------------------------------------------------------------------------
// Show a Win32 error as a MessageBox (from GetLastError() + FormatMessageW).
// NOTE: format differs slightly from source; reconstructed from disassembly.
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
// EnumWindows callback: collect visible top-level windows into g_procs.
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
// Inject a DLL into a remote process:
//   OpenProcess -> VirtualAllocEx -> WriteProcessMemory(path) ->
//   WriteProcessMemory(L"\0") -> GetModuleHandleW("kernel32.dll") ->
//   GetProcAddress("LoadLibraryW") -> CreateRemoteThread ->
//   WaitForSingleObject -> VirtualFreeEx + CloseHandle.
// Returns true on success.
// ---------------------------------------------------------------------------
bool InjectDll(DWORD pid, const char* dllPath)
{
    // Convert the multibyte path supplied by the caller to UTF-16.
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

    // (original wrote the wide path directly with WriteProcessMemory)
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

    // null terminator (2 bytes) right after the string
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
// Window procedure
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

        // Process list (ListBox, ID 1001) at (10,10) size 250x250
        g_listMain = CreateWindowExW(
            0, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_EXTENDEDSEL,
            10, 10, 250, 250, hWnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_LISTBOX_MAIN)), hInst, nullptr);
        if (!g_listMain) ShowErrorMessage(L"CreateWindowExW (ListBox) failed");
        if (g_listMain) SendMessageW(g_listMain, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // DLL path edit (Edit, ID 1002) at (270,10) size 250x25
        g_editDll = CreateWindowExW(
            0, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            270, 10, 250, 25, hWnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_EDIT_DLLPATH)), hInst, nullptr);
        if (!g_editDll) ShowErrorMessage(L"CreateWindowExW (Edit) failed");
        if (g_editDll) SendMessageW(g_editDll, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // Browse button (ID 1003) at (530,10) size 80x25
        CreateWindowExW(
            0, L"BUTTON", L"Browse",
            WS_CHILD | WS_VISIBLE,
            530, 10, 80, 25, hWnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_BTN_BROWSE)), hInst, nullptr);

        // Inject button (ID 1004) at (270,45) size 100x25
        CreateWindowExW(
            0, L"BUTTON", L"Inject",
            WS_CHILD | WS_VISIBLE,
            270, 45, 100, 25, hWnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_BTN_INJECT)), hInst, nullptr);

        // Refresh button (ID 1006) at (380,45) size 100x25
        CreateWindowExW(
            0, L"BUTTON", L"Refresh",
            WS_CHILD | WS_VISIBLE,
            380, 45, 100, 25, hWnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_BTN_REFRESH)), hInst, nullptr);

        // Secondary list (ID 1005) — hidden; no content to display
        g_listSmall = CreateWindowExW(
            0, L"LISTBOX", nullptr,
            WS_CHILD | WS_BORDER,
            470, 290, 150, 20, hWnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_LISTBOX_STATUS)), hInst, nullptr);

        // Footnote "Made by Mtech08" bottom-right of the window
        HWND hMadeLabel = CreateWindowExW(
            0, L"STATIC", L"Made by Mtech08",
            WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | SS_RIGHT,
            460, 285, 170, 20, hWnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_LABEL_MADE)), hInst, nullptr);
        if (hMadeLabel) SendMessageW(hMadeLabel, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // Show the private fonts already loaded for the main window
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
        // All other child controls (edit box, static labels, buttons):
        // green text on solid black background.
        SetBkMode(hdc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(g_hBgBrush ? g_hBgBrush : GetStockObject(BLACK_BRUSH));
    }

    case WM_COMMAND:
    {
        WORD id = LOWORD(wParam);
        WORD code = HIWORD(wParam);

        // Forzar selección única en el ListBox de procesos: al marcar/desmarcar
        // un proceso, deseleccionar todos los demás y quedarnos solo con uno.
        if (id == ID_LISTBOX_MAIN && code == LBN_SELCHANGE) {
            const int count = static_cast<int>(SendMessageW(g_listMain, LB_GETCOUNT, 0, 0));
            int selCount = static_cast<int>(SendMessageW(g_listMain, LB_GETSELCOUNT, 0, 0));
            if (selCount > 0 && selCount < count) {
                // get the currently selected (caret) index
                int caret = static_cast<int>(SendMessageW(g_listMain, LB_GETCARETINDEX, 0, 0));
                if (caret != LB_ERR && caret < count) {
                    // deselect everything, then re-select only the caret item
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

            // convert wide -> narrow and inject
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
// WinMain: register window class, create main window, run message loop.
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
