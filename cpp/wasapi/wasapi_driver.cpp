#include "wasapi_driver.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <mmreg.h>
#include <comdef.h>
#include <iostream>
#include <vector>
#include <chrono>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "avrt.lib")

WasapiDriver::WasapiDriver()
    : threadHandle(NULL), running(false), sr(48000), ch(2), framesPerBuf(0), callback(NULL), userPtr(NULL)
{
}

WasapiDriver::~WasapiDriver()
{
    stop();
    close();
}

bool WasapiDriver::open(int sampleRate, int channels, int framesPerBuffer)
{
    sr = sampleRate;
    ch = channels;
    framesPerBuf = framesPerBuffer;
    // full WASAPI initialization happens in the render thread for simplicity
    return true;
}

int WasapiDriver::query_mix_sample_rate()
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) return 0;

    int result = 0;
    IMMDeviceEnumerator* enumerator = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (SUCCEEDED(hr)) {
        IMMDevice* device = nullptr;
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        if (SUCCEEDED(hr)) {
            IAudioClient* audioClient = nullptr;
            hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&audioClient);
            if (SUCCEEDED(hr)) {
                WAVEFORMATEX* pwfx = nullptr;
                hr = audioClient->GetMixFormat(&pwfx);
                if (SUCCEEDED(hr) && pwfx) {
                    result = static_cast<int>(pwfx->nSamplesPerSec);
                    CoTaskMemFree(pwfx);
                }
                audioClient->Release();
            }
            device->Release();
        }
        enumerator->Release();
    }

    CoUninitialize();
    return result;
}

void WasapiDriver::close()
{
    // nothing to do in minimal skeleton
}

bool WasapiDriver::start(fp_audio_callback_t cb, void* user)
{
    if (running) return false;
    callback = cb;
    userPtr = user;
    running = true;
    threadHandle = CreateThread(NULL, 0, WasapiDriver::render_thread_proc, this, 0, NULL);
    return threadHandle != NULL;
}

void WasapiDriver::stop()
{
    if (!running) return;
    running = false;
    if (threadHandle) {
        WaitForSingleObject(threadHandle, INFINITE);
        CloseHandle(threadHandle);
        threadHandle = NULL;
    }
}

DWORD WINAPI WasapiDriver::render_thread_proc(LPVOID param)
{
    WasapiDriver* self = reinterpret_cast<WasapiDriver*>(param);
    return self->render_thread();
}

DWORD WasapiDriver::render_thread()
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        std::cerr << "CoInitializeEx failed: " << std::hex << hr << std::endl;
        running = false;
        return 1;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        std::cerr << "CoCreateInstance(MMDeviceEnumerator) failed: " << std::hex << hr << std::endl;
        CoUninitialize();
        running = false;
        return 1;
    }

    IMMDevice* device = nullptr;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr)) {
        std::cerr << "GetDefaultAudioEndpoint failed: " << std::hex << hr << std::endl;
        enumerator->Release();
        CoUninitialize();
        running = false;
        return 1;
    }

    IAudioClient* audioClient = nullptr;
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&audioClient);
    if (FAILED(hr)) {
        std::cerr << "Activate(IAudioClient) failed: " << std::hex << hr << std::endl;
        device->Release();
        enumerator->Release();
        CoUninitialize();
        running = false;
        return 1;
    }

    WAVEFORMATEX* pwfx = nullptr;
    hr = audioClient->GetMixFormat(&pwfx);
    if (FAILED(hr)) {
        std::cerr << "GetMixFormat failed: " << std::hex << hr << std::endl;
        audioClient->Release();
        device->Release();
        enumerator->Release();
        CoUninitialize();
        running = false;
        return 1;
    }

    // Diagnostic: print the mix format obtained from the device
    std::cout << "WASAPI MIX FORMAT: wFormatTag=" << pwfx->wFormatTag
              << " nChannels=" << pwfx->nChannels
              << " nSamplesPerSec=" << pwfx->nSamplesPerSec
              << " wBitsPerSample=" << pwfx->wBitsPerSample
              << " nBlockAlign=" << pwfx->nBlockAlign
              << " nAvgBytesPerSec=" << pwfx->nAvgBytesPerSec
              << std::endl;

    // If the device provided WAVE_FORMAT_EXTENSIBLE, print extra fields
    if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        WAVEFORMATEXTENSIBLE* pwfe = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pwfx);
        std::cout << "WAVE_FORMAT_EXTENSIBLE: dwChannelMask=0x" << std::hex << pwfe->dwChannelMask << std::dec << std::endl;
        // print SubFormat GUID first 4 bytes for quick identification
        const unsigned char* guid = reinterpret_cast<const unsigned char*>(&pwfe->SubFormat);
        std::cout << "SubFormat GUID prefix: "
                  << std::hex << (int)guid[0] << " " << (int)guid[1] << " " << (int)guid[2] << " " << (int)guid[3] << std::dec << std::endl;
    }

    // prefer float32, but accept what the device gives
    if (pwfx->wFormatTag != WAVE_FORMAT_IEEE_FLOAT) {
        // In a production driver you'd set up a converter. For this minimal sample we assume float or handle simple cases.
    }

    REFERENCE_TIME hnsRequestedDuration = 10000000; // 1 second buffer
    hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                 hnsRequestedDuration,
                                 0,
                                 pwfx,
                                 NULL);
    if (FAILED(hr)) {
        std::cerr << "AudioClient Initialize failed: " << std::hex << hr << std::endl;
        CoTaskMemFree(pwfx);
        audioClient->Release();
        device->Release();
        enumerator->Release();
        CoUninitialize();
        running = false;
        return 1;
    }

    HANDLE hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!hEvent) {
        std::cerr << "CreateEvent failed" << std::endl;
    }
    hr = audioClient->SetEventHandle(hEvent);
    if (FAILED(hr)) {
        std::cerr << "SetEventHandle failed: " << std::hex << hr << std::endl;
    }

    IAudioRenderClient* renderClient = nullptr;
    hr = audioClient->GetService(IID_PPV_ARGS(&renderClient));
    if (FAILED(hr)) {
        std::cerr << "GetService(IAudioRenderClient) failed: " << std::hex << hr << std::endl;
    }

    UINT32 bufferFrameCount = 0;
    hr = audioClient->GetBufferSize(&bufferFrameCount);
    if (FAILED(hr)) bufferFrameCount = 0;

    // simple local buffer for frames
    const int channels = pwfx->nChannels;
    const int sampleRate = static_cast<int>(pwfx->nSamplesPerSec);
    const UINT32 framesPerIteration = (framesPerBuf > 0) ? framesPerBuf : (bufferFrameCount / 4 + 1);

    std::vector<float> mixbuf(framesPerIteration * channels);

    // Diagnostic summary
    std::cout << "WASAPI DIAGNOSTICS: bufferFrameCount=" << bufferFrameCount
              << " framesPerIteration=" << framesPerIteration
              << " channels=" << channels
              << " sampleRate=" << sampleRate
              << " bitsPerSample=" << pwfx->wBitsPerSample
              << " formatTag=" << pwfx->wFormatTag
              << std::endl;

    hr = audioClient->Start();
    if (FAILED(hr)) {
        std::cerr << "audioClient Start failed: " << std::hex << hr << std::endl;
    }

    // render loop - wait on event and write silence or callback output
    size_t iter = 0;
    size_t underrun_count = 0;
    auto last_wake = std::chrono::high_resolution_clock::now();
    while (running) {
        auto before_wait = std::chrono::high_resolution_clock::now();
        DWORD wait = WaitForSingleObject(hEvent, 2000);
        auto after_wait = std::chrono::high_resolution_clock::now();
        auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(after_wait - before_wait).count();
        if (wait == WAIT_TIMEOUT) {
            std::cerr << "WASAPI WARNING: WaitForSingleObject timeout" << std::endl;
        }
        // measure time since last wake
        auto since_last = std::chrono::duration_cast<std::chrono::milliseconds>(after_wait - last_wake).count();
        last_wake = after_wait;

        UINT32 padding = 0;
        hr = audioClient->GetCurrentPadding(&padding);
        UINT32 framesAvailable = (bufferFrameCount > padding) ? (bufferFrameCount - padding) : 0;
        if (FAILED(hr)) {
            std::cerr << "GetCurrentPadding failed: " << std::hex << hr << std::endl;
        }
        // periodic log
        if ((iter % 100) == 0) {
            std::cout << "WASAPI TRACE: iter=" << iter
                      << " wait_ms=" << wait_ms
                      << " since_last_ms=" << since_last
                      << " padding=" << padding
                      << " framesAvailable=" << framesAvailable
                      << std::endl;
        }

        while (framesAvailable >= framesPerIteration) {
            BYTE* pData = nullptr;
            hr = renderClient->GetBuffer(framesPerIteration, &pData);
            if (FAILED(hr)) break;

            // zero buffer
            memset(pData, 0, framesPerIteration * channels * sizeof(float));

            // if callback is set, call it to fill mixbuf, then copy
            if (callback) {
                callback(mixbuf.data(), framesPerIteration, userPtr);
                // copy to pData (assume float)
                float* out = reinterpret_cast<float*>(pData);
                for (UINT32 i = 0; i < framesPerIteration * (UINT32)channels; ++i) {
                    out[i] = mixbuf[i];
                }
            }

            hr = renderClient->ReleaseBuffer(framesPerIteration, 0);
            if (FAILED(hr)) {
                std::cerr << "ReleaseBuffer failed: " << std::hex << hr << std::endl;
                break;
            }

            hr = audioClient->GetCurrentPadding(&padding);
            framesAvailable = (bufferFrameCount > padding) ? (bufferFrameCount - padding) : 0;
            if (framesAvailable < framesPerIteration) {
                // underrun: not enough frames available for next write
                underrun_count++;
                if ((underrun_count % 10) == 0) {
                    std::cerr << "WASAPI UNDERRUN: count=" << underrun_count << " iter=" << iter << " framesAvailable=" << framesAvailable << std::endl;
                }
            }
            iter++;
        }
    }

    audioClient->Stop();

    if (renderClient) renderClient->Release();
    if (hEvent) CloseHandle(hEvent);
    CoTaskMemFree(pwfx);
    if (audioClient) audioClient->Release();
    if (device) device->Release();
    if (enumerator) enumerator->Release();

    CoUninitialize();
    return 0;
}
