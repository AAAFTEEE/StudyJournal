// ============================================================
//  calendar.cpp — 农历换算 / 节日 / 休假调休
//  公历→农历 (1900-2100 查表), 干支/生肖, 公历&农历节日, 2026 官方放假调休
// ============================================================
#include "app.h"
#include <wininet.h>
#pragma comment(lib, "wininet.lib")

// ============================================================
//  联网节假日同步 (timor.tech 公开 API)
//  后台线程按需拉取各年份的放假/调休 → 内存缓存 g_holidayByYear
//  holidayStatusOf() 优先查缓存; 未缓存年份自动触发异步拉取; 断网回退内置表
// ============================================================
static map<int, map<pair<int,int>,char>> g_holidayByYear; // 年 → {月,日} → 'H'/'W'
static set<int>    g_loadedYears;      // 已成功缓存的年份
static vector<int> g_pendingYears;     // 待拉取年份队列 (后台线程串行消费)
static bool  g_syncQuit = false;
static HANDLE g_syncThread = nullptr;
static CRITICAL_SECTION g_holidayLock;

// 请求一年放假数据 (blocking), 成功返回 {月,日}→H/W 映射, 失败返回空
static map<pair<int,int>,char> fetchHolidayYear(int year) {
    map<pair<int,int>,char> tmp;
    wchar_t url[160];
    swprintf(url, 160, L"https://timor.tech/api/holiday/year/%d", year);
    HINTERNET hInt = InternetOpenW(L"StudyJournal", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hInt) return tmp;
    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE;
    HINTERNET hUrl = InternetOpenUrlW(hInt, url, nullptr, 0, flags, 0);
    if (!hUrl) { InternetCloseHandle(hInt); return tmp; }
    string data;
    char buf[4096];
    DWORD rd = 0;
    while (InternetReadFile(hUrl, buf, sizeof(buf), &rd) && rd > 0) {
        data.append(buf, rd);
        if (data.size() > 4*1024*1024) break;
    }
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInt);
    if (data.empty()) return tmp;

    // 极简 JSON 解析: 收集 "MM-DD":{"holiday":true/false,...}
    size_t pos = 0;
    while (pos < data.size()) {
        size_t q = data.find('"', pos);
        if (q == string::npos) break;
        size_t q2 = data.find('"', q + 1);
        if (q2 == string::npos) break;
        string key = data.substr(q + 1, q2 - q - 1);
        if (key.size() == 5 && key[2] == '-') {
            int mo = atoi(key.substr(0,2).c_str()), dd = atoi(key.substr(3,2).c_str());
            // 找 "holiday": 后面是 true/false
            size_t hk = data.find("holiday", q2);
            if (hk != string::npos && hk < q2 + 80) {
                bool isHol = data.compare(hk + 8, 5, "true") == 0;
                if (mo >= 1 && mo <= 12 && dd >= 1 && dd <= 31)
                    tmp[{mo, dd}] = isHol ? 'H' : 'W';
            }
        }
        pos = q2 + 1;
    }
    return tmp;
}

// 后台线程: 循环消费待拉年份队列
static DWORD WINAPI HolidaySyncProc(LPVOID) {
    while (!g_syncQuit) {
        int year = 0;
        EnterCriticalSection(&g_holidayLock);
        if (!g_pendingYears.empty()) { year = g_pendingYears.front(); g_pendingYears.erase(g_pendingYears.begin()); }
        LeaveCriticalSection(&g_holidayLock);
        if (year == 0) break;                 // 队列空 → 退出
        auto m = fetchHolidayYear(year);
        EnterCriticalSection(&g_holidayLock);
        if (!m.empty()) {
            g_holidayByYear[year] = m;
            if (g_hWnd) PostMessageW(g_hWnd, WM_USER + 5, 0, 0);  // 通知主窗口重绘, 即时显色
        }
        g_loadedYears.insert(year);
        LeaveCriticalSection(&g_holidayLock);
    }
    return 0;
}

// 请求同步某一年 (异步; 已缓存/已入队则忽略)
void requestHolidayYear(int year) {
    EnterCriticalSection(&g_holidayLock);
    if (g_loadedYears.count(year)) { LeaveCriticalSection(&g_holidayLock); return; }
    for (int q : g_pendingYears) if (q == year) { LeaveCriticalSection(&g_holidayLock); return; }
    g_pendingYears.push_back(year);
    bool needThread = (g_syncThread == nullptr);
    if (g_syncThread && WaitForSingleObject(g_syncThread, 0) == WAIT_OBJECT_0) {
        CloseHandle(g_syncThread); g_syncThread = nullptr; needThread = true;
    }
    LeaveCriticalSection(&g_holidayLock);
    if (needThread && !g_syncQuit)
        g_syncThread = CreateThread(nullptr, 0, HolidaySyncProc, nullptr, 0, nullptr);
}

void startHolidaySync() {
    if (g_syncThread) return;
    InitializeCriticalSection(&g_holidayLock);
    g_syncQuit = false;
    time_t now = time(nullptr); tm* lt = localtime(&now);
    int cur = lt->tm_year + 1900;
    // 启动拉取 近 3 年± (当前/上一年/下一年)
    for (int y : { cur, cur - 1, cur + 1 }) requestHolidayYear(y);
}

void cancelHolidaySync() {
    g_syncQuit = true;
    if (g_syncThread) {
        // 线程内部可能阻塞在网络请求, 不强制等待; 仅标记退出
        CloseHandle(g_syncThread);
        g_syncThread = nullptr;
    }
    DeleteCriticalSection(&g_holidayLock);
}

// ---------------- 农历数据表 (1900-2050, 权威天文台数据) ----------------
// 每 2 字节表示一年: 低 12 位 = 1..12 月大小(1 大月30 / 0 小月29), 高 1 位(bit16) = 闰月大小, 低 4 位 = 闰月月份(0 无闰)
static const int gLunarInfo[] = {
0x04bd8,0x04ae0,0x0a570,0x054d5,0x0d260,0x0d950,0x16554,0x056a0,0x09ad0,0x055d2,//1900-1909
0x04ae0,0x0a5b6,0x0a4d0,0x0d250,0x1d255,0x0b540,0x0d6a0,0x0ada2,0x095b0,0x14977,//1910-1919
0x04970,0x0a4b0,0x0b4b5,0x06a50,0x06d40,0x1ab54,0x02b60,0x09570,0x052f2,0x04970,//1920-1929
0x06566,0x0d4a0,0x0ea50,0x06e95,0x05ad0,0x02b60,0x186e3,0x092e0,0x1c8d7,0x0c950,//1930-1939
0x0d4a0,0x1d8a6,0x0b550,0x056a0,0x1a5b4,0x025d0,0x092d0,0x0d2b2,0x0a950,0x0b557,//1940-1949
0x06ca0,0x0b550,0x15355,0x04da0,0x0a5b0,0x14573,0x052b0,0x0a9a8,0x0e950,0x06aa0,//1950-1959
0x0aea6,0x0ab50,0x04b60,0x0aae4,0x0a570,0x05260,0x0f263,0x0d950,0x05b57,0x056a0,//1960-1969
0x096d0,0x04dd5,0x04ad0,0x0a4d0,0x0d4d4,0x0d250,0x0d558,0x0b540,0x0b6a0,0x195a6,//1970-1979
0x095b0,0x049b0,0x0a974,0x0a4b0,0x0b27a,0x06a50,0x06d40,0x0af46,0x0ab60,0x09570,//1980-1989
0x04af5,0x04970,0x064b0,0x074a3,0x0ea50,0x06b58,0x055c0,0x0ab60,0x096d5,0x092e0,//1990-1999
0x0c960,0x0d954,0x0d4a0,0x0da50,0x07552,0x056a0,0x0abb7,0x025d0,0x092d0,0x0cab5,//2000-2009
0x0a950,0x0b4a0,0x0baa4,0x0ad50,0x055d9,0x04ba0,0x0a5b0,0x15176,0x052b0,0x0a930,//2010-2019
0x07954,0x06aa0,0x0ad50,0x05b52,0x04b60,0x0a6e6,0x0a4e0,0x0d260,0x0ea65,0x0d530,//2020-2029
0x05aa0,0x076a3,0x096d0,0x04bd7,0x04ad0,0x0a4d0,0x1d0b6,0x0d250,0x0d520,0x0dd45,//2030-2039
0x0b5a0,0x056d0,0x055b2,0x049b0,0x0a577,0x0a4b0,0x0aa50,0x1b255,0x06d20,0x0ada0,//2040-2049
0x14b63                                                                              //2050
};
static int gLunarLeap(int y)  { if (y < 1900 || y > 2050) y = 1900; return gLunarInfo[y - 1900] & 0xf; }
static int gLunarMDays(int y, int m) { if (y < 1900 || y > 2050) y = 1900; return (gLunarInfo[y - 1900] & (0x10000 >> m)) ? 30 : 29; }
static int gLunarLeapDays(int y) {
    int lm = gLunarLeap(y);
    return lm ? ((gLunarInfo[y - 1900] & 0x10000) ? 30 : 29) : 0;   // bit16 = 闰月大小
}
static int gLunarYearDays(int y) {
    int sum = 348;                       // 12 * 29
    for (int m = 0x8000; m > 0x8; m >>= 1) if (gLunarInfo[y - 1900] & m) sum++;
    return sum + gLunarLeapDays(y);      // + 闰月天数
}

// 民用日历 → 天数 (相对 1970-01-01), Howard Hinnant 算法
static long daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}
// 公历 → 天数 (对外)
long civilDays(int y, int m, int d) { return daysFromCivil(y, m, d); }
// 天数 → 公历 (对外)
void civilFromDays(long z, int& y, int& m, int& d) {
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = (int)yoe + (int)era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1);
    m = (int)(mp + (mp < 10 ? 3 : -9));
    if (m <= 2) y++;
}
static long daysBetween(int y1, int m1, int d1, int y2, int m2, int d2) {
    return daysFromCivil(y1, m1, d1) - daysFromCivil(y2, m2, d2);
}

// 公历 → 农历
LunarDate solarToLunar(int y, int m, int d) {
    LunarDate r; r.year = 1900; r.month = 1; r.day = 1; r.isLeap = false;
    long offset = daysBetween(y, m, d, 1900, 1, 31);   // 1900-01-31 = 农历1900-1-1
    if (offset < 0) return r;
    // 防越界: 农历表只覆盖 1900-2050, 若目标日期晚于表末, 把 offset 夹到表内最后一天,
    // 避免 r.month / r.day 越界导致崩溃 (跳转到极远年份会触发)
    long maxOffset = 0;
    for (int k = 1900; k <= 2050; k++) maxOffset += gLunarYearDays(k);
    if (offset >= maxOffset) offset = maxOffset - 1;

    int i = 1900;
    while (i < 2051 && offset >= gLunarYearDays(i)) { offset -= gLunarYearDays(i); i++; }
    r.year = i;
    int leap = gLunarLeap(i);
    bool leapIter = false;      // 当前迭代是否正落在闰月
    int mm = 1, temp = 0;
    for (; mm <= 12; mm++) {
        if (leap > 0 && mm == leap + 1 && !leapIter) { leapIter = true; mm = mm - 1; temp = gLunarLeapDays(i); }
        else { leapIter = false; temp = gLunarMDays(i, mm); }
        if (offset < temp) break;
        offset -= temp;
    }
    if (mm > 12) { mm = 12; leapIter = false; }      // 兜底, 绝不越界
    r.month = mm;
    r.day = (int)offset + 1;
    if (r.day > 30) r.day = 30;                       // 兜底
    r.isLeap = leapIter;
    return r;
}

// ---------------- 农历文字 / 干支 / 生肖 ----------------
static const wchar_t* L_MON[]  = {L"", L"正月",L"二月",L"三月",L"四月",L"五月",L"六月",
                                  L"七月",L"八月",L"九月",L"十月",L"冬月",L"腊月"};
static const wchar_t* L_DAY[]  = {L"", L"初一",L"初二",L"初三",L"初四",L"初五",L"初六",L"初七",L"初八",L"初九",L"初十",
                                  L"十一",L"十二",L"十三",L"十四",L"十五",L"十六",L"十七",L"十八",L"十九",L"二十",
                                  L"廿一",L"廿二",L"廿三",L"廿四",L"廿五",L"廿六",L"廿七",L"廿八",L"廿九",L"三十"};
static const wchar_t* L_GAN[]  = {L"甲",L"乙",L"丙",L"丁",L"戊",L"己",L"庚",L"辛",L"壬",L"癸"};
static const wchar_t* L_ZHI[]  = {L"子",L"丑",L"寅",L"卯",L"辰",L"巳",L"午",L"未",L"申",L"酉",L"戌",L"亥"};
static const wchar_t* L_ZOO[]  = {L"鼠",L"牛",L"虎",L"兔",L"龙",L"蛇",L"马",L"羊",L"猴",L"鸡",L"狗",L"猪"};

wstring ganzhiOfYear(int y) { int k = (y - 4) % 60; if (k < 0) k += 60; return wstring(L_GAN[k % 10]) + L_ZHI[k % 12]; }
wstring zodiacOf(int y)     { int k = (y - 4) % 12; if (k < 0) k += 12; return L_ZOO[k]; }

// 农历某年某月最后一天
static int gLunarLastDay(int y, int m) { return gLunarMDays(y, m); }

// 农历文字: 初一显示月, 其余显示"八月十五"式; 初一/月首用"X月"
wstring lunarDisplay(int y, int m, int d) {
    LunarDate l = solarToLunar(y, m, d);
    if (l.day == 1) return wstring(l.isLeap ? L"闰" : L"") + L_MON[l.month];
    return wstring(l.isLeap ? L"闰" : L"") + L_MON[l.month] + L_DAY[l.day];
}

// 农历月日读法 (供日程/编辑界面), 无效范围返回空
wstring lunarMonthDayName(int mon, int day) {
    if (mon < 1 || mon > 12 || day < 1 || day > 30) return L"";
    return wstring(L_MON[mon]) + L_DAY[day];
}

// ---------------- 节日 ----------------
static const int SOLAR_FEST[][3] = {   // {月, 日, 节日名长度后面用索引} -> 用二维字符串表
    {1,1,0}, {2,14,1}, {3,8,2}, {3,12,3}, {4,1,4}, {5,1,5}, {5,4,6}, {6,1,7},
    {7,1,8}, {8,1,9}, {9,10,10}, {10,1,11}, {12,25,12}
};
static const wchar_t* SOLAR_NAME[] = {
    L"元旦",L"情人节",L"妇女节",L"植树节",L"愚人节",L"劳动节",L"青年节",L"儿童节",
    L"建党节",L"建军节",L"教师节",L"国庆节",L"圣诞节"
};
static const int LUNAR_FEST[][2] = { {1,1},{1,15},{2,2},{5,5},{7,7},{7,15},{8,15},{9,9},{12,8},{12,23} };
static const wchar_t* LUNAR_NAME[] = {
    L"春节",L"元宵节",L"龙抬头",L"端午节",L"七夕",L"中元节",L"中秋节",L"重阳节",L"腊八节",L"小年"
};

wstring festivalsOf(int y, int m, int d) {
    wstring res;
    for (int i = 0; i < (int)(sizeof(SOLAR_FEST)/sizeof(SOLAR_FEST[0])); i++)
        if (SOLAR_FEST[i][0] == m && SOLAR_FEST[i][1] == d)
            { if (!res.empty()) res += L"·"; res += SOLAR_NAME[SOLAR_FEST[i][2]]; }
    LunarDate l = solarToLunar(y, m, d);
    for (int i = 0; i < (int)(sizeof(LUNAR_FEST)/sizeof(LUNAR_FEST[0])); i++)
        if (l.month == LUNAR_FEST[i][0] && l.day == LUNAR_FEST[i][1])
            { if (!res.empty()) res += L"·"; res += LUNAR_NAME[i]; }
    // 除夕: 腊月最后一天
    if (l.month == 12 && l.day == gLunarLastDay(l.year, 12))
        { if (!res.empty()) res += L"·"; res += L"除夕"; }
    // 用户自定义农历节日 (农历某月某日 → 名称)
    for (auto& cf : g_customLunarFest)
        if (cf.first.first == l.month && cf.first.second == l.day) {
            if (!res.empty()) res += L"·"; res += cf.second;
        }
    return res;
}

// ---------------- 休假 / 调休 (2026 国务院安排) ----------------
// {起始月, 起始日, 结束月, 结束日, 类型}  H=放假 W=补班
static const int H2026[][5] = {
    {1,1,1,3,0},   // 元旦 H
    {2,15,2,23,0}, // 春节 H
    {4,4,4,6,0},   // 清明 H
    {5,1,5,5,0},   // 劳动节 H
    {6,19,6,21,0}, // 端午 H
    {9,25,9,27,0}, // 中秋 H
    {10,1,10,7,0}, // 国庆 H
    {1,4,1,4,1},   // 补班
    {2,14,2,14,1}, {2,28,2,28,1},
    {5,9,5,9,1},
    {9,20,9,20,1}, {10,10,10,10,1}
};
char holidayStatusOf(int y, int m, int d) {
    // 联网缓存的该年份数据优先 (覆盖任意年份)
    EnterCriticalSection(&g_holidayLock);
    auto it = g_holidayByYear.find(y);
    bool hasYear = (it != g_holidayByYear.end());
    char c = 0;
    if (hasYear) {
        auto it2 = it->second.find({m, d});
        if (it2 != it->second.end()) c = it2->second;
    }
    LeaveCriticalSection(&g_holidayLock);
    if (c) return c;
    // 该年份未缓存 → 后台异步拉取 (不阻塞), 本次先按内置表/普通处理
    if (!hasYear) requestHolidayYear(y);
    if (y != 2026) return 0;   // 内置表仅 2026; 其他年份等联网数据
    for (auto& r : H2026) {
        bool inRange = false;
        if (r[0] == r[2] && r[1] == r[3]) inRange = (m == r[0] && d == r[1]);
        else {
            int start = r[0] * 100 + r[1], end = r[2] * 100 + r[3], cur = m * 100 + d;
            inRange = (cur >= start && cur <= end);
        }
        if (inRange) return r[4] == 0 ? 'H' : 'W';
    }
    return 0;
}
