//-----------------------------------------------------------------------------
// CryBabyWah - VST3 editor view (Win32 window hosting the pedal UI)
//-----------------------------------------------------------------------------
#include "WahPlugin.h"

#include "pluginterfaces/gui/iplugview.h"
#include "../ui/PedalUI.h"

#include <windows.h>

#include <atomic>
#include <cstring>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

using namespace Steinberg;

namespace crybaby
{

namespace
{
constexpr UINT_PTR kTimerId = 1;
const wchar_t* kWindowClass = L"CryBabyWahPedalView";

void registerWindowClass (WNDPROC proc)
{
    static bool registered = false;
    if (registered)
        return;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof (wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW (nullptr);
    wc.hCursor = LoadCursorW (nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClass;

    RegisterClassExW (&wc);
    registered = true;
}

constexpr float kMinZoom = 0.7f;
constexpr float kMaxZoom = 3.0f;

inline float clampScale (float s)
{
    return s < kMinZoom ? kMinZoom : (s > kMaxZoom ? kMaxZoom : s);
}

// uniform zoom that makes the 440 x 580 layout fit the given window exactly
inline float fitScale (int w, int h)
{
    float s = static_cast<float> (w) / static_cast<float> (ui::kLogicalWidth);
    const float sy = static_cast<float> (h) / static_cast<float> (ui::kLogicalHeight);
    if (sy < s)
        s = sy;
    return s > 0.05f ? s : 0.05f;
}

// DPI scale of the monitor the host window lives on (1.0 = 96 dpi).
// GetDpiForWindow is resolved at run time so the plug-in still loads on
// systems where user32.dll does not export it.
float queryDpiScale (HWND hwnd)
{
    using GetDpiForWindowFn = UINT (WINAPI*) (HWND);
    using GetDpiForSystemFn = UINT (WINAPI*)();

    HMODULE user32 = GetModuleHandleW (L"user32.dll");
    if (user32 != nullptr)
    {
        if (hwnd != nullptr)
        {
            auto getForWindow = reinterpret_cast<GetDpiForWindowFn> (GetProcAddress (user32, "GetDpiForWindow"));
            if (getForWindow != nullptr)
            {
                const UINT dpi = getForWindow (hwnd);
                if (dpi >= 96)
                    return static_cast<float> (dpi) / 96.0f;
            }
        }

        auto getForSystem = reinterpret_cast<GetDpiForSystemFn> (GetProcAddress (user32, "GetDpiForSystem"));
        if (getForSystem != nullptr)
        {
            const UINT dpi = getForSystem();
            if (dpi >= 96)
                return static_cast<float> (dpi) / 96.0f;
        }
    }

    return 1.0f;
}
} // namespace

//-----------------------------------------------------------------------------
class PedalView final : public IPlugView
{
public:
    explicit PedalView (WahPlugin* controllerToUse) : controller (controllerToUse)
    {
        dpiScale = clampScale (queryDpiScale (nullptr));
        viewW = static_cast<int> (ui::kLogicalWidth * dpiScale + 0.5f);
        viewH = static_cast<int> (ui::kLogicalHeight * dpiScale + 0.5f);
        pedal.setSize (viewW, viewH, dpiScale);
        pedal.setParamReader ([this] (int i) { return controller->getParam (i); });
        pedal.setParamWriter ([this] (int i, float v) { controller->setParamFromUI (i, v); });
        pedal.setGestureCallbacks ([this] (int i) { controller->beginEdit (i); },
                                   [this] (int i) { controller->endEdit (i); });

        for (int i = 0; i < kNumParameters; ++i)
            lastParams[i] = controller->getParam (i);

        // Keep the controller alive for as long as the view exists: a host may
        // release the component before destroying the editor window.
        controller->addRef();
    }

    ~PedalView()
    {
        if (hwnd != nullptr)
        {
            if (IsWindow (hwnd))
            {
                KillTimer (hwnd, kTimerId);
                DestroyWindow (hwnd);
            }
            hwnd = nullptr;
        }

        releaseBuffer();
        controller->viewDestroyed (this);
        controller->release(); // may destroy the controller: do not touch it afterwards
    }

    //--- FUnknown ---------------------------------------------------------
    tresult PLUGIN_API queryInterface (const TUID _iid, void** obj) override
    {
        if (obj == nullptr)
            return kInvalidArgument;
        *obj = nullptr;

        if (FUnknownPrivate::iidEqual (_iid, FUnknown::iid)
            || FUnknownPrivate::iidEqual (_iid, IPlugView::iid))
        {
            addRef();
            *obj = static_cast<IPlugView*> (this);
            return kResultOk;
        }

        return kNoInterface;
    }

    uint32 PLUGIN_API addRef() override { return static_cast<uint32> (++refCount); }

    uint32 PLUGIN_API release() override
    {
        const int32 count = --refCount;
        if (count == 0)
        {
            delete this;
            return 0;
        }
        return static_cast<uint32> (count);
    }

    //--- IPlugView --------------------------------------------------------
    tresult PLUGIN_API isPlatformTypeSupported (FIDString type) override
    {
        if (type != nullptr && std::strcmp (type, kPlatformTypeHWND) == 0)
            return kResultTrue;
        return kResultFalse;
    }

    tresult PLUGIN_API attached (void* parent, FIDString type) override
    {
        if (parent == nullptr || hwnd != nullptr)
            return kResultFalse;

        if (type == nullptr || std::strcmp (type, kPlatformTypeHWND) != 0)
            return kResultFalse;

        registerWindowClass (&PedalView::wndProc);

        // follow the DPI of the monitor the editor is opened on
        const float parentScale = queryDpiScale (static_cast<HWND> (parent));
        if (parentScale > 0.0f)
        {
            dpiScale = clampScale (parentScale);
            viewW = static_cast<int> (ui::kLogicalWidth * dpiScale + 0.5f);
            viewH = static_cast<int> (ui::kLogicalHeight * dpiScale + 0.5f);
        }

        // a host may restore a previously resized editor: adopt the size of the
        // parent window when it looks like a real editor area
        RECT parentRect = {};
        if (GetClientRect (static_cast<HWND> (parent), &parentRect))
        {
            const int pw = parentRect.right - parentRect.left;
            const int ph = parentRect.bottom - parentRect.top;
            if (pw >= 240 && ph >= 240 && pw <= 4096 && ph <= 4096)
            {
                viewW = pw;
                viewH = ph;
                dpiScale = fitScale (pw, ph);
            }
        }

        pedal.setSize (viewW, viewH, dpiScale);

        hwnd = CreateWindowExW (0, kWindowClass, L"CB-95 Wah", WS_CHILD | WS_VISIBLE, 0, 0,
                                viewW, viewH, static_cast<HWND> (parent), nullptr,
                                GetModuleHandleW (nullptr), this);

        if (hwnd == nullptr)
            return kResultFalse;

        SetTimer (hwnd, kTimerId, 30, nullptr);
        return kResultOk;
    }

    tresult PLUGIN_API removed() override
    {
        if (hwnd != nullptr)
        {
            KillTimer (hwnd, kTimerId);
            DestroyWindow (hwnd);
            hwnd = nullptr;
        }
        releaseBuffer();
        return kResultOk;
    }

    tresult PLUGIN_API onWheel (float distance) override
    {
        // used by hosts that forward touchpad / wheel gestures
        if (hwnd == nullptr)
            return kResultFalse;

        const int delta = static_cast<int> (distance * 120.0f);
        if (pedal.mouseWheel (lastMouseX, lastMouseY, delta))
        {
            InvalidateRect (hwnd, nullptr, FALSE);
            return kResultTrue;
        }
        return kResultFalse;
    }

    tresult PLUGIN_API onKeyDown (char16 /*key*/, int16 /*keyCode*/, int16 /*modifiers*/) override
    {
        return kResultFalse;
    }

    tresult PLUGIN_API onKeyUp (char16 /*key*/, int16 /*keyCode*/, int16 /*modifiers*/) override
    {
        return kResultFalse;
    }

    tresult PLUGIN_API getSize (ViewRect* size) override
    {
        if (size == nullptr)
            return kInvalidArgument;

        size->left = 0;
        size->top = 0;
        size->right = viewW;
        size->bottom = viewH;
        return kResultOk;
    }

    tresult PLUGIN_API onSize (ViewRect* newSize) override
    {
        if (newSize == nullptr || hwnd == nullptr)
            return kResultFalse;

        const int w = newSize->right - newSize->left;
        const int h = newSize->bottom - newSize->top;
        if (w <= 0 || h <= 0)
            return kResultFalse;

        SetWindowPos (hwnd, nullptr, 0, 0, w, h, SWP_NOZORDER | SWP_NOMOVE);

        viewW = w;
        viewH = h;

        // the UI is drawn in a fixed 440 x 580 logical space; scale it with the
        // window (the aspect ratio is enforced in checkSizeConstraint)
        dpiScale = fitScale (w, h);
        pedal.setSize (w, h, dpiScale);

        releaseBuffer();
        InvalidateRect (hwnd, nullptr, FALSE);
        return kResultOk;
    }

    tresult PLUGIN_API onFocus (TBool /*state*/) override { return kResultFalse; }

    tresult PLUGIN_API setFrame (IPlugFrame* newFrame) override
    {
        frame = newFrame;
        return kResultOk;
    }

    tresult PLUGIN_API canResize() override { return kResultTrue; }

    tresult PLUGIN_API checkSizeConstraint (ViewRect* rect) override
    {
        if (rect == nullptr)
            return kInvalidArgument;

        const int w = rect->right - rect->left;
        const int h = rect->bottom - rect->top;
        if (w <= 0 || h <= 0)
            return kResultFalse;

        // keep the artwork proportions: fit the requested box, clamp the zoom
        float scale = static_cast<float> (w) / static_cast<float> (ui::kLogicalWidth);
        const float sy = static_cast<float> (h) / static_cast<float> (ui::kLogicalHeight);
        if (sy < scale)
            scale = sy;
        scale = clampScale (scale);

        rect->right = rect->left + static_cast<int> (ui::kLogicalWidth * scale + 0.5f);
        rect->bottom = rect->top + static_cast<int> (ui::kLogicalHeight * scale + 0.5f);
        return kResultTrue;
    }

private:
    static LRESULT CALLBACK wndProc (HWND h, UINT msg, WPARAM w, LPARAM l)
    {
        auto* self = reinterpret_cast<PedalView*> (GetWindowLongPtrW (h, GWLP_USERDATA));

        if (msg == WM_CREATE)
        {
            auto* cs = reinterpret_cast<CREATESTRUCTW*> (l);
            self = static_cast<PedalView*> (cs->lpCreateParams);
            SetWindowLongPtrW (h, GWLP_USERDATA, reinterpret_cast<LONG_PTR> (self));
        }

        if (self != nullptr)
            return self->handleMessage (h, msg, w, l);

        return DefWindowProcW (h, msg, w, l);
    }

    LRESULT handleMessage (HWND h, UINT msg, WPARAM w, LPARAM l)
    {
        switch (msg)
        {
            case WM_ERASEBKGND:
                return 1;

            case WM_PAINT:
            {
                PAINTSTRUCT ps;
                HDC dc = BeginPaint (h, &ps);
                paint (dc);
                EndPaint (h, &ps);
                return 0;
            }

            case WM_LBUTTONDOWN:
            {
                SetFocus (h);
                SetCapture (h);
                const int x = static_cast<short> (LOWORD (l));
                const int y = static_cast<short> (HIWORD (l));
                if (pedal.mouseDown (x, y))
                    InvalidateRect (h, nullptr, FALSE);
                return 0;
            }

            case WM_MOUSEMOVE:
            {
                const int x = static_cast<short> (LOWORD (l));
                const int y = static_cast<short> (HIWORD (l));
                lastMouseX = x;
                lastMouseY = y;

                // take focus so the wheel reaches us even before the first click
                if (GetFocus() != h)
                    SetFocus (h);

                if (pedal.mouseMove (x, y))
                    InvalidateRect (h, nullptr, FALSE);
                return 0;
            }

            case WM_MOUSEWHEEL:
            {
                POINT pt = { static_cast<short> (LOWORD (l)), static_cast<short> (HIWORD (l)) };
                ScreenToClient (h, &pt);
                const int delta = GET_WHEEL_DELTA_WPARAM (w);
                if (pedal.mouseWheel (pt.x, pt.y, delta))
                    InvalidateRect (h, nullptr, FALSE);
                return 0;
            }

            case WM_LBUTTONUP:
            {
                const bool changed = pedal.mouseUp();
                if (GetCapture() == h)
                    ReleaseCapture();
                if (changed)
                    InvalidateRect (h, nullptr, FALSE);
                return 0;
            }

            case WM_TIMER:
                onTimer();
                return 0;

            case WM_PRINTCLIENT:
                // used by host screenshot helpers and by our test host
                paint (reinterpret_cast<HDC> (w));
                return 0;

            case WM_DESTROY:
                KillTimer (h, kTimerId);
                return 0;

            default:
                break;
        }

        return DefWindowProcW (h, msg, w, l);
    }

    void onTimer()
    {
        const bool animated = pedal.tick (0.03);
        bool changed = animated;

        for (int i = 0; i < kNumParameters && ! changed; ++i)
        {
            const float v = controller->getParam (i);
            if (v != lastParams[i])
            {
                lastParams[i] = v;
                changed = true;
            }
        }

        if (changed && hwnd != nullptr)
            InvalidateRect (hwnd, nullptr, FALSE);
    }

    void ensureBuffer (HDC reference, int w, int h)
    {
        if (memDC != nullptr && bmpWidth == w && bmpHeight == h)
            return;

        releaseBuffer();

        memDC = CreateCompatibleDC (reference);
        if (memDC == nullptr)
            return;

        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h; // top-down
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        memBitmap = CreateDIBSection (reference, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (memBitmap == nullptr)
        {
            DeleteDC (memDC);
            memDC = nullptr;
            return;
        }

        oldBitmap = static_cast<HBITMAP> (SelectObject (memDC, memBitmap));
        bmpWidth = w;
        bmpHeight = h;
    }

    void releaseBuffer()
    {
        if (memDC != nullptr)
        {
            if (oldBitmap != nullptr)
                SelectObject (memDC, oldBitmap);
            DeleteDC (memDC);
            memDC = nullptr;
            oldBitmap = nullptr;
        }
        if (memBitmap != nullptr)
        {
            DeleteObject (memBitmap);
            memBitmap = nullptr;
        }
        bmpWidth = bmpHeight = 0;
    }

    void paint (HDC dc)
    {
        RECT rc;
        GetClientRect (hwnd, &rc);
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;

        if (w <= 0 || h <= 0)
            return;

        ensureBuffer (dc, w, h);

        if (memDC != nullptr)
        {
            {
                Gdiplus::Graphics g (memDC);
                g.Clear (Gdiplus::Color (255, 8, 9, 11));
                pedal.paint (g);
            }
            BitBlt (dc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
        }
        else
        {
            Gdiplus::Graphics g (dc);
            pedal.paint (g);
        }
    }

    WahPlugin* controller;
    std::atomic<int32> refCount { 1 };
    IPlugFrame* frame = nullptr;
    float dpiScale = 1.0f;
    int viewW = ui::kLogicalWidth;
    int viewH = ui::kLogicalHeight;
    int lastMouseX = 0;
    int lastMouseY = 0;

    HWND hwnd = nullptr;
    HDC memDC = nullptr;
    HBITMAP memBitmap = nullptr;
    HBITMAP oldBitmap = nullptr;
    int bmpWidth = 0;
    int bmpHeight = 0;

    ui::PedalUI pedal;
    float lastParams[kNumParameters] = {};
};

//-----------------------------------------------------------------------------
IPlugView* createPedalView (WahPlugin* controller)
{
    return new PedalView (controller);
}

} // namespace crybaby




