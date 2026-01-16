#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmsystem.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <winhttp.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <sddl.h>

#include <atomic>
#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "mmdevapi.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "uuid.lib")

// NOTE:
// This file supports BOTH:
// - CLI mode: lan.exe server <port> [fps] [jpegQuality]
// - GUI mode: double-click (no args) to show a launcher UI

static bool g_consoleAttached = false;

static bool HasConsole();

static void EnsureConsoleForCli()
{
    // If launched from an existing console (cmd/PowerShell), attach so printf/fprintf work.
    // If not, this is a no-op.
    if (g_consoleAttached) return;

    if (AttachConsole(ATTACH_PARENT_PROCESS))
    {
        FILE* f = nullptr;
        (void)freopen_s(&f, "CONOUT$", "w", stdout);
        (void)freopen_s(&f, "CONOUT$", "w", stderr);
        (void)freopen_s(&f, "CONIN$", "r", stdin);
        g_consoleAttached = true;
    }
}

static void EnsureConsoleAllocated()
{
    if (HasConsole()) return;
    if (g_consoleAttached) return;

    if (AllocConsole())
    {
        FILE* f = nullptr;
        (void)freopen_s(&f, "CONOUT$", "w", stdout);
        (void)freopen_s(&f, "CONOUT$", "w", stderr);
        (void)freopen_s(&f, "CONIN$", "r", stdin);
        g_consoleAttached = true;
    }
}

static void DetachConsoleForGui()
{
    // When built as a CONSOLE subsystem app, double-click creates a console.
    // Hide + detach so the launcher feels like a real desktop app.
    HWND cw = GetConsoleWindow();
    if (cw) ShowWindow(cw, SW_HIDE);
    FreeConsole();
}

static std::string WideToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), len, nullptr, nullptr);
    return s;
}

// Forward decl (used by GUI logging helpers)
static std::wstring Utf8ToWide(const std::string& s);

// ----------------------------
// Minimal GUI launcher (Win32)
// ----------------------------

static constexpr int IDI_APP_ICON = 101;

static HWND g_launcherHwnd = nullptr;
static HWND g_launcherLog = nullptr;
static HWND g_editPort = nullptr;
static HWND g_editFps = nullptr;
static HWND g_editQuality = nullptr;
static HWND g_editUrl = nullptr;
static HWND g_btnStartServer = nullptr;
static HWND g_btnStopServer = nullptr;
static HWND g_chkServerMute = nullptr;
static HWND g_chkClientMute = nullptr;
static HWND g_listServers = nullptr;
static HWND g_btnDetectServers = nullptr;
static HWND g_btnStopSelected = nullptr;

static HWND g_btnLinks = nullptr;
static HWND g_linksPopup = nullptr;
static HFONT g_uiFont = nullptr;
static int g_popupHoveredId = 0;

static std::thread g_serverThread;
static std::atomic<bool> g_serverThreadActive{ false };
static std::atomic<uint16_t> g_serverPort{ 0 };

static constexpr UINT WM_GUI_LOG = WM_APP + 42;
static constexpr UINT WM_SERVER_STOPPED = WM_APP + 43;
static constexpr UINT WM_FOUND_SERVER = WM_APP + 44;
static constexpr UINT WM_SCAN_DONE = WM_APP + 45;

static std::atomic<bool> g_scanRunning{ false };

static std::wstring MakeStopEventName(uint16_t port)
{
    wchar_t buf[64];
    swprintf_s(buf, L"Local\\LANSCR_STOP_%u", (unsigned)port);
    return std::wstring(buf);
}

static bool SignalStopServerOnPort(uint16_t port);

static HANDLE CreateStopEventForPort(uint16_t port)
{
    std::wstring name = MakeStopEventName(port);

    // Allow any process (even non-elevated) to open & set the event.
    // The low integrity label helps when the server was started elevated.
    // SDDL:
    // - D: allow Everyone (WD) generic all
    // - S: set Low mandatory integrity label
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;;GA;;;WD)S:(ML;;NW;;;LW)",
            SDDL_REVISION_1,
            &sd,
            nullptr))
    {
        // Fallback to default security
        return CreateEventW(nullptr, TRUE, FALSE, name.c_str());
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = sd;
    sa.bInheritHandle = FALSE;

    HANDLE h = CreateEventW(&sa, TRUE, FALSE, name.c_str());
    LocalFree(sd);
    return h;
}

static bool HasConsole()
{
    if (GetConsoleWindow() == nullptr) return false;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    return h && h != INVALID_HANDLE_VALUE;
}

static bool IsServerRunningOnPort(uint16_t port)
{
    std::wstring name = MakeStopEventName(port);
    HANDLE h = OpenEventW(SYNCHRONIZE, FALSE, name.c_str());
    if (h)
    {
        CloseHandle(h);
        return true;
    }
    return false;
}

static bool SignalStopServerOnPort(uint16_t port)
{
    std::wstring name = MakeStopEventName(port);
    HANDLE h = OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str());
    if (!h) return false;
    BOOL ok = SetEvent(h);
    CloseHandle(h);
    return ok != FALSE;
}

static void GuiAppendText(HWND edit, const wchar_t* text)
{
    if (!edit || !text) return;
    int len = GetWindowTextLengthW(edit);
    SendMessageW(edit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(edit, EM_REPLACESEL, FALSE, (LPARAM)text);
}

static void ServerScanThread()
{
    HWND target = g_launcherHwnd;
    // Scan ALL ports by named-event presence (LANSCR_STOP_<port>).
    // This avoids missing servers started on uncommon ports (e.g. 1434).
    for (uint32_t port = 1; port <= 65535; port++)
    {
        if (!g_scanRunning.load()) break;
        if (IsServerRunningOnPort((uint16_t)port))
        {
            if (target) PostMessageW(target, WM_FOUND_SERVER, (WPARAM)port, 0);
        }
    }

    if (target) PostMessageW(target, WM_SCAN_DONE, 0, 0);
}

static void PostGuiLogUtf8(const std::string& line)
{
    if (!g_launcherHwnd) return;
    std::wstring w = Utf8ToWide(line);
    // Heap-allocate so we can hand ownership across threads.
    wchar_t* heap = (wchar_t*)std::calloc(w.size() + 1, sizeof(wchar_t));
    if (!heap) return;
    std::memcpy(heap, w.c_str(), w.size() * sizeof(wchar_t));
    PostMessageW(g_launcherHwnd, WM_GUI_LOG, 0, (LPARAM)heap);
}

static void LogInfo(const char* fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (HasConsole())
    {
        std::fputs(buf, stdout);
        std::fflush(stdout);
    }

    // GUI
    PostGuiLogUtf8(std::string(buf));
}

static void LogError(const char* fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (HasConsole())
    {
        std::fputs(buf, stderr);
        std::fflush(stderr);
    }
    PostGuiLogUtf8(std::string(buf));
}

static constexpr const char* kBoundary = "frame";
static constexpr const char* kBoundaryLine = "--frame\r\n";

static std::atomic<bool> g_running{ true };
static std::atomic<int> g_clientCount{ 0 };
static bool g_verbose = false;

static std::atomic<bool> g_serverAudioEnabled{ true };
static std::atomic<bool> g_serverAudioMuted{ false };
static std::atomic<bool> g_clientAudioMuted{ false };
static std::atomic<bool> g_clientWantsServerMuted{ false };

// Optional HTTP Basic Auth ("private" mode)
static bool g_serverPrivateRequested = false;
static bool g_httpAuthEnabled = false;
static std::string g_httpAuthUser;
static std::string g_httpAuthPass;
static std::string g_httpAuthExpectedB64;
static std::wstring g_winhttpAuthHeaderW;

static std::wstring g_clientVideoUrl;

struct SharedJpegFrame
{
    std::mutex mtx;
    std::condition_variable cv;
    std::vector<uint8_t> bytes;
    uint64_t seq = 0;
};

static SharedJpegFrame g_sharedFrame;
static std::atomic<bool> g_captureThreadRunning{ false };
static bool SendAll(SOCKET s, const void* data, int len);
static HWND g_chkPrivate = nullptr;

static void PrintUsage()
{
    std::printf(
        "Usage:\n"
    "  LANSCR.exe [-v|--verbose] [--mute-audio] [--no-audio] [--private|--auth user:pass] server <port> [fps] [jpegQuality0to100]\n"
    "  LANSCR.exe [-v|--verbose] [--mute] [--auth user:pass] client <url>\n"
    "  LANSCR.exe [-v|--verbose] udp-server <port> [fps] [jpegQuality0to100]\n"
    "  LANSCR.exe [-v|--verbose] udp-client <serverIp> <port>\n"
    "  LANSCR.exe [--auth user:pass] audio-mute <urlOrPort> <0|1>\n"
    "  LANSCR.exe stop <port>\n"
    "  LANSCR.exe detect\n\n"
        "Examples:\n"
    "  LANSCR.exe server 8000 10 80\n"
    "  LANSCR.exe --private server 8000\n"
    "  LANSCR.exe --auth lanscr:YOURPASS server 8000\n"
    "  LANSCR.exe -v server 80 80 80\n"
    "  LANSCR.exe client http://192.168.1.50:8000/\n"
    "  LANSCR.exe --auth lanscr:YOURPASS client http://192.168.1.50:8000/\n"
    "  LANSCR.exe --mute client http://192.168.1.50:8000/\n"
    "  LANSCR.exe udp-server 9000 60 70\n"
    "  LANSCR.exe udp-client 192.168.1.50 9000\n"
    "  LANSCR.exe audio-mute 8000 1\n"
    "  LANSCR.exe stop 8000\n");
}

// ----------------------------
// HTTP Basic Auth helpers
// ----------------------------

extern "C" BOOLEAN NTAPI SystemFunction036(PVOID RandomBuffer, ULONG RandomBufferLength);

static std::string Base64Encode(const uint8_t* data, size_t len)
{
    static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < len)
    {
        uint32_t v = (uint32_t)data[i] << 16 | (uint32_t)data[i + 1] << 8 | (uint32_t)data[i + 2];
        out.push_back(k[(v >> 18) & 63]);
        out.push_back(k[(v >> 12) & 63]);
        out.push_back(k[(v >> 6) & 63]);
        out.push_back(k[v & 63]);
        i += 3;
    }
    if (i < len)
    {
        uint32_t v = (uint32_t)data[i] << 16;
        out.push_back(k[(v >> 18) & 63]);
        if (i + 1 < len)
        {
            v |= (uint32_t)data[i + 1] << 8;
            out.push_back(k[(v >> 12) & 63]);
            out.push_back(k[(v >> 6) & 63]);
            out.push_back('=');
        }
        else
        {
            out.push_back(k[(v >> 12) & 63]);
            out.push_back('=');
            out.push_back('=');
        }
    }
    return out;
}

static std::string TrimAscii(const std::string& s)
{
    size_t a = 0;
    while (a < s.size() && (unsigned char)s[a] <= 32) a++;
    size_t b = s.size();
    while (b > a && (unsigned char)s[b - 1] <= 32) b--;
    return s.substr(a, b - a);
}

static bool IStartsWith(const std::string& s, const char* prefix)
{
    size_t n = std::strlen(prefix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; i++)
    {
        char a = s[i];
        char b = prefix[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

static bool GetHttpHeaderValue(const std::string& req, const char* name, std::string& outValue)
{
    outValue.clear();
    size_t pos = req.find("\r\n");
    if (pos == std::string::npos) return false;
    pos += 2;
    const size_t nameLen = std::strlen(name);
    while (pos < req.size())
    {
        size_t eol = req.find("\r\n", pos);
        if (eol == std::string::npos) break;
        if (eol == pos) return false; // end of headers
        std::string line = req.substr(pos, eol - pos);
        pos = eol + 2;
        if (line.size() <= nameLen + 1) continue;
        bool match = true;
        for (size_t i = 0; i < nameLen; i++)
        {
            char a = line[i];
            char b = name[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) { match = false; break; }
        }
        if (!match) continue;
        if (line[nameLen] != ':') continue;
        outValue = TrimAscii(line.substr(nameLen + 1));
        return true;
    }
    return false;
}

static void ConfigureHttpAuth(const std::string& user, const std::string& pass)
{
    g_httpAuthUser = user;
    g_httpAuthPass = pass;
    std::string up = user + ":" + pass;
    g_httpAuthExpectedB64 = Base64Encode((const uint8_t*)up.data(), up.size());
    g_httpAuthEnabled = true;
    std::string hdr = "Authorization: Basic " + g_httpAuthExpectedB64 + "\r\n";
    g_winhttpAuthHeaderW = Utf8ToWide(hdr);
}

static void ClearHttpAuth()
{
    g_httpAuthEnabled = false;
    g_httpAuthUser.clear();
    g_httpAuthPass.clear();
    g_httpAuthExpectedB64.clear();
    g_winhttpAuthHeaderW.clear();
}

static std::string GenerateRandomPassword(size_t len)
{
    static const char* chars = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    const size_t n = std::strlen(chars);
    std::vector<uint8_t> rnd(len);
    if (!rnd.empty())
    {
        if (!SystemFunction036(rnd.data(), (ULONG)rnd.size()))
        {
            // very small fallback (should be rare)
            for (size_t i = 0; i < rnd.size(); i++) rnd[i] = (uint8_t)(GetTickCount() + (DWORD)i);
        }
    }
    std::string out;
    out.resize(len);
    for (size_t i = 0; i < len; i++) out[i] = chars[rnd[i] % n];
    return out;
}

static bool ParseAuthUserPass(const char* s, std::string& outUser, std::string& outPass)
{
    outUser.clear();
    outPass.clear();
    if (!s) return false;
    const char* colon = std::strchr(s, ':');
    if (!colon) return false;
    outUser.assign(s, colon);
    outPass.assign(colon + 1);
    if (outUser.empty() || outPass.empty()) return false;
    return true;
}

static bool IsHttpAuthorized(const std::string& req)
{
    if (!g_httpAuthEnabled) return true;
    std::string auth;
    if (!GetHttpHeaderValue(req, "Authorization", auth)) return false;
    auth = TrimAscii(auth);
    if (!IStartsWith(auth, "Basic")) return false;
    size_t sp = auth.find(' ');
    if (sp == std::string::npos) return false;
    std::string token = TrimAscii(auth.substr(sp + 1));
    return !g_httpAuthExpectedB64.empty() && token == g_httpAuthExpectedB64;
}

static bool SendHttpUnauthorized(SOCKET client)
{
    const std::string body = "Unauthorized";
    char hdr[512];
    std::snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 401 Unauthorized\r\n"
        "Connection: close\r\n"
        "WWW-Authenticate: Basic realm=\"LANSCR\"\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Content-Length: %zu\r\n"
        "\r\n",
        body.size());
    if (!SendAll(client, hdr, (int)std::strlen(hdr))) return false;
    return SendAll(client, body.data(), (int)body.size());
}

static void WinHttpMaybeAddAuthHeader(HINTERNET hReq)
{
    if (!g_httpAuthEnabled) return;
    if (g_winhttpAuthHeaderW.empty()) return;
    (void)WinHttpAddRequestHeaders(hReq, g_winhttpAuthHeaderW.c_str(), (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);
}

struct CredPromptState
{
    HWND hUser = nullptr;
    HWND hPass = nullptr;
    bool done = false;
    bool ok = false;
};

static void CenterWindowToParent(HWND hwnd, HWND parent)
{
    RECT rc{};
    RECT rp{};
    GetWindowRect(hwnd, &rc);
    if (!parent || !GetWindowRect(parent, &rp))
    {
        rp.left = 0;
        rp.top = 0;
        rp.right = GetSystemMetrics(SM_CXSCREEN);
        rp.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    int x = rp.left + ((rp.right - rp.left) - w) / 2;
    int y = rp.top + ((rp.bottom - rp.top) - h) / 2;
    SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
}

struct PrivateModeDialogState
{
    HWND hRadioAuto = nullptr;
    HWND hRadioManual = nullptr;
    HWND hUser = nullptr;
    HWND hPass = nullptr;
    HWND hRegen = nullptr;
    bool done = false;
    bool ok = false;
    bool autoMode = true;
    std::string user;
    std::string pass;
};

static void PrivateDlgSyncEnabled(PrivateModeDialogState* st)
{
    if (!st) return;
    bool manual = (SendMessageW(st->hRadioManual, BM_GETCHECK, 0, 0) == BST_CHECKED);
    EnableWindow(st->hUser, manual ? TRUE : FALSE);
    EnableWindow(st->hPass, manual ? TRUE : FALSE);
    EnableWindow(st->hRegen, manual ? FALSE : TRUE);
}

static void PrivateDlgSetCreds(PrivateModeDialogState* st, const std::string& user, const std::string& pass)
{
    if (!st) return;
    st->user = user;
    st->pass = pass;
    SetWindowTextW(st->hUser, Utf8ToWide(user).c_str());
    SetWindowTextW(st->hPass, Utf8ToWide(pass).c_str());
}

static LRESULT CALLBACK PrivateModeDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* st = (PrivateModeDialogState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg)
    {
    case WM_CREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)lParam)->lpCreateParams);
        return 0;
    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        if (!st) return 0;
        if (id == 1001 || id == 1002)
        {
            PrivateDlgSyncEnabled(st);
            return 0;
        }
        if (id == 1003)
        {
            // Regenerate (auto mode)
            PrivateDlgSetCreds(st, "lanscr", GenerateRandomPassword(12));
            return 0;
        }
        if (id == IDOK)
        {
            st->autoMode = (SendMessageW(st->hRadioAuto, BM_GETCHECK, 0, 0) == BST_CHECKED);
            wchar_t ubuf[256] = {};
            wchar_t pbuf[256] = {};
            GetWindowTextW(st->hUser, ubuf, (int)(sizeof(ubuf) / sizeof(ubuf[0])));
            GetWindowTextW(st->hPass, pbuf, (int)(sizeof(pbuf) / sizeof(pbuf[0])));
            st->user = WideToUtf8(std::wstring(ubuf));
            st->pass = WideToUtf8(std::wstring(pbuf));

            if (!st->autoMode)
            {
                if (st->user.empty() || st->pass.empty())
                {
                    MessageBoxW(hwnd, L"For Manual mode, both Username and Password are required.", L"LANSCR Private Mode", MB_ICONWARNING | MB_OK);
                    return 0;
                }
            }

            st->ok = true;
            st->done = true;
            return 0;
        }
        if (id == IDCANCEL)
        {
            st->ok = false;
            st->done = true;
            return 0;
        }
        return 0;
    }
    case WM_CLOSE:
        if (st) { st->ok = false; st->done = true; }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool ShowPrivateModeDialog(HWND parent, std::string& outUser, std::string& outPass)
{
    outUser.clear();
    outPass.clear();

    static ATOM s_cls = 0;
    if (!s_cls)
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = PrivateModeDlgProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"LanscrPrivateModeDlg";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        s_cls = RegisterClassW(&wc);
    }

    PrivateModeDialogState st;

    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"LanscrPrivateModeDlg",
        L"Private Stream Settings",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT,
        500, 310,
        parent,
        nullptr,
        GetModuleHandleW(nullptr),
        &st);

    if (!dlg) return false;

    CreateWindowW(L"STATIC", L"Mode:", WS_CHILD | WS_VISIBLE, 14, 14, 60, 18, dlg, nullptr, nullptr, nullptr);
    st.hRadioAuto = CreateWindowW(L"BUTTON", L"Automatic (recommended)", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 86, 12, 220, 20, dlg, (HMENU)1001, nullptr, nullptr);
    st.hRadioManual = CreateWindowW(L"BUTTON", L"Manual (set your own)", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 310, 12, 170, 20, dlg, (HMENU)1002, nullptr, nullptr);

    CreateWindowW(L"STATIC", L"Username:", WS_CHILD | WS_VISIBLE, 14, 58, 90, 18, dlg, nullptr, nullptr, nullptr);
    st.hUser = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 110, 54, 360, 24, dlg, nullptr, nullptr, nullptr);

    CreateWindowW(L"STATIC", L"Password:", WS_CHILD | WS_VISIBLE, 14, 98, 90, 18, dlg, nullptr, nullptr, nullptr);
    st.hPass = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_AUTOHSCROLL, 110, 94, 360, 24, dlg, nullptr, nullptr, nullptr);

    st.hRegen = CreateWindowW(L"BUTTON", L"Regenerate", WS_CHILD | WS_VISIBLE, 110, 130, 110, 26, dlg, (HMENU)1003, nullptr, nullptr);
    CreateWindowW(L"STATIC", L"Manual mode requires both fields.\r\nThis is HTTP Basic Auth (LAN use).", WS_CHILD | WS_VISIBLE, 14, 170, 460, 40, dlg, nullptr, nullptr, nullptr);

    CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 300, 230, 80, 30, dlg, (HMENU)IDOK, nullptr, nullptr);
    CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE, 390, 230, 80, 30, dlg, (HMENU)IDCANCEL, nullptr, nullptr);

    // Default: auto mode with generated pass.
    SendMessageW(st.hRadioAuto, BM_SETCHECK, BST_CHECKED, 0);
    PrivateDlgSetCreds(&st, "lanscr", GenerateRandomPassword(12));
    PrivateDlgSyncEnabled(&st);

    CenterWindowToParent(dlg, parent);
    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);

    if (parent) EnableWindow(parent, FALSE);
    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        if (!IsDialogMessageW(dlg, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (parent) EnableWindow(parent, TRUE);
    DestroyWindow(dlg);

    if (!st.ok) return false;
    outUser = st.user;
    outPass = st.pass;
    return !outUser.empty() && !outPass.empty();
}

static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType)
{
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT || ctrlType == CTRL_CLOSE_EVENT)
    {
        g_running.store(false);
        return TRUE;
    }
    return FALSE;
}

static std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), len);
    return w;
}

static bool SendAll(SOCKET s, const void* data, int len)
{
    const char* p = (const char*)data;
    int remaining = len;
    while (remaining > 0)
    {
        int sent = send(s, p, remaining, 0);
        if (sent == SOCKET_ERROR) return false;
        remaining -= sent;
        p += sent;
    }
    return true;
}

static bool SendAllWithTimeout(SOCKET s, const void* data, int len, int timeoutMs, HANDLE stopEvent)
{
    const char* p = (const char*)data;
    int left = len;

    while (left > 0)
    {
        if (stopEvent && WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0) return false;
        if (!g_running.load()) return false;

        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(s, &wfds);

        timeval tv{};
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;

        int sr = select(0, nullptr, &wfds, nullptr, &tv);
        if (sr <= 0) return false; // timeout or error

        int chunk = left;
        if (chunk > 16 * 1024) chunk = 16 * 1024;
        int sent = send(s, p, chunk, 0);
        if (sent == SOCKET_ERROR)
        {
            int e = WSAGetLastError();
            if (e == WSAEWOULDBLOCK) continue;
            return false;
        }
        if (sent == 0) return false;
        p += sent;
        left -= sent;
    }
    return true;
}

static bool HttpGetSimpleWinHttp(const std::wstring& url, std::string* outBody)
{
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    std::wstring host(256, L'\0');
    std::wstring path(2048, L'\0');
    uc.lpszHostName = host.data();
    uc.dwHostNameLength = (DWORD)host.size();
    uc.lpszUrlPath = path.data();
    uc.dwUrlPathLength = (DWORD)path.size();
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc))
    {
        return false;
    }
    host.resize(uc.dwHostNameLength);
    path.resize(uc.dwUrlPathLength);
    const bool https = (uc.nScheme == INTERNET_SCHEME_HTTPS);

    HINTERNET hSession = WinHttpOpen(L"lanscr-ctl/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), uc.nPort, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }
    DWORD flags = WINHTTP_FLAG_REFRESH;
    if (https) flags |= WINHTTP_FLAG_SECURE;
    HINTERNET hReq = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hReq) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

    WinHttpMaybeAddAuthHeader(hReq);

    BOOL ok = WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(hReq, nullptr);
    if (!ok)
    {
        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    if (outBody)
    {
        outBody->clear();
        for (;;)
        {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(hReq, &avail) || avail == 0) break;
            std::string chunk;
            chunk.resize(avail);
            DWORD read = 0;
            if (!WinHttpReadData(hReq, chunk.data(), avail, &read) || read == 0) break;
            chunk.resize(read);
            outBody->append(chunk);
        }
    }

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return true;
}

static bool RecvSome(SOCKET s, std::string& out)
{
    char buf[4096];
    int n = recv(s, buf, (int)sizeof(buf), 0);
    if (n <= 0) return false;
    out.append(buf, buf + n);
    return true;
}

static int GetIntArg(char** argv, int idx, int argc, int def)
{
    if (idx >= argc) return def;
    return std::atoi(argv[idx]);
}

// ----------------------------
// Screen capture (GDI) + JPEG (WIC)
// ----------------------------

struct JpegFrame
{
    std::vector<uint8_t> bytes;
    int width = 0;
    int height = 0;
};

static HRESULT CaptureScreenToJpeg(IWICImagingFactory* factory, int jpegQuality0to100, JpegFrame& out)
{
    out.bytes.clear();

    const int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    HDC screenDC = GetDC(nullptr);
    if (!screenDC) return E_FAIL;

    HDC memDC = CreateCompatibleDC(screenDC);
    if (!memDC)
    {
        ReleaseDC(nullptr, screenDC);
        return E_FAIL;
    }

    HBITMAP bmp = CreateCompatibleBitmap(screenDC, w, h);
    if (!bmp)
    {
        DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);
        return E_FAIL;
    }

    HGDIOBJ old = SelectObject(memDC, bmp);
    BOOL ok = BitBlt(memDC, 0, 0, w, h, screenDC, x, y, SRCCOPY | CAPTUREBLT);

    // Overlay the mouse cursor (hardware cursor isn't included in BitBlt capture).
    if (ok)
    {
        CURSORINFO ci{};
        ci.cbSize = sizeof(ci);
        if (GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING) && ci.hCursor)
        {
            ICONINFO ii{};
            if (GetIconInfo(ci.hCursor, &ii))
            {
                const int cx = (int)ci.ptScreenPos.x - x - (int)ii.xHotspot;
                const int cy = (int)ci.ptScreenPos.y - y - (int)ii.yHotspot;
                (void)DrawIconEx(memDC, cx, cy, ci.hCursor, 0, 0, 0, nullptr, DI_NORMAL);

                if (ii.hbmMask) DeleteObject(ii.hbmMask);
                if (ii.hbmColor) DeleteObject(ii.hbmColor);
            }
        }
    }
    SelectObject(memDC, old);

    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);

    if (!ok)
    {
        DeleteObject(bmp);
        return E_FAIL;
    }

    IWICBitmap* wicBitmap = nullptr;
    HRESULT hr = factory->CreateBitmapFromHBITMAP(bmp, nullptr, WICBitmapIgnoreAlpha, &wicBitmap);
    DeleteObject(bmp);
    if (FAILED(hr)) return hr;

    IStream* stream = nullptr;
    hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    if (FAILED(hr))
    {
        wicBitmap->Release();
        return hr;
    }

    IWICBitmapEncoder* encoder = nullptr;
    hr = factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder);
    if (FAILED(hr))
    {
        stream->Release();
        wicBitmap->Release();
        return hr;
    }

    hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    if (FAILED(hr))
    {
        encoder->Release();
        stream->Release();
        wicBitmap->Release();
        return hr;
    }

    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* props = nullptr;
    hr = encoder->CreateNewFrame(&frame, &props);
    if (FAILED(hr))
    {
        encoder->Release();
        stream->Release();
        wicBitmap->Release();
        return hr;
    }

    // Set JPEG encoder options.
    // - ImageQuality: float [0..1]
    // - JpegYCrCbSubsampling: BYTE enum (force 4:4:4 to avoid chroma blur)
    if (props)
    {
        PROPBAG2 options[2] = {};
        options[0].pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        options[1].pstrName = const_cast<LPOLESTR>(L"JpegYCrCbSubsampling");

        VARIANT vars[2];
        VariantInit(&vars[0]);
        VariantInit(&vars[1]);

        vars[0].vt = VT_R4;
        float q = (jpegQuality0to100 <= 1) ? 0.01f : (jpegQuality0to100 >= 100 ? 1.0f : (jpegQuality0to100 / 100.0f));
        vars[0].fltVal = q;

        vars[1].vt = VT_UI1;
        vars[1].bVal = (BYTE)WICJpegYCrCbSubsampling444;

        (void)props->Write(2, options, vars);
        VariantClear(&vars[0]);
        VariantClear(&vars[1]);
    }

    hr = frame->Initialize(props);
    if (props) props->Release();
    if (FAILED(hr))
    {
        frame->Release();
        encoder->Release();
        stream->Release();
        wicBitmap->Release();
        return hr;
    }

    hr = frame->WriteSource(wicBitmap, nullptr);
    wicBitmap->Release();
    if (FAILED(hr))
    {
        frame->Release();
        encoder->Release();
        stream->Release();
        return hr;
    }

    hr = frame->Commit();
    frame->Release();
    if (FAILED(hr))
    {
        encoder->Release();
        stream->Release();
        return hr;
    }

    hr = encoder->Commit();
    encoder->Release();
    if (FAILED(hr))
    {
        stream->Release();
        return hr;
    }

    HGLOBAL hg = nullptr;
    hr = GetHGlobalFromStream(stream, &hg);
    if (FAILED(hr))
    {
        stream->Release();
        return hr;
    }

    SIZE_T size = GlobalSize(hg);
    void* ptr = GlobalLock(hg);
    if (!ptr || size == 0)
    {
        if (ptr) GlobalUnlock(hg);
        stream->Release();
        return E_FAIL;
    }

    out.bytes.resize(size);
    std::memcpy(out.bytes.data(), ptr, size);
    GlobalUnlock(hg);
    stream->Release();

    out.width = w;
    out.height = h;
    return S_OK;
}

static void CaptureLoopThread(int fps, int jpegQuality0to100, HANDLE stopEvent)
{
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr))
    {
        if (SUCCEEDED(hrCo)) CoUninitialize();
        g_captureThreadRunning.store(false);
        return;
    }

    // Demand-driven capture:
    // - do not capture when there are no clients (reduces CPU + mouse/input disruption)
    // - cap fps to avoid overloading the machine
    if (fps <= 0) fps = 10;
    if (fps > 60) fps = 60;
    const int delayMs = (int)(1000 / fps);
    uint64_t seqLocal = 0;

    while (g_running.load())
    {
        if (stopEvent && WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0)
        {
            g_running.store(false);
            break;
        }

        // If nobody is watching, don't waste CPU capturing/encoding.
        if (g_clientCount.load() <= 0)
        {
            Sleep(50);
            continue;
        }

        JpegFrame frame;
        hr = CaptureScreenToJpeg(factory, jpegQuality0to100, frame);
        if (SUCCEEDED(hr) && !frame.bytes.empty())
        {
            std::lock_guard<std::mutex> lock(g_sharedFrame.mtx);
            g_sharedFrame.bytes = std::move(frame.bytes);
            g_sharedFrame.seq = ++seqLocal;
            g_sharedFrame.cv.notify_all();
        }
        Sleep(delayMs);
    }

    factory->Release();
    if (SUCCEEDED(hrCo)) CoUninitialize();
    g_captureThreadRunning.store(false);
}

// ----------------------------
// HTTP MJPEG server (WinSock)
// ----------------------------

static bool ParseHttpPath(const std::string& req, std::string& outPath)
{
    // Expected: "GET /path HTTP/1.1"...
    size_t sp1 = req.find(' ');
    if (sp1 == std::string::npos) return false;
    size_t sp2 = req.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) return false;
    outPath = req.substr(sp1 + 1, sp2 - (sp1 + 1));
    if (outPath.empty()) outPath = "/";
    size_t q = outPath.find('?');
    if (q != std::string::npos) outPath.resize(q);
    return true;
}

static bool ParseHttpTarget(const std::string& req, std::string& outPath, std::string& outQuery)
{
    outPath.clear();
    outQuery.clear();
    size_t sp1 = req.find(' ');
    if (sp1 == std::string::npos) return false;
    size_t sp2 = req.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) return false;
    std::string target = req.substr(sp1 + 1, sp2 - (sp1 + 1));
    if (target.empty()) target = "/";
    size_t q = target.find('?');
    if (q != std::string::npos)
    {
        outPath = target.substr(0, q);
        outQuery = target.substr(q + 1);
    }
    else
    {
        outPath = target;
    }
    return true;
}

static bool QueryGetInt(const std::string& query, const char* key, int& out)
{
    // very small query parser: key=value&...
    std::string k(key);
    size_t pos = 0;
    while (pos < query.size())
    {
        size_t amp = query.find('&', pos);
        if (amp == std::string::npos) amp = query.size();
        std::string part = query.substr(pos, amp - pos);
        size_t eq = part.find('=');
        std::string pk = (eq == std::string::npos) ? part : part.substr(0, eq);
        std::string pv = (eq == std::string::npos) ? "" : part.substr(eq + 1);
        if (pk == k)
        {
            out = std::atoi(pv.c_str());
            return true;
        }
        pos = amp + 1;
    }
    return false;
}

static bool SendHttpText(SOCKET client, const char* contentType, const std::string& body)
{
    char hdr[512];
    std::snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-cache\r\n"
        "Pragma: no-cache\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "\r\n",
        contentType,
        body.size());

    if (!SendAll(client, hdr, (int)std::strlen(hdr))) return false;
    return SendAll(client, body.data(), (int)body.size());
}

static std::string MakeLandingHtml(uint16_t port)
{
    // NOTE: Most browsers block autoplay audio until a user gesture.
    char buf[65536];
    const bool priv = g_httpAuthEnabled;
    std::snprintf(buf, sizeof(buf),
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<title>LANSCR</title>"
        "<style>"
        ":root{--primary:#12c780;--primary-dark:#0ea06a;--accent:#22b8ff;--accent-dark:#0b9ddf;--dark:#0b1220;--txt:#eaf6ff;--mut:rgba(234,246,255,.82);--card:rgba(255,255,255,.10);--bd:rgba(255,255,255,.16);--shadow:0 16px 45px rgba(2,12,27,.22);}"
        "*{box-sizing:border-box}"
        "body{margin:0;font-family:system-ui,-apple-system,Segoe UI,Inter,Arial;background:radial-gradient(1200px 600px at 10%% 0%%,rgba(18,199,128,0.10),transparent 55%%),radial-gradient(900px 500px at 90%% 20%%,rgba(34,184,255,0.12),transparent 60%%),linear-gradient(135deg,#071018 0%%,#0a1626 100%%);color:var(--txt);}"
        "a{color:rgba(210,248,255,.95);text-decoration:none}a:hover{text-decoration:underline}"
        "@keyframes fadeIn{0%%{opacity:0;transform:translateY(14px)}100%%{opacity:1;transform:translateY(0)}}"
        ".animate-in{animation:fadeIn .6s ease forwards}"
        ".wrap{max-width:1180px;margin:0 auto;padding:18px;}"
        ".hero{padding:22px 0 16px;text-align:left;background:linear-gradient(135deg,var(--primary) 0%%,var(--accent) 100%%);color:white;border-radius:0 0 26px 26px;position:relative;overflow:hidden;}"
        ".hero:before{content:'';position:absolute;inset:0;background:radial-gradient(900px 420px at 10%% 0%%,rgba(255,255,255,.16),transparent 55%%),radial-gradient(900px 420px at 95%% 25%%,rgba(255,255,255,.12),transparent 60%%);pointer-events:none;}"
        ".hero .wrap{position:relative}"
        ".top{display:flex;gap:12px;align-items:center;justify-content:space-between;flex-wrap:wrap;}"
        ".brand{display:flex;align-items:center;gap:10px;font-weight:900;font-size:18px;letter-spacing:.4px;}"
        ".logo{width:34px;height:34px;border-radius:10px;background:linear-gradient(135deg,rgba(255,255,255,.98),rgba(255,255,255,.82));box-shadow:0 14px 30px rgba(2,12,27,.22);}"
        ".pill{display:inline-flex;align-items:center;gap:10px;padding:8px 12px;border-radius:999px;background:rgba(255,255,255,.16);border:1px solid rgba(255,255,255,.22);}"
        ".badge{display:inline-flex;align-items:center;gap:8px;padding:6px 10px;border-radius:999px;font-size:12px;background:rgba(0,0,0,.20);border:1px solid rgba(255,255,255,.18);}"
        ".tabs{display:flex;gap:8px;flex-wrap:wrap;margin-top:12px;}"
        ".tab{appearance:none;border:1px solid rgba(255,255,255,.16);background:rgba(255,255,255,.05);color:var(--txt);padding:10px 12px;border-radius:12px;cursor:pointer;font-weight:800;}"
        ".tab.on{background:linear-gradient(135deg,rgba(18,199,128,.98),rgba(34,184,255,.98));color:#08131f;border:0}"
        ".panel{display:none;margin-top:12px;}"
        ".panel.on{display:block;}"
        ".grid{display:grid;grid-template-columns:1.35fr .65fr;gap:14px;}"
        "@media (max-width:980px){.grid{grid-template-columns:1fr;}}"
        ".card{background:var(--card);border:1px solid var(--bd);border-radius:16px;overflow:hidden;box-shadow:var(--shadow);backdrop-filter:blur(14px);}"
        ".card h3{margin:0;padding:14px 16px;border-bottom:1px solid var(--bd);font-size:14px;font-weight:900;opacity:.98;display:flex;justify-content:space-between;align-items:center;}"
        ".card .body{padding:14px 16px;}"
        ".btn{appearance:none;border:0;border-radius:12px;padding:10px 12px;font-weight:900;cursor:pointer;color:#08131f;background:linear-gradient(135deg,rgba(18,199,128,.98),rgba(34,184,255,.98));transition:transform .12s ease,filter .12s ease;}"
        ".btn:hover{transform:translateY(-1px);filter:brightness(1.03);}"
        ".btn2{appearance:none;border:1px solid rgba(255,255,255,.18);border-radius:12px;padding:10px 12px;font-weight:900;cursor:pointer;color:var(--txt);background:rgba(255,255,255,.06);transition:transform .12s ease,filter .12s ease;}"
        ".btn2:hover{transform:translateY(-1px);filter:brightness(1.05);}"
        ".row{display:flex;gap:10px;flex-wrap:wrap;margin-top:10px;}"
        ".muted{opacity:.88;font-size:13px;line-height:1.45;color:var(--mut);}"
        ".video{display:block;width:100%%;background:#000;}"
        ".kv{display:grid;grid-template-columns:120px 1fr;gap:6px 10px;margin-top:10px;font-size:13px;opacity:.92}"
        ".kv b{opacity:.92}"
        "</style></head><body><section class='hero animate-in'><div class='wrap'>"
        "<div class='top'>"
        "<div class='brand'><div class='logo'></div> LANSCR <span class='badge'>Live Screen + Audio</span>%s</div>"
        "<div class='pill'><span class='badge' id='mode'>%s</span> <span>Host: <b id='host'></b></span></div>"
        "</div></div></section><div class='wrap'>"
        "<div class='tabs'>"
        "<button class='tab on' data-tab='video'>Video</button>"
        "<button class='tab' data-tab='settings'>Settings</button>"
        "<button class='tab' data-tab='about'>About</button>"
        "</div>"

        "<div class='panel on' id='p-video'>"
        "<div class='grid'>"
        "<div class='card'><h3>Screen <span class='badge'><a href='/mjpeg' rel='nofollow'>/mjpeg</a></span></h3><div id='vwrap'><img id='vid' class='video' src='/mjpeg' alt='stream'></div></div>"
        "<div class='card'><h3>Quick Controls <span class='badge' id='st'>Loading...</span></h3><div class='body'>"
        "<div class='row'>"
        "<button class='btn2' id='fs'>Full Screen</button>"
        "<button class='btn2' id='cp'>Copy Link</button>"
        "</div>"
        "<div class='kv'>"
        "<b>Links</b><span><a href='/' rel='nofollow'>Home</a> | <a href='/mjpeg' rel='nofollow'>Video</a> | <a href='/audio' rel='nofollow'>Audio</a></span>"
        "<b>Share</b><span id='lnk' style='opacity:.95'></span></span>"
        "</div>"
        "<p class='muted' style='margin-top:10px'>Tip: Open this link on a phone on the same Wi-Fi for a live view.</p>"
        "</div></div>"
        "</div>"
        "</div>"

        "<div class='panel' id='p-settings'>"
        "<div class='grid'>"
        "<div class='card'><h3>Audio <span class='badge'><a href='/audio' rel='nofollow'>/audio</a></span></h3><div class='body'>"
        "<div class='row'>"
        "<button class='btn' id='en'>Enable Audio</button>"
        "<button class='btn2' id='mb'>Mute/Unmute Browser</button>"
        "<button class='btn2' id='mt'>Mute/Unmute Server</button>"
        "</div>"
        "<audio id='a' controls style='width:100%%;margin-top:12px' src='/audio'></audio>"
        "<p class='muted' style='margin-top:10px'>Browser audio may need a click due to autoplay rules.</p>"
        "</div></div>"
        "<div class='card'><h3>Status</h3><div class='body'>"
        "<div class='kv'>"
        "<b>Private</b><span id='priv'></span>"
        "<b>Server mute</b><span id='sm'></span>"
        "<b>Port</b><span id='prt'></span>"
        "</div>"
        "<div class='row'><button class='btn2' id='rf'>Refresh</button></div>"
        "</div></div>"
        "</div>"
        "</div>"

        "<div class='panel' id='p-about'>"
        "<div class='grid'>"
        "<div class='card'><h3>Project</h3><div class='body'>"
        "<p class='muted'>LANSCR is a lightweight LAN screen + audio sharing tool (MJPEG video + WAV audio). Open from any browser on the same network.</p>"
        "<div class='kv'>"
        "<b>Website</b><span><a href='http://info.likhil.42web.io/lanscr' target='_blank' rel='nofollow'>info.likhil.42web.io/lanscr</a></span>"
        "<b>GitHub</b><span><a href='https://github.com/pidugulikhil/LANSCR' target='_blank' rel='nofollow'>pidugulikhil/LANSCR</a></span>"
        "<b>Releases</b><span><a href='https://github.com/pidugulikhil/LANSCR/releases' target='_blank' rel='nofollow'>Download builds</a></span>"
        "</div>"
        "</div></div>"
        "<div class='card'><h3>Social / Bio</h3><div class='body'>"
        "<p class='muted'>Follow the links above for updates, releases, and source.</p>"
        "</div></div>"
        "</div>"
        "</div>"

        "</div><script>"
        "const a=document.getElementById('a');"
        "const st=document.getElementById('st');"
        "const host=document.getElementById('host');host.textContent=location.host||(':'+location.port);"
        "const lnk=document.getElementById('lnk');lnk.textContent=location.href;"
        "const privEl=document.getElementById('priv');"
        "const smEl=document.getElementById('sm');"
        "const prtEl=document.getElementById('prt');"

        "function setTab(name){document.querySelectorAll('.tab').forEach(b=>b.classList.toggle('on',b.dataset.tab===name));"
        "document.getElementById('p-video').classList.toggle('on',name==='video');"
        "document.getElementById('p-settings').classList.toggle('on',name==='settings');"
        "document.getElementById('p-about').classList.toggle('on',name==='about');}"
        "document.querySelectorAll('.tab').forEach(b=>b.onclick=()=>setTab(b.dataset.tab));"

        "document.getElementById('en').onclick=()=>{a.muted=false;a.play().catch(()=>{});};"
        "document.getElementById('mb').onclick=()=>{a.muted=!a.muted;st.textContent=a.muted?'Browser muted':'Browser unmuted';setTimeout(()=>poll(),500);};"
        "document.getElementById('fs').onclick=()=>{const el=document.getElementById('vwrap');(el.requestFullscreen||el.webkitRequestFullscreen||el.msRequestFullscreen||(()=>{})).call(el);};"
        "document.getElementById('cp').onclick=()=>{navigator.clipboard&&navigator.clipboard.writeText(location.href).then(()=>{st.textContent='Link copied';setTimeout(()=>poll(),800);}).catch(()=>{});};"
        "document.getElementById('rf').onclick=()=>poll();"
        "async function poll(){try{const r=await fetch('/control',{cache:'no-store'});const j=await r.json();"
        "st.textContent=j.audioMuted?'Server audio muted':'Server audio on';"
        "if(privEl) privEl.textContent=j.privateMode?'ON':'OFF';"
        "if(smEl) smEl.textContent=j.audioMuted?'Muted':'Unmuted';"
        "if(prtEl) prtEl.textContent=j.port;"
        "}catch(e){st.textContent='Status unavailable';}}"
        "async function toggleMute(){try{const r=await fetch('/control',{cache:'no-store'});const j=await r.json();const want=j.audioMuted?0:1;await fetch('/control?mute='+want,{cache:'no-store'});poll();}catch(e){}}"
        "document.getElementById('mt').onclick=toggleMute;"
        "poll();"
        "</script></body></html>",
        priv ? " <span class='badge'>Private</span>" : "",
        priv ? "Private" : "Public");
    return std::string(buf);
}

static bool SendWavHeaderPcm16(SOCKET client, int sampleRate, int channels)
{
    // Streaming WAV with unknown total size: use 0xFFFFFFFF placeholders.
    uint8_t hdr[44] = {};
    auto w32 = [&](int off, uint32_t v) {
        hdr[off + 0] = (uint8_t)(v & 0xFF);
        hdr[off + 1] = (uint8_t)((v >> 8) & 0xFF);
        hdr[off + 2] = (uint8_t)((v >> 16) & 0xFF);
        hdr[off + 3] = (uint8_t)((v >> 24) & 0xFF);
    };
    auto w16 = [&](int off, uint16_t v) {
        hdr[off + 0] = (uint8_t)(v & 0xFF);
        hdr[off + 1] = (uint8_t)((v >> 8) & 0xFF);
    };

    // RIFF
    hdr[0] = 'R'; hdr[1] = 'I'; hdr[2] = 'F'; hdr[3] = 'F';
    w32(4, 0xFFFFFFFF);
    hdr[8] = 'W'; hdr[9] = 'A'; hdr[10] = 'V'; hdr[11] = 'E';

    // fmt 
    hdr[12] = 'f'; hdr[13] = 'm'; hdr[14] = 't'; hdr[15] = ' ';
    w32(16, 16);
    w16(20, 1); // PCM
    w16(22, (uint16_t)channels);
    w32(24, (uint32_t)sampleRate);
    uint16_t blockAlign = (uint16_t)(channels * 2);
    uint32_t byteRate = (uint32_t)sampleRate * (uint32_t)blockAlign;
    w32(28, byteRate);
    w16(32, blockAlign);
    w16(34, 16);

    // data
    hdr[36] = 'd'; hdr[37] = 'a'; hdr[38] = 't'; hdr[39] = 'a';
    w32(40, 0xFFFFFFFF);

    return SendAll(client, hdr, (int)sizeof(hdr));
}

static inline int16_t FloatToS16(float f)
{
    if (f > 1.0f) f = 1.0f;
    if (f < -1.0f) f = -1.0f;
    return (int16_t)std::lrintf(f * 32767.0f);
}

static void StreamAudioThread(SOCKET client, const std::string& clientIp, HANDLE stopEvent)
{
    if (!g_serverAudioEnabled.load())
    {
        const std::string body = "Audio disabled";
        (void)SendHttpText(client, "text/plain; charset=utf-8", body);
        closesocket(client);
        return;
    }
    const std::string headers =
        "HTTP/1.1 200 OK\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-cache\r\n"
        "Pragma: no-cache\r\n"
        "Content-Type: audio/wav\r\n"
        "\r\n";

    if (!SendAll(client, headers.data(), (int)headers.size()))
    {
        closesocket(client);
        return;
    }

    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* audioClient = nullptr;
    IAudioCaptureClient* capture = nullptr;
    WAVEFORMATEX* mix = nullptr;

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) goto cleanup;

    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr)) goto cleanup;

    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, nullptr, (void**)&audioClient);
    if (FAILED(hr)) goto cleanup;

    hr = audioClient->GetMixFormat(&mix);
    if (FAILED(hr) || !mix) goto cleanup;

    // Shared-mode loopback must use the mix format.
    REFERENCE_TIME hnsBuffer = 10000000; // 1 second
    hr = audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        hnsBuffer,
        0,
        mix,
        nullptr);
    if (FAILED(hr)) goto cleanup;

    hr = audioClient->GetService(IID_PPV_ARGS(&capture));
    if (FAILED(hr)) goto cleanup;

    int channels = mix->nChannels ? (int)mix->nChannels : 2;
    int sampleRate = mix->nSamplesPerSec ? (int)mix->nSamplesPerSec : 48000;
    if (!SendWavHeaderPcm16(client, sampleRate, channels)) goto cleanup;

    hr = audioClient->Start();
    if (FAILED(hr)) goto cleanup;

    LogInfo("Audio streaming to %s (rate=%d, ch=%d)\n", clientIp.c_str(), sampleRate, channels);

    while (g_running.load())
    {
        if (stopEvent && WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0)
        {
            g_running.store(false);
            break;
        }

        UINT32 packetFrames = 0;
        hr = capture->GetNextPacketSize(&packetFrames);
        if (FAILED(hr)) break;
        if (packetFrames == 0)
        {
            Sleep(5);
            continue;
        }

        BYTE* data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        hr = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
        if (FAILED(hr)) break;

        size_t outSamples = (size_t)frames * (size_t)channels;
        std::vector<int16_t> out;
        out.resize(outSamples);

        if (g_serverAudioMuted.load())
        {
            std::memset(out.data(), 0, out.size() * sizeof(int16_t));
        }
        else if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
        {
            std::memset(out.data(), 0, out.size() * sizeof(int16_t));
        }
        else
        {
            bool isFloat = false;
            bool isPcm16 = false;
            if (mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
            {
                isFloat = true;
            }
            else if (mix->wFormatTag == WAVE_FORMAT_PCM && mix->wBitsPerSample == 16)
            {
                isPcm16 = true;
            }
            else if (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
            {
                auto* ext = (WAVEFORMATEXTENSIBLE*)mix;
                if (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) isFloat = true;
                if (ext->SubFormat == KSDATAFORMAT_SUBTYPE_PCM && mix->wBitsPerSample == 16) isPcm16 = true;
            }

            if (isPcm16)
            {
                std::memcpy(out.data(), data, out.size() * sizeof(int16_t));
            }
            else if (isFloat)
            {
                const float* f = (const float*)data;
                for (size_t s = 0; s < outSamples; s++) out[s] = FloatToS16(f[s]);
            }
            else
            {
                // Fallback: silence (unknown mix format)
                std::memset(out.data(), 0, out.size() * sizeof(int16_t));
            }
        }

        (void)capture->ReleaseBuffer(frames);

        if (!SendAll(client, out.data(), (int)(out.size() * sizeof(int16_t))))
        {
            break;
        }
    }

cleanup:
    if (audioClient) (void)audioClient->Stop();
    if (mix) CoTaskMemFree(mix);
    if (capture) capture->Release();
    if (audioClient) audioClient->Release();
    if (device) device->Release();
    if (enumerator) enumerator->Release();
    if (SUCCEEDED(hrCo)) CoUninitialize();
    closesocket(client);
}

static void StreamMjpegThread(SOCKET client, const std::string& clientIp, int fps, int jpegQuality0to100, HANDLE stopEvent)
{
    const std::string headers =
        "HTTP/1.1 200 OK\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store, no-cache, must-revalidate, max-age=0\r\n"
        "Pragma: no-cache\r\n"
        "Expires: 0\r\n"
        "X-Accel-Buffering: no\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
        "\r\n";

    // Non-blocking stream socket + bounded send: prevents multi-second buffering on slow clients.
    u_long nb = 1;
    (void)ioctlsocket(client, FIONBIO, &nb);
    int snd = 64 * 1024;
    (void)setsockopt(client, SOL_SOCKET, SO_SNDBUF, (const char*)&snd, sizeof(snd));

    if (!SendAllWithTimeout(client, headers.data(), (int)headers.size(), 1000, stopEvent))
    {
        closesocket(client);
        return;
    }

    g_clientCount.fetch_add(1);
    LogInfo("Streaming to %s (clients=%d)\n", clientIp.c_str(), g_clientCount.load());

    (void)fps;
    (void)jpegQuality0to100;

    uint64_t lastSeq = 0;

    while (g_running.load())
    {
        if (stopEvent && WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0)
        {
            g_running.store(false);
            break;
        }

        std::vector<uint8_t> bytes;
        {
            std::unique_lock<std::mutex> lock(g_sharedFrame.mtx);
            g_sharedFrame.cv.wait_for(lock, std::chrono::milliseconds(1000), [&]() {
                return !g_running.load() || g_sharedFrame.seq != lastSeq;
            });
            if (!g_running.load()) break;
            if (g_sharedFrame.seq == lastSeq || g_sharedFrame.bytes.empty())
            {
                continue;
            }
            lastSeq = g_sharedFrame.seq;
            bytes = g_sharedFrame.bytes;
        }

        char meta[256];
        std::snprintf(meta, sizeof(meta),
            "--frame\r\n"
            "Content-Type: image/jpeg\r\n"
            "Content-Length: %zu\r\n"
            "\r\n",
            bytes.size());

        // If a client can't keep up, drop it rather than accumulating seconds of latency.
        if (!SendAllWithTimeout(client, meta, (int)std::strlen(meta), 500, stopEvent)) break;
        if (!SendAllWithTimeout(client, bytes.data(), (int)bytes.size(), 500, stopEvent)) break;
        if (!SendAllWithTimeout(client, "\r\n", 2, 500, stopEvent)) break;
    }
    closesocket(client);

    int left = g_clientCount.fetch_sub(1) - 1;
    LogInfo("Client disconnected: %s (clients=%d)\n", clientIp.c_str(), left);
}

static void HandleHttpClientThread(SOCKET client, const std::string& clientIp, uint16_t serverPort, int fps, int jpegQuality0to100, HANDLE stopEvent)
{
    // Read request headers (best-effort)
    std::string req;
    for (int i = 0; i < 32; i++)
    {
        u_long avail = 0;
        ioctlsocket(client, FIONREAD, &avail);
        if (avail > 0)
        {
            if (!RecvSome(client, req)) break;
            if (req.find("\r\n\r\n") != std::string::npos) break;
        }
        Sleep(5);
    }

    std::string path;
    std::string query;
    (void)ParseHttpTarget(req, path, query);

    if (g_httpAuthEnabled && !IsHttpAuthorized(req))
    {
        (void)SendHttpUnauthorized(client);
        closesocket(client);
        return;
    }

    if (g_verbose)
    {
        size_t eol = req.find("\r\n");
        std::string first = (eol == std::string::npos) ? req : req.substr(0, eol);
        LogInfo("HTTP %s from %s\n", first.c_str(), clientIp.c_str());
    }

    if (path == "/control")
    {
        int mute = -1;
        if (QueryGetInt(query, "mute", mute))
        {
            g_serverAudioMuted.store(mute != 0);
        }

        // Always return status (also works as a read endpoint).
        std::string body = std::string("{\"audioMuted\":") + (g_serverAudioMuted.load() ? "true" : "false") +
            std::string(",\"privateMode\":") + (g_httpAuthEnabled ? "true" : "false") +
            std::string(",\"port\":") + std::to_string((unsigned)serverPort) +
            "}";
        (void)SendHttpText(client, "application/json; charset=utf-8", body);
        closesocket(client);
        return;
    }

    if (path == "/" || path == "/index.html")
    {
        std::string html = MakeLandingHtml(serverPort);
        (void)SendHttpText(client, "text/html; charset=utf-8", html);
        closesocket(client);
        return;
    }

    if (path == "/audio")
    {
        StreamAudioThread(client, clientIp, stopEvent);
        return;
    }

    // Default: MJPEG stream (also supports explicit /mjpeg)
    StreamMjpegThread(client, clientIp, fps, jpegQuality0to100, stopEvent);
}

static int RunServer(uint16_t port, int fps, int jpegQuality0to100)
{
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    // Improve Sleep() timing precision for lower jitter.
    timeBeginPeriod(1);

    // Reset run flag in case this process previously ran client/server.
    g_running.store(true);

    HANDLE stopEvent = CreateStopEventForPort(port);
    if (!stopEvent)
    {
        LogError("CreateEvent failed (stop event)\n");
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        LogError("A server is already running for port %u.\n", (unsigned)port);
        CloseHandle(stopEvent);
        return 1;
    }

    g_serverPort.store(port);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        LogError("WSAStartup failed\n");
        CloseHandle(stopEvent);
        return 1;
    }

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET)
    {
        LogError("socket() failed\n");
        WSACleanup();
        CloseHandle(stopEvent);
        return 1;
    }

    int yes = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        LogError("bind() failed (port %u). Maybe already in use?\n", (unsigned)port);
        closesocket(listenSock);
        WSACleanup();
        CloseHandle(stopEvent);
        return 1;
    }

    if (listen(listenSock, SOMAXCONN) == SOCKET_ERROR)
    {
        LogError("listen() failed\n");
        closesocket(listenSock);
        WSACleanup();
        CloseHandle(stopEvent);
        return 1;
    }

    LogInfo("LAN MJPEG server running on http://0.0.0.0:%u/\n", (unsigned)port);
    LogInfo("Press Ctrl+C to stop.\n");

    // Start one capture thread for all clients (much smoother for LAN / multiple clients).
    if (!g_captureThreadRunning.exchange(true))
    {
        std::thread cap([fps, jpegQuality0to100, stopEvent]() {
            CaptureLoopThread(fps, jpegQuality0to100, stopEvent);
        });
        cap.detach();
    }

    while (g_running.load())
    {
        if (WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0)
        {
            g_running.store(false);
            break;
        }

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(listenSock, &fds);

        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 250 * 1000;

        int r = select(0, &fds, nullptr, nullptr, &tv);
        if (r <= 0) continue;

        sockaddr_in clientAddr{};
        int clientLen = sizeof(clientAddr);
        SOCKET client = accept(listenSock, (sockaddr*)&clientAddr, &clientLen);
        if (client == INVALID_SOCKET) continue;

        // Lower latency for streaming.
        int one = 1;
        (void)setsockopt(client, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));

        char ip[64] = {};
        inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));
        LogInfo("Client connected: %s\n", ip);

        std::string ipStr(ip);

        // Duplicate stop event for the detached thread so we can safely close the originals later.
        HANDLE stopEventThread = nullptr;
        (void)DuplicateHandle(GetCurrentProcess(), stopEvent, GetCurrentProcess(), &stopEventThread, SYNCHRONIZE, FALSE, 0);

        std::thread t([client, ipStr, port, fps, jpegQuality0to100, stopEventThread]() {
            HandleHttpClientThread(client, ipStr, (uint16_t)port, fps, jpegQuality0to100, stopEventThread);
            if (stopEventThread) CloseHandle(stopEventThread);
        });
        t.detach();
    }

    closesocket(listenSock);
    WSACleanup();
    CloseHandle(stopEvent);
    timeEndPeriod(1);
    return 0;
}

// ----------------------------
// MJPEG client viewer (WinHTTP + WIC + Win32 window)
// ----------------------------

struct FrameBuffer
{
    std::mutex mtx;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> bgra;
    bool hasFrame = false;
};

static FrameBuffer g_frame;
static HWND g_hwnd = nullptr;
static constexpr UINT WM_NEW_FRAME = WM_APP + 1;

static std::string ToLowerAscii(std::string s)
{
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static bool FindBytes(const std::vector<uint8_t>& buf, const char* needle, size_t needleLen, size_t start, size_t& posOut)
{
    if (needleLen == 0 || buf.size() < needleLen) return false;
    for (size_t i = start; i + needleLen <= buf.size(); i++)
    {
        if (std::memcmp(buf.data() + i, needle, needleLen) == 0)
        {
            posOut = i;
            return true;
        }
    }
    return false;
}

static HRESULT DecodeJpegToBGRA(IWICImagingFactory* factory, const uint8_t* jpg, size_t jpgLen, int& outW, int& outH, std::vector<uint8_t>& outBGRA)
{
    outBGRA.clear();

    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, jpgLen);
    if (!hg) return E_OUTOFMEMORY;

    void* p = GlobalLock(hg);
    if (!p)
    {
        GlobalFree(hg);
        return E_FAIL;
    }

    std::memcpy(p, jpg, jpgLen);
    GlobalUnlock(hg);

    IStream* stream = nullptr;
    HRESULT hr = CreateStreamOnHGlobal(hg, TRUE, &stream); // stream frees hg
    if (FAILED(hr))
    {
        GlobalFree(hg);
        return hr;
    }

    IWICBitmapDecoder* decoder = nullptr;
    hr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
    stream->Release();
    if (FAILED(hr)) return hr;

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    decoder->Release();
    if (FAILED(hr)) return hr;

    UINT w = 0, h = 0;
    hr = frame->GetSize(&w, &h);
    if (FAILED(hr))
    {
        frame->Release();
        return hr;
    }

    IWICFormatConverter* conv = nullptr;
    hr = factory->CreateFormatConverter(&conv);
    if (FAILED(hr))
    {
        frame->Release();
        return hr;
    }

    hr = conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    frame->Release();
    if (FAILED(hr))
    {
        conv->Release();
        return hr;
    }

    const UINT stride = w * 4;
    outBGRA.resize((size_t)stride * (size_t)h);
    hr = conv->CopyPixels(nullptr, stride, (UINT)outBGRA.size(), outBGRA.data());
    conv->Release();
    if (FAILED(hr))
    {
        outBGRA.clear();
        return hr;
    }

    outW = (int)w;
    outH = (int)h;
    return S_OK;
}

static bool MakeAudioUrlFromVideoUrl(const std::wstring& url, std::wstring& outAudioUrl)
{
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);

    std::wstring scheme(16, L'\0');
    std::wstring host(256, L'\0');
    std::wstring path(1024, L'\0');
    uc.lpszScheme = scheme.data();
    uc.dwSchemeLength = (DWORD)scheme.size();
    uc.lpszHostName = host.data();
    uc.dwHostNameLength = (DWORD)host.size();
    uc.lpszUrlPath = path.data();
    uc.dwUrlPathLength = (DWORD)path.size();

    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;

    scheme.resize(uc.dwSchemeLength);
    host.resize(uc.dwHostNameLength);

    wchar_t buf[2048];
    swprintf_s(buf, L"%s://%s:%u/audio", scheme.c_str(), host.c_str(), (unsigned)uc.nPort);
    outAudioUrl = buf;
    return true;
}

static bool MakeControlUrlFromVideoUrl(const std::wstring& url, std::wstring& outControlUrl)
{
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);

    std::wstring scheme(16, L'\0');
    std::wstring host(256, L'\0');
    std::wstring path(1024, L'\0');
    uc.lpszScheme = scheme.data();
    uc.dwSchemeLength = (DWORD)scheme.size();
    uc.lpszHostName = host.data();
    uc.dwHostNameLength = (DWORD)host.size();
    uc.lpszUrlPath = path.data();
    uc.dwUrlPathLength = (DWORD)path.size();

    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;

    scheme.resize(uc.dwSchemeLength);
    host.resize(uc.dwHostNameLength);

    wchar_t buf[2048];
    swprintf_s(buf, L"%s://%s:%u/control", scheme.c_str(), host.c_str(), (unsigned)uc.nPort);
    outControlUrl = buf;
    return true;
}

static void ClientAudioThread(const std::wstring& audioUrl)
{
    // This expects the server to return PCM16 WAV with a streaming header.
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);

    std::wstring host(256, L'\0');
    std::wstring path(1024, L'\0');
    uc.lpszHostName = host.data();
    uc.dwHostNameLength = (DWORD)host.size();
    uc.lpszUrlPath = path.data();
    uc.dwUrlPathLength = (DWORD)path.size();

    if (!WinHttpCrackUrl(audioUrl.c_str(), 0, 0, &uc)) return;
    host.resize(uc.dwHostNameLength);
    path.resize(uc.dwUrlPathLength);

    const bool https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    HINTERNET hSession = WinHttpOpen(L"lanscr-audio/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return;
    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), uc.nPort, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return; }

    DWORD flags = WINHTTP_FLAG_REFRESH;
    if (https) flags |= WINHTTP_FLAG_SECURE;
    HINTERNET hReq = WinHttpOpenRequest(hConnect, L"GET", path.empty() ? L"/audio" : path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hReq) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return; }

    WinHttpMaybeAddAuthHeader(hReq);

    if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hReq, nullptr))
    {
        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return;
    }

    // If unauthorized, tell the user how to fix it.
    {
        wchar_t code[16] = {};
        DWORD codeLen = (DWORD)sizeof(code);
        if (WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE, WINHTTP_HEADER_NAME_BY_INDEX, code, &codeLen, WINHTTP_NO_HEADER_INDEX))
        {
            if (wcscmp(code, L"401") == 0)
            {
                std::fprintf(stderr, "Unauthorized (401). Use --auth user:pass\n");
            }
        }
    }

    // Read WAV header (44 bytes)
    uint8_t wav[44] = {};
    size_t got = 0;
    while (got < sizeof(wav) && g_running.load())
    {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hReq, &avail)) break;
        if (avail == 0) { Sleep(5); continue; }
        DWORD read = 0;
        DWORD toRead = (DWORD)std::min<DWORD>((DWORD)(sizeof(wav) - got), avail);
        if (!WinHttpReadData(hReq, wav + got, toRead, &read) || read == 0) break;
        got += read;
    }
    if (got < sizeof(wav))
    {
        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return;
    }

    auto r16 = [&](int off) -> uint16_t { return (uint16_t)(wav[off] | (wav[off + 1] << 8)); };
    auto r32 = [&](int off) -> uint32_t {
        return (uint32_t)(wav[off] | (wav[off + 1] << 8) | (wav[off + 2] << 16) | (wav[off + 3] << 24));
    };

    uint16_t fmt = r16(20);
    uint16_t channels = r16(22);
    uint32_t rate = r32(24);
    uint16_t bits = r16(34);
    if (fmt != 1 || bits != 16 || channels == 0 || rate == 0)
    {
        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return;
    }

    WAVEFORMATEX wf{};
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = channels;
    wf.nSamplesPerSec = rate;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = (WORD)(channels * 2);
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;

    HWAVEOUT hwo = nullptr;
    if (waveOutOpen(&hwo, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
    {
        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return;
    }

    const int kBufCount = 4;
    const DWORD kBufSize = 16384;
    std::vector<std::vector<uint8_t>> bufs(kBufCount);
    std::vector<WAVEHDR> hdrs(kBufCount);
    for (int i = 0; i < kBufCount; i++)
    {
        bufs[i].resize(kBufSize);
        std::memset(&hdrs[i], 0, sizeof(WAVEHDR));
        hdrs[i].lpData = (LPSTR)bufs[i].data();
        hdrs[i].dwBufferLength = 0;
        waveOutPrepareHeader(hwo, &hdrs[i], sizeof(WAVEHDR));
        hdrs[i].dwFlags |= WHDR_DONE;
    }

    int idx = 0;
    while (g_running.load())
    {
        // Local client-side mute: keep connection but silence output.
        if (g_clientAudioMuted.load())
        {
            (void)waveOutSetVolume(hwo, 0);
        }
        else
        {
            (void)waveOutSetVolume(hwo, 0xFFFFFFFF);
        }

        if (!(hdrs[idx].dwFlags & WHDR_DONE))
        {
            Sleep(5);
            continue;
        }

        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hReq, &avail)) break;
        if (avail == 0) { Sleep(5); continue; }

        DWORD toRead = (DWORD)std::min<DWORD>(avail, kBufSize);
        DWORD read = 0;
        if (!WinHttpReadData(hReq, bufs[idx].data(), toRead, &read) || read == 0) break;

        hdrs[idx].dwBufferLength = read;
        hdrs[idx].dwFlags &= ~WHDR_DONE;
        waveOutWrite(hwo, &hdrs[idx], sizeof(WAVEHDR));

        idx = (idx + 1) % kBufCount;
    }

    waveOutReset(hwo);
    for (int i = 0; i < kBufCount; i++) waveOutUnprepareHeader(hwo, &hdrs[i], sizeof(WAVEHDR));
    waveOutClose(hwo);

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
}

static void ClientNetworkThread(const std::wstring& url)
{
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr))
    {
        if (SUCCEEDED(hrCo)) CoUninitialize();
        PostMessage(g_hwnd, WM_CLOSE, 0, 0);
        return;
    }

    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);

    std::wstring host(256, L'\0');
    std::wstring path(1024, L'\0');
    uc.lpszHostName = host.data();
    uc.dwHostNameLength = (DWORD)host.size();
    uc.lpszUrlPath = path.data();
    uc.dwUrlPathLength = (DWORD)path.size();

    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc))
    {
        std::fprintf(stderr, "WinHttpCrackUrl failed\n");
        factory->Release();
        if (SUCCEEDED(hrCo)) CoUninitialize();
        PostMessage(g_hwnd, WM_CLOSE, 0, 0);
        return;
    }

    host.resize(uc.dwHostNameLength);
    path.resize(uc.dwUrlPathLength);

    // The server uses '/' for an HTML landing page. The native viewer needs the MJPEG endpoint.
    if (path.empty() || path == L"/" || path == L"/index.html")
    {
        path = L"/mjpeg";
    }

    const bool https = (uc.nScheme == INTERNET_SCHEME_HTTPS);

    HINTERNET hSession = WinHttpOpen(L"lan-mjpeg/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession)
    {
        std::fprintf(stderr, "WinHttpOpen failed\n");
        factory->Release();
        if (SUCCEEDED(hrCo)) CoUninitialize();
        PostMessage(g_hwnd, WM_CLOSE, 0, 0);
        return;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), uc.nPort, 0);
    if (!hConnect)
    {
        std::fprintf(stderr, "WinHttpConnect failed\n");
        WinHttpCloseHandle(hSession);
        factory->Release();
        if (SUCCEEDED(hrCo)) CoUninitialize();
        PostMessage(g_hwnd, WM_CLOSE, 0, 0);
        return;
    }

    DWORD flags = WINHTTP_FLAG_REFRESH;
    if (https) flags |= WINHTTP_FLAG_SECURE;

    HINTERNET hReq = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hReq)
    {
        std::fprintf(stderr, "WinHttpOpenRequest failed\n");
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        factory->Release();
        if (SUCCEEDED(hrCo)) CoUninitialize();
        PostMessage(g_hwnd, WM_CLOSE, 0, 0);
        return;
    }

    // Ask intermediaries not to buffer/cach e (helps latency in some environments).
    (void)WinHttpAddRequestHeaders(hReq, L"Cache-Control: no-cache\r\nPragma: no-cache\r\n", (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);

    WinHttpMaybeAddAuthHeader(hReq);

    if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hReq, nullptr))
    {
        std::fprintf(stderr, "HTTP request failed\n");
        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        factory->Release();
        if (SUCCEEDED(hrCo)) CoUninitialize();
        PostMessage(g_hwnd, WM_CLOSE, 0, 0);
        return;
    }

    {
        wchar_t code[16] = {};
        DWORD codeLen = (DWORD)sizeof(code);
        if (WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE, WINHTTP_HEADER_NAME_BY_INDEX, code, &codeLen, WINHTTP_NO_HEADER_INDEX))
        {
            if (wcscmp(code, L"401") == 0)
            {
                std::fprintf(stderr, "Unauthorized (401). Use --auth user:pass\n");
            }
        }
    }

    std::vector<uint8_t> buffer;
    buffer.reserve(1024 * 1024);

    const char* boundary = "--frame";
    const size_t boundaryLen = std::strlen(boundary);
    const char* headerEnd = "\r\n\r\n";
    const size_t headerEndLen = 4;

    while (g_running.load())
    {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hReq, &avail)) break;
        if (avail == 0)
        {
            Sleep(10);
            continue;
        }

        size_t oldSize = buffer.size();
        buffer.resize(oldSize + avail);

        DWORD read = 0;
        if (!WinHttpReadData(hReq, buffer.data() + oldSize, avail, &read) || read == 0)
        {
            buffer.resize(oldSize);
            break;
        }
        buffer.resize(oldSize + read);

        // Parse as many frames as possible
        for (;;)
        {
            size_t bpos = 0;
            if (!FindBytes(buffer, boundary, boundaryLen, 0, bpos))
            {
                // Keep buffer from growing unbounded
                if (buffer.size() > 4 * 1024 * 1024)
                {
                    buffer.erase(buffer.begin(), buffer.begin() + (buffer.size() - 1024 * 1024));
                }
                break;
            }

            if (bpos > 0)
            {
                buffer.erase(buffer.begin(), buffer.begin() + bpos);
            }

            // Need end of headers
            size_t hend = 0;
            if (!FindBytes(buffer, headerEnd, headerEndLen, 0, hend)) break;

            std::string headerStr((const char*)buffer.data(), (const char*)buffer.data() + hend + headerEndLen);
            std::string headerLower = ToLowerAscii(headerStr);

            size_t cl = headerLower.find("content-length:");
            if (cl == std::string::npos)
            {
                // Can't parse, resync by dropping boundary
                if (buffer.size() > boundaryLen) buffer.erase(buffer.begin(), buffer.begin() + boundaryLen);
                continue;
            }

            size_t numStart = cl + std::strlen("content-length:");
            while (numStart < headerLower.size() && (headerLower[numStart] == ' ' || headerLower[numStart] == '\t')) numStart++;

            int contentLen = std::atoi(headerLower.c_str() + numStart);
            if (contentLen <= 0)
            {
                if (buffer.size() > boundaryLen) buffer.erase(buffer.begin(), buffer.begin() + boundaryLen);
                continue;
            }

            size_t jpegStart = hend + headerEndLen;
            size_t need = jpegStart + (size_t)contentLen + 2; // + trailing \r\n
            if (buffer.size() < need) break;

            const uint8_t* jpg = buffer.data() + jpegStart;
            size_t jpgLen = (size_t)contentLen;

            int w = 0, h = 0;
            std::vector<uint8_t> bgra;
            HRESULT dhr = DecodeJpegToBGRA(factory, jpg, jpgLen, w, h, bgra);
            if (SUCCEEDED(dhr) && !bgra.empty())
            {
                {
                    std::lock_guard<std::mutex> lock(g_frame.mtx);
                    g_frame.width = w;
                    g_frame.height = h;
                    g_frame.bgra = std::move(bgra);
                    g_frame.hasFrame = true;
                }
                PostMessage(g_hwnd, WM_NEW_FRAME, 0, 0);
            }

            buffer.erase(buffer.begin(), buffer.begin() + need);
        }
    }

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    factory->Release();
    if (SUCCEEDED(hrCo)) CoUninitialize();

    PostMessage(g_hwnd, WM_CLOSE, 0, 0);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static constexpr UINT IDM_CLIENT_MUTE_LOCAL = 5001;
    static constexpr UINT IDM_CLIENT_MUTE_SERVER = 5002;

    switch (msg)
    {
    case WM_NEW_FRAME:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_CONTEXTMENU:
    {
        HMENU menu = CreatePopupMenu();
        if (!menu) return 0;

        const bool localMuted = g_clientAudioMuted.load();
        const bool serverMuted = g_clientWantsServerMuted.load();
        AppendMenuW(menu, MF_STRING | (localMuted ? MF_CHECKED : 0), IDM_CLIENT_MUTE_LOCAL, L"Mute client audio");
        AppendMenuW(menu, MF_STRING | (serverMuted ? MF_CHECKED : 0), IDM_CLIENT_MUTE_SERVER, L"Mute server audio");

        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
        if (pt.x == -1 && pt.y == -1)
        {
            GetCursorPos(&pt);
        }

        int cmd = (int)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
        DestroyMenu(menu);

        if (cmd == (int)IDM_CLIENT_MUTE_LOCAL)
        {
            g_clientAudioMuted.store(!g_clientAudioMuted.load());
            return 0;
        }
        if (cmd == (int)IDM_CLIENT_MUTE_SERVER)
        {
            bool newMuted = !g_clientWantsServerMuted.load();
            g_clientWantsServerMuted.store(newMuted);

            std::wstring control;
            if (MakeControlUrlFromVideoUrl(g_clientVideoUrl, control))
            {
                control += L"?mute=";
                control += newMuted ? L"1" : L"0";
                std::thread([control]() {
                    (void)HttpGetSimpleWinHttp(control, nullptr);
                }).detach();
            }
            return 0;
        }
        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);

        FrameBuffer local;
        {
            std::lock_guard<std::mutex> lock(g_frame.mtx);
            local.width = g_frame.width;
            local.height = g_frame.height;
            local.bgra = g_frame.bgra;
            local.hasFrame = g_frame.hasFrame;
        }

        if (local.hasFrame && local.width > 0 && local.height > 0 && !local.bgra.empty())
        {
            // High-quality scaling (default can look blocky).
            SetStretchBltMode(hdc, HALFTONE);
            SetBrushOrgEx(hdc, 0, 0, nullptr);

            // Preserve aspect ratio (letterbox if needed).
            const int dstW = rc.right - rc.left;
            const int dstH = rc.bottom - rc.top;
            double srcAR = (double)local.width / (double)local.height;
            double dstAR = dstH == 0 ? srcAR : (double)dstW / (double)dstH;

            int drawW = dstW;
            int drawH = dstH;
            int offX = 0;
            int offY = 0;
            if (dstAR > srcAR)
            {
                // Window is wider than source
                drawH = dstH;
                drawW = (int)(drawH * srcAR);
                offX = (dstW - drawW) / 2;
            }
            else if (dstAR < srcAR)
            {
                // Window is taller than source
                drawW = dstW;
                drawH = (int)(drawW / srcAR);
                offY = (dstH - drawH) / 2;
            }

            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = local.width;
            bmi.bmiHeader.biHeight = -local.height; // top-down
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            StretchDIBits(
                hdc,
                offX, offY, drawW, drawH,
                0, 0, local.width, local.height,
                local.bgra.data(),
                &bmi,
                DIB_RGB_COLORS,
                SRCCOPY);
        }
        else
        {
            const char* txt = "Waiting for frames...";
            TextOutA(hdc, 10, 10, txt, (int)std::strlen(txt));
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

static int RunClient(const std::wstring& url)
{
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    // Ensure running flag is set for both video + audio threads.
    g_running.store(true);

    g_clientVideoUrl = url;
    g_clientWantsServerMuted.store(false);

    // Reduce OS DPI bitmap-scaling blur on the window.
    if (HMODULE user32 = LoadLibraryW(L"user32.dll"))
    {
        using Fn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto fn = (Fn)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (fn)
        {
            (void)fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
        FreeLibrary(user32);
    }

    HINSTANCE hInst = GetModuleHandle(nullptr);

    WNDCLASS wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"LanMjpegClient";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APP_ICON));

    if (!RegisterClass(&wc))
    {
        std::fprintf(stderr, "RegisterClass failed\n");
        return 1;
    }

    g_hwnd = CreateWindowEx(
        0,
        wc.lpszClassName,
        L"LAN Screen Viewer (MJPEG over HTTP)",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720,
        nullptr, nullptr, hInst, nullptr);

    if (!g_hwnd)
    {
        std::fprintf(stderr, "CreateWindowEx failed\n");
        return 1;
    }

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    std::thread net([url]() { ClientNetworkThread(url); });
    net.detach();

    std::wstring audioUrl;
    if (MakeAudioUrlFromVideoUrl(url, audioUrl))
    {
        std::thread aud([audioUrl]() { ClientAudioThread(audioUrl); });
        aud.detach();
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    g_running.store(false);
    return 0;
}

static uint64_t GetTickMs()
{
    return (uint64_t)GetTickCount64();
}

static bool SameAddr(const sockaddr_in& a, const sockaddr_in& b)
{
    return a.sin_family == b.sin_family && a.sin_port == b.sin_port && a.sin_addr.s_addr == b.sin_addr.s_addr;
}

static std::string SockaddrToString(const sockaddr_in& a)
{
    char ip[64] = {};
    inet_ntop(AF_INET, &a.sin_addr, ip, sizeof(ip));
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s:%u", ip, (unsigned)ntohs(a.sin_port));
    return std::string(buf);
}

#pragma pack(push, 1)
struct UdpFrameChunkHeader
{
    uint32_t magic;
    uint32_t frameId;
    uint16_t chunkIndex;
    uint16_t chunkCount;
    uint16_t payloadLen;
    uint16_t reserved;
};
#pragma pack(pop)

static constexpr uint32_t kUdpMagic = 0x3255534Cu; // 'LSU2'
static constexpr int kUdpPayloadMax = 1200;

struct UdpClientEntry
{
    sockaddr_in addr{};
    uint64_t lastSeenMs = 0;
};

static void UdpServerRecvLoop(SOCKET s, std::mutex* mtx, std::vector<UdpClientEntry>* clients)
{
    char buf[256];
    while (g_running.load())
    {
        sockaddr_in from{};
        int fromLen = sizeof(from);
        int n = recvfrom(s, buf, (int)sizeof(buf), 0, (sockaddr*)&from, &fromLen);
        if (n == SOCKET_ERROR)
        {
            int e = WSAGetLastError();
            if (e == WSAEWOULDBLOCK)
            {
                Sleep(2);
                continue;
            }
            Sleep(2);
            continue;
        }
        if (n <= 0) continue;

        const uint64_t now = GetTickMs();
        std::lock_guard<std::mutex> lock(*mtx);
        bool found = false;
        for (auto& c : *clients)
        {
            if (SameAddr(c.addr, from))
            {
                c.lastSeenMs = now;
                found = true;
                break;
            }
        }
        if (!found)
        {
            UdpClientEntry e;
            e.addr = from;
            e.lastSeenMs = now;
            clients->push_back(e);
            if (g_verbose) LogInfo("UDP client added: %s\n", SockaddrToString(from).c_str());
        }
    }
}

static int RunUdpServer(uint16_t port, int fps, int jpegQuality0to100)
{
    EnsureConsoleAllocated();
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
    g_running.store(true);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        LogError("WSAStartup failed\n");
        return 1;
    }

    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
    {
        LogError("udp socket() failed\n");
        WSACleanup();
        return 1;
    }

    int snd = 4 * 1024 * 1024;
    int rcv = 4 * 1024 * 1024;
    (void)setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char*)&snd, sizeof(snd));
    (void)setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&rcv, sizeof(rcv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        LogError("udp bind() failed on port %u\n", (unsigned)port);
        closesocket(s);
        WSACleanup();
        return 1;
    }

    u_long nb = 1;
    (void)ioctlsocket(s, FIONBIO, &nb);

    if (fps <= 0) fps = 30;
    if (fps > 120) fps = 120;
    if (jpegQuality0to100 < 1) jpegQuality0to100 = 1;
    if (jpegQuality0to100 > 100) jpegQuality0to100 = 100;
    const int delayMs = (int)(1000 / fps);

    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr))
    {
        if (SUCCEEDED(hrCo)) CoUninitialize();
        closesocket(s);
        WSACleanup();
        LogError("WIC factory init failed\n");
        return 1;
    }

    std::mutex clientsMtx;
    std::vector<UdpClientEntry> clients;
    std::thread recvThread([&]() { UdpServerRecvLoop(s, &clientsMtx, &clients); });
    recvThread.detach();

    LogInfo("UDP server on 0.0.0.0:%u (run udp-client to subscribe)\n", (unsigned)port);

    uint32_t frameId = 0;
    while (g_running.load())
    {
        std::vector<UdpClientEntry> snap;
        {
            std::lock_guard<std::mutex> lock(clientsMtx);
            const uint64_t now = GetTickMs();
            clients.erase(std::remove_if(clients.begin(), clients.end(), [&](const UdpClientEntry& c) {
                return (now - c.lastSeenMs) > 3000;
            }), clients.end());
            snap = clients;
        }

        if (snap.empty())
        {
            Sleep(25);
            continue;
        }

        JpegFrame jf;
        hr = CaptureScreenToJpeg(factory, jpegQuality0to100, jf);
        if (FAILED(hr) || jf.bytes.empty())
        {
            Sleep(10);
            continue;
        }

        frameId++;
        const size_t total = jf.bytes.size();
        const uint16_t chunkCount = (uint16_t)((total + (kUdpPayloadMax - 1)) / kUdpPayloadMax);
        std::vector<uint8_t> packet(sizeof(UdpFrameChunkHeader) + kUdpPayloadMax);

        for (uint16_t ci = 0; ci < chunkCount; ci++)
        {
            const size_t off = (size_t)ci * kUdpPayloadMax;
            size_t len = total - off;
            if (len > (size_t)kUdpPayloadMax) len = (size_t)kUdpPayloadMax;

            UdpFrameChunkHeader hdr{};
            hdr.magic = kUdpMagic;
            hdr.frameId = frameId;
            hdr.chunkIndex = ci;
            hdr.chunkCount = chunkCount;
            hdr.payloadLen = (uint16_t)len;
            hdr.reserved = 0;
            std::memcpy(packet.data(), &hdr, sizeof(hdr));
            std::memcpy(packet.data() + sizeof(hdr), jf.bytes.data() + off, len);

            const int pktLen = (int)(sizeof(hdr) + len);
            for (const auto& c : snap)
            {
                (void)sendto(s, (const char*)packet.data(), pktLen, 0, (const sockaddr*)&c.addr, sizeof(c.addr));
            }
        }

        Sleep(delayMs);
    }

    factory->Release();
    if (SUCCEEDED(hrCo)) CoUninitialize();
    closesocket(s);
    WSACleanup();
    return 0;
}

static void UdpClientNetworkThread(const std::string& serverIp, uint16_t port)
{
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr))
    {
        if (SUCCEEDED(hrCo)) CoUninitialize();
        PostMessage(g_hwnd, WM_CLOSE, 0, 0);
        return;
    }

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        factory->Release();
        if (SUCCEEDED(hrCo)) CoUninitialize();
        PostMessage(g_hwnd, WM_CLOSE, 0, 0);
        return;
    }

    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
    {
        WSACleanup();
        factory->Release();
        if (SUCCEEDED(hrCo)) CoUninitialize();
        PostMessage(g_hwnd, WM_CLOSE, 0, 0);
        return;
    }

    int snd = 4 * 1024 * 1024;
    int rcv = 4 * 1024 * 1024;
    (void)setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char*)&snd, sizeof(snd));
    (void)setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&rcv, sizeof(rcv));

    sockaddr_in bindAddr{};
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    bindAddr.sin_port = htons(0);
    (void)bind(s, (sockaddr*)&bindAddr, sizeof(bindAddr));

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    if (inet_pton(AF_INET, serverIp.c_str(), &server.sin_addr) != 1)
    {
        closesocket(s);
        WSACleanup();
        factory->Release();
        if (SUCCEEDED(hrCo)) CoUninitialize();
        PostMessage(g_hwnd, WM_CLOSE, 0, 0);
        return;
    }

    u_long nb = 1;
    (void)ioctlsocket(s, FIONBIO, &nb);

    uint32_t curFrame = 0;
    uint16_t curCount = 0;
    uint16_t curLastChunkLen = 0;
    std::vector<uint8_t> accum;
    std::vector<uint8_t> got;
    uint64_t lastHello = 0;

    while (g_running.load())
    {
        const uint64_t now = GetTickMs();
        if (now - lastHello > 500)
        {
            const char hello[] = "LSU2";
            (void)sendto(s, hello, (int)sizeof(hello), 0, (const sockaddr*)&server, sizeof(server));
            lastHello = now;
        }

        uint8_t pkt[1600];
        sockaddr_in from{};
        int fromLen = sizeof(from);
        int n = recvfrom(s, (char*)pkt, (int)sizeof(pkt), 0, (sockaddr*)&from, &fromLen);
        if (n == SOCKET_ERROR)
        {
            int e = WSAGetLastError();
            if (e == WSAEWOULDBLOCK)
            {
                Sleep(1);
                continue;
            }
            Sleep(1);
            continue;
        }
        if (n < (int)sizeof(UdpFrameChunkHeader)) continue;

        UdpFrameChunkHeader hdr{};
        std::memcpy(&hdr, pkt, sizeof(hdr));
        if (hdr.magic != kUdpMagic) continue;
        if (hdr.payloadLen == 0 || hdr.payloadLen > kUdpPayloadMax) continue;
        if ((int)(sizeof(hdr) + hdr.payloadLen) > n) continue;
        if (hdr.chunkCount == 0) continue;
        if (hdr.chunkIndex >= hdr.chunkCount) continue;

        if (hdr.frameId != curFrame)
        {
            curFrame = hdr.frameId;
            curCount = hdr.chunkCount;
            curLastChunkLen = 0;
            accum.assign((size_t)curCount * kUdpPayloadMax, 0);
            got.assign(curCount, 0);
        }
        if (hdr.chunkCount != curCount) continue;
        if (got[hdr.chunkIndex]) continue;

        std::memcpy(accum.data() + (size_t)hdr.chunkIndex * kUdpPayloadMax, pkt + sizeof(hdr), hdr.payloadLen);
        got[hdr.chunkIndex] = 1;
        if (hdr.chunkIndex == (uint16_t)(hdr.chunkCount - 1)) curLastChunkLen = hdr.payloadLen;

        bool complete = true;
        for (uint16_t k = 0; k < curCount; k++) { if (!got[k]) { complete = false; break; } }
        if (complete)
        {
            if (curLastChunkLen == 0) continue;
            const size_t jpegLen = (size_t)(curCount - 1) * kUdpPayloadMax + (size_t)curLastChunkLen;
            int w = 0, h = 0;
            std::vector<uint8_t> bgra;
            HRESULT dhr = DecodeJpegToBGRA(factory, accum.data(), jpegLen, w, h, bgra);
            if (SUCCEEDED(dhr) && !bgra.empty())
            {
                {
                    std::lock_guard<std::mutex> lock(g_frame.mtx);
                    g_frame.width = w;
                    g_frame.height = h;
                    g_frame.bgra = std::move(bgra);
                    g_frame.hasFrame = true;
                }
                PostMessage(g_hwnd, WM_NEW_FRAME, 0, 0);
            }
            got.assign(curCount, 0);
        }
    }

    closesocket(s);
    WSACleanup();
    factory->Release();
    if (SUCCEEDED(hrCo)) CoUninitialize();
    PostMessage(g_hwnd, WM_CLOSE, 0, 0);
}

static int RunUdpClient(const std::string& serverIp, uint16_t port)
{
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
    g_running.store(true);

    // Reduce OS DPI bitmap-scaling blur on the window.
    if (HMODULE user32 = LoadLibraryW(L"user32.dll"))
    {
        using Fn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto fn = (Fn)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (fn) (void)fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        FreeLibrary(user32);
    }

    HINSTANCE hInst = GetModuleHandle(nullptr);

    const wchar_t* cls = L"LanUdpClient";
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = cls;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    (void)RegisterClassW(&wc);

    g_hwnd = CreateWindowExW(
        0,
        cls,
        L"LAN Screen Viewer (UDP JPEG)",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720,
        nullptr, nullptr, hInst, nullptr);

    if (!g_hwnd)
    {
        LogError("CreateWindowEx failed\n");
        return 1;
    }

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    std::thread net([serverIp, port]() { UdpClientNetworkThread(serverIp, port); });
    net.detach();

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    g_running.store(false);
    return 0;
}

static int RunCli(int argc, char** argv)
{
    if (argc < 2)
    {
        PrintUsage();
        return 1;
    }

    // Parse flags before subcommand.
    int i = 1;
    for (; i < argc; i++)
    {
        const char* a = argv[i];
        if (!a || a[0] != '-') break;
        if (std::strcmp(a, "-v") == 0 || std::strcmp(a, "--v") == 0 || std::strcmp(a, "--verbose") == 0)
        {
            g_verbose = true;
            continue;
        }
        if (std::strcmp(a, "--mute") == 0)
        {
            g_clientAudioMuted.store(true);
            continue;
        }
        if (std::strcmp(a, "--mute-audio") == 0)
        {
            g_serverAudioMuted.store(true);
            continue;
        }
        if (std::strcmp(a, "--no-audio") == 0)
        {
            g_serverAudioEnabled.store(false);
            continue;
        }
        if (std::strcmp(a, "--private") == 0)
        {
            g_serverPrivateRequested = true;
            continue;
        }
        if (std::strcmp(a, "--auth") == 0)
        {
            if (i + 1 >= argc)
            {
                PrintUsage();
                return 1;
            }
            std::string user, pass;
            if (!ParseAuthUserPass(argv[i + 1], user, pass))
            {
                LogError("Bad --auth value. Expected user:pass\n");
                return 1;
            }
            ConfigureHttpAuth(user, pass);
            i++; // consume value
            continue;
        }
        if (std::strcmp(a, "-h") == 0 || std::strcmp(a, "--help") == 0)
        {
            PrintUsage();
            return 0;
        }
        // Unknown flag
        PrintUsage();
        return 1;
    }

    if (i >= argc)
    {
        PrintUsage();
        return 1;
    }

    std::string mode = argv[i];
    if (mode == "server")
    {
        // If this EXE is /SUBSYSTEM:WINDOWS and launched without a parent console,
        // allocate one so the server doesn't look like it "exited".
        EnsureConsoleAllocated();

        if (i + 1 >= argc)
        {
            PrintUsage();
            return 1;
        }

        int port = GetIntArg(argv, i + 1, argc, 8000);
        int fps = GetIntArg(argv, i + 2, argc, 10);
        int quality = GetIntArg(argv, i + 3, argc, 92);

        if (port <= 0 || port > 65535) port = 8000;
        if (fps <= 0) fps = 10;
        if (quality < 1) quality = 1;
        if (quality > 100) quality = 100;

        if (g_serverPrivateRequested && !g_httpAuthEnabled)
        {
            std::string user = "lanscr";
            std::string pass = GenerateRandomPassword(12);
            ConfigureHttpAuth(user, pass);
            LogInfo("Private mode enabled (HTTP Basic Auth).\n");
            LogInfo("Username: %s\n", user.c_str());
            LogInfo("Password: %s\n", pass.c_str());
        }
        else if (!g_serverPrivateRequested)
        {
            // If user used --auth without --private, still keep it enabled (explicit).
        }

        return RunServer((uint16_t)port, fps, quality);
    }
    else if (mode == "client")
    {
        if (i + 1 >= argc)
        {
            PrintUsage();
            return 1;
        }

        std::string url8 = argv[i + 1];
        std::wstring urlW = Utf8ToWide(url8);
        return RunClient(urlW);
    }
    else if (mode == "udp-server")
    {
        if (i + 1 >= argc)
        {
            PrintUsage();
            return 1;
        }
        int port = GetIntArg(argv, i + 1, argc, 9000);
        int fps = GetIntArg(argv, i + 2, argc, 60);
        int quality = GetIntArg(argv, i + 3, argc, 70);
        if (port <= 0 || port > 65535) port = 9000;
        if (fps <= 0) fps = 60;
        if (quality < 1) quality = 1;
        if (quality > 100) quality = 100;
        return RunUdpServer((uint16_t)port, fps, quality);
    }
    else if (mode == "udp-client")
    {
        if (i + 2 >= argc)
        {
            PrintUsage();
            return 1;
        }
        std::string ip = argv[i + 1];
        int port = std::atoi(argv[i + 2]);
        if (port <= 0 || port > 65535)
        {
            LogError("Invalid port.\n");
            return 1;
        }
        return RunUdpClient(ip, (uint16_t)port);
    }
    else if (mode == "audio-mute")
    {
        if (i + 2 >= argc)
        {
            PrintUsage();
            return 1;
        }

        std::string target = argv[i + 1];
        int mute = std::atoi(argv[i + 2]);
        if (mute != 0) mute = 1;

        // If it's a number, treat as local port.
        bool isPort = true;
        for (char c : target) if (!std::isdigit((unsigned char)c)) { isPort = false; break; }

        std::wstring url;
        if (isPort)
        {
            int port = std::atoi(target.c_str());
            wchar_t buf[256];
            swprintf_s(buf, L"http://127.0.0.1:%d/control?mute=%d", port, mute);
            url = buf;
        }
        else
        {
            std::wstring base = Utf8ToWide(target);
            // If user gives server base URL, append /control
            if (!base.empty() && base.back() == L'/') base.pop_back();
            url = base + L"/control?mute=" + std::to_wstring(mute);
        }

        // Simple GET
        URL_COMPONENTS uc{};
        uc.dwStructSize = sizeof(uc);
        std::wstring host(256, L'\0');
        std::wstring path(1024, L'\0');
        uc.lpszHostName = host.data();
        uc.dwHostNameLength = (DWORD)host.size();
        uc.lpszUrlPath = path.data();
        uc.dwUrlPathLength = (DWORD)path.size();
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc))
        {
            LogError("Bad URL\n");
            return 1;
        }
        host.resize(uc.dwHostNameLength);
        path.resize(uc.dwUrlPathLength);
        const bool https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
        HINTERNET hSession = WinHttpOpen(L"lanscr-ctl/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return 1;
        HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), uc.nPort, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return 1; }
        DWORD flags = WINHTTP_FLAG_REFRESH;
        if (https) flags |= WINHTTP_FLAG_SECURE;
        HINTERNET hReq = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hReq) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return 1; }

        WinHttpMaybeAddAuthHeader(hReq);
        BOOL ok = WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(hReq, nullptr);
        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        if (!ok) return 2;
        std::printf("OK\n");
        return 0;
    }
    else if (mode == "stop")
    {
        if (i + 1 >= argc)
        {
            PrintUsage();
            return 1;
        }
        int port = GetIntArg(argv, i + 1, argc, 0);
        if (port <= 0 || port > 65535)
        {
            LogError("Invalid port.\n");
            return 1;
        }
        if (!SignalStopServerOnPort((uint16_t)port))
        {
            LogError("No running server detected on port %d (or access denied).\n", port);
            return 2;
        }
        LogInfo("Stop signal sent to port %d.\n", port);
        return 0;
    }
    else if (mode == "detect")
    {
        // Detect running LANSCR servers.
        // We use the stop-event presence (Local\LANSCR_STOP_<port>) so detection is instant and
        // works even without opening sockets. Scan all ports so we don't miss uncommon ones.
        int found = 0;
        for (uint32_t p = 1; p <= 65535; p++)
        {
            if (IsServerRunningOnPort((uint16_t)p))
            {
                std::printf("%u\n", (unsigned)p);
                found++;
            }
        }
        if (found == 0) std::printf("No running servers detected.\n");
        return 0;
    }

    PrintUsage();
    return 1;
}

enum
{
    IDC_PORT = 1001,
    IDC_FPS = 1002,
    IDC_QUALITY = 1003,
    IDC_URL = 1004,

    IDC_START_SERVER = 1101,
    IDC_STOP_SERVER = 1102,
    IDC_OPEN_BROWSER = 1103,
    IDC_OPEN_CLIENT = 1104,
    IDC_EXIT = 1105,

    IDC_SERVER_MUTE = 1110,
    IDC_CLIENT_MUTE = 1111,

    IDC_PRIVATE = 1112,

    IDC_LINK_SITE = 1201,
    IDC_LINK_PORTFOLIO = 1202,
    IDC_LINK_GITHUB = 1203,
    IDC_LINK_INSTAGRAM = 1204,
    IDC_LINK_LINKEDIN = 1205,
    IDC_LINK_YOUTUBE = 1206,
};

static bool SendServerMuteToLocalPort(uint16_t port, bool mute)
{
    wchar_t buf[256];
    swprintf_s(buf, L"http://127.0.0.1:%u/control?mute=%d", (unsigned)port, mute ? 1 : 0);
    return HttpGetSimpleWinHttp(buf, nullptr);
}

static constexpr const wchar_t* kUrlSite = L"https://info.likhil.42web.io/lanscr";
static constexpr const wchar_t* kUrlPortfolio = L"https://likhil.42web.io";
static constexpr const wchar_t* kUrlInstagram = L"https://instagram.com/pidugulikhil";
static constexpr const wchar_t* kUrlLinkedIn = L"https://linkedin.com/in/pidugulikhil";
static constexpr const wchar_t* kUrlGitHub = L"https://github.com/pidugulikhil";
static constexpr const wchar_t* kUrlYouTube = L"https://www.youtube.com/@pidugulikhil";

static int ReadIntFromEdit(HWND hEdit, int def)
{
    wchar_t buf[64] = {};
    GetWindowTextW(hEdit, buf, (int)(sizeof(buf) / sizeof(buf[0])));
    int v = _wtoi(buf);
    return v == 0 ? def : v;
}

static std::wstring ReadTextFromEdit(HWND hEdit, const wchar_t* def)
{
    wchar_t buf[2048] = {};
    GetWindowTextW(hEdit, buf, (int)(sizeof(buf) / sizeof(buf[0])));
    if (buf[0] == 0) return def ? std::wstring(def) : std::wstring();
    return std::wstring(buf);
}

static std::wstring GetSelfExePath()
{
    wchar_t path[MAX_PATH] = {};
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"";
    return std::wstring(path);
}

static bool LaunchSelfProcess(const std::wstring& args)
{
    std::wstring exe = GetSelfExePath();
    if (exe.empty()) return false;

    // CreateProcess requires a mutable command line buffer.
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    BOOL ok = CreateProcessW(
        nullptr,
        mutableCmd.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        nullptr,
        &si,
        &pi);

    if (!ok) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

static void SetControlFont(HWND hwnd, HFONT font)
{
    if (!hwnd || !font) return;
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)font, TRUE);
}

static HFONT CreateUiFont()
{
    // Segoe UI is the default modern Windows UI font.
    HDC hdc = GetDC(nullptr);
    int dpiY = hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
    if (hdc) ReleaseDC(nullptr, hdc);

    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(10, dpiY, 72);
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&lf);
}

static void ApplyRoundedCorners(HWND hwnd, int radius)
{
    RECT rc{};
    GetWindowRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    HRGN rgn = CreateRoundRectRgn(0, 0, w + 1, h + 1, radius, radius);
    SetWindowRgn(hwnd, rgn, TRUE);
    // Region is owned by the window after SetWindowRgn.
}

static void CloseLinksPopup();

static LRESULT CALLBACK LinksPopupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        const int pad = 10;
        const int btnH = 34;
        RECT rc{};
        GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left;

        auto mk = [&](int id, const wchar_t* text, int y) {
            HWND b = CreateWindowW(L"BUTTON", text,
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                pad, y, w - pad * 2, btnH,
                hwnd, (HMENU)(INT_PTR)id, nullptr, nullptr);
            if (g_uiFont) SetControlFont(b, g_uiFont);
        };

        int y = pad;
        mk(IDC_LINK_SITE, L"Website", y); y += btnH + 8;
        mk(IDC_LINK_PORTFOLIO, L"Portfolio", y); y += btnH + 8;
        mk(IDC_LINK_GITHUB, L"GitHub", y); y += btnH + 8;
        mk(IDC_LINK_LINKEDIN, L"LinkedIn", y); y += btnH + 8;
        mk(IDC_LINK_INSTAGRAM, L"Instagram", y); y += btnH + 8;
        mk(IDC_LINK_YOUTUBE, L"YouTube", y);

        ApplyRoundedCorners(hwnd, 14);
        return 0;
    }

    case WM_ERASEBKGND:
    {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH br = CreateSolidBrush(RGB(248, 249, 252));
        FillRect(hdc, &rc, br);
        DeleteObject(br);
        return 1;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH br = CreateSolidBrush(RGB(248, 249, 252));
        FillRect(hdc, &rc, br);
        DeleteObject(br);

        HPEN pen = CreatePen(PS_SOLID, 1, RGB(210, 215, 225));
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
        RoundRect(hdc, 0, 0, rc.right, rc.bottom, 14, 14);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        TRACKMOUSEEVENT tme{};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);

        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        HWND child = ChildWindowFromPointEx(hwnd, pt, CWP_SKIPINVISIBLE);
        int id = child ? GetDlgCtrlID(child) : 0;
        if (id != g_popupHoveredId)
        {
            g_popupHoveredId = id;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        g_popupHoveredId = 0;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (!dis || dis->CtlType != ODT_BUTTON) break;

        int id = (int)dis->CtlID;
        bool hot = (id != 0 && id == g_popupHoveredId);
        bool pressed = (dis->itemState & ODS_SELECTED) != 0;

        COLORREF bg = RGB(248, 249, 252);
        COLORREF fg = RGB(35, 38, 45);
        COLORREF border = RGB(220, 224, 232);
        if (hot) { bg = RGB(232, 241, 255); border = RGB(170, 200, 255); }
        if (pressed) { bg = RGB(214, 231, 255); border = RGB(140, 185, 255); }

        HBRUSH br = CreateSolidBrush(bg);
        FillRect(dis->hDC, &dis->rcItem, br);
        DeleteObject(br);

        HPEN pen = CreatePen(PS_SOLID, 1, border);
        HGDIOBJ oldPen = SelectObject(dis->hDC, pen);
        HGDIOBJ oldBrush = SelectObject(dis->hDC, GetStockObject(HOLLOW_BRUSH));
        RoundRect(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom, 10, 10);
        SelectObject(dis->hDC, oldBrush);
        SelectObject(dis->hDC, oldPen);
        DeleteObject(pen);

        wchar_t text[128] = {};
        GetWindowTextW(dis->hwndItem, text, (int)(sizeof(text) / sizeof(text[0])));
        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, fg);
        RECT tr = dis->rcItem;
        tr.left += 12;
        DrawTextW(dis->hDC, text, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_LINK_SITE:
            (void)ShellExecuteW(hwnd, L"open", kUrlSite, nullptr, nullptr, SW_SHOWNORMAL);
            CloseLinksPopup();
            return 0;
        case IDC_LINK_PORTFOLIO:
            (void)ShellExecuteW(hwnd, L"open", kUrlPortfolio, nullptr, nullptr, SW_SHOWNORMAL);
            CloseLinksPopup();
            return 0;
        case IDC_LINK_GITHUB:
            (void)ShellExecuteW(hwnd, L"open", kUrlGitHub, nullptr, nullptr, SW_SHOWNORMAL);
            CloseLinksPopup();
            return 0;
        case IDC_LINK_INSTAGRAM:
            (void)ShellExecuteW(hwnd, L"open", kUrlInstagram, nullptr, nullptr, SW_SHOWNORMAL);
            CloseLinksPopup();
            return 0;
        case IDC_LINK_LINKEDIN:
            (void)ShellExecuteW(hwnd, L"open", kUrlLinkedIn, nullptr, nullptr, SW_SHOWNORMAL);
            CloseLinksPopup();
            return 0;
        case IDC_LINK_YOUTUBE:
            (void)ShellExecuteW(hwnd, L"open", kUrlYouTube, nullptr, nullptr, SW_SHOWNORMAL);
            CloseLinksPopup();
            return 0;
        }
        break;

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE)
        {
            CloseLinksPopup();
            return 0;
        }
        break;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            CloseLinksPopup();
            return 0;
        }
        break;

    case WM_DESTROY:
        if (g_linksPopup == hwnd) g_linksPopup = nullptr;
        g_popupHoveredId = 0;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void EnsureLinksPopupClassRegistered()
{
    static bool registered = false;
    if (registered) return;

    WNDCLASSW wc{};
    wc.lpfnWndProc = LinksPopupWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"LanscrLinksPopup";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.style = CS_DROPSHADOW;
    RegisterClassW(&wc);
    registered = true;
}

static void CloseLinksPopup()
{
    if (g_linksPopup)
    {
        DestroyWindow(g_linksPopup);
        g_linksPopup = nullptr;
    }
}

static void ToggleLinksPopup(HWND owner)
{
    if (!g_btnLinks) return;
    if (g_linksPopup)
    {
        CloseLinksPopup();
        return;
    }

    EnsureLinksPopupClassRegistered();

    RECT br{};
    GetWindowRect(g_btnLinks, &br);

    const int popupW = 300;
    const int popupH = 6 * 34 + 5 * 8 + 20;
    int x = br.left;
    int y = br.bottom + 8;

    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    if (x + popupW > wa.right) x = wa.right - popupW;
    if (y + popupH > wa.bottom) y = br.top - popupH - 8;
    if (x < wa.left) x = wa.left;
    if (y < wa.top) y = wa.top;

    g_linksPopup = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        L"LanscrLinksPopup",
        L"",
        WS_POPUP,
        x, y, popupW, popupH,
        owner,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    if (!g_linksPopup) return;

    ShowWindow(g_linksPopup, SW_SHOWNORMAL);
    UpdateWindow(g_linksPopup);
    AnimateWindow(g_linksPopup, 140, AW_BLEND | AW_SLIDE | AW_VER_POSITIVE);
    SetForegroundWindow(g_linksPopup);
}

static void UpdateServerButtons(bool running)
{
    if (g_btnStartServer) EnableWindow(g_btnStartServer, running ? FALSE : TRUE);
    if (g_btnStopServer) EnableWindow(g_btnStopServer, running ? TRUE : FALSE);
}

static void UpdateServerButtonsFromPort()
{
    if (!g_editPort) return;

    wchar_t buf[64] = {};
    GetWindowTextW(g_editPort, buf, (int)(sizeof(buf) / sizeof(buf[0])));
    int port = _wtoi(buf);
    if (port <= 0 || port > 65535) port = 8000;

    if (g_serverThreadActive.load())
    {
        if (g_btnStopServer) SetWindowTextW(g_btnStopServer, L"Stop");
        UpdateServerButtons(true);
        return;
    }

    if (IsServerRunningOnPort((uint16_t)port))
    {
        if (g_btnStopServer) SetWindowTextW(g_btnStopServer, L"Stop Server");
        if (g_btnStartServer) EnableWindow(g_btnStartServer, FALSE);
        if (g_btnStopServer) EnableWindow(g_btnStopServer, TRUE);
        return;
    }

    if (g_btnStopServer) SetWindowTextW(g_btnStopServer, L"Stop");
    UpdateServerButtons(false);
}

static LRESULT CALLBACK LauncherWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_launcherHwnd = hwnd;

        if (!g_uiFont) g_uiFont = CreateUiFont();
        HFONT font = g_uiFont ? g_uiFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        CreateWindowW(L"STATIC", L"Server", WS_CHILD | WS_VISIBLE, 12, 10, 80, 18, hwnd, nullptr, nullptr, nullptr);
        CreateWindowW(L"STATIC", L"Port:", WS_CHILD | WS_VISIBLE, 12, 34, 60, 18, hwnd, nullptr, nullptr, nullptr);
        g_editPort = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"8000", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 70, 30, 90, 22, hwnd, (HMENU)IDC_PORT, nullptr, nullptr);

        CreateWindowW(L"STATIC", L"FPS:", WS_CHILD | WS_VISIBLE, 180, 34, 60, 18, hwnd, nullptr, nullptr, nullptr);
        g_editFps = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"10", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 220, 30, 70, 22, hwnd, (HMENU)IDC_FPS, nullptr, nullptr);

        CreateWindowW(L"STATIC", L"Quality (1-100):", WS_CHILD | WS_VISIBLE, 310, 34, 110, 18, hwnd, nullptr, nullptr, nullptr);
        g_editQuality = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"92", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 420, 30, 70, 22, hwnd, (HMENU)IDC_QUALITY, nullptr, nullptr);

        g_btnStartServer = CreateWindowW(L"BUTTON", L"Start Server", WS_CHILD | WS_VISIBLE, 12, 62, 120, 28, hwnd, (HMENU)IDC_START_SERVER, nullptr, nullptr);
        g_btnStopServer = CreateWindowW(L"BUTTON", L"Stop", WS_CHILD | WS_VISIBLE, 142, 62, 80, 28, hwnd, (HMENU)IDC_STOP_SERVER, nullptr, nullptr);
        HWND btnOpenBrowser = CreateWindowW(L"BUTTON", L"Open Browser", WS_CHILD | WS_VISIBLE, 232, 62, 120, 28, hwnd, (HMENU)IDC_OPEN_BROWSER, nullptr, nullptr);
        g_chkServerMute = CreateWindowW(L"BUTTON", L"Mute server audio", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 362, 66, 128, 22, hwnd, (HMENU)IDC_SERVER_MUTE, nullptr, nullptr);
        SendMessageW(g_chkServerMute, BM_SETCHECK, g_serverAudioMuted.load() ? BST_CHECKED : BST_UNCHECKED, 0);
    g_chkPrivate = CreateWindowW(L"BUTTON", L"Private mode (password)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 362, 88, 150, 22, hwnd, (HMENU)IDC_PRIVATE, nullptr, nullptr);
    SendMessageW(g_chkPrivate, BM_SETCHECK, g_httpAuthEnabled ? BST_CHECKED : BST_UNCHECKED, 0);

        CreateWindowW(L"STATIC", L"Client", WS_CHILD | WS_VISIBLE, 12, 106, 80, 18, hwnd, nullptr, nullptr, nullptr);
        CreateWindowW(L"STATIC", L"URL:", WS_CHILD | WS_VISIBLE, 12, 130, 40, 18, hwnd, nullptr, nullptr, nullptr);
        g_editUrl = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"http://127.0.0.1:8000/", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 52, 126, 438, 22, hwnd, (HMENU)IDC_URL, nullptr, nullptr);
        HWND btnOpenClient = CreateWindowW(L"BUTTON", L"Open Client Viewer", WS_CHILD | WS_VISIBLE, 12, 156, 160, 28, hwnd, (HMENU)IDC_OPEN_CLIENT, nullptr, nullptr);
        g_chkClientMute = CreateWindowW(L"BUTTON", L"Mute client audio", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 182, 160, 140, 22, hwnd, (HMENU)IDC_CLIENT_MUTE, nullptr, nullptr);
        SendMessageW(g_chkClientMute, BM_SETCHECK, g_clientAudioMuted.load() ? BST_CHECKED : BST_UNCHECKED, 0);

        // Social links (hover-card popup)
        CreateWindowW(L"STATIC", L"Social", WS_CHILD | WS_VISIBLE, 12, 196, 80, 18, hwnd, nullptr, nullptr, nullptr);
        g_btnLinks = CreateWindowW(L"BUTTON", L"Open Links", WS_CHILD | WS_VISIBLE, 12, 218, 140, 30, hwnd, (HMENU)1200, nullptr, nullptr);

        // Server detector
        CreateWindowW(L"STATIC", L"Detect running servers", WS_CHILD | WS_VISIBLE, 12, 286, 180, 18, hwnd, nullptr, nullptr, nullptr);
        g_btnDetectServers = CreateWindowW(L"BUTTON", L"Detect", WS_CHILD | WS_VISIBLE, 12, 308, 100, 26, hwnd, (HMENU)1301, nullptr, nullptr);
        g_btnStopSelected = CreateWindowW(L"BUTTON", L"Stop Selected", WS_CHILD | WS_VISIBLE, 120, 308, 130, 26, hwnd, (HMENU)1302, nullptr, nullptr);
        HWND btnStopAll = CreateWindowW(L"BUTTON", L"Stop All", WS_CHILD | WS_VISIBLE, 12, 338, 100, 26, hwnd, (HMENU)1304, nullptr, nullptr);
        g_listServers = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY, 260, 286, 230, 78, hwnd, (HMENU)1303, nullptr, nullptr);

        CreateWindowW(L"STATIC", L"Log", WS_CHILD | WS_VISIBLE, 12, 372, 80, 18, hwnd, nullptr, nullptr, nullptr);
        g_launcherLog = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
            12, 392, 478, 200,
            hwnd,
            nullptr,
            nullptr,
            nullptr);

        HWND btnExit = CreateWindowW(L"BUTTON", L"Exit", WS_CHILD | WS_VISIBLE, 400, 156, 90, 28, hwnd, (HMENU)IDC_EXIT, nullptr, nullptr);

        // Apply font
        SetControlFont(g_editPort, font);
        SetControlFont(g_editFps, font);
        SetControlFont(g_editQuality, font);
        SetControlFont(g_editUrl, font);
        SetControlFont(g_btnStartServer, font);
        SetControlFont(g_btnStopServer, font);
        SetControlFont(btnOpenBrowser, font);
        SetControlFont(g_chkServerMute, font);
            SetControlFont(g_chkPrivate, font);
        SetControlFont(btnOpenClient, font);
        SetControlFont(g_chkClientMute, font);
        SetControlFont(g_launcherLog, font);
        SetControlFont(btnExit, font);
        SetControlFont(g_btnLinks, font);
        SetControlFont(g_btnDetectServers, font);
        SetControlFont(g_btnStopSelected, font);
        SetControlFont(g_listServers, font);
        SetControlFont(btnStopAll, font);

        UpdateServerButtonsFromPort();
        // Periodically refresh so we detect servers started from another instance/CLI.
        SetTimer(hwnd, 2, 1000, nullptr);
        GuiAppendText(g_launcherLog, L"Double-click UI mode. CLI still supported.\r\n");
        GuiAppendText(g_launcherLog, L"Examples: LANSCR.exe server 8000 10 92 | LANSCR.exe client http://ip:8000/\r\n");
        return 0;
    }

    case WM_TIMER:
        if (wParam == 1)
        {
            UpdateServerButtonsFromPort();
            // Stop polling once Start becomes available again.
            if (g_btnStartServer && IsWindowEnabled(g_btnStartServer))
            {
                KillTimer(hwnd, 1);
            }
        }
        else if (wParam == 2)
        {
            UpdateServerButtonsFromPort();
        }
        return 0;

    case WM_ERASEBKGND:
    {
        // Reduce black flicker on minimize/restore.
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));
        return 1;
    }

    case WM_GUI_LOG:
    {
        wchar_t* heap = (wchar_t*)lParam;
        if (heap)
        {
            GuiAppendText(g_launcherLog, heap);
            // Ensure a newline if caller didn't include one.
            size_t n = wcslen(heap);
            if (n > 0 && heap[n - 1] != L'\n') GuiAppendText(g_launcherLog, L"\r\n");
            std::free(heap);
        }
        return 0;
    }

    case WM_FOUND_SERVER:
    {
        uint16_t port = (uint16_t)wParam;
        if (g_listServers)
        {
            wchar_t item[32];
            swprintf_s(item, L"%u", (unsigned)port);

            // Avoid duplicates
            int count = (int)SendMessageW(g_listServers, LB_GETCOUNT, 0, 0);
            for (int i = 0; i < count; i++)
            {
                wchar_t existing[32] = {};
                SendMessageW(g_listServers, LB_GETTEXT, (WPARAM)i, (LPARAM)existing);
                if (wcscmp(existing, item) == 0) return 0;
            }
            SendMessageW(g_listServers, LB_ADDSTRING, 0, (LPARAM)item);
        }
        return 0;
    }

    case WM_SCAN_DONE:
        g_scanRunning.store(false);
        if (g_btnDetectServers) EnableWindow(g_btnDetectServers, TRUE);
        GuiAppendText(g_launcherLog, L"Server scan done.\r\n");
        return 0;

    case WM_SERVER_STOPPED:
    {
        if (g_serverThread.joinable()) g_serverThread.join();
        g_serverThreadActive.store(false);
        UpdateServerButtonsFromPort();
        GuiAppendText(g_launcherLog, L"Server stopped.\r\n");
        return 0;
    }

    case WM_COMMAND:
    {
        // Live refresh when port field changes
        if (HIWORD(wParam) == EN_CHANGE && LOWORD(wParam) == IDC_PORT)
        {
            UpdateServerButtonsFromPort();
            return 0;
        }

        switch (LOWORD(wParam))
        {
        case IDC_SERVER_MUTE:
        {
            bool mute = false;
            if (g_chkServerMute)
            {
                mute = (SendMessageW(g_chkServerMute, BM_GETCHECK, 0, 0) == BST_CHECKED);
            }
            g_serverAudioMuted.store(mute);

            int port = ReadIntFromEdit(g_editPort, 8000);
            if (port <= 0 || port > 65535) port = 8000;

            const bool inProc = g_serverThreadActive.load() && g_serverPort.load() == (uint16_t)port;
            if (!inProc && IsServerRunningOnPort((uint16_t)port))
            {
                if (SendServerMuteToLocalPort((uint16_t)port, mute))
                {
                    GuiAppendText(g_launcherLog, mute ? L"Muted server audio on selected port.\r\n" : L"Unmuted server audio on selected port.\r\n");
                }
                else
                {
                    GuiAppendText(g_launcherLog, L"Failed to send mute control to that server.\r\n");
                }
            }
            else if (!inProc)
            {
                GuiAppendText(g_launcherLog, L"No running server on that port; mute will apply when you start.\r\n");
            }
            return 0;
        }

        case IDC_CLIENT_MUTE:
        {
            // Only affects newly launched client viewers.
            bool mute = false;
            if (g_chkClientMute)
            {
                mute = (SendMessageW(g_chkClientMute, BM_GETCHECK, 0, 0) == BST_CHECKED);
            }
            g_clientAudioMuted.store(mute);
            return 0;
        }

        case IDC_PRIVATE:
        {
            // Only affects in-process server starts + in-process client launches.
            const bool wantPrivate = g_chkPrivate && (SendMessageW(g_chkPrivate, BM_GETCHECK, 0, 0) == BST_CHECKED);
            if (!wantPrivate)
            {
                ClearHttpAuth();
                GuiAppendText(g_launcherLog, L"Private mode disabled (no password).\r\n");
                return 0;
            }

            // Checked: ask user Automatic vs Manual and apply credentials.
            std::string user;
            std::string pass;
            if (!ShowPrivateModeDialog(hwnd, user, pass))
            {
                // User canceled: revert checkbox and keep previous auth state.
                SendMessageW(g_chkPrivate, BM_SETCHECK, g_httpAuthEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
                GuiAppendText(g_launcherLog, L"Private mode dialog canceled.\r\n");
                return 0;
            }

            ConfigureHttpAuth(user, pass);
            GuiAppendText(g_launcherLog, L"Private mode enabled (HTTP Basic Auth).\r\n");
            GuiAppendText(g_launcherLog, (L"Username: " + Utf8ToWide(user) + L"\r\n").c_str());
            GuiAppendText(g_launcherLog, (L"Password: " + Utf8ToWide(pass) + L"\r\n").c_str());
            return 0;
        }

        case 1200: // Links popup
            ToggleLinksPopup(hwnd);
            return 0;

        case 1301: // Detect
        {
            if (g_scanRunning.load()) return 0;
            if (g_listServers) SendMessageW(g_listServers, LB_RESETCONTENT, 0, 0);
            GuiAppendText(g_launcherLog, L"Scanning for running servers...\r\n");
            g_scanRunning.store(true);
            if (g_btnDetectServers) EnableWindow(g_btnDetectServers, FALSE);
            std::thread(ServerScanThread).detach();
            return 0;
        }

        case 1302: // Stop Selected
        {
            if (!g_listServers)
            {
                GuiAppendText(g_launcherLog, L"No listbox.\r\n");
                return 0;
            }

            int sel = (int)SendMessageW(g_listServers, LB_GETCURSEL, 0, 0);
            if (sel == LB_ERR)
            {
                GuiAppendText(g_launcherLog, L"Select a port from the list first.\r\n");
                return 0;
            }

            wchar_t buf[32] = {};
            SendMessageW(g_listServers, LB_GETTEXT, (WPARAM)sel, (LPARAM)buf);
            int port = _wtoi(buf);
            if (port <= 0 || port > 65535)
            {
                GuiAppendText(g_launcherLog, L"Invalid selected port.\r\n");
                return 0;
            }

            if (!SignalStopServerOnPort((uint16_t)port))
            {
                GuiAppendText(g_launcherLog, L"Failed to stop server on that port.\r\n");
                return 0;
            }

            GuiAppendText(g_launcherLog, L"Stop signal sent to selected port.\r\n");
            SetTimer(hwnd, 1, 500, nullptr);
            return 0;
        }

        case 1304: // Stop All
        {
            if (!g_listServers)
            {
                GuiAppendText(g_launcherLog, L"No listbox.\r\n");
                return 0;
            }
            int count = (int)SendMessageW(g_listServers, LB_GETCOUNT, 0, 0);
            if (count <= 0)
            {
                GuiAppendText(g_launcherLog, L"List is empty. Run Detect first.\r\n");
                return 0;
            }
            int stopped = 0;
            for (int idx = 0; idx < count; idx++)
            {
                wchar_t buf[32] = {};
                SendMessageW(g_listServers, LB_GETTEXT, (WPARAM)idx, (LPARAM)buf);
                int port = _wtoi(buf);
                if (port > 0 && port <= 65535)
                {
                    if (SignalStopServerOnPort((uint16_t)port)) stopped++;
                }
            }
            wchar_t msgBuf[128];
            swprintf_s(msgBuf, L"Stop signal sent for %d ports.\r\n", stopped);
            GuiAppendText(g_launcherLog, msgBuf);
            SetTimer(hwnd, 1, 500, nullptr);
            return 0;
        }
        case IDC_START_SERVER:
        {
            if (g_serverThreadActive.load()) return 0;

            if (g_chkServerMute)
            {
                g_serverAudioMuted.store(SendMessageW(g_chkServerMute, BM_GETCHECK, 0, 0) == BST_CHECKED);
            }

            int port = ReadIntFromEdit(g_editPort, 8000);
            int fps = ReadIntFromEdit(g_editFps, 10);
            int quality = ReadIntFromEdit(g_editQuality, 92);
            const bool wantPrivate = g_chkPrivate && (SendMessageW(g_chkPrivate, BM_GETCHECK, 0, 0) == BST_CHECKED);
            if (!wantPrivate)
            {
                ClearHttpAuth();
            }
            else if (!g_httpAuthEnabled)
            {
                std::string user = "lanscr";
                std::string pass = GenerateRandomPassword(12);
                ConfigureHttpAuth(user, pass);
                GuiAppendText(g_launcherLog, L"Private mode enabled (HTTP Basic Auth).\r\n");
                GuiAppendText(g_launcherLog, (L"Username: " + Utf8ToWide(user) + L"\r\n").c_str());
                GuiAppendText(g_launcherLog, (L"Password: " + Utf8ToWide(pass) + L"\r\n").c_str());
            }

            if (port <= 0 || port > 65535) port = 8000;
            if (fps <= 0) fps = 10;
            if (quality < 1) quality = 1;
            if (quality > 100) quality = 100;

            // If another instance (or CLI) is already running a server on this port, don't start.
            if (IsServerRunningOnPort((uint16_t)port))
            {
                GuiAppendText(g_launcherLog, L"A server is already running on this port. Use Stop Server.\r\n");
                UpdateServerButtonsFromPort();
                return 0;
            }

            // Sync edits back (sanitized values)
            {
                wchar_t tmp[64];
                swprintf_s(tmp, L"%d", port);
                SetWindowTextW(g_editPort, tmp);
                swprintf_s(tmp, L"%d", fps);
                SetWindowTextW(g_editFps, tmp);
                swprintf_s(tmp, L"%d", quality);
                SetWindowTextW(g_editQuality, tmp);
                wchar_t url[256];
                swprintf_s(url, L"http://127.0.0.1:%d/", port);
                SetWindowTextW(g_editUrl, url);
            }

            g_running.store(true);
            g_serverThreadActive.store(true);
            g_serverPort.store((uint16_t)port);
            UpdateServerButtons(true);

            GuiAppendText(g_launcherLog, L"Starting server...\r\n");

            g_serverThread = std::thread([port, fps, quality]() {
                int rc = RunServer((uint16_t)port, fps, quality);
                if (g_launcherHwnd) PostMessageW(g_launcherHwnd, WM_SERVER_STOPPED, (WPARAM)rc, 0);
            });
            return 0;
        }

        case IDC_STOP_SERVER:
        {
            int port = ReadIntFromEdit(g_editPort, 8000);
            if (port <= 0 || port > 65535) port = 8000;

            // Stop whichever server is running on the selected port.
            if (!SignalStopServerOnPort((uint16_t)port))
            {
                GuiAppendText(g_launcherLog, L"No running server detected on that port.\r\n");
                UpdateServerButtonsFromPort();
                return 0;
            }

            GuiAppendText(g_launcherLog, L"Stop signal sent. Waiting...\r\n");
            // Only flip g_running for the in-process server thread.
            if (g_serverThreadActive.load() && g_serverPort.load() == (uint16_t)port)
            {
                g_running.store(false);
            }
            if (g_btnStopServer) EnableWindow(g_btnStopServer, FALSE);

            // Poll briefly so the UI updates when the other process exits.
            SetTimer(hwnd, 1, 500, nullptr);
            return 0;
        }

        case IDC_OPEN_BROWSER:
        {
            int port = ReadIntFromEdit(g_editPort, 8000);
            if (port <= 0 || port > 65535) port = 8000;
            wchar_t url[256];
            swprintf_s(url, L"http://127.0.0.1:%d/", port);
            (void)ShellExecuteW(hwnd, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }

        case IDC_OPEN_CLIENT:
        {
            std::wstring url = ReadTextFromEdit(g_editUrl, L"http://127.0.0.1:8000/");
            // Launch a new instance of this same EXE in CLI mode: keeps launcher responsive.
            bool mute = false;
            if (g_chkClientMute)
            {
                mute = (SendMessageW(g_chkClientMute, BM_GETCHECK, 0, 0) == BST_CHECKED);
            }
            std::wstring prefix;
            const bool wantPrivate = g_chkPrivate && (SendMessageW(g_chkPrivate, BM_GETCHECK, 0, 0) == BST_CHECKED);
            if (wantPrivate && g_httpAuthEnabled && !g_httpAuthUser.empty() && !g_httpAuthPass.empty())
            {
                std::string up = g_httpAuthUser + ":" + g_httpAuthPass;
                prefix = L"--auth " + Utf8ToWide(up) + L" ";
            }
            if (mute) prefix += L"--mute ";
            std::wstring args = prefix + L"client \"" + url + L"\"";
            if (!LaunchSelfProcess(args))
            {
                GuiAppendText(g_launcherLog, L"Failed to launch client process.\r\n");
            }
            return 0;
        }

        case IDC_EXIT:
            DestroyWindow(hwnd);
            return 0;
        }
        return 0;
    }

    case WM_CLOSE:
        CloseLinksPopup();
        if (g_serverThreadActive.load())
        {
            g_running.store(false);
            if (g_serverThread.joinable()) g_serverThread.join();
            g_serverThreadActive.store(false);
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        g_scanRunning.store(false);
        CloseLinksPopup();
        g_launcherHwnd = nullptr;
        g_launcherLog = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static int RunLauncherGui()
{
    // Make the UI crisp on high-DPI displays.
    if (HMODULE user32 = LoadLibraryW(L"user32.dll"))
    {
        using Fn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto fn = (Fn)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (fn) (void)fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        FreeLibrary(user32);
    }

    HINSTANCE hInst = GetModuleHandleW(nullptr);
    const wchar_t* cls = L"LanLauncher";

    WNDCLASSW wc{};
    wc.lpfnWndProc = LauncherWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = cls;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);

    if (!RegisterClassW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(
        0,
        cls,
        L"LAN Screen Share (Launcher)",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        520, 650,
        nullptr, nullptr, hInst, nullptr);

    if (!hwnd) return 1;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

int main(int argc, char** argv)
{
    // With /SUBSYSTEM:CONSOLE, shells will wait for this process.
    // If user double-clicks (argc==1), we run GUI and detach the console.
    if (argc <= 1)
    {
        DetachConsoleForGui();
        return RunLauncherGui();
    }
    return RunCli(argc, argv);
}

// For /SUBSYSTEM:WINDOWS builds: show GUI when no args, otherwise run CLI.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int argcW = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argcW);
    if (!argvW || argcW <= 0)
    {
        if (argvW) LocalFree(argvW);
        return RunLauncherGui();
    }

    if (argcW > 1)
    {
        EnsureConsoleForCli();

        std::vector<std::string> args8;
        args8.reserve((size_t)argcW);
        for (int i = 0; i < argcW; i++)
        {
            args8.push_back(WideToUtf8(std::wstring(argvW[i])));
        }

        std::vector<char*> argv8;
        argv8.reserve((size_t)argcW + 1);
        for (int i = 0; i < argcW; i++) argv8.push_back(args8[i].data());
        argv8.push_back(nullptr);

        LocalFree(argvW);
        return RunCli(argcW, argv8.data());
    }

    LocalFree(argvW);
    return RunLauncherGui();
}
