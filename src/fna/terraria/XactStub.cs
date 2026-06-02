using System;
using System.Reflection;

[assembly: AssemblyTitle("Microsoft.Xna.Framework.Xact")]
[assembly: AssemblyProduct("Microsoft.Xna.Framework.Xact")]
[assembly: AssemblyVersion("4.0.0.0")]

namespace Microsoft.Xna.Framework.Audio
{
    public enum AudioStopOptions
    {
        AsAuthored,
        Immediate
    }

    public sealed class AudioEngine : IDisposable
    {
        public float CategoryVolume { get; set; }

        public AudioEngine(string settingsFile) { }
        public AudioEngine(string settingsFile, TimeSpan lookAheadTime, string rendererId) { }

        public void Dispose() { }
        public void Update() { }
        public AudioCategory GetCategory(string name) { return new AudioCategory(name); }
    }

    public sealed class AudioCategory
    {
        readonly string _name;

        public AudioCategory(string name) { _name = name; }

        public string Name { get { return _name; } }
        public void Pause() { }
        public void Resume() { }
        public void SetVolume(float volume) { }
        public void Stop(AudioStopOptions options) { }
    }

    public sealed class WaveBank : IDisposable
    {
        public WaveBank(AudioEngine audioEngine, string nonStreamingWaveBankFilename) { }
        public WaveBank(AudioEngine audioEngine, string streamingWaveBankFilename, int offset, short packetsize) { }
        public WaveBank(AudioEngine audioEngine, string streamingWaveBankFilename, int offset, short packetsize, bool simulate) { }

        public bool IsPrepared { get { return true; } }

        public void Dispose() { }
    }

    public sealed class SoundBank : IDisposable
    {
        public SoundBank(AudioEngine audioEngine, string filename) { }

        public void Dispose() { }
        public Cue GetCue(string name) { return new Cue(name); }
        public void PlayCue(string name) { }
        public void PlayCue(string name, AudioListener listener, AudioEmitter emitter) { }
    }

    public sealed class Cue : IDisposable
    {
        readonly string _name;

        public Cue(string name) { _name = name; }

        public string Name { get { return _name; } }
        public bool IsCreated { get { return true; } }
        public bool IsDisposed { get; private set; }
        public bool IsPaused { get; private set; }
        public bool IsPlaying { get; private set; }
        public bool IsPrepared { get { return true; } }
        public bool IsStopped { get { return !IsPlaying; } }

        public void Apply3D(AudioListener listener, AudioEmitter emitter) { }
        public void Dispose() { IsDisposed = true; IsPlaying = false; }
        public float GetVariable(string name) { return 0.0f; }
        public void Pause() { IsPaused = true; IsPlaying = false; }
        public void Play() { IsPaused = false; IsPlaying = true; }
        public void Resume() { IsPaused = false; IsPlaying = true; }
        public void SetVariable(string name, float value) { }
        public void Stop(AudioStopOptions options) { IsPlaying = false; }
    }

    public sealed class AudioListener
    {
    }

    public sealed class AudioEmitter
    {
    }
}
