"""Builds tests/eqlayers_test.cpp from the equalizer code in src/headroom.cpp, so that the test runs the real code.

An equalizer is Basic (bass, mids, treble) with, on top of it, a fine tuning of 5, 10 or 15 bands. Going from Basic to
more bands must not change the sound (the fine tuning starts at zero), and the faders of the new layout show the curve
of Basic fitted to their bands.

  python tests/make_eqlayers_test.py
  clang++ -std=c++23 -target x86_64-w64-mingw32 -DUNICODE -D_UNICODE -static tests/eqlayers_test.cpp -o eqlayers_test.exe
"""
NL = chr(10)
src = open('src/headroom.cpp', encoding='utf-8', newline='').read().replace(chr(13) + chr(10), NL)


def between(start, end):
    a = src.index(start)
    b = src.index(end, a)
    return src[a:b]


state_code = between('constexpr int EQ_MAX_BANDS = 31;', '// Presets. The built-in curves')
filter_code = between('struct Biquad {', 'inline float RunBiquad(')

head = r'''#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>

struct Settings { bool eqRangeCustom = false; int eqRangeDb = 12; } g_settings;
int ClampInt(int value, int minimum, int maximum) { return value < minimum ? minimum : value > maximum ? maximum : value; }
static std::unordered_map<std::wstring, std::wstring> g_store;
void Wh_GetStringValue(const wchar_t* name, wchar_t* out, size_t size) {
    auto found = g_store.find(name);
    std::wstring value = found == g_store.end() ? L"" : found->second;
    wcsncpy(out, value.c_str(), size - 1);
    out[size - 1] = 0;
}

'''
tail = r'''
// ---------------------------------------------------------------------------------------------------------------------------
// What the engine plays: the filters of both layers, from their coefficients.
static double PlayedDb(const EqualizerState& eq, double frequency) {
    Biquad filters[EQ_FILTERS];
    DesignEqualizerFilters(filters, eq, 48000.0);
    const double pi = 3.14159265358979323846;
    double w = 2.0 * pi * frequency / 48000.0;
    double total = 0.0;
    for (int i = 0; i < EQ_FILTERS; ++i) {
        if (!filters[i].active) continue;
        double c1 = std::cos(w), s1 = std::sin(w), c2 = std::cos(2 * w), s2 = std::sin(2 * w);
        double nr = filters[i].b0 + filters[i].b1 * c1 + filters[i].b2 * c2;
        double ni = -(filters[i].b1 * s1 + filters[i].b2 * s2);
        double dr = 1.0 + filters[i].a1 * c1 + filters[i].a2 * c2;
        double di = -(filters[i].a1 * s1 + filters[i].a2 * s2);
        total += 10.0 * std::log10((nr * nr + ni * ni) / (dr * dr + di * di));
    }
    return total;
}

static double MostPlayedDifference(const EqualizerState& a, const EqualizerState& b) {
    double most = 0.0;
    for (double f = 20.0; f <= 20000.0; f *= 1.03) most = std::max(most, std::fabs(PlayedDb(a, f) - PlayedDb(b, f)));
    return most;
}

// What the faders of a fine tuning layout show of Basic, against Basic itself.
static double ShownDifference(const EqualizerState& basic, int layout, double from, double to) {
    int fit[EQ_MAX_BANDS] = {};
    FitEqualizerGains(basic, layout, fit);
    const EqLayout& l = EqLayoutFor(layout);
    double most = 0.0;
    for (double f = from; f <= to; f *= 1.03) {
        double shown = 0.0, played = PlayedDb(basic, f);
        for (int band = 0; band < l.count; ++band) shown += BandResponseDb(l, band, fit[band] / 10.0, f);
        most = std::max(most, std::fabs(shown - played));
    }
    return most;
}

static int g_failed = 0;
static void Check(bool ok, const char* what, double value = 0.0) {
    std::printf("%s  %s  (%.3f)\n", ok ? "ok  " : "FAIL", what, value);
    if (!ok) ++g_failed;
}

static EqualizerState Curve(int bass, int mids, int treble) {
    EqualizerState basic;
    basic.on = true;
    basic.gains[0] = bass;
    basic.gains[1] = mids;
    basic.gains[2] = treble;
    return basic;
}

int main() {
    struct Case { const char* name; EqualizerState basic; } cases[] = {
        {"bass +18, treble +13", Curve(180, 0, 130)},
        {"bass +12, mids -6, treble +6", Curve(120, -60, 60)},
        {"bass -10, treble -10", Curve(-100, 0, -100)},
        {"mids +10", Curve(0, 100, 0)},
    };
    for (const Case& c : cases) {
        std::printf("--- %s%c", c.name, 10);
        for (int layout : {5, 10, 15}) {
            EqualizerState view = c.basic;
            SwitchEqualizerView(view, layout);
            double played = MostPlayedDifference(c.basic, view);
            double shown = ShownDifference(c.basic, layout, 40.0, 16000.0);
            std::printf("     %2d bands: plays %.4f dB off, the faders show it within %.2f dB (40 Hz - 16 kHz)%c", layout, played, shown, 10);
            Check(played < 0.001, "Basic -> more bands keeps the sound exactly", played);
        }
    }

    // The faders of the user's own curve are not flat.
    EqualizerState own = Curve(180, 0, 130);
    int fit[EQ_MAX_BANDS] = {};
    auto started = std::chrono::steady_clock::now();
    FitEqualizerGains(own, 15, fit);
    double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    int moved = 0;
    for (int band = 0; band < 15; ++band) moved += fit[band] != 0;
    Check(moved >= 8, "the 15 faders show the curve instead of lying flat", moved);
    Check(milliseconds < 150.0, "the fit is quick enough to run when the layout is chosen (ms)", milliseconds);
    std::printf("     faders:");
    for (int band = 0; band < 15; ++band) std::printf(" %.1f", fit[band] / 10.0);
    std::printf("%c", 10);

    // Fine tune on 15 bands, then Basic and 15 again: the sound is the same all the way, and the fine tuning stays.
    EqualizerState tuned = own;
    SwitchEqualizerView(tuned, 15);
    tuned.gains[7] = 60;
    tuned.gains[3] = -40;
    Check(MostPlayedDifference(own, tuned) > 3.0, "fine tuning changes the sound", MostPlayedDifference(own, tuned));
    EqualizerState back = tuned;
    SwitchEqualizerView(back, 3);
    Check(back.bands == 3 && back.gains[0] == 180 && back.gains[1] == 0 && back.gains[2] == 130, "Basic comes back as it was");
    Check(MostPlayedDifference(tuned, back) < 0.001, "15 bands -> Basic keeps the sound", MostPlayedDifference(tuned, back));
    SwitchEqualizerView(back, 15);
    Check(back.gains[7] == 60 && back.gains[3] == -40, "the fine tuning comes back as it was");
    Check(MostPlayedDifference(tuned, back) < 0.001, "Basic -> 15 bands again keeps the sound", MostPlayedDifference(tuned, back));

    // Saving and loading.
    g_store[L"eq_v1:x"] = EqualizerValueText(tuned);
    EqualizerState loaded = LoadEqualizer(L"x");
    Check(MostPlayedDifference(tuned, loaded) < 0.001 && loaded.bands == 15, "save and load keep the sound", MostPlayedDifference(tuned, loaded));
    EqualizerState shown = tuned;
    SwitchEqualizerView(shown, 3);
    g_store[L"eq_v1:y"] = EqualizerValueText(shown);
    EqualizerState loaded2 = LoadEqualizer(L"y");
    Check(MostPlayedDifference(tuned, loaded2) < 0.001 && loaded2.bands == 3 && loaded2.hiddenBands == 15, "Basic shown, fine tuning kept: save and load", MostPlayedDifference(tuned, loaded2));

    // An older value: 10 bands, no tail.
    g_store[L"eq_v1:old"] = L"1;10;0,0,0,50,0,0,0,0,0,0";
    EqualizerState old = LoadEqualizer(L"old");
    Check(old.on && old.bands == 10 && old.gains[3] == 50 && old.hiddenBands == 3, "an older value loads");
    Check(PlayedDb(old, 250.0) > 4.0, "an older value keeps its sound", PlayedDb(old, 250.0));
    EqualizerState toBasic = old;
    SwitchEqualizerView(toBasic, 3);
    Check(MostPlayedDifference(old, toBasic) < 0.001, "an older value -> Basic keeps the sound", MostPlayedDifference(old, toBasic));
    EqualizerState empty = LoadEqualizer(L"missing");
    Check(!empty.on && empty.bands == 3 && !EqualizerHasGain(empty), "nothing stored gives a flat Basic");
    g_store[L"eq_v1:first"] = L"1;0,0,0,0,0,40,0,0,0,0";
    EqualizerState first = LoadEqualizer(L"first");
    Check(first.bands == 10 && first.gains[5] == 40, "the first format loads");

    std::printf(g_failed ? "FAILED%c" : "all passed%c", 10);
    return g_failed ? 1 : 0;
}
'''
open('tests/eqlayers_test.cpp', 'w', encoding='utf-8', newline=NL).write(head + state_code + filter_code + tail)
print('written')
