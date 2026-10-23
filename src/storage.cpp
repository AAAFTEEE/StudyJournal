// ============================================================
//  storage.cpp — DayJournal Markdown 存储层
//
//  目录结构 (g_journalRoot = 用户选择的日记本根目录):
//     entries/YYYY-MM-DD.md        每天一个 markdown 文件
//     attachments/YYYY-MM-DD/...   每天的附件
//     backup/YYYYMMDD/             每日整库备份 (滚动保留 7 份)
//     DayJournal.meta.json         元数据 (打开次数 / 版本)
//
//  每日 markdown 文件格式:
//     # 2026-09-26
//
//     ## 21:33:45
//     <该条日记的 markdown 正文>
//
//     ## 22:10:00
//     <第二条……>
//  一条日记 = 一个 "## HH:MM:SS" 小节 (保留原多条目/日模型)
// ============================================================
#include "app.h"

wstring g_journalRoot;
wstring g_exeDir;
wstring g_configFile = L"studyjournal_config.txt";
wstring g_metaFile = L"StudyJournal.meta.json";
wstring g_lastTaskName;
int32_t g_openCount = 0;
bool    g_hasDataDir = false;

// 农历显示开关 (设置 → 农历设置)
bool g_showLunar = true;
bool g_showFest  = true;
bool g_showHol   = true;

// 自定义农历节日 {农历月, 农历日} → 名称 (calendar.cpp 读取)
vector<pair<pair<int,int>,wstring>> g_customLunarFest;

// ============================================================
//  配置 (数据根目录 + 主题 + 显示开关 + 上次作业单 + 页面) — exe 同级
//  第 1 行: 数据根目录
//  第 2 行: 主题下标
//  第 3 行: '1'/'0' × 3 = 农历 / 节日 / 调休 显示开关
//  第 4 行: 上次打开的作业单名 (可空)
//  第 5 行: 上次停留的页面 (0 日记 / 1 待办)
// ============================================================
static void parseShowFlags(const wstring& f) {
    if (f.size() >= 1) g_showLunar = (f[0] == L'1');
    if (f.size() >= 2) g_showFest  = (f[1] == L'1');
    if (f.size() >= 3) g_showHol   = (f[2] == L'1');
}
static wstring trimConfigLine(const wstring& s) {
    wstring r = s;
    while (!r.empty() && (r.back()==L' '||r.back()==L'\t'||r.back()==L'\r')) r.pop_back();
    return r;
}
bool loadConfig(wstring& dir) {
    wstring cfg = g_exeDir.empty() ? g_configFile : joinPathW(g_exeDir, g_configFile);
    string b;
    if (!readWholeFile(cfg, b) || b.empty()) return false;
    wstring content = decodeText(b);
    vector<wstring> lines;
    size_t pos = 0;
    while (pos <= content.size()) {
        size_t eol = content.find(L'\n', pos);
        wstring line = (eol == wstring::npos) ? content.substr(pos)
                                              : content.substr(pos, eol - pos);
        lines.push_back(trimConfigLine(line));
        if (eol == wstring::npos) break;
        pos = eol + 1;
    }
    if (lines.empty()) return false;
    dir = lines[0];
    if (lines.size() > 1 && !lines[1].empty()) {
        try { g_themeIndex = stoi(lines[1]); } catch (...) { g_themeIndex = 0; }
        if (g_themeIndex < 0 || g_themeIndex >= 7) g_themeIndex = 0;
    }
    if (lines.size() > 2 && !lines[2].empty()) parseShowFlags(lines[2]);
    if (lines.size() > 3) g_lastTaskName = lines[3];
    if (lines.size() > 4) {
        try { g_activePage = stoi(lines[4]); } catch (...) { g_activePage = PAGE_JOURNAL; }
        if (g_activePage < 0 || g_activePage >= PAGE_COUNT) g_activePage = PAGE_JOURNAL;
    }
    return !dir.empty();
}
void saveConfig(const wstring& dir) {
    wstring cfg = g_exeDir.empty() ? g_configFile : joinPathW(g_exeDir, g_configFile);
    wchar_t flags[8];
    swprintf(flags, 8, L"%c%c%c", g_showLunar?L'1':L'0', g_showFest?L'1':L'0', g_showHol?L'1':L'0');
    wstring out = dir + L"\n" + to_wstring(g_themeIndex) + L"\n" + wstring(flags) + L"\n"
                + g_lastTaskName + L"\n" + to_wstring(g_activePage) + L"\n";
    writeWholeFile(cfg, toUTF8(out));
}
int savedThemeIndex() { return g_themeIndex; }
void saveThemeIndex(int idx) {
    if (idx < 0 || idx >= 7) idx = 0;
    g_themeIndex = idx;
    saveConfig(g_journalRoot);
}

// ============================================================
//  自定义农历节日 — lunar_fest.txt (根目录下)
//  每行: 农历月 \t 农历日 \t 名称   (如 "8\t15\t我的生日")
// ============================================================
void loadLunarFestCustom() {
    g_customLunarFest.clear();
    wstring path = joinPathW(g_journalRoot, L"lunar_fest.txt");
    string b;
    if (!readWholeFile(path, b)) return;
    wstring content = decodeText(b);
    size_t pos = 0;
    while (pos < content.size()) {
        size_t eol = content.find(L'\n', pos);
        wstring line = (eol == wstring::npos) ? content.substr(pos) : content.substr(pos, eol - pos);
        pos = (eol == wstring::npos) ? content.size() : eol + 1;
        while (!line.empty() && (line.back()==L'\r'||line.back()==L'\n')) line.pop_back();
        if (line.empty() || line[0] == L'#') continue;
        vector<wstring> f;
        size_t a = 0;
        while (a < line.size()) {
            size_t t = line.find(L'\t', a);
            if (t == wstring::npos) { f.push_back(line.substr(a)); break; }
            f.push_back(line.substr(a, t - a));
            a = t + 1;
        }
        if (f.size() < 3) continue;
        int mon = 0, day = 0;
        try { mon = stoi(f[0]); day = stoi(f[1]); } catch (...) { continue; }
        if (mon < 1 || mon > 12 || day < 1 || day > 30 || f[2].empty()) continue;
        g_customLunarFest.push_back({{mon, day}, f[2]});
    }
}
void saveLunarFestCustom() {
    wstring out = L"# DayJournal 自定义农历节日 (农历月\t农历日\t名称)\n";
    for (auto& cf : g_customLunarFest)
        out += to_wstring(cf.first.first) + L"\t" + to_wstring(cf.first.second) + L"\t" + cf.second + L"\n";
    writeWholeFile(joinPathW(g_journalRoot, L"lunar_fest.txt"), toUTF8(out));
}

// ============================================================
//  路径
// ============================================================
wstring dayFilePath(const wstring& dateStr) {
    return joinPathW(joinPathW(g_journalRoot, L"entries"), dateStr + L".md");
}
wstring attachmentsDirFor(const wstring& dateStr) {
    return joinPathW(joinPathW(g_journalRoot, L"attachments"), dateStr);
}
wstring attachmentPath(const wstring& dateStr, const wstring& fname) {
    return joinPathW(attachmentsDirFor(dateStr), fname);
}

static wstring trimW(const wstring& s);   // 前置声明 (供下方新存储 API 使用)

// ============================================================
//  新结构: 每条日记独立文件  entries/YYYY-MM-DD/HH-MM-SS.md
//  (同日多条不再挤在一个 YYYY-MM-DD.md 里, 每条一个文件)
// ============================================================
wstring dayDir(const wstring& dateStr) {
    return joinPathW(joinPathW(g_journalRoot, L"entries"), dateStr);
}

// 解析条目文件名 base (如 "21-33-45" 或 "21-33-45_2") → 时分秒+序号
static bool parseEntryBase(const wstring& base, int& h, int& m, int& s, int& suf) {
    wstring core = base;
    suf = 1;
    size_t us = core.find(L'_');
    if (us != wstring::npos) {
        try { suf = stoi(core.substr(us + 1)); } catch (...) { return false; }
        core = core.substr(0, us);
    }
    if (core.size() != 8 || core[2] != L'-' || core[5] != L'-') return false;
    try {
        h = stoi(core.substr(0, 2)); m = stoi(core.substr(3, 2)); s = stoi(core.substr(6, 2));
    } catch (...) { return false; }
    return h >= 0 && h <= 23 && m >= 0 && m <= 59 && s >= 0 && s <= 60 && suf >= 1;
}

wstring allocEntryBase(const wstring& dateStr, int h, int m, int s) {
    createDirW(dayDir(dateStr));
    wchar_t b[24];
    int n = 1;
    swprintf(b, 24, L"%02d-%02d-%02d", h, m, s);
    wstring base = b;
    while (fileExists(joinPathW(dayDir(dateStr), base + L".md"))) {
        n++;
        swprintf(b, 24, L"%02d-%02d-%02d_%d", h, m, s, n);
        base = b;
    }
    return base;
}

bool saveEntryFileBase(const wstring& dateStr, const wstring& base, const wstring& text) {
    createDirW(dayDir(dateStr));
    return writeWholeFile(joinPathW(dayDir(dateStr), base + L".md"), toUTF8(trimW(text)));
}

bool readEntryFileBase(const wstring& dateStr, const wstring& base, wstring& out) {
    string b;
    if (!readWholeFile(joinPathW(dayDir(dateStr), base + L".md"), b)) return false;
    out = decodeText(b);
    return true;
}

bool deleteEntryFileBase(const wstring& dateStr, const wstring& base) {
    wstring path = joinPathW(dayDir(dateStr), base + L".md");
    return DeleteFileW(path.c_str()) != 0 || GetLastError() == ERROR_FILE_NOT_FOUND;
}

// 读取一天的所有条目文件 (按时间+序号排序)
vector<DiaryEntry> listDayEntries(const wstring& dateStr) {
    vector<DiaryEntry> res;
    wstring dir = dayDir(dateStr);
    wstring pattern = joinPathW(dir, L"*.md");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return res;
    int y = 0, m = 0, d = 0;
    if (dateStr.size() >= 10) {
        try { y = stoi(dateStr.substr(0, 4)); m = stoi(dateStr.substr(5, 2)); d = stoi(dateStr.substr(8, 2)); }
        catch (...) {}
    }
    do {
        wstring fn = fd.cFileName;
        if (fn.size() < 4 || toLowerW(fn.substr(fn.size() - 3)) != L".md") continue;
        wstring base = fn.substr(0, fn.size() - 3);
        int hh, mm, ss, suf;
        if (!parseEntryBase(base, hh, mm, ss, suf)) continue;
        DiaryEntry e; e.open_cnt = 0; e.year = y; e.mon = m; e.day = d;
        e.hour = hh; e.min = mm; e.sec = ss; e.base = base;
        string b;
        if (readWholeFile(joinPathW(dir, fn), b)) e.text = trimW(decodeText(b));
        res.push_back(e);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    sort(res.begin(), res.end(), [](const DiaryEntry& a, const DiaryEntry& b) {
        if (a.hour != b.hour) return a.hour < b.hour;
        if (a.min  != b.min)  return a.min  < b.min;
        if (a.sec  != b.sec)  return a.sec  < b.sec;
        return a.base < b.base;
    });
    return res;
}

// ============================================================
//  文本工具
// ============================================================
static vector<wstring> splitLinesW(const wstring& s) {
    vector<wstring> lines;
    wstring cur;
    for (size_t i = 0; i < s.size(); i++) {
        wchar_t c = s[i];
        if (c == L'\n') { lines.push_back(cur); cur.clear(); }
        else if (c == L'\r') { /* skip */ }
        else cur += c;
    }
    lines.push_back(cur);
    return lines;
}
static wstring trimW(const wstring& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == L' ' || s[b] == L'\t' || s[b] == L'\n' || s[b] == L'\r')) b++;
    while (e > b && (s[e-1] == L' ' || s[e-1] == L'\t' || s[e-1] == L'\n' || s[e-1] == L'\r')) e--;
    return s.substr(b, e - b);
}
static bool startsWithW(const wstring& s, const wstring& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}
// "## HH:MM:SS" 或 "## HH:MM" 解析
static bool parseTimeHeading(const wstring& line, int& hh, int& mm, int& ss) {
    if (!startsWithW(line, L"## ")) return false;
    wstring t = trimW(line.substr(3));
    if (t.size() < 5) return false;
    for (int i = 0; i < (int)t.size(); i++) {
        if (t[i] == L':') t[i] = L' ';      // 容忍空格/冒号
        else if (t[i] == L'-' || t[i] == L'/') t[i] = L' ';
        else if (t[i] < L'0' || t[i] > L'9') return false;
    }
    int a=0,b=0,c=0, n = swscanf(t.c_str(), L"%d %d %d", &a, &b, &c);
    if (n < 2) return false;
    hh=a; mm=b; ss=(n>=3)?c:0;
    if (hh<0||hh>23||mm<0||mm>59||ss<0||ss>60) return false;
    return true;
}
static void trimTrailingBlank(vector<wstring>& lines) {
    while (!lines.empty() && trimW(lines.back()).empty()) lines.pop_back();
}

// ============================================================
//  解析一天的 markdown 文件 → 条目列表
// ============================================================
vector<DiaryEntry> parseDayMarkdown(const wstring& text, int year, int mon, int day) {
    vector<DiaryEntry> res;
    auto lines = splitLinesW(text);
    DiaryEntry cur;
    cur.open_cnt = 0; cur.year = year; cur.mon = mon; cur.day = day;
    cur.hour = cur.min = cur.sec = 0;
    bool haveContent = false;
    bool firstLine = true;

    auto flush = [&]() {
        if (haveContent) {
            cur.text = trimW(cur.text);
            if (!cur.text.empty()) res.push_back(cur);
        }
        cur.text.clear();
        haveContent = false;
        cur.hour = cur.min = cur.sec = 0;
    };

    for (auto& raw : lines) {
        wstring line = raw;
        if (firstLine) {
            // 跳过首行日期大标题 "# YYYY-MM-DD"
            if (startsWithW(line, L"# ") && !startsWithW(line, L"## ")) { firstLine = false; continue; }
            firstLine = false;
        }
        // 时间小节标题
        if (startsWithW(line, L"## ")) {
            int hh, mm, ss;
            if (parseTimeHeading(line, hh, mm, ss)) {
                flush();
                cur.hour = hh; cur.min = mm; cur.sec = ss;
                continue;
            }
            // "## " 但非时间 → 当作普通内容 (可能是用户自己的二级标题)
        }
        if (!trimW(line).empty()) haveContent = true;
        cur.text += line + L"\n";
    }
    flush();

    // 排序 (时间升序)
    sort(res.begin(), res.end(), [](const DiaryEntry& a, const DiaryEntry& b){
        if (a.hour != b.hour) return a.hour < b.hour;
        if (a.min  != b.min)  return a.min  < b.min;
        return a.sec < b.sec;
    });
    return res;
}

// 由条目列表构建一天的 markdown 文本
static wstring buildDayText(const wstring& dateStr, const vector<DiaryEntry>& entries) {
    wstring out = L"# " + dateStr + L"\n";
    for (auto& e : entries) {
        out += L"\n## " + e.timeStr() + L"\n";
        out += trimW(e.text) + L"\n";
    }
    return out;
}

// ============================================================
//  写入 / 重建内存
// ============================================================
bool writeDayFileText(const wstring& dateStr, const wstring& markdown) {
    wstring path = dayFilePath(dateStr);
    createDirW(joinPathW(g_journalRoot, L"entries"));
    // 原子写: 临时文件 + 替换
    string bytes = toUTF8(markdown);
    wstring tmp = path + L".tmp";
    if (!writeWholeFile(tmp, bytes)) return false;
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

bool saveDayEntries(const wstring& dateStr, const vector<DiaryEntry>& entries) {
    return writeDayFileText(dateStr, buildDayText(dateStr, entries));
}

// 重建 g_entries (新结构: 每个日期目录下一条一文件)
void refreshFromDisk(const wstring& onlyDate) {
    if (onlyDate.empty()) g_entries.clear();
    else {
        g_entries.erase(remove_if(g_entries.begin(), g_entries.end(),
            [&](const DiaryEntry& e){ return e.dateStr() == onlyDate; }), g_entries.end());
    }

    auto loadDay = [&](const wstring& dateStr) {
        auto ents = listDayEntries(dateStr);
        for (auto& e : ents) g_entries.push_back(e);
    };

    if (!onlyDate.empty()) { loadDay(onlyDate); }
    else {
        wstring dir = joinPathW(g_journalRoot, L"entries");
        wstring pattern = joinPathW(dir, L"*");
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    wstring dn = fd.cFileName;
                    if (dn.size() == 10 && dn[4] == L'-' && dn[7] == L'-') loadDay(dn);
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }

    sort(g_entries.begin(), g_entries.end(), [](const DiaryEntry& a, const DiaryEntry& b){
        wstring da = a.dateStr(), db = b.dateStr();
        if (da != db) return da < db;
        if (a.hour != b.hour) return a.hour < b.hour;
        if (a.min  != b.min)  return a.min  < b.min;
        if (a.sec  != b.sec)  return a.sec  < b.sec;
        return a.base < b.base;
    });
}

// ============================================================
//  元数据 (打开次数)
// ============================================================
void reloadMeta() {
    g_openCount = 0;
    string b;
    if (!readWholeFile(joinPathW(g_journalRoot, g_metaFile), b)) return;
    // 极简 JSON: {"app":"DayJournal","version":1,"open_count":N}
    wstring w = decodeText(b);
    size_t p = w.find(L"open_count");
    if (p == wstring::npos) return;
    p = w.find(L':', p);
    if (p == wstring::npos) return;
    while (p+1 < w.size() && (w[p+1] == L' ' || w[p+1] == L'\t')) p++;
    size_t e = p+1;
    while (e < w.size() && w[e] >= L'0' && w[e] <= L'9') e++;
    if (e == p+1) return;
    try { g_openCount = stoi(w.substr(p+1, e-(p+1))); } catch (...) {}
}
void saveMeta() {
    string s = "{\"app\":\"StudyJournal\",\"version\":1,\"open_count\":";
    s += to_string(g_openCount) + "}\n";
    writeWholeFile(joinPathW(g_journalRoot, g_metaFile), s);
}

// ============================================================
//  每日整库备份: entries → backup/YYYYMMDD, 滚动保留 7 份
// ============================================================
void makeDailyBackup() {
    wstring entriesDir = joinPathW(g_journalRoot, L"entries");
    if (!fileExists(entriesDir)) return;
    time_t now = time(nullptr); tm* lt = localtime(&now);
    wchar_t name[32];
    swprintf(name, 32, L"%04d%02d%02d", lt->tm_year+1900, lt->tm_mon+1, lt->tm_mday);
    wstring dstDir = joinPathW(joinPathW(g_journalRoot, L"backup"), wstring(name));
    if (fileExists(dstDir)) return;   // 今天已备份

    createDirW(joinPathW(g_journalRoot, L"backup"));
    createDirW(dstDir);
    // 整目录树复制 entries\* 与 tasks\* → backup\YYYYMMDD\ (SHFileOperation 需双空终止)
    auto copyTree = [&](const wstring& srcDir) {
        if (!fileExists(srcDir)) return;
        wstring srcSpec = srcDir + L"\\*";
        wstring dstSpec = dstDir + L"\\";
        srcSpec.push_back(0); srcSpec.push_back(0);
        dstSpec.push_back(0); dstSpec.push_back(0);
        SHFILEOPSTRUCTW so = {0};
        so.wFunc = FO_COPY;
        so.pFrom = srcSpec.c_str();
        so.pTo = dstSpec.c_str();
        so.fFlags = FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR | FOF_SILENT | FOF_NOERRORUI;
        SHFileOperationW(&so);
    };
    copyTree(entriesDir);
    copyTree(taskListDir());
    for (const wchar_t* f : { L"schedules.txt", L"lunar_fest.txt" }) {
        wstring src = joinPathW(g_journalRoot, f);
        if (fileExists(src)) CopyFileW(src.c_str(), joinPathW(dstDir, f).c_str(), FALSE);
    }

    // 滚动清理: 只保留最近 7 份
    vector<wstring> baks;
    wstring bPat = joinPathW(joinPathW(g_journalRoot, L"backup"), L"*");
    WIN32_FIND_DATAW bfd;
    HANDLE hb = FindFirstFileW(bPat.c_str(), &bfd);
    if (hb != INVALID_HANDLE_VALUE) {
        do {
            if (bfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) baks.push_back(bfd.cFileName);
        } while (FindNextFileW(hb, &bfd));
        FindClose(hb);
    }
    sort(baks.begin(), baks.end());
    while (baks.size() > 7) {
        wstring old = baks.front();
        wstring full = joinPathW(joinPathW(g_journalRoot, L"backup"), old);
        SHFILEOPSTRUCTW so2 = {0};
        wchar_t buf[MAX_PATH*2]; wcscpy(buf, full.c_str()); buf[full.size()] = 0; buf[full.size()+1] = 0;
        so2.wFunc = FO_DELETE;
        so2.pFrom = buf;
        so2.fFlags = FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
        SHFileOperationW(&so2);
        baks.erase(baks.begin());
    }
}

// ============================================================
//  旧数据迁移
// ============================================================
// 1) rili_data.dat (v2/v3/v4/v5 二进制)
static bool migrateBinaryDat(const wstring& datPath) {
    string b;
    if (!readWholeFile(datPath, b)) return false;
    size_t pos = 0;
    auto need = [&](size_t n){ return pos + n <= b.size(); };
    auto rd  = [&](void* p, size_t n){ memcpy(p, b.data()+pos, n); pos += n; };
    if (!need(4) || memcmp(b.data(), "RILI", 4) != 0) return false;
    pos += 4;
    uint16_t ver; int32_t oc, cnt;
    if (!need(sizeof(uint16_t)+sizeof(int32_t)*2)) return false;
    rd(&ver, 2); rd(&oc, 4); rd(&cnt, 4);
    if (cnt < 0 || cnt > 10000000) return false;
    vector<DiaryEntry> tmp;
    for (int32_t i = 0; i < cnt; i++) {
        DiaryEntry e; int16_t v;
        if (!need(sizeof(int32_t)+12+sizeof(int32_t))) return false;
        rd(&e.open_cnt, 4);
        rd(&v,2); e.year=v;
        rd(&v,2); e.mon=v;
        rd(&v,2); e.day=v;
        rd(&v,2); e.hour=v;
        rd(&v,2); e.min=v;
        rd(&v,2); e.sec=v;
        int32_t len; rd(&len, 4);
        if (len < 0 || len > 64*1024*1024 || !need((size_t)len)) return false;
        e.text = toWide(string(b.data()+pos, (size_t)len));
        pos += (size_t)len;
        tmp.push_back(e);
    }
    if (tmp.empty()) return false;
    // 按日分组写入独立条目文件
    map<wstring, vector<DiaryEntry>> byDay;
    for (auto& e : tmp) byDay[e.dateStr()].push_back(e);
    for (auto& kv : byDay) {
        createDirW(dayDir(kv.first));
        for (auto& e : kv.second) {
            wstring base = allocEntryBase(kv.first, e.hour, e.min, e.sec);
            saveEntryFileBase(kv.first, base, e.text);
        }
    }
    g_openCount = max<int32_t>(g_openCount, oc);
    // 隔离旧文件
    MoveFileExW(datPath.c_str(), (datPath + L".migrated").c_str(), 0);
    return true;
}

// 2) 更旧的 r.dat + s.dat
static bool migrateOldPair() {
    wstring cFile = joinPathW(g_journalRoot, L"r.dat");
    wstring rFile = joinPathW(g_journalRoot, L"s.dat");
    string cb;
    if (!readWholeFile(cFile, cb) || cb.size() < sizeof(int)) return false;
    int oc = 0; memcpy(&oc, cb.data(), sizeof(int));
    g_openCount = max<int32_t>(g_openCount, oc);
    string rb;
    if (!readWholeFile(rFile, rb)) return false;
    vector<DiaryEntry> tmp;
    size_t pos = 0;
    const size_t headLen = sizeof(int)*7 + sizeof(size_t);
    while (pos + headLen <= rb.size()) {
        int iv[7]; memcpy(iv, rb.data()+pos, sizeof(int)*7); pos += sizeof(int)*7;
        size_t len; memcpy(&len, rb.data()+pos, sizeof(size_t)); pos += sizeof(size_t);
        if (len > rb.size()-pos) break;
        DiaryEntry e;
        e.open_cnt=iv[0]; e.year=iv[1]; e.mon=iv[2]; e.day=iv[3];
        e.hour=iv[4]; e.min=iv[5]; e.sec=iv[6];
        e.text = decodeText(string(rb.data()+pos, len));
        pos += len;
        tmp.push_back(e);
    }
    if (tmp.empty()) return false;
    map<wstring, vector<DiaryEntry>> byDay;
    for (auto& e : tmp) byDay[e.dateStr()].push_back(e);
    for (auto& kv : byDay) {
        createDirW(dayDir(kv.first));
        for (auto& e : kv.second) {
            wstring base = allocEntryBase(kv.first, e.hour, e.min, e.sec);
            saveEntryFileBase(kv.first, base, e.text);
        }
    }
    MoveFileExW(cFile.c_str(), (cFile + L".migrated").c_str(), 0);
    MoveFileExW(rFile.c_str(), (rFile + L".migrated").c_str(), 0);
    return true;
}

static void migrateLegacy() {
    wstring entriesDir = joinPathW(g_journalRoot, L"entries");
    if (!fileExists(entriesDir)) return;

    // 1) 拆解旧顶层 YYYY-MM-DD.md (同日多条挤一个文件) → 每条独立文件
    wstring pattern = joinPathW(entriesDir, L"*.md");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    vector<wstring> legacy;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            wstring fn = fd.cFileName;
            if (fn.size() == 13 && toLowerW(fn.substr(fn.size()-3)) == L".md" &&
                fn[4] == L'-' && fn[7] == L'-') legacy.push_back(fn);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    for (auto& fn : legacy) {
        wstring dateStr = fn.substr(0, fn.size() - 3);
        string b;
        if (!readWholeFile(joinPathW(entriesDir, fn), b)) continue;
        int y = 0, m = 0, d = 0;
        try { y = stoi(dateStr.substr(0,4)); m = stoi(dateStr.substr(5,2)); d = stoi(dateStr.substr(8,2)); }
        catch (...) {}
        auto ents = parseDayMarkdown(decodeText(b), y, m, d);
        bool any = false;
        for (auto& e : ents) {
            wstring base = allocEntryBase(dateStr, e.hour, e.min, e.sec);
            if (saveEntryFileBase(dateStr, base, e.text)) any = true;
        }
        if (any)
            MoveFileExW(joinPathW(entriesDir, fn).c_str(),
                       (joinPathW(entriesDir, fn) + L".migrated").c_str(), 0);
    }

    // 2) 二进制旧数据 (rili_data.dat / r.dat+s.dat) — 仅当没有任何新条目目录时才迁移
    bool hasNew = false;
    wstring pat2 = joinPathW(entriesDir, L"*");
    WIN32_FIND_DATAW f2;
    HANDLE h2 = FindFirstFileW(pat2.c_str(), &f2);
    if (h2 != INVALID_HANDLE_VALUE) {
        do {
            if (f2.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { hasNew = true; break; }
        } while (FindNextFileW(h2, &f2));
        FindClose(h2);
    }
    if (hasNew) return;

    wstring dat = joinPathW(g_journalRoot, L"rili_data.dat");
    if (fileExists(dat)) { if (migrateBinaryDat(dat)) return; }
    if (migrateOldPair()) return;
}

// ============================================================
//  初始化
// ============================================================
bool initJournal() {
    g_exeDir = getExeDirW();
    // 从 DayJournal 升级: 读取旧配置 (仅当新配置不存在)
    wstring cfgNew = joinPathW(g_exeDir, g_configFile);
    wstring cfgOld = joinPathW(g_exeDir, L"dayjournal_config.txt");
    if (!fileExists(cfgNew) && fileExists(cfgOld)) CopyFileW(cfgOld.c_str(), cfgNew.c_str(), FALSE);

    if (!(loadConfig(g_journalRoot) && !g_journalRoot.empty())) {
        // 首次运行: 默认数据目录放在 exe 同级 data\, 开箱即用
        g_journalRoot = joinPathW(g_exeDir, L"data");
        if (!createDirW(g_journalRoot))
            g_journalRoot = getDefaultJournalDirW();
        g_hasDataDir = true;
        g_themeIndex = 0;
    }
    g_hasDataDir = true;

    if (createDirW(g_journalRoot)) {
        createDirW(joinPathW(g_journalRoot, L"entries"));
        createDirW(joinPathW(g_journalRoot, L"attachments"));
        createDirW(taskListDir());
        migrateLegacy();
        reloadMeta();
        loadLunarFestCustom();          // 自定义农历节日
        refreshFromDisk();
        g_openCount++;
        saveMeta();
        makeDailyBackup();
        saveConfig(g_journalRoot);      // 固化默认目录, 下次直接使用
    }

    time_t now = time(nullptr); tm* lt = localtime(&now);
    g_calYear = lt->tm_year + 1900;
    g_calMon = lt->tm_mon + 1;
    g_todayYear = g_calYear; g_todayMon = g_calMon; g_todayDay = lt->tm_mday;
    return true;
}

// ============================================================
//  数据目录切换
//  setJournalRoot: 切换到新目录前, 把旧目录的整个内容递归复制过去
//  (不删除旧目录; 空旧目录 / 无旧目录时直接新建)
// ============================================================
// 递归复制一个目录树 (src → dst), 返回是否成功
static bool copyDirTree(const wstring& src, const wstring& dst) {
    if (src.empty() || dst.empty() || src == dst) return true;
    if (!createDirW(dst)) return false;
    wstring pat = joinPathW(src, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return true;   // 旧目录不存在/为空, 视为成功
    bool ok = true;
    do {
        wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        wstring s = joinPathW(src, name), d = joinPathW(dst, name);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!copyDirTree(s, d)) ok = false;
        } else {
            if (!CopyFileW(s.c_str(), d.c_str(), TRUE)) ok = false;  // 已存在则不覆盖
        }
    } while (ok && FindNextFileW(h, &fd));
    FindClose(h);
    return ok;
}

bool setJournalRoot(const wstring& newDir) {
    if (newDir.empty()) return false;
    // 规范化: 去掉尾部反斜杠, 避免 joinPathW 拼接重复
    wstring dir = newDir;
    while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/')) dir.pop_back();
    if (dir.empty()) return false;

    // 复制旧目录全部内容 → 新目录 (若二者不同且旧目录已设置)
    if (g_hasDataDir && !g_journalRoot.empty() && g_journalRoot != dir) {
        copyDirTree(g_journalRoot, dir);
    }

    g_journalRoot = dir;
    g_hasDataDir = true;
    saveConfig(g_journalRoot);

    // 重新建立子目录并重载数据
    createDirW(g_journalRoot);
    createDirW(joinPathW(g_journalRoot, L"entries"));
    createDirW(joinPathW(g_journalRoot, L"attachments"));
    createDirW(taskListDir());
    reloadMeta();
    loadLunarFestCustom();
    refreshFromDisk();
    saveConfig(g_journalRoot);
    if (g_hWnd) InvalidateRect(g_hWnd, nullptr, FALSE);
    return true;
}
