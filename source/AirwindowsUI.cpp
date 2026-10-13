// AirwindowsUI by Chris Johnson
// Initial seed code for the meter created by Paul Walker on 8/23/21.
#include "AirwindowsUI.h"
#ifndef M_PI
#  define M_PI (3.14159265358979323846)
#endif
#ifndef M_PI_2
#  define M_PI_2 (1.57079632679489661923)
#endif

void AirwindowsMeter::mouseDown(const juce::MouseEvent &event)
{
    float vS = displayHeight/600.0f;
    if ((sqrt(vS*64.0f)*1.618f) > 10.0f && event.x < 150 && event.y < (int)(20.0f*vS)) {
        //TRIM label (top left): click = 1 dB lower, shift-click = 1 dB higher, right-click = back to 0 dB (original Meter).
        //Changing it restarts the measurement, since the held grades were earned at the old level.
        float newTrim = trimDb;
        if (event.mods.isRightButtonDown()) newTrim = 0.0f;
        else if (event.mods.isShiftDown()) newTrim += 1.0f;
        else newTrim -= 1.0f;
        if (newTrim > 0.0f) newTrim = 0.0f;
        if (newTrim < -24.0f) newTrim = -24.0f;
        if (newTrim != trimDb) {trimDb = newTrim; resetArrays();}
        return;
    }
    if (event.getNumberOfClicks() > 1) resetArrays();
    else meterPlaying = !meterPlaying;
}

void AirwindowsMeter::paint(juce::Graphics &g)
{
    float vS = displayHeight/600.0f; // short for vScale: everything * this
    
    const float slewDisplayScale = 0.75f; //slew meter display only: 1.0 = original calibration, lower = more headroom at the top
    
    //LAYOUT: each of the three meters has a text strip along its top (titles, grades and hints), and its data is drawn below that.
    //stripTop is for the zero cross meter (it also holds the top color bar), stripMid is for the peak and slew meters.
    //Units are the 600-high layout. Set both to 0.0f to get the original full-height meters back.
    const bool textStrips = (sqrt(vS*64.0f)*1.618f) > 10.0f; //same test that decides whether the titles get drawn
    const float stripTop = textStrips ? 50.0f : 0.0f;
    const float stripMid = textStrips ? 30.0f : 0.0f;
    const float stripH[3] = {stripTop, stripMid, stripMid};
    auto meterY = [&](int sec, float yv) -> float { //yv is a position in the original layout (0-600), result is in pixels
        const float base = 200.0f*(float)sec;
        return (base + stripH[sec] + (yv-base)*((200.0f-stripH[sec])/200.0f))*vS;
    };
    auto meterH = [&](int sec, float hv) -> float { //a height in the original layout, as pixels
        return hv*((200.0f-stripH[sec])/200.0f)*vS;
    };
    auto slewY = [&](float yv) -> float { //slew meter positions, which also follow slewDisplayScale
        return meterY(2, 600.0f-((600.0f-yv)*slewDisplayScale));
    };
    if ((sqrt(vS*61.8f)*1.618f) > 10.0f) {
        g.fillAll(juce::Colours::white); //blank screen before doing anything, unless our draw covers the whole display anyway
        g.setColour(juce::Colours::lightgrey);
        g.fillRect(0,  (int)meterY(0, 60.0f), getWidth(),1); // -6dB markings
        g.fillRect(0, (int)meterY(0, 101.02f), getWidth(),1); //-12dB markings
        g.fillRect(0, (int)meterY(0, 130.02f), getWidth(),1); //-18dB markings
        g.fillRect(0, (int)meterY(0, 150.2f), getWidth(),1); //-24dB markings
        g.fillRect(0, (int)meterY(0, 164.9f), getWidth(),1); //-30dB markings
        g.fillRect(0, (int)meterY(0, 175.2f), getWidth(),1); //-36dB markings
        
        g.fillRect(0,  (int)meterY(1, 260.0f), getWidth(),1); // -6dB markings
        g.fillRect(0, (int)meterY(1, 301.02f), getWidth(),1); //-12dB markings
        g.fillRect(0, (int)meterY(1, 330.02f), getWidth(),1); //-18dB markings
        g.fillRect(0, (int)meterY(1, 350.2f), getWidth(),1); //-24dB markings
        g.fillRect(0, (int)meterY(1, 364.9f), getWidth(),1); //-30dB markings
        g.fillRect(0, (int)meterY(1, 375.2f), getWidth(),1); //-36dB markings
        
        g.fillRect(0,  (int)slewY(460.0f), getWidth(),1); // -6dB markings
        g.fillRect(0, (int)slewY(501.02f), getWidth(),1); //-12dB markings
        g.fillRect(0, (int)slewY(530.02f), getWidth(),1); //-18dB markings
        g.fillRect(0, (int)slewY(550.2f), getWidth(),1); //-24dB markings
        g.fillRect(0, (int)slewY(564.9f), getWidth(),1); //-30dB markings
        g.fillRect(0, (int)slewY(575.2f), getWidth(),1); //-36dB markings
    } else {
        juce::ColourGradient backdropGradient = juce::ColourGradient::vertical(juce::Colours::white, 0.0f, backdropColour, 600.0f*vS); //600 tall version
        backdropGradient.addColour((float)gradientMin, backdropColour.interpolatedWith(juce::Colours::white, (float)gradientMin));
        //the mid point of the gradient goes toward the bottom and goes whiter when everything's in balance
        g.setFillType(backdropGradient);
        g.fillAll();
    } //blank screen before doing anything, unless our draw covers the whole display anyway
    
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
        float beam = fmin(199.0f,sqrt(dataBL[count]+dataBR[count])*700.0f);
        float departure = fmax(0.0f,5.0f-(dataDL[count]+dataDR[count])*1500.0f) * 100.0f * fmax(dataBL[count],dataBR[count]);
         //begin draw dots on meters L
        if (peakL > 197.0f) {
            g.setColour(juce::Colour(255, 0, 0));
            g.fillRect((float)count, meterY(1, 219.0f+(float)sustainedClip), 1.9f, meterH(1, 181.9f-(float)sustainedClip));
            sustainedClip *= 0.99f; //peak is clipping!
            if (peakR > 197.0f) sustainedClip *= 0.975f; //if both are clipping, escalate
        } else sustainedClip = 180.0f;
        
        g.setColour(juce::Colour(0, 0, 0));
        g.fillRect((float)count, meterY(2, 600.0f-beam), 1.0f, meterH(2, departure)); //draw beam
        
        psDotSizeL = (dataPL[count]*64.0f) / (fabs((peakL*0.945433426957143f)-slewL)+6.18033988749894f);
        slewDotSizeL = (sin(0.1618f/psDotSizeL)*6.18f)+(sqrt(slewL)*0.1618f);
        bassDotSizeL = sqrt(meterZeroL*0.1f*dataPL[count]);
        if (psDotSizeL > 1.0f) g.setColour(juce::Colour::fromFloatRGBA(fmax((slewL-(peakL*0.945433426957143f)),0.0f)*0.029f,
                                                                       fmax(((peakL*0.945433426957143f)-slewL),0.0f)*0.012f, 1.0f, 1.0f));
        else if (slewL > peakL) g.setColour(juce::Colour::fromFloatRGBA(fmin((64.0f+slewL)/128.0f,1.0f), 0.0f, 0.0f, 1.0f));
        else                    g.setColour(juce::Colour::fromFloatRGBA(0.0f, fmax((160.0f-peakL)/160.0f,0.0f), 0.0f, 1.0f)); //set COLOR
        g.fillRect((float)count, meterY(1, 400.0f - fmin(peakL, 200.0f)), psDotSizeL+0.618f, (psDotSizeL+0.618f)*vS);
        
        if (slewDrawL > 194.0f) g.fillRect((float)count, meterY(2, 599.0f-(sqrt(slewDrawL-194.0f)*1.618f)), 1.618f, meterH(2, sqrt(slewDrawL-194.0f)*1.618f));
        else g.fillRect((float)count, meterY(2, 600.0f-slewDrawL), slewDotSizeL+0.618f, slewDotSizeL*vS); //draw slew
        
        if (bassL < 199.0f-(bassDotSizeL+fmax(pow(peakL/140.0f,4.0f),0.0f))) g.fillRect((float)count, meterY(0, fmax(bassL, 0.0f)), bassDotSizeL+fmax(pow(peakL/140.0f,4.0f),0.0f), (bassDotSizeL+fmax(pow(peakL/140.0f,4.0f),0.0f))*vS); //zero cross subs
        //end draw dots on meters L
        //begin draw dots on meters R
        if (peakR > 197.0f) {
            g.setColour(juce::Colour(255, 0, 0));
            g.fillRect((float)count, meterY(1, 219.0f+(float)sustainedClip), 1.9f, meterH(1, 181.9f-(float)sustainedClip));
            sustainedClip *= 0.99f; //peak is clipping!
            if (peakL > 197.0f) sustainedClip *= 0.975f; //if both are clipping, escalate
        } else sustainedClip = 180.0f;
        
        psDotSizeR = (dataPR[count]*64.0f) / (fabs((peakR*0.945433426957143f)-slewR)+6.18033988749894f);
        slewDotSizeR = (sin(0.1618f/psDotSizeR)*6.18f)+(sqrt(slewR)*0.1618f);
        bassDotSizeR = sqrt(meterZeroR*0.1f*dataPR[count]);
        if (count < dataPosition && count > dataPosition-2) {
            backR[count] = (float)storeR;
            backG[count] = (float)storeG;
            backB[count] = (float)storeB; //RGB backdrop for text
        }
        if (psDotSizeR > 1.0f) g.setColour(juce::Colour::fromFloatRGBA(fmax((slewR-(peakR*0.945433426957143f)),0.0f)*0.029f,
                                                                       fmax(((peakR*0.945433426957143f)-slewR),0.0f)*0.012f, 1.0f, 1.0f));
        else if (slewR > peakR) g.setColour(juce::Colour::fromFloatRGBA(fmin((64.0f+slewR)/128.0f,1.0f), 0.0f, 0.0f, 1.0f));
        else                    g.setColour(juce::Colour::fromFloatRGBA(0.0f, fmax((160.0f-peakR)/160.0f,0.0f), 0.0f, 1.0f)); //set COLOR
        g.fillRect((float)count, meterY(1, 400.0f - fmin(peakR, 200.0f)), psDotSizeR+0.618f, (psDotSizeR+0.618f)*vS);
        
        if (slewDrawR > 194.0f) g.fillRect((float)count, meterY(2, 599.0f-(sqrt(slewDrawR-194.0f)*1.618f)), 1.618f, meterH(2, sqrt(slewDrawR-194.0f)*1.618f));
        else g.fillRect((float)count, meterY(2, 600.0f-slewDrawR), slewDotSizeR+0.618f, slewDotSizeR*vS); //draw slew
        
        if (bassR < 199.0f-(bassDotSizeR+fmax(pow(peakR/140.0f,4.0f),0.0f))) g.fillRect((float)count, meterY(0, fmax(bassR, 0.0f)), bassDotSizeR+fmax(pow(peakR/140.0f,4.0f),0.0f), (bassDotSizeR+fmax(pow(peakL/140.0f,4.0f),0.0f))*vS); //zero cross subs
        //end draw dots on meters R
        
        g.setColour(juce::Colour::fromFloatRGBA(backR[count], backG[count], backB[count], 1.0f)); //set backdrop colour
        g.fillRect((float)(count)-0.25f, 0.0f, 1.5f, 19.5f*vS); //top bar
        
        unsigned long bintracker;
        bintracker = (unsigned long)((peakL-((180.0f-sustainedClip)*1.618033988749894f)) * (0.005f*(float)totalBins));
        //converts 0-200 to 0-bin number for textscore bins, and the peak measurement will also try to incorporate varying clip lengths
        //on the grounds that FOR loudenated stuff, variety in the length of total clip registers for our purposes.
        bintracker = (unsigned long)(totalBins - (fabs((int)bintracker-(int)totalBins))); //mirror around max value so it reflects
        if (bintracker > 0 && bintracker <= totalBins) peakTrack[bintracker] += psDotSizeL;
        bintracker = (unsigned long)((peakR-((180.0f-sustainedClip)*1.618033988749894f)) * (0.005f*(float)totalBins));
        bintracker = (unsigned long)(totalBins - (fabs((int)bintracker-(int)totalBins)));
        if (bintracker > 0 && bintracker <= totalBins) peakTrack[bintracker] += psDotSizeR;
        //peak textscore bins
        bintracker = (unsigned long)(slewL * (0.005f*(float)totalBins));//converts 0-200 to 0-bin number for textscore bins
        bintracker = (unsigned long)(totalBins - (fabs((int)bintracker-(int)totalBins))); //mirror around max value so it reflects
        if (bintracker > 0 && bintracker <= totalBins) slewTrack[bintracker] += slewDotSizeL;
        bintracker = (unsigned long)(slewR * (0.005f*(float)totalBins));
        bintracker = (unsigned long)(totalBins - (fabs((int)bintracker-(int)totalBins)));
        if (bintracker > 0 && bintracker <= totalBins) slewTrack[bintracker] += slewDotSizeR;
        //slew textscore bins
        bintracker = (unsigned long)(bassL * (0.005f*(float)totalBins));//converts 0-200 to 0-bin number for textscore bins
        bintracker = (unsigned long)(totalBins - (fabs((int)bintracker-(int)totalBins))); //mirror around max value so it reflects
        if (bintracker > 0 && bintracker <= totalBins) bassTrack[bintracker] += bassDotSizeL;
        bintracker = (unsigned long)(bassR * (0.005f*(float)totalBins));
        bintracker = (unsigned long)(totalBins - (fabs((int)bintracker-(int)totalBins)));
        if (bintracker > 0 && bintracker <= totalBins) bassTrack[bintracker] += bassDotSizeR;
        //bass textscore bins
        
        //bins are for reinforcing score of a dispersed cloud of dot positions rather than maxing out a position
        float peakScore = 0.0;
        float slewScore = 0.0;
        float bassScore = 0.0;
        float sinScale = 0.0;
        for (unsigned long binscale = 0; binscale < totalBins; ++binscale) {
            peakTrack[binscale] = fmax(fmin(peakTrack[binscale]-0.1618033988749894f, 1.618033988749894f),0.0f); //decrement speed
            sinScale = fmax(sin(((float)binscale/(float)totalBins)*(float)M_PI_2),0.0f);
            //sin(1.57) weighting means big red dots in good places, ignore green slews at bottom
            peakScore += (float)sin(peakTrack[binscale]) * sinScale;
            
            slewTrack[binscale] = fmax(fmin(slewTrack[binscale]-0.1618033988749894f, 1.618033988749894f),0.0f); //decrement speed
            slewScore += (float)sin(slewTrack[binscale]) * sinScale;
            
            bassTrack[binscale] = fmax(fmin(bassTrack[binscale]-0.1618033988749894f, 1.618033988749894f),0.0f); //decrement speed
            sinScale = fmax(sin(((float)binscale/(float)totalBins)*(float)M_PI),0.0f);
            //sin(3.1415) does a sine weighting on zero cross meter, but resists overweighting midrange sections
            bassScore += (float)sin(bassTrack[binscale]) * sinScale;
        }
        if (count < dataPosition && count > dataPosition-2) {
            outputB += peakScore;
            outputR += slewScore;
            outputG += bassScore;
            outputVol = sqrt(fmax(fmax(dataPL[count],dataPR[count]),fmax(dataSL[count],dataSR[count])));
            //our volume measurement gets kicked up towards 0 by peak or slew
        }
        if (dataPosition == count) {
            float applyCurve = (float)fmin(fmin(outputR,outputG),outputB) / (float)fmax(fmax(outputR+0.000001f,outputG),outputB);
            applyCurve = 1.0f-pow(1.0f-applyCurve,1.618033988749894f);
            cumulative += applyCurve * outputVol;
            duration += outputVol;
            //when loudness rises it will be paying proportionally more attention to the balance.
            //that means on varying tracks it'll care more about the main part than intros/outros.
        }
        if (count == (unsigned long)dataPosition-1) { //only update text score display more infrequently
            if (pow(peakScore, 1.618033988749894f) > peakPrevGrade) peakPrevGrade = (peakPrevGrade*0.9)+(pow(peakScore, 1.618033988749894f)*0.1);
            if (pow(slewScore, 1.618033988749894f) > slewPrevGrade) slewPrevGrade = (slewPrevGrade*0.9)+(pow(slewScore, 1.618033988749894f)*0.1);
            if (pow(bassScore, 1.618033988749894f) > bassPrevGrade) bassPrevGrade = (bassPrevGrade*0.9)+(pow(bassScore, 1.618033988749894f)*0.1);
            peakGrade = (int)peakPrevGrade; //directly store the value so we can
            slewGrade = (int)slewPrevGrade; //always check to see if it's been higher
            bassGrade = (int)bassPrevGrade; //for two measurements in a row.
            
            if (peakGrade < 0) peakGrade = 0;
            if (peakGrade > 28) peakGrade = 28;
            switch (peakGrade) {
                case 0:
                    power = juce::String("s"); break;
                case 1:
                    power = juce::String("r"); break;
                case 2:
                    power = juce::String("q"); break;
                case 3:
                    power = juce::String("p"); break;
                case 4:
                    power = juce::String("o"); break;
                case 5:
                    power = juce::String("n"); break;
                case 6:
                    power = juce::String("m"); break;
                case 7:
                    power = juce::String("l"); break;
                case 8:
                    power = juce::String("k"); break;
                case 9:
                    power = juce::String("j"); break;
                case 10:
                    power = juce::String("i"); break;
                case 11:
                    power = juce::String("h"); break;
                case 12:
                    power = juce::String("g"); break;
                case 13:
                    power = juce::String("f"); break;
                case 14:
                    power = juce::String("e"); break;
                case 15:
                    power = juce::String("d"); break;
                case 16:
                    power = juce::String("c"); break;
                case 17:
                    power = juce::String("b"); break;
                case 18:
                    power = juce::String("a"); break;
                case 19:
                    power = juce::String("A"); break;
                case 20:
                    power = juce::String("B"); break;
                case 21:
                    power = juce::String("C"); break;
                case 22:
                    power = juce::String("D"); break;
                case 23:
                    power = juce::String("E"); break;
                case 24:
                    power = juce::String("F"); break;
                case 25:
                    power = juce::String("G"); break;
                case 26:
                    power = juce::String("H"); break;
                case 27:
                    power = juce::String("I"); break;
                case 28:
                    power = juce::String("J"); break;
            } //this is our letter score, incorporating all the measurements
            
            if (slewGrade < 0) slewGrade = 0;
            if (slewGrade > 32) slewGrade = 32;
            switch (slewGrade) {
                case 0:
                    detail = juce::String("v"); break;
                case 1:
                    detail = juce::String("u"); break;
                case 2:
                    detail = juce::String("t"); break;
                case 3:
                    detail = juce::String("s"); break;
                case 4:
                    detail = juce::String("r"); break;
                case 5:
                    detail = juce::String("q"); break;
                case 6:
                    detail = juce::String("p"); break;
                case 7:
                    detail = juce::String("o"); break;
                case 8:
                    detail = juce::String("n"); break;
                case 9:
                    detail = juce::String("m"); break;
                case 10:
                    detail = juce::String("l"); break;
                case 11:
                    detail = juce::String("k"); break;
                case 12:
                    detail = juce::String("j"); break;
                case 13:
                    detail = juce::String("i"); break;
                case 14:
                    detail = juce::String("h"); break;
                case 15:
                    detail = juce::String("g"); break;
                case 16:
                    detail = juce::String("f"); break;
                case 17:
                    detail = juce::String("e"); break;
                case 18:
                    detail = juce::String("d"); break;
                case 19:
                    detail = juce::String("c"); break;
                case 20:
                    detail = juce::String("b"); break;
                case 21:
                    detail = juce::String("a"); break;
                case 22:
                    detail = juce::String("A"); break;
                case 23:
                    detail = juce::String("B"); break;
                case 24:
                    detail = juce::String("C"); break;
                case 25:
                    detail = juce::String("D"); break;
                case 26:
                    detail = juce::String("E"); break;
                case 27:
                    detail = juce::String("F"); break;
                case 28:
                    detail = juce::String("G"); break;
                case 29:
                    detail = juce::String("H"); break;
                case 30:
                    detail = juce::String("I"); break;
                case 31:
                    detail = juce::String("J"); break;
                case 32:
                    detail = juce::String("K"); break;
            } //this is our two letter score, incorporating all the measurements
            
            if (bassGrade < 0) bassGrade = 0;
            if (bassGrade > 28) bassGrade = 28;
            switch (bassGrade) {
                case 0:
                    authority = juce::String("s"); break;
                case 1:
                    authority = juce::String("r"); break;
                case 2:
                    authority = juce::String("q"); break;
                case 3:
                    authority = juce::String("p"); break;
                case 4:
                    authority = juce::String("o"); break;
                case 5:
                    authority = juce::String("n"); break;
                case 6:
                    authority = juce::String("m"); break;
                case 7:
                    authority = juce::String("l"); break;
                case 8:
                    authority = juce::String("k"); break;
                case 9:
                    authority = juce::String("j"); break;
                case 10:
                    authority = juce::String("i"); break;
                case 11:
                    authority = juce::String("h"); break;
                case 12:
                    authority = juce::String("g"); break;
                case 13:
                    authority = juce::String("f"); break;
                case 14:
                    authority = juce::String("e"); break;
                case 15:
                    authority = juce::String("d"); break;
                case 16:
                    authority = juce::String("c"); break;
                case 17:
                    authority = juce::String("b"); break;
                case 18:
                    authority = juce::String("a"); break;
                case 19:
                    authority = juce::String("A"); break;
                case 20:
                    authority = juce::String("B"); break;
                case 21:
                    authority = juce::String("C"); break;
                case 22:
                    authority = juce::String("D"); break;
                case 23:
                    authority = juce::String("E"); break;
                case 24:
                    authority = juce::String("F"); break;
                case 25:
                    authority = juce::String("G"); break;
                case 26:
                    authority = juce::String("H"); break;
                case 27:
                    authority = juce::String("I"); break;
                case 28:
                    authority = juce::String("J"); break;
            } //this is our letter score, incorporating all the measurements
            
            totalPackage = juce::String(" ");
            double allMatch = (1.0-pow(1.0-(cumulative/duration),1.618033988749894))*27.618033988749894;
            if (allMatch < 0.0) allMatch = 0.0;
            if (allMatch > 27.0) allMatch = 27.0;
            switch ((int)allMatch) {
                case 0:
                    totalPackage = juce::String(" "); break;
                case 1:
                    totalPackage = juce::String(" "); break;
                case 2:
                    totalPackage = juce::String("Z"); break;
                case 3:
                    totalPackage = juce::String("Y"); break;
                case 4:
                    totalPackage = juce::String("X"); break;
                case 5:
                    totalPackage = juce::String("W"); break;
                case 6:
                    totalPackage = juce::String("V"); break;
                case 7:
                    totalPackage = juce::String("U"); break;
                case 8:
                    totalPackage = juce::String("T"); break;
                case 9:
                    totalPackage = juce::String("S"); break;
                case 10:
                    totalPackage = juce::String("R"); break;
                case 11:
                    totalPackage = juce::String("Q"); break;
                case 12:
                    totalPackage = juce::String("P"); break;
                case 13:
                    totalPackage = juce::String("O"); break;
                case 14:
                    totalPackage = juce::String("N"); break;
                case 15:
                    totalPackage = juce::String("M"); break;
                case 16:
                    totalPackage = juce::String("L"); break;
                case 17:
                    totalPackage = juce::String("K"); break;
                case 18:
                    totalPackage = juce::String("J"); break;
                case 19:
                    totalPackage = juce::String("I"); break;
                case 20:
                    totalPackage = juce::String("H"); break;
                case 21:
                    totalPackage = juce::String("G"); break;
                case 22:
                    totalPackage = juce::String("F"); break;
                case 23:
                    totalPackage = juce::String("E"); break;
                case 24:
                    totalPackage = juce::String("D"); break;
                case 25:
                    totalPackage = juce::String("C"); break;
                case 26:
                    totalPackage = juce::String("B"); break;
                case 27:
                    totalPackage = juce::String("A"); break;
            } //this is our letter score, incorporating all the measurements
            
            durationPlus += 1.0;
            double cumulativeAmount = 1.0;
            double blackLine = fmax(0.0f,5.0f-(dataDL[count]+dataDR[count])*1500.0f) * 100.0f * fmax(dataBL[count],dataBR[count])*0.2;
            cumulativeAmount = fmax(fabs((peakPrevGrade) - (slewPrevGrade-3.0)),cumulativeAmount);
            cumulativeAmount = fmax(fabs((bassPrevGrade) - (slewPrevGrade-3.0)),cumulativeAmount);
            cumulativeAmount = fmax(fabs((peakPrevGrade) - (bassPrevGrade)),cumulativeAmount);
            cumulativePlus += (blackLine/(cumulativeAmount*cumulativeAmount));
            if (cumulativePlus > durationPlus) cumulativePlus = durationPlus;
            
            bonusScore = juce::String(100-(int)fmin((cumulativePlus/durationPlus)*100.0,99.0));
            if (bonusScore.compare("100") < 1 && !(bonusScore.compare("10") < 1)) bonusScore = juce::String();
            else bonusScore = bonusScore + "-";
            if (100-(int)((cumulativePlus/durationPlus)*100.0) > 80) bonusScore = juce::String();
            //Meter is going to do not a top 40, but a top double 40. 1 to 80, then nothing.
            //When we include from 81 to 99 in there, it starts filling up with too many extra tracks
            //that are better tracked just using the letter rating and letter grades, which end up
            //being more predictive than the number when it's over 80.
            
            //PER-METER HINTS: the three scores are compared to the strongest of the three. The tone color is just these
            //three scores as red (slews), green (zero crosses) and blue (peaks), so white means all three are in balance.
            //A meter that is well behind the strongest gets a hint in its own top strip, and the further behind it is, the
            //stronger the hint shows. Quiet passages are gated out, like the tone color does.
            {
                const float hintStart = 0.75f; //a meter below this fraction of the strongest starts to show a hint
                const float hintFull = 0.45f;  //and below this fraction the hint is fully visible
                float scoreMax = (float)fmax(fmax(outputR, outputG), fmax(outputB, 0.000001));
                float scoreRatio[3] = {(float)(outputG/scoreMax), (float)(outputB/scoreMax), (float)(outputR/scoreMax)}; //zero crosses, peaks, slews
                float quietGate = (float)fmin(outputVol*outputVol*8.0, 1.0);
                hintText[0] = juce::String("more low end");
                hintText[1] = juce::String("more dynamics");
                hintText[2] = juce::String("more HF transients");
                for (int hint = 0; hint < 3; ++hint) {
                    hintOpacity[hint] = fmax(fmin((hintStart-scoreRatio[hint])/(hintStart-hintFull), 1.0f), 0.0f) * quietGate;
                }
            }
        }
    }
    
    float scaleFont = (sqrt(vS*64.0f)*1.618f);
    if (scaleFont > 10.0f) {
        g.setFont(scaleFont*1.618f);
        g.setColour(juce::Colours::black);
        g.drawText("authority "+authority, (int)scaleFont/2, (int)(23.0f*vS), displayWidth/2, 32, juce::Justification::topLeft);
        g.drawText("zero crosses", (displayWidth/2)-(int)(scaleFont*0.618f), (int)(23.0f*vS), displayWidth/2, 32, juce::Justification::topRight);
        g.setOpacity(fmin(hintOpacity[0],1.0f));
        g.drawText(hintText[0], 0, (int)(23.0f*vS), displayWidth, 32, juce::Justification::centredTop);
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
        g.drawText("power "+power, (int)scaleFont/2, (int)(203*vS), displayWidth/3, 32, juce::Justification::topLeft);
        g.drawText("peaks", (displayWidth/2)-(int)(scaleFont*0.618f), (int)(203*vS), displayWidth/2, 32, juce::Justification::topRight);
        g.setOpacity(fmin(hintOpacity[1],1.0f));
        g.drawText(hintText[1], 0, (int)(203*vS), displayWidth, 32, juce::Justification::centredTop);
        g.setOpacity(1.0f);
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
        
        g.drawText("detail "+detail, (int)scaleFont/2, (int)(403*vS), displayWidth/2, 32, juce::Justification::topLeft);
        g.drawText("slews", (displayWidth/2)-(int)(scaleFont*0.618f), (int)(403*vS), displayWidth/2, 32, juce::Justification::topRight);
        g.setOpacity(fmin(hintOpacity[2],1.0f));
        g.drawText(hintText[2], 0, (int)(403*vS), displayWidth, 32, juce::Justification::centredTop);
        g.setOpacity(1.0f);
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
        g.drawText(bonusScore+totalPackage+authority+power+detail, 1, 1, displayWidth, (int)(20.0f*vS), juce::Justification::centred);
        //underdrawing in white for areas prone to get covered up with dots
        g.setOpacity(1.0f);
        g.setColour(juce::Colours::black);
        g.drawText(bonusScore+totalPackage+authority+power+detail, 0, 0, displayWidth, (int)(20.0f*vS), juce::Justification::centred);
        
        //TRIM readout, top left. Dim at 0 dB (original Meter behaviour), full strength when trimming, so screenshots show the mode.
        juce::String trimText = "trim " + juce::String(trimDb, 1) + " dB";
        g.setFont(scaleFont*1.1f);
        g.setColour(juce::Colours::white);
        g.setOpacity(trimDb == 0.0f ? 0.3f : 0.618f);
        g.drawText(trimText, (int)scaleFont/2+1, 1, 140, (int)(20.0f*vS), juce::Justification::centredLeft);
        g.setColour(juce::Colours::black);
        g.setOpacity(trimDb == 0.0f ? 0.5f : 1.0f);
        g.drawText(trimText, (int)scaleFont/2, 0, 140, (int)(20.0f*vS), juce::Justification::centredLeft);
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
        g.drawText("900 Hz", (int)scaleFont, (int)meterY(0, 60.0f)-7, displayWidth/2, (int)scaleFont, juce::Justification::bottomLeft);
        g.drawText("210 Hz", (int)scaleFont, (int)meterY(0, 101.02f)-7, displayWidth/2, (int)scaleFont, juce::Justification::bottomLeft);
        g.drawText("100 Hz", (int)scaleFont, (int)meterY(0, 130.02f)-7, displayWidth/2, (int)scaleFont, juce::Justification::bottomLeft);
        g.drawText("60 Hz", (int)scaleFont, (int)meterY(0, 150.2f)-7, displayWidth/2, (int)scaleFont, juce::Justification::bottomLeft);
        g.drawText("45 Hz", (int)scaleFont, (int)meterY(0, 164.9f)-7, displayWidth/2, (int)scaleFont, juce::Justification::bottomLeft);
        g.drawText("35 Hz", (int)scaleFont, (int)meterY(0, 175.2f)-7, displayWidth/2, (int)scaleFont, juce::Justification::bottomLeft);
        
        g.drawText("-6 dB", (int)scaleFont, (int)meterY(1, 260.0f)-7, displayWidth/2, (int)scaleFont, juce::Justification::bottomLeft);
        g.drawText("-12 dB", (int)scaleFont, (int)meterY(1, 301.02f)-7, displayWidth/2, (int)scaleFont, juce::Justification::bottomLeft);
        g.drawText("-18 dB", (int)scaleFont, (int)meterY(1, 330.02f)-7, displayWidth/2, (int)scaleFont, juce::Justification::bottomLeft);
        g.drawText("-24 dB", (int)scaleFont, (int)meterY(1, 350.2f)-7, displayWidth/2, (int)scaleFont, juce::Justification::bottomLeft);
        g.drawText("-30 dB", (int)scaleFont, (int)meterY(1, 364.9f)-7, displayWidth/2, (int)scaleFont, juce::Justification::bottomLeft);
        g.drawText("-36 dB", (int)scaleFont, (int)meterY(1, 375.2f)-7, displayWidth/2, (int)scaleFont, juce::Justification::bottomLeft);

        g.drawText("exciting", (int)scaleFont, (int)slewY(455.0f)-7, displayWidth/2, (int)scaleFont, juce::Justification::bottomLeft);
        g.drawText("present", (int)scaleFont, (int)slewY(496.0f)-7, displayWidth/2, (int)scaleFont, juce::Justification::bottomLeft);
        g.drawText("relaxed", (int)scaleFont, (int)slewY(525.0f)-7, displayWidth/2, (int)scaleFont, juce::Justification::bottomLeft);

        g.setColour(juce::Colours::darkgrey);
        g.setOpacity(1.0f);
        g.fillRect(0, (int)(20.0f*vS), getWidth(), 2); // outline backdrop color line
        g.setColour(juce::Colours::lightgrey);
        g.fillRect(0, (int)(199.0f*vS), getWidth(), 1);
        g.fillRect(0, (int)(399.0f*vS), getWidth(), 1); // meter borders
        g.fillRect(0, (int)(599.0f*vS), getWidth(), 1); // meter borders
        g.setColour(juce::Colours::darkgrey);
        g.fillRect(0, (int)(200.0f*vS), getWidth(), 2);
        g.fillRect(0, (int)(400.0f*vS), getWidth(), 2); // meter borders
        
        g.setColour(juce::Colours::lightgrey);
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
        g.drawText(bonusScore+totalPackage+authority+power+detail, 0, 1, displayWidth, displayHeight+1, juce::Justification::centred, false);
        g.drawText(bonusScore+totalPackage+authority+power+detail, 1, 0, displayWidth+1, displayHeight, juce::Justification::centred, false);
        g.setOpacity(0.51f);
        g.setColour(juce::Colours::white);
        g.drawText(bonusScore+totalPackage+authority+power+detail, 1, 1, displayWidth+1, displayHeight+1, juce::Justification::centred, false);
        g.setOpacity(1.0f);
        g.setColour(juce::Colours::black);
        g.drawText(bonusScore+totalPackage+authority+power+detail, 0, 0, displayWidth, displayHeight, juce::Justification::centred, false);
    }
    
    g.setColour(juce::Colours::grey);
    g.fillRect((int)dataPosition, 0, 1, (int)(599.0f*vS)); //the moving line
        
    g.setColour (findColour(juce::ResizableWindow::backgroundColourId).interpolatedWith (juce::Colours::black, 0.25f));
    g.fillRect(0, 0, getWidth(), 2);
    g.fillRect(0, 0, 2, getHeight());
    
    g.setColour (findColour(juce::ResizableWindow::backgroundColourId).interpolatedWith (juce::Colours::black, 0.25f));
    g.fillRect(2, getHeight()-2, getWidth(), 2);
    g.fillRect(getWidth()-2, 2, 2, getHeight()-2);
}
