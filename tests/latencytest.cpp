// Measures the delay the mixer's audio engine adds, and how small its output
// queue can be before it underruns.
//
//   latencytest.exe                  main: runs every scenario, prints a table
//   latencytest.exe --sender MODE S  child: plays a very quiet tone with a
//                                    small click every 400 ms for S seconds.
//                                    MODE "direct" plays on the default output,
//                                    "cable" on the VB-CABLE output.
//
// Scenarios: the click straight to the output (baseline), and through the
// engine with different output queues. Each click is timed from the moment it
// is handed to Windows until it shows up in a loopback recording of the
// output. Clicks are about -26 dB and the tone about -40 dB.
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

struct Params { DWORD type, pid, mode; };

class Handler final : public IActivateAudioInterfaceCompletionHandler {
public:
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** o) override {
        static const IID agile = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};
        if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(IActivateAudioInterfaceCompletionHandler)) ||
            IsEqualIID(riid, agile)) { *o = this; AddRef(); return S_OK; }
        *o = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation*) override { SetEvent(done); return S_OK; }
};

static IAudioClient* ActivateLoopback(DWORD pid) {
    using Fn = HRESULT(WINAPI*)(LPCWSTR, REFIID, PROPVARIANT*, IActivateAudioInterfaceCompletionHandler*, IActivateAudioInterfaceAsyncOperation**);
    HMODULE m = LoadLibraryW(L"mmdevapi.dll");
    auto fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(m, "ActivateAudioInterfaceAsync")));
    if (!fn) return nullptr;
    Params params{1, pid, 0};
    PROPVARIANT v{};
    v.vt = VT_BLOB; v.blob.cbSize = sizeof(params); v.blob.pBlobData = reinterpret_cast<BYTE*>(&params);
    static Handler handler;
    ResetEvent(handler.done);
    IActivateAudioInterfaceAsyncOperation* op = nullptr;
    IAudioClient* client = nullptr;
    if (SUCCEEDED(fn(L"VAD\\Process_Loopback", __uuidof(IAudioClient), &v, &handler, &op)) &&
        WaitForSingleObject(handler.done, 5000) == WAIT_OBJECT_0) {
        HRESULT r = E_FAIL; IUnknown* u = nullptr;
        if (SUCCEEDED(op->GetActivateResult(&r, &u)) && SUCCEEDED(r) && u)
            u->QueryInterface(__uuidof(IAudioClient), reinterpret_cast<void**>(&client));
        if (u) u->Release();
    }
    return client;
}

static IMMDeviceEnumerator* Enumerator() {
    static IMMDeviceEnumerator* e = [] {
        IMMDeviceEnumerator* p = nullptr;
        CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&p));
        return p;
    }();
    return e;
}
static IMMDevice* DefaultRender() {
    IMMDevice* d = nullptr;
    Enumerator()->GetDefaultAudioEndpoint(eRender, eMultimedia, &d);
    return d;
}
static IMMDevice* CableRender() {
    IMMDeviceCollection* c = nullptr;
    Enumerator()->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &c);
    UINT n = 0; c->GetCount(&n);
    IMMDevice* found = nullptr;
    for (UINT i = 0; i < n && !found; ++i) {
        IMMDevice* d = nullptr; c->Item(i, &d);
        IPropertyStore* store = nullptr; d->OpenPropertyStore(STGM_READ, &store);
        PROPVARIANT name; PropVariantInit(&name);
        if (store && SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR) {
            std::wstring s = name.pwszVal;
            if (s.find(L"CABLE In") == 0 || s.find(L"VB-Audio Virtual Cable") != std::wstring::npos) {
                if (s.find(L"16ch") == std::wstring::npos) { found = d; d = nullptr; }
            }
        }
        PropVariantClear(&name);
        if (store) store->Release();
        if (d) d->Release();
    }
    c->Release();
    return found;
}

static double Now100ns() {
    LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) * 1e7 / static_cast<double>(f.QuadPart);
}

// ---- the child: tone plus clicks ------------------------------------------------------------
static int Sender(const wchar_t* mode, int seconds) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDevice* d = wcscmp(mode, L"cable") == 0 ? CableRender() : DefaultRender();
    if (!d) return 2;
    IAudioClient* c = nullptr; IAudioRenderClient* r = nullptr;
    d->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&c));
    WAVEFORMATEX* mix = nullptr; c->GetMixFormat(&mix);
    c->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 1000000, 0, mix, nullptr);
    UINT32 frames = 0; c->GetBufferSize(&frames);
    c->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&r));
    c->Start();
    FILE* log = nullptr;
    wchar_t path[MAX_PATH]; swprintf(path, MAX_PATH, L"emit_%ls.txt", mode);
    log = _wfopen(path, L"w");
    double rate = mix->nSamplesPerSec;
    double phase = 0;
    long long frame = 0;
    long long nextClick = static_cast<long long>(rate * 1.0);
    ULONGLONG end = GetTickCount64() + seconds * 1000ull;
    while (GetTickCount64() < end) {
        UINT32 pad = 0; c->GetCurrentPadding(&pad);
        UINT32 n = std::min<UINT32>(frames - pad, static_cast<UINT32>(rate * 0.004));  // small chunks
        if (n) {
            BYTE* data = nullptr; r->GetBuffer(n, &data);
            float* f = reinterpret_cast<float*>(data);
            double now = Now100ns();
            for (UINT32 i = 0; i < n; ++i, ++frame) {
                float s = 0.01f * static_cast<float>(std::sin(phase));
                phase += 2 * 3.14159265358979 * 330.0 / rate;
                if (frame >= nextClick && frame < nextClick + 6) {
                    s = 0.05f;
                    if (frame == nextClick && log) {
                        // When this frame will play: after what is queued already.
                        fprintf(log, "%.0f\n", now + (static_cast<double>(pad) + i) / rate * 1e7);
                        fflush(log);
                    }
                    if (frame == nextClick + 5) nextClick += static_cast<long long>(rate * 0.4);
                }
                for (int ch = 0; ch < mix->nChannels; ++ch) f[i * mix->nChannels + ch] = s;
            }
            r->ReleaseBuffer(n, 0);
        }
        Sleep(1);
    }
    c->Stop();
    if (log) fclose(log);
    return 0;
}

// ---- the main program: recording of the output, and the engine ------------------------------
struct Arrival { double time; };
static std::mutex g_lock;
static std::vector<double> g_arrivals;
static std::atomic<bool> g_quit{false};

static DWORD WINAPI OutputRecorder(void*) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    IMMDevice* d = DefaultRender();
    IAudioClient* c = nullptr; IAudioCaptureClient* cc = nullptr;
    d->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&c));
    WAVEFORMATEX* mix = nullptr; c->GetMixFormat(&mix);
    c->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 1000000, 0, mix, nullptr);
    c->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&cc));
    c->Start();
    double rate = mix->nSamplesPerSec;
    double lastClick = -1e18;
    while (!g_quit) {
        UINT32 packet = 0; cc->GetNextPacketSize(&packet);
        while (packet) {
            BYTE* data = nullptr; UINT32 n = 0; DWORD flags = 0; UINT64 dev = 0, qpc = 0;
            if (FAILED(cc->GetBuffer(&data, &n, &flags, &dev, &qpc))) break;
            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                float* f = reinterpret_cast<float*>(data);
                for (UINT32 i = 0; i < n; ++i) {
                    if (std::fabs(f[i * mix->nChannels]) > 0.015f) {
                        double t = static_cast<double>(qpc) + i / rate * 1e7;
                        if (t - lastClick > 2e6) {  // one click per 200 ms at least
                            std::lock_guard<std::mutex> lock(g_lock);
                            g_arrivals.push_back(t);
                        }
                        lastClick = t;
                    }
                }
            }
            cc->ReleaseBuffer(n);
            cc->GetNextPacketSize(&packet);
        }
        Sleep(1);
    }
    return 0;
}

struct Engine {
    DWORD pid = 0;
    int queueMs = 30;
    std::atomic<bool> stop{false};
    std::atomic<int> underruns{0};
    HANDLE thread = nullptr;
};

static DWORD WINAPI EngineThread(void* p) {
    Engine* e = static_cast<Engine*>(p);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    HANDLE captured = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    IAudioClient* cap = ActivateLoopback(e->pid);
    WAVEFORMATEX fmt{};
    fmt.wFormatTag = WAVE_FORMAT_IEEE_FLOAT; fmt.nChannels = 2; fmt.nSamplesPerSec = 48000; fmt.wBitsPerSample = 32;
    fmt.nBlockAlign = 8; fmt.nAvgBytesPerSec = 48000 * 8;
    IAudioCaptureClient* cc = nullptr;
    if (!cap || FAILED(cap->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                       AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                                       200000, 0, &fmt, nullptr)) ||
        FAILED(cap->SetEventHandle(captured)) || FAILED(cap->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&cc)))) {
        printf("engine: capture failed\n");
        return 1;
    }
    IMMDevice* d = DefaultRender();
    IAudioClient* rc = nullptr; IAudioRenderClient* rr = nullptr;
    d->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&rc));
    rc->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY, 500000, 0, &fmt, nullptr);
    UINT32 frames = 0; rc->GetBufferSize(&frames);
    rc->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&rr));
    cap->Start(); rc->Start();
    std::vector<float> fifo;
    ULONGLONG warm = GetTickCount64() + 1500;
    while (!e->stop) {
        WaitForSingleObject(captured, 50);
        UINT32 packet = 0;
        while (SUCCEEDED(cc->GetNextPacketSize(&packet)) && packet) {
            BYTE* data = nullptr; UINT32 n = 0; DWORD flags = 0;
            if (FAILED(cc->GetBuffer(&data, &n, &flags, nullptr, nullptr))) break;
            size_t start = fifo.size(); fifo.resize(start + n * 2ull);
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) std::fill(fifo.begin() + start, fifo.end(), 0.0f);
            else memcpy(fifo.data() + start, data, n * 2ull * sizeof(float));
            cc->ReleaseBuffer(n);
        }
        UINT32 padding = 0;
        if (SUCCEEDED(rc->GetCurrentPadding(&padding))) {
            if (padding == 0 && !fifo.empty() == false && GetTickCount64() > warm) e->underruns++;
            UINT32 wanted = 48000 * e->queueMs / 1000;
            UINT32 room = padding < wanted ? wanted - padding : 0;
            room = std::min(room, frames - std::min(frames, padding));
            UINT32 n = std::min<UINT32>(room, static_cast<UINT32>(fifo.size() / 2));
            BYTE* t = nullptr;
            if (n && SUCCEEDED(rr->GetBuffer(n, &t))) {
                memcpy(t, fifo.data(), n * 2ull * sizeof(float));
                rr->ReleaseBuffer(n, 0);
                fifo.erase(fifo.begin(), fifo.begin() + n * 2ull);
            }
        }
        if (fifo.size() > 9600) fifo.erase(fifo.begin(), fifo.end() - 4800);
    }
    rc->Stop(); cap->Stop();
    return 0;
}

static std::vector<double> ReadEmit(const wchar_t* mode) {
    std::vector<double> v;
    wchar_t path[MAX_PATH]; swprintf(path, MAX_PATH, L"emit_%ls.txt", mode);
    FILE* f = _wfopen(path, L"r");
    double x;
    while (f && fscanf(f, "%lf", &x) == 1) v.push_back(x);
    if (f) fclose(f);
    return v;
}

static void Scenario(const wchar_t* mode, bool engine, int queueMs, const char* name) {
    wchar_t self[MAX_PATH]; GetModuleFileNameW(nullptr, self, MAX_PATH);
    wchar_t cmd[MAX_PATH + 64]; swprintf(cmd, MAX_PATH + 64, L"\"%ls\" --sender %ls 9", self, mode);
    STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
    { std::lock_guard<std::mutex> l(g_lock); g_arrivals.clear(); }
    CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
    Engine e; e.pid = pi.dwProcessId; e.queueMs = queueMs;
    if (engine) {
        Sleep(1200);
        e.thread = CreateThread(nullptr, 0, EngineThread, &e, 0, nullptr);
    }
    WaitForSingleObject(pi.hProcess, 20000);
    Sleep(400);
    e.stop = true;
    if (e.thread) WaitForSingleObject(e.thread, 3000);
    std::vector<double> emit = ReadEmit(mode), arr;
    { std::lock_guard<std::mutex> l(g_lock); arr = g_arrivals; }
    // Pair each emitted click with the next arrival after it (minus a margin).
    std::vector<double> delays;
    for (double t : emit) {
        for (double a : arr) {
            if (a >= t - 2e5) { delays.push_back((a - t) / 1e4); break; }  // ms
        }
    }
    if (engine && !delays.empty()) {
        // The engine starts after the first clicks; keep the settled ones.
        if (delays.size() > 4) delays.erase(delays.begin(), delays.begin() + 3);
    }
    std::sort(delays.begin(), delays.end());
    if (delays.empty()) { printf("%-34s no clicks recorded\n", name); return; }
    printf("%-34s clicks %2zu   delay median %6.1f ms   min %6.1f   max %6.1f   underruns %d\n", name,
           delays.size(), delays[delays.size() / 2], delays.front(), delays.back(), e.underruns.load());
}

int wmain(int argc, wchar_t** argv) {
    if (argc >= 4 && wcscmp(argv[1], L"--sender") == 0) return Sender(argv[2], _wtoi(argv[3]));
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDevice* cable = CableRender();
    if (!cable) { printf("VB-CABLE output not found\n"); return 1; }
    cable->Release();
    HANDLE rec = CreateThread(nullptr, 0, OutputRecorder, nullptr, 0, nullptr);
    Sleep(500);
    Scenario(L"direct", false, 0, "direct to the output (baseline)");
    Scenario(L"cable", true, 30, "engine, 30 ms queue (as now)");
    Scenario(L"cable", true, 20, "engine, 20 ms queue");
    Scenario(L"cable", true, 15, "engine, 15 ms queue");
    Scenario(L"cable", true, 10, "engine, 10 ms queue");
    Scenario(L"cable", true, 6, "engine, 6 ms queue");
    g_quit = true; WaitForSingleObject(rec, 2000);
    return 0;
}
