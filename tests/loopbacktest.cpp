// Experiment: does application loopback capture an app's audio BEFORE the
// session volume and mute are applied?  If yes, the mixer does not need a
// virtual cable: it can mute the app's session, capture it, process it and play
// it on the real output.
//
// The program plays a quiet 440 Hz tone itself (about 1.5 s per phase), mutes
// or lowers its own session in turn and captures itself through process
// loopback.  It prints the peak it captured in each phase.
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <audiopolicy.h>
#include <cmath>
#include <cstdio>
#include <vector>
#include <atomic>

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

static std::atomic<bool> g_stop{false};
static ISimpleAudioVolume* g_volume = nullptr;
static HANDLE g_ready = nullptr;

static DWORD WINAPI RenderThread(void*) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* e = nullptr; IMMDevice* d = nullptr; IAudioClient* c = nullptr; IAudioRenderClient* r = nullptr;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&e));
    e->GetDefaultAudioEndpoint(eRender, eMultimedia, &d);
    d->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&c));
    WAVEFORMATEX* mix = nullptr; c->GetMixFormat(&mix);
    c->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 1000000, 0, mix, nullptr);
    UINT32 frames = 0; c->GetBufferSize(&frames);
    c->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&r));
    c->GetService(__uuidof(ISimpleAudioVolume), reinterpret_cast<void**>(&g_volume));
    c->Start();
    SetEvent(g_ready);
    double phase = 0;
    while (!g_stop) {
        UINT32 pad = 0; c->GetCurrentPadding(&pad);
        UINT32 n = frames - pad;
        if (n) {
            BYTE* data = nullptr; r->GetBuffer(n, &data);
            float* f = reinterpret_cast<float*>(data);   // the shared-mode mix format is float
            for (UINT32 i = 0; i < n; ++i) {
                float s = 0.05f * static_cast<float>(std::sin(phase));
                phase += 2 * 3.14159265358979 * 440.0 / mix->nSamplesPerSec;
                for (int ch = 0; ch < mix->nChannels; ++ch) f[i * mix->nChannels + ch] = s;
            }
            r->ReleaseBuffer(n, 0);
        }
        Sleep(5);
    }
    c->Stop();
    return 0;
}

static float Capture(IAudioClient* cap, IAudioCaptureClient* cc, int ms) {
    float peak = 0;
    ULONGLONG end = GetTickCount64() + ms;
    while (GetTickCount64() < end) {
        UINT32 packet = 0;
        cc->GetNextPacketSize(&packet);
        while (packet) {
            BYTE* data = nullptr; UINT32 n = 0; DWORD flags = 0;
            if (FAILED(cc->GetBuffer(&data, &n, &flags, nullptr, nullptr))) break;
            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                float* f = reinterpret_cast<float*>(data);
                for (UINT32 i = 0; i < n * 2; ++i) peak = std::max(peak, std::fabs(f[i]));
            }
            cc->ReleaseBuffer(n);
            cc->GetNextPacketSize(&packet);
        }
        Sleep(10);
    }
    (void)cap;
    return peak;
}

int main() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    g_ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE t = CreateThread(nullptr, 0, RenderThread, nullptr, 0, nullptr);
    WaitForSingleObject(g_ready, 5000);
    if (!g_volume) { printf("no session volume\n"); return 1; }

    IAudioClient* cap = ActivateLoopback(GetCurrentProcessId());
    if (!cap) { printf("process loopback not available\n"); return 1; }
    WAVEFORMATEX f{};
    f.wFormatTag = WAVE_FORMAT_IEEE_FLOAT; f.nChannels = 2; f.nSamplesPerSec = 48000; f.wBitsPerSample = 32;
    f.nBlockAlign = 8; f.nAvgBytesPerSec = 48000 * 8;
    HRESULT hr = cap->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                                 200000, 0, &f, nullptr);
    IAudioCaptureClient* cc = nullptr;
    if (FAILED(hr) || FAILED(cap->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&cc)))) {
        printf("capture init failed 0x%08lX\n", static_cast<unsigned long>(hr)); return 1;
    }
    cap->Start();
    Sleep(500);
    g_volume->SetMasterVolume(1.0f, nullptr); g_volume->SetMute(FALSE, nullptr);
    Sleep(300); Capture(cap, cc, 200);
    float a = Capture(cap, cc, 1200);
    g_volume->SetMasterVolume(0.0f, nullptr);
    Sleep(300); Capture(cap, cc, 200);
    float b = Capture(cap, cc, 1200);
    g_volume->SetMasterVolume(1.0f, nullptr); g_volume->SetMute(TRUE, nullptr);
    Sleep(300); Capture(cap, cc, 200);
    float c = Capture(cap, cc, 1200);
    g_volume->SetMute(FALSE, nullptr); g_volume->SetMasterVolume(1.0f, nullptr);
    g_stop = true; WaitForSingleObject(t, 2000);
    printf("tone amplitude played: 0.050\n");
    printf("A  volume 100%%, not muted : captured peak %.4f\n", a);
    printf("B  session volume 0%%      : captured peak %.4f\n", b);
    printf("C  session muted          : captured peak %.4f\n", c);
    printf("%s\n", (b > 0.02f && c > 0.02f) ? "RESULT: captured BEFORE volume and mute (no virtual cable needed)"
                                              : "RESULT: captured AFTER volume/mute (a silent sink is needed)");
    return 0;
}
