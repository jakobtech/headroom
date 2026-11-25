// Headroom: the mixer, the equalizer and the audio engine.
//
// Copyright (c) Jakob Hüttner. Portions derived from the Windhawk mod
// "Better Volume Mixer" by 0Allu (MIT), see LICENSE.
//
// The block below lists the settings that the mixer reads (name, type, default).

// ==WindhawkModSettings==
/*
- compactMode: false
  $name: Compact mode
  $description: Use a smaller, denser layout with shorter rows and a narrower window, so more apps fit at once. Off by default.
- theme: system
  $name: Theme
  $description: Choose the mixer appearance or follow the current Windows theme.
  $options:
  - system: Follow Windows
  - dark: Dark
  - light: Light
- visualStyle: windows11
  $name: Visual style
  $description: Windows 11 has rounded corners and the Windows 11 slider. Windows 10 has square corners, neutral gray colors, the flat Windows 10 slider with its bar-shaped handle, and no gap above the taskbar. Either style works with every theme.
  $options:
  - windows11: Windows 11
  - windows10: Windows 10
- backgroundOpacity: 85
  $name: Background opacity
  $description: Background opacity from 0 to 100 percent. Text and controls remain opaque. Blur is applied where supported by Windows.
- backgroundEffect: blur
  $name: Background effect
  $description: What shows through the mixer while background opacity is below 100. Frosted glass blurs the background and evens out its brightness, so text stays readable over anything; lower the background opacity to make it more glassy. Blur only blurs it, and bright backgrounds can wash out the text at low opacity.
  $options:
  - glass: Frosted glass
  - blur: Blur
  - none: No blur
- windowBorder: true
  $name: Window border
  $description: Show the thin outline that Windows draws around the mixer. Turn off for a borderless look.
- volumeStep: 5
  $name: Mouse-wheel volume step
  $description: Percentage points per mouse-wheel step over an app or master-volume row, or over the tray icon. Valid range is 1 to 20; default is 5.
- eqRangeCustom: false
  $name: Extended equalizer range
  $description: Lets the equalizer go beyond 12 dB. The most is set below.
- eqRangeDb: 24
  $name: Equalizer range
  $description: The most the equalizer bands can raise or lower, in dB, while the extended range is on. 12 to 60.
- keyboardControls: false
  $name: Keyboard controls
  $description: Up/Down selects a row, Left/Right changes volume by the mouse-wheel step, and M toggles mute. Works while the mixer is focused. Off by default.
- exactVolumeEntry: false
  $name: Exact volume entry
  $description: Click a percentage to enter a whole number from 0 to 100. Enter applies it; Escape or clicking elsewhere cancels. Off by default.
- animations: true
  $name: Enable animations
  $description: Animate opening and closing, hover effects, and volume changes. The mixer opens and closes like the taskbar flyouts of the chosen visual style. The Windows animation preference is also respected.
- closeWhenFocusIsLost: true
  $name: Close when focus is lost
  $description: Automatically close the mixer when you click somewhere else.
- visibleRows: auto
  $name: Visible rows
  $description: How many app and device rows are shown at once before the list scrolls. Master volume does not count. "Fill the screen" uses all the height the screen has.
  $options:
  - auto: Fill the screen
  - "5": "5"
  - "7": "7"
  - "10": "10"
  - "15": "15"
  - "20": "20"
- loudMaxPath: ""
  $name: LoudMax path
  $description: Full path to LoudMax64.dll, for example C:\VST\LoudMax64.dll. Leave empty to look for it in Equalizer APO's folders and the usual VST folders.
- switchCommunicationsDevice: true
  $name: Also switch the communications device
  $description: When you pick a playback device in the mixer, also make it the default communications device, like Windows Settings does. Calling apps such as Teams, Zoom or Discord then follow the switch. Turn off to keep the communications device unchanged.
- moveNotifications: true
  $name: Move notifications out of the way
  $description: While the mixer is open, Windows notifications that would cover it are moved aside, and come back when it closes.
- showIdleAppsOnAllOutputs: true
  $name: Show silent apps on every output
  $description: List apps that are not playing anything even when their audio is on an output other than the default one, for example Steam after switching from a headset to speakers. Turn off to list such apps only once they play something, as in the original mod.
- showAllSessions: false
  $name: Show all audio sessions
  $description: Show every audio session separately for each playback device, including unused or idle ones, instead of one row per app. Most users can leave this off.
- customApps:
  - - app: ""
      $name: App name
      $description: Enter the executable name (for example Spotify.exe or Spotify), its full path, or the original name shown in the mixer. Matching ignores capitalization. Leave blank to ignore this entry.
    - displayName: ""
      $name: Custom display name
      $description: Leave blank to keep the original name.
    - defaultVolume: -1
      $name: Default volume percentage
      $description: Use 0 to 100, or -1 to leave volume unchanged. Applied once when a session is detected, even with the mixer closed, and again if you change this default. Mute status is preserved.
  $name: Custom app configuration
  $description: Add an entry for each app to customize. The first matching entry wins. Up to 256 entries are supported.
- hiddenApps: [""]
  $name: Hidden applications
  $description: Add executable names (for example Spotify.exe or Spotify), full paths, or original mixer names to hide. Matching ignores capitalization. Blank entries are ignored. Hiding does not mute the app or disable its configured default volume. Up to 256 entries are supported.
*/
// ==/WindhawkModSettings==

#ifdef HEADROOM_STANDALONE
#include "headroom_platform.h"
#else
#include <windhawk_utils.h>
#endif

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <dwmapi.h>
#include <propsys.h>
#include <objidl.h>
#include <gdiplus.h>
#include <d2d1.h>
#include <dwrite.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cwchar>
#include <cwctype>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// Some Windhawk/MinGW headers only forward-declare this Core Audio interface.
// Define the standard COM method table, in ABI order, when it is missing.
// Keep this at global scope so it completes the header's forward declaration.
#ifndef __IAudioMeterInformation_INTERFACE_DEFINED__
#define __IAudioMeterInformation_INTERFACE_DEFINED__
MIDL_INTERFACE("c02216f6-8c67-4b5b-9d00-d008e73e0064")
IAudioMeterInformation : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetPeakValue(float* peak) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMeteringChannelCount(UINT* count) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetChannelsPeakValues(
        UINT count, float* peaks) = 0;
    virtual HRESULT STDMETHODCALLTYPE QueryHardwareSupport(DWORD* mask) = 0;
};
#ifdef __CRT_UUID_DECL
__CRT_UUID_DECL(IAudioMeterInformation, 0xc02216f6, 0x8c67, 0x4b5b,
                0x9d, 0x00, 0xd0, 0x08, 0xe7, 0x3e, 0x00, 0x64)
#endif
#endif

namespace {

constexpr PROPERTYKEY kDeviceFriendlyName = {
    {0xa45c254e, 0xdf1c, 0x4efd,
     {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}},
    14};
// PKEY_AudioEndpoint_FormFactor.
constexpr PROPERTYKEY kEndpointFormFactor = {
    {0x1da5d803, 0xd492, 0x4edd,
     {0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e}},
    0};

#ifndef NOTIFYICON_VERSION_4
#define NOTIFYICON_VERSION_4 4
#endif
#ifndef NIF_GUID
#define NIF_GUID 0x00000020
#endif
#ifndef NIF_SHOWTIP
#define NIF_SHOWTIP 0x00000080
#endif
#ifndef NIN_SELECT
#define NIN_SELECT (WM_USER + 0)
#endif
#ifndef NINF_KEY
#define NINF_KEY 0x1
#endif
#ifndef NIN_KEYSELECT
#define NIN_KEYSELECT (NIN_SELECT | NINF_KEY)
#endif

constexpr UINT WM_APP_TRAY = WM_APP + 1;
constexpr UINT WM_APP_RELOAD_SETTINGS = WM_APP + 2;
constexpr UINT WM_APP_SHOW_MIXER = WM_APP + 3;
constexpr UINT WM_APP_FINISH_VOLUME_ENTRY = WM_APP + 4;
constexpr UINT WM_APP_AUDIO_CHANGED = WM_APP + 5;
constexpr UINT WM_APP_MASTER_STATE_CHANGED = WM_APP + 6;
constexpr UINT WM_APP_DEFAULT_OUTPUT_CHANGED = WM_APP + 7;
constexpr UINT WM_APP_TRAY_WHEEL = WM_APP + 8;
constexpr UINT_PTR TIMER_METERS = 1;
constexpr UINT_PTR TIMER_REFRESH = 2;
constexpr UINT_PTR TIMER_CHECK_FOCUS = 3;
constexpr UINT_PTR TIMER_NAME_TOOLTIP = 4;
constexpr UINT_PTR TIMER_TRIM = 5;
constexpr UINT_PTR TIMER_TRAY_WATCH = 6;
constexpr UINT_PTR TIMER_VOLUME_INDICATOR = 7;
constexpr UINT VOLUME_INDICATOR_TIME = 1500;
constexpr UINT NAME_TOOLTIP_DELAY = 1000;
constexpr UINT TRAY_ICON_ID = 0x4D58;
// Generated once for this mod. Never regenerate it on startup or on updates.
constexpr GUID MIXER_TRAY_GUID = {
    0x6bb7d04a, 0xc78a, 0x4a7a,
    {0xac, 0x7a, 0x81, 0x7b, 0x68, 0x0f, 0xfd, 0x51}};

constexpr UINT MENU_OPEN = 1;
constexpr UINT MENU_REFRESH = 2;
constexpr UINT MENU_SOUND_SETTINGS = 3;
constexpr UINT MENU_WINDOWS_MIXER = 4;
constexpr UINT MENU_CLASSIC_SOUND = 5;
constexpr UINT MENU_RESTORE_HIDDEN = 6;
constexpr UINT MENU_RESTORE_PERMANENT = 7;
constexpr UINT MENU_RESTORE_OUTPUTS = 8;
#ifdef HEADROOM_STANDALONE
// Standalone app: extra tray menu entries, implemented in standalone_ui.h.
constexpr UINT MENU_APP_SETTINGS = 20;
constexpr UINT MENU_APP_AUTOSTART = 21;
constexpr UINT MENU_APP_LOG = 22;
constexpr UINT MENU_APP_ABOUT = 23;
constexpr UINT MENU_APP_EXIT = 24;
struct OutputDevice;
void AppExtendTrayMenu(std::vector<OutputDevice>& items, std::vector<UINT>& commands);
bool AppHideByName(const std::wstring& exe);
bool AppHandleTrayCommand(HWND hWnd, UINT command);
bool AppIsDialogMessage(MSG& message);
#endif

constexpr int DRAG_NONE = -2;
constexpr int DRAG_MASTER = -1;

struct CustomApp {
    std::wstring app;
    std::wstring displayName;
    int defaultVolume = -1;
};

// Rows for every output device, shown with the button in the mixer's header.
// Off until turned on there; the choice is kept in the mod's storage.
bool g_showOutputs;
constexpr PCWSTR SHOW_OUTPUTS_VALUE = L"showOutputs";

struct Settings {
    std::wstring theme = L"system";
    bool windows10Style = false;
    bool showAllSessions = false;
    bool showIdleAppsOnAllOutputs = true;
    bool compactMode = false;
    bool windowBorder = true;
    bool closeWhenFocusIsLost = true;
    bool moveNotifications = true;
    bool switchCommunicationsDevice = true;
    std::wstring loudMaxPath;
    bool animations = true;
    int backgroundOpacity = 85;
    std::wstring backgroundEffect = L"blur";
    int maxVisibleApps = 7;
    int volumeStep = 5;
    bool eqRangeCustom = false;  // The equalizer may go beyond 12 dB ...
    int eqRangeDb = 24;          // ... up to this many.
    bool keyboardControls = false;
    bool exactVolumeEntry = false;
    std::vector<CustomApp> customApps;
    std::vector<std::wstring> hiddenApps;
    std::wstring deviceEffects = L"auto";  // auto, apo or builtin: who runs an output's equalizer.
};

struct Theme {
    COLORREF background;
    COLORREF panel;
    COLORREF text;
    COLORREF secondaryText;
    COLORREF accent;
    COLORREF muted;
    COLORREF thumb;
    COLORREF sliderFill;
};

struct AppSession {
    DWORD processId = 0;
    // Every process whose sessions joined this row, for per-app output routing.
    std::vector<DWORD> processIds;
    std::wstring name;
    std::wstring executablePath;
    std::wstring pinKey;
    // Per-output pin keys saved before rows were merged across outputs.
    std::vector<std::wstring> legacyPinKeys;
    bool pinned = false;
    bool active = false;
    // Set while scanning: whether any session qualifies the row to be shown,
    // whether a hide rule matched, and which session named and leveled it.
    bool listed = false;
    bool hidden = false;
    int namePriority = -1;
    int levelPriority = -1;
    float volume = 1.0f;
    bool muted = false;
    HICON icon = nullptr;
    std::vector<ISimpleAudioVolume*> volumeControls;
    std::vector<IAudioMeterInformation*> meters;
    // Set for the rows of playback devices themselves, not apps.
    IAudioEndpointVolume* deviceVolume = nullptr;
    std::wstring deviceId;
    PCWSTR deviceGlyph = nullptr;
    bool defaultOutput = false;
    // The output an app is fixed to, by name; empty while it follows the default.
    std::wstring routedOutput;
    bool eqOn = false;  // Its equalizer is on, shown as a badge.
    bool systemSounds = false;
    bool loudMaxOn = false;  // So is LoudMax.
    bool loudnessOn = false;  // And loudness equalization.
    int startVolume = -1;     // The volume it is set to at startup, if any: shown as a badge.
    bool channelsOn = false;  // Surround channels are in use.
    bool virtualInput = false;  // An input listened to through the mixer: no Windows session behind it.

    bool IsDevice() const { return !deviceId.empty(); }

    AppSession() = default;
    AppSession(const AppSession&) = delete;
    AppSession& operator=(const AppSession&) = delete;

    AppSession(AppSession&& other) noexcept {
        *this = std::move(other);
    }

    AppSession& operator=(AppSession&& other) noexcept {
        if (this != &other) {
            Reset();
            processId = other.processId;
            processIds = std::move(other.processIds);
            name = std::move(other.name);
            executablePath = std::move(other.executablePath);
            pinKey = std::move(other.pinKey);
            legacyPinKeys = std::move(other.legacyPinKeys);
            pinned = other.pinned;
            active = other.active;
            listed = other.listed;
            hidden = other.hidden;
            namePriority = other.namePriority;
            levelPriority = other.levelPriority;
            volume = other.volume;
            muted = other.muted;
            icon = other.icon;
            volumeControls = std::move(other.volumeControls);
            meters = std::move(other.meters);
            deviceVolume = other.deviceVolume;
            deviceId = std::move(other.deviceId);
            deviceGlyph = other.deviceGlyph;
            defaultOutput = other.defaultOutput;
            routedOutput = std::move(other.routedOutput);
            eqOn = other.eqOn;
            systemSounds = other.systemSounds;
            loudMaxOn = other.loudMaxOn;
            loudnessOn = other.loudnessOn;
            startVolume = other.startVolume;
            channelsOn = other.channelsOn;
            virtualInput = other.virtualInput;
            other.deviceVolume = nullptr;
            other.icon = nullptr;
            other.volumeControls.clear();
            other.meters.clear();
        }
        return *this;
    }

    ~AppSession() {
        Reset();
    }

    void Reset() {
        for (auto* control : volumeControls) {
            control->Release();
        }
        volumeControls.clear();
        for (auto* meter : meters) {
            meter->Release();
        }
        meters.clear();
        if (deviceVolume) {
            deviceVolume->Release();
            deviceVolume = nullptr;
        }
        if (icon) {
            DestroyIcon(icon);
            icon = nullptr;
        }
    }
};

class EndpointVolumeNotification final : public IAudioEndpointVolumeCallback {
public:
    explicit EndpointVolumeNotification(HWND window) : m_window(window) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (iid != __uuidof(IUnknown) &&
            iid != __uuidof(IAudioEndpointVolumeCallback)) {
            return E_NOINTERFACE;
        }
        *object = static_cast<IAudioEndpointVolumeCallback*>(this);
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&m_references));
    }

    ULONG STDMETHODCALLTYPE Release() override {
        ULONG remaining = static_cast<ULONG>(InterlockedDecrement(&m_references));
        if (!remaining) delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA) override {
        HWND window = m_window.load();
        if (window) PostMessageW(window, WM_APP_MASTER_STATE_CHANGED, 0, 0);
        return S_OK;
    }

    void SetWindow(HWND window) { m_window.store(window); }

private:
    ~EndpointVolumeNotification() = default;
    LONG m_references = 1;
    std::atomic<HWND> m_window{nullptr};
};

Settings g_settings;
Theme g_theme;
bool g_lightTheme;

HANDLE g_thread;
HANDLE g_windowReadyEvent;
std::atomic<HWND> g_hWnd;
UINT g_taskbarCreatedMessage;

HICON g_trayIcon;
HICON g_masterIcon;
std::unordered_map<std::wstring, HICON> g_iconCache;
HFONT g_titleFont;
HFONT g_bodyFont;
HFONT g_smallFont;
HFONT g_symbolFont;
HFONT g_entryFont; // For the exact volume entry box, which Windows draws.
ULONG_PTR g_gdiplusToken;
Gdiplus::Font* g_titleTypography;
Gdiplus::Font* g_bodyTypography;
Gdiplus::Font* g_smallTypography;

// Scale of the thumb's accent dot in each pointer state, as used by the
// native Windows 11 slider.
constexpr float THUMB_DOT_REST = 0.86f;
constexpr float THUMB_DOT_HOVER = 1.167f;
constexpr float THUMB_DOT_PRESSED = 0.71f;

enum class ThumbState { Rest, Hover, Pressed };

struct RowVisual {
    float hover = 0.0f;
    float volume = 0.0f;
    float peak = 0.0f;
    float thumbDot = THUMB_DOT_REST;
    ThumbState thumbState = ThumbState::Rest;
    bool initialized = false;
};
std::unordered_map<std::wstring, RowVisual> g_rowVisuals;
bool g_motionEnabled;
bool g_closeHovered;
bool g_outputsButtonHovered;
bool g_flowButtonHovered;
float g_closeHoverAmount;
float g_frameSeconds = 0.016f;
// Animation times are AnimationClock() values, in microseconds.
ULONGLONG g_lastPaintTime;
ULONGLONG g_openTime;  // Start of the opening animation, while it runs.
ULONGLONG g_closeTime; // Start of the closing animation, while it runs.
bool g_openWarmup; // The first frame of the opening animation is still due.
bool g_closeQueued; // A close was asked for while the flyout was opening.
ULONGLONG g_frameRenderTime; // When the translucent frame was drawn; 0: none.
ULONGLONG g_pacedSince;
// The flyout opens and closes by sliding towards the taskbar edge its tray
// icon is on. Distances are in pixels, positions in screen coordinates.
RECT g_restRect{};     // Where the open flyout rests.
RECT g_slideClip{};    // The flyout is cut off outside this area as it slides.
int g_slideTravel;     // How far from its resting place a closed flyout is.
bool g_slideFromAbove; // The taskbar is above the flyout rather than below.
// Where the running animation set off from and what the last frame showed,
// so that an interrupted animation carries on from where the flyout is.
float g_slideStart;
float g_fadeStart = 1.0f;
float g_slideOffset;
float g_fadeOpacity = 1.0f;
int g_paintMatte = -1; // -1: opaque, 0/1: black/white alpha-recovery pass.
bool g_transparencyFailed;
float g_masterPaintVolume;
bool g_masterPaintMuted;
bool g_frameAnimating;
bool g_frameHasAudio;
UINT g_paintTimerInterval;

IAudioEndpointVolume* g_endpointVolume;
EndpointVolumeNotification* g_endpointVolumeCallback;
// The microphone tab: the mixer shows capture devices and the apps that record.
// The tray icon and wheel always stay with the playback output.
EDataFlow g_flow = eRender;
IAudioEndpointVolume* g_inputVolume;
EndpointVolumeNotification* g_inputVolumeCallback;

struct FlowScope {
    EDataFlow old;
    explicit FlowScope(EDataFlow flow) : old(g_flow) { g_flow = flow; }
    ~FlowScope() { g_flow = old; }
};
bool g_masterIconMuted;
PCWSTR g_trayIconGlyph;    // The volume glyph the tray icon shows.
PCWSTR g_symbolFace = L"Segoe MDL2 Assets"; // The icon font in use.
std::wstring g_endpointName;
// COM interfaces and icons must never be released by a process-exit destructor.
// Refresh clears the contents; the UI thread resets storage on controlled exit.
[[clang::no_destroy]] std::optional<std::vector<AppSession>> g_apps{std::in_place};
// Session instance IDs are unique across app instances. Do not remember PIDs:
// they can be reused, and one process can have sessions on multiple outputs.
std::unordered_set<std::wstring> g_activatedSessions;
std::unordered_set<std::wstring> g_liveSessions;
// Session instance IDs survive UI refreshes and distinguish restarted apps.
// Access is shared by the UI and Core Audio notification threads.
SRWLOCK g_defaultVolumeLock = SRWLOCK_INIT;
std::unordered_map<std::wstring, int> g_appliedDefaultVolumes;
std::unordered_set<std::wstring> g_liveDefaultSessions;

HANDLE g_audioNotificationThread;
HANDLE g_audioNotificationStopEvent;
HANDLE g_audioNotificationRebuildEvent;
HANDLE g_audioNotificationSessionEvent;
std::atomic<bool> g_audioUiRefreshPosted;

// Lifetime IMMNotificationClient used on the UI thread. It exists for the
// window's whole lifetime so default-output changes are always observed, even
// while the mixer is hidden. It posts WM_APP_DEFAULT_OUTPUT_CHANGED.
IMMDeviceEnumerator* g_uiDeviceEnumerator = nullptr;
class EndpointNotification;

int g_dpi = 96;
int g_scrollRow;
int g_availableAppRows = 100;
int g_hoverRow = DRAG_NONE;
bool g_hoverThumb; // The pointer is over the slider thumb of g_hoverRow.
POINT g_pointer{}; // Last pointer position, in client coordinates.
int g_dragRow = DRAG_NONE;
bool g_mouseTracking;
bool g_audioAvailable;
bool g_trayVersion4;
// Whether the tray icon is registered. If adding it failed (Explorer wasn't
// ready yet), the tray watch timer tries again, so the icon doesn't stay
// missing until the next Explorer restart.
bool g_trayAdded = false;
bool g_trayUsesGuid = true;
bool g_showRequestPending;
bool g_closeRequestPending;
bool g_trayPressKnown;
bool g_trayPressWasVisible;
std::atomic<bool> g_trayWheelArmed; // Listening to the wheel over the tray icon.
int g_trayWheelRemainder;  // Wheel movement short of a whole step.
bool g_showingMixer;
bool g_outputMenuOpen;
bool g_outputListAnchored;  // The open output list belongs to a row, not the picker.
bool g_outputListAllowHide;  // Ctrl+right-click on an output offers to hide it.
bool g_outputListDetached;   // The list runs while the mixer itself is closed (tray menu).
int g_outputListMenuWidth;   // Width of a menu opened at a point; 0 for the usual.
int g_outputListHideRequest = -1;
bool g_sourceMenuOpen;
bool g_outputHovered;
bool g_pendingDefaultOutputChange;
std::unordered_set<std::wstring> g_temporarilyHiddenSources;
// Sources hidden from the context menu until restored from the tray menu.
// Kept in the mod's storage, one source key per line.
std::unordered_set<std::wstring> g_permanentlyHiddenSources;
bool g_permanentlyHiddenLoaded;
constexpr PCWSTR PERMANENTLY_HIDDEN_VALUE = L"permanentlyHiddenSources";

void LoadPermanentlyHiddenSources() {
    if (g_permanentlyHiddenLoaded) return;
    g_permanentlyHiddenLoaded = true;
    std::vector<wchar_t> buffer(32768);
    Wh_GetStringValue(PERMANENTLY_HIDDEN_VALUE, buffer.data(), buffer.size());
    buffer.back() = L'\0';
    std::wstring stored(buffer.data());
    size_t start = 0;
    while (start < stored.size()) {
        size_t end = stored.find(L'\n', start);
        if (end == std::wstring::npos) end = stored.size();
        if (end > start) g_permanentlyHiddenSources.insert(stored.substr(start, end - start));
        start = end + 1;
    }
    Wh_Log(L"Mixer: the list of hidden sources was read: %d entries, System sounds %s", static_cast<int>(g_permanentlyHiddenSources.size()),
           g_permanentlyHiddenSources.count(L"systemSounds:everyOutput") ? L"hidden" : L"not hidden");
}

// Names given from the context menu, kept in the mod's storage per source.
std::unordered_map<std::wstring, std::wstring> g_sourceNames;  // Cache, "" = none.

std::wstring SourceNameValue(const std::wstring& key) {
    return L"name:" + key;
}

const std::wstring& StoredSourceName(const std::wstring& key) {
    auto found = g_sourceNames.find(key);
    if (found != g_sourceNames.end()) return found->second;
    wchar_t buffer[256]{};
    Wh_GetStringValue(SourceNameValue(key).c_str(), buffer, ARRAYSIZE(buffer));
    buffer[ARRAYSIZE(buffer) - 1] = L'\0';
    return g_sourceNames.emplace(key, buffer).first->second;
}

// An empty name goes back to the original one.
bool SaveSourceName(const std::wstring& key, const std::wstring& name) {
    g_sourceNames[key] = name;
    if (name.empty()) {
        Wh_DeleteValue(SourceNameValue(key).c_str());
        return true;
    }
    return Wh_SetStringValue(SourceNameValue(key).c_str(), name.c_str()) != FALSE;
}

bool SavePermanentlyHiddenSources() {
    Wh_Log(L"Mixer: the list of hidden sources is saved: %d entries, System sounds %s", static_cast<int>(g_permanentlyHiddenSources.size()),
           g_permanentlyHiddenSources.count(L"systemSounds:everyOutput") ? L"hidden" : L"not hidden");
    if (g_permanentlyHiddenSources.empty()) {
        Wh_DeleteValue(PERMANENTLY_HIDDEN_VALUE);
        return true;
    }
    std::wstring stored;
    for (const auto& key : g_permanentlyHiddenSources) stored += key + L"\n";
    return Wh_SetStringValue(PERMANENTLY_HIDDEN_VALUE, stored.c_str()) != FALSE;
}
HWND g_nameTooltip;
std::wstring g_nameTooltipText;
RECT g_nameTooltipRect{};
bool g_nameTooltipClipped;
ULONGLONG g_nameTooltipSince;

void CancelNameTooltip(HWND window);

int g_selectedRow = DRAG_MASTER;
DWORD g_selectedProcessId;
std::wstring g_selectedPinKey;
HWND g_volumeEntry;
HWND g_volumeEdit;
int g_volumeEntryRow = DRAG_NONE;
bool g_hotkeyEntry;           // The entry takes a mute hotkey for a source.
std::wstring g_hotkeyEntryPath;
bool g_nameEntry;            // The entry renames a source instead of setting a volume.
std::wstring g_nameEntryKey; // The source being renamed.
ULONG_PTR g_volumeEntryGeneration;
constexpr PCWSTR VOLUME_ENTRY_CLASS = L"HeadroomEntry";
constexpr PCWSTR NAME_TOOLTIP_CLASS = L"HeadroomTooltip";
constexpr PCWSTR VOLUME_INDICATOR_CLASS = L"HeadroomIndicator";
constexpr PCWSTR OUTPUT_LIST_CLASS = L"HeadroomOutputs";
HWND g_volumeIndicator;

void SelectRow(int row) {
    g_selectedRow = row;
    g_selectedPinKey.clear();
    if (row >= 0 && row < static_cast<int>(g_apps->size())) {
        g_selectedProcessId = (*g_apps)[row].processId;
        g_selectedPinKey = (*g_apps)[row].pinKey;
    }
}

void RestoreSelectedRow() {
    if (g_selectedRow < 0) return;
    for (size_t i = 0; i < g_apps->size(); ++i) {
        const auto& app = (*g_apps)[i];
        if (app.processId == g_selectedProcessId && app.pinKey == g_selectedPinKey) {
            g_selectedRow = static_cast<int>(i);
            return;
        }
    }
    SelectRow(DRAG_MASTER);
}


int Scale(int value) {
    return MulDiv(value, g_dpi, 96);
}

bool IsWindows10Style() {
    return g_settings.windows10Style;
}

// Corner radius for the current visual style: the Windows 10 style is square.
int Radius(int value) {
    return IsWindows10Style() ? 0 : Scale(value);
}

int ClampInt(int value, int minimum, int maximum) {
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}

float ClampVolume(float value) {
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

std::wstring TrimSetting(const wchar_t* value) {
    std::wstring text = value ? value : L"";
    size_t first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    return text.substr(first, text.find_last_not_of(L" \t\r\n") - first + 1);
}

bool HasDefaultVolumeRules() {
    return std::any_of(g_settings.customApps.begin(), g_settings.customApps.end(),
                      [](const CustomApp& rule) { return rule.defaultVolume >= 0; });
}

// A volume an app is set to whenever it starts to play: kept apart from the settings, so the menu can
// change it. LoadSettings adds these to the settings' rules for default volumes.
constexpr PCWSTR START_VOLUMES_VALUE = L"startVol_v1";

std::vector<std::pair<std::wstring, int>> LoadAppStartVolumes() {
    std::vector<wchar_t> buffer(8192);
    Wh_GetStringValue(START_VOLUMES_VALUE, buffer.data(), buffer.size());
    buffer.back() = L'\0';
    std::wstring stored(buffer.data());
    std::vector<std::pair<std::wstring, int>> list;
    size_t start = 0;
    while (start < stored.size()) {
        size_t end = stored.find(L'\n', start);
        if (end == std::wstring::npos) end = stored.size();
        std::wstring line = stored.substr(start, end - start);
        start = end + 1;
        size_t tab = line.find(L'\t');
        if (tab == std::wstring::npos || tab == 0) continue;
        int percent = _wtoi(line.c_str() + tab + 1);
        if (percent >= 0 && percent <= 100) list.emplace_back(line.substr(0, tab), percent);
    }
    return list;
}

int AppStartVolume(const std::wstring& exe) {
    for (const auto& [name, percent] : LoadAppStartVolumes()) {
        if (_wcsicmp(name.c_str(), exe.c_str()) == 0) return percent;
    }
    return -1;
}

// percent -1 takes the rule away.
void SetAppStartVolume(const std::wstring& exe, int percent) {
    auto list = LoadAppStartVolumes();
    list.erase(std::remove_if(list.begin(), list.end(), [&](const auto& entry) {
        return _wcsicmp(entry.first.c_str(), exe.c_str()) == 0;
    }), list.end());
    if (percent >= 0) list.emplace_back(exe, percent);
    std::wstring stored;
    for (const auto& [name, value] : list) stored += name + L"\t" + std::to_wstring(value) + L"\n";
    Wh_SetStringValue(START_VOLUMES_VALUE, stored.c_str());
}

void LoadSettings() {
    WindhawkUtils::StringSetting theme =
        WindhawkUtils::StringSetting::make(L"theme");
    g_settings.theme = theme.get();
    if (g_settings.theme != L"dark" && g_settings.theme != L"light") {
        g_settings.theme = L"system";
    }
    WindhawkUtils::StringSetting style =
        WindhawkUtils::StringSetting::make(L"visualStyle");
    g_settings.windows10Style = wcscmp(style.get(), L"windows10") == 0;

    g_settings.showAllSessions =
        Wh_GetIntSetting(L"showAllSessions") != 0;
    g_settings.showIdleAppsOnAllOutputs =
        Wh_GetIntSetting(L"showIdleAppsOnAllOutputs") != 0;
    g_settings.closeWhenFocusIsLost =
        Wh_GetIntSetting(L"closeWhenFocusIsLost") != 0;
    g_settings.moveNotifications = Wh_GetIntSetting(L"moveNotifications") != 0;
    g_settings.switchCommunicationsDevice =
        Wh_GetIntSetting(L"switchCommunicationsDevice") != 0;
    {
        WindhawkUtils::StringSetting loudMaxPath =
            WindhawkUtils::StringSetting::make(L"loudMaxPath");
        g_settings.loudMaxPath = TrimSetting(loudMaxPath.get());
        if (g_settings.loudMaxPath.size() >= 2 && g_settings.loudMaxPath.front() == L'"' &&
            g_settings.loudMaxPath.back() == L'"') {
            g_settings.loudMaxPath = g_settings.loudMaxPath.substr(1, g_settings.loudMaxPath.size() - 2);
        }
    }
    {
        WindhawkUtils::StringSetting deviceEffects = WindhawkUtils::StringSetting::make(L"deviceEffects");
        g_settings.deviceEffects = deviceEffects.get();
        if (g_settings.deviceEffects != L"apo" && g_settings.deviceEffects != L"builtin") {
            g_settings.deviceEffects = L"auto";
        }
    }
    g_showOutputs = Wh_GetIntValue(SHOW_OUTPUTS_VALUE, 0) != 0;
    g_settings.animations = Wh_GetIntSetting(L"animations") != 0;
    g_settings.backgroundOpacity = ClampInt(Wh_GetIntSetting(L"backgroundOpacity"), 0, 100);
    WindhawkUtils::StringSetting effect =
        WindhawkUtils::StringSetting::make(L"backgroundEffect");
    g_settings.backgroundEffect = effect.get();
    if (g_settings.backgroundEffect != L"glass" &&
        g_settings.backgroundEffect != L"none") {
        g_settings.backgroundEffect = L"blur";
    }
    WindhawkUtils::StringSetting pageSize =
        WindhawkUtils::StringSetting::make(L"visibleRows");
    int rows = _wtoi(pageSize.get());
    // "auto", or anything unknown, fills the screen.
    g_settings.maxVisibleApps = rows > 0 ? ClampInt(rows, 1, 100) : 100;
    g_settings.compactMode = Wh_GetIntSetting(L"compactMode") != 0;
    g_settings.windowBorder = Wh_GetIntSetting(L"windowBorder") != 0;
    g_settings.keyboardControls = Wh_GetIntSetting(L"keyboardControls") != 0;
    g_settings.exactVolumeEntry = Wh_GetIntSetting(L"exactVolumeEntry") != 0;
    g_settings.volumeStep =
        ClampInt(Wh_GetIntSetting(L"volumeStep"), 1, 20);
    g_settings.eqRangeCustom = Wh_GetIntSetting(L"eqRangeCustom") != 0;
    g_settings.eqRangeDb = ClampInt(Wh_GetIntSetting(L"eqRangeDb"), 12, 60);

    g_settings.customApps.clear();
    // The settings API has no array length query. Scan a bounded range so a
    // blank entry does not hide valid entries placed after it.
    for (int i = 0; i < 256; ++i) {
        auto app = WindhawkUtils::StringSetting::make(L"customApps[%d].app", i);
        CustomApp rule;
        rule.app = TrimSetting(app.get());
        if (rule.app.empty()) continue;
        auto name = WindhawkUtils::StringSetting::make(L"customApps[%d].displayName", i);
        rule.displayName = TrimSetting(name.get());
        int volume = Wh_GetIntSetting(L"customApps[%d].defaultVolume", i);
        rule.defaultVolume = volume >= 0 && volume <= 100 ? volume : -1;
        g_settings.customApps.push_back(std::move(rule));
    }
    g_settings.hiddenApps.clear();
    for (int i = 0; i < 256; ++i) {
        auto app = WindhawkUtils::StringSetting::make(L"hiddenApps[%d]", i);
        std::wstring name = TrimSetting(app.get());
        if (!name.empty()) g_settings.hiddenApps.push_back(std::move(name));
    }
    // Volumes set from the menu, unless the settings already have a rule for the app.
    for (const auto& [exe, percent] : LoadAppStartVolumes()) {
        bool known = std::any_of(g_settings.customApps.begin(), g_settings.customApps.end(),
                                 [&](const CustomApp& rule) { return _wcsicmp(rule.app.c_str(), exe.c_str()) == 0; });
        if (!known) {
            CustomApp rule;
            rule.app = exe;
            rule.defaultVolume = percent;
            g_settings.customApps.push_back(std::move(rule));
        }
    }
}

enum class AccentState : int {
    Disabled = 0,
    BlurBehind = 3,
    AcrylicBlurBehind = 4,
};

struct AccentPolicy {
    AccentState state;
    DWORD flags;
    DWORD gradientColor;
    DWORD animationId;
};

struct WindowCompositionAttributeData {
    int attribute;
    void* data;
    SIZE_T size;
};

void ApplyBackdropBlur(HWND hWnd) {
    using SetWindowCompositionAttribute_t = BOOL(WINAPI*)(
        HWND, const WindowCompositionAttributeData*);

    static auto setWindowCompositionAttribute =
        reinterpret_cast<SetWindowCompositionAttribute_t>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"),
                           "SetWindowCompositionAttribute"));

    if (!setWindowCompositionAttribute) {
        return;
    }

    bool transparent = g_settings.backgroundOpacity < 100 && !g_transparencyFailed;
    bool blur = transparent && g_settings.backgroundEffect != L"none";
    AccentPolicy policy{};
    WindowCompositionAttributeData data{19, &policy, sizeof(policy)};  // WCA_ACCENT_POLICY.

    if (blur && g_settings.backgroundEffect == L"glass") {
        // Acrylic blurs the backdrop and also pulls its brightness towards
        // the tint, so a bright or busy background cannot wash out the text.
        // The tint is nearly clear (ABGR, alpha 1; 0 would disable the
        // effect): the mixer paints its own translucent background on top,
        // which is what backgroundOpacity controls.
        policy.state = AccentState::AcrylicBlurBehind;
        policy.flags = 2;  // Use gradientColor as the tint.
        policy.gradientColor = 0x01000000 | (g_theme.background & 0x00FFFFFF);
        if (setWindowCompositionAttribute(hWnd, &data)) {
            return;
        }
        Wh_Log(L"Mixer: frosted glass backdrop unavailable; using plain blur");
        policy = {};
    }

    // Keep the plain blur untinted, because the mixer already paints its own
    // theme-aware translucent background on top.
    policy.state = blur ? AccentState::BlurBehind : AccentState::Disabled;
    if (!setWindowCompositionAttribute(hWnd, &data) && blur) {
        Wh_Log(L"Mixer: backdrop blur unavailable; using transparency without blur");
    }
}

// Windows 11 rounds the window on request and outlines it with a thin border
// unless told not to.
void ApplyWindowFrame(HWND hWnd) {
    // DWMWA_WINDOW_CORNER_PREFERENCE: DWMWCP_DONOTROUND or DWMWCP_ROUND.
    DWORD corner = IsWindows10Style() ? 1 : 2;
    DwmSetWindowAttribute(hWnd, 33, &corner, sizeof(corner));
    // DWMWA_BORDER_COLOR: DWMWA_COLOR_DEFAULT or DWMWA_COLOR_NONE.
    COLORREF color = g_settings.windowBorder ? 0xFFFFFFFF : 0xFFFFFFFE;
    DwmSetWindowAttribute(hWnd, 34, &color, sizeof(color));
}

void ApplyTransparencyStyle(HWND hWnd) {
    LONG_PTR style = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);
    bool transparent = g_settings.backgroundOpacity < 100 && !g_transparencyFailed;
    LONG_PTR next = transparent ? style | WS_EX_LAYERED : style & ~WS_EX_LAYERED;
    if (style != next) {
        SetLastError(0);
        if (!SetWindowLongPtrW(hWnd, GWL_EXSTYLE, next) && GetLastError()) {
            g_transparencyFailed = true;
            Wh_Log(L"Mixer: could not enable background transparency");
        }
        SetWindowPos(hWnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    ApplyBackdropBlur(hWnd);
}

void SetPaintTimer(HWND hWnd, UINT interval) {
    if (g_paintTimerInterval != interval &&
        SetTimer(hWnd, TIMER_METERS, interval, nullptr)) {
        g_paintTimerInterval = interval;
    }
}

UINT ChoosePaintInterval(bool motion, bool animating, bool audio) {
    if (motion && animating) return 16;
    if (audio) return motion ? 33 : 80;
    return 200;
}

void UpdatePaintTimer(HWND hWnd) {
    if (!IsWindowVisible(hWnd)) return;
    SetPaintTimer(hWnd, ChoosePaintInterval(g_motionEnabled,
        g_frameAnimating || g_openTime || g_closeTime || g_dragRow != DRAG_NONE,
        g_frameHasAudio));
}

// Microseconds from a steady high-resolution clock. GetTickCount64 advances
// in steps of about 16 ms, which is too coarse to time animation frames.
ULONGLONG AnimationClock() {
    static const LONGLONG frequency = [] {
        LARGE_INTEGER value;
        QueryPerformanceFrequency(&value);
        return value.QuadPart;
    }();
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return static_cast<ULONGLONG>(counter.QuadPart / frequency * 1000000 +
                                  counter.QuadPart % frequency * 1000000 / frequency);
}

// Call after presenting a frame. Timer messages arrive on a coarse and uneven
// 15 or 31 ms cadence, which makes short animations stutter. While something
// is animating, wait for the compositor to show this frame and then request
// the next one straight away, so frames follow the display's refresh rate.
void PaceAnimation(HWND hWnd) {
    if (g_openWarmup) {
        // The first frame after opening is slow, because caches are cold, and
        // was presented fully transparent. Start the animation only now.
        g_openWarmup = false;
        if (g_openTime) g_openTime = AnimationClock();
    }
    if (!g_motionEnabled || !(g_frameAnimating || g_openTime || g_closeTime) ||
        !IsWindowVisible(hWnd)) {
        g_pacedSince = 0;
        return;
    }
    // Paint messages outrank timer messages. Should an animation ever fail
    // to settle, fall back to the paint timer rather than starve the others.
    ULONGLONG now = AnimationClock();
    if (!g_pacedSince) g_pacedSince = now;
    if (now - g_pacedSince < 2000000 && SUCCEEDED(DwmFlush())) {
        InvalidateRect(hWnd, nullptr, FALSE);
    }
}

// Windows 11's entrance curve, cubic-bezier(0, 0, 0, 1): quick off the mark,
// then a long settle. Its exit curve, cubic-bezier(1, 0, 1, 1), is the same
// motion run backwards.
float EntranceEase(float progress) {
    float t = std::cbrt(progress);
    return t * t * (3.0f - 2.0f * t);
}

struct FlyoutFrame {
    float offset; // Distance from the resting place, towards the taskbar.
    float opacity;
    bool finished;
};

// The taskbar flyouts of Windows 11 slide all the way out from the taskbar's
// edge and back again, without fading. Those of Windows 10 fade in while they
// slide a short distance, and close without an animation (see HideMixer).
FlyoutFrame FlyoutFrameAt(ULONGLONG now) {
    bool closing = g_closeTime != 0;
    bool windows10 = IsWindows10Style();
    ULONGLONG start = closing ? g_closeTime : g_openTime;
    float offset = closing ? static_cast<float>(g_slideTravel) : 0.0f;
    if (!start || !g_motionEnabled) return {offset, 1.0f, true};
    float duration = closing ? 167000.0f : windows10 ? 370000.0f : 250000.0f;
    float progress = now > start
        ? ClampVolume(static_cast<float>(now - start) / duration) : 0.0f;
    float eased = closing ? 1.0f - EntranceEase(1.0f - progress)
        : windows10 ? 1.0f - std::pow(1.0f - progress, 5.0f)
        : EntranceEase(progress);
    return {g_slideStart + (offset - g_slideStart) * eased,
            g_fadeStart + (1.0f - g_fadeStart) * eased, progress >= 1.0f};
}

// The part of the flyout that shows when it is `offset` pixels away from its
// resting place, and where that part starts within the flyout.
RECT FlyoutBounds(float offset, POINT* origin) {
    int shift = static_cast<int>(std::lround(offset));
    RECT whole = g_restRect;
    OffsetRect(&whole, 0, g_slideFromAbove ? -shift : shift);
    RECT shown = whole;
    if (!IntersectRect(&shown, &whole, &g_slideClip)) shown = whole;
    *origin = {shown.left - whole.left, shown.top - whole.top};
    return shown;
}

// Moves an opaque flyout's window to where this frame of the animation has
// it, and returns where the window starts within the flyout. A translucent
// flyout is moved along with its contents instead; see PaintTransparentMixer.
POINT PlaceFlyoutFrame(HWND hWnd, const FlyoutFrame& frame) {
    POINT origin{};
    RECT bounds = FlyoutBounds(frame.offset, &origin);
    SetWindowPos(hWnd, nullptr, bounds.left, bounds.top, bounds.right - bounds.left,
                 bounds.bottom - bounds.top, SWP_NOZORDER | SWP_NOACTIVATE);
    g_slideOffset = frame.offset;
    g_fadeOpacity = frame.opacity;
    return origin;
}

// Puts the window in the flyout's resting place, whole.
void RestFlyoutWindow(HWND hWnd) {
    UINT flags = SWP_NOOWNERZORDER | SWP_NOACTIVATE;
    if (IsRectEmpty(&g_restRect)) flags |= SWP_NOMOVE | SWP_NOSIZE;
    SetWindowPos(hWnd, HWND_TOPMOST, g_restRect.left, g_restRect.top,
                 g_restRect.right - g_restRect.left,
                 g_restRect.bottom - g_restRect.top, flags);
}

// Ends the opening animation with the flyout in place.
void SettleFlyout(HWND hWnd) {
    g_openTime = 0;
    g_openWarmup = false;
    g_slideOffset = 0.0f;
    g_fadeOpacity = 1.0f;
    RestFlyoutWindow(hWnd);
    InvalidateRect(hWnd, nullptr, FALSE);
}

void CompleteHide(HWND hWnd);
void HideMixer(HWND hWnd);
void HideVolumeIndicator(HWND owner);

// Jumps to the end of whichever of the two animations is running.
void FinishFlyoutMotion(HWND hWnd) {
    if (g_closeTime) {
        CompleteHide(hWnd);
    } else if (g_openTime) {
        SettleFlyout(hWnd);
    }
}

// Like the taskbar's own flyouts, the mixer finishes opening before it
// closes. Call after a frame, to carry out a close that had to wait.
void RunQueuedClose(HWND hWnd) {
    if (g_closeQueued && !g_openTime) {
        g_closeQueued = false;
        HideMixer(hWnd);
    }
}

// The flyout's whole size. Its window can be cut short while it slides.
RECT MixerContentRect(HWND hWnd) {
    RECT rect{};
    if (g_openTime || g_closeTime) {
        rect.right = g_restRect.right - g_restRect.left;
        rect.bottom = g_restRect.bottom - g_restRect.top;
    } else {
        GetClientRect(hWnd, &rect);
    }
    return rect;
}

// Open as far as the user is concerned: on screen and not on its way out.
bool MixerOpen(HWND hWnd) {
    return IsWindowVisible(hWnd) && !g_closeTime;
}

void UpdateMotionPreference(HWND hWnd) {
    BOOL enabled = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0);
    g_motionEnabled = g_settings.animations && enabled;
    if (!g_motionEnabled) {
        FinishFlyoutMotion(hWnd);
    }
    if (IsWindowVisible(hWnd)) {
        SetPaintTimer(hWnd, g_motionEnabled ? 16 : 80);
    }
}

bool WindowsUsesLightTheme() {
    DWORD value = 0;
    DWORD size = sizeof(value);
    LSTATUS result = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return result == ERROR_SUCCESS && value != 0;
}

// AccentPalette holds eight RGBA entries, ordered from the lightest shade to
// the darkest, with the base accent color at index 3. Windows 11 controls use
// a lighter shade (1) on dark surfaces and a darker one (4) on light surfaces.
bool ReadAccentShade(int index, COLORREF* color) {
    BYTE palette[32];
    DWORD size = sizeof(palette);
    LSTATUS result = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent",
        L"AccentPalette", RRF_RT_REG_BINARY, nullptr, palette, &size);
    if (result != ERROR_SUCCESS || size < sizeof(palette)) return false;
    const BYTE* shade = palette + index * 4;
    *color = RGB(shade[0], shade[1], shade[2]);
    return true;
}

void ClearTextCache();

void UpdateTheme() {
    ClearTextCache();
    bool light = g_settings.theme == L"light" ||
                 (g_settings.theme == L"system" && WindowsUsesLightTheme());
    g_lightTheme = light;

    DWORD colorization = 0;
    BOOL opaque = FALSE;
    COLORREF accent = RGB(0, 120, 212);
    if (SUCCEEDED(DwmGetColorizationColor(&colorization, &opaque))) {
        accent = RGB((colorization >> 16) & 0xFF,
                     (colorization >> 8) & 0xFF,
                     colorization & 0xFF);
    }
    // Prefer the shade the native controls use; keep the DWM color otherwise.
    ReadAccentShade(light ? 4 : 1, &accent);
    // Windows 10 sliders use the base accent color in both themes.
    COLORREF sliderFill = accent;
    if (IsWindows10Style()) ReadAccentShade(3, &sliderFill);

    if (IsWindows10Style()) {
        // The neutral grays of the Windows 10 flyouts.
        if (light) {
            g_theme = {RGB(230, 230, 230), RGB(242, 242, 242),
                       RGB(0, 0, 0), RGB(95, 95, 95),
                       accent, RGB(120, 120, 120),
                       RGB(255, 255, 255), sliderFill};
        } else {
            g_theme = {RGB(31, 31, 31), RGB(43, 43, 43),
                       RGB(255, 255, 255), RGB(170, 170, 170),
                       accent, RGB(140, 140, 140),
                       RGB(69, 69, 69), sliderFill};
        }
        return;
    }

    // The slider thumb uses the native Windows 11 solid control color.
    if (light) {
        g_theme = {RGB(247, 248, 250), RGB(255, 255, 255),
                   RGB(27, 31, 38), RGB(98, 105, 117),
                   accent, RGB(115, 122, 134),
                   RGB(255, 255, 255), sliderFill};
    } else {
        g_theme = {RGB(27, 29, 34), RGB(38, 41, 48),
                   RGB(242, 244, 248), RGB(159, 168, 183),
                   accent, RGB(138, 146, 159),
                   RGB(69, 69, 69), sliderFill};
    }
}

void DeleteFonts() {
    ClearTextCache();
    delete g_titleTypography;
    delete g_bodyTypography;
    delete g_smallTypography;
    g_titleTypography = g_bodyTypography = g_smallTypography = nullptr;
    if (g_symbolFont) {
        DeleteObject(g_symbolFont);
        g_symbolFont = nullptr;
    }
    if (g_titleFont) {
        DeleteObject(g_titleFont);
        g_titleFont = nullptr;
    }
    if (g_bodyFont) {
        DeleteObject(g_bodyFont);
        g_bodyFont = nullptr;
    }
    if (g_smallFont) {
        DeleteObject(g_smallFont);
        g_smallFont = nullptr;
    }
    if (g_entryFont) {
        DeleteObject(g_entryFont);
        g_entryFont = nullptr;
    }
}

Gdiplus::Font* CreateTypographyFont(PCWSTR preferredFamily, float pixels) {
    Gdiplus::FontFamily family(preferredFamily);
    Gdiplus::Font* font = nullptr;
    if (family.GetLastStatus() == Gdiplus::Ok &&
        family.IsStyleAvailable(Gdiplus::FontStyleRegular)) {
        font = new Gdiplus::Font(&family, pixels, Gdiplus::FontStyleRegular,
                                 Gdiplus::UnitPixel);
        if (font->GetLastStatus() == Gdiplus::Ok) return font;
        delete font;
    }
    font = new Gdiplus::Font(L"Segoe UI", pixels, Gdiplus::FontStyleRegular,
                             Gdiplus::UnitPixel);
    if (font->GetLastStatus() == Gdiplus::Ok) return font;
    delete font;
    return nullptr; // DrawLabel retains a GDI fallback.
}

int CALLBACK FontFound(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM found) {
    *reinterpret_cast<bool*>(found) = true;
    return 0;
}

bool FontInstalled(PCWSTR face) {
    LOGFONTW request{};
    request.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW(request.lfFaceName, face, LF_FACESIZE);
    bool found = false;
    HDC dc = GetDC(nullptr);
    EnumFontFamiliesExW(dc, &request, FontFound, reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, dc);
    return found;
}

void CreateFonts(HWND hWnd) {
    DeleteFonts();
    g_dpi = static_cast<int>(GetDpiForWindow(hWnd));
    if (!g_dpi) {
        g_dpi = 96;
    }

    // Windows 10 does not come with the Windows 11 icon font. Its own has the
    // same icons at the same code points; without it, they would be missing.
    bool fluentIcons = !IsWindows10Style() && FontInstalled(L"Segoe Fluent Icons");
    g_symbolFace = fluentIcons ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";

    g_titleFont = CreateFontW(
        -MulDiv(15, g_dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_bodyFont = CreateFontW(
        -MulDiv(10, g_dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_smallFont = CreateFontW(
        -MulDiv(9, g_dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_symbolFont = CreateFontW(
        -MulDiv(13, g_dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, g_symbolFace);
    // The entry box is an ordinary opaque window, so it can have ClearType,
    // and the same family and size as the percentage it replaces. Plain
    // antialiasing renders Segoe UI unevenly at this size.
    bool variableText = !IsWindows10Style() && FontInstalled(L"Segoe UI Variable Text");
    g_entryFont = CreateFontW(
        -MulDiv(14, g_dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        variableText ? L"Segoe UI Variable Text" : L"Segoe UI");
    if (g_gdiplusToken) {
        float density = g_dpi / 96.0f;
        PCWSTR textFamily = IsWindows10Style() ? L"Segoe UI" : L"Segoe UI Variable Text";
        g_titleTypography = CreateTypographyFont(L"Segoe UI Semibold", 20.0f * density);
        g_bodyTypography = CreateTypographyFont(textFamily, 14.0f * density);
        g_smallTypography = CreateTypographyFont(textFamily, 12.0f * density);
    }
}

std::wstring BaseNameWithoutExtension(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        name.resize(dot);
    }
    return name;
}

std::wstring GetProcessPath(DWORD processId) {
    if (!processId) {
        return {};
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                 processId);
    if (!process) {
        return {};
    }

    std::vector<WCHAR> buffer(32768);
    DWORD size = static_cast<DWORD>(buffer.size());
    std::wstring result;
    if (QueryFullProcessImageNameW(process, 0, buffer.data(), &size)) {
        result.assign(buffer.data(), size);
    }
    CloseHandle(process);
    return result;
}

bool EqualizerActive(const std::wstring& valueName);
std::wstring EndpointGuid(const std::wstring& id);
bool g_masterEqOn;  // The default output's equalizer is on.
bool g_masterLoudMaxOn;
int g_masterStartVolume = -1;  // Same for the master row.
bool LoudMaxOnFor(const std::wstring& key);
bool LoudnessOnFor(const std::wstring& exe);
bool ChannelMixOnFor(const std::wstring& exe);
std::wstring DeviceStartKey(const std::wstring& id);

bool IsAudioEngineSession(const std::wstring& processPath, bool systemSounds) {
    if (systemSounds || processPath.empty()) {
        return false;
    }
    size_t slash = processPath.find_last_of(L"\\/");
    const wchar_t* filename = processPath.c_str() +
        (slash == std::wstring::npos ? 0 : slash + 1);
    return _wcsicmp(filename, L"audiodg.exe") == 0;
}

void ClearIconCache() {
    for (const auto& entry : g_iconCache) {
        if (entry.second) {
            DestroyIcon(entry.second);
        }
    }
    g_iconCache.clear();
}

HICON ExtractSizedIcon(const std::wstring& path, int pixels) {
    if (path.empty()) {
        return nullptr;
    }

    HICON icon = nullptr;
    HRESULT hr = SHDefExtractIconW(path.c_str(), 0, 0, &icon, nullptr,
                                   MAKELONG(pixels, 0));
    if (SUCCEEDED(hr) && icon) {
        return icon;
    }
    if (icon) {
        DestroyIcon(icon);
    }

    SHFILEINFOW info = {};
    if (SHGetFileInfoW(path.c_str(), FILE_ATTRIBUTE_NORMAL, &info, sizeof(info),
                       SHGFI_ICON | SHGFI_LARGEICON)) {
        return info.hIcon;
    }
    return nullptr;
}

HICON GetExecutableIcon(const std::wstring& path) {
    if (path.empty()) {
        return nullptr;
    }
    int pixels = Scale(24);
    std::wstring key = std::to_wstring(pixels) + L":" + path;
    auto found = g_iconCache.find(key);
    if (found != g_iconCache.end()) {
        return found->second ? CopyIcon(found->second) : nullptr;
    }
    if (g_iconCache.size() >= 256) {
        ClearIconCache();
    }
    HICON icon = ExtractSizedIcon(path, pixels);
    g_iconCache.emplace(std::move(key), icon);
    return icon ? CopyIcon(icon) : nullptr;
}

std::wstring GetSessionDisplayName(IAudioSessionControl* control,
                                   DWORD processId,
                                   bool systemSounds,
                                   std::wstring* processPath) {
    if (systemSounds) {
        return L"System sounds";
    }

    LPWSTR displayName = nullptr;
    if (SUCCEEDED(control->GetDisplayName(&displayName)) && displayName) {
        std::wstring value = displayName;
        CoTaskMemFree(displayName);
        if (!value.empty() && value.front() != L'@') {
            return value;
        }
    }

    if (processPath->empty()) {
        *processPath = GetProcessPath(processId);
    }
    std::wstring name = BaseNameWithoutExtension(*processPath);
    return name.empty() ? L"Unknown application" : name;
}

std::wstring ReadEndpointName(IMMDevice* device) {
    IPropertyStore* store = nullptr;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &store)) || !store) {
        return L"Default output";
    }

    PROPVARIANT value;
    PropVariantInit(&value);
    std::wstring result = L"Default output";
    if (SUCCEEDED(store->GetValue(kDeviceFriendlyName, &value)) &&
        value.vt == VT_LPWSTR && value.pwszVal) {
        result = value.pwszVal;
    }
    PropVariantClear(&value);
    store->Release();
    return result;
}

void ReleaseDefaultEndpointVolume() {
    if (g_endpointVolumeCallback) {
        g_endpointVolumeCallback->SetWindow(nullptr);
        if (g_endpointVolume) {
            g_endpointVolume->UnregisterControlChangeNotify(g_endpointVolumeCallback);
        }
        g_endpointVolumeCallback->Release();
        g_endpointVolumeCallback = nullptr;
    }
    if (g_endpointVolume) {
        g_endpointVolume->Release();
        g_endpointVolume = nullptr;
    }
}

void ReleaseInputEndpointVolume() {
    if (g_inputVolumeCallback) {
        g_inputVolumeCallback->SetWindow(nullptr);
        if (g_inputVolume) g_inputVolume->UnregisterControlChangeNotify(g_inputVolumeCallback);
        g_inputVolumeCallback->Release();
        g_inputVolumeCallback = nullptr;
    }
    if (g_inputVolume) {
        g_inputVolume->Release();
        g_inputVolume = nullptr;
    }
}

// Binds the default microphone for the master row of the microphone tab.
bool BindInputEndpointVolumeFromDevice(IMMDevice* device, HWND hWnd) {
    ReleaseInputEndpointVolume();
    if (!device ||
        FAILED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr,
                                reinterpret_cast<void**>(&g_inputVolume))) ||
        !g_inputVolume) {
        g_inputVolume = nullptr;
        return false;
    }
    auto* callback = new EndpointVolumeNotification(hWnd);
    if (SUCCEEDED(g_inputVolume->RegisterControlChangeNotify(callback))) {
        g_inputVolumeCallback = callback;
    } else {
        callback->Release();
    }
    return true;
}

// Binds the given endpoint, replacing any previous binding. Registers the
// IAudioEndpointVolumeCallback against the supplied window so that mute and
// volume changes propagate even when the mixer is hidden.
bool BindDefaultEndpointVolumeFromDevice(IMMDevice* device, HWND hWnd) {
    if (!device) return false;

    g_endpointName = ReadEndpointName(device);

    HRESULT hr = device->Activate(
        __uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr,
        reinterpret_cast<void**>(&g_endpointVolume));
    if (FAILED(hr) || !g_endpointVolume) {
        g_endpointVolume = nullptr;
        g_endpointName.clear();
        return false;
    }

    auto* callback = new EndpointVolumeNotification(hWnd);
    if (SUCCEEDED(g_endpointVolume->RegisterControlChangeNotify(callback))) {
        g_endpointVolumeCallback = callback;
    } else {
        callback->Release();
    }
    return true;
}

// Resolves the current eRender/eMultimedia endpoint and binds it, releasing
// any previous binding first. Safe to call while the mixer is hidden.
bool BindDefaultEndpointVolume(HWND hWnd) {
    ReleaseDefaultEndpointVolume();

    IMMDeviceEnumerator* enumerator = nullptr;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr) || !enumerator) return false;

    IMMDevice* device = nullptr;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
    enumerator->Release();
    if (FAILED(hr) || !device) return false;

    bool result = BindDefaultEndpointVolumeFromDevice(device, hWnd);
    device->Release();
    return result;
}

void ReleaseAudioData() {
    g_apps->clear();
    ReleaseDefaultEndpointVolume();
    ReleaseInputEndpointVolume();
    g_endpointName.clear();
    g_audioAvailable = false;
}

bool ShouldIncludeSession(AudioSessionState state, const std::wstring& id,
                          bool isDefault) {
    if (state == AudioSessionStateExpired) {
        return false;
    }
    if (!id.empty()) {
        g_liveSessions.insert(id);
        if (state == AudioSessionStateActive) {
            g_activatedSessions.insert(id);
        }
    }
    // Include the full default-output list. Only additional outputs need
    // playback history, preventing their unused idle sessions from flooding it.
    // Playback state also works for muted apps, allowing users to unmute them.
    // If the ID query failed, show active sessions without remembering them.
    return isDefault || g_settings.showAllSessions ||
           state == AudioSessionStateActive ||
           (!id.empty() && g_activatedSessions.count(id) != 0);
}

void PrunePlaybackHistory() {
    for (auto it = g_activatedSessions.begin(); it != g_activatedSessions.end();) {
        if (!g_liveSessions.count(*it)) {
            it = g_activatedSessions.erase(it);
        } else {
            ++it;
        }
    }
    g_liveSessions.clear();
}

// Apps unlocked above 100%. Windows itself stops at 100%; the mixer's audio
// engine adds the rest (see "Per-app audio engine" below).
constexpr float BOOST_MAX = 5.0f;
std::unordered_map<std::wstring, int> g_boost;  // Executable name, percent.
bool g_boostLoaded;

std::wstring ExeFileName(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    for (auto& c : name) c = static_cast<wchar_t>(std::towlower(c));
    return name;
}

void FxSettingsChanged(const std::wstring& exe);
void SetMonitorVolume(const std::wstring& key, float volume);
void SetMonitorMuted(const std::wstring& key, bool muted);
bool MonitorMuted(const std::wstring& guid);
void AppendMonitorRows();

// Kept in the mod's storage as one "name|percent" line per app.
void LoadBoost() {
    if (g_boostLoaded) return;
    g_boostLoaded = true;
    std::vector<wchar_t> buffer(8192);
    Wh_GetStringValue(L"boost_v1", buffer.data(), buffer.size());
    buffer.back() = L'\0';
    std::wstring stored(buffer.data());
    size_t start = 0;
    while (start < stored.size()) {
        size_t end = stored.find(L'\n', start);
        if (end == std::wstring::npos) end = stored.size();
        std::wstring line = stored.substr(start, end - start);
        start = end + 1;
        size_t bar = line.find(L'|');
        if (bar == std::wstring::npos || !bar) continue;
        g_boost[line.substr(0, bar)] =
            ClampInt(static_cast<int>(wcstol(line.c_str() + bar + 1, nullptr, 10)), 100, 500);
    }
}

void SaveBoost() {
    std::wstring stored;
    for (const auto& [name, percent] : g_boost) {
        stored += name + L"|" + std::to_wstring(percent) + L"\n";
    }
    if (stored.empty()) {
        Wh_DeleteValue(L"boost_v1");
    } else {
        Wh_SetStringValue(L"boost_v1", stored.c_str());
    }
}

bool BoostUnlocked(const std::wstring& path) {
    if (path.empty()) return false;
    LoadBoost();
    return g_boost.count(ExeFileName(path)) != 0;
}

int BoostPercent(const std::wstring& path) {
    LoadBoost();
    auto found = g_boost.find(ExeFileName(path));
    return found != g_boost.end() ? found->second : 100;
}

// Unlocks the app at `percent`, or changes how far it is raised.
void SetBoostPercent(const std::wstring& path, int percent) {
    if (path.empty()) return;
    LoadBoost();
    std::wstring name = ExeFileName(path);
    percent = ClampInt(percent, 100, 500);
    auto found = g_boost.find(name);
    if (found != g_boost.end() && found->second == percent) return;
    g_boost[name] = percent;
    SaveBoost();
    FxSettingsChanged(name);
}

void RemoveBoost(const std::wstring& path) {
    if (path.empty()) return;
    LoadBoost();
    std::wstring name = ExeFileName(path);
    g_boost.erase(name);
    SaveBoost();
    FxSettingsChanged(name);
}

constexpr PCWSTR SYSTEM_SOUNDS_KEY = L"systemSounds:everyOutput";

std::wstring SourcePinKey(std::wstring path, const std::wstring& name,
                          const std::wstring& endpoint, bool systemSounds) {
    for (auto& c : path) c = static_cast<wchar_t>(std::towlower(c));
    std::wstring identity = systemSounds ? L"system" :
        (path.empty() ? L"name:" + name : L"exe:" + path);
    identity += L"|output:" + endpoint;
    unsigned long long hash = 14695981039346656037ULL;
    for (wchar_t c : identity) {
        hash ^= static_cast<unsigned short>(c);
        hash *= 1099511628211ULL;
    }
    return L"sourcePin_v1_" + std::to_wstring(hash);
}

bool AppSortOrder(const AppSession& left, const AppSession& right) {
    if (left.IsDevice() != right.IsDevice()) return left.IsDevice();
    if (left.IsDevice() && left.defaultOutput != right.defaultOutput) {
        return left.defaultOutput;
    }
    if (left.pinned != right.pinned) return left.pinned > right.pinned;
    if (!left.pinned && left.active != right.active) return left.active > right.active;
    int nameOrder = _wcsicmp(left.name.c_str(), right.name.c_str());
    return nameOrder ? nameOrder < 0 : left.pinKey < right.pinKey;
}

void ToggleSourcePin(HWND window, int row) {
    CancelNameTooltip(window);
    if (row < 0 || row >= static_cast<int>(g_apps->size())) return;
    std::wstring key = (*g_apps)[row].pinKey;
    bool pinned = !(*g_apps)[row].pinned;
    BOOL saved = pinned ? Wh_SetIntValue(key.c_str(), 1)
                        : Wh_DeleteValue(key.c_str());
    if (!pinned) {
        // A merged row can still be pinned through its older per-output keys.
        for (const auto& legacy : (*g_apps)[row].legacyPinKeys) {
            saved |= Wh_DeleteValue(legacy.c_str());
        }
        (*g_apps)[row].legacyPinKeys.clear();
    }
    if (!saved) {
        Wh_Log(L"Mixer: could not save source pin");
        return;
    }
    for (auto& app : *g_apps) if (app.pinKey == key) app.pinned = pinned;
    std::stable_sort(g_apps->begin(), g_apps->end(), AppSortOrder);
    g_scrollRow = 0;
    SelectRow(DRAG_MASTER);
    g_hoverRow = DRAG_NONE;
    InvalidateRect(window, nullptr, FALSE);
}

bool MatchesAppName(const std::wstring& target, const std::wstring& path,
                    const std::wstring& name) {
    if (target.empty()) return false;
    if (_wcsicmp(target.c_str(), name.c_str()) == 0) return true;
    if (path.empty()) return false;
    size_t slash = path.find_last_of(L"\\/");
    const wchar_t* filename = path.c_str() + (slash == std::wstring::npos ? 0 : slash + 1);
    return _wcsicmp(target.c_str(), path.c_str()) == 0 ||
           _wcsicmp(target.c_str(), filename) == 0 ||
           _wcsicmp(target.c_str(), BaseNameWithoutExtension(path).c_str()) == 0;
}

const CustomApp* FindCustomAppInRules(const std::vector<CustomApp>& rules,
                                           const std::wstring& path,
                                           const std::wstring& name) {
    for (const auto& rule : rules) {
        if (MatchesAppName(rule.app, path, name)) return &rule;
    }
    return nullptr;
}

const CustomApp* FindCustomApp(const std::wstring& path, const std::wstring& name) {
    return FindCustomAppInRules(g_settings.customApps, path, name);
}

bool IsAppHidden(const std::wstring& path, const std::wstring& name) {
    return std::any_of(g_settings.hiddenApps.begin(), g_settings.hiddenApps.end(),
        [&](const std::wstring& target) { return MatchesAppName(target, path, name); });
}

void ForgetAppliedDefaultVolume(const std::wstring& instanceId) {
    if (instanceId.empty()) return;
    AcquireSRWLockExclusive(&g_defaultVolumeLock);
    g_appliedDefaultVolumes.erase(instanceId);
    ReleaseSRWLockExclusive(&g_defaultVolumeLock);
}

bool ApplyTrackedDefaultVolume(const std::wstring& instanceId, int percent,
                               ISimpleAudioVolume* volume) {
    if (instanceId.empty() || !volume || percent < 0 || percent > 100) return false;

    AcquireSRWLockShared(&g_defaultVolumeLock);
    auto previous = g_appliedDefaultVolumes.find(instanceId);
    bool alreadyApplied = previous != g_appliedDefaultVolumes.end() &&
                          previous->second == percent;
    ReleaseSRWLockShared(&g_defaultVolumeLock);
    if (alreadyApplied) return false;

    HRESULT result = volume->SetMasterVolume(percent / 100.0f, nullptr);
    if (FAILED(result)) return false;

    AcquireSRWLockExclusive(&g_defaultVolumeLock);
    g_appliedDefaultVolumes[instanceId] = percent;
    ReleaseSRWLockExclusive(&g_defaultVolumeLock);
    return true;
}

void QueueAudioUiRefresh(HWND window) {
    bool expected = false;
    if (!g_audioUiRefreshPosted.compare_exchange_strong(expected, true)) return;
    if (!window || !PostMessageW(window, WM_APP_AUDIO_CHANGED, 0, 0)) {
        g_audioUiRefreshPosted.store(false);
    }
}

void ApplyDefaultRuleToSession(IAudioSessionControl* control,
                               const std::vector<CustomApp>& rules) {
    if (!control) return;

    IAudioSessionControl2* control2 = nullptr;
    ISimpleAudioVolume* volume = nullptr;
    if (FAILED(control->QueryInterface(__uuidof(IAudioSessionControl2),
                                       reinterpret_cast<void**>(&control2))) ||
        FAILED(control->QueryInterface(__uuidof(ISimpleAudioVolume),
                                       reinterpret_cast<void**>(&volume))) ||
        !control2 || !volume) {
        if (control2) control2->Release();
        if (volume) volume->Release();
        return;
    }

    LPWSTR rawInstanceId = nullptr;
    std::wstring instanceId;
    if (SUCCEEDED(control2->GetSessionInstanceIdentifier(&rawInstanceId)) &&
        rawInstanceId) {
        instanceId = rawInstanceId;
    }
    if (rawInstanceId) CoTaskMemFree(rawInstanceId);

    DWORD processId = 0;
    HRESULT processResult = control2->GetProcessId(&processId);
    bool systemSounds = control2->IsSystemSoundsSession() == S_OK;
    std::wstring processPath = SUCCEEDED(processResult)
        ? GetProcessPath(processId) : std::wstring();
    if (!IsAudioEngineSession(processPath, systemSounds)) {
        std::wstring displayName = GetSessionDisplayName(
            control, processId, systemSounds, &processPath);
        const CustomApp* rule = FindCustomAppInRules(rules, processPath, displayName);
        if (rule && rule->defaultVolume >= 0) {
            ApplyTrackedDefaultVolume(instanceId, rule->defaultVolume, volume);
        } else {
            ForgetAppliedDefaultVolume(instanceId);
        }
    }

    volume->Release();
    control2->Release();
}

struct AudioNotificationContext {
    HWND window;
    HANDLE stopEvent;
    HANDLE rebuildEvent;
    HANDLE sessionEvent;
    SRWLOCK pendingLock = SRWLOCK_INIT;
    std::vector<IAudioSessionControl*> pendingSessions;
    std::vector<CustomApp> rules;
};

class AudioSessionNotification final : public IAudioSessionNotification {
public:
    explicit AudioSessionNotification(AudioNotificationContext* context)
        : m_context(context) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IAudioSessionNotification)) {
            *object = static_cast<IAudioSessionNotification*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&m_references));
    }

    ULONG STDMETHODCALLTYPE Release() override {
        ULONG remaining = static_cast<ULONG>(InterlockedDecrement(&m_references));
        if (!remaining) delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE OnSessionCreated(IAudioSessionControl* newSession) override {
        if (!newSession || !m_context) return S_OK;

        newSession->AddRef();
        AcquireSRWLockExclusive(&m_context->pendingLock);
        try {
            m_context->pendingSessions.push_back(newSession);
        } catch (...) {
            // Out of memory: an exception must not leave a COM callback.
            ReleaseSRWLockExclusive(&m_context->pendingLock);
            newSession->Release();
            return S_OK;
        }
        ReleaseSRWLockExclusive(&m_context->pendingLock);
        SetEvent(m_context->sessionEvent);
        return S_OK;
    }

private:
    ~AudioSessionNotification() = default;
    LONG m_references = 1;
    AudioNotificationContext* m_context;
};

// Generic IMMNotificationClient with two optional sinks: an event for the
// background audio-notification thread, and a window for the UI thread.
// Construct with either; the other sink stays unused.
class EndpointNotification final : public IMMNotificationClient {
public:
    explicit EndpointNotification(HANDLE rebuildEvent)
        : m_rebuildEvent(rebuildEvent) {}

    void SetWindow(HWND window) { m_window.store(window); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMMNotificationClient)) {
            *object = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&m_references));
    }

    ULONG STDMETHODCALLTYPE Release() override {
        ULONG remaining = static_cast<ULONG>(InterlockedDecrement(&m_references));
        if (!remaining) delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override {
        SignalRebuild();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override {
        SignalRebuild();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override {
        SignalRebuild();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole,
                                                     LPCWSTR) override {
        if (flow == eRender || flow == eCapture || flow == eAll) SignalRebuild();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override {
        return S_OK;
    }

private:
    ~EndpointNotification() = default;

    void SignalRebuild() {
        if (m_rebuildEvent) SetEvent(m_rebuildEvent);
        HWND window = m_window.load();
        if (window) PostMessageW(window, WM_APP_DEFAULT_OUTPUT_CHANGED, 0, 0);
    }

    LONG m_references = 1;
    HANDLE m_rebuildEvent = nullptr;
    std::atomic<HWND> m_window{nullptr};
};

EndpointNotification* g_uiEndpointNotification = nullptr;

struct AudioSessionWatcher {
    IAudioSessionManager2* manager = nullptr;
    AudioSessionNotification* notification = nullptr;

    ~AudioSessionWatcher() {
        if (manager && notification) {
            manager->UnregisterSessionNotification(notification);
        }
        if (notification) notification->Release();
        if (manager) manager->Release();
    }
};

void PrimeSessionManager(IAudioSessionManager2* manager,
                         const std::vector<CustomApp>& rules) {
    IAudioSessionEnumerator* sessions = nullptr;
    if (FAILED(manager->GetSessionEnumerator(&sessions)) || !sessions) return;

    int count = 0;
    if (SUCCEEDED(sessions->GetCount(&count))) {
        for (int i = 0; i < count; ++i) {
            IAudioSessionControl* control = nullptr;
            if (SUCCEEDED(sessions->GetSession(i, &control)) && control) {
                ApplyDefaultRuleToSession(control, rules);
                control->Release();
            }
        }
    }
    sessions->Release();
}

void RebuildAudioSessionWatchers(
        IMMDeviceEnumerator* enumerator, AudioNotificationContext* context,
        std::vector<std::unique_ptr<AudioSessionWatcher>>& watchers) {
    // Enumerate first: if that fails, the old watchers stay, so the default
    // volume rules keep applying instead of silently stopping.
    IMMDeviceCollection* devices = nullptr;
    HRESULT hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices);
    if (FAILED(hr) || !devices) {
        Wh_Log(L"Mixer: notification endpoint enumeration failed: 0x%08lX",
               static_cast<unsigned long>(hr));
        return;
    }
    watchers.clear();

    UINT count = 0;
    if (FAILED(devices->GetCount(&count))) count = 0;
    for (UINT i = 0; i < count; ++i) {
        IMMDevice* device = nullptr;
        if (FAILED(devices->Item(i, &device)) || !device) continue;

        IAudioSessionManager2* manager = nullptr;
        hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_INPROC_SERVER,
                              nullptr, reinterpret_cast<void**>(&manager));
        device->Release();
        if (FAILED(hr) || !manager) continue;

        auto* notification = new AudioSessionNotification(context);
        hr = manager->RegisterSessionNotification(notification);
        if (FAILED(hr)) {
            notification->Release();
            manager->Release();
            continue;
        }

        auto watcher = std::make_unique<AudioSessionWatcher>();
        watcher->manager = manager;
        watcher->notification = notification;
        PrimeSessionManager(manager, context->rules);
        watchers.push_back(std::move(watcher));
    }
    devices->Release();
}

void DrainPendingAudioSessions(AudioNotificationContext* context, bool applyRules) {
    std::vector<IAudioSessionControl*> pending;
    AcquireSRWLockExclusive(&context->pendingLock);
    pending.swap(context->pendingSessions);
    ReleaseSRWLockExclusive(&context->pendingLock);

    for (IAudioSessionControl* control : pending) {
        if (applyRules) {
            ApplyDefaultRuleToSession(control, context->rules);
        }
        control->Release();
    }

    if (applyRules && !pending.empty()) {
        QueueAudioUiRefresh(context->window);
    }
}

DWORD WINAPI AudioNotificationThreadProc(LPVOID parameter) {
    std::unique_ptr<AudioNotificationContext> context(
        static_cast<AudioNotificationContext*>(parameter));
    HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(comResult)) {
        Wh_Log(L"Mixer: audio notification COM initialization failed: 0x%08lX",
               static_cast<unsigned long>(comResult));
        return 1;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr) || !enumerator) {
        Wh_Log(L"Mixer: audio notification device enumerator failed: 0x%08lX",
               static_cast<unsigned long>(hr));
        CoUninitialize();
        return 1;
    }

    auto* endpointNotification = new EndpointNotification(context->rebuildEvent);
    bool endpointRegistered = SUCCEEDED(
        enumerator->RegisterEndpointNotificationCallback(endpointNotification));
    if (!endpointRegistered) {
        Wh_Log(L"Mixer: endpoint notification registration failed");
    }

    std::vector<std::unique_ptr<AudioSessionWatcher>> watchers;
    RebuildAudioSessionWatchers(enumerator, context.get(), watchers);

    HANDLE events[] = {
        context->stopEvent, context->rebuildEvent, context->sessionEvent};
    while (true) {
        DWORD wait = WaitForMultipleObjects(3, events, FALSE, INFINITE);
        if (wait == WAIT_OBJECT_0) break;
        if (wait == WAIT_OBJECT_0 + 1) {
            RebuildAudioSessionWatchers(enumerator, context.get(), watchers);
            QueueAudioUiRefresh(context->window);
            continue;
        }
        if (wait == WAIT_OBJECT_0 + 2) {
            DrainPendingAudioSessions(context.get(), true);
            continue;
        }
        break;
    }

    watchers.clear();
    DrainPendingAudioSessions(context.get(), false);
    if (endpointRegistered) {
        enumerator->UnregisterEndpointNotificationCallback(endpointNotification);
    }
    endpointNotification->Release();
    enumerator->Release();
    CoUninitialize();
    return 0;
}

bool StopDefaultVolumeNotifications() {
    if (!g_audioNotificationThread) return true;

    if (g_audioNotificationStopEvent) SetEvent(g_audioNotificationStopEvent);
    DWORD wait = WaitForSingleObject(g_audioNotificationThread, 5000);
    if (wait != WAIT_OBJECT_0) {
        Wh_Log(L"Mixer: audio notification thread did not stop in time");
        return false;
    }

    CloseHandle(g_audioNotificationThread);
    g_audioNotificationThread = nullptr;
    if (g_audioNotificationStopEvent) {
        CloseHandle(g_audioNotificationStopEvent);
        g_audioNotificationStopEvent = nullptr;
    }
    if (g_audioNotificationRebuildEvent) {
        CloseHandle(g_audioNotificationRebuildEvent);
        g_audioNotificationRebuildEvent = nullptr;
    }
    if (g_audioNotificationSessionEvent) {
        CloseHandle(g_audioNotificationSessionEvent);
        g_audioNotificationSessionEvent = nullptr;
    }
    g_audioUiRefreshPosted.store(false);
    return true;
}

void StartDefaultVolumeNotifications(HWND window) {
    if (!HasDefaultVolumeRules() || g_audioNotificationThread) return;

    g_audioNotificationStopEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_audioNotificationRebuildEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_audioNotificationSessionEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!g_audioNotificationStopEvent || !g_audioNotificationRebuildEvent ||
        !g_audioNotificationSessionEvent) {
        Wh_Log(L"Mixer: could not create audio notification events");
        if (g_audioNotificationStopEvent) CloseHandle(g_audioNotificationStopEvent);
        if (g_audioNotificationRebuildEvent) CloseHandle(g_audioNotificationRebuildEvent);
        if (g_audioNotificationSessionEvent) CloseHandle(g_audioNotificationSessionEvent);
        g_audioNotificationStopEvent = nullptr;
        g_audioNotificationRebuildEvent = nullptr;
        g_audioNotificationSessionEvent = nullptr;
        return;
    }

    auto* context = new AudioNotificationContext;
    context->window = window;
    context->stopEvent = g_audioNotificationStopEvent;
    context->rebuildEvent = g_audioNotificationRebuildEvent;
    context->sessionEvent = g_audioNotificationSessionEvent;
    context->rules = g_settings.customApps;
    g_audioNotificationThread = CreateThread(
        nullptr, 0, AudioNotificationThreadProc, context, 0, nullptr);
    if (!g_audioNotificationThread) {
        Wh_Log(L"Mixer: could not start audio notification thread, error %lu",
               GetLastError());
        delete context;
        CloseHandle(g_audioNotificationStopEvent);
        CloseHandle(g_audioNotificationRebuildEvent);
        CloseHandle(g_audioNotificationSessionEvent);
        g_audioNotificationStopEvent = nullptr;
        g_audioNotificationRebuildEvent = nullptr;
        g_audioNotificationSessionEvent = nullptr;
    }
}

void ApplyDefaultVolume(const CustomApp* rule, const std::wstring& instanceId,
                        ISimpleAudioVolume* volume) {
    if (instanceId.empty()) return;
    if (!rule || rule->defaultVolume < 0) {
        ForgetAppliedDefaultVolume(instanceId);
        return;
    }
    ApplyTrackedDefaultVolume(instanceId, rule->defaultVolume, volume);
}

void PruneDefaultVolumes(bool completeScan) {
    // An unavailable device/service is not evidence that a session ended.
    if (!completeScan) return;
    AcquireSRWLockExclusive(&g_defaultVolumeLock);
    for (auto it = g_appliedDefaultVolumes.begin(); it != g_appliedDefaultVolumes.end();) {
        if (!g_liveDefaultSessions.count(it->first)) it = g_appliedDefaultVolumes.erase(it);
        else ++it;
    }
    ReleaseSRWLockExclusive(&g_defaultVolumeLock);
}

bool AppendDeviceSessions(IMMDevice* device, bool isDefault, bool diagnostics,
                          bool buildRows,
                          std::unordered_map<std::wstring, size_t>& groups) {
    std::wstring endpointName = ReadEndpointName(device);
    std::wstring endpointId = endpointName;
    LPWSTR rawEndpointId = nullptr;
    if (SUCCEEDED(device->GetId(&rawEndpointId)) && rawEndpointId) endpointId = rawEndpointId;
    if (rawEndpointId) CoTaskMemFree(rawEndpointId);
    IAudioSessionManager2* manager = nullptr;
    HRESULT hr = device->Activate(__uuidof(IAudioSessionManager2),
                                  CLSCTX_INPROC_SERVER, nullptr,
                                  reinterpret_cast<void**>(&manager));
    if (FAILED(hr) || !manager) {
        if (diagnostics) {
            Wh_Log(L"Mixer: session manager failed for '%s': 0x%08lX",
                   endpointName.c_str(), static_cast<unsigned long>(hr));
        }
        return false;
    }

    IAudioSessionEnumerator* sessions = nullptr;
    hr = manager->GetSessionEnumerator(&sessions);
    manager->Release();
    if (FAILED(hr) || !sessions) {
        if (diagnostics) {
            Wh_Log(L"Mixer: session enumeration failed for '%s': 0x%08lX",
                   endpointName.c_str(), static_cast<unsigned long>(hr));
        }
        return false;
    }

    int count = 0;
    hr = sessions->GetCount(&count);
    bool complete = SUCCEEDED(hr);
    if (diagnostics) {
        Wh_Log(L"Mixer: output '%s', default=%d, sessions=%d, result=0x%08lX",
               endpointName.c_str(), isDefault, count,
               static_cast<unsigned long>(hr));
    }
    for (int i = 0; i < count; i++) {
        IAudioSessionControl* control = nullptr;
        if (FAILED(sessions->GetSession(i, &control)) || !control) {
            complete = false;
            continue;
        }

        AudioSessionState state = AudioSessionStateExpired;
        if (FAILED(control->GetState(&state))) complete = false;
        if (state == AudioSessionStateExpired) {
            control->Release();
            continue;
        }

        IAudioSessionControl2* control2 = nullptr;
        ISimpleAudioVolume* volume = nullptr;
        if (FAILED(control->QueryInterface(__uuidof(IAudioSessionControl2),
                                           reinterpret_cast<void**>(&control2))) ||
            FAILED(control->QueryInterface(__uuidof(ISimpleAudioVolume),
                                           reinterpret_cast<void**>(&volume))) ||
            !control2 || !volume) {
            complete = false;
            if (control2) {
                control2->Release();
            }
            if (volume) {
                volume->Release();
            }
            control->Release();
            continue;
        }

        DWORD processId = 0;
        HRESULT processResult = control2->GetProcessId(&processId);
        bool systemSounds = control2->IsSystemSoundsSession() == S_OK;
        std::wstring processPath = SUCCEEDED(processResult)
            ? GetProcessPath(processId) : std::wstring();
        if (IsAudioEngineSession(processPath, systemSounds) ||
            (SUCCEEDED(processResult) && processId == GetCurrentProcessId())) {
            if (diagnostics) {
                Wh_Log(L"Mixer: hiding audio engine session, PID=%lu, output='%s'",
                       processId, endpointName.c_str());
            }
            volume->Release();
            control2->Release();
            control->Release();
            continue;
        }

        std::wstring displayName = GetSessionDisplayName(control, processId,
                                                         systemSounds, &processPath);
        // A name the session supplied beats the executable-name fallback.
        int namePriority =
            displayName != BaseNameWithoutExtension(processPath) ? 1 : 0;
        // Like the Windows volume mixer, show one row per app: sessions of
        // the same executable are merged, including across outputs, where
        // audio routers leave idle duplicates behind. System sounds, sessions
        // without a readable path, and everything when showAllSessions is on,
        // stay scoped to their own output.
        bool scoped = g_settings.showAllSessions || systemSounds ||
                      processPath.empty();
        std::wstring outputPinKey =
            SourcePinKey(processPath, displayName, endpointId, systemSounds);
        // A microphone row has its own pin, hide and name state, apart from the app's playback row.
        std::wstring pinKey = scoped
            ? outputPinKey
            : SourcePinKey(processPath, displayName, g_flow == eCapture ? L"input" : L"", false);
        bool pinned = buildRows && Wh_GetIntValue(pinKey.c_str(), 0) != 0;
        bool legacyPinned = buildRows && !scoped &&
                            Wh_GetIntValue(outputPinKey.c_str(), 0) != 0;
        LPWSTR rawInstanceId = nullptr;
        std::wstring instanceId;
        if (SUCCEEDED(control2->GetSessionInstanceIdentifier(&rawInstanceId)) &&
            rawInstanceId) {
            instanceId = rawInstanceId;
        }
        if (rawInstanceId) {
            CoTaskMemFree(rawInstanceId);
        }
        if (instanceId.empty()) complete = false;
        else g_liveDefaultSessions.insert(instanceId);
        LoadPermanentlyHiddenSources();
        // Every output has its own System sounds; hiding one hides them all.
        bool hidden = IsAppHidden(processPath, displayName) ||
                      g_temporarilyHiddenSources.count(pinKey) != 0 ||
                      g_permanentlyHiddenSources.count(pinKey) != 0 ||
                      (systemSounds &&
                       (g_temporarilyHiddenSources.count(SYSTEM_SOUNDS_KEY) != 0 ||
                        g_permanentlyHiddenSources.count(SYSTEM_SOUNDS_KEY) != 0));
        const CustomApp* custom = FindCustomApp(processPath, displayName);
        if (g_flow == eRender) ApplyDefaultVolume(custom, instanceId, volume);
        // Build identity before applying the alias, so renaming keeps pins and
        // keyboard selection attached to the original source.
        if (custom && !custom->displayName.empty()) {
            displayName = custom->displayName;
            namePriority = 2;
        }
        // A name given in the mixer wins over the settings.
        if (const std::wstring& renamed = StoredSourceName(pinKey); !renamed.empty()) {
            displayName = renamed;
            namePriority = 3;
        }
        // Apps are listed whatever output their session is on, playing or not:
        // an app that opened its audio on another output still belongs in the
        // mixer. Their sessions merge into one row per app, so idle duplicates
        // on other outputs add no rows. Scoped rows, such as System sounds of
        // each output, keep the original rule.
        // The microphone tab lists the apps that record right now.
        bool include = g_flow == eCapture
            ? state == AudioSessionStateActive
            : ShouldIncludeSession(state, instanceId, isDefault) ||
                  (g_settings.showIdleAppsOnAllOutputs && !scoped &&
                   state != AudioSessionStateExpired);
        if (!buildRows) {
            volume->Release();
            control2->Release();
            control->Release();
            continue;
        }

        // The pin key already identifies the executable, per output when the
        // row is scoped. Sessions without a readable path are only grouped by
        // process, and are never merged into System sounds.
        std::wstring groupKey = pinKey;
        if (!systemSounds && processPath.empty()) {
            groupKey = (SUCCEEDED(processResult) && processId
                ? L"pid:" + std::to_wstring(processId)
                : L"session:" + std::to_wstring(i)) + L"|" + endpointId;
        }

        // Every session joins its row, so the slider also reaches sessions
        // that would not be listed on their own. Rows nothing qualifies are
        // dropped once all outputs are scanned.
        auto existing = groups.find(groupKey);
        if (existing == groups.end()) {
            AppSession created;
            created.processId = processId;
            created.executablePath = processPath;
            created.pinKey = pinKey;
            created.systemSounds = systemSounds;
            if (diagnostics) {
                Wh_Log(L"Mixer: app '%s', PID=%lu, active=%d, output='%s'",
                       displayName.c_str(), processId,
                       state == AudioSessionStateActive, endpointName.c_str());
            }
            existing = groups.emplace(groupKey, g_apps->size()).first;
            g_apps->push_back(std::move(created));
        }
        AppSession& app = (*g_apps)[existing->second];
        if (SUCCEEDED(processResult) && processId &&
            std::find(app.processIds.begin(), app.processIds.end(), processId) ==
                app.processIds.end()) {
            app.processIds.push_back(processId);
        }
        app.listed |= include || pinned || legacyPinned;
        app.hidden |= hidden;
        app.pinned |= pinned || legacyPinned;
        if (legacyPinned) app.legacyPinKeys.push_back(std::move(outputPinKey));
        app.active |= state == AudioSessionStateActive;
        if (namePriority > app.namePriority) {
            app.namePriority = namePriority;
            app.name = displayName;
            if (scoped && !isDefault) {
                app.name += L" [" + endpointName + L"]";
            }
        }
        // Show the level of the session that is most likely audible.
        int levelPriority = state == AudioSessionStateActive ? 2
                            : isDefault ? 1 : 0;
        if (levelPriority > app.levelPriority) {
            app.levelPriority = levelPriority;
            volume->GetMasterVolume(&app.volume);
            BOOL muted = FALSE;
            volume->GetMute(&muted);
            app.muted = muted != FALSE;
        }
        app.volumeControls.push_back(volume);

        IAudioMeterInformation* meter = nullptr;
        if (SUCCEEDED(control->QueryInterface(__uuidof(IAudioMeterInformation),
                                              reinterpret_cast<void**>(&meter))) &&
            meter) {
            app.meters.push_back(meter);
        }

        control2->Release();
        control->Release();
    }
    sessions->Release();
    return complete;
}

void ClampPageOffset();

void RefreshMasterIcons(HWND hWnd, bool force = false);

PCWSTR ReadEndpointGlyph(IMMDevice* device);

// "Lautsprecher (Realtek(R) Audio)" -> "Lautsprecher": Windows puts the
// adapter in parentheses after the endpoint's own name.
std::wstring ShortOutputName(const std::wstring& name) {
    if (name.empty() || name.back() != L')') return name;
    int depth = 0;
    for (size_t i = name.size(); i-- > 0;) {
        if (name[i] == L')') ++depth;
        else if (name[i] == L'(' && --depth == 0) {
            size_t end = i;
            while (end > 0 && name[end - 1] == L' ') --end;
            return end > 0 ? name.substr(0, end) : name;
        }
    }
    return name;
}
struct IAudioPolicyConfigFactory;
IAudioPolicyConfigFactory* CreateAudioPolicyConfig();
std::wstring GetAppOutput(IAudioPolicyConfigFactory* config, DWORD processId);
std::wstring HeardOutput(IAudioPolicyConfigFactory* config, DWORD pid, const std::wstring& path);
HRESULT RouteApps(IAudioPolicyConfigFactory* config, const std::vector<DWORD>& pids,
                  const std::wstring& target);
void ReleaseAudioPolicyConfig(IAudioPolicyConfigFactory* config);

// A row for a playback device itself, shown with "Show all output devices".
bool IsFxSinkName(const std::wstring& name);

void AppendOutputRow(IMMDevice* device, const std::wstring& id, bool isDefault) {
    if (IsFxSinkName(ReadEndpointName(device))) return;  // The engine's silent output.
    std::wstring key = L"device:" + id;
    if (g_temporarilyHiddenSources.count(key) ||
        (LoadPermanentlyHiddenSources(), g_permanentlyHiddenSources.count(key))) {
        return;
    }
    IAudioEndpointVolume* volume = nullptr;
    if (FAILED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER,
                                nullptr, reinterpret_cast<void**>(&volume))) || !volume) {
        return;
    }
    AppSession row;
    row.deviceVolume = volume;
    row.deviceId = id;
    row.deviceGlyph = ReadEndpointGlyph(device);
    row.defaultOutput = isDefault;
    row.pinKey = key;
    row.listed = true;
    row.active = true;
    row.pinned = Wh_GetIntValue(key.c_str(), 0) != 0;
    const std::wstring& renamed = StoredSourceName(key);
    row.name = renamed.empty() ? ReadEndpointName(device) : renamed;
    float level = 1.0f;
    BOOL muted = FALSE;
    if (SUCCEEDED(volume->GetMasterVolumeLevelScalar(&level))) row.volume = level;
    if (SUCCEEDED(volume->GetMute(&muted))) row.muted = muted != FALSE;
    IAudioMeterInformation* meter = nullptr;
    if (SUCCEEDED(device->Activate(__uuidof(IAudioMeterInformation), CLSCTX_INPROC_SERVER,
                                   nullptr, reinterpret_cast<void**>(&meter))) && meter) {
        row.meters.push_back(meter);
    }
    g_apps->push_back(std::move(row));
}

void RefreshAudioSessions(HWND hWnd, bool diagnostics = false) {
    if (g_dragRow != DRAG_NONE || g_volumeEntry || g_sourceMenuOpen) {
        return;
    }

    bool buildRows = IsWindowVisible(hWnd) || diagnostics;
    g_liveSessions.clear();
    g_liveDefaultSessions.clear();
    ReleaseAudioData();
    IMMDeviceEnumerator* enumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr) || !enumerator) {
        if (diagnostics) {
            Wh_Log(L"Mixer: device enumeration failed: 0x%08lX",
                   static_cast<unsigned long>(hr));
        }
        RestoreSelectedRow();
        ClampPageOffset();
        InvalidateRect(hWnd, nullptr, FALSE);
        return;
    }

    std::wstring defaultId;
    IMMDevice* defaultDevice = nullptr;
    // The tray icon and wheel follow the playback output, whichever tab shows.
    IMMDevice* renderDefault = nullptr;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(
            eRender, eMultimedia, &renderDefault)) && renderDefault) {
        // The helper releases any previous binding (via ReleaseAudioData
        // above) and re-registers the endpoint callback against this window.
        BindDefaultEndpointVolumeFromDevice(renderDefault, hWnd);
    }
    if (g_flow == eRender) {
        defaultDevice = renderDefault;
        renderDefault = nullptr;
    } else {
        g_masterEqOn = g_masterLoudMaxOn = false;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(g_flow, eMultimedia, &defaultDevice)) &&
            defaultDevice) {
            BindInputEndpointVolumeFromDevice(defaultDevice, hWnd);
            g_endpointName = ReadEndpointName(defaultDevice);  // The picker names the microphone.
        }
    }
    if (defaultDevice) {
        LPWSTR id = nullptr;
        if (SUCCEEDED(defaultDevice->GetId(&id)) && id) {
            defaultId = id;
            CoTaskMemFree(id);
        }
    }

    bool completeScan = true;
    g_masterStartVolume = -1;
    std::unordered_map<std::wstring, size_t> groups;
    std::vector<std::pair<std::wstring, std::wstring>> outputNames;  // Id, name.
    IMMDeviceCollection* devices = nullptr;
    hr = enumerator->EnumAudioEndpoints(g_flow, DEVICE_STATE_ACTIVE, &devices);
    if (SUCCEEDED(hr) && devices) {
        UINT count = 0;
        if (FAILED(devices->GetCount(&count))) completeScan = false;
        if (diagnostics) {
            Wh_Log(L"Mixer: scanning %u active playback outputs", count);
        }
        for (UINT i = 0; i < count; ++i) {
            IMMDevice* device = nullptr;
            if (FAILED(devices->Item(i, &device)) || !device) {
                completeScan = false;
                continue;
            }
            LPWSTR id = nullptr;
            bool isDefault = false;
            if (SUCCEEDED(device->GetId(&id)) && id) {
                isDefault = !defaultId.empty() && defaultId == id;
                if (isDefault) g_masterStartVolume = Wh_GetIntValue(DeviceStartKey(id).c_str(), -1);
                if (isDefault && g_flow == eRender) {
                    g_masterEqOn = EqualizerActive(L"eq_v1:" + EndpointGuid(id));
                    g_masterLoudMaxOn = LoudMaxOnFor(L"dev:" + EndpointGuid(id));
                }
                if (buildRows) {
                    const std::wstring& renamed = StoredSourceName(L"device:" + std::wstring(id));
                    // A name given in the mixer is shown as given.
                    outputNames.emplace_back(id, renamed.empty()
                        ? ShortOutputName(ReadEndpointName(device)) : renamed);
                }
                if (g_showOutputs && buildRows) {
                    AppendOutputRow(device, id, isDefault);
                }
                CoTaskMemFree(id);
            }
            if (!AppendDeviceSessions(device, isDefault, diagnostics, buildRows,
                                      groups)) {
                completeScan = false;
            }
            device->Release();
        }
        devices->Release();
    } else {
        completeScan = false;
        if (diagnostics) {
            Wh_Log(L"Mixer: playback collection failed: 0x%08lX",
                   static_cast<unsigned long>(hr));
        }
        // Preserve default-device functionality if collection enumeration fails.
        if (defaultDevice) {
            AppendDeviceSessions(defaultDevice, true, diagnostics, buildRows,
                                 groups);
        }
    }
    if (defaultDevice) {
        defaultDevice->Release();
    }
    if (renderDefault) renderDefault->Release();
    enumerator->Release();

    if (g_flow == eRender && buildRows) AppendMonitorRows();

    if (g_flow == eRender) {
        PruneDefaultVolumes(completeScan);
        PrunePlaybackHistory();
    }
    g_liveDefaultSessions.clear();
    g_liveSessions.clear();

    std::erase_if(*g_apps, [](const AppSession& app) {
        return !app.listed || app.hidden;
    });
    for (auto& app : *g_apps) {
        if (!app.IsDevice() && !app.virtualInput) app.icon = GetExecutableIcon(app.executablePath);
    }
    for (auto& app : *g_apps) {
        if (g_flow != eRender) break;  // Effects belong to playback.
        app.loudMaxOn = app.IsDevice()
            ? LoudMaxOnFor(L"dev:" + EndpointGuid(app.deviceId))
            : !app.executablePath.empty() &&
                  LoudMaxOnFor(L"app:" + ExeFileName(app.executablePath));
        app.loudnessOn = !app.IsDevice() && !app.executablePath.empty() &&
                         LoudnessOnFor(ExeFileName(app.executablePath));
        app.channelsOn = !app.IsDevice() && !app.executablePath.empty() &&
                         ChannelMixOnFor(ExeFileName(app.executablePath));
        app.eqOn = app.IsDevice()
            ? EqualizerActive(L"eq_v1:" + EndpointGuid(app.deviceId))
            : !app.executablePath.empty() &&
                  EqualizerActive(L"appEq_v1:" + ExeFileName(app.executablePath));
    }
    // Unlocked apps at 100% in Windows show how far they are raised. Turned
    // down below 100% elsewhere, they drop their boost.
    for (auto& app : *g_apps) {
        if (g_flow != eRender || app.IsDevice() || !BoostUnlocked(app.executablePath)) continue;
        int percent = BoostPercent(app.executablePath);
        if (app.volume >= 0.995f) {
            app.volume = percent / 100.0f;
        } else if (percent > 100) {
            SetBoostPercent(app.executablePath, 100);
        }
    }
    // Mark apps fixed to an output, so it is clear which follow the default.
    if (buildRows && g_flow == eRender) {
        if (IAudioPolicyConfigFactory* config = CreateAudioPolicyConfig()) {
            for (auto& app : *g_apps) {
                if (app.IsDevice()) continue;
                DWORD processId = !app.processIds.empty() ? app.processIds.front()
                                                         : app.processId;
                if (!processId) continue;
                std::wstring routed = HeardOutput(config, processId, app.executablePath);
                if (routed.empty()) continue;
                app.routedOutput = L"another output";
                for (const auto& [id, name] : outputNames) {
                    if (_wcsicmp(id.c_str(), routed.c_str()) == 0) {
                        app.routedOutput = name;
                        break;
                    }
                }
            }
            ReleaseAudioPolicyConfig(config);
        }
    }
    for (auto& app : *g_apps) {
        if (app.IsDevice()) {
            app.startVolume = Wh_GetIntValue(DeviceStartKey(app.deviceId).c_str(), -1);
        } else if (g_flow == eRender && !app.executablePath.empty()) {
            const CustomApp* rule = FindCustomApp(app.executablePath, app.name);
            app.startVolume = rule ? rule->defaultVolume : -1;
        }
    }
    std::stable_sort(g_apps->begin(), g_apps->end(), AppSortOrder);
    if (diagnostics) {
        std::wstring names;
        for (const auto& app : *g_apps) names += (names.empty() ? L"" : L", ") + app.name;
        Wh_Log(L"Mixer: the list shows: %s (System sounds hidden: %d)", names.c_str(),
               g_permanentlyHiddenSources.count(SYSTEM_SOUNDS_KEY) != 0 ? 1 : 0);
    }

    RestoreSelectedRow();
    g_audioAvailable = (g_flow == eCapture ? g_inputVolume : g_endpointVolume) != nullptr ||
                       !g_apps->empty();
    ClampPageOffset();
    RefreshMasterIcons(hWnd);
    InvalidateRect(hWnd, nullptr, FALSE);
}

bool EnsureDefaultEndpointVolume() {
    if (g_endpointVolume) {
        return true;
    }
    // Fallback path: no live binding yet. Bind through the helper so the
    // callback is registered against the current window.
    return BindDefaultEndpointVolume(g_hWnd.load());
}

// Runs `call` on the default endpoint's volume. If it fails, the binding is
// probably dead (the Windows audio service restarted, or the device was
// removed or replaced, and no notification arrives for that), so it's bound
// again and the call retried once. Re-binding is limited to once every 2
// seconds, so a missing device doesn't make every call slow.
template <typename F>
HRESULT WithEndpointVolume(F call) {
    if (!EnsureDefaultEndpointVolume()) return E_FAIL;
    HRESULT hr = call(g_endpointVolume);
    if (FAILED(hr)) {
        static ULONGLONG lastRebind = 0;
        ULONGLONG now = GetTickCount64();
        if (now - lastRebind >= 2000) {
            lastRebind = now;
            Wh_Log(L"Mixer: endpoint volume call failed (0x%08lX); binding again",
                   static_cast<unsigned long>(hr));
            ReleaseDefaultEndpointVolume();
            if (EnsureDefaultEndpointVolume()) hr = call(g_endpointVolume);
        }
    }
    return hr;
}

float GetMasterVolume() {
    float value = 0.0f;
    WithEndpointVolume([&](IAudioEndpointVolume* volume) {
        return volume->GetMasterVolumeLevelScalar(&value);
    });
    return value;
}

bool GetMasterMuted() {
    BOOL muted = FALSE;
    WithEndpointVolume([&](IAudioEndpointVolume* volume) { return volume->GetMute(&muted); });
    return muted != FALSE;
}

// The master row's own values: the playback output, or the microphone in its tab.
float GetRowMasterVolume() {
    if (g_flow == eCapture) {
        float value = 0.0f;
        if (g_inputVolume) g_inputVolume->GetMasterVolumeLevelScalar(&value);
        return value;
    }
    return GetMasterVolume();
}

bool GetRowMasterMuted() {
    if (g_flow == eCapture) {
        BOOL muted = FALSE;
        if (g_inputVolume) g_inputVolume->GetMute(&muted);
        return muted != FALSE;
    }
    return GetMasterMuted();
}

float GetAppPeak(const AppSession& app) {
    float peak = 0.0f;
    for (auto* meter : app.meters) {
        float value = 0.0f;
        if (SUCCEEDED(meter->GetPeakValue(&value))) {
            peak = std::max(peak, value);
        }
    }
    return peak;
}

// How far a row's slider reaches: 100%, or 500% for apps unlocked above it.
float RowMaxLevel(int row) {
    if (row < 0 || row >= static_cast<int>(g_apps->size())) return 1.0f;
    const AppSession& app = (*g_apps)[row];
    return (!app.IsDevice() || app.virtualInput) && BoostUnlocked(app.executablePath) ? BOOST_MAX : 1.0f;
}

// Setting an output's volume can take a while: USB and wireless headsets hand it on to their
// hardware, and a slider sends dozens of values a second. So it is done on a thread of its own, and
// only the latest value counts.
SRWLOCK g_deviceVolumeLock = SRWLOCK_INIT;
std::unordered_map<std::wstring, float> g_deviceVolumePending;
HANDLE g_deviceVolumeWake = nullptr;
HANDLE g_deviceVolumeThread = nullptr;

DWORD WINAPI DeviceVolumeThread(void*) {
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* enumerator = nullptr;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                     __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));
    std::unordered_map<std::wstring, IAudioEndpointVolume*> known;
    for (;;) {
        WaitForSingleObject(g_deviceVolumeWake, INFINITE);
        std::unordered_map<std::wstring, float> jobs;
        AcquireSRWLockExclusive(&g_deviceVolumeLock);
        jobs.swap(g_deviceVolumePending);
        ReleaseSRWLockExclusive(&g_deviceVolumeLock);
        for (const auto& [id, value] : jobs) {
            for (int attempt = 0; attempt < 2; ++attempt) {
                IAudioEndpointVolume*& volume = known[id];
                if (!volume && enumerator) {
                    IMMDevice* device = nullptr;
                    if (SUCCEEDED(enumerator->GetDevice(id.c_str(), &device)) && device) {
                        device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr,
                                         reinterpret_cast<void**>(&volume));
                        device->Release();
                    }
                }
                if (!volume) break;
                HRESULT result = volume->SetMasterVolumeLevelScalar(value, nullptr);
                if (SUCCEEDED(result)) {
                    if (value > 0.0f) volume->SetMute(FALSE, nullptr);
                    break;
                }
                volume->Release();  // The device went away or the service restarted: look it up again.
                volume = nullptr;
            }
        }
    }
    if (SUCCEEDED(com)) CoUninitialize();
    return 0;
}

void QueueDeviceVolume(const std::wstring& id, float value) {
    if (!g_deviceVolumeThread) {
        g_deviceVolumeWake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (g_deviceVolumeWake) {
            g_deviceVolumeThread = CreateThread(nullptr, 0, DeviceVolumeThread, nullptr, 0, nullptr);
        }
        if (!g_deviceVolumeThread) return;
    }
    AcquireSRWLockExclusive(&g_deviceVolumeLock);
    g_deviceVolumePending[id] = value;
    ReleaseSRWLockExclusive(&g_deviceVolumeLock);
    SetEvent(g_deviceVolumeWake);
}

void SetRowVolume(int row, float value) {
    // Above 100%, Windows stays at 100% and the boost makes up the rest.
    float level = std::clamp(value, 0.0f, RowMaxLevel(row));
    value = ClampVolume(value);
    if (row == DRAG_MASTER && g_flow == eCapture) {
        if (g_inputVolume) {
            g_inputVolume->SetMasterVolumeLevelScalar(value, nullptr);
            if (value > 0.0f) g_inputVolume->SetMute(FALSE, nullptr);
        }
        return;
    }
    if (row == DRAG_MASTER) {
        if (g_endpointVolume) {
            WithEndpointVolume([&](IAudioEndpointVolume* volume) {
                HRESULT hr = volume->SetMasterVolumeLevelScalar(value, nullptr);
                if (SUCCEEDED(hr) && value > 0.0f) volume->SetMute(FALSE, nullptr);
                return hr;
            });
            RefreshMasterIcons(g_hWnd.load(), false);
        }
        return;
    }

    if (row < 0 || row >= static_cast<int>(g_apps->size())) {
        return;
    }
    AppSession& app = (*g_apps)[row];
    if (app.virtualInput) {
        // A listened-to input has no Windows session: its volume is the mixer's own.
        SetMonitorVolume(app.executablePath, value);
        app.volume = level;
        // Like an app: above 100 % the engine makes up the rest (the boost, up to 500 %).
        if (level > 1.0f || BoostUnlocked(app.executablePath)) {
            SetBoostPercent(app.executablePath, static_cast<int>(std::lround(std::max(level, 1.0f) * 100.0f)));
        }
        if (value > 0.0f) app.muted = false;
        return;
    }
    if (app.deviceVolume) {
        if (!app.deviceId.empty()) {
            QueueDeviceVolume(app.deviceId, value);  // The slider does not wait for the hardware.
        } else {
            app.deviceVolume->SetMasterVolumeLevelScalar(value, nullptr);
            if (value > 0.0f) app.deviceVolume->SetMute(FALSE, nullptr);
        }
        if (app.defaultOutput) RefreshMasterIcons(g_hWnd.load(), false);
    }
    for (auto* control : app.volumeControls) {
        control->SetMasterVolume(value, nullptr);
        if (value > 0.0f) {
            control->SetMute(FALSE, nullptr);
        }
    }
    app.volume = level;
    if (level > 1.0f || (!app.IsDevice() && BoostUnlocked(app.executablePath))) {
        SetBoostPercent(app.executablePath,
                        static_cast<int>(std::lround(std::max(level, 1.0f) * 100.0f)));
    }
    if (value > 0.0f) {
        app.muted = false;
    }
}

void ToggleRowMute(int row) {
    if (row == DRAG_MASTER && g_flow == eCapture) {
        if (g_inputVolume) {
            BOOL muted = FALSE;
            g_inputVolume->GetMute(&muted);
            g_inputVolume->SetMute(!muted, nullptr);
            InvalidateRect(g_hWnd.load(), nullptr, FALSE);
        }
        return;
    }
    if (row == DRAG_MASTER) {
        if (EnsureDefaultEndpointVolume()) {
            bool muted = GetMasterMuted();
            g_endpointVolume->SetMute(!muted, nullptr);
            RefreshMasterIcons(g_hWnd.load(), false);
            InvalidateRect(g_hWnd.load(), nullptr, FALSE);
        }
        return;
    }

    if (row < 0 || row >= static_cast<int>(g_apps->size())) {
        return;
    }
    AppSession& app = (*g_apps)[row];
    if (app.virtualInput) {
        app.muted = !app.muted;
        SetMonitorMuted(app.executablePath, app.muted);
        return;
    }
    bool newMuted = !app.muted;
    if (app.deviceVolume) {
        app.deviceVolume->SetMute(newMuted, nullptr);
        if (app.defaultOutput) RefreshMasterIcons(g_hWnd.load(), false);
    }
    for (auto* control : app.volumeControls) {
        control->SetMute(newMuted, nullptr);
    }
    app.muted = newMuted;
}

bool IsCompact() {
    return g_settings.compactMode;
}

int HeaderHeight() {
    return Scale(IsCompact() ? 48 : 82);
}

int RowHeight() {
    // Compact Windows 10 rows are taller to fit the bar-shaped thumb.
    if (IsCompact()) return Scale(IsWindows10Style() ? 56 : 48);
    return Scale(62);
}

// Between master volume and the apps, with a faint line halfway.
int SectionGap() {
    return Scale(10);
}

int FooterHeight(bool paged) {
    // Without page buttons, the footer leaves as much room under the last
    // row as there is beside the rows: its 10 plus the 2 a row ends short.
    if (IsCompact()) {
        return Scale(paged ? 42 : 10);
    }
    return Scale(paged ? 48 : 10);
}

int RowTop(int visibleRow) {
    return HeaderHeight() + visibleRow * RowHeight() +
           (visibleRow > 0 ? SectionGap() : 0);
}

int PopupWidth() {
    return Scale(IsCompact() ? 360 : 400);
}

RECT TitleRect(int width) {
    return {Scale(24), Scale(11), width - Scale(132), Scale(39)};
}

// Left of the outputs button: switches between playback and microphone.
RECT FlowButtonRect(int width) {
    if (IsCompact()) {
        return {width - Scale(112), Scale(12), width - Scale(84), Scale(40)};
    }
    return {width - Scale(119), Scale(9), width - Scale(87), Scale(41)};
}

// Left of the close button: shows or hides the output device rows.
RECT OutputsButtonRect(int width) {
    if (IsCompact()) {
        return {width - Scale(80), Scale(12), width - Scale(52), Scale(40)};
    }
    return {width - Scale(83), Scale(9), width - Scale(51), Scale(41)};
}

RECT CloseButtonRect(int width) {
    if (IsCompact()) {
        return {width - Scale(44), Scale(12), width - Scale(16), Scale(40)};
    }
    // Centered over the arrow of the output picker below.
    return {width - Scale(47), Scale(9), width - Scale(15), Scale(41)};
}

RECT OutputPickerRect(int width) {
    if (IsCompact()) {
        // As far from the top edge as from the side.
        return {Scale(12), Scale(12), width - Scale(120), Scale(40)};
    }
    return {Scale(12), Scale(44), width - Scale(12), Scale(74)};
}

RECT DeviceNameRect(int width) {
    RECT picker = OutputPickerRect(width);
    return {picker.left + Scale(38), picker.top,
            picker.right - Scale(34), picker.bottom};
}

int PageSize() {
    return std::min(g_settings.maxVisibleApps, g_availableAppRows);
}

int AppRowsForHeight(int availableHeight) {
    int fixedHeight = HeaderHeight() + RowHeight() + SectionGap() +
                      FooterHeight(false);
    return ClampInt((availableHeight - fixedHeight) / RowHeight(), 1, 100);
}

// The app list scrolls a row at a time; g_scrollRow is the first app shown.
int MaxScrollRow() {
    return std::max(0, static_cast<int>(g_apps->size()) - PageSize());
}

bool AppsScroll() {
    return MaxScrollRow() > 0;
}

void ClampPageOffset() {
    g_scrollRow = ClampInt(g_scrollRow, 0, MaxScrollRow());
    // Keep the row chosen with the keyboard in view.
    if (g_settings.keyboardControls && g_selectedRow >= 0) {
        if (g_selectedRow < g_scrollRow) {
            g_scrollRow = g_selectedRow;
        } else if (g_selectedRow >= g_scrollRow + PageSize()) {
            g_scrollRow = g_selectedRow - PageSize() + 1;
        }
    }
}

// Scrolls the app list by `delta` rows.
void ChangePage(HWND window, int delta) {
    CancelNameTooltip(window);
    if (g_dragRow != DRAG_NONE || g_volumeEntry || !AppsScroll()) return;
    int scrolled = ClampInt(g_scrollRow + delta, 0, MaxScrollRow());
    if (scrolled == g_scrollRow) return;
    g_scrollRow = scrolled;
    g_hoverRow = DRAG_NONE;
    InvalidateRect(window, nullptr, FALSE);
}

// The scroll bar beside the rows. It is drawn slim but takes clicks across the
// whole margin right of the rows.
bool g_scrollDragging;
bool g_scrollHovered;
int g_scrollGrab;  // Where on the thumb it was taken hold of.

RECT ScrollTrackRect(int clientWidth) {
    return {clientWidth - Scale(12), RowTop(1) + Scale(6),
            clientWidth, RowTop(1 + PageSize()) - Scale(8)};
}

RECT ScrollThumbRect(int clientWidth) {
    RECT track = ScrollTrackRect(clientWidth);
    int height = track.bottom - track.top;
    int total = std::max(1, static_cast<int>(g_apps->size()));
    int thumb = std::min(height, std::max(Scale(32), height * PageSize() / total));
    int offset = (height - thumb) * g_scrollRow / std::max(1, MaxScrollRow());
    return {track.left, track.top + offset, track.right, track.top + offset + thumb};
}

// Scrolls so the thumb's top lands at `thumbTop`.
void ScrollToThumb(HWND window, int clientWidth, int thumbTop) {
    RECT track = ScrollTrackRect(clientWidth);
    RECT thumb = ScrollThumbRect(clientWidth);
    int range = (track.bottom - track.top) - (thumb.bottom - thumb.top);
    if (range <= 0) return;
    int position = ClampInt(thumbTop - track.top, 0, range);
    int row = (position * MaxScrollRow() + range / 2) / range;
    if (row == g_scrollRow) return;
    g_scrollRow = row;
    g_hoverRow = DRAG_NONE;
    InvalidateRect(window, nullptr, FALSE);
}

int VisibleAppCount() {
    return std::min(std::max(0, static_cast<int>(g_apps->size()) - g_scrollRow),
                    PageSize());
}

int PopupHeight() {
    int slots = std::max(1, std::min(static_cast<int>(g_apps->size()),
                                   PageSize()));
    return HeaderHeight() + RowHeight() * (1 + slots) + SectionGap() +
           FooterHeight(false);
}

RECT IconTileRectForRow(int visibleRow) {
    int top = RowTop(visibleRow);
    if (IsCompact()) {
        int offset = Scale(IsWindows10Style() ? 12 : 8);
        return {Scale(22), top + offset, Scale(52), top + offset + Scale(30)};
    }
    return {Scale(22), top + Scale(12), Scale(58), top + Scale(48)};
}

RECT NameRectForRow(int visibleRow, int clientWidth) {
    int top = RowTop(visibleRow);
    if (IsCompact()) {
        return {Scale(62), top + Scale(2), clientWidth - Scale(132),
                top + Scale(26)};
    }
    return {Scale(68), top + Scale(7), clientWidth - Scale(132),
            top + Scale(31)};
}

RECT PercentRectForRow(int visibleRow, int clientWidth) {
    int top = RowTop(visibleRow);
    if (IsCompact()) {
        if (visibleRow == 0) {
            return {clientWidth - Scale(100), top + Scale(2),
                    clientWidth - Scale(50), top + Scale(26)};
        }
        return {clientWidth - Scale(124), top + Scale(2),
                clientWidth - Scale(76), top + Scale(26)};
    }
    return {clientWidth - Scale(124), top + Scale(7),
            clientWidth - Scale(78), top + Scale(31)};
}

RECT SliderRectForRow(int visibleRow, int clientWidth) {
    int top = RowTop(visibleRow);
    if (IsCompact()) {
        int offset = Scale(IsWindows10Style() ? 36 : 30);
        return {Scale(62), top + offset, clientWidth - Scale(28),
                top + offset + Scale(8)};
    }
    return {Scale(68), top + Scale(38), clientWidth - Scale(78),
            top + Scale(46)};
}

RECT MuteRectForRow(int visibleRow, int clientWidth) {
    int top = RowTop(visibleRow);
    if (IsCompact()) {
        return {clientWidth - Scale(46), top + Scale(2),
                clientWidth - Scale(22), top + Scale(26)};
    }
    return {clientWidth - Scale(58), top + Scale(visibleRow ? 28 : 12),
            clientWidth - Scale(22), top + Scale(visibleRow ? 58 : 48)};
}

RECT PinRectForRow(int visibleRow, int clientWidth) {
    int top = RowTop(visibleRow);
    if (IsCompact()) {
        return {clientWidth - Scale(72), top + Scale(2),
                clientWidth - Scale(48), top + Scale(26)};
    }
    return {clientWidth - Scale(56), top + Scale(1),
            clientWidth - Scale(24), top + Scale(28)};
}

// Outer thumb size in pixels: the round Windows 11 thumb, border included, or
// the bar-shaped Windows 10 one.
float SliderThumbWidth() {
    return (IsWindows10Style() ? 8.0f : 20.0f) * g_dpi / 96.0f;
}

float SliderThumbHeight() {
    return (IsWindows10Style() ? 24.0f : 20.0f) * g_dpi / 96.0f;
}

// Distance from either end of the rail to the thumb's center at 0% and 100%.
// As in the native sliders, the thumb stays within the rail instead of being
// centered on its ends. The solid surface of the Windows 11 thumb still
// overlaps each end by a pixel, so no sliver of the rail shows through its
// translucent border.
float SliderThumbInset() {
    float half = SliderThumbWidth() * 0.5f;
    return IsWindows10Style() ? half : half - 2.0f * g_dpi / 96.0f;
}

// Horizontal center of the thumb for a volume in the 0 to 1 range.
float SliderThumbCenter(const RECT& slider, float volume) {
    float inset = SliderThumbInset();
    float travel = std::max(
        0.0f, static_cast<float>(slider.right - slider.left) - 2.0f * inset);
    return slider.left + inset + travel * ClampVolume(volume);
}

bool SliderThumbContains(const RECT& slider, float volume, POINT point) {
    float dx = point.x + 0.5f - SliderThumbCenter(slider, volume);
    float dy = point.y + 0.5f - (slider.top + slider.bottom) * 0.5f;
    if (IsWindows10Style()) {
        // The bar is narrow, so allow a little slack on either side.
        return std::abs(dx) <= SliderThumbWidth() * 0.5f + 2.0f * g_dpi / 96.0f &&
               std::abs(dy) <= SliderThumbHeight() * 0.5f;
    }
    float radius = SliderThumbWidth() * 0.5f;
    return dx * dx + dy * dy <= radius * radius;
}

RECT SliderHitRect(int visibleRow, int clientWidth) {
    RECT rect = SliderRectForRow(visibleRow, clientWidth);
    int vertical = IsCompact() ? 8 : 12;
    InflateRect(&rect, Scale(10), Scale(vertical));
    return rect;
}

int RowFromPoint(POINT point) {
    if (point.y < HeaderHeight()) {
        return DRAG_NONE;
    }
    int relative = point.y - HeaderHeight();
    int visibleRow = 0;
    if (relative >= RowHeight()) {
        if (relative < RowHeight() + SectionGap()) {
            return DRAG_NONE;
        }
        visibleRow = 1 + (relative - RowHeight() - SectionGap()) / RowHeight();
    }
    if (visibleRow > VisibleAppCount()) {
        return DRAG_NONE;
    }
    if (visibleRow == 0) {
        return DRAG_MASTER;
    }
    int appIndex = g_scrollRow + visibleRow - 1;
    return appIndex >= 0 && appIndex < static_cast<int>(g_apps->size())
               ? appIndex
               : DRAG_NONE;
}

int VisibleRowForDataRow(int row) {
    return row == DRAG_MASTER ? 0 : row - g_scrollRow + 1;
}

// A separate owned popup keeps the native text editor visible over the
// UpdateLayeredWindow surface. Audio enumeration is paused until it closes.
bool ParseVolumePercent(const std::wstring& text, int& value) {
    if (text.empty() || text.size() > 3) return false;
    int parsed = 0;
    for (wchar_t c : text) {
        if (c < L'0' || c > L'9') return false;
        parsed = parsed * 10 + (c - L'0');
    }
    if (parsed > 500) return false;  // Rows that cannot go above 100% stop there.
    value = parsed;
    return true;
}

void RegisterMuteHotkeys(HWND window);

bool FinishVolumeEntry(HWND owner, bool commit, bool returnFocus) {
    if (!g_volumeEntry) return true;
    bool hotkeyEntry = g_hotkeyEntry;
    if (hotkeyEntry) commit = false;
    if (commit && g_nameEntry) {
        WCHAR text[128]{};
        GetWindowTextW(g_volumeEdit, text, ARRAYSIZE(text));
        std::wstring name = TrimSetting(text);
        if (!SaveSourceName(g_nameEntryKey, name)) {
            Wh_Log(L"Mixer: could not save the new name");
        }
        // Takes effect now; the next scan reads the original name back when
        // the new one is empty.
        if (!name.empty()) {
            for (auto& app : *g_apps) {
                if (app.pinKey == g_nameEntryKey) app.name = name;
            }
        } else {
            PostMessageW(owner, WM_APP_AUDIO_CHANGED, 0, 0);
        }
    } else if (commit) {
        WCHAR text[32]{};
        GetWindowTextW(g_volumeEdit, text, ARRAYSIZE(text));
        int percent;
        if (!ParseVolumePercent(text, percent)) {
            MessageBeep(MB_ICONWARNING);
            SendMessageW(g_volumeEdit, EM_SETSEL, 0, -1);
            return false;
        }
        SetRowVolume(g_volumeEntryRow, percent / 100.0f);
    }
    HWND entry = g_volumeEntry;
    g_volumeEntry = nullptr;
    g_volumeEdit = nullptr;
    g_volumeEntryRow = DRAG_NONE;
    g_nameEntry = false;
    g_nameEntryKey.clear();
    g_hotkeyEntry = false;
    g_hotkeyEntryPath.clear();
    ++g_volumeEntryGeneration;
    DestroyWindow(entry);
    if (hotkeyEntry) RegisterMuteHotkeys(owner);
    if (returnFocus && IsWindowVisible(owner)) SetFocus(owner);
    InvalidateRect(owner, nullptr, FALSE);
    return true;
}

LRESULT CALLBACK VolumeEntryProc(HWND window, UINT message, WPARAM wParam,
                                 LPARAM lParam) {
    HWND owner = GetWindow(window, GW_OWNER);
    switch (message) {
        case WM_SETFOCUS:
            if (g_volumeEdit) SetFocus(g_volumeEdit);
            return 0;
        case WM_COMMAND:
            if (HIWORD(wParam) != EN_KILLFOCUS) break;
            PostMessageW(owner, WM_APP_FINISH_VOLUME_ENTRY, 0,
                         static_cast<LPARAM>(g_volumeEntryGeneration));
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) {
                PostMessageW(owner, WM_APP_FINISH_VOLUME_ENTRY, 0,
                             static_cast<LPARAM>(g_volumeEntryGeneration));
                SetTimer(owner, TIMER_CHECK_FOCUS, 150, nullptr);
            }
            break;
        case WM_CLOSE:
            PostMessageW(owner, WM_APP_FINISH_VOLUME_ENTRY, 0,
                         static_cast<LPARAM>(g_volumeEntryGeneration));
            return 0;
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, g_theme.text);
            SetBkColor(dc, g_theme.panel);
            SetDCBrushColor(dc, g_theme.panel);
            return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
        }
        case WM_ERASEBKGND: {
            RECT rect{};
            GetClientRect(window, &rect);
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetDCBrushColor(dc, g_theme.panel);
            FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            return 1;
        }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void BeginVolumeEntry(HWND owner, int row) {
    if (!g_settings.exactVolumeEntry || g_dragRow != DRAG_NONE ||
        (row == DRAG_MASTER && !(g_flow == eCapture ? g_inputVolume : g_endpointVolume))) return;
    if (row != DRAG_MASTER && (row < 0 || row >= static_cast<int>(g_apps->size()))) return;
    FinishVolumeEntry(owner, false, false);
    RECT client{};
    GetClientRect(owner, &client);
    RECT rect = PercentRectForRow(VisibleRowForDataRow(row), client.right);
    POINT origin{rect.left, rect.top};
    ClientToScreen(owner, &origin);
    HINSTANCE instance = GetModuleHandleW(nullptr);
    g_volumeEntryRow = row;
    ++g_volumeEntryGeneration;
    g_volumeEntry = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        VOLUME_ENTRY_CLASS, L"Volume (0-100%)", WS_POPUP | WS_BORDER,
        origin.x, origin.y, rect.right - rect.left, rect.bottom - rect.top,
        owner, nullptr, instance, nullptr);
    if (!g_volumeEntry) {
        g_volumeEntryRow = DRAG_NONE;
        return;
    }
    GetClientRect(g_volumeEntry, &client);
    g_volumeEdit = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_CENTER | ES_NUMBER | ES_AUTOHSCROLL,
        0, 0, client.right, client.bottom,
        g_volumeEntry, nullptr, instance, nullptr);
    if (!g_volumeEdit) {
        FinishVolumeEntry(owner, false, false);
        return;
    }
    SendMessageW(g_volumeEdit, EM_SETLIMITTEXT, 16, 0);
    SendMessageW(g_volumeEdit, WM_SETFONT,
                 reinterpret_cast<WPARAM>(g_entryFont ? g_entryFont : g_bodyFont), TRUE);
    float volume = row == DRAG_MASTER ? GetRowMasterVolume() : (*g_apps)[row].volume;
    std::wstring text = std::to_wstring(static_cast<int>(volume * 100.0f + 0.5f));
    SetWindowTextW(g_volumeEdit, text.c_str());
    SendMessageW(g_volumeEdit, EM_SETSEL, 0, -1);
    ShowWindow(g_volumeEntry, SW_SHOW);
    SetFocus(g_volumeEdit);
}

// Mute hotkeys per app. Each one toggles every audio session of that
// executable, whether or not the mixer is open, so they also work in games.
struct MuteHotkey {
    std::wstring path;  // Lowercase executable path.
    UINT modifiers = 0;
    UINT key = 0;
};
std::vector<MuteHotkey> g_hotkeys;
bool g_hotkeysLoaded;
constexpr int HOTKEY_FIRST_ID = 0x4100;
constexpr PCWSTR HOTKEYS_VALUE = L"muteHotkeys_v1";

std::wstring LowerPath(std::wstring path) {
    for (auto& c : path) c = static_cast<wchar_t>(std::towlower(c));
    return path;
}

// Kept in the mod's storage as one "modifiers|key|path" line per hotkey.
void LoadMuteHotkeys() {
    if (g_hotkeysLoaded) return;
    g_hotkeysLoaded = true;
    std::vector<wchar_t> buffer(16384);
    Wh_GetStringValue(HOTKEYS_VALUE, buffer.data(), buffer.size());
    buffer.back() = L'\0';
    std::wstring stored(buffer.data());
    size_t start = 0;
    while (start < stored.size()) {
        size_t end = stored.find(L'\n', start);
        if (end == std::wstring::npos) end = stored.size();
        std::wstring line = stored.substr(start, end - start);
        start = end + 1;
        size_t first = line.find(L'|'), second = line.find(L'|', first + 1);
        if (first == std::wstring::npos || second == std::wstring::npos) continue;
        MuteHotkey hotkey;
        hotkey.modifiers = wcstoul(line.substr(0, first).c_str(), nullptr, 10);
        hotkey.key = wcstoul(line.substr(first + 1, second - first - 1).c_str(), nullptr, 10);
        hotkey.path = LowerPath(line.substr(second + 1));
        if (hotkey.key && !hotkey.path.empty()) g_hotkeys.push_back(std::move(hotkey));
    }
}

void SaveMuteHotkeys() {
    std::wstring stored;
    for (const auto& hotkey : g_hotkeys) {
        stored += std::to_wstring(hotkey.modifiers) + L"|" + std::to_wstring(hotkey.key) +
                  L"|" + hotkey.path + L"\n";
    }
    if (stored.empty()) {
        Wh_DeleteValue(HOTKEYS_VALUE);
    } else if (!Wh_SetStringValue(HOTKEYS_VALUE, stored.c_str())) {
        Wh_Log(L"Mixer: could not save the mute hotkeys");
    }
}

void UnregisterMuteHotkeys(HWND window) {
    for (size_t i = 0; i < g_hotkeys.size() + 8; ++i) {
        UnregisterHotKey(window, HOTKEY_FIRST_ID + static_cast<int>(i));
    }
}

void RegisterMuteHotkeys(HWND window) {
    LoadMuteHotkeys();
    UnregisterMuteHotkeys(window);
    for (size_t i = 0; i < g_hotkeys.size(); ++i) {
        if (!RegisterHotKey(window, HOTKEY_FIRST_ID + static_cast<int>(i),
                            g_hotkeys[i].modifiers | MOD_NOREPEAT, g_hotkeys[i].key)) {
            Wh_Log(L"Mixer: mute hotkey for %s is taken by another program, error %lu",
                   g_hotkeys[i].path.c_str(), GetLastError());
        }
    }
}

// The key that opens the mixer, at the monitor where the pointer is: "modifiers|key" under "openHotkey_v1".
constexpr int HOTKEY_OPEN_ID = 0x40FE;
constexpr PCWSTR OPEN_HOTKEY_VALUE = L"openHotkey_v1";
bool g_openAtPointer;  // The mixer was opened with the key, so it sits at that monitor, not at the tray icon.
RECT g_openAnchor;

void RegisterOpenHotkey(HWND window) {
    UnregisterHotKey(window, HOTKEY_OPEN_ID);
    wchar_t buffer[32]{};
    Wh_GetStringValue(OPEN_HOTKEY_VALUE, buffer, ARRAYSIZE(buffer));
    buffer[ARRAYSIZE(buffer) - 1] = L'\0';
    const wchar_t* bar = wcschr(buffer, L'|');
    if (!bar) return;
    UINT modifiers = wcstoul(buffer, nullptr, 10), key = wcstoul(bar + 1, nullptr, 10);
    if (!key || !modifiers) return;  // A key on its own would take over typing.
    if (!RegisterHotKey(window, HOTKEY_OPEN_ID, modifiers | MOD_NOREPEAT, key)) {
        Wh_Log(L"Mixer: the key that opens the mixer is taken by another program, error %lu", GetLastError());
    }
}

const MuteHotkey* FindMuteHotkey(const std::wstring& path) {
    LoadMuteHotkeys();
    std::wstring lower = LowerPath(path);
    for (const auto& hotkey : g_hotkeys) {
        if (hotkey.path == lower) return &hotkey;
    }
    return nullptr;
}

bool IsModifierKey(UINT key) {
    switch (key) {
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
        case VK_MENU: case VK_LMENU: case VK_RMENU:
        case VK_LWIN: case VK_RWIN:
            return true;
    }
    return false;
}

UINT HeldModifiers() {
    UINT modifiers = 0;
    if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= MOD_CONTROL;
    if (GetKeyState(VK_MENU) & 0x8000) modifiers |= MOD_ALT;
    if (GetKeyState(VK_SHIFT) & 0x8000) modifiers |= MOD_SHIFT;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) modifiers |= MOD_WIN;
    return modifiers;
}

std::wstring HotkeyText(UINT modifiers, UINT key) {
    std::wstring text;
    if (modifiers & MOD_CONTROL) text += L"Ctrl+";
    if (modifiers & MOD_ALT) text += L"Alt+";
    if (modifiers & MOD_SHIFT) text += L"Shift+";
    if (modifiers & MOD_WIN) text += L"Win+";
    if (!key) return text;
    UINT scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
    switch (key) {
        // Keys whose scan code needs the extended bit to be named right.
        case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR:
        case VK_NEXT: case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
        case VK_DIVIDE: case VK_NUMLOCK:
            scan |= 0x100;
            break;
    }
    wchar_t name[64]{};
    if (scan && GetKeyNameTextW(static_cast<LONG>(scan << 16), name, ARRAYSIZE(name)) > 0) {
        text += name;
    } else {
        text += L"Key " + std::to_wstring(key);
    }
    return text;
}

// Mutes every session of the executable, or unmutes them when all are muted.
void ToggleMuteForPath(const std::wstring& path) {
    if (path.rfind(L"in:", 0) == 0) {
        // A listened-to input: its mute is the mixer's own.
        SetMonitorMuted(path, !MonitorMuted(path.substr(3)));
        return;
    }
    std::vector<ISimpleAudioVolume*> volumes;
    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                                __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&enumerator)))) {
        return;
    }
    IMMDeviceCollection* devices = nullptr;
    if (SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices))) {
        UINT deviceCount = 0;
        devices->GetCount(&deviceCount);
        for (UINT d = 0; d < deviceCount; ++d) {
            IMMDevice* device = nullptr;
            IAudioSessionManager2* manager = nullptr;
            IAudioSessionEnumerator* sessions = nullptr;
            if (SUCCEEDED(devices->Item(d, &device)) &&
                SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_INPROC_SERVER,
                                           nullptr, reinterpret_cast<void**>(&manager))) &&
                SUCCEEDED(manager->GetSessionEnumerator(&sessions))) {
                int count = 0;
                sessions->GetCount(&count);
                for (int i = 0; i < count; ++i) {
                    IAudioSessionControl* control = nullptr;
                    IAudioSessionControl2* control2 = nullptr;
                    DWORD processId = 0;
                    if (SUCCEEDED(sessions->GetSession(i, &control)) &&
                        SUCCEEDED(control->QueryInterface(__uuidof(IAudioSessionControl2),
                                                          reinterpret_cast<void**>(&control2))) &&
                        SUCCEEDED(control2->GetProcessId(&processId)) && processId &&
                        LowerPath(GetProcessPath(processId)) == path) {
                        ISimpleAudioVolume* volume = nullptr;
                        if (SUCCEEDED(control->QueryInterface(__uuidof(ISimpleAudioVolume),
                                                              reinterpret_cast<void**>(&volume)))) {
                            volumes.push_back(volume);
                        }
                    }
                    if (control2) control2->Release();
                    if (control) control->Release();
                }
            }
            if (sessions) sessions->Release();
            if (manager) manager->Release();
            if (device) device->Release();
        }
        devices->Release();
    }
    enumerator->Release();

    bool anyAudible = false;
    for (auto* volume : volumes) {
        BOOL muted = FALSE;
        if (SUCCEEDED(volume->GetMute(&muted)) && !muted) anyAudible = true;
    }
    for (auto* volume : volumes) {
        volume->SetMute(anyAudible, nullptr);
        volume->Release();
    }
    if (volumes.empty()) {
        Wh_Log(L"Mixer: mute hotkey: %s has no audio right now", path.c_str());
    }
}

void HandleMuteHotkey(HWND window, int id) {
    size_t index = static_cast<size_t>(id - HOTKEY_FIRST_ID);
    if (id < HOTKEY_FIRST_ID || index >= g_hotkeys.size()) return;
    ToggleMuteForPath(g_hotkeys[index].path);
    if (IsWindowVisible(window)) PostMessageW(window, WM_APP_AUDIO_CHANGED, 0, 0);
}

constexpr PCWSTR HOTKEY_PROMPT = L"Press keys \u00B7 Esc cancels \u00B7 Del removes";

// Takes a key combination for the row's app over its name.
void BeginHotkeyEntry(HWND owner, int row) {
    if (g_dragRow != DRAG_NONE || row < 0 || row >= static_cast<int>(g_apps->size()) ||
        (*g_apps)[row].executablePath.empty()) {
        return;
    }
    FinishVolumeEntry(owner, false, false);
    RECT client{};
    GetClientRect(owner, &client);
    RECT rect = NameRectForRow(VisibleRowForDataRow(row), client.right);
    rect.right = std::max(rect.right, rect.left + Scale(240));
    POINT origin{rect.left - Scale(4), rect.top};
    ClientToScreen(owner, &origin);
    HINSTANCE instance = GetModuleHandleW(nullptr);
    g_volumeEntryRow = row;
    g_hotkeyEntry = true;
    g_hotkeyEntryPath = LowerPath((*g_apps)[row].executablePath);
    ++g_volumeEntryGeneration;
    g_volumeEntry = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        VOLUME_ENTRY_CLASS, L"Hotkey", WS_POPUP | WS_BORDER,
        origin.x, origin.y, rect.right - rect.left + Scale(4), rect.bottom - rect.top,
        owner, nullptr, instance, nullptr);
    if (!g_volumeEntry) {
        g_volumeEntryRow = DRAG_NONE;
        g_hotkeyEntry = false;
        g_hotkeyEntryPath.clear();
        return;
    }
    // The current hotkeys are released while a new one is taken, so pressing
    // one of them reaches the entry.
    UnregisterMuteHotkeys(owner);
    GetClientRect(g_volumeEntry, &client);
    g_volumeEdit = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL | ES_READONLY,
        Scale(3), 0, client.right - Scale(3), client.bottom,
        g_volumeEntry, nullptr, instance, nullptr);
    if (!g_volumeEdit) {
        FinishVolumeEntry(owner, false, false);
        return;
    }
    SendMessageW(g_volumeEdit, WM_SETFONT,
                 reinterpret_cast<WPARAM>(g_entryFont ? g_entryFont : g_bodyFont), TRUE);
    SetWindowTextW(g_volumeEdit, HOTKEY_PROMPT);
    ShowWindow(g_volumeEntry, SW_SHOW);
    SetFocus(g_volumeEdit);
}

// Why a combination cannot be a mute hotkey, or null when it can. A global
// hotkey reaches the mixer before any window, so it must never take keys
// that typing or Windows itself relies on: Windows only refuses combinations
// another program registered, not ones that windows handle themselves.
PCWSTR HotkeyRejection(UINT modifiers, UINT key) {
    bool function = key >= VK_F1 && key <= VK_F24;
    bool harmless = (function && key != VK_F12) || key == VK_PAUSE || key == VK_SCROLL ||
                    key == VK_MEDIA_NEXT_TRACK || key == VK_MEDIA_PREV_TRACK ||
                    key == VK_MEDIA_STOP || key == VK_MEDIA_PLAY_PAUSE;
    // Shift alone still types: Shift+M is a capital M.
    if (!(modifiers & (MOD_CONTROL | MOD_ALT | MOD_WIN)) && !harmless) {
        return key == VK_F12 ? L"is reserved by Windows \u00B7 try another"
                             : L"would block typing \u00B7 add Ctrl, Alt or Win";
    }
    struct Combination {
        UINT modifiers;
        UINT key;
    };
    static const Combination used[] = {
        {MOD_ALT, VK_F4}, {MOD_ALT, VK_TAB}, {MOD_ALT | MOD_SHIFT, VK_TAB},
        {MOD_ALT, VK_ESCAPE}, {MOD_ALT, VK_RETURN}, {MOD_ALT, VK_SPACE},
        {MOD_ALT, VK_SNAPSHOT}, {MOD_CONTROL, VK_ESCAPE}, {MOD_CONTROL | MOD_SHIFT, VK_ESCAPE},
        {MOD_CONTROL, VK_TAB}, {MOD_CONTROL | MOD_SHIFT, VK_TAB}, {MOD_CONTROL, VK_F4},
        {MOD_CONTROL, 'A'}, {MOD_CONTROL, 'C'}, {MOD_CONTROL, 'F'}, {MOD_CONTROL, 'N'},
        {MOD_CONTROL, 'S'}, {MOD_CONTROL, 'T'}, {MOD_CONTROL, 'V'}, {MOD_CONTROL, 'W'},
        {MOD_CONTROL, 'X'}, {MOD_CONTROL, 'Y'}, {MOD_CONTROL, 'Z'},
    };
    for (const auto& combination : used) {
        if (combination.modifiers == modifiers && combination.key == key) {
            return L"is used by Windows \u00B7 try another";
        }
    }
    if (key == VK_SNAPSHOT) return L"is used by Windows \u00B7 try another";
    return nullptr;
}

// Every key message for the hotkey entry comes here instead of the edit box.
void HandleHotkeyEntryKey(HWND owner, const MSG& message) {
    bool down = message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN;
    bool up = message.message == WM_KEYUP || message.message == WM_SYSKEYUP;
    if (!down && !up) return;
    UINT key = static_cast<UINT>(message.wParam);
    UINT modifiers = HeldModifiers();
    if (IsModifierKey(key)) {
        // Shows the modifiers held so far.
        std::wstring held = HotkeyText(modifiers, 0);
        SetWindowTextW(g_volumeEdit, held.empty() ? HOTKEY_PROMPT : (held + L"\u2026").c_str());
        return;
    }
    if (!down) return;
    if (!modifiers && key == VK_ESCAPE) {
        FinishVolumeEntry(owner, false, true);
        return;
    }
    if (!(!modifiers && (key == VK_DELETE || key == VK_BACK))) {
        if (PCWSTR reason = HotkeyRejection(modifiers, key)) {
            MessageBeep(MB_ICONWARNING);
            SetWindowTextW(g_volumeEdit, (HotkeyText(modifiers, key) + L" " + reason).c_str());
            return;
        }
    }
    std::wstring path = g_hotkeyEntryPath;
    g_hotkeys.erase(std::remove_if(g_hotkeys.begin(), g_hotkeys.end(),
                                   [&](const MuteHotkey& hotkey) {
                                       return hotkey.path == path ||
                                              (hotkey.key == key && hotkey.modifiers == modifiers);
                                   }),
                    g_hotkeys.end());
    if (!modifiers && (key == VK_DELETE || key == VK_BACK)) {
        SaveMuteHotkeys();
        FinishVolumeEntry(owner, false, true);
        return;
    }
    // Make sure no other program holds the combination before keeping it.
    if (!RegisterHotKey(owner, HOTKEY_FIRST_ID - 1, modifiers | MOD_NOREPEAT, key)) {
        MessageBeep(MB_ICONWARNING);
        SetWindowTextW(g_volumeEdit, (HotkeyText(modifiers, key) +
                                      L" is taken \u00B7 try another").c_str());
        return;
    }
    UnregisterHotKey(owner, HOTKEY_FIRST_ID - 1);
    g_hotkeys.push_back({path, modifiers, key});
    SaveMuteHotkeys();
    FinishVolumeEntry(owner, false, true);
}

// Renames an app in place, over its name.
void BeginNameEntry(HWND owner, int row) {
    if (g_dragRow != DRAG_NONE || row < 0 || row >= static_cast<int>(g_apps->size())) return;
    FinishVolumeEntry(owner, false, false);
    RECT client{};
    GetClientRect(owner, &client);
    RECT rect = NameRectForRow(VisibleRowForDataRow(row), client.right);
    POINT origin{rect.left - Scale(4), rect.top};
    ClientToScreen(owner, &origin);
    HINSTANCE instance = GetModuleHandleW(nullptr);
    g_volumeEntryRow = row;
    g_nameEntry = true;
    g_nameEntryKey = (*g_apps)[row].pinKey;
    ++g_volumeEntryGeneration;
    g_volumeEntry = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        VOLUME_ENTRY_CLASS, L"Name", WS_POPUP | WS_BORDER,
        origin.x, origin.y, rect.right - rect.left + Scale(4), rect.bottom - rect.top,
        owner, nullptr, instance, nullptr);
    if (!g_volumeEntry) {
        g_volumeEntryRow = DRAG_NONE;
        g_nameEntry = false;
        g_nameEntryKey.clear();
        return;
    }
    GetClientRect(g_volumeEntry, &client);
    g_volumeEdit = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
        Scale(3), 0, client.right - Scale(3), client.bottom,
        g_volumeEntry, nullptr, instance, nullptr);
    if (!g_volumeEdit) {
        FinishVolumeEntry(owner, false, false);
        return;
    }
    SendMessageW(g_volumeEdit, EM_SETLIMITTEXT, 100, 0);
    SendMessageW(g_volumeEdit, WM_SETFONT,
                 reinterpret_cast<WPARAM>(g_entryFont ? g_entryFont : g_bodyFont), TRUE);
    SetWindowTextW(g_volumeEdit, (*g_apps)[row].name.c_str());
    SendMessageW(g_volumeEdit, EM_SETSEL, 0, -1);
    ShowWindow(g_volumeEntry, SW_SHOW);
    SetFocus(g_volumeEdit);
}

void MoveKeyboardSelection(HWND window, int direction) {
    CancelNameTooltip(window);
    int row = g_selectedRow;
    if (row == DRAG_MASTER && direction > 0 && !g_apps->empty()) {
        row = g_scrollRow;
    } else {
        row = ClampInt(row + direction, DRAG_MASTER,
                       static_cast<int>(g_apps->size()) - 1);
    }
    SelectRow(row);
    ClampPageOffset();
    InvalidateRect(window, nullptr, FALSE);
}

bool HandleRowKey(HWND window, WPARAM key, LPARAM flags) {
    if (!g_settings.keyboardControls || g_volumeEntry || g_dragRow != DRAG_NONE ||
        (GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000)) {
        return false;
    }
    if (key == VK_UP || key == VK_DOWN) {
        MoveKeyboardSelection(window, key == VK_DOWN ? 1 : -1);
        return true;
    }
    if (key == VK_LEFT || key == VK_RIGHT) {
        // The selected row can be gone (an app closed since it was selected).
        if (g_selectedRow != DRAG_MASTER &&
            (g_selectedRow < 0 || g_selectedRow >= static_cast<int>(g_apps->size()))) {
            return false;
        }
        float volume = g_selectedRow == DRAG_MASTER ? GetRowMasterVolume()
            : (*g_apps)[g_selectedRow].volume;
        SetRowVolume(g_selectedRow, volume +
            (key == VK_RIGHT ? 1 : -1) * g_settings.volumeStep / 100.0f);
        InvalidateRect(window, nullptr, FALSE);
        return true;
    }
    if (key == 'M') {
        if (!(flags & (1LL << 30))) ToggleRowMute(g_selectedRow);
        InvalidateRect(window, nullptr, FALSE);
        return true;
    }
    return false;
}

void ApplyVolumeFromPoint(HWND hWnd, int row, int x) {
    RECT client;
    GetClientRect(hWnd, &client);
    RECT slider = SliderRectForRow(VisibleRowForDataRow(row), client.right);
    float inset = SliderThumbInset();
    float travel = static_cast<float>(slider.right - slider.left) - 2.0f * inset;
    if (travel <= 0.0f) return;
    float value = (static_cast<float>(x - slider.left) - inset) / travel;
    SetRowVolume(row, ClampVolume(value) * RowMaxLevel(row));
    InvalidateRect(hWnd, nullptr, FALSE);
}

void FillSolidRect(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void DrawRoundedRect(HDC dc, const RECT& rect, int radius, COLORREF color) {
    if (radius <= 0) {
        // Unlike RoundRect with a null pen, this fills the last row and column.
        FillSolidRect(dc, rect, color);
        return;
    }
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_NULL, 0, color);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

struct CachedLabel {
    std::wstring text;
    HFONT font = nullptr;
    UINT format = 0;
    COLORREF color = 0;
    int width = 0, height = 0;
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    ~CachedLabel() {
        if (dc && previous) SelectObject(dc, previous);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
    }
};
struct LabelCache {
    std::vector<std::unique_ptr<CachedLabel>> labels;
    size_t bytes = 0;
};
struct TextCaches {
    LabelCache general;
    LabelCache percentages;
};
std::optional<TextCaches> g_textCache;
constexpr int TEXT_SUPERSAMPLE = 6;

void ClearTextCache() {
    GdiFlush();
    g_textCache.reset();
}

// Average the supersampled coverage mask, then construct premultiplied BGRA.
// The same coverage is used against both mattes, without colored ClearType fringes.
DWORD TextCoveragePixel(const BYTE* scan, int stride, int x, int y, COLORREF color) {
    unsigned sum = 0;
    for (int dy = 0; dy < TEXT_SUPERSAMPLE; ++dy) {
        const BYTE* row = scan + static_cast<std::ptrdiff_t>(
            y * TEXT_SUPERSAMPLE + dy) * stride;
        for (int dx = 0; dx < TEXT_SUPERSAMPLE; ++dx) {
            sum += row[(x * TEXT_SUPERSAMPLE + dx) * 4];
        }
    }
    constexpr unsigned samples = TEXT_SUPERSAMPLE * TEXT_SUPERSAMPLE;
    DWORD alpha = (sum + samples / 2) / samples;
    return (alpha << 24) |
        (((GetRValue(color) * alpha + 127) / 255) << 16) |
        (((GetGValue(color) * alpha + 127) / 255) << 8) |
        ((GetBValue(color) * alpha + 127) / 255);
}

bool DrawSmoothLabel(HDC dc, const std::wstring& text, const RECT& rect,
                     HFONT font, Gdiplus::Font* typography, COLORREF color, UINT format,
                     bool percentage) {
    int width = rect.right - rect.left, height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0 || text.empty()) return true;
    if (width > 4096 || height > 1024 || static_cast<size_t>(width) * height > 262144)
        return false;
    auto blend = [&](const CachedLabel& label) {
        BLENDFUNCTION operation{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        return AlphaBlend(dc, rect.left, rect.top, width, height,
                          label.dc, 0, 0, width, height, operation) != FALSE;
    };
    if (!g_textCache) g_textCache.emplace();
    LabelCache& cache = percentage ? g_textCache->percentages : g_textCache->general;
    for (const auto& label : cache.labels) {
        if (label->font == font && label->format == format && label->color == color &&
            label->width == width && label->height == height && label->text == text)
            return blend(*label);
    }

    Gdiplus::Bitmap mask(width * TEXT_SUPERSAMPLE, height * TEXT_SUPERSAMPLE,
                         PixelFormat32bppARGB);
    if (mask.GetLastStatus() != Gdiplus::Ok) return false;
    {
        Gdiplus::Graphics graphics(&mask);
        graphics.SetPageUnit(Gdiplus::UnitPixel);
        graphics.Clear(Gdiplus::Color(255, 0, 0, 0));
        graphics.ScaleTransform(static_cast<float>(TEXT_SUPERSAMPLE),
                                static_cast<float>(TEXT_SUPERSAMPLE));
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        Gdiplus::StringFormat layout(Gdiplus::StringFormat::GenericTypographic());
        layout.SetFormatFlags(Gdiplus::StringFormatFlagsNoFitBlackBox |
            ((format & DT_SINGLELINE) ? Gdiplus::StringFormatFlagsNoWrap : 0));
        layout.SetAlignment((format & DT_CENTER) ? Gdiplus::StringAlignmentCenter :
            (format & DT_RIGHT) ? Gdiplus::StringAlignmentFar : Gdiplus::StringAlignmentNear);
        layout.SetLineAlignment((format & DT_VCENTER) ? Gdiplus::StringAlignmentCenter :
            Gdiplus::StringAlignmentNear);
        layout.SetTrimming((format & DT_END_ELLIPSIS) ? Gdiplus::StringTrimmingEllipsisCharacter :
            Gdiplus::StringTrimmingNone);
        Gdiplus::SolidBrush brush(Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::RectF bounds(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
        if (graphics.DrawString(text.c_str(), static_cast<int>(text.size()), typography,
                                bounds, &layout, &brush) != Gdiplus::Ok) return false;
        graphics.Flush(Gdiplus::FlushIntentionSync);
    }

    auto label = std::make_unique<CachedLabel>();
    label->text = text; label->font = font; label->format = format; label->color = color;
    label->width = width; label->height = height;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    label->bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    label->dc = CreateCompatibleDC(nullptr);
    if (!label->bitmap || !bits || !label->dc) return false;
    HGDIOBJ previous = SelectObject(label->dc, label->bitmap);
    if (!previous || previous == HGDI_ERROR) return false;
    label->previous = previous;

    Gdiplus::Rect area(0, 0, width * TEXT_SUPERSAMPLE,
                       height * TEXT_SUPERSAMPLE);
    Gdiplus::BitmapData data{};
    if (mask.LockBits(&area, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB,
                      &data) != Gdiplus::Ok) return false;
    auto* pixels = static_cast<DWORD*>(bits);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            pixels[static_cast<size_t>(y) * width + x] = TextCoveragePixel(
                static_cast<const BYTE*>(data.Scan0), data.Stride, x, y, color);
        }
    }
    mask.UnlockBits(&data);

    size_t bytes = static_cast<size_t>(width) * height * sizeof(DWORD);
    GdiFlush();
    while (!cache.labels.empty() &&
           (cache.labels.size() >= 128 || cache.bytes + bytes > 8 * 1024 * 1024)) {
        const auto& oldest = cache.labels.front();
        cache.bytes -= static_cast<size_t>(oldest->width) * oldest->height * sizeof(DWORD);
        cache.labels.erase(cache.labels.begin());
    }
    cache.bytes += bytes;
    cache.labels.push_back(std::move(label));
    return blend(*cache.labels.back());
}

void DrawLabel(HDC dc, const std::wstring& text, RECT rect, HFONT font,
               COLORREF color, UINT format, bool percentage = false) {
    Gdiplus::Font* typography = font == g_titleFont ? g_titleTypography :
        font == g_bodyFont ? g_bodyTypography :
        font == g_smallFont ? g_smallTypography : nullptr;
    if (typography && typography->GetLastStatus() == Gdiplus::Ok &&
        DrawSmoothLabel(dc, text, rect, font, typography, color, format, percentage)) return;
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text.c_str(), -1, &rect, format | DT_NOPREFIX);
    SelectObject(dc, oldFont);
}

void CancelNameTooltip(HWND window) {
    KillTimer(window, TIMER_NAME_TOOLTIP);
    if (g_nameTooltip && IsWindowVisible(g_nameTooltip)) {
        ShowWindow(g_nameTooltip, SW_HIDE);
    }
    g_nameTooltipText.clear();
    g_nameTooltipClipped = false;
    g_nameTooltipSince = 0;
}

bool NameIsClipped(HWND window, const std::wstring& text, const RECT& rect) {
    int width = rect.right - rect.left;
    if (text.empty() || width <= 0) return false;

    if (g_bodyTypography && g_bodyTypography->GetLastStatus() == Gdiplus::Ok) {
        Gdiplus::Bitmap bitmap(1, 1, PixelFormat32bppARGB);
        Gdiplus::Graphics graphics(&bitmap);
        graphics.SetPageUnit(Gdiplus::UnitPixel);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

        Gdiplus::StringFormat layout(Gdiplus::StringFormat::GenericTypographic());
        layout.SetFormatFlags(Gdiplus::StringFormatFlagsNoFitBlackBox |
                              Gdiplus::StringFormatFlagsNoWrap);
        layout.SetTrimming(Gdiplus::StringTrimmingNone);

        Gdiplus::RectF measured{};
        if (graphics.MeasureString(
                text.c_str(), static_cast<int>(text.size()), g_bodyTypography,
                Gdiplus::PointF(0.0f, 0.0f), &layout, &measured) == Gdiplus::Ok) {
            return measured.Width > static_cast<float>(width);
        }
    }

    HDC dc = GetDC(window);
    if (!dc) return false;
    HGDIOBJ oldFont = SelectObject(dc, g_bodyFont);
    RECT measured{};
    DrawTextW(dc, text.c_str(), -1, &measured,
              DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, oldFont);
    ReleaseDC(window, dc);
    return measured.right - measured.left > width;
}

int BadgeAt(POINT point);
RECT BadgeRect(int row);

void UpdateNameTooltip(HWND window, POINT point) {
    // The output list has the tooltip while it is open.
    if (g_outputMenuOpen) return;
    if (!IsWindowVisible(window) || g_volumeEntry ||
        g_dragRow != DRAG_NONE) {
        CancelNameTooltip(window);
        return;
    }

    RECT client{};
    GetClientRect(window, &client);
    RECT rect = DeviceNameRect(client.right);
    std::wstring text;
    bool explain = false;  // Shown even when nothing is cut short.

    if (int badgeRow = BadgeAt(point); badgeRow >= 0 &&
                                       badgeRow < static_cast<int>(g_apps->size())) {
        rect = BadgeRect(badgeRow);
        text = L"Always plays on " + (*g_apps)[badgeRow].routedOutput +
               L". Click to follow the default instead (" +
               (g_endpointName.empty() ? std::wstring(L"none")
                                       : ShortOutputName(g_endpointName)) + L").";
        explain = true;
    } else if (PtInRect(&rect, point)) {
        text = g_endpointName.empty() ? L"No output device" : g_endpointName;
    } else if (PtInRect(&client, point)) {
        int row = RowFromPoint(point);
        if (row != DRAG_NONE) {
            rect = NameRectForRow(VisibleRowForDataRow(row), client.right);
            if (PtInRect(&rect, point)) {
                text = row == DRAG_MASTER
                    ? (g_flow == eCapture ? (g_inputVolume ? L"Microphone volume" : L"No microphone")
                                          : g_endpointVolume ? L"Master volume" : L"Output unavailable")
                    : (*g_apps)[row].name;
            }
        }
    }

    if (text == g_nameTooltipText && EqualRect(&rect, &g_nameTooltipRect)) return;

    CancelNameTooltip(window);
    g_nameTooltipText = std::move(text);
    g_nameTooltipRect = rect;
    g_nameTooltipClipped = explain || NameIsClipped(window, g_nameTooltipText, rect);

    if (g_nameTooltipClipped) {
        g_nameTooltipSince = GetTickCount64();
        SetTimer(window, TIMER_NAME_TOOLTIP, NAME_TOOLTIP_DELAY, nullptr);
    }
}

void PaintNameTooltip(HWND window, HDC dc) {
    RECT client{};
    GetClientRect(window, &client);

    HBRUSH background = CreateSolidBrush(g_theme.panel);
    FillRect(dc, &client, background);
    DeleteObject(background);

    RECT textRect = client;
    InflateRect(&textRect, -Scale(8), -Scale(3));

    DrawLabel(dc, g_nameTooltipText, textRect, g_bodyFont, g_theme.text,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

SIZE MeasureNameTooltipText() {
    SIZE result{0, 0};
    if (g_nameTooltipText.empty()) return result;

    if (g_bodyTypography && g_bodyTypography->GetLastStatus() == Gdiplus::Ok) {
        Gdiplus::Bitmap bitmap(1, 1, PixelFormat32bppARGB);
        Gdiplus::Graphics graphics(&bitmap);
        graphics.SetPageUnit(Gdiplus::UnitPixel);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

        Gdiplus::StringFormat layout(Gdiplus::StringFormat::GenericTypographic());
        layout.SetFormatFlags(Gdiplus::StringFormatFlagsNoFitBlackBox |
                              Gdiplus::StringFormatFlagsNoWrap);
        layout.SetTrimming(Gdiplus::StringTrimmingNone);

        Gdiplus::RectF measured{};
        if (graphics.MeasureString(
                g_nameTooltipText.c_str(),
                static_cast<int>(g_nameTooltipText.size()),
                g_bodyTypography,
                Gdiplus::PointF(0.0f, 0.0f),
                &layout,
                &measured) == Gdiplus::Ok) {
            result.cx = static_cast<LONG>(std::ceil(measured.Width));
            result.cy = static_cast<LONG>(std::ceil(measured.Height)) + Scale(2);
            return result;
        }
    }

    HWND owner = g_hWnd.load();
    HDC dc = owner ? GetDC(owner) : nullptr;
    if (!dc) return result;

    HGDIOBJ oldFont = SelectObject(dc, g_bodyFont);
    RECT measured{};
    DrawTextW(dc, g_nameTooltipText.c_str(), -1, &measured,
              DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, oldFont);
    ReleaseDC(owner, dc);

    result.cx = measured.right - measured.left;
    result.cy = measured.bottom - measured.top;
    return result;
}

void ApplyNameTooltipWindowStyle() {
    if (!g_nameTooltip) return;

    LONG_PTR exStyle = GetWindowLongPtrW(g_nameTooltip, GWL_EXSTYLE);

    if (g_settings.backgroundOpacity < 100) {
        if (!(exStyle & WS_EX_LAYERED)) {
            exStyle |= WS_EX_LAYERED;
            SetWindowLongPtrW(g_nameTooltip, GWL_EXSTYLE, exStyle);
        }

        int tooltipOpacity = std::max(85, g_settings.backgroundOpacity);
        BYTE alpha = static_cast<BYTE>(MulDiv(255, tooltipOpacity, 100));
        SetLayeredWindowAttributes(g_nameTooltip, 0, alpha, LWA_ALPHA);
    } else if (exStyle & WS_EX_LAYERED) {
        exStyle &= ~WS_EX_LAYERED;
        SetWindowLongPtrW(g_nameTooltip, GWL_EXSTYLE, exStyle);
    }

    RECT bounds{};
    if (GetWindowRect(g_nameTooltip, &bounds)) {
        int width = bounds.right - bounds.left;
        int height = bounds.bottom - bounds.top;
        int radius = Radius(8);

        HRGN region = CreateRoundRectRgn(
            0, 0, width + 1, height + 1, radius, radius);

        if (region && !SetWindowRgn(g_nameTooltip, region, TRUE)) {
            DeleteObject(region);
        }
    }
}

void PositionNameTooltip(POINT cursor) {
    if (!g_nameTooltip || g_nameTooltipText.empty()) return;

    SIZE measured = MeasureNameTooltipText();

    int width = std::max(
        Scale(28), static_cast<int>(measured.cx) + Scale(16));
    int height = std::max(
        Scale(26), static_cast<int>(measured.cy) + Scale(6));

    int x = cursor.x + Scale(12);
    int y = cursor.y + Scale(20);

    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{sizeof(monitorInfo)};

    if (monitor && GetMonitorInfoW(monitor, &monitorInfo)) {
        const RECT& work = monitorInfo.rcWork;
        int workWidth = work.right - work.left;

        width = std::min(width, std::max(Scale(40), workWidth - Scale(16)));

        if (x + width > work.right - Scale(8)) {
            x = work.right - Scale(8) - width;
        }
        if (x < work.left + Scale(8)) {
            x = work.left + Scale(8);
        }

        if (y + height > work.bottom - Scale(8)) {
            y = cursor.y - height - Scale(12);
        }
        if (y < work.top + Scale(8)) {
            y = work.top + Scale(8);
        }
    }

    SetWindowPos(
        g_nameTooltip, HWND_TOPMOST, x, y, width, height,
        SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);

    ApplyNameTooltipWindowStyle();
    InvalidateRect(g_nameTooltip, nullptr, FALSE);
    UpdateWindow(g_nameTooltip);
}

LRESULT CALLBACK NameTooltipProc(HWND window, UINT message,
                                 WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_NCHITTEST:
            return HTTRANSPARENT;

        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            PaintNameTooltip(window, dc);
            EndPaint(window, &paint);
            return 0;
        }

        case WM_PRINTCLIENT:
            if (reinterpret_cast<HDC>(wParam)) {
                PaintNameTooltip(window, reinterpret_cast<HDC>(wParam));
            }
            return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

bool EnsureNameTooltip(HWND owner) {
    if (!g_nameTooltip) {
        g_nameTooltip = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            NAME_TOOLTIP_CLASS, L"", WS_POPUP,
            0, 0, 1, 1, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    }
    return g_nameTooltip != nullptr;
}

void ShowNameTooltip(HWND window) {
    KillTimer(window, TIMER_NAME_TOOLTIP);

    POINT point{};
    if (!GetCursorPos(&point) || WindowFromPoint(point) != window) {
        CancelNameTooltip(window);
        return;
    }

    POINT clientPoint = point;
    ScreenToClient(window, &clientPoint);
    UpdateNameTooltip(window, clientPoint);

    if (!g_nameTooltipClipped) return;

    ULONGLONG elapsed = GetTickCount64() - g_nameTooltipSince;
    if (elapsed < NAME_TOOLTIP_DELAY) {
        SetTimer(window, TIMER_NAME_TOOLTIP,
                 NAME_TOOLTIP_DELAY - static_cast<UINT>(elapsed), nullptr);
        return;
    }

    if (EnsureNameTooltip(window)) PositionNameTooltip(point);
}

COLORREF MixColor(COLORREF from, COLORREF to, float amount) {
    amount = ClampVolume(amount);
    return RGB(
        static_cast<int>(GetRValue(from) + (GetRValue(to) - GetRValue(from)) * amount),
        static_cast<int>(GetGValue(from) + (GetGValue(to) - GetGValue(from)) * amount),
        static_cast<int>(GetBValue(from) + (GetBValue(to) - GetBValue(from)) * amount));
}

COLORREF IconColor() {
    return g_lightTheme
        ? MixColor(g_theme.text, g_theme.secondaryText, 0.35f)
        : g_theme.secondaryText;
}

// Share of the text color laid over a hovered surface. A highlight that reads
// well on an opaque background all but disappears on a translucent one, so it
// grows stronger as the background opacity drops.
float HoverStrength() {
    float opacity = g_settings.backgroundOpacity < 100 && !g_transparencyFailed
        ? g_settings.backgroundOpacity / 100.0f : 1.0f;
    float opaque = g_lightTheme ? 0.055f : 0.05f;
    float clear = g_lightTheme ? 0.11f : 0.085f;
    return clear + (opaque - clear) * opacity;
}

// Resolves a surface color for the current paint pass: the base color at the
// background opacity, with `highlight` (0 for none, 1 for a full hover) laid
// over it at a strength that does not fade with the background.
COLORREF SurfaceColor(COLORREF base, float highlight) {
    if (g_paintMatte >= 0) {
        base = MixColor(g_paintMatte ? RGB(255, 255, 255) : RGB(0, 0, 0),
                        base, g_settings.backgroundOpacity / 100.0f);
    }
    return highlight > 0.0f
        ? MixColor(base, g_theme.text, HoverStrength() * highlight) : base;
}

void DrawSurface(HDC dc, const RECT& rect, int radius, COLORREF color,
                 float highlight = 0.0f) {
    DrawRoundedRect(dc, rect, radius, SurfaceColor(color, highlight));
}

float EaseTowards(float current, float target, float speed) {
    if (!g_motionEnabled) {
        return target;
    }
    float next = current + (target - current) *
        (1.0f - std::exp(-speed * g_frameSeconds));
    return std::abs(target - next) < 0.001f ? target : next;
}

void DrawSlider(HDC dc, const RECT& rect, float volume, float peak,
                bool muted, float thumbDot, ThumbState thumbState) {
    std::optional<Gdiplus::Graphics> graphics;
    if (g_gdiplusToken) {
        graphics.emplace(dc);
        if (graphics->GetLastStatus() != Gdiplus::Ok ||
            graphics->SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias) != Gdiplus::Ok ||
            graphics->SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf) != Gdiplus::Ok) {
            graphics.reset();
        }
    }

    auto capsule = [&](float x, float y, float width, float height, COLORREF color,
                       BYTE alpha = 255) {
        if (width <= 0.0f || height <= 0.0f) return;
        float diameter = std::min(width, height);
        if (!graphics && alpha < 255) {
            // GDI cannot blend, so approximate against the mixer background.
            color = MixColor(g_theme.background, color, alpha / 255.0f);
        }
        if (graphics) {
            Gdiplus::GraphicsPath path;
            path.AddArc(x, y, diameter, diameter, 180.0f, 90.0f);
            path.AddArc(x + width - diameter, y, diameter, diameter, 270.0f, 90.0f);
            path.AddArc(x + width - diameter, y + height - diameter,
                        diameter, diameter, 0.0f, 90.0f);
            path.AddArc(x, y + height - diameter, diameter, diameter, 90.0f, 90.0f);
            path.CloseFigure();
            Gdiplus::SolidBrush brush(Gdiplus::Color(
                alpha, GetRValue(color), GetGValue(color), GetBValue(color)));
            graphics->FillPath(&brush, &path);
        } else {
            RECT bounds = {static_cast<LONG>(std::lround(x)),
                           static_cast<LONG>(std::lround(y)),
                           static_cast<LONG>(std::lround(x + width)),
                           static_cast<LONG>(std::lround(y + height))};
            DrawRoundedRect(dc, bounds, std::max(1, static_cast<int>(std::lround(diameter))),
                            color);
        }
    };

    float density = g_dpi / 96.0f;
    float centerY = (rect.top + rect.bottom) * 0.5f;
    float left = static_cast<float>(rect.left);
    float width = static_cast<float>(rect.right - rect.left);
    if (width <= 0.0f) return;
    bool windows10 = IsWindows10Style();
    float thumbDiameter = SliderThumbWidth();
    float thumbHeight = SliderThumbHeight();
    float thumbRadius = thumbDiameter * 0.5f;
    float trackHeight = (windows10 ? 2.0f : 4.0f) * density;
    float trackTop = centerY - trackHeight * 0.5f;
    float thumbX = SliderThumbCenter(rect, volume);

    // The native rail is a translucent overlay rather than a fixed gray, so
    // it adapts to whatever is behind it. A muted row dims both parts of the
    // rail, like a disabled native slider, with the filled part still distinct.
    // The Windows 10 rail is fainter and brightens under the pointer.
    COLORREF overlay = g_lightTheme ? RGB(0, 0, 0) : RGB(255, 255, 255);
    BYTE railAlpha = muted ? (g_lightTheme ? 0x51 : 0x3F)
                           : (g_lightTheme ? 0x72 : 0x8B);
    if (windows10) {
        railAlpha = muted ? 0x33 : thumbState == ThumbState::Hover ? 0x99 : 0x66;
    }
    COLORREF activeTrack = muted ? g_theme.muted : g_theme.sliderFill;
    capsule(left, trackTop, width, trackHeight, overlay, railAlpha);
    capsule(left, trackTop, thumbX - left, trackHeight, activeTrack);

    if (!muted && peak > 0.01f) {
        // Compact rows have no room below the thumb, so their meter runs
        // directly under the rail and passes behind the thumb.
        float meterTop = IsCompact() ? trackTop + trackHeight + density
                                     : centerY + thumbHeight * 0.5f + density;
        capsule(left, meterTop, width * ClampVolume(peak),
                (IsCompact() ? 2.0f : 3.0f) * density,
                MixColor(g_theme.sliderFill, g_theme.text, 0.15f));
    }

    if (windows10) {
        // The Windows 10 thumb is a flat bar in the accent color. It turns
        // near-white (near-black in the light theme) under the pointer, and
        // gray while it is held.
        COLORREF thumb = activeTrack;
        if (!muted && thumbState == ThumbState::Hover) {
            thumb = g_lightTheme ? RGB(23, 23, 23) : RGB(242, 242, 242);
        } else if (!muted && thumbState == ThumbState::Pressed) {
            thumb = g_lightTheme ? RGB(204, 204, 204) : RGB(118, 118, 118);
        }
        capsule(thumbX - thumbRadius, centerY - thumbHeight * 0.5f,
                thumbDiameter, thumbHeight, thumb);
        return;
    }

    // The outer thumb is a solid control surface, so it stays opaque at any
    // background opacity. Only the accent dot reacts to the pointer. Its 1px
    // border is a faint overlay that is slightly stronger along one edge.
    float surfaceDiameter = thumbDiameter - 2.0f * density;
    if (graphics) {
        float top = centerY - thumbRadius;
        BYTE red = GetRValue(overlay), green = GetGValue(overlay),
             blue = GetBValue(overlay);
        Gdiplus::LinearGradientBrush border(
            Gdiplus::PointF(0.0f, top - 1.0f),
            Gdiplus::PointF(0.0f, top + thumbDiameter + 1.0f),
            Gdiplus::Color(g_lightTheme ? 0x0F : 0x18, red, green, blue),
            Gdiplus::Color(g_lightTheme ? 0x29 : 0x12, red, green, blue));
        Gdiplus::GraphicsPath ring; // Alternate fill leaves the middle empty.
        ring.AddEllipse(thumbX - thumbRadius, top, thumbDiameter, thumbDiameter);
        ring.AddEllipse(thumbX - surfaceDiameter * 0.5f,
                        centerY - surfaceDiameter * 0.5f,
                        surfaceDiameter, surfaceDiameter);
        graphics->FillPath(&border, &ring);
    }
    capsule(thumbX - surfaceDiameter * 0.5f, centerY - surfaceDiameter * 0.5f,
            surfaceDiameter, surfaceDiameter, g_theme.thumb);
    float dotDiameter = thumbDiameter * 0.6f * thumbDot;
    capsule(thumbX - dotDiameter * 0.5f, centerY - dotDiameter * 0.5f,
            dotDiameter, dotDiameter, activeTrack);
}

// Same level thresholds as the speaker icon in the Windows volume flyout.
PCWSTR VolumeGlyph(float volume, bool muted) {
    if (muted) return L"\uE74F";
    int percent = static_cast<int>(volume * 100.0f + 0.5f);
    return percent == 0 ? L"\uE992" : percent < 33 ? L"\uE993" :
           percent < 66 ? L"\uE994" : L"\uE995";
}

// rowColor and rowHighlight describe the row surface behind the button.
void DrawMuteButton(HDC dc, int visibleRow, int clientWidth, bool muted,
                    float volume, COLORREF rowColor, float rowHighlight) {
    RECT button = MuteRectForRow(visibleRow, clientWidth);
    if (muted) {
        DrawSurface(dc, button, Radius(8), rowColor, rowHighlight + 1.0f);
    }
    DrawLabel(dc, VolumeGlyph(volume, muted), button, g_symbolFont,
              muted ? g_theme.accent : IconColor(),
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// The width of text as DrawLabel draws it, with the smooth typography when
// it is available.
int MeasureLabel(HDC dc, const std::wstring& text, HFONT font) {
    if (text.empty()) return 0;
    Gdiplus::Font* typography = font == g_bodyFont ? g_bodyTypography :
        font == g_smallFont ? g_smallTypography : nullptr;
    if (typography && typography->GetLastStatus() == Gdiplus::Ok) {
        Gdiplus::Bitmap bitmap(1, 1, PixelFormat32bppARGB);
        Gdiplus::Graphics graphics(&bitmap);
        graphics.SetPageUnit(Gdiplus::UnitPixel);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        Gdiplus::StringFormat layout(Gdiplus::StringFormat::GenericTypographic());
        layout.SetFormatFlags(Gdiplus::StringFormatFlagsNoFitBlackBox |
                              Gdiplus::StringFormatFlagsNoWrap |
                              Gdiplus::StringFormatFlagsMeasureTrailingSpaces);
        Gdiplus::RectF measured{};
        if (graphics.MeasureString(text.c_str(), static_cast<int>(text.size()), typography,
                                   Gdiplus::PointF(0.0f, 0.0f), &layout,
                                   &measured) == Gdiplus::Ok) {
            return static_cast<int>(std::ceil(measured.Width));
        }
    }
    HGDIOBJ oldFont = SelectObject(dc, font);
    RECT measured{};
    DrawTextW(dc, text.c_str(), -1, &measured, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, oldFont);
    return measured.right - measured.left;
}

// Where the "fixed to an output" badge of each app row was last drawn.
std::unordered_map<int, RECT> g_badgeRects;
bool g_badgeHovered;

std::unordered_map<int, RECT> g_eqBadgeRects;
std::unordered_map<int, RECT> g_lmBadgeRects;  // LM and LE open the effects.
std::unordered_map<int, RECT> g_leBadgeRects;
std::unordered_map<int, RECT> g_chBadgeRects;  // "CH": the surround channels.
std::unordered_map<int, RECT> g_svBadgeRects;  // "Start 50%": opens the volume at startup.

int EqBadgeAt(POINT point) {
    for (const auto& [row, rect] : g_eqBadgeRects) {
        if (PtInRect(&rect, point)) return row;
    }
    return DRAG_NONE;
}

int EffectsBadgeAt(POINT point) {
    for (const auto* rects : {&g_lmBadgeRects, &g_leBadgeRects, &g_chBadgeRects}) {
        for (const auto& [row, rect] : *rects) {
            if (PtInRect(&rect, point)) return row;
        }
    }
    return DRAG_NONE;
}

int SvBadgeAt(POINT point) {
    for (const auto& [row, rect] : g_svBadgeRects) {
        if (PtInRect(&rect, point)) return row;
    }
    return DRAG_NONE;
}

RECT BadgeRect(int row) {
    auto found = g_badgeRects.find(row);
    return found != g_badgeRects.end() ? found->second : RECT{};
}

int BadgeAt(POINT point) {
    for (const auto& [row, rect] : g_badgeRects) {
        if (PtInRect(&rect, point)) return row;
    }
    return DRAG_NONE;
}

void DrawAudioRow(HDC dc, int visibleRow, int dataRow, int clientWidth) {
    int top = RowTop(visibleRow);
    bool master = dataRow == DRAG_MASTER;
    std::wstring name;
    float volume;
    float peak = 0.0f;
    bool muted;
    HICON icon = nullptr;

    if (master) {
        name = g_flow == eCapture ? (g_inputVolume ? L"Microphone volume" : L"No microphone")
                                  : g_endpointVolume ? L"Master volume" : L"Output unavailable";
        if (g_paintMatte != 1) {
            g_masterPaintVolume = GetRowMasterVolume();
            g_masterPaintMuted = GetRowMasterMuted();
        }
        volume = g_masterPaintVolume;
        muted = g_masterPaintMuted;
        icon = g_flow == eCapture ? nullptr : g_masterIcon;
    } else {
        const AppSession& app = (*g_apps)[dataRow];
        name = app.name;
        if (app.defaultOutput) name += L" (default)";
        volume = app.volume;
        muted = app.muted;
        if (g_paintMatte != 1) peak = GetAppPeak(app);
        icon = app.icon;
    }

    // The slider shows the share of the row's range; the percentage, the level.
    float level = volume;
    float maxLevel = master ? 1.0f : RowMaxLevel(dataRow);
    volume = level / maxLevel;

    std::wstring visualKey = master ? L"master" :
        L"app:" + std::to_wstring((*g_apps)[dataRow].processId) + L":" + name;
    RowVisual& visual = g_rowVisuals[visualKey];
    if (!visual.initialized) {
        visual.volume = volume;
        visual.initialized = true;
    }
    if (g_paintMatte != 1) {
        float targetHover = g_hoverRow == dataRow ? 1.0f : 0.0f;
        // The accent dot grows while the pointer is over the thumb and shrinks
        // while the thumb is held. Test against the thumb as it is drawn, so
        // the state stays right when the volume changes under a still pointer.
        visual.thumbState = ThumbState::Rest;
        if (g_dragRow == dataRow) {
            visual.thumbState = ThumbState::Pressed;
        } else if (g_dragRow == DRAG_NONE && g_hoverRow == dataRow &&
                   SliderThumbContains(SliderRectForRow(visibleRow, clientWidth),
                                       visual.volume, g_pointer)) {
            visual.thumbState = ThumbState::Hover;
        }
        float targetThumbDot =
            visual.thumbState == ThumbState::Pressed ? THUMB_DOT_PRESSED :
            visual.thumbState == ThumbState::Hover ? THUMB_DOT_HOVER : THUMB_DOT_REST;
        g_frameAnimating |= std::abs(visual.hover - targetHover) >= 0.001f ||
                            std::abs(visual.volume - volume) >= 0.001f ||
                            std::abs(visual.thumbDot - targetThumbDot) >= 0.001f;
        visual.thumbDot = EaseTowards(visual.thumbDot, targetThumbDot, 18.0f);
        g_frameHasAudio |= peak > 0.005f || visual.peak > 0.005f;
        visual.hover = EaseTowards(visual.hover, g_hoverRow == dataRow ? 1.0f : 0.0f, 18.0f);
        visual.volume = g_dragRow == dataRow ? volume : EaseTowards(visual.volume, volume, 22.0f);
        visual.peak = EaseTowards(visual.peak, peak, peak > visual.peak ? 35.0f : 8.0f);
    }
    RECT rowRect = {Scale(12), top, clientWidth - Scale(12), top + RowHeight() - Scale(2)};
    COLORREF base = master ? g_theme.panel : g_theme.background;
    DrawSurface(dc, rowRect, Radius(16), base, visual.hover);

    if (g_settings.keyboardControls && g_selectedRow == dataRow) {
        RECT marker{Scale(14), top + Scale(12), Scale(17),
                    top + RowHeight() - Scale(14)};
        DrawRoundedRect(dc, marker, Radius(3), g_theme.accent);
    }

    RECT iconTile = IconTileRectForRow(visibleRow);
    DrawSurface(dc, iconTile, Radius(10),
                    master ? g_theme.background : g_theme.panel);
    if (icon) {
        int iconSize = Scale(24);
        DrawIconEx(dc,
                   iconTile.left + (iconTile.right - iconTile.left - iconSize) / 2,
                   iconTile.top + (iconTile.bottom - iconTile.top - iconSize) / 2,
                   icon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
    } else {
        PCWSTR glyph = !master && (*g_apps)[dataRow].deviceGlyph
            ? (*g_apps)[dataRow].deviceGlyph
            : master && g_flow == eCapture ? L"\uE720" : L"\uE8D6";
        DrawLabel(dc, glyph, iconTile, g_symbolFont, IconColor(),
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    RECT nameRect = NameRectForRow(visibleRow, clientWidth);
    // Badges right after the name: "EQ" while the equalizer is on, and
    // "-> Headphones" for an app fixed to an output. A click on one opens the
    // equalizer or lets the app follow the default again. The name gives way
    // first; the badges only shorten once the name is down to half.
    struct Badge {
        std::wstring text;
        bool lit;
        std::unordered_map<int, RECT>* rects;
    };
    std::vector<Badge> badges;
    if (master ? g_masterEqOn : (*g_apps)[dataRow].eqOn) {
        badges.push_back({L"EQ", true, &g_eqBadgeRects});
    }
    if (master ? g_masterLoudMaxOn : (*g_apps)[dataRow].loudMaxOn) {
        badges.push_back({L"LM", true, &g_lmBadgeRects});
    }
    if (!master && (*g_apps)[dataRow].loudnessOn) {
        badges.push_back({L"LE", true, &g_leBadgeRects});
    }
    if (!master && (*g_apps)[dataRow].channelsOn) {
        badges.push_back({L"CH", true, &g_chBadgeRects});
    }
    if (int start = master ? g_masterStartVolume : (*g_apps)[dataRow].startVolume; start >= 0) {
        badges.push_back({L"S" + std::to_wstring(start), true, &g_svBadgeRects});  // "S50": 50 % at startup.
    }
    if (!master && !(*g_apps)[dataRow].routedOutput.empty()) {
        badges.push_back({L"\u2192 " + (*g_apps)[dataRow].routedOutput, false, &g_badgeRects});
    }
    if (!badges.empty()) {
        int gap = Scale(8), spacing = Scale(3), padding = Scale(4);
        int available = nameRect.right - nameRect.left;
        std::vector<int> widths;
        int total = 0;
        for (const auto& badge : badges) {
            widths.push_back(MeasureLabel(dc, badge.text, g_smallFont) + 2 * padding + Scale(2));
            total += widths.back() + (widths.size() > 1 ? spacing : 0);
        }
        int nameWidth = MeasureLabel(dc, name, g_bodyFont) + Scale(2);
        // The name gives way down to a third of the room, so every badge can be seen.
        nameWidth = std::min(nameWidth, std::max(available / 3, available - gap - total));
        int x = nameRect.left + nameWidth + gap;
        int middle = (nameRect.top + nameRect.bottom) / 2;
        for (size_t i = 0; i < badges.size(); ++i) {
            int width = std::min(widths[i], static_cast<int>(nameRect.right) - x);
            // A short badge ("LE") fits whole; only one that would be cut down too far is left out.
            if (width < std::min(widths[i], Scale(24))) break;
            RECT badgeRect{x, middle - Scale(10), x + width, middle + Scale(10)};
            bool hovered = PtInRect(&badgeRect, g_pointer) != FALSE && g_dragRow == DRAG_NONE;
            DrawSurface(dc, badgeRect, Radius(10), base, hovered ? 1.0f : 0.6f);
            RECT text = badgeRect;
            InflateRect(&text, -padding, 0);
            DrawLabel(dc, badges[i].text, text, g_smallFont,
                      hovered || badges[i].lit ? g_theme.accent : g_theme.secondaryText,
                      (badges[i].lit ? DT_CENTER : DT_LEFT) | DT_VCENTER | DT_SINGLELINE |
                          DT_END_ELLIPSIS);
            (*badges[i].rects)[dataRow] = badgeRect;
            x += width + spacing;
        }
        nameRect.right = nameRect.left + nameWidth;
    }
    DrawLabel(dc, name, nameRect, g_bodyFont, g_theme.text,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    WCHAR percent[16];
    swprintf_s(percent, L"%d%%",
               static_cast<int>(level * 100.0f + 0.5f));
    RECT percentRect = PercentRectForRow(visibleRow, clientWidth);
    // The percentages have a cache of their own, so even a slider being
    // dragged soon finds its label already drawn. Above 100% they are lit.
    DrawLabel(dc, percent, percentRect, g_bodyFont,
              muted ? g_theme.secondaryText : level > 1.005f ? g_theme.accent : g_theme.text,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE, /*percentage=*/true);

    // In compact mode the thumb can reach the mute button's highlight, so the
    // slider is drawn on top of it.
    DrawMuteButton(dc, visibleRow, clientWidth, muted, std::min(level, 1.0f), base,
                   visual.hover);
    RECT sliderRect = SliderRectForRow(visibleRow, clientWidth);
    if (maxLevel > 1.0f) {
        // A notch where 100% is, the most Windows itself allows.
        float x = SliderThumbCenter(sliderRect, 1.0f / maxLevel);
        int middle = (sliderRect.top + sliderRect.bottom) / 2;
        RECT notch{static_cast<LONG>(std::lround(x)) - Scale(1), middle - Scale(6),
                   static_cast<LONG>(std::lround(x)) + Scale(1), middle + Scale(6)};
        DrawRoundedRect(dc, notch, Radius(1), g_theme.secondaryText);
    }
    DrawSlider(dc, sliderRect, visual.volume,
               visual.peak, muted, visual.thumbDot, visual.thumbState);
    if (!master) {
        RECT pin = PinRectForRow(visibleRow, clientWidth);
        bool pinned = (*g_apps)[dataRow].pinned;
        DrawLabel(dc, pinned ? L"\uE840" : L"\uE718", pin, g_symbolFont,
                  pinned ? g_theme.accent : IconColor(),
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

void RenderMixerContents(HDC dc, const RECT& client) {
    g_badgeRects.clear();
    g_eqBadgeRects.clear();
    g_lmBadgeRects.clear();
    g_leBadgeRects.clear();
    g_svBadgeRects.clear();
    g_chBadgeRects.clear();
    if (g_paintMatte >= 0) {
        FillSolidRect(dc, client, g_paintMatte ? RGB(255, 255, 255) : RGB(0, 0, 0));
        DrawSurface(dc, client, Radius(20), g_theme.background);
    } else {
        FillSolidRect(dc, client, g_theme.background);
    }

    if (!IsCompact()) {
        DrawLabel(dc, g_flow == eCapture ? L"Microphone" : L"Volume mixer", TitleRect(client.right), g_titleFont,
                  g_theme.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    RECT pickerRect = OutputPickerRect(client.right);
    DrawSurface(dc, pickerRect, Radius(16),
                MixColor(g_theme.panel, RGB(0, 0, 0), 0.15f),
                g_outputHovered || (g_outputMenuOpen && !g_outputListAnchored) ? 1.0f : 0.0f);
    RECT deviceIconRect = {pickerRect.left + Scale(8), pickerRect.top,
                           pickerRect.left + Scale(34), pickerRect.bottom};
    DrawLabel(dc, g_flow == eCapture ? L"\uE720" : L"\uE7F5", deviceIconRect, g_symbolFont, IconColor(),
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT deviceNameRect = DeviceNameRect(client.right);
    DrawLabel(dc, g_endpointName.empty() ? (g_flow == eCapture ? L"No input device" : L"No output device")
                                          : g_endpointName,
              deviceNameRect, g_bodyFont, g_theme.text,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT arrowRect = {pickerRect.right - Scale(30), pickerRect.top,
                      pickerRect.right - Scale(8), pickerRect.bottom};
    DrawLabel(dc, g_outputMenuOpen && !g_outputListAnchored ? L"\uE70E" : L"\uE70D", arrowRect, g_symbolFont, IconColor(),
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT closeRect = CloseButtonRect(client.right);
    if (g_paintMatte != 1) {
        g_frameAnimating |= std::abs(g_closeHoverAmount -
                                     (g_closeHovered ? 1.0f : 0.0f)) >= 0.001f;
        g_closeHoverAmount = EaseTowards(g_closeHoverAmount, g_closeHovered ? 1.0f : 0.0f, 18.0f);
    }
    DrawSurface(dc, closeRect, Radius(8), g_theme.background, g_closeHoverAmount);
    RECT outputsRect = OutputsButtonRect(client.right);
    if (g_outputsButtonHovered || g_showOutputs) {
        DrawSurface(dc, outputsRect, Radius(8), g_theme.background,
                    g_outputsButtonHovered ? 1.0f : 0.6f);
    }
    // The other tab: the microphone while playback shows, the speaker while the microphone does.
    {
        RECT flowRect = FlowButtonRect(client.right);
        if (g_flowButtonHovered) DrawSurface(dc, flowRect, Radius(8), g_theme.background, 1.0f);
        DrawLabel(dc, g_flow == eCapture ? L"\uE767" : L"\uE720", flowRect, g_symbolFont,
                  IconColor(),
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    // A menu icon: lit while every device is listed with its own volume.
    DrawLabel(dc, L"", outputsRect, g_symbolFont,
              g_showOutputs ? g_theme.accent : IconColor(),
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawLabel(dc, L"\uE8BB", closeRect, g_symbolFont, IconColor(),
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    if (!g_audioAvailable) {
        RECT message = {Scale(24), HeaderHeight() + Scale(30),
                        client.right - Scale(24), client.bottom - Scale(20)};
        DrawLabel(dc, L"No controllable playback sessions or default output.", message,
                  g_bodyFont, g_theme.secondaryText,
                  DT_CENTER | DT_VCENTER | DT_WORDBREAK);
    } else {
        DrawAudioRow(dc, 0, DRAG_MASTER, client.right);

        // Halfway between the master row's surface, which ends 2 pixels short
        // of the next row, and the first app row. Drawn like a hover
        // highlight, twice as strong: a shade of the background that is as
        // translucent as the background is.
        int middle = (HeaderHeight() + RowHeight() - Scale(2) + RowTop(1)) / 2;
        RECT line = {Scale(24), middle, client.right - Scale(24),
                     middle + std::max(1, Scale(1))};
        FillSolidRect(dc, line, SurfaceColor(g_theme.background, 2.0f));

        if (g_apps->empty()) {
            RECT empty = {Scale(24), RowTop(1) + Scale(8),
                          client.right - Scale(24), client.bottom - Scale(12)};
            DrawLabel(dc, g_flow == eCapture ? L"No app is using the microphone right now."
                      : g_settings.showAllSessions
                              ? L"No applications have a playback session."
                              : L"No app sessions found. Start playback to add one.",
                      empty, g_bodyFont, g_theme.secondaryText,
                      DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        } else {
            int count = VisibleAppCount();
            for (int i = 0; i < count; i++) {
                DrawAudioRow(dc, i + 1, g_scrollRow + i, client.right);
            }
        }
    }

    // A slim bar beside the app rows while they scroll; wider while it is
    // pointed at or dragged.
    if (g_audioAvailable && AppsScroll()) {
        RECT thumb = ScrollThumbRect(client.right);
        bool active = g_scrollHovered || g_scrollDragging;
        int width = Scale(active ? 8 : 6);
        int right = client.right - Scale(3);
        RECT bar{right - width, thumb.top, right, thumb.bottom};
        DrawRoundedRect(dc, bar, Radius(4),
                        MixColor(g_theme.background, g_theme.text, active ? 0.6f : 0.35f));
    }
}

struct PixelBuffer {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    DWORD* pixels = nullptr;
    int width = 0, height = 0;
    void Reset() {
        if (dc && previous) SelectObject(dc, previous);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        dc = nullptr; bitmap = nullptr; previous = nullptr; pixels = nullptr;
        width = height = 0;
    }
    bool Ensure(int w, int h) {
        if (dc && width == w && height == h) return true;
        Reset();
        if (w <= 0 || h <= 0) return false;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        dc = CreateCompatibleDC(nullptr);
        if (!bitmap || !bits || !dc) { Reset(); return false; }
        previous = SelectObject(dc, bitmap);
        if (!previous || previous == HGDI_ERROR) { previous = nullptr; Reset(); return false; }
        pixels = static_cast<DWORD*>(bits);
        width = w; height = h;
        return true;
    }
};
PixelBuffer g_blackFrame, g_whiteFrame;

DWORD RecoverPixelAlpha(DWORD black, DWORD white) {
    // Only surfaces are drawn differently against the two mattes; opaque
    // foreground content (text, icons, dividers, fully-covered pixels of
    // antialiased edges) comes out byte-identical either way. That case is
    // bit-for-bit equivalent to the general formula below (equal channels
    // give zero transparency, so alpha is 255 and each output channel is
    // just min(blackChannel, 255) == blackChannel), so skip straight to the
    // answer instead of doing three shifts, two subtractions and two clamps
    // per pixel for content that is going to end up fully opaque anyway.
    if ((black & 0x00FFFFFFu) == (white & 0x00FFFFFFu)) {
        return 0xFF000000u | (black & 0x00FFFFFFu);
    }
    int transparency = 0;
    for (int shift : {0, 8, 16}) {
        int difference = static_cast<int>((white >> shift) & 255) -
                         static_cast<int>((black >> shift) & 255);
        transparency = std::max(transparency, difference);
    }
    DWORD alpha = 255 - ClampInt(transparency, 0, 255);
    DWORD result = alpha << 24;
    for (int shift : {0, 8, 16}) {
        result |= std::min<DWORD>((black >> shift) & 255UL, alpha) << shift;
    }
    return result;
}

// Drawing the mixer is what a frame costs; moving a drawn frame is nearly
// free. A closing flyout therefore keeps the picture it has, and an opening
// one redraws only as often as the level meters update.
bool CanReuseTransparentFrame(const RECT& client, ULONGLONG now) {
    return g_frameRenderTime && !g_openWarmup && g_blackFrame.dc &&
        g_blackFrame.width == client.right && g_blackFrame.height == client.bottom &&
        (g_closeTime || (g_openTime && now - g_frameRenderTime < 33000));
}

bool PaintTransparentMixer(HWND window, const RECT& client, ULONGLONG now,
                           bool reuse = false) {
    if (!reuse) {
        g_frameRenderTime = 0;
        if (!g_blackFrame.Ensure(client.right, client.bottom) ||
            !g_whiteFrame.Ensure(client.right, client.bottom)) return false;
        // GDI does not preserve alpha. Draw the same scene against black and
        // white mattes to recover per-pixel coverage, including antialiased
        // text/icons. Only surfaces use backgroundOpacity; foreground
        // primitives stay opaque.
        g_paintMatte = 0;
        RenderMixerContents(g_blackFrame.dc, client);
        g_paintMatte = 1;
        RenderMixerContents(g_whiteFrame.dc, client);
        g_paintMatte = -1;
        GdiFlush();
        size_t count = static_cast<size_t>(client.right) * client.bottom;
        for (size_t i = 0; i < count; ++i) {
            g_blackFrame.pixels[i] = RecoverPixelAlpha(g_blackFrame.pixels[i],
                                                       g_whiteFrame.pixels[i]);
        }
        g_frameRenderTime = now;
    }
    POINT origin{};
    SIZE size{client.right, client.bottom};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    if (!g_openTime && !g_closeTime) {
        return UpdateLayeredWindow(window, nullptr, nullptr, &size, g_blackFrame.dc,
                                   &origin, 0, &blend, ULW_ALPHA) != FALSE;
    }
    // Opening or closing: the frame also moves the window and fades it.
    FlyoutFrame frame = FlyoutFrameAt(now);
    RECT bounds = FlyoutBounds(frame.offset, &origin);
    POINT position{bounds.left, bounds.top};
    size = {bounds.right - bounds.left, bounds.bottom - bounds.top};
    // The first frame after opening stays invisible; see PaceAnimation.
    blend.SourceConstantAlpha = g_openWarmup
        ? 0 : static_cast<BYTE>(255.0f * ClampVolume(frame.opacity));
    BOOL result = UpdateLayeredWindow(window, nullptr, &position, &size, g_blackFrame.dc,
                                     &origin, 0, &blend, ULW_ALPHA);
    g_slideOffset = frame.offset;
    g_fadeOpacity = frame.opacity;
    if (g_openTime && frame.finished && !g_openWarmup) SettleFlyout(window);
    return result != FALSE;
}

void PaintMixer(HWND hWnd) {
    ULONGLONG now = AnimationClock();
    if (g_openWarmup && g_openTime) g_openTime = now;
    if (g_closeTime && FlyoutFrameAt(now).finished) {
        ValidateRect(hWnd, nullptr);
        CompleteHide(hWnd);
        return;
    }
    bool transparent = g_settings.backgroundOpacity < 100 && !g_transparencyFailed;
    if (transparent) {
        RECT content = MixerContentRect(hWnd);
        if (CanReuseTransparentFrame(content, now) &&
            PaintTransparentMixer(hWnd, content, now, true)) {
            ValidateRect(hWnd, nullptr);
            RunQueuedClose(hWnd);
            PaceAnimation(hWnd);
            return;
        }
    }
    g_frameAnimating = false;
    g_frameHasAudio = false;
    g_frameSeconds = g_lastPaintTime
        ? std::min(0.05f, static_cast<float>(now - g_lastPaintTime) / 1000000.0f)
        : 0.016f;
    g_lastPaintTime = now;
    if (g_rowVisuals.size() > 256) g_rowVisuals.clear();
    POINT origin{};
    bool arrived = false;
    if (!transparent && (g_openTime || g_closeTime)) {
        // An opaque flyout is an ordinary window: it slides by being moved,
        // and it cannot fade.
        FlyoutFrame frame = FlyoutFrameAt(now);
        origin = PlaceFlyoutFrame(hWnd, frame);
        arrived = g_openTime && frame.finished && !g_openWarmup;
    }
    PAINTSTRUCT paint;
    HDC windowDc = BeginPaint(hWnd, &paint);
    RECT client = MixerContentRect(hWnd);
    if (client.right <= 0 || client.bottom <= 0) { EndPaint(hWnd, &paint); return; }
    if (transparent) {
        if (PaintTransparentMixer(hWnd, client, now)) {
            UpdatePaintTimer(hWnd);
            EndPaint(hWnd, &paint);
            RunQueuedClose(hWnd);
            PaceAnimation(hWnd);
            return;
        }
        Wh_Log(L"Mixer: transparent rendering failed, using opaque background, error %lu", GetLastError());
        g_transparencyFailed = true;
        ApplyTransparencyStyle(hWnd);
    }
    g_paintMatte = -1;
    HDC dc = CreateCompatibleDC(windowDc);
    HBITMAP bitmap = CreateCompatibleBitmap(windowDc, client.right, client.bottom);
    if (!dc || !bitmap) {
        if (dc) DeleteDC(dc);
        if (bitmap) DeleteObject(bitmap);
        EndPaint(hWnd, &paint);
        return;
    }
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    RenderMixerContents(dc, client);
    BitBlt(windowDc, 0, 0, client.right - origin.x, client.bottom - origin.y, dc,
           origin.x, origin.y, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    UpdatePaintTimer(hWnd);
    EndPaint(hWnd, &paint);
    if (arrived) SettleFlyout(hWnd);
    RunQueuedClose(hWnd);
    PaceAnimation(hWnd);
}

void ResizeMixer(HWND hWnd) {
    // A sliding flyout takes its size from PositionMixer with its next frame.
    if (g_openTime || g_closeTime) return;
    RECT windowRect;
    GetWindowRect(hWnd, &windowRect);
    SetWindowPos(hWnd, nullptr, windowRect.left, windowRect.top, PopupWidth(),
                 PopupHeight(), SWP_NOZORDER | SWP_NOACTIVATE);
}

bool GetTrayIconRect(RECT* rect) {
    NOTIFYICONIDENTIFIER identifier = {};
    identifier.cbSize = sizeof(identifier);
    identifier.hWnd = g_hWnd.load();
    identifier.uID = TRAY_ICON_ID;
    if (g_trayUsesGuid) identifier.guidItem = MIXER_TRAY_GUID;
    return SUCCEEDED(Shell_NotifyIconGetRect(&identifier, rect));
}

void PositionMixer(HWND hWnd) {
    RECT anchor = {};
    if (g_openAtPointer) {
        anchor = g_openAnchor;
    } else if (!GetTrayIconRect(&anchor)) {
        POINT cursor = {};
        GetCursorPos(&cursor);
        anchor = {cursor.x, cursor.y, cursor.x + 1, cursor.y + 1};
    }

    HMONITOR monitor = MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = {sizeof(info)};
    GetMonitorInfoW(monitor, &info);

    // The taskbar flyouts of Windows 10 sit directly on the taskbar. Those of
    // Windows 11 keep the same gap from the screen's edge as from the taskbar.
    int gap = IsWindows10Style() ? 0 : Scale(12);
    g_availableAppRows = AppRowsForHeight(info.rcWork.bottom - info.rcWork.top - 2 * gap);
    ClampPageOffset();
    int width = PopupWidth();
    int height = PopupHeight();
    int x = (anchor.left + anchor.right - width) / 2;
    int y = anchor.top - height - gap;

    if (y < info.rcWork.top) {
        y = anchor.bottom + gap;
    }
    x = ClampInt(x, info.rcWork.left + gap,
                 std::max<int>(info.rcWork.left + gap, info.rcWork.right - width - gap));
    y = ClampInt(y, info.rcWork.top, std::max(info.rcWork.top, info.rcWork.bottom - height));

    // The flyout slides out from the taskbar's edge and is cut off there, so
    // that nothing of it shows over or through the taskbar. Where the tray
    // icon is not in a taskbar that takes up screen space (the taskbar hides
    // itself, or the icon is in the overflow), that edge is the icon's own.
    g_restRect = {x, y, x + width, y + height};
    g_slideFromAbove = y + height / 2 > (anchor.top + anchor.bottom) / 2;
    int edge = g_slideFromAbove
        ? std::max<int>(info.rcWork.top, std::min<int>(y, anchor.bottom))
        : std::min<int>(info.rcWork.bottom, std::max<int>(y + height, anchor.top));
    g_slideClip = {LONG_MIN / 2, LONG_MIN / 2, LONG_MAX / 2, LONG_MAX / 2};
    (g_slideFromAbove ? g_slideClip.top : g_slideClip.bottom) = edge;
    // A closed flyout is past that edge: all of it in the Windows 11 style, a
    // short way in the Windows 10 style. Windows cannot round the corners of
    // a window only a few pixels tall, so a strip of the flyout stays put.
    int hidden = g_slideFromAbove ? y + height - edge : edge - y;
    int travel = IsWindows10Style() ? Scale(50) : hidden;
    g_slideTravel = std::max(std::min(travel, hidden - Scale(20)), 0);
    // While the flyout slides, each frame puts the window where it belongs.
    if (g_openTime || g_closeTime) return;

    RECT current = {};
    if (!GetWindowRect(hWnd, &current) || current.left != x || current.top != y ||
        current.right - current.left != width || current.bottom - current.top != height) {
        SetWindowPos(hWnd, HWND_TOPMOST, x, y, width, height,
                     SWP_NOOWNERZORDER | SWP_NOACTIVATE);
    }
}

// Windows shows notifications in a layer above every window but its own, so
// they would cover the mixer. Quick Settings, which is in that layer, covers
// them instead. While the mixer is open, a notification over it is moved off
// screen, and moved back once the mixer has closed.
//
// Windows reuses a notification's window: it places it for each new
// notification, and shrinks it away when one ends. One that Windows has
// placed again in the meantime is left where Windows put it.
constexpr int NOTIFICATION_AWAY = -32000;

struct MovedNotification {
    HWND window;
    POINT home;
};
std::vector<MovedNotification> g_movedNotifications;
HWINEVENTHOOK g_notificationHook;
DWORD g_notificationProcessId;  // ShellExperienceHost of this session.

// The title Windows gives a notification's window, in the display language.
// Other windows of the process, such as Quick Settings and the clock, open
// in the same corner and must stay where they are.
const std::wstring& NotificationTitle() {
    static const std::wstring title = [] {
        WCHAR windows[MAX_PATH] = {};
        GetWindowsDirectoryW(windows, ARRAYSIZE(windows));
        std::wstring source = L"@{" + std::wstring(windows) +
            L"\\SystemResources\\Windows.UI.ShellCommon\\Windows.UI.ShellCommon.pri"
            L"? ms-resource:///ActionCenter/AC_ToastCenter_Title}";
        WCHAR text[256] = {};
        if (SUCCEEDED(SHLoadIndirectString(source.c_str(), text, ARRAYSIZE(text), nullptr)) &&
            text[0]) {
            return std::wstring(text);
        }
        // The English fallback only matches on an English Windows. If this
        // shows in the log, moving notifications aside won't work.
        Wh_Log(L"Mixer: the notification window title couldn't be read from Windows");
        return std::wstring(L"New notification");
    }();
    return title;
}

bool IsNotificationWindow(HWND window) {
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (!processId || processId != g_notificationProcessId) return false;
    WCHAR className[32] = {};
    if (!GetClassNameW(window, className, ARRAYSIZE(className)) ||
        wcscmp(className, L"Windows.UI.Core.CoreWindow") != 0) {
        return false;
    }
    // Reads the title without sending a message, which could wait on a busy
    // process.
    WCHAR title[128] = {};
    InternalGetWindowText(window, title, ARRAYSIZE(title));
    return NotificationTitle() == title;
}

DWORD FindNotificationProcess() {
    DWORD session = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) return 0;
    DWORD found = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entry = {sizeof(entry)};
    for (BOOL more = Process32FirstW(snapshot, &entry); more && !found;
         more = Process32NextW(snapshot, &entry)) {
        DWORD processSession = 0;
        if (_wcsicmp(entry.szExeFile, L"ShellExperienceHost.exe") == 0 &&
            ProcessIdToSessionId(entry.th32ProcessID, &processSession) &&
            processSession == session) {
            found = entry.th32ProcessID;
        }
    }
    CloseHandle(snapshot);
    return found;
}

// Moves another process's window without waiting for it, so a busy shell
// cannot hold up the mixer.
constexpr UINT NOTIFICATION_MOVE_FLAGS = SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                                         SWP_NOOWNERZORDER | SWP_ASYNCWINDOWPOS;

void KeepNotificationAway(HWND window) {
    RECT rect;
    if (!IsWindowVisible(window) || !GetWindowRect(window, &rect)) return;
    auto moved = std::find_if(g_movedNotifications.begin(), g_movedNotifications.end(),
                              [&](const MovedNotification& item) { return item.window == window; });
    if (moved != g_movedNotifications.end()) {
        if (rect.left == NOTIFICATION_AWAY && rect.top == NOTIFICATION_AWAY) return;
        g_movedNotifications.erase(moved); // Windows has placed it again.
    }
    RECT overlap;
    if (!IntersectRect(&overlap, &rect, &g_restRect) || !IsNotificationWindow(window)) return;
    if (SetWindowPos(window, nullptr, NOTIFICATION_AWAY, NOTIFICATION_AWAY, 0, 0,
                     NOTIFICATION_MOVE_FLAGS)) {
        g_movedNotifications.push_back({window, {rect.left, rect.top}});
    }
}

void CALLBACK NotificationEvent(HWINEVENTHOOK, DWORD event, HWND window, LONG object,
                                LONG child, DWORD, DWORD) {
    if ((event == EVENT_OBJECT_SHOW || event == EVENT_OBJECT_LOCATIONCHANGE) && window &&
        object == OBJID_WINDOW && child == CHILDID_SELF &&
        GetAncestor(window, GA_ROOT) == window) {
        KeepNotificationAway(window);
    }
}

// False once the process the hook was set for has ended (the shell host
// restarts after a crash or an update), as the hook then watches nothing.
bool NotificationProcessAlive() {
    if (!g_notificationProcessId) return false;
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, g_notificationProcessId);
    if (!process) return false;
    bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return alive;
}

void KeepNotificationsAway() {
    if (!g_settings.moveNotifications) return;
    if (g_notificationHook && !NotificationProcessAlive()) {
        Wh_Log(L"Mixer: the notification process ended; watching the new one");
        UnhookWinEvent(g_notificationHook);
        g_notificationHook = nullptr;
    }
    // Only the notifications' own process is watched: watching every
    // process would also report each move of the pointer.
    if (!g_notificationHook) {
        g_notificationProcessId = FindNotificationProcess();
        if (!g_notificationProcessId) return;
        g_notificationHook = SetWinEventHook(
            EVENT_OBJECT_SHOW, EVENT_OBJECT_LOCATIONCHANGE, nullptr, NotificationEvent,
            g_notificationProcessId, 0, WINEVENT_OUTOFCONTEXT);
    }
    // Notifications showing already. They are not listed with other windows,
    // but can be found under the mixer.
    int step = Scale(40);
    for (int y = g_restRect.top + step / 2; y < g_restRect.bottom; y += step) {
        for (int x = g_restRect.left + step / 2; x < g_restRect.right; x += step) {
            HWND window = GetAncestor(WindowFromPoint({x, y}), GA_ROOT);
            if (window && window != g_hWnd.load()) KeepNotificationAway(window);
        }
    }
}

void ReturnNotifications() {
    if (g_notificationHook) {
        UnhookWinEvent(g_notificationHook);
        g_notificationHook = nullptr;
    }
    for (const MovedNotification& moved : g_movedNotifications) {
        RECT rect;
        if (IsWindow(moved.window) && GetWindowRect(moved.window, &rect) &&
            rect.left == NOTIFICATION_AWAY && rect.top == NOTIFICATION_AWAY) {
            SetWindowPos(moved.window, nullptr, moved.home.x, moved.home.y, 0, 0,
                         NOTIFICATION_MOVE_FLAGS);
        }
    }
    g_movedNotifications.clear();
}

void ShowMixer(HWND hWnd) {
    if (g_showingMixer) {
        return;
    }
    g_showingMixer = true;
    KillTimer(hWnd, TIMER_CHECK_FOCUS);
    HideVolumeIndicator(hWnd);
    g_closeQueued = false;
    bool opening = !MixerOpen(hWnd);
    bool turning = false;
    if (opening) {
        SelectRow(DRAG_MASTER);
        RefreshAudioSessions(hWnd, true);
        UpdateMotionPreference(hWnd);
        // A flyout that is still closing turns around where it is.
        turning = g_closeTime != 0;
        g_closeTime = 0;
        g_openTime = g_motionEnabled ? AnimationClock() : 0;
        g_openWarmup = g_openTime && !turning;
        KillTimer(hWnd, TIMER_TRIM);
        g_pacedSince = 0;
        g_lastPaintTime = 0;
        g_hoverRow = DRAG_NONE;
        g_closeHovered = false;
        g_closeHoverAmount = 0;
        g_rowVisuals.clear();
    }
    PositionMixer(hWnd);
    KeepNotificationsAway();
    if (opening && g_openTime) {
        g_slideStart = turning ? g_slideOffset : static_cast<float>(g_slideTravel);
        g_fadeStart = turning ? g_fadeOpacity : IsWindows10Style() ? 0.8f : 1.0f;
    }
    if (opening && g_settings.backgroundOpacity < 100 && !g_transparencyFailed) {
        // A hidden layered window keeps its last frame, and its backdrop
        // effect appears the moment the window is shown. Present the first
        // frame before showing the window, which is fully transparent when
        // animating, so neither can flash in the final position ahead of the
        // opening animation.
        RECT client = MixerContentRect(hWnd);
        if (client.right > 0 && client.bottom > 0) {
            PaintTransparentMixer(hWnd, client, g_openTime);
        }
    } else if (opening && g_openTime) {
        PlaceFlyoutFrame(hWnd, FlyoutFrameAt(g_openTime));
    }
    ShowWindow(hWnd, SW_SHOWNORMAL);
    SetTimer(hWnd, TIMER_REFRESH, 1000, nullptr);
    SetPaintTimer(hWnd, g_motionEnabled ? 16 : 80);
    BOOL foreground = SetForegroundWindow(hWnd);
    if (foreground) {
        SetFocus(hWnd);
    } else if (g_settings.closeWhenFocusIsLost) {
        SetTimer(hWnd, TIMER_CHECK_FOCUS, 150, nullptr);
    }
    InvalidateRect(hWnd, nullptr, FALSE);
    UpdateWindow(hWnd);
    g_showingMixer = false;
    Wh_Log(L"Mixer: popup shown, foreground request=%d", foreground);
}

void QueueMixerAction(HWND hWnd, bool close) {
    KillTimer(hWnd, TIMER_CHECK_FOCUS);
    if (!g_showRequestPending) {
        g_showRequestPending = true;
        g_closeRequestPending = close;
        // Return to the shell callback before showing/activating the popup.
        if (!PostMessageW(hWnd, WM_APP_SHOW_MIXER, 0, 0)) {
            g_showRequestPending = false;
            Wh_Log(L"Mixer: could not queue popup, error %lu", GetLastError());
        }
    }
}

void QueueShowMixer(HWND hWnd) {
    QueueMixerAction(hWnd, false);
}

void RememberTrayPress(HWND hWnd) {
    g_trayPressKnown = true;
    g_trayPressWasVisible = MixerOpen(hWnd);
}

void QueueTrayToggle(HWND hWnd, bool keyboard) {
    g_openAtPointer = false;  // Opened from the tray icon: next to it.
    // The taskbar can take focus and dismiss the popup before mouse-up.
    // Use its visibility at mouse-down, so a closing click cannot reopen it.
    bool close = !keyboard && g_trayPressKnown
        ? g_trayPressWasVisible : MixerOpen(hWnd);
    g_trayPressKnown = false;
    // Like the taskbar's own flyouts, the mixer finishes opening or closing
    // before the icon toggles it again.
    if (g_openTime || g_closeTime) {
        if (g_openTime) {
            // Pressing the icon took the focus away. Take it back, or the
            // mixer would close for having lost it.
            KillTimer(hWnd, TIMER_CHECK_FOCUS);
            if (SetForegroundWindow(hWnd)) {
                SetFocus(hWnd);
            } else if (g_settings.closeWhenFocusIsLost) {
                SetTimer(hWnd, TIMER_CHECK_FOCUS, 150, nullptr);
            }
        }
        return;
    }
    QueueMixerAction(hWnd, close);
}

// The key that opens the mixer: it opens at the bottom right of the monitor the pointer is on, where that
// monitor's taskbar has its clock, and closes again on the next press.
void HandleOpenHotkey(HWND hWnd) {
    if (MixerOpen(hWnd)) {
        QueueMixerAction(hWnd, true);
        return;
    }
    if (g_openTime || g_closeTime) return;
    POINT cursor{};
    GetCursorPos(&cursor);
    MONITORINFO info{sizeof(info)};
    if (!GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &info)) return;
    g_openAnchor = {info.rcWork.right - 1, info.rcWork.bottom, info.rcWork.right, info.rcWork.bottom + 1};
    g_openAtPointer = true;
    QueueShowMixer(hWnd);
}

// Frees what only an open mixer needs: its frame buffers and drawn text.
void TrimMixerMemory() {
    g_blackFrame.Reset();
    g_whiteFrame.Reset();
    ClearTextCache();
}

// Takes the flyout off screen, once any closing animation has run.
void CompleteHide(HWND hWnd) {
    g_flow = eRender;  // The mixer opens on playback again.
    KillTimer(hWnd, TIMER_METERS);
    g_paintTimerInterval = 0;
    g_openTime = 0;
    g_closeTime = 0;
    g_openWarmup = false;
    g_closeQueued = false;
    ShowWindow(hWnd, SW_HIDE);
    RestFlyoutWindow(hWnd);
    ReturnNotifications();
    // Reopening soon is much cheaper with the buffers and text still at
    // hand, so keep them for a little while.
    if (!SetTimer(hWnd, TIMER_TRIM, 10000, nullptr)) TrimMixerMemory();
}

void HideMixer(HWND hWnd) {
    FinishVolumeEntry(hWnd, false, false);
    CancelNameTooltip(hWnd);
    g_showRequestPending = false;
    KillTimer(hWnd, TIMER_CHECK_FOCUS);
    KillTimer(hWnd, TIMER_REFRESH);
    g_dragRow = DRAG_NONE;
    if (GetCapture() == hWnd) {
        ReleaseCapture();
    }
    if (g_closeTime) return; // Already on its way out.
    // Nothing of a flyout that has yet to show its first frame can be seen,
    // and the taskbar flyouts of Windows 10 close without an animation.
    if (!g_motionEnabled || !IsWindowVisible(hWnd) || g_openWarmup ||
        IsWindows10Style()) {
        CompleteHide(hWnd);
        return;
    }
    // A flyout that is still opening closes once it has arrived.
    if (g_openTime) {
        g_closeQueued = true;
        return;
    }
    g_slideStart = 0.0f;
    g_fadeStart = 1.0f;
    g_closeTime = AnimationClock();
    g_pacedSince = 0;
    SetPaintTimer(hWnd, 16);
    InvalidateRect(hWnd, nullptr, FALSE);
}

enum class TrayAction { None, Open, Menu, ToggleMute };

TrayAction GetTrayAction(UINT event, bool version4) {
    if (event == WM_MBUTTONUP) {
        return TrayAction::ToggleMute;
    }
    if (version4) {
        if (event == NIN_SELECT || event == NIN_KEYSELECT) {
            return TrayAction::Open;
        }
        if (event == WM_CONTEXTMENU) {
            return TrayAction::Menu;
        }
    } else {
        if (event == WM_LBUTTONUP) {
            return TrayAction::Open;
        }
        if (event == WM_RBUTTONUP) {
            return TrayAction::Menu;
        }
    }
    return TrayAction::None;
}

void AddTrayIcon(HWND hWnd) {
    // Windows also announces a new taskbar when the old one's icons have
    // survived, such as after a display scaling change. Adding the icon again
    // then fails, and the fallback below would add a second icon under the
    // other identity. Remove whatever is registered first; this also clears
    // an icon left behind by an instance that did not exit cleanly.
    NOTIFYICONDATAW stale = {};
    stale.cbSize = sizeof(stale);
    stale.hWnd = hWnd;
    stale.uID = TRAY_ICON_ID;
    Shell_NotifyIconW(NIM_DELETE, &stale);
    stale.uFlags = NIF_GUID;
    stale.guidItem = MIXER_TRAY_GUID;
    Shell_NotifyIconW(NIM_DELETE, &stale);

    g_trayVersion4 = false;
    g_trayUsesGuid = true;
    NOTIFYICONDATAW data = {};
    data.cbSize = sizeof(data);
    data.hWnd = hWnd;
    data.uID = TRAY_ICON_ID;
    data.guidItem = MIXER_TRAY_GUID;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_GUID | NIF_SHOWTIP;
    data.uCallbackMessage = WM_APP_TRAY;
    data.hIcon = g_trayIcon;
    wcscpy_s(data.szTip, L"Headroom");
    if (!Shell_NotifyIconW(NIM_ADD, &data)) {
        // A GUID can remain associated with an earlier Windhawk executable path.
        // Keep the same identity mode for all later operations on this icon.
        data.uFlags &= ~NIF_GUID;
        data.guidItem = {};
        g_trayUsesGuid = false;
        if (!Shell_NotifyIconW(NIM_ADD, &data)) {
            g_trayAdded = false;
            Wh_Log(L"Mixer: tray icon registration failed");
            return;
        }
        Wh_Log(L"Mixer: tray icon registered using window/ID fallback");
    }
    g_trayAdded = true;
    Wh_Log(L"Mixer: tray icon registered");

    data.uVersion = NOTIFYICON_VERSION_4;
    g_trayVersion4 = Shell_NotifyIconW(NIM_SETVERSION, &data) != FALSE;
    if (!g_trayVersion4) {
        Wh_Log(L"Mixer: tray icon version negotiation failed");
    }
    Wh_Log(L"Mixer: tray callback mode=%s", g_trayVersion4 ? L"v4" : L"legacy");
}

void RemoveTrayIcon(HWND hWnd) {
    NOTIFYICONDATAW data = {};
    data.cbSize = sizeof(data);
    data.hWnd = hWnd;
    data.uID = TRAY_ICON_ID;
    data.uFlags = g_trayUsesGuid ? NIF_GUID : 0;
    if (g_trayUsesGuid) data.guidItem = MIXER_TRAY_GUID;
    Shell_NotifyIconW(NIM_DELETE, &data);
}

// While the mixer is closed, changing master volume from the tray icon shows
// the result in a small indicator above the icon, which soon goes away again.
PixelBuffer g_indicatorBlack, g_indicatorWhite;

void DrawVolumeIndicator(HDC dc, const RECT& client, float volume, bool muted) {
    if (g_paintMatte >= 0) {
        FillSolidRect(dc, client, g_paintMatte ? RGB(255, 255, 255) : RGB(0, 0, 0));
        DrawSurface(dc, client, Radius(20), g_theme.background);
    } else {
        FillSolidRect(dc, client, g_theme.background);
    }
    RECT glyph{Scale(12), 0, Scale(40), client.bottom};
    DrawLabel(dc, VolumeGlyph(volume, muted), glyph, g_symbolFont, IconColor(),
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    WCHAR percent[16];
    swprintf_s(percent, L"%d%%", static_cast<int>(volume * 100.0f + 0.5f));
    RECT text{client.right - Scale(58), 0, client.right - Scale(14), client.bottom};
    DrawLabel(dc, percent, text, g_bodyFont, muted ? g_theme.secondaryText : g_theme.text,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE, /*percentage=*/true);
    int middle = client.bottom / 2;
    RECT rail{Scale(50), middle - Scale(2), client.right - Scale(68), middle + Scale(2)};
    DrawRoundedRect(dc, rail, Radius(2), MixColor(g_theme.background, g_theme.text, 0.3f));
    RECT fill = rail;
    fill.right = rail.left + static_cast<int>(std::lround((rail.right - rail.left) * volume));
    if (fill.right > fill.left) {
        DrawRoundedRect(dc, fill, Radius(2), muted ? g_theme.muted : g_theme.sliderFill);
    }
}

// Draws a popup the way the mixer is drawn: against black and against white
// to recover its translucency, or opaque. Then puts it on screen.
template <typename Draw>
void PaintLayeredPopup(HWND window, PixelBuffer& blackBuffer, PixelBuffer& whiteBuffer,
                       int width, int height, Draw draw) {
    if (!blackBuffer.Ensure(width, height) || !whiteBuffer.Ensure(width, height)) return;
    RECT client{0, 0, width, height};
    bool transparent = g_settings.backgroundOpacity < 100 && !g_transparencyFailed;
    g_paintMatte = transparent ? 0 : -1;
    draw(blackBuffer.dc, client);
    if (transparent) {
        g_paintMatte = 1;
        draw(whiteBuffer.dc, client);
    }
    g_paintMatte = -1;
    GdiFlush();
    size_t count = static_cast<size_t>(width) * height;
    for (size_t i = 0; i < count; ++i) {
        DWORD black = blackBuffer.pixels[i];
        blackBuffer.pixels[i] = transparent
            ? RecoverPixelAlpha(black, whiteBuffer.pixels[i])
            : 0xFF000000u | (black & 0x00FFFFFFu);
    }
    POINT origin{};
    SIZE size{width, height};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(window, nullptr, nullptr, &size, blackBuffer.dc, &origin, 0, &blend,
                        ULW_ALPHA);
}

void PaintVolumeIndicator(int width, int height) {
    float volume = GetMasterVolume();
    bool muted = GetMasterMuted();
    PaintLayeredPopup(g_volumeIndicator, g_indicatorBlack, g_indicatorWhite, width, height,
                      [&](HDC dc, const RECT& client) {
                          DrawVolumeIndicator(dc, client, volume, muted);
                      });
}

void HideVolumeIndicator(HWND owner) {
    KillTimer(owner, TIMER_VOLUME_INDICATOR);
    if (g_volumeIndicator && IsWindowVisible(g_volumeIndicator)) {
        ShowWindow(g_volumeIndicator, SW_HIDE);
    }
}

void ShowVolumeIndicator(HWND owner) {
    if (IsWindowVisible(owner)) return; // The mixer itself shows the volume.
    RECT anchor{};
    if (!GetTrayIconRect(&anchor)) return;
    if (!g_volumeIndicator) {
        g_volumeIndicator = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED |
                WS_EX_TRANSPARENT,
            VOLUME_INDICATOR_CLASS, L"", WS_POPUP, 0, 0, 1, 1, owner, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        if (!g_volumeIndicator) return;
    }
    // Placed like the mixer: above the icon, or below it on a top taskbar.
    MONITORINFO info = {sizeof(info)};
    GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &info);
    int width = Scale(220), height = Scale(48);
    int gap = IsWindows10Style() ? 0 : Scale(12);
    int x = (anchor.left + anchor.right - width) / 2;
    int y = anchor.top - height - gap;
    if (y < info.rcWork.top) y = anchor.bottom + gap;
    x = ClampInt(x, info.rcWork.left + gap,
                 std::max<int>(info.rcWork.left + gap, info.rcWork.right - width - gap));
    y = ClampInt(y, info.rcWork.top, std::max(info.rcWork.top, info.rcWork.bottom - height));
    SetWindowPos(g_volumeIndicator, HWND_TOPMOST, x, y, width, height,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    ApplyWindowFrame(g_volumeIndicator);
    ApplyBackdropBlur(g_volumeIndicator);
    PaintVolumeIndicator(width, height);
    if (!IsWindowVisible(g_volumeIndicator)) {
        ShowWindow(g_volumeIndicator, SW_SHOWNOACTIVATE);
    }
    SetTimer(owner, TIMER_VOLUME_INDICATOR, VOLUME_INDICATOR_TIME, nullptr);
}

LRESULT CALLBACK VolumeIndicatorProc(HWND window, UINT message, WPARAM wParam,
                                     LPARAM lParam) {
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(window, message, wParam, lParam);
}

// Tray icons get no mouse-wheel messages. While the pointer is over the icon,
// watch the mouse through a low-level hook instead, which works whatever has
// the focus and leaves the wheel working as usual everywhere else. Hooking
// only then keeps every move of the mouse from waking the mixer up.
//
// Windows holds up the mouse input of the whole system until a low-level hook
// returns, and calls it on the thread that set it. So the hook lives on a
// thread that does nothing else, never waiting for the UI thread while it
// paints a frame or refreshes the sessions.
//
// The taskbar reports the pointer moving onto the icon, but not always, so a
// slow timer also watches for it. Once armed, the hook itself notices the
// pointer leaving. Both test against a cached rect of the icon; the taskbar
// is asked for it again only when it may have changed.
constexpr UINT WM_TRAY_HOOK_ARM = WM_APP + 1; // Thread message. wParam: 1 arm, 0 disarm.
constexpr ULONGLONG TRAY_RECT_LIFETIME = 5000;

SRWLOCK g_trayIconRectLock = SRWLOCK_INIT;
RECT g_trayIconRect{};         // Read by the hook thread, under the lock.
ULONGLONG g_trayIconRectTime;  // When the rect was last asked for.
HANDLE g_trayHookThread;
DWORD g_trayHookThreadId;

void ForgetTrayIconRect() {
    g_trayIconRectTime = 0;
}

// The taskbar says the pointer is over the icon when it reports a move, so a
// rect that does not hold the pointer then may be out of date.
bool PointerOverTrayIcon(bool taskbarSaysOver) {
    POINT cursor{};
    if (!GetCursorPos(&cursor)) return false;
    AcquireSRWLockShared(&g_trayIconRectLock);
    RECT rect = g_trayIconRect;
    ReleaseSRWLockShared(&g_trayIconRectLock);
    ULONGLONG now = GetTickCount64();
    ULONGLONG age = now - g_trayIconRectTime;
    if (!g_trayIconRectTime || age > TRAY_RECT_LIFETIME ||
        (taskbarSaysOver && !PtInRect(&rect, cursor) && age > 100)) {
        if (!GetTrayIconRect(&rect)) SetRectEmpty(&rect);
        AcquireSRWLockExclusive(&g_trayIconRectLock);
        g_trayIconRect = rect;
        ReleaseSRWLockExclusive(&g_trayIconRectLock);
        g_trayIconRectTime = now;
    }
    return PtInRect(&rect, cursor);
}

// Runs on the hook thread, so it only looks at the pointer and posts.
LRESULT CALLBACK TrayWheelHook(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION && (message == WM_MOUSEMOVE || message == WM_MOUSEWHEEL)) {
        auto* mouse = reinterpret_cast<const MSLLHOOKSTRUCT*>(data);
        AcquireSRWLockShared(&g_trayIconRectLock);
        bool over = PtInRect(&g_trayIconRect, mouse->pt);
        ReleaseSRWLockShared(&g_trayIconRectLock);
        if (!over) {
            PostThreadMessageW(GetCurrentThreadId(), WM_TRAY_HOOK_ARM, 0, 0);
        } else if (message == WM_MOUSEWHEEL) {
            if (HWND window = g_hWnd.load()) {
                PostMessageW(window, WM_APP_TRAY_WHEEL,
                             static_cast<WPARAM>(static_cast<SHORT>(HIWORD(mouse->mouseData))),
                             0);
            }
        }
    }
    return CallNextHookEx(nullptr, code, message, data);
}

DWORD WINAPI TrayHookThreadProc(LPVOID readyEvent) {
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    MSG message;
    PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE); // Create the queue.
    SetEvent(static_cast<HANDLE>(readyEvent));
    HHOOK hook = nullptr;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message != WM_TRAY_HOOK_ARM) continue;
        if (message.wParam && !hook) {
            hook = SetWindowsHookExW(WH_MOUSE_LL, TrayWheelHook, GetModuleHandleW(nullptr), 0);
            if (hook) {
                g_trayWheelArmed = true;
                Wh_Log(L"Mixer: pointer over the tray icon, listening to the wheel");
            } else {
                Wh_Log(L"Mixer: tray icon scrolling unavailable, error %lu", GetLastError());
            }
        } else if (!message.wParam && hook) {
            UnhookWindowsHookEx(hook);
            hook = nullptr;
            g_trayWheelArmed = false;
            Wh_Log(L"Mixer: pointer left the tray icon");
        }
    }
    if (hook) UnhookWindowsHookEx(hook);
    g_trayWheelArmed = false;
    return 0;
}

void StartTrayHookThread() {
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ready) return;
    g_trayHookThread = CreateThread(nullptr, 0, TrayHookThreadProc, ready, 0, &g_trayHookThreadId);
    if (g_trayHookThread) {
        WaitForSingleObject(ready, INFINITE);
    } else {
        g_trayHookThreadId = 0;
        Wh_Log(L"Mixer: tray icon scrolling unavailable, error %lu", GetLastError());
    }
    CloseHandle(ready);
}

void StopTrayHookThread() {
    if (!g_trayHookThread) return;
    PostThreadMessageW(g_trayHookThreadId, WM_QUIT, 0, 0);
    WaitForSingleObject(g_trayHookThread, INFINITE);
    CloseHandle(g_trayHookThread);
    g_trayHookThread = nullptr;
    g_trayHookThreadId = 0;
}

void ArmTrayWheel() {
    if (g_trayWheelArmed || !g_trayHookThreadId) return;
    g_trayWheelRemainder = 0;
    PostThreadMessageW(g_trayHookThreadId, WM_TRAY_HOOK_ARM, 1, 0);
}

void DisarmTrayWheel() {
    if (g_trayHookThreadId) PostThreadMessageW(g_trayHookThreadId, WM_TRAY_HOOK_ARM, 0, 0);
}

// Runs on TIMER_TRAY_WATCH, for the moves onto the icon the taskbar misses.
void WatchTrayPointer() {
    bool over = PointerOverTrayIcon(false);
    if (!g_trayWheelArmed && over) {
        ArmTrayWheel();
    } else if (g_trayWheelArmed && !over) {
        // The hook disarms itself when it sees the pointer leave. If Windows
        // removed it for being slow, it never will, and the wheel would stay
        // "armed" without a working hook until the mixer restarts.
        DisarmTrayWheel();
    }
}

// Changes master volume by the mouse-wheel step for each notch. High-resolution
// wheels report fractions of a notch, which add up.
void ScrollMasterVolume(HWND hWnd, int delta) {
    g_trayWheelRemainder += delta;
    int notches = g_trayWheelRemainder / WHEEL_DELTA;
    if (!notches) return;
    g_trayWheelRemainder -= notches * WHEEL_DELTA;
    {
        FlowScope playback(eRender);
        SetRowVolume(DRAG_MASTER,
                     GetMasterVolume() + notches * g_settings.volumeStep / 100.0f);
    }
    if (IsWindowVisible(hWnd)) {
        InvalidateRect(hWnd, nullptr, FALSE);
    } else {
        ShowVolumeIndicator(hWnd);
    }
}

bool SpeakerShapeContains(float x, float y, bool includeWaves = true) {
    constexpr float points[][2] = {
        {1.5f, 5.5f}, {4.5f, 5.5f}, {8.0f, 2.5f},
        {8.0f, 13.5f}, {4.5f, 10.5f}, {1.5f, 10.5f}};
    bool inside = false;
    for (int i = 0, j = 5; i < 6; j = i++) {
        if ((points[i][1] > y) != (points[j][1] > y) &&
            x < (points[j][0] - points[i][0]) * (y - points[i][1]) /
                    (points[j][1] - points[i][1]) + points[i][0]) {
            inside = !inside;
        }
    }
    if (inside || !includeWaves) return inside;
    float dx = x - 6.8f, dy = y - 8.0f;
    float radius = std::sqrt(dx * dx + dy * dy);
    bool inner = dx > 2.2f && std::abs(dy) < 2.8f &&
                 std::abs(radius - 3.8f) < 0.65f;
    bool outer = dx > 3.8f && std::abs(dy) < 5.6f &&
                 std::abs(radius - 6.6f) < 0.65f;
    return inner || outer;
}

bool SpeakerMuteMarkContains(float x, float y) {
    auto segmentDistance = [](float px, float py, float ax, float ay,
                              float bx, float by) {
        float vx = bx - ax;
        float vy = by - ay;
        float wx = px - ax;
        float wy = py - ay;
        float lengthSquared = vx * vx + vy * vy;
        float t = lengthSquared > 0.0f
            ? std::max(0.0f, std::min(1.0f,
                  (wx * vx + wy * vy) / lengthSquared))
            : 0.0f;
        float dx = px - (ax + t * vx);
        float dy = py - (ay + t * vy);
        return std::sqrt(dx * dx + dy * dy);
    };

    constexpr float thickness = 0.8f;
    return segmentDistance(x, y, 10.0f, 6.0f, 14.0f, 10.0f) < thickness ||
           segmentDistance(x, y, 10.0f, 10.0f, 14.0f, 6.0f) < thickness;
}

HICON CreateSpeakerIcon(int pixels, COLORREF color, float shapeScale = 1.0f,
                        bool softenEdges = false, bool muted = false) {
    pixels = ClampInt(pixels, 16, 256);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = pixels;
    info.bmiHeader.biHeight = -pixels;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        return nullptr;
    }
    auto output = static_cast<DWORD*>(bits);
    const int samples = softenEdges ? 12 : 8;
    const float sampleSpan = softenEdges ? 1.35f : 1.0f;
    for (int y = 0; y < pixels; ++y) {
        for (int x = 0; x < pixels; ++x) {
            int covered = 0;
            for (int sy = 0; sy < samples; ++sy) {
                for (int sx = 0; sx < samples; ++sx) {
                    float offsetX = ((sx + 0.5f) / samples - 0.5f) * sampleSpan;
                    float offsetY = ((sy + 0.5f) / samples - 0.5f) * sampleSpan;
                    float designX = (x + 0.5f + offsetX) * 16.0f / pixels;
                    float designY = (y + 0.5f + offsetY) * 16.0f / pixels;
                    float shapeX = 8.0f + (designX - 8.0f) / shapeScale;
                    float shapeY = 8.0f + (designY - 8.0f) / shapeScale;
                    bool inside = SpeakerShapeContains(shapeX, shapeY, !muted);
                    if (muted && SpeakerMuteMarkContains(shapeX, shapeY)) {
                        inside = true;
                    }
                    covered += inside;
                }
            }
            DWORD alpha = (covered * 255 + samples * samples / 2) / (samples * samples);
            output[y * pixels + x] = (alpha << 24) |
                ((GetRValue(color) * alpha / 255) << 16) |
                ((GetGValue(color) * alpha / 255) << 8) |
                (GetBValue(color) * alpha / 255);
        }
    }
    std::vector<BYTE> maskBits(((pixels + 15) / 16) * 2 * pixels, 0);
    HBITMAP mask = CreateBitmap(pixels, pixels, 1, 1, maskBits.data());
    HICON icon = nullptr;
    if (mask) {
        ICONINFO iconInfo{};
        iconInfo.fIcon = TRUE;
        iconInfo.hbmColor = bitmap;
        iconInfo.hbmMask = mask;
        icon = CreateIconIndirect(&iconInfo);
        DeleteObject(mask);
    }
    DeleteObject(bitmap);
    return icon;
}

HICON LoadMixerIcon(int pixels, bool tray = false, bool muted = false) {
    COLORREF color = g_theme.text;
    if (tray) {
        DWORD light = 0, size = sizeof(light);
        RegGetValueW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
        color = light ? RGB(30, 32, 36) : RGB(248, 249, 252);
    }
    if (HICON icon = CreateSpeakerIcon(pixels, color, tray ? 1.12f : 1.0f, !tray, muted)) {
        return icon;
    }
    WCHAR systemDirectory[MAX_PATH];
    UINT length = GetSystemDirectoryW(systemDirectory, ARRAYSIZE(systemDirectory));
    if (length && length < ARRAYSIZE(systemDirectory)) {
        std::wstring path(systemDirectory, length);
        path += L"\\SndVol.exe";
        if (HICON icon = ExtractSizedIcon(path, pixels)) {
            return icon;
        }
    }
    return static_cast<HICON>(LoadImageW(nullptr, IDI_APPLICATION, IMAGE_ICON,
                                         pixels, pixels, 0));
}

// Draws a volume glyph as an icon the way the taskbar draws its own icons:
// with DirectWrite, at regular weight, with grayscale antialiasing. Compared
// with the Windows 11 wifi icon, this matches it nearly pixel for pixel.
HICON CreateGlyphIcon(int pixels, PCWSTR glyph, COLORREF color) {
    // Created once and kept for the life of the process.
    static ID2D1Factory* d2d = [] {
        ID2D1Factory* factory = nullptr;
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory);
        return factory;
    }();
    static IDWriteFactory* dwrite = [] {
        IDWriteFactory* factory = nullptr;
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown**>(&factory));
        return factory;
    }();
    if (!d2d || !dwrite) return nullptr;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = pixels;
    info.bmiHeader.biHeight = -pixels;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC dc = CreateCompatibleDC(nullptr);
    if (!bitmap || !bits || !dc) {
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        return nullptr;
    }
    HGDIOBJ previous = SelectObject(dc, bitmap);

    // White on black: the brightness of each pixel is the glyph's coverage.
    bool drawn = false;
    ID2D1DCRenderTarget* target = nullptr;
    IDWriteTextFormat* format = nullptr;
    ID2D1SolidColorBrush* brush = nullptr;
    D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.0f, 96.0f);
    RECT area{0, 0, pixels, pixels};
    if (SUCCEEDED(d2d->CreateDCRenderTarget(&properties, &target)) &&
        SUCCEEDED(target->BindDC(dc, &area)) &&
        SUCCEEDED(dwrite->CreateTextFormat(g_symbolFace, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                           DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                           static_cast<float>(pixels), L"", &format)) &&
        SUCCEEDED(target->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f), &brush))) {
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        target->BeginDraw();
        target->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f));
        target->DrawText(glyph, static_cast<UINT32>(wcslen(glyph)), format,
                         D2D1::RectF(0.0f, 0.0f, static_cast<float>(pixels),
                                     static_cast<float>(pixels)),
                         brush, D2D1_DRAW_TEXT_OPTIONS_NO_SNAP);
        drawn = SUCCEEDED(target->EndDraw());
    }
    if (brush) brush->Release();
    if (format) format->Release();
    if (target) target->Release();
    SelectObject(dc, previous);
    DeleteDC(dc);
    if (!drawn) {
        DeleteObject(bitmap);
        return nullptr;
    }

    DWORD ink = (static_cast<DWORD>(GetRValue(color)) << 16) |
                (static_cast<DWORD>(GetGValue(color)) << 8) | GetBValue(color);
    auto pixelsOut = static_cast<DWORD*>(bits);
    for (size_t i = 0; i < static_cast<size_t>(pixels) * pixels; ++i) {
        DWORD p = pixelsOut[i];
        DWORD coverage = std::max({(p >> 16) & 255u, (p >> 8) & 255u, p & 255u});
        pixelsOut[i] = (coverage << 24) | ink;
    }
    std::vector<BYTE> maskBits(((pixels + 15) / 16) * 2 * pixels, 0);
    HBITMAP mask = CreateBitmap(pixels, pixels, 1, 1, maskBits.data());
    HICON icon = nullptr;
    if (mask) {
        ICONINFO iconInfo{};
        iconInfo.fIcon = TRUE;
        iconInfo.hbmColor = bitmap;
        iconInfo.hbmMask = mask;
        icon = CreateIconIndirect(&iconInfo);
        DeleteObject(mask);
    }
    DeleteObject(bitmap);
    return icon;
}

// The tray icon shows the volume level like the Windows volume icon does:
// muted, silent, low, medium or high.
HICON CreateTrayIcon(float volume, bool muted) {
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    UINT trayDpi = taskbar ? GetDpiForWindow(taskbar) : 96;
    if (!trayDpi) trayDpi = 96;
    int pixels = std::max(16, GetSystemMetricsForDpi(SM_CXSMICON, trayDpi));
    DWORD light = 0, size = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
    COLORREF color = light ? RGB(30, 32, 36) : RGB(248, 249, 252);
    if (HICON icon = CreateGlyphIcon(pixels, VolumeGlyph(volume, muted), color)) {
        return icon;
    }
    return LoadMixerIcon(pixels, true, muted);
}

void SetTrayIcon(HWND hWnd, HICON tray) {
    if (!tray) return;
    if (g_hWnd.load() == hWnd) {
        NOTIFYICONDATAW data = {};
        data.cbSize = sizeof(data);
        data.hWnd = hWnd;
        data.uID = TRAY_ICON_ID;
        if (g_trayUsesGuid) data.guidItem = MIXER_TRAY_GUID;
        data.uFlags = NIF_ICON | (g_trayUsesGuid ? NIF_GUID : 0);
        data.hIcon = tray;
        if (!Shell_NotifyIconW(NIM_MODIFY, &data)) {
            Wh_Log(L"Mixer: tray icon update failed");
        }
    }
    if (g_trayIcon) DestroyIcon(g_trayIcon);
    g_trayIcon = tray;
}

void RefreshIconSizes(HWND hWnd) {
    bool muted = GetMasterMuted();
    float volume = GetMasterVolume();
    SetTrayIcon(hWnd, CreateTrayIcon(volume, muted));
    g_trayIconGlyph = VolumeGlyph(volume, muted);
    HICON master = LoadMixerIcon(Scale(24), false, muted);
    if (master) {
        if (g_masterIcon) {
            DestroyIcon(g_masterIcon);
        }
        g_masterIcon = master;
    }
    g_masterIconMuted = muted;
    for (auto& app : *g_apps) {
        if (app.icon) {
            DestroyIcon(app.icon);
        }
        app.icon = GetExecutableIcon(app.executablePath);
    }
}

// Called on every change of master volume, so it redraws only what changed:
// the tray icon when the volume crosses into another level, the master row
// icon when mute changes.
void RefreshMasterIcons(HWND hWnd, bool force) {
    bool muted = GetMasterMuted();
    float volume = GetMasterVolume();
    PCWSTR glyph = VolumeGlyph(volume, muted);
    if (force || !g_trayIconGlyph || wcscmp(glyph, g_trayIconGlyph) != 0) {
        SetTrayIcon(hWnd, CreateTrayIcon(volume, muted));
        g_trayIconGlyph = glyph;
    }
    if (!force && muted == g_masterIconMuted) {
        return;
    }

    HICON master = LoadMixerIcon(Scale(24), false, muted);
    if (master) {
        if (g_masterIcon) DestroyIcon(g_masterIcon);
        g_masterIcon = master;
    }
    g_masterIconMuted = muted;
}

std::wstring ProcessNameFromPath(const std::wstring& path) {
    size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? path : path.substr(separator + 1);
}

bool CopyProcessName(HWND owner, const std::wstring& name) {
    if (name.empty()) return false;
    SIZE_T bytes = (name.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) return false;

    auto* text = static_cast<wchar_t*>(GlobalLock(memory));
    if (!text) {
        GlobalFree(memory);
        return false;
    }

    std::wmemcpy(text, name.c_str(), name.size() + 1);
    GlobalUnlock(memory);

    if (!OpenClipboard(owner)) {
        GlobalFree(memory);
        return false;
    }

    bool copied = EmptyClipboard() &&
                  SetClipboardData(CF_UNICODETEXT, memory);
    CloseClipboard();

    if (!copied) GlobalFree(memory);
    return copied;
}

void RefreshSourceList(HWND window) {
    CancelNameTooltip(window);
    g_hoverRow = DRAG_NONE;
    RefreshAudioSessions(window);
    g_rowVisuals.clear();
    ResizeMixer(window);
    if (IsWindowVisible(window)) PositionMixer(window);
    InvalidateRect(window, nullptr, FALSE);
}

// Per-app output devices. Windows keeps a default output per app through an
// undocumented WinRT factory, the same one behind the per-app output picker of
// the Windows volume mixer. Its functions are loaded from combase.dll at run
// time, so the mod needs no extra libraries.
struct HStringOpaque;
using HStr = HStringOpaque*;

struct IAudioPolicyConfigFactory : public IUnknown {
    // IInspectable.
    virtual HRESULT STDMETHODCALLTYPE GetIids(ULONG*, IID**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HStr*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTrustLevel(int*) = 0;
    // Methods this mod does not call, declared to keep the vtable layout.
    virtual HRESULT STDMETHODCALLTYPE Unused01() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused02() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused03() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused04() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused05() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused06() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused07() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused08() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused09() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused10() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused11() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused12() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused13() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused14() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused15() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused16() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused17() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused18() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused19() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPersistedDefaultAudioEndpoint(
        UINT processId, EDataFlow flow, ERole role, HStr deviceId) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPersistedDefaultAudioEndpoint(
        UINT processId, EDataFlow flow, ERole role, HStr* deviceId) = 0;
    virtual HRESULT STDMETHODCALLTYPE ClearAllPersistedApplicationDefaultEndpoints() = 0;
};

struct WinRtApi {
    using GetActivationFactory_t = HRESULT(WINAPI*)(HStr, REFIID, void**);
    using CreateString_t = HRESULT(WINAPI*)(PCWSTR, UINT32, HStr*);
    using DeleteString_t = HRESULT(WINAPI*)(HStr);
    using RawBuffer_t = PCWSTR(WINAPI*)(HStr, UINT32*);
    GetActivationFactory_t getActivationFactory = nullptr;
    CreateString_t createString = nullptr;
    DeleteString_t deleteString = nullptr;
    RawBuffer_t rawBuffer = nullptr;
    bool loaded = false;
};

const WinRtApi& GetWinRtApi() {
    static const WinRtApi api = [] {
        WinRtApi result;
        HMODULE combase =
            LoadLibraryExW(L"combase.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!combase) return result;
        result.getActivationFactory = reinterpret_cast<WinRtApi::GetActivationFactory_t>(
            reinterpret_cast<void*>(GetProcAddress(combase, "RoGetActivationFactory")));
        result.createString = reinterpret_cast<WinRtApi::CreateString_t>(
            reinterpret_cast<void*>(GetProcAddress(combase, "WindowsCreateString")));
        result.deleteString = reinterpret_cast<WinRtApi::DeleteString_t>(
            reinterpret_cast<void*>(GetProcAddress(combase, "WindowsDeleteString")));
        result.rawBuffer = reinterpret_cast<WinRtApi::RawBuffer_t>(
            reinterpret_cast<void*>(GetProcAddress(combase, "WindowsGetStringRawBuffer")));
        result.loaded = result.getActivationFactory && result.createString &&
                        result.deleteString && result.rawBuffer;
        return result;
    }();
    return api;
}

// The caller releases the factory. Null when Windows does not offer it.
IAudioPolicyConfigFactory* CreateAudioPolicyConfig() {
    const WinRtApi& api = GetWinRtApi();
    if (!api.loaded) return nullptr;
    static constexpr wchar_t kClassName[] = L"Windows.Media.Internal.AudioPolicyConfig";
    HStr className = nullptr;
    if (FAILED(api.createString(kClassName, ARRAYSIZE(kClassName) - 1, &className))) {
        return nullptr;
    }
    // Windows 10 21H2 and later first, then the interface of earlier builds.
    // Both have the same method layout.
    static const IID kInterfaces[] = {
        {0xab3d4648, 0xe242, 0x459f, {0xb0, 0x2f, 0x54, 0x1c, 0x70, 0x30, 0x63, 0x24}},
        {0x2a59116d, 0x6c4f, 0x45e0, {0xa7, 0x4f, 0x70, 0x7e, 0x3f, 0xef, 0x92, 0x58}},
    };
    IAudioPolicyConfigFactory* factory = nullptr;
    for (const IID& iid : kInterfaces) {
        if (SUCCEEDED(api.getActivationFactory(className, iid,
                                               reinterpret_cast<void**>(&factory))) &&
            factory) {
            break;
        }
        factory = nullptr;
    }
    api.deleteString(className);
    if (!factory) Wh_Log(L"Mixer: per-app output routing is not available");
    return factory;
}

void ReleaseAudioPolicyConfig(IAudioPolicyConfigFactory* config) {
    if (config) config->Release();
}

// The factory names playback endpoints by their device interface path rather
// than by the Core Audio endpoint id.
constexpr wchar_t kMmDevApiPrefix[] = L"\\\\?\\SWD#MMDEVAPI#";
constexpr wchar_t kRenderInterfaceSuffix[] = L"#{e6327cad-dcec-4949-ae8a-991e976a79d2}";

std::wstring EndpointIdFromInterfacePath(const std::wstring& path) {
    size_t start = path.find(L"MMDEVAPI#");
    if (start == std::wstring::npos) return path;
    start += 9;
    size_t end = path.find(L'#', start);
    return path.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
}

// The output an app is routed to, or empty while it follows the Windows default.
std::wstring GetAppOutput(IAudioPolicyConfigFactory* config, DWORD processId) {
    const WinRtApi& api = GetWinRtApi();
    HStr value = nullptr;
    if (FAILED(config->GetPersistedDefaultAudioEndpoint(processId, eRender, eMultimedia,
                                                        &value)) || !value) {
        return {};
    }
    UINT32 length = 0;
    PCWSTR raw = api.rawBuffer(value, &length);
    std::wstring path = raw ? std::wstring(raw, length) : std::wstring();
    api.deleteString(value);
    return path.empty() ? path : EndpointIdFromInterfacePath(path);
}

// Routes every process of an app to an output, or back to the Windows default
// when `endpointId` is empty. Windows remembers the choice for the app.
HRESULT SetAppOutput(IAudioPolicyConfigFactory* config, const std::vector<DWORD>& processIds,
                     const std::wstring& endpointId) {
    const WinRtApi& api = GetWinRtApi();
    HStr value = nullptr;  // An empty string clears the routing.
    if (!endpointId.empty()) {
        std::wstring path = kMmDevApiPrefix + endpointId + kRenderInterfaceSuffix;
        HRESULT created =
            api.createString(path.c_str(), static_cast<UINT32>(path.size()), &value);
        if (FAILED(created)) return created;
    }
    HRESULT result = S_OK;
    for (DWORD processId : processIds) {
        // Communications too, so calling apps follow the choice.
        for (ERole role : {eConsole, eMultimedia, eCommunications}) {
            HRESULT set = config->SetPersistedDefaultAudioEndpoint(processId, eRender, role, value);
            if (FAILED(set)) {
                Wh_Log(L"Mixer: per-app output failed for PID %lu: 0x%08lX", processId,
                       static_cast<unsigned long>(set));
                result = set;
            }
        }
    }
    if (value) api.deleteString(value);
    return result;
}

void ShowAppOutputPicker(HWND window, int row);
void ReleaseAppOutput(HWND window, int row);

void ShowSourceMenu(HWND window, int row, POINT screenPoint);

void OpenClassicSound(HWND owner) {
    wchar_t directory[MAX_PATH];
    UINT length = GetSystemDirectoryW(directory, ARRAYSIZE(directory));
    if (!length || length >= ARRAYSIZE(directory)) {
        Wh_Log(L"Mixer: could not locate the Windows system directory");
        return;
    }

    std::wstring control =
        std::wstring(directory, length) + L"\\control.exe";
    HINSTANCE result = ShellExecuteW(
        owner, L"open", control.c_str(), L"/name Microsoft.Sound",
        nullptr, SW_SHOWNORMAL);

    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        Wh_Log(L"Mixer: could not open the classic Sound control panel");
        MessageBoxW(owner,
                    L"Windows could not open the Sound control panel.",
                    L"Headroom",
                    MB_OK | MB_ICONWARNING);
    }
}

void OpenSystemPage(PCWSTR uri) {
    ShellExecuteW(nullptr, L"open", uri, nullptr, nullptr, SW_SHOWNORMAL);
}

// Default-output switching uses the undocumented Windows shell policy interface.
static const CLSID kCLSID_PolicyConfigClient = {
    0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};
static const IID kIID_IPolicyConfig = {
    0xf8679f50, 0x850a, 0x41cf, {0x9c, 0x72, 0x43, 0x0f, 0x29, 0x02, 0x90, 0xc8}};

struct DeviceShareModeOpaque;

// The undocumented interface used by the sound applet to change the default
// endpoint. Only SetDefaultEndpoint is called, the other methods are declared
// just to keep the vtable layout correct.
struct IPolicyConfig : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, DeviceShareModeOpaque*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, DeviceShareModeOpaque*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR deviceId, ERole role) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};

struct OutputDevice {
    std::wstring id;
    std::wstring name;
    PCWSTR glyph;
    bool disabled = false;  // Shown dimmed and cannot be chosen.
};

// The icon for the kind of device: headphones, a display, or speakers.
PCWSTR ReadEndpointGlyph(IMMDevice* device) {
    PCWSTR glyph = L"\uE7F5";
    IPropertyStore* store = nullptr;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &store)) || !store) return glyph;
    PROPVARIANT value;
    PropVariantInit(&value);
    if (SUCCEEDED(store->GetValue(kEndpointFormFactor, &value)) && value.vt == VT_UI4) {
        if (value.ulVal == Headphones || value.ulVal == Headset) {
            glyph = L"\uE7F6";
        } else if (value.ulVal == DigitalAudioDisplayDevice) {
            glyph = L"\uE7F4";  // Over HDMI or DisplayPort.
        }
    }
    PropVariantClear(&value);
    store->Release();
    return glyph;
}

// Every process of the apps in the mixer.
std::vector<DWORD> AllAppProcessIds() {
    std::vector<DWORD> processIds;
    auto add = [&](DWORD id) {
        if (id && std::find(processIds.begin(), processIds.end(), id) == processIds.end()) {
            processIds.push_back(id);
        }
    };
    for (const AppSession& app : *g_apps) {
        if (app.IsDevice()) continue;
        for (DWORD id : app.processIds) add(id);
        if (app.processIds.empty()) add(app.processId);
    }
    return processIds;
}

// Lets apps routed to `outputId` follow the default output again. Apps routed
// to any other output keep their choice.
void ReleaseAppsFromOutput(const std::wstring& outputId) {
    if (outputId.empty()) return;
    IAudioPolicyConfigFactory* config = CreateAudioPolicyConfig();
    if (!config) return;
    std::vector<DWORD> released;
    for (DWORD processId : AllAppProcessIds()) {
        std::wstring routed = HeardOutput(config, processId, GetProcessPath(processId));
        if (!routed.empty() && _wcsicmp(routed.c_str(), outputId.c_str()) == 0) {
            released.push_back(processId);
        }
    }
    if (!released.empty()) RouteApps(config, released, L"");
    config->Release();
}

std::wstring DefaultOutputId() {
    std::wstring id;
    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                                __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&enumerator)))) {
        return id;
    }
    IMMDevice* device = nullptr;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device)) && device) {
        LPWSTR raw = nullptr;
        if (SUCCEEDED(device->GetId(&raw)) && raw) {
            id = raw;
            CoTaskMemFree(raw);
        }
        device->Release();
    }
    enumerator->Release();
    return id;
}

HRESULT SetPlaybackOutput(const std::wstring& id) {
    // Apps playing on the old default go along to the new one, even where they
    // were routed to it on their own.
    std::wstring previous = DefaultOutputId();
    bool changing = !previous.empty() && _wcsicmp(previous.c_str(), id.c_str()) != 0;

    IPolicyConfig* policy = nullptr;
    HRESULT result = CoCreateInstance(kCLSID_PolicyConfigClient, nullptr, CLSCTX_INPROC_SERVER,
                                      kIID_IPolicyConfig, reinterpret_cast<void**>(&policy));
    if (SUCCEEDED(result)) {
        // Like Windows Settings: ordinary playback, and unless turned off in
        // the settings, the communications output too.
        result = policy->SetDefaultEndpoint(id.c_str(), eMultimedia);
        // Only now that the switch worked are the apps released from the old
        // output; releasing them before it could leave them unrouted with the
        // default unchanged if it failed.
        if (SUCCEEDED(result) && changing) ReleaseAppsFromOutput(previous);
        if (SUCCEEDED(result)) result = policy->SetDefaultEndpoint(id.c_str(), eConsole);
        if (SUCCEEDED(result) && g_settings.switchCommunicationsDevice) {
            result = policy->SetDefaultEndpoint(id.c_str(), eCommunications);
        }
        policy->Release();
    }
    return result;
}

// The list of outputs under the picker, drawn like the mixer. Its window never
// takes the focus, so the mixer stays active and keeps the keyboard.
struct OutputList {
    HWND window = nullptr;
    const std::vector<OutputDevice>* outputs = nullptr;
    PCWSTR message = nullptr;  // Shown in place of an empty list.
    int current = -1;          // The default output.
    int hover = -1;            // Under the pointer.
    int focus = -1;            // Chosen with the arrow keys.
    int scroll = 0;            // The first output shown.
    int visible = 1;           // How many fit.
    int itemHeight = 0;        // Zero for the usual height.
    bool tracking = false;
};
OutputList g_outputList;
HWND g_panelWindow = nullptr;  // The equalizer / effects panel, which shares the list's window class.
int g_listDepth = 0;           // Lists open inside each other (a menu over a list).

// A list window that has outlived its list is an empty box on the screen. Once the outermost list is
// closed, no list window may remain except the panel's.
BOOL CALLBACK SweepStaleListWindow(HWND hwnd, LPARAM) {
    WCHAR name[64] = {};
    GetClassNameW(hwnd, name, ARRAYSIZE(name));
    if (wcscmp(name, L"HeadroomOutputs") == 0 && hwnd != g_panelWindow &&
        hwnd != g_outputList.window) {
        Wh_Log(L"Mixer: removed a stale list window");
        DestroyWindow(hwnd);
    }
    return TRUE;
}
PixelBuffer g_outputListBlack, g_outputListWhite;

int OutputItemHeight() {
    if (g_outputList.itemHeight) return g_outputList.itemHeight;
    return Scale(IsCompact() ? 36 : 40);
}

// Rows of the context menu, which uses the same list.
int MenuItemHeight() {
    return Scale(IsCompact() ? 32 : 34);
}

int OutputListPadding() {
    return Scale(4);
}

int OutputCount() {
    return static_cast<int>(g_outputList.outputs->size());
}

RECT OutputItemRect(int index, int width) {
    int top = OutputListPadding() + (index - g_outputList.scroll) * OutputItemHeight();
    return {OutputListPadding(), top, width - OutputListPadding(), top + OutputItemHeight()};
}

RECT OutputNameRect(int index, int width) {
    RECT item = OutputItemRect(index, width);
    return {item.left + Scale(34), item.top, item.right - Scale(12), item.bottom};
}

int OutputItemAt(POINT point, int width) {
    int padding = OutputListPadding();
    if (point.x < padding || point.x >= width - padding || point.y < padding) return -1;
    int index = g_outputList.scroll + (point.y - padding) / OutputItemHeight();
    if (index >= g_outputList.scroll + g_outputList.visible || index >= OutputCount()) {
        return -1;
    }
    return index;
}

// The icons and names line up with those of the picker above.
void DrawOutputList(HDC dc, const RECT& client) {
    if (g_paintMatte >= 0) {
        FillSolidRect(dc, client, g_paintMatte ? RGB(255, 255, 255) : RGB(0, 0, 0));
        DrawSurface(dc, client, Radius(20), g_theme.background);
    } else {
        FillSolidRect(dc, client, g_theme.background);
    }
    if (g_outputList.outputs->empty()) {
        RECT text = OutputItemRect(0, client.right);
        text.left += Scale(34);
        DrawLabel(dc, g_outputList.message, text, g_bodyFont, g_theme.secondaryText,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }
    int last = std::min(OutputCount(), g_outputList.scroll + g_outputList.visible);
    for (int i = g_outputList.scroll; i < last; ++i) {
        const OutputDevice& output = (*g_outputList.outputs)[i];
        RECT item = OutputItemRect(i, client.right);
        bool current = i == g_outputList.current;
        bool lit = !output.disabled &&
                   (i == g_outputList.hover || i == g_outputList.focus);
        if (lit || current) {
            DrawSurface(dc, item, Radius(8), g_theme.background, lit ? 1.0f : 0.6f);
        }
        if (current) {
            int middle = (item.top + item.bottom) / 2;
            RECT bar{item.left, middle - Scale(8), item.left + Scale(3), middle + Scale(8)};
            DrawRoundedRect(dc, bar, Radius(3), g_theme.accent);
        }
        RECT icon{item.left + Scale(4), item.top, item.left + Scale(30), item.bottom};
        DrawLabel(dc, output.glyph, icon, g_symbolFont,
                  output.disabled ? g_theme.secondaryText : IconColor(),
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DrawLabel(dc, output.name, OutputNameRect(i, client.right), g_bodyFont,
                  output.disabled ? g_theme.secondaryText : g_theme.text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

void PaintOutputList() {
    RECT rect;
    if (!g_outputList.window || !GetWindowRect(g_outputList.window, &rect)) return;
    PaintLayeredPopup(g_outputList.window, g_outputListBlack, g_outputListWhite,
                      rect.right - rect.left, rect.bottom - rect.top, DrawOutputList);
}

// A name cut short by the list shows in full in the mixer's name tooltip,
// after the same delay.
void UpdateOutputTooltip() {
    HWND list = g_outputList.window;
    CancelNameTooltip(list);
    int index = g_outputList.hover;
    if (index < 0) return;
    RECT client;
    GetClientRect(list, &client);
    const std::wstring& name = (*g_outputList.outputs)[index].name;
    if (!NameIsClipped(list, name, OutputNameRect(index, client.right))) return;
    g_nameTooltipText = name;
    g_nameTooltipClipped = true;
    SetTimer(list, TIMER_NAME_TOOLTIP, NAME_TOOLTIP_DELAY, nullptr);
}

LRESULT CALLBACK OutputListProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_TIMER && wParam == TIMER_NAME_TOOLTIP) {
        KillTimer(window, TIMER_NAME_TOOLTIP);
        POINT cursor{};
        if (g_nameTooltipClipped && GetCursorPos(&cursor) &&
            EnsureNameTooltip(GetWindow(window, GW_OWNER))) {
            PositionNameTooltip(cursor);
        }
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void SetOutputHover(int index) {
    if (g_outputList.hover == index) return;
    g_outputList.hover = index;
    UpdateOutputTooltip();
    PaintOutputList();
}

void ScrollOutputList(int first) {
    first = ClampInt(first, 0, std::max(0, OutputCount() - g_outputList.visible));
    if (first == g_outputList.scroll) return;
    g_outputList.scroll = first;
    POINT cursor{};
    GetCursorPos(&cursor);
    ScreenToClient(g_outputList.window, &cursor);
    RECT client;
    GetClientRect(g_outputList.window, &client);
    g_outputList.hover = OutputItemAt(cursor, client.right);
    UpdateOutputTooltip();
    PaintOutputList();
}

void FocusOutput(int index) {
    if (g_outputList.outputs->empty()) return;
    index = ClampInt(index, 0, OutputCount() - 1);
    g_outputList.focus = index;
    g_outputList.hover = -1;
    UpdateOutputTooltip();
    if (index < g_outputList.scroll) {
        g_outputList.scroll = index;
    } else if (index >= g_outputList.scroll + g_outputList.visible) {
        g_outputList.scroll = index - g_outputList.visible + 1;
    }
    PaintOutputList();
}

// Runs the list the way a menu runs, until an output is chosen (its index is
// returned) or the list is dismissed (-1).
// With `menuAt`, a screen point, the list opens there like a context menu.
bool AskHideOutput(HWND owner, POINT screenPoint);

int RunOutputList(HWND owner, const std::vector<OutputDevice>& outputs, int current,
                  PCWSTR message, const RECT* anchor = nullptr,
                  const POINT* menuAt = nullptr) {
    g_outputListHideRequest = -1;
    struct DepthGuard {
        DepthGuard() { ++g_listDepth; }
        ~DepthGuard() { --g_listDepth; }
    } depthGuard;
    RECT client;
    GetClientRect(owner, &client);
    // As wide as the picker, under the picker or under the row it was opened for.
    RECT pickerClient = OutputPickerRect(client.right);
    if (anchor) {
        pickerClient.top = anchor->top;
        pickerClient.bottom = anchor->bottom;
    }
    RECT picker = pickerClient;
    MapWindowPoints(owner, nullptr, reinterpret_cast<POINT*>(&picker), 2);
    if (menuAt) {
        // A zero-height strip at the pointer; clicks on the mixer pass through.
        pickerClient = {};
        // As wide as the longest entry needs, so no name is cut off.
        HDC measure = GetDC(nullptr);
        int widest = 0;
        for (const OutputDevice& item : outputs) widest = std::max(widest, MeasureLabel(measure, item.name, g_bodyFont));
        ReleaseDC(nullptr, measure);
        int menuWidth = std::max(g_outputListMenuWidth ? g_outputListMenuWidth : Scale(230), widest + Scale(62));
        picker = {menuAt->x, menuAt->y, menuAt->x + menuWidth, menuAt->y};
    }
    MONITORINFO info = {sizeof(info)};
    GetMonitorInfoW(MonitorFromRect(&picker, MONITOR_DEFAULTTONEAREST), &info);
    if (menuAt && picker.right > info.rcWork.right) {
        OffsetRect(&picker, info.rcWork.right - picker.right, 0);
    }
    if (menuAt && picker.left < info.rcWork.left) {
        OffsetRect(&picker, info.rcWork.left - picker.left, 0);
    }

    // Below the picker, or above it when there is more room there.
    int gap = menuAt ? 0 : Scale(4), padding = OutputListPadding();
    int itemHeight = menuAt ? MenuItemHeight() : Scale(IsCompact() ? 36 : 40);
    int count = std::max<int>(1, static_cast<int>(outputs.size()));
    int below = info.rcWork.bottom - picker.bottom - gap;
    int above = picker.top - gap - info.rcWork.top;
    bool down = below >= 2 * padding + count * itemHeight || below >= above;
    int visible = ClampInt(((down ? below : above) - 2 * padding) / itemHeight, 1, count);
    int width = picker.right - picker.left;
    int height = 2 * padding + visible * itemHeight;
    int y = down ? picker.bottom + gap : picker.top - gap - height;

    g_outputList = {};
    g_outputList.outputs = &outputs;
    g_outputList.message = message;
    g_outputList.current = current;
    g_outputList.visible = visible;
    g_outputList.itemHeight = itemHeight;
    if (current >= visible) g_outputList.scroll = current - visible + 1;
    g_outputList.window = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        OUTPUT_LIST_CLASS, L"", WS_POPUP, picker.left, y, width, height, owner, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (!g_outputList.window) {
        Wh_Log(L"Mixer: output list unavailable, error %lu", GetLastError());
        g_outputList = {};
        return -1;
    }
    HWND list = g_outputList.window;
    ApplyWindowFrame(list);
    ApplyBackdropBlur(list);
    PaintOutputList();
    ShowWindow(list, SW_SHOWNOACTIVATE);

    int chosen = -1;
    bool done = false;
    RECT ownerAt{};
    GetWindowRect(owner, &ownerAt);
    MSG msg;
    while (!done && (g_outputListDetached || (IsWindowVisible(owner) && !g_closeTime)) &&
           GetForegroundWindow() == owner) {
        // The mixer moves when it grows or shrinks, as apps start or stop
        // playing. The list goes with the picker.
        RECT ownerNow{};
        if (GetWindowRect(owner, &ownerNow) &&
            (ownerNow.left != ownerAt.left || ownerNow.top != ownerAt.top)) {
            RECT listRect{};
            GetWindowRect(list, &listRect);
            SetWindowPos(list, nullptr, listRect.left + ownerNow.left - ownerAt.left,
                         listRect.top + ownerNow.top - ownerAt.top, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            ownerAt = ownerNow;
        }
        BOOL result = GetMessageW(&msg, nullptr, 0, 0);
        if (result <= 0) {
            if (result == 0) PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }
        POINT point{GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam)};
        switch (msg.message) {
            case WM_MOUSEMOVE:
                if (msg.hwnd != list) break;
                if (!g_outputList.tracking) {
                    TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, list, 0};
                    g_outputList.tracking = TrackMouseEvent(&track) != FALSE;
                }
                g_outputList.focus = -1;
                SetOutputHover(OutputItemAt(point, width));
                continue;
            case WM_MOUSELEAVE:
                if (msg.hwnd != list) break;
                g_outputList.tracking = false;
                SetOutputHover(-1);
                continue;
            case WM_LBUTTONDOWN:
            case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN:
            case WM_MBUTTONDOWN:
                if (msg.hwnd == list) continue;  // An output is chosen on release.
                // A click anywhere else closes the list. One on the picker
                // does only that; any other click on the mixer goes on to it.
                done = true;
                if (msg.hwnd == owner) {
                    if (!PtInRect(&pickerClient, point)) {
                        PostMessageW(owner, msg.message, msg.wParam, msg.lParam);
                    }
                }
                continue;
            case WM_LBUTTONUP:
                if (msg.hwnd != list) break;
                chosen = OutputItemAt(point, width);
                if (chosen >= 0 && outputs[chosen].disabled) chosen = -1;
                done = chosen >= 0;
                continue;
            case WM_RBUTTONUP:
                if (msg.hwnd != list) break;
                // Ctrl+right-click offers to hide an output, in a small menu.
                if (g_outputListAllowHide && (GetKeyState(VK_CONTROL) & 0x8000)) {
                    int item = OutputItemAt(point, width);
                    if (item >= 0 && !outputs[item].id.empty()) {
                        POINT screen = point;
                        ClientToScreen(list, &screen);
                        if (AskHideOutput(owner, screen)) {
                            g_outputListHideRequest = item;
                            done = true;
                        }
                    }
                }
                continue;
            case WM_MOUSEWHEEL:
                ScrollOutputList(g_outputList.scroll -
                                 GET_WHEEL_DELTA_WPARAM(msg.wParam) / WHEEL_DELTA);
                continue;
            case WM_SYSKEYDOWN:
                done = true;
                continue;
            case WM_KEYDOWN: {
                int at = g_outputList.focus >= 0 ? g_outputList.focus
                    : g_outputList.hover >= 0 ? g_outputList.hover : -1;
                switch (msg.wParam) {
                    case VK_UP:
                        FocusOutput(at >= 0 ? at - 1 : current >= 0 ? current : OutputCount() - 1);
                        break;
                    case VK_DOWN:
                        FocusOutput(at >= 0 ? at + 1 : current >= 0 ? current : 0);
                        break;
                    case VK_HOME:
                        FocusOutput(0);
                        break;
                    case VK_END:
                        FocusOutput(OutputCount() - 1);
                        break;
                    case VK_RETURN:
                    case VK_SPACE:
                        if (at >= 0 && outputs[at].disabled) break;
                        chosen = at;
                        done = true;
                        break;
                    case VK_ESCAPE:
                    case VK_TAB:
                        done = true;
                        break;
                }
                continue;
            }
            case WM_CHAR:
            case WM_SYSCHAR:
                continue;
        }
        TranslateMessage(&msg);
        try {
            DispatchMessageW(&msg);
        } catch (...) {
            Wh_Log(L"Mixer: exception in a window procedure (output list)");
        }
    }
    CancelNameTooltip(list);
    DestroyWindow(list);
    g_outputList = {};
    g_outputListBlack.Reset();
    g_outputListWhite.Reset();
    if (g_listDepth == 1) EnumThreadWindows(GetCurrentThreadId(), SweepStaleListWindow, 0);
    return chosen;
}

// The one-entry menu that asks whether to hide an output. It runs over the
// list it was opened from, which is kept as it was.
bool AskHideOutput(HWND owner, POINT screenPoint) {
    OutputList saved = g_outputList;
    bool allowed = g_outputListAllowHide;
    g_outputListAllowHide = false;
    std::vector<OutputDevice> items;
    items.push_back({L"", L"Hide this device", L"\uED1A", false});
    int chosen = RunOutputList(owner, items, -1, L"", nullptr, &screenPoint);
    g_outputList = saved;
    g_outputListAllowHide = allowed;
    PaintOutputList();
    return chosen == 0;
}

// Active playback outputs sorted by name, and the id of the default one.
bool OutputHiddenFromLists(const std::wstring& id) {
    std::wstring key = L"device:" + id;
    LoadPermanentlyHiddenSources();
    return g_permanentlyHiddenSources.count(key) || g_temporarilyHiddenSources.count(key);
}

int HiddenOutputCount() {
    LoadPermanentlyHiddenSources();
    int count = 0;
    for (const auto& key : g_permanentlyHiddenSources) count += key.rfind(L"device:", 0) == 0;
    for (const auto& key : g_temporarilyHiddenSources) count += key.rfind(L"device:", 0) == 0;
    return count;
}

// Brings every hidden output back (the tray menu's "Restore hidden devices").
void RestoreHiddenOutputs() {
    LoadPermanentlyHiddenSources();
    for (auto* set : {&g_permanentlyHiddenSources, &g_temporarilyHiddenSources}) {
        for (auto it = set->begin(); it != set->end();) {
            it = it->rfind(L"device:", 0) == 0 ? set->erase(it) : std::next(it);
        }
    }
    SavePermanentlyHiddenSources();
}

// Ctrl+right-click hid an output from the list: remembers it. True when the
// list should open again.
bool HandleOutputListExtras(const std::vector<OutputDevice>& outputs, int selected) {
    (void)selected;
    int hide = g_outputListHideRequest;
    g_outputListHideRequest = -1;
    if (hide >= 0 && hide < static_cast<int>(outputs.size())) {
        LoadPermanentlyHiddenSources();
        g_permanentlyHiddenSources.insert(L"device:" + outputs[hide].id);
        SavePermanentlyHiddenSources();
        return true;
    }
    return false;
}

// The tray icon's menu. It is the mixer's own list, so it looks like the rest
// of the app, even while the mixer is closed.
void ShowTrayMenu(HWND hWnd, int x, int y) {
    if (g_sourceMenuOpen || g_outputMenuOpen) return;

    std::vector<OutputDevice> items;
    std::vector<UINT> commands;
    auto add = [&](UINT command, PCWSTR text, PCWSTR glyph) {
        items.push_back({L"", text, glyph, false});
        commands.push_back(command);
    };
    static std::wstring outputsLabel;
    add(MENU_REFRESH, L"Refresh applications", L"\uE72C");
    if (!g_temporarilyHiddenSources.empty()) {
        add(MENU_RESTORE_HIDDEN, L"Restore temporarily hidden sources", L"\uE890");
    }
    LoadPermanentlyHiddenSources();
    int hiddenOutputs = HiddenOutputCount();
    if (g_permanentlyHiddenSources.size() > static_cast<size_t>(hiddenOutputs)) {
        add(MENU_RESTORE_PERMANENT, L"Restore permanently hidden sources", L"\uE890");
    }
    if (hiddenOutputs > 0) {
        outputsLabel = L"Restore hidden devices (" + std::to_wstring(hiddenOutputs) + L")";
        add(MENU_RESTORE_OUTPUTS, outputsLabel.c_str(), L"\uE890");
    }
    add(MENU_SOUND_SETTINGS, L"Sound settings", L"\uE713");
    add(MENU_WINDOWS_MIXER, L"Windows volume mixer", L"\uE767");
    add(MENU_CLASSIC_SOUND, L"Sound control panel", L"\uE7F5");
#ifdef HEADROOM_STANDALONE
    AppExtendTrayMenu(items, commands);
#endif

    SetForegroundWindow(hWnd);
    // The menu opens beside the taskbar, not over it: the click is moved to the edge of the work area.
    MONITORINFO monitor = {sizeof(monitor)};
    POINT click{x, y};
    if (GetMonitorInfoW(MonitorFromPoint(click, MONITOR_DEFAULTTONEAREST), &monitor)) {
        int margin = Scale(8);
        if (y >= monitor.rcWork.bottom) y = monitor.rcWork.bottom - margin;
        else if (y < monitor.rcWork.top) y = monitor.rcWork.top + margin;
        if (x >= monitor.rcWork.right) x = monitor.rcWork.right - margin;
        else if (x < monitor.rcWork.left) x = monitor.rcWork.left + margin;
    }
    POINT at{x, y};
    g_outputListDetached = true;
    g_outputListMenuWidth = Scale(300);
    int chosen = RunOutputList(hWnd, items, -1, L"", nullptr, &at);
    g_outputListDetached = false;
    g_outputListMenuWidth = 0;
    UINT command = chosen >= 0 ? commands[chosen] : 0;

    PostMessageW(hWnd, WM_NULL, 0, 0);

    if (g_pendingDefaultOutputChange) {
        g_pendingDefaultOutputChange = false;
        PostMessageW(hWnd, WM_APP_DEFAULT_OUTPUT_CHANGED, 0, 0);
    }

#ifdef HEADROOM_STANDALONE
    if (AppHandleTrayCommand(hWnd, command)) return;
#endif
    switch (command) {
        case MENU_OPEN:
            QueueShowMixer(hWnd);
            break;

        case MENU_REFRESH:
            RefreshAudioSessions(hWnd, true);
            if (IsWindowVisible(hWnd)) {
                ResizeMixer(hWnd);
                PositionMixer(hWnd);
            }
            break;

        case MENU_SOUND_SETTINGS:
            OpenSystemPage(L"ms-settings:sound");
            break;

        case MENU_CLASSIC_SOUND:
            OpenClassicSound(hWnd);
            break;

        case MENU_WINDOWS_MIXER:
            OpenSystemPage(L"ms-settings:apps-volume");
            break;

        case MENU_RESTORE_HIDDEN:
            g_temporarilyHiddenSources.clear();
            RefreshSourceList(hWnd);
            break;

        case MENU_RESTORE_OUTPUTS:
            RestoreHiddenOutputs();
            RefreshSourceList(hWnd);
            break;

        case MENU_RESTORE_PERMANENT:
            g_permanentlyHiddenSources.clear();
            if (!SavePermanentlyHiddenSources()) {
                Wh_Log(L"Mixer: could not clear permanently hidden sources");
            }
            RefreshSourceList(hWnd);
            break;
    }
}


HRESULT LoadOutputDevices(std::vector<OutputDevice>& outputs, std::wstring& currentId) {
    IMMDeviceEnumerator* enumerator = nullptr;
    HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
        CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(&enumerator));
    if (SUCCEEDED(result)) {
        IMMDevice* current = nullptr;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(g_flow, eMultimedia, &current))) {
            LPWSTR id = nullptr;
            if (SUCCEEDED(current->GetId(&id)) && id) {
                currentId = id; CoTaskMemFree(id);
            }
            current->Release();
        }
        IMMDeviceCollection* collection = nullptr;
        result = enumerator->EnumAudioEndpoints(g_flow, DEVICE_STATE_ACTIVE, &collection);
        if (SUCCEEDED(result)) {
            UINT count = 0; collection->GetCount(&count);
            for (UINT i = 0; i < count; ++i) {
                IMMDevice* device = nullptr;
                if (FAILED(collection->Item(i, &device))) continue;
                LPWSTR id = nullptr;
                if (SUCCEEDED(device->GetId(&id)) && id) {
                    std::wstring name = ReadEndpointName(device);
                    // Hidden outputs stay listed while they are the default.
                    if ((g_flow == eCapture || !IsFxSinkName(name)) &&
                        (!OutputHiddenFromLists(id) || currentId == id)) {
                        outputs.push_back({id, name, ReadEndpointGlyph(device)});
                    }
                    CoTaskMemFree(id);
                }
                device->Release();
            }
            collection->Release();
        }
        enumerator->Release();
    }
    std::sort(outputs.begin(), outputs.end(), [](const auto& a, const auto& b) {
        int order = _wcsicmp(a.name.c_str(), b.name.c_str());
        return order ? order < 0 : a.id < b.id;
    });
    return result;
}

// The row's place in the mixer, in client coordinates.
RECT RowRect(int row, int clientWidth) {
    int top = RowTop(VisibleRowForDataRow(row));
    return {0, top, clientWidth, top + RowHeight()};
}

// `anchor`, in client coordinates, places the list under a row instead of
// under the picker.
void RefreshSourceList(HWND window);

void ShowOutputPicker(HWND window, const RECT* anchor = nullptr) {
    CancelNameTooltip(window);
    if (g_outputMenuOpen || g_dragRow != DRAG_NONE) return;
    g_outputMenuOpen = true;
    g_outputListAnchored = anchor != nullptr;
    KillTimer(window, TIMER_CHECK_FOCUS);
    std::vector<OutputDevice> outputs;
    std::wstring currentId;
    HRESULT result = LoadOutputDevices(outputs, currentId);
    int current = -1;
    for (size_t i = 0; i < outputs.size(); ++i) {
        if (outputs[i].id == currentId) current = static_cast<int>(i);
    }
    SetForegroundWindow(window);
    InvalidateRect(window, nullptr, FALSE);  // The picker's arrow turns up.
    g_outputListAllowHide = true;
    int selected = RunOutputList(window, outputs, current,
        FAILED(result) ? L"Could not load audio outputs" : L"No available audio outputs",
        anchor);
    g_outputListAllowHide = false;
    g_outputListAnchored = false;
    bool reopen = HandleOutputListExtras(outputs, selected);
    if (reopen) selected = -1;
    if (!IsWindow(window)) {
        g_outputMenuOpen = false;
        return;
    }
    if (selected >= 0 && selected != current) {
        result = SetPlaybackOutput(outputs[selected].id);
        if (FAILED(result)) {
            Wh_Log(L"Mixer: output switch failed: 0x%08lX", static_cast<unsigned long>(result));
            MessageBoxW(window, L"Windows could not fully switch the audio output. The device may have disconnected."
                        L"\n\nYou can also select an output in Windows Sound settings.",
                        L"Headroom", MB_OK | MB_ICONWARNING);
        }
        SelectRow(DRAG_MASTER);
        RefreshAudioSessions(window, true);
        g_scrollRow = 0;
        g_rowVisuals.clear();
        PositionMixer(window);
    }
    g_outputMenuOpen = false;
    if (g_pendingDefaultOutputChange) {
        g_pendingDefaultOutputChange = false;
        PostMessageW(window, WM_APP_DEFAULT_OUTPUT_CHANGED, 0, 0);
    }
    InvalidateRect(window, nullptr, FALSE);
    if (reopen && IsWindowVisible(window)) {
        RefreshSourceList(window);
        ShowOutputPicker(window, anchor);
        return;
    }
    if (g_settings.closeWhenFocusIsLost && IsWindowVisible(window))
        SetTimer(window, TIMER_CHECK_FOCUS, 150, nullptr);
}

// ---------------------------------------------------------------------------
// Equalizer per output, through Equalizer APO. The mixer keeps the bands and
// writes them as Headroom.txt into Equalizer APO's config folder;
// Equalizer APO, which runs inside the Windows audio service, applies them.

// Graphic equalizers as on hardware: 5 bands two octaves apart, 10 an octave
// apart, 15 at 2/3 and 31 at 1/3 octave (ISO frequencies). The finer the
// bands, the narrower each one is, so bass and treble can be shaped closer.
constexpr int EQ_MAX_BANDS = 31;
struct EqLayout {
    int count;
    double q;  // Bandwidth: the bands meet about 3 dB down.
    float frequencies[EQ_MAX_BANDS];
    bool tone = false;  // Bass and treble shelves around a mid band, as on a radio.
};
const EqLayout EQ_LAYOUTS[] = {
    {3, 0.71, {100, 1000, 8000}, true},
    {5, 0.67, {60, 230, 910, 3600, 14000}},
    {10, 1.41, {31.5f, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000}},
    {15, 2.15, {25, 40, 63, 100, 160, 250, 400, 630, 1000, 1600, 2500, 4000, 6300, 10000,
                16000}},
    {31, 4.32, {20, 25, 31.5f, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630,
                800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500,
                16000, 20000}},
};
constexpr int EQ_TONE_BANDS = 3;
constexpr int EQ_DEFAULT_BANDS = EQ_TONE_BANDS;
constexpr int EQ_HARD_MAX = 600;  // Tenths of a decibel: the most a saved band can hold, +-60 dB.

// How far the faders go: +-12 dB, or what the settings allow.
int EqLimit() {
    return g_settings.eqRangeCustom ? ClampInt(g_settings.eqRangeDb * 10, 120, EQ_HARD_MAX) : 120;
}

const EqLayout& EqLayoutFor(int count) {
    for (const auto& layout : EQ_LAYOUTS) {
        if (layout.count == count) return layout;
    }
    return EQ_LAYOUTS[2];  // 10 bands.
}
constexpr PCWSTR EQ_INDEX_VALUE = L"eqDevices_v1";
constexpr PCWSTR EQ_FILE = L"Headroom.txt";

struct EqualizerState {
    bool on = false;
    int bands = EQ_DEFAULT_BANDS;
    int gains[EQ_MAX_BANDS] = {};  // Tenths of a decibel.
};

// Carries a curve over to another number of bands, reading the old curve
// between its bands on a logarithmic frequency scale.
EqualizerState ResampleEqualizer(const EqualizerState& from, int count) {
    EqualizerState to;
    to.on = from.on;
    const EqLayout& source = EqLayoutFor(from.bands);
    const EqLayout& target = EqLayoutFor(count);
    to.bands = target.count;
    for (int i = 0; i < target.count; ++i) {
        double at = std::log(target.frequencies[i]);
        double gain = from.gains[0];
        if (at >= std::log(source.frequencies[source.count - 1])) {
            gain = from.gains[source.count - 1];
        } else {
            for (int j = 0; j + 1 < source.count; ++j) {
                double low = std::log(source.frequencies[j]);
                double high = std::log(source.frequencies[j + 1]);
                if (at >= low && at <= high) {
                    double t = (at - low) / (high - low);
                    gain = from.gains[j] + (from.gains[j + 1] - from.gains[j]) * t;
                    break;
                }
            }
        }
        int rounded = static_cast<int>(std::lround(gain / 5.0)) * 5;  // 0.5 dB steps.
        to.gains[i] = ClampInt(rounded, -EQ_HARD_MAX, EQ_HARD_MAX);
    }
    return to;
}

// Equalizer APO tells outputs apart by the GUID at the end of the endpoint id.
std::wstring EndpointGuid(const std::wstring& id) {
    size_t dot = id.rfind(L'.');
    return dot == std::wstring::npos ? id : id.substr(dot + 1);
}

// Stored as "on;bands;gain,gain,...", or "on;gain,..." for 10 bands.
EqualizerState LoadEqualizerValue(const std::wstring& valueName) {
    EqualizerState state;
    wchar_t buffer[512]{};
    Wh_GetStringValue(valueName.c_str(), buffer, ARRAYSIZE(buffer));
    buffer[ARRAYSIZE(buffer) - 1] = L'\0';
    if (!buffer[0]) return state;
    state.on = buffer[0] == L'1';
    const wchar_t* list = wcschr(buffer, L';');
    if (!list) return state;
    ++list;
    state.bands = 10;  // The first format had no count: always 10 bands.
    if (const wchar_t* second = wcschr(list, L';')) {
        state.bands = EqLayoutFor(static_cast<int>(wcstol(list, nullptr, 10))).count;
        list = second + 1;
    }
    for (int band = 0; band < state.bands && list && *list; ++band) {
        state.gains[band] =
            ClampInt(static_cast<int>(wcstol(list, nullptr, 10)), -EQ_HARD_MAX, EQ_HARD_MAX);
        list = wcschr(list, L',');
        if (list) ++list;
    }
    return state;
}

// On, with at least one band moved.
bool EqualizerActive(const std::wstring& valueName) {
    EqualizerState state = LoadEqualizerValue(valueName);
    if (!state.on) return false;
    for (int band = 0; band < state.bands; ++band) {
        if (state.gains[band]) return true;
    }
    return false;
}

EqualizerState LoadEqualizer(const std::wstring& guid) {
    return LoadEqualizerValue(L"eq_v1:" + guid);
}

std::wstring EqualizerValueText(const EqualizerState& state) {
    std::wstring value = (state.on ? L"1;" : L"0;") + std::to_wstring(state.bands);
    for (int band = 0; band < state.bands; ++band) {
        value += (band == 0 ? L";" : L",") + std::to_wstring(state.gains[band]);
    }
    return value;
}

// Presets. The built-in curves are given for 10 bands and carried over to
// the band count in use; custom presets keep the band count they were saved
// with. Custom ones are kept in the mod's storage as "name|bands|gains" lines.
struct EqPreset {
    std::wstring name;
    EqualizerState curve;
    bool custom = false;
};

struct BuiltinPreset {
    PCWSTR name;
    int gains[10];  // 31.5 Hz to 16 kHz, tenths of a decibel.
};
const BuiltinPreset EQ_BUILTIN_PRESETS[] = {
    {L"Flat", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {L"Bass boost", {60, 55, 45, 25, 5, 0, 0, 0, 0, 0}},
    {L"Bass reducer", {-60, -55, -45, -25, -5, 0, 0, 0, 0, 0}},
    {L"Treble boost", {0, 0, 0, 0, 0, 5, 20, 40, 55, 60}},
    {L"Treble reducer", {0, 0, 0, 0, 0, -5, -20, -40, -55, -60}},
    {L"Loudness", {50, 40, 20, 0, -15, -15, 0, 20, 40, 50}},
    {L"Vocal", {-30, -25, -15, 0, 20, 35, 35, 20, 0, -10}},
    {L"Speech", {-60, -40, -10, 10, 20, 30, 30, 10, -10, -30}},
    {L"Footsteps", {-30, -30, -20, -10, 0, 10, 35, 50, 30, 0}},
    {L"Small speakers", {-60, -30, 20, 30, 10, 0, 0, 10, 20, 10}},
};
constexpr PCWSTR EQ_PRESETS_VALUE = L"eqPresets_v1";

std::vector<EqPreset> LoadEqPresets() {
    std::vector<EqPreset> presets;
    for (const auto& builtin : EQ_BUILTIN_PRESETS) {
        EqPreset preset;
        preset.name = builtin.name;
        preset.curve.on = true;
        preset.curve.bands = 10;
        std::copy(builtin.gains, builtin.gains + 10, preset.curve.gains);
        presets.push_back(preset);
    }
    std::vector<wchar_t> buffer(16384);
    Wh_GetStringValue(EQ_PRESETS_VALUE, buffer.data(), buffer.size());
    buffer.back() = L'\0';
    std::wstring stored(buffer.data());
    size_t start = 0;
    while (start < stored.size()) {
        size_t end = stored.find(L'\n', start);
        if (end == std::wstring::npos) end = stored.size();
        std::wstring line = stored.substr(start, end - start);
        start = end + 1;
        size_t first = line.find(L'|'), second = line.find(L'|', first + 1);
        if (first == std::wstring::npos || second == std::wstring::npos || !first) continue;
        EqPreset preset;
        preset.custom = true;
        preset.name = line.substr(0, first);
        preset.curve.on = true;
        preset.curve.bands =
            EqLayoutFor(static_cast<int>(wcstol(line.c_str() + first + 1, nullptr, 10))).count;
        const wchar_t* list = line.c_str() + second + 1;
        for (int band = 0; band < preset.curve.bands && list && *list; ++band) {
            preset.curve.gains[band] =
                ClampInt(static_cast<int>(wcstol(list, nullptr, 10)), -EQ_HARD_MAX, EQ_HARD_MAX);
            list = wcschr(list, L',');
            if (list) ++list;
        }
        presets.push_back(preset);
    }
    return presets;
}

void SaveCustomEqPresets(const std::vector<EqPreset>& presets) {
    std::wstring stored;
    for (const auto& preset : presets) {
        if (!preset.custom) continue;
        stored += preset.name + L"|" + std::to_wstring(preset.curve.bands) + L"|";
        for (int band = 0; band < preset.curve.bands; ++band) {
            stored += (band ? L"," : L"") + std::to_wstring(preset.curve.gains[band]);
        }
        stored += L"\n";
    }
    if (stored.empty()) {
        Wh_DeleteValue(EQ_PRESETS_VALUE);
    } else {
        Wh_SetStringValue(EQ_PRESETS_VALUE, stored.c_str());
    }
}

// The preset the bands match, or -1.
int MatchingEqPreset(const std::vector<EqPreset>& presets, const EqualizerState& state) {
    for (size_t i = 0; i < presets.size(); ++i) {
        EqualizerState curve = ResampleEqualizer(presets[i].curve, state.bands);
        if (std::equal(curve.gains, curve.gains + state.bands, state.gains)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// An app's own equalizer, applied by the mixer's audio engine.
EqualizerState LoadAppEqualizer(const std::wstring& exe) {
    return LoadEqualizerValue(L"appEq_v1:" + exe);
}

std::vector<std::wstring> EqualizerDevices() {
    std::vector<std::wstring> devices;
    std::vector<wchar_t> buffer(8192);
    Wh_GetStringValue(EQ_INDEX_VALUE, buffer.data(), buffer.size());
    buffer.back() = L'\0';
    std::wstring stored(buffer.data());
    size_t start = 0;
    while (start < stored.size()) {
        size_t end = stored.find(L'\n', start);
        if (end == std::wstring::npos) end = stored.size();
        if (end > start) devices.push_back(stored.substr(start, end - start));
        start = end + 1;
    }
    return devices;
}

void AddEqualizerDevice(const std::wstring& guid);

void SaveEqualizer(const std::wstring& guid, const EqualizerState& state) {
    Wh_SetStringValue((L"eq_v1:" + guid).c_str(), EqualizerValueText(state).c_str());
    AddEqualizerDevice(guid);
}

void AddEqualizerDevice(const std::wstring& guid) {
    std::vector<std::wstring> devices = EqualizerDevices();
    if (std::find(devices.begin(), devices.end(), guid) == devices.end()) {
        std::wstring index;
        for (const auto& device : devices) index += device + L"\n";
        Wh_SetStringValue(EQ_INDEX_VALUE, (index + guid + L"\n").c_str());
    }
}

// Equalizer APO's config folder, or empty when it is not installed.
std::wstring EqualizerApoFolder() {
    wchar_t path[MAX_PATH]{};
    DWORD size = sizeof(path);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\EqualizerAPO", L"ConfigPath",
                     RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, path, &size) !=
            ERROR_SUCCESS ||
        !path[0]) {
        // Where the installer puts it by default.
        if (!ExpandEnvironmentStringsW(L"%ProgramW6432%\\EqualizerAPO\\config", path,
                                       ARRAYSIZE(path)) ||
            GetFileAttributesW((std::wstring(path) + L"\\config.txt").c_str()) ==
                INVALID_FILE_ATTRIBUTES) {
            return {};
        }
    }
    std::wstring folder = path;
    if (folder.back() != L'\\') folder += L'\\';
    return folder;
}

bool WriteTextFile(const std::wstring& path, const std::string& text) {
    std::wstring temporary = path + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
              written == text.size();
    CloseHandle(file);
    // Replaced in one step, so Equalizer APO never reads half a file.
    ok = ok && MoveFileExW(temporary.c_str(), path.c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) DeleteFileW(temporary.c_str());
    return ok;
}

// Makes Equalizer APO read the mixer's file. Its own config.txt, which by
// default holds example filters for every output, is kept as a backup.
bool EnsureEqualizerInclude(const std::wstring& folder) {
    std::wstring config = folder + L"config.txt";
    std::string content;
    HANDLE file = CreateFileW(config.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        char buffer[4096];
        DWORD read = 0;
        while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read) {
            content.append(buffer, read);
        }
        CloseHandle(file);
    }
    std::string lower = content;
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.find("headroom.txt") != std::string::npos) return true;
    // Earlier versions went by another name: their files are taken over, the backup of the original
    // configuration moves along, and the old filters are no longer read.
    if (lower.find("bettervolumemixer.txt") != std::string::npos) {
        MoveFileExW((folder + L"config.before-better-volume-mixer.txt").c_str(),
                    (folder + L"config.before-headroom.txt").c_str(), 0);
        DeleteFileW((folder + L"BetterVolumeMixer.txt").c_str());
        return WriteTextFile(config,
            "# Equalizer APO, managed by Headroom.\r\n"
            "# The configuration from before is in config.before-headroom.txt.\r\n"
            "Include: Headroom.txt\r\n");
    }
    if (!content.empty()) {
        CopyFileW(config.c_str(), (folder + L"config.before-headroom.txt").c_str(),
                  TRUE);
    }
    return WriteTextFile(config,
        "# Equalizer APO, managed by Headroom.\r\n"
        "# The configuration from before is in config.before-headroom.txt.\r\n"
        "Include: Headroom.txt\r\n");
}

// ---------------------------------------------------------------------------
// LoudMax, Thomas Mundt's free look-ahead limiter (a VST2 plugin), when it is
// in Equalizer APO's VSTPlugins folder. For an output, Equalizer APO runs it
// from a line in the mixer's file, as its own editor writes it. For an app,
// the mixer's audio engine runs it itself, so it can be on for single apps.

struct LoudMaxState {
    bool on = false;
    std::vector<std::pair<std::string, float>> params;  // By name, 0 to 1.
};

// Apps run it in the mixer, so it must match the mixer's bitness.
PCWSTR LoudMaxFileName() {
    return sizeof(void*) == 8 ? L"LoudMax64.dll" : L"LoudMax32.dll";
}

// Outputs run it in Windows' audio service, which is 64-bit on 64-bit Windows.
PCWSTR LoudMaxOutputFileName() {
    BOOL wow64 = FALSE;
    IsWow64Process(GetCurrentProcess(), &wow64);
    return sizeof(void*) == 8 || wow64 ? L"LoudMax64.dll" : L"LoudMax32.dll";
}

bool FileExists(const std::wstring& path) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

// The plugin in `folder` or up to `depth` folders below it.
std::wstring FindFileBelow(const std::wstring& folder, PCWSTR name, int depth) {
    if (folder.empty()) return {};
    std::wstring direct = folder + name;
    if (FileExists(direct)) return direct;
    if (depth <= 0) return {};
    WIN32_FIND_DATAW entry;
    HANDLE find = FindFirstFileW((folder + L"*").c_str(), &entry);
    if (find == INVALID_HANDLE_VALUE) return {};
    std::wstring found;
    do {
        if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || entry.cFileName[0] == L'.') {
            continue;
        }
        found = FindFileBelow(folder + entry.cFileName + L"\\", name, depth - 1);
    } while (found.empty() && FindNextFileW(find, &entry));
    FindClose(find);
    return found;
}

// The plugin named in Equalizer APO's own config.txt, when it is there.
std::wstring LoudMaxFromConfig(const std::wstring& configFolder, const std::wstring& base) {
    HANDLE file = CreateFileW((configFolder + L"config.txt").c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    std::string text;
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read) text.append(buffer, read);
    CloseHandle(file);
    std::string lower = text;
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    size_t at = lower.find("loudmax");
    if (at == std::string::npos) return {};
    size_t lineStart = lower.rfind('\n', at);
    lineStart = lineStart == std::string::npos ? 0 : lineStart + 1;
    size_t library = lower.find("library", lineStart);
    if (library == std::string::npos || library > at) return {};
    size_t start = text.find_first_not_of(" \t", library + 7);
    if (start == std::string::npos) return {};
    std::string path;
    if (text[start] == '"') {
        size_t end = text.find('"', start + 1);
        path = text.substr(start + 1, end == std::string::npos ? std::string::npos : end - start - 1);
    } else {
        size_t end = text.find_first_of(" \t\r\n", start);
        path = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
    }
    int length = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (length <= 1) return {};
    std::wstring wide(length - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), length);
    if (wide.size() > 2 && (wide[1] == L':' || wide[0] == L'\\')) return FileExists(wide) ? wide : L"";
    for (const std::wstring& folder : {base + L"VSTPlugins\\", configFolder, base}) {
        if (FileExists(folder + wide)) return folder + wide;
    }
    return {};
}

// Where a LoudMax DLL is, or empty. The setting wins (with the other bitness
// next to it); then the plugin Equalizer APO itself loads; then the usual
// folders. Looked up again every few seconds.
std::wstring FindLoudMax(PCWSTR fileName) {
    struct Cached {
        std::wstring path;
        ULONGLONG checked = 0;
    };
    static std::unordered_map<std::wstring, Cached> cache;
    Cached& entry = cache[fileName];
    ULONGLONG now = GetTickCount64();
    if (entry.checked && now - entry.checked < 5000) return entry.path;
    entry.checked = now;
    std::wstring previous = entry.path;
    entry.path.clear();
    if (!g_settings.loudMaxPath.empty()) {
        std::wstring set = g_settings.loudMaxPath;
        size_t slash = set.find_last_of(L'\\');
        std::wstring sibling = (slash == std::wstring::npos ? L"" : set.substr(0, slash + 1)) + fileName;
        if (_wcsicmp(set.substr(slash == std::wstring::npos ? 0 : slash + 1).c_str(), fileName) == 0 &&
            FileExists(set)) {
            entry.path = set;
        } else if (FileExists(sibling)) {
            entry.path = sibling;
        }
    } else {
        std::wstring base;
        std::wstring config = EqualizerApoFolder();  // ...\EqualizerAPO\config\.
        if (config.size() > 1) {
            std::wstring trimmed = config.substr(0, config.size() - 1);
            size_t slash = trimmed.find_last_of(L'\\');
            if (slash != std::wstring::npos) base = trimmed.substr(0, slash + 1);
        }
        wchar_t programs[MAX_PATH]{}, common[MAX_PATH]{};
        ExpandEnvironmentStringsW(L"%ProgramW6432%\\", programs, MAX_PATH);
        ExpandEnvironmentStringsW(L"%CommonProgramW6432%\\", common, MAX_PATH);
        if (base.empty()) base = std::wstring(programs) + L"EqualizerAPO\\";
        if (config.empty()) config = base + L"config\\";
        std::wstring named = LoudMaxFromConfig(config, base);
        size_t slash = named.find_last_of(L'\\');
        if (!named.empty() && _wcsicmp(named.c_str() + slash + 1, fileName) == 0) {
            entry.path = named;
        } else if (!named.empty()) {
            std::wstring sibling = named.substr(0, slash + 1) + fileName;
            if (FileExists(sibling)) entry.path = sibling;
        }
        std::wstring folders[] = {
            base + L"VSTPlugins\\", config, base,
            std::wstring(programs) + L"VSTPlugins\\",
            std::wstring(programs) + L"Steinberg\\VSTPlugins\\",
            std::wstring(common) + L"VST2\\",
            std::wstring(common) + L"Steinberg\\VST2\\",
        };
        for (const auto& folder : folders) {
            if (!entry.path.empty()) break;
            entry.path = FindFileBelow(folder, fileName, 2);
        }
    }
    if (entry.path != previous) {
        Wh_Log(L"Mixer: %s %s%s", fileName, entry.path.empty() ? L"not found" : L"found at ",
               entry.path.c_str());
    }
    return entry.path;
}

SRWLOCK g_loudMaxLoad = SRWLOCK_INIT;
std::wstring g_loudMaxShared;  // The apps' LoudMax, for the audio threads.

// The LoudMax apps use, in the mixer's own bitness.
std::wstring LoudMaxPath() {
    std::wstring path = FindLoudMax(LoudMaxFileName());
    AcquireSRWLockExclusive(&g_loudMaxLoad);
    if (g_loudMaxShared != path) g_loudMaxShared = path;
    ReleaseSRWLockExclusive(&g_loudMaxLoad);
    return path;
}

std::wstring LoudMaxOutputPath() {
    return FindLoudMax(LoudMaxOutputFileName());
}

bool LoudMaxForApps() {
    return !LoudMaxPath().empty();
}

bool LoudMaxForOutputs() {
    return !LoudMaxOutputPath().empty();
}

LoudMaxState LoadLoudMax(const std::wstring& key);

bool LoudMaxOnFor(const std::wstring& key) {
    return LoadLoudMax(key).on &&
           (key.rfind(L"dev:", 0) == 0 ? LoudMaxForOutputs() : LoudMaxForApps());
}

// Kept as "1|Thresh=0.32|Output=0.32|Fader Link=1|ISP Detection=0" under
// "lm_v1:app:<exe>" or "lm_v1:dev:<output GUID>".
LoudMaxState LoadLoudMax(const std::wstring& key) {
    LoudMaxState state;
    wchar_t buffer[512]{};
    Wh_GetStringValue((L"lm_v1:" + key).c_str(), buffer, ARRAYSIZE(buffer));
    buffer[ARRAYSIZE(buffer) - 1] = L'\0';
    std::wstring stored = buffer;
    if (stored.empty()) return state;
    state.on = stored[0] == L'1';
    size_t start = stored.find(L'|');
    while (start != std::wstring::npos) {
        size_t end = stored.find(L'|', start + 1);
        std::wstring item = stored.substr(start + 1, end == std::wstring::npos
                                                          ? std::wstring::npos
                                                          : end - start - 1);
        size_t equals = item.find(L'=');
        if (equals != std::wstring::npos && equals) {
            std::string name;
            for (wchar_t c : item.substr(0, equals)) name += static_cast<char>(c);
            float value = std::clamp(static_cast<float>(_wtof(item.c_str() + equals + 1)),
                                     0.0f, 1.0f);
            state.params.emplace_back(name, value);
        }
        start = end;
    }
    return state;
}

void SaveLoudMax(const std::wstring& key, const LoudMaxState& state) {
    std::wstring stored = state.on ? L"1" : L"0";
    for (const auto& [name, value] : state.params) {
        wchar_t number[32];
        swprintf_s(number, L"%g", value);
        stored += L"|" + std::wstring(name.begin(), name.end()) + L"=" + number;
    }
    Wh_SetStringValue((L"lm_v1:" + key).c_str(), stored.c_str());
}

// --- a minimal VST 2.4 host: just what running LoudMax needs.

struct VstEffect;
using VstHostCallback = intptr_t (*)(VstEffect*, int32_t, int32_t, intptr_t, void*, float);
struct VstEffect {
    int32_t magic;  // 'VstP'
    intptr_t (*dispatcher)(VstEffect*, int32_t, int32_t, intptr_t, void*, float);
    void (*process)(VstEffect*, float**, float**, int32_t);
    void (*setParameter)(VstEffect*, int32_t, float);
    float (*getParameter)(VstEffect*, int32_t);
    int32_t numPrograms, numParams, numInputs, numOutputs, flags;
    intptr_t reserved1, reserved2;
    int32_t initialDelay, realQualities, offQualities;
    float ioRatio;
    void* object;
    void* user;
    int32_t uniqueID, version;
    void (*processReplacing)(VstEffect*, float**, float**, int32_t);
    void (*processDoubleReplacing)(VstEffect*, double**, double**, int32_t);
    char future[56];
};
constexpr int32_t VST_MAGIC = 0x56737450;
enum VstOpcode : int32_t {
    VST_OPEN = 0, VST_CLOSE = 1, VST_GET_PARAM_NAME = 8, VST_SET_SAMPLE_RATE = 10,
    VST_SET_BLOCK_SIZE = 11, VST_MAINS_CHANGED = 12, VST_EDIT_GET_RECT = 13,
    VST_EDIT_OPEN = 14, VST_EDIT_CLOSE = 15, VST_EDIT_IDLE = 19, VST_START_PROCESS = 71,
    VST_STOP_PROCESS = 72,
};
struct VstRect {
    int16_t top, left, bottom, right;
};
constexpr int LOUDMAX_BLOCK = 1024;

intptr_t LoudMaxHostCallback(VstEffect*, int32_t opcode, int32_t, intptr_t, void* ptr, float) {
    switch (opcode) {
        case 1: return 2400;               // Host VST version.
        case 16: return 48000;             // Sample rate.
        case 17: return LOUDMAX_BLOCK;     // Block size.
        case 32:                           // Vendor string.
        case 33:                           // Product string.
            if (ptr) strcpy_s(static_cast<char*>(ptr), 64, "Headroom");
            return 1;
    }
    return 0;
}

// A new LoudMax, opened and running at `rate`; null when it cannot load.
VstEffect* CreateLoudMax(double rate) {
    static HMODULE module = nullptr;
    AcquireSRWLockExclusive(&g_loudMaxLoad);
    if (!module && !g_loudMaxShared.empty()) {
        module = LoadLibraryExW(g_loudMaxShared.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    }
    HMODULE loaded = module;
    ReleaseSRWLockExclusive(&g_loudMaxLoad);
    if (!loaded) return nullptr;
    using Entry = VstEffect* (*)(VstHostCallback);
    Entry entry = reinterpret_cast<Entry>(
        reinterpret_cast<void*>(GetProcAddress(loaded, "VSTPluginMain")));
    if (!entry) {
        entry = reinterpret_cast<Entry>(reinterpret_cast<void*>(GetProcAddress(loaded, "main")));
    }
    VstEffect* effect = entry ? entry(LoudMaxHostCallback) : nullptr;
    if (!effect || effect->magic != VST_MAGIC || !effect->processReplacing) return nullptr;
    effect->dispatcher(effect, VST_OPEN, 0, 0, nullptr, 0.0f);
    effect->dispatcher(effect, VST_SET_SAMPLE_RATE, 0, 0, nullptr, static_cast<float>(rate));
    effect->dispatcher(effect, VST_SET_BLOCK_SIZE, 0, LOUDMAX_BLOCK, nullptr, 0.0f);
    effect->dispatcher(effect, VST_MAINS_CHANGED, 0, 1, nullptr, 0.0f);
    effect->dispatcher(effect, VST_START_PROCESS, 0, 0, nullptr, 0.0f);
    return effect;
}

void CloseLoudMax(VstEffect* effect) {
    if (!effect) return;
    effect->dispatcher(effect, VST_STOP_PROCESS, 0, 0, nullptr, 0.0f);
    effect->dispatcher(effect, VST_MAINS_CHANGED, 0, 0, nullptr, 0.0f);
    effect->dispatcher(effect, VST_CLOSE, 0, 0, nullptr, 0.0f);  // The plugin frees itself.
}

std::vector<std::string> LoudMaxParamNames(VstEffect* effect) {
    std::vector<std::string> names;
    for (int32_t i = 0; i < effect->numParams && i < 64; ++i) {
        char name[256]{};
        effect->dispatcher(effect, VST_GET_PARAM_NAME, i, 0, name, 0.0f);
        names.push_back(name);
    }
    return names;
}

void ApplyLoudMaxParams(VstEffect* effect, const std::vector<std::string>& names,
                        const LoudMaxState& state) {
    for (const auto& [name, value] : state.params) {
        for (size_t i = 0; i < names.size(); ++i) {
            if (names[i] == name) effect->setParameter(effect, static_cast<int32_t>(i), value);
        }
    }
}

// One LoudMax in an app's stream.
struct LoudMaxInstance {
    VstEffect* effect = nullptr;
    std::vector<std::string> names;
    std::vector<float> left, right;

    bool Open(double rate) {
        effect = CreateLoudMax(rate);
        if (!effect) return false;
        names = LoudMaxParamNames(effect);
        left.resize(LOUDMAX_BLOCK);
        right.resize(LOUDMAX_BLOCK);
        return true;
    }

    void Close() {
        CloseLoudMax(effect);
        effect = nullptr;
    }

    // Interleaved stereo, in place.
    void Process(float* samples, size_t frames) {
        if (!effect) return;
        while (frames) {
            int32_t count = static_cast<int32_t>(std::min<size_t>(frames, LOUDMAX_BLOCK));
            for (int32_t i = 0; i < count; ++i) {
                left[i] = samples[i * 2];
                right[i] = samples[i * 2 + 1];
            }
            float* channels[2] = {left.data(), right.data()};
            effect->processReplacing(effect, channels, channels, count);
            for (int32_t i = 0; i < count; ++i) {
                samples[i * 2] = left[i];
                samples[i * 2 + 1] = right[i];
            }
            samples += count * 2;
            frames -= count;
        }
    }
};

// An output's equalizer and LoudMax run in Equalizer APO when it is installed
// (no extra delay), and in the mixer's own engine when it is not, or when the
// settings ask for that.
// The gain of an output: everything that plays on it is raised by up to 12 dB, for a device that is too quiet.
// A limiter (the mixer's own, or LoudMax) catches the peaks, which would crackle otherwise.
// Kept as "on;tenths of a decibel" under "devGain_v1:<output GUID>".
struct DeviceGain {
    bool on = false;
    int tenths = 0;  // 0 to DEVICE_GAIN_MAX.
};
constexpr int DEVICE_GAIN_MAX = 120;

bool DeviceGainActive(const DeviceGain& gain) {
    return gain.on && gain.tenths > 0;
}

DeviceGain LoadDeviceGain(const std::wstring& guid) {
    DeviceGain gain;
    wchar_t buffer[32]{};
    Wh_GetStringValue((L"devGain_v1:" + guid).c_str(), buffer, ARRAYSIZE(buffer));
    buffer[ARRAYSIZE(buffer) - 1] = L'\0';
    if (!buffer[0]) return gain;
    gain.on = buffer[0] == L'1';
    const wchar_t* semicolon = wcschr(buffer, L';');
    if (semicolon) gain.tenths = std::clamp(static_cast<int>(wcstol(semicolon + 1, nullptr, 10)), 0, DEVICE_GAIN_MAX);
    return gain;
}

void SaveDeviceGain(const std::wstring& guid, const DeviceGain& gain) {
    std::wstring stored = std::wstring(gain.on ? L"1;" : L"0;") + std::to_wstring(gain.tenths);
    Wh_SetStringValue((L"devGain_v1:" + guid).c_str(), stored.c_str());
}

// An output that is also played on a second one: "on;offset in ms;the other output's id" under
// "devMirror_v1:<output GUID>". The offset delays the second output when it is above 0 and the first one when it
// is below 0, so two outputs that are late by different amounts (a Bluetooth headset next to speakers) line up.
struct DeviceMirror {
    bool on = false;
    int offsetMs = 0;
    std::wstring target;
};
constexpr int MIRROR_DELAY_MAX = 1000;

bool DeviceMirrorActive(const DeviceMirror& mirror) {
    return mirror.on && !mirror.target.empty();
}

DeviceMirror LoadMirrorValue(const std::wstring& name) {
    DeviceMirror mirror;
    wchar_t buffer[256]{};
    Wh_GetStringValue(name.c_str(), buffer, ARRAYSIZE(buffer));
    buffer[ARRAYSIZE(buffer) - 1] = L'\0';
    if (!buffer[0]) return mirror;
    mirror.on = buffer[0] == L'1';
    const wchar_t* first = wcschr(buffer, L';');
    if (!first) return mirror;
    wchar_t* end = nullptr;
    mirror.offsetMs = std::clamp(static_cast<int>(wcstol(first + 1, &end, 10)), -MIRROR_DELAY_MAX, MIRROR_DELAY_MAX);
    const wchar_t* second = end ? wcschr(end, L';') : nullptr;
    if (second) mirror.target = second + 1;
    return mirror;
}

void SaveMirrorValue(const std::wstring& name, const DeviceMirror& mirror) {
    std::wstring stored = std::wstring(mirror.on ? L"1;" : L"0;") + std::to_wstring(mirror.offsetMs) + L";" + mirror.target;
    Wh_SetStringValue(name.c_str(), stored.c_str());
}

// Of an output ("devMirror_v1:<output GUID>"), or of an app or listened-to input ("appMirror_v1:<name>"): an app
// that has one plays on it, whatever the output it plays on does.
DeviceMirror LoadDeviceMirror(const std::wstring& guid) {
    return LoadMirrorValue(L"devMirror_v1:" + guid);
}
DeviceMirror LoadAppMirror(const std::wstring& exe) {
    return LoadMirrorValue(L"appMirror_v1:" + exe);
}

bool UseApoForDevices() {
    if (g_settings.deviceEffects == L"builtin") return false;
    return !EqualizerApoFolder().empty();
}
std::atomic<bool> g_deviceFxBuiltIn{false};
void PublishDeviceFx();

// Writes the bands of every output into the mixer's file.
bool WriteEqualizerConfig() {
    bool apo = UseApoForDevices();
    PublishDeviceFx();
    std::wstring folder = EqualizerApoFolder();
    if (folder.empty()) return !apo;
    // With the mixer's own engine, an earlier file of ours is emptied, so nothing is applied twice.
    if (!apo && GetFileAttributesW((folder + EQ_FILE).c_str()) == INVALID_FILE_ATTRIBUTES) return true;
    if (!EnsureEqualizerInclude(folder)) return false;
    std::string text = "# Written by Headroom. Changes here are overwritten.\r\n";
    bool loudMax = LoudMaxForOutputs();
    for (const std::wstring& guid : EqualizerDevices()) {
        if (!apo) break;
        EqualizerState state = LoadEqualizer(guid);
        LoudMaxState limiter = LoadLoudMax(L"dev:" + guid);
        bool limit = loudMax && limiter.on;
        DeviceGain boost = LoadDeviceGain(guid);
        bool boosted = DeviceGainActive(boost);
        if (!state.on && !limit && !boosted) continue;
        if (!state.on) state = EqualizerState();
        int highest = 0;
        for (int gain : state.gains) highest = std::max(highest, gain);
        char line[160];
        int length = WideCharToMultiByte(CP_UTF8, 0, guid.c_str(), -1, line, sizeof(line),
                                         nullptr, nullptr);
        if (length <= 1) continue;
        text += std::string("Device: ") + line + "\r\n";
        // Without a limiter after it, Equalizer APO would clip the boosts, so it
        // gets headroom; with LoudMax limiting, the bands simply add.
        snprintf(line, sizeof(line), "Preamp: %.1f dB\r\n", (boosted ? boost.tenths / 10.0 : 0.0) + (limit ? 0.0 : -highest / 10.0));
        text += line;
        const EqLayout& layout = EqLayoutFor(state.bands);
        for (int band = 0; band < layout.count; ++band) {
            if (!state.gains[band]) continue;
            PCSTR type = !layout.tone ? "PK" : band == 0 ? "LSC"
                       : band == layout.count - 1 ? "HSC" : "PK";
            double q = layout.tone && band != 0 && band != layout.count - 1 ? 0.7 : layout.q;
            snprintf(line, sizeof(line), "Filter: ON %s Fc %g Hz Gain %.1f dB Q %.2f\r\n", type,
                     layout.frequencies[band], state.gains[band] / 10.0, q);
            text += line;
        }
        if (limit) {
            // As Equalizer APO's editor writes it, from its VSTPlugins folder.
            std::string plugin = "VSTPlugin: Library \"";
            std::wstring path = LoudMaxOutputPath();
            int size = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string narrow(size > 1 ? size - 1 : 0, '\0');
            if (size > 1) {
                WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, narrow.data(), size, nullptr,
                                    nullptr);
            }
            plugin += narrow + "\"";
            for (const auto& [name, value] : limiter.params) {
                if (name == "Large GUI") continue;
                bool spaced = name.find(' ') != std::string::npos;
                snprintf(line, sizeof(line), " %s%s%s %g", spaced ? "\"" : "", name.c_str(),
                         spaced ? "\"" : "", value);
                plugin += line;
            }
            text += plugin + "\r\n";
        }
    }
    text += "Device: all\r\n";
    return WriteTextFile(folder + EQ_FILE, text);
}

// ---------------------------------------------------------------------------
// Per-app audio engine: boost above 100% and an equalizer per app, without
// loading anything into the app. The app is routed, with the same per-app
// output setting Windows Settings uses, to a silent virtual output
// (VB-CABLE). The mixer records just that app through Windows' application
// loopback capture, processes it, and plays it on the output the app should
// be heard on. If anything fails, the app is routed back at once.

constexpr UINT WM_APP_FX_UPDATE = WM_APP + 40;
constexpr UINT_PTR TIMER_FX = 40;
constexpr PCWSTR FX_FOLLOW_DEFAULT = L"-";

std::wstring g_fxSinkId;  // The silent output, while it is installed.
bool g_fxSinkMissingShown;
bool g_fxUpdatePending;

// VB-CABLE's playback side: "CABLE Input (VB-Audio Virtual Cable)" or, in
// newer packs, "CABLE In 16ch (VB-Audio Virtual Cable)".
bool IsFxSinkName(const std::wstring& name) {
    std::wstring lower = name;
    for (auto& c : lower) c = static_cast<wchar_t>(std::towlower(c));
    return lower.find(L"vb-audio virtual cable") != std::wstring::npos ||
           lower.rfind(L"cable in", 0) == 0;
}

std::wstring FindFxSink() {
    std::wstring sink;
    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                                __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&enumerator)))) {
        return sink;
    }
    IMMDeviceCollection* devices = nullptr;
    if (SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices))) {
        UINT count = 0;
        devices->GetCount(&count);
        for (UINT i = 0; i < count && sink.empty(); ++i) {
            IMMDevice* device = nullptr;
            if (FAILED(devices->Item(i, &device))) continue;
            LPWSTR id = nullptr;
            if (IsFxSinkName(ReadEndpointName(device)) && SUCCEEDED(device->GetId(&id)) && id) {
                sink = id;
            }
            if (id) CoTaskMemFree(id);
            device->Release();
        }
        devices->Release();
    }
    enumerator->Release();
    return sink;
}

bool IsFxSink(const std::wstring& id) {
    return !id.empty() && !g_fxSinkId.empty() && _wcsicmp(id.c_str(), g_fxSinkId.c_str()) == 0;
}

// Where an app's processed audio is played: an output id, "-" to follow the
// default output, or "" while the app has never been processed.
std::wstring FxTarget(const std::wstring& exe) {
    wchar_t buffer[512]{};
    Wh_GetStringValue((L"fxTarget_v1:" + exe).c_str(), buffer, ARRAYSIZE(buffer));
    buffer[ARRAYSIZE(buffer) - 1] = L'\0';
    return buffer;
}

void SetFxTarget(const std::wstring& exe, const std::wstring& target);

// Loudness equalization for one app, like the enhancement Windows offers per
// output: quiet passages are raised and loud ones lowered, slowly.
// What the expert mode of loudness equalization can set. The ready-made
// modes are these values too; "custom" keeps the user's own and never
// overwrites a ready-made one.
constexpr int LOUD_PARAMS = 8;
struct LoudParam {
    PCWSTR name;
    double low, high;
    bool logarithmic;
    int kind;  // 0 milliseconds, 1 decibels
};
constexpr LoudParam LOUD_PARAM[LOUD_PARAMS] = {
    {L"Level window", 2, 1000, true, 0},     // How long the level is averaged.
    {L"Reaction time", 1, 1000, true, 0},    // How fast it turns the app down.
    {L"Recovery time", 50, 10000, true, 0},  // How slowly it brings it back up.
    {L"Max. reduction", 0, 24, false, 1},
    {L"Max. boost", 0, 24, false, 1},
    {L"Target level", -30, -6, false, 1},    // RMS.
    {L"Look-ahead", 0, 20, false, 0},        // The sound waits, so the gain is ahead of it.
    {L"Silence gate", -70, -30, false, 1},   // Below this the gain holds.
};
constexpr double LOUD_ULTRA[LOUD_PARAMS] = {12, 8, 250, 12, 12, -18, 4, -50};
constexpr double LOUD_MODES[6][LOUD_PARAMS] = {
    {400, 100, 500, 6, 12, -18, 0, -50},   // 0 short
    {400, 100, 2000, 6, 12, -18, 0, -50},  // 1 medium
    {400, 100, 6000, 6, 12, -18, 0, -50},  // 2 long
    {12, 8, 250, 12, 12, -18, 4, -50},     // 3 ultra short
    {12, 8, 250, 12, 12, -18, 4, -50},     // 4 custom (its own values are used)
    // 5 competitive: quiet steps up to 15 dB louder, shots and explosions down by up to 18 dB almost at
    // once, back up within 180 ms, only 1.5 ms of look-ahead and a short output queue.
    {6, 2, 180, 18, 15, -20, 1.5, -60},
};
constexpr int LOUD_COMPETITIVE = 5;

struct LoudnessState {
    bool on = false;
    int release = 1;  // 0 short, 1 medium, 2 long, 3 ultra short, 4 custom, 5 competitive.
    double custom[LOUD_PARAMS] = {12, 8, 250, 12, 12, -18, 4, -50};
};

LoudnessState LoadLoudness(const std::wstring& exe) {
    LoudnessState state;
    wchar_t buffer[200]{};
    Wh_GetStringValue((L"loud_v1:" + exe).c_str(), buffer, ARRAYSIZE(buffer));
    buffer[ARRAYSIZE(buffer) - 1] = L'\0';
    if (!buffer[0]) return state;
    state.on = buffer[0] == L'1';
    if (const wchar_t* release = wcschr(buffer, L';')) {
        state.release = ClampInt(static_cast<int>(wcstol(release + 1, nullptr, 10)), 0, 5);
        // "on;mode;v,v,v,..." : the custom values follow the mode.
        const wchar_t* list = wcschr(release + 1, L';');
        for (int i = 0; list && i < LOUD_PARAMS; ++i) {
            ++list;
            wchar_t* end = nullptr;
            double value = wcstod(list, &end);
            if (end == list) break;
            state.custom[i] = std::clamp(value, LOUD_PARAM[i].low, LOUD_PARAM[i].high);
            list = end;
            if (*list != L',') break;
        }
    }
    return state;
}

void SaveLoudness(const std::wstring& exe, const LoudnessState& state) {
    std::wstring value = (state.on ? L"1;" : L"0;") + std::to_wstring(state.release);
    for (int i = 0; i < LOUD_PARAMS; ++i) {
        wchar_t number[32];
        swprintf_s(number, L"%g", state.custom[i]);
        value += (i == 0 ? L";" : L",") + std::wstring(number);
    }
    Wh_SetStringValue((L"loud_v1:" + exe).c_str(), value.c_str());
}

bool LoudnessOnFor(const std::wstring& exe) {
    return LoadLoudness(exe).on;
}

// Custom loudness settings the user saved under a name. They belong to no
// app, so a setting found for one game can be used for the next.
struct LoudPreset {
    std::wstring name;
    double values[LOUD_PARAMS] = {};
};
constexpr PCWSTR LOUD_PRESETS_VALUE = L"loudPresets_v1";

std::vector<LoudPreset> LoadLoudPresets() {
    std::vector<wchar_t> buffer(8192);
    Wh_GetStringValue(LOUD_PRESETS_VALUE, buffer.data(), buffer.size());
    buffer.back() = L'\0';
    std::wstring stored(buffer.data());
    std::vector<LoudPreset> presets;
    size_t start = 0;
    while (start < stored.size()) {
        size_t end = stored.find(L'\n', start);
        if (end == std::wstring::npos) end = stored.size();
        std::wstring line = stored.substr(start, end - start);
        start = end + 1;
        size_t tab = line.find(L'\t');
        if (tab == std::wstring::npos || tab == 0) continue;
        LoudPreset preset;
        preset.name = line.substr(0, tab);
        const wchar_t* list = line.c_str() + tab + 1;
        bool complete = true;
        for (int i = 0; i < LOUD_PARAMS; ++i) {
            wchar_t* stop = nullptr;
            double value = wcstod(list, &stop);
            if (stop == list) { complete = false; break; }
            preset.values[i] = std::clamp(value, LOUD_PARAM[i].low, LOUD_PARAM[i].high);
            list = stop;
            if (*list == L',') ++list;
        }
        if (complete) presets.push_back(std::move(preset));
    }
    return presets;
}

void SaveLoudPresets(const std::vector<LoudPreset>& presets) {
    std::wstring stored;
    for (const LoudPreset& preset : presets) {
        stored += preset.name + L"\t";
        for (int i = 0; i < LOUD_PARAMS; ++i) {
            wchar_t number[32];
            swprintf_s(number, L"%g", preset.values[i]);
            stored += (i ? L"," : L"") + std::wstring(number);
        }
        stored += L"\n";
    }
    Wh_SetStringValue(LOUD_PRESETS_VALUE, stored.c_str());
}

int MatchingLoudPreset(const std::vector<LoudPreset>& presets, const double* values) {
    for (size_t i = 0; i < presets.size(); ++i) {
        bool same = true;
        for (int j = 0; j < LOUD_PARAMS; ++j) {
            if (std::fabs(presets[i].values[j] - values[j]) > 1e-6) same = false;
        }
        if (same) return static_cast<int>(i);
    }
    return -1;
}

// The channels of an app that plays 5.1 or 7.1 sound: any of them can be switched off before the
// sound is mixed down to the two of the headphones or speakers. Front left, front right, center, bass,
// back left, back right, side left, side right.
constexpr int SURROUND_CHANNELS = 8;
struct ChannelMixState {
    bool on = false;
    int percent[SURROUND_CHANNELS] = {100, 100, 100, 100, 100, 100, 100, 100};  // 0 is off, up to 200.
};

// Kept as "on;p,p,p,p,p,p,p,p", p being the volume of each channel in percent.
ChannelMixState LoadChannelMix(const std::wstring& exe) {
    ChannelMixState state;
    wchar_t buffer[64]{};
    Wh_GetStringValue((L"chan_v2:" + exe).c_str(), buffer, ARRAYSIZE(buffer));
    buffer[ARRAYSIZE(buffer) - 1] = L'\0';
    if (!buffer[0]) return state;
    state.on = buffer[0] == L'1';
    const wchar_t* list = wcschr(buffer, L';');
    for (int i = 0; list && i < SURROUND_CHANNELS; ++i) {
        ++list;
        state.percent[i] = std::clamp(_wtoi(list), 0, 200);
        list = wcschr(list, L',');
    }
    return state;
}

// The apps whose channels are on, one executable name per line. The cable stays in 7.1 while there is
// one, so a game that starts later finds it that way and plays surround sound to it.
constexpr PCWSTR CHANNEL_INDEX_VALUE = L"chanIndex_v1";

std::vector<std::wstring> ChannelMixApps() {
    std::vector<wchar_t> buffer(4096);
    Wh_GetStringValue(CHANNEL_INDEX_VALUE, buffer.data(), buffer.size());
    buffer.back() = L'\0';
    std::vector<std::wstring> apps;
    std::wstring stored(buffer.data());
    size_t start = 0;
    while (start < stored.size()) {
        size_t end = stored.find(L'\n', start);
        if (end == std::wstring::npos) end = stored.size();
        if (end > start) apps.push_back(stored.substr(start, end - start));
        start = end + 1;
    }
    return apps;
}

void SaveChannelMix(const std::wstring& exe, const ChannelMixState& state) {
    std::wstring value = state.on ? L"1;" : L"0;";
    for (int i = 0; i < SURROUND_CHANNELS; ++i) value += std::wstring(i ? L"," : L"") + std::to_wstring(state.percent[i]);
    Wh_SetStringValue((L"chan_v2:" + exe).c_str(), value.c_str());
    std::vector<std::wstring> apps = ChannelMixApps();
    apps.erase(std::remove(apps.begin(), apps.end(), exe), apps.end());
    if (state.on) apps.push_back(exe);
    std::wstring index;
    for (const std::wstring& app : apps) index += app + L"\n";
    Wh_SetStringValue(CHANNEL_INDEX_VALUE, index.c_str());
}

bool ChannelMixOnFor(const std::wstring& exe) {
    return LoadChannelMix(exe).on;
}

// --- inputs: a line-in (or any input) can be listened to through the mixer, with an equalizer and effects of its own.
// The input is treated like an app called "in:<GUID>": its equalizer, loudness and LoudMax are kept under that name.
bool IsInputKey(const std::wstring& key) {
    return key.rfind(L"in:", 0) == 0;
}
std::wstring InputKey(const std::wstring& deviceId) {
    return L"in:" + EndpointGuid(deviceId);
}
std::wstring InputGuid(const std::wstring& key) {
    return key.substr(3);
}

// The inputs that are listened to, one GUID per line.
constexpr PCWSTR MONITOR_INDEX_VALUE = L"monitorIndex_v1";

std::vector<std::wstring> MonitorGuids() {
    std::vector<wchar_t> buffer(4096);
    Wh_GetStringValue(MONITOR_INDEX_VALUE, buffer.data(), buffer.size());
    buffer.back() = L'\0';
    std::vector<std::wstring> guids;
    std::wstring stored(buffer.data());
    size_t start = 0;
    while (start < stored.size()) {
        size_t end = stored.find(L'\n', start);
        if (end == std::wstring::npos) end = stored.size();
        if (end > start) guids.push_back(stored.substr(start, end - start));
        start = end + 1;
    }
    return guids;
}

bool MonitorOn(const std::wstring& guid) {
    std::vector<std::wstring> guids = MonitorGuids();
    return std::find(guids.begin(), guids.end(), guid) != guids.end();
}

void SetMonitorOn(const std::wstring& guid, bool on) {
    std::vector<std::wstring> guids = MonitorGuids();
    guids.erase(std::remove(guids.begin(), guids.end(), guid), guids.end());
    if (on) guids.push_back(guid);
    std::wstring stored;
    for (const std::wstring& g : guids) stored += g + L"\n";
    Wh_SetStringValue(MONITOR_INDEX_VALUE, stored.c_str());
}

// Windows' own "Listen to this device" for an input: while it is on, Windows plays the input as well.
bool ListenEnabled(const std::wstring& guid) {
    std::wstring path = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Capture\\" + guid + L"\\Properties";
    BYTE data[32]{};
    DWORD size = sizeof(data);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, path.c_str(), L"{24dbb0fc-9311-4b3d-9cf0-18ff155639d4},1", RRF_RT_REG_BINARY,
                     nullptr, data, &size) != ERROR_SUCCESS || size < 10) {
        return false;
    }
    return data[8] == 0xFF && data[9] == 0xFF;  // A property variant: VARIANT_TRUE.
}

// The full id of an active input with this GUID, or empty.
std::wstring CaptureDeviceId(const std::wstring& guid) {
    std::wstring found;
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDeviceCollection* devices = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                                   __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator))) &&
        SUCCEEDED(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &devices))) {
        UINT count = 0;
        devices->GetCount(&count);
        for (UINT i = 0; i < count && found.empty(); ++i) {
            IMMDevice* device = nullptr;
            LPWSTR id = nullptr;
            if (SUCCEEDED(devices->Item(i, &device)) && SUCCEEDED(device->GetId(&id)) && id) {
                if (EndpointGuid(id) == guid) found = id;
                CoTaskMemFree(id);
            }
            if (device) device->Release();
        }
    }
    if (devices) devices->Release();
    if (enumerator) enumerator->Release();
    return found;
}

// The volume and mute of a listened-to input, the mixer's own: 0 to 100 %.
float MonitorVolume(const std::wstring& guid) {
    return std::clamp(Wh_GetIntValue((L"monitorVol_v1:" + guid).c_str(), 100), 0, 100) / 100.0f;
}

bool MonitorMuted(const std::wstring& guid) {
    return Wh_GetIntValue((L"monitorMute_v1:" + guid).c_str(), 0) != 0;
}

void SetMonitorVolume(const std::wstring& key, float volume) {
    std::wstring guid = InputGuid(key);
    Wh_SetIntValue((L"monitorVol_v1:" + guid).c_str(), static_cast<int>(std::lround(std::clamp(volume, 0.0f, 1.0f) * 100.0f)));
    if (volume > 0.0f) Wh_SetIntValue((L"monitorMute_v1:" + guid).c_str(), 0);
    FxSettingsChanged(key);
}

void SetMonitorMuted(const std::wstring& key, bool muted) {
    Wh_SetIntValue((L"monitorMute_v1:" + InputGuid(key)).c_str(), muted ? 1 : 0);
    FxSettingsChanged(key);
}

// An input that is listened to shows up in the playback list like an app: with its volume, mute, output,
// equalizer and effects.
void AppendMonitorRows() {
    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                                __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator)))) {
        return;
    }
    for (const std::wstring& guid : MonitorGuids()) {
        std::wstring id = CaptureDeviceId(guid);
        IMMDevice* device = nullptr;
        bool connected = !id.empty() && SUCCEEDED(enumerator->GetDevice(id.c_str(), &device)) && device;
        // An input that is not connected (a console that is off can make the jack read as unplugged) stays in the
        // list, so it does not seem to vanish.
        if (!connected) Wh_Log(L"Mixer: the input %s that is listened to is not connected", guid.c_str());
        AppSession row;
        row.virtualInput = true;
        row.executablePath = L"in:" + guid;  // The name its settings are kept under.
        row.pinKey = L"input:" + guid;
        row.listed = true;
        row.active = true;
        row.pinned = Wh_GetIntValue(row.pinKey.c_str(), 0) != 0;
        const std::wstring& renamed = StoredSourceName(row.pinKey);
        row.name = renamed.empty() ? (connected ? ReadEndpointName(device) : std::wstring(L"Input")) : renamed;
        if (!connected) row.name += L" (not connected)";
        row.volume = MonitorVolume(guid);
        // "Volume at startup": set once per run, like an app's.
        static std::unordered_set<std::wstring> started;
        int startVolume = AppStartVolume(row.executablePath);
        if (startVolume >= 0 && started.insert(guid).second) {
            SetMonitorVolume(row.executablePath, startVolume / 100.0f);
            row.volume = startVolume / 100.0f;
        }
        row.muted = MonitorMuted(guid);
        if (device) device->Release();
        Wh_Log(L"Mixer: listened-to input row '%s', volume %.0f %%, %s", row.name.c_str(), row.volume * 100.0f, row.muted ? L"muted" : L"not muted");
        g_apps->push_back(std::move(row));
    }
    enumerator->Release();
}

struct FxSettings {
    float gain = 1.0f;
    EqualizerState eq;
    LoudMaxState loudMax;
    LoudnessState loudness;
    ChannelMixState channels;
    std::wstring target;  // Empty: the default output.
    DeviceMirror mirror;  // Also played on a second output.
};

bool FxNeeded(const FxSettings& settings) {
    if (settings.gain > 1.001f || settings.loudMax.on || settings.loudness.on || settings.channels.on ||
        DeviceMirrorActive(settings.mirror)) {
        return true;
    }
    if (!settings.eq.on) return false;
    for (int gain : settings.eq.gains) {
        if (gain) return true;
    }
    return false;
}

SRWLOCK g_fxLock = SRWLOCK_INIT;
std::unordered_map<std::wstring, FxSettings> g_fxSettings;  // By executable name.
std::atomic<unsigned> g_fxVersion{1};

FxSettings BuildFxSettings(const std::wstring& exe) {
    FxSettings settings;
    LoadBoost();
    auto boost = g_boost.find(exe);
    if (boost != g_boost.end()) settings.gain = boost->second / 100.0f;
    settings.eq = LoadAppEqualizer(exe);
    settings.loudMax = LoadLoudMax(L"app:" + exe);
    if (!LoudMaxForApps()) settings.loudMax.on = false;
    settings.loudness = LoadLoudness(exe);
    settings.channels = LoadChannelMix(exe);
    std::wstring target = FxTarget(exe);
    settings.target = target == FX_FOLLOW_DEFAULT ? std::wstring() : target;
    settings.mirror = LoadAppMirror(exe);
    if (IsInputKey(exe)) {
        DeviceGain boost = LoadDeviceGain(InputGuid(exe));
        float volume = MonitorMuted(InputGuid(exe)) ? 0.0f : MonitorVolume(InputGuid(exe));
        settings.gain *= (DeviceGainActive(boost) ? static_cast<float>(std::pow(10.0, boost.tenths / 200.0)) : 1.0f) * volume;
    }
    return settings;
}

// Hands an app's settings to its streams.
void PublishFxSettings(const std::wstring& exe) {
    FxSettings settings = BuildFxSettings(exe);
    AcquireSRWLockExclusive(&g_fxLock);
    g_fxSettings[exe] = settings;
    ReleaseSRWLockExclusive(&g_fxLock);
    g_fxVersion.fetch_add(1);
}

FxSettings ReadFxSettings(const std::wstring& exe) {
    FxSettings settings;
    AcquireSRWLockShared(&g_fxLock);
    auto found = g_fxSettings.find(exe);
    if (found != g_fxSettings.end()) settings = found->second;
    ReleaseSRWLockShared(&g_fxLock);
    return settings;
}

// The effects of an output, for streams that play on it (mixer's own engine).
struct DeviceFx {
    EqualizerState eq;
    LoudMaxState loudMax;
    DeviceGain gain;
};
std::unordered_map<std::wstring, DeviceFx> g_deviceFx;  // By output GUID, under g_fxLock.
std::unordered_map<std::wstring, DeviceMirror> g_deviceMirror;  // By output GUID, under g_fxLock.

bool DeviceFxActive(const DeviceFx& fx) {
    if (fx.loudMax.on || DeviceGainActive(fx.gain)) return true;
    if (!fx.eq.on) return false;
    for (int gain : fx.eq.gains) {
        if (gain) return true;
    }
    return false;
}

void PublishDeviceFx() {
    bool builtIn = !UseApoForDevices();
    g_deviceFxBuiltIn = builtIn;
    std::unordered_map<std::wstring, DeviceFx> next;
    std::unordered_map<std::wstring, DeviceMirror> nextMirror;
    for (const std::wstring& guid : EqualizerDevices()) {
        DeviceMirror mirror = LoadDeviceMirror(guid);
        if (DeviceMirrorActive(mirror)) nextMirror[guid] = std::move(mirror);
    }
    if (builtIn) {
        for (const std::wstring& guid : EqualizerDevices()) {
            DeviceFx fx;
            fx.eq = LoadEqualizer(guid);
            fx.loudMax = LoadLoudMax(L"dev:" + guid);
            fx.gain = LoadDeviceGain(guid);
            if (!LoudMaxForOutputs()) fx.loudMax.on = false;
            next[guid] = std::move(fx);
        }
    }
    AcquireSRWLockExclusive(&g_fxLock);
    g_deviceFx = std::move(next);
    g_deviceMirror = std::move(nextMirror);
    ReleaseSRWLockExclusive(&g_fxLock);
    g_fxVersion.fetch_add(1);
    HWND window = g_hWnd.load();
    if (window && !g_fxUpdatePending) {
        g_fxUpdatePending = true;
        PostMessageW(window, WM_APP_FX_UPDATE, 0, 0);
    }
}

DeviceFx ReadDeviceFx(const std::wstring& guid) {
    DeviceFx fx;
    AcquireSRWLockShared(&g_fxLock);
    auto found = g_deviceFx.find(guid);
    if (found != g_deviceFx.end()) fx = found->second;
    ReleaseSRWLockShared(&g_fxLock);
    return fx;
}

DeviceMirror ReadDeviceMirror(const std::wstring& guid) {
    DeviceMirror mirror;
    AcquireSRWLockShared(&g_fxLock);
    auto found = g_deviceMirror.find(guid);
    if (found != g_deviceMirror.end()) mirror = found->second;
    ReleaseSRWLockShared(&g_fxLock);
    return mirror;
}

bool MirrorNeededFor(const std::wstring& guid) {
    return DeviceMirrorActive(ReadDeviceMirror(guid));
}

bool DeviceFxNeededFor(const std::wstring& guid) {
    return g_deviceFxBuiltIn && DeviceFxActive(ReadDeviceFx(guid));
}

void SetFxTarget(const std::wstring& exe, const std::wstring& target) {
    Wh_SetStringValue((L"fxTarget_v1:" + exe).c_str(),
                      target.empty() ? FX_FOLLOW_DEFAULT : target.c_str());
    PublishFxSettings(exe);
}

std::unordered_map<std::wstring, bool> g_fxNeeded;  // By executable name.

// Running streams pick up new settings at once. The window looks for streams
// to start or stop only when an app starts or stops needing one.
void FxSettingsChanged(const std::wstring& exe) {
    if (!exe.empty()) {
        PublishFxSettings(exe);
        bool needed = FxNeeded(ReadFxSettings(exe));
        auto known = g_fxNeeded.find(exe);
        if (known != g_fxNeeded.end() && known->second == needed) return;
        g_fxNeeded[exe] = needed;
    }
    HWND window = g_hWnd.load();
    if (window && !g_fxUpdatePending) {
        g_fxUpdatePending = true;
        PostMessageW(window, WM_APP_FX_UPDATE, 0, 0);
    }
}

// An input was switched on or off: the streams are looked at again.
void MonitorsChanged() {
    HWND window = g_hWnd.load();
    if (window && !g_fxUpdatePending) {
        g_fxUpdatePending = true;
        PostMessageW(window, WM_APP_FX_UPDATE, 0, 0);
    }
}

// --- signal processing

struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1[2] = {}, z2[2] = {};
    bool active = false;
};

// A peaking filter from the Audio EQ Cookbook; the state is kept, so changing
// a band while it plays does not click.
void DesignPeak(Biquad& filter, double rate, double frequency, double gainDb, double q) {
    filter.active = gainDb != 0.0 && frequency < rate * 0.45;
    if (!filter.active) return;
    double a = std::pow(10.0, gainDb / 40.0);
    double w0 = 2.0 * 3.14159265358979323846 * frequency / rate;
    double alpha = std::sin(w0) / (2.0 * q);
    double cosine = std::cos(w0);
    double a0 = 1.0 + alpha / a;
    filter.b0 = static_cast<float>((1.0 + alpha * a) / a0);
    filter.b1 = static_cast<float>(-2.0 * cosine / a0);
    filter.b2 = static_cast<float>((1.0 - alpha * a) / a0);
    filter.a1 = static_cast<float>(-2.0 * cosine / a0);
    filter.a2 = static_cast<float>((1.0 - alpha / a) / a0);
}

// Bass and treble shelves from the same cookbook.
void DesignShelf(Biquad& filter, double rate, double frequency, double gainDb, double q,
                 bool high) {
    filter.active = gainDb != 0.0 && frequency < rate * 0.45;
    if (!filter.active) return;
    double a = std::pow(10.0, gainDb / 40.0);
    double w0 = 2.0 * 3.14159265358979323846 * frequency / rate;
    double alpha = std::sin(w0) / (2.0 * q);
    double cosine = std::cos(w0), root = 2.0 * std::sqrt(a) * alpha;
    double sign = high ? -1.0 : 1.0;
    double a0 = (a + 1) + sign * (a - 1) * cosine + root;
    filter.b0 = static_cast<float>(a * ((a + 1) - sign * (a - 1) * cosine + root) / a0);
    filter.b1 = static_cast<float>(sign * 2 * a * ((a - 1) - sign * (a + 1) * cosine) / a0);
    filter.b2 = static_cast<float>(a * ((a + 1) - sign * (a - 1) * cosine - root) / a0);
    filter.a1 = static_cast<float>(-sign * 2 * ((a - 1) + sign * (a + 1) * cosine) / a0);
    filter.a2 = static_cast<float>(((a + 1) + sign * (a - 1) * cosine - root) / a0);
}

// One band of a layout: shelves at the ends of the bass/mids/treble layout,
// peaks everywhere else.
void DesignBand(Biquad& filter, double rate, const EqLayout& layout, int band, double gainDb) {
    if (layout.tone && band == 0) {
        DesignShelf(filter, rate, layout.frequencies[0], gainDb, layout.q, false);
    } else if (layout.tone && band == layout.count - 1) {
        DesignShelf(filter, rate, layout.frequencies[band], gainDb, layout.q, true);
    } else {
        DesignPeak(filter, rate, layout.frequencies[band], gainDb, layout.tone ? 0.7 : layout.q);
    }
}

inline float RunBiquad(Biquad& filter, int channel, float x) {
    float y = filter.b0 * x + filter.z1[channel];
    filter.z1[channel] = filter.b1 * x - filter.a1 * y + filter.z2[channel];
    filter.z2[channel] = filter.b2 * x - filter.a2 * y;
    return y;
}

// Up to 80% of full scale samples pass unchanged; above, a soft knee bends
// them towards full scale instead of clipping.
inline float SoftLimit(float sample) {
    constexpr float knee = 0.8f;
    float magnitude = std::fabs(sample);
    if (magnitude <= knee) return sample;
    float bent = knee + (1.0f - knee) * std::tanh((magnitude - knee) / (1.0f - knee));
    return sample < 0.0f ? -bent : bent;
}

struct FxChain {
    Biquad bands[EQ_MAX_BANDS];
    float gain = 1.0f;
    // How far above full scale the stream may go: the output's own volume
    // turns the mix down after it, so with the speakers at 30% a boosted app
    // has room to be louder before anything clips.
    float ceiling = 1.0f;
    float preamp = 1.0f;  // Headroom for boosted bands, like Equalizer APO's preamp.

    // Loudness equalization: a slow level follower that steers a gain
    // towards a target level, quickly down and, by the release time, up.
    bool leveling = false;
    float detect = 0.0f, attack = 0.0f, release = 0.0f;
    float meanSquare = 0.0f, levelDb = 0.0f, levelGain = 1.0f;
    float lowestDb = -6.0f;       // How far it may turn the app down.
    float highestDb = 12.0f;      // ... and up.
    float targetDb = -18.0f;      // RMS level it aims at.
    float gateLevel = 1e-5f;      // Mean square below which the gain holds.
    size_t lookahead = 0;         // Ultra short: the sound waits a few ms for the gain.
    size_t delayPosition = 0;
    std::vector<float> delay;

    void Configure(const FxSettings& settings, double rate) {
        gain = settings.gain;
        leveling = settings.loudness.on;
        auto coefficient = [rate](double seconds) {
            return static_cast<float>(1.0 - std::exp(-1.0 / (seconds * rate)));
        };
        int mode = ClampInt(settings.loudness.release, 0, 5);
        const double* v = mode == 4 ? settings.loudness.custom : LOUD_MODES[mode];
        auto seconds = [](double milliseconds) { return std::max(0.0005, milliseconds / 1000.0); };
        detect = coefficient(seconds(v[0]));
        attack = coefficient(seconds(v[1]));
        release = coefficient(seconds(v[2]));
        lowestDb = static_cast<float>(-v[3]);
        highestDb = static_cast<float>(v[4]);
        targetDb = static_cast<float>(v[5]);
        gateLevel = static_cast<float>(std::pow(10.0, v[7] / 10.0));
        size_t wanted = leveling ? static_cast<size_t>(std::clamp(v[6], 0.0, 50.0) * rate / 1000.0) : 0;
        if (wanted != lookahead) {
            lookahead = wanted;
            delay.assign(wanted * 2, 0.0f);
            delayPosition = 0;
        }
        int highest = 0;
        const EqLayout& layout = EqLayoutFor(settings.eq.bands);
        for (int band = 0; band < EQ_MAX_BANDS; ++band) {
            bool used = settings.eq.on && band < layout.count;
            int value = used ? settings.eq.gains[band] : 0;
            if (used) {
                DesignBand(bands[band], rate, layout, band, value / 10.0);
            } else {
                bands[band].active = false;
            }
            highest = std::max(highest, value);
        }
        // No preamp: a boosted band only makes that band louder. The soft
        // limiter (or LoudMax) catches the peaks.
        (void)highest;
        preamp = 1.0f;
    }

    // Interleaved stereo. Without `limit`, LoudMax limits after this.
    void Process(float* samples, size_t frames, bool limit = true) {
        float level = gain * preamp;
        for (size_t i = 0; i < frames; ++i) {
            if (leveling) {
                // Aims at the target RMS level, within the allowed reduction and
                // boost; below the gate (silence) the gain holds.
                float l = samples[i * 2], r = samples[i * 2 + 1];
                meanSquare += detect * ((l * l + r * r) * 0.5f - meanSquare);
                if ((i & 31) == 0) {
                    if (meanSquare > gateLevel) {
                        float target = std::clamp(targetDb - 10.0f * std::log10(meanSquare),
                                                  lowestDb, highestDb);
                        float step = (target < levelDb ? attack : release) * 32.0f;
                        levelDb += std::min(step, 1.0f) * (target - levelDb);
                    }
                    levelGain = std::pow(10.0f, levelDb / 20.0f);
                }
            }
            float frameLevel = leveling ? level * levelGain : level;
            for (int channel = 0; channel < 2; ++channel) {
                float x = samples[i * 2 + channel];
                if (lookahead) std::swap(x, delay[delayPosition * 2 + channel]);
                for (auto& band : bands) {
                    if (band.active) x = RunBiquad(band, channel, x);
                }
                samples[i * 2 + channel] =
                    limit ? SoftLimit(x * frameLevel / ceiling) * ceiling : x * frameLevel;
            }
            if (lookahead && ++delayPosition >= lookahead) delayPosition = 0;
        }
    }

    // The output's own equalizer, when the mixer's engine does it.
    Biquad deviceBands[EQ_MAX_BANDS];
    bool deviceEq = false;
    float deviceGain = 1.0f;

    void ConfigureDevice(const EqualizerState& eq, const DeviceGain& gain, double rate) {
        deviceGain = DeviceGainActive(gain) ? static_cast<float>(std::pow(10.0, gain.tenths / 200.0)) : 1.0f;
        const EqLayout& layout = EqLayoutFor(eq.bands);
        deviceEq = false;
        for (int band = 0; band < EQ_MAX_BANDS; ++band) {
            bool used = eq.on && band < layout.count && eq.gains[band] != 0;
            if (used) {
                DesignBand(deviceBands[band], rate, layout, band, eq.gains[band] / 10.0);
                deviceEq = true;
            } else {
                deviceBands[band].active = false;
            }
        }
    }

    // Interleaved stereo. Without `limit`, LoudMax limits after this.
    void ProcessDevice(float* samples, size_t frames, bool limit) {
        for (size_t i = 0; i < frames; ++i) {
            for (int channel = 0; channel < 2; ++channel) {
                float x = samples[i * 2 + channel];
                if (deviceEq) {
                    for (auto& band : deviceBands) {
                        if (band.active) x = RunBiquad(band, channel, x);
                    }
                }
                x *= deviceGain;
                samples[i * 2 + channel] = limit ? SoftLimit(x / ceiling) * ceiling : x;
            }
        }
    }
};

// --- application loopback capture (Windows 10 build 20348 and later)

struct FxLoopbackParams {
    DWORD activationType;      // AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK = 1
    DWORD targetProcessId;
    DWORD processLoopbackMode;  // PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE = 0
};

class FxActivation final : public IActivateAudioInterfaceCompletionHandler {
public:
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ~FxActivation() {
        if (done) CloseHandle(done);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        // IAgileObject: the activation may complete on any thread.
        static const IID agile = {0x94ea2b94, 0xe9cc, 0x49e0,
                                  {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};
        if (IsEqualIID(riid, __uuidof(IUnknown)) ||
            IsEqualIID(riid, __uuidof(IActivateAudioInterfaceCompletionHandler)) ||
            IsEqualIID(riid, agile)) {
            *object = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = InterlockedDecrement(&refs_);
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation*) override {
        SetEvent(done);
        return S_OK;
    }

private:
    LONG refs_ = 1;
};

IAudioClient* ActivateProcessLoopback(DWORD processId) {
    using Activate_t = HRESULT(WINAPI*)(LPCWSTR, REFIID, PROPVARIANT*,
                                        IActivateAudioInterfaceCompletionHandler*,
                                        IActivateAudioInterfaceAsyncOperation**);
    static Activate_t activate = [] {
        HMODULE module = LoadLibraryExW(L"mmdevapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        return module ? reinterpret_cast<Activate_t>(reinterpret_cast<void*>(
                            GetProcAddress(module, "ActivateAudioInterfaceAsync")))
                      : nullptr;
    }();
    if (!activate) return nullptr;
    FxLoopbackParams params{1, processId, 0};
    PROPVARIANT variant{};
    variant.vt = VT_BLOB;
    variant.blob.cbSize = sizeof(params);
    variant.blob.pBlobData = reinterpret_cast<BYTE*>(&params);
    FxActivation* handler = new FxActivation();
    IActivateAudioInterfaceAsyncOperation* operation = nullptr;
    IAudioClient* client = nullptr;
    if (handler->done &&
        SUCCEEDED(activate(L"VAD\\Process_Loopback", __uuidof(IAudioClient), &variant, handler,
                           &operation)) &&
        WaitForSingleObject(handler->done, 5000) == WAIT_OBJECT_0) {
        HRESULT result = E_FAIL;
        IUnknown* unknown = nullptr;
        if (SUCCEEDED(operation->GetActivateResult(&result, &unknown)) && SUCCEEDED(result) &&
            unknown) {
            unknown->QueryInterface(__uuidof(IAudioClient), reinterpret_cast<void**>(&client));
        }
        if (unknown) unknown->Release();
    }
    if (operation) operation->Release();
    handler->Release();
    return client;
}

WAVEFORMATEX FxFormat(bool isFloat, DWORD rate) {
    WAVEFORMATEX format{};
    format.wFormatTag = isFloat ? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = rate;
    format.wBitsPerSample = isFloat ? 32 : 16;
    format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    return format;
}

// 7.1 sound as 32-bit float, for recording an app that plays surround sound.
WAVEFORMATEXTENSIBLE SurroundFormat(DWORD rate) {
    WAVEFORMATEXTENSIBLE format{};
    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.nChannels = SURROUND_CHANNELS;
    format.Format.nSamplesPerSec = rate;
    format.Format.wBitsPerSample = 32;
    format.Format.nBlockAlign = SURROUND_CHANNELS * 4;
    format.Format.nAvgBytesPerSec = rate * format.Format.nBlockAlign;
    format.Format.cbSize = 22;
    format.Samples.wValidBitsPerSample = 32;
    format.dwChannelMask = 0x63F;  // The 7.1 layout.
    static const GUID kFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
    format.SubFormat = kFloat;
    return format;
}

// Interleaved 7.1 to stereo, each channel at its own volume.
void DownmixSurround(const float* in, float* out, size_t frames, const int* percent) {
    constexpr float k = 0.70710678f;
    float g[SURROUND_CHANNELS];
    for (int ch = 0; ch < SURROUND_CHANNELS; ++ch) g[ch] = percent ? percent[ch] / 100.0f : 1.0f;
    for (size_t i = 0; i < frames; ++i) {
        const float* f = in + i * SURROUND_CHANNELS;
        float fl = f[0] * g[0], fr = f[1] * g[1], c = f[2] * g[2], lfe = f[3] * g[3];
        float bl = f[4] * g[4], br = f[5] * g[5], sl = f[6] * g[6], sr = f[7] * g[7];
        out[i * 2] = fl + k * c + 0.5f * lfe + k * sl + 0.5f * bl;
        out[i * 2 + 1] = fr + k * c + 0.5f * lfe + k * sr + 0.5f * br;
    }
}

struct FxStream {
    DWORD pid = 0;
    std::wstring exe;
    std::wstring inputId;  // An input listened to through the mixer: its id; empty for an app.
    std::wstring sink;  // Copied for the thread, which must not read g_fxSinkId.
    HANDLE thread = nullptr;
    HANDLE stop = nullptr;
    std::atomic<bool> running{true};
    std::atomic<bool> failed{false};
};
std::vector<std::unique_ptr<FxStream>> g_fxStreams;
std::unordered_map<std::wstring, ULONGLONG> g_fxRetryAfter;  // By executable name.

struct FxRender {
    IAudioClient* client = nullptr;
    IAudioRenderClient* render = nullptr;
    UINT32 frames = 0;
    std::wstring deviceId;
    void Close() {
        if (client) client->Stop();
        if (render) render->Release();
        if (client) client->Release();
        *this = {};
    }
};

std::wstring DefaultRenderId(IMMDeviceEnumerator* enumerator) {
    std::wstring id;
    IMMDevice* device = nullptr;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device))) {
        LPWSTR raw = nullptr;
        if (SUCCEEDED(device->GetId(&raw)) && raw) {
            id = raw;
            CoTaskMemFree(raw);
        }
        device->Release();
    }
    return id;
}

// Opens the output the processed audio goes to. Never the silent output.
bool OpenFxRender(IMMDeviceEnumerator* enumerator, const std::wstring& target,
                  const std::wstring& sink, DWORD rate, FxRender* out, bool exact = false) {
    std::wstring id = target;
    if (!exact && (id.empty() || _wcsicmp(id.c_str(), sink.c_str()) == 0)) id = DefaultRenderId(enumerator);
    if (id.empty() || _wcsicmp(id.c_str(), sink.c_str()) == 0) return false;
    IMMDevice* device = nullptr;
    if (FAILED(enumerator->GetDevice(id.c_str(), &device))) {
        if (target.empty() || exact) return false;
        return OpenFxRender(enumerator, L"", sink, rate, out);  // The output is gone.
    }
    FxRender render;
    render.deviceId = id;
    WAVEFORMATEX format = FxFormat(true, rate);
    HRESULT step[5];
    step[0] = device->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, nullptr,
                               reinterpret_cast<void**>(&render.client));
    step[1] = SUCCEEDED(step[0])
        ? render.client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                    AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                    500000, 0, &format, nullptr)
        : E_FAIL;
    step[2] = SUCCEEDED(step[1]) ? render.client->GetBufferSize(&render.frames) : E_FAIL;
    step[3] = SUCCEEDED(step[2]) ? render.client->GetService(__uuidof(IAudioRenderClient),
                                                              reinterpret_cast<void**>(&render.render))
                                 : E_FAIL;
    step[4] = SUCCEEDED(step[3]) ? render.client->Start() : E_FAIL;
    bool ok = SUCCEEDED(step[4]);
    if (!ok && exact) {
        Wh_Log(L"Mixer: opening an output failed: activate 0x%08lX, initialize 0x%08lX, start 0x%08lX",
               static_cast<unsigned long>(step[0]), static_cast<unsigned long>(step[1]), static_cast<unsigned long>(step[4]));
    }
    device->Release();
    if (!ok) {
        render.Close();
        return false;
    }
    *out = render;
    return true;
}

// The room above full scale an output leaves: the inverse of its volume when
// Windows turns it down in software, none when the device does it itself.
float FxCeiling(IMMDeviceEnumerator* enumerator, const std::wstring& id) {
    float ceiling = 1.0f;
    IMMDevice* device = nullptr;
    IAudioEndpointVolume* volume = nullptr;
    if (!id.empty() && SUCCEEDED(enumerator->GetDevice(id.c_str(), &device)) &&
        SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER,
                                   nullptr, reinterpret_cast<void**>(&volume)))) {
        DWORD hardware = 0;
        float decibels = 0.0f;
        if (SUCCEEDED(volume->QueryHardwareSupport(&hardware)) &&
            !(hardware & ENDPOINT_HARDWARE_SUPPORT_VOLUME) &&
            SUCCEEDED(volume->GetMasterVolumeLevel(&decibels))) {
            ceiling = std::clamp(static_cast<float>(std::pow(10.0, -decibels / 20.0)), 1.0f, 8.0f);
        }
    }
    if (volume) volume->Release();
    if (device) device->Release();
    return ceiling;
}

// Lets a queue's samples through to its output: as many as keep about queueMs queued there, and never the last
// `hold` samples, which are that output's delay. What piles up when two devices' clocks drift apart is dropped.
struct PumpResult {
    UINT32 padding = 0;  // Already queued in the device before this call.
    size_t ready = 0;    // Samples that could be let through.
    bool wrote = false;
};

PumpResult PumpOutput(FxRender& output, std::vector<float>& queue, size_t hold, DWORD rate, int queueMs) {
    PumpResult result;
    result.ready = queue.size() > hold ? queue.size() - hold : 0;
    if (output.client) {
        UINT32 padding = 0;
        HRESULT state = output.client->GetCurrentPadding(&padding);
        if (state == AUDCLNT_E_DEVICE_INVALIDATED) {
            output.Close();
        } else if (SUCCEEDED(state)) {
            result.padding = padding;
            UINT32 wanted = rate * queueMs / 1000;
            UINT32 room = padding < wanted ? wanted - padding : 0;
            room = std::min(room, output.frames - std::min(output.frames, padding));
            UINT32 frames = std::min<UINT32>(room, static_cast<UINT32>(result.ready / 2));
            BYTE* target = nullptr;
            if (frames && SUCCEEDED(output.render->GetBuffer(frames, &target))) {
                memcpy(target, queue.data(), static_cast<size_t>(frames) * 2 * sizeof(float));
                output.render->ReleaseBuffer(frames, 0);
                queue.erase(queue.begin(), queue.begin() + static_cast<size_t>(frames) * 2);
                result.wrote = true;
            }
        }
    }
    size_t limit = static_cast<size_t>(rate) / 10 * 2;  // 100 ms.
    if (queue.size() > hold + limit) {
        queue.erase(queue.begin(), queue.end() - static_cast<std::ptrdiff_t>(hold + limit / 2));
    }
    return result;
}

// A test sound for lining up two outputs: a soft tap every second on each (a low tone with a quick start and a short
// fade), a fifth apart so the two can be told from each other, with the delay between them applied the way the engine
// does it. The delay can be changed while it plays.
struct MirrorTest {
    HANDLE thread = nullptr;
    HANDLE stop = nullptr;
    std::wstring first, second;
};
MirrorTest g_mirrorTest;
std::atomic<int> g_mirrorTestOffset{0};

DWORD WINAPI MirrorTestThread(void*) {
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    constexpr DWORD rate = 48000;
    IMMDeviceEnumerator* enumerator = nullptr;
    FxRender one, two;
    std::vector<float> queueOne, queueTwo;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                                   __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator)))) {
        OpenFxRender(enumerator, g_mirrorTest.first, L"", rate, &one, true);
        OpenFxRender(enumerator, g_mirrorTest.second, L"", rate, &two, true);
        Wh_Log(L"Mixer: sync test sound, first output %s, second output %s", one.client ? L"open" : L"not open",
               two.client ? L"open" : L"not open");
    }
    const ULONGLONG start = GetTickCount64();
    unsigned long long made = 0;
    constexpr unsigned long long burst = rate / 5;  // 200 ms: the tap has faded long before.
    const double pi = 3.14159265358979323846;
    while (WaitForSingleObject(g_mirrorTest.stop, 5) == WAIT_TIMEOUT) {
        unsigned long long wanted = (GetTickCount64() - start) * rate / 1000;
        for (; made < wanted; ++made) {
            unsigned long long position = made % rate;
            float a = 0.0f, b = 0.0f;
            if (position < burst) {
                double seconds = static_cast<double>(position) / rate;
                // 3 ms to come up, then it fades away.
                float envelope = static_cast<float>(std::min(1.0, seconds / 0.003) * std::exp(-seconds / 0.045));
                a = 0.14f * envelope * static_cast<float>(std::sin(2.0 * pi * 220.0 * seconds));
                b = 0.14f * envelope * static_cast<float>(std::sin(2.0 * pi * 330.0 * seconds));
            }
            queueOne.push_back(a);
            queueOne.push_back(a);
            queueTwo.push_back(b);
            queueTwo.push_back(b);
        }
        int offset = g_mirrorTestOffset.load();
        size_t delay = static_cast<size_t>(std::abs(offset)) * rate / 1000 * 2;
        PumpOutput(one, queueOne, offset < 0 ? delay : 0, rate, 30);
        PumpOutput(two, queueTwo, offset > 0 ? delay : 0, rate, 30);
        if (!one.client) queueOne.clear();
        if (!two.client) queueTwo.clear();
    }
    one.Close();
    two.Close();
    if (enumerator) enumerator->Release();
    if (SUCCEEDED(com)) CoUninitialize();
    return 0;
}

void StopMirrorTest() {
    if (!g_mirrorTest.thread) return;
    SetEvent(g_mirrorTest.stop);
    WaitForSingleObject(g_mirrorTest.thread, 3000);
    CloseHandle(g_mirrorTest.thread);
    CloseHandle(g_mirrorTest.stop);
    g_mirrorTest = {};
}

bool StartMirrorTest(const std::wstring& first, const std::wstring& second, int offsetMs) {
    StopMirrorTest();
    g_mirrorTest.first = first;
    g_mirrorTest.second = second;
    g_mirrorTestOffset = offsetMs;
    g_mirrorTest.stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_mirrorTest.thread = g_mirrorTest.stop ? CreateThread(nullptr, 0, MirrorTestThread, nullptr, 0, nullptr) : nullptr;
    if (!g_mirrorTest.thread) {
        if (g_mirrorTest.stop) CloseHandle(g_mirrorTest.stop);
        g_mirrorTest = {};
        return false;
    }
    return true;
}

DWORD WINAPI FxStreamThread(void* parameter) {
    FxStream* stream = static_cast<FxStream*>(parameter);
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    // Also ask Windows to schedule this thread like an audio thread, so a game that keeps every core busy
    // does not make it late.
    if (HMODULE avrt = LoadLibraryW(L"avrt.dll")) {
        using SetCharacteristics = HANDLE(WINAPI*)(LPCWSTR, LPDWORD);
        if (auto set = reinterpret_cast<SetCharacteristics>(GetProcAddress(avrt, "AvSetMmThreadCharacteristicsW"))) {
            DWORD task = 0;
            set(L"Pro Audio", &task);
        }
    }
    const std::wstring sink = stream->sink;
    const bool monitor = !stream->inputId.empty();
    HANDLE process = monitor ? nullptr : OpenProcess(SYNCHRONIZE, FALSE, stream->pid);
    HANDLE captured = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    IMMDeviceEnumerator* enumerator = nullptr;
    IAudioClient* capture = nullptr;
    IAudioCaptureClient* captureClient = nullptr;
    FxRender render;
    FxChain chain;
    std::vector<float> fifo;  // Processed stereo samples waiting for the output.
    bool isFloat = true;
    DWORD rate = 48000;
    LoudMaxInstance loudMax;
    LoudMaxInstance deviceLoudMax;  // The output's own, when the engine does the output's effects.
    // The output may be played on a second one as well: the same sound, each with its own queue and delay.
    FxRender mirror;
    std::vector<float> mirrorQueue;  // Processed stereo samples waiting for the second output.
    std::wstring mirrorWanted;       // The second output the settings ask for, empty for none.
    size_t holdMain = 0, holdMirror = 0;  // Samples kept back in a queue: the delay of that output.
    ULONGLONG nextMirrorTry = 0;
    DeviceFx deviceFx;
    bool deviceStage = false;
    std::wstring lastDevice;
    // LoudMax runs in this stream while the app's settings ask for it.
    auto syncLoudMax = [&](const FxSettings& current) {
        if (current.loudMax.on && !loudMax.effect && !loudMax.Open(rate)) {
            Wh_Log(L"Mixer: LoudMax did not load for %s", stream->exe.c_str());
        }
        if (!current.loudMax.on && loudMax.effect) loudMax.Close();
        if (loudMax.effect) ApplyLoudMaxParams(loudMax.effect, loudMax.names, current.loudMax);
    };

    // The effects of the output this stream plays on, picked up when the output or the settings change.
    auto refreshDevice = [&]() {
        deviceFx = g_deviceFxBuiltIn && !render.deviceId.empty()
                       ? ReadDeviceFx(EndpointGuid(render.deviceId)) : DeviceFx{};
        chain.ConfigureDevice(deviceFx.eq, deviceFx.gain, rate);
        deviceStage = DeviceFxActive(deviceFx);
        if (deviceFx.loudMax.on && !deviceLoudMax.effect && !deviceLoudMax.Open(rate)) {
            Wh_Log(L"Mixer: LoudMax did not load for the output of %s", stream->exe.c_str());
        }
        if (!deviceFx.loudMax.on && deviceLoudMax.effect) deviceLoudMax.Close();
        if (deviceLoudMax.effect) {
            ApplyLoudMaxParams(deviceLoudMax.effect, deviceLoudMax.names, deviceFx.loudMax);
        }
    };

    // The second output of the one this stream plays on, picked up when the output or the settings change.
    auto refreshMirror = [&](const FxSettings& current) {
        DeviceMirror want = DeviceMirrorActive(current.mirror) ? current.mirror
                          : render.deviceId.empty() ? DeviceMirror{} : ReadDeviceMirror(EndpointGuid(render.deviceId));
        bool use = DeviceMirrorActive(want) && _wcsicmp(want.target.c_str(), render.deviceId.c_str()) != 0 &&
                   _wcsicmp(want.target.c_str(), sink.c_str()) != 0;
        holdMain = holdMirror = 0;
        if (!use) {
            mirror.Close();
            mirrorQueue.clear();
            mirrorWanted.clear();
            return;
        }
        size_t delay = static_cast<size_t>(std::abs(want.offsetMs)) * rate / 1000 * 2;
        (want.offsetMs > 0 ? holdMirror : holdMain) = delay;  // The one that is early waits.
        if (_wcsicmp(mirrorWanted.c_str(), want.target.c_str()) != 0) {
            mirror.Close();
            mirrorQueue.clear();
            mirrorWanted = want.target;
            nextMirrorTry = 0;
            Wh_Log(L"Mixer: %s is to be played on a second output as well (%s), %d ms apart", stream->exe.c_str(),
                   DeviceMirrorActive(current.mirror) ? L"its own choice" : L"the choice of its output", want.offsetMs);
        }
    };

    // An app whose channels are in use is recorded as 7.1; the others as stereo.
    const int captureChannels = ReadFxSettings(stream->exe).channels.on ? SURROUND_CHANNELS : 2;
    int streamChannels = 2;
    bool restart = false;
    // Float at 48 kHz first, then the 16-bit format Microsoft's sample uses.
    for (int attempt = 0; attempt < 2 && !captureClient; ++attempt) {
        isFloat = attempt == 0;
        rate = isFloat ? 48000 : 44100;
        if (capture) {
            capture->Release();
            capture = nullptr;
        }
        if (monitor) {
            // An input: recorded like a microphone, not through the application loopback.
            IMMDeviceEnumerator* inputs = nullptr;
            IMMDevice* input = nullptr;
            if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                                           __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&inputs))) &&
                SUCCEEDED(inputs->GetDevice(stream->inputId.c_str(), &input))) {
                input->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, nullptr, reinterpret_cast<void**>(&capture));
            }
            if (input) input->Release();
            if (inputs) inputs->Release();
        } else {
            capture = ActivateProcessLoopback(stream->pid);
        }
        WAVEFORMATEX stereoFormat = FxFormat(isFloat, rate);
        WAVEFORMATEXTENSIBLE surroundFormat = SurroundFormat(rate);
        bool wide = captureChannels == SURROUND_CHANNELS && isFloat;
        const WAVEFORMATEX* format = wide ? &surroundFormat.Format : &stereoFormat;
        if (capture &&
            SUCCEEDED(capture->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                          (monitor ? 0u : AUDCLNT_STREAMFLAGS_LOOPBACK) |
                                              AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                              AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                                          200000, 0, format, nullptr)) &&
            SUCCEEDED(capture->SetEventHandle(captured))) {
            capture->GetService(__uuidof(IAudioCaptureClient),
                                reinterpret_cast<void**>(&captureClient));
            if (captureClient) streamChannels = wide ? SURROUND_CHANNELS : 2;
        }
    }

    unsigned version = 0;
    FxSettings settings;
    int queueMs = 30;  // How much is kept queued in the output.
    int extraMs = 0;   // Added when the output ran dry: a busy computer needs a larger queue.
    ULONGLONG lastPlayed = 0;
    const ULONGLONG started = GetTickCount64();  // The first seconds of a stream do not count as running dry.
    ULONGLONG lastGrow = 0;
    ULONGLONG nextDefaultCheck = 0;
    ULONGLONG nextCeilingCheck = 0;
    ULONGLONG nextPeakLog = 0;
    float peak = 0.0f;
    int peakLogs = 0;
    bool ok = captureClient && captured && (monitor || sink.size()) &&
              SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                         CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator),
                                         reinterpret_cast<void**>(&enumerator))) &&
              SUCCEEDED(capture->Start());
    Wh_Log(L"Mixer: stream for %s records %d channels (wanted %d)", stream->exe.c_str(), streamChannels, captureChannels);
    if (ok) {
        // Ready to play before the app goes silent: settings, output, route.
        version = g_fxVersion.load();
        settings = ReadFxSettings(stream->exe);
        queueMs = settings.loudness.on && settings.loudness.release == LOUD_COMPETITIVE ? 15 : 30;
        chain.Configure(settings, rate);
        syncLoudMax(settings);
        IAudioPolicyConfigFactory* config = nullptr;
        ok = OpenFxRender(enumerator, settings.target, sink, rate, &render) &&
             (monitor || ((config = CreateAudioPolicyConfig()) != nullptr &&
                          SUCCEEDED(SetAppOutput(config, {stream->pid}, sink))));
        if (config) config->Release();
        if (ok) {
            Wh_Log(L"Mixer: processing %s (PID %lu)", stream->exe.c_str(), stream->pid);
        }
    }
    if (!ok) {
        Wh_Log(L"Mixer: could not process %s (PID %lu) through application loopback",
               stream->exe.c_str(), stream->pid);
        stream->failed = true;
    }
    (void)restart;

    while (ok) {
        HANDLE waits[3] = {stream->stop, captured, process};
        DWORD count = process ? 3 : 2;
        DWORD woke = WaitForMultipleObjects(count, waits, FALSE, 50);
        if (woke == WAIT_OBJECT_0 || woke == WAIT_OBJECT_0 + 2 || woke == WAIT_FAILED) break;

        unsigned current = g_fxVersion.load();
        if (current != version) {
            version = current;
            std::wstring oldTarget = settings.target;
            settings = ReadFxSettings(stream->exe);
            // Turning the channels on or off changes what is recorded: the stream starts again.
            if ((captureChannels == SURROUND_CHANNELS) != settings.channels.on) {
                restart = true;
                break;
            }
            queueMs = settings.loudness.on && settings.loudness.release == LOUD_COMPETITIVE ? 15 : 30;
            chain.Configure(settings, rate);
            syncLoudMax(settings);
            if (settings.target != oldTarget) render.Close();
            lastDevice.clear();  // The output's effects are read again.
        }
        ULONGLONG now = GetTickCount64();
        if (render.client && settings.target.empty() && now >= nextDefaultCheck) {
            // Following the default output: go along when it changes.
            nextDefaultCheck = now + 500;
            if (_wcsicmp(DefaultRenderId(enumerator).c_str(), render.deviceId.c_str()) != 0) {
                render.Close();
            }
        }
        if (!render.client && now >= nextDefaultCheck) {
            if (!OpenFxRender(enumerator, settings.target, sink, rate, &render)) {
                nextDefaultCheck = now + 500;  // Try again shortly, the output may return.
            }
            nextCeilingCheck = 0;
        }
        if (render.client && render.deviceId != lastDevice) {
            lastDevice = render.deviceId;
            refreshDevice();
            refreshMirror(settings);
        }
        if (!mirrorWanted.empty() && !mirror.client && now >= nextMirrorTry) {
            nextMirrorTry = now + 2000;  // The second output may be off or unplugged: look again shortly.
            if (OpenFxRender(enumerator, mirrorWanted, sink, rate, &mirror, true)) {
                mirrorQueue.clear();
                Wh_Log(L"Mixer: %s is also played on a second output", stream->exe.c_str());
            } else {
                Wh_Log(L"Mixer: the second output of %s could not be opened", stream->exe.c_str());
            }
        }
        if (!render.client) lastDevice.clear();
        if (render.client && now >= nextCeilingCheck) {
            nextCeilingCheck = now + 250;  // Follows the output's volume.
            chain.ceiling = FxCeiling(enumerator, render.deviceId);
        }

        if (monitor && now >= nextPeakLog) {
            nextPeakLog = now + 5000;
            if (++peakLogs <= 6) {
                Wh_Log(L"Mixer: input %s: loudest sample %.4f in the last seconds, output '%s'", stream->inputId.c_str(), peak,
                       render.client ? render.deviceId.c_str() : L"none open");
            }
            peak = 0.0f;
        }
        // Everything the app played since the last wake.
        UINT32 packet = 0;
        while (SUCCEEDED(captureClient->GetNextPacketSize(&packet)) && packet) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(captureClient->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
                ok = false;
                break;
            }
            size_t start = fifo.size();
            fifo.resize(start + static_cast<size_t>(frames) * 2);
            float* out = fifo.data() + start;
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                std::fill(out, out + frames * 2, 0.0f);
            } else if (isFloat && streamChannels == SURROUND_CHANNELS) {
                // The fifo holds stereo: the channels are mixed down here, the off ones left out.
                DownmixSurround(reinterpret_cast<const float*>(data), out, frames,
                                settings.channels.on ? settings.channels.percent : nullptr);
            } else if (isFloat) {
                memcpy(out, data, static_cast<size_t>(frames) * 2 * sizeof(float));
            } else {
                const short* pcm = reinterpret_cast<const short*>(data);
                for (size_t i = 0; i < static_cast<size_t>(frames) * 2; ++i) {
                    out[i] = pcm[i] / 32768.0f;
                }
            }
            captureClient->ReleaseBuffer(frames);
            if (monitor) {
                for (size_t i = 0; i < static_cast<size_t>(frames) * 2; ++i) peak = std::max(peak, std::fabs(out[i]));
            }
            chain.Process(out, frames, !loudMax.effect && !deviceStage);
            loudMax.Process(out, frames);
            if (deviceStage) {
                chain.ProcessDevice(out, frames, !deviceLoudMax.effect);
                deviceLoudMax.Process(out, frames);
            }
            if (mirror.client) mirrorQueue.insert(mirrorQueue.end(), out, out + static_cast<size_t>(frames) * 2);
        }
        if (!ok) {
            stream->failed = true;
            break;
        }

        // Keep about 40 ms queued in each output; drop what piles up when the devices' clocks drift apart.
        // What an output is delayed by stays in its queue and is let through later.
        auto feed = [&](FxRender& output, std::vector<float>& queue, size_t hold, bool first) {
            PumpResult pumped = PumpOutput(output, queue, hold, rate, queueMs + extraMs);
            if (!first) return;
            if (output.client && pumped.padding == 0 && pumped.ready && lastPlayed && now - lastPlayed < 60 &&
                extraMs < 25 && now - started > 3000 && now - lastGrow > 2000) {
                lastGrow = now;
                extraMs += 5;
                Wh_Log(L"Mixer: output of %s ran dry, queue now %d ms", stream->exe.c_str(), queueMs + extraMs);
            }
            if (pumped.wrote) lastPlayed = now;
        };
        feed(render, fifo, holdMain, true);
        if (mirror.client) feed(mirror, mirrorQueue, holdMirror, false);
        if (!mirror.client) mirrorQueue.clear();
    }

    render.Close();
    mirror.Close();
    loudMax.Close();
    deviceLoudMax.Close();
    if (capture) capture->Stop();
    if (captureClient) captureClient->Release();
    if (capture) capture->Release();
    if (enumerator) enumerator->Release();
    if (captured) CloseHandle(captured);
    if (process) CloseHandle(process);
    if (SUCCEEDED(com)) CoUninitialize();
    stream->running = false;
    return 0;
}

// --- managing the streams, on the mixer's thread

struct FxSession {
    DWORD pid;
    std::wstring exe;
};

std::vector<FxSession> EnumerateFxSessions() {
    std::vector<FxSession> found;
    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                                __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&enumerator)))) {
        return found;
    }
    IMMDeviceCollection* devices = nullptr;
    if (SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices))) {
        UINT deviceCount = 0;
        devices->GetCount(&deviceCount);
        for (UINT d = 0; d < deviceCount; ++d) {
            IMMDevice* device = nullptr;
            IAudioSessionManager2* manager = nullptr;
            IAudioSessionEnumerator* sessions = nullptr;
            if (SUCCEEDED(devices->Item(d, &device)) &&
                SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_INPROC_SERVER,
                                           nullptr, reinterpret_cast<void**>(&manager))) &&
                SUCCEEDED(manager->GetSessionEnumerator(&sessions))) {
                int count = 0;
                sessions->GetCount(&count);
                for (int i = 0; i < count; ++i) {
                    IAudioSessionControl* control = nullptr;
                    IAudioSessionControl2* control2 = nullptr;
                    DWORD processId = 0;
                    AudioSessionState state = AudioSessionStateExpired;
                    if (SUCCEEDED(sessions->GetSession(i, &control)) &&
                        SUCCEEDED(control->QueryInterface(__uuidof(IAudioSessionControl2),
                                                          reinterpret_cast<void**>(&control2))) &&
                        SUCCEEDED(control2->GetProcessId(&processId)) && processId &&
                        processId != GetCurrentProcessId() &&
                        control2->IsSystemSoundsSession() != S_OK &&
                        SUCCEEDED(control->GetState(&state)) &&
                        state != AudioSessionStateExpired &&
                        std::none_of(found.begin(), found.end(), [&](const FxSession& s) {
                            return s.pid == processId;
                        })) {
                        std::wstring path = GetProcessPath(processId);
                        if (!path.empty()) found.push_back({processId, ExeFileName(path)});
                    }
                    if (control2) control2->Release();
                    if (control) control->Release();
                }
            }
            if (sessions) sessions->Release();
            if (manager) manager->Release();
            if (device) device->Release();
        }
        devices->Release();
    }
    enumerator->Release();
    return found;
}

std::unordered_map<DWORD, DWORD> ParentProcesses() {
    std::unordered_map<DWORD, DWORD> parents;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return parents;
    PROCESSENTRY32W entry{sizeof(entry)};
    for (BOOL more = Process32FirstW(snapshot, &entry); more;
         more = Process32NextW(snapshot, &entry)) {
        parents[entry.th32ProcessID] = entry.th32ParentProcessID;
    }
    CloseHandle(snapshot);
    return parents;
}

void StopFxStream(FxStream& stream) {
    if (stream.stop) SetEvent(stream.stop);
    if (stream.thread) {
        WaitForSingleObject(stream.thread, 3000);
        CloseHandle(stream.thread);
        stream.thread = nullptr;
    }
    if (stream.stop) {
        CloseHandle(stream.stop);
        stream.stop = nullptr;
    }
}

// Routes a process back to where it should be heard without the engine.
void RestoreFxRoute(IAudioPolicyConfigFactory* config, DWORD pid, const std::wstring& exe) {
    std::wstring target = FxTarget(exe);
    if (target == FX_FOLLOW_DEFAULT || IsFxSink(target)) target.clear();
    SetAppOutput(config, {pid}, target);
}

void EnsureSinkSurround(bool wanted);

void StopAllFx(bool restore) {
    if (restore) EnsureSinkSurround(false);  // The cable goes back to 2 channels.
    IAudioPolicyConfigFactory* config = restore ? CreateAudioPolicyConfig() : nullptr;
    for (auto& stream : g_fxStreams) {
        StopFxStream(*stream);
        if (config && stream->inputId.empty()) RestoreFxRoute(config, stream->pid, stream->exe);
    }
    g_fxStreams.clear();
    if (config) config->Release();
}

// The output an app is heard on right now, as an id; the default output when it follows that.
std::wstring HeardDeviceId(IAudioPolicyConfigFactory* config, DWORD pid, const std::wstring& exe,
                           const std::wstring& defaultId) {
    std::wstring routed = GetAppOutput(config, pid);
    if (IsFxSink(routed)) {
        routed = FxTarget(exe);
        if (routed == FX_FOLLOW_DEFAULT || IsFxSink(routed)) routed.clear();
    }
    return routed.empty() ? defaultId : routed;
}

// The silent cable offers 2 channels. Apps only play 5.1 or 7.1 sound to a device that offers it, so
// while an app's channels are in use the cable is set to 7.1, and back to 2 afterwards.
bool g_sinkSurroundByUs = false;
ULONGLONG g_sinkChangedAt = 0;  // When the cable's format last changed: it takes a moment to settle.

int SinkChannels(const std::wstring& id) {
    int channels = 0;
    IMMDeviceEnumerator* enumerator = nullptr;
    if (!id.empty() &&
        SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                                   __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator)))) {
        IMMDevice* device = nullptr;
        IAudioClient* client = nullptr;
        if (SUCCEEDED(enumerator->GetDevice(id.c_str(), &device)) &&
            SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, nullptr,
                                       reinterpret_cast<void**>(&client)))) {
            WAVEFORMATEX* mix = nullptr;
            if (SUCCEEDED(client->GetMixFormat(&mix)) && mix) {
                channels = mix->nChannels;
                CoTaskMemFree(mix);
            }
        }
        if (client) client->Release();
        if (device) device->Release();
        enumerator->Release();
    }
    return channels;
}

bool SetSinkChannels(const std::wstring& id, int channels) {
    IPolicyConfig* policy = nullptr;
    if (FAILED(CoCreateInstance(kCLSID_PolicyConfigClient, nullptr, CLSCTX_ALL, kIID_IPolicyConfig,
                                reinterpret_cast<void**>(&policy))) || !policy) {
        return false;
    }
    WAVEFORMATEXTENSIBLE format{};
    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.nChannels = static_cast<WORD>(channels);
    format.Format.nSamplesPerSec = 48000;
    format.Format.wBitsPerSample = 24;
    format.Format.nBlockAlign = static_cast<WORD>(channels * 3);
    format.Format.nAvgBytesPerSec = 48000 * format.Format.nBlockAlign;
    format.Format.cbSize = 22;
    format.Samples.wValidBitsPerSample = 24;
    format.dwChannelMask = channels == SURROUND_CHANNELS ? 0x63F : 0x3;
    static const GUID kPcm = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
    format.SubFormat = kPcm;
    HRESULT result = policy->SetDeviceFormat(id.c_str(), &format.Format, &format.Format);
    policy->Release();
    Wh_Log(L"Mixer: silent output set to %d channels: 0x%08lX", channels, static_cast<unsigned long>(result));
    return SUCCEEDED(result);
}

void EnsureSinkSurround(bool wanted) {
    if (g_fxSinkId.empty()) return;
    static bool known = false;
    if (!known) {
        known = true;
        g_sinkSurroundByUs = Wh_GetIntValue(L"sinkSurround_v1", 0) != 0;  // Also if the mixer was closed in between.
    }
    int channels = SinkChannels(g_fxSinkId);
    if (wanted && channels == 2) {
        if (SetSinkChannels(g_fxSinkId, SURROUND_CHANNELS)) {
            g_sinkSurroundByUs = true;
            Wh_SetIntValue(L"sinkSurround_v1", 1);
            g_sinkChangedAt = GetTickCount64();
        }
    } else if (!wanted && channels == SURROUND_CHANNELS) {  // Also if the flag was lost, for example by a reset.
        if (SetSinkChannels(g_fxSinkId, 2)) {
            g_sinkSurroundByUs = false;
            Wh_DeleteValue(L"sinkSurround_v1");
            g_sinkChangedAt = GetTickCount64();
        }
    }
}

bool FxActiveForPid(DWORD pid) {
    for (const auto& stream : g_fxStreams) {
        if (stream->inputId.empty() && stream->pid == pid && stream->running) return true;
    }
    return false;
}

// Starts streams for apps that need processing, stops the rest, and routes
// back apps left on the silent output, also after a crash.
void UpdateFx(HWND window) {
    g_fxUpdatePending = false;
    static bool devicePublished = false;
    if (!devicePublished) {
        devicePublished = true;
        PublishDeviceFx();
    }
    g_fxSinkId = FindFxSink();
    std::vector<FxSession> sessions = EnumerateFxSessions();
    IAudioPolicyConfigFactory* config = CreateAudioPolicyConfig();
    if (!config) {
        StopAllFx(false);
        return;
    }

    std::unordered_map<std::wstring, FxSettings> settingsByExe;
    for (const auto& session : sessions) {
        if (!settingsByExe.count(session.exe)) {
            PublishFxSettings(session.exe);
            settingsByExe[session.exe] = ReadFxSettings(session.exe);
            g_fxNeeded[session.exe] = FxNeeded(settingsByExe[session.exe]);
        }
    }

    // One stream per process tree: a child under a processed parent of the
    // same app is already recorded with it.
    std::unordered_map<DWORD, DWORD> parents = ParentProcesses();
    ULONGLONG now = GetTickCount64();
    std::vector<const FxSession*> wanted;
    bool sinkNeeded = false;
    std::wstring defaultId;
    {
        IMMDeviceEnumerator* defaultEnumerator = nullptr;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                                       __uuidof(IMMDeviceEnumerator),
                                       reinterpret_cast<void**>(&defaultEnumerator)))) {
            defaultId = DefaultRenderId(defaultEnumerator);
            defaultEnumerator->Release();
        }
    }
    for (const auto& session : sessions) {
        bool needed = FxNeeded(settingsByExe[session.exe]);
        // Apps on an output whose effects the mixer's own engine does need a stream too.
        if (!needed) {
            std::wstring heard = EndpointGuid(HeardDeviceId(config, session.pid, session.exe, defaultId));
            needed = DeviceFxNeededFor(heard) || MirrorNeededFor(heard);  // Playing on a second output takes a stream too.
        }
        if (!needed) continue;
        sinkNeeded = true;
        auto retry = g_fxRetryAfter.find(session.exe);
        if (g_fxSinkId.empty() || (retry != g_fxRetryAfter.end() && now < retry->second)) continue;
        bool covered = false;
        DWORD parent = session.pid;
        for (int depth = 0; depth < 8 && !covered; ++depth) {
            auto up = parents.find(parent);
            if (up == parents.end() || !up->second || up->second == parent) break;
            parent = up->second;
            covered = std::any_of(sessions.begin(), sessions.end(), [&](const FxSession& s) {
                return s.pid == parent && s.exe == session.exe;
            });
        }
        if (!covered) wanted.push_back(&session);
    }

    // The cable offers 7.1 while an app's channels are in use.
    EnsureSinkSurround(!ChannelMixApps().empty());

    // The inputs that are listened to through the mixer.
    std::vector<std::pair<std::wstring, std::wstring>> monitors;  // Settings name, device id.
    for (const std::wstring& guid : MonitorGuids()) {
        std::wstring id = CaptureDeviceId(guid);
        if (id.empty()) continue;
        PublishFxSettings(L"in:" + guid);
        monitors.emplace_back(L"in:" + guid, id);
    }

    // Stop what is no longer wanted, or failed.
    for (size_t i = 0; i < g_fxStreams.size();) {
        FxStream& stream = *g_fxStreams[i];
        bool keep = stream.running && (stream.inputId.empty()
            ? std::any_of(wanted.begin(), wanted.end(), [&](const FxSession* s) { return s->pid == stream.pid; })
            : std::any_of(monitors.begin(), monitors.end(),
                          [&](const std::pair<std::wstring, std::wstring>& m) { return m.second == stream.inputId; }));
        if (keep) {
            ++i;
            continue;
        }
        // Recording this app does not work here; leave it alone for a while.
        // An input that is not ready yet (just after the start of Windows) is tried again soon.
        if (stream.failed) g_fxRetryAfter[stream.exe] = now + (stream.inputId.empty() ? 600000 : 5000);
        StopFxStream(stream);
        if (stream.inputId.empty()) RestoreFxRoute(config, stream.pid, stream.exe);
        g_fxStreams.erase(g_fxStreams.begin() + static_cast<std::ptrdiff_t>(i));
    }

    // Start what is new.
    for (const FxSession* session : wanted) {
        if (FxActiveForPid(session->pid)) continue;
        // An app whose channels are in use waits until the cable has settled in 7.1.
        if (settingsByExe[session->exe].channels.on && GetTickCount64() - g_sinkChangedAt < 2500) continue;
        if (FxTarget(session->exe).empty()) {
            // The first time: keep playing where the app plays now.
            std::wstring routed = GetAppOutput(config, session->pid);
            SetFxTarget(session->exe, IsFxSink(routed) ? std::wstring() : routed);
        }
        auto stream = std::make_unique<FxStream>();
        stream->pid = session->pid;
        stream->exe = session->exe;
        stream->sink = g_fxSinkId;
        stream->stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        stream->thread = stream->stop
            ? CreateThread(nullptr, 0, FxStreamThread, stream.get(), 0, nullptr) : nullptr;
        if (!stream->thread) {
            StopFxStream(*stream);
            continue;
        }
        // The stream routes the app to the silent output once it is ready.
        g_fxStreams.push_back(std::move(stream));
    }

    // Start the inputs that are listened to.
    for (const auto& [key, id] : monitors) {
        bool running = std::any_of(g_fxStreams.begin(), g_fxStreams.end(),
                                   [&](const std::unique_ptr<FxStream>& s) { return s->inputId == id && s->running; });
        auto retry = g_fxRetryAfter.find(key);
        if (running || (retry != g_fxRetryAfter.end() && now < retry->second)) continue;
        auto stream = std::make_unique<FxStream>();
        stream->exe = key;
        stream->inputId = id;
        stream->sink = g_fxSinkId;
        stream->stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        stream->thread = stream->stop ? CreateThread(nullptr, 0, FxStreamThread, stream.get(), 0, nullptr) : nullptr;
        if (!stream->thread) {
            StopFxStream(*stream);
            continue;
        }
        g_fxStreams.push_back(std::move(stream));
    }

    // Apps left on the silent output without a stream go back.
    if (!g_fxSinkId.empty()) {
        for (const auto& session : sessions) {
            if (FxActiveForPid(session.pid)) continue;
            if (IsFxSink(GetAppOutput(config, session.pid))) {
                RestoreFxRoute(config, session.pid, session.exe);
            }
        }
    }
    config->Release();

    if (sinkNeeded && g_fxSinkId.empty() && !g_fxSinkMissingShown && window &&
        IsWindowVisible(window)) {
        g_fxSinkMissingShown = true;
        if (MessageBoxW(window,
                L"Boost above 100% and the equalizer per app need VB-CABLE, a small free "
                L"virtual audio output. The mixer routes the app there silently and plays it "
                L"processed on your real output, without touching the app or game.\n\n"
                L"Install VB-CABLE and restart Windows. Open the download page now?",
                L"Headroom", MB_YESNO | MB_ICONINFORMATION) == IDYES) {
            ShellExecuteW(window, L"open", L"https://vb-audio.com/Cable/", nullptr, nullptr,
                          SW_SHOWNORMAL);
        }
    }
}

// The output an app is heard on, seen through the engine: its target while
// it sits on the silent output.
std::wstring HeardOutput(IAudioPolicyConfigFactory* config, DWORD pid, const std::wstring& path) {
    std::wstring routed = GetAppOutput(config, pid);
    if (!IsFxSink(routed)) return routed;
    std::wstring target = FxTarget(ExeFileName(path));
    return target == FX_FOLLOW_DEFAULT || IsFxSink(target) ? std::wstring() : target;
}

// Sends apps to an output: processed ones change their stream's target,
// the rest are routed by Windows.
HRESULT RouteApps(IAudioPolicyConfigFactory* config, const std::vector<DWORD>& pids,
                  const std::wstring& target) {
    std::vector<DWORD> plain;
    for (DWORD pid : pids) {
        if (IsFxSink(GetAppOutput(config, pid))) {
            std::wstring path = GetProcessPath(pid);
            if (!path.empty()) SetFxTarget(ExeFileName(path), target);
        } else {
            plain.push_back(pid);
        }
    }
    return plain.empty() ? S_OK : SetAppOutput(config, plain, target);
}

// LoudMax's own window, to set it up for an app or an output. Its settings
// go to the app's streams, or into Equalizer APO's file, while they change.
struct LoudMaxEditor {
    HWND window = nullptr;
    HWND host = nullptr;  // The child window the plugin draws into.
    VstEffect* effect = nullptr;
    std::vector<std::string> names;
    std::vector<float> last;
    std::wstring key;    // "app:<exe>" or "dev:<GUID>".
    std::wstring app;    // The executable name, for an app.
    std::wstring title;  // What it belongs to.
    RECT closeRect{};
    RECT cardRect{};
    bool closeHot = false;
    bool pending = false;
    ULONGLONG savedAt = 0;
};
LoudMaxEditor g_loudMaxEditor;
constexpr PCWSTR LOUDMAX_EDITOR_CLASS = L"HeadroomLoudMax";

void SaveLoudMaxEditor(bool now) {
    LoudMaxEditor& editor = g_loudMaxEditor;
    if (!editor.pending || (!now && GetTickCount64() - editor.savedAt < 200)) return;
    editor.pending = false;
    editor.savedAt = GetTickCount64();
    LoudMaxState state = LoadLoudMax(editor.key);
    state.on = true;  // Setting it up turns it on.
    state.params.clear();
    for (size_t i = 0; i < editor.names.size(); ++i) {
        if (editor.names[i].empty() || editor.names[i] == "Large GUI") continue;
        state.params.emplace_back(editor.names[i], editor.last[i]);
    }
    SaveLoudMax(editor.key, state);
    if (!editor.app.empty()) {
        FxSettingsChanged(editor.app);
    } else {
        AddEqualizerDevice(editor.key.substr(4));
        WriteEqualizerConfig();
    }
}

// The frame around LoudMax's own panel: a header like the mixer's, and the
// plugin sitting on a card.
void PaintLoudMaxEditor(HWND window) {
    LoudMaxEditor& editor = g_loudMaxEditor;
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
    RECT title{Scale(24), Scale(11), editor.closeRect.left - Scale(8), Scale(39)};
    DrawLabel(dc, L"LoudMax", title, g_titleFont, g_theme.text,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT subtitle{Scale(24), Scale(40), editor.closeRect.left - Scale(8), Scale(60)};
    DrawLabel(dc, editor.title, subtitle, g_smallFont, g_theme.secondaryText,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (editor.closeHot) DrawSurface(dc, editor.closeRect, Radius(8), g_theme.panel, 1.0f);
    DrawLabel(dc, L"", editor.closeRect, g_symbolFont, IconColor(),
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawSurface(dc, editor.cardRect, Radius(16), g_theme.panel);
    g_paintMatte = matte;
    BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    EndPaint(window, &paint);
}

LRESULT CALLBACK LoudMaxEditorProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    LoudMaxEditor& editor = g_loudMaxEditor;
    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            PaintLoudMaxEditor(window);
            return 0;
        case WM_NCHITTEST: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(window, &point);
            if (point.y < Scale(68) && !PtInRect(&editor.closeRect, point)) return HTCAPTION;
            return HTCLIENT;
        }
        case WM_MOUSEMOVE: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            bool hot = PtInRect(&editor.closeRect, point) != FALSE;
            if (hot != editor.closeHot) {
                editor.closeHot = hot;
                InvalidateRect(window, &editor.closeRect, FALSE);
            }
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
            TrackMouseEvent(&track);
            return 0;
        }
        case WM_MOUSELEAVE:
            if (editor.closeHot) {
                editor.closeHot = false;
                InvalidateRect(window, &editor.closeRect, FALSE);
            }
            return 0;
        case WM_LBUTTONUP: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (PtInRect(&editor.closeRect, point)) DestroyWindow(window);
            return 0;
        }
        case WM_TIMER:
            if (!editor.effect) break;
            editor.effect->dispatcher(editor.effect, VST_EDIT_IDLE, 0, 0, nullptr, 0.0f);
            for (size_t i = 0; i < editor.names.size(); ++i) {
                float value = editor.effect->getParameter(editor.effect, static_cast<int32_t>(i));
                if (value != editor.last[i]) {
                    editor.last[i] = value;
                    editor.pending = true;
                }
            }
            SaveLoudMaxEditor(false);
            return 0;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            KillTimer(window, 1);
            SaveLoudMaxEditor(true);
            if (editor.effect) {
                editor.effect->dispatcher(editor.effect, VST_EDIT_CLOSE, 0, 0, nullptr, 0.0f);
                CloseLoudMax(editor.effect);
            }
            editor = {};
            if (HWND mixer = g_hWnd.load()) RefreshSourceList(mixer);
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void OpenLoudMaxEditor(HWND owner, const std::wstring& key, const std::wstring& app,
                       const std::wstring& title) {
    if (g_loudMaxEditor.window) {
        SetForegroundWindow(g_loudMaxEditor.window);
        return;
    }
    VstEffect* effect = CreateLoudMax(48000.0);
    if (!effect) {
        MessageBoxW(owner, L"LoudMax could not be loaded from Equalizer APO's VSTPlugins folder.",
                    L"Headroom", MB_OK | MB_ICONWARNING);
        return;
    }
    LoudMaxEditor& editor = g_loudMaxEditor;
    editor.effect = effect;
    editor.key = key;
    editor.app = app;
    editor.title = title;
    editor.names = LoudMaxParamNames(effect);
    ApplyLoudMaxParams(effect, editor.names, LoadLoudMax(key));
    for (size_t i = 0; i < editor.names.size(); ++i) {
        editor.last.push_back(effect->getParameter(effect, static_cast<int32_t>(i)));
    }

    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.lpfnWndProc = LoudMaxEditorProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = LOUDMAX_EDITOR_CLASS;
    RegisterClassExW(&windowClass);  // Fails harmlessly the second time.

    POINT cursor{};
    GetCursorPos(&cursor);
    editor.window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, LOUDMAX_EDITOR_CLASS,
                                    L"LoudMax", WS_POPUP | WS_CLIPCHILDREN, cursor.x - Scale(200),
                                    cursor.y - Scale(150), Scale(400), Scale(300), nullptr,
                                    nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!editor.window) {
        CloseLoudMax(effect);
        editor = {};
        return;
    }
    editor.host = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, Scale(400),
                                  Scale(300), editor.window, nullptr, GetModuleHandleW(nullptr),
                                  nullptr);
    effect->dispatcher(effect, VST_EDIT_OPEN, 0, 0, editor.host, 0.0f);
    // The size of the plugin's panel, known once it is open.
    VstRect* size = nullptr;
    effect->dispatcher(effect, VST_EDIT_GET_RECT, 0, 0, &size, 0.0f);
    int hostWidth = Scale(330), hostHeight = Scale(190);
    if (size && size->right > size->left && size->bottom > size->top) {
        hostWidth = size->right - size->left;
        hostHeight = size->bottom - size->top;
    }
    int pad = Scale(10), margin = Scale(16), header = Scale(68);
    int cardWidth = hostWidth + 2 * pad;
    int width = std::max(cardWidth + 2 * margin, Scale(300));
    int cardLeft = (width - cardWidth) / 2;
    editor.cardRect = {cardLeft, header, cardLeft + cardWidth, header + hostHeight + 2 * pad};
    editor.closeRect = {width - Scale(47), Scale(9), width - Scale(15), Scale(41)};
    int height = editor.cardRect.bottom + margin;
    MoveWindow(editor.host, cardLeft + pad, header + pad, hostWidth, hostHeight, TRUE);

    MONITORINFO info = {sizeof(info)};
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &info);
    int x = std::clamp<int>(cursor.x - width / 2, info.rcWork.left,
                            std::max<int>(info.rcWork.left, info.rcWork.right - width));
    int y = std::clamp<int>(cursor.y - height / 2, info.rcWork.top,
                            std::max<int>(info.rcWork.top, info.rcWork.bottom - height));
    SetWindowPos(editor.window, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    ApplyWindowFrame(editor.window);
    SetTimer(editor.window, 1, 50, nullptr);
    ShowWindow(editor.window, SW_SHOWNORMAL);
    SetForegroundWindow(editor.window);
}

// ---------------------------------------------------------------------------
// The (i) marks: a short explanation in a small card while the pointer is on
// one.

struct InfoTip {
    HWND window = nullptr;
    std::wstring text;
};
InfoTip g_infoTip;
constexpr PCWSTR INFO_TIP_CLASS = L"HeadroomInfoTip";

void PaintInfoTip(HWND window) {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window, &paint);
    RECT client;
    GetClientRect(window, &client);
    int matte = g_paintMatte;
    g_paintMatte = -1;
    FillSolidRect(dc, client, MixColor(g_theme.panel, g_theme.text, 0.05f));
    RECT text = client;
    InflateRect(&text, -Scale(14), -Scale(11));
    HGDIOBJ old = SelectObject(dc, g_smallFont);
    SetTextColor(dc, g_theme.text);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, g_infoTip.text.c_str(), -1, &text, DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
    g_paintMatte = matte;
    EndPaint(window, &paint);
}

LRESULT CALLBACK InfoTipProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            PaintInfoTip(window);
            return 0;
        case WM_NCHITTEST:
            return HTTRANSPARENT;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void HideInfoTip() {
    if (g_infoTip.window) {
        DestroyWindow(g_infoTip.window);
        g_infoTip.window = nullptr;
    }
}

// `anchor` is the (i) mark, in screen coordinates.
void ShowInfoTip(HWND owner, const RECT& anchor, const std::wstring& text) {
    HideInfoTip();
    if (text.empty()) return;
    // The tip is made at the scale of the window it explains (the settings window has its own).
    struct Awareness {
        DPI_AWARENESS_CONTEXT previous;
        explicit Awareness(HWND window)
            : previous(SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(window))) {}
        ~Awareness() { SetThreadDpiAwarenessContext(previous); }
    } awareness(owner);
    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.lpfnWndProc = InfoTipProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = INFO_TIP_CLASS;
    RegisterClassExW(&windowClass);  // Fails harmlessly the second time.

    g_infoTip.text = text;
    int textWidth = Scale(300);
    HDC dc = GetDC(nullptr);
    HGDIOBJ old = SelectObject(dc, g_smallFont);
    RECT measured{0, 0, textWidth, 0};
    DrawTextW(dc, text.c_str(), -1, &measured, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);
    int width = textWidth + 2 * Scale(14);
    int height = (measured.bottom - measured.top) + 2 * Scale(11);

    MONITORINFO info = {sizeof(info)};
    GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &info);
    int x = std::clamp<int>(anchor.left - width / 2, info.rcWork.left,
                            std::max<int>(info.rcWork.left, info.rcWork.right - width));
    int y = anchor.bottom + Scale(6);
    if (y + height > info.rcWork.bottom) y = anchor.top - Scale(6) - height;
    g_infoTip.window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                       INFO_TIP_CLASS, L"", WS_POPUP, x, y, width, height, owner,
                                       nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!g_infoTip.window) return;
    ApplyWindowFrame(g_infoTip.window);
    ShowWindow(g_infoTip.window, SW_SHOWNOACTIVATE);
}

// The mark itself: a small circled i.
void DrawInfoMark(HDC dc, const RECT& rect, bool hot) {
    DrawLabel(dc, L"\uE946", rect, g_symbolFont, hot ? g_theme.text : g_theme.secondaryText,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// ---------------------------------------------------------------------------
// The equalizer panel. It reads like the mixer: a header, the preset picker
// where the mixer has its output picker, and rows like the mixer's rows for
// bass, mids and treble, LoudMax and loudness equalization. The finer bands
// fold out under "Advanced".

struct EqualizerPanel {
    HWND window = nullptr;
    std::wstring app;       // An app's executable name, for its own equalizer.
    std::wstring guid;      // Or an output, through Equalizer APO.
    std::wstring subtitle;  // What the equalizer belongs to.
    EqualizerState state;
    bool effects = false;   // The effects page (LoudMax, loudness) instead of the bands.
    int drag = -1;          // The band being dragged.
    int hover = -1;         // The band under the pointer.
    int focus = -1;         // The band the arrow keys move.
    int control = -1;       // The button under the pointer, see EqControl.
    bool naming = false;    // Typing a name for a new preset.
    std::wstring name;
    bool loudMaxShown = false;  // LoudMax is installed: the panel has its row.
    bool loudMaxUsable = false; // ... and can run here.
    std::wstring loudMaxNote;
    LoudMaxState loudMax;
    bool openLoudMax = false;   // Its settings were asked for; open after closing.
    LoudnessState loudness;     // Apps only.
    int expertDrag = -1;        // The expert value being dragged.
    int channelDrag = -1;       // The surround channel being dragged.
    ULONGLONG channelSaved = 0;
    bool namingLoud = false;    // The name being typed is for a loudness preset.
    ChannelMixState channels;   // Apps only: which surround channels are left out.
    DeviceGain gain;            // Outputs, and inputs listened to through the mixer.
    bool input = false;         // An input: its settings are kept like an app's, under "in:<GUID>".
    bool monitorOn = false;
    bool gainDrag = false;
    // The page that lines up two outputs (the second output of an output or an app): a test sound, a slider, steps.
    bool sync = false;
    std::wstring mirrorKey;     // Its settings name: "devMirror_v1:<GUID>" or "appMirror_v1:<app>".
    std::wstring mirrorApp;     // The app (or input) it belongs to; empty for an output.
    std::wstring mirrorGuid;    // The output's GUID, for an output.
    std::wstring syncFirst;     // The id of the output the sound plays on first.
    std::wstring firstName, mirrorName;
    DeviceMirror mirror;
    bool mirrorDrag = false;
    bool testing = false;       // The test sound plays.
    ULONGLONG mirrorSaved = 0;
    ULONGLONG gainSaved = 0;
    ULONGLONG expertSaved = 0;
    bool dirty = false;
    ULONGLONG written = 0;
    bool failed = false;
};
EqualizerPanel g_eq;
PixelBuffer g_eqBlack, g_eqWhite;

enum EqControl {
    EQ_CLOSE, EQ_TOGGLE, EQ_PRESETS, EQ_LM_TOGGLE, EQ_LM_SETTINGS,
    EQ_LOUDNESS_TOGGLE, EQ_RELEASE_FIRST, EQ_LAYOUT_FIRST = EQ_RELEASE_FIRST + 6,
    EQ_EXPERT_RESET = EQ_LAYOUT_FIRST + 16, EQ_LOUD_PRESETS, EQ_EXPERT_FIRST,
    EQ_CHANNELS_TOGGLE = EQ_EXPERT_FIRST + LOUD_PARAMS, EQ_CHANNEL_FIRST,
    EQ_GAIN_TOGGLE = EQ_CHANNEL_FIRST + SURROUND_CHANNELS, EQ_GAIN_SLIDER, EQ_MONITOR_TOGGLE,
    EQ_SYNC_TEST, EQ_MIRROR_SLIDER, EQ_SYNC_NUDGE_FIRST, EQ_SYNC_NUDGE_END = EQ_SYNC_NUDGE_FIRST + 5,
    EQ_INFO_LOUDMAX, EQ_INFO_LOUDNESS, EQ_INFO_CHANNELS, EQ_INFO_GAIN, EQ_INFO_MONITOR, EQ_INFO_FIRST,
};

// Where everything sits, for the current page.
struct EqGeometry {
    RECT close, toggle, presets;
    RECT strip;            // The band counts.
    RECT readout;
    RECT surface;          // The faders.
    RECT loudMaxRow, loudMaxSettings, loudMaxToggle;
    RECT loudnessRow, loudnessToggle, release[6];
    RECT expertHeader, expertReset, expert[LOUD_PARAMS];
    RECT loudCaption, loudPresets;
    RECT infoLoudMax, infoLoudness, info[LOUD_PARAMS];  // The (i) marks.
    RECT channelsRow, channelsToggle, infoChannels, channelCaption, channelChip[SURROUND_CHANNELS];
    RECT gainRow, gainToggle, infoGain, gainValue, gainTrack, gainNote;
    RECT monitorRow, monitorToggle, infoMonitor, monitorNote;
    RECT syncText, syncNames, syncTest, mirrorValue, mirrorTrack, syncNudge[5], syncHint;
    RECT footer;
    int height;
};

// The 31-band layout stays for curves saved by earlier versions; it is no longer offered.
constexpr int EQ_STRIP_LAYOUTS = static_cast<int>(ARRAYSIZE(EQ_LAYOUTS)) - 1;
constexpr int EQ_SEGMENT = 52;  // One band count in the strip.

bool EqHasEffects() {
    return true;  // Outputs have a gain, apps have more.
}

EqGeometry EqMeasure(int width) {
    EqGeometry at{};
    at.close = {width - Scale(47), Scale(9), width - Scale(15), Scale(41)};
    int y = Scale(68);
    if (g_eq.sync) {
        at.syncText = {Scale(24), y, width - Scale(24), y + Scale(40)};
        at.syncNames = {Scale(24), y + Scale(46), width - Scale(24), y + Scale(86)};
        at.syncTest = {Scale(24), y + Scale(96), width - Scale(24), y + Scale(136)};
        at.mirrorValue = {Scale(24), y + Scale(148), width - Scale(24), y + Scale(172)};
        at.mirrorTrack = {Scale(24), y + Scale(176), width - Scale(24), y + Scale(198)};
        int gap = Scale(6);
        int buttonWidth = (width - Scale(48) - 4 * gap) / 5;
        for (int i = 0; i < 5; ++i) {
            int left = Scale(24) + i * (buttonWidth + gap);
            at.syncNudge[i] = {left, y + Scale(208), left + buttonWidth, y + Scale(244)};
        }
        at.syncHint = {Scale(24), y + Scale(252), width - Scale(24), y + Scale(292)};
        y += Scale(298);
    } else if (g_eq.effects) {
        if (g_eq.input) {
            // Listening to the input through the mixer: a switch and a note.
            at.monitorRow = {Scale(12), y, width - Scale(12), y + Scale(92)};
            int middle = y + Scale(26);
            at.monitorToggle = {width - Scale(66), middle - Scale(10), width - Scale(26), middle + Scale(10)};
            at.infoMonitor = {at.monitorToggle.left - Scale(30), middle - Scale(9), at.monitorToggle.left - Scale(12), middle + Scale(9)};
            at.monitorNote = {Scale(28), y + Scale(48), width - Scale(28), y + Scale(86)};
            y += Scale(96);
        }
        if (g_eq.loudMaxShown) {
            at.loudMaxRow = {Scale(12), y, width - Scale(12), y + Scale(52)};
            int middle = y + Scale(26);
            at.loudMaxToggle = {width - Scale(66), middle - Scale(10), width - Scale(26),
                                middle + Scale(10)};
            at.loudMaxSettings = {width - Scale(160), y + Scale(10), width - Scale(76),
                                  y + Scale(42)};
            at.infoLoudMax = {Scale(12) + Scale(84), middle - Scale(9), Scale(12) + Scale(102),
                              middle + Scale(9)};
            y += Scale(56);
        }
        if (g_eq.app.empty() || g_eq.input) {
            // The gain of the output: a switch, the value, and a slider.
            at.gainRow = {Scale(12), y, width - Scale(12), y + Scale(112)};
            int middle = y + Scale(26);
            at.gainToggle = {width - Scale(66), middle - Scale(10), width - Scale(26), middle + Scale(10)};
            at.infoGain = {Scale(12) + Scale(64), middle - Scale(9), Scale(12) + Scale(82), middle + Scale(9)};
            at.gainValue = {at.gainToggle.left - Scale(110), y + Scale(10), at.gainToggle.left - Scale(10), y + Scale(42)};
            at.gainTrack = {Scale(28), y + Scale(52), width - Scale(28), y + Scale(74)};
            at.gainNote = {Scale(28), y + Scale(78), width - Scale(28), y + Scale(102)};
            y += Scale(116);
        }
        if (!g_eq.app.empty()) {
            bool custom = g_eq.loudness.release == 4;
            int rowHeight = Scale(100) + (custom ? Scale(44 + 26 + 30 * LOUD_PARAMS + 10) : 0);
            at.loudnessRow = {Scale(12), y, width - Scale(12), y + rowHeight};
            at.loudCaption = {Scale(28), y + Scale(38), width - Scale(28), y + Scale(56)};
            int top = y + Scale(58);
            int segmentWidth = (width - Scale(56)) / 6;
            for (int i = 0; i < 6; ++i) {
                int left = Scale(28) + i * segmentWidth;
                at.release[i] = {left, top, left + segmentWidth, top + Scale(30)};
            }
            int middle = y + Scale(22);
            at.loudnessToggle = {width - Scale(66), middle - Scale(10), width - Scale(26),
                                 middle + Scale(10)};
            at.infoLoudness = {at.loudnessToggle.left - Scale(30), middle - Scale(9),
                               at.loudnessToggle.left - Scale(12), middle + Scale(9)};
            if (custom) {
                int presetTop = y + Scale(100);
                at.loudPresets = {Scale(28), presetTop, width - Scale(28), presetTop + Scale(36)};
                int headTop = presetTop + Scale(44);
                at.expertHeader = {Scale(28), headTop, width - Scale(28), headTop + Scale(26)};
                at.expertReset = {width - Scale(96), headTop, width - Scale(28), headTop + Scale(24)};
                for (int i = 0; i < LOUD_PARAMS; ++i) {
                    int rowTop = headTop + Scale(26) + i * Scale(30);
                    at.expert[i] = {Scale(24), rowTop, width - Scale(24), rowTop + Scale(30)};
                    int center = rowTop + Scale(15);
                    at.info[i] = {Scale(24) + Scale(122), center - Scale(9), Scale(24) + Scale(140),
                                  center + Scale(9)};
                }
            }
            y += rowHeight + Scale(4);
            if (!g_eq.input) {
            // The surround channels: a switch, and one chip for each channel.
            at.channelsRow = {Scale(12), y, width - Scale(12), y + Scale(172)};
            int channelsMiddle = y + Scale(22);
            at.channelsToggle = {width - Scale(66), channelsMiddle - Scale(10), width - Scale(26),
                                 channelsMiddle + Scale(10)};
            at.infoChannels = {at.channelsToggle.left - Scale(30), channelsMiddle - Scale(9),
                               at.channelsToggle.left - Scale(12), channelsMiddle + Scale(9)};
            at.channelCaption = {Scale(28), y + Scale(38), width - Scale(28), y + Scale(56)};
            int chipWidth = (width - Scale(56)) / SURROUND_CHANNELS;
            for (int i = 0; i < SURROUND_CHANNELS; ++i) {
                int left = Scale(28) + i * chipWidth;
                at.channelChip[i] = {left + Scale(2), y + Scale(58), left + chipWidth - Scale(2), y + Scale(164)};
            }
            y += Scale(176);
            }
        }
        y = std::max(y, Scale(394));  // As tall as the equalizer.
    } else {
        at.toggle = {width - Scale(100), Scale(15), width - Scale(60), Scale(35)};
        at.presets = {Scale(12), y, width - Scale(12), y + Scale(40)};
        y += Scale(48);
        at.strip = {Scale(12), y, Scale(12) + EQ_STRIP_LAYOUTS * Scale(EQ_SEGMENT), y + Scale(30)};
        at.readout = {at.strip.right + Scale(8), y, width - Scale(16), y + Scale(30)};
        y += Scale(38);
        at.surface = {Scale(12), y, width - Scale(12), y + Scale(232)};
        y += Scale(240);
    }
    at.footer = {Scale(24), y, width - Scale(24), y + Scale(26)};
    at.height = y + Scale(34);
    return at;
}

EqGeometry g_eqAt;
int g_eqWidth;
int g_eqHomeTop;  // The panel's top when it opened.

const wchar_t* EqInfoText(int control) {
    static const wchar_t* params[LOUD_PARAMS] = {
        L"How long the sound is averaged to judge how loud it is. Short (2-20 ms) reacts to single "
        L"sounds such as shots, but can pump on deep bass. Long (300 ms and more) follows the overall "
        L"loudness and sounds calmer. Keep it shorter than the reaction time wants to be fast. "
        L"For games with sudden loud effects try 10-30 ms.",
        L"How fast the volume is turned down once something gets too loud. 1-10 ms catches shots "
        L"almost at once; longer lets the first moment through. Very short values can make loud "
        L"sounds feel cut off.",
        L"How slowly the volume comes back up afterwards. 100-300 ms is quick, but quiet background "
        L"noise may surge; 2-6 s is smooth. If the sound seems to pump up and down, raise it.",
        L"The most the volume is ever turned down, in dB. 6 dB is about half the amplitude. 12 dB "
        L"tames loud shots strongly; too much makes everything feel flat.",
        L"The most quiet parts are raised, in dB. 12 dB helps to hear footsteps, but raises noise "
        L"too. Set 0 to only turn loud sounds down.",
        L"The loudness the app is steered to (RMS). Higher, such as -12 dB, is louder overall; lower, "
        L"such as -24 dB, is quieter and leaves room for peaks. The speaker volume still decides the "
        L"final loudness.",
        L"The sound is delayed by this long, so the volume can already be down when a loud sound "
        L"arrives. 3-6 ms is enough for shots. It adds that delay to the sound (on top of the "
        L"usual 30 ms). 0 turns it off.",
        L"Below this level the volume is left as it is, so silence is not raised into hiss. -50 dB "
        L"suits most. If noise gets louder in quiet moments, raise it towards -40 dB.",
    };
    if (control == EQ_INFO_LOUDMAX) {
        return L"A limiter: it keeps peaks from going over a ceiling without crackling. Turn it on, "
               L"then open Settings to set the threshold (lower is quieter and safer) and the "
               L"output level. Needs LoudMax64.dll; see Setup guide in the app's settings.";
    }
    if (control == EQ_INFO_CHANNELS) {
        return L"Many games play surround sound (5.1 or 7.1) when Windows offers it, and put some sounds on a "
               L"single channel. Set the volume of a channel, from 0 (off) to 200 %, before the sound is mixed down to your "
               L"headphones or speakers. FL and FR are the front, C the center, LFE the bass, BL and BR the "
               L"back, SL and SR the sides. It only works for an app that plays 5.1 or 7.1: set the speakers "
               L"of the game to surround, and start the game after turning this on (a game reads the speaker setup "
               L"when it starts). While it is on, the silent VB-CABLE output is set to 7.1; it goes "
               L"back to stereo when no app uses it. Apps that play stereo are not changed.";
    }
    if (control == EQ_INFO_MONITOR) {
        return L"Headroom records this input, applies the equalizer and the effects below, and plays it on your "
               L"default output, about 40 ms later (a little less with the Comp loudness mode). Use it for a "
               L"line-in, for example a console. If Windows is already playing the input (the Listen to this "
               L"device setting of the input), turn that off, or you hear it twice.";
    }
    if (control == EQ_INFO_GAIN) {
        return L"Raises everything that plays on this output, by up to 12 dB, for a device that is too "
               L"quiet. With Equalizer APO it works for all sound, system sounds included. With the mixer's own "
               L"engine it works for apps that run through the mixer. Sound above full scale would crackle, so "
               L"a limiter catches the peaks: the mixer's own soft limiter, or LoudMax when it is on. Above about "
               L"+6 dB loud sounds start to sound squashed. The volumes in Windows and in the app still decide the "
               L"final loudness: try it quietly first.";
    }
    if (control == EQ_INFO_LOUDNESS) {
        return L"Keeps this app's volume even: loud sounds are turned down, quiet ones raised. Choose "
               L"a ready-made speed (Comp is made for games: quiet footsteps come up, shots and explosions are cut "
               L"within milliseconds, and its output queue is the shortest, about 6 ms less delay), or Custom to set every value yourself "
               L"and save it as a preset for other apps. Only for apps that run through the mixer "
               L"(needs VB-CABLE).";
    }
    if (control >= EQ_INFO_FIRST && control < EQ_INFO_FIRST + LOUD_PARAMS) {
        return params[control - EQ_INFO_FIRST];
    }
    return L"";
}

// Shows or hides the explanation for the (i) mark under the pointer.
void UpdateEqInfoTip(int control) {
    if (control < EQ_INFO_LOUDMAX || !g_eq.window) {
        HideInfoTip();
        return;
    }
    if (control != EQ_INFO_LOUDMAX && control != EQ_INFO_LOUDNESS && control != EQ_INFO_CHANNELS && control != EQ_INFO_GAIN && control != EQ_INFO_MONITOR &&
        (control < EQ_INFO_FIRST || control >= EQ_INFO_FIRST + LOUD_PARAMS)) {
        HideInfoTip();
        return;
    }
    RECT rect = control == EQ_INFO_LOUDMAX ? g_eqAt.infoLoudMax
              : control == EQ_INFO_LOUDNESS ? g_eqAt.infoLoudness
              : control == EQ_INFO_CHANNELS ? g_eqAt.infoChannels
              : control == EQ_INFO_GAIN ? g_eqAt.infoGain
              : control == EQ_INFO_MONITOR ? g_eqAt.infoMonitor
                                            : g_eqAt.info[control - EQ_INFO_FIRST];
    MapWindowPoints(g_eq.window, nullptr, reinterpret_cast<POINT*>(&rect), 2);
    ShowInfoTip(g_eq.window, rect, EqInfoText(control));
}

std::wstring EqLoudMaxKey() {
    return g_eq.app.empty() ? L"dev:" + g_eq.guid : L"app:" + g_eq.app;
}

// The columns of the faders, right of the dB scale.
void EqColumns(int* left, int* right, int* top, int* bottom) {
    const RECT& surface = g_eqAt.surface;
    *left = surface.left + Scale(36);
    *right = surface.right - Scale(10);
    *top = surface.top + Scale(18);
    *bottom = surface.bottom - Scale(32);
}

RECT EqBandRect(int band) {
    int left, right, top, bottom;
    EqColumns(&left, &right, &top, &bottom);
    int count = g_eq.state.bands;
    int x0 = left + (right - left) * band / count;
    int x1 = left + (right - left) * (band + 1) / count;
    return {x0, g_eqAt.surface.top, x1, g_eqAt.surface.bottom};
}

int EqYForGain(int gain, int top, int bottom) {
    int limit = EqLimit();
    gain = ClampInt(gain, -limit, limit);  // A band saved with a larger range sits at the end of the fader.
    return bottom - (gain + limit) * (bottom - top) / (2 * limit);
}

float EqBandX(int band) {
    RECT rect = EqBandRect(band);
    return (rect.left + rect.right) / 2.0f;
}

int EqBandAt(POINT point) {
    if (g_eq.effects || !PtInRect(&g_eqAt.surface, point)) return -1;
    for (int band = 0; band < g_eq.state.bands; ++band) {
        RECT rect = EqBandRect(band);
        if (PtInRect(&rect, point)) return band;
    }
    return -1;
}

int EqControlAt(POINT point) {
    const EqGeometry& at = g_eqAt;
    if (PtInRect(&at.close, point)) return EQ_CLOSE;
    if (g_eq.sync) {
        if (PtInRect(&at.syncTest, point)) return EQ_SYNC_TEST;
        if (PtInRect(&at.mirrorTrack, point)) return EQ_MIRROR_SLIDER;
        for (int i = 0; i < 5; ++i) {
            if (PtInRect(&at.syncNudge[i], point)) return EQ_SYNC_NUDGE_FIRST + i;
        }
        return -1;
    }
    if (g_eq.effects) {
        if (g_eq.input) {
            if (PtInRect(&at.monitorToggle, point)) return EQ_MONITOR_TOGGLE;
            if (PtInRect(&at.infoMonitor, point)) return EQ_INFO_MONITOR;
        }
        if (g_eq.loudMaxShown && g_eq.loudMaxUsable) {
            if (PtInRect(&at.loudMaxToggle, point)) return EQ_LM_TOGGLE;
            if (PtInRect(&at.infoLoudMax, point)) return EQ_INFO_LOUDMAX;
            if (PtInRect(&at.loudMaxSettings, point)) return EQ_LM_SETTINGS;
        }
        if (g_eq.app.empty() || g_eq.input) {
            if (PtInRect(&at.gainToggle, point)) return EQ_GAIN_TOGGLE;
            if (PtInRect(&at.infoGain, point)) return EQ_INFO_GAIN;
            if (PtInRect(&at.gainTrack, point)) return EQ_GAIN_SLIDER;
        }
        if (!g_eq.app.empty()) {
            if (PtInRect(&at.loudnessToggle, point)) return EQ_LOUDNESS_TOGGLE;
            for (int i = 0; i < 6; ++i) {
                if (PtInRect(&at.release[i], point)) return EQ_RELEASE_FIRST + i;
            }
            if (PtInRect(&at.infoLoudness, point)) return EQ_INFO_LOUDNESS;
            if (!g_eq.input && PtInRect(&at.channelsToggle, point)) return EQ_CHANNELS_TOGGLE;
            if (!g_eq.input && PtInRect(&at.infoChannels, point)) return EQ_INFO_CHANNELS;
            for (int i = 0; i < SURROUND_CHANNELS && !g_eq.input; ++i) {
                if (PtInRect(&at.channelChip[i], point)) return EQ_CHANNEL_FIRST + i;
            }
            if (g_eq.loudness.release == 4) {
                if (PtInRect(&at.expertReset, point)) return EQ_EXPERT_RESET;
                if (PtInRect(&at.loudPresets, point)) return EQ_LOUD_PRESETS;
                for (int i = 0; i < LOUD_PARAMS; ++i) {
                    if (PtInRect(&at.info[i], point)) return EQ_INFO_FIRST + i;
                }
                for (int i = 0; i < LOUD_PARAMS; ++i) {
                    RECT track = at.expert[i];
                    track.left += Scale(150);
                    if (PtInRect(&track, point)) return EQ_EXPERT_FIRST + i;
                }
            }
        }
        return -1;
    }
    if (PtInRect(&at.toggle, point)) return EQ_TOGGLE;
    if (PtInRect(&at.presets, point)) return EQ_PRESETS;
    for (int i = 0; i < EQ_STRIP_LAYOUTS; ++i) {
        int left = at.strip.left + i * Scale(EQ_SEGMENT);
        RECT segment{left, at.strip.top, left + Scale(EQ_SEGMENT), at.strip.bottom};
        if (PtInRect(&segment, point)) return EQ_LAYOUT_FIRST + i;
    }
    return -1;
}

std::wstring EqGainText(int gain) {
    wchar_t text[16];
    if (gain % 10) {
        swprintf_s(text, L"%+.1f dB", gain / 10.0);
    } else {
        swprintf_s(text, gain ? L"%+d dB" : L"%d dB", gain / 10);
    }
    return text;
}
std::wstring EqFrequencyText(float frequency, bool unit) {
    wchar_t text[24];
    if (frequency >= 1000.0f) {
        swprintf_s(text, unit ? L"%g kHz" : L"%gk", frequency / 1000.0f);
    } else {
        swprintf_s(text, unit ? L"%g Hz" : L"%g", frequency);
    }
    return text;
}
void EqCapsule(HDC dc, Gdiplus::Graphics* graphics, float x, float y, float width, float height,
               COLORREF color, BYTE alpha = 255) {
    if (width <= 0.0f || height <= 0.0f) return;
    float diameter = std::min(width, height);
    if (graphics) {
        Gdiplus::GraphicsPath path;
        path.AddArc(x, y, diameter, diameter, 180.0f, 90.0f);
        path.AddArc(x + width - diameter, y, diameter, diameter, 270.0f, 90.0f);
        path.AddArc(x + width - diameter, y + height - diameter, diameter, diameter, 0.0f, 90.0f);
        path.AddArc(x, y + height - diameter, diameter, diameter, 90.0f, 90.0f);
        path.CloseFigure();
        Gdiplus::SolidBrush brush(
            Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color)));
        graphics->FillPath(&brush, &path);
        return;
    }
    if (alpha < 255) color = MixColor(g_theme.background, color, alpha / 255.0f);
    RECT bounds = {static_cast<LONG>(std::lround(x)), static_cast<LONG>(std::lround(y)),
                   static_cast<LONG>(std::lround(x + width)),
                   static_cast<LONG>(std::lround(y + height))};
    DrawRoundedRect(dc, bounds, std::max(1, static_cast<int>(std::lround(diameter))), color);
}
void DrawEqSlider(HDC dc, Gdiplus::Graphics* graphics, float x, float top, float bottom,
                  float zero, float value, float maxThumb, ThumbState state, bool enabled) {
    float density = g_dpi / 96.0f;
    bool windows10 = IsWindows10Style();
    COLORREF overlay = g_lightTheme ? RGB(0, 0, 0) : RGB(255, 255, 255);
    BYTE railAlpha = !enabled ? (g_lightTheme ? 0x51 : 0x3F) : (g_lightTheme ? 0x72 : 0x8B);
    if (windows10) railAlpha = !enabled ? 0x33 : state == ThumbState::Hover ? 0x99 : 0x66;
    COLORREF fill = enabled ? g_theme.sliderFill : g_theme.muted;
    float track = (windows10 ? 2.0f : 4.0f) * density;
    EqCapsule(dc, graphics, x - track / 2, top, track, bottom - top, overlay, railAlpha);
    EqCapsule(dc, graphics, x - track / 2, std::min(zero, value), track,
              std::fabs(zero - value), fill);

    if (windows10) {
        COLORREF thumb = fill;
        if (enabled && state == ThumbState::Hover) {
            thumb = g_lightTheme ? RGB(23, 23, 23) : RGB(242, 242, 242);
        } else if (enabled && state == ThumbState::Pressed) {
            thumb = g_lightTheme ? RGB(204, 204, 204) : RGB(118, 118, 118);
        }
        float length = std::min(SliderThumbHeight(), maxThumb);
        float thickness = SliderThumbWidth();
        EqCapsule(dc, graphics, x - length / 2, value - thickness / 2, length, thickness, thumb);
        return;
    }
    float diameter = std::min(SliderThumbWidth(), maxThumb);
    float radius = diameter / 2;
    float surface = diameter - 2.0f * density;
    if (graphics) {
        BYTE red = GetRValue(overlay), green = GetGValue(overlay), blue = GetBValue(overlay);
        Gdiplus::LinearGradientBrush border(
            Gdiplus::PointF(0.0f, value - radius - 1.0f),
            Gdiplus::PointF(0.0f, value + radius + 1.0f),
            Gdiplus::Color(g_lightTheme ? 0x0F : 0x18, red, green, blue),
            Gdiplus::Color(g_lightTheme ? 0x29 : 0x12, red, green, blue));
        Gdiplus::GraphicsPath ring;
        ring.AddEllipse(x - radius, value - radius, diameter, diameter);
        ring.AddEllipse(x - surface / 2, value - surface / 2, surface, surface);
        graphics->FillPath(&border, &ring);
    }
    EqCapsule(dc, graphics, x - surface / 2, value - surface / 2, surface, surface, g_theme.thumb);
    float dot = diameter * 0.6f * (state == ThumbState::Pressed ? THUMB_DOT_PRESSED
                                   : state == ThumbState::Hover ? THUMB_DOT_HOVER
                                                                : THUMB_DOT_REST);
    EqCapsule(dc, graphics, x - dot / 2, value - dot / 2, dot, dot, fill);
}
void DrawEqToggle(HDC dc, Gdiplus::Graphics* graphics, const RECT& rect, bool on, bool hovered) {
    float density = g_dpi / 96.0f;
    float x = static_cast<float>(rect.left), y = static_cast<float>(rect.top);
    float width = static_cast<float>(rect.right - rect.left);
    float height = static_cast<float>(rect.bottom - rect.top);
    COLORREF overlay = g_lightTheme ? RGB(0, 0, 0) : RGB(255, 255, 255);
    if (on) {
        EqCapsule(dc, graphics, x, y, width, height,
                  hovered ? MixColor(g_theme.accent, g_theme.text, 0.1f) : g_theme.accent);
    } else {
        EqCapsule(dc, graphics, x, y, width, height, overlay, g_lightTheme ? 0x72 : 0x8B);
        EqCapsule(dc, graphics, x + density, y + density, width - 2 * density,
                  height - 2 * density, hovered ? SurfaceColor(g_theme.background, 1.0f)
                                                : g_theme.background);
    }
    float knob = height - (hovered ? 8.0f : 10.0f) * density;
    float knobX = on ? x + width - (height + knob) / 2 : x + (height - knob) / 2;
    EqCapsule(dc, graphics, knobX, y + (height - knob) / 2, knob, knob,
              on ? (g_lightTheme ? RGB(255, 255, 255) : RGB(0, 0, 0)) : g_theme.secondaryText);
}

// The mixer's slider lying down, filled from the left to the value.
void DrawEqHSlider(HDC dc, Gdiplus::Graphics* graphics, float left, float right, float y,
                   float value, ThumbState state, bool enabled) {
    float density = g_dpi / 96.0f;
    bool windows10 = IsWindows10Style();
    COLORREF overlay = g_lightTheme ? RGB(0, 0, 0) : RGB(255, 255, 255);
    BYTE railAlpha = !enabled ? (g_lightTheme ? 0x51 : 0x3F) : (g_lightTheme ? 0x72 : 0x8B);
    if (windows10) railAlpha = !enabled ? 0x33 : state == ThumbState::Hover ? 0x99 : 0x66;
    COLORREF fill = enabled ? g_theme.sliderFill : g_theme.muted;
    float track = (windows10 ? 2.0f : 4.0f) * density;
    float inset = SliderThumbWidth() / 2;
    EqCapsule(dc, graphics, left - inset, y - track / 2, right - left + 2 * inset, track, overlay,
              railAlpha);
    EqCapsule(dc, graphics, left - inset, y - track / 2, value - left + inset, track, fill);
    if (windows10) {
        COLORREF thumb = fill;
        if (enabled && state == ThumbState::Hover) {
            thumb = g_lightTheme ? RGB(23, 23, 23) : RGB(242, 242, 242);
        } else if (enabled && state == ThumbState::Pressed) {
            thumb = g_lightTheme ? RGB(204, 204, 204) : RGB(118, 118, 118);
        }
        EqCapsule(dc, graphics, value - SliderThumbWidth() / 2, y - SliderThumbHeight() / 2,
                  SliderThumbWidth(), SliderThumbHeight(), thumb);
        return;
    }
    // The upright slider draws the same thumb; a zero-length one is just that.
    DrawEqSlider(dc, graphics, value, y, y, y, y, SliderThumbWidth(), state, enabled);
}

// Expert values: the track of one, and how a position maps to a value.
void LoudTrack(int index, float* left, float* right, float* y) {
    const RECT& row = g_eqAt.expert[index];
    float inset = SliderThumbWidth() / 2;
    *left = row.left + Scale(160) + inset;
    *right = row.right - Scale(76) - inset;
    *y = (row.top + row.bottom) / 2.0f;
}
// The track of a channel's volume: from 200 % at the top to 0 at the bottom.
void ChannelTrack(int index, float* top, float* bottom, float* x) {
    const RECT& chip = g_eqAt.channelChip[index];
    float inset = SliderThumbWidth() / 2;
    *top = chip.top + Scale(22) + inset;
    *bottom = chip.bottom - Scale(22) - inset;
    *x = (chip.left + chip.right) / 2.0f;
}

void PaintEqualizer();

// The track of the gain, from 0 on the left to the most on the right.
void GainTrack(float* left, float* right, float* y) {
    const RECT& area = g_eqAt.gainTrack;
    float inset = SliderThumbWidth() / 2;
    *left = area.left + inset;
    *right = area.right - inset;
    *y = (area.top + area.bottom) / 2.0f;
}

// Drags the gain: in steps of half a decibel, saved and handed on at most every 100 ms.
void SetGainFromPoint(POINT point, bool now) {
    float left, right, y;
    GainTrack(&left, &right, &y);
    double t = std::clamp((point.x - left) / std::max(1.0f, right - left), 0.0f, 1.0f);
    g_eq.gain.tenths = static_cast<int>(std::lround(t * (DEVICE_GAIN_MAX / 5))) * 5;
    g_eq.gain.on = g_eq.gain.tenths > 0 ? true : g_eq.gain.on;
    ULONGLONG tick = GetTickCount64();
    if (now || tick - g_eq.gainSaved >= 100) {
        g_eq.gainSaved = tick;
        SaveDeviceGain(g_eq.guid, g_eq.gain);
        if (g_eq.input) {
            FxSettingsChanged(g_eq.app);
        } else {
            AddEqualizerDevice(g_eq.guid);
            g_eq.failed = !WriteEqualizerConfig();
        }
    }
    PaintEqualizer();
}

// Keeps the second output of an output or of an app, and hands it on: the streams pick it up.
void StoreMirror(const std::wstring& key, const std::wstring& app, const std::wstring& guid, const DeviceMirror& mirror) {
    SaveMirrorValue(key, mirror);
    if (!app.empty()) {
        FxSettingsChanged(app);
    } else {
        AddEqualizerDevice(guid);
        WriteEqualizerConfig();
    }
}

void SaveMirror() {
    StoreMirror(g_eq.mirrorKey, g_eq.mirrorApp, g_eq.mirrorGuid, g_eq.mirror);
    g_mirrorTestOffset = g_eq.mirror.offsetMs;  // The test sound follows the slider.
}

std::wstring OutputNameForId(const std::wstring& id) {
    std::wstring name;
    if (id.empty()) return name;
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                                   __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator))) &&
        SUCCEEDED(enumerator->GetDevice(id.c_str(), &device))) {
        name = ShortOutputName(ReadEndpointName(device));
    }
    if (device) device->Release();
    if (enumerator) enumerator->Release();
    return name;
}

// The delay track: the middle is no delay, to the right delays the second output, to the left the first one.
// Close to the middle it is fine (1 ms), further out coarser, where it is the Bluetooth range.
void MirrorTrack(float* left, float* right, float* y) {
    const RECT& area = g_eqAt.mirrorTrack;
    float inset = SliderThumbWidth() / 2;
    *left = area.left + inset;
    *right = area.right - inset;
    *y = (area.top + area.bottom) / 2.0f;
}

float MirrorPosition(int offsetMs) {
    float u = std::sqrt(std::abs(offsetMs) / static_cast<float>(MIRROR_DELAY_MAX));
    return 0.5f + (offsetMs < 0 ? -u : u) / 2.0f;
}

void SetMirrorFromPoint(POINT point, bool now) {
    float left, right, y;
    MirrorTrack(&left, &right, &y);
    float t = std::clamp((point.x - left) / std::max(1.0f, right - left), 0.0f, 1.0f);
    float u = std::abs(t * 2.0f - 1.0f);
    int value = static_cast<int>(std::lround(u * u * MIRROR_DELAY_MAX));
    if (value >= 100) value = (value + 2) / 5 * 5;
    g_eq.mirror.offsetMs = t < 0.5f ? -value : value;
    ULONGLONG tick = GetTickCount64();
    if (now || tick - g_eq.mirrorSaved >= 100) {
        g_eq.mirrorSaved = tick;
        SaveMirror();
    }
    PaintEqualizer();
}

// The small steps under the slider: 10 ms and 1 ms either way, and back to none.
void NudgeMirror(int index) {
    static constexpr int deltas[5] = {-10, -1, 0, 1, 10};
    g_eq.mirror.offsetMs = index == 2 ? 0 : std::clamp(g_eq.mirror.offsetMs + deltas[index], -MIRROR_DELAY_MAX, MIRROR_DELAY_MAX);
    SaveMirror();
    PaintEqualizer();
}

double LoudValueAt(int index, double t) {
    const LoudParam& param = LOUD_PARAM[index];
    t = std::clamp(t, 0.0, 1.0);
    double value = param.logarithmic ? param.low * std::pow(param.high / param.low, t)
                                     : param.low + (param.high - param.low) * t;
    // Round to steps that are easy to read.
    double step = param.kind == 1 || param.high <= 20 ? 0.5
                  : value < 10 ? 0.5 : value < 100 ? 1.0 : value < 1000 ? 5.0 : 50.0;
    return std::clamp(std::round(value / step) * step, param.low, param.high);
}
double LoudPosition(int index, double value) {
    const LoudParam& param = LOUD_PARAM[index];
    double t = param.logarithmic ? std::log(value / param.low) / std::log(param.high / param.low)
                                 : (value - param.low) / (param.high - param.low);
    return std::clamp(t, 0.0, 1.0);
}
std::wstring LoudText(int index, double value) {
    wchar_t text[32];
    if (LOUD_PARAM[index].kind == 1) {
        swprintf_s(text, value == std::floor(value) ? L"%.0f dB" : L"%.1f dB", value);
    } else if (value >= 1000) {
        swprintf_s(text, L"%.1f s", value / 1000.0);
    } else {
        swprintf_s(text, value == std::floor(value) ? L"%.0f ms" : L"%.1f ms", value);
    }
    return text;
}

// A three-way choice, like the band counts.
void DrawEqSegments(HDC dc, const RECT* segments, int count, const PCWSTR* labels, int chosen,
                    int litControl, int firstControl, COLORREF control) {
    RECT strip = segments[0];
    strip.right = segments[count - 1].right;
    DrawSurface(dc, strip, Radius(12), control);
    for (int i = 0; i < count; ++i) {
        RECT segment = segments[i];
        bool selected = i == chosen;
        if (selected || litControl == firstControl + i) {
            RECT inner = segment;
            InflateRect(&inner, -Scale(3), -Scale(3));
            DrawSurface(dc, inner, Radius(9), control, selected ? 1.6f : 1.0f);
        }
        DrawLabel(dc, labels[i], segment, g_bodyFont,
                  selected ? g_theme.text : g_theme.secondaryText,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

void DrawEqualizer(HDC dc, const RECT& client) {
    if (g_paintMatte >= 0) {
        FillSolidRect(dc, client, g_paintMatte ? RGB(255, 255, 255) : RGB(0, 0, 0));
        DrawSurface(dc, client, Radius(20), g_theme.background);
    } else {
        FillSolidRect(dc, client, g_theme.background);
    }
    std::optional<Gdiplus::Graphics> graphics;
    if (g_gdiplusToken) {
        graphics.emplace(dc);
        if (graphics->GetLastStatus() != Gdiplus::Ok ||
            graphics->SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias) != Gdiplus::Ok ||
            graphics->SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf) != Gdiplus::Ok) {
            graphics.reset();
        }
    }
    Gdiplus::Graphics* g = graphics ? &*graphics : nullptr;
    const EqGeometry& at = g_eqAt;
    int width = client.right;
    bool on = g_eq.state.on;
    COLORREF control = MixColor(g_theme.panel, RGB(0, 0, 0), 0.15f);  // The picker's color.
    COLORREF overlay = g_lightTheme ? RGB(0, 0, 0) : RGB(255, 255, 255);

    // Header, as in the mixer. The effects page has a way back instead of the switch.
    RECT title{Scale(24), Scale(11), at.toggle.left - Scale(8), Scale(39)};
    if (g_eq.effects) title.right = at.close.left - Scale(8);
    DrawLabel(dc, g_eq.sync ? L"Sync the outputs" : g_eq.effects ? L"Effects" : L"Equalizer", title, g_titleFont, g_theme.text,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT subtitle{title.left, Scale(40), width - Scale(24), Scale(60)};
    DrawLabel(dc, g_eq.subtitle, subtitle, g_smallFont, g_theme.secondaryText,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (g_eq.control == EQ_CLOSE) DrawSurface(dc, at.close, Radius(8), g_theme.background, 1.0f);
    DrawLabel(dc, L"", at.close, g_symbolFont, IconColor(),
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (!g_eq.effects) DrawEqToggle(dc, g, at.toggle, on, g_eq.control == EQ_TOGGLE);

    if (g_eq.sync) {
        // Lining up two outputs by ear: a test sound on both, a slider, and small steps.
        DrawLabel(dc, L"Both outputs play the same sound. Press the test sound, then move the slider until the two taps "
                      L"are heard as one.",
                  at.syncText, g_smallFont, g_theme.secondaryText, DT_LEFT | DT_TOP | DT_WORDBREAK);
        RECT firstLine = at.syncNames, secondLine = at.syncNames;
        firstLine.bottom = secondLine.top = (at.syncNames.top + at.syncNames.bottom) / 2;
        DrawLabel(dc, L"Low tap: " + (g_eq.firstName.empty() ? std::wstring(L"the first output") : g_eq.firstName), firstLine,
                  g_bodyFont, g_theme.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        DrawLabel(dc, L"High tap: " + (g_eq.mirrorName.empty() ? std::wstring(L"the second output") : g_eq.mirrorName), secondLine,
                  g_bodyFont, g_theme.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        bool testHot = g_eq.control == EQ_SYNC_TEST;
        DrawSurface(dc, at.syncTest, Radius(14), g_eq.testing ? g_theme.accent : control, testHot ? 1.0f : 0.0f);
        DrawLabel(dc, g_eq.testing ? L"Stop the test sound" : L"Play a test sound", at.syncTest, g_bodyFont,
                  g_eq.testing ? RGB(255, 255, 255) : g_theme.text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        int offset = g_eq.mirror.offsetMs;
        wchar_t value[96];
        if (!offset) {
            swprintf_s(value, L"No delay: both start together");
        } else {
            swprintf_s(value, offset > 0 ? L"The second output starts %d ms later" : L"The first output starts %d ms later",
                       std::abs(offset));
        }
        DrawLabel(dc, value, at.mirrorValue, g_bodyFont, g_theme.text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        float left, right, y;
        MirrorTrack(&left, &right, &y);
        float position = left + (right - left) * MirrorPosition(offset);
        ThumbState thumb = g_eq.mirrorDrag ? ThumbState::Pressed
                           : g_eq.control == EQ_MIRROR_SLIDER ? ThumbState::Hover : ThumbState::Rest;
        DrawEqHSlider(dc, g, left, right, y, position, thumb, true);
        static constexpr PCWSTR steps[5] = {L"-10 ms", L"-1 ms", L"Reset", L"+1 ms", L"+10 ms"};
        for (int i = 0; i < 5; ++i) {
            DrawSurface(dc, at.syncNudge[i], Radius(12), control, g_eq.control == EQ_SYNC_NUDGE_FIRST + i ? 1.0f : 0.0f);
            DrawLabel(dc, steps[i], at.syncNudge[i], g_bodyFont, g_theme.text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        DrawLabel(dc, L"If the high tap comes first, move right (or +). If the low tap comes first, move left (or -). "
                      L"The arrow keys change it by 1 ms.",
                  at.syncHint, g_smallFont, g_theme.secondaryText, DT_LEFT | DT_TOP | DT_WORDBREAK);
        DrawLabel(dc, L"Played through the mixer's engine, about 40 ms later.", at.footer, g_smallFont,
                  g_theme.secondaryText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }

    const EqLayout& layout = EqLayoutFor(g_eq.state.bands);
    int shown = g_eq.drag >= 0 ? g_eq.drag : g_eq.hover >= 0 ? g_eq.hover : g_eq.focus;

    if (!g_eq.effects) {
    // The preset picker, drawn like the mixer's output picker. While a new
    // preset is named, it is the name's entry box.
    static std::vector<EqPreset> known;
    static unsigned knownAt = 0;
    if (known.empty() || knownAt != g_fxVersion.load()) {
        known = LoadEqPresets();
        knownAt = g_fxVersion.load();
    }
    DrawSurface(dc, at.presets, Radius(16), control,
                g_eq.naming || g_eq.control == EQ_PRESETS ? 1.0f : 0.0f);
    RECT icon{at.presets.left + Scale(8), at.presets.top, at.presets.left + Scale(34),
              at.presets.bottom};
    DrawLabel(dc, g_eq.naming ? L"" : L"", icon, g_symbolFont, IconColor(),
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT presetText{at.presets.left + Scale(40), at.presets.top, at.presets.right - Scale(34),
                    at.presets.bottom};
    if (g_eq.naming) {
        DrawLabel(dc, (g_eq.name.empty() ? std::wstring(L"Preset name") : g_eq.name) + L"|",
                  presetText, g_bodyFont, g_eq.name.empty() ? g_theme.secondaryText : g_theme.text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT hint{at.presets.right - Scale(150), at.presets.top, at.presets.right - Scale(12),
                  at.presets.bottom};
        DrawLabel(dc, L"Enter saves · Esc cancels", hint, g_smallFont,
                  g_theme.secondaryText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    } else {
        int matching = MatchingEqPreset(known, g_eq.state);
        DrawLabel(dc, matching >= 0 ? known[matching].name : std::wstring(L"Custom"), presetText,
                  g_bodyFont, g_theme.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT chevron{at.presets.right - Scale(30), at.presets.top, at.presets.right - Scale(8),
                     at.presets.bottom};
        DrawLabel(dc, L"", chevron, g_symbolFont, IconColor(),
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // The band counts. Basic is bass, mids and treble.
    {
        RECT segments[EQ_STRIP_LAYOUTS];
        PCWSTR labels[EQ_STRIP_LAYOUTS];
        static std::wstring texts[EQ_STRIP_LAYOUTS];
        int chosen = -1;
        for (int i = 0; i < EQ_STRIP_LAYOUTS; ++i) {
            int left = at.strip.left + i * Scale(EQ_SEGMENT);
            segments[i] = {left, at.strip.top, left + Scale(EQ_SEGMENT), at.strip.bottom};
            texts[i] = EQ_LAYOUTS[i].tone ? std::wstring(L"Basic")
                                          : std::to_wstring(EQ_LAYOUTS[i].count);
            labels[i] = texts[i].c_str();
            if (EQ_LAYOUTS[i].count == g_eq.state.bands) chosen = i;
        }
        DrawEqSegments(dc, segments, EQ_STRIP_LAYOUTS, labels, chosen, g_eq.control,
                       EQ_LAYOUT_FIRST, control);
        static constexpr PCWSTR toneNames[3] = {L"Bass", L"Mids", L"Treble"};
        std::wstring readout;
        if (shown >= 0) {
            readout = (layout.tone ? std::wstring(toneNames[shown]) + L"  ·  " : L"") +
                      EqFrequencyText(layout.frequencies[shown], true) + L"  ·  " +
                      EqGainText(g_eq.state.gains[shown]);
        }
        if (shown >= 0) {
            DrawLabel(dc, readout, at.readout, g_bodyFont, g_theme.text,
                      DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        } else {
            DrawLabel(dc, L"Drag or scroll a band", at.readout, g_smallFont,
                      g_theme.secondaryText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    }

    // The faders, the same for every band count.
    {
        const RECT& surface = at.surface;
        DrawSurface(dc, surface, Radius(16), g_theme.panel);
        int left, right, top, bottom;
        EqColumns(&left, &right, &top, &bottom);
        int zero = EqYForGain(0, top, bottom);
        for (int gain : {EqLimit(), 0, -EqLimit()}) {
            int y = EqYForGain(gain, top, bottom);
            RECT scale{surface.left + Scale(4), y - Scale(9), left - Scale(6), y + Scale(9)};
            wchar_t scaleText[16];
            swprintf_s(scaleText, gain > 0 ? L"+%d" : gain < 0 ? L"−%d" : L"0", std::abs(gain) / 10);
            DrawLabel(dc, scaleText, scale, g_smallFont,
                      g_theme.secondaryText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
        EqCapsule(dc, g, static_cast<float>(left), zero - 0.5f * g_dpi / 96.0f,
                  static_cast<float>(right - left), g_dpi / 96.0f, overlay, 0x30);
        int column = (right - left) / std::max(1, layout.count);
        static constexpr PCWSTR toneNames[3] = {L"Bass", L"Mids", L"Treble"};
        for (int band = 0; band < layout.count; ++band) {
            float x = EqBandX(band);
            ThumbState state = band == g_eq.drag ? ThumbState::Pressed
                             : band == shown     ? ThumbState::Hover
                                                 : ThumbState::Rest;
            DrawEqSlider(dc, g, x, static_cast<float>(top), static_cast<float>(bottom),
                         static_cast<float>(zero),
                         static_cast<float>(EqYForGain(g_eq.state.gains[band], top, bottom)),
                         column - 2.0f * g_dpi / 96.0f, state, on);
            // Every band is named, except where 31 of them would crowd: there
            // the 1-2-5 steps are, and the hovered band.
            float frequency = layout.frequencies[band];
            bool crowded = layout.count > 15;
            bool round = false;
            for (float mark : {20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f}) {
                if (frequency == mark) round = true;
            }
            if (crowded && !round && band != shown) continue;
            int half = layout.count > 15 ? Scale(18) : column / 2;
            RECT label{static_cast<LONG>(x) - half, bottom + Scale(10), static_cast<LONG>(x) + half,
                       surface.bottom - Scale(4)};
            DrawLabel(dc, layout.tone ? std::wstring(toneNames[band]) : EqFrequencyText(frequency, false),
                      label, g_smallFont, band == shown ? g_theme.text : g_theme.secondaryText,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    }  // !effects

    // LoudMax, a row like the mixer's.
    if (g_eq.effects && g_eq.loudMaxShown) {
        const RECT& row = at.loudMaxRow;
        DrawSurface(dc, row, Radius(16), g_theme.panel);
        RECT name{row.left + Scale(16), row.top, row.left + Scale(100), row.bottom};
        DrawLabel(dc, L"LoudMax", name, g_bodyFont, g_theme.text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        DrawInfoMark(dc, at.infoLoudMax, g_eq.control == EQ_INFO_LOUDMAX);
        RECT what{row.left + Scale(112), row.top,
                  (g_eq.loudMaxUsable ? at.loudMaxSettings.left : row.right) - Scale(8), row.bottom};
        DrawLabel(dc, g_eq.loudMaxUsable ? L"Limiter" : g_eq.loudMaxNote.c_str(), what,
                  g_smallFont, g_theme.secondaryText,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (g_eq.loudMaxUsable) {
            DrawSurface(dc, at.loudMaxSettings, Radius(12), control,
                        g_eq.control == EQ_LM_SETTINGS ? 1.0f : 0.0f);
            DrawLabel(dc, L"Settings", at.loudMaxSettings, g_bodyFont, g_theme.text,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            DrawEqToggle(dc, g, at.loudMaxToggle, g_eq.loudMax.on, g_eq.control == EQ_LM_TOGGLE);
        }
    }

    // Listening to an input through the mixer.
    if (g_eq.effects && g_eq.input) {
        const RECT& row = at.monitorRow;
        DrawSurface(dc, row, Radius(16), g_theme.panel);
        RECT name{row.left + Scale(16), row.top + Scale(8), at.infoMonitor.left - Scale(4), row.top + Scale(44)};
        DrawLabel(dc, L"Listen through Headroom", name, g_bodyFont, g_theme.text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        DrawInfoMark(dc, at.infoMonitor, g_eq.control == EQ_INFO_MONITOR);
        DrawEqToggle(dc, g, at.monitorToggle, g_eq.monitorOn, g_eq.control == EQ_MONITOR_TOGGLE);
        bool twice = g_eq.monitorOn && ListenEnabled(InputGuid(g_eq.app));
        PCWSTR note = twice ? L"Windows plays this input as well (Listen to this device). Turn that off in the Sound settings, or you hear it twice."
                    : g_eq.monitorOn ? L"Plays on your default output, with this equalizer and these effects."
                                     : L"Off: Headroom does not play this input.";
        DrawLabel(dc, note, at.monitorNote, g_smallFont, twice ? g_theme.accent : g_theme.secondaryText,
                  DT_LEFT | DT_TOP | DT_WORDBREAK);
    }

    // The gain of an output.
    if (g_eq.effects && (g_eq.app.empty() || g_eq.input)) {
        const RECT& row = at.gainRow;
        DrawSurface(dc, row, Radius(16), g_theme.panel);
        RECT name{row.left + Scale(16), row.top + Scale(8), at.infoGain.left - Scale(4), row.top + Scale(44)};
        DrawLabel(dc, L"Gain", name, g_bodyFont, g_theme.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        DrawInfoMark(dc, at.infoGain, g_eq.control == EQ_INFO_GAIN);
        wchar_t value[32];
        swprintf_s(value, L"+%.1f dB", g_eq.gain.tenths / 10.0);
        DrawLabel(dc, value, at.gainValue, g_bodyFont, g_eq.gain.on ? g_theme.text : g_theme.secondaryText,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        DrawEqToggle(dc, g, at.gainToggle, g_eq.gain.on, g_eq.control == EQ_GAIN_TOGGLE);
        float left, right, y;
        GainTrack(&left, &right, &y);
        float position = left + (right - left) * g_eq.gain.tenths / DEVICE_GAIN_MAX;
        ThumbState thumb = g_eq.gainDrag ? ThumbState::Pressed
                           : g_eq.control == EQ_GAIN_SLIDER ? ThumbState::Hover : ThumbState::Rest;
        DrawEqHSlider(dc, g, left, right, y, position, thumb, g_eq.gain.on);
        bool limiter = g_eq.input || !UseApoForDevices() || (g_eq.loudMaxShown && g_eq.loudMaxUsable && g_eq.loudMax.on);
        PCWSTR note = !g_eq.gain.on ? L"Raises the volume of an output that is too quiet."
                    : !limiter && g_eq.gain.tenths >= 30
                        ? L"No limiter: loud sounds can crackle. Turn LoudMax on above."
                    : g_eq.gain.tenths >= 60 ? L"High gain: loud sounds can sound squashed."
                    : L"A limiter catches the peaks.";
        bool warn = g_eq.gain.on && ((!limiter && g_eq.gain.tenths >= 30) || g_eq.gain.tenths >= 60);
        DrawLabel(dc, note, at.gainNote, g_smallFont, warn ? g_theme.accent : g_theme.secondaryText,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    // Loudness equalization: the mixer's own, for this app alone.
    if (g_eq.effects && !g_eq.app.empty()) {
        const RECT& row = at.loudnessRow;
        DrawSurface(dc, row, Radius(16), g_theme.panel);
        RECT name{row.left + Scale(16), row.top + Scale(8), at.infoLoudness.left - Scale(4),
                  row.top + Scale(36)};
        DrawInfoMark(dc, at.infoLoudness, g_eq.control == EQ_INFO_LOUDNESS);
        DrawLabel(dc, L"Loudness equalization", name, g_bodyFont, g_theme.text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        DrawEqToggle(dc, g, at.loudnessToggle, g_eq.loudness.on,
                     g_eq.control == EQ_LOUDNESS_TOGGLE);
        static constexpr PCWSTR releases[6] = {L"Comp", L"Ultra", L"Short", L"Medium", L"Long", L"Custom"};
        static constexpr int positions[6] = {2, 3, 4, 1, 5, 0};  // Release value -> place.
        DrawEqSegments(dc, at.release, 6, releases, positions[ClampInt(g_eq.loudness.release, 0, 5)],
                       g_eq.control, EQ_RELEASE_FIRST, control);
        DrawLabel(dc, L"Speed: how fast it reacts and recovers", at.loudCaption, g_smallFont,
                  g_theme.secondaryText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (g_eq.loudness.release == 4) {
            // The picker for saved custom settings, drawn like the equalizer's presets.
            static std::vector<LoudPreset> savedLoud;
            static unsigned savedLoudAt = 0;
            if (savedLoudAt != g_fxVersion.load() + 1u || savedLoud.empty()) {
                savedLoud = LoadLoudPresets();
                savedLoudAt = g_fxVersion.load() + 1u;
            }
            DrawSurface(dc, at.loudPresets, Radius(16), control,
                        (g_eq.naming && g_eq.namingLoud) || g_eq.control == EQ_LOUD_PRESETS ? 1.0f : 0.0f);
            RECT glyph{at.loudPresets.left + Scale(8), at.loudPresets.top,
                       at.loudPresets.left + Scale(34), at.loudPresets.bottom};
            DrawLabel(dc, g_eq.naming && g_eq.namingLoud ? L"\uE74E" : L"\uE9E9", glyph, g_symbolFont, IconColor(),
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            RECT presetText{at.loudPresets.left + Scale(40), at.loudPresets.top,
                            at.loudPresets.right - Scale(34), at.loudPresets.bottom};
            if (g_eq.naming && g_eq.namingLoud) {
                DrawLabel(dc, (g_eq.name.empty() ? std::wstring(L"Preset name") : g_eq.name) + L"|",
                          presetText, g_bodyFont,
                          g_eq.name.empty() ? g_theme.secondaryText : g_theme.text,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                RECT hint{at.loudPresets.right - Scale(150), at.loudPresets.top,
                          at.loudPresets.right - Scale(12), at.loudPresets.bottom};
                DrawLabel(dc, L"Enter saves \u00B7 Esc cancels", hint, g_smallFont,
                          g_theme.secondaryText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            } else {
                int matching = MatchingLoudPreset(savedLoud, g_eq.loudness.custom);
                DrawLabel(dc, matching >= 0 ? savedLoud[matching].name : std::wstring(L"Custom settings"),
                          presetText, g_bodyFont, g_theme.text,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                RECT chevron{at.loudPresets.right - Scale(30), at.loudPresets.top,
                             at.loudPresets.right - Scale(8), at.loudPresets.bottom};
                DrawLabel(dc, L"\uE70D", chevron, g_symbolFont, IconColor(),
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            DrawLabel(dc, L"Expert settings", at.expertHeader, g_smallFont, g_theme.secondaryText,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            DrawSurface(dc, at.expertReset, Radius(10), control,
                        g_eq.control == EQ_EXPERT_RESET ? 1.0f : 0.0f);
            DrawLabel(dc, L"Reset", at.expertReset, g_smallFont, g_theme.text,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            for (int i = 0; i < LOUD_PARAMS; ++i) {
                const RECT& line = at.expert[i];
                bool active = g_eq.expertDrag == i;
                bool hover = g_eq.control == EQ_EXPERT_FIRST + i;
                RECT name{line.left, line.top, line.left + Scale(122), line.bottom};
                DrawInfoMark(dc, at.info[i], g_eq.control == EQ_INFO_FIRST + i);
                DrawLabel(dc, LOUD_PARAM[i].name, name, g_smallFont, g_theme.text,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                float left, right, y;
                LoudTrack(i, &left, &right, &y);
                double value = g_eq.loudness.custom[i];
                float x = left + static_cast<float>(LoudPosition(i, value)) * (right - left);
                DrawEqHSlider(dc, g, left, right, y, x,
                              active ? ThumbState::Pressed : hover ? ThumbState::Hover
                                                                   : ThumbState::Rest, true);
                RECT text{line.right - Scale(70), line.top, line.right, line.bottom};
                DrawLabel(dc, LoudText(i, value), text, g_smallFont,
                          active || hover ? g_theme.text : g_theme.secondaryText,
                          DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            }
        }
    }

    // The surround channels of the app.
    if (g_eq.effects && !g_eq.app.empty() && !g_eq.input) {
        const RECT& row = at.channelsRow;
        DrawSurface(dc, row, Radius(16), g_theme.panel);
        RECT name{row.left + Scale(16), row.top + Scale(8), at.infoChannels.left - Scale(4), row.top + Scale(36)};
        DrawInfoMark(dc, at.infoChannels, g_eq.control == EQ_INFO_CHANNELS);
        DrawLabel(dc, L"Surround channels", name, g_bodyFont, g_theme.text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        DrawEqToggle(dc, g, at.channelsToggle, g_eq.channels.on, g_eq.control == EQ_CHANNELS_TOGGLE);
        DrawLabel(dc, L"Drag a channel's volume; right-click sets it back to 100 %", at.channelCaption, g_smallFont,
                  g_theme.secondaryText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        static constexpr PCWSTR channelNames[SURROUND_CHANNELS] = {L"FL", L"FR", L"C", L"LFE", L"BL", L"BR", L"SL", L"SR"};
        for (int i = 0; i < SURROUND_CHANNELS; ++i) {
            int percent = g_eq.channels.percent[i];
            const RECT& chip = at.channelChip[i];
            bool hot = g_eq.control == EQ_CHANNEL_FIRST + i;
            RECT nameRect{chip.left, chip.top, chip.right, chip.top + Scale(18)};
            RECT valueRect{chip.left, chip.bottom - Scale(18), chip.right, chip.bottom};
            DrawLabel(dc, channelNames[i], nameRect, g_smallFont, g_theme.text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            wchar_t text[16];
            swprintf_s(text, L"%d", percent);
            DrawLabel(dc, text, valueRect, g_smallFont, percent == 0 ? g_theme.secondaryText : g_theme.text,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            float top, bottom, x;
            ChannelTrack(i, &top, &bottom, &x);
            float value = bottom - (bottom - top) * percent / 200.0f;
            ThumbState state = g_eq.channelDrag == i ? ThumbState::Pressed : hot ? ThumbState::Hover : ThumbState::Rest;
            DrawEqSlider(dc, g, x, top, bottom, bottom, value, SliderThumbWidth(), state, g_eq.channels.on);
        }
    }

    DrawLabel(dc, g_eq.failed
                  ? L"Could not write to Equalizer APO's config folder."
                  : !g_eq.app.empty()
                  ? L"Played through the mixer, about 40 ms later."
                  : UseApoForDevices()
                  ? L"Works on outputs enabled in Equalizer APO."
                  : L"Played through the mixer's own engine, about 40 ms later.",
              at.footer, g_smallFont, g_eq.failed ? g_theme.accent : g_theme.secondaryText,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void PaintEqualizer() {
    RECT rect;
    if (!g_eq.window || !GetWindowRect(g_eq.window, &rect)) return;
    PaintLayeredPopup(g_eq.window, g_eqBlack, g_eqWhite, rect.right - rect.left,
                      rect.bottom - rect.top, DrawEqualizer);
}

// Saves the bands and hands them on, at most every 120 ms while a band is
// dragged.
void ApplyEqualizer(bool now) {
    if (!g_eq.dirty) return;
    ULONGLONG tick = GetTickCount64();
    if (!now && tick - g_eq.written < 120) return;
    g_eq.dirty = false;
    g_eq.written = tick;
    if (!g_eq.app.empty()) {
        Wh_SetStringValue((L"appEq_v1:" + g_eq.app).c_str(),
                          EqualizerValueText(g_eq.state).c_str());
        FxSettingsChanged(g_eq.app);
        return;
    }
    SaveEqualizer(g_eq.guid, g_eq.state);
    bool failed = !WriteEqualizerConfig();
    if (failed != g_eq.failed) {
        g_eq.failed = failed;
        PaintEqualizer();
    }
}

void SetEqualizerGain(int band, int gain, bool show = true) {
    gain = ClampInt((gain + (gain >= 0 ? 2 : -2)) / 5 * 5, -EqLimit(), EqLimit());  // 0.5 dB steps.
    if (band < 0 || band >= g_eq.state.bands) return;
    if (g_eq.state.gains[band] == gain && g_eq.state.on) return;
    g_eq.state.gains[band] = gain;
    g_eq.state.on = true;  // Moving a band turns the equalizer on.
    g_eq.dirty = true;
    if (!show) return;
    PaintEqualizer();
    ApplyEqualizer(false);
}

int EqGainForY(int y, int top, int bottom) {
    y = ClampInt(y, top, bottom);
    int limit = EqLimit();
    return limit - (y - top) * 2 * limit / std::max(1, bottom - top);
}

void SetEqualizerFromPoint(int band, POINT point) {
    int left, right, top, bottom;
    EqColumns(&left, &right, &top, &bottom);
    SetEqualizerGain(band, EqGainForY(point.y, top, bottom));
}

// Resizes the panel to the current page, keeping its top where it is.
void ResizeEqualizer() {
    RECT rect;
    if (!g_eq.window || !GetWindowRect(g_eq.window, &rect)) return;
    g_eqAt = EqMeasure(g_eqWidth);
    int height = g_eqAt.height;
    MONITORINFO info = {sizeof(info)};
    GetMonitorInfoW(MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST), &info);
    int top = std::clamp<int>(g_eqHomeTop, info.rcWork.top,
                              std::max<int>(info.rcWork.top, info.rcWork.bottom - height));
    SetWindowPos(g_eq.window, nullptr, rect.left, top, g_eqWidth, height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    g_eqBlack.Reset();
    g_eqWhite.Reset();
    ApplyWindowFrame(g_eq.window);
    PaintEqualizer();
}

// The presets list under its picker; also saving and deleting.
void ShowEqPresetMenu(HWND owner) {
    std::vector<EqPreset> presets = LoadEqPresets();
    int matching = MatchingEqPreset(presets, g_eq.state);
    std::vector<OutputDevice> items;
    bool anyCustom = false;
    for (const auto& preset : presets) {
        items.push_back({L"", preset.name, preset.custom ? L"\uE734" : L"\uE9E9"});
        anyCustom |= preset.custom;
    }
    int save = static_cast<int>(items.size());
    items.push_back({L"", L"Save as preset\u2026", L"\uE74E"});
    int remove = anyCustom ? static_cast<int>(items.size()) : -1;
    if (anyCustom) items.push_back({L"", L"Delete a preset\u2026", L"\uE74D"});

    // As wide as the picker, right under it.
    RECT anchor = g_eqAt.presets;
    MapWindowPoints(g_eq.window, owner, reinterpret_cast<POINT*>(&anchor), 2);
    int chosen = RunOutputList(owner, items, matching, L"", &anchor);
    if (chosen >= 0 && chosen < save) {
        g_eq.state = ResampleEqualizer(presets[chosen].curve, g_eq.state.bands);
        g_eq.state.on = true;
        g_eq.dirty = true;
        ApplyEqualizer(true);
    } else if (chosen == save) {
        int count = 0;
        for (const auto& preset : presets) count += preset.custom;
        g_eq.naming = true;
        g_eq.namingLoud = false;
        g_eq.name = L"My preset " + std::to_wstring(count + 1);
    } else if (chosen >= 0 && chosen == remove) {
        std::vector<OutputDevice> customs;
        std::vector<size_t> indexes;
        for (size_t i = 0; i < presets.size(); ++i) {
            if (!presets[i].custom) continue;
            customs.push_back({L"", presets[i].name, L"\uE74D"});
            indexes.push_back(i);
        }
        int doomed = RunOutputList(owner, customs, -1, L"", &anchor);
        if (doomed >= 0) {
            presets.erase(presets.begin() + static_cast<std::ptrdiff_t>(indexes[doomed]));
            SaveCustomEqPresets(presets);
            g_fxVersion.fetch_add(1);  // The picker's name is looked up again.
        }
    }
    PaintEqualizer();
}

void SaveLoudPresetFromPanel();

void SaveEqPresetFromPanel() {
    if (g_eq.namingLoud) {
        SaveLoudPresetFromPanel();
        return;
    }
    std::wstring name = TrimSetting(g_eq.name.c_str());
    g_eq.naming = false;
    if (name.empty()) return;
    std::vector<EqPreset> presets = LoadEqPresets();
    // A preset of the same name is replaced; built-in names stay built in.
    presets.erase(std::remove_if(presets.begin(), presets.end(), [&](const EqPreset& preset) {
        return preset.custom && _wcsicmp(preset.name.c_str(), name.c_str()) == 0;
    }), presets.end());
    EqPreset preset;
    preset.name = name;
    preset.custom = true;
    preset.curve = g_eq.state;
    preset.curve.on = true;
    presets.push_back(preset);
    SaveCustomEqPresets(presets);
    g_fxVersion.fetch_add(1);
}

void SaveEqLoudness() {
    SaveLoudness(g_eq.app, g_eq.loudness);
    FxSettingsChanged(g_eq.app);
    PaintEqualizer();
}

// The list of saved custom settings under its picker; also saving and deleting.
void ShowLoudPresetMenu(HWND owner) {
    std::vector<LoudPreset> presets = LoadLoudPresets();
    int matching = MatchingLoudPreset(presets, g_eq.loudness.custom);
    // The ready-made speeds come first: each one is a starting point for the custom values.
    struct Start { PCWSTR label; int mode; };
    static constexpr Start starts[5] = {{L"Start from Competitive", LOUD_COMPETITIVE},
                                        {L"Start from Ultra", 3},
                                        {L"Start from Short", 0},
                                        {L"Start from Medium", 1},
                                        {L"Start from Long", 2}};
    constexpr int startCount = 5;
    std::vector<OutputDevice> items;
    for (const Start& start : starts) items.push_back({L"", start.label, L"\uE9E9", false});
    for (const auto& preset : presets) items.push_back({L"", preset.name, L"\uE734", false});
    int save = static_cast<int>(items.size());
    items.push_back({L"", L"Save these settings as a preset\u2026", L"\uE74E", false});
    int remove = presets.empty() ? -1 : static_cast<int>(items.size());
    if (!presets.empty()) items.push_back({L"", L"Delete a preset\u2026", L"\uE74D", false});

    RECT anchor = g_eqAt.loudPresets;
    MapWindowPoints(g_eq.window, owner, reinterpret_cast<POINT*>(&anchor), 2);
    int chosen = RunOutputList(owner, items, matching >= 0 ? matching + startCount : -1, L"", &anchor);
    if (chosen >= 0 && chosen < startCount) {
        std::copy(std::begin(LOUD_MODES[starts[chosen].mode]), std::end(LOUD_MODES[starts[chosen].mode]),
                  g_eq.loudness.custom);
        SaveEqLoudness();
    } else if (chosen >= startCount && chosen < save) {
        const LoudPreset& preset = presets[static_cast<size_t>(chosen - startCount)];
        std::copy(std::begin(preset.values), std::end(preset.values), g_eq.loudness.custom);
        SaveEqLoudness();
    } else if (chosen == save) {
        g_eq.namingLoud = g_eq.naming = true;
        g_eq.name = L"My setting " + std::to_wstring(presets.size() + 1);
    } else if (chosen >= 0 && chosen == remove) {
        std::vector<OutputDevice> doomed;
        for (const auto& preset : presets) doomed.push_back({L"", preset.name, L"\uE74D", false});
        int index = RunOutputList(owner, doomed, -1, L"", &anchor);
        if (index >= 0) {
            presets.erase(presets.begin() + index);
            SaveLoudPresets(presets);
            g_fxVersion.fetch_add(1);
        }
    }
    PaintEqualizer();
}

void SaveLoudPresetFromPanel() {
    std::wstring name = TrimSetting(g_eq.name.c_str());
    g_eq.naming = g_eq.namingLoud = false;
    if (name.empty()) return;
    std::vector<LoudPreset> presets = LoadLoudPresets();
    presets.erase(std::remove_if(presets.begin(), presets.end(), [&](const LoudPreset& preset) {
        return _wcsicmp(preset.name.c_str(), name.c_str()) == 0;
    }), presets.end());
    LoudPreset preset;
    preset.name = name;
    std::copy(std::begin(g_eq.loudness.custom), std::end(g_eq.loudness.custom), preset.values);
    presets.push_back(std::move(preset));
    SaveLoudPresets(presets);
    g_fxVersion.fetch_add(1);
}

// Drags one channel's volume, saved at most every 100 ms.
void SetChannelFromPoint(POINT point) {
    int index = g_eq.channelDrag;
    if (index < 0 || index >= SURROUND_CHANNELS) return;
    float top, bottom, x;
    ChannelTrack(index, &top, &bottom, &x);
    double t = (bottom - point.y) / std::max(1.0f, bottom - top);
    int percent = static_cast<int>(std::lround(std::clamp(t, 0.0, 1.0) * 40.0)) * 5;
    if (percent >= 95 && percent <= 105) percent = 100;  // Sticks to 100 %.
    g_eq.channels.percent[index] = percent;
    g_eq.channels.on = true;
    ULONGLONG tick = GetTickCount64();
    if (tick - g_eq.channelSaved >= 100) {
        g_eq.channelSaved = tick;
        SaveChannelMix(g_eq.app, g_eq.channels);
        FxSettingsChanged(g_eq.app);
    }
    PaintEqualizer();
}

// Drags one expert value. It is handed to the audio engine while dragging,
// at most every 100 ms.
void SetExpertFromPoint(POINT point) {
    int index = g_eq.expertDrag;
    if (index < 0 || index >= LOUD_PARAMS) return;
    float left, right, y;
    LoudTrack(index, &left, &right, &y);
    double t = (point.x - left) / std::max(1.0f, right - left);
    g_eq.loudness.custom[index] = LoudValueAt(index, t);
    ULONGLONG tick = GetTickCount64();
    if (tick - g_eq.expertSaved >= 100) {
        g_eq.expertSaved = tick;
        SaveLoudness(g_eq.app, g_eq.loudness);
        FxSettingsChanged(g_eq.app);
    }
    PaintEqualizer();
}

// Shows an equalizer over the mixer until it is closed. With `app`, an
// executable name, it is that app's own; otherwise the output's.
// What the sync page needs: where the second output is kept, and the output the sound plays on first.
struct SyncRequest {
    std::wstring key;    // "devMirror_v1:<GUID>" or "appMirror_v1:<app>".
    std::wstring app;    // The app or input, empty for an output.
    std::wstring guid;   // The output's GUID, for an output.
    std::wstring first;  // The id of the output the sound plays on first.
    std::wstring name;   // What the page is about, in its header.
};

void RunEqualizer(HWND owner, const std::wstring& deviceId, const std::wstring& deviceName,
                  const std::wstring& app = L"", bool startOnEffects = false, const SyncRequest* sync = nullptr) {
    if (g_eq.window || g_outputMenuOpen || (!sync && deviceId.empty() && app.empty())) return;
    if (!sync && app.empty() && EqualizerApoFolder().empty() && FindFxSink().empty()) {
        MessageBoxW(owner,
                L"The equalizer of an output needs one of two things:\n\n"
                L"\u2022 Equalizer APO, a small free and open-source audio filter (no delay), or\n"
                L"\u2022 VB-CABLE, a small free virtual output, so the mixer's own engine does it "
                L"(about 40 ms delay).\n\n"
                L"The settings have a Setup guide with both.",
                L"Headroom", MB_OK | MB_ICONINFORMATION);
        return;
    }

    CancelNameTooltip(owner);
    g_outputMenuOpen = true;
    KillTimer(owner, TIMER_CHECK_FOCUS);
    g_eq = {};
    g_eq.app = app;
    if (sync) {
        g_eq.sync = true;
        g_eq.mirrorKey = sync->key;
        g_eq.mirrorApp = sync->app;
        g_eq.mirrorGuid = sync->guid;
        g_eq.syncFirst = sync->first;
        g_eq.subtitle = ShortOutputName(sync->name);
        g_eq.mirror = LoadMirrorValue(sync->key);
        g_eq.firstName = OutputNameForId(sync->first);
        g_eq.mirrorName = OutputNameForId(g_eq.mirror.target);
        g_mirrorTestOffset = g_eq.mirror.offsetMs;
    } else if (app.empty()) {
        g_eq.guid = EndpointGuid(deviceId);
        g_eq.subtitle = ShortOutputName(deviceName);
        g_eq.state = LoadEqualizer(g_eq.guid);
        g_eq.loudMaxShown = LoudMaxForOutputs();
        g_eq.loudMaxUsable = g_eq.loudMaxShown;
    } else {
        g_eq.subtitle = deviceName;
        g_eq.state = LoadAppEqualizer(app);
        g_eq.loudness = LoadLoudness(app);
        g_eq.channels = LoadChannelMix(app);
        g_eq.input = IsInputKey(app);
        if (g_eq.input) {
            g_eq.guid = InputGuid(app);
            g_eq.monitorOn = MonitorOn(g_eq.guid);
            g_eq.gain = LoadDeviceGain(g_eq.guid);
        }
        g_eq.loudMaxShown = LoudMaxForOutputs() || LoudMaxForApps();
        g_eq.loudMaxUsable = LoudMaxForApps();
        if (!g_eq.loudMaxUsable) {
            g_eq.loudMaxNote = sizeof(void*) == 8 ? L"not found" : L"for apps, needs LoudMax32.dll";
        }
    }
    g_eq.loudMax = LoadLoudMax(EqLoudMaxKey());
    if (app.empty() && !sync) g_eq.gain = LoadDeviceGain(g_eq.guid);
    if (g_eq.state.bands > 15) {
        g_eq.state = ResampleEqualizer(g_eq.state, 15);
        g_eq.dirty = true;
    }
    g_eq.effects = (startOnEffects || sync) && EqHasEffects();

    // Over the mixer, as wide as it.
    RECT client{};
    GetClientRect(owner, &client);
    g_eqWidth = client.right;
    g_eqAt = EqMeasure(g_eqWidth);
    RECT area{0, Scale(8), client.right, Scale(8) + g_eqAt.height};
    MapWindowPoints(owner, nullptr, reinterpret_cast<POINT*>(&area), 2);
    g_eqHomeTop = area.top;  // Where it belongs, before it is moved to fit the screen.
    MONITORINFO info = {sizeof(info)};
    GetMonitorInfoW(MonitorFromRect(&area, MONITOR_DEFAULTTONEAREST), &info);
    if (area.bottom > info.rcWork.bottom) OffsetRect(&area, 0, info.rcWork.bottom - area.bottom);
    if (area.top < info.rcWork.top) OffsetRect(&area, 0, info.rcWork.top - area.top);

    g_eq.window = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        OUTPUT_LIST_CLASS, L"", WS_POPUP, area.left, area.top, area.right - area.left,
        area.bottom - area.top, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!g_eq.window) {
        g_outputMenuOpen = false;
        return;
    }
    HWND panel = g_eq.window;
    g_panelWindow = panel;
    ApplyWindowFrame(panel);
    ApplyBackdropBlur(panel);
    PaintEqualizer();
    ShowWindow(panel, SW_SHOWNOACTIVATE);

    bool done = false;
    bool tracking = false;
    MSG msg;
    while (!done && IsWindowVisible(owner) && !g_closeTime &&
           GetForegroundWindow() == owner) {
        BOOL result = GetMessageW(&msg, nullptr, 0, 0);
        if (result <= 0) {
            if (result == 0) PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }
        POINT point{GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam)};
        switch (msg.message) {
            case WM_MOUSEMOVE: {
                if (msg.hwnd != panel) break;
                if (!tracking) {
                    TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, panel, 0};
                    tracking = TrackMouseEvent(&track) != FALSE;
                }
                if (g_eq.drag >= 0) {
                    SetEqualizerFromPoint(g_eq.drag, point);
                    continue;
                }
                if (g_eq.expertDrag >= 0) {
                    SetExpertFromPoint(point);
                    continue;
                }
                if (g_eq.channelDrag >= 0) {
                    SetChannelFromPoint(point);
                    continue;
                }
                if (g_eq.gainDrag) {
                    SetGainFromPoint(point, false);
                    continue;
                }
                if (g_eq.mirrorDrag) {
                    SetMirrorFromPoint(point, false);
                    continue;
                }
                int band = EqBandAt(point);
                int control = EqControlAt(point);
                if (band != g_eq.hover || control != g_eq.control) {
                    g_eq.hover = band;
                    g_eq.control = control;
                    UpdateEqInfoTip(control);
                    PaintEqualizer();
                }
                continue;
            }
            case WM_MOUSELEAVE:
                if (msg.hwnd != panel) break;
                tracking = false;
                HideInfoTip();
                if (g_eq.drag < 0 && g_eq.expertDrag < 0 && g_eq.channelDrag < 0 && !g_eq.gainDrag && !g_eq.mirrorDrag && (g_eq.hover >= 0 || g_eq.control >= 0)) {
                    g_eq.hover = -1;
                    g_eq.control = -1;
                    PaintEqualizer();
                }
                continue;
            case WM_LBUTTONDOWN:
            case WM_LBUTTONDBLCLK: {
                if (msg.hwnd != panel) {
                    done = true;  // A click elsewhere closes the equalizer.
                    continue;
                }
                int control = EqControlAt(point);
                if (control == EQ_CLOSE) {
                    done = true;
                } else if (control == EQ_TOGGLE) {
                    g_eq.state.on = !g_eq.state.on;
                    g_eq.dirty = true;
                    PaintEqualizer();
                    ApplyEqualizer(true);
                } else if (control == EQ_PRESETS) {
                    g_eq.naming = false;
                    ShowEqPresetMenu(owner);
                } else if (control == EQ_LM_TOGGLE) {
                    g_eq.loudMax.on = !g_eq.loudMax.on;
                    SaveLoudMax(EqLoudMaxKey(), g_eq.loudMax);
                    if (!g_eq.app.empty()) {
                        FxSettingsChanged(g_eq.app);
                    } else {
                        AddEqualizerDevice(g_eq.guid);
                        g_eq.failed = !WriteEqualizerConfig();
                    }
                    PaintEqualizer();
                } else if (control == EQ_LM_SETTINGS) {
                    g_eq.openLoudMax = true;
                    done = true;
                } else if (control == EQ_LOUDNESS_TOGGLE) {
                    g_eq.loudness.on = !g_eq.loudness.on;
                    SaveEqLoudness();
                } else if (control >= EQ_RELEASE_FIRST && control < EQ_RELEASE_FIRST + 6) {
                    static constexpr int modes[6] = {5, 3, 0, 1, 2, 4};  // Place -> release value.
                    bool wasCustom = g_eq.loudness.release == 4;
                    int previousMode = ClampInt(g_eq.loudness.release, 0, 5);
                    g_eq.loudness.release = modes[control - EQ_RELEASE_FIRST];
                    if (!wasCustom && g_eq.loudness.release == 4) {
                        // Custom starts from the speed that was chosen, not from scratch.
                        std::copy(std::begin(LOUD_MODES[previousMode]), std::end(LOUD_MODES[previousMode]),
                                  g_eq.loudness.custom);
                    }
                    SaveEqLoudness();
                    if (wasCustom != (g_eq.loudness.release == 4)) ResizeEqualizer();
                } else if (control == EQ_LOUD_PRESETS) {
                    g_eq.naming = g_eq.namingLoud = false;
                    HideInfoTip();
                    ShowLoudPresetMenu(owner);
                } else if (control >= EQ_INFO_LOUDMAX) {
                    // The (i) marks only explain, on hover.
                } else if (control == EQ_MONITOR_TOGGLE) {
                    g_eq.monitorOn = !g_eq.monitorOn;
                    SetMonitorOn(g_eq.guid, g_eq.monitorOn);
                    g_fxRetryAfter.erase(g_eq.app);
                    FxSettingsChanged(g_eq.app);
                    MonitorsChanged();
                    PaintEqualizer();
                } else if (control == EQ_GAIN_TOGGLE) {
                    g_eq.gain.on = !g_eq.gain.on;
                    if (g_eq.gain.on && g_eq.gain.tenths == 0) g_eq.gain.tenths = 30;  // Starts at +3 dB.
                    SaveDeviceGain(g_eq.guid, g_eq.gain);
                    if (g_eq.input) {
                        FxSettingsChanged(g_eq.app);
                    } else {
                        AddEqualizerDevice(g_eq.guid);
                        g_eq.failed = !WriteEqualizerConfig();
                    }
                    PaintEqualizer();
                } else if (control == EQ_GAIN_SLIDER) {
                    g_eq.gainDrag = true;
                    SetCapture(panel);
                    SetGainFromPoint(point, false);
                } else if (control == EQ_SYNC_TEST) {
                    if (g_eq.testing) {
                        StopMirrorTest();
                        g_eq.testing = false;
                    } else if (!g_eq.syncFirst.empty() && !g_eq.mirror.target.empty()) {
                        g_eq.testing = StartMirrorTest(g_eq.syncFirst, g_eq.mirror.target, g_eq.mirror.offsetMs);
                    }
                    PaintEqualizer();
                } else if (control >= EQ_SYNC_NUDGE_FIRST && control < EQ_SYNC_NUDGE_END) {
                    NudgeMirror(control - EQ_SYNC_NUDGE_FIRST);
                } else if (control == EQ_MIRROR_SLIDER) {
                    g_eq.mirrorDrag = true;
                    SetCapture(panel);
                    SetMirrorFromPoint(point, false);
                } else if (control == EQ_CHANNELS_TOGGLE) {
                    g_eq.channels.on = !g_eq.channels.on;
                    SaveChannelMix(g_eq.app, g_eq.channels);
                    FxSettingsChanged(g_eq.app);
                    PaintEqualizer();
                } else if (control >= EQ_CHANNEL_FIRST && control < EQ_CHANNEL_FIRST + SURROUND_CHANNELS) {
                    g_eq.channelDrag = control - EQ_CHANNEL_FIRST;
                    SetCapture(panel);
                    SetChannelFromPoint(point);
                } else if (control == EQ_EXPERT_RESET) {
                    std::copy(std::begin(LOUD_ULTRA), std::end(LOUD_ULTRA), g_eq.loudness.custom);
                    SaveEqLoudness();
                } else if (control >= EQ_EXPERT_FIRST && control < EQ_EXPERT_FIRST + LOUD_PARAMS) {
                    g_eq.expertDrag = control - EQ_EXPERT_FIRST;
                    SetCapture(panel);
                    SetExpertFromPoint(point);
                } else if (control >= EQ_LAYOUT_FIRST) {
                    int count = EQ_LAYOUTS[control - EQ_LAYOUT_FIRST].count;
                    if (count != g_eq.state.bands) {
                        // The curve so far carries over to the new bands.
                        g_eq.state = ResampleEqualizer(g_eq.state, count);
                        g_eq.hover = g_eq.focus = -1;
                        g_eq.dirty = true;
                        PaintEqualizer();
                        ApplyEqualizer(true);
                    }
                } else if (int band = EqBandAt(point); band >= 0) {
                    g_eq.drag = g_eq.focus = band;
                    SetCapture(panel);
                    SetEqualizerFromPoint(band, point);
                }
                continue;
            }
            case WM_LBUTTONUP:
                if (g_eq.gainDrag) {
                    g_eq.gainDrag = false;
                    ReleaseCapture();
                    SetGainFromPoint(point, true);
                }
                if (g_eq.mirrorDrag) {
                    g_eq.mirrorDrag = false;
                    ReleaseCapture();
                    SetMirrorFromPoint(point, true);
                }
                if (g_eq.channelDrag >= 0) {
                    g_eq.channelDrag = -1;
                    ReleaseCapture();
                    SaveChannelMix(g_eq.app, g_eq.channels);
                    FxSettingsChanged(g_eq.app);
                    PaintEqualizer();
                }
                if (g_eq.expertDrag >= 0) {
                    g_eq.expertDrag = -1;
                    ReleaseCapture();
                    SaveEqLoudness();
                }
                if (g_eq.drag >= 0) {
                    g_eq.drag = -1;
                    ReleaseCapture();
                    ApplyEqualizer(true);
                    PaintEqualizer();
                }
                continue;
            case WM_RBUTTONDOWN:
                if (msg.hwnd != panel) {
                    done = true;
                } else if (int band = EqBandAt(point); band >= 0) {
                    SetEqualizerGain(band, 0);  // A right-click sets a band back to 0.
                    ApplyEqualizer(true);
                } else if (int control = EqControlAt(point);
                           control >= EQ_CHANNEL_FIRST && control < EQ_CHANNEL_FIRST + SURROUND_CHANNELS) {
                    g_eq.channels.percent[control - EQ_CHANNEL_FIRST] = 100;
                    SaveChannelMix(g_eq.app, g_eq.channels);
                    FxSettingsChanged(g_eq.app);
                    PaintEqualizer();
                }
                continue;
            case WM_RBUTTONUP:
            case WM_CONTEXTMENU:
                if (msg.hwnd == panel) continue;
                break;
            case WM_MOUSEWHEEL: {
                POINT cursor{};
                GetCursorPos(&cursor);
                ScreenToClient(panel, &cursor);
                int band = EqBandAt(cursor);
                if (band >= 0) {
                    int notches = GET_WHEEL_DELTA_WPARAM(msg.wParam) / WHEEL_DELTA;
                    SetEqualizerGain(band, g_eq.state.gains[band] + notches * 5);
                    ApplyEqualizer(true);
                }
                continue;
            }
            case WM_KEYDOWN:
                if (g_eq.naming) {
                    if (msg.wParam == VK_RETURN) {
                        SaveEqPresetFromPanel();
                    } else if (msg.wParam == VK_ESCAPE) {
                        g_eq.naming = g_eq.namingLoud = false;
                    } else if (msg.wParam == VK_BACK) {
                        if (!g_eq.name.empty()) g_eq.name.pop_back();
                    } else {
                        TranslateMessage(&msg);  // Typed letters come as WM_CHAR.
                    }
                    PaintEqualizer();
                    continue;
                }
                if (g_eq.sync && (msg.wParam == VK_LEFT || msg.wParam == VK_RIGHT)) {
                    NudgeMirror(msg.wParam == VK_RIGHT ? 3 : 1);  // 1 ms either way.
                    continue;
                }
                if (g_eq.sync && (msg.wParam == VK_UP || msg.wParam == VK_DOWN)) continue;
                switch (msg.wParam) {
                    case VK_LEFT:
                    case VK_RIGHT:
                        g_eq.focus = ClampInt((g_eq.focus < 0 ? 0 : g_eq.focus) +
                                                  (msg.wParam == VK_RIGHT ? 1 : -1),
                                              0, g_eq.state.bands - 1);
                        PaintEqualizer();
                        break;
                    case VK_UP:
                    case VK_DOWN:
                        if (g_eq.focus < 0) g_eq.focus = 0;
                        SetEqualizerGain(g_eq.focus, g_eq.state.gains[g_eq.focus] +
                                                         (msg.wParam == VK_UP ? 5 : -5));
                        ApplyEqualizer(true);
                        break;
                    case VK_ESCAPE:
                    case VK_RETURN:
                    case VK_TAB:
                        done = true;
                        break;
                }
                continue;
            case WM_SYSKEYDOWN:
                done = true;
                continue;
            case WM_CHAR:
                if (g_eq.naming && msg.wParam >= 32 && msg.wParam != L'|' &&
                    g_eq.name.size() < 32) {
                    g_eq.name += static_cast<wchar_t>(msg.wParam);
                    PaintEqualizer();
                }
                continue;
            case WM_SYSCHAR:
                continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (GetCapture() == panel) ReleaseCapture();
    StopMirrorTest();
    HideInfoTip();
    ApplyEqualizer(true);
    DestroyWindow(panel);
    g_eq.window = nullptr;
    g_panelWindow = nullptr;
    g_eqBlack.Reset();
    g_eqWhite.Reset();
    g_outputMenuOpen = false;
    if (!IsWindow(owner)) return;
    if (g_pendingDefaultOutputChange) {
        g_pendingDefaultOutputChange = false;
        PostMessageW(owner, WM_APP_DEFAULT_OUTPUT_CHANGED, 0, 0);
    }
    RefreshSourceList(owner);  // The badges follow.
    if (g_eq.openLoudMax) {
        g_eq.openLoudMax = false;
        OpenLoudMaxEditor(owner, EqLoudMaxKey(), g_eq.app, g_eq.subtitle);
    }
    InvalidateRect(owner, nullptr, FALSE);
    if (g_settings.closeWhenFocusIsLost && IsWindowVisible(owner)) {
        SetTimer(owner, TIMER_CHECK_FOCUS, 150, nullptr);
    }
}

// The page that lines up an output with its second output.
void OpenSyncPage(HWND window, const SyncRequest& request) {
    RunEqualizer(window, L"", request.name, L"", false, &request);
}

// Offers the other outputs for "Also play on": the one picked becomes the second output and the sync page opens,
// so the delay can be set right away. The first entry turns it off.
void ChooseMirror(HWND window, const SyncRequest& request, POINT screenPoint) {
    if (g_outputMenuOpen) return;
    DeviceMirror mirror = LoadMirrorValue(request.key);
    std::vector<OutputDevice> outputs;
    std::wstring currentId;
    LoadOutputDevices(outputs, currentId);
    std::vector<OutputDevice> items;
    std::vector<std::wstring> ids;
    items.push_back({L"", L"Not on a second output", L"", !DeviceMirrorActive(mirror)});
    ids.push_back(L"");
    int current = DeviceMirrorActive(mirror) ? -1 : 0;
    const bool active = DeviceMirrorActive(mirror);
    for (const OutputDevice& output : outputs) {
        if (EndpointGuid(output.id) == EndpointGuid(request.first)) continue;  // Not the output it plays on already.
        if (DeviceMirrorActive(mirror) && _wcsicmp(output.id.c_str(), mirror.target.c_str()) == 0) {
            current = static_cast<int>(items.size());
        }
        items.push_back(output);
        ids.push_back(output.id);
    }
    // Lining the two up by ear: at the end of the list, once there is a second output.
    static const wchar_t syncGlyph[] = {0xE916, 0};
    const int syncIndex = active ? static_cast<int>(items.size()) : -1;
    if (active) {
        items.push_back({L"", L"Sync the two outputs", syncGlyph, false});
        ids.push_back(L"");
    }
    g_outputMenuOpen = true;
    int chosen = RunOutputList(window, items, current, L"No other output", nullptr, &screenPoint);
    g_outputMenuOpen = false;
    if (!IsWindow(window) || chosen < 0 || chosen >= static_cast<int>(ids.size())) return;
    if (chosen == syncIndex) {
        OpenSyncPage(window, request);
        return;
    }
    if (chosen == 0) {
        mirror.on = false;
        StoreMirror(request.key, request.app, request.guid, mirror);
        RefreshSourceList(window);
        return;
    }
    mirror.on = true;
    mirror.target = ids[static_cast<size_t>(chosen)];
    StoreMirror(request.key, request.app, request.guid, mirror);
    RefreshSourceList(window);
    OpenSyncPage(window, request);
}

// The output an app is heard on right now.
std::wstring AppFirstOutputId(DWORD pid, const std::wstring& exe) {
    std::wstring id = DefaultOutputId();
    if (IAudioPolicyConfigFactory* config = CreateAudioPolicyConfig()) {
        id = HeardDeviceId(config, pid, exe, id);
        config->Release();
    }
    return id;
}

// The label of the entry in the menus, and whether it is on.
std::wstring MirrorMenuLabel(const std::wstring& key) {
    DeviceMirror mirror = LoadMirrorValue(key);
    if (!DeviceMirrorActive(mirror)) return L"Also play on another output";
    std::wstring name = OutputNameForId(mirror.target);
    return L"Also playing on: " + (name.empty() ? std::wstring(L"another output") : name);
}

// A click on a row's "EQ" or "LM" badge.
void OpenEqualizerForRow(HWND window, int row, bool effects = false) {
    if (row == DRAG_MASTER) {
        RunEqualizer(window, DefaultOutputId(), g_endpointName, L"", effects);
    } else if (row >= 0 && row < static_cast<int>(g_apps->size())) {
        const AppSession& app = (*g_apps)[row];
        std::wstring name = app.name;
        if (app.IsDevice()) {
            RunEqualizer(window, app.deviceId, name, L"", effects);
        } else if (!app.executablePath.empty()) {
            RunEqualizer(window, L"", name, ExeFileName(app.executablePath), effects);
        }
    }
}

// Ctrl+right-click on master volume: the default output's own menu.
void ShowStartVolumeMenu(HWND window, bool device, const std::wstring& deviceId, const std::wstring& exe,
                         int volumeNow, POINT screenPoint);
std::wstring DefaultDeviceId(EDataFlow flow);

void ShowMasterMenu(HWND window, POINT screenPoint) {
    if (g_sourceMenuOpen || g_outputMenuOpen || g_dragRow != DRAG_NONE) return;
    FinishVolumeEntry(window, false, false);
    CancelNameTooltip(window);
    SelectRow(DRAG_MASTER);
    std::vector<OutputDevice> items;
    int mirrorIndex = -1, syncIndex = -1;
    SyncRequest request;
    items.push_back({L"", L"Output device", L"\uE7F5", false});
    if (g_flow == eRender) {
        items.push_back({L"", L"Equalizer", L"\uE9E9", g_endpointName.empty()});
        items.push_back({L"", L"Effects", L"\uE9D9", g_endpointName.empty()});
        request.first = DefaultOutputId();
        request.guid = EndpointGuid(request.first);
        request.key = L"devMirror_v1:" + request.guid;
        request.name = g_endpointName;
        static const wchar_t mirrorGlyph[] = {0xE7F5, 0}, syncGlyph[] = {0xE916, 0};
        mirrorIndex = static_cast<int>(items.size());
        items.push_back({L"", MirrorMenuLabel(request.key), mirrorGlyph, request.first.empty()});
    }
    const std::wstring defaultDeviceId = DefaultDeviceId(g_flow);
    const int startVolume = defaultDeviceId.empty() ? -1 : Wh_GetIntValue(DeviceStartKey(defaultDeviceId).c_str(), -1);
    const int startIndex = static_cast<int>(items.size());
    items.push_back({L"", startVolume >= 0 ? L"Volume at startup: " + std::to_wstring(startVolume) + L" %"
                                           : std::wstring(L"Volume at startup"),
                     L"\uE7E8", defaultDeviceId.empty()});
    g_sourceMenuOpen = true;
    KillTimer(window, TIMER_CHECK_FOCUS);
    SetForegroundWindow(window);
    InvalidateRect(window, nullptr, FALSE);
    int chosen = RunOutputList(window, items, -1, L"", nullptr, &screenPoint);
    g_sourceMenuOpen = false;
    if (!IsWindow(window)) return;
    if (chosen == 0) {
        RECT client{};
        GetClientRect(window, &client);
        RECT anchor = RowRect(DRAG_MASTER, client.right);
        ShowOutputPicker(window, &anchor);
        return;
    }
    if (chosen >= 0 && chosen == mirrorIndex) {
        ChooseMirror(window, request, screenPoint);
        return;
    }
    if (chosen >= 0 && chosen == syncIndex) {
        OpenSyncPage(window, request);
        return;
    }
    if (chosen == startIndex) {
        int now = static_cast<int>(std::lround(std::min(GetRowMasterVolume(), 1.0f) * 100.0f));
        ShowStartVolumeMenu(window, true, defaultDeviceId, L"", now, screenPoint);
        return;
    }
    if (chosen == 1 || chosen == 2) {
        RunEqualizer(window, DefaultOutputId(), g_endpointName, L"", chosen == 2);
        return;
    }
    InvalidateRect(window, nullptr, FALSE);
    if (g_settings.closeWhenFocusIsLost && IsWindowVisible(window)) {
        SetTimer(window, TIMER_CHECK_FOCUS, 150, nullptr);
    }
}

// Lets an app follow the default output again.
void ReleaseAppOutput(HWND window, int row) {
    if (row < 0 || row >= static_cast<int>(g_apps->size())) return;
    std::vector<DWORD> processIds = (*g_apps)[row].processIds;
    if (processIds.empty() && (*g_apps)[row].processId) {
        processIds.push_back((*g_apps)[row].processId);
    }
    IAudioPolicyConfigFactory* config =
        processIds.empty() ? nullptr : CreateAudioPolicyConfig();
    if (!config) return;
    if (FAILED(RouteApps(config, processIds, L""))) {
        Wh_Log(L"Mixer: could not let the app follow the default output");
    }
    config->Release();
    std::wstring key = (*g_apps)[row].pinKey;
    RefreshSourceList(window);
    for (size_t i = 0; i < g_apps->size(); ++i) {
        if ((*g_apps)[i].pinKey == key) SelectRow(static_cast<int>(i));
    }
}

// The output list for one app: the Windows default first, then each output.
// The output a listened-to input plays on: the default one, or a chosen output.
void ShowMonitorOutputPicker(HWND window, int row) {
    if (g_outputMenuOpen || g_dragRow != DRAG_NONE || row < 0 || row >= static_cast<int>(g_apps->size())) return;
    const std::wstring key = (*g_apps)[row].pinKey;
    const std::wstring exe = (*g_apps)[row].executablePath;
    g_outputMenuOpen = true;
    g_outputListAnchored = true;
    KillTimer(window, TIMER_CHECK_FOCUS);
    SelectRow(row);

    std::vector<OutputDevice> outputs;
    std::wstring defaultId;
    HRESULT result = LoadOutputDevices(outputs, defaultId);
    std::wstring defaultName = L"Follow default";
    for (const OutputDevice& output : outputs) {
        if (output.id == defaultId) {
            defaultName += L" (" + output.name + L")";
            break;
        }
    }
    outputs.insert(outputs.begin(), OutputDevice{L"", defaultName, L""});
    std::wstring target = FxTarget(exe);
    int current = target.empty() || target == FX_FOLLOW_DEFAULT ? 0 : -1;
    for (size_t i = 1; i < outputs.size(); ++i) {
        if (!target.empty() && _wcsicmp(outputs[i].id.c_str(), target.c_str()) == 0) current = static_cast<int>(i);
    }
    RECT client;
    GetClientRect(window, &client);
    RECT anchor = RowRect(row, client.right);
    SetForegroundWindow(window);
    InvalidateRect(window, nullptr, FALSE);
    int selected = RunOutputList(window, outputs, current,
                                 FAILED(result) ? L"Could not load audio outputs" : L"No available audio outputs", &anchor);
    g_outputListAnchored = false;
    if (IsWindow(window) && selected >= 0 && selected != current) {
        SetFxTarget(exe, selected == 0 ? std::wstring(FX_FOLLOW_DEFAULT) : outputs[selected].id);
        FxSettingsChanged(exe);
    }
    g_outputMenuOpen = false;
    if (!IsWindow(window)) return;
    if (selected >= 0 && selected != current) {
        RefreshSourceList(window);
        for (size_t i = 0; i < g_apps->size(); ++i) {
            if ((*g_apps)[i].pinKey == key) SelectRow(static_cast<int>(i));
        }
    }
    InvalidateRect(window, nullptr, FALSE);
    if (g_settings.closeWhenFocusIsLost && IsWindowVisible(window)) SetTimer(window, TIMER_CHECK_FOCUS, 150, nullptr);
}

void ShowAppOutputPicker(HWND window, int row) {
    CancelNameTooltip(window);
    if (g_flow == eCapture) return;  // Apps pick their output only in the playback tab.
    if (row >= 0 && row < static_cast<int>(g_apps->size()) && (*g_apps)[row].virtualInput) {
        ShowMonitorOutputPicker(window, row);
        return;
    }
    if (g_outputMenuOpen || g_dragRow != DRAG_NONE || row < 0 ||
        row >= static_cast<int>(g_apps->size())) {
        return;
    }
    std::vector<DWORD> processIds = (*g_apps)[row].processIds;
    if (processIds.empty() && (*g_apps)[row].processId) {
        processIds.push_back((*g_apps)[row].processId);
    }
    const std::wstring key = (*g_apps)[row].pinKey;
    IAudioPolicyConfigFactory* config =
        processIds.empty() ? nullptr : CreateAudioPolicyConfig();
    if (!config) {
        MessageBoxW(window, L"This version of Windows does not let the mixer choose an output "
                    L"for this app.\n\nYou can still choose one in the Windows volume mixer.",
                    L"Headroom", MB_OK | MB_ICONINFORMATION);
        return;
    }

    g_outputMenuOpen = true;
    g_outputListAnchored = true;
    KillTimer(window, TIMER_CHECK_FOCUS);
    SelectRow(row);

    std::vector<OutputDevice> outputs;
    std::wstring defaultId;
    HRESULT result = LoadOutputDevices(outputs, defaultId);
    std::wstring defaultName = L"Follow default";
    for (const OutputDevice& output : outputs) {
        if (output.id == defaultId) {
            defaultName += L" (" + output.name + L")";
            break;
        }
    }
    // Following the default is offered first, under the speaker icon.
    outputs.insert(outputs.begin(), OutputDevice{L"", defaultName, L"\uE7F5"});

    std::wstring routed =
        HeardOutput(config, processIds.front(), (*g_apps)[row].executablePath);
    int current = routed.empty() ? 0 : -1;
    for (size_t i = 1; i < outputs.size(); ++i) {
        if (!routed.empty() && _wcsicmp(outputs[i].id.c_str(), routed.c_str()) == 0) {
            current = static_cast<int>(i);
        }
    }

    RECT client;
    GetClientRect(window, &client);
    RECT anchor = RowRect(row, client.right);
    SetForegroundWindow(window);
    InvalidateRect(window, nullptr, FALSE);
    g_outputListAllowHide = true;
    int selected = RunOutputList(window, outputs, current,
        FAILED(result) ? L"Could not load audio outputs" : L"No available audio outputs",
        &anchor);
    g_outputListAllowHide = false;
    g_outputListAnchored = false;
    bool reopen = HandleOutputListExtras(outputs, selected);
    if (reopen) selected = -1;

    if (IsWindow(window) && selected >= 0 && selected != current) {
        result = RouteApps(config, processIds, outputs[selected].id);
        if (FAILED(result)) {
            MessageBoxW(window, L"Windows could not change the output device of this app."
                        L"\n\nYou can also choose it in the Windows volume mixer.",
                        L"Headroom", MB_OK | MB_ICONWARNING);
        }
    }
    config->Release();
    g_outputMenuOpen = false;
    if (!IsWindow(window)) return;

    if (selected >= 0 && selected != current) {
        RefreshSourceList(window);
        // Keep the app selected where it now is.
        for (size_t i = 0; i < g_apps->size(); ++i) {
            if ((*g_apps)[i].pinKey == key) SelectRow(static_cast<int>(i));
        }
    }
    if (g_pendingDefaultOutputChange) {
        g_pendingDefaultOutputChange = false;
        PostMessageW(window, WM_APP_DEFAULT_OUTPUT_CHANGED, 0, 0);
    }
    InvalidateRect(window, nullptr, FALSE);
    if (reopen && IsWindowVisible(window)) {
        for (size_t i = 0; i < g_apps->size(); ++i) {
            if ((*g_apps)[i].pinKey == key) {
                ShowAppOutputPicker(window, static_cast<int>(i));
                return;
            }
        }
    }
    if (g_settings.closeWhenFocusIsLost && IsWindowVisible(window))
        SetTimer(window, TIMER_CHECK_FOCUS, 150, nullptr);
}

std::wstring DeviceStartKey(const std::wstring& id) {
    return L"devVol_v1:" + EndpointGuid(id);
}

// Sets every output and input that has a volume at startup to it. Once per device and run: what you
// change afterwards is yours.
void ApplyDeviceStartVolumes() {
    static std::unordered_set<std::wstring> applied;
    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                                __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator)))) {
        return;
    }
    for (EDataFlow flow : {eRender, eCapture}) {
        IMMDeviceCollection* devices = nullptr;
        if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &devices)) || !devices) continue;
        UINT count = 0;
        devices->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            IMMDevice* device = nullptr;
            if (FAILED(devices->Item(i, &device)) || !device) continue;
            LPWSTR raw = nullptr;
            if (SUCCEEDED(device->GetId(&raw)) && raw) {
                std::wstring id = raw;
                int percent = applied.count(id) ? -1 : Wh_GetIntValue(DeviceStartKey(id).c_str(), -1);
                if (percent >= 0 && percent <= 100) {
                    IAudioEndpointVolume* volume = nullptr;
                    if (SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER,
                                                   nullptr, reinterpret_cast<void**>(&volume))) && volume) {
                        volume->SetMasterVolumeLevelScalar(percent / 100.0f, nullptr);
                        volume->Release();
                        applied.insert(id);
                        Wh_Log(L"Mixer: output volume at startup, %d %%", percent);
                    }
                }
                CoTaskMemFree(raw);
            }
            device->Release();
        }
        devices->Release();
    }
    enumerator->Release();
}

// Off, the volume it has now, or a fixed one: for a device (by its id) or an app (by its executable).
void ShowStartVolumeMenu(HWND window, bool device, const std::wstring& deviceId, const std::wstring& exe,
                         int volumeNow, POINT screenPoint) {
    if (g_sourceMenuOpen || g_outputMenuOpen) return;
    static std::wstring nowLabel;
    nowLabel = L"Use the current volume (" + std::to_wstring(volumeNow) + L" %)";
    std::vector<OutputDevice> options;
    options.push_back({L"", L"Off", L"\uE711", false});
    options.push_back({L"", nowLabel, L"\uE73E", false});
    static constexpr int fixed[5] = {10, 25, 50, 75, 100};
    for (int percent : fixed) options.push_back({L"", std::to_wstring(percent) + L" %", L"\uE767", false});
    g_sourceMenuOpen = true;
    KillTimer(window, TIMER_CHECK_FOCUS);
    SetForegroundWindow(window);
    int pick = RunOutputList(window, options, -1, L"", nullptr, &screenPoint);
    g_sourceMenuOpen = false;
    if (!IsWindow(window)) return;
    if (pick >= 0) {
        int percent = pick == 0 ? -1 : pick == 1 ? volumeNow : fixed[pick - 2];
        if (device) {
            if (percent < 0) Wh_DeleteValue(DeviceStartKey(deviceId).c_str());
            else Wh_SetIntValue(DeviceStartKey(deviceId).c_str(), percent);
            RefreshSourceList(window);
        } else if (!exe.empty()) {
            SetAppStartVolume(exe, percent);
            PostMessageW(window, WM_APP_RELOAD_SETTINGS, 0, 0);  // The app is set to it at once.
        }
    }
    if (g_settings.closeWhenFocusIsLost && IsWindowVisible(window)) {
        SetTimer(window, TIMER_CHECK_FOCUS, 150, nullptr);
    }
}

std::wstring DefaultDeviceId(EDataFlow flow) {
    std::wstring id;
    IMMDeviceEnumerator* enumerator = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                                   __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator)))) {
        IMMDevice* device = nullptr;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(flow, eMultimedia, &device)) && device) {
            LPWSTR raw = nullptr;
            if (SUCCEEDED(device->GetId(&raw)) && raw) {
                id = raw;
                CoTaskMemFree(raw);
            }
            device->Release();
        }
        enumerator->Release();
    }
    return id;
}

// A click on a row's "Start 50%" badge.
void OpenStartVolumeForRow(HWND window, int row, POINT screenPoint) {
    if (row == DRAG_MASTER) {
        int now = static_cast<int>(std::lround(std::min(GetRowMasterVolume(), 1.0f) * 100.0f));
        ShowStartVolumeMenu(window, true, DefaultDeviceId(g_flow), L"", now, screenPoint);
    } else if (row >= 0 && row < static_cast<int>(g_apps->size())) {
        const AppSession& app = (*g_apps)[row];
        int now = static_cast<int>(std::lround(std::min(app.volume, 1.0f) * 100.0f));
        ShowStartVolumeMenu(window, app.IsDevice(), app.deviceId, ExeFileName(app.executablePath), now,
                            screenPoint);
    }
}

// The app's context menu, drawn like the output list.
void ShowSourceMenu(HWND window, int row, POINT screenPoint) {
    if (g_sourceMenuOpen || g_outputMenuOpen || g_dragRow != DRAG_NONE ||
        row < 0 || row >= static_cast<int>(g_apps->size())) {
        return;
    }

    const std::wstring key = (*g_apps)[row].pinKey;
    const std::wstring processName =
        ProcessNameFromPath((*g_apps)[row].executablePath);
    bool routable = (*g_apps)[row].processId || !(*g_apps)[row].processIds.empty() || (*g_apps)[row].virtualInput;

    FinishVolumeEntry(window, false, false);
    CancelNameTooltip(window);
    SelectRow(row);

    // System sounds and sessions without a process cannot be routed.
    enum MenuItem { OUTPUT, RENAME, COPY, HIDE, HIDE_PERMANENTLY, MAKE_DEFAULT, MOVE_ALL_APPS,
                    BOOST, HOTKEY, EQUALIZER, EFFECTS, STARTVOL, STOPLISTEN, LISTEN, MIRROR, SYNC };
    const std::wstring executablePath = (*g_apps)[row].executablePath;
    const bool systemSoundsRow = (*g_apps)[row].systemSounds;
    const bool boosted = BoostUnlocked(executablePath);
    const bool device = (*g_apps)[row].IsDevice();
    const std::wstring deviceId = (*g_apps)[row].deviceId;
    std::vector<OutputDevice> items;
    std::vector<int> actions;
    auto add = [&](int action, PCWSTR text, PCWSTR glyph, bool disabled) {
        items.push_back({L"", text, glyph, disabled});
        actions.push_back(action);
    };
    if (device) {
        add(MAKE_DEFAULT, L"Set as default output", L"\uE7F5", (*g_apps)[row].defaultOutput);
        if (g_flow == eRender) {
            add(MOVE_ALL_APPS, L"Move all apps here", L"\uE8AB", false);
            add(EQUALIZER, L"Equalizer", L"\uE9E9", false);
            add(EFFECTS, L"Effects", L"\uE9D9", false);
        } else {
            add(EQUALIZER, L"Equalizer", L"\uE9E9", false);
            add(EFFECTS, L"Effects and listening", L"\uE9D9", false);
            add(LISTEN, MonitorOn(EndpointGuid(deviceId)) ? L"Stop listening through Headroom" : L"Listen through Headroom",
                L"", false);
        }
    } else if (g_flow == eRender) {
        add(OUTPUT, L"Output device", L"\uE7F5", !routable);
    }
    add(RENAME, L"Rename", L"\uE8AC", false);
    // A volume the app (or the device) is set to whenever it starts.
    const int startVolume = device ? Wh_GetIntValue(DeviceStartKey(deviceId).c_str(), -1)
                                   : executablePath.empty() ? -1 : AppStartVolume(ExeFileName(executablePath));
    const int volumeNow = std::clamp(
        static_cast<int>(std::lround(std::min((*g_apps)[row].volume, 1.0f) * 100.0f)), 0, 100);
    if (device || (g_flow == eRender && !executablePath.empty())) {
        add(STARTVOL, startVolume >= 0 ? (L"Volume at startup: " + std::to_wstring(startVolume) + L" %").c_str()
                                       : L"Volume at startup",
            L"\uE7E8", false);
    }
    if (!device && g_flow == eRender) {
        add(BOOST, boosted ? L"Limit volume to 100%" : L"Unlock more volume (up to 500%)",
            L"\uE995", executablePath.empty());
    }
    std::wstring hotkeyLabel = L"Set mute hotkey";
    if (const MuteHotkey* hotkey = FindMuteHotkey(executablePath)) {
        hotkeyLabel = L"Mute hotkey: " + HotkeyText(hotkey->modifiers, hotkey->key);
    }
    if (!device && g_flow == eRender) add(EQUALIZER, L"Equalizer", L"\uE9E9", executablePath.empty());
    if (!device && g_flow == eRender) add(EFFECTS, L"Effects", L"\uE9D9", executablePath.empty());
    // Also played on a second output: of an output, or of an app (and its output, whatever that is).
    SyncRequest mirrorRequest;
    static const wchar_t mirrorGlyph[] = {0xE7F5, 0}, syncGlyph[] = {0xE916, 0};
    const bool canMirror = g_flow == eRender && (device ? !deviceId.empty() : routable && !executablePath.empty());
    if (canMirror) {
        if (device) {
            mirrorRequest.first = deviceId;
            mirrorRequest.guid = EndpointGuid(deviceId);
            mirrorRequest.key = L"devMirror_v1:" + mirrorRequest.guid;
        } else {
            mirrorRequest.app = ExeFileName(executablePath);
            mirrorRequest.key = L"appMirror_v1:" + mirrorRequest.app;
        }
        mirrorRequest.name = (*g_apps)[row].name;
        add(MIRROR, MirrorMenuLabel(mirrorRequest.key).c_str(), mirrorGlyph, false);
    }
    if (!device && g_flow == eRender) add(HOTKEY, hotkeyLabel.c_str(), L"\uE765", executablePath.empty());
    if (!device && !(*g_apps)[row].virtualInput) add(COPY, L"Copy process name", L"\uE8C8", processName.empty());
    if ((*g_apps)[row].virtualInput) add(STOPLISTEN, L"Stop listening", L"", false);
    add(HIDE, L"Hide temporarily", L"\uE916", false);
    add(HIDE_PERMANENTLY, L"Hide permanently", L"\uED1A", false);

    g_sourceMenuOpen = true;
    KillTimer(window, TIMER_CHECK_FOCUS);
    SetForegroundWindow(window);
    InvalidateRect(window, nullptr, FALSE);
    int chosen = RunOutputList(window, items, -1, L"", nullptr, &screenPoint);
    int command = chosen >= 0 ? actions[chosen] : -1;

    if (IsWindow(window)) {
        if (command == COPY && !processName.empty()) {
            if (!CopyProcessName(window, processName)) {
                Wh_Log(L"Mixer: could not copy the process name to the clipboard");
                MessageBoxW(window,
                            L"Could not copy the process name. Please try again.",
                            L"Headroom",
                            MB_OK | MB_ICONWARNING);
            }
        } else if (command == HIDE) {
            g_temporarilyHiddenSources.insert(key);
            if (systemSoundsRow) g_temporarilyHiddenSources.insert(SYSTEM_SOUNDS_KEY);
        } else if (command == HIDE_PERMANENTLY &&
                   [&] {
#ifdef HEADROOM_STANDALONE
                       // The app's name goes to the settings' list of hidden apps, where
                       // it can be seen and taken out again.
                       return !device && !systemSoundsRow && !executablePath.empty() &&
                              AppHideByName(ExeFileName(executablePath));
#else
                       return false;
#endif
                   }()) {
            // Hidden by name.
        } else if (command == HIDE_PERMANENTLY) {
            LoadPermanentlyHiddenSources();
            g_permanentlyHiddenSources.insert(key);
            if (systemSoundsRow) g_permanentlyHiddenSources.insert(SYSTEM_SOUNDS_KEY);
            if (!SavePermanentlyHiddenSources()) {
                Wh_Log(L"Mixer: could not save the permanently hidden source");
                MessageBoxW(window,
                            L"Could not save this choice. The app is hidden until the mod restarts.",
                            L"Headroom", MB_OK | MB_ICONWARNING);
            }
        }
    }

    g_sourceMenuOpen = false;
    if (g_pendingDefaultOutputChange) {
        g_pendingDefaultOutputChange = false;
        PostMessageW(window, WM_APP_DEFAULT_OUTPUT_CHANGED, 0, 0);
    }
    if (!IsWindow(window)) return;

    if (command == HIDE || command == HIDE_PERMANENTLY) {
        RefreshSourceList(window);
    }

    if (command == BOOST) {
        if (boosted) {
            // Back to what Windows allows.
            for (size_t i = 0; i < g_apps->size(); ++i) {
                if ((*g_apps)[i].pinKey == key && (*g_apps)[i].volume > 1.0f) {
                    SetRowVolume(static_cast<int>(i), 1.0f);
                }
            }
            RemoveBoost(executablePath);
        } else {
            SetBoostPercent(executablePath, 100);
            if (!Wh_GetIntValue(L"boostInfoShown", 0) && executablePath.rfind(L"in:", 0) != 0) {
                Wh_SetIntValue(L"boostInfoShown", 1);
                MessageBoxW(window,
                    L"The slider of this app now goes up to 500%.\n\n"
                    L"Above 100%, the mixer plays the app itself: Windows routes it to "
                    L"VB-CABLE, a silent virtual output, and the mixer records just this app "
                    L"and plays it louder on your real output. Nothing is loaded into the app, "
                    L"so this is safe with anti-cheat. It adds about 40 ms of delay.",
                    L"Headroom", MB_OK | MB_ICONINFORMATION);
            }
        }
    }

    if (command == MOVE_ALL_APPS) {
        // Every app in the mixer, including those listed on other outputs.
        std::vector<DWORD> processIds = AllAppProcessIds();
        // On the default output, apps simply follow the default again, so
        // they also go along when it changes later.
        std::wstring target = deviceId;
        if (_wcsicmp(DefaultOutputId().c_str(), deviceId.c_str()) == 0) target.clear();
        IAudioPolicyConfigFactory* config =
            processIds.empty() ? nullptr : CreateAudioPolicyConfig();
        if (!processIds.empty() && !config) {
            MessageBoxW(window, L"This version of Windows does not let the mixer choose an output "
                        L"for apps.\n\nYou can still choose one per app in the Windows volume mixer.",
                        L"Headroom", MB_OK | MB_ICONINFORMATION);
        } else if (config) {
            HRESULT result = RouteApps(config, processIds, target);
            config->Release();
            if (FAILED(result)) {
                MessageBoxW(window, L"Windows could not move every app to this output. "
                            L"Some apps may have closed in the meantime.",
                            L"Headroom", MB_OK | MB_ICONWARNING);
            }
            RefreshSourceList(window);
        }
    }

    if (command == MAKE_DEFAULT) {
        HRESULT result = SetPlaybackOutput(deviceId);
        if (FAILED(result)) {
            Wh_Log(L"Mixer: output switch failed: 0x%08lX", static_cast<unsigned long>(result));
            MessageBoxW(window, L"Windows could not fully switch the audio output. The device may have disconnected.",
                        L"Headroom", MB_OK | MB_ICONWARNING);
        }
        SelectRow(DRAG_MASTER);
        RefreshSourceList(window);
    }

    if (command == STOPLISTEN || command == LISTEN) {
        std::wstring guid = command == LISTEN ? EndpointGuid(deviceId) : InputGuid(executablePath);
        SetMonitorOn(guid, command == LISTEN && !MonitorOn(guid));
        g_fxRetryAfter.erase(L"in:" + guid);
        MonitorsChanged();
        RefreshSourceList(window);
        return;
    }

    if (command == MIRROR || command == SYNC) {
        if (!device) {
            DWORD pid = (*g_apps)[row].processId ? (*g_apps)[row].processId
                        : (*g_apps)[row].processIds.empty() ? 0 : (*g_apps)[row].processIds.front();
            mirrorRequest.first = AppFirstOutputId(pid, mirrorRequest.app);
        }
        if (command == MIRROR) {
            ChooseMirror(window, mirrorRequest, screenPoint);
        } else {
            OpenSyncPage(window, mirrorRequest);
        }
        return;
    }

    if (command == STARTVOL) {
        ShowStartVolumeMenu(window, device, deviceId, ExeFileName(executablePath), volumeNow, screenPoint);
        return;
    }

    if (command == EQUALIZER || command == EFFECTS) {
        bool effects = command == EFFECTS;
        if (device && g_flow == eCapture) {
            RunEqualizer(window, L"", (*g_apps)[row].name, InputKey(deviceId), effects);
        } else if (device) {
            RunEqualizer(window, deviceId, (*g_apps)[row].name, L"", effects);
        } else {
            std::wstring name = row < static_cast<int>(g_apps->size())
                ? (*g_apps)[row].name : ExeFileName(executablePath);
            RunEqualizer(window, L"", name, ExeFileName(executablePath), effects);
        }
        return;
    }

    if (command == OUTPUT || command == RENAME || command == HOTKEY) {
        // The rows may have been rebuilt while the menu was open.
        for (size_t i = 0; i < g_apps->size(); ++i) {
            if ((*g_apps)[i].pinKey == key) {
                if (command == OUTPUT) {
                    ShowAppOutputPicker(window, static_cast<int>(i));
                } else if (command == HOTKEY) {
                    BeginHotkeyEntry(window, static_cast<int>(i));
                } else {
                    BeginNameEntry(window, static_cast<int>(i));
                }
                return;
            }
        }
    }

    InvalidateRect(window, nullptr, FALSE);
    if (g_settings.closeWhenFocusIsLost && IsWindowVisible(window)) {
        SetTimer(window, TIMER_CHECK_FOCUS, 150, nullptr);
    }
}

void HandlePointerMove(HWND hWnd, POINT point) {
    RECT client;
    GetClientRect(hWnd, &client);
    if (g_scrollDragging) {
        ScrollToThumb(hWnd, client.right, point.y - g_scrollGrab);
        return;
    }
    RECT track = ScrollTrackRect(client.right);
    bool scrollHovered = AppsScroll() && PtInRect(&track, point) != FALSE;
    if (scrollHovered != g_scrollHovered) {
        g_scrollHovered = scrollHovered;
        InvalidateRect(hWnd, nullptr, FALSE);
    }
    UpdateNameTooltip(hWnd, point);
    RECT closeRect = CloseButtonRect(client.right);
    RECT pickerRect = OutputPickerRect(client.right);
    bool outputHovered = PtInRect(&pickerRect, point) != FALSE;
    if (outputHovered != g_outputHovered) {
        g_outputHovered = outputHovered;
        InvalidateRect(hWnd, nullptr, FALSE);
    }
    bool closeHovered = PtInRect(&closeRect, point) != FALSE;
    if (closeHovered != g_closeHovered) {
        g_closeHovered = closeHovered;
        InvalidateRect(hWnd, nullptr, FALSE);
    }
    bool badgeHovered = BadgeAt(point) != DRAG_NONE || EqBadgeAt(point) != DRAG_NONE ||
                        EffectsBadgeAt(point) != DRAG_NONE || SvBadgeAt(point) != DRAG_NONE;
    if (badgeHovered != g_badgeHovered) {
        g_badgeHovered = badgeHovered;
        InvalidateRect(hWnd, nullptr, FALSE);
    }
    RECT flowRect = FlowButtonRect(client.right);
    bool flowHovered = PtInRect(&flowRect, point) != FALSE;
    if (flowHovered != g_flowButtonHovered) {
        g_flowButtonHovered = flowHovered;
        InvalidateRect(hWnd, nullptr, FALSE);
    }
    RECT outputsRect = OutputsButtonRect(client.right);
    bool outputsHovered = PtInRect(&outputsRect, point) != FALSE;
    if (outputsHovered != g_outputsButtonHovered) {
        g_outputsButtonHovered = outputsHovered;
        InvalidateRect(hWnd, nullptr, FALSE);
    }
    int row = RowFromPoint(point);
    bool thumbHovered = false;
    if (row != DRAG_NONE) {
        RECT slider = SliderRectForRow(VisibleRowForDataRow(row), client.right);
        float volume = row == DRAG_MASTER ? g_masterPaintVolume
                                          : (*g_apps)[row].volume;
        thumbHovered = SliderThumbContains(slider, volume, point);
    }
    g_pointer = point;
    if (row != g_hoverRow || thumbHovered != g_hoverThumb) {
        g_hoverRow = row;
        g_hoverThumb = thumbHovered;
        InvalidateRect(hWnd, nullptr, FALSE);
    }

    if (!g_mouseTracking) {
        TRACKMOUSEEVENT tracking = {sizeof(tracking), TME_LEAVE, hWnd, 0};
        TrackMouseEvent(&tracking);
        g_mouseTracking = true;
    }

    if (g_dragRow != DRAG_NONE && (GetKeyState(VK_LBUTTON) & 0x8000)) {
        ApplyVolumeFromPoint(hWnd, g_dragRow, point.x);
    }
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam,
                            LPARAM lParam) {
    if (message == g_taskbarCreatedMessage && g_taskbarCreatedMessage) {
        ForgetTrayIconRect();
        AddTrayIcon(hWnd);
        return 0;
    }

    // A flyout on its way out no longer reacts to input. One that is still
    // on its way in is put in place first, and takes a click where the
    // pointer then is over it.
    if (g_closeTime) {
        if ((message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) ||
            (message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
            message == WM_CONTEXTMENU) {
            return 0;
        }
    } else if (g_openTime && (message == WM_LBUTTONDOWN || message == WM_MBUTTONDOWN ||
                              message == WM_MOUSEWHEEL || message == WM_CONTEXTMENU)) {
        SettleFlyout(hWnd);
        if (message == WM_LBUTTONDOWN || message == WM_MBUTTONDOWN) {
            DWORD position = GetMessagePos();
            POINT point = {GET_X_LPARAM(position), GET_Y_LPARAM(position)};
            ScreenToClient(hWnd, &point);
            lParam = MAKELPARAM(point.x, point.y);
        }
    }

    switch (message) {
        case WM_CREATE: {
            Gdiplus::GdiplusStartupInput startup;
            if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &startup, nullptr) != Gdiplus::Ok) {
                g_gdiplusToken = 0;
                Wh_Log(L"Mixer: typography initialization failed; using GDI text");
            }
            CreateFonts(hWnd);
            UpdateTheme();

            // Bind the master endpoint before anything queries its mute state.
            // This registers the IAudioEndpointVolumeCallback immediately so
            // the tray icon and middle-click mute stay accurate even while the
            // mixer is hidden.
            BindDefaultEndpointVolume(hWnd);

            RefreshIconSizes(hWnd);
            ApplyTransparencyStyle(hWnd);
            ApplyWindowFrame(hWnd);
            if (HasDefaultVolumeRules()) {
                RefreshAudioSessions(hWnd);
                StartDefaultVolumeNotifications(hWnd);
            }
            RefreshMasterIcons(hWnd, true);

            // Lifetime watcher: react to default-output changes even when the
            // mixer is hidden, so tray mute controls always target the current
            // device. CoCreateInstance here runs on the UI thread and the
            // notifications are marshalled through the window message below.
            IMMDeviceEnumerator* uiEnumerator = nullptr;
            if (SUCCEEDED(CoCreateInstance(
                    __uuidof(MMDeviceEnumerator), nullptr,
                    CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator),
                    reinterpret_cast<void**>(&uiEnumerator))) &&
                uiEnumerator) {
                auto* uiNotification = new EndpointNotification(nullptr);
                uiNotification->SetWindow(hWnd);
                if (SUCCEEDED(uiEnumerator->RegisterEndpointNotificationCallback(
                        uiNotification))) {
                    g_uiDeviceEnumerator = uiEnumerator;
                    g_uiEndpointNotification = uiNotification;
                } else {
                    Wh_Log(L"Mixer: default output notification registration failed");
                    uiNotification->Release();
                    uiEnumerator->Release();
                }
            }
            return 0;
        }

        case WM_APP_TRAY: {
            UINT event = g_trayVersion4 ? LOWORD(lParam)
                                        : static_cast<UINT>(lParam);
            if (event == WM_LBUTTONDOWN || event == WM_LBUTTONDBLCLK) {
                RememberTrayPress(hWnd);
            }
            if (event == WM_MOUSEMOVE && !g_trayWheelArmed && PointerOverTrayIcon(true)) {
                ArmTrayWheel();
            }
            TrayAction action = GetTrayAction(event, g_trayVersion4);
            if (action != TrayAction::None) {
                Wh_Log(L"Mixer: tray action event=0x%X", event);
            }
            if (action == TrayAction::ToggleMute) {
                FlowScope playback(eRender);
                ToggleRowMute(DRAG_MASTER);
                InvalidateRect(hWnd, nullptr, FALSE);
                ShowVolumeIndicator(hWnd);
            } else if (action == TrayAction::Open) {
                QueueTrayToggle(hWnd, event == NIN_KEYSELECT);
            } else if (action == TrayAction::Menu) {
                g_trayPressKnown = false;
                // WM_CONTEXTMENU coordinates in the tray callback are not
                // guaranteed. Anchor keyboard menus at the icon, others at
                // the cursor, rather than decoding an undefined wParam.
                POINT cursor = {};
                GetCursorPos(&cursor);
                RECT iconRect = {};
                if (GetKeyState(VK_RBUTTON) >= 0 && GetTrayIconRect(&iconRect)) {
                    cursor = {iconRect.left, iconRect.bottom};
                }
                ShowTrayMenu(hWnd, cursor.x, cursor.y);
            }
            return 0;
        }

        case WM_APP_SHOW_MIXER:
            if (g_showRequestPending) {
                g_showRequestPending = false;
                if (g_closeRequestPending) {
                    HideMixer(hWnd);
                } else {
                    ShowMixer(hWnd);
                }
            }
            return 0;

        case WM_APP_FINISH_VOLUME_ENTRY:
            if (g_volumeEntry && static_cast<ULONG_PTR>(lParam) == g_volumeEntryGeneration) {
                FinishVolumeEntry(hWnd, wParam != 0, wParam != 0);
            }
            return 0;

        case WM_APP_AUDIO_CHANGED:
            g_audioUiRefreshPosted.store(false);
            if (MixerOpen(hWnd)) {
                RefreshAudioSessions(hWnd);
                PositionMixer(hWnd);
                POINT point{};
                if (GetCursorPos(&point)) {
                    ScreenToClient(hWnd, &point);
                    UpdateNameTooltip(hWnd, point);
                }
            }
            return 0;

        case WM_APP_MASTER_STATE_CHANGED:
            RefreshMasterIcons(hWnd);
            return 0;

        case WM_APP_DEFAULT_OUTPUT_CHANGED: {
            ApplyDeviceStartVolumes();  // A device that has come back gets its volume.
            // A popup menu's modal loop delivers this message while the menu
            // is on screen. Shell_NotifyIconW (via RefreshMasterIcons) talks
            // to the taskbar and can demote the popup inside the topmost
            // band; the STA COM calls in BindDefaultEndpointVolume re-enter
            // the menu's message pump. Defer the whole update until the menu
            // closes and re-post the message from the menu paths below.
            if (g_sourceMenuOpen || g_outputMenuOpen) {
                g_pendingDefaultOutputChange = true;
                return 0;
            }
            bool canRefresh =
                MixerOpen(hWnd) && g_dragRow == DRAG_NONE &&
                !g_volumeEntry;
            if (canRefresh) {
                RefreshAudioSessions(hWnd);
                PositionMixer(hWnd);
            } else {
                BindDefaultEndpointVolume(hWnd);
            }
            RefreshMasterIcons(hWnd, true);
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }

        case WM_APP_RELOAD_SETTINGS:
            FinishFlyoutMotion(hWnd);
            FinishVolumeEntry(hWnd, false, false);
            CancelNameTooltip(hWnd);
            SelectRow(DRAG_MASTER);
            StopDefaultVolumeNotifications();
            LoadSettings();
            RegisterOpenHotkey(hWnd);
            WriteEqualizerConfig();  // Who runs the outputs' effects may have changed.
            CreateFonts(hWnd);  // The visual style selects the font families.
            if (!g_settings.moveNotifications) {
                ReturnNotifications();
            } else if (IsWindowVisible(hWnd)) {
                KeepNotificationsAway();
            }
            if (IsWindowVisible(hWnd)) {
                SetTimer(hWnd, TIMER_REFRESH, 1000, nullptr);
            } else {
                KillTimer(hWnd, TIMER_REFRESH);
            }
            StartDefaultVolumeNotifications(hWnd);
            g_transparencyFailed = false;
            // The frosted glass backdrop is tinted with the theme color.
            UpdateTheme();
            ApplyTransparencyStyle(hWnd);
            ApplyWindowFrame(hWnd);
            UpdateMotionPreference(hWnd);
            RefreshIconSizes(hWnd);
            RefreshAudioSessions(hWnd);
            ResizeMixer(hWnd);
            if (IsWindowVisible(hWnd)) {
                PositionMixer(hWnd);
            }
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;

        case WM_TIMER:
            if (wParam == TIMER_FX) {
                // Settings that could not be read at start-up are loaded as soon as the file can be read.
                if (headroom::RetryLoadStore()) PostMessageW(hWnd, WM_APP_RELOAD_SETTINGS, 0, 0);
                UpdateFx(hWnd);
                return 0;
            }
            if (wParam == TIMER_NAME_TOOLTIP) {
                ShowNameTooltip(hWnd);
                return 0;
            }
            if (wParam == TIMER_VOLUME_INDICATOR) {
                HideVolumeIndicator(hWnd);
                return 0;
            }
            if (wParam == TIMER_TRAY_WATCH) {
                // Retry a failed tray registration about every 3 seconds.
                static int ticksWithoutIcon = 0;
                if (!g_trayAdded && ++ticksWithoutIcon >= 6) {
                    ticksWithoutIcon = 0;
                    AddTrayIcon(hWnd);
                }
                WatchTrayPointer();
                return 0;
            }
            if (wParam == TIMER_TRIM) {
                KillTimer(hWnd, TIMER_TRIM);
                if (!IsWindowVisible(hWnd)) TrimMixerMemory();
                return 0;
            }
            if (wParam == TIMER_CHECK_FOCUS) {
                KillTimer(hWnd, TIMER_CHECK_FOCUS);
                HWND foreground = GetForegroundWindow();
                bool mixerOwnsFocus = foreground == hWnd ||
                    (foreground && GetAncestor(foreground, GA_ROOTOWNER) == hWnd);
                if (!g_showingMixer && !g_showRequestPending && !g_outputMenuOpen &&
                    !g_sourceMenuOpen && g_settings.closeWhenFocusIsLost && MixerOpen(hWnd) &&
                    !mixerOwnsFocus && GetCapture() != hWnd) {
                    Wh_Log(L"Mixer: closing after confirmed focus loss");
                    HideMixer(hWnd);
                }
                return 0;
            }
            if (wParam == TIMER_METERS && IsWindowVisible(hWnd)) {
                // Paint messages drive the opening and closing animations.
                // Should they ever stop coming, do not leave the flyout
                // stranded half way.
                ULONGLONG sliding = g_closeTime ? g_closeTime : g_openTime;
                if (sliding && AnimationClock() - sliding > 1000000) {
                    FinishFlyoutMotion(hWnd);
                }
                InvalidateRect(hWnd, nullptr, FALSE);
                return 0;
            }
            if (wParam == TIMER_REFRESH) {
                // KillTimer doesn't remove a timer message already in the queue.
                if (MixerOpen(hWnd)) {
                    RefreshAudioSessions(hWnd);
                    PositionMixer(hWnd);
                    POINT point{};
                    if (GetCursorPos(&point)) {
                        ScreenToClient(hWnd, &point);
                        UpdateNameTooltip(hWnd, point);
                    }
                }
                return 0;
            }
            break;

        case WM_PAINT:
            if (IsWindowVisible(hWnd)) {
                PaintMixer(hWnd);
            } else {
                ValidateRect(hWnd, nullptr);
            }
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_MOUSEMOVE: {
            POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            HandlePointerMove(hWnd, point);
            return 0;
        }

        // The hook only sends the wheel while the pointer is on the icon.
        case WM_APP_TRAY_WHEEL:
            ScrollMasterVolume(hWnd, static_cast<SHORT>(wParam));
            return 0;

        case WM_MOUSELEAVE:
            if (!g_outputMenuOpen) CancelNameTooltip(hWnd);
            g_outputHovered = false;
            g_mouseTracking = false;
            g_hoverRow = DRAG_NONE;
            g_hoverThumb = false;
            g_closeHovered = false;
            g_scrollHovered = false;
            g_outputsButtonHovered = false;
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;

        case WM_LBUTTONDOWN: {
            CancelNameTooltip(hWnd);
            FinishVolumeEntry(hWnd, false, false);
            SetFocus(hWnd);
            POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            RECT client;
            GetClientRect(hWnd, &client);

            RECT closeRect = CloseButtonRect(client.right);
            if (PtInRect(&closeRect, point)) {
                HideMixer(hWnd);
                return 0;
            }

            RECT flowRect = FlowButtonRect(client.right);
            if (PtInRect(&flowRect, point)) {
                g_flow = g_flow == eCapture ? eRender : eCapture;
                g_scrollRow = 0;
                SelectRow(DRAG_MASTER);
                RefreshSourceList(hWnd);
                return 0;
            }

            RECT outputsRect = OutputsButtonRect(client.right);
            if (PtInRect(&outputsRect, point)) {
                g_showOutputs = !g_showOutputs;
                Wh_SetIntValue(SHOW_OUTPUTS_VALUE, g_showOutputs ? 1 : 0);
                g_scrollRow = 0;
                SelectRow(DRAG_MASTER);
                RefreshSourceList(hWnd);
                return 0;
            }

            RECT pickerRect = OutputPickerRect(client.right);
            if (PtInRect(&pickerRect, point)) {
                ShowOutputPicker(hWnd);
                return 0;
            }

            if (AppsScroll() && g_audioAvailable) {
                RECT track = ScrollTrackRect(client.right);
                if (PtInRect(&track, point)) {
                    RECT thumb = ScrollThumbRect(client.right);
                    int height = thumb.bottom - thumb.top;
                    // Off the thumb, it first jumps to put its middle there.
                    if (point.y < thumb.top || point.y >= thumb.bottom) {
                        ScrollToThumb(hWnd, client.right, point.y - height / 2);
                        thumb = ScrollThumbRect(client.right);
                    }
                    g_scrollGrab = point.y - thumb.top;
                    g_scrollDragging = true;
                    CancelNameTooltip(hWnd);
                    SetCapture(hWnd);
                    InvalidateRect(hWnd, nullptr, FALSE);
                    return 0;
                }
            }

            int row = RowFromPoint(point);
            if (row == DRAG_NONE) {
                return 0;
            }
            if (row != DRAG_NONE && EqBadgeAt(point) == row) {
                OpenEqualizerForRow(hWnd, row);
                return 0;
            }
            if (row != DRAG_NONE && EffectsBadgeAt(point) == row) {
                OpenEqualizerForRow(hWnd, row, true);
                return 0;
            }
            if (row != DRAG_NONE && SvBadgeAt(point) == row) {
                POINT screen = point;
                ClientToScreen(hWnd, &screen);
                OpenStartVolumeForRow(hWnd, row, screen);
                return 0;
            }
            if (row >= 0 && BadgeAt(point) == row) {
                ReleaseAppOutput(hWnd, row);
                return 0;
            }
            SelectRow(row);
            InvalidateRect(hWnd, nullptr, FALSE);
            int visibleRow = VisibleRowForDataRow(row);
            RECT percentRect = PercentRectForRow(visibleRow, client.right);
            if (g_settings.exactVolumeEntry && PtInRect(&percentRect, point)) {
                BeginVolumeEntry(hWnd, row);
                return 0;
            }
            RECT pinRect = PinRectForRow(visibleRow, client.right);
            if (row >= 0 && PtInRect(&pinRect, point)) {
                ToggleSourcePin(hWnd, row);
                return 0;
            }
            RECT muteRect = MuteRectForRow(visibleRow, client.right);
            if (PtInRect(&muteRect, point)) {
                ToggleRowMute(row);
                InvalidateRect(hWnd, nullptr, FALSE);
                return 0;
            }

            RECT sliderRect = SliderHitRect(visibleRow, client.right);
            if (PtInRect(&sliderRect, point)) {
                g_dragRow = row;
                SetCapture(hWnd);
                ApplyVolumeFromPoint(hWnd, row, point.x);
            }
            return 0;
        }

        case WM_CONTEXTMENU: {
            if (reinterpret_cast<HWND>(wParam) != hWnd) break;

            POINT screenPoint{
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam)
            };
            int row;

            if (screenPoint.x == -1 && screenPoint.y == -1) {
                row = g_selectedRow;
                if (row == DRAG_MASTER) {
                    RECT client{};
                    GetClientRect(hWnd, &client);
                    RECT anchor = RowRect(DRAG_MASTER, client.right);
                    ShowOutputPicker(hWnd, &anchor);
                    return 0;
                }
                if (row < g_scrollRow ||
                    row >= g_scrollRow + VisibleAppCount()) {
                    return 0;
                }

                RECT client{};
                GetClientRect(hWnd, &client);
                RECT name =
                    NameRectForRow(VisibleRowForDataRow(row), client.right);
                screenPoint = {name.left, name.bottom};
                ClientToScreen(hWnd, &screenPoint);
            } else {
                POINT clientPoint = screenPoint;
                ScreenToClient(hWnd, &clientPoint);

                RECT client{};
                GetClientRect(hWnd, &client);
                if (!PtInRect(&client, clientPoint)) return 0;

                row = RowFromPoint(clientPoint);
                if (row == DRAG_MASTER && !g_sourceMenuOpen &&
                    (GetKeyState(VK_CONTROL) & 0x8000)) {
                    ShowMasterMenu(hWnd, screenPoint);
                    return 0;
                }
                if (row == DRAG_MASTER && !g_sourceMenuOpen) {
                    // The master row picks the output everything plays on.
                    SelectRow(DRAG_MASTER);
                    RECT anchor = RowRect(DRAG_MASTER, client.right);
                    ShowOutputPicker(hWnd, &anchor);
                    return 0;
                }
            }

            // A right-click goes straight to the app's output. With Ctrl, or
            // for sources that cannot be routed such as System sounds, it
            // opens the menu with everything else.
            bool routable = row >= 0 && row < static_cast<int>(g_apps->size()) &&
                            ((*g_apps)[row].processId || !(*g_apps)[row].processIds.empty());
            if ((GetKeyState(VK_CONTROL) & 0x8000) || !routable) {
                ShowSourceMenu(hWnd, row, screenPoint);
            } else if (!g_sourceMenuOpen) {
                FinishVolumeEntry(hWnd, false, false);
                ShowAppOutputPicker(hWnd, row);
            }
            return 0;
        }

        case WM_MBUTTONDOWN: {
            CancelNameTooltip(hWnd);
            if (g_dragRow != DRAG_NONE) return 0;
            FinishVolumeEntry(hWnd, false, false);
            int row = RowFromPoint({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            if (row != DRAG_NONE) {
                SelectRow(row);
                ToggleRowMute(row);
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONUP:
            if (g_dragRow != DRAG_NONE || g_scrollDragging) {
                g_dragRow = DRAG_NONE;
                g_scrollDragging = false;
                ReleaseCapture();
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            return 0;

        case WM_CAPTURECHANGED:
            g_dragRow = DRAG_NONE;
            if (g_scrollDragging) {
                g_scrollDragging = false;
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            return 0;

        case WM_MOUSEWHEEL: {
            if (g_volumeEntry || g_dragRow != DRAG_NONE) return 0;
            POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hWnd, &point);
            RECT client;
            GetClientRect(hWnd, &client);
            if (!PtInRect(&client, point)) return 0;
            int row = RowFromPoint(point);

            int notches = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            // While the apps scroll, the wheel changes an app's volume only
            // over its slider, mute button or percentage, and scrolls elsewhere.
            if (row >= 0 && AppsScroll()) {
                int visibleRow = VisibleRowForDataRow(row);
                RECT slider = SliderHitRect(visibleRow, client.right);
                RECT mute = MuteRectForRow(visibleRow, client.right);
                RECT percent = PercentRectForRow(visibleRow, client.right);
                if (!PtInRect(&slider, point) && !PtInRect(&mute, point) &&
                    !PtInRect(&percent, point)) {
                    if (notches) ChangePage(hWnd, -notches);
                    HandlePointerMove(hWnd, point);
                    return 0;
                }
            }
            if (row != DRAG_NONE && notches) {
                float current = row == DRAG_MASTER ? GetRowMasterVolume()
                                                   : (*g_apps)[row].volume;
                SetRowVolume(row, current + notches *
                                              g_settings.volumeStep / 100.0f);
                InvalidateRect(hWnd, nullptr, FALSE);
            } else if (!g_apps->empty() && notches) {
                ChangePage(hWnd, -notches);
                HandlePointerMove(hWnd, point);
            }
            return 0;
        }

        case WM_KEYDOWN:
            if (HandleRowKey(hWnd, wParam, lParam)) return 0;
            if (wParam == VK_ESCAPE) {
                HideMixer(hWnd);
                return 0;
            }
            if (wParam == VK_PRIOR || wParam == VK_NEXT) {
                ChangePage(hWnd, wParam == VK_NEXT ? PageSize() : -PageSize());
                return 0;
            }
            break;

        case WM_ACTIVATE:
            // The output list closes with the focus; make sure it hears of it.
            if (LOWORD(wParam) == WA_INACTIVE && g_outputList.window) {
                PostMessageW(hWnd, WM_NULL, 0, 0);
            }
            if (LOWORD(wParam) != WA_INACTIVE) {
                KillTimer(hWnd, TIMER_CHECK_FOCUS);
            } else if (!g_showingMixer && !g_showRequestPending && !g_outputMenuOpen && !g_sourceMenuOpen &&
                g_settings.closeWhenFocusIsLost && MixerOpen(hWnd)) {
                // Capture intent even if activation arrives before the tray's
                // mouse-down callback. Focus dismissal remains independent.
                POINT cursor = {};
                RECT iconRect = {};
                if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) &&
                    GetCursorPos(&cursor) && GetTrayIconRect(&iconRect) &&
                    PtInRect(&iconRect, cursor)) {
                    RememberTrayPress(hWnd);
                }
                // Do not hide inside a synchronous activation transition.
                // Recheck the actual foreground window after the shell settles.
                SetTimer(hWnd, TIMER_CHECK_FOCUS, 150, nullptr);
            }
            break;

        case WM_DWMCOLORIZATIONCOLORCHANGED:
            // The slider and other accent-colored controls use g_theme.accent.
            // Refresh it immediately when Windows changes the system accent,
            // even when the mixer itself is forced to the light or dark theme.
            UpdateTheme();
            InvalidateRect(hWnd, nullptr, FALSE);
            break;

        case WM_SETTINGCHANGE: {
            UpdateMotionPreference(hWnd);
            bool immersiveColorChanged =
                lParam && wcscmp(reinterpret_cast<PCWSTR>(lParam),
                                 L"ImmersiveColorSet") == 0;
            if (g_settings.theme == L"system" || immersiveColorChanged) {
                UpdateTheme();
                ApplyBackdropBlur(hWnd);
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            if (immersiveColorChanged) {
                RefreshIconSizes(hWnd);
            }
            break;
        }

        case WM_DISPLAYCHANGE:
            FinishVolumeEntry(hWnd, false, false);
            CancelNameTooltip(hWnd);
            RefreshIconSizes(hWnd);
            InvalidateRect(hWnd, nullptr, FALSE);
            break;

        case WM_DPICHANGED: {
            FinishFlyoutMotion(hWnd);
            FinishVolumeEntry(hWnd, false, false);
            CancelNameTooltip(hWnd);
            g_dpi = HIWORD(wParam);
            CreateFonts(hWnd);
            RefreshIconSizes(hWnd);
            RECT* suggested = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(hWnd, nullptr, suggested->left, suggested->top,
                         PopupWidth(), PopupHeight(),
                         SWP_NOZORDER | SWP_NOACTIVATE);
            PositionMixer(hWnd);
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }

        case WM_APP_FX_UPDATE:
            UpdateFx(hWnd);
            return 0;

        case WM_HOTKEY:
            if (static_cast<int>(wParam) == HOTKEY_OPEN_ID) {
                HandleOpenHotkey(hWnd);
                return 0;
            }
            HandleMuteHotkey(hWnd, static_cast<int>(wParam));
            return 0;

        case WM_DESTROY:
            // Every processed app goes back to where it plays without the mixer.
            KillTimer(hWnd, TIMER_FX);
            StopAllFx(true);
            // The visible leftovers first: the steps below can block (the
            // audio thread is waited for), and if shutdown is cut short the
            // tray icon and the moved notifications must already be back.
            KillTimer(hWnd, TIMER_TRAY_WATCH);
            DisarmTrayWheel();
            ReturnNotifications();
            RemoveTrayIcon(hWnd);
            FinishVolumeEntry(hWnd, false, false);
            CancelNameTooltip(hWnd);
            StopDefaultVolumeNotifications();

            if (g_uiDeviceEnumerator && g_uiEndpointNotification) {
                g_uiDeviceEnumerator->UnregisterEndpointNotificationCallback(
                    g_uiEndpointNotification);
            }
            if (g_uiEndpointNotification) {
                g_uiEndpointNotification->Release();
                g_uiEndpointNotification = nullptr;
            }
            if (g_uiDeviceEnumerator) {
                g_uiDeviceEnumerator->Release();
                g_uiDeviceEnumerator = nullptr;
            }

            if (g_nameTooltip) {
                DestroyWindow(g_nameTooltip);
                g_nameTooltip = nullptr;
            }
            KillTimer(hWnd, TIMER_VOLUME_INDICATOR);
            if (g_volumeIndicator) {
                DestroyWindow(g_volumeIndicator);
                g_volumeIndicator = nullptr;
            }
            g_indicatorBlack.Reset();
            g_indicatorWhite.Reset();
            g_blackFrame.Reset();
            g_whiteFrame.Reset();
            KillTimer(hWnd, TIMER_METERS);
            KillTimer(hWnd, TIMER_REFRESH);
            KillTimer(hWnd, TIMER_CHECK_FOCUS);
            KillTimer(hWnd, TIMER_TRIM);
            g_showRequestPending = false;
            g_openTime = 0;
            g_closeTime = 0;
            g_closeQueued = false;
            KillTimer(hWnd, TIMER_TRAY_WATCH);
            DisarmTrayWheel();
            ReturnNotifications();
            RemoveTrayIcon(hWnd);
            ReleaseAudioData();
            g_activatedSessions.clear();
            g_liveSessions.clear();
            g_temporarilyHiddenSources.clear();
            AcquireSRWLockExclusive(&g_defaultVolumeLock);
            g_appliedDefaultVolumes.clear();
            ReleaseSRWLockExclusive(&g_defaultVolumeLock);
            g_liveDefaultSessions.clear();
            g_rowVisuals.clear();
            ClearIconCache();
            if (g_masterIcon) {
                DestroyIcon(g_masterIcon);
                g_masterIcon = nullptr;
            }
            DeleteFonts();
            if (g_gdiplusToken) {
                Gdiplus::GdiplusShutdown(g_gdiplusToken);
                g_gdiplusToken = 0;
            }
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hWnd, message, wParam, lParam);
}

constexpr PCWSTR WINDOW_CLASS_NAME = L"HeadroomMixer";

DWORD WINAPI ThreadProc(LPVOID) {
    Wh_Log(L"Mixer: UI thread started, PID=%lu", GetCurrentProcessId());
    if (!SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        Wh_Log(L"Mixer: per-monitor DPI setup failed, error %lu", GetLastError());
    }
    HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Wh_Log(L"Mixer: COM initialization result=0x%08lX",
           static_cast<unsigned long>(comResult));
    if (FAILED(comResult)) {
        SetEvent(g_windowReadyEvent);
        return 1;
    }

    g_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    // No CS_DROPSHADOW: that shadow stays behind when the flyout slides open,
    // and Windows 11 gives a rounded window a shadow of its own.
    windowClass.style = CS_DBLCLKS;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = WINDOW_CLASS_NAME;

    if (!RegisterClassExW(&windowClass)) {
        Wh_Log(L"RegisterClassExW failed, error %u", GetLastError());
        SetEvent(g_windowReadyEvent);
        if (g_trayIcon) {
            DestroyIcon(g_trayIcon);
            g_trayIcon = nullptr;
        }
        if (SUCCEEDED(comResult)) {
            CoUninitialize();
        }
        return 1;
    }

    WNDCLASSEXW entryClass{};
    entryClass.cbSize = sizeof(entryClass);
    entryClass.lpfnWndProc = VolumeEntryProc;
    entryClass.hInstance = windowClass.hInstance;
    entryClass.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    entryClass.lpszClassName = VOLUME_ENTRY_CLASS;
    if (!RegisterClassExW(&entryClass)) {
        UnregisterClassW(WINDOW_CLASS_NAME, windowClass.hInstance);
        SetEvent(g_windowReadyEvent);
        CoUninitialize();
        return 1;
    }

    WNDCLASSEXW tooltipClass{};
    tooltipClass.cbSize = sizeof(tooltipClass);
    tooltipClass.lpfnWndProc = NameTooltipProc;
    tooltipClass.hInstance = windowClass.hInstance;
    tooltipClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    tooltipClass.lpszClassName = NAME_TOOLTIP_CLASS;
    if (!RegisterClassExW(&tooltipClass)) {
        UnregisterClassW(VOLUME_ENTRY_CLASS, windowClass.hInstance);
        UnregisterClassW(WINDOW_CLASS_NAME, windowClass.hInstance);
        SetEvent(g_windowReadyEvent);
        CoUninitialize();
        return 1;
    }

    // Without its class, the volume indicator simply never shows.
    WNDCLASSEXW indicatorClass{};
    indicatorClass.cbSize = sizeof(indicatorClass);
    indicatorClass.lpfnWndProc = VolumeIndicatorProc;
    indicatorClass.hInstance = windowClass.hInstance;
    indicatorClass.lpszClassName = VOLUME_INDICATOR_CLASS;
    if (!RegisterClassExW(&indicatorClass)) {
        Wh_Log(L"Mixer: volume indicator unavailable, error %lu", GetLastError());
    }

    // Without its class, the output picker opens no list.
    WNDCLASSEXW outputListClass{};
    outputListClass.cbSize = sizeof(outputListClass);
    outputListClass.lpfnWndProc = OutputListProc;
    outputListClass.hInstance = windowClass.hInstance;
    outputListClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    outputListClass.lpszClassName = OUTPUT_LIST_CLASS;
    if (!RegisterClassExW(&outputListClass)) {
        Wh_Log(L"Mixer: output list unavailable, error %lu", GetLastError());
    }

    HWND hWnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST, WINDOW_CLASS_NAME, L"Volume mixer",
        WS_POPUP, 0, 0, 420, 400, nullptr, nullptr, windowClass.hInstance,
        nullptr);
    if (!hWnd) {
        Wh_Log(L"CreateWindowExW failed, error %u", GetLastError());
        UnregisterClassW(OUTPUT_LIST_CLASS, windowClass.hInstance);
        UnregisterClassW(VOLUME_INDICATOR_CLASS, windowClass.hInstance);
        UnregisterClassW(NAME_TOOLTIP_CLASS, windowClass.hInstance);
        UnregisterClassW(VOLUME_ENTRY_CLASS, windowClass.hInstance);
        UnregisterClassW(WINDOW_CLASS_NAME, windowClass.hInstance);
        SetEvent(g_windowReadyEvent);
        if (g_trayIcon) {
            DestroyIcon(g_trayIcon);
            g_trayIcon = nullptr;
        }
        if (SUCCEEDED(comResult)) {
            CoUninitialize();
        }
        return 1;
    }

    g_hWnd.store(hWnd);
    RegisterMuteHotkeys(hWnd);
    RegisterOpenHotkey(hWnd);
    UpdateFx(hWnd);
    SetTimer(hWnd, TIMER_FX, 1500, nullptr);
    Wh_Log(L"Mixer: window created, HWND=%p", static_cast<void*>(hWnd));
    AddTrayIcon(hWnd);
    StartTrayHookThread();
    SetTimer(hWnd, TIMER_TRAY_WATCH, 500, nullptr);
    SetEvent(g_windowReadyEvent);
    ApplyDeviceStartVolumes();

    MSG message;
    BOOL result;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) != 0) {
        if (result == -1) {
            Wh_Log(L"Mixer: GetMessage failed, error %lu", GetLastError());
            break;
        }
#ifdef HEADROOM_STANDALONE
        if (AppIsDialogMessage(message)) continue;
#endif
        if (g_hotkeyEntry && message.hwnd == g_volumeEdit &&
            (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN ||
             message.message == WM_KEYUP || message.message == WM_SYSKEYUP ||
             message.message == WM_CHAR || message.message == WM_SYSCHAR)) {
            HandleHotkeyEntryKey(hWnd, message);
            continue;
        }
        if (message.hwnd == g_volumeEdit && message.message == WM_KEYDOWN) {
            if (message.wParam == VK_RETURN || message.wParam == VK_ESCAPE) {
                FinishVolumeEntry(hWnd, message.wParam == VK_RETURN, true);
                continue;
            }
            if (message.wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                SendMessageW(g_volumeEdit, EM_SETSEL, 0, -1);
                continue;
            }
        }
        TranslateMessage(&message);
        try {
            DispatchMessageW(&message);
        } catch (...) {
            // An uncaught exception would end the whole mixer process.
            Wh_Log(L"Mixer: exception in a window procedure");
        }
    }

    if (IsWindow(hWnd)) DestroyWindow(hWnd);
    g_hWnd.store(nullptr);
    StopTrayHookThread();
    g_apps.reset();
    UnregisterClassW(OUTPUT_LIST_CLASS, windowClass.hInstance);
    UnregisterClassW(VOLUME_INDICATOR_CLASS, windowClass.hInstance);
    UnregisterClassW(NAME_TOOLTIP_CLASS, windowClass.hInstance);
    UnregisterClassW(VOLUME_ENTRY_CLASS, windowClass.hInstance);
    UnregisterClassW(WINDOW_CLASS_NAME, windowClass.hInstance);
    if (g_trayIcon) {
        DestroyIcon(g_trayIcon);
        g_trayIcon = nullptr;
    }

    if (SUCCEEDED(comResult)) {
        CoUninitialize();
    }
    return 0;
}

}  // namespace

BOOL WhTool_ModInit() {
    Wh_Log(L"Mixer: initializing tool");
    LoadSettings();

    g_windowReadyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_windowReadyEvent) {
        Wh_Log(L"Mixer: CreateEvent failed, error %lu", GetLastError());
        return FALSE;
    }

    g_thread = CreateThread(nullptr, 0, ThreadProc, nullptr, 0, nullptr);
    if (!g_thread) {
        Wh_Log(L"Mixer: CreateThread failed, error %lu", GetLastError());
        CloseHandle(g_windowReadyEvent);
        g_windowReadyEvent = nullptr;
        return FALSE;
    }
    return TRUE;
}

void WhTool_ModSettingsChanged() {
    if (g_windowReadyEvent) {
        WaitForSingleObject(g_windowReadyEvent, 5000);
    }
    if (HWND hWnd = g_hWnd.load()) {
        PostMessageW(hWnd, WM_APP_RELOAD_SETTINGS, 0, 0);
    }
}

void WhTool_ModUninit() {
    if (g_windowReadyEvent) {
        WaitForSingleObject(g_windowReadyEvent, 5000);
    }
    if (HWND hWnd = g_hWnd.load()) {
        PostMessageW(hWnd, WM_CLOSE, 0, 0);
    }
    if (g_thread) {
        DWORD result = WaitForSingleObject(g_thread, 5000);
        if (result != WAIT_OBJECT_0) {
            // The caller immediately exits this dedicated tool process. Keep
            // handles alive until then: the UI thread might still use them.
            Wh_Log(L"Mixer: UI shutdown did not complete; exiting the dedicated tool process");
            return;
        }
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    if (g_windowReadyEvent) {
        CloseHandle(g_windowReadyEvent);
        g_windowReadyEvent = nullptr;
    }
}

#ifndef HEADROOM_STANDALONE
// Windhawk tool-mod startup. The standalone app has its own entry point.
bool g_isToolModProcessLauncher;
HANDLE g_toolModProcessMutex;

void WINAPI EntryPoint_Hook() {
    Wh_Log(L">");
    ExitThread(0);
}

BOOL Wh_ModInit() {
    DWORD sessionId;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &sessionId) &&
        sessionId == 0) {
        return FALSE;
    }

    bool isExcluded = false;
    bool isToolModProcess = false;
    bool isCurrentToolModProcess = false;
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLine(), &argc);
    if (!argv) {
        Wh_Log(L"CommandLineToArgvW failed");
        return FALSE;
    }

    for (int i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"-service") == 0 ||
            wcscmp(argv[i], L"-service-start") == 0 ||
            wcscmp(argv[i], L"-service-stop") == 0) {
            isExcluded = true;
            break;
        }
    }

    for (int i = 1; i < argc - 1; i++) {
        if (wcscmp(argv[i], L"-tool-mod") == 0) {
            isToolModProcess = true;
            if (wcscmp(argv[i + 1], WH_MOD_ID) == 0) {
                isCurrentToolModProcess = true;
            }
            break;
        }
    }

    LocalFree(argv);

    if (isExcluded) {
        return FALSE;
    }

    if (isCurrentToolModProcess) {
        g_toolModProcessMutex =
            CreateMutex(nullptr, TRUE, L"windhawk-tool-mod_" WH_MOD_ID);
        if (!g_toolModProcessMutex) {
            Wh_Log(L"CreateMutex failed");
            ExitProcess(1);
        }

        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            Wh_Log(L"Tool mod already running (%s)", WH_MOD_ID);
            ExitProcess(1);
        }

        if (!WhTool_ModInit()) {
            ExitProcess(1);
        }

        IMAGE_DOS_HEADER* dosHeader =
            (IMAGE_DOS_HEADER*)GetModuleHandle(nullptr);
        IMAGE_NT_HEADERS* ntHeaders =
            (IMAGE_NT_HEADERS*)((BYTE*)dosHeader + dosHeader->e_lfanew);

        DWORD entryPointRVA = ntHeaders->OptionalHeader.AddressOfEntryPoint;
        void* entryPoint = (BYTE*)dosHeader + entryPointRVA;

        Wh_SetFunctionHook(entryPoint, (void*)EntryPoint_Hook, nullptr);
        return TRUE;
    }

    if (isToolModProcess) {
        return FALSE;
    }

    g_isToolModProcessLauncher = true;
    return TRUE;
}

void Wh_ModAfterInit() {
    if (!g_isToolModProcessLauncher) {
        return;
    }

    WCHAR currentProcessPath[MAX_PATH];
    switch (GetModuleFileName(nullptr, currentProcessPath,
                              ARRAYSIZE(currentProcessPath))) {
        case 0:
        case ARRAYSIZE(currentProcessPath):
            Wh_Log(L"GetModuleFileName failed");
            return;
    }

    WCHAR
    commandLine[MAX_PATH + 2 +
                (sizeof(L" -tool-mod \"" WH_MOD_ID "\"") / sizeof(WCHAR)) - 1];
    swprintf_s(commandLine, L"\"%s\" -tool-mod \"%s\"", currentProcessPath,
               WH_MOD_ID);

    HMODULE kernelModule = GetModuleHandle(L"kernelbase.dll");
    if (!kernelModule) {
        kernelModule = GetModuleHandle(L"kernel32.dll");
        if (!kernelModule) {
            Wh_Log(L"No kernelbase.dll/kernel32.dll");
            return;
        }
    }

    using CreateProcessInternalW_t = BOOL(WINAPI*)(
        HANDLE hUserToken, LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
        LPSECURITY_ATTRIBUTES lpProcessAttributes,
        LPSECURITY_ATTRIBUTES lpThreadAttributes, WINBOOL bInheritHandles,
        DWORD dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
        LPSTARTUPINFOW lpStartupInfo,
        LPPROCESS_INFORMATION lpProcessInformation,
        PHANDLE hRestrictedUserToken);
    CreateProcessInternalW_t pCreateProcessInternalW =
        (CreateProcessInternalW_t)GetProcAddress(kernelModule,
                                                 "CreateProcessInternalW");
    STARTUPINFO si{
        .cb = sizeof(STARTUPINFO),
        .dwFlags = STARTF_FORCEOFFFEEDBACK,
    };
    PROCESS_INFORMATION pi;
    // The undocumented export is tried first, and the documented
    // CreateProcessW is the fallback if it's gone or fails, so a Windows
    // update can't stop the mixer from starting.
    bool started = pCreateProcessInternalW &&
                   pCreateProcessInternalW(nullptr, currentProcessPath, commandLine,
                                           nullptr, nullptr, FALSE, NORMAL_PRIORITY_CLASS,
                                           nullptr, nullptr, &si, &pi, nullptr);
    if (!started) {
        Wh_Log(L"CreateProcessInternalW %s; falling back to CreateProcessW",
               pCreateProcessInternalW ? L"failed" : L"not found");
        started = CreateProcessW(currentProcessPath, commandLine, nullptr, nullptr, FALSE,
                                 NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &pi);
    }
    if (!started) {
        Wh_Log(L"CreateProcess failed");
        return;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

void Wh_ModSettingsChanged() {
    if (g_isToolModProcessLauncher) {
        return;
    }

    WhTool_ModSettingsChanged();
}

void Wh_ModUninit() {
    if (g_isToolModProcessLauncher) {
        return;
    }

    WhTool_ModUninit();
    ExitProcess(0);
}

#endif  // !HEADROOM_STANDALONE
