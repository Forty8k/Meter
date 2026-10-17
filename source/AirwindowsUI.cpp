// AirwindowsUI by Chris Johnson
// Initial seed code for the meter created by Paul Walker on 8/23/21.
#include "AirwindowsUI.h"
#ifndef M_PI
#  define M_PI (3.14159265358979323846)
#endif
#ifndef M_PI_2
#  define M_PI_2 (1.57079632679489661923)
#endif

//THE CONTROL RAILS: a thin dark strip along the bottom of each header strip holds that meter's hint gauge, its display trim and, when tune is open,
//its extra settings. These helpers are shared by the drawing and the clicking, so the two always agree.
static float tuneFont(float vS) {return (float)sqrt(vS*64.0f)*1.618f;}
static float railBottomUnits(int sec) {return 200.0f*(float)sec + (sec == 0 ? AirwindowsMeter::stripTopUnits : AirwindowsMeter::stripMidUnits);}
static float railTopPx(int sec, float vS) {return (railBottomUnits(sec) - AirwindowsMeter::railUnits)*vS;}
static float railHeightPx(float vS) {return AirwindowsMeter::railUnits*vS;}
static bool onRail(const juce::MouseEvent &event, int sec, float vS) {return event.y >= (int)railTopPx(sec, vS) && event.y < (int)(railTopPx(sec, vS)+railHeightPx(vS));}
static int tuneLabelW(float vS) {return (int)(tuneFont(vS)*11.0f);}
static int tuneGap(float vS) {return (int)(tuneFont(vS)*0.2f);}
static int tuneCentreX(int displayWidth, float vS) {return displayWidth/2 - tuneLabelW(vS) - tuneGap(vS)/2;} //left edge of the centred gauge pair
static int tunePairX(int displayWidth, float vS, int k) {return tuneCentreX(displayWidth, vS) + k*(tuneLabelW(vS)+tuneGap(vS));}
static int tuneRightX(int displayWidth, float vS, int k) {return displayWidth - (k+1)*(tuneLabelW(vS)+tuneGap(vS)) - (int)(tuneFont(vS)*0.5f);} //k = 0 is the rightmost label

//The trim labels live on the left of the peaks rail (1) and the slews rail (2). Returns which one a click at this position is on, or 0 for neither.
static int onTrimLabel(const juce::MouseEvent &event, int displayWidth, float vS)
{
    const bool bigLayout = (sqrt(vS*61.8f)*1.618f) > 10.0f;
    if (!bigLayout || displayWidth < 700 || event.x >= (int)(tuneFont(vS)*14.0f)) return 0;
    for (int sec = 1; sec <= 2; ++sec) if (onRail(event, sec, vS)) return sec;
    return 0;
}

//The tune controls: a "tune" label at the bottom right opens the settings that live on the rails.
static bool onTuneToggle(const juce::MouseEvent &event, int displayWidth, int displayHeight)
{
    return displayWidth >= 700 && event.x >= displayWidth-70 && event.y >= displayHeight-28;
}

static int onTuneLabel(const juce::MouseEvent &event, int displayWidth, int displayHeight, float vS)
{
    if (displayWidth < 700 || tuneFont(vS) <= 10.0f) return -1;
    if (onRail(event, 0, vS)) {
        const int x0 = tuneCentreX(displayWidth, vS), span = 2*tuneLabelW(vS) + tuneGap(vS);
        if (event.x >= x0 && event.x < x0+span) { //the weight gauge is one control in three parts: light distance, balance point, heavy distance
            const int third = (event.x-x0)*3/span;
            return third == 0 ? 0 : (third == 1 ? 9 : 1);
        }
        const int xr = tuneRightX(displayWidth, vS, 0);
        if (event.x >= xr && event.x < xr+tuneLabelW(vS)) return 8; //hint speed
        const int xp = tuneRightX(displayWidth, vS, 1);
        if (event.x >= xp && event.x < xp+tuneLabelW(vS)) return 10; //frequency push
        return -1;
    }
    for (int sec = 1; sec < 3; ++sec) {
        if (!onRail(event, sec, vS)) continue;
        if (sec == 1) { //the dynamics gauge is one control in three parts, like weight: headroom distance, balance point, squashed distance
            const int x0 = tuneCentreX(displayWidth, vS), span = 2*tuneLabelW(vS) + tuneGap(vS);
            if (event.x >= x0 && event.x < x0+span) {
                const int third = (event.x-x0)*3/span;
                return third == 0 ? 2 : (third == 1 ? 11 : 3);
            }
            continue;
        }
        for (int k = 0; k < 2; ++k) {
            const int x0 = tunePairX(displayWidth, vS, k);
            if (event.x >= x0 && event.x < x0+tuneLabelW(vS)) return sec*2 + k;
        }
    }
    return -1;
}

void AirwindowsMeter::mouseDown(const juce::MouseEvent &event)
{
    float vS = displayHeight/600.0f;
    if (onTuneToggle(event, displayWidth, displayHeight)) {tuneOpen = !tuneOpen; repaint(); return;}
    if (tuneOpen && displayWidth >= 700 && event.y >= displayHeight-28) {
        if (event.x >= displayWidth-190 && event.x < displayWidth-76) { //save the current tune values and trims as the default for new instances
            if (onSaveDefault) onSaveDefault();
            savedAtMs = juce::Time::getMillisecondCounter();
            repaint();
            return;
        }
        if (event.x >= displayWidth-270 && event.x < displayWidth-196) { //back to the built-in values
            for (int i = 0; i < 12; ++i) tune[i] = tuneDefault[i];
            repaint();
            return;
        }
    }
    if (tuneOpen) {
        const int t = onTuneLabel(event, displayWidth, displayHeight, vS);
        if (t >= 0) {
            if (event.getNumberOfClicks() > 1) {tune[t] = tuneUser[t]; tuneDragging = -1; repaint();}
            else {tuneDragging = t; tuneDragStart = tune[t];}
            return;
        }
    }
    const int which = onTrimLabel(event, displayWidth, vS);
    if (which != 0) {
        float& trim = (which == 1) ? peakTrimDb : slewTrimDb;
        if (event.getNumberOfClicks() > 1) {trim = 0.0f; trimDragging = 0; repaint();} //double-click = back to the original view
        else {trimDragging = which; trimDragStart = trim;} //press and drag up or down to set it
        return;
    }
    if (event.getNumberOfClicks() > 1) {resetArrays(); if (onReset) onReset();}
    else meterPlaying = !meterPlaying;
}

void AirwindowsMeter::mouseDrag(const juce::MouseEvent &event)
{
    if (tuneDragging >= 0) {
        float v = tuneDragStart - (float)event.getDistanceFromDragStartY() * 0.004f; //drag up = higher
        v = std::round(v * 100.0f) / 100.0f;
        if (v < (tuneDragging >= 6 ? 0.0f : 0.02f)) v = (tuneDragging >= 6 ? 0.0f : 0.02f);
        if (v > 0.98f) v = 0.98f;
        if (v != tune[tuneDragging]) {tune[tuneDragging] = v; repaint();}
        return;
    }
    if (trimDragging == 0) return;
    float& trim = (trimDragging == 1) ? peakTrimDb : slewTrimDb;
    float newTrim = trimDragStart - (float)event.getDistanceFromDragStartY() * 0.12f; //dragging up lowers the zoom, down raises it
    newTrim = std::round(newTrim * 10.0f) / 10.0f;
    if (newTrim > 0.0f) newTrim = 0.0f;
    if (newTrim < -24.0f) newTrim = -24.0f;
    if (newTrim != trim) {trim = newTrim; repaint();}
}

void AirwindowsMeter::mouseUp(const juce::MouseEvent &)
{
    trimDragging = 0;
    tuneDragging = -1;
}

//Everything that is SCORED lives here and runs exactly once per audio window, as the window arrives (not while painting). The painting
//only reads the results. That keeps the scores, grades, tone bar and hints independent of the window size and the redraw rate.
//The formulas for the per-column numbers must match the ones the painting uses to draw the dots.
AirwindowsMeter::ColumnMetrics AirwindowsMeter::columnMetrics(unsigned long col) const
{
    ColumnMetrics m;
    m.peakL = dataPL[col] * 200.0f;
    m.peakR = dataPR[col] * 200.0f;
    m.slewL = sqrt(dataSL[col])*300.0f;
    m.slewR = sqrt(dataSR[col])*300.0f;
    m.meterZeroL = (sqrt(dataZL[col])*6.0f)-6.0f;
    if (m.meterZeroL > 192.0f) m.meterZeroL = 192.0f;
    m.bassL = fmin((sqrt(m.meterZeroL)*16.2f)-20.0f,199.0f);
    m.meterZeroR = (sqrt(dataZR[col])*6.0f)-6.0f;
    if (m.meterZeroR > 192.0f) m.meterZeroR = 192.0f;
    m.bassR = fmin((sqrt(m.meterZeroR)*16.2f)-20.0f,199.0f);
    m.psDotSizeL = (dataPL[col]*64.0f) / (fabs((m.peakL*0.945433426957143f)-m.slewL)+6.18033988749894f);
    m.slewDotSizeL = (sin(0.1618f/m.psDotSizeL)*6.18f)+(sqrt(m.slewL)*0.1618f);
    m.bassDotSizeL = sqrt(m.meterZeroL*0.1f*dataPL[col]);
    m.psDotSizeR = (dataPR[col]*64.0f) / (fabs((m.peakR*0.945433426957143f)-m.slewR)+6.18033988749894f);
    m.slewDotSizeR = (sin(0.1618f/m.psDotSizeR)*6.18f)+(sqrt(m.slewR)*0.1618f);
    m.bassDotSizeR = sqrt(m.meterZeroR*0.1f*dataPR[col]);
    return m;
}

void AirwindowsMeter::scoreColumn(unsigned long col)
{
    if (++labCheckCounter >= 24) {labCheckCounter = 0; lab.reloadIfChanged();} //about once a second: settings exported from the Analyser's Lab take effect live
    const ColumnMetrics m = columnMetrics(col);
    const float peakL = m.peakL, peakR = m.peakR, slewL = m.slewL, slewR = m.slewR, bassL = m.bassL, bassR = m.bassR;
    const float psDotSizeL = m.psDotSizeL, psDotSizeR = m.psDotSizeR;
    const float slewDotSizeL = m.slewDotSizeL, slewDotSizeR = m.slewDotSizeR;
    const float bassDotSizeL = m.bassDotSizeL, bassDotSizeR = m.bassDotSizeR;
    //clip length, as the drawing tracks it: clipping peaks shorten the allowed length, escalating when both channels clip
    if (peakL > 197.0f) {
        scoreSustainedClip *= 0.99f;
        if (peakR > 197.0f) scoreSustainedClip *= 0.975f;
    } else scoreSustainedClip = 180.0f;
    if (peakR > 197.0f) {
        scoreSustainedClip *= 0.99f;
        if (peakL > 197.0f) scoreSustainedClip *= 0.975f;
    } else scoreSustainedClip = 180.0f;
        {
            backR[col] = (float)storeR;
            backG[col] = (float)storeG;
            backB[col] = (float)storeB; //RGB backdrop for text
            {   //PER-METER TONE: the same three scores the tone color is made from, kept separately for each meter
                const float scoreMax = (float)fmax(fmax(outputR, outputG), fmax(outputB, 0.000001));
                const float ratioZ = (float)(outputG/scoreMax); //zero crosses
                const float ratioP = (float)(outputB/scoreMax); //peaks
                const float ratioS = (float)(outputR/scoreMax); //slews
                const float quietGate = (float)fmin(outputVol*outputVol*8.0, 1.0); //quiet passages fade to neutral, as the tone color does
                devZ[col] = (ratioZ - (ratioP+ratioS)*0.5f) * quietGate;
                devP[col] = (ratioP - (ratioZ+ratioS)*0.5f) * quietGate;
                devS[col] = (ratioS - (ratioZ+ratioP)*0.5f) * quietGate;
            }
            {   //LIVE HINT DETECTORS (full-level values, never the trimmed display). Position 0-200 is how far up the meter's good zone a dot sits.
                //Windows below about -18 dB (peak position under 70) are skipped by every hint: silence reads as the deepest bass (no zero crossings) and
                //as "never reaching the top", so quiet gaps, breaks and fades would otherwise bend the averages.
                const float avgRate = 0.01f + 0.07f*tune[8]; //how fast the hint averages move (the hint speed control)
                //WEIGHT (mass): the size of the weight dots as they are drawn (long zero-cross gaps and strong peaks both make them bigger), with the peak part measured
                //against the track's own running average peak level (the same slow mean the dynamics spread uses, which settles in about 2 seconds after any level change),
                //so a rough mix, a normalised track and a full master read on the same scale. Big, dense clusters read high (heavy), small sparse ones read low (light).
                //This has its own quiet gate, relative to the track's recent ceiling (about 18 dB under it, and the ceiling falls back in about 4 seconds) instead of the fixed level the other hints use.
                {
                    const float topPeakM = fmax(peakL, peakR);
                    if (topPeakM > 8.0f) {
                        if (topPeakM > massCeil) massCeil += (topPeakM - massCeil) * lab.ceilRise; else massCeil -= (massCeil - topPeakM) * lab.ceilFall;
                        if (topPeakM > lab.gateRatio()*massCeil) {
                            //the reference level: the running mean peak (in dB) plus 4.5 dB, which is where a typical master's ceiling sits above its mean peak, so the scale stays where it was.
                            //Before the mean has started, the ceiling stands in for it.
                            const float refLevel = spreadInit ? 200.0f*powf(10.0f, (spreadMean + lab.refOffsetDb)/40.0f) : massCeil;
                            const float f = fmin(fmax(lab.refTarget/fmax(refLevel, 40.0f), 0.8f), lab.fMax); //brings that reference level to about -1.5 dB
                            //FREQUENCY PUSH: dots low on the meter (below about 80 Hz, positions past 139) count extra, dots high on it (above about 180 Hz, positions under 107)
                            //count less. It ramps in smoothly rather than cutting off, and tune[10] sets how strong it is (0 = pure mass).
                            auto dotMass = [&](float meterZero, float peak, float dataP, float bassPos) {
                                const float lowPush = LabTuning::ramp(bassPos, lab.lowFromPos, lab.lowFullPos);
                                const float highPush = LabTuning::ramp(bassPos, lab.highFromPos, lab.highFullPos);
                                const float push = 1.0f + tune[10]*(lab.lowPushGain*lowPush - lab.highPushGain*highPush);
                                return (sqrtf(fmax(meterZero, 0.0f)*0.1f*dataP*f) + lab.peakInfluence*powf(fmin(peak*f, 200.0f)/lab.peakDiv, lab.peakPow)) * push;
                            };
                            const float massNow = 0.5f*(dotMass(m.meterZeroL, peakL, dataPL[col], bassL) + dotMass(m.meterZeroR, peakR, dataPR[col], bassR));
                            massAvg += (massNow - massAvg) * avgRate;
                            hintUpperAvg[0] = fmin(fmax((massAvg - lab.massOffset)/lab.massRange, 0.0f), 1.0f); //the live weight reading, 0 = light, 0.5 = balanced, 1 = heavy
                            //DYNAMICS (spread): how far each window's peak lands from the slow mean peak level, in dB (the peak position is a square root of the level, so 40 per decade).
                            //Peaks that swing about give a wide spread (headroom, undercompressed). Peaks that all land in the same place give none (squashed). Level does not matter.
                            const float dbNow = 40.0f*log10f(fmax(topPeakM, 1.0f)/200.0f);
                            if (!spreadInit) {spreadMean = dbNow; spreadInit = true;}
                            else {
                                spreadAvg += (fabsf(dbNow - spreadMean) - spreadAvg) * avgRate;
                                spreadMean += (dbNow - spreadMean) * lab.followRate;
                            }
                            hintUpperAvg[1] = 1.0f - fmin(spreadAvg/lab.dynRangeDb, 1.0f); //the live dynamics reading, 0 = peaks swing 8 dB or more, 1 = a flat line
                        }
                    }
                }
                //SLEWS are judged by the dot colours, the meter's own balance measure: red = slews well ahead of peaks, green/teal = peaks well
                //ahead. (Overflow bars and a high beam are normal on loud, well balanced masters, so they are not used.)
                const float redNow = ((psDotSizeL <= 1.0f && slewL > peakL) ? 0.5f : 0.0f) + ((psDotSizeR <= 1.0f && slewR > peakR) ? 0.5f : 0.0f);
                const float greenNow = ((psDotSizeL <= 1.0f && slewL <= peakL) ? 0.5f : 0.0f) + ((psDotSizeR <= 1.0f && slewR <= peakR) ? 0.5f : 0.0f);
                if (fmax(peakL, peakR) > lab.detailGate) { //quiet gaps between notes (peak below about -18 dB) say nothing about balance, so they are skipped
                    //Consecutive windows count more: a lone hi-hat hit is often fine, a sustained run is not. A lone window counts 40%, rising to full over 8 in a row, and an off window only halves the run.
                    hintRedRun = (redNow > 0.5f) ? hintRedRun + 1.0f : hintRedRun*0.5f; //a single off window halves the run instead of ending it
                    hintGreenRun = (greenNow > 0.5f) ? hintGreenRun + 1.0f : hintGreenRun*0.5f;
                    const float redRamp = lab.lone + (1.0f - lab.lone)*fmin(hintRedRun / lab.runLength, 1.0f); //a lone hot window counts 40%, a run of 8 in a row counts fully
                    const float redWeighted = redNow * redRamp;
                    const float greenRamp = lab.lone + (1.0f - lab.lone)*fmin(hintGreenRun / lab.runLength, 1.0f);
                    const float greenWeighted = greenNow * greenRamp;
                    hintRedAvg += (redWeighted - hintRedAvg) * avgRate;
                    hintGreenAvg += (greenWeighted - hintGreenAvg) * avgRate;
                }
            }
        }
        //(the grades and the tone colour come from Chris's own meter, run once per window underneath: see PluginEditor::idle)

}

void AirwindowsMeter::paint(juce::Graphics &g)
{
    float vS = displayHeight/600.0f; // short for vScale: everything * this
    
    //The slew meter's display zoom (see slewTrimDb). The slew meter shows sqrt of the slew value, so a trim in dB scales the picture by dB/40.
    //0 dB = 1.0 = the original look. Only DRAWN positions use this: colours, dot sizes and every score use the full-level values.
    const float slewDisplayScale = (float)pow(10.0, (double)slewTrimDb/40.0);
    //Same for the peak meter: the peak position is the square root of the amplitude, so a trim in dB scales the drawn position by dB/40.
    //Clip detection, the yellow clip columns, the hint detectors and all scoring use the untrimmed peaks.
    const float peakDisplayScale = (float)pow(10.0, (double)peakTrimDb/40.0);
    
    //THEME: dark meters with light text, so that a strip lighting up draws the eye. Set kDarkTheme to false for the original light look.
    static const bool kDarkTheme = true;
    const bool bigLayout = (sqrt(vS*61.8f)*1.618f) > 10.0f; //the full three-meter layout, as opposed to the tiny-window grade display
    const bool dark = kDarkTheme && bigLayout;
    const juce::Colour meterBg = dark ? juce::Colour(14, 16, 20) : juce::Colours::white;
    const juce::Colour gridCol = dark ? juce::Colour(58, 63, 74) : juce::Colours::lightgrey;
    const juce::Colour borderCol = dark ? juce::Colour(96, 102, 116) : juce::Colours::darkgrey;
    const juce::Colour textCol = dark ? juce::Colour(236, 238, 242) : juce::Colours::black;
    const juce::Colour labelCol = dark ? juce::Colour(206, 211, 221) : juce::Colours::black;
    const juce::Colour clipCol = dark ? juce::Colour(255, 225, 40) : juce::Colour(255, 0, 0); //clipping is yellow: no dot or strip ever is
    const juce::Colour beamCol = dark ? juce::Colour(238, 240, 246).withAlpha(0.88f) : juce::Colours::black; //bright trace of the lead element, sitting on a darker band so it stands out from the dots
    const juce::Colour beamBand = juce::Colour(4, 5, 8).withAlpha(0.92f);
    //the text strips stay white, as in the original, so the three sections stand apart from the dark meters. A strip tints toward its own
    //meter's color (zero crosses green, peaks blue, slews red, as in the top bar) when that meter is ahead.
    const juce::Colour stripBase[3] = {juce::Colours::white, juce::Colours::white, juce::Colours::white};
    const juce::Colour stripTextCol = dark ? juce::Colour(12, 14, 18) : juce::Colours::black;
    const juce::Colour stripGlow[3] = {juce::Colour(40, 210, 90), juce::Colour(60, 110, 255), juce::Colour(255, 70, 60)};
    
    //Dot colors, from how slews and peaks compare (blue = in balance/sonority, purple = slews leaning ahead, teal = peaks leaning ahead,
    //red = slews well ahead, green = peaks ahead, more vivid as it gets louder). The light theme is exactly the original formulas.
    //The dark theme lifts them so blue and the dark dots stay visible on black. The hues keep their meaning.
    auto dotColour = [&](float slew, float peak, float psDot) -> juce::Colour {
        const float slewVsPeak = fmax(slew-(peak*0.945433426957143f),0.0f)*0.029f;
        const float peakVsSlew = fmax((peak*0.945433426957143f)-slew,0.0f)*0.012f;
        if (!dark) {
            if (psDot > 1.0f) return juce::Colour::fromFloatRGBA(slewVsPeak, peakVsSlew, 1.0f, 1.0f);
            if (slew > peak) return juce::Colour::fromFloatRGBA(fmin((64.0f+slew)/128.0f,1.0f), 0.0f, 0.0f, 1.0f);
            return juce::Colour::fromFloatRGBA(0.0f, fmax((160.0f-peak)/160.0f,0.0f), 0.0f, 1.0f);
        }
        if (psDot > 1.0f) return juce::Colour::fromFloatRGBA(0.28f+0.72f*fmin(slewVsPeak,1.0f), 0.58f+0.42f*fmin(peakVsSlew,1.0f), 1.0f, 1.0f); //azure: the target colour, bright and vivid next to the green
        if (slew > peak) return juce::Colour::fromFloatRGBA(0.80f+0.20f*fmin((64.0f+slew)/128.0f,1.0f), 0.16f, 0.15f, 1.0f); //red pushed to its brightest while staying red
        const float quiet = fmax((160.0f-peak)/160.0f,0.0f); //1 = quiet (soft teal-green), 0 = loud (vivid green): peaks-ahead moments stay green, matching the low end color
        return juce::Colour::fromFloatRGBA(0.20f+(0.36f-0.20f)*quiet, 0.94f+(0.72f-0.94f)*quiet, 0.36f+(0.56f-0.36f)*quiet, 1.0f);
    };
    
    //Dots are brightened in the columns where their meter was AHEAD of the other two and dimmed where it was behind, using the same per-column
    //lead that lights the strips. Only brightness changes, never hue, and a balanced column is left alone (apart from a general lift).
    auto shade = [&](const juce::Colour& c, int sec, unsigned long col) -> juce::Colour {
        if (!dark) return c;
        const float dv = (sec == 0) ? devZ[col] : (sec == 1) ? devP[col] : devS[col];
        const float lead = fmax(fmin(dv*2.5f, 1.0f), -1.0f); //+1 = clearly ahead, -1 = clearly behind
        return c.withMultipliedBrightness((lead >= 0.0f ? 1.0f + 0.3f*lead : 1.0f + 0.15f*lead) * 1.1f); //behind dims only gently (about 85%), ahead brightens
    };
    
    //LAYOUT: each of the three meters has a text strip along its top (titles, grades and hints), and its data is drawn below that.
    //stripTop is for the zero cross meter (it also holds the top color bar), stripMid is for the peak and slew meters.
    //Units are the 600-high layout. Set both to 0.0f to get the original full-height meters back.
    const bool textStrips = bigLayout;
    const float stripTop = textStrips ? stripTopUnits : 0.0f;
    const float stripMid = textStrips ? stripMidUnits : 0.0f;
    const float stripH[3] = {stripTop, stripMid, stripMid};
    auto meterY = [&](int sec, float yv) -> float { //yv is a position in the original layout (0-600), result is in pixels
        const float base = 200.0f*(float)sec;
        return (base + stripH[sec] + (yv-base)*((200.0f-stripH[sec])/200.0f))*vS;
    };
    auto pk = [&](float yv) -> float { //a position in the peak meter, moved the way the peak trim moves the dots (so the dB lines and labels follow the trim)
        return 400.0f - (400.0f - yv)*peakDisplayScale;
    };
    auto meterH = [&](int sec, float hv) -> float { //a height in the original layout, as pixels
        return hv*((200.0f-stripH[sec])/200.0f)*vS;
    };
    if ((sqrt(vS*61.8f)*1.618f) > 10.0f) {
        g.fillAll(meterBg); //blank screen before doing anything, unless our draw covers the whole display anyway
        if (dark) { //the text strips, each in its own very dark tint, so the title areas are separate from the data areas
            for (int sec = 0; sec < 3; ++sec) {
                const int stripFrom = (int)((sec == 0 ? 21.0f : 200.0f*(float)sec + 2.0f) * vS);
                const int stripTo = (int)((200.0f*(float)sec + stripH[sec]) * vS);
                g.setColour(stripBase[sec]);
                g.fillRect(0, stripFrom, getWidth(), stripTo - stripFrom);
            }
        }
        g.setColour(gridCol);
        g.fillRect(0,  (int)meterY(0, 60.0f), getWidth(),1); // -6dB markings
        g.fillRect(0, (int)meterY(0, 101.02f), getWidth(),1); //-12dB markings
        g.fillRect(0, (int)meterY(0, 130.02f), getWidth(),1); //-18dB markings
        g.fillRect(0, (int)meterY(0, 150.2f), getWidth(),1); //-24dB markings
        g.fillRect(0, (int)meterY(0, 164.9f), getWidth(),1); //-30dB markings
        g.fillRect(0, (int)meterY(0, 175.2f), getWidth(),1); //-36dB markings
        
        if (peakDisplayScale < 0.97f) g.fillRect(0, (int)meterY(1, pk(200.0f)), getWidth(),1); //0dB: where the yellow overshoots start once the peaks are trimmed
        g.fillRect(0, (int)meterY(1, pk(260.0f)), getWidth(),1); // -6dB markings
        g.fillRect(0, (int)meterY(1, pk(301.02f)), getWidth(),1); //-12dB markings
        g.fillRect(0, (int)meterY(1, pk(330.02f)), getWidth(),1); //-18dB markings
        g.fillRect(0, (int)meterY(1, pk(350.2f)), getWidth(),1); //-24dB markings
        g.fillRect(0, (int)meterY(1, pk(364.9f)), getWidth(),1); //-30dB markings
        g.fillRect(0, (int)meterY(1, pk(375.2f)), getWidth(),1); //-36dB markings
        
        g.fillRect(0,  (int)meterY(2, 460.0f), getWidth(),1); // -6dB markings
        g.fillRect(0, (int)meterY(2, 501.02f), getWidth(),1); //-12dB markings
        g.fillRect(0, (int)meterY(2, 530.02f), getWidth(),1); //-18dB markings
        g.fillRect(0, (int)meterY(2, 550.2f), getWidth(),1); //-24dB markings
        g.fillRect(0, (int)meterY(2, 564.9f), getWidth(),1); //-30dB markings
        g.fillRect(0, (int)meterY(2, 575.2f), getWidth(),1); //-36dB markings
    } else {
        juce::ColourGradient backdropGradient = juce::ColourGradient::vertical(juce::Colours::white, 0.0f, backdropColour, 600.0f*vS); //600 tall version
        backdropGradient.addColour((float)gradientMin, backdropColour.interpolatedWith(juce::Colours::white, (float)gradientMin));
        //the mid point of the gradient goes toward the bottom and goes whiter when everything's in balance
        g.setFillType(backdropGradient);
        g.fillAll();
    } //blank screen before doing anything, unless our draw covers the whole display anyway
    
    //Clip columns follow the peak trim. Their length is a full-scale overshoot in the same units as the peaks. The part that fits above the
    //(trimmed) full-scale line is drawn rising from that line, attached to the peaks, and any remainder wraps round and rises from the bottom.
    //With no trim the line is at the very top, so nothing fits above it and the original look is unchanged.
    auto drawClip = [&](unsigned long col) {
        const float fullLine = 400.0f - 200.0f*peakDisplayScale; //position of full scale, in the original 0-600 layout
        const float room = fullLine - 200.0f;
        const float len = (181.9f-(float)sustainedClip) * peakDisplayScale;
        const float up = fmin(len, room);
        const float wrap = len - up;
        if (up > 0.0f) g.fillRect((float)col, meterY(1, fullLine-up), 1.9f, meterH(1, up));
        if (wrap > 0.0f) g.fillRect((float)col, meterY(1, 400.9f-wrap), 1.9f, meterH(1, wrap));
    };
    for (unsigned long count = 0; count < fmin(displayWidth,5150); ++count) //count through all the points in the array
    {
        g.setColour(juce::Colours::black);
        float psDotSizeL = 0.0f;
        float psDotSizeR = 0.0f;
        float slewDotSizeL = 0.0f;
        float slewDotSizeR = 0.0f;
        float bassDotSizeL = 0.0f;
        float bassDotSizeR = 0.0f;
        float peakL = dataPL[count] * 200.0f;
        float peakR = dataPR[count] * 200.0f;
        float slewL = sqrt(dataSL[count])*300.0f;
        float slewR = sqrt(dataSR[count])*300.0f;
        float slewDrawL = slewL*slewDisplayScale; //colours, dot sizes and scoring still use the unscaled slewL
        float slewDrawR = slewR*slewDisplayScale;
        float meterZeroL = (sqrt(dataZL[count])*6.0f)-6.0f;
        if (meterZeroL > 192.0f) meterZeroL = 192.0f;
        float bassL = fmin((sqrt(meterZeroL)*16.2f)-20.0f,199.0f);
        float meterZeroR = (sqrt(dataZR[count])*6.0f)-6.0f;
        if (meterZeroR > 192.0f) meterZeroR = 192.0f;
        float bassR = fmin((sqrt(meterZeroR)*16.2f)-20.0f,199.0f);
        float beamRaw = sqrt(dataBL[count]+dataBR[count])*700.0f*slewDisplayScale; //drawn position follows the slew zoom
        //The original capped the beam at 199, so on loud material it sat flat against the top edge and hid its own movement.
        //Now it is the original (linear) up to beamKnee and then a gentle log curve, so higher values still sit higher and still move.
        const float beamKnee = 165.0f;
        const float beamSpan = 1350.0f; //how far past the knee the curve keeps separating values, before it reaches the top
        float beam = beamRaw;
        if (beamRaw > beamKnee) beam = beamKnee + (199.0f-beamKnee) * (log2f(1.0f+(beamRaw-beamKnee)/45.0f) / log2f(1.0f+beamSpan/45.0f));
        beam = fmin(beam, 199.0f);
        float departure = fmax(0.0f,5.0f-(dataDL[count]+dataDR[count])*1500.0f) * 100.0f * fmax(dataBL[count],dataBR[count]) * slewDisplayScale;
        if (hasZeroRmsLines) { //Meter 0.5.6 on: Chris's newer slew line (his formula), with the same gentle top curve as above
            beamRaw = sqrt(dataBL[count]+dataBR[count])*618.033988749894f*slewDisplayScale;
            beam = beamRaw;
            if (beamRaw > beamKnee) beam = beamKnee + (199.0f-beamKnee) * (log2f(1.0f+(beamRaw-beamKnee)/45.0f) / log2f(1.0f+beamSpan/45.0f));
            beam = fmin(beam, 199.0f);
            departure = fmin(6.18033988749894f, fmax(0.0f, (5.0f-(dataDL[count]+dataDR[count])) * sqrt(fmax(dataBL[count],dataBR[count]))) * 6.18033988749894f);
        }
         //begin draw dots on meters L
        if (peakL > 197.0f) {
            g.setColour(clipCol);
            drawClip(count);
            sustainedClip *= 0.99f; //peak is clipping!
            if (peakR > 197.0f) sustainedClip *= 0.975f; //if both are clipping, escalate
        } else sustainedClip = 180.0f;
        
        //THE LINES (Chris's black lines), in Meter F8K's style: white while there is enough movement to give the line a body; when the body
        //collapses (static, unchanging material) it turns yellow, the same yellow as clipping, and keeps a minimum height so it stays visible.
        //It sits on a darker band so it stands out from the dots. 0.5.5 has the slew line only; 0.5.6 adds one on the zero cross and peak meters.
        auto drawLine = [&](int sec, float yv, float heightUnits) {
            if (dark) { g.setColour(beamBand); g.fillRect((float)count, meterY(sec, yv)-4.0f*vS, 1.0f, meterH(sec, heightUnits)+8.0f*vS); }
            const float bodyPx = meterH(sec, heightUnits);
            const float fastness = 1.0f - fmin(bodyPx/1.5f, 1.0f);
            const float minPx = 2.0f;
            const float drawPx = fmax(bodyPx, minPx);
            g.setColour(dark ? beamCol.interpolatedWith(clipCol, fastness).withAlpha(0.88f+0.12f*fastness) : beamCol);
            g.fillRect((float)count, meterY(sec, yv) - (drawPx-bodyPx)*0.5f*(dark ? 1.0f : 0.0f), 1.0f, dark ? drawPx : bodyPx);
        };
        drawLine(2, 600.0f-beam, departure); //the slew line
        if (hasZeroRmsLines) { //Chris's formulas for where the zero cross and RMS lines sit and how thick they are
            const float beamZ = (sqrt((dataBZL[count]+dataBZR[count])*0.5f)*6.0f)-6.0f;
            const float beamZero = fmax(fmin((sqrt(fmax(beamZ, 0.0f))*16.2f)-20.0f, 199.0f), 0.5f);
            const float departureZero = fmin(6.18033988749894f, (dataDZL[count]*dataDZR[count])*(float)(23000000.0/pow(beamZero, 4.0)));
            const float beamRMS = fmin(199.0f, sqrt(dataBRL[count]+dataBRR[count])*140.0f);
            const float departureRMS = fmin(6.18033988749894f, (dataDRL[count]*dataDRR[count])*12000.0f);
            drawLine(0, beamZero, departureZero);
            drawLine(1, pk(400.0f-beamRMS), departureRMS*peakDisplayScale);
        }
        
        psDotSizeL = (dataPL[count]*64.0f) / (fabs((peakL*0.945433426957143f)-slewL)+6.18033988749894f);
        slewDotSizeL = (sin(0.1618f/psDotSizeL)*6.18f)+(sqrt(slewL)*0.1618f);
        bassDotSizeL = sqrt(meterZeroL*0.1f*dataPL[count]);
        const juce::Colour dotL = dotColour(slewL, peakL, psDotSizeL); //set COLOR
        g.setColour(shade(dotL, 1, count));
        g.fillRect((float)count, meterY(1, 400.0f - fmin(peakL*peakDisplayScale, 200.0f)), psDotSizeL+0.618f, (psDotSizeL+0.618f)*vS);
        
        g.setColour(shade(dotL, 2, count));
        if (slewDrawL > 194.0f) g.fillRect((float)count, meterY(2, 599.0f-(sqrt(slewDrawL-194.0f)*1.618f)), 1.618f, meterH(2, sqrt(slewDrawL-194.0f)*1.618f));
        else g.fillRect((float)count, meterY(2, 600.0f-slewDrawL), slewDotSizeL+0.618f, slewDotSizeL*vS); //draw slew
        
        g.setColour(shade(dotL, 0, count));
        if (bassL < 199.0f-(bassDotSizeL+fmax(pow(peakL/140.0f,4.0f),0.0f))) g.fillRect((float)count, meterY(0, fmax(bassL, 0.0f)), bassDotSizeL+fmax(pow(peakL/140.0f,4.0f),0.0f), (bassDotSizeL+fmax(pow(peakL/140.0f,4.0f),0.0f))*vS); //zero cross subs
        //end draw dots on meters L
        //begin draw dots on meters R
        if (peakR > 197.0f) {
            g.setColour(clipCol);
            drawClip(count);
            sustainedClip *= 0.99f; //peak is clipping!
            if (peakL > 197.0f) sustainedClip *= 0.975f; //if both are clipping, escalate
        } else sustainedClip = 180.0f;
        
        psDotSizeR = (dataPR[count]*64.0f) / (fabs((peakR*0.945433426957143f)-slewR)+6.18033988749894f);
        slewDotSizeR = (sin(0.1618f/psDotSizeR)*6.18f)+(sqrt(slewR)*0.1618f);
        bassDotSizeR = sqrt(meterZeroR*0.1f*dataPR[count]);
        const juce::Colour dotR = dotColour(slewR, peakR, psDotSizeR); //set COLOR
        g.setColour(shade(dotR, 1, count));
        g.fillRect((float)count, meterY(1, 400.0f - fmin(peakR*peakDisplayScale, 200.0f)), psDotSizeR+0.618f, (psDotSizeR+0.618f)*vS);
        
        g.setColour(shade(dotR, 2, count));
        if (slewDrawR > 194.0f) g.fillRect((float)count, meterY(2, 599.0f-(sqrt(slewDrawR-194.0f)*1.618f)), 1.618f, meterH(2, sqrt(slewDrawR-194.0f)*1.618f));
        else g.fillRect((float)count, meterY(2, 600.0f-slewDrawR), slewDotSizeR+0.618f, slewDotSizeR*vS); //draw slew
        
        g.setColour(shade(dotR, 0, count));
        if (bassR < 199.0f-(bassDotSizeR+fmax(pow(peakR/140.0f,4.0f),0.0f))) g.fillRect((float)count, meterY(0, fmax(bassR, 0.0f)), bassDotSizeR+fmax(pow(peakR/140.0f,4.0f),0.0f), (bassDotSizeR+fmax(pow(peakL/140.0f,4.0f),0.0f))*vS); //zero cross subs
        //end draw dots on meters R
        
        g.setColour(juce::Colour::fromFloatRGBA(backR[count], backG[count], backB[count], 1.0f)); //set backdrop colour
        g.fillRect((float)(count)-0.25f, 0.0f, 1.5f, 19.5f*vS); //top bar
        
        if (textStrips) { //PER-METER TONE behind each meter's title and hint. It is the tone color split up by meter.
            const float devs[3] = {devZ[count], devP[count], devS[count]};
            for (int sec = 0; sec < 3; ++sec) {
                const float shadeTop = (sec == 0 ? 21.0f : 200.0f*(float)sec + 2.0f) * vS;
                const float shadeBottom = (200.0f*(float)sec + stripH[sec]) * vS;
                if (dark) {
                    //dark theme: a strip lights up in its own meter's color (zero crosses green, peaks blue, slews red, the same as the
                    //top bar) when that meter is AHEAD of the other two. Balanced or behind, it stays dark.
                    const float glowGain = 2.0f; //higher = lights up for smaller leads
                    const float glowMax = 0.9f;
                    const float amount = fmin(fmax(devs[sec], 0.0f)*glowGain, glowMax);
                    if (amount < 0.02f) continue;
                    g.setColour(stripBase[sec].interpolatedWith(stripGlow[sec], amount*0.78f));
                } else { //light theme: warm = this meter is stronger than the other two, blue = weaker, white = balanced
                    const float amount = fmin(fabs(devs[sec])*1.6f, 0.55f);
                    if (amount < 0.02f) continue;
                    g.setColour((devs[sec] > 0.0f ? juce::Colour(255, 150, 50) : juce::Colour(70, 140, 255)).withAlpha(amount));
                }
                g.fillRect((float)(count)-0.25f, shadeTop, 1.5f, shadeBottom - shadeTop);
            }
        }
        
        if (count == (unsigned long)dataPosition-1) { //only update text score display more infrequently
            
            //(the grade letters and number are Chris's own, taken from his meter: see PluginEditor::takeScores)
            
            //PER-METER HINTS: each meter is judged on its own against a target, not against the other two.
            //under target -> "more X" / "raise peaks", over target -> "reduce X". On target -> no hint. The balance between the meters
            //then comes from fixing whichever is off. Thresholds have hysteresis (they enter at one level and leave at another) so hints don't flicker,
            //and quiet passages are gated out, as the tone color is.
            {
                //Where each hint switches on is set by the tune controls (tune[0..5], see AirwindowsUI.h). It leaves a little before it entered, so hints don't flicker.
                const float quietGate = (float)fmin(outputVol*outputVol*8.0, 1.0);
                //The hints name a state, not an order: a dominant sub, a bright top or a dark record can all be deliberate.
                //(hintTextMore = under target, hintTextLess = over target)
                hintTextMore[0] = juce::String("light");      hintTextLess[0] = juce::String("heavy");
                hintTextMore[1] = juce::String("headroom");   hintTextLess[1] = juce::String("squashed");
                hintTextMore[2] = juce::String("dark");       hintTextLess[2] = juce::String("bright");
                for (int hint = 0; hint < 3; ++hint) {
                    int state = hintState[hint];
                    bool overNow, overKeep, underNow, underKeep;
                    if (hint == 2) { //slews: colour balance. over = bright (red share), under = dark (green share)
                        const float brightLive = 1.0f - expf(-lab.brightSpread*hintRedAvg); //spreads the red share (rarely above 0.5, usually under 0.15) over the whole 0-1 range so it can be tuned
                        overNow = brightLive > tune[5];    overKeep = brightLive > fmax(tune[5]-0.06f, 0.0f);
                        const float darkRead = 1.0f - hintGreenAvg; //1 = nothing dark, 0 = fully dark, so dark sits on the left like light and headroom
                        underNow = darkRead < tune[4]; underKeep = darkRead < fmin(tune[4]+0.10f, 1.0f);
                    } else if (hint == 1) { //dynamics: the spread reading against the balance point. Peaks that all land in one place are squashed, peaks that swing about have headroom
                        const float headLevel = tune[11] - tune[2], squashLevel = tune[11] + tune[3];
                        overNow = hintUpperAvg[1] > squashLevel;
                        overKeep = hintUpperAvg[1] > squashLevel - lab.keepWeight;
                        underNow = hintUpperAvg[1] < headLevel;
                        underKeep = hintUpperAvg[1] < headLevel + lab.keepWeight;
                    } else { //weight: the mass reading against the balance point. Below it by the light distance is light, above it by the heavy distance is heavy
                        const float lightLevel = tune[9] - tune[0], heavyLevel = tune[9] + tune[1];
                        overNow = hintUpperAvg[0] > heavyLevel;
                        overKeep = hintUpperAvg[0] > heavyLevel - lab.keepWeight;
                        underNow = hintUpperAvg[0] < lightLevel;
                        underKeep = hintUpperAvg[0] < lightLevel + lab.keepWeight;
                    }
                    if (state == 1 && !overKeep) state = 0;
                    if (state == -1 && !underKeep) state = 0;
                    if (overNow) state = 1;                    //"over" can take over from "under" directly (e.g. heavy replaces light), it does not have to wait for it to clear
                    else if (state == 0 && underNow) state = -1;
                    hintState[hint] = state;
                    if (state != 0) hintText[hint] = (state > 0) ? hintTextLess[hint] : hintTextMore[hint]; //the text keeps its last wording while it fades out (it must not flip to the other word)
                    const float target = (state != 0) ? quietGate : 0.0f;
                    hintOpacity[hint] += (target - hintOpacity[hint]) * 0.12f;
                }
            }
        }
    }
    
    const juce::String scoreStr = bonusScore+totalPackage+authority+power+detail;
    float scaleFont = (sqrt(vS*64.0f)*1.618f);
    if (scaleFont > 10.0f) {
        g.setFont(scaleFont*1.618f);
        auto stripText = [&](const juce::String& text, int x, int y, int w, int h, juce::Justification just, float opacity) {
            if (dark) { //a soft white halo keeps the dark text readable when a strip tints
                g.setColour(juce::Colours::white);
                g.setOpacity(0.5f*opacity);
                g.drawText(text, x+1, y+1, w, h, just);
            }
            g.setColour(stripTextCol);
            g.setOpacity(opacity);
            g.drawText(text, x, y, w, h, just);
        };
        stripText("weight "+authority, (int)scaleFont/2, (int)(21.0f*vS), displayWidth/2, (int)(29.0f*vS), juce::Justification::centredLeft, 1.0f);
        stripText("zero crosses", (displayWidth/2)-(int)(scaleFont*0.618f), (int)(21.0f*vS), displayWidth/2, (int)(29.0f*vS), juce::Justification::centredRight, 1.0f);
        stripText(hintText[0], 0, (int)(23.0f*vS), displayWidth, 32, juce::Justification::centredTop, fmin(hintOpacity[0],1.0f));
         //authority is the amount of varying zero cross energy, between maximum and minimum.
        //it can't be always maximum because that is just one frequency and reads as boring, plus it doesn't translate across bassbins as well.
        //The zero cross meter measures how long the audio can go before crossing the middle of the waveform again.
        //As such, loudness of bass makes it go higher and loudness of treble tends to disrupt this and take away low zero cross data.
        //Thing is, high and mid frequencies do also show up here, and it is just as subject to the need for an 'evenly spaced dot cloud'
        //as everything else, but we hear evenness of this cloud as a sense of scale, especially when it reaches low frequencies.
        //Simple resonant lows down here produce only one point of zero cross, which is why something as simple as an electronic kick
        //that drops in frequency 'translates' better and is more interesting to feel and hear. Sweeping across bassbin resonances works better.
        //But the way higher frequency content interferes with the zero cross is mix-dependent too: for instance, if you lack low mids that
        //should be there, the zero cross will have gaps of no data and then the bass all by itself all the way at the bottom.
        //Bringing in audio at other frequency ranges directly brings in zero cross ranges as they interfere with the zero crossings,
        //and this can be tracked as a dot cloud like the other meters, but maximizing this area (especially with the includion of DEEP bass)
        //is what produces a sense of imposing, majestic authority to the track.
        //This is pretty easily screwed up by having RMS too high, peaks too clipped, and detail boosted to match what RMS is doing.
        //The funny thing is that, since this can be made to balance with peak energy and not simply RMS, that means if you have a
        //high power mix with lots of energy beyond simple RMS, maximizing authority means hitting on all frequencies with perfect low mids.
        //That is a recipe for bass authority, not simply bass loudness, so authority is 'power through peaks, but for the lows'.
        //considering what a black art that sort of thing is, it's useful to view it in this light.
        
        g.setOpacity(1.0f);
        stripText("dynamics "+power, (int)scaleFont/2, (int)(202*vS), displayWidth/3, (int)(28.0f*vS), juce::Justification::centredLeft, 1.0f);
        stripText("peaks", (displayWidth/2)-(int)(scaleFont*0.618f), (int)(202*vS), displayWidth/2, (int)(28.0f*vS), juce::Justification::centredRight, 1.0f);
        stripText(hintText[1], 0, (int)(203*vS), displayWidth, 32, juce::Justification::centredTop, fmin(hintOpacity[1],1.0f));
        //power is the intensity of varying peak energy, between maximum and minimum.
        //it can't be always maximum because that is just constant loudness and can't startle or vary.
        //Power is the derivative, the unexpected. it's the ability of sound to be producing peak 'aura' beyond what our ears think is the median loud.
        //we do not leave crest factor to be quiet with it, we leave crest factor so we can constantly have the ear tickled by stuff happening,
        //that alerts the brain to peaks far beyond what the loudness seems to be. If they aren't present, the sound is boring.
        //The key is DERIVATIVE: peaks matter to the extent that they extend beyond the RMS (root-mean-square, body/density of the sound),
        //and power is the extent to which peaks are both happening and varying from each other constantly. That's a second derivative.
        //This cloud expands as the capacity for loudness surprise rises: if you have continuously playing sound,
        //whether it's a drone or saturation or simply a reverberation in your mix, it'll restrict the ability of the peak cloud
        //to drop lower. Limiting the volume or bass of such sustaining elements works to widen the peak cloud's range,
        //bearing in mind that the ideal situation is constant peak activity at every dB level at once at all times.
        //This is of course impossible and a contradiction, which is what makes it interesting :)
        //We hear 'activity at very low dB during loud sounds' as openness of the sound, drama/excitement, and mix ease of listening.
        
        stripText("detail "+detail, (int)scaleFont/2, (int)(402*vS), displayWidth/2, (int)(28.0f*vS), juce::Justification::centredLeft, 1.0f);
        stripText("slews", (displayWidth/2)-(int)(scaleFont*0.618f), (int)(402*vS), displayWidth/2, (int)(28.0f*vS), juce::Justification::centredRight, 1.0f);
        stripText(hintText[2], 0, (int)(403*vS), displayWidth, 32, juce::Justification::centredTop, fmin(hintOpacity[2],1.0f));
        if (displayWidth >= 700) { //THE CONTROL RAILS: hint gauges always visible; settings (greyed ticks) only respond once tune is open
            g.setFont(scaleFont*0.95f);
            auto fmt = [](float v) { juce::String t = juce::String(v, 2); return t.startsWith("0.") ? t.substring(1) : t; };
            const int rw = tuneLabelW(vS), gap = tuneGap(vS);
            const juce::Colour railCol = dark ? juce::Colour(24, 27, 33) : juce::Colour(232, 234, 240);
            const juce::Colour railText = dark ? labelCol : juce::Colours::black;
            for (int sec = 0; sec < 3; ++sec) { //the dark rail itself
                g.setColour(railCol);
                g.fillRect(0.0f, railTopPx(sec, vS), (float)displayWidth, railHeightPx(vS));
                g.setColour(gridCol);
                g.fillRect(0.0f, railTopPx(sec, vS), (float)displayWidth, 1.0f);
            }
            auto railLabel = [&](const juce::String& t, int x, int sec, int w, juce::Justification just, float opacity) {
                g.setColour(railText);
                g.setOpacity(opacity);
                g.drawText(t, x, (int)railTopPx(sec, vS) + 1, w, (int)(railHeightPx(vS)*0.68f), just);
            };
            //a gauge under a label: ticks are settings, the dot is the live reading
            auto railGauge = [&](int sec, int x, float width, const float* ticks, int numTicks, float midTick, float liveV, bool hasLive, bool active) {
                const float gy = railTopPx(sec, vS) + railHeightPx(vS) - 5.0f;
                g.setColour(railText); g.setOpacity(0.3f);
                g.fillRect((float)x, gy, width, 1.0f);
                g.setOpacity(0.9f);
                for (int t = 0; t < numTicks; ++t) g.fillRect((float)x + width*fmin(fmax(ticks[t], 0.0f), 1.0f) - 1.0f, gy - 3.0f, 2.0f, 7.0f);
                if (midTick >= 0.0f) g.fillRect((float)x + width*fmin(fmax(midTick, 0.0f), 1.0f) - 1.0f, gy - 5.0f, 2.0f, 11.0f);
                if (hasLive) {
                    g.setColour(active ? juce::Colour(255, 70, 60) : railText);
                    g.fillEllipse((float)x + width*fmin(fmax(liveV, 0.0f), 1.0f) - 3.0f, gy - 3.0f, 6.0f, 6.0f);
                }
            };
            //DISPLAY TRIMS: press and drag up or down on the label to shift that meter's picture, double-click to reset
            auto trimText = [](float db) { return juce::String("display trim ") + juce::String(db > -0.05f ? 0.0f : db, 1) + " dB"; };
            railLabel(trimText(peakTrimDb), (int)scaleFont/2, 1, (int)(tuneFont(vS)*14.0f), juce::Justification::centredLeft, 0.6f);
            railLabel(trimText(slewTrimDb), (int)scaleFont/2, 2, (int)(tuneFont(vS)*14.0f), juce::Justification::centredLeft, 0.6f);
            //WEIGHT and DYNAMICS: one gauge with three points each (low distance, balance point, high distance). The dot is the live reading, and the hint shows
            //when it goes past the low or high tick. Weight: 0 = small and sparse dots (light), 1 = big and dense (heavy). Dynamics: 0 = peaks swing (headroom), 1 = a flat line (squashed).
            auto threePoint = [&](int sec, int idxLow, int idxBal, int idxHigh, const char* lowName, const char* highName, float liveV, int hint) {
                const int x0 = tuneCentreX(displayWidth, vS), span = 2*rw + gap, third = span/3;
                const bool lowOn = hintState[hint] == -1, highOn = hintState[hint] == 1;
                const float bal = tune[idxBal];
                railLabel(juce::String(lowName) + " -" + fmt(tune[idxLow]), x0, sec, third, juce::Justification::centredLeft, lowOn ? 1.0f : 0.8f);
                railLabel("balance " + fmt(bal) + " [" + fmt(liveV) + "]", x0 + third, sec, third, juce::Justification::centred, 0.9f);
                railLabel(juce::String(highName) + " +" + fmt(tune[idxHigh]), x0 + 2*third, sec, third, juce::Justification::centredRight, highOn ? 1.0f : 0.8f);
                //The balance line stays in the middle of the gauge. The dot is drawn relative to it (the reading minus the balance point), and the two ticks sit that far either side,
                //so changing the balance value pulls the dot toward the middle without moving anything else. The number in brackets is always the raw reading.
                const float ticks[2] = {0.5f - tune[idxLow], 0.5f + tune[idxHigh]};
                railGauge(sec, x0, (float)span*0.98f, ticks, 2, 0.5f, 0.5f + (liveV - bal), true, lowOn || highOn);
            };
            threePoint(0, 0, 9, 1, "light", "heavy", hintUpperAvg[0], 0);
            threePoint(1, 2, 11, 3, "headroom", "squashed", hintUpperAvg[1], 1);
            {   //DETAIL: a pair of gauges. The hint shows when the dot crosses the tick in the arrow's direction.
                const char* tuneNames[2] = {"dark<", "bright>"};
                const float live[2] = {1.0f - hintGreenAvg, 1.0f - expf(-lab.brightSpread*hintRedAvg)};
                for (int j = 0; j < 2; ++j) {
                    const int i = j + 4, sec = 2;
                    const bool active = (i%2 == 0) ? (hintState[sec] == -1) : (hintState[sec] == 1); //this label's hint is showing now
                    const int lx = tunePairX(displayWidth, vS, i%2);
                    railLabel(juce::String(active ? "* " : "") + juce::String(tuneNames[j]) + fmt(tune[i]) + " [" + fmt(live[j]) + "]", lx, sec, rw, juce::Justification::centredLeft, active ? 1.0f : 0.8f);
                    railGauge(sec, lx, (float)rw*0.98f, &tune[i], 1, -1.0f, live[j], true, active);
                }
            }
            if (tuneOpen) {   //the extra settings sit at the right of their rails
                auto setting = [&](int sec, int k, const char* name, int idx, float liveV, bool hasLive) {
                    const int x = tuneRightX(displayWidth, vS, k);
                    railLabel(juce::String(name) + fmt(tune[idx]) + (hasLive ? juce::String(" [") + fmt(liveV) + "]" : juce::String()), x, sec, rw, juce::Justification::centredLeft, 0.9f);
                    railGauge(sec, x, (float)rw*0.9f, &tune[idx], 1, -1.0f, liveV, hasLive, false);
                };
                setting(0, 0, "hint speed ", 8, 0.0f, false);
                setting(0, 1, "push ", 10, 0.0f, false);                 //frequency push: low dots count heavier, high dots lighter
                {   //DEFAULT PRESET: "save default" keeps the current tune values and trims for every new instance, "factory" puts the built-in values back (here only)
                    const bool justSaved = (juce::Time::getMillisecondCounter() - savedAtMs) < 1600u;
                    g.setColour(labelCol);
                    g.setOpacity(0.9f);
                    g.drawText(justSaved ? "saved" : "save default", displayWidth-190, displayHeight-24, 114, 20, juce::Justification::bottomRight);
                    g.setOpacity(0.7f);
                    g.drawText("factory", displayWidth-270, displayHeight-24, 74, 20, juce::Justification::bottomRight);
                }
            }
            g.setColour(labelCol);
            g.setOpacity(tuneOpen ? 0.9f : 0.4f);
            g.drawText("tune", displayWidth-62, displayHeight-24, 56, 20, juce::Justification::bottomRight);
            g.setColour(labelCol); g.setOpacity(0.35f); //which original Meter is underneath
            g.drawText(juce::String("on Airwindows Meter ") + chris::kVersion, (int)scaleFont/2, displayHeight-24, 260, 20, juce::Justification::bottomLeft);
            if (lab.fromFile) { //the reading settings came from the Analyser's Lab
                g.setColour(juce::Colour(255, 225, 40)); g.setOpacity(0.7f);
                g.drawText("lab", displayWidth-110, displayHeight-24, 44, 20, juce::Justification::bottomRight);
            }
            g.setFont(scaleFont*1.618f);
        }
        //detail is the intensity of varying slew energy, between maximum and minimum.
        //it can't be always maximum because that is just hardness and glare and sounds bad to people.
        //it also can't be minimum or it comes off as dull and uneventful in the treble range, so there's a balance to be struck.
        //Recording mediums used to set hard limits to how much treble you could have in the very high frequencies, an artificial limit,
        //but it accidentally served to restrict detail to where it would balance with power (maximized peak energy) because it would
        //end up quieter, balanced with the more hearable RMS loudness, allowing the power to be experienced as a sound event.
        //Modern technology has no such limitations, so detail is commonly made to balance with RMS loudness in a context where
        //ONLY RMS remains and the peak energy is not there anymore. (also, detail can contribute to loudness)
        //That means you can get a 'correct' RMS/detail balance, but it only highlights the lack of power from missing peak energy.
        //When you balance detail with a loudness balance that retains power and uses it, it still sounds 'correct' but the aura
        //of the thing is very different, because the active peak energy is also sensed and it too can be in balance with other parts of the sound.
        //this is the same phenomenon as the balance of taste stimuli in Heinz ketchup. It's like a cheat code.
        
        g.setOpacity(0.618f);
        g.setColour(juce::Colours::white);
        g.drawText(scoreStr, 1, 1, displayWidth, (int)(20.0f*vS), juce::Justification::centred);
        //underdrawing in white for areas prone to get covered up with dots
        g.setOpacity(1.0f);
        g.setColour(juce::Colours::black);
        g.drawText(scoreStr, 0, 0, displayWidth, (int)(20.0f*vS), juce::Justification::centred);
        
        //KEYS on the top bar. The bar is the three meters' scores mixed as color. Each meter has its own color (the same one that tints its strip
        //and colors the playhead as it crosses that strip): green = low end, blue = dynamics, red = detail. Chips and words, so the colors can't be
        //mistaken for the grade letters.
        if (displayWidth >= 760) {
            const float keyFont = scaleFont*0.95f;
            g.setFont(keyFont);
            const float chip = fmin(fmax(11.0f*vS*1.6f, 8.0f), 14.0f);
            const juce::String keyWords[3] = {juce::String("weight"), juce::String("dynamics"), juce::String("detail")};
            float kx = (float)((int)scaleFont/2);
            const float ky = (19.5f*vS - chip) * 0.5f;
            for (int k = 0; k < 3; ++k) {
                g.setColour(juce::Colours::black); g.fillRect(kx-1.0f, ky-1.0f, chip+2.0f, chip+2.0f);
                g.setColour(juce::Colours::white); g.fillRect(kx-0.5f, ky-0.5f, chip+1.0f, chip+1.0f);
                g.setColour(stripGlow[k]);          g.fillRect(kx+0.5f, ky+0.5f, chip-1.0f, chip-1.0f);
                const int tw = (int)((float)keyWords[k].length() * keyFont * 0.58f) + 4;
                g.setColour(juce::Colours::white); g.setOpacity(0.618f);
                g.drawText(keyWords[k], (int)(kx+chip+4.0f)+1, 1, tw, (int)(20.0f*vS), juce::Justification::centredLeft);
                g.setColour(juce::Colours::black); g.setOpacity(0.85f);
                g.drawText(keyWords[k], (int)(kx+chip+4.0f), 0, tw, (int)(20.0f*vS), juce::Justification::centredLeft);
                g.setOpacity(1.0f);
                kx += chip + 4.0f + (float)tw + 8.0f;
            }
            const juce::String keyRight("colour tint = focus area");
            g.setColour(juce::Colours::white);
            g.setOpacity(0.618f);
            g.drawText(keyRight, displayWidth-321-(int)scaleFont/2, 1, 320, (int)(20.0f*vS), juce::Justification::centredRight);
            g.setColour(juce::Colours::black);
            g.setOpacity(0.85f);
            g.drawText(keyRight, displayWidth-320-(int)scaleFont/2, 0, 320, (int)(20.0f*vS), juce::Justification::centredRight);
        }
        g.setOpacity(1.0f);
       //the Hit Record Letter Grade is 'totalPackage' followed by Power, Detail and Scale in order. If you have different priorities,
        //it's fair to want those in a different order for your quality metric: bass music might go scale first, pop might want detail.
        //I'm using power first simply because it is the most spectacular contrast between modern production and lasting hit status,
        //and so it most highlights what is lacking in production done wrong, and music that won't last or be heard beyond the moment.
        //The importance of totalPackage is that it's a running tally of how well balanced each of these existing categories are with each other.
        //This is best understood with the ketchup analogy again: you can have corn syrup sweet all day long, but it gets cloying.
        //But if you balance it with a lot of salt, and some bitterness, and sourness, no one sensation predominates and the composite
        //ends up with a compelling, addictive profile and takes over entire supermarket shelves :)
        //The sound of music can do that as well, and as much as any given characteristic can have its own superfans
        //(rock for power, pop for detail, orchestration or bassmusic for scale) the ability to cross over and go huge
        //is conditional on making ALL the categories max out, but max out in a way that is perfectly balanced.
        //As such, you can get a high hit record score at totally different music intensities, because there's no ONE ultimate density
        //for musical event: it seems like there's a correlation with natural phenomena similar to how we can be impressed both by
        //a gentle rain's moderate alerting quality, and by the force of a giant scary thunderstorm. These all produce balanced
        //point-clouds of audio data, with dangerous wind increasing the bass that gives you scale, and rain producing power and detail.
        //So the final score letter ends up being a derivative of derivatives: where each of the categories is about how well a data point
        //sits alongside other data points and how evenly it's distributed to produce a sensation that's a part of how music is heard,
        //the final score letter is about how well all the parts come together into a whole, even though it's not actually
        //made up of the other letters: the other letters are maximum scores, an indicator of where you might direct attention
        //in fixing up a mix, but the final score is a running tally of the experience of the whole music as it unfolds.
        //the weight of any given moment is governed by how intense the moment is, but not simply by RMS loudness, the weight of a moment
        //over time is set by the highest peak or slew or RMS (handles scale) at that moment. So, if something dramatic happens,
        //Meter builds more of it into the score, but if there's a quiet intro or a fade, Meter pays less attention to that in favor of
        //caring about what happens when the music is active. Not no attention, just scaled to how intense the music is actively trying to be.
        
        g.setOpacity(1.0f);
        g.setFont(scaleFont);
        auto axisLabel = [&](const juce::String& text, int y) { //scale labels get a light backing so the dots can't hide them
            g.setColour(meterBg);
            g.setOpacity(0.72f);
            g.fillRect((int)scaleFont - 2, y + 1, (int)((float)text.length()*scaleFont*0.6f) + 6, (int)scaleFont);
            g.setColour(labelCol);
            g.setOpacity(1.0f);
            g.drawText(text, (int)scaleFont, y, displayWidth/2, (int)scaleFont, juce::Justification::bottomLeft);
        };
        axisLabel("900 Hz", (int)meterY(0, 60.0f)-7);
        axisLabel("210 Hz", (int)meterY(0, 101.02f)-7);
        axisLabel("100 Hz", (int)meterY(0, 130.02f)-7);
        axisLabel("60 Hz", (int)meterY(0, 150.2f)-7);
        axisLabel("45 Hz", (int)meterY(0, 164.9f)-7);
        axisLabel("35 Hz", (int)meterY(0, 175.2f)-7);
        
        {   //peak dB labels follow the peak trim, so 0 dB sits at the bottom of the yellow overshoots; labels that would crowd are skipped
            struct DbMark {const char* text; float yv;};
            const DbMark marks[7] = {{"0 dB", 200.0f}, {"-6 dB", 260.0f}, {"-12 dB", 301.02f}, {"-18 dB", 330.02f}, {"-24 dB", 350.2f}, {"-30 dB", 364.9f}, {"-36 dB", 375.2f}};
            float lastY = -1000.0f;
            for (int k = (peakDisplayScale < 0.97f ? 0 : 1); k < 7; ++k) {
                const float yy = meterY(1, pk(marks[k].yv));
                if (yy - lastY < scaleFont*0.95f) continue;
                axisLabel(juce::String(marks[k].text), (int)yy-7);
                lastY = yy;
            }
        }

        axisLabel("exciting", (int)meterY(2, 455.0f)-7);
        axisLabel("present", (int)meterY(2, 496.0f)-7);
        axisLabel("relaxed", (int)meterY(2, 525.0f)-7);

        g.setColour(borderCol);
        g.setOpacity(1.0f);
        g.fillRect(0, (int)(20.0f*vS), getWidth(), 2); // outline backdrop color line
        g.setColour(gridCol);
        g.fillRect(0, (int)(199.0f*vS), getWidth(), 1);
        g.fillRect(0, (int)(399.0f*vS), getWidth(), 1); // meter borders
        g.fillRect(0, (int)(599.0f*vS), getWidth(), 1); // meter borders
        g.setColour(borderCol);
        g.fillRect(0, (int)(200.0f*vS), getWidth(), 2);
        g.fillRect(0, (int)(400.0f*vS), getWidth(), 2); // meter borders
        
        g.setColour(gridCol);
        g.fillRect(0, (int)((stripTop)*vS), getWidth(), 1);
        g.fillRect(0, (int)((200.0f+stripMid)*vS), getWidth(), 1);
        g.fillRect(0, (int)((400.0f+stripMid)*vS), getWidth(), 1); // bottom edge of each meter's text strip

    } else {
        g.setColour(juce::Colours::darkgrey);
        g.setOpacity(1.0f);
        g.fillRect(0, (int)(20.0f*vS), getWidth(), 1); // outline backdrop color line
        g.setColour(juce::Colours::lightgrey);
        g.fillRect(0, (int)(199.0f*vS), getWidth(), 1);
        g.fillRect(0, (int)(399.0f*vS), getWidth(), 1); // meter borders
        g.fillRect(0, (int)(599.0f*vS), getWidth(), 1); // meter borders
       g.setColour(juce::Colours::darkgrey);
        g.fillRect(0, (int)(200.0f*vS), getWidth(), 2);
        g.fillRect(0, (int)(400.0f*vS), getWidth(), 2); // meter borders

        g.setFont(scaleFont*11.0f); //larger font for the tiny window, and the style seen in ConsoleX3
        g.setOpacity(0.21f);
        g.setColour(juce::Colours::white);
        g.drawText(scoreStr, 0, 1, displayWidth, displayHeight+1, juce::Justification::centred, false);
        g.drawText(scoreStr, 1, 0, displayWidth+1, displayHeight, juce::Justification::centred, false);
        g.setOpacity(0.51f);
        g.setColour(juce::Colours::white);
        g.drawText(scoreStr, 1, 1, displayWidth+1, displayHeight+1, juce::Justification::centred, false);
        g.setOpacity(1.0f);
        g.setColour(juce::Colours::black);
        g.drawText(scoreStr, 0, 0, displayWidth, displayHeight, juce::Justification::centred, false);
    }
    
    g.setColour(juce::Colours::grey);
    g.fillRect((int)dataPosition, 0, 1, (int)(599.0f*vS)); //the moving line
    if (textStrips && dark) { //where the moving line crosses each text strip it takes that meter's color (green low end, blue dynamics, red detail)
        for (int sec = 0; sec < 3; ++sec) {
            const int stripFrom = (int)((sec == 0 ? 21.0f : 200.0f*(float)sec + 2.0f) * vS);
            const int stripTo = (int)((200.0f*(float)sec + stripH[sec]) * vS);
            g.setColour(juce::Colour(30, 33, 40));
            g.fillRect((int)dataPosition-2, stripFrom, 5, stripTo - stripFrom);
            g.setColour(stripGlow[sec]);
            g.fillRect((int)dataPosition-1, stripFrom, 3, stripTo - stripFrom);
        }
    }
        
    g.setColour (findColour(juce::ResizableWindow::backgroundColourId).interpolatedWith (juce::Colours::black, 0.25f));
    g.fillRect(0, 0, getWidth(), 2);
    g.fillRect(0, 0, 2, getHeight());
    
    g.setColour (findColour(juce::ResizableWindow::backgroundColourId).interpolatedWith (juce::Colours::black, 0.25f));
    g.fillRect(2, getHeight()-2, getWidth(), 2);
    g.fillRect(getWidth()-2, 2, 2, getHeight()-2);
}
