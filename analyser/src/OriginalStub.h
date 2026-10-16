// Minimal stand-ins for the few JUCE types the ORIGINAL Airwindows Meter 0.5.5 scoring code touches.
// They live inside namespace f8korig, so the original code can be compiled unchanged (it says juce::String,
// juce::Graphics and so on) without clashing with the real JUCE in the rest of the analyser.
// Drawing calls do nothing. Colour keeps JUCE's 8-bit behaviour so the original "directions" text comes out the same.
#pragma once
#include <cmath>
#include <string>
#include <algorithm>
#include <cstdint>

namespace f8korig {
namespace juce {

struct String {
    std::string s;
    String() {}
    String(const char* c) : s(c) {}
    String(int i) : s(std::to_string(i)) {}
    String operator+(const String& o) const { String r; r.s = s + o.s; return r; }
    String operator+(const char* c) const { String r; r.s = s + c; return r; }
    int compare(const char* c) const { int r = s.compare(c); return r < 0 ? -1 : (r > 0 ? 1 : 0); }
    bool operator==(const String& o) const { return s == o.s; }
    const std::string& toStdString() const { return s; }
};
inline String operator+(const char* a, const String& b) { String r(a); r.s += b.s; return r; }

struct Colour {
    uint8_t r = 0, g = 0, b = 0, a = 255;
    Colour() {}
    Colour(uint32_t argb) : r((argb >> 16) & 255), g((argb >> 8) & 255), b(argb & 255), a((argb >> 24) & 255) {}
    Colour(int R, int G, int B) : r((uint8_t)R), g((uint8_t)G), b((uint8_t)B) {}
    static uint8_t toByte(float v) { int i = (int)std::lround(v * 255.0f); return (uint8_t)std::clamp(i, 0, 255); }
    static Colour fromFloatRGBA(float R, float G, float B, float A) { Colour c; c.r = toByte(R); c.g = toByte(G); c.b = toByte(B); c.a = toByte(A); return c; }
    float getFloatRed() const { return r / 255.0f; }
    float getFloatGreen() const { return g / 255.0f; }
    float getFloatBlue() const { return b / 255.0f; }
    float getLightness() const { const int hi = std::max({r, g, b}), lo = std::min({r, g, b}); return ((float)(hi + lo) / 2.0f) / 255.0f; }
    float getHue() const { //JUCE's HSB hue, 0-1
        const int hi = std::max({r, g, b}), lo = std::min({r, g, b});
        if (hi <= 0 || hi == lo) return 0.0f;
        const float invDiff = 1.0f / (float)(hi - lo);
        const float red = (float)(hi - r) * invDiff, green = (float)(hi - g) * invDiff, blue = (float)(hi - b) * invDiff;
        float hue;
        if (r == hi) hue = blue - green; else if (g == hi) hue = 2.0f + red - blue; else hue = 4.0f + green - red;
        hue *= 1.0f / 6.0f;
        if (hue < 0.0f) hue += 1.0f;
        return hue;
    }
    Colour interpolatedWith(Colour o, float p) const {
        Colour c; p = std::clamp(p, 0.0f, 1.0f);
        c.r = (uint8_t)(r + (int)std::lround((o.r - r) * p)); c.g = (uint8_t)(g + (int)std::lround((o.g - g) * p));
        c.b = (uint8_t)(b + (int)std::lround((o.b - b) * p)); c.a = (uint8_t)(a + (int)std::lround((o.a - a) * p)); return c;
    }
    bool operator!=(const Colour& o) const { return r != o.r || g != o.g || b != o.b || a != o.a; }
    bool operator==(const Colour& o) const { return !(*this != o); }
};
namespace Colours {
    static const Colour white(255, 255, 255), black(0, 0, 0), lightgrey(211, 211, 211), darkgrey(85, 85, 85), grey(128, 128, 128), red(255, 0, 0);
}
struct ColourGradient { static ColourGradient vertical(Colour, float, Colour, float) { return {}; } void addColour(double, Colour) {} };
struct FillType { FillType() {} FillType(const ColourGradient&) {} FillType(Colour) {} };
struct Justification { enum Flags { topLeft, topRight, centredTop, centred, centredBottom, bottomLeft, centredLeft, centredRight }; Justification(Flags) {} };
struct MouseEvent { int getNumberOfClicks() const { return 1; } };
namespace ResizableWindow { enum { backgroundColourId }; }

struct Graphics { //every drawing call is a no-op: only the scoring side effects of paint() matter here
    template <typename... T> void fillRect(T...) {}
    template <typename... T> void drawText(T...) {}
    void setColour(Colour) {}
    void setOpacity(float) {}
    template <typename T> void setFont(T) {}
    void fillAll() {}
    void fillAll(Colour) {}
    void setFillType(const FillType&) {}
};

struct Component {
    virtual ~Component() {}
    virtual void paint(Graphics&) {}
    virtual void mouseDown(const MouseEvent&) {}
    int w = 1058, h = 698;
    int getWidth() const { return w; }
    int getHeight() const { return h; }
    void repaint() {}
    Colour findColour(int) const { return Colours::lightgrey; }
};

} // namespace juce
} // namespace f8korig
