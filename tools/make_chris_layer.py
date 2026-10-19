#!/usr/bin/env python3
"""meterDARK is a skin: underneath it runs Chris's ORIGINAL Airwindows Meter code, untouched.

chris/<id>/ holds one original Meter version's source exactly as published (AirwindowsUI.cpp/.h, PluginEditor.cpp,
PluginProcessor.cpp/.h) and upstream.json. For each one this writes source/chris/Chris_<id>.h containing, word for word:
  - the audio-thread part of his PluginProcessor (message types, state, both processBlocks), talking to the real host
  - his AirwindowsMeter struct with its paint() and mouseDown() (its drawing calls go nowhere: only its scoring is used)
  - his editor's message switch from PluginEditor::idle()
in namespace chris_<id>. source/chris/Current.h picks the version meterDARK is built on: the newest, or the one named
on the command line (python3 tools/make_chris_layer.py 0.5.5-18d9cf5). Nothing in the output is edited by hand.
Exit code 1 if a version's source is shaped differently from what this script understands."""
import json, pathlib, re, sys

root = pathlib.Path(__file__).resolve().parent.parent
src = root / "chris"; out = root / "source" / "chris"
out.mkdir(parents=True, exist_ok=True)

def fail(msg): print("make_chris_layer: " + msg, file=sys.stderr); sys.exit(1)
def block_from(text, start):
    i = text.index("{", start); depth = 0
    for j in range(i, len(text)):
        if text[j] == "{": depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0: return text[start:j + 1]
    fail("unbalanced braces")
def ident(vid): return "v" + re.sub(r"[^A-Za-z0-9]", "_", vid)

dirs = sorted((p for p in src.iterdir() if p.is_dir()), key=lambda p: [int(x) if x.isdigit() else x for x in re.split(r"[.-]", p.name)])
for d in dirs:
    f = {n: (d / n).read_text() for n in ["AirwindowsUI.cpp", "AirwindowsUI.h", "PluginEditor.cpp", "PluginProcessor.cpp", "PluginProcessor.h"]}
    up = json.loads((d / "upstream.json").read_text())
    vid = d.name; ns = "chris_" + ident(vid)
    ph = f["PluginProcessor.h"]; a = ph.find("enum Parameters"); b = ph.find("private:", a)
    if a < 0 or b < 0: fail(f"{vid}: PluginProcessor.h layout not recognised")
    members = ph[a:b]
    m = re.search(r"struct AudioToUIMessage\s*\{\s*enum What\s*\{([^}]*)\}", members)
    whats = [w.strip() for w in re.sub(r"//[^\n]*", "", m.group(1)).split(",") if w.strip()]
    pc = f["PluginProcessor.cpp"]; blocks = []
    for t in ["float", "double"]:
        k = pc.find(f"void PluginProcessor::processBlock (juce::AudioBuffer<{t}>&")
        if k < 0: fail(f"{vid}: {t} processBlock not found")
        blocks.append(block_from(pc, k).replace("void PluginProcessor::processBlock", "void processBlock", 1))
    uh = f["AirwindowsUI.h"]; s = uh.find("struct AirwindowsMeter")
    struct = block_from(uh, s) + ";"
    uc = f["AirwindowsUI.cpp"]; s = uc.find("void AirwindowsMeter::mouseDown")
    body = uc[s:].replace("void AirwindowsMeter::mouseDown", "inline void AirwindowsMeter::mouseDown", 1).replace("void AirwindowsMeter::paint", "inline void AirwindowsMeter::paint", 1)
    pe = f["PluginEditor.cpp"]; s = pe.find("switch (msg.what)")
    if s < 0: fail(f"{vid}: editor message switch not found")
    sw = block_from(pe, s)
    # (added) the level gain: meterDARK can give the audio loop a scratch copy of the audio at an anchor sample rate and with a gain. Each message
    # carries the gain that audio had, so meterDARK's own display can take it off again. 1 = nothing applied = exactly Chris's code.
    m2 = re.search(r"struct AudioToUIMessage\s*\{.*?float newValue = 0\.0;", members, re.S)
    if m2 is None: fail(f"{vid}: AudioToUIMessage not shaped as expected")
    members = members[:m2.end()] + " float mdGain = 1.0f; //(added) gain the scratch audio carried when this window was measured" + members[m2.end():]
    for i in range(2):
        if blocks[i].count("AudioToUIMessage msg;") != 1: fail(f"{vid}: message creation not shaped as expected")
        blocks[i] = blocks[i].replace("AudioToUIMessage msg;", "AudioToUIMessage msg; msg.mdGain = (float)mdGainNow; //(added)", 1)
    names = ", ".join(f'"{w}"' for w in whats)
    # THE TUNED COPY (namespace <ns>_tuned): the same meter with meterDARK's two Detail settings added to its slew bins. With both at 0
    # it is identical to the original. Only used when "grade" is ticked; the original above always runs too.
    fill = "slewTrack[bintracker] += slewDotSize"
    dec = "slewTrack[binscale] = fmax(fmin(slewTrack[binscale]-0.1618033988749894f, 1.618033988749894f),0.0f);"
    roll = "float peakScore = 0.0;"
    if body.count(fill) != 2 or body.count(dec) != 1 or body.count(roll) != 1: fail(f"{vid}: slew bins not shaped as expected (tuned copy)")
    tbody = body.replace(fill + "L;", "mdSlewFill(bintracker, slewDotSizeL);").replace(fill + "R;", "mdSlewFill(bintracker, slewDotSizeR);")
    tbody = tbody.replace(dec, "{const float u = (float)binscale/(float)totalBins; slewTrack[binscale] = fmax(fmin(slewTrack[binscale]-0.1618033988749894f*(1.0f+mdDecay*4.0f*u*u), 1.618033988749894f),0.0f);} //(tuned) hot bins empty faster")
    tbody = tbody.replace(roll, "mdRollHits(); //(tuned)\n        " + roll)
    if "mdSlewFill(bintracker, slewDotSizeL)" not in tbody or "mdSlewFill(bintracker, slewDotSizeR)" not in tbody: fail(f"{vid}: slew fills not found (tuned copy)")
    tstruct = struct[:struct.rindex("}")] + """    //(tuned) DETAIL DECAY: hot slew bins (high up the meter) empty faster, so a brief overslew fades quickly and a sustained one keeps refilling.
    //FILL GATE: a hot slew bin fills at full strength only if it was also hit in the window before; a lone hit counts for less, more so the hotter the bin.
    float mdDecay = 0.0f, mdGate = 0.0f;
    bool mdHitPrev[totalBins+2] = {}, mdHitNow[totalBins+2] = {};
    void mdSlewFill(unsigned long b, float size) {
        if (b > 0 && b <= totalBins) { const float up = (float)b/(float)totalBins; slewTrack[b] += size*(mdHitPrev[b] ? 1.0f : 1.0f - 0.6f*mdGate*up); mdHitNow[b] = true; }
    }
    void mdRollHits() { for (int b = 0; b < totalBins+2; ++b) {mdHitPrev[b] = mdHitNow[b]; mdHitNow[b] = false;} }
};"""
    text = f"""// GENERATED by tools/make_chris_layer.py from chris/{vid} (Airwindows Meter {up['version']}, {up['commit']}, {up['date']}). Do not edit by hand.
#pragma once
#include "../ChrisMix.h"
#ifndef M_PI
#  define M_PI (3.14159265358979323846)
#endif
#ifndef M_PI_2
#  define M_PI_2 (1.57079632679489661923)
#endif
namespace {ns} {{
namespace juce = ::mdmix::juce;
static constexpr const char* kVersion = "{up['version']}";
static constexpr const char* kCommit = "{up['commit']}";

//==== Chris's PluginProcessor: its audio thread, talking to meterDARK's real processor for the host
struct PluginProcessor {{
{members}
    ::juce::AudioProcessor* host = nullptr; //(added) the real processor
    double mdRate = 0.0;     //(added) 0 = the host's sample rate; else the anchor rate meterDARK converted the audio to
    double mdGainNow = 1.0;  //(added) the level gain on the audio being measured (1 = none)
    double getSampleRate() const {{ return mdRate > 0.0 ? mdRate : host->getSampleRate(); }}
    int getTotalNumInputChannels() const {{ return host->getTotalNumInputChannels(); }}
    int getTotalNumOutputChannels() const {{ return host->getTotalNumOutputChannels(); }}
    ::juce::AudioProcessor::Bus* getBus(bool in, int i) {{ return host->getBus(in, i); }}
    template <typename B> auto getBusBuffer(B& b, bool in, int i) {{ return host->getBusBuffer(b, in, i); }}
    ::juce::AudioPlayHead* getPlayHead() {{ return host->getPlayHead(); }}
{blocks[0]}
{blocks[1]}
}};

//==== Chris's meter (its scoring runs once per audio window; its drawing calls go nowhere)
{struct}

{body}

//==== Chris's editor: what it does with each message
inline void dispatch(AirwindowsMeter& meter, const PluginProcessor::AudioToUIMessage& msg, bool& repaintTS) {{
    {sw}
}}
static const char* kWhatNames[] = {{{names}}};
static constexpr int kNumWhats = {len(whats)};
}} // namespace {ns}

//==== The tuned copy: Chris's meter with meterDARK's Detail decay and fill gate (identical to the original when both are 0)
namespace {ns}_tuned {{
namespace juce = ::mdmix::juce;
using PluginProcessor = ::{ns}::PluginProcessor;
{tstruct}

{tbody}

inline void dispatch(AirwindowsMeter& meter, const PluginProcessor::AudioToUIMessage& msg, bool& repaintTS) {{
    {sw}
}}
}} // namespace {ns}_tuned
"""
    (out / f"Chris_{ident(vid)}.h").write_text(text)
    print(f"{vid}: {len(whats)} message types")

pick = sys.argv[1] if len(sys.argv) > 1 else dirs[-1].name
if not (src / pick).is_dir(): fail(f"no chris/{pick}")
(out / "Current.h").write_text(f"""// GENERATED by tools/make_chris_layer.py: the original Meter version this meterDARK is built on.
#pragma once
#include "Chris_{ident(pick)}.h"
namespace chris = chris_{ident(pick)};
namespace chrisTuned = chris_{ident(pick)}_tuned; //the same, with meterDARK's Detail decay and fill gate (used when "grade" is ticked)
""")
print("meterDARK is built on", pick)
