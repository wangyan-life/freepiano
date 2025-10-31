using System;
using System.Runtime.InteropServices;
using System.Threading;

internal static class Native
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public unsafe delegate void AudioCallback(float* interleaved, UIntPtr frames, IntPtr user);

    [DllImport("freepiano_minimal.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public static extern int fp_init();
    [DllImport("freepiano_minimal.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public static extern int fp_open_default_device(int sampleRate, int channels, int framesPerBuffer);
    [DllImport("freepiano_minimal.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public static extern int fp_start_stream(IntPtr cb, IntPtr user);
    [DllImport("freepiano_minimal.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public static extern int fp_stop_stream();

}

class Program
{
    // keep delegate alive
    static Native.AudioCallback? callback;

    static unsafe void SineCallback(float* interleaved, UIntPtr frames, IntPtr user)
    {
        int ch = 2;
        int sampleRate = 48000;
        double freq = 440.0;
        int frameCount = (int)frames;
        // simple static phase
        double* phasePtr = (double*)user.ToPointer();
        double phase = *phasePtr;
        for (int i = 0; i < frameCount; ++i)
        {
            float sample = (float)(Math.Sin(phase) * 0.2);
            phase += 2.0 * Math.PI * freq / sampleRate;
            if (phase > 2.0 * Math.PI) phase -= 2.0 * Math.PI;
            for (int c = 0; c < ch; ++c)
            {
                interleaved[i * ch + c] = sample;
            }
        }
        *phasePtr = phase;
    }

    static void Main()
    {
        Console.WriteLine("WasapiDemo starting");
        Native.fp_init();
        Native.fp_open_default_device(48000, 2, 256);

        double phase = 0.0;
        callback = new Native.AudioCallback(SineCallback);
        IntPtr fp = Marshal.GetFunctionPointerForDelegate(callback);
        IntPtr user = new IntPtr(&phase);
        Native.fp_start_stream(fp, user);

        Console.WriteLine("Streaming for 5 seconds...");
        Thread.Sleep(5000);

        Native.fp_stop_stream();
        Console.WriteLine("Stopped");
    }
}
