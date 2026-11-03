//-----------------------------------------------------------------------------
// CryBabyWah - VST3 class factory and module entry points
//-----------------------------------------------------------------------------
#include "WahPlugin.h"

#include <cstring>

using namespace Steinberg;

namespace crybaby
{

namespace
{
class PluginFactory : public IPluginFactory3
{
public:
    PluginFactory() : refCount (1) {}

    //--- FUnknown ---------------------------------------------------------
    tresult PLUGIN_API queryInterface (const TUID _iid, void** obj) override
    {
        if (obj == nullptr)
            return kInvalidArgument;
        *obj = nullptr;

        if (FUnknownPrivate::iidEqual (_iid, FUnknown::iid))
        {
            addRef();
            *obj = static_cast<IPluginFactory*> (this);
            return kResultOk;
        }
        if (FUnknownPrivate::iidEqual (_iid, IPluginFactory::iid))
        {
            addRef();
            *obj = static_cast<IPluginFactory*> (this);
            return kResultOk;
        }
        if (FUnknownPrivate::iidEqual (_iid, IPluginFactory2::iid))
        {
            addRef();
            *obj = static_cast<IPluginFactory2*> (this);
            return kResultOk;
        }
        if (FUnknownPrivate::iidEqual (_iid, IPluginFactory3::iid))
        {
            addRef();
            *obj = static_cast<IPluginFactory3*> (this);
            return kResultOk;
        }
        return kNoInterface;
    }

    uint32 PLUGIN_API addRef() override { return static_cast<uint32> (++refCount); }

    uint32 PLUGIN_API release() override
    {
        const int32 count = --refCount;
        return count <= 0 ? 0 : static_cast<uint32> (count);
    }

    //--- IPluginFactory ---------------------------------------------------
    tresult PLUGIN_API getFactoryInfo (PFactoryInfo* info) override
    {
        if (info == nullptr)
            return kInvalidArgument;

        *info = PFactoryInfo ("CryBabyWah Project", "",
                              "",
                              PFactoryInfo::kUnicode | PFactoryInfo::kComponentNonDiscardable);
        return kResultOk;
    }

    int32 PLUGIN_API countClasses() override { return 1; }

    tresult PLUGIN_API getClassInfo (int32 index, PClassInfo* info) override
    {
        if (info == nullptr || index != 0)
            return kInvalidArgument;

        std::memset (info, 0, sizeof (PClassInfo));
        kProcessorUID.toTUID (info->cid);
        info->cardinality = PClassInfo::kManyInstances;
        std::strncpy (info->category, "Audio Module Class", PClassInfo::kCategorySize - 1);
        std::strncpy (info->name, "CB-95 Wah", PClassInfo::kNameSize - 1);
        return kResultOk;
    }

    tresult PLUGIN_API getClassInfo2 (int32 index, PClassInfo2* info) override
    {
        if (info == nullptr || index != 0)
            return kInvalidArgument;

        std::memset (info, 0, sizeof (PClassInfo2));
        kProcessorUID.toTUID (info->cid);
        info->cardinality = PClassInfo::kManyInstances;
        std::strncpy (info->category, "Audio Module Class", PClassInfo::kCategorySize - 1);
        std::strncpy (info->name, "CB-95 Wah", PClassInfo::kNameSize - 1);
        info->classFlags = 0;
        std::strncpy (info->subCategories, "Fx|Filter", PClassInfo2::kSubCategoriesSize - 1);
        std::strncpy (info->vendor, "CryBabyWah Project", 63);
        std::strncpy (info->version, "1.3.0", 63);
        std::strncpy (info->sdkVersion, "VST 3.7.0", 63);
        return kResultOk;
    }

    tresult PLUGIN_API getClassInfoUnicode (int32 index, PClassInfoW* info) override
    {
        if (info == nullptr || index != 0)
            return kInvalidArgument;

        std::memset (info, 0, sizeof (PClassInfoW));
        kProcessorUID.toTUID (info->cid);
        info->cardinality = PClassInfo::kManyInstances;
        std::strncpy (info->category, "Audio Module Class", PClassInfo::kCategorySize - 1);
        copyString (info->name, "CB-95 Wah");
        info->classFlags = 0;
        std::strncpy (info->subCategories, "Fx|Filter", PClassInfo2::kSubCategoriesSize - 1);
        copyStringN (info->vendor, "CryBabyWah Project", 64);
        copyStringN (info->version, "1.3.0", 64);
        copyStringN (info->sdkVersion, "VST 3.7.0", 64);
        return kResultOk;
    }

    tresult PLUGIN_API createInstance (FIDString cid, FIDString _iid, void** obj) override
    {
        if (obj == nullptr)
            return kInvalidArgument;
        *obj = nullptr;

        if (cid == nullptr || _iid == nullptr)
            return kInvalidArgument;

        if (! tuidEqual (cid, kProcessorUID.toTUID()))
            return kNoInterface;

        auto* instance = new WahPlugin();
        FUnknown* unknown = nullptr;
        const tresult result = instance->queryInterface (_iid, reinterpret_cast<void**> (&unknown));
        instance->release();

        if (result == kResultOk)
        {
            *obj = unknown;
            return kResultOk;
        }

        return kNoInterface;
    }

    //--- IPluginFactory3 --------------------------------------------------
    tresult PLUGIN_API setHostContext (FUnknown* /*context*/) override { return kResultOk; }

private:
    std::atomic<int32> refCount;
};

} // namespace

} // namespace crybaby

//-----------------------------------------------------------------------------
extern "C"
{
SMTG_EXPORT_SYMBOL IPluginFactory* PLUGIN_API GetPluginFactory()
{
    static crybaby::PluginFactory factory;
    factory.addRef();
    return &factory;
}

SMTG_EXPORT_SYMBOL bool PLUGIN_API InitDll()
{
    return true;
}

SMTG_EXPORT_SYMBOL bool PLUGIN_API ExitDll()
{
    return true;
}

SMTG_EXPORT_SYMBOL bool PLUGIN_API ModuleEntry (void* /*sharedLibraryHandle*/)
{
    return true;
}

SMTG_EXPORT_SYMBOL bool PLUGIN_API ModuleExit()
{
    return true;
}
}






