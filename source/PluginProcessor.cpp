#include "PluginProcessor.h"
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
    loadUserDefaults(); //a new instance starts from the saved default preset, if there is one
    for (int i = 0; i < kNumTune; ++i) hintTune[i] = userTune[i];
    peakTrimDb = userPeakTrim;
    slewTrimDb = userSlewTrim;
 }

juce::File PluginProcessor::userDefaultsFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Meter F8K").getChildFile("defaults.xml");
}

void PluginProcessor::loadUserDefaults()
{
    const juce::File f = userDefaultsFile();
    if (!f.existsAsFile()) return;
    std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(f));
    if (xml == nullptr || !xml->hasTagName("meterdefaults")) return;
    for (int i = 0; i < kNumTune; ++i) {
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
    if (fileVer < 8) {userTune[0] = userTune[1] = userTune[2] = userTune[3] = 0.50f;} //version 8: the light/heavy/headroom/squashed distances are shares of each side of the balance point
    const double pt = xml->getDoubleAttribute("peaktrim", 0.0);
    const double st = xml->getDoubleAttribute("slewtrim", 0.0);
    userPeakTrim = (pt <= 0.0 && pt >= -24.0) ? (float)pt : 0.0f;
    userSlewTrim = (st <= 0.0 && st >= -24.0) ? (float)st : 0.0f;
}

void PluginProcessor::saveUserDefaults(const float* tuneIn, float peakTrimIn, float slewTrimIn)
{
    for (int i = 0; i < kNumTune; ++i) userTune[i] = tuneIn[i];
    userPeakTrim = peakTrimIn;
    userSlewTrim = slewTrimIn;
    juce::XmlElement xml("meterdefaults");
    for (int i = 0; i < kNumTune; ++i) xml.setAttribute(juce::String("tune")+juce::String(i), (double)userTune[i]);
    xml.setAttribute("ver", 8); //version 2: dark level reads dark<. Version 3: weight is a mass reading with a balance point. Version 4: the mass scale is stretched and balanced reads .50. Version 5: the bright reading is spread so its threshold sits like squashed (bright>.70). Version 6: dynamics is a spread reading with a balance point (tune 2, 3, 11)
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
void PluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock) {juce::ignoreUnused (sampleRate, samplesPerBlock);}
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
    for (int i = 0; i < kNumTune; ++i) xml->setAttribute(juce::String("awm_tune")+juce::String(i), (double)hintTune[i]);
    xml->setAttribute("awm_tunever", 8); //version 5: bright reading spread so its level sits like squashed. Version 6: dynamics spread reading (older projects are converted or reset when loaded)
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
            for (int i = 0; i < kNumTune; ++i) {
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
            if (stateVer < 8) {hintTune[0] = hintTune[1] = hintTune[2] = hintTune[3] = 0.50f;} //version 8: those distances are now shares of each side of the balance point
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


void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());
    
    if (!(getBus(false, 0)->isEnabled() && getBus(true, 0)->isEnabled())) return;
    auto mainOutput = getBusBuffer(buffer, false, 0); //if we have audio busses at all,
    auto mainInput = getBusBuffer(buffer, true, 0); //they're now mainOutput and mainInput.
    
    UIToAudioMessage uim;
    while (uiToAudio.pop(uim)) {
        //switch (uim.what) {
        //case UIToAudioMessage::NEW_VALUE: params[uim.which]->setValueNotifyingHost(params[uim.which]->convertTo0to1(uim.newValue)); break;
        //case UIToAudioMessage::BEGIN_EDIT: params[uim.which]->beginChangeGesture(); break;
        //case UIToAudioMessage::END_EDIT: params[uim.which]->endChangeGesture(); break;
        //}
    } //Handle inbound messages from the UI thread
    
    double windowSize = (1881.0 / 44100.0)*getSampleRate(); //higher is slower with larger RMS buffers
    double zeroCrossScale = (1.0 / getSampleRate())*44100.0;
    double beamScale = 1.0/getSampleRate();
    
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        auto outL = mainOutput.getWritePointer(0, i);
        auto outR = mainOutput.getWritePointer(1, i);
        auto inL = mainInput.getReadPointer(0, i); //in isBussesLayoutSupported, we have already
        auto inR = mainInput.getReadPointer(1, i); //specified that we can only be stereo and never mono
        double inputSampleL = *inL;
        double inputSampleR = *inR;
        
        auto playHead = getPlayHead();
        if (playHead) {auto playHeadPos = playHead->getPosition();
            if (playHeadPos->getTimeInSeconds() > 0.0) playheadMoving = !playHeadPos->getIsPlaying();
            //I have a sound editor that generates a playhead but won't do IsPlaying. This checks if the DAW speaks 'playHead'.
        }
        if (audioPlaying != playheadMoving) {
            double currentslewL = (fabs(inputSampleL-previousLeft)/32000.0)*getSampleRate();
            double currentslewR = (fabs(inputSampleR-previousRight)/32000.0)*getSampleRate();
            if (currentslewL > slewLeft) slewLeft = currentslewL;
            if (currentslewR > slewRight) slewRight = currentslewR;
            previousLeft = inputSampleL;
            previousRight = inputSampleR;
            
            if (fabs(beamLeft-currentslewL) < 0.2) beamLeft = (beamLeft*(1.0-beamScale)) + (currentslewL*beamScale);
            else beamLeft = (beamLeft*(1.0-beamScale));
            if (fabs(beamRight-currentslewR) < 0.2) beamRight = (beamRight*(1.0-beamScale)) + (currentslewR*beamScale);
            else beamRight = (beamRight*(1.0-beamScale));
            departureLeft = (departureLeft*(1.0-beamScale)) + (fabs(beamLeft-currentslewL)*beamScale);
            departureRight = (departureRight*(1.0-beamScale)) + (fabs(beamRight-currentslewR)*beamScale);
            
            double rectifiedL = fabs(inputSampleL);
            double rectifiedR = fabs(inputSampleR);
            if (rectifiedL > peakLeft) peakLeft = rectifiedL;
            if (rectifiedR > peakRight) peakRight = rectifiedR;
            
            zeroLeft += zeroCrossScale;
            if (longestZeroLeft < zeroLeft) longestZeroLeft = zeroLeft;
            if (wasPositiveL && inputSampleL < 0.0) {
                wasPositiveL = false;
                zeroLeft = 0.0;
            } else if (!wasPositiveL && inputSampleL > 0.0) {
                wasPositiveL = true;
                zeroLeft = 0.0;
            }
            zeroRight += zeroCrossScale;
            if (longestZeroRight < zeroRight) longestZeroRight = zeroRight;
            if (wasPositiveR && inputSampleR < 0.0) {
                wasPositiveR = false;
                zeroRight = 0.0;
            } else if (!wasPositiveR && inputSampleR > 0.0) {
                wasPositiveR = true;
                zeroRight = 0.0;
            }
            windowCount++;
        }
        
        *outL = (float)inputSampleL;
        *outR = (float)inputSampleR; //this is a meter. Raw pass-through
        
        if (windowCount > windowSize)
        {
            AudioToUIMessage msg; //define the thing we're telling JUCE
            msg.what = AudioToUIMessage::PEAK_LEFT; msg.newValue = (float)sqrt(peakLeft); audioToUI.push(msg);
            msg.what = AudioToUIMessage::PEAK_RIGHT; msg.newValue = (float)sqrt(peakRight); audioToUI.push(msg);
            msg.what = AudioToUIMessage::SLEW_LEFT; msg.newValue = (float)slewLeft; audioToUI.push(msg);
            msg.what = AudioToUIMessage::SLEW_RIGHT; msg.newValue = (float)slewRight; audioToUI.push(msg);
            msg.what = AudioToUIMessage::ZERO_LEFT; msg.newValue = (float)longestZeroLeft; audioToUI.push(msg);
            msg.what = AudioToUIMessage::ZERO_RIGHT; msg.newValue = (float)longestZeroRight; audioToUI.push(msg);
            msg.what = AudioToUIMessage::BEAM_LEFT; msg.newValue = (float)beamLeft; audioToUI.push(msg);
            msg.what = AudioToUIMessage::BEAM_RIGHT; msg.newValue = (float)beamRight; audioToUI.push(msg);
            msg.what = AudioToUIMessage::DEPT_LEFT; msg.newValue = (float)departureLeft; audioToUI.push(msg);
            msg.what = AudioToUIMessage::DEPT_RIGHT; msg.newValue = (float)departureRight; audioToUI.push(msg);
            msg.what = AudioToUIMessage::INCREMENT; msg.newValue = 1200.0f; audioToUI.push(msg);
            peakLeft = 0.0;
            peakRight = 0.0;
            slewLeft = 0.0;
            slewRight = 0.0; //reset our variables to do the RMS again
            zeroLeft = 0.0;
            zeroRight = 0.0;
            departureLeft = 0.0;
            departureRight = 0.0;
            longestZeroLeft = 0.0;
            longestZeroRight = 0.0;
            windowCount = 0;
        }
    }
}

//==============================================================================

void PluginProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());
    
    if (!(getBus(false, 0)->isEnabled() && getBus(true, 0)->isEnabled())) return;
    auto mainOutput = getBusBuffer(buffer, false, 0); //if we have audio busses at all,
    auto mainInput = getBusBuffer(buffer, true, 0); //they're now mainOutput and mainInput.
    
    UIToAudioMessage uim;
    while (uiToAudio.pop(uim)) {
        //switch (uim.what) {
        //case UIToAudioMessage::NEW_VALUE: params[uim.which]->setValueNotifyingHost(params[uim.which]->convertTo0to1(uim.newValue)); break;
        //case UIToAudioMessage::BEGIN_EDIT: params[uim.which]->beginChangeGesture(); break;
        //case UIToAudioMessage::END_EDIT: params[uim.which]->endChangeGesture(); break;
        //}
    } //Handle inbound messages from the UI thread
    
    double windowSize = (1881.0 / 44100.0)*getSampleRate(); //higher is slower with larger RMS buffers
    double zeroCrossScale = (1.0 / getSampleRate())*44100.0;
    double beamScale = 1.0/getSampleRate();

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        auto outL = mainOutput.getWritePointer(0, i);
        auto outR = mainOutput.getWritePointer(1, i);
        auto inL = mainInput.getReadPointer(0, i); //in isBussesLayoutSupported, we have already
        auto inR = mainInput.getReadPointer(1, i); //specified that we can only be stereo and never mono
        double inputSampleL = *inL;
        double inputSampleR = *inR;
        
        auto playHead = getPlayHead();
        if (playHead) {auto playHeadPos = playHead->getPosition();
            if (playHeadPos->getTimeInSeconds() > 0.0) playheadMoving = !playHeadPos->getIsPlaying();
            //I have a sound editor that generates a playhead but won't do IsPlaying. This checks if the DAW speaks 'playHead'.
        }
        if (audioPlaying != playheadMoving) {
            double currentslewL = (fabs(inputSampleL-previousLeft)/32000.0)*getSampleRate();
            double currentslewR = (fabs(inputSampleR-previousRight)/32000.0)*getSampleRate();
            if (currentslewL > slewLeft) slewLeft = currentslewL;
            if (currentslewR > slewRight) slewRight = currentslewR;
            previousLeft = inputSampleL;
            previousRight = inputSampleR;
            
            if (fabs(beamLeft-currentslewL) < 0.2) beamLeft = (beamLeft*(1.0-beamScale)) + (currentslewL*beamScale);
            else beamLeft = (beamLeft*(1.0-beamScale));
            if (fabs(beamRight-currentslewR) < 0.2) beamRight = (beamRight*(1.0-beamScale)) + (currentslewR*beamScale);
            else beamRight = (beamRight*(1.0-beamScale));
            departureLeft = (departureLeft*(1.0-beamScale)) + (fabs(beamLeft-currentslewL)*beamScale);
            departureRight = (departureRight*(1.0-beamScale)) + (fabs(beamRight-currentslewR)*beamScale);

            double rectifiedL = fabs(inputSampleL);
            double rectifiedR = fabs(inputSampleR);
            if (rectifiedL > peakLeft) peakLeft = rectifiedL;
            if (rectifiedR > peakRight) peakRight = rectifiedR;
            
             zeroLeft += zeroCrossScale;
            if (longestZeroLeft < zeroLeft) longestZeroLeft = zeroLeft;
            if (wasPositiveL && inputSampleL < 0.0) {
                wasPositiveL = false;
                zeroLeft = 0.0;
            } else if (!wasPositiveL && inputSampleL > 0.0) {
                wasPositiveL = true;
                zeroLeft = 0.0;
            }
            zeroRight += zeroCrossScale;
            if (longestZeroRight < zeroRight) longestZeroRight = zeroRight;
            if (wasPositiveR && inputSampleR < 0.0) {
                wasPositiveR = false;
                zeroRight = 0.0;
            } else if (!wasPositiveR && inputSampleR > 0.0) {
                wasPositiveR = true;
                zeroRight = 0.0;
            }
            windowCount++;
        }
        
        *outL = inputSampleL;
        *outR = inputSampleR; //this is a meter. Raw pass-through
        
        if (windowCount > windowSize)
        {
            AudioToUIMessage msg; //define the thing we're telling JUCE
            msg.what = AudioToUIMessage::PEAK_LEFT; msg.newValue = (float)sqrt(peakLeft); audioToUI.push(msg);
            msg.what = AudioToUIMessage::PEAK_RIGHT; msg.newValue = (float)sqrt(peakRight); audioToUI.push(msg);
            msg.what = AudioToUIMessage::SLEW_LEFT; msg.newValue = (float)slewLeft; audioToUI.push(msg);
            msg.what = AudioToUIMessage::SLEW_RIGHT; msg.newValue = (float)slewRight; audioToUI.push(msg);
            msg.what = AudioToUIMessage::ZERO_LEFT; msg.newValue = (float)longestZeroLeft; audioToUI.push(msg);
            msg.what = AudioToUIMessage::ZERO_RIGHT; msg.newValue = (float)longestZeroRight; audioToUI.push(msg);
            msg.what = AudioToUIMessage::BEAM_LEFT; msg.newValue = (float)beamLeft; audioToUI.push(msg);
            msg.what = AudioToUIMessage::BEAM_RIGHT; msg.newValue = (float)beamRight; audioToUI.push(msg);
            msg.what = AudioToUIMessage::DEPT_LEFT; msg.newValue = (float)departureLeft; audioToUI.push(msg);
            msg.what = AudioToUIMessage::DEPT_RIGHT; msg.newValue = (float)departureRight; audioToUI.push(msg);
            msg.what = AudioToUIMessage::INCREMENT; msg.newValue = 1200.0f; audioToUI.push(msg);
            peakLeft = 0.0;
            peakRight = 0.0;
            slewLeft = 0.0;
            slewRight = 0.0; //reset our variables to do the RMS again
            zeroLeft = 0.0;
            zeroRight = 0.0;
            departureLeft = 0.0;
            departureRight = 0.0;
            longestZeroLeft = 0.0;
            longestZeroRight = 0.0;
            windowCount = 0;
        }
    }
}

