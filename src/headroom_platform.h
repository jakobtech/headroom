// Headroom, standalone platform layer.
//
// Replaces the small part of the Windhawk API that the mixer uses (logging,
// stored values, settings) so the same source builds as a normal program.
// Values and settings live in one file: %APPDATA%\Headroom\settings.cfg
// Log: %LOCALAPPDATA%\Headroom\log.txt
//
// Copyright (c) Jakob HÃ¼ttner. Portions derived from the Windhawk mod
// "Better Volume Mixer" by 0Allu (MIT), see LICENSE.
#pragma once

// The mixer's messages use Windows printf rules (%s is a wide string), on every toolchain.
#ifndef __USE_MINGW_ANSI_STDIO
#define __USE_MINGW_ANSI_STDIO 0
#endif

#include <windows.h>
#include <shlobj.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <memory>
#include <string>
#include <unordered_map>

#define WH_MOD_ID L"headroom"
#define WH_MOD_VERSION L"2.1.0-alpha1"
#define HEADROOM_VERSION_TEXT L"2.1.0-alpha4"

namespace headroom {

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------
inline std::wstring EnvDir(PCWSTR variable) {
    WCHAR buffer[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(variable, buffer, ARRAYSIZE(buffer));
    std::wstring dir = (n > 0 && n < ARRAYSIZE(buffer)) ? buffer : L".";
    // The folder of earlier versions moves along.
    std::wstring old = dir + L"\\BetterVolumeMixer";
    dir += L"\\Headroom";
    if (GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES) MoveFileW(old.c_str(), dir.c_str());
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}
inline const std::wstring& DataDir() {  // settings
    static std::wstring dir = EnvDir(L"APPDATA");
    return dir;
}
inline const std::wstring& LogDir() {
    static std::wstring dir = EnvDir(L"LOCALAPPDATA");
    return dir;
}

// ---------------------------------------------------------------------------
// Log
// ---------------------------------------------------------------------------
struct LogState {
    CRITICAL_SECTION lock;
    HANDLE file = INVALID_HANDLE_VALUE;
    unsigned written = 0;
    bool ready = false;
};
inline LogState& Log() {
    static LogState state;
    return state;
}

inline std::string ToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n > 0 ? n : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), out.data(), n, nullptr, nullptr);
    return out;
}
inline std::wstring FromUtf8(const char* data, size_t size) {
    if (!size) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, data, (int)size, nullptr, 0);
    std::wstring out(n > 0 ? n : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, data, (int)size, out.data(), n);
    return out;
}

inline void InitLog() {
    LogState& s = Log();
    if (s.ready) return;
    InitializeCriticalSection(&s.lock);
    std::wstring path = LogDir() + L"\\log.txt";
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) &&
        info.nFileSizeHigh == 0 && info.nFileSizeLow > 1024 * 1024) {
        MoveFileExW(path.c_str(), (LogDir() + L"\\log.old.txt").c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    s.file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    s.ready = true;
}

// printf with Windows semantics: %s is a wide string, %S a narrow one. This is
// what the Windhawk logger does, and the mixer's messages rely on it.
inline void LogV(PCWSTR format, va_list args) {
    LogState& s = Log();
    if (!s.ready) return;
    WCHAR text[1536];
    int n = _vsnwprintf(text, ARRAYSIZE(text) - 1, format, args);
    if (n < 0) n = ARRAYSIZE(text) - 1;
    text[n] = 0;
    SYSTEMTIME t;
    GetLocalTime(&t);
    WCHAR line[1700];
    int m = swprintf(line, ARRAYSIZE(line), L"%02d:%02d:%02d.%03d %ls\r\n", t.wHour, t.wMinute,
                     t.wSecond, t.wMilliseconds, text);
    if (m <= 0) return;
    std::string utf8 = ToUtf8(std::wstring(line, (size_t)m));
    EnterCriticalSection(&s.lock);
    if (s.file != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(s.file, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
    }
    LeaveCriticalSection(&s.lock);
    OutputDebugStringW(line);
}
inline void LogF(PCWSTR format, ...) {
    va_list args;
    va_start(args, format);
    LogV(format, args);
    va_end(args);
}

// ---------------------------------------------------------------------------
// Store: one file, "key<TAB>value" per line, UTF-8.
//   "s:<name>"  settings (what the settings window edits)
//   "v:<name>"  values the mixer keeps (EQ curves, names, hotkeys, ...)
// ---------------------------------------------------------------------------
struct StoreState {
    CRITICAL_SECTION lock;
    std::unordered_map<std::wstring, std::wstring> map;
    bool dirty = false;
    bool ready = false;
    bool existedAtStart = false;
    HANDLE wake = nullptr;
    HANDLE thread = nullptr;
    volatile LONG stop = 0;
};
inline StoreState& Store() {
    static StoreState state;
    return state;
}

inline std::wstring StorePath() { return DataDir() + L"\\settings.cfg"; }

inline std::string Escape(const std::wstring& text) {
    std::string in = ToUtf8(text), out;
    for (char c : in) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\t': out += "\\t"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default: out += c;
        }
    }
    return out;
}
inline std::wstring Unescape(const char* data, size_t size) {
    std::string out;
    for (size_t i = 0; i < size; ++i) {
        if (data[i] == '\\' && i + 1 < size) {
            char c = data[++i];
            out += c == 't' ? '\t' : c == 'n' ? '\n' : c == 'r' ? '\r' : c;
        } else {
            out += data[i];
        }
    }
    return FromUtf8(out.data(), out.size());
}

inline void WriteStoreLocked() {
    StoreState& s = Store();
    std::string text;
    for (const auto& [key, value] : s.map) {
        text += Escape(key);
        text += '\t';
        text += Escape(value);
        text += "\n";
    }
    std::wstring path = StorePath(), temp = path + L".tmp";
    HANDLE f = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        LogF(L"Store: cannot write %ls, error %lu", temp.c_str(), GetLastError());
        return;
    }
    DWORD written = 0;
    BOOL ok = WriteFile(f, text.data(), (DWORD)text.size(), &written, nullptr) && written == text.size();
    if (ok) FlushFileBuffers(f);
    CloseHandle(f);
    if (!ok || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        LogF(L"Store: replacing %ls failed, error %lu", path.c_str(), GetLastError());
        DeleteFileW(temp.c_str());
        return;
    }
    s.dirty = false;
}

inline void FlushStore() {
    StoreState& s = Store();
    if (!s.ready) return;
    EnterCriticalSection(&s.lock);
    if (s.dirty) WriteStoreLocked();
    LeaveCriticalSection(&s.lock);
}

inline DWORD WINAPI StoreWriter(LPVOID) {
    StoreState& s = Store();
    while (!s.stop) {
        WaitForSingleObject(s.wake, INFINITE);
        if (s.stop) break;
        Sleep(300);  // gather quick changes (dragging an equalizer band)
        FlushStore();
    }
    return 0;
}

inline void MarkDirty() {
    StoreState& s = Store();
    s.dirty = true;
    if (s.wake) SetEvent(s.wake);
}

// Fills the store from the text of a settings file.
inline void ParseStoreData(const std::string& data) {
    StoreState& s = Store();
    size_t pos = 0;
    while (pos < data.size()) {
        size_t end = data.find('\n', pos);
        if (end == std::string::npos) end = data.size();
        size_t tab = data.find('\t', pos);
        if (tab != std::string::npos && tab < end) {
            s.map[Unescape(data.data() + pos, tab - pos)] =
                Unescape(data.data() + tab + 1, end - tab - 1);
        }
        pos = end + 1;
    }
}

// True if the text looks like a file of this app: key, tab, value, with s: or v: keys.
inline bool LooksLikeStoreData(const std::string& data) {
    return data.size() < 64u * 1024 * 1024 && (data.rfind("s:", 0) == 0 || data.rfind("v:", 0) == 0) &&
           data.find('\t') != std::string::npos;
}

// Everything the app keeps is replaced by the content of a file, and written to disk.
inline void ReplaceStore(const std::string& data) {
    StoreState& s = Store();
    EnterCriticalSection(&s.lock);
    s.map.clear();
    ParseStoreData(data);
    s.dirty = true;
    WriteStoreLocked();
    LeaveCriticalSection(&s.lock);
}

inline void LoadStore() {
    StoreState& s = Store();
    HANDLE f = CreateFileW(StorePath().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    s.existedAtStart = true;
    LARGE_INTEGER size;
    std::string data;
    if (GetFileSizeEx(f, &size) && size.QuadPart < 64 * 1024 * 1024) {
        data.resize((size_t)size.QuadPart);
        DWORD read = 0;
        if (!ReadFile(f, data.data(), (DWORD)data.size(), &read, nullptr)) read = 0;
        data.resize(read);
    }
    CloseHandle(f);
    ParseStoreData(data);
}

inline bool StoreGet(const std::wstring& key, std::wstring* value) {
    StoreState& s = Store();
    EnterCriticalSection(&s.lock);
    auto it = s.map.find(key);
    bool found = it != s.map.end();
    if (found) *value = it->second;
    LeaveCriticalSection(&s.lock);
    return found;
}
inline bool StoreSet(const std::wstring& key, const std::wstring& value) {
    StoreState& s = Store();
    EnterCriticalSection(&s.lock);
    auto it = s.map.find(key);
    if (it == s.map.end() || it->second != value) {
        s.map[key] = value;
        MarkDirty();
    }
    LeaveCriticalSection(&s.lock);
    return true;
}
inline bool StoreDelete(const std::wstring& key) {
    StoreState& s = Store();
    EnterCriticalSection(&s.lock);
    if (s.map.erase(key)) MarkDirty();
    LeaveCriticalSection(&s.lock);
    return true;
}

inline void InitStore() {
    StoreState& s = Store();
    if (s.ready) return;
    InitializeCriticalSection(&s.lock);
    LoadStore();
    s.wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    s.thread = CreateThread(nullptr, 0, StoreWriter, nullptr, 0, nullptr);
    s.ready = true;
}
inline void ShutdownStore() {
    StoreState& s = Store();
    if (!s.ready) return;
    FlushStore();
    InterlockedExchange(&s.stop, 1);
    SetEvent(s.wake);
    if (s.thread) {
        WaitForSingleObject(s.thread, 2000);
        CloseHandle(s.thread);
        s.thread = nullptr;
    }
}

// Settings (the "s:" part) -------------------------------------------------
inline std::wstring FormatName(PCWSTR name, va_list args) {
    WCHAR buffer[256];
    int n = _vsnwprintf(buffer, ARRAYSIZE(buffer) - 1, name, args);
    if (n < 0) n = ARRAYSIZE(buffer) - 1;
    buffer[n] = 0;
    return buffer;
}

inline PCWSTR DefaultString(const std::wstring& name) {
    if (name == L"theme") return L"system";
    if (name == L"visualStyle") return L"windows11";
    if (name == L"backgroundEffect") return L"blur";
    if (name == L"visibleRows") return L"auto";
    return L"";
}
inline int DefaultInt(const std::wstring& name) {
    static const std::unordered_map<std::wstring, int> defaults = {
        {L"compactMode", 0},        {L"backgroundOpacity", 85},
        {L"windowBorder", 1},       {L"volumeStep", 5},
        {L"keyboardControls", 0},   {L"exactVolumeEntry", 0},
        {L"animations", 1},         {L"closeWhenFocusIsLost", 1},
        {L"switchCommunicationsDevice", 1}, {L"moveNotifications", 1},
        {L"showIdleAppsOnAllOutputs", 1},   {L"showAllSessions", 0},
    };
    auto it = defaults.find(name);
    if (it != defaults.end()) return it->second;
    if (name.size() > 14 && name.find(L".defaultVolume") != std::wstring::npos) return -1;
    return 0;
}

inline std::wstring SettingKey(const std::wstring& name) { return L"s:" + name; }
inline std::wstring ValueKey(const std::wstring& name) { return L"v:" + name; }

inline void SetSettingString(const std::wstring& name, const std::wstring& value) {
    StoreSet(SettingKey(name), value);
}
inline void SetSettingInt(const std::wstring& name, int value) {
    StoreSet(SettingKey(name), std::to_wstring(value));
}
inline std::wstring GetSettingString(const std::wstring& name) {
    std::wstring value;
    if (StoreGet(SettingKey(name), &value)) return value;
    return DefaultString(name);
}
inline int GetSettingInt(const std::wstring& name) {
    std::wstring value;
    if (StoreGet(SettingKey(name), &value)) return (int)wcstol(value.c_str(), nullptr, 10);
    return DefaultInt(name);
}

}  // namespace headroom

// ---------------------------------------------------------------------------
// The Windhawk API, as the mixer calls it.
// ---------------------------------------------------------------------------
#define Wh_Log(message, ...) ::headroom::LogF(L"[%d:%S] " message, __LINE__, __FUNCTION__, ##__VA_ARGS__)

inline int Wh_GetIntValue(PCWSTR valueName, int defaultValue) {
    std::wstring value;
    if (!headroom::StoreGet(headroom::ValueKey(valueName), &value)) return defaultValue;
    return (int)wcstol(value.c_str(), nullptr, 10);
}
inline BOOL Wh_SetIntValue(PCWSTR valueName, int value) {
    return headroom::StoreSet(headroom::ValueKey(valueName), std::to_wstring(value));
}
inline size_t Wh_GetStringValue(PCWSTR valueName, PWSTR buffer, size_t bufferChars) {
    if (!buffer || !bufferChars) return 0;
    buffer[0] = 0;
    std::wstring value;
    if (!headroom::StoreGet(headroom::ValueKey(valueName), &value)) return 0;
    if (value.size() + 1 > bufferChars) return 0;  // same as Windhawk: too small = empty
    wmemcpy(buffer, value.c_str(), value.size() + 1);
    return value.size();
}
inline BOOL Wh_SetStringValue(PCWSTR valueName, PCWSTR value) {
    return headroom::StoreSet(headroom::ValueKey(valueName), value ? value : L"");
}
inline BOOL Wh_DeleteValue(PCWSTR valueName) {
    return headroom::StoreDelete(headroom::ValueKey(valueName));
}

inline int Wh_GetIntSetting(PCWSTR valueName, ...) {
    va_list args;
    va_start(args, valueName);
    std::wstring name = headroom::FormatName(valueName, args);
    va_end(args);
    return headroom::GetSettingInt(name);
}
inline PCWSTR Wh_GetStringSetting(PCWSTR valueName, ...) {
    va_list args;
    va_start(args, valueName);
    std::wstring name = headroom::FormatName(valueName, args);
    va_end(args);
    std::wstring value = headroom::GetSettingString(name);
    WCHAR* copy = new WCHAR[value.size() + 1];
    wmemcpy(copy, value.c_str(), value.size() + 1);
    return copy;
}
inline void Wh_FreeStringSetting(PCWSTR string) { delete[] const_cast<WCHAR*>(string); }

namespace WindhawkUtils {
class StringSetting {
   public:
    StringSetting() = default;
    explicit StringSetting(PCWSTR value) : m_value(value) {}
    template <typename... Args>
    static StringSetting make(PCWSTR valueName, Args... args) {
        return StringSetting(Wh_GetStringSetting(valueName, args...));
    }
    PCWSTR get() const { return m_value ? m_value.get() : L""; }

   private:
    struct Deleter {
        void operator()(const WCHAR* p) const { Wh_FreeStringSetting(p); }
    };
    std::unique_ptr<const WCHAR[], Deleter> m_value;
};
}  // namespace WindhawkUtils
