#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "chris/Current.h"
#include "Resampler.h"
//meterDARK's tune settings: 0-11 the hint and Detail controls, 12 = "grade" (1 = the Detail decay and fill gate also change the grade and tone colours),
//13 = anchor rate (0 off = the host's rate, 0.5 = 44.1 kHz, 1 = 96 kHz), 14 = level mode (0 original, 0.5 normalised, 1 mix),
//15 = "input at" (LUFS = -30 + 24 x value, default -14), 16 = "score as if at" (default -11)
static constexpr int kTuneCount = 19; //tune17 = hide hints (0 = shown), tune18 = the dark <-> bright balance point
static constexpr float kTuneInputAtDefault = 16.0f/24.0f, kTuneAsIfAtDefault = 19.0f/24.0f;

#if (MSVC)
#include "ipps.h"
#endif

class PluginProcessor : public juce::AudioProcessor, public juce::AudioProcessorParameter::Listener
{
public:
    PluginProcessor();
    ~PluginProcessor() override;
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool starting) override;
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;
    bool supportsDoublePrecisionProcessing() const override;
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    // updateTrackProperties may be called by the host if it feels like it
    // this method calls a similar one in the editor class that updates the editor
    void updateTrackProperties(const TrackProperties& properties) override;
    void updatePluginSize(int w, int h);
    
    TrackProperties trackProperties;
    int pluginWidth;
    int pluginHeight;

   //now we can declare variables used in the audio thread
    
    enum Parameters
    {
        KNOBA,
    };
    static constexpr int n_params = 0;
    //std::array<juce::AudioParameterFloat *, n_params> params;
    //This is where we're defining things that go into the plugin's interface.
    
    struct UIToAudioMessage
    {
        enum What
        {
            NEW_VALUE,
            BEGIN_EDIT,
            END_EDIT,
            FLIP
        } what{NEW_VALUE};
        Parameters which;
        float newValue = 0.0;
    };
    //This is things the interface can tell the audio thread about.
    
    struct AudioToUIMessage
    {
        enum What
        {
            NEW_VALUE,
            PEAK_LEFT,
            PEAK_RIGHT,
            SLEW_LEFT,
            SLEW_RIGHT,
            ZERO_LEFT,
            ZERO_RIGHT,
            BEAM_LEFT,
            BEAM_RIGHT,
            DEPT_LEFT,
            DEPT_RIGHT,
            INCREMENT
        } what{NEW_VALUE};
        Parameters which;
        float newValue = 0.0;
    };
    //This is kinds of information the audio thread can give the interface.
    
    template <typename T, int qSize = 4096> class LockFreeQueue //4096
    {
      public:
        LockFreeQueue() : af(qSize) {}
        bool push(const T &ad)
        {
            auto ret = false;
            int start1, size1, start2, size2;
            af.prepareToWrite(1, start1, size1, start2, size2);
            if (size1 > 0)
            {
                dq[start1] = ad;
                ret = true;
            }
            af.finishedWrite(size1 + size2);
            return ret;
        }
        bool pop(T &ad)
        {
            bool ret = false;
            int start1, size1, start2, size2;
            af.prepareToRead(1, start1, size1, start2, size2);
            if (size1 > 0)
            {
                ad = dq[start1];
                ret = true;
            }
            af.finishedRead(size1 + size2);
            return ret;
        }
        juce::AbstractFifo af;
        T dq[qSize];
    };
    
    LockFreeQueue<UIToAudioMessage> uiToAudio;
    LockFreeQueue<AudioToUIMessage> audioToUI;

    double peakLeft = 0.0;
    double peakRight = 0.0;
    double slewLeft = 0.0;
    double slewRight = 0.0;
    double previousLeft = 0.0;
    double previousRight = 0.0;
    double zeroLeft = 0.0;
    double zeroRight = 0.0;
    double longestZeroLeft = 0.0;
    double longestZeroRight = 0.0;
    double beamLeft = 0.0;
    double beamRight = 0.0;
    double departureLeft = 0.0;
    double departureRight = 0.0;
    bool wasPositiveL = false;
    bool wasPositiveR = false;
    int windowCount = 0;
    bool audioPlaying = true;
    bool playheadMoving = false;
    float hintTune[kTuneCount] = {0.30f, 0.30f, 0.30f, 0.30f, 0.30f, 0.30f, 0.00f, 0.00f, 0.10f, 0.50f, 0.30f, 0.50f, 0.00f, 1.00f, 0.00f, kTuneInputAtDefault, kTuneAsIfAtDefault, 0.00f, 0.50f}; //where each hint switches on (see the meter's tune controls), saved with the project
    chris::PluginProcessor chrisAudio; //Chris's original audio thread (measures each window and sends it to the display)
    float peakTrimDb = 0.0f; //display-only level trim for the peak meter, same idea as slewTrimDb
    float slewTrimDb = 0.0f; //display-only zoom for the slew meter (0 = original). Saved with the project, never touches the audio or the scoring
    //YOUR DEFAULT PRESET: the tune values and the two display trims, kept in a small file in the user's application data folder. New instances
    //start from it, and a project that lacks a value falls back to it. (The numbers in the code are the factory ones, used when there is no file.)
    float userTune[kTuneCount] = {0.30f, 0.30f, 0.30f, 0.30f, 0.30f, 0.30f, 0.00f, 0.00f, 0.10f, 0.50f, 0.30f, 0.50f, 0.00f, 1.00f, 0.00f, kTuneInputAtDefault, kTuneAsIfAtDefault, 0.00f, 0.50f};
    float userPeakTrim = 0.0f;
    float userSlewTrim = 0.0f;
    //WHAT CHRIS'S AUDIO LOOP IS GIVEN: a scratch copy of the audio, never the host's buffer (the host's audio passes through untouched).
    //With the anchor rate and the level mode both off the copy is exact, so he measures the same as ever. The anchor rate converts the audio
    //to 44.1 or 96 kHz first (so a file's sample rate no longer changes the reading). The level mode applies a gain before his loop: normalised =
    //fixed gain from "input at" to "score as if at"; mix = a slowly floating gain that keeps the running peak ceiling at -1 dBFS (max +24 dB).
    //Each window message carries the gain, and the display takes it off again, so what you see stays at the real level.
    void requestLevelReset() {levelResetPending = true;}
    static double tuneToLufs(float v) {return -30.0 + std::round(24.0*(double)v);}
    static juce::File userDefaultsFile();
    void loadUserDefaults();
    void saveUserDefaults(const float* tuneIn, float peakTrimIn, float slewTrimIn);

private:
    template <typename T> void feedChris(juce::AudioBuffer<T>& buffer, juce::MidiBuffer& midi);
    void prepareAnchors(double sampleRate, int samplesPerBlock);
    double levelGain(int mode, const float* l, const float* r, int count, double rate);
    juce::AudioBuffer<float> scratchF;
    juce::AudioBuffer<double> scratchD;
    f8kdsp::Resampler anchorL[2], anchorR[2]; //0 = to 44.1 kHz, 1 = to 96 kHz
    std::vector<float> rsOutL, rsOutR, convL, convR;
    int lastAnchorSel = 0;
    double mixCeiling = 0.0; int mixHold = 0;
    std::atomic<bool> levelResetPending{false};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
