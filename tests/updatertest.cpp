// Tests of the update check (src/updater.h): the version order, and, with the network, the latest release of the
// project: its files are downloaded and checked against the published checksums.
//
//   updatertest.exe           version order only
//   updatertest.exe --net     also asks GitHub and downloads the portable program into the temp folder
#include <cstdio>
#include <cstring>

#include "../src/updater.h"

static int failures = 0;

static void Expect(const wchar_t* a, const wchar_t* b, int want) {
    int got = updater::CompareVersions(a, b);
    bool ok = got == want;
    if (!ok) ++failures;
    wprintf(L"%ls %ls %ls -> %d (want %d)\n", ok ? L"ok  " : L"FAIL", a, b, got, want);
}

int main(int argc, char** argv) {
    Expect(L"2.1.0-alpha3", L"2.1.0-alpha3", 0);
    Expect(L"v2.1.0-alpha4", L"2.1.0-alpha3", 1);
    Expect(L"2.1.0-alpha3", L"2.1.0-alpha10", -1);
    Expect(L"2.1.0", L"2.1.0-alpha3", 1);
    Expect(L"2.1.0-rc1", L"2.1.0-beta9", 1);
    Expect(L"2.1.0-beta1", L"2.1.0-alpha9", 1);
    Expect(L"2.2.0-alpha1", L"2.1.9", 1);
    Expect(L"2.0.0", L"2.1.0-alpha3", -1);
    Expect(L"v3.0.0", L"2.9.9", 1);

    std::string sums =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa *Headroom.exe\r\n"
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB *Headroom-Setup-1.exe\n";
    std::string h = updater::ExpectedHash(sums, "Headroom-Setup-1.exe");
    bool hashOk = h == std::string(64, 'b') && updater::ExpectedHash(sums, "Headroom.exe") == std::string(64, 'a') &&
                  updater::ExpectedHash(sums, "Other.exe").empty();
    wprintf(L"%ls checksum lines\n", hashOk ? L"ok  " : L"FAIL");
    if (!hashOk) ++failures;

    if (argc > 1 && strcmp(argv[1], "--net") == 0) {
        updater::Release release;
        std::wstring error;
        if (!updater::FetchLatest(&release, &error)) {
            wprintf(L"FAIL fetch: %ls\n", error.c_str());
            return 1;
        }
        wprintf(L"latest: %ls\n  setup: %ls\n  exe:   %ls\n  sums:  %ls\n", release.tag.c_str(), release.setupUrl.c_str(),
                release.exeUrl.c_str(), release.sumsUrl.c_str());
        wchar_t temp[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        std::wstring path = std::wstring(temp) + L"headroom-update-test.exe";
        if (!updater::DownloadChecked(release.exeUrl, L"Headroom.exe", release.sumsUrl, path, &error)) {
            wprintf(L"FAIL download: %ls\n", error.c_str());
            return 1;
        }
        wprintf(L"ok   downloaded and checked: %ls\n", path.c_str());
        // A wrong name must fail on the checksum lookup, a tampered file on the hash.
        if (updater::DownloadChecked(release.exeUrl, L"Nothing.exe", release.sumsUrl, path + L".x", &error)) {
            wprintf(L"FAIL a file without a checksum was accepted\n");
            ++failures;
        } else {
            wprintf(L"ok   refused: %ls\n", error.c_str());
        }
        DeleteFileW(path.c_str());
    }
    wprintf(failures ? L"%d failure(s)\n" : L"all good\n", failures);
    return failures ? 1 : 0;
}
