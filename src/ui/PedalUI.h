//-----------------------------------------------------------------------------
// CryBabyWah - pedal user interface (Win32 + GDI+)
//
// Platform independent of VST3: the plugin's IPlugView wrapper owns an HWND and
// forwards paint / mouse messages here.  The same code is used by the offline
// UI preview tool, so the artwork can be rendered to PNG without a DAW.
//
// The UI is laid out in a fixed 380 x 560 "logical" space and rendered through
// a scaling transform, so it stays identical on high-DPI screens.
//-----------------------------------------------------------------------------
#pragma once

#include <windows.h>
#include <gdiplus.h>

#include <functional>
#include <string>

namespace ui
{

constexpr int kLogicalWidth = 500;
constexpr int kLogicalHeight = 600;

class PedalUI
{
public:
    PedalUI();
    ~PedalUI();

    // pixel size of the target surface + UI scale factor (1.0 = 96 dpi)
    void setSize (int pixelWidth, int pixelHeight, float uiScale = 1.0f);
    float scale() const noexcept { return uiScale; }

    void paint (Gdiplus::Graphics& g);

    // mouse coordinates are in *device* pixels, as delivered by Win32
    bool mouseDown (int x, int y);
    bool mouseMove (int x, int y);
    bool mouseUp();
    bool mouseWheel (int x, int y, int wheelDelta);

    bool tick (double deltaSeconds);

    void setParamReader (std::function<float (int)> fn) { readParam = std::move (fn); }
    void setParamWriter (std::function<void (int, float)> fn) { writeParam = std::move (fn); }

    // called when a mouse gesture on a parameter starts / ends (host beginEdit)
    void setGestureCallbacks (std::function<void (int)> begin, std::function<void (int)> end)
    {
        beginEdit = std::move (begin);
        endEdit = std::move (end);
    }

    bool isDragging() const noexcept { return dragMode != DragMode::None; }

private:
    enum class DragMode { None, Pedal, Knob };

    struct Knob
    {
        int param = 0;
        float cx = 0.0f, cy = 0.0f, r = 24.0f;
        const wchar_t* label = L"";
    };

    void layout();
    int knobAt (int x, int y) const noexcept;
    bool inPedalBody (int x, int y) const noexcept;
    float param (int index) const noexcept;
    void setParam (int index, float value);
    void beginGesture (int paramIndex);
    void endGesture();
    void nudgeParam (int index, float amount);

    void drawBackground (Gdiplus::Graphics& g);
    void drawPedal (Gdiplus::Graphics& g);
    void drawKnob (Gdiplus::Graphics& g, const Knob& k);
    void drawSwitch (Gdiplus::Graphics& g);

    int pixelW = kLogicalWidth, pixelH = kLogicalHeight;
    float uiScale = 1.0f;

    float displayPedal = 0.45f;
    double time = 0.0;

    DragMode dragMode = DragMode::None;
    int dragKnob = -1;
    int lastX = 0, lastY = 0;
    bool fineDrag = false;
    double lastClickTime = 0.0;
    int lastClickKnob = -1;

    // pedal geometry, computed in layout()
    float cx = 190.0f;
    float bodyX = 26.0f, bodyY = 88.0f, bodyW = 328.0f, bodyH = 292.0f;
    float heelY = 120.0f, heelHalf = 112.0f;
    float knobY = 440.0f;
    float switchY = 520.0f;

    Knob knobs[7];

    std::function<float (int)> readParam;
    std::function<void (int, float)> writeParam;
    std::function<void (int)> beginEdit;
    std::function<void (int)> endEdit;
    int gestureParam = -1;

    Gdiplus::FontFamily fontFamily;
    Gdiplus::Font titleFont, subtitleFont, labelFont, valueFont, tinyFont, logoFont;
};

} // namespace ui



