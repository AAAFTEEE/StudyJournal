// ============================================================
//  schedules.cpp — 自定义日程 / 倒数计时
//  模型 + 文本存储 (schedules.txt) + 倒数/计时计算 + 管理窗口
//  类型: 0 每年公历 / 1 每N年公历 / 2 单次公历 / 3 每年农历
// ============================================================
#include "app.h"

static vector<Sched> g_schedules;
static const wchar_t* SCHED_F = L"schedules.txt";
static const wchar_t* TYPE_NAME[] = { L"每年(公历)", L"每N年(公历)", L"单次(公历)", L"每年(农历)" };

// ---------------- 存取 ----------------
static wstring schedFilePath() { return joinPathW(g_journalRoot, SCHED_F); }

void loadSchedules() {
    g_schedules.clear();
    string b;
    if (!readWholeFile(schedFilePath(), b)) return;
    wstring content = decodeText(b);
    size_t pos = 0;
    while (pos < content.size()) {
        size_t eol = content.find(L'\n', pos);
        wstring line = (eol == wstring::npos) ? content.substr(pos) : content.substr(pos, eol - pos);
        pos = (eol == wstring::npos) ? content.size() : eol + 1;
        while (!line.empty() && (line.back()==L'\r'||line.back()==L'\n')) line.pop_back();
        if (line.empty() || line[0] == L'#') continue;
        // 字段: type \t name \t mon \t day \t interval \t startYear \t fixedYear \t countFrom \t note
        vector<wstring> f;
        size_t a = 0;
        while (a < line.size()) {
            size_t t = line.find(L'\t', a);
            if (t == wstring::npos) { f.push_back(line.substr(a)); break; }
            f.push_back(line.substr(a, t - a));
            a = t + 1;
        }
        if (f.size() < 9) continue;
        Sched s;
        try { s.type = stoi(f[0]); } catch (...) { continue; }
        s.name = f[1];
        try { s.mon = stoi(f[2]); s.day = stoi(f[3]); } catch (...) { continue; }
        try { s.intervalYears = stoi(f[4]); } catch (...) {}
        try { s.startYear = stoi(f[5]); } catch (...) {}
        try { s.fixedYear = stoi(f[6]); } catch (...) {}
        try { s.countFrom = (f[7] == L"1"); } catch (...) {}
        try { s.cat = (f.size() > 9) ? stoi(f[9]) : 0; } catch (...) { s.cat = 0; }  // v2: 分类标签
        s.note = (f.size() > 8) ? f[8] : L"";
        if (s.cat < SCAT_ORDINARY || s.cat > SCAT_REMIND) s.cat = SCAT_ORDINARY;
        g_schedules.push_back(s);
    }
}

void saveSchedules() {
    wstring out = L"# DayJournal schedules v2\n";
    for (auto& s : g_schedules) {
        wstring line = to_wstring(s.type) + L"\t" + s.name + L"\t" +
            to_wstring(s.mon) + L"\t" + to_wstring(s.day) + L"\t" +
            to_wstring(s.intervalYears) + L"\t" + to_wstring(s.startYear) + L"\t" +
            to_wstring(s.fixedYear) + L"\t" + (s.countFrom ? L"1" : L"0") + L"\t" + s.note +
            L"\t" + to_wstring(s.cat);
        out += line + L"\n";
    }
    writeWholeFile(schedFilePath(), toUTF8(out));
}

// ---------------- 计算 ----------------
static bool isLeapYear(int y) { return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)); }
static int maxDayOfMonth(int y, int m) {
    static int dm[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2 && isLeapYear(y)) return 29;
    return (m >= 1 && m <= 12) ? dm[m] : 30;
}

bool nextOccurrence(const Sched& s, int& y, int& m, int& d) {
    time_t now = time(nullptr); tm* lt = localtime(&now);
    int ty = lt->tm_year + 1900, tm_ = lt->tm_mon + 1, td = lt->tm_mday;
    long today = civilDays(ty, tm_, td);

    if (s.type == 2) {   // 单次公历
        y = s.fixedYear; m = s.mon; d = s.day;
        return civilDays(y, m, d) >= today;
    }
    if (s.type == 3) {   // 每年农历: 顺推扫描 (最多 380 天覆盖一年)
        for (long k = 0; k <= 380; k++) {
            int yy, mm, dd; civilFromDays(today + k, yy, mm, dd);
            LunarDate l = solarToLunar(yy, mm, dd);
            if (!l.isLeap && l.month == s.mon && l.day == s.day) { y = yy; m = mm; d = dd; return true; }
        }
        return false;
    }
    // type 0 / 1 公历
    int y0 = (s.type == 0) ? ty : s.startYear;
    if (s.type == 1) {
        if (y0 < ty && s.intervalYears > 0) {
            long diff = ty - y0;
            long step = diff / s.intervalYears + (diff % s.intervalYears ? 1 : 0);
            y0 = s.startYear + (int)(step * s.intervalYears);
        }
        if (y0 < ty) y0 = ty;
        for (int cand = y0; cand <= y0 + s.intervalYears; cand += s.intervalYears) {
            if (s.day > maxDayOfMonth(cand, s.mon)) continue;
            if (civilDays(cand, s.mon, s.day) >= today) { y = cand; m = s.mon; d = s.day; return true; }
        }
        return false;
    }
    // type 0 每年
    int yy0 = ty;
    if (s.mon == 2 && s.day == 29 && !isLeapYear(yy0)) while (!isLeapYear(yy0)) yy0++;
    if (s.day > maxDayOfMonth(yy0, s.mon)) yy0++;
    if (civilDays(yy0, s.mon, s.day) < today) {
        yy0++;
        if (s.mon == 2 && s.day == 29 && !isLeapYear(yy0)) while (!isLeapYear(yy0)) yy0++;
        if (s.day > maxDayOfMonth(yy0, s.mon)) yy0++;
    }
    y = yy0; m = s.mon; d = s.day;
    return true;
}

bool previousOccurrence(const Sched& s, int& y, int& m, int& d) {
    time_t now = time(nullptr); tm* lt = localtime(&now);
    int ty = lt->tm_year + 1900, tm_ = lt->tm_mon + 1, td = lt->tm_mday;
    long today = civilDays(ty, tm_, td);
    if (s.type == 2) { y = s.fixedYear; m = s.mon; d = s.day; return civilDays(y,m,d) <= today; }
    if (s.type == 3) {
        for (long k = 0; k <= 380; k++) {
            int yy, mm, dd; civilFromDays(today - k, yy, mm, dd);
            LunarDate l = solarToLunar(yy, mm, dd);
            if (!l.isLeap && l.month == s.mon && l.day == s.day) { y = yy; m = mm; d = dd; return true; }
        }
        return false;
    }
    int y0 = (s.type == 0) ? ty : s.startYear;
    if (s.type == 1) {
        if (y0 > ty && s.intervalYears > 0) {
            long diff = y0 - ty;
            long step = (diff + s.intervalYears - 1) / s.intervalYears;
            y0 = s.startYear - (int)(step * s.intervalYears);
        }
        if (y0 > ty) y0 = ty;
        for (int cand = y0; cand >= s.startYear && cand >= ty - s.intervalYears; cand -= s.intervalYears) {
            if (s.day > maxDayOfMonth(cand, s.mon)) continue;
            if (civilDays(cand, s.mon, s.day) <= today) { y = cand; m = s.mon; d = s.day; return true; }
        }
        return false;
    }
    int yy0 = ty;
    if (s.mon == 2 && s.day == 29 && !isLeapYear(yy0)) while (!isLeapYear(yy0)) yy0--;
    if (s.day > maxDayOfMonth(yy0, s.mon)) yy0--;
    if (civilDays(yy0, s.mon, s.day) > today) {
        yy0--;
        if (s.mon == 2 && s.day == 29 && !isLeapYear(yy0)) while (!isLeapYear(yy0)) yy0--;
        if (s.day > maxDayOfMonth(yy0, s.mon)) yy0--;
    }
    y = yy0; m = s.mon; d = s.day;
    return true;
}

long daysRelativeToToday(int y, int m, int d) {
    time_t now = time(nullptr); tm* lt = localtime(&now);
    long today = civilDays(lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
    return civilDays(y, m, d) - today;
}

bool anyScheduleOn(int y, int m, int d) {
    for (auto& s : g_schedules) {
        if (s.type == 0) { if (s.mon == m && s.day == d) return true; }
        else if (s.type == 1) {
            if (s.intervalYears > 0 && y >= s.startYear &&
                (y - s.startYear) % s.intervalYears == 0 && s.mon == m && s.day == d) return true;
        }
        else if (s.type == 2) { if (y == s.fixedYear && s.mon == m && s.day == d) return true; }
        else {
            LunarDate l = solarToLunar(y, m, d);
            if (!l.isLeap && l.month == s.mon && l.day == s.day) return true;
        }
    }
    return false;
}

// 今天命中的日程 → 提示文本 (主界面滚动显示)
vector<wstring> todayScheduleMessages() {
    vector<wstring> out;
    time_t now = time(nullptr); tm* lt = localtime(&now);
    int ty = lt->tm_year + 1900, tm_ = lt->tm_mon + 1, td = lt->tm_mday;
    for (auto& s : g_schedules) {
        bool hit = false;
        if (s.type == 0) hit = (s.mon == tm_ && s.day == td);
        else if (s.type == 1) hit = (s.intervalYears > 0 && ty >= s.startYear &&
                                     (ty - s.startYear) % s.intervalYears == 0 && s.mon == tm_ && s.day == td);
        else if (s.type == 2) hit = (ty == s.fixedYear && s.mon == tm_ && s.day == td);
        else { LunarDate l = solarToLunar(ty, tm_, td); hit = (!l.isLeap && l.month == s.mon && l.day == s.day); }
        if (!hit) continue;
        wstring m;
        switch (s.cat) {
        case SCAT_BIRTHDAY:    m = L"今天是 " + s.name + L" 的生日，别忘了祝Ta生日快乐！"; break;
        case SCAT_ANNIVERSARY: m = L"今天是 " + s.name + L" 的纪念日，祝你们越来越好！"; break;
        case SCAT_EXAM:        m = L"今天是 " + s.name + L" 考试日，祝旗开得胜！"; break;
        case SCAT_FESTIVAL:    m = L"今天是 " + s.name + L"，节日快乐！"; break;
        case SCAT_REMIND:      m = L"今天是日程：" + s.name; break;
        default:               m = L"今天是日程：" + s.name; break;
        }
        if (!s.note.empty()) m += L"（" + s.note + L"）";
        out.push_back(m);
    }
    return out;
}

// 日程列表显示条目 (倒数/计时/已结束), 并按下一次日期排序
struct SchedView {
    size_t idx;
    wstring name;      // 日程名称
    int    cat;        // 分类
    wstring tail;      // 倒数/计时 + 日期, 如 "还有 3 天 (2026-10-21)"
    long sortKey;
};
static vector<SchedView> g_view;

// 分类 → 标签颜色 (在浅/深主题下均可见)
static COLORREF schedCatColor(int cat) {
    switch (cat) {
    case SCAT_BIRTHDAY:    return RGB(234, 88, 88);    // 红
    case SCAT_ANNIVERSARY: return RGB(217, 119, 6);   // 金橙
    case SCAT_EXAM:        return RGB(16, 122, 101);  // 深青
    case SCAT_FESTIVAL:    return RGB(124, 58, 237);  // 紫
    case SCAT_REMIND:      return RGB(2, 132, 199);   // 蓝
    default:               return RGB(107, 114, 128); // 灰
    }
}

// 日程列表显示条目
static void rebuildSchedView() {
    g_view.clear();
    for (size_t i = 0; i < g_schedules.size(); i++) {
        auto& s = g_schedules[i];
        SchedView sv; sv.idx = i; sv.name = s.name; sv.cat = s.cat;
        wstring t;
        int ny, nm, nd;
        if (nextOccurrence(s, ny, nm, nd)) {
            long d = daysRelativeToToday(ny, nm, nd);
            if (s.countFrom) {
                int py, pm, pd;
                if (previousOccurrence(s, py, pm, pd)) {
                    long e = daysRelativeToToday(py, pm, pd);
                    wchar_t db[64];
                    swprintf(db, 64, L"已过 %ld 天  (%04d-%02d-%02d)", e < 0 ? 0 : e, ny, nm, nd);
                    t += db;
                }
            } else {
                wchar_t db[64];
                swprintf(db, 64, L"还有 %ld 天  (%04d-%02d-%02d)", d < 0 ? 0 : d, ny, nm, nd);
                t += db;
            }
            if (!s.note.empty()) t += L"   " + s.note;
            sv.tail = t;
            sv.sortKey = civilDays(ny, nm, nd);
            g_view.push_back(sv);
        } else {
            wchar_t db[64];
            swprintf(db, 64, L"已结束  (%04d-%02d-%02d)", s.fixedYear, s.mon, s.day);
            t += db;
            if (!s.note.empty()) t += L"   " + s.note;
            sv.tail = t;
            sv.sortKey = LONG_MAX;
            g_view.push_back(sv);
        }
    }
    sort(g_view.begin(), g_view.end(), [](const SchedView& a, const SchedView& b){ return a.sortKey < b.sortKey; });
}

// ---------------- 添加/编辑对话框 ----------------
static Sched g_editSched;
static bool  g_editOk = false;
static bool  g_editIsNew = false;

static const wchar_t* SED_CLASS = L"StudyJournalSchedEditWnd";
enum { SED_NAME=201, SED_TYPE, SED_MON, SED_DAY, SED_INTERVAL, SED_STARTY,
       SED_FIXEDY, SED_COUNT, SED_NOTE, SED_CAT, SED_OK=1, SED_CANCEL=2 };

// 前置声明: 由 SchedEditProc 在类型切换 / 输入变化时调用
static void refreshSchedLunar(HWND h);

static LRESULT CALLBACK SchedEditProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_COMMAND: {
        int id = LOWORD(w);
        // 类型切换 → 刷新农历月日标签; 月/日输入变化 → 刷新农历预览
        if ((id == SED_TYPE && HIWORD(w) == CBN_SELCHANGE) ||
            ((id == SED_MON || id == SED_DAY) && HIWORD(w) == EN_CHANGE)) {
            refreshSchedLunar(h);
            return 0;
        }
        if (id == SED_OK) {
            auto txt = [&](int cid) {
                HWND e = GetDlgItem(h, cid);
                int len = GetWindowTextLengthW(e);
                wstring t(len, 0); if (len) GetWindowTextW(e, &t[0], len + 1);
                return t;
            };
            g_editSched.name = txt(SED_NAME);
            if (g_editSched.name.empty()) { MessageBoxW(h, L"请输入日程名称", L"StudyJournal", MB_OK); return 0; }
            int type = (int)SendMessageW(GetDlgItem(h, SED_TYPE), CB_GETCURSEL, 0, 0);
            if (type < 0) type = 0;
            g_editSched.type = type;
            try { g_editSched.mon = stoi(txt(SED_MON)); } catch (...) { g_editSched.mon = 1; }
            try { g_editSched.day = stoi(txt(SED_DAY)); } catch (...) { g_editSched.day = 1; }
            try { g_editSched.intervalYears = stoi(txt(SED_INTERVAL)); } catch (...) { g_editSched.intervalYears = 1; }
            try { g_editSched.startYear = stoi(txt(SED_STARTY)); } catch (...) { g_editSched.startYear = 0; }
            try { g_editSched.fixedYear = stoi(txt(SED_FIXEDY)); } catch (...) { g_editSched.fixedYear = 0; }
            int cnt = (int)SendMessageW(GetDlgItem(h, SED_COUNT), CB_GETCURSEL, 0, 0);
            g_editSched.countFrom = (cnt == 1);
            g_editSched.cat = (int)SendMessageW(GetDlgItem(h, SED_CAT), CB_GETCURSEL, 0, 0);
            if (g_editSched.cat < 0) g_editSched.cat = SCAT_ORDINARY;
            g_editSched.note = txt(SED_NOTE);
            if (g_editSched.mon < 1 || g_editSched.mon > 12 || g_editSched.day < 1 || g_editSched.day > 31) {
                MessageBoxW(h, L"月份需在 1-12, 日期需在 1-31", L"StudyJournal", MB_OK); return 0;
            }
            if (type == 1 && g_editSched.intervalYears < 1) g_editSched.intervalYears = 1;
            if (type == 2 && g_editSched.fixedYear < 1900) g_editSched.fixedYear = 2026;
            g_editOk = true;
            DestroyWindow(h);
        } else if (id == SED_CANCEL) DestroyWindow(h);
        return 0;
    }
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)w;
        RECT rc; GetClientRect(h, &rc);
        HBRUSH hb = CreateSolidBrush(g_th.bg);
        FillRect(hdc, &rc, hb);
        DeleteObject(hb);
        return 1;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)w;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, g_th.textPrimary);
        return (LRESULT)g_hBrBg;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)w;
        SetBkColor(hdc, g_th.card);
        SetTextColor(hdc, g_th.textPrimary);
        return (LRESULT)g_hBrCard;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)l;
        if (g_btns.count(di->hwndItem)) { drawButton(di); return TRUE; }
        break;
    }
    case WM_CLOSE: DestroyWindow(h); return 0;
    default: return DefWindowProcW(h, m, w, l);
    }
    return 0;
}

static void setEditText(HWND h, int cid, const wstring& v) {
    SetWindowTextW(GetDlgItem(h, cid), v.c_str());
}
static void addEditControl(HWND parent, const wchar_t* label, int x, int y, int w, int id,
                           int hh = 28, bool numeric = false) {
    CreateWindowW(L"STATIC", label, WS_CHILD | WS_VISIBLE, x, y, w, S(20), parent, nullptr, g_hInst, nullptr);
    HWND e = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        x, y + S(22), w, S(hh), parent, (HMENU)(INT_PTR)id, g_hInst, nullptr);
    SendMessageW(e, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    (void)numeric;
}
static HWND addCombo(HWND parent, int id, int x, int y, int w) {
    HWND c = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
        x, y + S(22), w, S(160), parent, (HMENU)(INT_PTR)id, g_hInst, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    return c;
}
static void addLabel(HWND parent, const wchar_t* t, int x, int y, int w) {
    HWND s = CreateWindowW(L"STATIC", t, WS_CHILD | WS_VISIBLE, x, y, w, S(20), parent, nullptr, g_hInst, nullptr);
    SendMessageW(s, WM_SETFONT, (WPARAM)g_hFont, TRUE);
}
static HWND addLabelRet(HWND parent, const wchar_t* t, int x, int y, int w) {
    HWND s = CreateWindowW(L"STATIC", t, WS_CHILD | WS_VISIBLE, x, y, w, S(20), parent, nullptr, g_hInst, nullptr);
    SendMessageW(s, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    return s;
}

// 日程编辑对话框里, 类型切到"农历"时, 月/日标签与预览 (跨过程共享)
static HWND g_edMonLbl = nullptr, g_edDayLbl = nullptr, g_edPrev = nullptr;

// 根据当前类型刷新 月/日 标签与农历预览
static void refreshSchedLunar(HWND h) {
    if (!g_edMonLbl || !g_edDayLbl || !g_edPrev) return;
    int type = (int)SendMessageW(GetDlgItem(h, SED_TYPE), CB_GETCURSEL, 0, 0);
    if (type == 3) {
        SetWindowTextW(g_edMonLbl, L"农历月:");
        SetWindowTextW(g_edDayLbl, L"农历日:");
        int mon = 1, day = 1;
        auto edTxt = [&](int cid) {
            HWND e = GetDlgItem(h, cid);
            int len = GetWindowTextLengthW(e);
            wstring t(len, 0); if (len) GetWindowTextW(e, &t[0], len + 1);
            return t;
        };
        try { mon = stoi(edTxt(SED_MON)); } catch (...) {}
        try { day = stoi(edTxt(SED_DAY)); } catch (...) {}
        wstring nm = lunarMonthDayName(mon, day);
        SetWindowTextW(g_edPrev, (nm.empty() ? wstring() : (wstring(L"≈ ") + nm)).c_str());
    } else {
        SetWindowTextW(g_edMonLbl, L"月:");
        SetWindowTextW(g_edDayLbl, L"日:");
        SetWindowTextW(g_edPrev, L"");
    }
}

static void openSchedEdit(HWND parent, Sched s, bool isNew) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = SchedEditProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = SED_CLASS;
        RegisterClassExW(&wc);
        registered = true;
    }
    g_editSched = s; g_editOk = false; g_editIsNew = isNew;
    // 客户区高度至少要到 16 + lh*8 + 按钮高(32) + 边距, 否则确定/取消被裁掉
    RECT rc = {0, 0, S(440), S(600)};
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    HWND h = CreateWindowExW(0, SED_CLASS, isNew ? L"新增日程" : L"编辑日程",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        parent, nullptr, g_hInst, nullptr);
    if (!h) return;
    DWORD pref = 2; DwmSetWindowAttribute(h, 33, &pref, sizeof(pref));
    COLORREF c = g_th.bg; DwmSetWindowAttribute(h, 35, &c, sizeof(c));

    int x = S(24), w = S(390), lh = S(64);
    addLabel(h, L"名称:", x, S(16), S(80));
    HWND nm = CreateWindowW(L"EDIT", s.name.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        x + S(90), S(16), w - S(90), S(28), h, (HMENU)SED_NAME, g_hInst, nullptr);
    SendMessageW(nm, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    addLabel(h, L"类型:", x, S(16) + lh, S(80));
    HWND type = addCombo(h, SED_TYPE, x + S(90), S(16) + lh, S(150));
    for (int i = 0; i < 4; i++) SendMessageW(type, CB_ADDSTRING, 0, (LPARAM)TYPE_NAME[i]);
    SendMessageW(type, CB_SETCURSEL, s.type, 0);

    g_edMonLbl = addLabelRet(h, L"月:", x, S(16) + lh*2, S(60));
    addEditControl(h, L"", x + S(40), S(16) + lh*2, S(80), SED_MON, 28, true);
    setEditText(h, SED_MON, to_wstring(s.mon));
    g_edDayLbl = addLabelRet(h, L"日:", x + S(140), S(16) + lh*2, S(50));
    addEditControl(h, L"", x + S(180), S(16) + lh*2, S(80), SED_DAY, 28, true);
    setEditText(h, SED_DAY, to_wstring(s.day));
    g_edPrev = addLabelRet(h, L"", x + S(270), S(16) + lh*2, S(110));

    addLabel(h, L"每 N 年(每N年类型):", x, S(16) + lh*3, S(170));
    addEditControl(h, L"", x + S(190), S(16) + lh*3, S(80), SED_INTERVAL, 28, true);
    setEditText(h, SED_INTERVAL, to_wstring(s.intervalYears));
    addLabel(h, L"起始年:", x + S(285), S(16) + lh*3, S(60));
    addEditControl(h, L"", x + S(335), S(16) + lh*3, S(80), SED_STARTY, 28, true);
    setEditText(h, SED_STARTY, to_wstring(s.startYear));

    addLabel(h, L"固定年份(单次类型):", x, S(16) + lh*4, S(180));
    addEditControl(h, L"", x + S(190), S(16) + lh*4, S(80), SED_FIXEDY, 28, true);
    setEditText(h, SED_FIXEDY, to_wstring(s.fixedYear));

    addLabel(h, L"模式:", x, S(16) + lh*5, S(60));
    HWND cnt = addCombo(h, SED_COUNT, x + S(90), S(16) + lh*5, S(150));
    SendMessageW(cnt, CB_ADDSTRING, 0, (LPARAM)L"倒数日 (还有几天)");
    SendMessageW(cnt, CB_ADDSTRING, 0, (LPARAM)L"计时日 (已过几天)");
    SendMessageW(cnt, CB_SETCURSEL, s.countFrom ? 1 : 0, 0);

    addLabel(h, L"分类:", x, S(16) + lh*6, S(60));
    HWND cat = addCombo(h, SED_CAT, x + S(90), S(16) + lh*6, S(150));
    for (int i = 0; i < 6; i++) SendMessageW(cat, CB_ADDSTRING, 0, (LPARAM)SCAT_NAME[i]);
    SendMessageW(cat, CB_SETCURSEL, s.cat, 0);

    addLabel(h, L"备注(可选):", x, S(16) + lh*7, S(120));
    HWND note = CreateWindowW(L"EDIT", s.note.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        x + S(130), S(16) + lh*7, w - S(130), S(28), h, (HMENU)SED_NOTE, g_hInst, nullptr);
    SendMessageW(note, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    HWND ok = CreateWindowW(L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        S(230), S(16) + lh*8, S(80), S(32), h, (HMENU)SED_OK, g_hInst, nullptr); regBtn(ok, BK_PRIMARY);
    HWND cancel = CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        S(322), S(16) + lh*8, S(80), S(32), h, (HMENU)SED_CANCEL, g_hInst, nullptr); regBtn(cancel, BK_SECONDARY);
    SendMessageW(ok, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    SendMessageW(cancel, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    refreshSchedLunar(h);   // 按当前类型设置 月/日 标签与农历预览
    ShowWindow(h, SW_SHOW);
    MSG msg;
    while (IsWindow(h) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) { PostMessageW(h, WM_COMMAND, SED_OK, 0); continue; }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) { PostMessageW(h, WM_COMMAND, SED_CANCEL, 0); continue; }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

// ---------------- 主日程窗口 ----------------
static HWND g_schedWnd = nullptr;
static const wchar_t* SCHE_CLASS = L"StudyJournalSchedWnd";
static const int SCHE_LIST = 1100, SCHE_ADD = 1111, SCHE_EDIT = 1112, SCHE_DEL = 1113, SCHE_CLOSE = 1114;

HWND schedulesWindow() { return g_schedWnd; }

static void fillSchedList(HWND list) {
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (auto& sv : g_view) {
        wstring disp = sv.name + L"   " + sv.tail;
        LRESULT idx = SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)disp.c_str());
        SendMessageW(list, LB_SETITEMDATA, (WPARAM)idx, (LPARAM)sv.idx);
    }
}

static void schedLayout(HWND h) {
    RECT rc; GetClientRect(h, &rc);
    HWND list = GetDlgItem(h, SCHE_LIST);
    HWND add = GetDlgItem(h, SCHE_ADD), ed = GetDlgItem(h, SCHE_EDIT),
         del = GetDlgItem(h, SCHE_DEL), close_ = GetDlgItem(h, SCHE_CLOSE);
    SetWindowPos(list,  nullptr, S(16), S(14), rc.right - S(32), rc.bottom - S(96), SWP_NOZORDER);
    SetWindowPos(add,   nullptr, S(16), rc.bottom - S(66), S(84), S(38), SWP_NOZORDER);
    SetWindowPos(ed,    nullptr, S(110), rc.bottom - S(66), S(84), S(38), SWP_NOZORDER);
    SetWindowPos(del,   nullptr, S(204), rc.bottom - S(66), S(84), S(38), SWP_NOZORDER);
    SetWindowPos(close_,nullptr, rc.right - S(120), rc.bottom - S(66), S(100), S(38), SWP_NOZORDER);
}

static LRESULT CALLBACK SchedWndProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HWND list = CreateWindowW(L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT |
            LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_VSCROLL | WS_BORDER,
            0, 0, 0, 0, h, (HMENU)(INT_PTR)SCHE_LIST, g_hInst, nullptr);
        HWND add = CreateWindowW(L"BUTTON", L"新增", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 0, 0, h, (HMENU)(INT_PTR)SCHE_ADD, g_hInst, nullptr); regBtn(add, BK_PRIMARY);
        HWND ed = CreateWindowW(L"BUTTON", L"编辑", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 0, 0, h, (HMENU)(INT_PTR)SCHE_EDIT, g_hInst, nullptr); regBtn(ed, BK_SECONDARY);
        HWND del = CreateWindowW(L"BUTTON", L"删除", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 0, 0, h, (HMENU)(INT_PTR)SCHE_DEL, g_hInst, nullptr); regBtn(del, BK_DANGER);
        HWND close_ = CreateWindowW(L"BUTTON", L"关闭", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 0, 0, h, (HMENU)(INT_PTR)SCHE_CLOSE, g_hInst, nullptr); regBtn(close_, BK_SECONDARY);
        for (HWND c : { list, add, ed, del, close_ }) SendMessageW(c, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        rebuildSchedView();
        fillSchedList(list);
        schedLayout(h);
        break;
    }
    case WM_SIZE: schedLayout(h); return 0;
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc; GetClientRect(h, &rc);
        HBRUSH hb = CreateSolidBrush(g_th.bg);
        FillRect(hdc, &rc, hb);
        DeleteObject(hb);
        return 1;
    }
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, g_th.card);
        return (LRESULT)g_hBrCard;
    }
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT* mi = (MEASUREITEMSTRUCT*)lParam;
        if (mi->CtlID == SCHE_LIST) { mi->itemHeight = S(42); return TRUE; }
        break;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lParam;
        if (g_btns.count(di->hwndItem)) { drawButton(di); return TRUE; }
        if (di->CtlID == SCHE_LIST) {
            HDC hdc = di->hDC;
            RECT rc = di->rcItem;
            bool selected = (di->itemState & ODS_SELECTED) != 0;
            HBRUSH hb = CreateSolidBrush(selected ? g_th.accentLight : g_th.card);
            FillRect(hdc, &rc, hb);
            DeleteObject(hb);
            size_t idx = (size_t)di->itemData;
            if (idx < g_schedules.size()) {
                SetBkMode(hdc, TRANSPARENT);
                // 用 sv (g_view) 拿 name/tail; 通过 idx 反查 g_view
                for (auto& sv : g_view) if (sv.idx == idx) {
                    // 分类标签
                    COLORREF lc = schedCatColor(sv.cat);
                    int tagW = S(64), tagH = S(22);
                    int tagY = rc.top + (rc.bottom - rc.top - tagH) / 2;
                    HBRUSH tb = CreateSolidBrush(lc);
                    HGDIOBJ op = SelectObject(hdc, (HPEN)GetStockObject(NULL_PEN));
                    RoundRect(hdc, rc.left + S(12), tagY, rc.left + S(12) + tagW, tagY + tagH, S(11), S(11));
                    SelectObject(hdc, op); DeleteObject(tb);
                    SetTextColor(hdc, RGB(255,255,255));
                    SelectObject(hdc, g_hFontSmall);
                    RECT tr = { rc.left + S(12), tagY, rc.left + S(12) + tagW, tagY + tagH };
                    DrawTextW(hdc, SCAT_NAME[sv.cat], -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    // 名称
                    SetTextColor(hdc, selected ? g_th.accentDark : g_th.textPrimary);
                    SelectObject(hdc, g_hFont);
                    RECT rn = { rc.left + S(12) + tagW + S(10), rc.top, rc.left + S(280), rc.bottom };
                    DrawTextW(hdc, sv.name.c_str(), -1, &rn, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                    // 倒数/计时 + 日期
                    SetTextColor(hdc, g_th.textSecondary);
                    SelectObject(hdc, g_hFontSmall);
                    RECT rt = { rc.right - S(300), rc.top, rc.right - S(12), rc.bottom };
                    DrawTextW(hdc, sv.tail.c_str(), -1, &rt, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                    break;
                }
            }
            return TRUE;
        }
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        switch (id) {
        case SCHE_ADD: {
            Sched s;
            time_t now = time(nullptr); tm* lt = localtime(&now);
            s.mon = lt->tm_mon + 1; s.day = lt->tm_mday;
            s.intervalYears = 1; s.startYear = lt->tm_year + 1900;
            s.fixedYear = lt->tm_year + 1900; s.countFrom = false;
            openSchedEdit(h, s, true);
            if (g_editOk) {
                g_schedules.push_back(g_editSched);
                saveSchedules();
                rebuildSchedView();
                fillSchedList(GetDlgItem(h, SCHE_LIST));
                InvalidateRect(g_hWnd, nullptr, FALSE);
            }
            break;
        }
        case SCHE_EDIT: {
            HWND list = GetDlgItem(h, SCHE_LIST);
            int sel = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
            if (sel == LB_ERR) { MessageBoxW(h, L"请先选中一个日程", L"StudyJournal", MB_OK); break; }
            size_t idx = (size_t)SendMessageW(list, LB_GETITEMDATA, sel, 0);
            if (idx >= g_schedules.size()) break;
            openSchedEdit(h, g_schedules[idx], false);
            if (g_editOk) {
                g_schedules[idx] = g_editSched;
                saveSchedules();
                rebuildSchedView();
                fillSchedList(list);
                InvalidateRect(g_hWnd, nullptr, FALSE);
            }
            break;
        }
        case SCHE_DEL: {
            HWND list = GetDlgItem(h, SCHE_LIST);
            int sel = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
            if (sel == LB_ERR) break;
            size_t idx = (size_t)SendMessageW(list, LB_GETITEMDATA, sel, 0);
            if (idx >= g_schedules.size()) break;
            if (MessageBoxW(h, (L"删除日程 \"" + g_schedules[idx].name + L"\" ?").c_str(),
                            L"StudyJournal", MB_YESNO | MB_ICONWARNING) == IDYES) {
                g_schedules.erase(g_schedules.begin() + idx);
                saveSchedules();
                rebuildSchedView();
                fillSchedList(list);
                InvalidateRect(g_hWnd, nullptr, FALSE);
            }
            break;
        }
        case SCHE_CLOSE: DestroyWindow(h); break;
        }
        return 0;
    }
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: g_schedWnd = nullptr; break;
    default: return DefWindowProcW(h, msg, wParam, lParam);
    }
    return 0;
}

void openSchedulesWindow(HWND parent) {
    if (g_schedWnd) { SetForegroundWindow(g_schedWnd); return; }
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = SchedWndProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = SCHE_CLASS;
        RegisterClassExW(&wc);
        registered = true;
    }
    RECT rc = {0, 0, S(520), S(460)};
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    g_schedWnd = CreateWindowExW(0, SCHE_CLASS, L"StudyJournal 日程与倒数",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        parent, nullptr, g_hInst, nullptr);
    if (!g_schedWnd) return;
    DWORD pref = 2; DwmSetWindowAttribute(g_schedWnd, 33, &pref, sizeof(pref));
    COLORREF c = g_th.bg; DwmSetWindowAttribute(g_schedWnd, 35, &c, sizeof(c));
    ShowWindow(g_schedWnd, SW_SHOW);
    UpdateWindow(g_schedWnd);
}
