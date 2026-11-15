// End-to-end test of the mixer's surround channels.
//
//   surroundtest.exe --sender S     plays three tones on S seconds as a 7.1 app would:
//                                   311 Hz on front left, 617 Hz on center, 929 Hz on side left
//   surroundtest.exe                starts a copy of itself as "chsender.exe" (so the mixer can have its own
//                                   settings for it), records the default output for 20 seconds and prints
//                                   how loud each tone is on the left and on the right, every 2 seconds
//
// Set the mixer's channels for chsender.exe first (center off, for example) and watch the 617 Hz tone vanish
// once the mixer has taken the app over.
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <ksmedia.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static const GUID kFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

static int Sender(int seconds) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* e = nullptr;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&e));
    IMMDevice* d = nullptr;
    e->GetDefaultAudioEndpoint(eRender, eMultimedia, &d);
    IAudioClient* c = nullptr; IAudioRenderClient* r = nullptr;
    d->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&c));
    WAVEFORMATEXTENSIBLE f{};
    f.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE; f.Format.nChannels = 8; f.Format.nSamplesPerSec = 48000;
    f.Format.wBitsPerSample = 32; f.Format.nBlockAlign = 32; f.Format.nAvgBytesPerSec = 48000 * 32; f.Format.cbSize = 22;
    f.Samples.wValidBitsPerSample = 32; f.dwChannelMask = KSAUDIO_SPEAKER_7POINT1_SURROUND; f.SubFormat = kFloat;
    HRESULT hr = c->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                               1000000, 0, reinterpret_cast<WAVEFORMATEX*>(&f), nullptr);
    if (FAILED(hr)) { printf("sender: 0x%08lX\n", static_cast<unsigned long>(hr)); return 1; }
    UINT32 frames = 0; c->GetBufferSize(&frames);
    c->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&r));
    c->Start();
    double p[3] = {0, 0, 0};
    const double freq[3] = {311.0, 617.0, 929.0};
    const int channel[3] = {0, 2, 6};  // front left, center, side left
    ULONGLONG end = GetTickCount64() + seconds * 1000ull;
    while (GetTickCount64() < end) {
        UINT32 pad = 0; c->GetCurrentPadding(&pad);
        UINT32 n = frames - pad;
        if (n) {
            BYTE* data = nullptr; r->GetBuffer(n, &data);
            float* out = reinterpret_cast<float*>(data);
            for (UINT32 i = 0; i < n; ++i) {
                for (int ch = 0; ch < 8; ++ch) out[i * 8 + ch] = 0.0f;
                for (int t = 0; t < 3; ++t) {
                    out[i * 8 + channel[t]] = 0.05f * static_cast<float>(std::sin(p[t]));
                    p[t] += 2 * 3.14159265358979 * freq[t] / 48000.0;
                }
            }
            r->ReleaseBuffer(n, 0);
        }
        Sleep(4);
    }
    c->Stop();
    return 0;
}

static double Goertzel(const std::vector<float>& x, double freq, double rate) {
    double w = 2 * 3.14159265358979 * freq / rate, coeff = 2 * std::cos(w), s1 = 0, s2 = 0;
    for (float v : x) { double s = v + coeff * s1 - s2; s2 = s1; s1 = s; }
    double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    return 2.0 * std::sqrt(std::max(0.0, power)) / x.size();
}

int main(int argc, char** argv) {
    if (argc > 2 && std::string(argv[1]) == "--sender") return Sender(atoi(argv[2]));
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    char self[MAX_PATH]; GetModuleFileNameA(nullptr, self, MAX_PATH);
    std::string copy = std::string(self);
    copy = copy.substr(0, copy.find_last_of("\\/") + 1) + "chsender.exe";
    CopyFileA(self, copy.c_str(), FALSE);
    std::string cmd = "\"" + copy + "\" --sender 24";
    STARTUPINFOA si{sizeof(si)}; PROCESS_INFORMATION pi{};
    CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);

    IMMDeviceEnumerator* e = nullptr;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&e));
    IMMDevice* d = nullptr; e->GetDefaultAudioEndpoint(eRender, eMultimedia, &d);
    IAudioClient* c = nullptr; IAudioCaptureClient* cc = nullptr;
    d->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&c));
    WAVEFORMATEX* mix = nullptr; c->GetMixFormat(&mix);
    c->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 2000000, 0, mix, nullptr);
    c->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&cc));
    c->Start();
    double rate = mix->nSamplesPerSec; int ch = mix->nChannels;
    printf("seconds   311 Hz (FL)  L / R      617 Hz (C)  L / R      929 Hz (SL)  L / R\n");
    std::vector<float> left, right;
    ULONGLONG start = GetTickCount64(), windowStart = start;
    while (GetTickCount64() - start < 24000) {
        UINT32 packet = 0; cc->GetNextPacketSize(&packet);
        while (packet) {
            BYTE* data = nullptr; UINT32 n = 0; DWORD flags = 0;
            if (FAILED(cc->GetBuffer(&data, &n, &flags, nullptr, nullptr))) break;
            float* in = reinterpret_cast<float*>(data);
            for (UINT32 i = 0; i < n; ++i) {
                bool silent = flags & AUDCLNT_BUFFERFLAGS_SILENT;
                left.push_back(silent ? 0.0f : in[i * ch]); right.push_back(silent ? 0.0f : in[i * ch + 1]);
            }
            cc->ReleaseBuffer(n);
            cc->GetNextPacketSize(&packet);
        }
        if (GetTickCount64() - windowStart >= 2000 && left.size() > 48000) {
            printf("%5.0f     %.4f / %.4f      %.4f / %.4f      %.4f / %.4f\n", (GetTickCount64() - start) / 1000.0,
                   Goertzel(left, 311, rate), Goertzel(right, 311, rate), Goertzel(left, 617, rate), Goertzel(right, 617, rate),
                   Goertzel(left, 929, rate), Goertzel(right, 929, rate));
            fflush(stdout);
            left.clear(); right.clear(); windowStart = GetTickCount64();
        }
        Sleep(5);
    }
    WaitForSingleObject(pi.hProcess, 3000);
    return 0;
}
