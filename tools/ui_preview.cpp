//-----------------------------------------------------------------------------
// ui_preview - renders the pedal UI to PNG files (no DAW required)
//
//   ui_preview <outDir> [scale]
//-----------------------------------------------------------------------------
#include "../src/ui/PedalUI.h"
#include "../src/plugin/Parameters.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

using namespace Gdiplus;

static int getEncoderClsid (const WCHAR* format, CLSID* clsid)
{
    UINT num = 0, size = 0;
    GetImageEncodersSize (&num, &size);
    if (size == 0)
        return -1;

    auto* info = static_cast<ImageCodecInfo*> (malloc (size));
    if (info == nullptr)
        return -1;

    GetImageEncoders (num, size, info);
    int result = -1;
    for (UINT i = 0; i < num; ++i)
    {
        if (wcscmp (info[i].MimeType, format) == 0)
        {
            *clsid = info[i].Clsid;
            result = static_cast<int> (i);
            break;
        }
    }
    free (info);
    return result;
}

int main (int argc, char** argv)
{
    const std::string outDir = argc > 1 ? argv[1] : ".";
    const float scale = argc > 2 ? std::max (0.5f, static_cast<float> (std::atof (argv[2]))) : 1.0f;
    const int width = static_cast<int> (ui::kLogicalWidth * scale + 0.5f);
    const int height = static_cast<int> (ui::kLogicalHeight * scale + 0.5f);

    float params[crybaby::kNumParameters];
    for (int i = 0; i < crybaby::kNumParameters; ++i)
        params[i] = crybaby::parameterDefault (i);

    ui::PedalUI pedal;
    pedal.setSize (width, height, scale);
    pedal.setParamReader ([&] (int i) { return params[i]; });
    pedal.setParamWriter ([&] (int i, float v) { params[i] = v; });

    const float positions[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };

    CLSID pngClsid;
    getEncoderClsid (L"image/png", &pngClsid);

    for (float p : positions)
    {
        params[crybaby::kPedal] = p;
        params[crybaby::kDrive] = 0.32f;
        params[crybaby::kResonance] = 0.55f;
        params[crybaby::kTone] = 0.55f;
        params[crybaby::kMix] = 1.0f;
        params[crybaby::kVolume] = 0.5f;
        params[crybaby::kBypass] = 0.0f;

        for (int i = 0; i < 60; ++i)
            pedal.tick (0.02);
        pedal.tick (1.0);

        Bitmap bitmap (width, height, PixelFormat32bppARGB);
        {
            Graphics g (&bitmap);
            g.Clear (Color (255, 8, 9, 11));
            pedal.paint (g);
        }

        wchar_t name[512];
        swprintf (name, 512, L"%S\\pedal_%03d%s.png", outDir.c_str(), static_cast<int> (p * 100.0f),
                  scale > 1.01f ? L"_scaled" : L"");
        bitmap.Save (name, &pngClsid, nullptr);
        std::printf ("wrote %ls\n", name);
    }

    return 0;
}

