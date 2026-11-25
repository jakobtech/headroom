"""Builds tests/eqlayers_test.cpp from the equalizer code in src/headroom.cpp, so that the test runs the real code:
Basic and the fine tuning are two layers, and switching between them must not change the sound."""
NL = chr(10)
src = open('src/headroom.cpp', encoding='utf-8', newline='').read().replace(chr(13) + chr(10), NL)


def between(start, end):
    a = src.index(start)
    b = src.index(end, a)
    return src[a:b]


state_code = between('constexpr int EQ_MAX_BANDS = 31;', '// Presets. The built-in curves')
filter_code = between('struct Biquad {', 'inline float RunBiquad(')

head = r'''#include <algorithm>
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
static double ResponseDb(const EqualizerState& eq, double frequency) {
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

static double MostDifference(const EqualizerState& a, const EqualizerState& b) {
    double most = 0.0;
    for (double f = 20.0; f <= 20000.0; f *= 1.05) most = std::max(most, std::fabs(ResponseDb(a, f) - ResponseDb(b, f)));
    return most;
}

static int g_failed = 0;
static void Check(bool ok, const char* what, double value = 0.0) {
    std::printf("%s  %s  (%.4f dB)\n", ok ? "ok  " : "FAIL", what, value);
    if (!ok) ++g_failed;
}

int main() {
    // Basic: bass +18 dB, treble +10 dB.
    EqualizerState basic;
    basic.on = true;
    basic.gains[0] = 180;
    basic.gains[2] = 100;
    double bass = ResponseDb(basic, 60.0);
    Check(bass > 10.0, "Basic really boosts the bass", bass);

    for (int layout : {5, 10, 15}) {
        EqualizerState view = basic;
        SwitchEqualizerView(view, layout);
        Check(view.bands == layout, "the layout is shown");
        double diff = MostDifference(basic, view);
        Check(diff < 0.001, "Basic -> more bands keeps the sound", diff);
    }

    // Fine tune on 15 bands, then go back to Basic and forth again: the sound stays.
    EqualizerState tuned = basic;
    SwitchEqualizerView(tuned, 15);
    tuned.gains[7] = 60;  // 630 Hz +6 dB.
    tuned.gains[3] = -40;
    double changed = MostDifference(basic, tuned);
    Check(changed > 3.0, "fine tuning changes the sound", changed);
    EqualizerState back = tuned;
    SwitchEqualizerView(back, 3);
    Check(back.bands == 3 && back.gains[0] == 180 && back.gains[2] == 100, "Basic comes back as it was");
    double d1 = MostDifference(tuned, back);
    Check(d1 < 0.001, "15 bands -> Basic keeps the sound", d1);
    SwitchEqualizerView(back, 15);
    Check(back.gains[7] == 60 && back.gains[3] == -40, "the fine tuning comes back as it was");
    double d2 = MostDifference(tuned, back);
    Check(d2 < 0.001, "Basic -> 15 bands again keeps the sound", d2);

    // Saving and loading.
    g_store[L"eq_v1:x"] = EqualizerValueText(tuned);
    EqualizerState loaded = LoadEqualizer(L"x");
    double d3 = MostDifference(tuned, loaded);
    Check(d3 < 0.001 && loaded.bands == 15, "save and load keep the sound", d3);
    EqualizerState shown = tuned;
    SwitchEqualizerView(shown, 3);
    g_store[L"eq_v1:y"] = EqualizerValueText(shown);
    EqualizerState loaded2 = LoadEqualizer(L"y");
    double d4 = MostDifference(tuned, loaded2);
    Check(d4 < 0.001 && loaded2.bands == 3 && loaded2.hiddenBands == 15, "Basic shown, fine tuning kept: save and load", d4);

    // An older value: 10 bands, no tail. Its sound is the same as the same bands as a fine tuning without Basic.
    g_store[L"eq_v1:old"] = L"1;10;0,0,0,50,0,0,0,0,0,0";
    EqualizerState old = LoadEqualizer(L"old");
    Check(old.on && old.bands == 10 && old.gains[3] == 50 && old.hiddenBands == 3, "an older value loads");
    double beforeAndAfter = ResponseDb(old, 250.0);
    Check(beforeAndAfter > 4.0, "an older value keeps its sound", beforeAndAfter);
    EqualizerState toBasic = old;
    SwitchEqualizerView(toBasic, 3);
    Check(MostDifference(old, toBasic) < 0.001, "an older value -> Basic keeps the sound", MostDifference(old, toBasic));

    // An empty value, and the first format without a count.
    EqualizerState empty = LoadEqualizer(L"missing");
    Check(!empty.on && empty.bands == 3 && !EqualizerHasGain(empty), "nothing stored gives a flat Basic");
    g_store[L"eq_v1:first"] = L"1;0,0,0,0,0,40,0,0,0,0";
    EqualizerState first = LoadEqualizer(L"first");
    Check(first.bands == 10 && first.gains[5] == 40, "the first format loads");

    std::printf(g_failed ? "FAILED\n" : "all passed\n");
    return g_failed ? 1 : 0;
}
'''
open('tests/eqlayers_test.cpp', 'w', encoding='utf-8', newline=NL).write(head + state_code + filter_code + tail)
print('written')
