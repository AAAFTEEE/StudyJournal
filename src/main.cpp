// ============================================================
//  main.cpp — StudyJournal 入口
//  初始化 DPI / 共用控件 / 数据 → 主窗口 → 消息循环
//  StudyJournal = DayJournal(日历日记) + StudyTodo(作业待办)
// ============================================================
#include "app.h"

bool createMainWindow(HINSTANCE hInst, int nCmdShow);

// 把设计尺寸 (1180×780 @96dpi) 缩放到当前屏幕可容纳的范围:
// 高 DPI 但屏幕较小时, 过大的缩放会让窗口超出桌面, 这里统一收敛。
double fitUiScale(double requested) {
    POINT origin = { 0, 0 };
    HMONITOR mon = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(mi) };
    int sw = 1280, sh = 800;
    if (GetMonitorInfoW(mon, &mi)) {
        sw = mi.rcWork.right - mi.rcWork.left;
        sh = mi.rcWork.bottom - mi.rcWork.top;
    }
    double fx = (sw - 40.0) / 1180.0;
    double fy = (sh - 50.0) / 780.0;
    double s = min(requested, min(fx, fy));
    return max(0.80, s);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    g_hInst = hInst;

    // 单实例: 重复启动时只唤醒已有窗口, 避免两个实例同时写同一份数据
    HANDLE hMutex = CreateMutexW(nullptr, FALSE, L"Local\\StudyJournal_SingleInstance");
    if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(L"StudyJournalMainWnd", nullptr);
        if (existing) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        CloseHandle(hMutex);
        return 0;
    }

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_PROGRESS_CLASS |
                ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    g_sysACP = GetACP();
    g_systemIsGBK = (g_sysACP == 936 || g_sysACP == 54936);

    // 高 DPI 感知 (Per-Monitor V2, 回退 System Aware)
    typedef BOOL (WINAPI *SetCtxFn)(HANDLE);
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    SetCtxFn setCtx = u32 ? (SetCtxFn)(void*)GetProcAddress(u32, "SetProcessDpiAwarenessContext") : nullptr;
    if (setCtx) setCtx((HANDLE)-4 /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */);
    else SetProcessDPIAware();
    HDC scr = GetDC(nullptr);
    int dpi = GetDeviceCaps(scr, LOGPIXELSX);
    ReleaseDC(nullptr, scr);
    g_scale = fitUiScale(dpi / 96.0);

    g_hCursorHand = LoadCursorW(nullptr, (LPCWSTR)IDC_HAND);
    createFonts();

    if (!initJournal()) { if (hMutex) CloseHandle(hMutex); return -1; }

    // 主题 + 共用画刷 (页面 / 对话框都用这套配色)
    applyTheme(savedThemeIndex());
    g_hBrBg = CreateSolidBrush(g_th.bg);
    g_hBrCard = CreateSolidBrush(g_th.card);

    loadSchedules();
    startHolidaySync();

    // 从旧 StudyTodo 的 homework 目录导入作业单 (仅在 tasks 为空时)
    wstring legacy = joinPathW(g_exeDir, L"homework");
    if (!fileExists(legacy)) legacy = joinPathW(joinPathW(g_exeDir, L".."), L"homework");
    importLegacyHomework(legacy);
    loadStartupTaskList();

    if (!createMainWindow(hInst, nCmdShow)) {
        cancelHolidaySync();
        if (hMutex) CloseHandle(hMutex);
        return -1;
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE && handleEditorEscape(msg.hwnd))
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    destroyFonts();
    cancelHolidaySync();
    if (g_hBrBg) DeleteObject(g_hBrBg);
    if (g_hBrCard) DeleteObject(g_hBrCard);
    if (hMutex) CloseHandle(hMutex);
    return (int)msg.wParam;
}
