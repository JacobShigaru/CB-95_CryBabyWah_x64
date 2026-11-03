//-----------------------------------------------------------------------------
// CryBabyWah - parameter identifiers shared by DSP, plug-in and GUI
//-----------------------------------------------------------------------------
#pragma once

namespace crybaby
{

enum ParameterId
{
    kPedal = 0,   // wah position, 0 = heel, 1 = toe
    kDrive,       // input stage gain / saturation
    kResonance,   // tank Q
    kLow,         // lower edge of the swept frequency range
    kHigh,        // upper edge of the swept frequency range
    kTone,        // output low-pass
    kMix,         // dry / wet balance (1 = fully processed)
    kVolume,      // output level
    kBypass,      // 0 = effect on, 1 = bypass
    kNumParameters
};

inline const char* parameterName (int index)
{
    switch (index)
    {
        case kPedal:     return "Wah";
        case kDrive:     return "Drive";
        case kResonance: return "Resonance";
        case kLow:       return "Low Freq";
        case kHigh:      return "High Freq";
        case kTone:      return "Tone";
        case kMix:       return "Mix";
        case kVolume:    return "Volume";
        case kBypass:    return "Bypass";
        default:         return "";
    }
}

inline const char* parameterShortName (int index)
{
    switch (index)
    {
        case kResonance: return "Reso";
        case kLow:       return "Low";
        case kHigh:      return "High";
        case kVolume:    return "Vol";
        default:         return parameterName (index);
    }
}

inline const char* parameterUnit (int index)
{
    switch (index)
    {
        case kLow:
        case kHigh:   return "Hz";
        case kVolume: return "dB";
        case kMix:    return "%";
        default:      return "";
    }
}

inline float parameterDefault (int index)
{
    switch (index)
    {
        case kPedal:     return 0.45f;
        case kDrive:     return 0.32f;
        case kResonance: return 0.55f;
        case kLow:       return 0.55f;   // ~375 Hz
        case kHigh:      return 0.66f;   // ~2200 Hz
        case kTone:      return 0.55f;
        case kMix:       return 1.00f;
        case kVolume:    return 0.50f;
        case kBypass:    return 0.0f;
        default:         return 0.0f;
    }
}

} // namespace crybaby
