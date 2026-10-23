// ============================================================
//  test_core.cpp — StudyJournal 核心逻辑无界面自测
//  验证: 日记纯文本按条存储 / 旧日文件解析 / 作业单读写与 GBK 兼容
//  编译见 build.bat 或 CMakeLists.txt
// ============================================================
#include "../src/app.h"

HINSTANCE g_hInst = nullptr;
HWND g_hWnd = nullptr;
UINT g_sysACP = 936;
bool g_systemIsGBK = true;
double g_scale = 1.0;
vector<DiaryEntry> g_entries;
int g_calYear=2026, g_calMon=9, g_selectedDay=0;
int g_todayDay=0, g_todayMon=0, g_todayYear=0;
int g_hoverCell=-1;
bool g_mouseTracking=false;
int g_themeIndex=0;
int g_activePage=0;

int fails = 0;
#define CHECK(cond, name) do { if (cond) printf("[PASS] %s\n", name); \
    else { printf("[FAIL] %s\n", name); fails++; } } while(0)

int main() {
    g_journalRoot = L".\\build\\test_journal";
    g_exeDir = L".";
    createDirW(g_journalRoot);
    createDirW(joinPathW(g_journalRoot, L"entries"));

    // ---- 1) 纯文本条目按天保存 ----
    DiaryEntry e1; e1.open_cnt=0; e1.year=2026; e1.mon=9; e1.day=26;
    e1.hour=9; e1.min=5; e1.sec=0;
    e1.text = L"早上写了一题，很棒。\n- 列表项1\n- 列表项2";
    DiaryEntry e2; e2.open_cnt=0; e2.year=2026; e2.mon=9; e2.day=26;
    e2.hour=21; e2.min=30; e2.sec=0;
    e2.text = L"第二段纯文本。\n换行也要原样保留。";
    vector<DiaryEntry> day = { e1, e2 };
    CHECK(saveDayEntries(L"2026-09-26", day), "保存条目 → 日文件");

    // ---- 2) 读取并往返解析 ----
    string raw;
    CHECK(readWholeFile(dayFilePath(L"2026-09-26"), raw), "读取日文件");
    wstring text = decodeText(raw);
    auto parsed = parseDayMarkdown(text, 2026, 9, 26);
    CHECK(parsed.size() == 2, "解析出 2 条条目");
    if (parsed.size() == 2) {
        CHECK(parsed[0].hour==9 && parsed[0].min==5, "条目1 时间正确");
        CHECK(parsed[1].hour==21 && parsed[1].min==30, "条目2 时间正确");
        CHECK(parsed[0].text.find(L"很棒") != wstring::npos, "条目1 内容保留");
        CHECK(parsed[1].text.find(L"换行") != wstring::npos, "条目2 内容保留");
    }

    // ---- 3) 直接写整日文本 (旧接口兼容) ----
    wstring edited = L"# 2026-09-26\n\n## 12:00:00\n直接编辑的内容\n";
    CHECK(writeDayFileText(L"2026-09-26", edited), "直接写日文件文本");
    string raw2; readWholeFile(dayFilePath(L"2026-09-26"), raw2);
    auto parsed2 = parseDayMarkdown(decodeText(raw2), 2026, 9, 26);
    CHECK(parsed2.size()==1 && parsed2[0].text.find(L"直接编辑")!=wstring::npos, "直接编辑往返正确");

    // ---- 4) 纯文本按条文件读写 (当前存储结构) ----
    {
        wstring body = L"周五 · 纯文本日记\n\n这里没有 Markdown 语法，所见即所存。\n附件：说明.txt";
        CHECK(saveEntryFileBase(L"2026-09-26", L"23-10-00", body), "保存单条纯文本文件");
        wstring back;
        CHECK(readEntryFileBase(L"2026-09-26", L"23-10-00", back), "读取单条纯文本文件");
        CHECK(back == body, "纯文本逐字往返 (含空行/中文)");
        auto list = listDayEntries(L"2026-09-26");
        CHECK(list.size() == 1 && list[0].base == L"23-10-00", "按条列出当天日记");
        CHECK(deleteEntryFileBase(L"2026-09-26", L"23-10-00"), "删除单条纯文本文件");
    }

    // ---- 5) 待办: 作业单保存/读取 + 属性 + 旧 GBK 兼容 ----
    {
        createDirW(taskListDir());
        g_taskListName = L"unit_tasks";
        g_subjects.clear();
        Subject math; math.name = L"数学";
        Task t1; t1.text = L"极限练习"; t1.priority = 2; t1.due = L"2026-10-23"; t1.tag = L"复习";
        Task t2; t2.text = L"行列式"; t2.done = true;
        math.tasks = { t1, t2 };
        g_subjects.push_back(math);
        CHECK(saveTaskFile(L"unit_tasks"), "保存作业单");
        g_subjects.clear();
        CHECK(loadTaskFile(L"unit_tasks.txt"), "读取作业单");
        CHECK(g_subjects.size()==1 && g_subjects[0].name==L"数学", "科目名往返");
        CHECK(taskTotal()==2 && taskDone()==1, "任务数量/完成数往返");
        CHECK(!g_subjects.empty() && g_subjects[0].tasks[0].priority==2, "优先级属性往返");
        CHECK(!g_subjects.empty() && g_subjects[0].tasks[0].due==L"2026-10-23", "截止日期属性往返");
        CHECK(tasksDueOn(2026,10,23)==1, "当天到期待办统计");

        // 旧 StudyTodo 的 GBK 文本文件也应能读取
        wstring gbkPath = joinPathW(taskListDir(), L"legacy_gbk.txt");
        string gbk = wideToMb(wstring(L"# 英语\n- 背单词\t@@PRI=1@@\n"), 936);
        writeWholeFile(gbkPath, gbk);
        CHECK(loadTaskFile(L"legacy_gbk.txt"), "读取旧 GBK 作业单");
        CHECK(g_subjects.size()==1 && g_subjects[0].tasks[0].text==L"背单词", "GBK 中文解码正确");
    }

    printf("\n%s (%d 项失败)\n", fails==0 ? "全部通过" : "存在失败", fails);
    return fails;
}
