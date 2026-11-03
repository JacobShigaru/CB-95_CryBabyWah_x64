//-----------------------------------------------------------------------------
// CryBabyWah - virtual-analog model of a Cry Baby style wah pedal
//
// The model follows the signal flow of the classic discrete circuit:
//
//   input buffer -> high-pass (coupling caps) -> transconductance gain stage
//   (asymmetric, saturating) -> resonant LC tank (2-pole band-pass, swept)
//   -> output buffer with tone shaping
//
// The resonant tank is implemented as a zero-delay-feedback state variable
// filter (TPT), which stays stable while the pedal sweeps quickly.  The
// saturating transistor stage uses first order antiderivative antialiasing,
// so the harmonics it generates do not fold back into the audio band.
//
// Header only, no dependencies besides the C++ standard library.
//-----------------------------------------------------------------------------
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace wah
{

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

inline float clampf (float v, float lo, float hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

//-----------------------------------------------------------------------------
// One pole parameter smoother.  All parameters are smoothed so that host
// automation, mouse drags and the pedal's own mechanical inertia never produce
// zipper noise or filter coefficient jumps.
//-----------------------------------------------------------------------------
class Smoother
{
public:
    void reset (float sampleRate, float timeMs, float initialValue) noexcept
    {
        setTime (sampleRate, timeMs);
        current = target = initialValue;
    }

    void setTime (float sampleRate, float timeMs) noexcept
    {
        const float t = std::max (timeMs, 0.01f) * 0.001f;
        coeff = 1.0f - std::exp (-1.0f / (t * std::max (sampleRate, 1.0f)));
    }

    void setTarget (float v) noexcept { target = v; }
    void snap (float v) noexcept { current = target = v; }
    float next() noexcept { return current += coeff * (target - current); }
    float value() const noexcept { return current; }

private:
    float coeff = 0.1f, current = 0.0f, target = 0.0f;
};

//-----------------------------------------------------------------------------
// tanh with antiderivative antialiasing (first order).
//-----------------------------------------------------------------------------
class AdaaTanh
{
public:
    void reset() noexcept
    {
        x1 = 0.0f;
        f1 = 0.0f;
    }

    float process (float x) noexcept
    {
        const float f = antiderivative (x);
        const float dx = x - x1;
        float y;

        if (std::fabs (dx) > 1.0e-5f)
            y = (f - f1) / dx;
        else
            y = std::tanh (0.5f * (x + x1));

        x1 = x;
        f1 = f;
        return y;
    }

private:
    static float antiderivative (float x) noexcept
    {
        // log (2 cosh x) = |x| + log1p (exp (-2|x|)) - log 2
        const float ax = std::fabs (x);
        return ax + std::log1p (std::exp (-2.0f * ax)) - 0.693147180559945309f;
    }

    float x1 = 0.0f, f1 = 0.0f;
};

//-----------------------------------------------------------------------------
// Zero-delay-feedback state variable filter (Andrew Simper / Vadim Zavalishin
// TPT form).  Stable under heavy modulation.
//-----------------------------------------------------------------------------
class Svf
{
public:
    void reset() noexcept
    {
        s1 = s2 = 0.0f;
    }

    void setCoeffs (float frequency, float q, float sampleRate) noexcept
    {
        const float f = clampf (frequency, 20.0f, sampleRate * 0.45f);
        const float g = std::tan (kPi * f / sampleRate);
        const float k = 1.0f / std::max (q, 0.5f);

        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        kk = k;
    }

    // returns band-pass output
    float bandPass (float x) noexcept
    {
        const float v3 = x - s2;
        const float v1 = a1 * s1 + a2 * v3;
        const float v2 = s2 + a2 * s1 + a3 * v3;

        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;

        s1 = flush (s1);
        s2 = flush (s2);

        return v1;
    }

    float k() const noexcept { return kk; }

private:
    static float flush (float v) noexcept
    {
        return (std::fabs (v) < 1.0e-20f) ? 0.0f : v;
    }

    float s1 = 0.0f, s2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, kk = 1.0f;
};

//-----------------------------------------------------------------------------
// TPT one pole low-pass / high-pass
//-----------------------------------------------------------------------------
class OnePole
{
public:
    void reset() noexcept { s = 0.0f; }

    void setLowPass (float frequency, float sampleRate) noexcept
    {
        const float g = std::tan (kPi * clampf (frequency, 5.0f, sampleRate * 0.45f) / sampleRate);
        G = g / (1.0f + g);
        highPassMode = false;
    }

    void setHighPass (float frequency, float sampleRate) noexcept
    {
        const float g = std::tan (kPi * clampf (frequency, 5.0f, sampleRate * 0.45f) / sampleRate);
        G = g / (1.0f + g);
        highPassMode = true;
    }

    float process (float x) noexcept
    {
        const float v = (x - s) * G;
        const float lp = v + s;
        s = lp + v;
        s = (std::fabs (s) < 1.0e-20f) ? 0.0f : s;

        return highPassMode ? x - lp : lp;
    }

private:
    float s = 0.0f, G = 0.0f;
    bool highPassMode = false;
};

//-----------------------------------------------------------------------------
// DC blocker (used after the asymmetric gain stage)
//-----------------------------------------------------------------------------
class DcBlocker
{
public:
    void reset() noexcept { x1 = y1 = 0.0f; }

    void prepare (float sampleRate) noexcept
    {
        r = std::exp (-kTwoPi * 12.0f / std::max (sampleRate, 1.0f));
    }

    float process (float x) noexcept
    {
        const float y = x - x1 + r * y1;
        x1 = x;
        y1 = (std::fabs (y) < 1.0e-20f) ? 0.0f : y;
        return y;
    }

private:
    float x1 = 0.0f, y1 = 0.0f, r = 0.999f;
};

//-----------------------------------------------------------------------------
// Parameters (all normalised 0..1)
//-----------------------------------------------------------------------------
struct Params
{
    float pedal = 0.45f;      // 0 = heel down (low), 1 = toe down (high)
    float drive = 0.30f;      // input stage saturation / gain
    float resonance = 0.55f;  // tank Q
    float low = 0.55f;        // lower edge of the sweep (normalised)
    float high = 0.66f;       // upper edge of the sweep (normalised)
    float tone = 0.55f;       // output low-pass (dark .. bright)
    float mix = 1.0f;         // dry / wet (1 = fully processed)
    float volume = 0.70f;     // output level
    bool bypass = false;
};

//-----------------------------------------------------------------------------
// The pedal
//-----------------------------------------------------------------------------
class Engine
{
public:
    static constexpr int kMaxChannels = 2;

    void prepare (double sampleRate, int maxBlockSize, int maxChannels = kMaxChannels) noexcept
    {
        (void) maxBlockSize;
        fs = static_cast<float> (std::max (sampleRate, 8000.0));
        numChannels = std::max (1, std::min (maxChannels, kMaxChannels));
        reset();

        pedalSm.reset (fs, 18.0f, params.pedal);
        driveSm.reset (fs, 25.0f, params.drive);
        resSm.reset (fs, 25.0f, params.resonance);
        lowSm.reset (fs, 25.0f, params.low);
        highSm.reset (fs, 25.0f, params.high);
        toneSm.reset (fs, 25.0f, params.tone);
        volSm.reset (fs, 25.0f, params.volume);
        mixSm.reset (fs, 25.0f, params.mix);

        // start exactly at the current parameter values (no fade-in sweep)
        pedalSm.snap (params.pedal);
        driveSm.snap (params.drive);
        resSm.snap (params.resonance);
        lowSm.snap (params.low);
        highSm.snap (params.high);
        toneSm.snap (params.tone);
        volSm.snap (params.volume);
        mixSm.snap (params.mix);

        for (auto& ch : state)
        {
            ch.dc.prepare (fs);
            ch.inHp.setHighPass (95.0f, fs);
            ch.outHp.setHighPass (45.0f, fs);
        }

        updateTone (toneSm.value());
        updateFilter (pedalSm.value(), resSm.value(), lowSm.value(), highSm.value());
    }

    void reset() noexcept
    {
        for (auto& ch : state)
            ch.reset();
    }

    void setParams (const Params& p) noexcept
    {
        params = p;
        pedalSm.setTarget (p.pedal);
        driveSm.setTarget (p.drive);
        resSm.setTarget (p.resonance);
        lowSm.setTarget (p.low);
        highSm.setTarget (p.high);
        toneSm.setTarget (p.tone);
        volSm.setTarget (p.volume);
        mixSm.setTarget (p.mix);
    }

    // Smoothed pedal position used by the audio thread (for GUI feedback).
    float currentPedal() const noexcept { return pedalSm.value(); }

    // In-place processing.  Each channel has its own filter state.
    void process (float* const* channels, int numChannelsIn, int numSamples) noexcept
    {
        if (params.bypass)
            return; // true bypass: leave the host buffer untouched

        const int nch = std::min (numChannelsIn, kMaxChannels);

        for (int i = 0; i < numSamples; ++i)
        {
            const float pedal = pedalSm.next();
            const float drive = driveSm.next();
            const float res = resSm.next();
            const float lowN = lowSm.next();
            const float highN = highSm.next();
            const float tone = toneSm.next();
            const float vol = volSm.next();
            const float mix = mixSm.next();

            if ((i & 7) == 0)
            {
                updateFilter (pedal, res, lowN, highN);
                updateTone (tone);
            }

            const float inputGain = 0.75f + 3.2f * drive;
            const float bias = 0.06f + 0.10f * pedal; // bias shift -> harmonics move with the sweep
            const float biasOut = std::tanh (bias);
            const float q = pedalToQ (res, pedal);
            const float peakGain = 1.45f + 0.9f * res;
            const float makeup = 1.0f + 0.45f * pedal;
            const float outGain = volumeGain (vol);

            for (int ch = 0; ch < nch; ++ch)
            {
                float* buf = channels[ch];
                if (buf == nullptr)
                    continue;

                auto& s = state[ch];
                const float dry = buf[i];
                float x = dry;

                // input coupling capacitor
                x = s.inHp.process (x);

                // transistor gain stage: asymmetric soft saturation
                float driven = s.sat.process (x * inputGain + bias) - biasOut;
                driven = s.dc.process (driven);

                // resonant LC tank (band-pass), normalised to unity peak so the
                // resonance control changes sharpness rather than only level
                float y = s.svf.bandPass (driven) / q;

                // transistor / op-amp compression around the resonant peak
                y *= peakGain;
                y = std::tanh (y * 1.35f) * (1.0f / 1.35f);

                // makeup: the pedal gets thinner, but not quieter, as it sweeps up
                y *= makeup;

                // output tone shaping
                y = s.toneLP.process (y);
                y = s.outHp.process (y);

                // dry / wet balance
                y = dry + (y - dry) * mix;

                y *= outGain;

                // final safety clip
                y = std::tanh (clampf (y, -1.2f, 1.2f));

                buf[i] = y;
            }
        }
    }

    static float volumeGain (float v) noexcept
    {
        // 0 -> silence, 0.5 -> 0 dB, 1 -> +9 dB
        const float db = (v - 0.5f) * 18.0f;
        return v < 0.001f ? 0.0f : std::pow (10.0f, db / 20.0f);
    }

    // Lower / upper edge of the swept band, from the two frequency controls.
    // low:  120 Hz .. 960 Hz   (3 octaves)
    // high: 700 Hz .. 3960 Hz  (2.5 octaves)
    static float lowFrequency (float normalised) noexcept
    {
        return 120.0f * std::pow (2.0f, clampf (normalised, 0.0f, 1.0f) * 3.0f);
    }

    static float highFrequency (float normalised) noexcept
    {
        return 700.0f * std::pow (2.0f, clampf (normalised, 0.0f, 1.0f) * 2.5f);
    }

    // Guard band: the upper edge always stays at least a fifth above the lower
    // one, otherwise the band-pass would collapse and the pedal stops sweeping.
    static float highLimit (float lowHz, float highHz) noexcept
    {
        return std::max (highHz, lowHz * 1.5f);
    }

    // Resonance frequency for a pedal position inside the [low, high] window.
    // The sweep is logarithmic with a slight "vocal" compression at the top.
    static float sweepFrequency (float pedal, float lowNormalised, float highNormalised) noexcept
    {
        const float low = lowFrequency (lowNormalised);
        const float high = highLimit (low, highFrequency (highNormalised));
        const float curved = std::pow (clampf (pedal, 0.0f, 1.0f), 0.82f);
        return low * std::pow (high / low, curved);
    }

    // Default-window helper (kept for the tools).
    static float pedalToFrequency (float p) noexcept
    {
        return sweepFrequency (p, 0.55f, 0.66f);
    }

    static float pedalToQ (float resonance, float p) noexcept
    {
        const float base = 1.6f + 7.5f * clampf (resonance, 0.0f, 1.0f);
        return base * (0.92f + 0.28f * p);
    }

private:
    struct ChannelState
    {
        AdaaTanh sat;
        Svf svf;
        OnePole toneLP, inHp, outHp;
        DcBlocker dc;

        void reset() noexcept
        {
            sat.reset();
            svf.reset();
            toneLP.reset();
            inHp.reset();
            outHp.reset();
            dc.reset();
        }
    };

    void updateFilter (float pedal, float resonance, float lowN, float highN) noexcept
    {
        const float f = sweepFrequency (pedal, lowN, highN);
        const float q = pedalToQ (resonance, pedal);
        for (auto& ch : state)
            ch.svf.setCoeffs (f, q, fs);
    }

    void updateTone (float tone) noexcept
    {
        // 2.6 kHz .. 12 kHz
        const float f = 2600.0f * std::pow (12000.0f / 2600.0f, clampf (tone, 0.0f, 1.0f));
        for (auto& ch : state)
            ch.toneLP.setLowPass (f, fs);
    }

    Params params;
    float fs = 44100.0f;
    int numChannels = 2;

    ChannelState state[kMaxChannels];

    Smoother pedalSm, driveSm, resSm, lowSm, highSm, toneSm, volSm, mixSm;
};

} // namespace wah


