//-----------------------------------------------------------------------------
// CryBabyWah - pedal UI drawing
//-----------------------------------------------------------------------------
// WahCore uses std::min / std::max, so it must be parsed before windows.h
// brings in the min/max macros (via PedalUI.h).
#include "../dsp/WahCore.h"

#include "PedalUI.h"
#include "../plugin/Parameters.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>

// GDI+ headers rely on the Windows min/max macros; after they are included the
// macros only get in the way of std::min / std::max.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

using namespace Gdiplus;

namespace
{
ULONG_PTR gdiplusToken = 0;

void ensureGdiPlus()
{
    static bool initialised = []
    {
        GdiplusStartupInput input;
        return GdiplusStartup (&gdiplusToken, &input, nullptr) == Ok;
    }();
    (void) initialised;
}

Color rgba (int r, int g, int b, int a = 255)
{
    return Color (static_cast<BYTE> (a), static_cast<BYTE> (r), static_cast<BYTE> (g),
                  static_cast<BYTE> (b));
}

GraphicsPath* makeRoundRect (const RectF& r, float radius, GraphicsPath& path)
{
    path.Reset();
    const float d = std::min (radius * 2.0f, std::min (r.Width, r.Height));
    if (d <= 0.1f)
    {
        path.AddRectangle (r);
        return &path;
    }

    path.AddArc (r.X, r.Y, d, d, 180.0f, 90.0f);
    path.AddArc (r.X + r.Width - d, r.Y, d, d, 270.0f, 90.0f);
    path.AddArc (r.X + r.Width - d, r.Y + r.Height - d, d, d, 0.0f, 90.0f);
    path.AddArc (r.X, r.Y + r.Height - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
    return &path;
}

void fillRoundRect (Graphics& g, const RectF& r, float radius, Brush& brush)
{
    GraphicsPath path;
    g.FillPath (&brush, makeRoundRect (r, radius, path));
}

void strokeRoundRect (Graphics& g, const RectF& r, float radius, Pen& pen)
{
    GraphicsPath path;
    g.DrawPath (&pen, makeRoundRect (r, radius, path));
}

StringFormat& centredFormat()
{
    static StringFormat fmt;
    static bool once = []
    {
        fmt.SetAlignment (StringAlignmentCenter);
        fmt.SetLineAlignment (StringAlignmentCenter);
        return true;
    }();
    (void) once;
    return fmt;
}

const wchar_t* valueText (int param, float v, wchar_t* buffer, int size)
{
    switch (param)
    {
        case crybaby::kLow:
        case crybaby::kHigh:
        {
            const float hz = (param == crybaby::kLow) ? wah::Engine::lowFrequency (v) : wah::Engine::highFrequency (v);
            if (hz >= 1000.0f)
                std::swprintf (buffer, size, L"%.2f kHz", hz / 1000.0f);
            else
                std::swprintf (buffer, size, L"%.0f Hz", hz);
            break;
        }
        case crybaby::kVolume: std::swprintf (buffer, size, L"%+.1f dB", (v - 0.5f) * 18.0f); break;
        case crybaby::kMix:    std::swprintf (buffer, size, L"%.0f %% wet", v * 100.0f); break;
        case crybaby::kPedal:
        {
            const float hz = 380.0f * std::pow (2200.0f / 380.0f, std::pow (v, 0.82f));
            if (hz >= 1000.0f)
                std::swprintf (buffer, size, L"%.2f kHz", hz / 1000.0f);
            else
                std::swprintf (buffer, size, L"%.0f Hz", hz);
            break;
        }
        case crybaby::kBypass: std::swprintf (buffer, size, L"%s", v > 0.5f ? L"BYPASS" : L"ON"); break;
        default: std::swprintf (buffer, size, L"%.0f %%", v * 100.0f); break;
    }
    return buffer;
}

constexpr float kPiF = 3.14159265358979f;

// Pick the first installed UI font.  Some sandboxed host processes expose only
// a reduced font set, which would silently break all DrawString calls.
const wchar_t* pickFontFamily()
{
    static const wchar_t* candidates[] = {
        L"Segoe UI", L"Tahoma", L"Verdana", L"Arial", L"Microsoft Sans Serif"
    };

    for (const wchar_t* name : candidates)
    {
        FontFamily probe (name);
        if (probe.GetLastStatus() == Ok && probe.IsAvailable())
            return name;
    }

    return L"Microsoft Sans Serif";
}

// IMPORTANT: the GDI+ font objects are built in PedalUI's member initialiser
// list, i.e. before the constructor body runs.  GDI+ must therefore be started
// from the initialiser too - otherwise every FontFamily/Font is invalid and
// DrawString silently draws nothing (while plain shapes still work).
const wchar_t* startGdiPlusAndPickFamily()
{
    ensureGdiPlus();
    return pickFontFamily();
}
} // namespace

namespace ui
{

PedalUI::PedalUI()
    : fontFamily (startGdiPlusAndPickFamily()),
      titleFont (&fontFamily, 31.0f, FontStyleBold, UnitPixel),
      subtitleFont (&fontFamily, 11.0f, FontStyleRegular, UnitPixel),
      labelFont (&fontFamily, 9.8f, FontStyleBold, UnitPixel),
      valueFont (&fontFamily, 10.6f, FontStyleRegular, UnitPixel),
      tinyFont (&fontFamily, 9.4f, FontStyleRegular, UnitPixel),
      logoFont (&fontFamily, 22.0f, FontStyleBold, UnitPixel)
{
    layout();
}

PedalUI::~PedalUI() = default;

void PedalUI::setSize (int pixelWidth, int pixelHeight, float uiScaleIn)
{
    pixelW = std::max (pixelWidth, 100);
    pixelH = std::max (pixelHeight, 100);
    uiScale = (uiScaleIn > 0.1f) ? uiScaleIn : 1.0f;
    layout();
}

void PedalUI::layout()
{
    cx = kLogicalWidth * 0.5f;

    // the pedal keeps its own proportions and stays centred; the extra width of
    // the window is used by the (larger) knob row and its labels
    bodyW = 400.0f;
    bodyX = (kLogicalWidth - bodyW) * 0.5f;
    bodyY = 92.0f;
    bodyH = 316.0f;

    heelY = bodyY + 32.0f;
    heelHalf = bodyW * 0.34f;

    knobY = bodyY + bodyH + 60.0f;    // 468
    switchY = kLogicalHeight - 34.0f; // 566

    const float r = 21.0f;
    const float knobMargin = 26.0f;
    const float spacing = (kLogicalWidth - knobMargin * 2.0f) / 7.0f;
    const float first = knobMargin + spacing * 0.5f;

    knobs[0] = { crybaby::kDrive, first, knobY, r, L"DRIVE" };
    knobs[1] = { crybaby::kResonance, first + spacing, knobY, r, L"RESONANCE" };
    knobs[2] = { crybaby::kLow, first + spacing * 2.0f, knobY, r, L"LOW" };
    knobs[3] = { crybaby::kHigh, first + spacing * 3.0f, knobY, r, L"HIGH" };
    knobs[4] = { crybaby::kTone, first + spacing * 4.0f, knobY, r, L"TONE" };
    knobs[5] = { crybaby::kMix, first + spacing * 5.0f, knobY, r, L"MIX" };
    knobs[6] = { crybaby::kVolume, first + spacing * 6.0f, knobY, r, L"VOLUME" };
}

float PedalUI::param (int index) const noexcept
{
    if (readParam)
        return readParam (index);
    return crybaby::parameterDefault (index);
}

void PedalUI::setParam (int index, float value)
{
    value = std::min (1.0f, std::max (0.0f, value));
    if (writeParam)
        writeParam (index, value);
}

void PedalUI::beginGesture (int paramIndex)
{
    endGesture();
    gestureParam = paramIndex;
    if (beginEdit)
        beginEdit (paramIndex);
}

void PedalUI::endGesture()
{
    if (gestureParam >= 0)
    {
        if (endEdit)
            endEdit (gestureParam);
        gestureParam = -1;
    }
}

void PedalUI::nudgeParam (int index, float amount)
{
    setParam (index, param (index) + amount);
}

//-----------------------------------------------------------------------------
void PedalUI::paint (Graphics& g)
{
    g.SetSmoothingMode (SmoothingModeAntiAlias);
    g.SetTextRenderingHint (TextRenderingHintAntiAliasGridFit);
    g.SetPixelOffsetMode (PixelOffsetModeHighQuality);
    g.SetInterpolationMode (InterpolationModeHighQualityBicubic);

    // everything below is drawn in the 440 x 580 logical space; if the window
    // aspect differs the content is centred and the edges stay dark
    const float contentW = static_cast<float> (kLogicalWidth) * uiScale;
    const float contentH = static_cast<float> (kLogicalHeight) * uiScale;
    const float offsetX = (static_cast<float> (pixelW) - contentW) * 0.5f;
    const float offsetY = (static_cast<float> (pixelH) - contentH) * 0.5f;

    Matrix transform (uiScale, 0.0f, 0.0f, uiScale, offsetX, offsetY);
    g.SetTransform (&transform);

    drawBackground (g);
    drawPedal (g);

    for (const auto& k : knobs)
        drawKnob (g, k);

    drawSwitch (g);
}

void PedalUI::drawBackground (Graphics& g)
{
    const RectF full (0.0f, 0.0f, static_cast<float> (kLogicalWidth), static_cast<float> (kLogicalHeight));
    LinearGradientBrush bg (PointF (0.0f, 0.0f), PointF (0.0f, static_cast<float> (kLogicalHeight)),
                            rgba (31, 32, 38), rgba (8, 9, 11));
    g.FillRectangle (&bg, full);

    // brushed metal hint: very faint horizontal lines
    Pen hairline (rgba (255, 255, 255, 6), 1.0f);
    for (float y = 0.0f; y < static_cast<float> (kLogicalHeight); y += 3.0f)
        g.DrawLine (&hairline, 0.0f, y, static_cast<float> (kLogicalWidth), y);

    // vignette
    GraphicsPath vignette;
    vignette.AddEllipse (RectF (-0.25f * kLogicalWidth, -84.0f, 1.5f * kLogicalWidth, 700.0f));
    PathGradientBrush vign (&vignette);
    vign.SetCenterColor (rgba (0, 0, 0, 0));
    Color surround[1] = { rgba (0, 0, 0, 120) };
    int count = 1;
    vign.SetSurroundColors (surround, &count);
    g.FillRectangle (&vign, full);

    // thin outer frame
    Pen frame (rgba (255, 255, 255, 18), 1.0f);
    g.DrawRectangle (&frame, 0.5f, 0.5f, static_cast<float> (kLogicalWidth) - 1.0f,
                     static_cast<float> (kLogicalHeight) - 1.0f);

    // header
    SolidBrush titleBrush (rgba (239, 240, 244));
    g.DrawString (L"CB-95", -1, &titleFont, PointF (30.0f, 12.0f), &titleBrush);

    SolidBrush subBrush (rgba (154, 158, 168));
    g.DrawString (L"W A H   P E D A L", -1, &subtitleFont, PointF (32.0f, 54.0f), &subBrush);

    StringFormat right;
    right.SetAlignment (StringAlignmentFar);

    SolidBrush dimBrush (rgba (124, 128, 137));
    g.DrawString (L"ANALOG MODELLED", -1, &tinyFont, RectF (0.0f, 13.0f, static_cast<float> (kLogicalWidth) - 30.0f, 16.0f),
                  &right, &dimBrush);

    // live resonant frequency readout (follows the sweep range controls)
    wchar_t buffer[32];
    const float hz = wah::Engine::sweepFrequency (param (crybaby::kPedal), param (crybaby::kLow),
                                                  param (crybaby::kHigh));
    if (hz >= 1000.0f)
        std::swprintf (buffer, 32, L"%.2f kHz", hz / 1000.0f);
    else
        std::swprintf (buffer, 32, L"%.0f Hz", hz);

    SolidBrush freqBrush (rgba (216, 192, 138));
    g.DrawString (buffer, -1, &valueFont,
                  RectF (0.0f, 31.0f, static_cast<float> (kLogicalWidth) - 30.0f, 20.0f), &right, &freqBrush);
}

void PedalUI::drawPedal (Graphics& g)
{
    const float p = std::min (1.0f, std::max (0.0f, displayPedal));

    // --- shadow under the pedal -------------------------------------------
    for (int i = 3; i >= 1; --i)
    {
        const float grow = static_cast<float> (i) * 5.0f;
        SolidBrush shadow (rgba (0, 0, 0, 26));
        fillRoundRect (g, RectF (bodyX - grow + 4.0f, bodyY + bodyH - 8.0f + grow * 0.6f,
                                 bodyW + grow * 2.0f, 26.0f + grow),
                       16.0f, shadow);
    }

    // --- base body ---------------------------------------------------------
    const RectF body (bodyX, bodyY, bodyW, bodyH);
    LinearGradientBrush bodyBrush (PointF (bodyX, bodyY), PointF (bodyX, bodyY + bodyH),
                                   rgba (60, 62, 70), rgba (12, 13, 15));
    fillRoundRect (g, body, 15.0f, bodyBrush);

    Pen bodyEdge (rgba (10, 11, 13, 220), 1.4f);
    strokeRoundRect (g, body, 15.0f, bodyEdge);

    // inner bevel
    LinearGradientBrush bevel (PointF (bodyX, bodyY), PointF (bodyX, bodyY + 60.0f),
                               rgba (255, 255, 255, 36), rgba (255, 255, 255, 0));
    Pen bevelPen (&bevel, 1.2f);
    strokeRoundRect (g, RectF (bodyX + 1.0f, bodyY + 1.0f, bodyW - 2.0f, bodyH - 2.0f), 14.0f, bevelPen);

    // side rails (the pivot frame)
    for (int side = 0; side < 2; ++side)
    {
        const float rx = side == 0 ? bodyX + 7.0f : bodyX + bodyW - 15.0f;
        const RectF rail (rx, bodyY + 14.0f, 8.0f, bodyH - 28.0f);
        LinearGradientBrush railBrush (PointF (rx, 0.0f), PointF (rx + 8.0f, 0.0f),
                                       rgba (132, 137, 146), rgba (30, 32, 36));
        fillRoundRect (g, rail, 4.0f, railBrush);
        Pen railPen (rgba (0, 0, 0, 120), 1.0f);
        strokeRoundRect (g, rail, 4.0f, railPen);
    }

    // screws
    const PointF screws[4] = {
        { bodyX + 13.0f, bodyY + 13.0f }, { bodyX + bodyW - 13.0f, bodyY + 13.0f },
        { bodyX + 13.0f, bodyY + bodyH - 13.0f }, { bodyX + bodyW - 13.0f, bodyY + bodyH - 13.0f }
    };
    for (const auto& s : screws)
    {
        const RectF sr (s.X - 5.0f, s.Y - 5.0f, 10.0f, 10.0f);
        LinearGradientBrush sb (PointF (sr.X, sr.Y), PointF (sr.X + 10.0f, sr.Y + 10.0f),
                                rgba (154, 159, 168), rgba (44, 46, 52));
        g.FillEllipse (&sb, sr);
        Pen sp (rgba (0, 0, 0, 150), 1.0f);
        g.DrawEllipse (&sp, sr);
        g.DrawLine (&sp, s.X - 3.0f, s.Y, s.X + 3.0f, s.Y);
    }

    // pivot axle across the heel
    {
        const RectF axle (bodyX + 12.0f, heelY - 14.0f, bodyW - 24.0f, 11.0f);
        LinearGradientBrush axleBrush (PointF (0.0f, axle.Y), PointF (0.0f, axle.Y + axle.Height),
                                       rgba (180, 185, 194), rgba (48, 51, 57));
        fillRoundRect (g, axle, 5.0f, axleBrush);
        Pen axlePen (rgba (0, 0, 0, 150), 1.0f);
        strokeRoundRect (g, axle, 5.0f, axlePen);
        for (int side = 0; side < 2; ++side)
        {
            const float ax = side == 0 ? axle.X + 2.0f : axle.X + axle.Width - 2.0f;
            SolidBrush capBrush (rgba (210, 215, 224));
            g.FillEllipse (&capBrush, ax - 4.0f, axle.Y + 1.5f, 8.0f, 8.0f);
            Pen capPen (rgba (0, 0, 0, 140), 1.0f);
            g.DrawEllipse (&capPen, ax - 4.0f, axle.Y + 1.5f, 8.0f, 8.0f);
        }
    }

    // --- treadle -----------------------------------------------------------
    const float toeY = bodyY + bodyH * 0.71f + bodyH * 0.19f * p;
    const float toeHalf = heelHalf + bodyW * 0.043f + bodyW * 0.067f * p;
    const float halfAtToe = std::min (toeHalf, bodyW * 0.5f - bodyW * 0.055f);
    const float halfAtHeel = heelHalf;

    // thickness along the sides
    for (int side = 0; side < 2; ++side)
    {
        const float sgn = side == 0 ? -1.0f : 1.0f;
        PointF sideQuad[4] = {
            { cx + sgn * halfAtHeel, heelY }, { cx + sgn * halfAtToe, toeY },
            { cx + sgn * (halfAtToe - 8.0f), toeY + 9.0f }, { cx + sgn * (halfAtHeel - 8.0f), heelY + 9.0f }
        };
        LinearGradientBrush sideBrush (PointF (0.0f, heelY), PointF (0.0f, toeY),
                                       rgba (38, 40, 45), rgba (10, 11, 13));
        g.FillPolygon (&sideBrush, sideQuad, 4);
    }

    // thickness / front lip
    PointF lip[4] = {
        { cx - halfAtToe, toeY }, { cx + halfAtToe, toeY },
        { cx + halfAtToe - 5.0f, toeY + 14.0f }, { cx - halfAtToe + 5.0f, toeY + 14.0f }
    };
    LinearGradientBrush lipBrush (PointF (0.0f, toeY), PointF (0.0f, toeY + 14.0f),
                                  rgba (30, 31, 35), rgba (6, 6, 8));
    g.FillPolygon (&lipBrush, lip, 4);

    // plate
    PointF plate[4] = {
        { cx - halfAtHeel, heelY }, { cx + halfAtHeel, heelY },
        { cx + halfAtToe, toeY }, { cx - halfAtToe, toeY }
    };
    GraphicsPath platePath;
    platePath.AddPolygon (plate, 4);

    PathGradientBrush plateBrush (&platePath);
    plateBrush.SetCenterPoint (PointF (cx - halfAtHeel * 0.35f, heelY + (toeY - heelY) * 0.30f));
    plateBrush.SetCenterColor (rgba (90, 93, 102));
    Color plateEdges[1] = { rgba (17, 18, 21) };
    int edgeCount = 1;
    plateBrush.SetSurroundColors (plateEdges, &edgeCount);
    g.FillPath (&plateBrush, &platePath);

    Pen plateEdge (rgba (5, 5, 7, 230), 1.6f);
    g.DrawPath (&plateEdge, &platePath);

    // heel highlight
    Pen heelGlow (rgba (255, 255, 255, 60), 1.2f);
    g.DrawLine (&heelGlow, cx - halfAtHeel + 3.0f, heelY + 1.0f, cx + halfAtHeel - 3.0f, heelY + 1.0f);

    // rubber ribs
    const float ribTop = heelY + bodyH * 0.25f;
    const float ribBottom = toeY - 8.0f;
    const int numRibs = 11;
    for (int i = 0; i < numRibs; ++i)
    {
        const float t = static_cast<float> (i) / static_cast<float> (numRibs - 1);
        const float y = ribTop + (ribBottom - ribTop) * t;
        const float half = halfAtHeel + (halfAtToe - halfAtHeel) * ((y - heelY) / std::max (toeY - heelY, 1.0f)) - 8.0f;

        Pen ribDark (rgba (4, 4, 6, 170), 3.0f);
        g.DrawLine (&ribDark, cx - half, y, cx + half, y);
        Pen ribLight (rgba (255, 255, 255, 28), 1.0f);
        g.DrawLine (&ribLight, cx - half, y + 2.6f, cx + half, y + 2.6f);
    }

    // heel pad with the model name
    const float padWidth = bodyW * 0.42f;
    const RectF pad (cx - padWidth * 0.5f, heelY + 16.0f, padWidth, 48.0f);
    LinearGradientBrush padBrush (PointF (pad.X, pad.Y), PointF (pad.X, pad.Y + pad.Height),
                                  rgba (38, 39, 44), rgba (14, 15, 18));
    fillRoundRect (g, pad, 8.0f, padBrush);
    Pen padPen (rgba (255, 255, 255, 28), 1.0f);
    strokeRoundRect (g, pad, 8.0f, padPen);

    SolidBrush logoBrush (rgba (206, 210, 218));
    g.DrawString (L"CB-95", -1, &logoFont, RectF (pad.X, pad.Y + 1.0f, pad.Width, pad.Height - 6.0f),
                  &centredFormat(), &logoBrush);

    SolidBrush padSub (rgba (150, 154, 163));
    g.DrawString (L"WAH", -1, &tinyFont, RectF (pad.X, pad.Y + 30.0f, pad.Width, 14.0f),
                  &centredFormat(), &padSub);
}

void PedalUI::drawKnob (Graphics& g, const Knob& k)
{
    const float v = std::min (1.0f, std::max (0.0f, param (k.param)));
    const float r = k.r;

    // label
    SolidBrush labelBrush (rgba (188, 192, 201));
    g.DrawString (k.label, -1, &labelFont,
                  RectF (k.cx - 52.0f, k.cy - r - 26.0f, 104.0f, 16.0f), &centredFormat(), &labelBrush);

    // shadow
    SolidBrush shadow (rgba (0, 0, 0, 90));
    g.FillEllipse (&shadow, k.cx - r + 1.0f, k.cy - r + 4.0f, r * 2.0f, r * 2.0f);

    // body
    RectF outer (k.cx - r, k.cy - r, r * 2.0f, r * 2.0f);
    LinearGradientBrush bodyBrush (PointF (k.cx - r, k.cy - r), PointF (k.cx + r, k.cy + r),
                                   rgba (78, 81, 89), rgba (13, 14, 16));
    g.FillEllipse (&bodyBrush, outer);

    Pen rim (rgba (0, 0, 0, 170), 1.2f);
    g.DrawEllipse (&rim, outer);

    // cap
    GraphicsPath capPath;
    const RectF cap (k.cx - r * 0.74f, k.cy - r * 0.74f, r * 1.48f, r * 1.48f);
    capPath.AddEllipse (cap);
    PathGradientBrush capBrush (&capPath);
    capBrush.SetCenterPoint (PointF (k.cx - r * 0.25f, k.cy - r * 0.32f));
    capBrush.SetCenterColor (rgba (98, 102, 111));
    Color capEdge[1] = { rgba (21, 22, 26) };
    int one = 1;
    capBrush.SetSurroundColors (capEdge, &one);
    g.FillPath (&capBrush, &capPath);

    Pen capRim (rgba (255, 255, 255, 40), 1.0f);
    g.DrawPath (&capRim, &capPath);

    // value arc
    const float startAngle = -225.0f;
    const float sweep = 270.0f * v;
    Pen arcBack (rgba (255, 255, 255, 24), 2.2f);
    g.DrawArc (&arcBack, k.cx - r - 5.0f, k.cy - r - 5.0f, (r + 5.0f) * 2.0f, (r + 5.0f) * 2.0f,
               startAngle, 270.0f);
    Pen arcValue (rgba (232, 163, 61, 235), 2.4f);
    if (sweep > 0.5f)
        g.DrawArc (&arcValue, k.cx - r - 5.0f, k.cy - r - 5.0f, (r + 5.0f) * 2.0f, (r + 5.0f) * 2.0f,
                   startAngle, sweep);

    // indicator
    const float angle = (startAngle + sweep) * kPiF / 180.0f;
    const float x0 = k.cx + std::cos (angle) * r * 0.30f;
    const float y0 = k.cy + std::sin (angle) * r * 0.30f;
    const float x1 = k.cx + std::cos (angle) * r * 0.64f;
    const float y1 = k.cy + std::sin (angle) * r * 0.64f;
    Pen indicator (rgba (244, 245, 249), 2.4f);
    indicator.SetLineCap (LineCapRound, LineCapRound, DashCapRound);
    g.DrawLine (&indicator, x0, y0, x1, y1);

    // value
    wchar_t buffer[32];
    SolidBrush valueBrush (rgba (150, 154, 163));
    g.DrawString (valueText (k.param, v, buffer, 32), -1, &valueFont,
                  RectF (k.cx - 56.0f, k.cy + r + 11.0f, 112.0f, 16.0f), &centredFormat(), &valueBrush);
}

void PedalUI::drawSwitch (Graphics& g)
{
    const bool bypassed = param (crybaby::kBypass) > 0.5f;
    const float swY = switchY;
    const float swR = 15.0f;

    // LED
    const float ledY = swY - 30.0f;
    if (! bypassed)
    {
        for (int i = 3; i >= 1; --i)
        {
            SolidBrush glow (rgba (255, 70, 52, 20));
            const float gr = 4.0f + static_cast<float> (i) * 3.0f;
            g.FillEllipse (&glow, cx - gr, ledY - gr, gr * 2.0f, gr * 2.0f);
        }
        SolidBrush led (rgba (255, 92, 70));
        g.FillEllipse (&led, cx - 4.0f, ledY - 4.0f, 8.0f, 8.0f);
        SolidBrush core (rgba (255, 214, 205, 230));
        g.FillEllipse (&core, cx - 1.6f, ledY - 1.6f, 3.2f, 3.2f);
    }
    else
    {
        SolidBrush ledOff (rgba (60, 33, 31));
        g.FillEllipse (&ledOff, cx - 4.0f, ledY - 4.0f, 8.0f, 8.0f);
        Pen ledRing (rgba (0, 0, 0, 120), 1.0f);
        g.DrawEllipse (&ledRing, cx - 4.0f, ledY - 4.0f, 8.0f, 8.0f);
    }

    // switch body
    RectF sw (cx - swR, swY - swR, swR * 2.0f, swR * 2.0f);
    SolidBrush shadow (rgba (0, 0, 0, 110));
    g.FillEllipse (&shadow, sw.X, sw.Y + 4.0f, sw.Width, sw.Height);

    LinearGradientBrush swBrush (PointF (sw.X, sw.Y), PointF (sw.X, sw.Y + sw.Height),
                                 rgba (90, 94, 102), rgba (22, 23, 27));
    g.FillEllipse (&swBrush, sw);
    Pen swPen (rgba (0, 0, 0, 160), 1.2f);
    g.DrawEllipse (&swPen, sw);

    const RectF inner (sw.X + 4.0f, sw.Y + 4.0f, sw.Width - 8.0f, sw.Height - 8.0f);
    LinearGradientBrush innerBrush (PointF (inner.X, inner.Y), PointF (inner.X, inner.Y + inner.Height),
                                    rgba (54, 56, 62), rgba (18, 19, 22));
    g.FillEllipse (&innerBrush, inner);

    // state label, to the right of the switch
    SolidBrush swLabel (rgba (168, 172, 181));
    StringFormat left;
    left.SetAlignment (StringAlignmentNear);
    left.SetLineAlignment (StringAlignmentCenter);
    g.DrawString (bypassed ? L"BYPASS" : L"ON", -1, &tinyFont,
                  RectF (cx + 24.0f, swY - 8.0f, 60.0f, 16.0f), &left, &swLabel);
}

//-----------------------------------------------------------------------------
int PedalUI::knobAt (int x, int y) const noexcept
{
    const float lx = static_cast<float> (x) / uiScale;
    const float ly = static_cast<float> (y) / uiScale;

    for (int i = 0; i < 7; ++i)
    {
        const float dx = lx - knobs[i].cx;
        const float dy = ly - knobs[i].cy;
        if (dx * dx + dy * dy <= (knobs[i].r + 10.0f) * (knobs[i].r + 10.0f))
            return i;
    }
    return -1;
}

bool PedalUI::inPedalBody (int x, int y) const noexcept
{
    const float lx = static_cast<float> (x) / uiScale;
    const float ly = static_cast<float> (y) / uiScale;
    return lx >= bodyX && lx <= bodyX + bodyW && ly >= bodyY && ly <= bodyY + bodyH;
}

bool PedalUI::mouseDown (int x, int y)
{
    time += 0.01;
    lastX = x;
    lastY = y;
    fineDrag = (GetKeyState (VK_SHIFT) & 0x8000) != 0;

    // footswitch
    {
        const float lx = static_cast<float> (x) / uiScale;
        const float ly = static_cast<float> (y) / uiScale;
        const float dx = lx - cx;
        const float dy = ly - switchY;
        if (dx * dx + dy * dy <= 24.0f * 24.0f)
        {
            beginGesture (crybaby::kBypass);
            setParam (crybaby::kBypass, param (crybaby::kBypass) > 0.5f ? 0.0f : 1.0f);
            endGesture();
            return true;
        }
    }

    const int knob = knobAt (x, y);
    if (knob >= 0)
    {
        beginGesture (knobs[knob].param);

        // double click resets to default
        if (lastClickKnob == knob && (time - lastClickTime) < 0.45)
        {
            setParam (knobs[knob].param, crybaby::parameterDefault (knobs[knob].param));
            lastClickKnob = -1;
        }
        else
        {
            lastClickKnob = knob;
            lastClickTime = time;
        }

        dragMode = DragMode::Knob;
        dragKnob = knob;
        return true;
    }

    if (inPedalBody (x, y))
    {
        beginGesture (crybaby::kPedal);
        dragMode = DragMode::Pedal;
        return true;
    }

    return false;
}

bool PedalUI::mouseMove (int x, int y)
{
    if (dragMode == DragMode::None)
        return false;

    fineDrag = (GetKeyState (VK_SHIFT) & 0x8000) != 0;
    const float dy = static_cast<float> (lastY - y) / uiScale;
    lastX = x;
    lastY = y;

    if (dragMode == DragMode::Pedal)
    {
        nudgeParam (crybaby::kPedal, dy * (fineDrag ? 0.0012f : 0.0042f));
        return true;
    }

    if (dragMode == DragMode::Knob && dragKnob >= 0)
    {
        nudgeParam (knobs[dragKnob].param, dy * (fineDrag ? 0.0012f : 0.0042f));
        return true;
    }

    return false;
}

bool PedalUI::mouseUp()
{
    const bool wasDragging = dragMode != DragMode::None;
    dragMode = DragMode::None;
    dragKnob = -1;
    endGesture();
    return wasDragging;
}

bool PedalUI::mouseWheel (int x, int y, int wheelDelta)
{
    if (wheelDelta == 0)
        return false;

    fineDrag = (GetKeyState (VK_SHIFT) & 0x8000) != 0;
    const float step = (fineDrag ? 0.004f : 0.02f) * (static_cast<float> (wheelDelta) / 120.0f);

    const int knob = knobAt (x, y);
    if (knob >= 0)
    {
        beginGesture (knobs[knob].param);
        nudgeParam (knobs[knob].param, step);
        endGesture();
        return true;
    }

    if (inPedalBody (x, y))
    {
        beginGesture (crybaby::kPedal);
        nudgeParam (crybaby::kPedal, step * 0.5f);
        endGesture();
        return true;
    }

    return false;
}

bool PedalUI::tick (double deltaSeconds)
{
    time += deltaSeconds;

    const float target = param (crybaby::kPedal);
    const float prev = displayPedal;
    const float coeff = 1.0f - std::exp (-static_cast<float> (deltaSeconds) / 0.045f);
    displayPedal += (target - displayPedal) * coeff;

    return std::fabs (displayPedal - prev) > 0.0004f;
}

} // namespace ui







