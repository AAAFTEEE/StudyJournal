// ============================================================
//  tasks_page.cpp — 待办 / 作业页面
//  从 StudyTodo v4 迁移: 科目树 + 任务表 + 优先级/截止/标签、
//  就地编辑、筛选/搜索/排序、番茄钟、进度统计、多作业单。
// ============================================================
#include "app.h"

HWND g_hTasksPage = nullptr;

static HWND g_hTree = nullptr, g_hTaskList = nullptr;
static HWND g_hProgress = nullptr, g_hStatus = nullptr, g_hStatText = nullptr;
static HWND g_hSearch = nullptr, g_hSort = nullptr, g_hListName = nullptr;

enum ViewMode { VM_ALL, VM_TODAY, VM_UNDONE, VM_DONE, VM_SUBJECT };
static ViewMode g_view = VM_ALL;
static int  g_viewSubject = -1;
static wstring g_searchWord;
enum SortMode { S_ORIG, S_PRIORITY, S_DUE, S_SUBJECT };
static SortMode g_sort = S_ORIG;
static vector<pair<int,int>> g_visible;
static bool g_rebuildingTree = false;

// 控件 ID (与日记页 / 编辑器的 ID 段错开)
enum {
    ID_TREE = 3300, ID_LIST, ID_PROGRESS, ID_STAT,
    ID_SEARCH = 3310, ID_SORT,
    BTN_NEW = 3320, BTN_OPEN, BTN_SAVE, BTN_SAVEAS,
    BTN_ADDSUBJ, BTN_ADDTASK, BTN_EDIT, BTN_DEL, BTN_TOGGLE,
    BTN_POMO, BTN_STATS,
    ID_TASK_TEXT = 3400, ID_TASK_PRIO, ID_TASK_DUE, ID_TASK_TAG,
    ID_SUBJ_NAME, ID_OPEN_LIST, ID_OPEN_OPEN, ID_OPEN_DEL, ID_OPEN_REFRESH,
    ID_SAVEAS_NAME,
    ID_POMO_LABEL = 3500, ID_POMO_BAR, ID_POMO_START, ID_POMO_PAUSE,
    ID_POMO_RESET, ID_POMO_CLOSE
};

// 表格内就地编辑
static HWND    g_ipEdit = nullptr;
static WNDPROC g_ipEditOrig = nullptr;
static int     g_ipRow = -1, g_ipCol = -1;
static bool    g_ipCancel = false;

// 番茄钟
static bool g_pomoRunning = false, g_pomoPaused = false;
static int  g_pomoRemain = 25 * 60;
static HWND g_hPomoWnd = nullptr, g_hPomoLabel = nullptr, g_hPomoBar = nullptr;

static int g_dlgSubj = -1, g_dlgIdx = -1;
static bool g_insertMode = false;
static int  g_insertPos = 0;
static bool g_newListMode = false;

static wstring trimW2(const wstring& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == L' ' || s[b] == L'\t' || s[b] == L'\r' || s[b] == L'\n')) b++;
    while (e > b && (s[e-1] == L' ' || s[e-1] == L'\t' || s[e-1] == L'\r' || s[e-1] == L'\n')) e--;
    return s.substr(b, e - b);
}

static COLORREF subjectTint(int idx) {
    static const COLORREF light[] = {
        RGB(228,238,252), RGB(226,246,231), RGB(254,241,226), RGB(242,232,252),
        RGB(253,232,239), RGB(224,246,246), RGB(252,248,222), RGB(253,231,231),
    };
    static const COLORREF dark[] = {
        RGB(38,50,74), RGB(36,62,44), RGB(64,54,36), RGB(54,44,70),
        RGB(70,44,56), RGB(34,60,60), RGB(62,58,34), RGB(70,42,42),
    };
    if (isDarkTheme()) return dark[idx % 8];
    return light[idx % 8];
}
static COLORREF doneTextColor() { return isDarkTheme() ? RGB(110,190,110) : RGB(60,130,60); }
static COLORREF prioColor(int p) {
    if (p == 2) return isDarkTheme() ? RGB(248,113,113) : RGB(200,52,52);
    if (p == 1) return isDarkTheme() ? RGB(251,191,36) : RGB(180,120,10);
    return isDarkTheme() ? RGB(148,163,184) : RGB(120,128,138);
}
static const wchar_t* prioName(int p) { return p == 2 ? L"高" : p == 1 ? L"中" : L"低"; }

static wstring todayStr() {
    time_t now = time(nullptr); tm* lt = localtime(&now);
    wchar_t b[16];
    swprintf(b, 16, L"%04d-%02d-%02d", lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
    return b;
}

// ============================================================
//  数据视图
// ============================================================
static void computeVisible() {
    g_visible.clear();
    wstring today = todayStr();
    for (int i = 0; i < (int)g_subjects.size(); ++i) {
        for (int j = 0; j < (int)g_subjects[i].tasks.size(); ++j) {
            const Task& t = g_subjects[i].tasks[j];
            if (g_view == VM_UNDONE && t.done) continue;
            if (g_view == VM_DONE && !t.done) continue;
            if (g_view == VM_SUBJECT && g_viewSubject != i) continue;
            if (g_view == VM_TODAY) {
                if (t.done || t.due.empty() || t.due > today) continue;
            }
            if (!g_searchWord.empty() &&
                t.text.find(g_searchWord) == wstring::npos &&
                t.tag.find(g_searchWord) == wstring::npos &&
                g_subjects[i].name.find(g_searchWord) == wstring::npos) continue;
            g_visible.push_back({i, j});
        }
    }
    if (g_sort == S_PRIORITY) {
        stable_sort(g_visible.begin(), g_visible.end(), [](const pair<int,int>& a, const pair<int,int>& b) {
            const Task& x = g_subjects[a.first].tasks[a.second];
            const Task& y = g_subjects[b.first].tasks[b.second];
            if (x.priority != y.priority) return x.priority > y.priority;
            return a.second < b.second;
        });
    } else if (g_sort == S_DUE) {
        stable_sort(g_visible.begin(), g_visible.end(), [](const pair<int,int>& a, const pair<int,int>& b) {
            const Task& x = g_subjects[a.first].tasks[a.second];
            const Task& y = g_subjects[b.first].tasks[b.second];
            wstring dx = x.due.empty() ? L"9999" : x.due;
            wstring dy = y.due.empty() ? L"9999" : y.due;
            if (dx != dy) return dx < dy;
            return a.second < b.second;
        });
    } else if (g_sort == S_SUBJECT) {
        stable_sort(g_visible.begin(), g_visible.end(), [](const pair<int,int>& a, const pair<int,int>& b) {
            if (a.first != b.first) return a.first < b.first;
            return a.second < b.second;
        });
    }
}

static void rebuildList() {
    int curSi = -1, curTi = -1;
    int sel = ListView_GetNextItem(g_hTaskList, -1, LVNI_SELECTED);
    if (sel >= 0 && sel < (int)g_visible.size()) {
        curSi = g_visible[sel].first;
        curTi = g_visible[sel].second;
    }
    computeVisible();
    SendMessageW(g_hTaskList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_hTaskList);
    LVITEMW it{};
    it.mask = LVIF_TEXT;
    for (int r = 0; r < (int)g_visible.size(); ++r) {
        int si = g_visible[r].first;
        const Task& t = g_subjects[si].tasks[g_visible[r].second];
        it.iItem = r; it.iSubItem = 0;
        it.pszText = (LPWSTR)(t.done ? L"✔ 完成" : L"☐ 待办");
        ListView_InsertItem(g_hTaskList, &it);
        ListView_SetItemText(g_hTaskList, r, 1, (LPWSTR)g_subjects[si].name.c_str());
        ListView_SetItemText(g_hTaskList, r, 2, (LPWSTR)t.text.c_str());
        ListView_SetItemText(g_hTaskList, r, 3, (LPWSTR)prioName(t.priority));
        wstring due = dueDisplayText(t);
        ListView_SetItemText(g_hTaskList, r, 4, (LPWSTR)due.c_str());
        ListView_SetItemText(g_hTaskList, r, 5, (LPWSTR)t.tag.c_str());
    }
    int target = -1;
    if (curSi >= 0) {
        for (int r = 0; r < (int)g_visible.size(); ++r)
            if (g_visible[r].first == curSi && g_visible[r].second == curTi) { target = r; break; }
    }
    if (target < 0 && !g_visible.empty()) target = 0;
    if (target >= 0)
        ListView_SetItemState(g_hTaskList, target, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    SendMessageW(g_hTaskList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hTaskList, nullptr, FALSE);
}

static int todayCount() {
    int n = 0;
    wstring today = todayStr();
    for (auto& s : g_subjects)
        for (auto& t : s.tasks)
            if (!t.done && !t.due.empty() && t.due <= today) ++n;
    return n;
}

static void updateStatus() {
    int tot = taskTotal(), don = taskDone();
    int pct = tot ? don * 100 / tot : 0;
    wstring s = L"作业单：" + taskListDisplayName() +
                L"    总任务 " + to_wstring(tot) +
                L"    已完成 " + to_wstring(don) +
                L"    完成率 " + to_wstring(pct) + L"%";
    SetWindowTextW(g_hStatus, s.c_str());
    SendMessageW(g_hProgress, PBM_SETPOS, (WPARAM)pct, 0);
    SetWindowTextW(g_hStatText,
        (L"进度 " + to_wstring(pct) + L"%  ·  待办 " + to_wstring(tot - don) + L" 项").c_str());
    if (g_hListName) {
        wstring nm = L"当前：" + taskListDisplayName();
        SetWindowTextW(g_hListName, nm.c_str());
    }
}

static void rebuildTree() {
    g_rebuildingTree = true;
    TreeView_DeleteAllItems(g_hTree);
    auto addChild = [&](HTREEITEM parent, const wstring& txt, LPARAM lp) -> HTREEITEM {
        TVINSERTSTRUCTW c{};
        c.hParent = parent;
        c.hInsertAfter = TVI_LAST;
        c.item.mask = TVIF_TEXT | TVIF_PARAM;
        c.item.pszText = (LPWSTR)txt.c_str();
        c.item.lParam = lp;
        return TreeView_InsertItem(g_hTree, &c);
    };
    HTREEITEM root = addChild(nullptr, L"作业单：" + taskListDisplayName(), -1);
    wstring all = L"全部任务  (" + to_wstring(taskTotal()) + L")";
    wstring today = L"今天要做  (" + to_wstring(todayCount()) + L")";
    wstring undone = L"未完成  (" + to_wstring(taskTotal() - taskDone()) + L")";
    wstring done = L"已完成  (" + to_wstring(taskDone()) + L")";
    HTREEITEM iAll = addChild(root, all, VM_ALL);
    HTREEITEM iToday = addChild(root, today, VM_TODAY);
    HTREEITEM iUndone = addChild(root, undone, VM_UNDONE);
    HTREEITEM iDone = addChild(root, done, VM_DONE);
    HTREEITEM target = iAll;
    for (int i = 0; i < (int)g_subjects.size(); ++i) {
        int d = 0;
        for (auto& t : g_subjects[i].tasks) if (t.done) ++d;
        wstring label = g_subjects[i].name + L"  (" + to_wstring(d) + L"/" +
                        to_wstring((int)g_subjects[i].tasks.size()) + L")";
        HTREEITEM it = addChild(root, label, (LPARAM)(VM_SUBJECT * 10000 + i));
        if (g_view == VM_SUBJECT && g_viewSubject == i) target = it;
    }
    if (g_view == VM_TODAY) target = iToday;
    if (g_view == VM_UNDONE) target = iUndone;
    if (g_view == VM_DONE) target = iDone;
    TreeView_Expand(g_hTree, root, TVE_EXPAND);
    TreeView_SelectItem(g_hTree, target);
    g_rebuildingTree = false;
}

static void afterChange(bool treeToo = true) {
    if (treeToo) rebuildTree();
    rebuildList();
    updateStatus();
    autoSaveTasks();
    updateSidebarSummary();
}

static bool getSelected(int& si, int& ti) {
    int sel = ListView_GetNextItem(g_hTaskList, -1, LVNI_SELECTED);
    if (sel < 0 || sel >= (int)g_visible.size()) return false;
    si = g_visible[sel].first;
    ti = g_visible[sel].second;
    return true;
}
static int pickSubjectIdx() { int si, ti; return getSelected(si, ti) ? si : 0; }

// ============================================================
//  弹窗基础设施
// ============================================================
static void runModal(HWND dlg) {
    EnableWindow(g_hWnd, FALSE);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN && (msg.wParam == VK_RETURN || msg.wParam == VK_ESCAPE)) {
            HWND focus = GetFocus();
            wchar_t cls[32] = {0};
            if (focus) GetClassNameW(focus, cls, 32);
            bool inCombo = (wcscmp(cls, L"ComboBox") == 0);
            if (msg.wParam == VK_ESCAPE || !inCombo) {
                SendMessageW(dlg, WM_COMMAND, msg.wParam == VK_RETURN ? IDOK : IDCANCEL, 0);
                continue;
            }
        }
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!IsWindow(dlg)) break;
    }
    EnableWindow(g_hWnd, TRUE);
    SetForegroundWindow(g_hWnd);
}

static HWND addBtn(HWND parent, int id, const wstring& text, int x, int y, int w, int h) {
    HWND b = CreateWindowW(L"BUTTON", text.c_str(), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                           x, y, w, h, parent, (HMENU)(INT_PTR)id, g_hInst, nullptr);
    regBtn(b, (id == IDOK || id == BTN_SAVE) ? BK_PRIMARY : BK_SECONDARY);
    SendMessageW(b, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    return b;
}
static HWND addEdit(HWND parent, int id, const wstring& text, int x, int y, int w, int h) {
    HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text.c_str(),
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                             x, y, w, h, parent, (HMENU)(INT_PTR)id, g_hInst, nullptr);
    SendMessageW(e, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    return e;
}
static HWND addLabel(HWND parent, const wstring& text, int x, int y, int w, int h) {
    HWND lb = CreateWindowW(L"STATIC", text.c_str(), WS_CHILD | WS_VISIBLE,
                            x, y, w, h, parent, nullptr, g_hInst, nullptr);
    SendMessageW(lb, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    return lb;
}
static HWND addCombo(HWND parent, int id, int x, int y, int w, int h) {
    HWND c = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                           CBS_DROPDOWNLIST | WS_VSCROLL, x, y, w, h, parent,
                           (HMENU)(INT_PTR)id, g_hInst, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    return c;
}

static LRESULT dlgCommon(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ERASEBKGND: {
        HDC dc = (HDC)w; RECT rc; GetClientRect(h, &rc);
        HBRUSH br = CreateSolidBrush(g_th.bg); FillRect(dc, &rc, br); DeleteObject(br);
        return 1;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)w; SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, g_th.textPrimary); return (LRESULT)g_hBrBg;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)w; SetBkColor(dc, g_th.card); SetTextColor(dc, g_th.textPrimary);
        return (LRESULT)g_hBrCard;
    }
    case WM_CTLCOLORLISTBOX: {
        HDC dc = (HDC)w; SetBkColor(dc, g_th.card); SetTextColor(dc, g_th.textPrimary);
        return (LRESULT)g_hBrCard;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)l;
        if (di && g_btns.count(di->hwndItem)) { drawButton(di); return TRUE; }
        break;
    }
    }
    return 0;
}

// ============================================================
//  对话框：新增/编辑任务
// ============================================================
static LRESULT CALLBACK TaskDlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        Task* t = (Task*)((CREATESTRUCTW*)l)->lpCreateParams;
        addLabel(h, L"任务内容", 16, 16, 120, 20);
        HWND et = addEdit(h, ID_TASK_TEXT, t->text, 16, 40, 348, 28);
        addLabel(h, L"优先级", 16, 78, 90, 20);
        HWND cb = addCombo(h, ID_TASK_PRIO, 16, 102, 150, 200);
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)L"低");
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)L"中");
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)L"高");
        SendMessageW(cb, CB_SETCURSEL, t->priority, 0);
        addLabel(h, L"截止日期 (yyyy-MM-dd)", 190, 78, 190, 20);
        addEdit(h, ID_TASK_DUE, t->due, 190, 102, 174, 28);
        addLabel(h, L"标签", 16, 140, 120, 20);
        addEdit(h, ID_TASK_TAG, t->tag, 16, 164, 348, 28);
        addBtn(h, IDOK, L"确定", 168, 210, 92, 32);
        addBtn(h, IDCANCEL, L"取消", 272, 210, 92, 32);
        SetFocus(et);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == IDOK) {
            wchar_t buf[1024];
            GetDlgItemTextW(h, ID_TASK_TEXT, buf, 1024);
            wstring text = trimW2(buf);
            if (text.empty()) { MessageBoxW(h, L"任务内容不能为空。", L"提示", MB_OK); return 0; }
            GetDlgItemTextW(h, ID_TASK_DUE, buf, 1024);
            wstring due = trimW2(buf);
            if (!due.empty()) {
                bool ok = (due.size() == 10 && due[4] == L'-' && due[7] == L'-');
                for (int i = 0; ok && i < 10; i++)
                    if (i != 4 && i != 7 && (due[i] < L'0' || due[i] > L'9')) ok = false;
                if (!ok) { MessageBoxW(h, L"日期格式应为 yyyy-MM-dd。", L"提示", MB_OK); return 0; }
            }
            int pri = (int)SendMessageW(GetDlgItem(h, ID_TASK_PRIO), CB_GETCURSEL, 0, 0);
            if (pri < 0) pri = 0;
            GetDlgItemTextW(h, ID_TASK_TAG, buf, 1024);
            wstring tag = trimW2(buf);
            if (g_dlgIdx >= 0) {
                Task& t = g_subjects[g_dlgSubj].tasks[g_dlgIdx];
                t.text = text; t.priority = pri; t.due = due; t.tag = tag;
            } else {
                Task t; t.text = text; t.priority = pri; t.due = due; t.tag = tag;
                int si = (g_dlgSubj >= 0 && g_dlgSubj < (int)g_subjects.size()) ? g_dlgSubj : 0;
                if (g_subjects.empty()) { g_subjects.push_back(Subject{L"学习", {}}); si = 0; }
                if (si >= (int)g_subjects.size()) si = 0;
                if (g_insertMode) {
                    int pos = max(0, min(g_insertPos, (int)g_subjects[si].tasks.size()));
                    g_subjects[si].tasks.insert(g_subjects[si].tasks.begin() + pos, t);
                } else {
                    g_subjects[si].tasks.push_back(t);
                }
            }
            DestroyWindow(h);
            return 0;
        }
        if (id == IDCANCEL) { DestroyWindow(h); return 0; }
        break;
    }
    case WM_CLOSE: DestroyWindow(h); return 0;
    }
    LRESULT r = dlgCommon(h, m, w, l);
    if (r) return r;
    return DefWindowProcW(h, m, w, l);
}

static HWND makeDialog(const wchar_t* cls, const wchar_t* title, int w, int h, void* param) {
    HWND d = CreateWindowW(cls, title, WS_CAPTION | WS_SYSMENU | WS_POPUP,
                           0, 0, S(w), S(h), g_hWnd, nullptr, g_hInst, param);
    if (!d) return nullptr;
    RECT r{}; GetWindowRect(g_hWnd, &r);
    int dw = S(w), dh = S(h);
    SetWindowPos(d, nullptr, r.left + (r.right - r.left - dw) / 2,
                 r.top + (r.bottom - r.top - dh) / 2, dw, dh, SWP_NOZORDER);
    DWORD pref = 2; DwmSetWindowAttribute(d, 33, &pref, sizeof(pref));
    COLORREF c = g_th.bg; DwmSetWindowAttribute(d, 35, &c, sizeof(c));
    ShowWindow(d, SW_SHOW);
    UpdateWindow(d);
    return d;
}

static void doAddTask() {
    if (g_subjects.empty()) {
        g_subjects.push_back(Subject{L"学习", {}});
        rebuildTree();
    }
    Task tmp;
    g_dlgSubj = pickSubjectIdx(); g_dlgIdx = -1; g_insertMode = false;
    HWND d = makeDialog(L"SJTaskDlg", L"新增任务", 380, 264, &tmp);
    if (d) runModal(d);
    afterChange();
}
static void doEditTask() {
    int si, ti;
    if (!getSelected(si, ti)) { MessageBoxW(g_hWnd, L"请先在列表中选择一个任务。", L"提示", MB_OK); return; }
    Task t = g_subjects[si].tasks[ti];
    g_dlgSubj = si; g_dlgIdx = ti; g_insertMode = false;
    HWND d = makeDialog(L"SJTaskDlg", L"编辑任务", 380, 264, &t);
    if (d) runModal(d);
    afterChange();
}
static void doInsert(bool after) {
    int si, ti;
    if (!getSelected(si, ti)) { MessageBoxW(g_hWnd, L"请先选择一个任务作为插入位置。", L"提示", MB_OK); return; }
    Task t;
    g_dlgSubj = si; g_dlgIdx = -1;
    g_insertMode = true; g_insertPos = after ? ti + 1 : ti;
    HWND d = makeDialog(L"SJTaskDlg", L"插入任务", 380, 264, &t);
    if (d) runModal(d);
    g_insertMode = false;
    afterChange();
}

// ============================================================
//  对话框：新增/编辑科目
// ============================================================
static int g_dlgSubjIdx = -1;
static LRESULT CALLBACK SubjectDlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        wstring* name = (wstring*)((CREATESTRUCTW*)l)->lpCreateParams;
        addLabel(h, L"科目名称", 16, 20, 120, 20);
        HWND e = addEdit(h, ID_SUBJ_NAME, *name, 16, 46, 268, 28);
        addBtn(h, IDOK, L"确定", 82, 98, 90, 32);
        addBtn(h, IDCANCEL, L"取消", 184, 98, 90, 32);
        SetFocus(e);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == IDOK) {
            wchar_t buf[256];
            GetDlgItemTextW(h, ID_SUBJ_NAME, buf, 256);
            wstring name = trimW2(buf);
            if (name.empty()) { MessageBoxW(h, L"科目名称不能为空。", L"提示", MB_OK); return 0; }
            for (int i = 0; i < (int)g_subjects.size(); ++i)
                if (i != g_dlgSubjIdx && g_subjects[i].name == name) {
                    MessageBoxW(h, L"该科目名称已存在。", L"提示", MB_OK); return 0;
                }
            if (g_dlgSubjIdx >= 0) g_subjects[g_dlgSubjIdx].name = name;
            else g_subjects.push_back(Subject{name, {}});
            DestroyWindow(h);
            return 0;
        }
        if (id == IDCANCEL) { DestroyWindow(h); return 0; }
        break;
    }
    case WM_CLOSE: DestroyWindow(h); return 0;
    }
    LRESULT r = dlgCommon(h, m, w, l);
    if (r) return r;
    return DefWindowProcW(h, m, w, l);
}
static void doAddSubject() {
    wstring name;
    g_dlgSubjIdx = -1;
    HWND d = makeDialog(L"SJSubjectDlg", L"新增科目", 320, 172, &name);
    if (d) runModal(d);
    rebuildTree(); rebuildList(); updateStatus(); autoSaveTasks(); updateSidebarSummary();
}
static void doRenameSubject() {
    int si, ti;
    if (!getSelected(si, ti)) {
        MessageBoxW(g_hWnd, L"请先选中该科目下的任意任务再重命名。", L"提示", MB_OK);
        return;
    }
    wstring name = g_subjects[si].name;
    g_dlgSubjIdx = si;
    HWND d = makeDialog(L"SJSubjectDlg", L"重命名科目", 320, 172, &name);
    if (d) runModal(d);
    rebuildTree(); rebuildList(); updateStatus(); autoSaveTasks();
}
static void doDeleteSubject() {
    int si, ti;
    if (!getSelected(si, ti)) { MessageBoxW(g_hWnd, L"请先选中该科目的任意任务。", L"提示", MB_OK); return; }
    wstring q = L"确定删除科目【" + g_subjects[si].name + L"】及其全部任务？";
    if (MessageBoxW(g_hWnd, q.c_str(), L"删除科目", MB_YESNO | MB_ICONWARNING) != IDYES) return;
    g_subjects.erase(g_subjects.begin() + si);
    g_view = VM_ALL; g_viewSubject = -1;
    afterChange();
}

// ============================================================
//  对话框：打开 / 删除作业单
// ============================================================
static LRESULT CALLBACK OpenDlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        addLabel(h, L"已保存的作业单", 16, 14, 300, 20);
        HWND lb = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER | LBS_NOTIFY,
            16, 40, 356, 220, h, (HMENU)(INT_PTR)ID_OPEN_LIST, g_hInst, nullptr);
        SendMessageW(lb, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        addBtn(h, ID_OPEN_OPEN, L"打开", 24, 274, 88, 32);
        addBtn(h, ID_OPEN_DEL, L"删除", 122, 274, 88, 32);
        addBtn(h, ID_OPEN_REFRESH, L"刷新", 220, 274, 68, 32);
        addBtn(h, IDCANCEL, L"取消", 298, 274, 74, 32);
        for (auto& f : listTaskFiles()) SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)f.c_str());
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        HWND lb = GetDlgItem(h, ID_OPEN_LIST);
        if (id == ID_OPEN_REFRESH) {
            SendMessageW(lb, LB_RESETCONTENT, 0, 0);
            for (auto& f : listTaskFiles()) SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)f.c_str());
            return 0;
        }
        if (id == ID_OPEN_OPEN || (id == ID_OPEN_LIST && HIWORD(w) == LBN_DBLCLK)) {
            int sel = (int)SendMessageW(lb, LB_GETCURSEL, 0, 0);
            if (sel == LB_ERR) { MessageBoxW(h, L"请先选择一个作业单。", L"提示", MB_OK); return 0; }
            wchar_t buf[256];
            SendMessageW(lb, LB_GETTEXT, sel, (LPARAM)buf);
            if (loadTaskFile(buf)) {
                g_lastTaskName = g_taskListName;
                saveConfig(g_journalRoot);
                DestroyWindow(h);
            } else {
                MessageBoxW(h, L"读取失败或文件为空。", L"错误", MB_OK);
            }
            return 0;
        }
        if (id == ID_OPEN_DEL) {
            int sel = (int)SendMessageW(lb, LB_GETCURSEL, 0, 0);
            if (sel == LB_ERR) { MessageBoxW(h, L"请先选择要删除的作业单。", L"提示", MB_OK); return 0; }
            wchar_t buf[256];
            SendMessageW(lb, LB_GETTEXT, sel, (LPARAM)buf);
            wstring q = L"确定删除作业单 " + (wstring)buf + L" ？";
            if (MessageBoxW(h, q.c_str(), L"删除", MB_YESNO | MB_ICONWARNING) == IDYES) {
                DeleteFileW(joinPathW(taskListDir(), buf).c_str());
                SendMessageW(lb, LB_RESETCONTENT, 0, 0);
                for (auto& f : listTaskFiles()) SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)f.c_str());
                if (g_taskListName + L".txt" == wstring(buf)) {
                    g_subjects.clear();
                    g_taskListName.clear();
                    ensureTaskList();
                    loadTaskFile(g_taskListName);
                    g_lastTaskName = g_taskListName;
                }
            }
            return 0;
        }
        if (id == IDCANCEL) { DestroyWindow(h); return 0; }
        break;
    }
    case WM_CLOSE: DestroyWindow(h); return 0;
    }
    LRESULT r = dlgCommon(h, m, w, l);
    if (r) return r;
    return DefWindowProcW(h, m, w, l);
}
static void doOpen() {
    HWND d = makeDialog(L"SJOpenDlg", L"打开作业单", 400, 326, nullptr);
    if (d) runModal(d);
    g_view = VM_ALL; g_viewSubject = -1;
    rebuildTree(); rebuildList(); updateStatus(); updateSidebarSummary();
}

// ============================================================
//  对话框：另存为 / 新建
// ============================================================
static LRESULT CALLBACK SaveAsDlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        wstring* init = (wstring*)((CREATESTRUCTW*)l)->lpCreateParams;
        addLabel(h, L"作业单名称（保存到数据目录 tasks）", 16, 16, 300, 20);
        HWND e = addEdit(h, ID_SAVEAS_NAME, *init, 16, 42, 288, 28);
        addBtn(h, IDOK, L"保存", 82, 88, 90, 32);
        addBtn(h, IDCANCEL, L"取消", 184, 88, 90, 32);
        SetFocus(e);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == IDOK) {
            wchar_t buf[256];
            GetDlgItemTextW(h, ID_SAVEAS_NAME, buf, 256);
            wstring name = trimW2(buf);
            if (name.empty()) { MessageBoxW(h, L"名称不能为空。", L"提示", MB_OK); return 0; }
            if (g_newListMode) {
                g_subjects.clear();
                g_subjects.push_back(Subject{L"学习", {}});
            }
            if (saveTaskFile(name)) {
                g_lastTaskName = g_taskListName;
                saveConfig(g_journalRoot);
                DestroyWindow(h);
            } else {
                MessageBoxW(h, L"保存失败。", L"错误", MB_OK);
            }
            return 0;
        }
        if (id == IDCANCEL) { DestroyWindow(h); return 0; }
        break;
    }
    case WM_CLOSE: DestroyWindow(h); return 0;
    }
    LRESULT r = dlgCommon(h, m, w, l);
    if (r) return r;
    return DefWindowProcW(h, m, w, l);
}
static void doSaveAs(bool newList) {
    wstring init = newList ? L"" : g_taskListName;
    g_newListMode = newList;
    HWND d = makeDialog(L"SJSaveAsDlg", newList ? L"新建作业单" : L"另存为", 340, 164, &init);
    if (d) runModal(d);
    g_newListMode = false;
    g_view = VM_ALL; g_viewSubject = -1;
    afterChange();
}

// ============================================================
//  对话框：进度统计
// ============================================================
static LRESULT CALLBACK StatsDlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        int y = 18, W = 380;
        int tot = taskTotal(), don = taskDone();
        wchar_t buf[256];
        swprintf(buf, 256, L"总体完成率    %d / %d   (%d%%)", don, tot, tot ? don * 100 / tot : 0);
        addLabel(h, buf, 20, y, W, 22);
        y += 36;
        for (int i = 0; i < (int)g_subjects.size(); ++i) {
            int d = 0;
            for (auto& t : g_subjects[i].tasks) if (t.done) ++d;
            int c = (int)g_subjects[i].tasks.size();
            wstring txt = g_subjects[i].name + L"   " + to_wstring(d) + L"/" + to_wstring(c) +
                          L"   (" + to_wstring(c ? d * 100 / c : 0) + L"%)";
            addLabel(h, txt, 20, y, W, 20);
            y += 22;
            HWND pb = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE,
                                      20, y, W, 16, h, nullptr, g_hInst, nullptr);
            SendMessageW(pb, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            SendMessageW(pb, PBM_SETPOS, c ? d * 100 / c : 0, 0);
            y += 26;
        }
        addBtn(h, IDOK, L"关闭", 170, y + 8, 90, 32);
        SetWindowPos(h, nullptr, 0, 0, S(420), S(y + 60), SWP_NOMOVE | SWP_NOZORDER);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) == IDOK || LOWORD(w) == IDCANCEL) { DestroyWindow(h); return 0; }
        break;
    case WM_CLOSE: DestroyWindow(h); return 0;
    }
    LRESULT r = dlgCommon(h, m, w, l);
    if (r) return r;
    return DefWindowProcW(h, m, w, l);
}
void openTaskStatsWindow(HWND parent) {
    int height = 120 + (int)g_subjects.size() * 48;
    height = min(height, 620);
    HWND d = makeDialog(L"SJStatsDlg", L"进度统计", 420, height, nullptr);
    if (d) runModal(d);
}

// ============================================================
//  番茄钟 (非模态, 不阻塞主界面)
// ============================================================
static void pomoUpdate() {
    int mm = g_pomoRemain / 60, ss = g_pomoRemain % 60;
    wchar_t buf[64];
    swprintf(buf, 64, L"%02d : %02d", mm, ss);
    SetWindowTextW(g_hPomoLabel, buf);
    SendMessageW(g_hPomoBar, PBM_SETPOS, (WPARAM)(g_pomoRemain * 100 / (25 * 60)), 0);
}
static LRESULT CALLBACK PomoDlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE:
        g_hPomoWnd = h;
        g_hPomoLabel = CreateWindowExW(0, L"STATIC", L"25 : 00",
                WS_CHILD | WS_VISIBLE | SS_CENTER, S(40), S(26), S(240), S(60),
                h, nullptr, g_hInst, nullptr);
        g_hPomoBar = CreateWindowExW(0, PROGRESS_CLASSW, nullptr,
                WS_CHILD | WS_VISIBLE, S(40), S(96), S(240), S(20),
                h, (HMENU)(INT_PTR)ID_POMO_BAR, g_hInst, nullptr);
        SendMessageW(g_hPomoLabel, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);
        SendMessageW(g_hPomoBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        SendMessageW(g_hPomoBar, PBM_SETPOS, 100, 0);
        addBtn(h, ID_POMO_START, L"开始", S(40), S(132), S(70), S(32));
        addBtn(h, ID_POMO_PAUSE, L"暂停", S(118), S(132), S(70), S(32));
        addBtn(h, ID_POMO_RESET, L"重置", S(196), S(132), S(70), S(32));
        addBtn(h, ID_POMO_CLOSE, L"关闭", S(188), S(176), S(78), S(30));
        SetTimer(h, 1, 1000, nullptr);
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)w; SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, g_th.textPrimary);
        return (LRESULT)g_hBrBg;
    }
    case WM_ERASEBKGND: {
        HDC dc = (HDC)w; RECT rc; GetClientRect(h, &rc);
        HBRUSH br = CreateSolidBrush(g_th.bg); FillRect(dc, &rc, br); DeleteObject(br);
        return 1;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)l;
        if (g_btns.count(di->hwndItem)) { drawButton(di); return TRUE; }
        break;
    }
    case WM_TIMER:
        if (g_pomoRunning && !g_pomoPaused) {
            if (g_pomoRemain > 0) {
                --g_pomoRemain;
                pomoUpdate();
                if (g_pomoRemain == 0) {
                    g_pomoRunning = false;
                    MessageBeep(MB_ICONASTERISK);
                    MessageBoxW(h, L"专注时间结束！休息一下吧。", L"番茄钟", MB_OK | MB_ICONINFORMATION);
                }
            }
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case ID_POMO_START:
            if (!g_pomoRunning) { if (g_pomoRemain <= 0) g_pomoRemain = 25 * 60; g_pomoRunning = true; g_pomoPaused = false; }
            pomoUpdate(); return 0;
        case ID_POMO_PAUSE:
            g_pomoPaused = !g_pomoPaused; return 0;
        case ID_POMO_RESET:
            g_pomoRemain = 25 * 60; g_pomoRunning = false; g_pomoPaused = false; pomoUpdate(); return 0;
        case ID_POMO_CLOSE:
            DestroyWindow(h); return 0;
        }
        break;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY:
        KillTimer(h, 1);
        g_hPomoWnd = nullptr; g_hPomoLabel = nullptr; g_hPomoBar = nullptr;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
void showPomodoroWindow(HWND parent) {
    if (g_hPomoWnd && IsWindow(g_hPomoWnd)) { SetForegroundWindow(g_hPomoWnd); return; }
    HWND d = CreateWindowW(L"SJPomoDlg", L"番茄钟 · 25 分钟", WS_CAPTION | WS_SYSMENU | WS_POPUP,
                           0, 0, S(320), S(250), parent, nullptr, g_hInst, nullptr);
    if (!d) return;
    RECT r{}; GetWindowRect(parent, &r);
    SetWindowPos(d, nullptr, r.left + (r.right - r.left - S(320)) / 2,
                 r.top + (r.bottom - r.top - S(250)) / 2, S(320), S(250), SWP_NOZORDER);
    DWORD pref = 2; DwmSetWindowAttribute(d, 33, &pref, sizeof(pref));
    COLORREF c = g_th.bg; DwmSetWindowAttribute(d, 35, &c, sizeof(c));
    ShowWindow(d, SW_SHOW);
    UpdateWindow(d);
}

// ============================================================
//  就地编辑
// ============================================================
static void ipCommit();
static LRESULT CALLBACK IPEditProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) { g_ipCancel = true; ipCommit(); return 0; }
        break;
    case WM_CHAR:
        if (w == VK_RETURN) { ipCommit(); return 0; }
        break;
    case WM_KILLFOCUS:
        if (g_ipEdit == h) ipCommit();
        return 0;
    }
    return CallWindowProcW(g_ipEditOrig, h, m, w, l);
}
static void ipCommit() {
    HWND h = g_ipEdit;
    if (!h) return;
    g_ipEdit = nullptr;
    int row = g_ipRow, col = g_ipCol;
    bool cancel = g_ipCancel; g_ipCancel = false;
    wchar_t buf[1024]; GetWindowTextW(h, buf, 1024);
    wstring text = trimW2(buf);
    bool isCombo = (col == 1);
    int combo = -1;
    if (isCombo) combo = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
    DestroyWindow(h);
    if (cancel || row < 0 || row >= (int)g_visible.size()) return;
    int si = g_visible[row].first, ti = g_visible[row].second;
    if (col == 1) {
        if (combo >= 0 && combo != si && combo < (int)g_subjects.size()) {
            Task t = g_subjects[si].tasks[ti];
            g_subjects[si].tasks.erase(g_subjects[si].tasks.begin() + ti);
            g_subjects[combo].tasks.push_back(t);
        }
    } else if (col == 2) {
        if (!text.empty()) g_subjects[si].tasks[ti].text = text;
    }
    afterChange();
}
static void ipStartEdit(int row, int col) {
    if (g_ipEdit) ipCommit();
    if (row < 0 || row >= (int)g_visible.size()) return;
    if (col < 1 || col > 2) col = 2;
    int si = g_visible[row].first, ti = g_visible[row].second;
    Task& t = g_subjects[si].tasks[ti];
    RECT rc;
    rc.top = col; rc.left = LVIR_BOUNDS; rc.right = 0; rc.bottom = 0;
    SendMessageW(g_hTaskList, LVM_GETSUBITEMRECT, row, (LPARAM)&rc);
    InflateRect(&rc, -2, -2);
    if (rc.right <= rc.left || rc.bottom <= rc.top) { doEditTask(); return; }
    g_ipRow = row; g_ipCol = col; g_ipCancel = false;
    bool isCombo = (col == 1);
    DWORD style = WS_CHILD | WS_VISIBLE |
                  (isCombo ? (CBS_DROPDOWNLIST | WS_VSCROLL) : ES_AUTOHSCROLL);
    g_ipEdit = CreateWindowExW(WS_EX_CLIENTEDGE, isCombo ? L"COMBOBOX" : L"EDIT", nullptr,
        style, rc.left, rc.top, rc.right - rc.left, isCombo ? S(140) : rc.bottom - rc.top,
        g_hTaskList, nullptr, g_hInst, nullptr);
    if (!g_ipEdit) { doEditTask(); return; }
    SendMessageW(g_ipEdit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    if (isCombo) {
        for (int i = 0; i < (int)g_subjects.size(); ++i)
            SendMessageW(g_ipEdit, CB_ADDSTRING, 0, (LPARAM)g_subjects[i].name.c_str());
        SendMessageW(g_ipEdit, CB_SETCURSEL, si, 0);
    } else {
        SetWindowTextW(g_ipEdit, t.text.c_str());
        SendMessageW(g_ipEdit, EM_SETSEL, 0, -1);
    }
    g_ipEditOrig = (WNDPROC)SetWindowLongPtrW(g_ipEdit, GWLP_WNDPROC, (LONG_PTR)IPEditProc);
    SetFocus(g_ipEdit);
}

// ============================================================
//  列表交互 / 操作
// ============================================================
static void doDeleteTask() {
    int si, ti;
    if (!getSelected(si, ti)) { MessageBoxW(g_hWnd, L"请先选择要删除的任务。", L"提示", MB_OK); return; }
    wstring q = L"确定删除任务：【" + g_subjects[si].tasks[ti].text + L"】？";
    if (MessageBoxW(g_hWnd, q.c_str(), L"删除", MB_YESNO | MB_ICONWARNING) != IDYES) return;
    g_subjects[si].tasks.erase(g_subjects[si].tasks.begin() + ti);
    if (g_subjects[si].tasks.empty()) {
        if (MessageBoxW(g_hWnd, (L"科目【" + g_subjects[si].name + L"】已无任务，是否同时删除该科目？").c_str(),
                        L"提示", MB_YESNO) == IDYES) {
            g_subjects.erase(g_subjects.begin() + si);
            g_view = VM_ALL; g_viewSubject = -1;
        }
    }
    afterChange();
}
static void doToggle() {
    int si, ti;
    if (!getSelected(si, ti)) { MessageBoxW(g_hWnd, L"请先选择一个任务。", L"提示", MB_OK); return; }
    g_subjects[si].tasks[ti].done = !g_subjects[si].tasks[ti].done;
    afterChange();
}
static void applyFilter(int lp) {
    if (lp == VM_ALL || lp == VM_TODAY || lp == VM_UNDONE || lp == VM_DONE) {
        g_view = (ViewMode)lp; g_viewSubject = -1;
    } else if (lp >= VM_SUBJECT * 10000) {
        g_view = VM_SUBJECT; g_viewSubject = lp % 10000;
    }
    rebuildList(); updateStatus();
}

static void doNewList() {
    if (g_subjects.empty() && g_taskListName.empty()) { doSaveAs(true); return; }
    if (MessageBoxW(g_hWnd, L"新建一个空作业单并切换过去？（当前作业单已自动保存）",
                    L"新建作业单", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    doSaveAs(true);
}

static void layoutTasksPage() {
    if (!g_hTasksPage) return;
    RECT rc; GetClientRect(g_hTasksPage, &rc);
    int W = rc.right, H = rc.bottom;
    int pad = S(18);
    int rowH = S(32);
    int gap = S(8);

    struct B { HWND h; int w; };
    B row1[] = {
        { GetDlgItem(g_hTasksPage, BTN_NEW), S(58) },
        { GetDlgItem(g_hTasksPage, BTN_OPEN), S(58) },
        { GetDlgItem(g_hTasksPage, BTN_SAVE), S(58) },
        { GetDlgItem(g_hTasksPage, BTN_SAVEAS), S(72) },
        { GetDlgItem(g_hTasksPage, BTN_ADDSUBJ), S(68) },
        { GetDlgItem(g_hTasksPage, BTN_ADDTASK), S(68) },
        { GetDlgItem(g_hTasksPage, BTN_EDIT), S(56) },
        { GetDlgItem(g_hTasksPage, BTN_DEL), S(56) },
        { GetDlgItem(g_hTasksPage, BTN_TOGGLE), S(72) },
        { GetDlgItem(g_hTasksPage, BTN_POMO), S(70) },
        { GetDlgItem(g_hTasksPage, BTN_STATS), S(58) },
    };
    int x = pad;
    for (int i = 0; i < (int)(sizeof(row1)/sizeof(row1[0])); i++) {
        SetWindowPos(row1[i].h, nullptr, x, S(14), row1[i].w, rowH, SWP_NOZORDER);
        x += row1[i].w + gap;
        if (i == 3) x += S(6);
    }

    int y2 = S(54);
    SetWindowPos(g_hSearch, nullptr, pad, y2, S(300), S(30), SWP_NOZORDER);
    SetWindowPos(g_hSort, nullptr, pad + S(310), y2, S(150), S(200), SWP_NOZORDER);
    SetWindowPos(g_hListName, nullptr, W - pad - S(320), y2, S(320), S(30), SWP_NOZORDER);

    int top = S(96);
    int bottom = S(52);
    int treeW = S(208);
    int contentH = H - top - bottom;
    if (contentH < S(120)) contentH = S(120);
    SetWindowPos(g_hTree, nullptr, pad, top, treeW, contentH, SWP_NOZORDER);
    int listX = pad + treeW + S(10);
    SetWindowPos(g_hTaskList, nullptr, listX, top, W - listX - pad, contentH, SWP_NOZORDER);

    int listW = W - listX - pad;
    int fixed = S(62) + S(80) + S(66) + S(124) + S(84);
    int rest = max(S(120), listW - fixed - S(24));
    int taskW = rest * 58 / 100;
    int tagW = rest - taskW;
    ListView_SetColumnWidth(g_hTaskList, 0, S(62));
    ListView_SetColumnWidth(g_hTaskList, 1, S(80));
    ListView_SetColumnWidth(g_hTaskList, 2, taskW);
    ListView_SetColumnWidth(g_hTaskList, 3, S(66));
    ListView_SetColumnWidth(g_hTaskList, 4, S(124));
    ListView_SetColumnWidth(g_hTaskList, 5, tagW);

    SetWindowPos(g_hProgress, nullptr, pad, H - S(36), S(220), S(16), SWP_NOZORDER);
    SetWindowPos(g_hStatText, nullptr, pad + S(232), H - S(38), S(260), S(22), SWP_NOZORDER);
    SetWindowPos(g_hStatus, nullptr, pad + S(500), H - S(38), max(S(120), W - pad - S(500) - pad), S(22), SWP_NOZORDER);

    HWND ctrls[] = { g_hTree, g_hTaskList, g_hSearch };
    for (HWND c : ctrls) SendMessageW(c, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    SendMessageW(g_hListName, WM_SETFONT, (WPARAM)g_hFontSmall, TRUE);
    SendMessageW(g_hStatus, WM_SETFONT, (WPARAM)g_hFontSmall, TRUE);
    SendMessageW(g_hStatText, WM_SETFONT, (WPARAM)g_hFontSmall, TRUE);
    InvalidateRect(g_hTasksPage, nullptr, FALSE);
}

static void applyThemeToControls() {
    SendMessageW(g_hTaskList, LVM_SETBKCOLOR, 0, (LPARAM)g_th.card);
    SendMessageW(g_hTaskList, LVM_SETTEXTCOLOR, 0, (LPARAM)g_th.textPrimary);
    SendMessageW(g_hTaskList, LVM_SETTEXTBKCOLOR, 0, (LPARAM)g_th.card);
    SendMessageW(g_hTree, TVM_SETBKCOLOR, 0, (LPARAM)g_th.card);
    SendMessageW(g_hTree, TVM_SETTEXTCOLOR, 0, (LPARAM)g_th.textPrimary);
    SendMessageW(g_hProgress, PBM_SETBARCOLOR, 0, (LPARAM)g_th.accent);
    SendMessageW(g_hProgress, PBM_SETBKCOLOR, 0, (LPARAM)g_th.hover);
    InvalidateRect(g_hTasksPage, nullptr, TRUE);
    InvalidateRect(g_hTree, nullptr, TRUE);
    InvalidateRect(g_hTaskList, nullptr, TRUE);
}

void refreshTasksPage() {
    if (!g_hTasksPage) return;
    rebuildTree();
    rebuildList();
    updateStatus();
    InvalidateRect(g_hTasksPage, nullptr, TRUE);
}

// ============================================================
//  页面窗口过程
// ============================================================
static LRESULT CALLBACK TasksPageProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hTasksPage = hWnd;
        g_hSearch = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 0, 0, hWnd, (HMENU)(INT_PTR)ID_SEARCH, g_hInst, nullptr);
        SendMessageW(g_hSearch, EM_SETCUEBANNER, TRUE, (LPARAM)L"搜索任务、标签或科目…");

        g_hSort = CreateWindowW(L"COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            0, 0, 0, 0, hWnd, (HMENU)(INT_PTR)ID_SORT, g_hInst, nullptr);
        SendMessageW(g_hSort, CB_ADDSTRING, 0, (LPARAM)L"原始顺序");
        SendMessageW(g_hSort, CB_ADDSTRING, 0, (LPARAM)L"按优先级");
        SendMessageW(g_hSort, CB_ADDSTRING, 0, (LPARAM)L"按截止日期");
        SendMessageW(g_hSort, CB_ADDSTRING, 0, (LPARAM)L"按科目分组");
        SendMessageW(g_hSort, CB_SETCURSEL, 0, 0);

        g_hListName = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
            0, 0, 0, 0, hWnd, (HMENU)(INT_PTR)(ID_SORT + 1), g_hInst, nullptr);

        g_hTree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | TVS_HASLINES | TVS_HASBUTTONS |
            TVS_LINESATROOT | TVS_SHOWSELALWAYS,
            0, 0, 0, 0, hWnd, (HMENU)(INT_PTR)ID_TREE, g_hInst, nullptr);

        g_hTaskList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
            0, 0, 0, 0, hWnd, (HMENU)(INT_PTR)ID_LIST, g_hInst, nullptr);
        ListView_SetExtendedListViewStyle(g_hTaskList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES |
                                                    LVS_EX_DOUBLEBUFFER);

        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        const wchar_t* heads[] = { L"状态", L"科目", L"任务", L"优先级", L"截止", L"标签" };
        int widths[] = { 62, 80, 300, 66, 92, 100 };
        for (int i = 0; i < 6; ++i) {
            col.iSubItem = i; col.pszText = (LPWSTR)heads[i]; col.cx = widths[i];
            ListView_InsertColumn(g_hTaskList, i, &col);
        }

        g_hProgress = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hWnd, (HMENU)(INT_PTR)ID_PROGRESS, g_hInst, nullptr);
        SendMessageW(g_hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

        g_hStatText = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            0, 0, 0, 0, hWnd, (HMENU)(INT_PTR)ID_STAT, g_hInst, nullptr);
        g_hStatus = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
            0, 0, 0, 0, hWnd, (HMENU)(INT_PTR)(ID_STAT + 1), g_hInst, nullptr);

        const wchar_t* btnText[] = { L"新建", L"打开", L"保存", L"另存为",
                                     L"＋科目", L"＋任务", L"编辑", L"删除",
                                     L"切换完成", L"番茄钟", L"统计" };
        int btnId[] = { BTN_NEW, BTN_OPEN, BTN_SAVE, BTN_SAVEAS, BTN_ADDSUBJ,
                        BTN_ADDTASK, BTN_EDIT, BTN_DEL, BTN_TOGGLE, BTN_POMO, BTN_STATS };
        for (int i = 0; i < 11; i++) {
            HWND b = CreateWindowW(L"BUTTON", btnText[i], WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                0, 0, 0, 0, hWnd, (HMENU)(INT_PTR)btnId[i], g_hInst, nullptr);
            regBtn(b, i == 2 ? BK_PRIMARY : (i == 7 ? BK_DANGER : BK_SECONDARY));
            SendMessageW(b, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        }

        layoutTasksPage();
        applyThemeToControls();
        rebuildTree();
        rebuildList();
        updateStatus();
        return 0;
    }
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) layoutTasksPage();
        return 0;
    case WM_ERASEBKGND: {
        HDC dc = (HDC)wParam; RECT rc; GetClientRect(hWnd, &rc);
        HBRUSH br = CreateSolidBrush(g_th.bg); FillRect(dc, &rc, br); DeleteObject(br);
        return 1;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wParam; SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, g_th.textSecondary);
        return (LRESULT)g_hBrBg;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wParam; SetBkColor(dc, g_th.card); SetTextColor(dc, g_th.textPrimary);
        return (LRESULT)g_hBrCard;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lParam;
        if (g_btns.count(di->hwndItem)) { drawButton(di); return TRUE; }
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam), code = HIWORD(wParam);
        switch (id) {
        case BTN_NEW: doNewList(); break;
        case BTN_OPEN: doOpen(); break;
        case BTN_SAVE:
            if (g_taskListName.empty()) doSaveAs(false);
            else { saveTaskFile(g_taskListName); afterChange(); }
            break;
        case BTN_SAVEAS: doSaveAs(false); break;
        case BTN_ADDSUBJ: doAddSubject(); break;
        case BTN_ADDTASK: doAddTask(); break;
        case BTN_EDIT: doEditTask(); break;
        case BTN_DEL: doDeleteTask(); break;
        case BTN_TOGGLE: doToggle(); break;
        case BTN_POMO: showPomodoroWindow(g_hWnd); break;
        case BTN_STATS: openTaskStatsWindow(g_hWnd); break;
        case ID_SEARCH:
            if (code == EN_CHANGE) {
                wchar_t buf[256];
                GetWindowTextW(g_hSearch, buf, 256);
                g_searchWord = trimW2(buf);
                rebuildList(); updateStatus();
            }
            break;
        case ID_SORT:
            if (code == CBN_SELCHANGE) {
                int sel = (int)SendMessageW(g_hSort, CB_GETCURSEL, 0, 0);
                g_sort = (SortMode)max(0, min(sel, 3));
                rebuildList();
            }
            break;
        }
        return 0;
    }
    case WM_NOTIFY: {
        NMHDR* hdr = (NMHDR*)lParam;
        if (hdr->idFrom == ID_TREE && hdr->code == TVN_SELCHANGED) {
            NMTREEVIEWW* ntv = (NMTREEVIEWW*)lParam;
            if (!g_rebuildingTree && ntv->itemNew.lParam != -1)
                applyFilter((int)ntv->itemNew.lParam);
            return 0;
        }
        if (hdr->idFrom == ID_LIST) {
            if (hdr->code == LVN_COLUMNCLICK) {
                NMLISTVIEW* nlv = (NMLISTVIEW*)lParam;
                switch (nlv->iSubItem) {
                case 0: g_sort = S_ORIG; SendMessageW(g_hSort, CB_SETCURSEL, 0, 0); break;
                case 1: g_sort = S_SUBJECT; SendMessageW(g_hSort, CB_SETCURSEL, 3, 0); break;
                case 3: g_sort = S_PRIORITY; SendMessageW(g_hSort, CB_SETCURSEL, 1, 0); break;
                case 4: g_sort = S_DUE; SendMessageW(g_hSort, CB_SETCURSEL, 2, 0); break;
                default: g_sort = S_ORIG; SendMessageW(g_hSort, CB_SETCURSEL, 0, 0); break;
                }
                rebuildList();
            } else if (hdr->code == NM_CLICK) {
                POINT pt; GetCursorPos(&pt); ScreenToClient(g_hTaskList, &pt);
                LVHITTESTINFO hit{};
                hit.pt = pt;
                int row = ListView_SubItemHitTest(g_hTaskList, &hit);
                if (row >= 0 && hit.iSubItem <= 0) doToggle();
            } else if (hdr->code == NM_DBLCLK) {
                POINT pt; GetCursorPos(&pt); ScreenToClient(g_hTaskList, &pt);
                LVHITTESTINFO hit{};
                hit.pt = pt;
                int row = ListView_SubItemHitTest(g_hTaskList, &hit);
                if (row >= 0) {
                    if (hit.iSubItem <= 0) doToggle();
                    else if (hit.iSubItem == 1) ipStartEdit(row, 1);
                    else if (hit.iSubItem == 2) ipStartEdit(row, 2);
                    else doEditTask();
                }
            } else if (hdr->code == NM_RCLICK) {
                int si, ti;
                if (getSelected(si, ti)) {
                    POINT pt; GetCursorPos(&pt);
                    HMENU pop = CreatePopupMenu();
                    AppendMenuW(pop, MF_STRING, BTN_TOGGLE, L"标记完成 / 取消");
                    AppendMenuW(pop, MF_STRING, BTN_EDIT, L"编辑…");
                    AppendMenuW(pop, MF_STRING, BTN_DEL, L"删除");
                    AppendMenuW(pop, MF_SEPARATOR, 0, nullptr);
                    AppendMenuW(pop, MF_STRING, BTN_ADDTASK, L"新增任务…");
                    int cmd = (int)TrackPopupMenu(pop, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                                  pt.x, pt.y, 0, hWnd, nullptr);
                    DestroyMenu(pop);
                    if (cmd == BTN_TOGGLE) doToggle();
                    else if (cmd == BTN_EDIT) doEditTask();
                    else if (cmd == BTN_DEL) doDeleteTask();
                    else if (cmd == BTN_ADDTASK) doAddTask();
                }
            } else if (hdr->code == LVN_KEYDOWN) {
                NMLVKEYDOWN* k = (NMLVKEYDOWN*)lParam;
                if (k->wVKey == VK_DELETE) doDeleteTask();
                else if (k->wVKey == VK_SPACE) doToggle();
                else if (k->wVKey == VK_F2) doEditTask();
            } else if (hdr->code == NM_CUSTOMDRAW) {
                NMLVCUSTOMDRAW* cd = (NMLVCUSTOMDRAW*)lParam;
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    int row = (int)cd->nmcd.dwItemSpec;
                    if (row >= 0 && row < (int)g_visible.size()) {
                        int si = g_visible[row].first;
                        const Task& t = g_subjects[si].tasks[g_visible[row].second];
                        bool sel = (ListView_GetItemState(g_hTaskList, row, LVIS_SELECTED) & LVIS_SELECTED) != 0;
                        if (sel) {
                            cd->clrText = RGB(255, 255, 255);
                        } else {
                            cd->clrTextBk = subjectTint(si);
                            wstring today = todayStr();
                            if (t.done) cd->clrText = doneTextColor();
                            else if (!t.due.empty() && t.due < today) cd->clrText = RGB(220, 68, 68);
                            else cd->clrText = g_th.textPrimary;
                        }
                        return CDRF_NEWFONT;
                    }
                }
            }
        }
        return 0;
    }
    case WM_SJ_THEME:
        applyThemeToControls();
        rebuildTree();
        rebuildList();
        updateStatus();
        RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        return 0;
    case WM_SJ_REFRESH:
        refreshTasksPage();
        return 0;
    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

static void registerTaskClasses() {
    static bool done = false;
    if (done) return;
    done = true;
    struct { const wchar_t* name; WNDPROC proc; } classes[] = {
        { L"SJTaskDlg", TaskDlgProc },
        { L"SJSubjectDlg", SubjectDlgProc },
        { L"SJOpenDlg", OpenDlgProc },
        { L"SJSaveAsDlg", SaveAsDlgProc },
        { L"SJStatsDlg", StatsDlgProc },
        { L"SJPomoDlg", PomoDlgProc },
    };
    for (auto& c : classes) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = c.proc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = c.name;
        RegisterClassW(&wc);
    }
}

HWND createTasksPage(HWND parent) {
    registerTaskClasses();
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = TasksPageProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"StudyJournalTasksPage";
        RegisterClassExW(&wc);
        registered = true;
    }
    HWND h = CreateWindowExW(0, L"StudyJournalTasksPage", L"",
        WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 0, 0, parent, nullptr, g_hInst, nullptr);
    return h;
}

void tasksPageLayout() {
    if (g_hTasksPage) layoutTasksPage();
}
