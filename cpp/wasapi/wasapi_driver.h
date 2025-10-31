#pragma once
// Minimal WASAPI render driver skeleton
// - uses shared mode
// - creates a render thread which fills the audio buffer using a user-provided callback

#include <windows.h>
#include <functional>

typedef void (__cdecl *fp_audio_callback_t)(float* interleaved, size_t frames, void* user);

class WasapiDriver {
public:
    WasapiDriver();
    ~WasapiDriver();

    // non-copyable
    WasapiDriver(const WasapiDriver&) = delete;
    WasapiDriver& operator=(const WasapiDriver&) = delete;

    bool open(int sampleRate, int channels, int framesPerBuffer);
    void close();
    bool start(fp_audio_callback_t cb, void* user);
    void stop();

private:
    HANDLE threadHandle;
    volatile bool running;
    int sr;
    int ch;
    int framesPerBuf;
    fp_audio_callback_t callback;
    void* userPtr;

    static DWORD WINAPI render_thread_proc(LPVOID param);
    DWORD render_thread();
};
