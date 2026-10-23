// ============================================================
//  settings.cpp — DayJournal 设置窗口
//  主题色板列表 + 农历设置 (显示开关)
// ============================================================
#include "app.h"

static HWND g_settings = nullptr;
static const wchar_t* SET_CLASS = L"StudyJournalSettingsWnd";
static const int SID_LIST = 1000;
static const int SID_CLOSE = 900;
static const int SID_CHDIR = 901;
static const int SID_CB_LUNAR = 902;
static const int SID_CB_HOL   = 904;
static const int SID_DIRLABEL = 905;

HWND settingsWindow() { return g_settings; }

static void layoutSettings(HWND h) {
    RECT rc; GetClientRect(h, &rc);
    HWND list  = GetDlgItem(h, SID_LIST);
    HWND close = GetDlgItem(h, SID_CLOSE);
    HWND chdir = GetDlgItem(h, SID_CHDIR);
    HWND cbL = GetDlgItem(h, SID_CB_LUNAR), cbH = GetDlgItem(h, SID_CB_HOL);
    HWND dirLabel = GetDlgItem(h, SID_DIRLABEL);

    int L = S(16), w = rc.right - S(32);
    SetWindowPos(list,  nullptr, L, S(16),        w, S(250), SWP_NOZORDER);
    SetWindowPos(cbL,   nullptr, L, S(286),       S(90),  S(24), SWP_NOZORDER);
    SetWindowPos(cbH,   nullptr, L+S(96), S(286), S(130), S(24), SWP_NOZORDER);
    SetWindowPos(dirLabel, nullptr, L, S(318), w, S(40), SWP_NOZORDER);
    SetWindowPos(chdir, nullptr, L, rc.bottom - S(48), S(140), S(34), SWP_NOZORDER);
    SetWindowPos(close, nullptr, rc.right - S(120), rc.bottom - S(48), S(100), S(34), SWP_NOZORDER);
}

static LRESULT CALLBACK SettingsWndProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HWND list = CreateWindowW(L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT |
            LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_VSCROLL | WS_BORDER,
            0, 0, 0, 0, h, (HMENU)(INT_PTR)SID_LIST, g_hInst, nullptr);
        for (int i = 0; i < getThemeCount(); i++) {
            LRESULT idx = SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)getThemeName(i));
            SendMessageW(list, LB_SETITEMDATA, (WPARAM)idx, (LPARAM)i);
        }
        SendMessageW(list, LB_SETCURSEL, g_themeIndex, 0);

        // 农历设置区
        CreateWindowW(L"BUTTON", L"显示农历", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            0, 0, 0, 0, h, (HMENU)(INT_PTR)SID_CB_LUNAR, g_hInst, nullptr);
        CreateWindowW(L"BUTTON", L"显示调休/补班", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            0, 0, 0, 0, h, (HMENU)(INT_PTR)SID_CB_HOL, g_hInst, nullptr);
        SendMessageW(GetDlgItem(h, SID_CB_LUNAR), BM_SETCHECK, g_showLunar ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(GetDlgItem(h, SID_CB_HOL),   BM_SETCHECK, g_showHol   ? BST_CHECKED : BST_UNCHECKED, 0);

        HWND close = CreateWindowW(L"BUTTON", L"关闭", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 0, 0, h, (HMENU)(INT_PTR)SID_CLOSE, g_hInst, nullptr);
        regBtn(close, BK_SECONDARY);
        HWND chdir = CreateWindowW(L"BUTTON", L"更改数据目录", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 0, 0, h, (HMENU)(INT_PTR)SID_CHDIR, g_hInst, nullptr);
        regBtn(chdir, BK_SECONDARY);

        // 数据目录显示 (未设置时提示去选择)
        wstring dirText = g_hasDataDir && !g_journalRoot.empty()
                        ? (L"数据目录: " + g_journalRoot) : L"数据目录: 未设置 — 请点击下方按钮选择";
        HWND dirLabel = CreateWindowW(L"STATIC", dirText.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0, h, (HMENU)(INT_PTR)SID_DIRLABEL, g_hInst, nullptr);

        HWND ctrls[] = { list, close, chdir, GetDlgItem(h, SID_CB_LUNAR),
                         GetDlgItem(h, SID_CB_HOL), dirLabel };
        for (HWND c : ctrls) SendMessageW(c, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        layoutSettings(h);
        break;
    }
    case WM_SIZE: layoutSettings(h); return 0;
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
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, g_th.textPrimary);
        return (LRESULT)g_hBrBg;
    }
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT* mi = (MEASUREITEMSTRUCT*)lParam;
        if (mi->CtlID == SID_LIST) { mi->itemHeight = S(44); return TRUE; }
        break;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lParam;
        if (g_btns.count(di->hwndItem)) { drawButton(di); return TRUE; }
        if (di->CtlID == SID_LIST) {
            HDC hdc = di->hDC;
            RECT rc = di->rcItem;
            int idx = (int)di->itemData;
            bool selected = (di->itemState & ODS_SELECTED) != 0;
            bool isCur = (idx == g_themeIndex);

            HBRUSH hb = CreateSolidBrush(selected ? g_th.accentLight : g_th.card);
            FillRect(hdc, &rc, hb);
            DeleteObject(hb);

            int sw = S(40), off = S(14);
            RECT s1 = { rc.left+off, rc.top+S(7), rc.left+off+sw, rc.bottom-S(7) };
            HBRUSH bAcc = CreateSolidBrush(g_themePresets[idx].accent);
            FillRect(hdc, &s1, bAcc);
            DeleteObject(bAcc);
            RECT s2 = { rc.left+off+sw+S(6), rc.top+S(7), rc.left+off+sw+S(6)+S(22), rc.bottom-S(7) };
            HBRUSH bBg = CreateSolidBrush(g_themePresets[idx].bg);
            FillRect(hdc, &s2, bBg);
            DeleteObject(bBg);
            RECT s3 = { rc.left+off+sw+S(34), rc.top+S(7), rc.left+off+sw+S(34)+S(22), rc.bottom-S(7) };
            HBRUSH bTxt = CreateSolidBrush(g_themePresets[idx].textPrimary);
            FillRect(hdc, &s3, bTxt);
            DeleteObject(bTxt);

            SetBkMode(hdc, TRANSPARENT);
            SelectObject(hdc, g_hFontBold);
            SetTextColor(hdc, isCur ? g_th.accentDark : g_th.textPrimary);
            wstring label = getThemeName(idx);
            if (isCur) label += L"   ← 当前";
            RECT tr = { rc.left + off + sw + S(70), rc.top, rc.right - S(12), rc.bottom };
            DrawTextW(hdc, label.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == SID_LIST && HIWORD(wParam) == LBN_SELCHANGE) {
            HWND list = GetDlgItem(h, SID_LIST);
            int sel = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR) {
                int idx = (int)SendMessageW(list, LB_GETITEMDATA, sel, 0);
                applyTheme(idx);
                saveThemeIndex(idx);
                InvalidateRect(h, nullptr, TRUE);
            }
        } else if (id == SID_CB_LUNAR || id == SID_CB_HOL) {
            if (HIWORD(wParam) == BN_CLICKED) {
                g_showLunar = (SendMessageW(GetDlgItem(h, SID_CB_LUNAR), BM_GETCHECK, 0, 0) == BST_CHECKED);
                g_showHol   = (SendMessageW(GetDlgItem(h, SID_CB_HOL),   BM_GETCHECK, 0, 0) == BST_CHECKED);
                saveConfig(g_journalRoot);
                if (g_hWnd) InvalidateRect(g_hWnd, nullptr, FALSE);
            }
        } else if (id == SID_CHDIR) {
            BROWSEINFOW bi = {0};
            wchar_t szPath[MAX_PATH] = {0};
            bi.hwndOwner = h;
            bi.lpszTitle = L"选择 StudyJournal 数据存放文件夹 (旧数据会自动复制过去)";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
            if (pidl) {
                SHGetPathFromIDListW(pidl, szPath);
                CoTaskMemFree(pidl);
                if (wcslen(szPath) > 0) {
                    if (setJournalRoot(szPath)) {
                        HWND dirLabel = GetDlgItem(h, SID_DIRLABEL);
                        if (dirLabel) SetWindowTextW(dirLabel, (L"数据目录: " + wstring(szPath)).c_str());
                        loadStartupTaskList();
                        loadSchedules();
                        refreshJournalPage();
                        refreshTasksPage();
                        updateSidebarSummary();
                        InvalidateRect(h, nullptr, TRUE);
                    } else {
                        MessageBoxW(h, L"数据目录设置失败, 请检查路径是否有权限。", L"StudyJournal",
                                    MB_OK | MB_ICONWARNING);
                    }
                }
            }
        } else if (id == SID_CLOSE) {
            DestroyWindow(h);
        }
        return 0;
    }
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: g_settings = nullptr; break;
    default: return DefWindowProcW(h, msg, wParam, lParam);
    }
    return 0;
}

void openSettingsWindow(HWND parent) {
    if (g_settings) { SetForegroundWindow(g_settings); return; }
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = SettingsWndProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = SET_CLASS;
        wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        RegisterClassExW(&wc);
        registered = true;
    }
    RECT rc = {0, 0, S(380), S(440)};
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    g_settings = CreateWindowExW(0, SET_CLASS, L"StudyJournal 设置 — 主题 / 日历 / 数据",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        parent, nullptr, g_hInst, nullptr);
    if (!g_settings) return;
    DWORD pref = 2; DwmSetWindowAttribute(g_settings, 33, &pref, sizeof(pref));
    COLORREF c = g_th.bg; DwmSetWindowAttribute(g_settings, 35, &c, sizeof(c));
    ShowWindow(g_settings, SW_SHOW);
    UpdateWindow(g_settings);
}
