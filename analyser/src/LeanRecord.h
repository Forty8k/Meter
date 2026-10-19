// meterDARK Analyser: the LEAN RECORD. Everything the Library's views and the grades need from one track, small enough to share (about 7 KB for
// the display part, plus the replay block).
//  display part: overall tone bar (RGB), weight / peaks / slew tone bars (how far each meter led the other two, signed), overlay lines (peak, bass position,
//                slew position), grade and sort values, Meter version, sample rate, length, levels, and the tags. At most 1024 columns; a track shorter than
//                about 44 seconds gets one column per window.
//  replay block: the packed window data (ReplayPack.h), enough to regrade the track in any Meter version / gradeLAB setting without the audio.
// Never stored here: file paths, your own columns, fine blocks, pitch. Pure C++ (no JUCE); gzip is applied by the caller.
#pragma once
#include "OriginalScorer.h"
#include "ReplayPack.h"
#include <string>
#include <vector>
#include <utility>
#include <cstdint>

namespace mdark {

struct LeanRecord {
    std::vector<std::pair<std::string, std::string>> meta; // key/value text: id, artist, title, album, year, source, meter_version, sample_rate, bits, length_s, peak_db, true_peak_db, lufs, grade, bonus, letter_value, authority_value, power_value, detail_value ...
    int columns = 0;
    std::vector<uint8_t> toneRgb;                 // columns x 3: the top tone bar, 8-bit R,G,B
    std::vector<int8_t> devWeight, devPeaks, devSlew; // columns each: how far that meter led the other two (-127 .. +127 = -1 .. +1), already faded in quiet passages
    std::vector<uint8_t> linePeak, lineBass, lineSlew; // columns each: the average peak / bass / slew position (0-200) for the overlay lines
    std::vector<uint8_t> replay;                  // packWindows() output; empty if not included
    std::string get(const std::string& k) const { for (auto& kv : meta) if (kv.first == k) return kv.second; return {}; }
    void set(const std::string& k, const std::string& v) { for (auto& kv : meta) if (kv.first == k) {kv.second = v; return;} meta.emplace_back(k, v); }
};

// Replays the window data (the layout of MeterRun::core) through the Meter version and builds the record. `meta` is carried over; the grade keys are filled in.
LeanRecord buildLeanRecord(const MeterVersion& version, double sampleRate, const std::vector<std::string>& names, const std::vector<float>& windows,
                           const std::vector<std::pair<std::string, std::string>>& meta, int maxColumns = 1024, int packBits = 16, bool includeReplay = true);

std::vector<uint8_t> serialiseLean(const LeanRecord& r);          // plain bytes (compress with gzip for storage)
bool parseLean(const std::vector<uint8_t>& bytes, LeanRecord& r);

} // namespace mdark
