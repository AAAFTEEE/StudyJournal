// ============================================================
//  main_window.cpp — StudyJournal 主窗口
//  左侧导航 (日记 / 待办 / 日程 / 统计 / 设置) + 右侧页面容器
//  主题、DPI、今日概览与页面切换都在这里统一处理
// ============================================================
#include "app.h"

HWND g_hWnd = nullptr;
int  g_activePage = PAGE_JOURNAL;
bool g_themeSwitching = false;

static const int SJ_SIDEBAR = 208;
static int  g_navHover = -1;
static bool g_hoverTheme = false;
static bool g_hoverFolder = false;
static bool g_mouseIn = false;

// 导航项: 0/1 为页面, 2 日程, 3 统计, 4 设置
static const wchar_t* NAV_TEXT[] = { L"日记", L"待办", L"日程", L"统计", L"设置" };
static const int NAV_COUNT = 5;

static RECT navItemRect(int i) {
    RECT rc; GetClientRect(g_hWnd, &rc);
    int x = S(12), w = S(SJ_SIDEBAR) - S(24);
    int y = S(98), h = S(42);
    for (int k = 0; k < i; k++) {
        y += h + S(4);
        if (k == 1) y += S(12);            // 页面组与工具组之间留白
    }
    RECT r = { x, y, x + w, y + h };
    return r;
}
static RECT themeRect() {
    RECT rc; GetClientRect(g_hWnd, &rc);
    int x = S(12), w = S(SJ_SIDEBAR) - S(24);
    RECT r = { x, rc.bottom - S(78), x + w, rc.bottom - S(44) };
    return r;
}
static RECT folderRect() {
    RECT rc; GetClientRect(g_hWnd, &rc);
    int x = S(12), w = S(SJ_SIDEBAR) - S(24);
    RECT r = { x, rc.bottom - S(38), x + w, rc.bottom - S(10) };
    return r;
}
static RECT summaryRect() {
    RECT rc; GetClientRect(g_hWnd, &rc);
    int x = S(12), w = S(SJ_SIDEBAR) - S(24);
    RECT r = { x, rc.bottom - S(166), x + w, rc.bottom - S(88) };
    return r;
}

// ---------------- 小图标 (纯 GDI 绘制, 不依赖字体符号) ----------------
static void drawNavIcon(HDC dc, int kind, int cx, int cy, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, max(1, S(2)), color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBr = SelectObject(dc, GetStockObject(NULL_BRUSH));
    int r = S(8);
    switch (kind) {
    case 0: {  // 日记本
        RoundRect(dc, cx - r, cy - S(9), cx + r, cy + S(9), S(3), S(3));
        MoveToEx(dc, cx - S(3), cy - S(9), nullptr);
        LineTo(dc, cx - S(3), cy + S(9));
        MoveToEx(dc, cx + S(2), cy - S(4), nullptr);
        LineTo(dc, cx + S(5), cy - S(4));
        MoveToEx(dc, cx + S(2), cy + S(2), nullptr);
        LineTo(dc, cx + S(5), cy + S(2));
        break;
    }
    case 1: {  // 待办勾选
        POINT p[3] = { {cx - S(8), cy}, {cx - S(2), cy + S(6)}, {cx + S(8), cy - S(7)} };
        Polyline(dc, p, 3);
        break;
    }
    case 2: {  // 日历
        RoundRect(dc, cx - S(9), cy - S(7), cx + S(9), cy + S(8), S(3), S(3));
        MoveToEx(dc, cx - S(9), cy - S(1), nullptr);
        LineTo(dc, cx + S(9), cy - S(1));
        MoveToEx(dc, cx - S(4), cy - S(10), nullptr);
        LineTo(dc, cx - S(4), cy - S(5));
        MoveToEx(dc, cx + S(4), cy - S(10), nullptr);
        LineTo(dc, cx + S(4), cy - S(5));
        break;
    }
    case 3: {  // 统计柱状
        SelectObject(dc, GetStockObject(NULL_PEN));
        HBRUSH br = CreateSolidBrush(color);
        HGDIOBJ ob = SelectObject(dc, br);
        int barW = S(4);
        RoundRect(dc, cx - S(9), cy + S(1), cx - S(9) + barW, cy + S(9), S(2), S(2));
        RoundRect(dc, cx - S(2), cy - S(4), cx - S(2) + barW, cy + S(9), S(2), S(2));
        RoundRect(dc, cx + S(5), cy - S(9), cx + S(5) + barW, cy + S(9), S(2), S(2));
        SelectObject(dc, ob);
        DeleteObject(br);
        break;
    }
    default: { // 设置: 圆环 + 四个齿
        Ellipse(dc, cx - S(7), cy - S(7), cx + S(7), cy + S(7));
        Ellipse(dc, cx - S(2), cy - S(2), cx + S(2), cy + S(2));
        for (int k = 0; k < 4; k++) {
            static const int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
            MoveToEx(dc, cx + dx[k]*S(7), cy + dy[k]*S(7), nullptr);
            LineTo(dc, cx + dx[k]*S(11), cy + dy[k]*S(11));
        }
        break;
    }
    }
    SelectObject(dc, oldBr);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

static void paintSidebar(HDC dc, const RECT& rc) {
    HBRUSH card = CreateSolidBrush(g_th.card);
    FillRect(dc, &rc, card);
    DeleteObject(card);
    SetBkMode(dc, TRANSPARENT);

    int sw = S(SJ_SIDEBAR);
    HPEN border = CreatePen(PS_SOLID, 1, g_th.cardBorder);
    HGDIOBJ ob = SelectObject(dc, border);
    MoveToEx(dc, sw - 1, 0, nullptr);
    LineTo(dc, sw - 1, rc.bottom);
    SelectObject(dc, ob);
    DeleteObject(border);

    // 品牌标记
    RECT mark = { S(20), S(22), S(20) + S(34), S(22) + S(34) };
    HBRUSH acc = CreateSolidBrush(g_th.accent);
    HGDIOBJ oldBr = SelectObject(dc, acc);
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, mark.left, mark.top, mark.right, mark.bottom, S(9), S(9));
    SelectObject(dc, oldBr);
    SelectObject(dc, oldPen);
    DeleteObject(acc);
    SetTextColor(dc, RGB(255,255,255));
    SelectObject(dc, g_hFontBold);
    DrawTextW(dc, L"SJ", -1, &mark, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SetTextColor(dc, g_th.textPrimary);
    SelectObject(dc, g_hFontLogo);
    RECT title = { S(62), S(20), sw - S(8), S(48) };
    DrawTextW(dc, L"StudyJournal", -1, &title, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    SetTextColor(dc, g_th.textSecondary);
    SelectObject(dc, g_hFontSmall);
    RECT sub = { S(63), S(47), sw - S(8), S(68) };
    DrawTextW(dc, L"学习 · 记录 · 待办", -1, &sub, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    SetTextColor(dc, g_th.textSecondary);
    RECT lab = { S(22), S(76), sw - S(12), S(94) };
    DrawTextW(dc, L"工作区", -1, &lab, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    // 导航项
    for (int i = 0; i < NAV_COUNT; i++) {
        RECT r = navItemRect(i);
        bool active = (i == 0 && g_activePage == PAGE_JOURNAL) ||
                      (i == 1 && g_activePage == PAGE_TASKS);
        bool hover = (g_navHover == i);
        if (active || hover) {
            HBRUSH b = CreateSolidBrush(active ? g_th.accentLight : g_th.hover);
            HGDIOBJ o = SelectObject(dc, b);
            HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
            RoundRect(dc, r.left, r.top, r.right, r.bottom, S(9), S(9));
            SelectObject(dc, o); SelectObject(dc, op); DeleteObject(b);
        }
        if (active) {
            RECT bar = { r.left + S(2), r.top + S(10), r.left + S(5), r.bottom - S(10) };
            HBRUSH b = CreateSolidBrush(g_th.accent);
            FillRect(dc, &bar, b);
            DeleteObject(b);
        }
        COLORREF fg = active ? g_th.accentDark : g_th.textPrimary;
        drawNavIcon(dc, i, r.left + S(22), (r.top + r.bottom) / 2, fg);
        SetTextColor(dc, fg);
        SelectObject(dc, g_hFontBold);
        RECT tr = { r.left + S(40), r.top, r.right - S(8), r.bottom };
        DrawTextW(dc, NAV_TEXT[i], -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    // 今日概览
    RECT sr = summaryRect();
    HBRUSH sb = CreateSolidBrush(g_th.bg);
    HGDIOBJ so = SelectObject(dc, sb);
    HPEN sp = CreatePen(PS_SOLID, 1, g_th.cardBorder);
    HGDIOBJ sop = SelectObject(dc, sp);
    RoundRect(dc, sr.left, sr.top, sr.right, sr.bottom, S(9), S(9));
    SelectObject(dc, so); SelectObject(dc, sop);
    DeleteObject(sb); DeleteObject(sp);

    SetTextColor(dc, g_th.textSecondary);
    SelectObject(dc, g_hFontSmall);
    RECT sh = { sr.left + S(12), sr.top + S(6), sr.right - S(10), sr.top + S(26) };
    DrawTextW(dc, L"今日概览", -1, &sh, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    int jc = journalEntriesToday();
    int tc = tasksDueToday();
    wchar_t line[96];
    swprintf(line, 96, L"%d 条日记  ·  %d 项待办", jc, tc);
    SetTextColor(dc, g_th.textPrimary);
    SelectObject(dc, g_hFontBold);
    RECT sv = { sr.left + S(12), sr.top + S(28), sr.right - S(8), sr.bottom - S(4) };
    DrawTextW(dc, line, -1, &sv, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    // 主题切换
    RECT tr = themeRect();
    HBRUSH tb = CreateSolidBrush(g_hoverTheme ? g_th.hover : g_th.bg);
    HGDIOBJ to = SelectObject(dc, tb);
    HGDIOBJ tp = SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, tr.left, tr.top, tr.right, tr.bottom, S(8), S(8));
    SelectObject(dc, to); SelectObject(dc, tp);
    DeleteObject(tb);
    SetTextColor(dc, g_th.textPrimary);
    SelectObject(dc, g_hFontSmall);
    const wchar_t* themeLabel = isDarkTheme() ? L"☀  切换浅色主题" : L"☾  切换深色主题";
    DrawTextW(dc, themeLabel, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // 数据目录
    RECT fr = folderRect();
    SetTextColor(dc, g_hoverFolder ? g_th.accent : g_th.textSecondary);
    SelectObject(dc, g_hFontSmall);
    DrawTextW(dc, L"打开数据目录", -1, &fr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// ---------------- 今日概览 / 统计 ----------------
static void showOverviewWindow(HWND parent) {
    static HWND s_overview = nullptr;
    if (s_overview) { SetForegroundWindow(s_overview); return; }

    int words = 0, monthCnt = 0;
    set<wstring> days;
    for (auto& e : g_entries) {
        words += (int)e.text.size();
        days.insert(e.dateStr());
        if (e.year == g_todayYear && e.mon == g_todayMon) monthCnt++;
    }
    int tot = taskTotal(), don = taskDone();
    int overdue = 0;
    time_t now = time(nullptr); tm* lt = localtime(&now);
    wchar_t today[16];
    swprintf(today, 16, L"%04d-%02d-%02d", lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
    for (auto& s : g_subjects)
        for (auto& t : s.tasks)
            if (!t.done && !t.due.empty() && t.due < today) overdue++;
    int schedToday = (int)todayScheduleMessages().size();

    wchar_t text[1600];
    swprintf(text, 1600,
        L"日记\n"
        L"    总记录        %d 条\n"
        L"    有记录天数    %d 天\n"
        L"    总字数        %d 字\n"
        L"    本月记录      %d 条\n\n"
        L"待办  ·  %ls\n"
        L"    任务总数      %d 项\n"
        L"    已完成        %d 项  (%d%%)\n"
        L"    今日到期      %d 项\n"
        L"    已超期        %d 项\n\n"
        L"日程\n"
        L"    今天命中      %d 条",
        (int)g_entries.size(), (int)days.size(), words, monthCnt,
        taskListDisplayName().c_str(), tot, don, tot ? don * 100 / tot : 0,
        tasksDueToday(), overdue, schedToday);

    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
            switch (m) {
            case WM_CREATE: {
                HWND st = CreateWindowW(L"STATIC", (const wchar_t*)((CREATESTRUCTW*)l)->lpCreateParams,
                    WS_CHILD | WS_VISIBLE | SS_LEFT, S(26), S(22), S(408), S(320),
                    h, nullptr, g_hInst, nullptr);
                SendMessageW(st, WM_SETFONT, (WPARAM)g_hFont, TRUE);
                HWND b = CreateWindowW(L"BUTTON", L"关闭", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                    0, 0, 0, 0, h, (HMENU)(INT_PTR)1, g_hInst, nullptr);
                regBtn(b, BK_SECONDARY);
                SendMessageW(b, WM_SETFONT, (WPARAM)g_hFont, TRUE);
                return 0;
            }
            case WM_SIZE: {
                RECT rc; GetClientRect(h, &rc);
                SetWindowPos(GetDlgItem(h, 1), nullptr, rc.right - S(96), rc.bottom - S(46), S(76), S(32), SWP_NOZORDER);
                SetWindowPos(GetDlgItem(h, 0), nullptr, S(26), S(22), rc.right - S(52), rc.bottom - S(80), SWP_NOZORDER);
                return 0;
            }
            case WM_ERASEBKGND: {
                HDC dc = (HDC)w; RECT rc; GetClientRect(h, &rc);
                HBRUSH br = CreateSolidBrush(g_th.bg); FillRect(dc, &rc, br); DeleteObject(br);
                return 1;
            }
            case WM_CTLCOLORSTATIC: {
                HDC dc = (HDC)w; SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, g_th.textPrimary); return (LRESULT)g_hBrBg;
            }
            case WM_DRAWITEM: {
                DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)l;
                if (g_btns.count(di->hwndItem)) { drawButton(di); return TRUE; }
                break;
            }
            case WM_COMMAND:
                if (LOWORD(w) == 1 || LOWORD(w) == IDCANCEL) DestroyWindow(h);
                return 0;
            case WM_CLOSE: DestroyWindow(h); return 0;
            case WM_DESTROY: s_overview = nullptr; return 0;
            }
            return DefWindowProcW(h, m, w, l);
        };
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        wc.lpszClassName = L"StudyJournalOverview";
        RegisterClassExW(&wc);
        reg = true;
    }
    RECT rc = { 0, 0, S(460), S(420) };
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    s_overview = CreateWindowExW(0, L"StudyJournalOverview", L"学习概览",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        parent, nullptr, g_hInst, (LPVOID)text);
    if (!s_overview) return;
    DWORD pref = 2; DwmSetWindowAttribute(s_overview, 33, &pref, sizeof(pref));
    COLORREF c = g_th.bg; DwmSetWindowAttribute(s_overview, 35, &c, sizeof(c));
    ShowWindow(s_overview, SW_SHOW);
    UpdateWindow(s_overview);
}

// ---------------- 页面切换 ----------------
void switchPage(int idx) {
    if (idx < 0 || idx >= PAGE_COUNT) return;
    g_activePage = idx;
    if (g_hJournalPage) ShowWindow(g_hJournalPage, idx == PAGE_JOURNAL ? SW_SHOW : SW_HIDE);
    if (g_hTasksPage)   ShowWindow(g_hTasksPage,   idx == PAGE_TASKS   ? SW_SHOW : SW_HIDE);
    if (idx == PAGE_JOURNAL) { refreshJournalPage(); }
    else { refreshTasksPage(); }
    updateSidebarSummary();
    saveConfig(g_journalRoot);
    InvalidateRect(g_hWnd, nullptr, FALSE);
}

void updateSidebarSummary() {
    if (!g_hWnd) return;
    RECT rc; GetClientRect(g_hWnd, &rc);
    RECT sr = { 0, 0, S(SJ_SIDEBAR), rc.bottom };
    InvalidateRect(g_hWnd, &sr, FALSE);
}

// ---------------- 主窗口过程 ----------------
static LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hWnd = hWnd;
        g_hJournalPage = createJournalPage(hWnd);
        g_hTasksPage = createTasksPage(hWnd);
        RECT rc; GetClientRect(hWnd, &rc);
        int cw = max(1, (int)(rc.right - S(SJ_SIDEBAR)));
        MoveWindow(g_hJournalPage, S(SJ_SIDEBAR), 0, cw, rc.bottom, TRUE);
        MoveWindow(g_hTasksPage, S(SJ_SIDEBAR), 0, cw, rc.bottom, TRUE);
        switchPage(g_activePage >= PAGE_COUNT ? PAGE_JOURNAL : g_activePage);
        return 0;
    }
    case WM_SIZE: {
        if (wParam == SIZE_MINIMIZED) return 0;
        RECT rc; GetClientRect(hWnd, &rc);
        int cw = max(1, (int)(rc.right - S(SJ_SIDEBAR)));
        MoveWindow(g_hJournalPage, S(SJ_SIDEBAR), 0, cw, rc.bottom, TRUE);
        MoveWindow(g_hTasksPage, S(SJ_SIDEBAR), 0, cw, rc.bottom, TRUE);
        RECT sr = { 0, 0, S(SJ_SIDEBAR), rc.bottom };
        InvalidateRect(hWnd, &sr, FALSE);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lParam;
        mm->ptMinTrackSize.x = S(1000);
        mm->ptMinTrackSize.y = S(680);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc; GetClientRect(hWnd, &rc);
        RECT srect = { 0, 0, S(SJ_SIDEBAR), rc.bottom };
        paintSidebar(hdc, srect);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!g_mouseIn) {
            g_mouseIn = true;
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
        }
        int x = (short)LOWORD(lParam), y = (short)HIWORD(lParam);
        int hover = -1;
        for (int i = 0; i < NAV_COUNT; i++) {
            RECT r = navItemRect(i);
            if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) { hover = i; break; }
        }
        bool ht = false, hf = false;
        if (hover < 0) {
            RECT r = themeRect();
            ht = (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom);
            r = folderRect();
            hf = (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom);
        }
        if (hover != g_navHover || ht != g_hoverTheme || hf != g_hoverFolder) {
            g_navHover = hover; g_hoverTheme = ht; g_hoverFolder = hf;
            updateSidebarSummary();
        }
        if (x < S(SJ_SIDEBAR))
            SetCursor((hover >= 0 || ht || hf) ? g_hCursorHand : LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW));
        return 0;
    }
    case WM_MOUSELEAVE:
        g_mouseIn = false;
        g_navHover = -1; g_hoverTheme = false; g_hoverFolder = false;
        updateSidebarSummary();
        return 0;
    case WM_LBUTTONDOWN: {
        int x = (short)LOWORD(lParam), y = (short)HIWORD(lParam);
        if (x >= S(SJ_SIDEBAR)) return 0;
        for (int i = 0; i < NAV_COUNT; i++) {
            RECT r = navItemRect(i);
            if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) {
                if (i == 0) switchPage(PAGE_JOURNAL);
                else if (i == 1) switchPage(PAGE_TASKS);
                else if (i == 2) openSchedulesWindow(g_hWnd);
                else if (i == 3) showOverviewWindow(g_hWnd);
                else if (i == 4) openSettingsWindow(g_hWnd);
                return 0;
            }
        }
        RECT r = themeRect();
        if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) {
            int idx = isDarkTheme() ? 0 : 4;
            applyTheme(idx);
            saveThemeIndex(idx);
            return 0;
        }
        r = folderRect();
        if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) {
            ShellExecuteW(nullptr, L"open", g_journalRoot.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        return 0;
    }
    case WM_DPICHANGED: {
        g_scale = fitUiScale(HIWORD(wParam) / 96.0);
        createFonts();
        RECT rc = { 0, 0, S(1180), S(780) };
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
        int w = (int)(rc.right - rc.left);
        int h = (int)(rc.bottom - rc.top);
        POINT origin = { 0, 0 };
        HMONITOR mon = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = { sizeof(mi) };
        int mx = 0, my = 0, mw = 1440, mh = 852;
        if (GetMonitorInfoW(mon, &mi)) {
            mx = mi.rcWork.left; my = mi.rcWork.top;
            mw = mi.rcWork.right - mi.rcWork.left;
            mh = mi.rcWork.bottom - mi.rcWork.top;
        }
        w = min(w, mw - 10); h = min(h, mh - 10);
        SetWindowPos(hWnd, nullptr, mx + (mw - w) / 2, my + (mh - h) / 2, w, h,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }
    case WM_USER + 5:
        if (g_hJournalPage) InvalidateRect(g_hJournalPage, nullptr, FALSE);
        return 0;
    case WM_SJ_THEME:
        InvalidateRect(hWnd, nullptr, TRUE);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_F12) {                 // 快捷切换深浅主题
            int idx = isDarkTheme() ? 0 : 4;
            applyTheme(idx);
            saveThemeIndex(idx);
            return 0;
        }
        break;
    case WM_CLOSE:
        autoSaveTasks();
        if (!g_taskListName.empty()) g_lastTaskName = g_taskListName;
        saveConfig(g_journalRoot);
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

bool createMainWindow(HINSTANCE hInst, int nCmdShow) {
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"StudyJournalMainWnd";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    RECT rc = { 0, 0, S(1180), S(780) };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    g_hWnd = CreateWindowExW(0, L"StudyJournalMainWnd", L"StudyJournal — 学习日记 · 待办",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInst, nullptr);
    if (!g_hWnd) return false;

    BOOL dark = isDarkTheme();
    DwmSetWindowAttribute(g_hWnd, 20, &dark, sizeof(dark));
    DWORD pref = 2; DwmSetWindowAttribute(g_hWnd, 33, &pref, sizeof(pref));
    COLORREF c = g_th.bg; DwmSetWindowAttribute(g_hWnd, 35, &c, sizeof(c));

    // 居中到工作区, 避免 CW_USEDEFAULT 在 DPI 缩放下把窗口底部推出屏幕
    {
        RECT wr, work = { 0, 0, 1280, 800 };
        GetWindowRect(g_hWnd, &wr);
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        int w = wr.right - wr.left, h = wr.bottom - wr.top;
        int x = work.left + ((work.right - work.left) - w) / 2;
        int y = work.top + ((work.bottom - work.top) - h) / 2;
        SetWindowPos(g_hWnd, nullptr, max((int)work.left, x), max((int)work.top, y), w, h,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);
    return true;
}
