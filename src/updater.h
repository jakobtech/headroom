// Looking for a new version of Headroom on GitHub, and downloading it.
//
// Only what the settings window needs: ask GitHub for the latest release of the project, compare its version with
// the running one, download the setup (or the program), and check the download against the checksums published
// with the release. Nothing is sent but the request itself; the client names itself "Headroom-Update-Check".
#pragma once

#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <string>
#include <vector>

namespace updater {

constexpr wchar_t kApiHost[] = L"api.github.com";
constexpr wchar_t kLatestPath[] = L"/repos/jakobtech/headroom/releases/latest";
constexpr char kDownloadPrefix[] = "https://github.com/jakobtech/headroom/releases/download/";

struct Release {
    std::wstring tag;                 // For example "v2.1.0-alpha3".
    std::wstring setupUrl, setupName; // Headroom-Setup-<version>.exe
    std::wstring exeUrl;              // Headroom.exe
    std::wstring sumsUrl;             // SHA256SUMS.txt
};

inline std::wstring Widen(const std::string& text) {
    if (text.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

// --- versions -------------------------------------------------------------------------------------------------------------

struct Version {
    int major = 0, minor = 0, patch = 0;
    std::wstring label;  // "alpha3", empty for a final version.
};

inline Version ParseVersion(std::wstring text) {
    Version v;
    if (!text.empty() && (text[0] == L'v' || text[0] == L'V')) text.erase(0, 1);
    size_t dash = text.find(L'-');
    std::wstring numbers = text.substr(0, dash);
    if (dash != std::wstring::npos) v.label = text.substr(dash + 1);
    int* parts[3] = {&v.major, &v.minor, &v.patch};
    size_t start = 0;
    for (int i = 0; i < 3 && start <= numbers.size(); ++i) {
        size_t dot = numbers.find(L'.', start);
        *parts[i] = _wtoi(numbers.substr(start, dot == std::wstring::npos ? std::wstring::npos : dot - start).c_str());
        if (dot == std::wstring::npos) break;
        start = dot + 1;
    }
    return v;
}

// A label such as "alpha3" splits into its letters and its number: alpha < beta < rc, then by number.
inline int CompareLabels(const std::wstring& a, const std::wstring& b) {
    auto split = [](const std::wstring& label, std::wstring* letters, int* number) {
        size_t i = 0;
        while (i < label.size() && !iswdigit(label[i])) ++i;
        *letters = label.substr(0, i);
        *number = _wtoi(label.c_str() + i);
    };
    std::wstring la, lb;
    int na = 0, nb = 0;
    split(a, &la, &na);
    split(b, &lb, &nb);
    if (la != lb) return la < lb ? -1 : 1;
    return na < nb ? -1 : na > nb ? 1 : 0;
}

// -1 if a is older than b, 0 if they are the same, 1 if a is newer. A final version is newer than its tests.
inline int CompareVersions(const std::wstring& a, const std::wstring& b) {
    Version x = ParseVersion(a), y = ParseVersion(b);
    if (x.major != y.major) return x.major < y.major ? -1 : 1;
    if (x.minor != y.minor) return x.minor < y.minor ? -1 : 1;
    if (x.patch != y.patch) return x.patch < y.patch ? -1 : 1;
    if (x.label.empty() != y.label.empty()) return x.label.empty() ? 1 : -1;
    if (x.label.empty()) return 0;
    return CompareLabels(x.label, y.label);
}

// --- HTTPS ---------------------------------------------------------------------------------------------------------------

// Reads the answer to a GET request into `body` (or into the file `toFile`, when it is not empty).
inline bool Get(const std::wstring& url, std::string* body, const std::wstring& toFile, std::wstring* error) {
    URL_COMPONENTSW parts{sizeof(parts)};
    wchar_t host[256]{}, path[2048]{};
    parts.lpszHostName = host;
    parts.dwHostNameLength = ARRAYSIZE(host);
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = ARRAYSIZE(path);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) {
        *error = L"The address is not an https address.";
        return false;
    }
    HINTERNET session = WinHttpOpen(L"Headroom-Update-Check", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
    if (!session) {
        *error = L"Could not start a connection.";
        return false;
    }
    WinHttpSetTimeouts(session, 10000, 10000, 20000, 30000);
    bool ok = false;
    HANDLE file = INVALID_HANDLE_VALUE;
    HINTERNET connection = WinHttpConnect(session, host, parts.nPort, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                                        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                                   : nullptr;
    if (request && WinHttpSendRequest(request, L"Accept: application/vnd.github+json\r\n", static_cast<DWORD>(-1L),
                                      WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        if (status != 200) {
            *error = L"GitHub answered with status " + std::to_wstring(status) + L".";
        } else {
            if (!toFile.empty()) {
                file = CreateFileW(toFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            }
            ok = toFile.empty() || file != INVALID_HANDLE_VALUE;
            if (!ok) *error = L"Could not write the downloaded file.";
            std::vector<char> chunk(64 * 1024);
            for (;;) {
                DWORD available = 0, read = 0;
                if (!ok) break;
                if (!WinHttpQueryDataAvailable(request, &available)) {
                    ok = false;
                    *error = L"The connection broke off.";
                    break;
                }
                if (!available) break;
                available = std::min<DWORD>(available, static_cast<DWORD>(chunk.size()));
                if (!WinHttpReadData(request, chunk.data(), available, &read) || !read) {
                    ok = false;
                    *error = L"The connection broke off.";
                    break;
                }
                if (file != INVALID_HANDLE_VALUE) {
                    DWORD written = 0;
                    if (!WriteFile(file, chunk.data(), read, &written, nullptr) || written != read) {
                        ok = false;
                        *error = L"Could not write the downloaded file.";
                        break;
                    }
                } else if (body) {
                    body->append(chunk.data(), read);
                    if (body->size() > 4 * 1024 * 1024) {
                        ok = false;
                        *error = L"The answer is too large.";
                        break;
                    }
                }
            }
        }
    } else if (error->empty()) {
        *error = L"Could not reach GitHub. Is there an internet connection?";
    }
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (!ok && !toFile.empty()) DeleteFileW(toFile.c_str());
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return ok;
}

// --- reading the release ------------------------------------------------------------------------------------------

// The string value of the next `"key":"..."` at or after `from`, or empty.
inline std::string JsonString(const std::string& json, const std::string& key, size_t* from) {
    std::string needle = "\"" + key + "\"";
    size_t at = json.find(needle, *from);
    if (at == std::string::npos) return {};
    at += needle.size();
    while (at < json.size() && (json[at] == ' ' || json[at] == ':' || json[at] == '\t')) ++at;
    if (at >= json.size() || json[at] != '"') return {};
    ++at;
    std::string value;
    while (at < json.size() && json[at] != '"') {
        if (json[at] == '\\' && at + 1 < json.size()) ++at;  // The addresses have no escapes worth keeping.
        value += json[at++];
    }
    *from = at;
    return value;
}

inline bool FetchLatest(Release* release, std::wstring* error) {
    std::string json;
    if (!Get(std::wstring(L"https://") + kApiHost + kLatestPath, &json, L"", error)) return false;
    size_t at = 0;
    release->tag = Widen(JsonString(json, "tag_name", &at));
    if (release->tag.empty()) {
        *error = L"GitHub's answer has no version in it.";
        return false;
    }
    for (size_t cursor = at;;) {
        std::string url = JsonString(json, "browser_download_url", &cursor);
        if (url.empty()) break;
        // Only files of this project's releases.
        if (url.rfind(kDownloadPrefix, 0) != 0) continue;
        std::wstring name = Widen(url.substr(url.find_last_of('/') + 1));
        if (name.rfind(L"Headroom-Setup-", 0) == 0 && name.size() > 4 && name.substr(name.size() - 4) == L".exe") {
            release->setupUrl = Widen(url);
            release->setupName = name;
        } else if (name == L"Headroom.exe") {
            release->exeUrl = Widen(url);
        } else if (name == L"SHA256SUMS.txt") {
            release->sumsUrl = Widen(url);
        }
    }
    return true;
}

// --- checking a download ---------------------------------------------------------------------------------------

inline bool Sha256File(const std::wstring& path, std::string* hex) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    bool ok = CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
              CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash);
    std::vector<BYTE> chunk(256 * 1024);
    while (ok) {
        DWORD read = 0;
        if (!ReadFile(file, chunk.data(), static_cast<DWORD>(chunk.size()), &read, nullptr)) {
            ok = false;
            break;
        }
        if (!read) break;
        ok = CryptHashData(hash, chunk.data(), read, 0) != FALSE;
    }
    BYTE digest[32];
    DWORD size = sizeof(digest);
    if (ok) ok = CryptGetHashParam(hash, HP_HASHVAL, digest, &size, 0) && size == 32;
    if (ok) {
        static const char digits[] = "0123456789abcdef";
        hex->clear();
        for (BYTE b : digest) {
            *hex += digits[b >> 4];
            *hex += digits[b & 15];
        }
    }
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    CloseHandle(file);
    return ok;
}

// The line "<hash> *<name>" of a SHA256SUMS.txt for `name`; the hash in lower case, or empty.
inline std::string ExpectedHash(const std::string& sums, const std::string& name) {
    size_t start = 0;
    while (start < sums.size()) {
        size_t end = sums.find('\n', start);
        if (end == std::string::npos) end = sums.size();
        std::string line = sums.substr(start, end - start);
        start = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        size_t space = line.find(' ');
        if (space != 64) continue;
        std::string file = line.substr(space + 1);
        if (!file.empty() && file[0] == '*') file.erase(0, 1);
        if (file != name) continue;
        std::string hash = line.substr(0, 64);
        for (char& c : hash) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        return hash;
    }
    return {};
}

// Downloads `url` to `path`, and checks it against the release's checksums. False, with `error`, if anything fails.
inline bool DownloadChecked(const std::wstring& url, const std::wstring& name, const std::wstring& sumsUrl,
                            const std::wstring& path, std::wstring* error) {
    if (sumsUrl.empty()) {
        *error = L"The release has no checksums, so the download cannot be checked.";
        return false;
    }
    std::string sums;
    if (!Get(sumsUrl, &sums, L"", error)) return false;
    int size = WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string narrow(size > 1 ? static_cast<size_t>(size) - 1 : 0, '\0');
    if (size > 1) WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, narrow.data(), size, nullptr, nullptr);
    std::string expected = ExpectedHash(sums, narrow);
    if (expected.empty()) {
        *error = L"The checksums have no entry for this file.";
        return false;
    }
    if (!Get(url, nullptr, path, error)) return false;
    std::string actual;
    if (!Sha256File(path, &actual) || actual != expected) {
        DeleteFileW(path.c_str());
        *error = L"The download does not match its checksum and was thrown away.";
        return false;
    }
    return true;
}

}  // namespace updater
