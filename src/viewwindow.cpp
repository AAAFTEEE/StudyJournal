// ============================================================
//  viewwindow.cpp — 日记查看窗口（纯文本）
//  只读显示该条（或当天全部）日记的原始文本，不再做 Markdown 渲染。
// ============================================================
#include "app.h"

#ifndef EM_SETTEXTMODE
#define EM_SETTEXTMODE (WM_USER + 89)
#endif
#ifndef TM_PLAINTEXT
#define TM_PLAINTEXT 1
#endif

static HWND g_view = nullptr;
static HWND g_viewText = nullptr;
static HFONT g_viewFont = nullptr;
static wstring g_viewDate;
static wstring g_viewBase;
static const wchar_t* VIEW_CLASS = L"StudyJournalViewWnd";
enum { VID_EDIT = 1, VID_CLOSE = 2 };

static void applyViewFont() {
    if (!g_viewText) return;
    if (!g_viewFont)
        g_viewFont = CreateFontW(-S(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                 L"Microsoft YaHei UI");
    SendMessageW(g_viewText, WM_SETFONT, (WPARAM)g_viewFont, TRUE);
    SendMessageW(g_viewText, EM_SETBKGNDCOLOR, 0, (LPARAM)g_th.card);

    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR;
    cf.yHeight = S(15) * 15;
    cf.crTextColor = g_th.textPrimary;
    wcscpy(cf.szFaceName, L"Microsoft YaHei UI");
    SendMessageW(g_viewText, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
}

static void layoutView(HWND h) {
    RECT rc; GetClientRect(h, &rc);
    int bh = S(50);
    SetWindowPos(g_viewText, nullptr, S(12), S(12),
                 rc.right - S(24), max(S(80), (int)(rc.bottom - bh - S(18))), SWP_NOZORDER);
    SetWindowPos(GetDlgItem(h, VID_EDIT), nullptr,
                 rc.right - S(12) - S(200), rc.bottom - bh, S(92), S(34), SWP_NOZORDER);
    SetWindowPos(GetDlgItem(h, VID_CLOSE), nullptr,
                 rc.right - S(12) - S(96), rc.bottom - bh, S(84), S(34), SWP_NOZORDER);
}

static LRESULT CALLBACK ViewProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        g_viewText = CreateWindowExW(WS_EX_CLIENTEDGE, L"RichEdit20W", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
            0, 0, 0, 0, h, nullptr, g_hInst, nullptr);
        SendMessageW(g_viewText, EM_SETTEXTMODE, TM_PLAINTEXT, 0);
        applyViewFont();
        HWND e = CreateWindowW(L"BUTTON", L"编辑", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 0, 0, h, (HMENU)(INT_PTR)VID_EDIT, g_hInst, nullptr);
        regBtn(e, BK_PRIMARY);
        HWND c = CreateWindowW(L"BUTTON", L"关闭", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 0, 0, h, (HMENU)(INT_PTR)VID_CLOSE, g_hInst, nullptr);
        regBtn(c, BK_SECONDARY);
        for (HWND x : { e, c }) SendMessageW(x, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        break;
    }
    case WM_SIZE: layoutView(h); return 0;
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)w; RECT rc; GetClientRect(h, &rc);
        HBRUSH hb = CreateSolidBrush(g_th.bg); FillRect(hdc, &rc, hb); DeleteObject(hb);
        return 1;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)l;
        if (g_btns.count(di->hwndItem)) { drawButton(di); return TRUE; }
        break;
    }
    case WM_SJ_THEME:
        applyViewFont();
        InvalidateRect(h, nullptr, TRUE);
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == VID_EDIT) {
            wstring ds = g_viewDate;
            wstring base = g_viewBase;
            DestroyWindow(h);
            openEntryEditor(ds, false, base);
        } else if (id == VID_CLOSE) {
            DestroyWindow(h);
        }
        return 0;
    }
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY:
        if (g_viewFont) { DeleteObject(g_viewFont); g_viewFont = nullptr; }
        g_view = nullptr; g_viewText = nullptr;
        break;
    default: return DefWindowProcW(h, m, w, l);
    }
    return 0;
}

void openViewWindow(int y, int m, int d, const wstring& base) {
    int firstIdx = -1;
    bool single = !base.empty();
    wstring singleText;
    vector<wstring> texts;
    for (size_t i = 0; i < g_entries.size(); i++) {
        auto& e = g_entries[i];
        if (e.year == y && e.mon == m && e.day == d) {
            if (single) {
                if (e.base == base) { firstIdx = (int)i; singleText = e.text; break; }
                continue;
            }
            if (firstIdx < 0) firstIdx = (int)i;
            texts.push_back(e.text);
        }
    }
    if (firstIdx < 0) return;
    auto& first = g_entries[firstIdx];

    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc); wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = ViewProc; wc.hInstance = g_hInst;
        wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        wc.lpszClassName = VIEW_CLASS;
        RegisterClassExW(&wc); reg = true;
    }
    if (g_view) DestroyWindow(g_view);

    wstring title = entryTitle(first.text);
    wchar_t cap[200];
    if (!title.empty())
        swprintf(cap, 200, L"%ls  |  %d-%02d-%02d", title.c_str(), y, m, d);
    else
        swprintf(cap, 200, L"%d-%02d-%02d", y, m, d);

    RECT rc = {0, 0, S(780), S(600)};
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    g_view = CreateWindowExW(0, VIEW_CLASS, cap,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        g_hWnd, nullptr, g_hInst, nullptr);
    if (!g_view) return;
    DWORD pref = 2; DwmSetWindowAttribute(g_view, 33, &pref, sizeof(pref));
    BOOL dark = isDarkTheme(); DwmSetWindowAttribute(g_view, 20, &dark, sizeof(dark));
    COLORREF cv = g_th.bg; DwmSetWindowAttribute(g_view, 35, &cv, sizeof(cv));

    g_viewDate = first.dateStr();
    g_viewBase = single ? base : first.base;
    wstring merged = single ? singleText : L"";
    if (!single) {
        for (size_t k = 0; k < texts.size(); k++) {
            if (k > 0) merged += L"\n\n";
            merged += texts[k];
        }
    }
    SendMessageW(g_viewText, WM_SETTEXT, 0, (LPARAM)merged.c_str());
    applyViewFont();
    ShowWindow(g_view, SW_SHOW);
    UpdateWindow(g_view);
}
