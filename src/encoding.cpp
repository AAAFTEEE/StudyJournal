// ============================================================
//  encoding.cpp — 编码转换 / Unicode 文件读写 / 路径工具
//  (沿用 rili v4/v5 的统一多字节<->宽字符层, 内部恒为 UTF-8)
// ============================================================
#include "app.h"

wstring mbToWide(const string& s, UINT cp) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(cp, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return L"";
    wstring r(n, 0);
    MultiByteToWideChar(cp, 0, s.c_str(), (int)s.size(), &r[0], n);
    return r;
}
string wideToMb(const wstring& w, UINT cp) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(cp, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    string r(n, 0);
    WideCharToMultiByte(cp, 0, w.c_str(), (int)w.size(), &r[0], n, nullptr, nullptr);
    return r;
}
wstring toWide(const string& s) { return mbToWide(s, CP_UTF8); }
string  toUTF8(const wstring& w) { return wideToMb(w, CP_UTF8); }

wstring toLowerW(const wstring& s) {
    wstring r = s;
    for (auto& c : r) c = (wchar_t)towlower(c);
    return r;
}

bool isValidUTF8(const string& s) {
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { i++; continue; }
        int extra; uint32_t cp, minCp;
        if ((c & 0xE0) == 0xC0)      { extra = 1; cp = c & 0x1F; minCp = 0x80; }
        else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0F; minCp = 0x800; }
        else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07; minCp = 0x10000; }
        else return false;
        if (i + (size_t)extra >= n) return false;
        for (int k = 1; k <= extra; k++) {
            unsigned char cc = (unsigned char)s[i + k];
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (cp < minCp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        i += (size_t)extra + 1;
    }
    return true;
}

// 自动识别文本编码: UTF-8 BOM / 合法 UTF-8 / 否则按 GBK(936)
wstring decodeText(const string& b) {
    if (b.size() >= 3 &&
        (unsigned char)b[0] == 0xEF && (unsigned char)b[1] == 0xBB && (unsigned char)b[2] == 0xBF)
        return mbToWide(b.substr(3), CP_UTF8);
    if (isValidUTF8(b)) return mbToWide(b, CP_UTF8);
    return mbToWide(b, 936);
}

UINT exportCodePage() { return g_systemIsGBK ? 936 : CP_UTF8; }

// ============================================================
//  路径工具 (Unicode)
// ============================================================
wstring getExeDirW() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    wstring path(buf);
    size_t pos = path.find_last_of(L"\\/");
    return (pos != wstring::npos) ? path.substr(0, pos) : L".";
}
wstring joinPathW(const wstring& base, const wstring& name) {
    if (base.empty()) return name;
    wchar_t last = base.back();
    return (last == L'\\' || last == L'/') ? base + name : base + L"\\" + name;
}
bool createDirW(const wstring& dir) {
    if (CreateDirectoryW(dir.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}
bool fileExists(const wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}
wstring getHomeDirW() {
    wchar_t buf[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, 0, buf)))
        return wstring(buf);
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return wstring(buf, n);
    return L"C:\\Users\\Default";
}
wstring getDefaultJournalDirW() {
    // 兜底: exe 目录不可写时, 使用用户目录下的 StudyJournal
    return joinPathW(getHomeDirW(), L"StudyJournal");
}

// ============================================================
//  文件读写 (基于 CreateFileW, 与代码页无关)
// ============================================================
bool readWholeFile(const wstring& path, string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz)) { CloseHandle(h); return false; }
    out.resize((size_t)sz.QuadPart);
    size_t off = 0;
    while (off < out.size()) {
        size_t remain = out.size() - off;
        DWORD chunk = remain > 0x40000000 ? 0x40000000 : (DWORD)remain;
        DWORD got = 0;
        if (!ReadFile(h, &out[off], chunk, &got, nullptr) || got == 0) break;
        off += got;
    }
    CloseHandle(h);
    out.resize(off);
    return true;
}
bool writeWholeFile(const wstring& path, const string& bytes) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0,
                           nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    size_t off = 0;
    bool ok = true;
    while (off < bytes.size()) {
        size_t remain = bytes.size() - off;
        DWORD chunk = remain > 0x40000000 ? 0x40000000 : (DWORD)remain;
        DWORD written = 0;
        if (!WriteFile(h, bytes.data() + off, chunk, &written, nullptr) || written == 0) {
            ok = false; break;
        }
        off += written;
    }
    FlushFileBuffers(h);
    CloseHandle(h);
    return ok;
}

// 复制单个文件 (UTF-8/宽字符路径)
bool copyFileTo(const wstring& src, const wstring& dst) {
    return CopyFileW(src.c_str(), dst.c_str(), FALSE) != FALSE;
}
