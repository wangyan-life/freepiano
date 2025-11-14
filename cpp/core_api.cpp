#include "include/core_api.h"
#include "include/spsc_ring.h"
#include "wasapi/wasapi_driver.h"
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>
#include <memory>

using namespace fp;

static std::unique_ptr<WasapiDriver> g_driver;
static std::unique_ptr<SpscRing<float>> g_ring;
static std::thread g_producer;
static std::atomic<bool> g_running(false);
static int g_sample_rate = 48000;
static int g_channels = 2;
static int g_frames_per_buf = 256;
static fp_audio_callback_t g_user_cb = nullptr;
static void* g_user_ptr = nullptr;

fp_result_t fp_init(void)
{
    // nothing for now
    return FP_OK;
}

fp_result_t fp_shutdown(void)
{
    fp_stop_stream();
    fp_close_device();
    return FP_OK;
}

fp_result_t fp_open_default_device(int sample_rate, int channels, int frames_per_buffer)
{
    if (g_driver) return FP_ERR_GENERIC;
    g_sample_rate = sample_rate > 0 ? sample_rate : 48000;
    g_channels = (channels >= 1) ? channels : 2;
    g_frames_per_buf = (frames_per_buffer > 0) ? frames_per_buffer : 256;

    g_driver.reset(new WasapiDriver());
    if (!g_driver->open(g_sample_rate, g_channels, g_frames_per_buf)) {
        g_driver.reset();
        return FP_ERR_DEVICE;
    }

    // Query device mix sample rate and align producer sample rate to avoid hardcoding mismatches
    int device_sr = g_driver->query_mix_sample_rate();
    if (device_sr > 0 && device_sr != g_sample_rate) {
        // set producer/sample scheduling to the device rate
        g_sample_rate = device_sr;
    }

    // allocate ring: store floats; capacity = power-of-two number of samples
    size_t capacity = 1u << 16; // 65536 samples
    g_ring.reset(new SpscRing<float>(capacity));

    return FP_OK;
}

fp_result_t fp_close_device(void)
{
    fp_stop_stream();
    g_driver.reset();
    g_ring.reset();
    return FP_OK;
}

static void producer_thread()
{
    const size_t frames = (size_t)g_frames_per_buf;
    std::vector<float> tmp(frames * g_channels);
    using namespace std::chrono;
    auto period = duration<double>(double(frames) / double(g_sample_rate));
    while (g_running.load(std::memory_order_acquire)) {
        if (g_user_cb) {
            g_user_cb(tmp.data(), frames, g_user_ptr);
            // try to push all samples; if ring full, drop
            size_t pushed = g_ring->push(tmp.data(), tmp.size());
            (void)pushed;
        }
        std::this_thread::sleep_for(period);
    }
}

fp_result_t fp_start_stream(fp_audio_callback_t cb, void* user)
{
    if (!g_driver) return FP_ERR_NOT_INITIALIZED;
    if (g_running.load(std::memory_order_acquire)) return FP_ERR_GENERIC;

    g_user_cb = cb;
    g_user_ptr = user;
    g_running.store(true, std::memory_order_release);

    // start producer
    g_producer = std::thread(producer_thread);

    // start wasapi driver with a bridge callback that pulls from ring
    auto bridge = [](float* interleaved, size_t frames, void* user)->void {
        (void)user;
        size_t want = frames * g_channels;
        size_t got = g_ring->pop(interleaved, want);
        if (got < want) {
            // zero the rest
            for (size_t i = got; i < want; ++i) interleaved[i] = 0.0f;
        }
    };

    // start driver with bridge
    if (!g_driver->start(bridge, nullptr)) {
        g_running.store(false, std::memory_order_release);
        if (g_producer.joinable()) g_producer.join();
        return FP_ERR_DEVICE;
    }

    return FP_OK;
}

fp_result_t fp_stop_stream(void)
{
    if (!g_running.load(std::memory_order_acquire)) return FP_OK;
    g_running.store(false, std::memory_order_release);
    if (g_driver) g_driver->stop();
    if (g_producer.joinable()) g_producer.join();
    g_user_cb = nullptr;
    g_user_ptr = nullptr;
    return FP_OK;
}
