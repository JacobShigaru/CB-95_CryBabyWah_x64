//-----------------------------------------------------------------------------
// CryBabyWah - VST3 plug-in (single component effect)
//
// The processor and the edit controller are the same object: a "single
// component effect", which is a legal VST3 configuration (IComponent::
// getControllerClassId returns kResultFalse and the host uses the component as
// controller).  This keeps parameter handling trivial and avoids a second
// object plus message plumbing.
//-----------------------------------------------------------------------------
#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include "../dsp/WahCore.h"
#include "Parameters.h"

#include <atomic>

namespace Steinberg
{
class IPlugView;
}

namespace crybaby
{

// Class identifier of the plug-in (bytes of the TUID, big endian words).
static const Steinberg::FUID kProcessorUID (0x9A6C1B2E, 0x4F3D4C8A, 0x9E712B55, 0x183D77A1);
static const Steinberg::FUID kControllerUID (0x9A6C1B2E, 0x4F3D4C8A, 0x9E712B55, 0x183D77A2);

class WahPlugin;

// Implemented in WahView.cpp
Steinberg::IPlugView* createPedalView (WahPlugin* controller);

class WahPlugin : public Steinberg::Vst::IComponent,
                  public Steinberg::Vst::IAudioProcessor,
                  public Steinberg::Vst::IEditController
{
public:
    WahPlugin();
    ~WahPlugin();

    //--- FUnknown ---------------------------------------------------------
    Steinberg::tresult PLUGIN_API queryInterface (const Steinberg::TUID _iid, void** obj) override;
    Steinberg::uint32 PLUGIN_API addRef() override;
    Steinberg::uint32 PLUGIN_API release() override;

    //--- IPluginBase ------------------------------------------------------
    Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;

    //--- IComponent -------------------------------------------------------
    Steinberg::tresult PLUGIN_API getControllerClassId (Steinberg::TUID classId) override;
    Steinberg::tresult PLUGIN_API setIoMode (Steinberg::Vst::IoMode mode) override;
    Steinberg::int32 PLUGIN_API getBusCount (Steinberg::Vst::MediaType type,
                                             Steinberg::Vst::BusDirection dir) override;
    Steinberg::tresult PLUGIN_API getBusInfo (Steinberg::Vst::MediaType type,
                                              Steinberg::Vst::BusDirection dir,
                                              Steinberg::int32 index,
                                              Steinberg::Vst::BusInfo& bus) override;
    Steinberg::tresult PLUGIN_API getRoutingInfo (Steinberg::Vst::RoutingInfo& inInfo,
                                                  Steinberg::Vst::RoutingInfo& outInfo) override;
    Steinberg::tresult PLUGIN_API activateBus (Steinberg::Vst::MediaType type,
                                               Steinberg::Vst::BusDirection dir,
                                               Steinberg::int32 index,
                                               Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API setActive (Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) override;

    //--- IAudioProcessor --------------------------------------------------
    Steinberg::tresult PLUGIN_API setBusArrangements (Steinberg::Vst::SpeakerArrangement* inputs,
                                                      Steinberg::int32 numIns,
                                                      Steinberg::Vst::SpeakerArrangement* outputs,
                                                      Steinberg::int32 numOuts) override;
    Steinberg::tresult PLUGIN_API getBusArrangement (Steinberg::Vst::BusDirection dir,
                                                     Steinberg::int32 index,
                                                     Steinberg::Vst::SpeakerArrangement& arr) override;
    Steinberg::tresult PLUGIN_API canProcessSampleSize (Steinberg::int32 symbolicSampleSize) override;
    Steinberg::uint32 PLUGIN_API getLatencySamples() override;
    Steinberg::tresult PLUGIN_API setupProcessing (Steinberg::Vst::ProcessSetup& setup) override;
    Steinberg::tresult PLUGIN_API setProcessing (Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API process (Steinberg::Vst::ProcessData& data) override;
    Steinberg::uint32 PLUGIN_API getTailSamples() override;

    //--- IEditController --------------------------------------------------
    Steinberg::tresult PLUGIN_API setComponentState (Steinberg::IBStream* state) override;
    Steinberg::int32 PLUGIN_API getParameterCount() override;
    Steinberg::tresult PLUGIN_API getParameterInfo (Steinberg::int32 paramIndex,
                                                    Steinberg::Vst::ParameterInfo& info) override;
    Steinberg::tresult PLUGIN_API getParamStringByValue (Steinberg::Vst::ParamID id,
                                                         Steinberg::Vst::ParamValue valueNormalized,
                                                         Steinberg::Vst::String128 string) override;
    Steinberg::tresult PLUGIN_API getParamValueByString (Steinberg::Vst::ParamID id,
                                                         Steinberg::Vst::TChar* string,
                                                         Steinberg::Vst::ParamValue& valueNormalized) override;
    Steinberg::Vst::ParamValue PLUGIN_API normalizedParamToPlain (Steinberg::Vst::ParamID id,
                                                                  Steinberg::Vst::ParamValue valueNormalized) override;
    Steinberg::Vst::ParamValue PLUGIN_API plainParamToNormalized (Steinberg::Vst::ParamID id,
                                                                  Steinberg::Vst::ParamValue plainValue) override;
    Steinberg::Vst::ParamValue PLUGIN_API getParamNormalized (Steinberg::Vst::ParamID id) override;
    Steinberg::tresult PLUGIN_API setParamNormalized (Steinberg::Vst::ParamID id,
                                                      Steinberg::Vst::ParamValue value) override;
    Steinberg::tresult PLUGIN_API setComponentHandler (Steinberg::Vst::IComponentHandler* handler) override;
    Steinberg::IPlugView* PLUGIN_API createView (Steinberg::FIDString name) override;

    //--- own API (used by the view) ----------------------------------------
    float getParam (int index) const noexcept { return values[index].load (std::memory_order_relaxed); }
    void setParamFromUI (int index, float value);
    void beginEdit (int index);
    void endEdit (int index);
    void viewDestroyed (Steinberg::IPlugView* view);

private:
    void readState (Steinberg::IBStream* state);
    void writeState (Steinberg::IBStream* state);

    std::atomic<Steinberg::int32> refCount { 1 };
    std::atomic<float> values[kNumParameters];

    wah::Engine engine;
    double currentSampleRate = 44100.0;
    Steinberg::int32 maxBlockSize = 512;
    Steinberg::int32 busChannels = 2;
    bool active = false;

    Steinberg::Vst::IComponentHandler* componentHandler = nullptr;
    std::atomic<Steinberg::IPlugView*> currentView { nullptr };
};

//-----------------------------------------------------------------------------
// Helpers shared with the factory / view
//-----------------------------------------------------------------------------
void copyString (Steinberg::Vst::TChar* dst, const char* src);
void copyStringN (Steinberg::Vst::TChar* dst, const char* src, int maxChars);
bool tuidEqual (const char* a, const char* b);

} // namespace crybaby

