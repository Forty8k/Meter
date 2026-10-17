// The real JUCE, except for the three things Chris's meter must not touch in Meter F8K: its Graphics (its drawing goes
// nowhere, Meter F8K draws its own display), and the Component it is built on (it is never on screen).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <iostream>

namespace f8kmix {
namespace juce {
using namespace ::juce;
struct Graphics {
    template <typename... T> void fillRect(T...) {}
    template <typename... T> void drawText(T...) {}
    template <typename... T> void drawFittedText(T...) {}
    template <typename... T> void drawImage(T...) {}
    template <typename... T> void drawImageWithin(T...) {}
    template <typename... T> void fillAll(T...) {}
    template <typename T> void setColour(T) {}
    void setOpacity(float) {}
    template <typename T> void setFont(T) {}
    template <typename T> void setFillType(const T&) {}
    template <typename T> void setGradientFill(const T&) {}
    template <typename T> void setImageResamplingQuality(T) {}
};
struct Component {
    virtual ~Component() = default;
    virtual void paint(Graphics&) {}
    virtual void mouseDown(const ::juce::MouseEvent&) {}
    int w = 1058, h = 698;
    int getWidth() const { return w; }
    int getHeight() const { return h; }
    void repaint() {}
    void setOpaque(bool) {}
    ::juce::Colour findColour(int) const { return ::juce::Colours::lightgrey; }
};
} // namespace juce
} // namespace f8kmix
