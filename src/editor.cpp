// ============================================================
//  editor.cpp — 日记条目编辑器（纯文本）
//  一个多行文本框，所见即所存；不再做 Markdown 渲染或分屏预览。
//  附件以普通文本行插入正文，不嵌入图片。
// ============================================================
#include "app.h"

#ifndef EM_SETTEXTMODE
#define EM_SETTEXTMODE (WM_USER + 89)
#endif
#ifndef TM_PLAINTEXT
#define TM_PLAINTEXT 1
#endif

static const wchar_t* ED_CLASS = L"StudyJournalEditorWnd";
static bool g_richLoaded = false;

struct EditorCtx {
    HWND hWnd = nullptr;
    HWND hSource = nullptr;
    HWND hSave = nullptr, hCancel = nullptr, hAttach = nullptr, hOpenFolder = nullptr;
    wstring dateStr;
    wstring base;
    bool modified = false;
    HFONT hFont = nullptr;
};
static EditorCtx g_ed;

static void ensureRichEdit() {
    if (g_richLoaded) return;
    LoadLibraryW(L"riched20.dll");
    g_richLoaded = true;
}

static wstring textOf(HWND hEdit) {
    int len = GetWindowTextLengthW(hEdit);
    if (len <= 0) return L"";
    wstring t(len, 0);
    GetWindowTextW(hEdit, &t[0], len + 1);
    return t;
}

static wstring baseToTimeStr(const wstring& base) {
    wstring r = base;
    for (auto& c : r) if (c == L'-') c = L':';
    return r;
}

static void setEditorTitle() {
    if (!g_ed.hWnd) return;
    wstring title = L"StudyJournal — 编辑日记 <" + g_ed.dateStr + L" " +
                    baseToTimeStr(g_ed.base) + L">";
    if (g_ed.modified) title += L"  *";
    SetWindowTextW(g_ed.hWnd, title.c_str());
}

static void applyEditorFonts() {
    if (!g_ed.hSource) return;
    if (!g_ed.hFont)
        g_ed.hFont = CreateFontW(-S(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                 L"Microsoft YaHei UI");
    SendMessageW(g_ed.hSource, WM_SETFONT, (WPARAM)g_ed.hFont, TRUE);
    SendMessageW(g_ed.hSource, EM_SETBKGNDCOLOR, 0, (LPARAM)g_th.card);

    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR;
    cf.yHeight = S(15) * 15;
    cf.crTextColor = g_th.textPrimary;
    wcscpy(cf.szFaceName, L"Microsoft YaHei UI");
    SendMessageW(g_ed.hSource, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
}

static void layoutEditor() {
    if (!g_ed.hWnd) return;
    RECT rc; GetClientRect(g_ed.hWnd, &rc);
    int pad = S(14), hh = S(34);
    int x = pad;
    auto place = [&](HWND h, int w) {
        SetWindowPos(h, nullptr, x, pad, w, hh, SWP_NOZORDER);
        x += w + S(8);
    };
    place(g_ed.hSave, S(78));
    place(g_ed.hCancel, S(78));
    place(g_ed.hAttach, S(88));
    SetWindowPos(g_ed.hOpenFolder, nullptr, rc.right - pad - S(128), pad, S(128), hh, SWP_NOZORDER);

    int top = pad + hh + S(10);
    SetWindowPos(g_ed.hSource, nullptr, pad, top, rc.right - pad * 2,
                 max(S(80), (int)(rc.bottom - top - pad)), SWP_NOZORDER);
    InvalidateRect(g_ed.hWnd, nullptr, FALSE);
}

static bool doSaveEditor() {
    if (!g_ed.hWnd || !g_ed.hSource) return false;
    wstring text = textOf(g_ed.hSource);
    if (text.find_first_not_of(L" \t\r\n") == wstring::npos) {
        MessageBoxW(g_ed.hWnd, L"内容不能为空。", L"StudyJournal", MB_OK | MB_ICONINFORMATION);
        return false;
    }
    if (!saveEntryFileBase(g_ed.dateStr, g_ed.base, text)) {
        MessageBoxW(g_ed.hWnd, L"写入失败，请检查数据目录权限。", L"StudyJournal", MB_OK | MB_ICONERROR);
        return false;
    }
    g_ed.modified = false;
    setEditorTitle();
    refreshJournalPage();
    return true;
}

static void closeEditor(bool force = false) {
    if (!g_ed.hWnd) return;
    if (!force && g_ed.modified) {
        int r = MessageBoxW(g_ed.hWnd, L"这条日记还没有保存，要保存吗？", L"StudyJournal",
                            MB_YESNOCANCEL | MB_ICONQUESTION);
        if (r == IDCANCEL) return;
        if (r == IDYES && !doSaveEditor()) return;
    }
    DestroyWindow(g_ed.hWnd);
}

static void insertAtCaret(const wstring& text) {
    if (!g_ed.hSource) return;
    SendMessageW(g_ed.hSource, EM_REPLACESEL, TRUE, (LPARAM)text.c_str());
    g_ed.modified = true;
    setEditorTitle();
}

static void doAttach() {
    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_ed.hWnd;
    ofn.lpstrFilter = L"所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"添加附件（复制到该日期的附件目录）";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;

    wstring src = path;
    size_t slash = src.find_last_of(L"\\/");
    wstring name = (slash == wstring::npos) ? src : src.substr(slash + 1);
    if (name.empty()) return;
    createDirW(attachmentsDirFor(g_ed.dateStr));
    wstring dst = attachmentPath(g_ed.dateStr, name);
    if (fileExists(dst)) {
        size_t dot = name.find_last_of(L'.');
        wstring stem = (dot == wstring::npos) ? name : name.substr(0, dot);
        wstring ext  = (dot == wstring::npos) ? L"" : name.substr(dot);
        int n = 1;
        do {
            dst = attachmentPath(g_ed.dateStr, stem + L"_" + to_wstring(n) + ext);
            n++;
        } while (fileExists(dst));
    }
    if (!copyFileTo(src, dst)) {
        MessageBoxW(g_ed.hWnd, L"附件复制失败。", L"StudyJournal", MB_OK | MB_ICONWARNING);
        return;
    }
    size_t slash2 = dst.find_last_of(L"\\/");
    wstring savedName = (slash2 == wstring::npos) ? dst : dst.substr(slash2 + 1);
    insertAtCaret(L"\r\n附件：" + savedName + L"\r\n");
    MessageBoxW(g_ed.hWnd, (L"已添加附件：" + savedName).c_str(),
                L"StudyJournal", MB_OK | MB_ICONINFORMATION);
}

static LRESULT CALLBACK EditorWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_ed.hWnd = hWnd;
        auto mkBtn = [&](const wchar_t* txt, int id, BtnKind k) {
            HWND h = CreateWindowW(L"BUTTON", txt, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                0, 0, 0, 0, hWnd, (HMENU)(INT_PTR)id, g_hInst, nullptr);
            regBtn(h, k);
            SendMessageW(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            return h;
        };
        g_ed.hSave       = mkBtn(L"保存", ED_SAVE, BK_PRIMARY);
        g_ed.hCancel     = mkBtn(L"取消", ED_CANCEL, BK_SECONDARY);
        g_ed.hAttach     = mkBtn(L"＋ 附件", ED_ATTACH, BK_SECONDARY);
        g_ed.hOpenFolder = mkBtn(L"打开数据目录", ED_OPENFOLDER, BK_SECONDARY);

        g_ed.hSource = CreateWindowExW(WS_EX_CLIENTEDGE, L"RichEdit20W", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_WANTRETURN | WS_VSCROLL |
            ES_AUTOVSCROLL,
            0, 0, 0, 0, hWnd, (HMENU)ED_SOURCE, g_hInst, nullptr);
        SendMessageW(g_ed.hSource, EM_SETTEXTMODE, TM_PLAINTEXT, 0);
        SendMessageW(g_ed.hSource, EM_SETEVENTMASK, 0, ENM_CHANGE);
        applyEditorFonts();
        layoutEditor();
        SetFocus(g_ed.hSource);
        return 0;
    }

    case WM_SIZE:
        layoutEditor();
        return 0;

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc; GetClientRect(hWnd, &rc);
        HBRUSH hb = CreateSolidBrush(g_th.bg);
        FillRect(hdc, &rc, hb);
        DeleteObject(hb);
        return 1;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lParam;
        if (g_btns.count(di->hwndItem)) { drawButton(di); return TRUE; }
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (HIWORD(wParam) == EN_CHANGE && (HWND)lParam == g_ed.hSource) {
            g_ed.modified = true;
            setEditorTitle();
            return 0;
        }
        switch (id) {
        case ED_SAVE:
            if (doSaveEditor()) DestroyWindow(hWnd);
            return 0;
        case ED_CANCEL:
            closeEditor();
            return 0;
        case ED_ATTACH:
            doAttach();
            return 0;
        case ED_OPENFOLDER:
            ShellExecuteW(nullptr, L"open", g_journalRoot.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        break;
    }

    case WM_SJ_THEME:
        applyEditorFonts();
        InvalidateRect(hWnd, nullptr, TRUE);
        return 0;

    case WM_CLOSE:
        closeEditor();
        return 0;

    case WM_DESTROY:
        if (g_ed.hFont) { DeleteObject(g_ed.hFont); g_ed.hFont = nullptr; }
        g_ed.hWnd = nullptr;
        g_ed.hSource = nullptr;
        g_ed.hSave = g_ed.hCancel = g_ed.hAttach = g_ed.hOpenFolder = nullptr;
        break;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

void openEntryEditor(const wstring& dateStr, bool newEntry, const wstring& base) {
    ensureRichEdit();
    if (g_ed.hWnd) {
        if (g_ed.dateStr == dateStr && g_ed.base == base) {
            SetForegroundWindow(g_ed.hWnd);
            return;
        }
        closeEditor(true);
    }

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = EditorWndProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = ED_CLASS;
        wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        RegisterClassExW(&wc);
        registered = true;
    }

    g_ed.dateStr = dateStr;
    g_ed.modified = false;
    if (newEntry) {
        time_t t = time(nullptr); tm* p = localtime(&t);
        g_ed.base = allocEntryBase(dateStr, p->tm_hour, p->tm_min, p->tm_sec);
    } else {
        g_ed.base = base;
    }

    wstring text;
    if (!newEntry && !base.empty()) readEntryFileBase(dateStr, base, text);

    RECT rc = {0, 0, S(900), S(640)};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    g_ed.hWnd = CreateWindowExW(0, ED_CLASS, L"",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        g_hWnd, nullptr, g_hInst, nullptr);
    if (!g_ed.hWnd) return;

    DWORD pref = 2; DwmSetWindowAttribute(g_ed.hWnd, 33, &pref, sizeof(pref));
    BOOL dark = isDarkTheme(); DwmSetWindowAttribute(g_ed.hWnd, 20, &dark, sizeof(dark));
    COLORREF c = g_th.bg; DwmSetWindowAttribute(g_ed.hWnd, 35, &c, sizeof(c));

    SendMessageW(g_ed.hSource, WM_SETTEXT, 0, (LPARAM)text.c_str());
    applyEditorFonts();
    setEditorTitle();
    int len = (int)text.size();
    SendMessageW(g_ed.hSource, EM_SETSEL, len, len);
    ShowWindow(g_ed.hWnd, SW_SHOW);
    UpdateWindow(g_ed.hWnd);
    SetFocus(g_ed.hSource);
}

bool handleEditorEscape(HWND focus) {
    if (!g_ed.hWnd) return false;
    HWND all[] = { g_ed.hSource, g_ed.hSave, g_ed.hCancel, g_ed.hAttach, g_ed.hOpenFolder };
    for (HWND h : all) {
        if (focus == h) {
            PostMessageW(g_ed.hWnd, WM_CLOSE, 0, 0);
            return true;
        }
    }
    return false;
}

HWND editorWindow() { return g_ed.hWnd; }
