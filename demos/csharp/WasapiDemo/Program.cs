using System;
using System.Runtime.InteropServices;
using System.Threading;

internal static class Native
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void AudioCallback(IntPtr interleaved, UIntPtr frames, IntPtr user);

    [DllImport("freepiano_minimal.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public static extern int fp_init();
    [DllImport("freepiano_minimal.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public static extern int fp_open_default_device(int sampleRate, int channels, int framesPerBuffer);
        [DllImport("freepiano_minimal.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int fp_get_effective_sample_rate();
    [DllImport("freepiano_minimal.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public static extern int fp_start_stream(IntPtr cb, IntPtr user);
    [DllImport("freepiano_minimal.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public static extern int fp_stop_stream();

}

class Program
{
    // keep delegate alive
    static Native.AudioCallback? callback;
    static double _phase = 0.0;
    static int synthSampleRate = 44100;

    static void SineCallback(IntPtr interleavedPtr, UIntPtr frames, IntPtr user)
    {
    int ch = 2;
    int sampleRate = synthSampleRate; // default
        // if native returned an effective sample rate, use it
        // note: synthSampleRate variable is set in Main and used by callback
    double freq = 440.0;
        int frameCount = (int)frames;

        // use managed static phase to avoid unsafe pointer usage
        // keep it simple for the demo
        // generate into a managed buffer then copy to native memory
        float[] buffer = new float[frameCount * ch];
        for (int i = 0; i < frameCount; ++i)
        {
            float sample = (float)(Math.Sin(_phase) * 0.1); // lower amplitude to avoid clipping
            _phase += 2.0 * Math.PI * freq / sampleRate;
            if (_phase > 2.0 * Math.PI) _phase -= 2.0 * Math.PI;
            for (int c = 0; c < ch; ++c)
            {
                buffer[i * ch + c] = sample;
            }
        }

        // copy managed float[] to native buffer
        System.Runtime.InteropServices.Marshal.Copy(buffer, 0, interleavedPtr, buffer.Length);
    }

    static void Main()
    {
        Console.WriteLine("WasapiDemo starting");
        Native.fp_init();
        // request driver choose the best mix format (pass 0) and then query effective sample rate
        Native.fp_open_default_device(0, 2, 256);
    int effective = Native.fp_get_effective_sample_rate();
    synthSampleRate = (effective > 0) ? effective : 44100;

    callback = new Native.AudioCallback(SineCallback);
    IntPtr fp = Marshal.GetFunctionPointerForDelegate(callback);
    Native.fp_start_stream(fp, IntPtr.Zero);

        Console.WriteLine("Streaming for 5 seconds...");
        Thread.Sleep(5000);

        Native.fp_stop_stream();
        Console.WriteLine("Stopped");
    }
}
