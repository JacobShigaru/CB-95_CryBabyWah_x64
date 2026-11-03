//-----------------------------------------------------------------------------
// CryBabyWah - interface UIDs and FUID support code
//
// The VST3 SDK normally compiles one small .cpp per interface to define its
// static FUID.  Because only the headers are vendored here, this translation
// unit includes every header with INIT_CLASS_IID defined, which makes the
// DECLARE_CLASS_IID macro emit all of those definitions at once.
//-----------------------------------------------------------------------------
#define INIT_CLASS_IID

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/fplatform.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/fvariant.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstattributes.h"
#include "pluginterfaces/vst/ivstunits.h"
#include "pluginterfaces/gui/iplugview.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if SMTG_OS_WINDOWS
#include <windows.h>
#endif

namespace Steinberg
{

FUID::FUID()
{
    std::memset (data, 0, sizeof (TUID));
}

FUID::FUID (uint32 l1, uint32 l2, uint32 l3, uint32 l4)
{
    const TUID uid = INLINE_UID (l1, l2, l3, l4);
    std::memcpy (data, uid, sizeof (TUID));
}

FUID::FUID (const FUID& other)
{
    std::memcpy (data, other.data, sizeof (TUID));
}

FUID& FUID::operator= (const FUID& other)
{
    std::memcpy (data, other.data, sizeof (TUID));
    return *this;
}

#if SMTG_CPP11_STDLIBSUPPORT
FUID::FUID (FUID&& other)
{
    std::memcpy (data, other.data, sizeof (TUID));
}

FUID& FUID::operator= (FUID&& other)
{
    std::memcpy (data, other.data, sizeof (TUID));
    return *this;
}
#endif

bool FUID::generate()
{
    static uint32 counter = 0;
    std::srand (static_cast<unsigned> (std::rand () ^ static_cast<int> (counter++)));

    for (char& c : data)
        c = static_cast<char> (std::rand () & 0xFF);

    // version 4 UUID layout hint
    data[6] = static_cast<char> ((data[6] & 0x0F) | 0x40);
    data[8] = static_cast<char> ((data[8] & 0x3F) | 0x80);
    return true;
}

bool FUID::isValid() const
{
    for (char c : data)
        if (c != 0)
            return true;
    return false;
}

uint32 FUID::getLong1() const
{
#if COM_COMPATIBLE
    return static_cast<uint32> (static_cast<unsigned char> (data[0]))
           | (static_cast<uint32> (static_cast<unsigned char> (data[1])) << 8)
           | (static_cast<uint32> (static_cast<unsigned char> (data[2])) << 16)
           | (static_cast<uint32> (static_cast<unsigned char> (data[3])) << 24);
#else
    return (static_cast<uint32> (static_cast<unsigned char> (data[0])) << 24)
           | (static_cast<uint32> (static_cast<unsigned char> (data[1])) << 16)
           | (static_cast<uint32> (static_cast<unsigned char> (data[2])) << 8)
           | static_cast<uint32> (static_cast<unsigned char> (data[3]));
#endif
}

uint32 FUID::getLong2() const
{
#if COM_COMPATIBLE
    return (static_cast<uint32> (static_cast<unsigned char> (data[4])) << 16)
           | (static_cast<uint32> (static_cast<unsigned char> (data[5])) << 24)
           | static_cast<uint32> (static_cast<unsigned char> (data[6]))
           | (static_cast<uint32> (static_cast<unsigned char> (data[7])) << 8);
#else
    return (static_cast<uint32> (static_cast<unsigned char> (data[4])) << 24)
           | (static_cast<uint32> (static_cast<unsigned char> (data[5])) << 16)
           | (static_cast<uint32> (static_cast<unsigned char> (data[6])) << 8)
           | static_cast<uint32> (static_cast<unsigned char> (data[7]));
#endif
}

uint32 FUID::getLong3() const
{
#if COM_COMPATIBLE
    return (static_cast<uint32> (static_cast<unsigned char> (data[8])) << 8)
           | (static_cast<uint32> (static_cast<unsigned char> (data[9])) << 24)
           | static_cast<uint32> (static_cast<unsigned char> (data[10]))
           | (static_cast<uint32> (static_cast<unsigned char> (data[11])) << 16);
#else
    return (static_cast<uint32> (static_cast<unsigned char> (data[8])) << 24)
           | (static_cast<uint32> (static_cast<unsigned char> (data[9])) << 16)
           | (static_cast<uint32> (static_cast<unsigned char> (data[10])) << 8)
           | static_cast<uint32> (static_cast<unsigned char> (data[11]));
#endif
}

uint32 FUID::getLong4() const
{
    return (static_cast<uint32> (static_cast<unsigned char> (data[12])) << 24)
           | (static_cast<uint32> (static_cast<unsigned char> (data[13])) << 16)
           | (static_cast<uint32> (static_cast<unsigned char> (data[14])) << 8)
           | static_cast<uint32> (static_cast<unsigned char> (data[15]));
}

void FUID::from4Int (uint32 d1, uint32 d2, uint32 d3, uint32 d4)
{
    *this = FUID (d1, d2, d3, d4);
}

void FUID::to4Int (uint32& d1, uint32& d2, uint32& d3, uint32& d4) const
{
    d1 = getLong1();
    d2 = getLong2();
    d3 = getLong3();
    d4 = getLong4();
}

void FUID::toString (char8* string) const
{
    if (string == nullptr)
        return;

    for (int i = 0; i < 16; ++i)
        std::sprintf (string + i * 2, "%02X", static_cast<unsigned char> (data[i]));
    string[32] = 0;
}

bool FUID::fromString (const char8* string)
{
    if (string == nullptr || std::strlen (string) < 32)
        return false;

    for (int i = 0; i < 16; ++i)
    {
        char byte[3] = { string[i * 2], string[i * 2 + 1], 0 };
        data[i] = static_cast<char> (std::strtoul (byte, nullptr, 16));
    }
    return true;
}

void FUID::print (int32 style, char8* string, size_t stringBufferSize) const
{
    (void) style;
    if (string == nullptr || stringBufferSize < 33)
        return;
    toString (string);
}

namespace FUnknownPrivate
{
int32 PLUGIN_API atomicAdd (int32& value, int32 amount)
{
#if SMTG_OS_WINDOWS
    return static_cast<int32> (InterlockedAdd (reinterpret_cast<volatile LONG*> (&value), amount));
#else
    return __sync_add_and_fetch (&value, amount);
#endif
}
} // namespace FUnknownPrivate

} // namespace Steinberg
