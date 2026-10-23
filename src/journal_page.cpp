// ============================================================
//  journal_page.cpp — 日记页面 (主窗口子页面)
//  由 DayJournal 主窗口改造: 月历(自绘) + 日记列表 + 工具栏
//  写日记 / 双击 → 打开纯文本编辑器窗口
// ============================================================
#include "app.h"

HINSTANCE g_hInst = nullptr;
HFONT g_hFont, g_hFontBold, g_hFontTitle, g_hFontCal, g_hFontCalBold, g_hFontSmall;
HBRUSH g_hBrBg = nullptr, g_hBrCard = nullptr;
HCURSOR g_hCursorHand = nullptr;
UINT  g_sysACP = 0;
bool  g_systemIsGBK = false;
double g_scale = 1.0;

vector<DiaryEntry> g_entries;
int g_calYear = 2026, g_calMon = 8, g_selectedDay = 0;
int g_todayDay = 0, g_todayMon = 0, g_todayYear = 0;

HWND g_hJournalPage = nullptr;

HWND g_hPrev, g_hNext, g_hYearLabel, g_hTodayBtn, g_hWriteBtn;
HWND g_hDeleteBtn, g_hStatsBtn, g_hExportBtn, g_hImportBtn, g_hExitBtn;
HWND g_hList, g_hHintLabel, g_hPlaceholder, g_hOpenFolderBtn;
HWND g_hTitleBar;
HWND g_hSettingsBtn, g_hPrevYear, g_hNextYear, g_hSchedBtn;

HFONT g_hFontLogo = nullptr;
int   g_hoverCell = -1;
bool  g_mouseTracking = false;

static HWND g_hTodayMsgLabel = nullptr;

static vector<wstring> g_todayMsgs;
static int  g_todayMsgIdx = 0;
static int  g_ctxEntry = -1;
static bool g_farJumpWarned = false;

const int MARGIN = 18;

struct LayoutRec {
    int calX, calY, calW, calH;
    int weekdayY, gridX, gridY, cellW, cellH;
    int listY, listH;
};
static LayoutRec g_L;

// ============================================================
//  字体 / 布局
// ============================================================
void destroyFonts() {
    if (g_hFont)       { DeleteObject(g_hFont); g_hFont = nullptr; }
    if (g_hFontBold)   { DeleteObject(g_hFontBold); g_hFontBold = nullptr; }
    if (g_hFontTitle)  { DeleteObject(g_hFontTitle); g_hFontTitle = nullptr; }
    if (g_hFontCal)    { DeleteObject(g_hFontCal); g_hFontCal = nullptr; }
    if (g_hFontCalBold){ DeleteObject(g_hFontCalBold); g_hFontCalBold = nullptr; }
    if (g_hFontSmall)  { DeleteObject(g_hFontSmall); g_hFontSmall = nullptr; }
    if (g_hFontLogo)   { DeleteObject(g_hFontLogo); g_hFontLogo = nullptr; }
}
static HFONT makeFont(int px, bool bold) {
    return CreateFontW(-S(px), 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Microsoft YaHei UI");
}
void createFonts() {
    destroyFonts();
    g_hFont        = makeFont(14, false);
    g_hFontBold    = makeFont(14, true);
    g_hFontTitle   = makeFont(21, true);
    g_hFontCal     = makeFont(15, false);
    g_hFontCalBold = makeFont(15, true);
    g_hFontSmall   = makeFont(12, false);
    g_hFontLogo    = makeFont(19, true);
}

static void computeLayout() {
    RECT rc; GetClientRect(g_hJournalPage, &rc);
    int W = rc.right, H = rc.bottom;
    int pad = S(MARGIN);
    g_L.calX = pad;
    g_L.calY = S(62);
    g_L.calW = W - pad * 2;
    int cellH = (H - S(348)) / 6;
    cellH = max(S(40), min(S(56), cellH));
    g_L.cellH = cellH;
    g_L.weekdayY = g_L.calY + S(10);
    g_L.gridY = g_L.weekdayY + S(28);
    g_L.gridX = g_L.calX + S(12);
    g_L.cellW = (g_L.calW - S(24)) / 7;
    g_L.calH = S(10) + S(28) + cellH * 6 + S(10);
    g_L.listY = g_L.calY + g_L.calH + S(58);
    g_L.listH = max(S(84), H - g_L.listY - S(54));
}

static void placeCtrl(HWND h, int x, int y, int w, int hh) {
    SetWindowPos(h, nullptr, x, y, w, hh, SWP_NOZORDER | SWP_NOACTIVATE);
}

static void relayout() {
    if (!g_hJournalPage) return;
    RECT rc; GetClientRect(g_hJournalPage, &rc);
    int W = rc.right, H = rc.bottom;
    int pad = S(MARGIN);
    computeLayout();

    placeCtrl(g_hPrev,     pad,        S(14), S(36), S(36));
    placeCtrl(g_hPrevYear, pad + S(40), S(14), S(44), S(36));
    placeCtrl(g_hYearLabel,pad + S(90), S(14), S(160), S(36));
    placeCtrl(g_hNextYear, pad + S(256),S(14), S(44), S(36));
    placeCtrl(g_hNext,     pad + S(304),S(14), S(36), S(36));
    placeCtrl(g_hTodayBtn, pad + S(348),S(14), S(64), S(36));

    int writeW = S(122), setW = S(66);
    placeCtrl(g_hWriteBtn,   W - pad - writeW, S(14), writeW, S(36));
    placeCtrl(g_hSettingsBtn,W - pad - writeW - S(8) - setW, S(14), setW, S(36));

    int infoY = g_L.calY + g_L.calH + S(4);
    int hintW = (W - pad * 2) * 54 / 100;
    placeCtrl(g_hHintLabel, pad, infoY, hintW, S(24));
    placeCtrl(g_hTodayMsgLabel, pad + hintW + S(8), infoY,
              W - pad * 2 - hintW - S(8), S(24));
    placeCtrl(g_hTitleBar, pad, infoY + S(30), W - pad * 2, S(24));

    placeCtrl(g_hList, pad, g_L.listY, W - pad * 2, g_L.listH);
    placeCtrl(g_hPlaceholder, pad + S(6), g_L.listY, W - pad * 2 - S(12), g_L.listH);

    int by = H - S(48);
    int bw = S(64), gap = S(8);
    placeCtrl(g_hDeleteBtn, pad, by, bw, S(32));
    placeCtrl(g_hStatsBtn, pad + (bw + gap), by, bw, S(32));
    placeCtrl(g_hExportBtn, pad + (bw + gap) * 2, by, S(112), S(32));
    placeCtrl(g_hImportBtn, pad + (bw + gap) * 2 + S(112) + gap, by, bw, S(32));
    placeCtrl(g_hOpenFolderBtn, pad + (bw + gap) * 2 + S(112) + gap + bw + gap, by, S(104), S(32));
    placeCtrl(g_hSchedBtn, pad + (bw + gap) * 2 + S(112) + gap + bw + gap + S(104) + gap, by, bw, S(32));

    HWND all[] = { g_hPrev, g_hNext, g_hPrevYear, g_hNextYear, g_hTodayBtn, g_hWriteBtn,
                   g_hSettingsBtn, g_hDeleteBtn, g_hStatsBtn, g_hExportBtn,
                   g_hImportBtn, g_hOpenFolderBtn, g_hSchedBtn, g_hList };
    for (HWND h : all) SendMessageW(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    SendMessageW(g_hYearLabel, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);
    SendMessageW(g_hTitleBar, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);
    SendMessageW(g_hHintLabel, WM_SETFONT, (WPARAM)g_hFontSmall, TRUE);
    SendMessageW(g_hTodayMsgLabel, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);
    SendMessageW(g_hPlaceholder, WM_SETFONT, (WPARAM)g_hFontSmall, TRUE);
    InvalidateRect(g_hJournalPage, nullptr, FALSE);
}

// ============================================================
//  日历工具
// ============================================================
static int daysInMonth(int y, int m) {
    static int d[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2 && (y%4==0 && (y%100!=0 || y%400==0))) return 29;
    return d[m];
}
static int firstDayOfWeek(int y, int m) {
    tm t = {}; t.tm_year = y-1900; t.tm_mon = m-1; t.tm_mday = 1;
    mktime(&t); return t.tm_wday;
}
static set<int> getDaysWithEntries(int y, int m) {
    set<int> s;
    for (auto& e : g_entries) if (e.year==y && e.mon==m) s.insert(e.day);
    return s;
}

// 提取条目标题: 取正文第一行非空文本 (兼容旧数据的 "# 标题" 写法, 仅去掉行首 #)
wstring entryTitle(const wstring& text) {
    size_t p = 0;
    while (p <= text.size()) {
        size_t eol = text.find(L'\n', p);
        wstring line = text.substr(p, (eol == wstring::npos) ? wstring::npos : eol - p);
        while (!line.empty() && (line.back() == L'\r' || line.back() == L'\t' || line.back() == L' '))
            line.pop_back();
        size_t s0 = line.find_first_not_of(L" \t\r");
        if (s0 != wstring::npos) {
            line = line.substr(s0);
            // 旧 Markdown 数据的标题行, 显示时去掉 "### " 前缀
            size_t i = 0;
            while (i < line.size() && line[i] == L'#') i++;
            if (i > 0 && i < line.size() && line[i] == L' ') {
                line = line.substr(i + 1);
                size_t t = line.find_first_not_of(L" \t");
                if (t != wstring::npos) line = line.substr(t);
            }
            if (line.size() > 46) line = line.substr(0, 46) + L"...";
            return line;
        }
        if (eol == wstring::npos) break;
        p = eol + 1;
    }
    return L"";
}

int calHitTest(int x, int y) {
    if (x < g_L.gridX || y < g_L.gridY) return -1;
    int col = (x - g_L.gridX) / g_L.cellW;
    int row = (y - g_L.gridY) / g_L.cellH;
    if (col < 0 || col > 6 || row < 0 || row > 5) return -1;
    int idx = row * 7 + col;
    int dayNum = idx - firstDayOfWeek(g_calYear, g_calMon) + 1;
    if (dayNum < 1 || dayNum > daysInMonth(g_calYear, g_calMon)) return -1;
    return idx;
}

void updateCalendar() {
    if (!g_hJournalPage) return;
    wchar_t buf[64];
    swprintf(buf, 64, L"%d 年 %d 月", g_calYear, g_calMon);
    SetWindowTextW(g_hYearLabel, buf);
    if (g_selectedDay > 0) {
        wstring tbT;
        for (auto& e : g_entries)
            if (e.year==g_calYear && e.mon==g_calMon && e.day==g_selectedDay) { tbT = entryTitle(e.text); break; }
        wchar_t tb[200];
        if (!tbT.empty()) swprintf(tb, 200, L"%d/%d  %ls", g_calMon, g_selectedDay, tbT.c_str());
        else swprintf(tb, 200, L"%d/%d", g_calMon, g_selectedDay);
        SetWindowTextW(g_hTitleBar, tb);
    } else {
        SetWindowTextW(g_hTitleBar, L"");
    }
    RECT rc = { g_L.calX, g_L.calY, g_L.calX + g_L.calW, g_L.calY + g_L.calH };
    InvalidateRect(g_hJournalPage, &rc, FALSE);
}

// 今天日程滚动提示: 日程 + 到期待办
static void updateTodayMsg() {
    if (!g_hJournalPage || !g_hTodayMsgLabel) return;
    g_todayMsgs = todayScheduleMessages();
    int due = tasksDueToday();
    if (due > 0) {
        wchar_t b[96];
        swprintf(b, 96, L"今天有 %d 项待办到期", due);
        g_todayMsgs.insert(g_todayMsgs.begin(), wstring(b));
    }
    g_todayMsgIdx = 0;
    if (g_todayMsgs.empty()) {
        SetWindowTextW(g_hTodayMsgLabel, L"");
        KillTimer(g_hJournalPage, 1);
        ShowWindow(g_hTodayMsgLabel, SW_HIDE);
        return;
    }
    ShowWindow(g_hTodayMsgLabel, SW_SHOW);
    SetWindowTextW(g_hTodayMsgLabel, g_todayMsgs[0].c_str());
    if (g_todayMsgs.size() > 1) SetTimer(g_hJournalPage, 1, 4000, nullptr);
    else KillTimer(g_hJournalPage, 1);
}

static void paintCalendar(HDC hdc) {
    HBRUSH hb = CreateSolidBrush(g_th.card);
    HPEN hp = CreatePen(PS_SOLID, 1, g_th.cardBorder);
    HGDIOBJ ob = SelectObject(hdc, hb), op = SelectObject(hdc, hp);
    RoundRect(hdc, g_L.calX, g_L.calY, g_L.calX+g_L.calW, g_L.calY+g_L.calH, S(14), S(14));
    SelectObject(hdc, ob); SelectObject(hdc, op); DeleteObject(hb); DeleteObject(hp);
    SetBkMode(hdc, TRANSPARENT);

    const wchar_t* wd[] = { L"日", L"一", L"二", L"三", L"四", L"五", L"六" };
    SelectObject(hdc, g_hFontBold);
    for (int i = 0; i < 7; i++) {
        RECT r = { g_L.gridX + i*g_L.cellW, g_L.weekdayY,
                   g_L.gridX + (i+1)*g_L.cellW, g_L.weekdayY + S(28) };
        SetTextColor(hdc, i == 0 ? g_th.textWeekend : g_th.textSecondary);
        DrawTextW(hdc, wd[i], -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    set<int> daysWith = getDaysWithEntries(g_calYear, g_calMon);
    int firstDow = firstDayOfWeek(g_calYear, g_calMon);
    int dim = daysInMonth(g_calYear, g_calMon);
    COLORREF taskMark = isDarkTheme() ? RGB(52,211,153) : RGB(16,163,106);

    for (int i = 0; i < 42; i++) {
        int dayNum = i - firstDow + 1;
        if (dayNum < 1 || dayNum > dim) continue;
        int row = i / 7, col = i % 7;
        RECT rc = { g_L.gridX + col*g_L.cellW + 2, g_L.gridY + row*g_L.cellH + 2,
                    g_L.gridX + (col+1)*g_L.cellW - 2, g_L.gridY + (row+1)*g_L.cellH - 2 };
        bool isToday = (dayNum == g_todayDay && g_calMon == g_todayMon && g_calYear == g_todayYear);
        bool isSel   = (dayNum == g_selectedDay);
        bool isHover = (i == g_hoverCell);
        bool hasEntry = daysWith.count(dayNum) > 0;
        bool hasSched = anyScheduleOn(g_calYear, g_calMon, dayNum);
        bool hasTask  = tasksDueOn(g_calYear, g_calMon, dayNum) > 0;

        if (isToday) {
            HBRUSH b = CreateSolidBrush(isSel ? g_th.accentDark : g_th.accent);
            HGDIOBJ o = SelectObject(hdc, b);
            HGDIOBJ o2 = SelectObject(hdc, (HPEN)GetStockObject(NULL_PEN));
            RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, S(10), S(10));
            SelectObject(hdc, o); SelectObject(hdc, o2); DeleteObject(b);
        } else if (isSel) {
            HBRUSH b = CreateSolidBrush(g_th.accentLight);
            HPEN p = CreatePen(PS_SOLID, 2, g_th.accent);
            HGDIOBJ o = SelectObject(hdc, b), o2 = SelectObject(hdc, p);
            RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, S(10), S(10));
            SelectObject(hdc, o); SelectObject(hdc, o2); DeleteObject(b); DeleteObject(p);
        } else if (isHover) {
            HBRUSH b = CreateSolidBrush(g_th.hover);
            HGDIOBJ o = SelectObject(hdc, b);
            HGDIOBJ o2 = SelectObject(hdc, (HPEN)GetStockObject(NULL_PEN));
            RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, S(10), S(10));
            SelectObject(hdc, o); SelectObject(hdc, o2); DeleteObject(b);
        }

        wchar_t txt[8];
        swprintf(txt, 8, L"%d", dayNum);
        RECT tr = rc;
        tr.bottom -= S(20);
        SelectObject(hdc, (isToday || isSel) ? g_hFontCalBold : g_hFontCal);
        SetTextColor(hdc, isToday ? RGB(255,255,255)
                    : isSel ? g_th.accentDark
                    : (col==0 || col==6) ? g_th.textSecondary : g_th.textPrimary);
        DrawTextW(hdc, txt, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        wstring sub;
        if (g_showLunar) sub = lunarDisplay(g_calYear, g_calMon, dayNum);
        wstring fest;
        if (g_showFest) fest = festivalsOf(g_calYear, g_calMon, dayNum);
        char hst = g_showHol ? holidayStatusOf(g_calYear, g_calMon, dayNum) : 0;
        bool holiday = (hst == 'H'), workday = (hst == 'W');
        if (!fest.empty()) sub = fest;
        if (workday) sub = L"班·" + sub;
        RECT br = { rc.left, rc.bottom - S(18), rc.right, rc.bottom };
        SetTextColor(hdc, isToday ? RGB(255,255,255)
                    : holiday ? RGB(220,68,68)
                    : workday ? RGB(147,51,234)
                    : isSel ? g_th.accentDark : g_th.textSecondary);
        SelectObject(hdc, g_hFontSmall);
        if (!sub.empty())
            DrawTextW(hdc, sub.c_str(), -1, &br, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        int nMark = (hasEntry?1:0) + (hasSched?1:0) + (hasTask?1:0);
        if (nMark > 0) {
            int lw = S(9), lh = S(2), gap = S(7);
            int total = lw + (nMark - 1) * (lw + gap);
            int sx = (rc.left + rc.right) / 2 - total / 2;
            int cy = (rc.top + rc.bottom) / 2 + S(4);
            auto drawMark = [&](COLORREF c, int& x) {
                HBRUSH b = CreateSolidBrush(c);
                HGDIOBJ o = SelectObject(hdc, b), o2 = SelectObject(hdc, (HPEN)GetStockObject(NULL_PEN));
                RoundRect(hdc, x, cy - lh/2, x + lw, cy + lh/2 + 1, S(2), S(2));
                SelectObject(hdc, o); SelectObject(hdc, o2); DeleteObject(b);
                x += lw + gap;
            };
            if (hasEntry) drawMark(isToday ? RGB(255,255,255) : g_th.entryDot, sx);
            if (hasSched) drawMark(isToday ? RGB(255,255,255) : g_th.warn, sx);
            if (hasTask)  drawMark(isToday ? RGB(255,255,255) : taskMark, sx);
        }
    }
}

// ============================================================
//  列表
// ============================================================
void updateList() {
    if (!g_hJournalPage) return;
    SendMessageW(g_hList, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < g_entries.size(); i++) {
        auto& e = g_entries[i];
        wstring display = e.dateStr() + L"  " + e.timeStr() + L"  |  ";
        display += entryTitle(e.text);
        for (auto& ch : display) if (ch == L'\n') ch = L' ';
        LRESULT idx = SendMessageW(g_hList, LB_ADDSTRING, 0, (LPARAM)display.c_str());
        SendMessageW(g_hList, LB_SETITEMDATA, (WPARAM)idx, (LPARAM)i);
    }
    if (!g_hasDataDir) {
        SetWindowTextW(g_hPlaceholder, L"请在设置中选择数据目录！");
        ShowWindow(g_hPlaceholder, SW_SHOW);
        SetWindowTextW(g_hHintLabel, L"未设置数据目录 — 点右上角「设置」→「更改数据目录」");
        return;
    }
    wchar_t hint[260];
    swprintf(hint, 260, L"共 %d 条记录  |  第 %d 次打开  |  今日待办 %d 项  |  双击条目可编辑",
             (int)g_entries.size(), g_openCount, tasksDueToday());
    SetWindowTextW(g_hHintLabel, hint);
    ShowWindow(g_hPlaceholder, g_entries.empty() ? SW_SHOW : SW_HIDE);
    SetWindowTextW(g_hPlaceholder, L"还没有日记 — 点右上角「写日记」开始记录吧");
}

static void drawListItem(const DRAWITEMSTRUCT* di) {
    HDC hdc = di->hDC;
    RECT rc = di->rcItem;
    int idx = (int)di->itemData;
    bool selected = (di->itemState & ODS_SELECTED) != 0;
    HBRUSH hb = CreateSolidBrush(selected ? g_th.accentLight : g_th.card);
    FillRect(hdc, &rc, hb);
    DeleteObject(hb);
    if (idx >= 0 && idx < (int)g_entries.size()) {
        auto& e = g_entries[idx];
        if (selected) {
            RECT bar = { rc.left, rc.top, rc.left + S(4), rc.bottom };
            HBRUSH b2 = CreateSolidBrush(g_th.accent);
            FillRect(hdc, &bar, b2);
            DeleteObject(b2);
        }
        SetBkMode(hdc, TRANSPARENT);
        SelectObject(hdc, g_hFontSmall);
        wstring dts = e.dateStr() + L"  " + e.timeStr();
        RECT r1 = { rc.left + S(16), rc.top, rc.left + S(180), rc.bottom };
        SetTextColor(hdc, g_th.textSecondary);
        DrawTextW(hdc, dts.c_str(), -1, &r1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        wstring txt = entryTitle(e.text);
        for (auto& ch : txt) if (ch == L'\n' || ch == L'\r') ch = L' ';
        RECT r2 = { rc.left + S(190), rc.top, rc.right - S(16), rc.bottom };
        SetTextColor(hdc, g_th.textPrimary);
        SelectObject(hdc, g_hFont);
        DrawTextW(hdc, txt.c_str(), -1, &r2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

// ============================================================
//  功能
// ============================================================
static void checkFarJump() {
    int diff = g_calYear - g_todayYear;
    bool isFar = (diff > 3 || diff < -3);
    if (isFar && !g_farJumpWarned) {
        g_farJumpWarned = true;
        wchar_t buf[256];
        if (diff > 0)
            swprintf(buf, 256, L"嚯, 你一步跨到 %d 年去了?\n\n"
                L"再往前跳, 怕是连豆包都还没出生哦…\n"
                L"先把这一年的日记写起, 别光顾着赶未来, 现在这天也值得记一笔。",
                g_calYear);
        else
            swprintf(buf, 256, L"哟喂, 一个跟头翻回 %d 年了!\n\n"
                L"过去的日子是拿来回忆的, 不是拿来赶路嘞。\n"
                L"小心别把日历翻出火星嘞, 慢慢来~",
                g_calYear);
        MessageBoxW(g_hWnd, buf, L"StudyJournal 温馨提示", MB_OK | MB_ICONINFORMATION);
    } else if (!isFar) {
        g_farJumpWarned = false;
    }
}

static void onCalendarClick(int cellIdx) {
    int firstDow = firstDayOfWeek(g_calYear, g_calMon);
    int dayNum = cellIdx - firstDow + 1;
    int dim = daysInMonth(g_calYear, g_calMon);
    if (dayNum < 1 || dayNum > dim) return;
    if (g_selectedDay == dayNum) {
        g_selectedDay = 0;
        updateList();
        updateCalendar();
        return;
    }
    g_selectedDay = dayNum;
    vector<int> found;
    for (size_t i = 0; i < g_entries.size(); i++) {
        auto& e = g_entries[i];
        if (e.year==g_calYear && e.mon==g_calMon && e.day==dayNum) found.push_back((int)i);
    }
    int dueCnt = tasksDueOn(g_calYear, g_calMon, dayNum);
    if (found.empty()) {
        wchar_t msg[160];
        if (dueCnt > 0)
            swprintf(msg, 160, L"%d-%02d-%02d 没有日记  ·  当天有 %d 项待办到期",
                     g_calYear, g_calMon, dayNum, dueCnt);
        else
            swprintf(msg, 160, L"%d-%02d-%02d 没有日记", g_calYear, g_calMon, dayNum);
        SetWindowTextW(g_hHintLabel, msg);
        SendMessageW(g_hList, LB_RESETCONTENT, 0, 0);
    } else {
        SendMessageW(g_hList, LB_RESETCONTENT, 0, 0);
        for (int idx : found) {
            auto& e = g_entries[idx];
            wstring display = e.dateStr() + L"  " + e.timeStr() + L"  |  " +
                (e.text.size() > 46 ? e.text.substr(0,46)+L"..." : e.text);
            for (auto& ch : display) if (ch == L'\n') ch = L' ';
            LRESULT li = SendMessageW(g_hList, LB_ADDSTRING, 0, (LPARAM)display.c_str());
            SendMessageW(g_hList, LB_SETITEMDATA, (WPARAM)li, (LPARAM)idx);
        }
        wchar_t hint[200];
        if (dueCnt > 0)
            swprintf(hint, 200, L"%d-%02d-%02d 的日记: %d 条 · 待办 %d 项  (再点一次该天可恢复全部)",
                     g_calYear, g_calMon, dayNum, (int)found.size(), dueCnt);
        else
            swprintf(hint, 200, L"%d-%02d-%02d 的日记: %d 条  (再点一次该天可恢复全部)",
                     g_calYear, g_calMon, dayNum, (int)found.size());
        SetWindowTextW(g_hHintLabel, hint);
    }
    ShowWindow(g_hPlaceholder, found.empty() ? SW_SHOW : SW_HIDE);
    updateCalendar();
}

static void doDelete();
static void doStats();
static void doExport();
static void doImport();

static void copyToClipboard(const wstring& text) {
    if (!OpenClipboard(g_hWnd)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (h) {
        wchar_t* p = (wchar_t*)GlobalLock(h);
        if (p) { memcpy(p, text.c_str(), bytes); GlobalUnlock(h); }
        SetClipboardData(CF_UNICODETEXT, h);
    }
    CloseClipboard();
}

static void popContextMenu(POINT ptScreen) {
    POINT pt = ptScreen;
    ScreenToClient(g_hJournalPage, &pt);
    bool onList = false;
    if (g_hList) {
        RECT lr; GetClientRect(g_hList, &lr);
        MapWindowPoints(g_hList, g_hJournalPage, (LPPOINT)&lr, 2);
        onList = (pt.x >= lr.left && pt.x <= lr.right && pt.y >= lr.top && pt.y <= lr.bottom);
    }
    int calIdx = calHitTest(pt.x, pt.y);

    HMENU m = CreatePopupMenu();
    if (calIdx >= 0) {
        int dayNum = calIdx - firstDayOfWeek(g_calYear, g_calMon) + 1;
        if (dayNum >= 1 && dayNum <= daysInMonth(g_calYear, g_calMon)) {
            g_selectedDay = dayNum;
            updateCalendar();
            wchar_t title[160];
            swprintf(title, 160, L"%d/%d", g_calMon, g_selectedDay);
            AppendMenuW(m, MF_STRING, MNU_WRITE, (L"在该天写日记 (" + wstring(title) + L")").c_str());
            AppendMenuW(m, MF_STRING, MNU_VIEW,  L"查看当天日记");
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(m, MF_STRING, MNU_SCHED_NEW, L"新建日程");
        }
    } else if (onList) {
        g_ctxEntry = -1;
        LRESULT hit = LB_ERR;
        if (g_hList) {
            POINT lp = pt; MapWindowPoints(g_hJournalPage, g_hList, &lp, 1);
            LRESULT item = SendMessageW(g_hList, LB_ITEMFROMPOINT, 0, MAKELPARAM(lp.x, lp.y));
            int idx = LOWORD(item);
            if (idx != (int)LB_ERR && (HIWORD(item) & 1) == 0 && idx < (int)SendMessageW(g_hList, LB_GETCOUNT, 0, 0)) {
                hit = idx;
                SendMessageW(g_hList, LB_SETCURSEL, (WPARAM)idx, 0);
                g_ctxEntry = (int)SendMessageW(g_hList, LB_GETITEMDATA, (WPARAM)idx, 0);
            }
        }
        if (hit != LB_ERR && g_ctxEntry >= 0 && g_ctxEntry < (int)g_entries.size()) {
            AppendMenuW(m, MF_STRING, MNU_VIEW,   L"查看");
            AppendMenuW(m, MF_STRING, MNU_EDIT,   L"编辑");
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(m, MF_STRING, MNU_COPY_TITLE, L"复制标题");
            AppendMenuW(m, MF_STRING, MNU_COPY_TEXT,  L"复制正文");
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(m, MF_STRING, MNU_OPENDAY, L"打开所在日期");
            AppendMenuW(m, MF_STRING, MNU_DELETE, L"删除");
        }
    } else {
        AppendMenuW(m, MF_STRING, MNU_WRITE,     L"写日记 (今天)");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, MNU_TODAY,     L"回到今天");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, MNU_SCHED_NEW, L"打开日程");
        AppendMenuW(m, MF_STRING, MNU_STATS,     L"统计");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, MNU_EXPORT,    L"导出文本");
        AppendMenuW(m, MF_STRING, MNU_IMPORT,    L"导入");
        AppendMenuW(m, MF_STRING, MNU_OPENFOLDER, L"打开数据目录");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, MNU_SETTINGS,  L"设置");
    }

    int n = GetMenuItemCount(m);
    if (n > 0) {
        int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_RIGHTBUTTON,
            ptScreen.x, ptScreen.y, 0, g_hJournalPage, nullptr);
        if (cmd > 0) SendMessageW(g_hJournalPage, WM_COMMAND, (WPARAM)cmd, 0);
    }
    DestroyMenu(m);
}

static void handleCtxCommand(int id) {
    if (id == MNU_OPENDAY) {
        if (g_ctxEntry >= 0 && g_ctxEntry < (int)g_entries.size()) {
            auto& e = g_entries[g_ctxEntry];
            g_calYear = e.year; g_calMon = e.mon; g_selectedDay = e.day;
            updateCalendar(); InvalidateRect(g_hJournalPage, nullptr, FALSE);
        }
        return;
    }
    if (id == MNU_COPY_TITLE) {
        if (g_ctxEntry >= 0 && g_ctxEntry < (int)g_entries.size())
            copyToClipboard(entryTitle(g_entries[g_ctxEntry].text));
        return;
    }
    if (id == MNU_COPY_TEXT || id == MNU_COPY_AS_MARKDOWN) {
        if (g_ctxEntry >= 0 && g_ctxEntry < (int)g_entries.size())
            copyToClipboard(g_entries[g_ctxEntry].text);
        return;
    }
    if (id == MNU_WRITE) {
        wchar_t ds[16];
        swprintf(ds, 16, L"%04d-%02d-%02d",
            (g_selectedDay > 0 ? g_calYear : g_todayYear),
            (g_selectedDay > 0 ? g_calMon : g_todayMon),
            (g_selectedDay > 0 ? g_selectedDay : g_todayDay));
        openEntryEditor(wstring(ds), true);
        return;
    }
    if (id == MNU_VIEW) {
        if (g_ctxEntry >= 0 && g_ctxEntry < (int)g_entries.size()) {
            auto& e = g_entries[g_ctxEntry];
            openViewWindow(e.year, e.mon, e.day, e.base);
        } else if (g_selectedDay > 0) {
            openViewWindow(g_calYear, g_calMon, g_selectedDay);
        }
        return;
    }
    if (id == MNU_EDIT) {
        if (g_ctxEntry >= 0 && g_ctxEntry < (int)g_entries.size()) {
            auto& e = g_entries[g_ctxEntry];
            openEntryEditor(e.dateStr(), false, e.base);
        }
        return;
    }
    if (id == MNU_DELETE) {
        if (g_ctxEntry >= 0 && g_ctxEntry < (int)g_entries.size()) {
            int sel = -1;
            for (int i = 0; i < (int)SendMessageW(g_hList, LB_GETCOUNT, 0, 0); i++)
                if ((int)SendMessageW(g_hList, LB_GETITEMDATA, i, 0) == g_ctxEntry) { sel = i; break; }
            if (sel >= 0) SendMessageW(g_hList, LB_SETCURSEL, (WPARAM)sel, 0);
            doDelete();
        }
        return;
    }
    if (id == MNU_SCHED_NEW) { openSchedulesWindow(g_hWnd); updateTodayMsg(); return; }
    if (id == MNU_TODAY)     SendMessageW(g_hJournalPage, WM_COMMAND, IDC_TODAY, 0);
    if (id == MNU_STATS)     doStats();
    if (id == MNU_EXPORT)    doExport();
    if (id == MNU_IMPORT)    doImport();
    if (id == MNU_OPENFOLDER) ShellExecuteW(nullptr, L"open", g_journalRoot.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (id == MNU_SETTINGS)  openSettingsWindow(g_hWnd);
}

static void doDelete() {
    int sel = (int)SendMessageW(g_hList, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) { MessageBoxW(g_hWnd, L"请先在列表中选择一条记录", L"StudyJournal", MB_OK); return; }
    int idx = (int)SendMessageW(g_hList, LB_GETITEMDATA, sel, 0);
    if (idx < 0 || idx >= (int)g_entries.size()) return;
    auto& e = g_entries[idx];
    wstring msg = L"确定删除这条记录吗?\n\n" + e.dateStr() + L" " + e.timeStr() + L"\n" + e.text;
    if (MessageBoxW(g_hWnd, msg.c_str(), L"确认删除", MB_YESNO | MB_ICONWARNING) == IDYES) {
        wstring ds = e.dateStr();
        if (deleteEntryFileBase(ds, e.base))
            MessageBoxW(g_hWnd, L"已删除该条日记", L"StudyJournal", MB_OK | MB_ICONINFORMATION);
        else
            MessageBoxW(g_hWnd, L"删除失败: 找不到该条日记文件", L"StudyJournal", MB_OK | MB_ICONWARNING);
        refreshFromDisk(ds);
        updateList();
        updateCalendar();
        updateTodayMsg();
        updateSidebarSummary();
    }
}

static void doStats() {
    if (g_entries.empty()) { MessageBoxW(g_hWnd, L"还没有任何记录", L"日记统计", MB_OK); return; }
    int totalWords = 0;
    set<wstring> uniqueDays;
    for (auto& e : g_entries) {
        totalWords += (int)e.text.size();
        uniqueDays.insert(e.dateStr());
    }
    vector<wstring> sd(uniqueDays.begin(), uniqueDays.end());
    sort(sd.begin(), sd.end());
    int maxStreak = 1, curStreak = 1;
    for (size_t i = 1; i < sd.size(); i++) {
        string prev = toUTF8(sd[i-1]);
        int y=stoi(prev.substr(0,4)), m=stoi(prev.substr(5,2)), d=stoi(prev.substr(8,2));
        d++; if(d>daysInMonth(y,m)){d=1;m++;if(m>12){m=1;y++;}}
        wchar_t nd[16]; swprintf(nd,16,L"%04d-%02d-%02d",y,m,d);
        if (sd[i] == wstring(nd)) { curStreak++; maxStreak=max(maxStreak,curStreak); }
        else curStreak = 1;
    }
    time_t now=time(nullptr); tm* lt=localtime(&now);
    int thisMonth=0;
    for(auto& e:g_entries) if(e.year==lt->tm_year+1900 && e.mon==lt->tm_mon+1) thisMonth++;

    int attachCnt = 0;
    wstring attRoot = joinPathW(g_journalRoot, L"attachments");
    {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(joinPathW(attRoot, L"*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) attachCnt++;
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }

    int avgWords = uniqueDays.empty() ? 0 : (int)lround((double)totalWords / uniqueDays.size());
    wchar_t buf[700];
    swprintf(buf, 700,
        L"  总记录数:     %d 条\n"
        L"  总字数:       %d 字\n"
        L"  平均每天:     %d 字\n"
        L"  有记录的天数: %d 天\n"
        L"  最长连续:     %d 天\n"
        L"  本月记录:     %d 条\n"
        L"  附件总数:     %d 个\n"
        L"  今日待办:     %d 项\n"
        L"  打开次数:     %d 次",
        (int)g_entries.size(), totalWords, avgWords, (int)uniqueDays.size(),
        maxStreak, thisMonth, attachCnt, tasksDueToday(), g_openCount);
    MessageBoxW(g_hWnd, buf, L"日记统计", MB_OK | MB_ICONINFORMATION);
}

static void doExport() {
    wchar_t szFile[MAX_PATH] = L"StudyJournal_export.txt";
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hWnd;
    ofn.lpstrFilter = L"文本文件 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"txt";
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&ofn)) return;

    wstring out;
    wstring entriesDir = joinPathW(g_journalRoot, L"entries");
    vector<wstring> days;
    {
        wstring pattern = joinPathW(entriesDir, L"*");
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                    fd.cFileName[0] && wcslen(fd.cFileName) == 10) days.push_back(fd.cFileName);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
    sort(days.begin(), days.end());
    for (auto& day : days) {
        vector<wstring> files;
        wstring pattern = joinPathW(dayDir(day), L"*.md");
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                wstring fn = fd.cFileName;
                if (fn.size() >= 3 && toLowerW(fn.substr(fn.size()-3)) == L".md") files.push_back(fn);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        sort(files.begin(), files.end());
        for (auto& fn : files) {
            string b;
            if (readWholeFile(joinPathW(dayDir(day), fn), b)) {
                out += L"===== " + day + L"  " + fn.substr(0, fn.size()-3).c_str() + L" =====\n\n";
                out += decodeText(b);
                if (!out.empty() && out.back() != L'\n') out += L"\n";
                out += L"\n\n";
            }
        }
    }
    if (writeWholeFile(wstring(szFile), toUTF8(out)))
        MessageBoxW(g_hWnd, (L"已导出到: " + wstring(szFile)).c_str(), L"导出成功", MB_OK | MB_ICONINFORMATION);
    else
        MessageBoxW(g_hWnd, L"导出失败", L"错误", MB_OK | MB_ICONERROR);
}

static bool parseDateStr2(const string& ds, int& y, int& m, int& d) {
    if (ds.size() < 10) return false;
    try { y = stoi(ds.substr(0,4)); m = stoi(ds.substr(5,2)); d = stoi(ds.substr(8,2)); }
    catch (...) { return false; }
    return y>=1 && m>=1 && m<=12 && d>=1 && d<=31;
}
static bool parseTimeStr2(const string& ts, int& hh, int& mm, int& ss) {
    if (ts.size() < 8) return false;
    try { hh = stoi(ts.substr(0,2)); mm = stoi(ts.substr(3,2)); ss = stoi(ts.substr(6,2)); }
    catch (...) { return false; }
    return hh>=0 && hh<=23 && mm>=0 && mm<=59 && ss>=0 && ss<=60;
}
static string jsonUnescape(const string& s) {
    string r; r.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i]=='\\' && i+1<s.size()) {
            char c = s[++i];
            switch (c) { case 'n': r+='\n'; break; case 't': r+='\t'; break;
                case 'r': r+='\r'; break; default: r+=c; }
        } else r += s[i];
    }
    return r;
}
static void doImport() {
    wchar_t szFile[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hWnd;
    ofn.lpstrFilter = L"JSON 文件 (*.json)\0*.json\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;

    string b;
    if (!readWholeFile(wstring(szFile), b)) { MessageBoxW(g_hWnd, L"读取失败", L"错误", MB_OK|MB_ICONERROR); return; }
    wstring wb = decodeText(b);
    string content = toUTF8(wb);
    vector<DiaryEntry> imp;
    size_t pos = content.find("\"entries\"");
    if (pos == string::npos) { MessageBoxW(g_hWnd, L"格式错误", L"错误", MB_OK|MB_ICONERROR); return; }
    pos = content.find('[', pos);
    if (pos == string::npos) { MessageBoxW(g_hWnd, L"格式错误", L"错误", MB_OK|MB_ICONERROR); return; }
    pos++;
    while (pos < content.size()) {
        size_t ob = content.find('{', pos);
        size_t cb = content.find('}', pos);
        if (ob == string::npos || cb == string::npos || cb < ob) break;
        string obj = content.substr(ob, cb-ob+1);
        pos = cb + 1;
        DiaryEntry e; e.open_cnt = 0; e.year=e.mon=e.day=e.hour=e.min=e.sec=0;
        string ds, ts, tx;
        size_t dp = obj.find("\"date\"");
        if (dp != string::npos) { size_t q1 = obj.find('"', dp+7); size_t q2 = q1==string::npos?string::npos:obj.find('"', q1+1); if (q1!=string::npos&&q2!=string::npos) ds = obj.substr(q1+1, q2-q1-1); }
        size_t tp = obj.find("\"time\"");
        if (tp != string::npos) { size_t q1 = obj.find('"', tp+7); size_t q2 = q1==string::npos?string::npos:obj.find('"', q1+1); if (q1!=string::npos&&q2!=string::npos) ts = obj.substr(q1+1, q2-q1-1); }
        size_t xp = obj.find("\"text\"");
        if (xp != string::npos) { size_t q1 = obj.find('"', xp+7); size_t q2 = q1==string::npos?string::npos:obj.find('"', q1+1); if (q1!=string::npos&&q2!=string::npos) tx = obj.substr(q1+1, q2-q1-1); }
        int y,m,d,hh,mm,ssv;
        if (parseDateStr2(ds,y,m,d)) { e.year=y; e.mon=m; e.day=d; }
        if (parseTimeStr2(ts,hh,mm,ssv)) { e.hour=hh; e.min=mm; e.sec=ssv; }
        e.text = toWide(jsonUnescape(tx));
        if (e.year>0 && !e.text.empty()) imp.push_back(e);
    }
    if (imp.empty()) { MessageBoxW(g_hWnd, L"无有效数据", L"错误", MB_OK|MB_ICONERROR); return; }

    map<wstring, vector<DiaryEntry>> byDay;
    for (auto& ne : imp) {
        bool ex = false;
        for (auto& oe : g_entries)
            if (oe.dateStr()==ne.dateStr() && oe.timeStr()==ne.timeStr() && oe.text==ne.text) { ex=true; break; }
        if (!ex) byDay[ne.dateStr()].push_back(ne);
    }
    int added = 0;
    for (auto& kv : byDay) {
        for (auto& en : kv.second) {
            wstring base = allocEntryBase(kv.first, en.hour, en.min, en.sec);
            if (saveEntryFileBase(kv.first, base, en.text)) added++;
        }
    }
    refreshFromDisk();
    updateList();
    updateCalendar();
    updateTodayMsg();
    updateSidebarSummary();
    wchar_t msg[128];
    swprintf(msg, 128, L"导入完成: 新增 %d 条 (自动去重合并)", added);
    MessageBoxW(g_hWnd, msg, L"导入成功", MB_OK | MB_ICONINFORMATION);
}

// ============================================================
//  窗口过程
// ============================================================
static LRESULT CALLBACK JournalPageProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hJournalPage = hWnd;
        auto mkBtn = [&](const wchar_t* text, int id, BtnKind k) {
            HWND h = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                0, 0, 0, 0, hWnd, (HMENU)(INT_PTR)id, g_hInst, nullptr);
            regBtn(h, k);
            return h;
        };
        g_hPrev = mkBtn(L"‹", IDC_PREV, BK_GHOST);
        g_hNext = mkBtn(L"›", IDC_NEXT, BK_GHOST);
        g_hPrevYear = mkBtn(L"‹‹", IDC_PREVYEAR, BK_GHOST);
        g_hNextYear = mkBtn(L"››", IDC_NEXTYEAR, BK_GHOST);
        g_hTodayBtn = mkBtn(L"今天", IDC_TODAY, BK_SECONDARY);
        g_hSettingsBtn = mkBtn(L"设置", IDC_SETTINGS, BK_SECONDARY);
        g_hWriteBtn = mkBtn(L"＋ 写日记", IDC_WRITE, BK_PRIMARY);
        g_hYearLabel = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_CENTER,
            0, 0, 0, 0, hWnd, (HMENU)IDC_YEARLABEL, g_hInst, nullptr);
        g_hHintLabel = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            0, 0, 0, 0, hWnd, (HMENU)IDC_HINT, g_hInst, nullptr);
        g_hTodayMsgLabel = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
            0, 0, 0, 0, hWnd, (HMENU)IDC_TODAYMSG, g_hInst, nullptr);
        g_hTitleBar = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            0, 0, 0, 0, hWnd, (HMENU)IDC_TITLEBAR, g_hInst, nullptr);
        g_hList = CreateWindowW(L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT |
            LBS_OWNERDRAWFIXED | WS_VSCROLL | WS_BORDER,
            0, 0, 0, 0, hWnd, (HMENU)IDC_LIST, g_hInst, nullptr);
        g_hPlaceholder = CreateWindowW(L"STATIC", L"还没有日记 — 点右上角「写日记」开始记录吧",
            WS_CHILD | SS_CENTER | SS_CENTERIMAGE,
            0, 0, 0, 0, hWnd, (HMENU)IDC_PLACEHOLDER, g_hInst, nullptr);
        g_hDeleteBtn = mkBtn(L"删除", IDC_DELETE, BK_DANGER);
        g_hStatsBtn = mkBtn(L"统计", IDC_STATS, BK_SECONDARY);
        g_hExportBtn = mkBtn(L"导出文本", IDC_EXPORT, BK_SECONDARY);
        g_hImportBtn = mkBtn(L"导入", IDC_IMPORT, BK_SECONDARY);
        g_hOpenFolderBtn = mkBtn(L"打开数据目录", IDC_OPENFOLDER, BK_SECONDARY);
        g_hSchedBtn = mkBtn(L"日程", IDC_SCHED, BK_PRIMARY);
        relayout();
        updateCalendar();
        updateList();
        updateTodayMsg();
        break;
    }

    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) relayout();
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc; GetClientRect(hWnd, &rc);
        HDC mem = CreateCompatibleDC(hdc);
        int bw = max(1, (int)(rc.right - rc.left));
        int bh = max(1, (int)(rc.bottom - rc.top));
        HBITMAP bmp = CreateCompatibleBitmap(hdc, bw, bh);
        HGDIOBJ old = SelectObject(mem, bmp);
        HBRUSH hb = CreateSolidBrush(g_th.bg);
        FillRect(mem, &rc, hb);
        DeleteObject(hb);
        paintCalendar(mem);
        BitBlt(hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        int id = GetDlgCtrlID((HWND)lParam);
        if (id == IDC_YEARLABEL || id == IDC_TITLEBAR) SetTextColor(hdc, g_th.textPrimary);
        else if (id == IDC_TODAYMSG) SetTextColor(hdc, g_th.accent);
        else SetTextColor(hdc, g_th.textSecondary);
        return (LRESULT)g_hBrBg;
    }
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, g_th.card);
        SetTextColor(hdc, g_th.textPrimary);
        return (LRESULT)g_hBrCard;
    }
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT* mi = (MEASUREITEMSTRUCT*)lParam;
        if (mi->CtlID == IDC_LIST) { mi->itemHeight = S(34); return TRUE; }
        break;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lParam;
        if (g_btns.count(di->hwndItem)) { drawButton(di); return TRUE; }
        if (di->CtlID == IDC_LIST) { drawListItem(di); return TRUE; }
        break;
    }
    case WM_MOUSEMOVE: {
        if (!g_mouseTracking) {
            g_mouseTracking = true;
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
        }
        int idx = calHitTest((short)LOWORD(lParam), (short)HIWORD(lParam));
        if (idx != g_hoverCell) {
            g_hoverCell = idx;
            RECT rc = { g_L.calX, g_L.calY, g_L.calX+g_L.calW, g_L.calY+g_L.calH };
            InvalidateRect(hWnd, &rc, FALSE);
        }
        SetCursor(idx >= 0 ? g_hCursorHand : LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW));
        return 0;
    }
    case WM_MOUSELEAVE: {
        g_mouseTracking = false;
        if (g_hoverCell != -1) {
            g_hoverCell = -1;
            RECT rc = { g_L.calX, g_L.calY, g_L.calX+g_L.calW, g_L.calY+g_L.calH };
            InvalidateRect(hWnd, &rc, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int idx = calHitTest((short)LOWORD(lParam), (short)HIWORD(lParam));
        if (idx >= 0) onCalendarClick(idx);
        return 0;
    }
    case WM_CONTEXTMENU: {
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        if (pt.x == -1 && pt.y == -1) GetCursorPos(&pt);
        popContextMenu(pt);
        return 0;
    }
    case WM_TIMER:
        if (wParam == 1 && !g_todayMsgs.empty()) {
            g_todayMsgIdx = (g_todayMsgIdx + 1) % (int)g_todayMsgs.size();
            SetWindowTextW(g_hTodayMsgLabel, g_todayMsgs[g_todayMsgIdx].c_str());
        }
        return 0;
    case WM_USER + 5:
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    case WM_SJ_THEME:
        InvalidateRect(hWnd, nullptr, TRUE);
        SendMessageW(g_hList, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        return 0;
    case WM_SJ_REFRESH:
        refreshJournalPage();
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id >= MNU_WRITE && id <= MNU_COPY_AS_MARKDOWN) { handleCtxCommand(id); break; }
        switch (id) {
        case IDC_PREV:
            g_calMon--; if (g_calMon<1){g_calMon=12;g_calYear--;}
            requestHolidayYear(g_calYear);
            g_selectedDay=0; g_hoverCell=-1; updateCalendar(); checkFarJump(); break;
        case IDC_NEXT:
            g_calMon++; if (g_calMon>12){g_calMon=1;g_calYear++;}
            requestHolidayYear(g_calYear);
            g_selectedDay=0; g_hoverCell=-1; updateCalendar(); checkFarJump(); break;
        case IDC_PREVYEAR:
            g_calYear--; g_selectedDay=0; g_hoverCell=-1;
            requestHolidayYear(g_calYear); updateCalendar(); checkFarJump(); break;
        case IDC_NEXTYEAR:
            g_calYear++; g_selectedDay=0; g_hoverCell=-1;
            requestHolidayYear(g_calYear); updateCalendar(); checkFarJump(); break;
        case IDC_SETTINGS:
            openSettingsWindow(g_hWnd); break;
        case IDC_TODAY: {
            g_calYear=g_todayYear; g_calMon=g_todayMon;
            requestHolidayYear(g_calYear);
            g_farJumpWarned = false;
            int fd = firstDayOfWeek(g_calYear, g_calMon);
            onCalendarClick(fd + g_todayDay - 1);
            break;
        }
        case IDC_WRITE: {
            if (!g_hasDataDir) {
                MessageBoxW(g_hWnd, L"请先在设置中选择数据目录！", L"StudyJournal", MB_OK | MB_ICONINFORMATION);
                break;
            }
            wchar_t today[16];
            swprintf(today, 16, L"%04d-%02d-%02d", g_todayYear, g_todayMon, g_todayDay);
            openEntryEditor(wstring(today), true);
            break;
        }
        case IDC_DELETE: doDelete(); break;
        case IDC_STATS: doStats(); break;
        case IDC_EXPORT: doExport(); break;
        case IDC_IMPORT: doImport(); break;
        case IDC_OPENFOLDER: ShellExecuteW(nullptr, L"open", g_journalRoot.c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
        case IDC_SCHED: openSchedulesWindow(g_hWnd); updateTodayMsg(); break;
        case IDC_LIST:
            if (HIWORD(wParam) == LBN_DBLCLK) {
                int sel = (int)SendMessageW(g_hList, LB_GETCURSEL, 0, 0);
                if (sel != LB_ERR) {
                    int idx = (int)SendMessageW(g_hList, LB_GETITEMDATA, sel, 0);
                    if (idx >= 0 && idx < (int)g_entries.size()) {
                        auto& e = g_entries[idx];
                        g_calYear = e.year; g_calMon = e.mon; g_selectedDay = e.day;
                        updateCalendar();
                        InvalidateRect(hWnd, nullptr, FALSE);
                        openViewWindow(e.year, e.mon, e.day, e.base);
                    }
                }
            }
            break;
        }
        break;
    }
    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

HWND createJournalPage(HWND parent) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = JournalPageProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"StudyJournalJournalPage";
        RegisterClassExW(&wc);
        registered = true;
    }
    HWND h = CreateWindowExW(0, L"StudyJournalJournalPage", L"",
        WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 0, 0, parent, nullptr, g_hInst, nullptr);
    return h;
}

void refreshJournalPage() {
    if (!g_hJournalPage) return;
    refreshFromDisk();
    updateList();
    updateCalendar();
    updateTodayMsg();
    updateSidebarSummary();
}

void journalPageLayout() {
    if (g_hJournalPage) relayout();
}
