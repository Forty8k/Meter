#pragma once

#include "AirwindowsUI.h"
#include "PluginProcessor.h"
#include "BinaryData.h"


//==============================================================================
class PluginEditor : public juce::AudioProcessorEditor, juce::Slider::Listener
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void sliderValueChanged(juce::Slider *) override;
    void sliderDragStarted(juce::Slider *) override;
    void sliderDragEnded(juce::Slider *) override;
    void sliderDragInternal(juce::Slider *, bool started);
    void updateTrackProperties();
    void updatePluginSize();
    void idle();
    void resetChris();
    void takeScores();

    struct IdleTimer : juce::Timer
        {
            IdleTimer(PluginEditor *ed) : ed(ed) {}
            ~IdleTimer() = default;
            void timerCallback() override { ed->idle(); }
            PluginEditor *ed;
        };


private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    PluginProcessor& processorRef; //quick ref
    
   std::unique_ptr<IdleTimer> idleTimer;
   float lastPeakTrimSeen = 0.0f;
   float lastTuneSeen[12] = {0.30f, 0.30f, 0.30f, 0.30f, 0.30f, 0.70f, 0.30f, 0.70f, 0.10f, 0.50f, 0.30f, 0.50f};
   float lastSlewTrimSeen = 0.0f; //keeps the on-screen slew trim and the saved one in step, in both directions
    AirwindowsLookAndFeel airwindowsLookAndFeel;
    AirwindowsMeter meter;
    chris::AirwindowsMeter chrisMeter;   //Chris's original meter, never on screen: it does the scoring
    chris::juce::Graphics noDraw;        //where its drawing goes (nowhere)
    
    juce::Colour hostTrackColour = juce::Colours::lightgrey;
    juce::String hostTrackName = juce::String();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
