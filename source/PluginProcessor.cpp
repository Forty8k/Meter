#include "PluginProcessor.h"
#include <type_traits>
#include "PluginEditor.h"
#ifndef M_PI
#  define M_PI (3.14159265358979323846)
#endif
#ifndef M_PI_2
#  define M_PI_2 (1.57079632679489661923)
#endif

//==============================================================================
PluginProcessor::PluginProcessor():AudioProcessor (
                    BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
){
    // (internal ID, how it's shown in DAW generic view, {min, max}, default)
    chrisAudio.host = this;
    loadUserDefaults(); //a new instance starts from the saved default preset, if there is one
    for (int i = 0; i < kTuneCount; ++i) hintTune[i] = userTune[i];
    peakTrimDb = userPeakTrim;
    slewTrimDb = userSlewTrim;
 }

juce::File PluginProcessor::userDefaultsFile()
{
    const auto data = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    const auto f = data.getChildFile("meterDARK").getChildFile("defaults.xml");
    const auto old = data.getChildFile("Meter F8K").getChildFile("defaults.xml"); //from before the rename: carried over once
    if (!f.existsAsFile() && old.existsAsFile()) { f.getParentDirectory().createDirectory(); old.copyFileTo(f); }
    return f;
}

void PluginProcessor::loadUserDefaults()
{
    const juce::File f = userDefaultsFile();
    if (!f.existsAsFile()) return;
    std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(f));
    if (xml == nullptr || !xml->hasTagName("meterdefaults")) return;
    for (int i = 0; i < kTuneCount; ++i) {
        const double v = xml->getDoubleAttribute(juce::String("tune")+juce::String(i), (double)userTune[i]);
        if (v >= 0.0 && v <= 1.0) userTune[i] = (float)v;
    }
    const int fileVer = xml->getIntAttribute("ver", 1);
    if (fileVer < 2) userTune[4] = 1.0f - userTune[4]; //older file: the dark level used to be measured the other way (dark>)
    if (fileVer < 3) {userTune[0] = 0.30f; userTune[1] = 0.30f; userTune[9] = 0.50f; userTune[10] = 0.30f;} //older file: the weight settings measured something else, so they restart from the new defaults
    else if (fileVer < 4) { //version 3 used a narrower scale with balance at .45: stretch it so the same trigger points are kept
        userTune[0] = fmin(userTune[0]*1.765f, 0.98f); userTune[1] = fmin(userTune[1]*1.765f, 0.98f);
        userTune[9] = fmin(fmax(0.5f + (userTune[9] - 0.45f)*1.765f, 0.0f), 0.98f);
    }
    if (fileVer < 5) userTune[5] = fmin(1.0f - powf(1.0f - userTune[5], 3.375f), 0.98f); //version 5 spread the bright reading (13.5 instead of 4): same trigger point, new number
    if (fileVer < 6) {userTune[2] = 0.30f; userTune[3] = 0.30f; userTune[11] = 0.50f;} //older file: the dynamics settings measured something else, so they restart from the new defaults
    if (fileVer < 7) {userTune[6] = 0.0f; userTune[7] = 0.0f; userTune[12] = 0.0f;} //version 7: Detail decay and fill gate start at 0 (stock)
    if (fileVer < 8) {userTune[13] = 0.0f; userTune[14] = 0.0f; userTune[15] = kTuneInputAtDefault; userTune[16] = kTuneAsIfAtDefault;} //version 8: anchor rate and level mode start off (stock)
    if (fileVer < 9) userTune[13] = 1.0f; //version 9: the 96 kHz anchor rate is on by default
    if (fileVer < 10) userTune[17] = 0.0f; //version 10: hide hints (tune17) starts off, so the hints show
    const double pt = xml->getDoubleAttribute("peaktrim", 0.0);
    const double st = xml->getDoubleAttribute("slewtrim", 0.0);
    userPeakTrim = (pt <= 0.0 && pt >= -24.0) ? (float)pt : 0.0f;
    userSlewTrim = (st <= 0.0 && st >= -24.0) ? (float)st : 0.0f;
}

void PluginProcessor::saveUserDefaults(const float* tuneIn, float peakTrimIn, float slewTrimIn)
{
    for (int i = 0; i < kTuneCount; ++i) userTune[i] = tuneIn[i];
    userPeakTrim = peakTrimIn;
    userSlewTrim = slewTrimIn;
    juce::XmlElement xml("meterdefaults");
    for (int i = 0; i < kTuneCount; ++i) xml.setAttribute(juce::String("tune")+juce::String(i), (double)userTune[i]);
    xml.setAttribute("ver", 10); //version 10: tune17 = hide hints (0 = shown, stock). Version 9: the 96 kHz anchor rate is on by default. Version 8: tune13 anchor rate, tune14 level mode, tune15/16 their LUFS (0 = stock). Version 7: Detail decay/fill gate 0 = stock, tune12 = grade. version 2: dark level reads dark<. Version 3: weight is a mass reading with a balance point. Version 4: the mass scale is stretched and balanced reads .50. Version 5: the bright reading is spread so its threshold sits like squashed (bright>.70). Version 6: dynamics is a spread reading with a balance point (tune 2, 3, 11)
    xml.setAttribute("peaktrim", (double)userPeakTrim);
    xml.setAttribute("slewtrim", (double)userSlewTrim);
    const juce::File f = userDefaultsFile();
    f.getParentDirectory().createDirectory();
    xml.writeTo(f);
}

PluginProcessor::~PluginProcessor() {}
void PluginProcessor::parameterValueChanged(int parameterIndex, float newValue)
{
    AudioToUIMessage msg;
    msg.what = AudioToUIMessage::NEW_VALUE;
    msg.which = (PluginProcessor::Parameters)parameterIndex;
    //msg.newValue = params[parameterIndex]->convertFrom0to1(newValue);
    audioToUI.push(msg);
}
void PluginProcessor::parameterGestureChanged(int parameterIndex, bool starting) {}
const juce::String PluginProcessor::getName() const {return JucePlugin_Name;}
bool PluginProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}
bool PluginProcessor::supportsDoublePrecisionProcessing() const
{
   #if JucePlugin_SupportsDoublePrecisionProcessing
    return true;
   #else
    return true;
    //note: I don't know whether that config option is set, so I'm hardcoding it
    //knowing I have enabled such support: keeping the boilerplate stuff tho
    //in case I can sort out where it's enabled as a flag
   #endif
}
bool PluginProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}
bool PluginProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}
double PluginProcessor::getTailLengthSeconds() const {return 0.0;}
int PluginProcessor::getNumPrograms() {return 1;}
int PluginProcessor::getCurrentProgram() {return 0;}
void PluginProcessor::setCurrentProgram (int index) {juce::ignoreUnused (index);}
const juce::String PluginProcessor::getProgramName (int index) {juce::ignoreUnused (index); return {};}
void PluginProcessor::changeProgramName (int index, const juce::String& newName) {juce::ignoreUnused (index, newName);}

//==============================================================================
void PluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    prepareAnchors(sampleRate, samplesPerBlock);
    const int ch = juce::jmax(2, getTotalNumInputChannels(), getTotalNumOutputChannels());
    scratchF.setSize(ch, juce::jmax(samplesPerBlock, 64)*4 + 2048); //room for the biggest conversion up (44.1 -> 96 kHz is x2.2), so the audio thread never allocates
    scratchD.setSize(ch, juce::jmax(samplesPerBlock, 64));
    rsOutL.reserve((size_t)juce::jmax(samplesPerBlock, 64)*4 + 2048);
    rsOutR.reserve((size_t)juce::jmax(samplesPerBlock, 64)*4 + 2048);
    convL.reserve((size_t)juce::jmax(samplesPerBlock, 64)); convR.reserve((size_t)juce::jmax(samplesPerBlock, 64));
    mixCeiling = 0.0; mixHold = 0;
}

void PluginProcessor::prepareAnchors(double sampleRate, int samplesPerBlock)
{
    const double rates[2] = {44100.0, 96000.0};
    for (int k = 0; k < 2; ++k) {
        anchorL[k].setRates(sampleRate, rates[k]);
        anchorR[k].setRates(sampleRate, rates[k]);
        anchorL[k].reserve((size_t)juce::jmax(samplesPerBlock, 64));
        anchorR[k].reserve((size_t)juce::jmax(samplesPerBlock, 64));
    }
    lastAnchorSel = 0;
}
void PluginProcessor::releaseResources() {}
bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this metering code we only support stereo.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}

//==============================================================================

bool PluginProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

//==============================================================================
void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
    std::unique_ptr<juce::XmlElement> xml(new juce::XmlElement("meter"));
    xml->setAttribute("streamingVersion", (int)8524);
    if (pluginWidth < 8 || pluginWidth > 16386) pluginWidth = 1080;
    xml->setAttribute(juce::String("awm_width"), pluginWidth);
    if (pluginHeight < 8 || pluginHeight > 16386) pluginHeight = 720;
    xml->setAttribute(juce::String("awm_height"), pluginHeight);
    xml->setAttribute(juce::String("awm_slewtrim"), (double)slewTrimDb);
    xml->setAttribute(juce::String("awm_peaktrim"), (double)peakTrimDb);
    for (int i = 0; i < kTuneCount; ++i) xml->setAttribute(juce::String("awm_tune")+juce::String(i), (double)hintTune[i]);
    xml->setAttribute("awm_tunever", 10); //version 10: tune17 = hide hints (0 = shown). Version 8: anchor rate and level mode (tune13-16, 0 = stock). Version 7: Detail decay/fill gate 0 = stock, tune12 = grade. version 5: bright reading spread so its level sits like squashed. Version 6: dynamics spread reading (older projects are converted or reset when loaded)
    copyXmlToBinary(*xml, destData);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState.get() != nullptr)
    {
        if (xmlState->hasTagName("meter"))
        {
            auto w = xmlState->getIntAttribute(juce::String("awm_width"));
            if (w < 8 || w > 16386) w = 1080;
            auto h = xmlState->getIntAttribute(juce::String("awm_height"));
            if (h < 8 || h > 16386) h = 720;
            updatePluginSize(w, h);
            slewTrimDb = (float)xmlState->getDoubleAttribute(juce::String("awm_slewtrim"), (double)userSlewTrim);
            if (slewTrimDb > 0.0f || slewTrimDb < -24.0f) slewTrimDb = userSlewTrim;
            peakTrimDb = (float)xmlState->getDoubleAttribute(juce::String("awm_peaktrim"), (double)userPeakTrim);
            if (peakTrimDb > 0.0f || peakTrimDb < -24.0f) peakTrimDb = userPeakTrim;
            for (int i = 0; i < kTuneCount; ++i) {
                hintTune[i] = (float)xmlState->getDoubleAttribute(juce::String("awm_tune")+juce::String(i), (double)userTune[i]);
                if (!(hintTune[i] >= 0.0f && hintTune[i] <= 1.0f)) hintTune[i] = userTune[i];
            }
            const int stateVer = xmlState->getIntAttribute("awm_tunever", 1);
            if (xmlState->hasAttribute("awm_tune4") && stateVer < 2) hintTune[4] = 1.0f - hintTune[4]; //older project: dark> level becomes dark<
            if (xmlState->hasAttribute("awm_tune0") && stateVer < 3) {hintTune[0] = 0.30f; hintTune[1] = 0.30f; hintTune[9] = 0.50f; hintTune[10] = 0.30f;} //older project: weight settings restart from the new defaults
            else if (xmlState->hasAttribute("awm_tune0") && stateVer < 4) { //version 3 used a narrower scale with balance at .45: stretch it so the same trigger points are kept
                hintTune[0] = fmin(hintTune[0]*1.765f, 0.98f); hintTune[1] = fmin(hintTune[1]*1.765f, 0.98f);
                hintTune[9] = fmin(fmax(0.5f + (hintTune[9] - 0.45f)*1.765f, 0.0f), 0.98f);
            }
            if (xmlState->hasAttribute("awm_tune2") && stateVer < 6) {hintTune[2] = 0.30f; hintTune[3] = 0.30f; hintTune[11] = 0.50f;} //older project: dynamics settings restart from the new defaults
            if (stateVer < 7) {hintTune[6] = 0.0f; hintTune[7] = 0.0f; hintTune[12] = 0.0f;} //older project: Detail decay and fill gate start at 0 (stock)
            if (stateVer < 8) {hintTune[13] = 0.0f; hintTune[14] = 0.0f; hintTune[15] = kTuneInputAtDefault; hintTune[16] = kTuneAsIfAtDefault;} //older project: anchor rate and level mode start off (stock)
            if (stateVer < 9) hintTune[13] = 1.0f; //version 9: the 96 kHz anchor rate is on by default
            if (stateVer < 10) hintTune[17] = 0.0f; //version 10: hide hints starts off (the hints show)
            if (xmlState->hasAttribute("awm_tune5") && stateVer < 5) hintTune[5] = fmin(1.0f - powf(1.0f - hintTune[5], 3.375f), 0.98f); //older project: same trigger point on the spread bright scale
        }
        updateHostDisplay();
    }
    //These functions are adapted (simplified) from baconpaul's airwin2rack and all thanks to getting
    //it working shall go there, though sudara or anyone could've spotted that I hadn't done these.
    //baconpaul pointed me to the working versions in airwin2rack, that I needed to see.
}

void PluginProcessor::updateTrackProperties(const TrackProperties& properties)
{
    trackProperties = properties;
    // call the version in the editor to update there
    if (auto* editor = dynamic_cast<PluginEditor*> (getActiveEditor()))
        editor->updateTrackProperties();
}

void PluginProcessor::updatePluginSize(int w, int h)
{
    pluginWidth = w;
    pluginHeight = h;
    // call the version in the editor to update there
    if (auto* editor = dynamic_cast<PluginEditor*> (getActiveEditor()))
        editor->updatePluginSize();
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}


//The level gain for the next stretch of scratch audio (at the rate Chris's loop sees). normalised: a fixed gain. mix: a floating gain from the running peak ceiling.
double PluginProcessor::levelGain(int mode, const float* l, const float* r, int count, double rate)
{
    if (mode == 1) return std::pow(10.0, (tuneToLufs(hintTune[16]) - tuneToLufs(hintTune[15]))/20.0);
    if (mode != 2) return 1.0;
    float pk = 0.0f;
    for (int i = 0; i < count; ++i) pk = std::max(pk, std::max(std::fabs(l[i]), std::fabs(r[i])));
    //Fast attack: a louder peak sets the ceiling at once, so the gain never lets a peak past the target. Slow release: the ceiling is held for 8 seconds
    //after its last peak, then falls with a 20 second time constant, so a quiet passage keeps the gain it had and the gain climbs only slowly.
    if ((double)pk > mixCeiling) {mixCeiling = pk; mixHold = (int)(8.0*rate);}
    else if (mixHold > 0) mixHold -= count;
    else mixCeiling *= std::exp(-(double)count/(20.0*rate));
    const double target = 0.8912509381337456; //-1 dBFS
    return juce::jlimit(0.1, 15.848931924611133, target/std::max(mixCeiling, 1.0e-9)); //at most +24 dB
}

//Chris's audio loop gets a scratch copy of the audio, never the host's buffer, so the host's audio passes through untouched.
template <typename T> void PluginProcessor::feedChris(juce::AudioBuffer<T>& buffer, juce::MidiBuffer& midi)
{
    const int anchorSel = hintTune[13] > 0.75f ? 2 : (hintTune[13] > 0.25f ? 1 : 0);
    const int mode = hintTune[14] > 0.75f ? 2 : (hintTune[14] > 0.25f ? 1 : 0);
    const int nIn = buffer.getNumSamples(), nch = buffer.getNumChannels();
    if (nIn <= 0 || nch < 1) return;
    if (levelResetPending.exchange(false)) {mixCeiling = 0.0; mixHold = 0;}
    if (anchorSel == 0 && mode == 0) { //stock: an exact copy, measured as Chris's plugin would
        chrisAudio.f8kRate = 0.0; chrisAudio.f8kGainNow = 1.0;
        if constexpr (std::is_same_v<T, double>) {
            scratchD.setSize(nch, nIn, false, false, true);
            for (int c = 0; c < nch; ++c) scratchD.copyFrom(c, 0, buffer, c, 0, nIn);
            chrisAudio.processBlock(scratchD, midi);
        } else {
            scratchF.setSize(nch, nIn, false, false, true);
            for (int c = 0; c < nch; ++c) scratchF.copyFrom(c, 0, buffer, c, 0, nIn);
            chrisAudio.processBlock(scratchF, midi);
        }
        return;
    }
    //the audio as floats, at the anchor rate if one is set
    const double hostRate = getSampleRate();
    const double anchorRate = anchorSel == 1 ? 44100.0 : (anchorSel == 2 ? 96000.0 : 0.0);
    if (anchorSel != lastAnchorSel) { //switched: start the converters from silence
        for (int k = 0; k < 2; ++k) {anchorL[k].reset(); anchorR[k].reset();}
        lastAnchorSel = anchorSel;
    }
    const T* inL = buffer.getReadPointer(0);
    const T* inR = buffer.getReadPointer(nch > 1 ? 1 : 0);
    int n = nIn;
    chrisAudio.f8kRate = 0.0;
    f8kdsp::Resampler* rl = nullptr; f8kdsp::Resampler* rr = nullptr;
    if (anchorRate > 0.0 && std::fabs(hostRate - anchorRate) > 0.5 && anchorL[anchorSel-1].ok() && !anchorL[anchorSel-1].isPassThrough()) {
        rl = &anchorL[anchorSel-1]; rr = &anchorR[anchorSel-1];
    }
    if (anchorRate > 0.0 && (rl != nullptr || std::fabs(hostRate - anchorRate) <= 0.5)) chrisAudio.f8kRate = anchorRate;
    if (rl != nullptr) {
        rsOutL.clear(); rsOutR.clear();
        if constexpr (std::is_same_v<T, float>) {rl->process(inL, (size_t)nIn, rsOutL); rr->process(inR, (size_t)nIn, rsOutR);}
        else {
            convL.assign(inL, inL + nIn); convR.assign(inR, inR + nIn); //(double host: converted to float for the converter)
            rl->process(convL.data(), (size_t)nIn, rsOutL); rr->process(convR.data(), (size_t)nIn, rsOutR);
        }
        n = (int)std::min(rsOutL.size(), rsOutR.size());
    }
    if (n <= 0) return;
    scratchF.setSize(nch, n, false, false, true);
    if (rl != nullptr) {
        std::copy(rsOutL.begin(), rsOutL.begin() + n, scratchF.getWritePointer(0));
        if (nch > 1) std::copy(rsOutR.begin(), rsOutR.begin() + n, scratchF.getWritePointer(1));
        for (int c = 2; c < nch; ++c) scratchF.clear(c, 0, n);
    } else {
        for (int c = 0; c < nch; ++c) { float* d = scratchF.getWritePointer(c); const T* sIn = buffer.getReadPointer(c); for (int i = 0; i < n; ++i) d[i] = (float)sIn[i]; }
    }
    //gain, in stretches of 64 samples so the mix gain can follow the ceiling; each stretch is measured with its own gain tag
    const double rate = chrisAudio.f8kRate > 0.0 ? chrisAudio.f8kRate : hostRate;
    const int stretch = mode == 2 ? 64 : n;
    for (int start = 0; start < n; start += stretch) {
        const int cnt = std::min(stretch, n - start);
        float* l = scratchF.getWritePointer(0) + start;
        float* r = scratchF.getWritePointer(nch > 1 ? 1 : 0) + start;
        const double g = levelGain(mode, l, r, cnt, rate);
        if (g != 1.0) for (int c = 0; c < std::min(nch, 2); ++c) { float* d = scratchF.getWritePointer(c) + start; for (int i = 0; i < cnt; ++i) d[i] = (float)(d[i]*g); }
        chrisAudio.f8kGainNow = g;
        juce::AudioBuffer<float> view(scratchF.getArrayOfWritePointers(), nch, start, cnt);
        chrisAudio.processBlock(view, midi);
    }
    chrisAudio.f8kGainNow = 1.0;
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    feedChris(buffer, midiMessages); //Chris's own audio loop (source/chris), fed a scratch copy: meterDARK only changes what you see
}

//==============================================================================

void PluginProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midiMessages)
{
    feedChris(buffer, midiMessages);
}
