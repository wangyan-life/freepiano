#ifndef FP_CORE_API_H
#define FP_CORE_API_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#if defined(_WIN32) || defined(_WIN64)
  #ifdef FP_BUILDING_DLL
    #define FP_API __declspec(dllexport)
  #else
    #define FP_API __declspec(dllimport)
  #endif
#else
  #define FP_API
#endif

typedef enum {
    FP_OK = 0,
    FP_ERR_GENERIC = -1,
    FP_ERR_NOT_INITIALIZED = -2,
    FP_ERR_DEVICE = -3,
    FP_ERR_INVALID_ARG = -4
} fp_result_t;

/* Audio callback invoked on the audio thread. Interleaved float samples (32-bit float) are passed.
 * frames: number of frames to render (per channel).
 * user: opaque pointer passed through from start call.
 */
typedef void (__cdecl *fp_audio_callback_t)(float* interleaved, size_t frames, void* user);

/* Lifecycle */
FP_API fp_result_t fp_init(void);
FP_API fp_result_t fp_shutdown(void);

/* Open the default render device. Must be called before start.
 * sample_rate: requested sample rate (driver may choose mix format if mismatched)
 * channels: number of channels (1=mono,2=stereo)
 * frames_per_buffer: desired frames per callback; 0 means driver choose.
 */
FP_API fp_result_t fp_open_default_device(int sample_rate, int channels, int frames_per_buffer);
FP_API fp_result_t fp_close_device(void);

/* Start/stop streaming. The callback will be called on the producer thread (synth thread).
 * The callback must fill exactly (frames * channels) floats into the buffer.
 */
FP_API fp_result_t fp_start_stream(fp_audio_callback_t cb, void* user);
FP_API fp_result_t fp_stop_stream(void);

#ifdef __cplusplus
}
#endif

#endif /* FP_CORE_API_H */
