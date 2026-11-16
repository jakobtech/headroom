// Headroom, standalone app.
//
// Same mixer source as the Windhawk mod (headroom.cpp), built as
// a normal program: tray icon, settings window, start with Windows.
//
// Copyright (c) Jakob Hüttner. Portions derived from the Windhawk mod
// "Better Volume Mixer" by 0Allu (MIT), see LICENSE.

#define HEADROOM_STANDALONE 1
#include "headroom.cpp"

#include <commctrl.h>
#include <commdlg.h>
#include <shellscalingapi.h>
#include <uxtheme.h>

#include <sstream>

namespace {

constexpr PCWSTR RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr PCWSTR RUN_NAME = L"Headroom";
constexpr PCWSTR WINDHAWK_MOD_KEY =
    L"SOFTWARE\\Windhawk\\Engine\\Mods\\local@better-volume-mixer";
constexpr PCWSTR SETTINGS_CLASS = L"HeadroomSettings";

std::wstring ExePath() {
    WCHAR path[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    return std::wstring(path, n);
}

// ---------------------------------------------------------------------------
// Start with Windows
// ---------------------------------------------------------------------------
bool AutostartEnabled() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    bool present = RegQueryValueExW(key, RUN_NAME, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    RegCloseKey(key);
    return present;
}

// An earlier version started with Windows under another name: that entry becomes this one.
void MigrateAutostart() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return;
    if (RegQueryValueExW(key, L"BetterVolumeMixer", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        RegDeleteValueW(key, L"BetterVolumeMixer");
        std::wstring command = L"\"" + ExePath() + L"\"";
        RegSetValueExW(key, RUN_NAME, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                       static_cast<DWORD>((command.size() + 1) * sizeof(WCHAR)));
    }
    RegCloseKey(key);
}

void SetAutostart(bool on) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS) {
        return;
    }
    if (on) {
        std::wstring command = L"\"" + ExePath() + L"\"";
        RegSetValueExW(key, RUN_NAME, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                       static_cast<DWORD>((command.size() + 1) * sizeof(WCHAR)));
    } else {
        RegDeleteValueW(key, RUN_NAME);
    }
    RegCloseKey(key);
}

// ---------------------------------------------------------------------------
// Import from the Windhawk mod (settings and stored values)
// ---------------------------------------------------------------------------
int ImportKey(PCWSTR subKey, PCWSTR prefix) {
    std::wstring path = std::wstring(WINDHAWK_MOD_KEY) + L"\\" + subKey;
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) {
        return 0;
    }
    int count = 0;
    for (DWORD index = 0;; ++index) {
        WCHAR name[512];
        DWORD nameLen = ARRAYSIZE(name), type = 0, size = 0;
        LONG r = RegEnumValueW(key, index, name, &nameLen, nullptr, &type, nullptr, &size);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r != ERROR_SUCCESS && r != ERROR_MORE_DATA) continue;
        std::vector<BYTE> data(size + 4, 0);
        DWORD dataSize = size;
        nameLen = ARRAYSIZE(name);
        if (RegEnumValueW(key, index, name, &nameLen, nullptr, &type, data.data(), &dataSize) != ERROR_SUCCESS) {
            continue;
        }
        std::wstring value;
        if (type == REG_DWORD && dataSize >= 4) {
            value = std::to_wstring(static_cast<int>(*reinterpret_cast<DWORD*>(data.data())));
        } else if (type == REG_SZ) {
            value = reinterpret_cast<WCHAR*>(data.data());
        } else {
            continue;
        }
        headroom::StoreSet(std::wstring(prefix) + name, value);
        ++count;
    }
    RegCloseKey(key);
    return count;
}

int ImportFromWindhawk() {
    int n = ImportKey(L"Settings", L"s:");
    n += ImportKey(L"Storage", L"v:");
    Wh_Log(L"Import from the Windhawk mod: %d entries", n);
    return n;
}

bool WindhawkModRunning() {
    HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, L"windhawk-tool-mod_better-volume-mixer");
    if (!mutex) return false;
    CloseHandle(mutex);
    return true;
}

// ---------------------------------------------------------------------------
// Settings window, drawn with the mixer's own look: the same cards, switches,
// segmented choices and sliders as the equalizer.
// ---------------------------------------------------------------------------
struct Option {
    PCWSTR value;
    PCWSTR label;
};
constexpr Option kThemeOptions[] = {{L"system", L"Windows"}, {L"dark", L"Dark"}, {L"light", L"Light"}};
constexpr Option kStyleOptions[] = {{L"windows11", L"Windows 11"}, {L"windows10", L"Windows 10"}};
constexpr Option kEffectOptions[] = {{L"glass", L"Frosted"}, {L"blur", L"Blur"}, {L"none", L"None"}};
constexpr Option kRowsOptions[] = {{L"auto", L"Fill"}, {L"5", L"5"},   {L"7", L"7"},
                                   {L"10", L"10"},     {L"15", L"15"}, {L"20", L"20"}};

enum class Kind { Toggle, Segments, Slider };

struct Item {
    Kind kind;
    PCWSTR label;
    PCWSTR name;
    const Option* options = nullptr;
    int optionCount = 0;
    int low = 0, high = 0;
    PCWSTR unit = L"";
    PCWSTR info = L"";
    RECT row{}, control{}, infoRect{};
    RECT segments[6]{};
};

// What each setting does, shown next to it as an (i).
PCWSTR SettingInfo(const std::wstring& name) {
    struct Entry { PCWSTR name; PCWSTR text; };
    static const Entry entries[] = {
        {L"theme", L"The look of the mixer. Windows follows the light or dark mode of Windows; Dark and Light force one."},
        {L"visualStyle", L"Windows 11: rounded corners and the Windows 11 slider. Windows 10: square corners, grey colors and the flat slider with a bar-shaped handle."},
        {L"backgroundEffect", L"What shows through the mixer while Opacity is below 100%. Frosted blurs it and evens out its brightness, so text stays readable on anything. Blur only blurs it; bright backgrounds can wash out text. None is see-through but sharp."},
        {L"backgroundOpacity", L"How see-through the background is, 0 to 100%. 100% is solid. Text and controls always stay solid. The effect above only shows below 100%."},
        {L"windowBorder", L"Shows the thin outline Windows draws around the mixer. Turn it off for a borderless look."},
        {L"compactMode", L"Smaller, denser rows and a narrower window, so more apps fit at once."},
        {L"animations", L"Animates opening and closing, hover effects and volume changes. The Windows animation setting is respected as well."},
        {L"visibleRows", L"How many app rows show before the list scrolls. Fill uses all the height the screen has; a number keeps the mixer smaller."},
        {L"closeWhenFocusIsLost", L"Closes the mixer when you click somewhere else. Turn it off to keep it open until you close it yourself."},
        {L"keyboardControls", L"While the mixer is focused: Up and Down choose a row, Left and Right change the volume by the mouse-wheel step, M mutes."},
        {L"exactVolumeEntry", L"Click a percentage to type an exact whole number from 0 to 100. Enter applies it, Esc or a click elsewhere cancels."},
        {L"switchCommunicationsDevice", L"When you pick an output in the mixer, also make it the default communications device, like Windows Settings does. Teams, Zoom and Discord then follow. Turn it off to leave the communications device alone."},
        {L"moveNotifications", L"While the mixer is open, Windows notifications that would cover it are moved aside, and come back when it closes."},
        {L"showIdleAppsOnAllOutputs", L"Lists apps that are silent even when their audio goes to an output other than the default one, for example Steam after you switched from headset to speakers. Off: such apps are listed once they play something."},
        {L"showAllSessions", L"Shows every audio session separately per output, including unused ones, instead of one row per app. Most people leave this off."},
        {L"volumeStep", L"How many percent one step of the mouse wheel changes, over an app row, the master volume or the tray icon. 1 to 20; 5 is usual."},
    };
    for (const Entry& entry : entries) {
        if (name == entry.name) return entry.text;
    }
    return L"";
}

struct HiddenOutput {
    std::wstring id, name;
    RECT show{};
};

struct HiddenApp {
    std::wstring name;
    RECT show{};
    bool system = false;  // The mixer's "System sounds" rows, hidden as a group.
};

struct SettingsUi {
    HWND window = nullptr;
    HWND pathEdit = nullptr, addEdit = nullptr;
    std::vector<Item> items;
    std::unordered_map<std::wstring, int> ints;
    std::unordered_map<std::wstring, std::wstring> strings;
    int page = 0;                   // 0 settings, 1 setup guide.
    int hot = -1, hotSegment = -1;  // The item under the pointer (and its segment).
    int hotButton = -1;             // 1 save, 2 cancel, 3 browse, 4 show all, 5 add an app, 10+ show an output, 20+ guide, 30+ show an app.
    int hotInfo = -1;               // 0..n-1 items, 100.. cards.
    int hotTab = -1;
    int drag = -1;
    bool tracking = false;
    RECT buttons[9]{};  // 6 reset, 7 export, 8 import.
    RECT tabs[2]{};
    RECT pathCard{}, hiddenCard{}, outputsCard{};
    RECT addField{};
    int capacity = 3;  // How many rows the two lists show.
    int scroll[2] = {0, 0};  // The first row shown: 0 the apps, 1 the devices.
    int scrollDrag = -1, scrollGrab = 0;
    RECT cardInfo[4]{};
    RECT guideCards[3]{}, guideButtons[3]{};
    std::vector<HiddenOutput> hiddenOutputs;
    std::vector<HiddenApp> hiddenApps;
    int otherHidden = 0;
    int width = 0, height = 0;
    HICON bigIcon = nullptr, smallIcon = nullptr;
};
SettingsUi g_ui;

enum : int { ITEM_BASE = 1000, TAB_BASE = 900 };

RECT Box(int x, int y, int w, int h) { return {Scale(x), Scale(y), Scale(x + w), Scale(y + h)}; }

std::wstring EndpointNameById(const std::wstring& id) {
    std::wstring name;
    IMMDeviceEnumerator* enumerator = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                                   __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator)))) {
        IMMDevice* device = nullptr;
        if (SUCCEEDED(enumerator->GetDevice(id.c_str(), &device))) {
            name = ReadEndpointName(device);
            device->Release();
        }
        enumerator->Release();
    }
    return name.empty() ? L"Output that is not connected" : name;
}

// The keys under which the System sounds rows of all outputs and inputs are hidden.
std::unordered_set<std::wstring> SystemSoundsKeys() {
    std::unordered_set<std::wstring> keys;
    keys.insert(SYSTEM_SOUNDS_KEY);
    IMMDeviceEnumerator* enumerator = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                                   __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator)))) {
        IMMDeviceCollection* devices = nullptr;
        if (SUCCEEDED(enumerator->EnumAudioEndpoints(eAll, DEVICE_STATE_ACTIVE | DEVICE_STATE_DISABLED |
                                                         DEVICE_STATE_UNPLUGGED, &devices)) && devices) {
            UINT count = 0;
            devices->GetCount(&count);
            for (UINT i = 0; i < count; ++i) {
                IMMDevice* device = nullptr;
                LPWSTR id = nullptr;
                if (SUCCEEDED(devices->Item(i, &device)) && device && SUCCEEDED(device->GetId(&id)) && id) {
                    keys.insert(SourcePinKey(L"", L"", id, true));
                    CoTaskMemFree(id);
                }
                if (device) device->Release();
            }
            devices->Release();
        }
        enumerator->Release();
    }
    return keys;
}

void GatherHiddenOutputs() {
    g_ui.hiddenOutputs.clear();
    g_ui.hiddenApps.clear();
    for (int i = 0; i < 256; ++i) {
        std::wstring name = headroom::GetSettingString(L"hiddenApps[" + std::to_wstring(i) + L"]");
        if (!name.empty()) g_ui.hiddenApps.push_back({name, {}});
    }
    g_ui.otherHidden = 0;
    LoadPermanentlyHiddenSources();
    std::unordered_set<std::wstring> systemKeys = SystemSoundsKeys();
    bool systemHidden = false;
    for (const std::wstring& key : g_permanentlyHiddenSources) {
        if (systemKeys.count(key)) {
            systemHidden = true;
        } else if (key.rfind(L"device:", 0) == 0) {
            HiddenOutput output;
            output.id = key.substr(7);
            output.name = EndpointNameById(output.id);
            g_ui.hiddenOutputs.push_back(std::move(output));
        } else {
            ++g_ui.otherHidden;
        }
    }
    // System sounds belong with the apps.
    if (systemHidden) g_ui.hiddenApps.push_back({L"System sounds", {}, true});
}

// The scroll bar of a list: its track, and where the thumb is.
int ListCount(int list) {
    return static_cast<int>(list == 0 ? g_ui.hiddenApps.size() : g_ui.hiddenOutputs.size());
}
const RECT& ListCard(int list) { return list == 0 ? g_ui.hiddenCard : g_ui.outputsCard; }
int MaxScroll(int list) { return std::max(0, ListCount(list) - g_ui.capacity); }
RECT ScrollTrack(int list) {
    const RECT& card = ListCard(list);
    return {card.right - Scale(9), card.top + Scale(52), card.right - Scale(5),
            card.top + Scale(48) + g_ui.capacity * Scale(36) - Scale(6)};
}
RECT ScrollThumb(int list) {
    RECT track = ScrollTrack(list);
    int count = std::max(1, ListCount(list));
    int length = track.bottom - track.top;
    int thumb = std::max(Scale(24), length * g_ui.capacity / count);
    int range = std::max(1, MaxScroll(list));
    int top = track.top + (length - thumb) * g_ui.scroll[list] / range;
    return {track.left, top, track.right, top + thumb};
}

// Where the rows, the field and the buttons of the two hidden lists sit.
void LayoutHiddenLists() {
    int cap = g_ui.capacity;
    for (int list = 0; list < 2; ++list) g_ui.scroll[list] = std::clamp(g_ui.scroll[list], 0, MaxScroll(list));
    for (size_t i = 0; i < g_ui.hiddenApps.size(); ++i) {
        int top = g_ui.hiddenCard.top + Scale(48) + (static_cast<int>(i) - g_ui.scroll[0]) * Scale(36);
        g_ui.hiddenApps[i].show = {g_ui.hiddenCard.right - Scale(100), top + Scale(3),
                                   g_ui.hiddenCard.right - Scale(16), top + Scale(31)};
        if (static_cast<int>(i) < g_ui.scroll[0] || static_cast<int>(i) >= g_ui.scroll[0] + cap) {
            g_ui.hiddenApps[i].show = {};
        }
    }
    for (size_t i = 0; i < g_ui.hiddenOutputs.size(); ++i) {
        int top = g_ui.outputsCard.top + Scale(48) + (static_cast<int>(i) - g_ui.scroll[1]) * Scale(36);
        g_ui.hiddenOutputs[i].show = {g_ui.outputsCard.right - Scale(100), top + Scale(3),
                                      g_ui.outputsCard.right - Scale(16), top + Scale(31)};
        if (static_cast<int>(i) < g_ui.scroll[1] || static_cast<int>(i) >= g_ui.scroll[1] + cap) {
            g_ui.hiddenOutputs[i].show = {};
        }
    }
    int footerTop = g_ui.hiddenCard.bottom - Scale(48);
    g_ui.addField = {g_ui.hiddenCard.left + Scale(16), footerTop, g_ui.hiddenCard.right - Scale(108),
                     footerTop + Scale(34)};
    g_ui.buttons[5] = {g_ui.hiddenCard.right - Scale(100), footerTop, g_ui.hiddenCard.right - Scale(16),
                       footerTop + Scale(34)};
    g_ui.buttons[4] = {g_ui.outputsCard.right - Scale(140), footerTop, g_ui.outputsCard.right - Scale(16),
                       footerTop + Scale(34)};
}

void BuildItems() {
    g_ui.items.clear();
    auto toggle = [](PCWSTR label, PCWSTR name) {
        Item item;
        item.kind = Kind::Toggle;
        item.label = label;
        item.name = name;
        return item;
    };
    auto segments = [](PCWSTR label, PCWSTR name, const Option* options, int count) {
        Item item;
        item.kind = Kind::Segments;
        item.label = label;
        item.name = name;
        item.options = options;
        item.optionCount = count;
        return item;
    };
    auto slider = [](PCWSTR label, PCWSTR name, int low, int high, PCWSTR unit) {
        Item item;
        item.kind = Kind::Slider;
        item.label = label;
        item.name = name;
        item.low = low;
        item.high = high;
        item.unit = unit;
        return item;
    };
    std::vector<Item> left = {
        segments(L"Theme", L"theme", kThemeOptions, 3),
        segments(L"Visual style", L"visualStyle", kStyleOptions, 2),
        segments(L"Background", L"backgroundEffect", kEffectOptions, 3),
        slider(L"Opacity", L"backgroundOpacity", 0, 100, L" %"),
        toggle(L"Window border", L"windowBorder"),
        toggle(L"Compact mode", L"compactMode"),
        toggle(L"Animations", L"animations"),
        segments(L"Visible rows", L"visibleRows", kRowsOptions, 6),
    };
    std::vector<Item> right = {
        toggle(L"Close when focus is lost", L"closeWhenFocusIsLost"),
        toggle(L"Keyboard controls", L"keyboardControls"),
        toggle(L"Exact volume entry", L"exactVolumeEntry"),
        toggle(L"Switch communications device", L"switchCommunicationsDevice"),
        toggle(L"Move notifications aside", L"moveNotifications"),
        toggle(L"Silent apps on every output", L"showIdleAppsOnAllOutputs"),
        toggle(L"Show all audio sessions", L"showAllSessions"),
        slider(L"Mouse-wheel step", L"volumeStep", 1, 20, L" %"),
    };
    const int columnWidth = 410, rowHeight = 44, rowStep = 48, top = 76;
    for (int column = 0; column < 2; ++column) {
        std::vector<Item>& list = column == 0 ? left : right;
        for (size_t i = 0; i < list.size(); ++i) {
            Item item = list[i];
            item.info = SettingInfo(item.name);
            int x = column == 0 ? 20 : 450;
            int y = top + static_cast<int>(i) * rowStep;
            item.row = Box(x, y, columnWidth, rowHeight);
            switch (item.kind) {
                case Kind::Toggle:
                    item.control = Box(x + columnWidth - 56, y + 12, 40, 20);
                    break;
                case Kind::Slider:
                    item.control = Box(x + 190, y, columnWidth - 190 - 70, rowHeight);
                    break;
                case Kind::Segments: {
                    int segmentWidth = std::min(88, (columnWidth - 150) / item.optionCount);
                    int total = segmentWidth * item.optionCount;
                    int start = x + columnWidth - 12 - total;
                    for (int s = 0; s < item.optionCount; ++s) {
                        item.segments[s] = Box(start + s * segmentWidth, y + 7, segmentWidth, 30);
                    }
                    item.control = Box(start, y + 7, total, 30);
                    break;
                }
            }
            g_ui.items.push_back(item);
        }
    }
    int y = top + 8 * rowStep + 4;
    g_ui.pathCard = Box(20, y, 840, 96);
    g_ui.buttons[3] = Box(20 + 840 - 104, y + 52, 92, 30);  // Browse.
    y += 106;
    GatherHiddenOutputs();
    size_t most = std::max(g_ui.hiddenApps.size(), g_ui.hiddenOutputs.size() + (g_ui.otherHidden ? 1 : 0));
    g_ui.capacity = std::clamp<int>(static_cast<int>(most), 3, 6);
    int cardHeight = 48 + g_ui.capacity * 36 + 60;
    g_ui.hiddenCard = Box(20, y, 410, cardHeight);
    g_ui.outputsCard = Box(450, y, 410, cardHeight);
    LayoutHiddenLists();
    y += cardHeight + 14;
    g_ui.buttons[1] = Box(860 - 200, y, 96, 34);    // Save.
    g_ui.buttons[2] = Box(860 - 96, y, 96, 34);     // Cancel.
    g_ui.buttons[6] = Box(20, y, 150, 34);          // Reset to default.
    g_ui.buttons[7] = Box(180, y, 100, 34);         // Export.
    g_ui.buttons[8] = Box(290, y, 100, 34);         // Import.
    g_ui.width = Scale(880);
    g_ui.height = Scale(y + 34 + 20);
    g_ui.tabs[0] = Box(660 - 20, 16, 100, 30);
    g_ui.tabs[1] = Box(660 + 80, 16, 140, 30);
    for (int i = 0; i < 3; ++i) {
        g_ui.guideCards[i] = Box(20, 76 + i * 200, 840, 190);
        g_ui.guideButtons[i] = Box(20 + 840 - 196, 76 + i * 200 + 14, 180, 32);
    }
}

int ReadInt(const std::wstring& name) {
    auto found = g_ui.ints.find(name);
    return found != g_ui.ints.end() ? found->second : headroom::GetSettingInt(name);
}
std::wstring ReadString(const std::wstring& name) {
    auto found = g_ui.strings.find(name);
    return found != g_ui.strings.end() ? found->second : headroom::GetSettingString(name);
}

std::wstring GetText(HWND hwnd) {
    int n = GetWindowTextLengthW(hwnd);
    std::wstring text(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(hwnd, text.data(), n + 1);
    text.resize(static_cast<size_t>(n));
    return text;
}

std::wstring Trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t");
    if (a == std::wstring::npos) return {};
    return s.substr(a, s.find_last_not_of(L" \t") - a + 1);
}

void LoadLists() {
    SetWindowTextW(g_ui.pathEdit, headroom::GetSettingString(L"loudMaxPath").c_str());
}

// The hidden apps are kept as settings; they change as soon as a row is shown or added.
// Hides or shows the System sounds of every output and input.
void SetSystemSoundsHidden(bool hidden) {
    LoadPermanentlyHiddenSources();
    for (const std::wstring& key : SystemSoundsKeys()) {
        if (hidden) {
            if (key == SYSTEM_SOUNDS_KEY) g_permanentlyHiddenSources.insert(key);
        } else {
            g_permanentlyHiddenSources.erase(key);
            g_temporarilyHiddenSources.erase(key);
        }
    }
    SavePermanentlyHiddenSources();
}

void WriteHiddenApps() {
    for (int i = 0; i < 256; ++i) {
        headroom::StoreDelete(headroom::SettingKey(L"hiddenApps[" + std::to_wstring(i) + L"]"));
    }
    int index = 0;
    for (const HiddenApp& app : g_ui.hiddenApps) {
        if (app.system) continue;
        if (index >= 256) break;
        headroom::SetSettingString(L"hiddenApps[" + std::to_wstring(index++) + L"]", app.name);
    }
    headroom::FlushStore();
    LoadSettings();
    if (HWND mixer = g_hWnd.load()) RefreshSourceList(mixer);
}

void SaveAll() {
    for (const Item& item : g_ui.items) {
        switch (item.kind) {
            case Kind::Toggle:
            case Kind::Slider:
                headroom::SetSettingInt(item.name, ReadInt(item.name));
                break;
            case Kind::Segments:
                headroom::SetSettingString(item.name, ReadString(item.name));
                break;
        }
    }
    headroom::SetSettingString(L"loudMaxPath", Trim(GetText(g_ui.pathEdit)));
    headroom::FlushStore();
}

// -- painting ------------------------------------------------------------------
int SliderValue(const Item& item, int x) {
    float inset = SliderThumbWidth() / 2;
    float left = item.control.left + inset, right = item.control.right - inset;
    double t = std::clamp((x - left) / std::max(1.0f, right - left), 0.0f, 1.0f);
    return item.low + static_cast<int>(std::lround(t * (item.high - item.low)));
}

PCWSTR CardInfo(int card) {
    switch (card) {
        case 0:
            return L"The full path to LoudMax64.dll, if the mixer does not find it on its own. Leave it empty to search Equalizer APO's VSTPlugins folder and the usual VST folders. Quotes are not needed.";
        case 1:
            return L"Gives an app its own name or a starting volume. One app per line: the program name (for example Spotify.exe), then a bar, the name to show, a bar and a volume from 0 to 100. Both parts after the app are optional. The first matching line wins.";
        case 2:
            return L"Apps that are never listed in the mixer. Type a program name, for example Spotify.exe, and press Add. Show brings an app back. Apps you hide with 'Hide permanently' in the mixer appear here too. Hiding does not mute the app.";
        default:
            return L"Devices (speakers, headphones, microphones) you hid with Ctrl+right-click in a device list. Show brings one back. 'Show all other' also brings back what was hidden before names were kept (system sounds and the like).";
    }
}

void DrawWrapped(HDC dc, const std::wstring& text, RECT rect, COLORREF color, HFONT font) {
    HGDIOBJ old = SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text.c_str(), -1, &rect, DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
}

void PaintSettings(HWND window) {
    PAINTSTRUCT paint;
    HDC target = BeginPaint(window, &paint);
    RECT client;
    GetClientRect(window, &client);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
    HGDIOBJ old = SelectObject(dc, bitmap);
    int matte = g_paintMatte;
    g_paintMatte = -1;
    FillSolidRect(dc, client, g_theme.background);
    std::optional<Gdiplus::Graphics> graphics;
    if (g_gdiplusToken) {
        graphics.emplace(dc);
        graphics->SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics->SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    }
    Gdiplus::Graphics* g = graphics ? &*graphics : nullptr;
    COLORREF control = MixColor(g_theme.panel, RGB(0, 0, 0), 0.15f);

    DrawLabel(dc, g_ui.page == 0 ? L"Settings" : L"Setup guide", Box(24, 14, 400, 30), g_titleFont,
              g_theme.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawLabel(dc, L"Headroom " HEADROOM_VERSION_TEXT, Box(24, 44, 600, 20), g_smallFont,
              g_theme.secondaryText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    {
        RECT segments[2] = {g_ui.tabs[0], g_ui.tabs[1]};
        PCWSTR labels[2] = {L"Settings", L"Setup guide"};
        DrawEqSegments(dc, segments, 2, labels, g_ui.page, g_ui.hotTab >= 0 ? TAB_BASE + g_ui.hotTab : -1,
                       TAB_BASE, control);
    }

    if (g_ui.page == 1) {
        struct Step { PCWSTR title; PCWSTR text; PCWSTR button; bool found; PCWSTR need; };
        std::wstring apoFolder = EqualizerApoFolder();
        Step steps[3] = {
            {L"1.  Equalizer APO", L"For the equalizer and LoudMax on an output (speakers, headset).\n"
             L"\u2022 Download the installer from the SourceForge page.\n"
             L"\u2022 Run it. In the Configurator that opens, tick the playback devices to use.\n"
             L"\u2022 Restart Windows.\n"
             L"\u2022 Then open Equalizer on that output in the mixer (Ctrl+right-click the master volume).\n"
             L"Free and open source.",
             L"Open download page", !apoFolder.empty(), L"Needed for equalizer and LoudMax per output"},
            {L"2.  VB-CABLE", L"For volume above 100%, and the equalizer, LoudMax and loudness equalization of single apps.\n"
             L"\u2022 Download the driver pack from the VB-Audio page and extract it.\n"
             L"\u2022 Run its setup program as administrator, then restart Windows.\n"
             L"\u2022 Nothing else to set up: the mixer routes an app through this silent virtual output only while it needs to.\n"
             L"Nothing is loaded into your games, so it is safe with anti-cheat. Donationware.",
             L"Open VB-Audio page", !FindFxSink().empty(), L"Needed for per-app boost and effects"},
            {L"3.  LoudMax (optional)", L"A free limiter that keeps loud peaks in check.\n"
             L"\u2022 Download the VST2 package from the author's page and extract it.\n"
             L"\u2022 Copy LoudMax64.dll into Equalizer APO's VSTPlugins folder "
             L"(usually C:\\Program Files\\EqualizerAPO\\VSTPlugins).\n"
             L"\u2022 The mixer finds it by itself. If you keep it elsewhere, enter the path in Settings.\n"
             L"Use these official pages only: other sites that offer the same downloads may add unwanted software.",
             L"Open LoudMax page", !LoudMaxPath().empty(), L"Needed for the LoudMax limiter"},
        };
        for (int i = 0; i < 3; ++i) {
            const RECT& card = g_ui.guideCards[i];
            DrawSurface(dc, card, Radius(16), g_theme.panel);
            RECT head{card.left + Scale(16), card.top + Scale(12), card.right - Scale(216), card.top + Scale(40)};
            DrawLabel(dc, steps[i].title, head, g_titleFont, g_theme.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            RECT status{card.left + Scale(16), card.top + Scale(40), card.right - Scale(216), card.top + Scale(58)};
            DrawLabel(dc, steps[i].found ? L"\u2713  Found on this PC" : L"Not found yet.  " + std::wstring(steps[i].need),
                      status, g_smallFont, steps[i].found ? g_theme.accent : g_theme.secondaryText,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            RECT text{card.left + Scale(16), card.top + Scale(66), card.right - Scale(16), card.bottom - Scale(8)};
            DrawWrapped(dc, steps[i].text, text, g_theme.secondaryText, g_smallFont);
            bool hot = g_ui.hotButton == 20 + i;
            DrawRoundedRect(dc, g_ui.guideButtons[i], Radius(12),
                            hot ? MixColor(g_theme.accent, g_theme.text, 0.1f) : g_theme.accent);
            DrawLabel(dc, steps[i].button, g_ui.guideButtons[i], g_bodyFont,
                      g_lightTheme ? RGB(255, 255, 255) : RGB(0, 0, 0), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        g_paintMatte = matte;
        BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
        EndPaint(window, &paint);
        return;
    }

    for (size_t index = 0; index < g_ui.items.size(); ++index) {
        Item& item = g_ui.items[index];
        bool hot = static_cast<int>(index) == g_ui.hot;
        DrawSurface(dc, item.row, Radius(16), g_theme.panel, hot ? 0.5f : 0.0f);
        int labelWidth = MeasureLabel(dc, item.label, g_bodyFont);
        int limit = (item.kind == Kind::Slider ? item.control.left : item.control.left) - Scale(30);
        int textRight = std::min<int>(item.row.left + Scale(16) + labelWidth + Scale(4), limit);
        RECT label{item.row.left + Scale(16), item.row.top, textRight, item.row.bottom};
        DrawLabel(dc, item.label, label, g_bodyFont, g_theme.text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        int middle = (item.row.top + item.row.bottom) / 2;
        item.infoRect = {textRight + Scale(2), middle - Scale(9), textRight + Scale(20), middle + Scale(9)};
        DrawInfoMark(dc, item.infoRect, g_ui.hotInfo == static_cast<int>(index));
        if (item.kind == Kind::Toggle) {
            DrawEqToggle(dc, g, item.control, ReadInt(item.name) != 0, hot);
        } else if (item.kind == Kind::Segments) {
            std::wstring value = ReadString(item.name);
            PCWSTR labels[6];
            int chosen = 0;
            for (int i = 0; i < item.optionCount; ++i) {
                labels[i] = item.options[i].label;
                if (value == item.options[i].value) chosen = i;
            }
            int lit = hot && g_ui.hotSegment >= 0
                          ? ITEM_BASE + static_cast<int>(index) * 10 + g_ui.hotSegment : -1;
            DrawEqSegments(dc, item.segments, item.optionCount, labels, chosen, lit,
                           ITEM_BASE + static_cast<int>(index) * 10, control);
        } else {
            int value = ReadInt(item.name);
            float inset = SliderThumbWidth() / 2;
            float left = item.control.left + inset, right = item.control.right - inset;
            float x = left + (right - left) * (value - item.low) / std::max(1, item.high - item.low);
            float y = (item.control.top + item.control.bottom) / 2.0f;
            DrawEqHSlider(dc, g, left, right, y, x,
                          g_ui.drag == static_cast<int>(index) ? ThumbState::Pressed
                          : hot                                ? ThumbState::Hover
                                                               : ThumbState::Rest, true);
            RECT text{item.control.right + Scale(8), item.row.top, item.row.right - Scale(12), item.row.bottom};
            DrawLabel(dc, std::to_wstring(value) + item.unit, text, g_bodyFont, g_theme.text,
                      DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
    }

    auto card = [&](int number, const RECT& rect, PCWSTR title, PCWSTR hint) {
        DrawSurface(dc, rect, Radius(16), g_theme.panel);
        int titleWidth = MeasureLabel(dc, title, g_bodyFont);
        RECT head{rect.left + Scale(16), rect.top + Scale(8), rect.left + Scale(16) + titleWidth + Scale(4),
                  rect.top + Scale(30)};
        DrawLabel(dc, title, head, g_bodyFont, g_theme.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        g_ui.cardInfo[number] = {head.right + Scale(2), head.top + Scale(2), head.right + Scale(20), head.bottom - Scale(2)};
        DrawInfoMark(dc, g_ui.cardInfo[number], g_ui.hotInfo == 100 + number);
        if (hint && *hint) {
            RECT sub{rect.left + Scale(16), rect.top + Scale(30), rect.right - Scale(16), rect.top + Scale(48)};
            DrawLabel(dc, hint, sub, g_smallFont, g_theme.secondaryText,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    };
    card(0, g_ui.pathCard, L"LoudMax path", L"Full path to LoudMax64.dll. Empty: the mixer looks in Equalizer APO's folders.");
    card(2, g_ui.hiddenCard, L"Hidden apps", L"Never listed in the mixer");
    card(3, g_ui.outputsCard, L"Hidden devices", L"Left out of the device lists");
    // The boxes the text is typed into.
    {
        RECT field = g_ui.pathCard;
        field.left += Scale(10);
        field.right -= Scale(10) + Scale(108);
        field.bottom -= Scale(10);
        field.top += Scale(52);
        DrawRoundedRect(dc, field, Radius(10), control);
        DrawRoundedRect(dc, g_ui.addField, Radius(10), control);
    }

    // The two lists: one pill per entry, each with a button to bring it back.
    auto pill = [&](const RECT& cardRect, int index) {
        int top = cardRect.top + Scale(48) + index * Scale(36);
        return RECT{cardRect.left + Scale(10), top, cardRect.right - Scale(10), top + Scale(34)};
    };
    auto showButton = [&](const RECT& show, bool hot) {
        DrawSurface(dc, show, Radius(10), g_theme.panel, hot ? 1.0f : 0.0f);
        DrawLabel(dc, L"Show", show, g_smallFont, g_theme.text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    };
    if (g_ui.hiddenApps.empty()) {
        DrawLabel(dc, L"No hidden apps.", pill(g_ui.hiddenCard, 0), g_smallFont, g_theme.secondaryText,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    for (size_t i = static_cast<size_t>(g_ui.scroll[0]);
         i < g_ui.hiddenApps.size() && static_cast<int>(i) < g_ui.scroll[0] + g_ui.capacity; ++i) {
        RECT row = pill(g_ui.hiddenCard, static_cast<int>(i) - g_ui.scroll[0]);
        DrawRoundedRect(dc, row, Radius(10), control);
        RECT name{row.left + Scale(12), row.top, g_ui.hiddenApps[i].show.left - Scale(8), row.bottom};
        DrawLabel(dc, g_ui.hiddenApps[i].name, name, g_bodyFont, g_theme.text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        showButton(g_ui.hiddenApps[i].show, g_ui.hotButton == 30 + static_cast<int>(i));
    }
    if (g_ui.hiddenOutputs.empty()) {
        DrawLabel(dc, L"No hidden devices.", pill(g_ui.outputsCard, 0), g_smallFont, g_theme.secondaryText,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    for (size_t i = static_cast<size_t>(g_ui.scroll[1]);
         i < g_ui.hiddenOutputs.size() && static_cast<int>(i) < g_ui.scroll[1] + g_ui.capacity; ++i) {
        RECT row = pill(g_ui.outputsCard, static_cast<int>(i) - g_ui.scroll[1]);
        DrawRoundedRect(dc, row, Radius(10), control);
        RECT name{row.left + Scale(12), row.top, g_ui.hiddenOutputs[i].show.left - Scale(8), row.bottom};
        DrawLabel(dc, g_ui.hiddenOutputs[i].name, name, g_bodyFont, g_theme.text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        showButton(g_ui.hiddenOutputs[i].show, g_ui.hotButton == 10 + static_cast<int>(i));
    }
    // A scroll bar for a list with more rows than fit.
    for (int list = 0; list < 2; ++list) {
        if (MaxScroll(list) <= 0) continue;
        RECT track = ScrollTrack(list), thumb = ScrollThumb(list);
        DrawRoundedRect(dc, track, Scale(4), control);
        DrawRoundedRect(dc, thumb, Scale(4), g_ui.scrollDrag == list ? g_theme.accent : g_theme.secondaryText);
    }
    if (g_ui.otherHidden) {
        RECT note{g_ui.outputsCard.left + Scale(16), g_ui.buttons[4].top, g_ui.buttons[4].left - Scale(8),
                  g_ui.buttons[4].bottom};
        DrawLabel(dc, std::to_wstring(g_ui.otherHidden) + L" more hidden without a name", note, g_smallFont,
                  g_theme.secondaryText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        DrawSurface(dc, g_ui.buttons[4], Radius(12), control, g_ui.hotButton == 4 ? 1.0f : 0.0f);
        DrawLabel(dc, L"Show all", g_ui.buttons[4], g_bodyFont, g_theme.text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    {
        bool hot = g_ui.hotButton == 5;
        DrawRoundedRect(dc, g_ui.buttons[5], Radius(12),
                        hot ? MixColor(g_theme.accent, g_theme.text, 0.1f) : g_theme.accent);
        DrawLabel(dc, L"Add", g_ui.buttons[5], g_bodyFont, g_lightTheme ? RGB(255, 255, 255) : RGB(0, 0, 0),
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    auto button = [&](int index, PCWSTR text, bool primary) {
        const RECT& rect = g_ui.buttons[index];
        bool hot = g_ui.hotButton == index;
        if (primary) {
            DrawRoundedRect(dc, rect, Radius(12), hot ? MixColor(g_theme.accent, g_theme.text, 0.1f) : g_theme.accent);
            DrawLabel(dc, text, rect, g_bodyFont, g_lightTheme ? RGB(255, 255, 255) : RGB(0, 0, 0),
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            DrawSurface(dc, rect, Radius(12), index == 3 ? control : g_theme.panel, hot ? 1.0f : 0.0f);
            DrawLabel(dc, text, rect, g_bodyFont, g_theme.text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    };
    button(1, L"Save", true);
    button(2, L"Cancel", false);
    button(3, L"Browse\u2026", false);
    button(6, L"Reset to default", false);
    button(7, L"Export\u2026", false);
    button(8, L"Import\u2026", false);

    g_paintMatte = matte;
    BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    EndPaint(window, &paint);
}

// What is under the point.
struct Hit {
    int item = -1, segment = -1, button = -1, info = -1, tab = -1;
};

Hit HitTest(POINT point) {
    Hit hit;
    for (int i = 0; i < 2; ++i) {
        if (PtInRect(&g_ui.tabs[i], point)) hit.tab = i;
    }
    if (g_ui.page == 1) {
        for (int i = 0; i < 3; ++i) {
            if (PtInRect(&g_ui.guideButtons[i], point)) hit.button = 20 + i;
        }
        return hit;
    }
    for (int i = 0; i < 9; ++i) {
        if (i == 4 && !g_ui.otherHidden) continue;
        if (PtInRect(&g_ui.buttons[i], point)) hit.button = i;
    }
    for (size_t i = 0; i < g_ui.hiddenApps.size(); ++i) {
        if (PtInRect(&g_ui.hiddenApps[i].show, point)) hit.button = 30 + static_cast<int>(i);
    }
    for (size_t i = 0; i < g_ui.hiddenOutputs.size(); ++i) {
        if (PtInRect(&g_ui.hiddenOutputs[i].show, point)) hit.button = 10 + static_cast<int>(i);
    }
    for (int i = 0; i < 4; ++i) {
        if (PtInRect(&g_ui.cardInfo[i], point)) hit.info = 100 + i;
    }
    for (size_t i = 0; i < g_ui.items.size(); ++i) {
        const Item& it = g_ui.items[i];
        if (PtInRect(&it.infoRect, point)) hit.info = static_cast<int>(i);
        if (!PtInRect(&it.row, point)) continue;
        hit.item = static_cast<int>(i);
        if (it.kind == Kind::Segments) {
            for (int s = 0; s < it.optionCount; ++s) {
                if (PtInRect(&it.segments[s], point)) hit.segment = s;
            }
        }
        break;
    }
    return hit;
}

std::string ReadWholeFile(const std::wstring& path) {
    std::string data;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return data;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(f, &size) && size.QuadPart < 64ll * 1024 * 1024) {
        data.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        if (!ReadFile(f, data.data(), static_cast<DWORD>(data.size()), &read, nullptr)) read = 0;
        data.resize(read);
    }
    CloseHandle(f);
    return data;
}

// Everything the app keeps (settings, equalizers, loudness, hidden lists ...) in one file.
void RestartApp();

// A copy of everything, made before a reset or an import replaces it: the last way back.
void KeepBackupBeforeReplacing() {
    headroom::MarkDirty();
    headroom::FlushStore();
    CopyFileW(headroom::StorePath().c_str(), (headroom::DataDir() + L"\\settings.before-replace.cfg").c_str(), FALSE);
}

void ExportSettings(HWND owner) {
    WCHAR file[MAX_PATH * 2] = L"Headroom settings.cfg";
    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Headroom settings (*.cfg)\0*.cfg\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = ARRAYSIZE(file);
    ofn.lpstrDefExt = L"cfg";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;
    SaveAll();  // What is typed in the window goes along.
    headroom::MarkDirty();
    headroom::FlushStore();
    if (!CopyFileW(headroom::StorePath().c_str(), file, FALSE)) {
        MessageBoxW(owner, L"The file could not be written.", L"Headroom", MB_OK | MB_ICONWARNING);
    }
}

void ImportSettings(HWND owner) {
    WCHAR file[MAX_PATH * 2] = {};
    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Headroom settings (*.cfg)\0*.cfg\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = ARRAYSIZE(file);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    std::string data = ReadWholeFile(file);
    if (!headroom::LooksLikeStoreData(data)) {
        MessageBoxW(owner, L"This is not a Headroom settings file.", L"Headroom", MB_OK | MB_ICONWARNING);
        return;
    }
    if (MessageBoxW(owner,
                    L"Importing replaces all your settings, equalizers, loudness settings and hidden lists with "
                    L"the ones in the file.\n\nHeadroom restarts afterwards. Continue?",
                    L"Headroom", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }
    KeepBackupBeforeReplacing();
    headroom::ReplaceStore(data);
    RestartApp();
}

// Everything back to a fresh install: asks first, since it cannot be undone.
void ResetEverything(HWND owner) {
    if (MessageBoxW(owner,
                    L"Reset everything to default?\n\n"
                    L"All settings, equalizers, loudness settings, surround channels, start volumes, names and "
                    L"hidden lists are deleted. A copy is kept as settings.before-replace.cfg in the settings folder, until the next reset or import. Use Export to keep it for good.\n\n"
                    L"Headroom restarts afterwards.",
                    L"Reset to default", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
        return;
    }
    KeepBackupBeforeReplacing();
    headroom::ReplaceStore("");
    RestartApp();
}

void RestartApp() {
    // The new copy waits for this one to end, then starts with the stored settings.
    std::wstring command = L"\"" + ExePath() + L"\" --restart";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    if (g_ui.window) DestroyWindow(g_ui.window);
    if (HWND mixer = g_hWnd.load()) PostMessageW(mixer, WM_CLOSE, 0, 0);
}

void ShowPage(int page) {
    g_ui.page = page;
    HideInfoTip();
    g_ui.hotInfo = -1;
    for (HWND edit : {g_ui.pathEdit, g_ui.addEdit}) {
        if (edit) ShowWindow(edit, page == 0 ? SW_SHOW : SW_HIDE);
    }
    InvalidateRect(g_ui.window, nullptr, FALSE);
}

void UpdateInfoTip(int info) {
    if (info == g_ui.hotInfo) return;
    g_ui.hotInfo = info;
    InvalidateRect(g_ui.window, nullptr, FALSE);
    if (info < 0) {
        HideInfoTip();
        return;
    }
    RECT anchor = info >= 100 ? g_ui.cardInfo[info - 100] : g_ui.items[static_cast<size_t>(info)].infoRect;
    MapWindowPoints(g_ui.window, nullptr, reinterpret_cast<POINT*>(&anchor), 2);
    ShowInfoTip(g_ui.window, anchor, info >= 100 ? CardInfo(info - 100) : g_ui.items[static_cast<size_t>(info)].info);
}

// Adds the program name typed under "Hidden apps".
void AddHiddenApp() {
    std::wstring name = Trim(GetText(g_ui.addEdit));
    if (name.empty() || g_ui.hiddenApps.size() >= 256) return;
    bool known = false;
    for (const HiddenApp& app : g_ui.hiddenApps) {
        if (_wcsicmp(app.name.c_str(), name.c_str()) == 0) known = true;
    }
    if (_wcsicmp(name.c_str(), L"system sounds") == 0) {
        // The group of System sounds rows, not an app of that name.
        SetSystemSoundsHidden(true);
        known = std::any_of(g_ui.hiddenApps.begin(), g_ui.hiddenApps.end(), [](const HiddenApp& app) { return app.system; });
        if (!known) g_ui.hiddenApps.push_back({L"System sounds", {}, true});
        known = true;
    }
    if (!known) {
        g_ui.hiddenApps.push_back({name, {}});
        g_ui.scroll[0] = MaxScroll(0);  // Shows the new one.
    }
    SetWindowTextW(g_ui.addEdit, L"");
    WriteHiddenApps();
    LayoutHiddenLists();
    InvalidateRect(g_ui.window, nullptr, FALSE);
}

void DragSlider(int index, int x) {
    const Item& item = g_ui.items[static_cast<size_t>(index)];
    g_ui.ints[item.name] = SliderValue(item, x);
    InvalidateRect(g_ui.window, nullptr, FALSE);
}

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            PaintSettings(hwnd);
            return 0;
        case WM_CTLCOLOREDIT: {
            static HBRUSH brush = nullptr;
            static COLORREF made = 0;
            COLORREF color = MixColor(g_theme.panel, RGB(0, 0, 0), 0.15f);
            if (!brush || made != color) {
                if (brush) DeleteObject(brush);
                brush = CreateSolidBrush(color);
                made = color;
            }
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, g_theme.text);
            SetBkColor(dc, color);
            return reinterpret_cast<LRESULT>(brush);
        }
        case WM_MOUSEWHEEL: {
            POINT cursor{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &cursor);
            if (g_ui.page != 0) return 0;
            for (int list = 0; list < 2; ++list) {
                if (!PtInRect(&ListCard(list), cursor) || MaxScroll(list) <= 0) continue;
                int step = GET_WHEEL_DELTA_WPARAM(wParam) < 0 ? 1 : -1;
                g_ui.scroll[list] = std::clamp(g_ui.scroll[list] + step, 0, MaxScroll(list));
                LayoutHiddenLists();
                InvalidateRect(hwnd, nullptr, FALSE);
                break;
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (g_ui.scrollDrag >= 0) {
                int list = g_ui.scrollDrag;
                RECT track = ScrollTrack(list), thumb = ScrollThumb(list);
                int room = std::max<LONG>(1, (track.bottom - track.top) - (thumb.bottom - thumb.top));
                int top = point.y - g_ui.scrollGrab - track.top;
                g_ui.scroll[list] = std::clamp(
                    static_cast<int>(std::lround(static_cast<double>(top) * MaxScroll(list) / room)), 0,
                    MaxScroll(list));
                LayoutHiddenLists();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (g_ui.drag >= 0) {
                DragSlider(g_ui.drag, point.x);
                return 0;
            }
            Hit hit = HitTest(point);
            if (hit.item != g_ui.hot || hit.segment != g_ui.hotSegment || hit.button != g_ui.hotButton ||
                hit.tab != g_ui.hotTab) {
                g_ui.hot = hit.item;
                g_ui.hotSegment = hit.segment;
                g_ui.hotButton = hit.button;
                g_ui.hotTab = hit.tab;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            UpdateInfoTip(hit.info);
            if (!g_ui.tracking) {
                TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
                g_ui.tracking = TrackMouseEvent(&track) != FALSE;
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            g_ui.tracking = false;
            g_ui.hot = g_ui.hotSegment = g_ui.hotButton = g_ui.hotTab = -1;
            UpdateInfoTip(-1);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN: {
            HideInfoTip();
            g_ui.hotInfo = -1;
            Hit hit = HitTest(point);
            bool onScroll = false;
            if (g_ui.page == 0) {
                for (int list = 0; list < 2 && !onScroll; ++list) {
                    if (MaxScroll(list) <= 0) continue;
                    RECT track = ScrollTrack(list);
                    track.left -= Scale(4);
                    track.right += Scale(4);
                    if (!PtInRect(&track, point)) continue;
                    onScroll = true;
                    RECT thumb = ScrollThumb(list);
                    // A click on the track takes the thumb there; a click on the thumb holds it where it is.
                    g_ui.scrollGrab = point.y >= thumb.top && point.y < thumb.bottom
                                          ? point.y - thumb.top
                                          : (thumb.bottom - thumb.top) / 2;
                    g_ui.scrollDrag = list;
                    SetCapture(hwnd);
                    SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(point.x, point.y));
                }
            }
            if (onScroll) {
                // Handled above.
            } else if (hit.tab >= 0) {
                ShowPage(hit.tab);
            } else if (hit.button >= 30) {
                size_t index = static_cast<size_t>(hit.button - 30);
                if (index < g_ui.hiddenApps.size()) {
                    if (g_ui.hiddenApps[index].system) SetSystemSoundsHidden(false);
                    g_ui.hiddenApps.erase(g_ui.hiddenApps.begin() + static_cast<std::ptrdiff_t>(index));
                    WriteHiddenApps();
                    LayoutHiddenLists();
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            } else if (hit.button == 5) {
                AddHiddenApp();
            } else if (hit.button >= 20) {
                static constexpr PCWSTR pages[3] = {L"https://sourceforge.net/projects/equalizerapo/",
                                                    L"https://vb-audio.com/Cable/",
                                                    L"https://loudmax.blogspot.com/"};
                ShellExecuteW(hwnd, L"open", pages[hit.button - 20], nullptr, nullptr, SW_SHOWNORMAL);
            } else if (hit.button >= 10) {
                size_t index = static_cast<size_t>(hit.button - 10);
                if (index < g_ui.hiddenOutputs.size()) {
                    g_permanentlyHiddenSources.erase(L"device:" + g_ui.hiddenOutputs[index].id);
                    SavePermanentlyHiddenSources();
                    g_ui.hiddenOutputs.erase(g_ui.hiddenOutputs.begin() + static_cast<std::ptrdiff_t>(index));
                    LayoutHiddenLists();  // The rows move up; the card keeps its size.
                    if (HWND mixer = g_hWnd.load()) RefreshSourceList(mixer);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            } else if (hit.button == 4) {
                for (auto it = g_permanentlyHiddenSources.begin(); it != g_permanentlyHiddenSources.end();) {
                    it = it->rfind(L"device:", 0) == 0 ? std::next(it) : g_permanentlyHiddenSources.erase(it);
                }
                SavePermanentlyHiddenSources();
                g_ui.otherHidden = 0;
                if (HWND mixer = g_hWnd.load()) RefreshSourceList(mixer);
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (hit.button == 1) {
                SaveAll();
                WhTool_ModSettingsChanged();
                DestroyWindow(hwnd);
            } else if (hit.button == 2) {
                DestroyWindow(hwnd);
            } else if (hit.button == 6) {
                ResetEverything(hwnd);
            } else if (hit.button == 7) {
                ExportSettings(hwnd);
            } else if (hit.button == 8) {
                ImportSettings(hwnd);
            } else if (hit.button == 3) {
                WCHAR file[MAX_PATH * 2] = {};
                OPENFILENAMEW ofn = {sizeof(ofn)};
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = L"VST plugin (*.dll)\0*.dll\0All files\0*.*\0";
                ofn.lpstrFile = file;
                ofn.nMaxFile = ARRAYSIZE(file);
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                if (GetOpenFileNameW(&ofn)) SetWindowTextW(g_ui.pathEdit, file);
            } else if (hit.item >= 0 && g_ui.page == 0) {
                const Item& it = g_ui.items[static_cast<size_t>(hit.item)];
                if (it.kind == Kind::Toggle && !PtInRect(&it.infoRect, point)) {
                    g_ui.ints[it.name] = ReadInt(it.name) ? 0 : 1;
                } else if (it.kind == Kind::Segments && hit.segment >= 0) {
                    g_ui.strings[it.name] = it.options[hit.segment].value;
                } else if (it.kind == Kind::Slider && PtInRect(&it.control, point)) {
                    g_ui.drag = hit.item;
                    SetCapture(hwnd);
                    DragSlider(hit.item, point.x);
                }
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONUP:
            if (g_ui.scrollDrag >= 0) {
                g_ui.scrollDrag = -1;
                ReleaseCapture();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            if (g_ui.drag >= 0) {
                g_ui.drag = -1;
                ReleaseCapture();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDCANCEL) DestroyWindow(hwnd);  // Esc.
            else if (LOWORD(wParam) == IDOK && GetFocus() == g_ui.addEdit) AddHiddenApp();  // Enter.
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            HideInfoTip();
            if (g_ui.bigIcon) DestroyIcon(g_ui.bigIcon);
            if (g_ui.smallIcon) DestroyIcon(g_ui.smallIcon);
            g_ui.bigIcon = g_ui.smallIcon = nullptr;
            g_ui.window = nullptr;
            g_ui.page = 0;
            g_ui.ints.clear();
            g_ui.strings.clear();
            g_ui.hot = g_ui.hotSegment = g_ui.hotButton = g_ui.hotInfo = g_ui.hotTab = g_ui.drag = -1;
            return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

// A one-line text box inside `field` (the dark rounded box behind it is painted by the window).
HWND MakeEdit(HWND parent, const RECT& field, PCWSTR hint) {
    HWND edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                field.left + Scale(10), field.top + Scale(7),
                                field.right - field.left - Scale(20), field.bottom - field.top - Scale(14),
                                parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (edit) {
        SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(g_bodyFont), TRUE);
        SetWindowTheme(edit, g_lightTheme ? L"Explorer" : L"DarkMode_Explorer", nullptr);
        if (hint) SendMessageW(edit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(hint));
    }
    return edit;
}

void ShowSettingsWindow() {
    if (g_ui.window) {
        ShowWindow(g_ui.window, SW_RESTORE);
        SetForegroundWindow(g_ui.window);
        return;
    }
    static bool registered = false;
    if (!registered) {
        INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&icc);
        WNDCLASSEXW wc = {sizeof(wc)};
        wc.lpfnWndProc = SettingsProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = SETTINGS_CLASS;
        RegisterClassExW(&wc);
        registered = true;
    }
    BuildItems();

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    RECT rc = {0, 0, g_ui.width, g_ui.height};
    AdjustWindowRectExForDpi(&rc, style, FALSE, 0, g_dpi);
    // The window lives at the system's scale, the one the mixer's fonts and sizes were made for (the primary
    // monitor's). On a monitor with another scale, Windows stretches the whole window evenly instead of
    // leaving the layout half scaled.
    DPI_AWARENESS_CONTEXT previousAwareness = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    HMONITOR monitor = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info = {sizeof(info)};
    GetMonitorInfoW(monitor, &info);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    int x = info.rcWork.left + (info.rcWork.right - info.rcWork.left - w) / 2;
    int y = std::max<int>(info.rcWork.top, info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - h) / 2);

    g_ui.window = CreateWindowExW(0, SETTINGS_CLASS, L"Headroom settings", style, x, y, w, h,
                                  nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!g_ui.window) {
        Wh_Log(L"Settings window failed, error %lu", GetLastError());
        SetThreadDpiAwarenessContext(previousAwareness);
        return;
    }
    // The title bar and the taskbar button get an icon of their own size, so
    // they stay sharp.
    COLORREF ink = g_lightTheme ? RGB(30, 32, 36) : RGB(248, 249, 252);
    // The program icon (the H) comes from the executable; the speaker is the fallback.
    int bigSize = GetSystemMetricsForDpi(SM_CXICON, g_dpi), smallSize = GetSystemMetricsForDpi(SM_CXSMICON, g_dpi);
    HINSTANCE self = GetModuleHandleW(nullptr);
    g_ui.bigIcon = static_cast<HICON>(LoadImageW(self, MAKEINTRESOURCEW(1), IMAGE_ICON, bigSize, bigSize, 0));
    g_ui.smallIcon = static_cast<HICON>(LoadImageW(self, MAKEINTRESOURCEW(1), IMAGE_ICON, smallSize, smallSize, 0));
    if (!g_ui.bigIcon) g_ui.bigIcon = CreateGlyphIcon(GetSystemMetricsForDpi(SM_CXICON, g_dpi), L"\uE767", ink);
    if (!g_ui.smallIcon) g_ui.smallIcon = CreateGlyphIcon(GetSystemMetricsForDpi(SM_CXSMICON, g_dpi), L"\uE767", ink);
    if (g_ui.bigIcon) SendMessageW(g_ui.window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(g_ui.bigIcon));
    if (g_ui.smallIcon) SendMessageW(g_ui.window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(g_ui.smallIcon));
    BOOL dark = !g_lightTheme;
    DwmSetWindowAttribute(g_ui.window, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));

    RECT pathField = g_ui.pathCard;
    pathField.left += Scale(10);
    pathField.right -= Scale(10) + Scale(108);
    pathField.bottom -= Scale(10);
    pathField.top += Scale(52);
    g_ui.pathEdit = MakeEdit(g_ui.window, pathField, nullptr);
    g_ui.addEdit = MakeEdit(g_ui.window, g_ui.addField, L"Hide another app, for example Spotify.exe");
    LoadLists();
    SetThreadDpiAwarenessContext(previousAwareness);
    ShowWindow(g_ui.window, SW_SHOW);
    SetForegroundWindow(g_ui.window);
}

// ---------------------------------------------------------------------------
// Hooks into the mixer (declared in headroom.cpp)
// ---------------------------------------------------------------------------
constexpr UINT MENU_APP_RESTORE_APPS = 25;

int HiddenAppCount() {
    int n = 0;
    for (int i = 0; i < 256; ++i) {
        if (!headroom::GetSettingString(L"hiddenApps[" + std::to_wstring(i) + L"]").empty()) ++n;
    }
    return n;
}

void AppExtendTrayMenu(std::vector<OutputDevice>& items, std::vector<UINT>& commands) {
    static std::wstring restoreApps;
    auto add = [&](UINT command, PCWSTR text, PCWSTR glyph) {
        items.push_back({L"", text, glyph, false});
        commands.push_back(command);
    };
    if (int hidden = HiddenAppCount()) {
        restoreApps = L"Restore hidden apps (" + std::to_wstring(hidden) + L")";
        add(MENU_APP_RESTORE_APPS, restoreApps.c_str(), L"\uE890");
    }
    add(MENU_APP_SETTINGS, L"Settings", L"\uE713");
    add(MENU_APP_AUTOSTART, L"Start with Windows", AutostartEnabled() ? L"\uE73E" : L"\uE7E8");
    add(MENU_APP_LOG, L"Open log folder", L"\uE838");
    add(MENU_APP_ABOUT, L"About", L"\uE946");
    add(MENU_APP_EXIT, L"Exit", L"\uE711");
}

bool AppHideByName(const std::wstring& exe) {
    for (int i = 0; i < 256; ++i) {
        std::wstring name = L"hiddenApps[" + std::to_wstring(i) + L"]";
        std::wstring current = headroom::GetSettingString(name);
        if (_wcsicmp(current.c_str(), exe.c_str()) == 0) return true;
    }
    for (int i = 0; i < 256; ++i) {
        std::wstring name = L"hiddenApps[" + std::to_wstring(i) + L"]";
        if (headroom::GetSettingString(name).empty()) {
            headroom::SetSettingString(name, exe);
            headroom::FlushStore();
            LoadSettings();
            Wh_Log(L"Hidden app added to the settings: %s", exe.c_str());
            return true;
        }
    }
    return false;
}

bool AppHandleTrayCommand(HWND hWnd, UINT command) {
    switch (command) {
        case MENU_APP_SETTINGS:
            ShowSettingsWindow();
            return true;
        case MENU_APP_AUTOSTART:
            SetAutostart(!AutostartEnabled());
            return true;
        case MENU_APP_RESTORE_APPS:
            for (int i = 0; i < 256; ++i) {
                headroom::StoreDelete(headroom::SettingKey(L"hiddenApps[" + std::to_wstring(i) + L"]"));
            }
            headroom::FlushStore();
            LoadSettings();
            RefreshSourceList(hWnd);
            return true;
        case MENU_APP_LOG:
            ShellExecuteW(nullptr, L"open", headroom::LogDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return true;
        case MENU_APP_ABOUT:
            MessageBoxW(hWnd,
                        L"Headroom " HEADROOM_VERSION_TEXT
                        L"\n\nCopyright (c) Jakob Hüttner\n"
                        L"Portions derived from the Windhawk mod \"Better Volume Mixer\" by 0Allu (MIT).\n\n"
                        L"Settings: %APPDATA%\\Headroom\\settings.cfg\n"
                        L"Log: %LOCALAPPDATA%\\Headroom\\log.txt",
                        L"About Headroom", MB_OK | MB_ICONINFORMATION);
            return true;
        case MENU_APP_EXIT:
            if (g_ui.window) DestroyWindow(g_ui.window);
            PostMessageW(hWnd, WM_CLOSE, 0, 0);
            return true;
    }
    return false;
}

bool AppIsDialogMessage(MSG& message) {
    return g_ui.window && IsDialogMessageW(g_ui.window, &message);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR commandLine, int) {
    // After an import the new copy waits (up to 20 s) for the old one to end.
    int waits = commandLine && wcsstr(commandLine, L"--restart") ? 100 : 0;
    HANDLE instance = CreateMutexW(nullptr, TRUE, L"Local\\Headroom.Standalone");
    while (instance && GetLastError() == ERROR_ALREADY_EXISTS && waits-- > 0) {
        CloseHandle(instance);
        Sleep(200);
        instance = CreateMutexW(nullptr, TRUE, L"Local\\Headroom.Standalone");
    }
    if (instance && GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    headroom::InitLog();
    headroom::InitStore();
    Wh_Log(L"Headroom %s starting", HEADROOM_VERSION_TEXT);
    MigrateAutostart();

    // The Windhawk mod and this app would both put a tray icon and hotkeys up.
    while (WindhawkModRunning()) {
        int answer = MessageBoxW(
            nullptr,
            L"The Windhawk mod \"Better Volume Mixer\" is still running.\n\n"
            L"Turn it off in Windhawk (disable the mod), then click Retry.",
            L"Headroom", MB_RETRYCANCEL | MB_ICONWARNING);
        if (answer != IDRETRY) return 0;
    }

    if (!headroom::Store().existedAtStart) {
        ImportFromWindhawk();
        headroom::FlushStore();
    }

    if (!WhTool_ModInit()) {
        Wh_Log(L"Startup failed");
        headroom::ShutdownStore();
        return 1;
    }
    if (g_thread) WaitForSingleObject(g_thread, INFINITE);
    Wh_Log(L"Exiting");
    headroom::ShutdownStore();
    return 0;
}
