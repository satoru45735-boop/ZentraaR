// ZentraRPG SA-MP Launcher (32-bit, Win32 + GDI+)
// Put the built exe inside your GTA San Andreas folder (next to gta_sa.exe and samp.dll).
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <algorithm>
using std::min;
using std::max;
#include <gdiplus.h>
#include <string>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using namespace Gdiplus;

// ---- Server ----
static const wchar_t* SERVER_IP   = L"195.18.10.168";
static const wchar_t* SERVER_PORT = L"7777";

// ---- Layout (coordinates of the original 1672x941 artwork) ----
static const int WIN_W = 1000;
static const int WIN_H = 563;
static const double S  = 1000.0 / 1672.0;

struct R { int l, t, r, b; };
static const R FIELD_R = {430, 510, 1242, 620};
static const R PLAY_R  = {430, 660, 1242, 778};
static const R MIN_R   = {1495, 20, 1565, 85};
static const R CLOSE_R = {1570, 20, 1655, 85};

static RECT Sc(const R& r) {
    RECT o = { (LONG)(r.l * S), (LONG)(r.t * S), (LONG)(r.r * S), (LONG)(r.b * S) };
    return o;
}
static bool In(const R& r, int x, int y) {
    RECT o = Sc(r);
    return x >= o.left && x < o.right && y >= o.top && y < o.bottom;
}

static HWND    g_hEdit  = NULL;
static Bitmap* g_bg     = NULL;
static HBRUSH  g_brush  = NULL;
static HFONT   g_font   = NULL;
static int     g_hover  = 0;       // 0 none, 1 play, 2 min, 3 close
static bool    g_track  = false;
static const COLORREF FIELD_BG = RGB(4, 9, 26);   // tweak if the field colour looks off

#define ID_EDIT 101

// ---- Helpers ----
static Bitmap* LoadBackground() {
    HRSRC hr = FindResourceW(NULL, MAKEINTRESOURCEW(1), RT_RCDATA);
    if (!hr) return NULL;
    HGLOBAL hg = LoadResource(NULL, hr);
    DWORD sz = SizeofResource(NULL, hr);
    void* p = LockResource(hg);
    if (!p || !sz) return NULL;
    HGLOBAL copy = GlobalAlloc(GMEM_MOVEABLE, sz);
    memcpy(GlobalLock(copy), p, sz);
    GlobalUnlock(copy);
    IStream* st = NULL;
    CreateStreamOnHGlobal(copy, TRUE, &st);   // stream kept alive for the app lifetime
    return st ? Bitmap::FromStream(st) : NULL;
}

static std::wstring LoadNick() {
    wchar_t buf[64] = {0};
    DWORD n = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\SAMP", L"PlayerName", RRF_RT_REG_SZ, NULL, buf, &n) == ERROR_SUCCESS)
        return buf;
    return L"";
}

static void SaveNick(const std::wstring& nick) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\SAMP", 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(k, L"PlayerName", 0, REG_SZ, (const BYTE*)nick.c_str(), (DWORD)((nick.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(k);
    }
}

static bool ValidNick(const std::wstring& n) {
    if (n.size() < 3 || n.size() > 20) return false;
    for (wchar_t c : n) {
        bool ok = (c < 128 && iswalnum(c)) || wcschr(L"_[]$=@.()", c) != NULL;
        if (!ok) return false;
    }
    return true;
}

static bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static std::wstring ExeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring s = buf;
    return s.substr(0, s.find_last_of(L"\\/"));
}

static std::wstring FindGtaDir() {
    std::wstring d = ExeDir();
    if (FileExists(d + L"\\gta_sa.exe")) return d;
    wchar_t buf[MAX_PATH] = {0};
    DWORD n = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\SAMP", L"gta_sa_exe", RRF_RT_REG_SZ, NULL, buf, &n) == ERROR_SUCCESS) {
        std::wstring s = buf;
        size_t pos = s.find_last_of(L"\\/");
        if (pos != std::wstring::npos) {
            std::wstring dir = s.substr(0, pos);
            if (FileExists(dir + L"\\gta_sa.exe")) return dir;
        }
    }
    return L"";
}

static bool Inject(HANDLE hProc, const std::wstring& dll) {
    SIZE_T bytes = (dll.size() + 1) * sizeof(wchar_t);
    void* mem = VirtualAllocEx(hProc, NULL, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mem) return false;
    if (!WriteProcessMemory(hProc, mem, dll.c_str(), bytes, NULL)) return false;
    FARPROC ll = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HANDLE th = CreateRemoteThread(hProc, NULL, 0, (LPTHREAD_START_ROUTINE)ll, mem, 0, NULL);
    if (!th) return false;
    WaitForSingleObject(th, 10000);
    DWORD ret = 0;
    GetExitCodeThread(th, &ret);
    CloseHandle(th);
    VirtualFreeEx(hProc, mem, 0, MEM_RELEASE);
    return ret != 0;
}

static void LaunchGame(HWND hwnd) {
    wchar_t buf[64];
    GetWindowTextW(g_hEdit, buf, 64);
    std::wstring nick = buf;

    if (!ValidNick(nick)) {
        MessageBoxW(hwnd, L"Nickname must be 3-20 characters (letters, numbers and _ [ ] $ = @ . ( ) only).",
                    L"ZentraRPG", MB_ICONWARNING);
        return;
    }
    std::wstring dir = FindGtaDir();
    if (dir.empty()) {
        MessageBoxW(hwnd, L"gta_sa.exe not found.\nPut this launcher inside your GTA San Andreas folder.",
                    L"ZentraRPG", MB_ICONERROR);
        return;
    }
    std::wstring sampDll = dir + L"\\samp.dll";
    if (!FileExists(sampDll)) {
        MessageBoxW(hwnd, L"samp.dll not found in the GTA folder.\nInstall SA-MP first.", L"ZentraRPG", MB_ICONERROR);
        return;
    }

    SaveNick(nick);

    std::wstring cmd = L"\"" + dir + L"\\gta_sa.exe\" -c -n " + nick + L" -h " + SERVER_IP + L" -p " + SERVER_PORT;
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    std::wstring cmdMutable = cmd;
    if (!CreateProcessW(NULL, &cmdMutable[0], NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, dir.c_str(), &si, &pi)) {
        MessageBoxW(hwnd, L"Could not start gta_sa.exe (try running the launcher as Administrator).", L"ZentraRPG", MB_ICONERROR);
        return;
    }
    if (!Inject(pi.hProcess, sampDll)) {
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        MessageBoxW(hwnd, L"Failed to load samp.dll.", L"ZentraRPG", MB_ICONERROR);
        return;
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
}

static void Paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc; GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bm = CreateCompatibleBitmap(hdc, W, H);
    HGDIOBJ old = SelectObject(mem, bm);
    {
        Graphics g(mem);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        if (g_bg) g.DrawImage(g_bg, 0, 0, W, H);
        else      g.Clear(Color(255, 5, 10, 30));

        if (g_hover) {
            const R* r = g_hover == 1 ? &PLAY_R : g_hover == 2 ? &MIN_R : &CLOSE_R;
            RECT o = Sc(*r);
            SolidBrush br(Color(g_hover == 1 ? 40 : 55, 255, 255, 255));
            g.FillRectangle(&br, (INT)o.left + 6, (INT)o.top + 6, (INT)(o.right - o.left - 12), (INT)(o.bottom - o.top - 12));
        }
    }
    BitBlt(hdc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bm);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_brush = CreateSolidBrush(FIELD_BG);
        g_font = CreateFontW(-24, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        RECT f = Sc(FIELD_R);
        int x = (int)(560 * S), w = (int)(1215 * S) - x;
        int h = 32, y = f.top + ((f.bottom - f.top) - h) / 2;
        g_hEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                  x, y, w, h, hwnd, (HMENU)ID_EDIT, NULL, NULL);
        SendMessageW(g_hEdit, WM_SETFONT, (WPARAM)g_font, TRUE);
        SendMessageW(g_hEdit, EM_SETLIMITTEXT, 20, 0);
        SendMessageW(g_hEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Nickname");
        SetWindowTextW(g_hEdit, LoadNick().c_str());
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, RGB(235, 240, 255));
        SetBkColor(dc, FIELD_BG);
        return (LRESULT)g_brush;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Paint(hwnd);
        return 0;
    case WM_NCHITTEST: {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &p);
        if (In(PLAY_R, p.x, p.y) || In(MIN_R, p.x, p.y) || In(CLOSE_R, p.x, p.y) || In(FIELD_R, p.x, p.y))
            return HTCLIENT;
        return HTCAPTION;   // drag the window by its background
    }
    case WM_SETCURSOR: {
        POINT p; GetCursorPos(&p); ScreenToClient(hwnd, &p);
        if (In(PLAY_R, p.x, p.y) || In(MIN_R, p.x, p.y) || In(CLOSE_R, p.x, p.y)) {
            SetCursor(LoadCursor(NULL, IDC_HAND));
            return TRUE;
        }
        break;
    }
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        int h = In(PLAY_R, x, y) ? 1 : In(MIN_R, x, y) ? 2 : In(CLOSE_R, x, y) ? 3 : 0;
        if (h != g_hover) { g_hover = h; InvalidateRect(hwnd, NULL, FALSE); }
        if (!g_track) {
            TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&t);
            g_track = true;
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_track = false;
        if (g_hover) { g_hover = 0; InvalidateRect(hwnd, NULL, FALSE); }
        return 0;
    case WM_LBUTTONUP: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (In(CLOSE_R, x, y))      PostMessageW(hwnd, WM_CLOSE, 0, 0);
        else if (In(MIN_R, x, y))   ShowWindow(hwnd, SW_MINIMIZE);
        else if (In(PLAY_R, x, y))  LaunchGame(hwnd);
        return 0;
    }
    case WM_DESTROY:
        DeleteObject(g_brush);
        DeleteObject(g_font);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nShow) {
    InitCommonControls();
    GdiplusStartupInput gi;
    ULONG_PTR token;
    GdiplusStartup(&token, &gi, NULL);
    g_bg = LoadBackground();

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"ZentraRPGLauncher";
    RegisterClassW(&wc);

    int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"ZentraRPG Launcher", WS_POPUP,
                                (sx - WIN_W) / 2, (sy - WIN_H) / 2, WIN_W, WIN_H, NULL, NULL, hInst, NULL);
    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) {
        if (m.message == WM_KEYDOWN && m.wParam == VK_RETURN && m.hwnd == g_hEdit) {
            LaunchGame(hwnd);
            continue;
        }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    delete g_bg;
    GdiplusShutdown(token);
    return 0;
}
