//-----------------------------------------------------------------------------
// wah_render - offline test / demo tool for the WahCore DSP
//
//   wah_render ir   <pedal 0..1> <drive 0..1> <res 0..1> <tone 0..1> <out.wav>
//   wah_render proc <in.wav> <out.wav> <pedal> <drive> <res> <tone> <vol> [auto]
//   wah_render demo <outDry.wav> <outWet.wav> [drive] [res] [tone]
//
// Used for verifying the model (impulse response) and for producing demo
// audio without a DAW.
//-----------------------------------------------------------------------------
#include "../src/dsp/WahCore.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
//--------------------------------------------------------------- WAV helpers
struct Wav
{
    std::vector<float> samples;
    int sampleRate = 48000;
    int channels = 1;
};

bool readWav (const char* path, Wav& wav)
{
    FILE* f = std::fopen (path, "rb");
    if (f == nullptr)
        return false;

    char riff[4], wave[4];
    std::uint32_t size = 0;
    std::fread (riff, 1, 4, f);
    std::fread (&size, 4, 1, f);
    std::fread (wave, 1, 4, f);
    if (std::memcmp (riff, "RIFF", 4) != 0 || std::memcmp (wave, "WAVE", 4) != 0)
    {
        std::fclose (f);
        return false;
    }

    std::uint16_t format = 0, channels = 0, bits = 0;
    std::uint32_t rate = 0, dataSize = 0;
    bool haveFmt = false;

    char id[4];
    std::uint32_t chunkSize = 0;
    while (std::fread (id, 1, 4, f) == 4 && std::fread (&chunkSize, 4, 1, f) == 1)
    {
        if (std::memcmp (id, "fmt ", 4) == 0)
        {
            std::fread (&format, 2, 1, f);
            std::fread (&channels, 2, 1, f);
            std::fread (&rate, 4, 1, f);
            std::fseek (f, 6, SEEK_CUR);
            std::fread (&bits, 2, 1, f);
            if (chunkSize > 16)
                std::fseek (f, static_cast<long> (chunkSize) - 16, SEEK_CUR);
            haveFmt = true;
        }
        else if (std::memcmp (id, "data", 4) == 0)
        {
            dataSize = chunkSize;
            break;
        }
        else
        {
            std::fseek (f, static_cast<long> (chunkSize), SEEK_CUR);
        }
    }

    if (! haveFmt || dataSize == 0 || (format != 1 && format != 3) || bits != 16)
    {
        std::fclose (f);
        return false;
    }

    const std::size_t frames = dataSize / (2u * channels);
    wav.channels = channels;
    wav.sampleRate = static_cast<int> (rate);
    wav.samples.resize (frames * channels);

    std::vector<std::int16_t> raw (frames * channels);
    std::fread (raw.data(), 2, raw.size(), f);
    std::fclose (f);

    for (std::size_t i = 0; i < raw.size(); ++i)
        wav.samples[i] = static_cast<float> (raw[i]) / 32768.0f;

    return true;
}

bool writeWav (const char* path, const Wav& wav)
{
    FILE* f = std::fopen (path, "wb");
    if (f == nullptr)
        return false;

    const std::uint32_t dataSize = static_cast<std::uint32_t> (wav.samples.size() * 2);
    const std::uint32_t riffSize = 36 + dataSize;
    const std::uint16_t channels = static_cast<std::uint16_t> (wav.channels);
    const std::uint32_t rate = static_cast<std::uint32_t> (wav.sampleRate);
    const std::uint32_t byteRate = rate * channels * 2;
    const std::uint16_t blockAlign = static_cast<std::uint16_t> (channels * 2);
    const std::uint16_t bits = 16;
    const std::uint16_t format = 1;

    std::fwrite ("RIFF", 1, 4, f);
    std::fwrite (&riffSize, 4, 1, f);
    std::fwrite ("WAVEfmt ", 1, 8, f);
    const std::uint32_t fmtSize = 16;
    std::fwrite (&fmtSize, 4, 1, f);
    std::fwrite (&format, 2, 1, f);
    std::fwrite (&channels, 2, 1, f);
    std::fwrite (&rate, 4, 1, f);
    std::fwrite (&byteRate, 4, 1, f);
    std::fwrite (&blockAlign, 2, 1, f);
    std::fwrite (&bits, 2, 1, f);
    std::fwrite ("data", 1, 4, f);
    std::fwrite (&dataSize, 4, 1, f);

    for (float s : wav.samples)
    {
        const float c = wah::clampf (s, -1.0f, 1.0f);
        const std::int16_t v = static_cast<std::int16_t> (std::lrint (c * 32767.0f));
        std::fwrite (&v, 2, 1, f);
    }

    std::fclose (f);
    return true;
}

//--------------------------------------------------------------- test signal
// Karplus-Strong plucked string with a little pick noise; "guitar-ish" enough
// to judge the pedal by ear and to measure the sweep.
struct Pluck
{
    std::vector<float> delay;
    std::size_t pos = 0;
    float damp = 0.5f;
    float level = 0.8f;
    int countdown = 0;

    void trigger (float freq, float sampleRate, float amp)
    {
        const std::size_t len = static_cast<std::size_t> (sampleRate / freq);
        delay.assign (len, 0.0f);
        pos = 0;
        level = amp;
        damp = 0.996f;

        std::uint32_t seed = 22222;
        for (auto& d : delay)
        {
            seed = seed * 1664525u + 1013904223u;
            d = (static_cast<float> ((seed >> 9) & 0x7fff) / 16384.0f - 1.0f) * 0.7f;
        }

        // pick position comb: bright, slightly nasal
        for (std::size_t i = 0; i < len / 5; ++i)
            delay[i] *= 0.35f;
    }

    float process()
    {
        if (delay.empty())
            return 0.0f;

        const float out = delay[pos];
        const std::size_t next = (pos + 1) % delay.size();
        delay[pos] = damp * 0.5f * (delay[pos] + delay[next]);
        pos = next;
        return out * level;
    }
};

float noteHz (int midi)
{
    return 440.0f * std::pow (2.0f, static_cast<float> (midi - 69) / 12.0f);
}

// A short funk riff in E minor, 96 BPM, sixteenth grid.
void makeRiff (Wav& out, int sampleRate)
{
    const float bpm = 96.0f;
    const int sr = sampleRate;
    const float step = 60.0f / bpm / 4.0f; // sixteenth
    const int steps = 64;

    struct Hit { int step; int note; float amp; };
    // E2 = 40, G2 = 43, A2 = 45, B2 = 47, D3 = 50, E3 = 52
    const Hit hits[] = {
        {0, 40, 1.0f}, {2, 40, 0.7f}, {3, 52, 0.8f}, {6, 47, 0.9f},
        {8, 45, 0.95f}, {10, 40, 0.6f}, {11, 43, 0.8f}, {12, 40, 1.0f},
        {16, 47, 0.9f}, {18, 50, 0.85f}, {19, 47, 0.7f}, {22, 45, 0.9f},
        {24, 40, 1.0f}, {26, 40, 0.55f}, {27, 52, 0.85f}, {30, 47, 0.8f},
        {32, 45, 0.95f}, {34, 43, 0.8f}, {35, 40, 0.7f}, {38, 47, 0.9f},
        {40, 50, 0.85f}, {41, 52, 0.8f}, {44, 47, 0.9f}, {46, 45, 0.75f},
        {48, 40, 1.0f}, {50, 40, 0.6f}, {51, 43, 0.8f}, {54, 45, 0.9f},
        {56, 47, 0.95f}, {58, 50, 0.8f}, {59, 47, 0.7f}, {62, 40, 1.0f},
    };

    const int frames = static_cast<int> (steps * step * sr) + sr / 2;
    out.samples.assign (static_cast<std::size_t> (frames), 0.0f);
    out.sampleRate = sr;
    out.channels = 1;

    Pluck pluck;
    int nextHit = 0;
    for (int i = 0; i < frames; ++i)
    {
        const float t = static_cast<float> (i) / static_cast<float> (sr);
        const int curStep = static_cast<int> (t / step);

        while (nextHit < static_cast<int> (sizeof (hits) / sizeof (hits[0])) && hits[nextHit].step == curStep)
        {
            pluck.trigger (noteHz (hits[nextHit].note), static_cast<float> (sr), hits[nextHit].amp);
            ++nextHit;
        }

        float s = pluck.process();

        // gentle body resonance + high-pass, no distortion
        static float lp = 0.0f;
        lp += 0.25f * (s - lp);
        s = s * 0.85f + lp * 0.25f;

        out.samples[static_cast<std::size_t> (i)] = s * 0.55f;
    }

    // normalise
    float peak = 0.0f;
    for (float s : out.samples)
        peak = std::max (peak, std::fabs (s));
    if (peak > 0.0f)
        for (auto& s : out.samples)
            s *= 0.7f / peak;
}

//--------------------------------------------------------------- processing
void processBuffer (Wav& wav, const wah::Params& base, bool automate)
{
    const int frames = static_cast<int> (wav.samples.size() / wav.channels);
    const int numChannels = wav.channels;

    std::vector<std::vector<float>> chans (static_cast<std::size_t> (numChannels),
                                           std::vector<float> (static_cast<std::size_t> (frames)));

    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < frames; ++i)
            chans[static_cast<std::size_t> (ch)][static_cast<std::size_t> (i)] =
                wav.samples[static_cast<std::size_t> (i) * numChannels + ch];

    wah::Engine engine;
    engine.setParams (base);
    engine.prepare (wav.sampleRate, 512, numChannels);

    const int block = 64;
    for (int start = 0; start < frames; start += block)
    {
        const int n = std::min (block, frames - start);

        wah::Params p = base;
        if (automate)
        {
            // slow heel/toe sweep, one cycle every four seconds
            const double t = static_cast<double> (start) / wav.sampleRate;
            p.pedal = static_cast<float> (0.5 + 0.45 * std::sin (t * 2.0 * 3.14159265358979 / 4.0));
        }

        engine.setParams (p);

        float* ptrs[wah::Engine::kMaxChannels] = {};
        for (int ch = 0; ch < numChannels && ch < wah::Engine::kMaxChannels; ++ch)
            ptrs[ch] = chans[static_cast<std::size_t> (ch)].data() + start;

        engine.process (ptrs, numChannels, n);
    }

    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < frames; ++i)
            wav.samples[static_cast<std::size_t> (i) * numChannels + ch] =
                chans[static_cast<std::size_t> (ch)][static_cast<std::size_t> (i)];
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: ir | proc | demo\n");
        return 1;
    }

    const std::string mode = argv[1];

    if (mode == "ir" && argc >= 7)
    {
        wah::Params p;
        p.pedal = std::stof (argv[2]);
        p.drive = std::stof (argv[3]);
        p.resonance = std::stof (argv[4]);
        p.tone = std::stof (argv[5]);
        p.volume = 0.5f;

        Wav wav;
        wav.sampleRate = 48000;
        wav.channels = 1;
        wav.samples.assign (48000, 0.0f);
        wav.samples[100] = 0.1f;

        processBuffer (wav, p, false);
        if (! writeWav (argv[6], wav))
            return 2;
        std::printf ("wrote %s\n", argv[6]);
        return 0;
    }

    if (mode == "proc" && argc >= 9)
    {
        Wav in;
        if (! readWav (argv[2], in))
        {
            std::printf ("cannot read %s\n", argv[2]);
            return 2;
        }

        wah::Params p;
        p.pedal = std::stof (argv[4]);
        p.drive = std::stof (argv[5]);
        p.resonance = std::stof (argv[6]);
        p.tone = std::stof (argv[7]);
        p.volume = std::stof (argv[8]);
        const bool automate = argc >= 10 && std::string (argv[9]) == "auto";

        processBuffer (in, p, automate);
        if (! writeWav (argv[3], in))
            return 2;
        std::printf ("wrote %s\n", argv[3]);
        return 0;
    }

    if (mode == "demo" && argc >= 4)
    {
        const float drive = argc > 5 ? std::stof (argv[5]) : 0.32f;
        const float res = argc > 6 ? std::stof (argv[6]) : 0.55f;
        const float tone = argc > 7 ? std::stof (argv[7]) : 0.55f;

        Wav dry;
        makeRiff (dry, 48000);
        if (! writeWav (argv[2], dry))
            return 2;

        Wav wet = dry;
        wah::Params p;
        p.pedal = 0.5f;
        p.drive = drive;
        p.resonance = res;
        p.tone = tone;
        p.volume = 0.5f;
        processBuffer (wet, p, true);

        if (! writeWav (argv[3], wet))
            return 2;

        std::printf ("wrote %s and %s\n", argv[2], argv[3]);
        return 0;
    }

    std::printf ("bad arguments\n");
    return 1;
}
