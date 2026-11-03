//-----------------------------------------------------------------------------
// CryBabyWah - VST3 plug-in implementation
//-----------------------------------------------------------------------------
#include "WahPlugin.h"

#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "pluginterfaces/vst/ivstunits.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cwchar>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace crybaby
{

namespace
{
constexpr int32 kStateVersion = 3;

inline float clamp01 (float v) noexcept
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

void copyWide (TChar* dst, const wchar_t* src)
{
    int i = 0;
    for (; src[i] != 0 && i < 127; ++i)
        dst[i] = static_cast<TChar> (src[i]);
    for (; i < 128; ++i)
        dst[i] = 0;
}

void formatParamString (int index, float value, float lowN, float highN, wchar_t* buffer, int size)
{
    switch (index)
    {
        case kPedal:
        {
            const float hz = wah::Engine::sweepFrequency (value, lowN, highN);
            if (hz >= 1000.0f)
                std::swprintf (buffer, size, L"%.2f kHz", hz / 1000.0f);
            else
                std::swprintf (buffer, size, L"%.0f Hz", hz);
            break;
        }
        case kLow:
        {
            const float hz = wah::Engine::lowFrequency (value);
            std::swprintf (buffer, size, hz >= 1000.0f ? L"%.2f kHz" : L"%.0f Hz", hz >= 1000.0f ? hz / 1000.0f : hz);
            break;
        }
        case kHigh:
        {
            const float hz = wah::Engine::highFrequency (value);
            std::swprintf (buffer, size, hz >= 1000.0f ? L"%.2f kHz" : L"%.0f Hz", hz >= 1000.0f ? hz / 1000.0f : hz);
            break;
        }
        case kMix:
            std::swprintf (buffer, size, L"%.0f %% wet", value * 100.0f);
            break;
        case kVolume:
            std::swprintf (buffer, size, L"%+.1f dB", (value - 0.5f) * 18.0f);
            break;
        case kBypass:
            std::swprintf (buffer, size, L"%s", value > 0.5f ? L"Bypass" : L"On");
            break;
        default:
            std::swprintf (buffer, size, L"%.0f %%", value * 100.0f);
            break;
    }
}
} // namespace

void copyStringN (TChar* dst, const char* src, int maxChars)
{
    int i = 0;
    for (; src[i] != 0 && i < maxChars - 1; ++i)
        dst[i] = static_cast<TChar> (static_cast<unsigned char> (src[i]));
    for (; i < maxChars; ++i)
        dst[i] = 0;
}

void copyString (TChar* dst, const char* src)
{
    copyStringN (dst, src, 128);
}

bool tuidEqual (const char* a, const char* b)
{
    return std::memcmp (a, b, 16) == 0;
}

//-----------------------------------------------------------------------------
WahPlugin::WahPlugin()
{
    for (int i = 0; i < kNumParameters; ++i)
        values[i].store (parameterDefault (i), std::memory_order_relaxed);

    wah::Params initial;
    initial.pedal = values[kPedal].load();
    initial.drive = values[kDrive].load();
    initial.resonance = values[kResonance].load();
    initial.low = values[kLow].load();
    initial.high = values[kHigh].load();
    initial.tone = values[kTone].load();
    initial.mix = values[kMix].load();
    initial.volume = values[kVolume].load();
    initial.bypass = values[kBypass].load() > 0.5f;

    engine.setParams (initial);
    engine.prepare (currentSampleRate, maxBlockSize, busChannels);
}

WahPlugin::~WahPlugin() = default;

//--- FUnknown -----------------------------------------------------------------
tresult PLUGIN_API WahPlugin::queryInterface (const TUID _iid, void** obj)
{
    if (obj == nullptr)
        return kInvalidArgument;

    *obj = nullptr;

    if (FUnknownPrivate::iidEqual (_iid, FUnknown::iid)
        || FUnknownPrivate::iidEqual (_iid, IComponent::iid))
    {
        addRef();
        *obj = static_cast<IComponent*> (this);
        return kResultOk;
    }

    if (FUnknownPrivate::iidEqual (_iid, IAudioProcessor::iid))
    {
        addRef();
        *obj = static_cast<IAudioProcessor*> (this);
        return kResultOk;
    }

    if (FUnknownPrivate::iidEqual (_iid, IEditController::iid))
    {
        addRef();
        *obj = static_cast<IEditController*> (this);
        return kResultOk;
    }

    return kNoInterface;
}

uint32 PLUGIN_API WahPlugin::addRef()
{
    return static_cast<uint32> (++refCount);
}

uint32 PLUGIN_API WahPlugin::release()
{
    const int32 count = --refCount;
    if (count == 0)
    {
        delete this;
        return 0;
    }
    return static_cast<uint32> (count);
}

//--- IPluginBase ---------------------------------------------------------------
tresult PLUGIN_API WahPlugin::initialize (FUnknown* /*context*/)
{
    return kResultOk;
}

tresult PLUGIN_API WahPlugin::terminate()
{
    return kResultOk;
}

//--- IComponent ----------------------------------------------------------------
tresult PLUGIN_API WahPlugin::getControllerClassId (TUID /*classId*/)
{
    // single component effect: the component is its own controller
    return kResultFalse;
}

tresult PLUGIN_API WahPlugin::setIoMode (IoMode /*mode*/)
{
    return kResultOk;
}

int32 PLUGIN_API WahPlugin::getBusCount (MediaType type, BusDirection dir)
{
    (void) dir;
    return type == kAudio ? 1 : 0;
}

tresult PLUGIN_API WahPlugin::getBusInfo (MediaType type, BusDirection dir, int32 index, BusInfo& bus)
{
    if (type != kAudio || index != 0)
        return kInvalidArgument;

    bus.mediaType = kAudio;
    bus.direction = dir;
    bus.channelCount = busChannels;
    copyString (bus.name, dir == kInput ? "Input" : "Output");
    bus.busType = kMain;
    bus.flags = BusInfo::kDefaultActive;

    return kResultOk;
}

tresult PLUGIN_API WahPlugin::getRoutingInfo (RoutingInfo& /*inInfo*/, RoutingInfo& /*outInfo*/)
{
    return kResultFalse;
}

tresult PLUGIN_API WahPlugin::activateBus (MediaType type, BusDirection dir, int32 index, TBool state)
{
    (void) state;
    if (type != kAudio || index != 0)
        return kInvalidArgument;
    (void) dir;
    return kResultOk;
}

tresult PLUGIN_API WahPlugin::setActive (TBool state)
{
    active = state != 0;
    if (active)
        engine.reset();
    return kResultOk;
}

tresult PLUGIN_API WahPlugin::setState (IBStream* state)
{
    if (state == nullptr)
        return kInvalidArgument;
    readState (state);
    return kResultOk;
}

tresult PLUGIN_API WahPlugin::getState (IBStream* state)
{
    if (state == nullptr)
        return kInvalidArgument;
    writeState (state);
    return kResultOk;
}

//--- IAudioProcessor -----------------------------------------------------------
tresult PLUGIN_API WahPlugin::setBusArrangements (SpeakerArrangement* inputs, int32 numIns,
                                                  SpeakerArrangement* outputs, int32 numOuts)
{
    if (numIns != 1 || numOuts != 1 || inputs == nullptr || outputs == nullptr)
        return kResultFalse;

    if (inputs[0] != outputs[0])
        return kResultFalse;

    if (inputs[0] == SpeakerArr::kMono)
    {
        busChannels = 1;
        return kResultTrue;
    }

    if (inputs[0] == SpeakerArr::kStereo)
    {
        busChannels = 2;
        return kResultTrue;
    }

    return kResultFalse;
}

tresult PLUGIN_API WahPlugin::getBusArrangement (BusDirection /*dir*/, int32 index, SpeakerArrangement& arr)
{
    if (index != 0)
        return kInvalidArgument;

    arr = (busChannels == 1) ? SpeakerArr::kMono : SpeakerArr::kStereo;
    return kResultOk;
}

tresult PLUGIN_API WahPlugin::canProcessSampleSize (int32 symbolicSampleSize)
{
    return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
}

uint32 PLUGIN_API WahPlugin::getLatencySamples()
{
    return 0;
}

tresult PLUGIN_API WahPlugin::setupProcessing (ProcessSetup& setup)
{
    currentSampleRate = setup.sampleRate > 0.0 ? setup.sampleRate : 44100.0;
    maxBlockSize = setup.maxSamplesPerBlock > 0 ? setup.maxSamplesPerBlock : 512;
    engine.prepare (currentSampleRate, maxBlockSize, busChannels);
    return kResultOk;
}

tresult PLUGIN_API WahPlugin::setProcessing (TBool /*state*/)
{
    return kResultOk;
}

uint32 PLUGIN_API WahPlugin::getTailSamples()
{
    return kNoTail;
}

tresult PLUGIN_API WahPlugin::process (ProcessData& data)
{
    if (data.symbolicSampleSize != kSample32 || data.numSamples <= 0)
        return kResultOk;

    // host automation / UI gestures arrive here
    if (data.inputParameterChanges != nullptr)
    {
        const int32 count = data.inputParameterChanges->getParameterCount();
        for (int32 i = 0; i < count; ++i)
        {
            IParamValueQueue* queue = data.inputParameterChanges->getParameterData (i);
            if (queue == nullptr)
                continue;

            const ParamID id = queue->getParameterId();
            const int32 points = queue->getPointCount();
            if (id >= static_cast<ParamID> (kNumParameters) || points <= 0)
                continue;

            int32 sampleOffset = 0;
            ParamValue value = 0.0;
            if (queue->getPoint (points - 1, sampleOffset, value) == kResultOk)
                values[id].store (static_cast<float> (value), std::memory_order_relaxed);
        }
    }

    if (data.numInputs == 0 || data.numOutputs == 0 || data.inputs == nullptr || data.outputs == nullptr)
        return kResultOk;

    AudioBusBuffers& in = data.inputs[0];
    AudioBusBuffers& out = data.outputs[0];

    if (in.numChannels <= 0 || out.numChannels <= 0 || in.channelBuffers32 == nullptr
        || out.channelBuffers32 == nullptr)
        return kResultOk;

    const int nch = std::min (std::min (static_cast<int> (in.numChannels),
                                        static_cast<int> (out.numChannels)),
                              wah::Engine::kMaxChannels);

    float* ptrs[wah::Engine::kMaxChannels] = { nullptr, nullptr };

    for (int ch = 0; ch < nch; ++ch)
    {
        if (out.channelBuffers32[ch] == nullptr)
            return kResultOk;

        if (in.channelBuffers32[ch] != nullptr && in.channelBuffers32[ch] != out.channelBuffers32[ch])
            std::memcpy (out.channelBuffers32[ch], in.channelBuffers32[ch],
                         sizeof (float) * static_cast<size_t> (data.numSamples));

        ptrs[ch] = out.channelBuffers32[ch];
    }

    wah::Params p;
    p.pedal = values[kPedal].load (std::memory_order_relaxed);
    p.drive = values[kDrive].load (std::memory_order_relaxed);
    p.resonance = values[kResonance].load (std::memory_order_relaxed);
    p.low = values[kLow].load (std::memory_order_relaxed);
    p.high = values[kHigh].load (std::memory_order_relaxed);
    p.tone = values[kTone].load (std::memory_order_relaxed);
    p.mix = values[kMix].load (std::memory_order_relaxed);
    p.volume = values[kVolume].load (std::memory_order_relaxed);
    p.bypass = values[kBypass].load (std::memory_order_relaxed) > 0.5f;

    engine.setParams (p);
    engine.process (ptrs, nch, data.numSamples);

    return kResultOk;
}

//--- IEditController -----------------------------------------------------------
tresult PLUGIN_API WahPlugin::setComponentState (IBStream* state)
{
    if (state == nullptr)
        return kInvalidArgument;
    readState (state);
    return kResultOk;
}

int32 PLUGIN_API WahPlugin::getParameterCount()
{
    return kNumParameters;
}

tresult PLUGIN_API WahPlugin::getParameterInfo (int32 paramIndex, ParameterInfo& info)
{
    if (paramIndex < 0 || paramIndex >= kNumParameters)
        return kInvalidArgument;

    std::memset (&info, 0, sizeof (info));

    info.id = static_cast<ParamID> (paramIndex);
    copyString (info.title, parameterName (paramIndex));
    copyString (info.shortTitle, parameterName (paramIndex));
    copyString (info.units, parameterUnit (paramIndex));
    info.stepCount = (paramIndex == kBypass) ? 1 : 0;
    info.defaultNormalizedValue = parameterDefault (paramIndex);
    info.unitId = kRootUnitId;
    info.flags = ParameterInfo::kCanAutomate;
    if (paramIndex == kBypass)
        info.flags |= ParameterInfo::kIsBypass;

    return kResultOk;
}

tresult PLUGIN_API WahPlugin::getParamStringByValue (ParamID id, ParamValue valueNormalized, String128 string)
{
    if (id >= static_cast<ParamID> (kNumParameters) || string == nullptr)
        return kInvalidArgument;

    wchar_t buffer[128];
    formatParamString (static_cast<int> (id), static_cast<float> (valueNormalized),
                       values[kLow].load (std::memory_order_relaxed),
                       values[kHigh].load (std::memory_order_relaxed), buffer, 128);
    copyWide (string, buffer);
    return kResultOk;
}

tresult PLUGIN_API WahPlugin::getParamValueByString (ParamID id, TChar* string, ParamValue& valueNormalized)
{
    if (id >= static_cast<ParamID> (kNumParameters) || string == nullptr)
        return kInvalidArgument;

    wchar_t buffer[128];
    int i = 0;
    for (; string[i] != 0 && i < 127; ++i)
        buffer[i] = static_cast<wchar_t> (string[i]);
    buffer[i] = 0;

    if (id == kBypass)
    {
        const bool on = (std::wcscmp (buffer, L"Bypass") != 0);
        valueNormalized = on ? 0.0 : 1.0;
        return kResultOk;
    }

    wchar_t* end = nullptr;
    const double v = std::wcstod (buffer, &end);
    if (end == buffer)
        return kResultFalse;

    if (id == kVolume)
        valueNormalized = clamp01 (static_cast<float> (v / 18.0 + 0.5));
    else if (id == kLow)
    {
        // Hz -> normalised, inverse of lowFrequency(): 120 Hz * 2^(n*3)
        const float hz = static_cast<float> (v);
        valueNormalized = clamp01 (std::log2 (hz / 120.0f) / 3.0f);
    }
    else if (id == kHigh)
    {
        // Hz -> normalised, inverse of highFrequency(): 700 Hz * 2^(n*2.5)
        const float hz = static_cast<float> (v);
        valueNormalized = clamp01 (std::log2 (hz / 700.0f) / 2.5f);
    }
    else if (id == kPedal)
        valueNormalized = clamp01 (static_cast<float> (v)); // pedal position, 0..1
    else
        valueNormalized = clamp01 (static_cast<float> (v / 100.0));

    return kResultOk;
}

ParamValue PLUGIN_API WahPlugin::normalizedParamToPlain (ParamID /*id*/, ParamValue valueNormalized)
{
    return valueNormalized;
}

ParamValue PLUGIN_API WahPlugin::plainParamToNormalized (ParamID /*id*/, ParamValue plainValue)
{
    return clamp01 (static_cast<float> (plainValue));
}

ParamValue PLUGIN_API WahPlugin::getParamNormalized (ParamID id)
{
    if (id >= static_cast<ParamID> (kNumParameters))
        return 0.0;
    return values[id].load (std::memory_order_relaxed);
}

tresult PLUGIN_API WahPlugin::setParamNormalized (ParamID id, ParamValue value)
{
    if (id >= static_cast<ParamID> (kNumParameters))
        return kInvalidArgument;

    values[id].store (clamp01 (static_cast<float> (value)), std::memory_order_relaxed);
    return kResultOk;
}

void WahPlugin::setParamFromUI (int index, float value)
{
    if (index < 0 || index >= kNumParameters)
        return;

    const float v = clamp01 (value);
    values[index].store (v, std::memory_order_relaxed);

    if (componentHandler != nullptr)
        componentHandler->performEdit (static_cast<ParamID> (index), v);
}

void WahPlugin::beginEdit (int index)
{
    if (componentHandler != nullptr && index >= 0 && index < kNumParameters)
        componentHandler->beginEdit (static_cast<ParamID> (index));
}

void WahPlugin::endEdit (int index)
{
    if (componentHandler != nullptr && index >= 0 && index < kNumParameters)
        componentHandler->endEdit (static_cast<ParamID> (index));
}

tresult PLUGIN_API WahPlugin::setComponentHandler (IComponentHandler* handler)
{
    componentHandler = handler;
    return kResultOk;
}

IPlugView* PLUGIN_API WahPlugin::createView (FIDString name)
{
    if (name != nullptr && std::strcmp (name, ViewType::kEditor) != 0)
        return nullptr;

    IPlugView* view = createPedalView (this);
    if (view != nullptr)
        currentView.store (view);
    return view;
}

void WahPlugin::viewDestroyed (IPlugView* view)
{
    IPlugView* expected = view;
    currentView.compare_exchange_strong (expected, nullptr);
}

//--- state ---------------------------------------------------------------------
void WahPlugin::writeState (IBStream* state)
{
    int32 version = kStateVersion;
    state->write (&version, sizeof (version), nullptr);

    float p[kNumParameters];
    for (int i = 0; i < kNumParameters; ++i)
        p[i] = values[i].load (std::memory_order_relaxed);

    state->write (p, sizeof (p), nullptr);
}

void WahPlugin::readState (IBStream* state)
{
    int32 version = 0;
    int32 read = 0;

    if (state->read (&version, sizeof (version), &read) != kResultOk || read != sizeof (version))
        return;

    if (version == 1)
    {
        // version 1 had no Mix parameter: pedal, drive, resonance, tone, volume, bypass
        float old[6] = {};
        if (state->read (old, sizeof (old), &read) != kResultOk || read != static_cast<int32> (sizeof (old)))
            return;

        values[kPedal].store (clamp01 (old[0]), std::memory_order_relaxed);
        values[kDrive].store (clamp01 (old[1]), std::memory_order_relaxed);
        values[kResonance].store (clamp01 (old[2]), std::memory_order_relaxed);
        values[kLow].store (parameterDefault (kLow), std::memory_order_relaxed);
        values[kHigh].store (parameterDefault (kHigh), std::memory_order_relaxed);
        values[kTone].store (clamp01 (old[3]), std::memory_order_relaxed);
        values[kVolume].store (clamp01 (old[4]), std::memory_order_relaxed);
        values[kBypass].store (clamp01 (old[5]), std::memory_order_relaxed);
        values[kMix].store (1.0f, std::memory_order_relaxed);
        return;
    }

    if (version == 2)
    {
        // version 2: pedal, drive, resonance, tone, mix, volume, bypass (no sweep range)
        float old[7] = {};
        if (state->read (old, sizeof (old), &read) != kResultOk || read != static_cast<int32> (sizeof (old)))
            return;

        values[kPedal].store (clamp01 (old[0]), std::memory_order_relaxed);
        values[kDrive].store (clamp01 (old[1]), std::memory_order_relaxed);
        values[kResonance].store (clamp01 (old[2]), std::memory_order_relaxed);
        values[kLow].store (parameterDefault (kLow), std::memory_order_relaxed);
        values[kHigh].store (parameterDefault (kHigh), std::memory_order_relaxed);
        values[kTone].store (clamp01 (old[3]), std::memory_order_relaxed);
        values[kMix].store (clamp01 (old[4]), std::memory_order_relaxed);
        values[kVolume].store (clamp01 (old[5]), std::memory_order_relaxed);
        values[kBypass].store (clamp01 (old[6]), std::memory_order_relaxed);
        return;
    }

    if (version != kStateVersion)
        return;

    float p[kNumParameters] = {};
    if (state->read (p, sizeof (p), &read) != kResultOk || read != static_cast<int32> (sizeof (p)))
        return;

    for (int i = 0; i < kNumParameters; ++i)
        values[i].store (clamp01 (p[i]), std::memory_order_relaxed);
}

} // namespace crybaby


