// ============================================================
//  app.h — StudyJournal 共享声明 / 数据结构 / 主题 / 全局变量
//  StudyJournal 1.0 — 日历日记 + 作业待办 (原生 Win32, 零外部依赖)
// ============================================================
#ifndef STUDYJOURNAL_H
#define STUDYJOURNAL_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#ifndef EM_SETBKGNDCOLOR
#define EM_SETBKGNDCOLOR (WM_USER + 67)
#endif

#include <windows.h>
#include <richedit.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <objidl.h>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include <ctime>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cwctype>

using namespace std;

// ============================================================
//  主题 (内置多套调色, 可在设置中切换)
// ============================================================
struct Theme {
    COLORREF bg, card, cardBorder, accent, accentDark, accentLight;
    COLORREF hover, hoverBtn, pressBtn, textPrimary, textSecondary, textWeekend;
    COLORREF entryDot, danger, dangerHover, disabledText, disabledFill, warn;
};
extern Theme g_th;                       // 当前生效主题
extern Theme g_themePresets[7];          // 内置主题库
extern int   g_themeIndex;               // 当前主题下标
extern const wchar_t* g_themeNames[7];
int   getThemeCount();
void  applyTheme(int idx);               // 应用主题并重绘 / 刷新标题栏
const wchar_t* getThemeName(int idx);
bool  isDarkTheme();                     // 当前主题是否为深色

// 页面 / 主窗口自定义消息
#define WM_SJ_THEME      (WM_APP + 11)   // 主题变化 → 页面刷新配色
#define WM_SJ_REFRESH    (WM_APP + 12)   // 请求页面刷新数据

// ============================================================
//  日记条目  (内部恒为宽字符, 落盘为 UTF-8 纯文本)
// ============================================================
struct DiaryEntry {
    int32_t open_cnt;
    int16_t year, mon, day;
    int16_t hour, min, sec;
    wstring text;                       // 该条目的纯文本正文
    wstring base;                       // 独立条目文件名 (HH-MM-SS 或 HH-MM-SS_N)

    wstring dateStr() const {
        wchar_t buf[16];
        swprintf(buf, 16, L"%04d-%02d-%02d", year, mon, day);
        return wstring(buf);
    }
    wstring timeStr() const {
        wchar_t buf[12];
        swprintf(buf, 12, L"%02d:%02d:%02d", hour, min, sec);
        return wstring(buf);
    }
};

// ============================================================
//  全局变量
// ============================================================
extern HINSTANCE g_hInst;
extern HWND    g_hWnd;
extern HFONT   g_hFont, g_hFontBold, g_hFontTitle, g_hFontCal, g_hFontCalBold, g_hFontSmall;
extern HFONT   g_hFontLogo;              // 侧边栏品牌字
extern HBRUSH  g_hBrBg, g_hBrCard;
extern HCURSOR g_hCursorHand;
extern UINT    g_sysACP;
extern bool    g_systemIsGBK;

extern wstring g_journalRoot;           // 日记本数据根目录
extern wstring g_exeDir;
extern wstring g_configFile;            // dayjournal_config.txt
extern wstring g_metaFile;              // DayJournal.meta.json
extern int32_t g_openCount;
extern bool    g_hasDataDir;            // 是否已设置有效数据目录 (false → 主界面提示去设置)

extern vector<DiaryEntry> g_entries;

extern int g_calYear, g_calMon, g_selectedDay;
extern int g_todayDay, g_todayMon, g_todayYear;

extern int  g_hoverCell;
extern bool g_mouseTracking;

// v5: DPI 缩放
extern double g_scale;
inline int S(int v) { return (int)lround(v * g_scale); }

// ============================================================
//  控件 ID — 主窗口
// ============================================================
enum {
    IDC_PREV = 200, IDC_NEXT, IDC_WRITE, IDC_TODAY,
    IDC_DELETE, IDC_STATS,
    IDC_EXPORT, IDC_IMPORT, IDC_LIST,
    IDC_YEARLABEL, IDC_HINT, IDC_PLACEHOLDER,
    IDC_OPENFOLDER, IDC_SETTINGS, IDC_PREVYEAR, IDC_NEXTYEAR,
    IDC_SCHED, IDC_TITLEBAR, IDC_TODAYMSG,
    IDC_CAL_FIRST = 1000
};

//  右键菜单命令 ID
enum {
    MNU_WRITE = 300, MNU_VIEW, MNU_EDIT, MNU_DELETE, MNU_COPY_TITLE, MNU_COPY_TEXT,
    MNU_SCHED_NEW, MNU_TODAY, MNU_STATS, MNU_EXPORT,
    MNU_IMPORT, MNU_OPENFOLDER, MNU_SETTINGS, MNU_OPENDAY,
    MNU_COPY_AS_MARKDOWN
};

//  控件 ID — 纯文本编辑器窗口
enum {
    ED_SAVE = 500, ED_CANCEL, ED_ATTACH, ED_INSERTTIME, ED_OPENFOLDER,
    ED_SOURCE, ED_PREVIEW, ED_SPLIT, ED_IMGLINK,
    ED_BOLD, ED_ITALIC, ED_CODE, ED_FONTPLUS, ED_FONTMINUS,
    ED_H1, ED_H2, ED_QUOTE, ED_UL, ED_OL, ED_LINK, ED_HR, ED_STRIKE,
    ED_CODEBLOCK, ED_TODO, ED_MATHIN, ED_MATHBLK, ED_TITLE
};

extern HWND g_hPrev, g_hNext, g_hYearLabel, g_hTodayBtn, g_hWriteBtn;
extern HWND g_hDeleteBtn, g_hStatsBtn, g_hExportBtn, g_hImportBtn, g_hExitBtn;
extern HWND g_hList, g_hHintLabel, g_hPlaceholder, g_hOpenFolderBtn;
extern HWND g_hSettingsBtn, g_hPrevYear, g_hNextYear, g_hSchedBtn;
extern HWND g_hTitleBar;

// 条目标题提取 (正文第一行非空文本)
wstring entryTitle(const wstring& text);
// 日历查看窗口: 单击日期弹出, 预览该天全文, 底部"编辑"进入编辑器
// base 非空时仅预览该单个条目 (编辑时精确回填原条目文件)
void openViewWindow(int year, int mon, int day, const wstring& base = L"");

// ============================================================
//  自绘按钮
// ============================================================
enum BtnKind { BK_PRIMARY, BK_SECONDARY, BK_GHOST, BK_DANGER };
struct BtnInfo { BtnKind kind; bool hover; WNDPROC oldProc; };
extern map<HWND, BtnInfo> g_btns;
void regBtn(HWND h, BtnKind kind);
void drawButton(const DRAWITEMSTRUCT* di);

// ============================================================
//  encoding.cpp — 编码 / 文件 / 路径工具
// ============================================================
wstring mbToWide(const string& s, UINT cp);
string  wideToMb(const wstring& w, UINT cp);
wstring toWide(const string& s);
string  toUTF8(const wstring& w);
wstring toLowerW(const wstring& s);
bool    isValidUTF8(const string& s);
wstring decodeText(const string& b);
UINT    exportCodePage();

wstring getExeDirW();
wstring joinPathW(const wstring& base, const wstring& name);
bool    createDirW(const wstring& dir);
bool    fileExists(const wstring& path);
bool    readWholeFile(const wstring& path, string& out);
bool    writeWholeFile(const wstring& path, const string& bytes);
wstring getHomeDirW();
wstring getDefaultJournalDirW();

// ============================================================
//  storage.cpp — 纯文本日记存储 / 配置 / 迁移 / 备份
// ============================================================
bool    loadConfig(wstring& dir);
void    saveConfig(const wstring& dir);
bool    initJournal();                       // 配置 → 迁移 → 载入 → 备份 → 元数据
bool    setJournalRoot(const wstring& newDir); // 切换到新数据目录 (自动复制旧目录内容), 返回是否成功
void    reloadMeta();
void    saveMeta();
wstring dayFilePath(const wstring& dateStr); // entries/YYYY-MM-DD.md
wstring attachmentsDirFor(const wstring& dateStr);
wstring attachmentPath(const wstring& dateStr, const wstring& fname);
bool    copyFileTo(const wstring& src, const wstring& dst);
vector<DiaryEntry> parseDayMarkdown(const wstring& text, int year, int mon, int day);
bool    saveDayEntries(const wstring& dateStr, const vector<DiaryEntry>& entries); // 写整个日文件
bool    writeDayFileText(const wstring& dateStr, const wstring& markdown);         // (兼容旧按天文件)
void    refreshFromDisk(const wstring& onlyDate = L"");   // 重建 g_entries
void    makeDailyBackup();

// --- 新结构: 每条日记独立文件 entries/YYYY-MM-DD/HH-MM-SS.md ---
wstring dayDir(const wstring& dateStr);                              // entries/YYYY-MM-DD
wstring allocEntryBase(const wstring& dateStr, int h, int m, int s); // 生成不冲突的条目 base
bool    saveEntryFileBase(const wstring& dateStr, const wstring& base, const wstring& text);
bool    readEntryFileBase(const wstring& dateStr, const wstring& base, wstring& out);
bool    deleteEntryFileBase(const wstring& dateStr, const wstring& base);
vector<DiaryEntry> listDayEntries(const wstring& dateStr);           // 读取一天的所有条目文件

// ============================================================
//  editor.cpp — 纯文本日记编辑器 (附件插入正文)
// ============================================================
void openEntryEditor(const wstring& dateStr, bool newEntry, const wstring& base = L"");
bool handleEditorEscape(HWND focus);
HWND editorWindow();                    // 编辑器窗口句柄 (未打开返回 nullptr)

// ============================================================
//  settings.cpp — 设置窗口 (主题等)
// ============================================================
void openSettingsWindow(HWND parent);
HWND settingsWindow();
int  savedThemeIndex();                 // 从配置文件读取的主题
void saveThemeIndex(int idx);

// ============================================================
//  ui_main.cpp — 主窗口
// ============================================================
void updateCalendar();
void updateList();
int  calHitTest(int x, int y);
void destroyFonts();
void createFonts();

// 农历显示开关 (设置 → 农历设置)
extern bool g_showLunar;      // 显示农历小字
extern bool g_showFest;       // 显示节日小字
extern bool g_showHol;        // 显示调休/补班标记

// 自定义农历节日 {农历月, 农历日} → 名称 (calendar 读 / storage 持久化)
extern vector<pair<pair<int,int>,wstring>> g_customLunarFest;
void loadLunarFestCustom();
void saveLunarFestCustom();

// ============================================================
//  calendar.cpp — 农历 / 节日 / 休假调休
// ============================================================
struct LunarDate { int year = 0, month = 0, day = 0; bool isLeap = false; };
LunarDate solarToLunar(int y, int m, int d);
wstring lunarDisplay(int y, int m, int d);      // 农历文字, 如 "八月十五"
wstring lunarMonthDayName(int mon, int day);    // 农历月日读法, 如 8,15 → "八月十五"
wstring festivalsOf(int y, int m, int d);       // 当天节日 (可多个, 分号分隔; 无则空)
char    holidayStatusOf(int y, int m, int d);   // 'H' 放假 / 'W' 补班 / 0 普通
wstring zodiacOf(int y);                        // 生肖
wstring ganzhiOfYear(int y);                    // 干支纪年
long    civilDays(int y, int m, int d);         // 公历 → 天数 (1970-01-01 基准)
void    civilFromDays(long days, int& y, int& m, int& d); // 天数 → 公历

// 联网同步节假日: 启动时异步获取若干年份的放假/调休到内存缓存 (holidayStatusOf 优先查)
void    startHolidaySync();                     // 后台线程拉取 近3年± 数据
void    cancelHolidaySync();                    // 程序退出时终止线程
void    requestHolidayYear(int year);           // 按需异步同步某一年 (切年份时调用)

// ============================================================
//  schedules.cpp — 自定义日程 / 倒数计时
// ============================================================
// 分类: 0 普通 / 1 生日 / 2 纪念日 / 3 考试 / 4 节日 / 5 提醒
enum SchedCat {
    SCAT_ORDINARY = 0, SCAT_BIRTHDAY, SCAT_ANNIVERSARY,
    SCAT_EXAM, SCAT_FESTIVAL, SCAT_REMIND
};
static const wchar_t* SCAT_NAME[] = { L"普通", L"生日", L"纪念日", L"考试", L"节日", L"提醒" };

struct Sched {
    wstring name;
    int type = 0;            // 0 每年公历 / 1 每N年公历 / 2 单次公历 / 3 每年农历
    int mon = 1, day = 1;    // 公历月日 (type0/1/2) 或 农历月日 (type3)
    int intervalYears = 1;   // 每 N 年 (type1)
    int startYear = 0;       // type1 周期起始年
    int fixedYear = 0;       // type2 单次年
    bool countFrom = false;  // true=计时(已过天数) / false=倒数(距下次)
    int cat = SCAT_ORDINARY; // 分类标签
    wstring note;
};
void loadSchedules();
void saveSchedules();
bool nextOccurrence(const Sched& s, int& y, int& m, int& d);   // 下一次发生日期
bool previousOccurrence(const Sched& s, int& y, int& m, int& d); // 最近一次(≤今天)发生日期
long   daysRelativeToToday(int y, int m, int d);               // 相对今天天数(可为负)
bool   anyScheduleOn(int y, int m, int d);                     // 该公历日是否有日程
void openSchedulesWindow(HWND parent);
// 今天命中的日程 → 提示文本列表 (用于主界面滚动提示)
vector<wstring> todayScheduleMessages();

// ============================================================
//  StudyJournal 1.0 — 统一主窗口 / 页面 / 待办模型
// ============================================================
#define SJ_APP_NAME    L"StudyJournal"
#define SJ_APP_VERSION L"1.1.0"

enum { PAGE_JOURNAL = 0, PAGE_TASKS = 1, PAGE_COUNT = 2 };

extern HWND g_hJournalPage;            // 日记页面 (主窗口子窗口)
extern HWND g_hTasksPage;              // 待办页面 (主窗口子窗口)
extern int  g_activePage;              // 当前页面 PAGE_JOURNAL / PAGE_TASKS
extern bool g_themeSwitching;          // 主题切换时抑制子控件闪烁

HWND createJournalPage(HWND parent);
HWND createTasksPage(HWND parent);
double fitUiScale(double requested);   // 按屏幕大小收敛 DPI 缩放
bool createMainWindow(HINSTANCE hInst, int nCmdShow);
void journalPageLayout();
void tasksPageLayout();
void refreshJournalPage();             // 重新载入列表 + 重绘日历
void refreshTasksPage();               // 重建科目树 / 任务列表 / 状态
void refreshThemeAll();                // 主题变化后刷新所有页面与窗口
void switchPage(int idx);
void updateSidebarSummary();           // 侧边栏「今日概览」

// ---- 待办 / 作业数据模型 (沿用 StudyTodo 的文本格式) ----
struct Task {
    wstring text;
    bool    done = false;
    int     priority = 0;              // 0 低 / 1 中 / 2 高
    wstring due;                       // "yyyy-MM-dd" 或空
    wstring tag;
};
struct Subject {
    wstring name;
    vector<Task> tasks;
};

extern vector<Subject> g_subjects;
extern wstring g_taskListName;         // 当前作业单名 (不含扩展名)
extern wstring g_lastTaskName;         // 配置中记住的上次作业单

wstring taskListDir();                 // <数据根>/tasks
vector<wstring> listTaskFiles();       // 作业单文件名 (含 .txt)
bool loadTaskFile(const wstring& fileName);      // 支持带/不带 .txt
bool saveTaskFile(const wstring& name);          // name 不含 .txt
void autoSaveTasks();
bool ensureTaskList();                 // 无作业单时创建默认空作业单
void importLegacyHomework(const wstring& sourceDir);
void loadStartupTaskList();
int  taskTotal();
int  taskDone();
int  tasksDueOn(int y, int m, int d);  // 当天到期且未完的待办数
int  tasksDueToday();
int  journalEntriesToday();
wstring taskListDisplayName();
wstring dueDisplayText(const Task& t); // 截止日期 + 超期/临期标记

// 待办页面内部
void openTaskStatsWindow(HWND parent);
void showPomodoroWindow(HWND parent);

#endif // STUDYJOURNAL_H
