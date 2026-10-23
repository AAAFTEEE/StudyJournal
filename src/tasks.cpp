// ============================================================
//  tasks.cpp — 待办 / 作业数据层
//  从 StudyTodo 迁移: 作业单文本格式、科目分组、优先级 / 截止 / 标签，
//  数据统一存到 <数据根>/tasks/*.txt (UTF-8，自动兼容旧的 GBK 文件)。
// ============================================================
#include "app.h"

vector<Subject> g_subjects;
wstring g_taskListName;

// ---------------- 小工具 ----------------
static wstring trimTaskW(const wstring& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == L' ' || s[b] == L'\t' || s[b] == L'\r' || s[b] == L'\n')) b++;
    while (e > b && (s[e-1] == L' ' || s[e-1] == L'\t' || s[e-1] == L'\r' || s[e-1] == L'\n')) e--;
    return s.substr(b, e - b);
}

static wstring safeFileStem(const wstring& name) {
    wstring s = trimTaskW(name);
    for (wchar_t& c : s) {
        if (c == L'/' || c == L'\\' || c == L':' || c == L'*' || c == L'?' ||
            c == L'"' || c == L'<' || c == L'>' || c == L'|') c = L'_';
    }
    if (s.empty()) s = L"未命名作业单";
    return s;
}

static wstring stripTxt(const wstring& fn) {
    if (fn.size() >= 4 && toLowerW(fn.substr(fn.size() - 4)) == L".txt")
        return fn.substr(0, fn.size() - 4);
    return fn;
}

static bool parseTaskAttrs(Task& t, wstring rest) {
    rest = trimTaskW(rest);
    size_t pos = 0;
    while ((pos = rest.find(L"@@", pos)) != wstring::npos) {
        size_t eq = rest.find(L'=', pos + 2);
        size_t end = rest.find(L"@@", eq == wstring::npos ? pos + 2 : eq + 1);
        if (eq == wstring::npos || end == wstring::npos) break;
        wstring key = rest.substr(pos + 2, eq - pos - 2);
        wstring val = rest.substr(eq + 1, end - eq - 1);
        if (key == L"PRI") t.priority = _wtoi(val.c_str());
        else if (key == L"DUE") t.due = trimTaskW(val);
        else if (key == L"TAG") t.tag = trimTaskW(val);
        pos = end + 2;
    }
    if (t.priority < 0 || t.priority > 2) t.priority = 0;
    return true;
}

// ---------------- 路径 / 文件 ----------------
wstring taskListDir() {
    return joinPathW(g_journalRoot, L"tasks");
}

vector<wstring> listTaskFiles() {
    vector<wstring> files;
    wstring pattern = joinPathW(taskListDir(), L"*.txt");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return files;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            files.push_back(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    sort(files.begin(), files.end());
    return files;
}

bool loadTaskFile(const wstring& fileName) {
    wstring fn = fileName;
    if (toLowerW(fn.substr(fn.size() >= 4 ? fn.size() - 4 : 0)) != L".txt") fn += L".txt";
    wstring path = joinPathW(taskListDir(), fn);
    string bytes;
    if (!readWholeFile(path, bytes)) return false;

    wstring content = decodeText(bytes);
    vector<Subject> parsed;
    Subject* cur = nullptr;
    size_t pos = 0;
    while (pos <= content.size()) {
        size_t eol = content.find(L'\n', pos);
        wstring line = (eol == wstring::npos) ? content.substr(pos)
                                              : content.substr(pos, eol - pos);
        pos = (eol == wstring::npos) ? content.size() + 1 : eol + 1;
        line = trimTaskW(line);
        if (line.empty()) continue;
        if (line[0] == L'#') {
            parsed.push_back(Subject{trimTaskW(line.substr(1)), {}});
            cur = &parsed.back();
        } else if (line[0] == L'-' || line[0] == L'+') {
            if (!cur) { parsed.push_back(Subject{L"未分类", {}}); cur = &parsed.back(); }
            wstring body = line.substr(1);
            wstring attrs;
            size_t tab = body.find(L'\t');
            if (tab != wstring::npos) { attrs = body.substr(tab + 1); body = body.substr(0, tab); }
            body = trimTaskW(body);
            if (body.empty()) continue;
            Task t;
            t.text = body;
            t.done = (line[0] == L'+');
            parseTaskAttrs(t, attrs);
            cur->tasks.push_back(t);
        }
    }
    if (parsed.empty()) return false;
    g_subjects.swap(parsed);
    g_taskListName = stripTxt(fn);
    return true;
}

bool saveTaskFile(const wstring& name) {
    wstring stem = safeFileStem(stripTxt(name));
    if (!createDirW(taskListDir())) return false;
    wstring out;
    for (auto& s : g_subjects) {
        out += L"# " + s.name + L"\n";
        for (auto& t : s.tasks) {
            out += (t.done ? L"+ " : L"- ") + t.text;
            if (t.priority != 0) out += L"\t@@PRI=" + to_wstring(t.priority) + L"@@";
            if (!t.due.empty())  out += L"\t@@DUE=" + t.due + L"@@";
            if (!t.tag.empty())  out += L"\t@@TAG=" + t.tag + L"@@";
            out += L"\n";
        }
    }
    if (out.empty()) out = L"# 学习\n";
    if (!writeWholeFile(joinPathW(taskListDir(), stem + L".txt"), toUTF8(out))) return false;
    bool renamed = (g_taskListName != stem);
    g_taskListName = stem;
    g_lastTaskName = stem;
    if (renamed) saveConfig(g_journalRoot);
    return true;
}

void autoSaveTasks() {
    if (!g_taskListName.empty()) saveTaskFile(g_taskListName);
}

bool ensureTaskList() {
    if (!listTaskFiles().empty()) return true;
    g_subjects.clear();
    g_subjects.push_back(Subject{L"学习", {}});
    return saveTaskFile(L"我的作业");
}

void importLegacyHomework(const wstring& sourceDir) {
    if (!fileExists(sourceDir)) return;
    if (!listTaskFiles().empty()) return;   // 已有新数据 → 不覆盖
    createDirW(taskListDir());
    wstring pattern = joinPathW(sourceDir, L"*.txt");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        CopyFileW(joinPathW(sourceDir, fd.cFileName).c_str(),
                  joinPathW(taskListDir(), fd.cFileName).c_str(), TRUE);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

void loadStartupTaskList() {
    auto files = listTaskFiles();
    bool ok = false;
    if (!g_lastTaskName.empty() && fileExists(joinPathW(taskListDir(), g_lastTaskName + L".txt")))
        ok = loadTaskFile(g_lastTaskName);
    if (!ok && !files.empty()) ok = loadTaskFile(files.back());
    if (!ok) {
        if (!ensureTaskList()) {
            g_subjects.clear();
            g_taskListName.clear();
        }
    }
    if (!g_taskListName.empty()) g_lastTaskName = g_taskListName;
}

// ---------------- 统计 ----------------
int taskTotal() {
    int n = 0;
    for (auto& s : g_subjects) n += (int)s.tasks.size();
    return n;
}
int taskDone() {
    int n = 0;
    for (auto& s : g_subjects) for (auto& t : s.tasks) if (t.done) ++n;
    return n;
}

int tasksDueOn(int y, int m, int d) {
    wchar_t buf[16];
    swprintf(buf, 16, L"%04d-%02d-%02d", y, m, d);
    wstring ds(buf);
    int n = 0;
    for (auto& s : g_subjects)
        for (auto& t : s.tasks)
            if (!t.done && t.due == ds) ++n;
    return n;
}

int tasksDueToday() {
    time_t now = time(nullptr); tm* lt = localtime(&now);
    return tasksDueOn(lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
}

int journalEntriesToday() {
    wchar_t buf[16];
    swprintf(buf, 16, L"%04d-%02d-%02d", g_todayYear, g_todayMon, g_todayDay);
    wstring ds(buf);
    int n = 0;
    for (auto& e : g_entries) if (e.dateStr() == ds) ++n;
    return n;
}

wstring taskListDisplayName() {
    return g_taskListName.empty() ? L"未命名作业单" : g_taskListName;
}

wstring dueDisplayText(const Task& t) {
    if (t.due.empty()) return L"";
    wstring s = t.due;
    time_t now = time(nullptr); tm* lt = localtime(&now);
    wchar_t today[16];
    swprintf(today, 16, L"%04d-%02d-%02d", lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
    if (t.due < today) s += L"  超期";
    else if (t.due == today) s += L"  今天";
    return s;
}
