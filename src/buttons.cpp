// ============================================================
//  buttons.cpp — 自绘按钮 (主窗口 / 编辑器共用) + 主题
// ============================================================
#include "app.h"

Theme g_th;
Theme g_themePresets[7];
int   g_themeIndex = 0;
const wchar_t* g_themeNames[7] = {
    L"经典浅蓝", L"薄荷绿", L"暖阳橙", L"樱花粉", L"深夜黑", L"墨玉暗绿", L"极简灰"
};

static void initThemePresets() {
    static bool done = false;
    if (done) return;
    done = true;
    // 0 经典浅蓝
    g_themePresets[0] = {
        RGB(243,245,248), RGB(255,255,255), RGB(229,231,235),
        RGB(59,130,246), RGB(29,78,216), RGB(226,238,255),
        RGB(240,242,245), RGB(243,244,246), RGB(229,231,235),
        RGB(31,41,55), RGB(107,114,128), RGB(220,96,96),
        RGB(245,158,11), RGB(220,68,68), RGB(254,226,226),
        RGB(156,163,175), RGB(203,213,225), RGB(147,51,234)
    };
    // 1 薄荷绿
    g_themePresets[1] = {
        RGB(236,246,238), RGB(255,255,255), RGB(214,231,220),
        RGB(16,163,106), RGB(6,120,76), RGB(220,242,231),
        RGB(240,246,241), RGB(244,247,244), RGB(224,236,228),
        RGB(25,42,32), RGB(108,130,116), RGB(220,96,96),
        RGB(245,158,11), RGB(220,68,68), RGB(254,226,226),
        RGB(156,163,175), RGB(214,226,218), RGB(147,51,234)
    };
    // 2 暖阳橙
    g_themePresets[2] = {
        RGB(251,244,235), RGB(255,255,255), RGB(236,222,206),
        RGB(234,133,44), RGB(194,100,20), RGB(254,238,222),
        RGB(250,241,232), RGB(252,247,241), RGB(242,230,216),
        RGB(55,42,28), RGB(130,112,92), RGB(220,96,96),
        RGB(16,163,106), RGB(220,68,68), RGB(254,226,226),
        RGB(156,163,175), RGB(234,222,208), RGB(147,51,234)
    };
    // 3 樱花粉
    g_themePresets[3] = {
        RGB(250,240,244), RGB(255,255,255), RGB(238,218,225),
        RGB(233,95,158), RGB(196,54,120), RGB(254,234,241),
        RGB(250,241,245), RGB(252,245,248), RGB(242,226,233),
        RGB(58,35,45), RGB(140,112,122), RGB(220,96,96),
        RGB(245,158,11), RGB(220,68,68), RGB(254,226,226),
        RGB(156,163,175), RGB(236,220,226), RGB(147,51,234)
    };
    // 4 深夜黑
    g_themePresets[4] = {
        RGB(22,27,34), RGB(30,37,46), RGB(45,55,66),
        RGB(96,165,250), RGB(59,130,246), RGB(38,58,84),
        RGB(34,42,52), RGB(38,47,58), RGB(26,33,41),
        RGB(226,232,240), RGB(148,163,184), RGB(240,130,130),
        RGB(245,158,11), RGB(244,100,100), RGB(70,40,40),
        RGB(100,110,124), RGB(60,70,82), RGB(168,85,247)
    };
    // 5 墨玉暗绿
    g_themePresets[5] = {
        RGB(18,28,24), RGB(27,40,34), RGB(40,58,50),
        RGB(52,211,153), RGB(16,148,100), RGB(34,66,52),
        RGB(30,46,39), RGB(34,52,44), RGB(22,35,29),
        RGB(216,235,224), RGB(134,168,152), RGB(240,130,130),
        RGB(245,158,11), RGB(244,100,100), RGB(60,40,40),
        RGB(92,116,104), RGB(54,74,64), RGB(168,85,247)
    };
    // 6 极简灰
    g_themePresets[6] = {
        RGB(241,243,246), RGB(255,255,255), RGB(224,228,234),
        RGB(79,89,102), RGB(51,60,70), RGB(234,238,243),
        RGB(239,241,245), RGB(243,245,248), RGB(228,232,237),
        RGB(35,40,48), RGB(120,128,138), RGB(220,96,96),
        RGB(79,89,102), RGB(220,68,68), RGB(254,226,226),
        RGB(156,163,175), RGB(214,219,226), RGB(147,51,234)
    };
}

int getThemeCount() { initThemePresets(); return 7; }
const wchar_t* getThemeName(int idx) { return (idx>=0&&idx<7)?g_themeNames[idx]:L""; }

bool isDarkTheme() {
    int lum = (GetRValue(g_th.bg) * 299 + GetGValue(g_th.bg) * 587 + GetBValue(g_th.bg) * 114) / 1000;
    return lum < 128;
}

// 应用主题: 更新 g_th + 画刷 + 标题栏颜色 + 重绘所有窗口
void applyTheme(int idx) {
    initThemePresets();
    if (idx < 0 || idx >= 7) idx = 0;
    g_themeIndex = idx;
    g_th = g_themePresets[idx];

    if (g_hBrBg)   { DeleteObject(g_hBrBg);   g_hBrBg   = CreateSolidBrush(g_th.bg); }
    if (g_hBrCard) { DeleteObject(g_hBrCard); g_hBrCard = CreateSolidBrush(g_th.card); }

    if (g_hWnd) {
        COLORREF c = g_th.bg;
        BOOL dark = isDarkTheme();
        DwmSetWindowAttribute(g_hWnd, 20, &dark, sizeof(dark));
        DwmSetWindowAttribute(g_hWnd, 35, &c, sizeof(c));
        InvalidateRect(g_hWnd, nullptr, TRUE);
    }
    HWND ed = editorWindow();
    if (ed) {
        COLORREF c = g_th.bg;
        DwmSetWindowAttribute(ed, 35, &c, sizeof(c));
        InvalidateRect(ed, nullptr, TRUE);
    }
    HWND st = settingsWindow();
    if (st) InvalidateRect(st, nullptr, TRUE);

    // 重绘所有自绘按钮 (切主题后按钮还停在旧配色 → 强制刷新, 避免旧浅色/白色残留)
    for (auto& kv : g_btns) InvalidateRect(kv.first, nullptr, FALSE);
    // 页面配色 / ListView / TreeView 统一刷新
    refreshThemeAll();
}

void refreshThemeAll() {
    if (g_hJournalPage) {
        RedrawWindow(g_hJournalPage, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }
    if (g_hTasksPage) {
        SendMessageW(g_hTasksPage, WM_SJ_THEME, 0, 0);
        RedrawWindow(g_hTasksPage, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }
    if (g_hWnd) InvalidateRect(g_hWnd, nullptr, TRUE);
}

map<HWND, BtnInfo> g_btns;

LRESULT CALLBACK BtnProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto it = g_btns.find(h);
    if (it != g_btns.end()) {
        BtnInfo& bi = it->second;
        if (m == WM_MOUSEMOVE && !bi.hover) {
            bi.hover = true;
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, h, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(h, nullptr, FALSE);
        } else if (m == WM_MOUSELEAVE) {
            bi.hover = false;
            InvalidateRect(h, nullptr, FALSE);
        } else if (m == WM_ENABLE) {
            InvalidateRect(h, nullptr, FALSE);
        }
        return CallWindowProcW(bi.oldProc, h, m, w, l);
    }
    return DefWindowProcW(h, m, w, l);
}

void regBtn(HWND h, BtnKind kind) {
    BtnInfo bi;
    bi.kind = kind;
    bi.hover = false;
    bi.oldProc = (WNDPROC)SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)BtnProc);
    g_btns[h] = bi;
}

void drawButton(const DRAWITEMSTRUCT* di) {
    auto it = g_btns.find(di->hwndItem);
    if (it == g_btns.end()) return;
    BtnInfo& bi = it->second;
    HDC hdc = di->hDC;
    RECT rc = di->rcItem;
    bool pressed  = (di->itemState & ODS_SELECTED) != 0;
    bool disabled = (di->itemState & ODS_DISABLED) != 0;
    bool hover    = bi.hover && !disabled;

    COLORREF fill, border, text;
    switch (bi.kind) {
    case BK_PRIMARY:
        fill   = disabled ? g_th.disabledFill
               : pressed ? g_th.accentDark
               : hover   ? RGB(43,108,230) : g_th.accent;
        border = fill;
        text   = disabled ? RGB(240,244,250) : RGB(255,255,255);
        break;
    case BK_SECONDARY:
        fill   = pressed ? g_th.pressBtn : hover ? g_th.hoverBtn : g_th.card;
        border = hover ? RGB(209,213,219) : g_th.cardBorder;
        text   = disabled ? g_th.disabledText : g_th.textPrimary;
        break;
    case BK_DANGER:
        fill   = pressed ? RGB(254,202,202) : hover ? g_th.dangerHover : g_th.card;
        border = hover ? RGB(252,165,165) : g_th.cardBorder;
        text   = disabled ? g_th.disabledText : g_th.danger;
        break;
    default: // BK_GHOST
        fill   = pressed ? RGB(226,229,233) : hover ? RGB(233,236,240) : g_th.bg;
        border = fill;
        text   = disabled ? g_th.disabledText : g_th.textPrimary;
        break;
    }

    // 先整块填背景色, 否则圆角外四角露出默认白色
    HBRUSH bbg = CreateSolidBrush(g_th.bg);
    FillRect(hdc, &rc, bbg);
    DeleteObject(bbg);

    HBRUSH hb = CreateSolidBrush(fill);
    HPEN   hp = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ ob = SelectObject(hdc, hb);
    HGDIOBJ op = SelectObject(hdc, hp);
    int r = (rc.bottom - rc.top) / 2;
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, r, r);
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(hb); DeleteObject(hp);

    wchar_t label[64] = {0};
    GetWindowTextW(di->hwndItem, label, 64);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, text);
    SelectObject(hdc, g_hFont);
    DrawTextW(hdc, label, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}
