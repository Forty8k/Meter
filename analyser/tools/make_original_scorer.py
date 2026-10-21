#!/usr/bin/env python3
"""Generates the analyser's copy of every ORIGINAL Airwindows Meter version in analyser/original/<id>/.

Each folder holds that version's untouched source (AirwindowsUI.cpp/.h, PluginEditor.cpp, PluginProcessor.cpp/.h) and an
upstream.json ({"version","commit","date"}). For each one this writes src/gen/Meter_<id>.inc, containing word for word:
  - the audio-thread part of PluginProcessor (its message types, its state variables and its float processBlock)
  - the AirwindowsMeter struct and its paint()/mouseDown()
  - the editor's message switch from PluginEditor::idle()
wrapped in namespace mdarkorig_<id> so it compiles against the stand-ins in OriginalStub.h, plus a small runner that drives
them the way the plugin does in real time. src/gen/Versions.inc lists them all. Nothing in the output is edited by hand.
Exit code 1 (with a message) if a version's source is shaped differently from what this script understands."""
import json, pathlib, re, sys

here = pathlib.Path(__file__).resolve().parent.parent
origin = here / "original"
gen = here / "src" / "gen"
gen.mkdir(parents=True, exist_ok=True)

def fail(msg):
    print("make_original_scorer: " + msg, file=sys.stderr); sys.exit(1)

def block_from(text, start):
    """text from index start (at or before an opening brace) to the matching closing brace, inclusive"""
    i = text.index("{", start); depth = 0
    for j in range(i, len(text)):
        if text[j] == "{": depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0: return text[start:j + 1]
    fail("unbalanced braces")

def ident(vid): return "v" + re.sub(r"[^A-Za-z0-9]", "_", vid)

# TUNABLE copy (newest version only): the same code with chosen constants read from the Lab's settings instead.
# Each rule: (setting, text to find, replacement, where). A rule that finds nothing switches that Lab setting off for
# this version (the code changed), it never fails the build.
RULES = [
    ("decayPeak",  "peakTrack[binscale]-0.1618033988749894f", "peakTrack[binscale]-(float)P().decayPeak", "paint"),
    ("decaySlew",  "slewTrack[binscale]-0.1618033988749894f", "slewTrack[binscale]-(float)P().decaySlew", "paint"),
    ("decayBass",  "bassTrack[binscale]-0.1618033988749894f", "bassTrack[binscale]-(float)P().decayBass", "paint"),
    ("binCap",     "-(float)P().decayPeak, 1.618033988749894f)", "-(float)P().decayPeak, (float)P().binCap)", "paint"),
    ("binCap",     "-(float)P().decaySlew, 1.618033988749894f)", "-(float)P().decaySlew, (float)P().binCap)", "paint"),
    ("binCap",     "-(float)P().decayBass, 1.618033988749894f)", "-(float)P().decayBass, (float)P().binCap)", "paint"),
    ("curvePS",    "*(float)M_PI_2),0.0f)", "*(float)P().curvePS),0.0f)", "paint"),
    ("curveBass",  "*(float)M_PI),0.0f)", "*(float)P().curveBass),0.0f)", "paint"),
    ("gradeExp",   r"re:pow\((peak|slew|bass)Score, 1\.618033988749894f\)", r"pow(\1Score, (float)P().gradeExp)", "paint"),
    ("gradeRise",  r"re:\((peak|slew|bass)PrevGrade\*0\.9\)\+\(pow\((\w+), \(float\)P\(\)\.gradeExp\)\*0\.1\)", r"(\1PrevGrade*(1.0-P().gradeRise))+(pow(\2, (float)P().gradeExp)*P().gradeRise)", "paint"),
    ("balanceExp", "applyCurve = 1.0f-pow(1.0f-applyCurve,1.618033988749894f);", "applyCurve = 1.0f-pow(1.0f-applyCurve,(float)P().balanceExp);", "paint"),
    ("letterExp",  "pow(1.0-(cumulative/duration),1.618033988749894)", "pow(1.0-(cumulative/duration),P().letterExp)", "paint"),
    ("clipLevel",  "> 197.0f", "> (float)P().clipLevel", "paint"),
    ("clipDecay",  "sustainedClip *= 0.99f;", "sustainedClip *= (float)P().clipDecay;", "paint"),
    ("clipDecayBoth", "sustainedClip *= 0.975f;", "sustainedClip *= (float)P().clipDecayBoth;", "paint"),
    ("clipWeight", "(180.0f-sustainedClip)*1.618033988749894f)", "(180.0f-sustainedClip)*(float)P().clipWeight)", "paint"),
    ("blackLine",  " * departureSlew)*0.3;", " * departureSlew)*P().blackLine;", "paint"),
    ("toneDecay",  "*0.125f*meter.outputVol", "*(float)P().toneDecay*meter.outputVol", "editor"),
    # meterDARK's two Detail settings (0 = the original): the same maths as meterDARK's tuned copy (tools/make_chris_layer.py there)
    ("detailDecay", "slewTrack[binscale]-(float)P().decaySlew,", "slewTrack[binscale]-(float)P().decaySlew*(1.0f+(float)P().detailDecay*4.0f*((float)binscale/(float)totalBins)*((float)binscale/(float)totalBins)),", "paint"),
    ("fillGate",   "slewTrack[bintracker] += slewDotSizeL;", "mdarkSlewFill(bintracker, slewDotSizeL);", "paint"),
    ("fillGate",   "slewTrack[bintracker] += slewDotSizeR;", "mdarkSlewFill(bintracker, slewDotSizeR);", "paint"),
    ("fillGate",   "float peakScore = 0.0;", "mdarkRollHits(); float peakScore = 0.0;", "paint"),
]
FILL_GATE_MEMBERS = """    // (tunable copy) meterDARK's fill gate: a hot slew bin fills fully only if it was also hit in the window before
    bool mdarkHitPrev[totalBins+2] = {}, mdarkHitNow[totalBins+2] = {};
    void mdarkSlewFill(unsigned long b, float size) {
        if (b > 0 && b <= totalBins) { const float up = (float)b/(float)totalBins; slewTrack[b] += size*(mdarkHitPrev[b] ? 1.0f : 1.0f - 0.6f*(float)P().fillGate*up); mdarkHitNow[b] = true; }
    }
    void mdarkRollHits() { for (int b = 0; b < totalBins+2; ++b) {mdarkHitPrev[b] = mdarkHitNow[b]; mdarkHitNow[b] = false;} }
"""

def apply_rules(body, sw):
    used = []
    for key, find, repl, where in RULES:
        src = body if where == "paint" else sw
        if find.startswith("re:"):
            new, n = re.subn(find[3:], repl, src)
        else:
            n = src.count(find); new = src.replace(find, repl)
        if n > 0 and key not in used: used.append(key)
        if where == "paint": body = new
        else: sw = new
    return body, sw, used

versions = []
dirs = sorted(p for p in origin.iterdir() if p.is_dir())
tunable_used = {}   # version id -> the settings its tunable copy has
for d in dirs:
    files = {n: (d / n).read_text() for n in ["AirwindowsUI.cpp", "AirwindowsUI.h", "PluginEditor.cpp", "PluginProcessor.cpp", "PluginProcessor.h"] if (d / n).exists()}
    if len(files) < 5: fail(f"{d.name}: missing source files")
    up = json.loads((d / "upstream.json").read_text()) if (d / "upstream.json").exists() else {}
    vid = d.name; ns = "mdarkorig_" + ident(vid)
    ph = files["PluginProcessor.h"]
    # the audio-thread members: from "enum Parameters" to the "private:" that closes the public section
    a = ph.find("enum Parameters"); b = ph.find("private:", a)
    if a < 0 or b < 0: fail(f"{vid}: PluginProcessor.h layout not recognised")
    members = ph[a:b]
    m = re.search(r"struct AudioToUIMessage\s*\{\s*enum What\s*\{([^}]*)\}", members)
    if not m: fail(f"{vid}: AudioToUIMessage not found")
    whats = [w.strip() for w in re.sub(r"//[^\n]*", "", m.group(1)).split(",") if w.strip()]
    pc = files["PluginProcessor.cpp"]
    k = pc.find("void PluginProcessor::processBlock (juce::AudioBuffer<float>&")
    if k < 0: k = pc.find("void PluginProcessor::processBlock(juce::AudioBuffer<float>&")
    if k < 0: fail(f"{vid}: float processBlock not found")
    pb = block_from(pc, k).replace("void PluginProcessor::processBlock", "void processBlock", 1)
    uh = files["AirwindowsUI.h"]
    s = uh.find("struct AirwindowsMeter")
    if s < 0: fail(f"{vid}: AirwindowsMeter not found")
    struct = block_from(uh, s) + ";"
    uc = files["AirwindowsUI.cpp"]
    s = uc.find("void AirwindowsMeter::mouseDown")
    if s < 0: s = uc.find("void AirwindowsMeter::paint")
    body = uc[s:]
    pe = files["PluginEditor.cpp"]
    s = pe.find("switch (msg.what)")
    if s < 0: fail(f"{vid}: editor message switch not found")
    sw = block_from(pe, s)
    for need in ["directions", "outputVol", "cumulativePlus", "bonusScore", "totalPackage", "peakPrevGrade"]:
        if need not in struct: fail(f"{vid}: the meter no longer has '{need}', the runner needs updating")
    names_cpp = ", ".join(f'"{w}"' for w in whats)
    variants = [("", body, sw)]
    tb, tsw, used_here = apply_rules(body, sw)   # a tunable copy of EVERY version (gradeLAB lets you pick which one to experiment with)
    tunable_used[vid] = used_here
    variants.append(("_tunable", tb, tsw))
    base_struct = struct
    for suffix, body, sw in variants:
      nsv = ns + suffix
      struct = base_struct
      if suffix and "mdarkSlewFill" in body: struct = base_struct[:base_struct.rindex("}")] + FILL_GATE_MEMBERS + "};"
      extra = "inline ::mdark::ScoreParams& P() { return ::mdark::scoreParams(); }\n" if suffix else ""
      width = "(int)::mdark::scoreParams().width" if suffix else "1058"
      out = f"""// GENERATED by tools/make_original_scorer.py from analyser/original/{vid} (Airwindows Meter {up.get('version','?')}, {up.get('commit','')}). Do not edit by hand.
#ifndef M_PI
#  define M_PI (3.14159265358979323846)
#endif
#ifndef M_PI_2
#  define M_PI_2 (1.57079632679489661923)
#endif
namespace {nsv} {{
namespace juce = ::mdarkstub::juce;
{extra}
//==== the original PluginProcessor's audio thread, with stand-ins for the host
struct PluginProcessor {{
{members}
    // ---- stand-ins for the host (added by the analyser)
    double sr = 44100.0;
    double getSampleRate() const {{ return sr; }}
    int getTotalNumInputChannels() const {{ return 2; }}
    int getTotalNumOutputChannels() const {{ return 2; }}
    struct Bus {{ bool isEnabled() const {{ return true; }} }} bus;
    Bus* getBus(bool, int) {{ return &bus; }}
    template <typename B> B& getBusBuffer(B& b, bool, int) {{ return b; }}
    juce::AudioPlayHead* getPlayHead() {{ return nullptr; }}
    // ---- the original processBlock
{pb}
}};

//==== the original meter
{struct}

{body}

//==== the original editor's handling of each message (from PluginEditor::idle)
inline void dispatch(AirwindowsMeter& meter, const PluginProcessor::AudioToUIMessage& msg, bool& repaintTS) {{
    {sw}
}}

//==== the runner (added by the analyser): drives the above the way the plugin does in real time
static const char* kWhatNames[] = {{{names_cpp}}};
static constexpr int kNumWhats = {len(whats)};
class Run : public ::mdark::MeterRun {{
public:
    Run(double sampleRate, int pe) : paintEvery(pe < 1 ? 1 : pe) {{
        proc.sr = sampleRate;
        meter.w = {width}; meter.h = 698;
        meter.displayWidth = {width}; meter.displayHeight = 698; //as PluginEditor::resized() sets them at the default size
        meter.pluginColour = juce::Colours::lightgrey;
        meter.resetArrays(); //as the PluginEditor constructor does
        meter.outputR = 10.0; meter.outputG = 10.0; meter.outputB = 10.0;
    }}
    void process(const float* L, const float* R, int n) override {{
        for (int pos = 0; pos < n; pos += 2048) {{ //small blocks, so the message queue never fills
            const int m = std::min(2048, n - pos);
            buf.setSize(2, m);
            for (int i = 0; i < m; ++i) {{ buf.ch[0][(size_t)i] = L[pos + i]; buf.ch[1][(size_t)i] = R[pos + i]; }}
            proc.processBlock(buf, midi);
            PluginProcessor::AudioToUIMessage msg;
            while (proc.audioToUI.pop(msg)) handle(msg, true);
        }}
    }}
    bool feedWindow(const std::vector<std::string>& nm, const float* values) override {{
        for (size_t i = 0; i < nm.size(); ++i) {{
            int w = -1;
            for (int k = 0; k < kNumWhats; ++k) if (nm[i] == kWhatNames[k]) w = k;
            if (w < 0) return false;
            PluginProcessor::AudioToUIMessage msg; msg.what = (PluginProcessor::AudioToUIMessage::What)w; msg.newValue = values[i];
            handle(msg, false);
        }}
        PluginProcessor::AudioToUIMessage inc; inc.what = PluginProcessor::AudioToUIMessage::INCREMENT; inc.newValue = 1200.0f;
        handle(inc, false);
        return true;
    }}
    ::mdark::OriginalResult result() const override {{
        const auto& m = meter;
        ::mdark::OriginalResult r;
        r.bonus = m.bonusScore.s; r.letter = m.totalPackage.s; r.authority = m.authority.s; r.power = m.power.s; r.detail = m.detail.s;
        r.directions = m.directions.s;
        r.grade = r.bonus + r.letter + r.authority + r.power + r.detail; //the order the original draws it
        r.peakPrevGrade = m.peakPrevGrade; r.slewPrevGrade = m.slewPrevGrade; r.bassPrevGrade = m.bassPrevGrade;
        r.outputR = m.outputR; r.outputG = m.outputG; r.outputB = m.outputB;
        r.storeR = m.storeR; r.storeG = m.storeG; r.storeB = m.storeB; r.outputVol = m.outputVol; //the tone colour as the top bar draws it (after the window's INCREMENT)
        r.cumulative = m.cumulative; r.duration = m.duration; r.cumulativePlus = m.cumulativePlus; r.durationPlus = m.durationPlus;
        r.letterValue = (1.0-pow(1.0-(m.cumulative/m.duration),1.618033988749894))*27.618033988749894; //the original's allMatch
        r.bonusValue = 100-(int)fmin((m.cumulativePlus/m.durationPlus)*100.0,99.0);                    //the original's bonus number
        r.directionsMain = ::mdark::mainDirection(dirs, vols);
        r.windows = windows; r.paints = paints;
        return r;
    }}
private:
    void handle(const PluginProcessor::AudioToUIMessage& msg, bool record) {{
        bool repaintTS = false;
        if (msg.what == PluginProcessor::AudioToUIMessage::INCREMENT) {{
            if (record) {{
                if (names.empty()) names = rowNames;
                if (rowNames == names) core.insert(core.end(), row.begin(), row.end());
                row.clear(); rowNames.clear();
            }}
            dispatch(meter, msg, repaintTS);
            ++windows;
            if (windows % paintEvery == 0) {{
                meter.paint(g); ++paints;
                dirs.push_back(m_trim(meter.directions.s)); vols.push_back(meter.outputVol);
            }}
        }} else {{
            if (record) {{ row.push_back(msg.newValue); rowNames.push_back(kWhatNames[(int)msg.what]); }}
            dispatch(meter, msg, repaintTS);
        }}
    }}
    static std::string m_trim(const std::string& s) {{ size_t a = s.find_first_not_of(' '), b = s.find_last_not_of(' '); return a == std::string::npos ? std::string() : s.substr(a, b - a + 1); }}
    PluginProcessor proc;
    AirwindowsMeter meter;
    juce::Graphics g;
    juce::AudioBuffer<float> buf;
    juce::MidiBuffer midi;
    std::vector<float> row; std::vector<std::string> rowNames;
    std::vector<std::string> dirs; std::vector<double> vols;
    int paintEvery; long windows = 0, paints = 0;
}};
}} // namespace {nsv}
"""
      (gen / f"Meter_{ident(vid)}{suffix}.inc").write_text(out)
    versions.append((vid, ns, up))
    print(f"{vid}: {len(whats)} message types, processBlock {pb.count(chr(10))} lines, paint {body.count(chr(10))} lines")

lst = ["// GENERATED by tools/make_original_scorer.py. Do not edit by hand."]
for vid, ns, up in versions: lst.append(f'#include "Meter_{ident(vid)}.inc"')
for vid, ns, up in versions: lst.append(f'#include "Meter_{ident(vid)}_tunable.inc"')
lst.append("namespace mdark {\nconst std::vector<MeterVersion>& meterVersions() {\n    static const std::vector<MeterVersion> v = {")
for vid, ns, up in versions:
    lst.append(f'        {{"{vid}", "{up.get("version", vid)}", "{up.get("commit", "")}", "{up.get("date", "")}", [](double sr, int pe) {{ return std::unique_ptr<MeterRun>(new {ns}::Run(sr, pe)); }}}},')
lst.append("    };\n    return v;\n}")
tv, tns, tup = versions[-1]
lst.append(f'const std::string& tunableVersionId() {{ static const std::string s = "{tv}"; return s; }}  // the newest version')
lst.append("const std::vector<std::string>& tunableSettingsUsed(const std::string& versionId) {")
lst.append("    static const std::vector<std::string> none;")
for vid, ns, up in versions:
    used_cpp = ", ".join(f'"{k}"' for k in tunable_used[vid])
    lst.append(f'    {{ static const std::vector<std::string> v = {{{used_cpp}}}; if (versionId == "{vid}") return v; }}')
lst.append(f'    {{ static const std::vector<std::string> v = {{{", ".join(chr(34)+k+chr(34) for k in tunable_used[tv])}}}; if (versionId.empty()) return v; }}')
lst.append("    return none;\n}")
lst.append("std::unique_ptr<MeterRun> createTunableRun(const std::string& versionId, double sr, int pe) {")
for vid, ns, up in versions:
    lst.append(f'    if (versionId == "{vid}") return std::unique_ptr<MeterRun>(new {ns}_tunable::Run(sr, pe));')
lst.append(f'    return std::unique_ptr<MeterRun>(new {tns}_tunable::Run(sr, pe));')
lst.append("}")
lst.append("} // namespace mdark")
(gen / "Versions.inc").write_text("\n".join(lst) + "\n")
(here / "src" / "OriginalScorer.inc").unlink(missing_ok=True)
for vid_, u_ in tunable_used.items(): print("tunable copy of", vid_, "uses Lab settings:", ", ".join(u_))
