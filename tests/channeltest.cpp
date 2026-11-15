// Experiment: can the mixer's engine see an app's channels one by one (5.1 / 7.1)?
//
// The program lists the formats of the output devices, then plays a tone on one channel at a time into a
// device (as an app with 7.1 sound would), records itself through application loopback as 8 channels, and
// prints which recorded channel carried each tone. Nothing is played on a real output.
//
//   channeltest.exe              lists the formats
//   channeltest.exe "<name>"     tests the output whose name contains <name>, for example "CABLE In 16ch"
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
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

static std::wstring NameOf(IMMDevice* d) {
    IPropertyStore* store = nullptr; d->OpenPropertyStore(STGM_READ, &store);
    PROPVARIANT n; PropVariantInit(&n);
    std::wstring name;
    if (store && SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &n)) && n.vt == VT_LPWSTR) name = n.pwszVal;
    PropVariantClear(&n);
    if (store) store->Release();
    return name;
}

static WAVEFORMATEXTENSIBLE Format(int channels, DWORD mask, int rate = 48000) {
    WAVEFORMATEXTENSIBLE f{};
    f.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    f.Format.nChannels = static_cast<WORD>(channels);
    f.Format.nSamplesPerSec = rate;
    f.Format.wBitsPerSample = 32;
    f.Format.nBlockAlign = static_cast<WORD>(channels * 4);
    f.Format.nAvgBytesPerSec = rate * f.Format.nBlockAlign;
    f.Format.cbSize = 22;
    f.Samples.wValidBitsPerSample = 32;
    f.dwChannelMask = mask;
    static const GUID kFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
    f.SubFormat = kFloat;
    return f;
}

struct DeviceShareModeOpaque;
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
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR, ERole) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};
static IPolicyConfig* Policy() {
    static const CLSID clsid = {0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};
    static const IID iid = {0xf8679f50, 0x850a, 0x41cf, {0x9c, 0x72, 0x43, 0x0f, 0x29, 0x02, 0x90, 0xc8}};
    IPolicyConfig* p = nullptr;
    CoCreateInstance(clsid, nullptr, CLSCTX_ALL, iid, reinterpret_cast<void**>(&p));
    return p;
}

static std::atomic_bool g_stop{false};
static std::atomic<int> g_activeChannel{-1};

static DWORD WINAPI Sender(void* param) {
    IMMDevice* d = static_cast<IMMDevice*>(param);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IAudioClient* c = nullptr; IAudioRenderClient* r = nullptr;
    d->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&c));
    WAVEFORMATEXTENSIBLE f = Format(8, KSAUDIO_SPEAKER_7POINT1_SURROUND);
    HRESULT hr = c->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                               1000000, 0, reinterpret_cast<WAVEFORMATEX*>(&f), nullptr);
    if (FAILED(hr)) { printf("sender: 8-channel stream refused (0x%08lX)\n", static_cast<unsigned long>(hr)); g_stop = true; return 1; }
    UINT32 frames = 0; c->GetBufferSize(&frames);
    c->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&r));
    c->Start();
    double phase = 0;
    while (!g_stop) {
        UINT32 pad = 0; c->GetCurrentPadding(&pad);
        UINT32 n = frames - pad;
        if (n) {
            BYTE* data = nullptr; r->GetBuffer(n, &data);
            float* out = reinterpret_cast<float*>(data);
            int active = g_activeChannel;
            for (UINT32 i = 0; i < n; ++i) {
                float s = 0.2f * static_cast<float>(std::sin(phase));
                phase += 2 * 3.14159265358979 * 440.0 / 48000.0;
                for (int ch = 0; ch < 8; ++ch) out[i * 8 + ch] = ch == active ? s : 0.0f;
            }
            r->ReleaseBuffer(n, 0);
        }
        Sleep(4);
    }
    c->Stop();
    return 0;
}

int main(int argc, char** argv) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* e = nullptr;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&e));
    IMMDeviceCollection* devices = nullptr;
    e->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices);
    UINT count = 0; devices->GetCount(&count);
    std::string want = argc > 1 ? argv[1] : "";
    IMMDevice* chosen = nullptr;
    printf("Outputs:\n");
    for (UINT i = 0; i < count; ++i) {
        IMMDevice* d = nullptr; devices->Item(i, &d);
        IAudioClient* c = nullptr; d->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&c));
        WAVEFORMATEX* mix = nullptr; c->GetMixFormat(&mix);
        DWORD mask = mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE ? reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix)->dwChannelMask : 0;
        std::wstring name = NameOf(d);
        printf("  %-52ls %2d ch  %6lu Hz  mask 0x%lX\n", name.c_str(), mix->nChannels, mix->nSamplesPerSec, mask);
        if (!want.empty() && !chosen) {
            std::wstring w(want.begin(), want.end());
            if (name.find(w) != std::wstring::npos) { chosen = d; d = nullptr; }
        }
        if (d) d->Release();
    }
    if (!chosen) return 0;

    printf("\nTest on: %ls\n", NameOf(chosen).c_str());
    std::wstring chosenId;
    { LPWSTR raw = nullptr; if (SUCCEEDED(chosen->GetId(&raw)) && raw) { chosenId = raw; CoTaskMemFree(raw); } }
    IPolicyConfig* policy = nullptr;
    WAVEFORMATEX* original = nullptr;
    bool changed = false;
    if (argc > 2 && std::string(argv[2]) == "--set2") {
        // Back to the usual format of the cable: 2 channels, 24 bit, 48 kHz.
        IPolicyConfig* back = Policy();
        WAVEFORMATEXTENSIBLE f2 = Format(2, 3);
        static const GUID kPcm2 = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
        f2.SubFormat = kPcm2;
        f2.Format.wBitsPerSample = 24;
        f2.Format.nBlockAlign = 6;
        f2.Format.nAvgBytesPerSec = 48000 * 6;
        f2.Samples.wValidBitsPerSample = 24;
        HRESULT rh = back ? back->SetDeviceFormat(chosenId.c_str(), reinterpret_cast<WAVEFORMATEX*>(&f2),
                                                  reinterpret_cast<WAVEFORMATEX*>(&f2)) : E_FAIL;
        printf("device set to 2 channels: 0x%08lX\n", static_cast<unsigned long>(rh));
        return 0;
    }
    if (argc > 2 && std::string(argv[2]) == "--reset") {
        IPolicyConfig* back = Policy();
        HRESULT rh = back ? back->ResetDeviceFormat(chosenId.c_str()) : E_FAIL;
        printf("device format reset to its default: 0x%08lX\n", static_cast<unsigned long>(rh));
        return 0;
    }
    if (argc > 2 && std::string(argv[2]) == "--set8") {
        policy = Policy();
        if (!policy || FAILED(policy->GetDeviceFormat(chosenId.c_str(), FALSE, &original))) {
            printf("cannot read the device format\n");
            return 1;
        }
        WAVEFORMATEXTENSIBLE f8 = Format(8, KSAUDIO_SPEAKER_7POINT1_SURROUND);
        static const GUID kPcm = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
        f8.SubFormat = kPcm;
        f8.Format.wBitsPerSample = 24;
        f8.Format.nBlockAlign = 24;
        f8.Format.nAvgBytesPerSec = 48000 * 24;
        f8.Samples.wValidBitsPerSample = 24;
        HRESULT sh = policy->SetDeviceFormat(chosenId.c_str(), reinterpret_cast<WAVEFORMATEX*>(&f8),
                                             reinterpret_cast<WAVEFORMATEX*>(&f8));
        printf("device set to 8 channels: 0x%08lX\n", static_cast<unsigned long>(sh));
        changed = SUCCEEDED(sh);
        Sleep(1500);
        IMMDevice* again = nullptr;
        e->GetDevice(chosenId.c_str(), &again);
        if (again) {
            chosen->Release();
            chosen = again;
            IAudioClient* c = nullptr;
            chosen->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&c));
            WAVEFORMATEX* mix = nullptr;
            c->GetMixFormat(&mix);
            printf("mix format now: %d ch, %lu Hz\n", mix->nChannels, mix->nSamplesPerSec);
        }
    }
    auto restore = [&]() {
        if (changed && policy && original) {
            HRESULT rh = policy->SetDeviceFormat(chosenId.c_str(), original, original);
            printf("device format restored: 0x%08lX (%d ch)\n", static_cast<unsigned long>(rh), original->nChannels);
        }
    };
    HANDLE sender = CreateThread(nullptr, 0, Sender, chosen, 0, nullptr);
    Sleep(800);
    if (g_stop) {
        restore();
        return 1;
    }
    IAudioClient* cap = ActivateLoopback(GetCurrentProcessId());
    if (!cap) {
        printf("application loopback not available\n");
        g_stop = true;
        restore();
        return 1;
    }
    WAVEFORMATEXTENSIBLE f = Format(8, KSAUDIO_SPEAKER_7POINT1_SURROUND);
    HRESULT hr = cap->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                                 200000, 0, reinterpret_cast<WAVEFORMATEX*>(&f), nullptr);
    IAudioCaptureClient* cc = nullptr;
    if (FAILED(hr) || FAILED(cap->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&cc)))) {
        printf("8-channel capture refused (0x%08lX)\n", static_cast<unsigned long>(hr));
        g_stop = true;
        restore();
        return 1;
    }
    cap->Start();
    static const char* names[8] = {"FL", "FR", "C", "LFE", "BL", "BR", "SL", "SR"};
    printf("\nsent on   -> peak per recorded channel\n");
    for (int sent = 0; sent < 8; ++sent) {
        g_activeChannel = sent;
        Sleep(400);
        float peak[8] = {};
        ULONGLONG end = GetTickCount64() + 700;
        while (GetTickCount64() < end) {
            UINT32 packet = 0; cc->GetNextPacketSize(&packet);
            while (packet) {
                BYTE* data = nullptr; UINT32 n = 0; DWORD flags = 0;
                if (FAILED(cc->GetBuffer(&data, &n, &flags, nullptr, nullptr))) break;
                if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                    float* in = reinterpret_cast<float*>(data);
                    for (UINT32 i = 0; i < n; ++i)
                        for (int ch = 0; ch < 8; ++ch) peak[ch] = std::max(peak[ch], std::fabs(in[i * 8 + ch]));
                }
                cc->ReleaseBuffer(n);
                cc->GetNextPacketSize(&packet);
            }
            Sleep(5);
        }
        printf("  %-4s ->", names[sent]);
        for (int ch = 0; ch < 8; ++ch) printf(" %s %.3f ", names[ch], peak[ch]);
        printf("\n");
    }
    g_stop = true;
    WaitForSingleObject(sender, 2000);
    restore();
    return 0;
}
