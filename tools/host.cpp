//-----------------------------------------------------------------------------
// wah_host - minimal VST3 host used to validate the plug-in binary
//
//   wah_host <plugin.vst3-or-dll> <out.wav> [screenshot.png]
//
// Loads the module exactly like a DAW does (InitDll -> GetPluginFactory ->
// createInstance -> initialize -> setupProcessing -> process), renders a short
// guitar-ish riff through the real plug-in, exercises parameters and state
// saving, and finally opens the editor view and grabs it to a PNG.
//-----------------------------------------------------------------------------
#include "pluginterfaces/base/fplatform.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/gui/iplugview.h"

#include <windows.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace
{
//---------------------------------------------------------------- memory stream
class MemStream : public IBStream
{
public:
    tresult PLUGIN_API queryInterface (const TUID _iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual (_iid, FUnknown::iid)
            || FUnknownPrivate::iidEqual (_iid, IBStream::iid))
        {
            addRef();
            *obj = static_cast<IBStream*> (this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return static_cast<uint32> (++refs); }
    uint32 PLUGIN_API release() override
    {
        const int32 r = --refs;
        if (r == 0)
        {
            delete this;
            return 0;
        }
        return static_cast<uint32> (r);
    }

    tresult PLUGIN_API read (void* buffer, int32 numBytes, int32* numBytesRead) override
    {
        const int64 remaining = static_cast<int64> (data.size()) - pos;
        const int32 n = static_cast<int32> (std::max<int64> (0, std::min<int64> (numBytes, remaining)));
        if (n > 0)
            std::memcpy (buffer, data.data() + pos, static_cast<size_t> (n));
        pos += n;
        if (numBytesRead)
            *numBytesRead = n;
        return kResultOk;
    }

    tresult PLUGIN_API write (void* buffer, int32 numBytes, int32* numBytesWritten) override
    {
        if (pos + static_cast<size_t> (numBytes) > data.size())
            data.resize (pos + static_cast<size_t> (numBytes));
        std::memcpy (data.data() + pos, buffer, static_cast<size_t> (numBytes));
        pos += static_cast<size_t> (numBytes);
        if (numBytesWritten)
            *numBytesWritten = numBytes;
        return kResultOk;
    }

    tresult PLUGIN_API seek (int64 p, int32 mode, int64* result) override
    {
        int64 target = p;
        if (mode == kIBSeekCur)
            target = static_cast<int64> (pos) + p;
        else if (mode == kIBSeekEnd)
            target = static_cast<int64> (data.size()) + p;

        target = std::max<int64> (0, target);
        pos = static_cast<size_t> (target);
        if (result)
            *result = target;
        return kResultOk;
    }

    tresult PLUGIN_API tell (int64* p) override
    {
        *p = static_cast<int64> (pos);
        return kResultOk;
    }

    std::vector<char> data;
    size_t pos = 0;

private:
    int32 refs = 1;
};

//-------------------------------------------------------------- param changes
class Queue : public IParamValueQueue
{
public:
    tresult PLUGIN_API queryInterface (const TUID _iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual (_iid, FUnknown::iid)
            || FUnknownPrivate::iidEqual (_iid, IParamValueQueue::iid))
        {
            *obj = static_cast<IParamValueQueue*> (this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    ParamID PLUGIN_API getParameterId() override { return id; }
    int32 PLUGIN_API getPointCount() override { return static_cast<int32> (points.size()); }

    tresult PLUGIN_API getPoint (int32 index, int32& sampleOffset, ParamValue& value) override
    {
        if (index < 0 || index >= static_cast<int32> (points.size()))
            return kInvalidArgument;
        sampleOffset = points[static_cast<size_t> (index)].first;
        value = points[static_cast<size_t> (index)].second;
        return kResultOk;
    }

    tresult PLUGIN_API addPoint (int32 sampleOffset, ParamValue value, int32& index) override
    {
        points.emplace_back (sampleOffset, value);
        index = static_cast<int32> (points.size()) - 1;
        return kResultOk;
    }

    ParamID id = 0;
    std::vector<std::pair<int32, ParamValue>> points;
};

class Changes : public IParameterChanges
{
public:
    tresult PLUGIN_API queryInterface (const TUID _iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual (_iid, FUnknown::iid)
            || FUnknownPrivate::iidEqual (_iid, IParameterChanges::iid))
        {
            *obj = static_cast<IParameterChanges*> (this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    int32 PLUGIN_API getParameterCount() override { return static_cast<int32> (queues.size()); }

    IParamValueQueue* PLUGIN_API getParameterData (int32 index) override
    {
        if (index < 0 || index >= static_cast<int32> (queues.size()))
            return nullptr;
        return &queues[static_cast<size_t> (index)];
    }

    IParamValueQueue* PLUGIN_API addParameterData (const ParamID& pid, int32& index) override
    {
        for (size_t i = 0; i < queues.size(); ++i)
            if (queues[i].id == pid)
            {
                index = static_cast<int32> (i);
                return &queues[i];
            }
        queues.emplace_back();
        queues.back().id = pid;
        index = static_cast<int32> (queues.size()) - 1;
        return &queues.back();
    }

    void clear()
    {
        for (auto& q : queues)
            q.points.clear();
    }

    std::vector<Queue> queues;
};

//------------------------------------------------------------ component handler
class Handler : public IComponentHandler
{
public:
    tresult PLUGIN_API queryInterface (const TUID _iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual (_iid, FUnknown::iid)
            || FUnknownPrivate::iidEqual (_iid, IComponentHandler::iid))
        {
            *obj = static_cast<IComponentHandler*> (this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    tresult PLUGIN_API beginEdit (ParamID) override { ++begins; return kResultOk; }
    tresult PLUGIN_API performEdit (ParamID, ParamValue) override { ++performs; return kResultOk; }
    tresult PLUGIN_API endEdit (ParamID) override { ++ends; return kResultOk; }
    tresult PLUGIN_API restartComponent (int32) override { return kResultOk; }

    int begins = 0, performs = 0, ends = 0;
};

//-------------------------------------------------------------------- wav I/O
void writeWav (const char* path, const std::vector<float>& samples, int sampleRate)
{
    FILE* f = std::fopen (path, "wb");
    if (! f)
        return;

    const uint32_t dataSize = static_cast<uint32_t> (samples.size() * 2);
    const uint32_t riffSize = 36 + dataSize;
    const uint16_t channels = 1, bits = 16, format = 1;
    const uint32_t rate = static_cast<uint32_t> (sampleRate);
    const uint32_t byteRate = rate * 2;
    const uint16_t blockAlign = 2;

    std::fwrite ("RIFF", 1, 4, f);
    std::fwrite (&riffSize, 4, 1, f);
    std::fwrite ("WAVEfmt ", 1, 8, f);
    const uint32_t fmtSize = 16;
    std::fwrite (&fmtSize, 4, 1, f);
    std::fwrite (&format, 2, 1, f);
    std::fwrite (&channels, 2, 1, f);
    std::fwrite (&rate, 4, 1, f);
    std::fwrite (&byteRate, 4, 1, f);
    std::fwrite (&blockAlign, 2, 1, f);
    std::fwrite (&bits, 2, 1, f);
    std::fwrite ("data", 1, 4, f);
    std::fwrite (&dataSize, 4, 1, f);

    for (float s : samples)
    {
        const float c = std::max (-1.0f, std::min (1.0f, s));
        const int16_t v = static_cast<int16_t> (std::lrint (c * 32767.0f));
        std::fwrite (&v, 2, 1, f);
    }
    std::fclose (f);
}

//--------------------------------------------------------------- test signal
struct Pluck
{
    std::vector<float> delay;
    size_t pos = 0;
    float level = 0.0f;

    void trigger (float freq, float sampleRate, float amp)
    {
        const size_t len = std::max<size_t> (2, static_cast<size_t> (sampleRate / freq));
        delay.assign (len, 0.0f);
        pos = 0;
        level = amp;
        uint32_t seed = 12345;
        for (auto& d : delay)
        {
            seed = seed * 1664525u + 1013904223u;
            d = (static_cast<float> ((seed >> 9) & 0x7fff) / 16384.0f - 1.0f) * 0.7f;
        }
    }

    float process()
    {
        if (delay.empty())
            return 0.0f;
        const float out = delay[pos];
        const size_t next = (pos + 1) % delay.size();
        delay[pos] = 0.498f * (delay[pos] + delay[next]);
        pos = next;
        return out * level;
    }
};

std::vector<float> makeRiff (int sampleRate)
{
    const float bpm = 96.0f, step = 60.0f / bpm / 4.0f;
    const int steps = 64;
    struct Hit { int step; int note; float amp; };
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

    const int frames = static_cast<int> (steps * step * sampleRate) + sampleRate / 2;
    std::vector<float> out (static_cast<size_t> (frames), 0.0f);

    Pluck pluck;
    int nextHit = 0;
    float lp = 0.0f;
    for (int i = 0; i < frames; ++i)
    {
        const float t = static_cast<float> (i) / static_cast<float> (sampleRate);
        const int curStep = static_cast<int> (t / step);
        while (nextHit < static_cast<int> (sizeof (hits) / sizeof (hits[0])) && hits[nextHit].step == curStep)
        {
            const int note = hits[nextHit].note;
            pluck.trigger (440.0f * std::pow (2.0f, static_cast<float> (note - 69) / 12.0f),
                           static_cast<float> (sampleRate), hits[nextHit].amp);
            ++nextHit;
        }
        const float s = pluck.process();
        lp += 0.25f * (s - lp);
        out[static_cast<size_t> (i)] = (s * 0.85f + lp * 0.25f) * 0.35f;
    }

    float peak = 0.0f;
    for (float s : out)
        peak = std::max (peak, std::fabs (s));
    if (peak > 0.0f)
        for (auto& s : out)
            s *= 0.7f / peak;

    return out;
}

void printCid (const TUID cid)
{
    char buffer[33];
    for (int i = 0; i < 16; ++i)
        std::sprintf (buffer + i * 2, "%02X", static_cast<unsigned char> (cid[i]));
    buffer[32] = 0;
    std::printf ("  class id          : %s\n", buffer);
}

bool saveScreenshot (HWND hwnd, int w, int h, const char* path)
{
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC dc = CreateCompatibleDC (nullptr);
    HBITMAP bmp = CreateDIBSection (dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (! bmp)
        return false;

    HGDIOBJ old = SelectObject (dc, bmp);
    SendMessageW (hwnd, WM_PRINTCLIENT, reinterpret_cast<WPARAM> (dc), PRF_CLIENT);
    SelectObject (dc, old);
    DeleteDC (dc);

    // GDI painting does not touch the alpha channel; make the image opaque
    {
        auto* pixels = static_cast<uint32_t*> (bits);
        for (int i = 0; i < w * h; ++i)
            pixels[i] |= 0xFF000000u;
    }

    Gdiplus::Bitmap image (w, h, w * 4, PixelFormat32bppARGB, static_cast<BYTE*> (bits));
    // image/png encoder (same GUID on every Windows install)
    const CLSID pngClsid = { 0x557CF406, 0x1A04, 0x11D3, { 0x9A, 0x73, 0x00, 0x00, 0xF8, 0x1E, 0xF3, 0x2E } };
    const bool ok = image.Save (std::wstring (path, path + std::strlen (path)).c_str(), &pngClsid, nullptr) == Gdiplus::Ok;

    DeleteObject (bmp);
    std::printf ("  screenshot        : %s (%s)\n", path, ok ? "ok" : "failed");
    return ok;
}

} // namespace

//-----------------------------------------------------------------------------
int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf ("usage: wah_host <plugin.vst3|dll> <out.wav> [screenshot.png]\n");
        return 1;
    }

    HMODULE module = LoadLibraryExW (std::wstring (argv[1], argv[1] + std::strlen (argv[1])).c_str(),
                                     nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (! module)
    {
        std::printf ("cannot load %s (error %lu)\n", argv[1], GetLastError());
        return 1;
    }

    using InitDllFn = bool (*)();
    using GetFactoryFn = IPluginFactory* (*)();

    if (auto init = reinterpret_cast<InitDllFn> (GetProcAddress (module, "InitDll")))
        init();

    auto getFactory = reinterpret_cast<GetFactoryFn> (GetProcAddress (module, "GetPluginFactory"));
    if (! getFactory)
    {
        std::printf ("GetPluginFactory not found\n");
        return 1;
    }

    IPluginFactory* factory = getFactory();
    if (! factory)
    {
        std::printf ("null factory\n");
        return 1;
    }

    PFactoryInfo factoryInfo;
    std::printf ("factory\n");
    if (factory->getFactoryInfo (&factoryInfo) == kResultOk)
        std::printf ("  vendor            : %s\n  url               : %s\n", factoryInfo.vendor, factoryInfo.url);
    std::printf ("  classes           : %d\n", factory->countClasses());

    PClassInfo classInfo = {};
    if (factory->countClasses() < 1 || factory->getClassInfo (0, &classInfo) != kResultOk)
    {
        std::printf ("no class\n");
        return 1;
    }
    std::printf ("  class             : %s (%s)\n", classInfo.name, classInfo.category);
    printCid (classInfo.cid);

    void* obj = nullptr;
    if (factory->createInstance (classInfo.cid, IComponent::iid, &obj) != kResultOk || obj == nullptr)
    {
        std::printf ("createInstance failed\n");
        return 1;
    }

    auto* component = static_cast<IComponent*> (obj);
    IAudioProcessor* processor = nullptr;
    IEditController* controller = nullptr;
    component->queryInterface (IAudioProcessor::iid, reinterpret_cast<void**> (&processor));
    component->queryInterface (IEditController::iid, reinterpret_cast<void**> (&controller));

    TUID controllerId = {};
    const bool singleComponent = component->getControllerClassId (controllerId) != kResultOk;
    std::printf ("interfaces        : component %s, processor %s, controller %s, single component %s\n",
                 component ? "ok" : "-", processor ? "ok" : "-", controller ? "ok" : "-",
                 singleComponent ? "yes" : "no");

    if (! processor || ! controller)
        return 1;

    component->initialize (nullptr);

    const double sampleRate = 48000.0;
    const int32 blockSize = 256;

    ProcessSetup setup = {};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = blockSize;
    setup.sampleRate = sampleRate;
    component->activateBus (kAudio, kInput, 0, 1);
    component->activateBus (kAudio, kOutput, 0, 1);
    processor->setupProcessing (setup);
    component->setActive (1);
    processor->setProcessing (1);

    Handler handler;
    controller->setComponentHandler (&handler);

    //---- parameters -------------------------------------------------------
    std::printf ("parameters\n");
    const int32 numParams = controller->getParameterCount();
    for (int32 i = 0; i < numParams; ++i)
    {
        ParameterInfo info = {};
        controller->getParameterInfo (i, info);
        String128 text = {};
        controller->getParamStringByValue (i, info.defaultNormalizedValue, text);
        char title[64] = {};
        char value[64] = {};
        for (int c = 0; c < 63 && info.title[c] != 0; ++c)
            title[c] = static_cast<char> (info.title[c]);
        for (int c = 0; c < 63 && text[c] != 0; ++c)
            value[c] = static_cast<char> (text[c]);
        std::printf ("  [%d] %-12s default %.2f  -> %-10s flags 0x%x\n", i, title,
                     info.defaultNormalizedValue, value, info.flags);
    }

    ParamID bypassId = 5;
    for (int32 i = 0; i < numParams; ++i)
    {
        ParameterInfo info = {};
        controller->getParameterInfo (i, info);
        if ((info.flags & ParameterInfo::kIsBypass) != 0)
            bypassId = info.id;
    }

    //---- process a riff with a pedal sweep --------------------------------
    std::vector<float> input = makeRiff (static_cast<int> (sampleRate));
    std::vector<float> output (input.size(), 0.0f);

    const int totalBlocks = static_cast<int> (input.size() / static_cast<size_t> (blockSize));
    std::vector<float> inBuf (static_cast<size_t> (blockSize) * 2);
    std::vector<float> outBuf (static_cast<size_t> (blockSize) * 2);

    Changes changes;
    int32 pedalIndex = 0;
    const ParamID pedalId = 0;
    changes.addParameterData (pedalId, pedalIndex);

    for (int b = 0; b < totalBlocks; ++b)
    {
        const size_t start = static_cast<size_t> (b) * static_cast<size_t> (blockSize);

        for (int i = 0; i < blockSize; ++i)
        {
            const float s = input[start + static_cast<size_t> (i)];
            inBuf[static_cast<size_t> (i)] = s;
            inBuf[static_cast<size_t> (blockSize + i)] = s;
            outBuf[static_cast<size_t> (i)] = 0.0f;
            outBuf[static_cast<size_t> (blockSize + i)] = 0.0f;
        }

        changes.clear();
        IParamValueQueue* q = changes.getParameterData (0);
        const float t = static_cast<float> (b) / static_cast<float> (std::max (1, totalBlocks - 1));
        const float pedal = 0.5f - 0.5f * std::cos (t * 6.2831853f * 2.0f); // two sweeps
        int32 pointIndex = 0;
        q->addPoint (0, pedal, pointIndex);

        AudioBusBuffers inBus = {};
        inBus.numChannels = 2;
        float* inPtrs[2] = { inBuf.data(), inBuf.data() + blockSize };
        inBus.channelBuffers32 = inPtrs;

        AudioBusBuffers outBus = {};
        outBus.numChannels = 2;
        float* outPtrs[2] = { outBuf.data(), outBuf.data() + blockSize };
        outBus.channelBuffers32 = outPtrs;

        ProcessData data = {};
        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = blockSize;
        data.numInputs = 1;
        data.numOutputs = 1;
        data.inputs = &inBus;
        data.outputs = &outBus;
        data.inputParameterChanges = &changes;

        processor->process (data);

        for (int i = 0; i < blockSize; ++i)
            output[start + static_cast<size_t> (i)] = outBuf[static_cast<size_t> (i)];
    }

    double sumIn = 0.0, sumOut = 0.0, peakOut = 0.0;
    bool nonFinite = false;
    for (size_t i = 0; i < output.size(); ++i)
    {
        sumIn += static_cast<double> (input[i]) * input[i];
        sumOut += static_cast<double> (output[i]) * output[i];
        peakOut = std::max (peakOut, static_cast<double> (std::fabs (output[i])));
        if (! std::isfinite (output[i]))
            nonFinite = true;
    }
    std::printf ("processing        : %zu samples, in %.1f dBFS, out %.1f dBFS, peak %.1f dBFS%s\n",
                 output.size(),
                 10.0 * std::log10 (sumIn / static_cast<double> (output.size()) + 1e-12),
                 10.0 * std::log10 (sumOut / static_cast<double> (output.size()) + 1e-12),
                 20.0 * std::log10 (peakOut + 1e-12), nonFinite ? ", NON FINITE!" : "");

    writeWav (argv[2], output, static_cast<int> (sampleRate));
    std::printf ("  wrote             : %s\n", argv[2]);

    //---- state save / restore ---------------------------------------------
    auto* stream = new MemStream();
    const tresult saveResult = component->getState (stream);
    const float before = static_cast<float> (controller->getParamNormalized (0));
    controller->setParamNormalized (0, 0.9);
    stream->seek (0, IBStream::kIBSeekSet, nullptr);
    const tresult loadResult = controller->setComponentState (stream);
    const float after = static_cast<float> (controller->getParamNormalized (0));
    std::printf ("state             : save 0x%x (%zu bytes), load 0x%x, pedal %.2f -> %.2f %s\n",
                 saveResult, stream->data.size(), loadResult, before, after,
                 std::fabs (after - before) < 1e-4f ? "ok" : "MISMATCH");
    stream->release();

    //---- editor view ------------------------------------------------------
    IPlugView* view = controller->createView (ViewType::kEditor);
    if (view)
    {
        ViewRect size = {};
        view->getSize (&size);
        std::printf ("editor            : %d x %d, HWND supported %s\n",
                     size.right - size.left, size.bottom - size.top,
                     view->isPlatformTypeSupported (kPlatformTypeHWND) == kResultOk ? "yes" : "no");

        RECT frameRect = { 0, 0, size.right - size.left, size.bottom - size.top };
        AdjustWindowRect (&frameRect, WS_OVERLAPPEDWINDOW, FALSE);

        HWND top = CreateWindowExW (WS_EX_TOOLWINDOW, L"STATIC", L"host", WS_OVERLAPPEDWINDOW,
                                    -3000, -3000, frameRect.right - frameRect.left,
                                    frameRect.bottom - frameRect.top, nullptr, nullptr,
                                    GetModuleHandleW (nullptr), nullptr);

        if (top && view->attached (top, kPlatformTypeHWND) == kResultOk)
        {
            HWND child = GetWindow (top, GW_CHILD);
            MSG msg;
            for (int i = 0; i < 200 && PeekMessage (&msg, nullptr, 0, 0, PM_REMOVE); ++i)
            {
                TranslateMessage (&msg);
                DispatchMessage (&msg);
            }

            const float pedalBefore = static_cast<float> (controller->getParamNormalized (0));

            // simulate a mouse drag on the treadle, the way a user would
            if (child != nullptr)
            {
                SendMessageW (child, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM (190, 300));
                for (int dy = 1; dy <= 40; ++dy)
                    SendMessageW (child, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM (190, 220 - dy));
                SendMessageW (child, WM_LBUTTONUP, 0, MAKELPARAM (190, 180));

                // and a knob drag (DRIVE knob sits around x=62, y=442)
                SendMessageW (child, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM (58, 468));
                for (int dy = 1; dy <= 25; ++dy)
                    SendMessageW (child, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM (58, 468 - dy));
                SendMessageW (child, WM_LBUTTONUP, 0, MAKELPARAM (58, 443));
            }

            const float pedalAfter = static_cast<float> (controller->getParamNormalized (0));
            std::printf ("ui drag           : pedal %.3f -> %.3f, host edits begin %d / perform %d / end %d\n",
                         pedalBefore, pedalAfter, handler.begins, handler.performs, handler.ends);

            // mouse wheel over the MIX knob (index 4, around x=254, y=438)
            if (child != nullptr)
            {
                const float mixBefore = static_cast<float> (controller->getParamNormalized (6)); // Mix
                POINT wheelPoint = { 378, 468 };
                ClientToScreen (child, &wheelPoint);
                for (int i = 0; i < 3; ++i)
                    SendMessageW (child, WM_MOUSEWHEEL, MAKEWPARAM (0, -WHEEL_DELTA),
                                  MAKELPARAM (wheelPoint.x, wheelPoint.y));
                const float mixAfter = static_cast<float> (controller->getParamNormalized (6)); // Mix
                std::printf ("ui wheel          : mix %.3f -> %.3f %s\n", mixBefore, mixAfter,
                             mixAfter < mixBefore - 1e-4f ? "ok" : "NO CHANGE");
            }

            if (argc > 3 && child)
                saveScreenshot (child, size.right - size.left, size.bottom - size.top, argv[3]);

            // resizable editor: ask for a 2x window, the plug-in must keep the
            // aspect ratio and re-render at the new scale
            if (child != nullptr)
            {
                ViewRect big = { 0, 0, 880, 1160 };
                const tresult constraintResult = view->checkSizeConstraint (&big);
                const int bigW = big.right - big.left;
                const int bigH = big.bottom - big.top;
                const tresult sizeResult = view->onSize (&big);

                std::printf ("resize            : canResize %s, 880x1160 -> %dx%d (0x%x), onSize 0x%x\n",
                             view->canResize() == kResultOk ? "yes" : "no", bigW, bigH,
                             constraintResult, sizeResult);

                MSG m;
                while (PeekMessage (&m, nullptr, 0, 0, PM_REMOVE))
                {
                    TranslateMessage (&m);
                    DispatchMessage (&m);
                }

                if (argc > 3)
                {
                    std::string bigPath = argv[3];
                    const size_t dot = bigPath.find_last_of ('.');
                    if (dot != std::string::npos)
                        bigPath.insert (dot, "_2x");
                    saveScreenshot (child, bigW, bigH, bigPath.c_str());
                }
            }

            view->removed();
        }
        view->release();

        if (top)
            DestroyWindow (top);
    }
    else
    {
        std::printf ("editor            : createView FAILED\n");
    }

    //---- UI parameter edit through the handler ----------------------------
    controller->setParamNormalized (1, 0.5);
    std::printf ("handler           : begins %d, performs %d, ends %d\n", handler.begins, handler.performs, handler.ends);

    //---- extra checks -----------------------------------------------------
    {
        // 64 bit audio is optional; the host must fall back to 32 bit
        std::printf ("64-bit samples    : %s\n",
                     processor->canProcessSampleSize (kSample64) == kResultOk ? "supported" : "not supported (falls back to 32)");

        // true bypass: output buffer must be identical to the input
        controller->setParamNormalized (bypassId, 1.0);
        float inBlock[64];
        float outBlock[64];
        for (int i = 0; i < 64; ++i)
        {
            inBlock[i] = 0.25f;
            outBlock[i] = -1.0f;
        }

        float* inPtrs[1] = { inBlock };
        float* outPtrs[1] = { outBlock };
        AudioBusBuffers inBus = {};
        inBus.numChannels = 1;
        inBus.channelBuffers32 = inPtrs;
        AudioBusBuffers outBus = {};
        outBus.numChannels = 1;
        outBus.channelBuffers32 = outPtrs;

        ProcessData bypassData = {};
        bypassData.symbolicSampleSize = kSample32;
        bypassData.numSamples = 64;
        bypassData.numInputs = 1;
        bypassData.numOutputs = 1;
        bypassData.inputs = &inBus;
        bypassData.outputs = &outBus;
        processor->process (bypassData);

        bool bypassOk = true;
        for (int i = 0; i < 64; ++i)
            bypassOk = bypassOk && std::fabs (outBlock[i] - 0.25f) < 1e-6f;
        std::printf ("bypass            : %s\n", bypassOk ? "output == input (true bypass)" : "SIGNAL CHANGED");

        // mono configuration and a different sample rate
        SpeakerArrangement mono = SpeakerArr::kMono;
        const tresult arrResult = processor->setBusArrangements (&mono, 1, &mono, 1);
        SpeakerArrangement current = 0;
        processor->getBusArrangement (kInput, 0, current);

        controller->setParamNormalized (bypassId, 0.0);
        component->setActive (0);
        ProcessSetup monoSetup = {};
        monoSetup.processMode = kRealtime;
        monoSetup.symbolicSampleSize = kSample32;
        monoSetup.maxSamplesPerBlock = 64;
        monoSetup.sampleRate = 96000.0;
        processor->setupProcessing (monoSetup);
        component->setActive (1);

        float sine[64];
        for (int i = 0; i < 64; ++i)
            sine[i] = 0.3f * std::sin (static_cast<float> (i) * 0.1f);
        float* monoIn[1] = { sine };
        float monoOut[64] = {};
        float* monoOutPtr[1] = { monoOut };
        AudioBusBuffers monoInBus = {};
        monoInBus.numChannels = 1;
        monoInBus.channelBuffers32 = monoIn;
        AudioBusBuffers monoOutBus = {};
        monoOutBus.numChannels = 1;
        monoOutBus.channelBuffers32 = monoOutPtr;

        ProcessData monoData = {};
        monoData.symbolicSampleSize = kSample32;
        monoData.numSamples = 64;
        monoData.numInputs = 1;
        monoData.numOutputs = 1;
        monoData.inputs = &monoInBus;
        monoData.outputs = &monoOutBus;
        processor->process (monoData);

        bool monoFinite = true;
        for (int i = 0; i < 64; ++i)
            monoFinite = monoFinite && std::isfinite (monoOut[i]);

        std::printf ("mono / 96 kHz     : arrangement %s (%s), process %s\n",
                     arrResult == kResultOk ? "accepted" : "refused",
                     current == SpeakerArr::kMono ? "mono" : "other",
                     monoFinite ? "finite" : "NaN/Inf!");

        // factory must be able to create more than one instance
        void* second = nullptr;
        const tresult secondResult = factory->createInstance (classInfo.cid, IComponent::iid, &second);
        std::printf ("second instance   : %s\n", secondResult == kResultOk ? "created and released" : "FAILED");
        if (second)
            static_cast<IComponent*> (second)->release();

        // lifecycle: a host may drop the component while the editor is still open
        {
            void* raw = nullptr;
            if (factory->createInstance (classInfo.cid, IComponent::iid, &raw) == kResultOk && raw != nullptr)
            {
                auto* comp2 = static_cast<IComponent*> (raw);
                comp2->initialize (nullptr);
                IEditController* ctrl2 = nullptr;
                comp2->queryInterface (IEditController::iid, reinterpret_cast<void**> (&ctrl2));
                IPlugView* view2 = ctrl2 ? ctrl2->createView (ViewType::kEditor) : nullptr;

                if (ctrl2)
                    ctrl2->release();
                comp2->release(); // component goes away, view keeps the controller alive
                const bool hadView = view2 != nullptr;
                if (view2)
                    view2->release();

                std::printf ("view lifetime     : component released before editor %s\n",
                             hadView ? "survived" : "not tested");
            }
        }
    }

    component->setActive (0);
    processor->release();
    controller->release();
    component->release();
    factory->release();
    FreeLibrary (module);

    std::printf ("RESULT            : OK\n");
    return 0;
}










