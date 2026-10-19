// meterDARK Analyser: the REPLAY BLOCK. A track's stored window data (about 23 windows a second, 10 or 18 values each) packed small:
// every column is log-scaled between 0 and its own maximum, rounded to `bits` bits, delta-coded down the track and written as
// zigzag variable-length integers. Compress the result with gzip for storage. Pure C++, no JUCE.
// The precision (bits) is chosen by tools/packtest.cpp and the library check: the smallest that gives the SAME grade, bonus and tone as the exact data.
#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace f8k {

inline void rpPutVar(std::vector<uint8_t>& o, uint64_t v) { while (v >= 0x80) {o.push_back((uint8_t)(v | 0x80)); v >>= 7;} o.push_back((uint8_t)v); }
inline bool rpGetVar(const uint8_t*& p, const uint8_t* e, uint64_t& v) {
    v = 0; int sh = 0;
    while (p < e) {const uint8_t b = *p++; v |= (uint64_t)(b & 0x7f) << sh; if (!(b & 0x80)) return true; sh += 7; if (sh > 63) return false;}
    return false;
}
inline void rpPutF(std::vector<uint8_t>& o, float f) { uint8_t b[4]; std::memcpy(b, &f, 4); o.insert(o.end(), b, b + 4); }

constexpr double kRpFloorRatio = 1.0e-5; // the smallest value told apart from zero, relative to the column's maximum

// names.size() values per window, windows one after another (as MeterRun::core)
inline std::vector<uint8_t> packWindows(const std::vector<std::string>& names, const std::vector<float>& values, int bits)
{
    std::vector<uint8_t> o;
    const size_t nc = names.size(), nw = nc ? values.size()/nc : 0;
    o.push_back('R'); o.push_back('P'); o.push_back('K'); o.push_back(1);
    o.push_back((uint8_t)bits);
    rpPutVar(o, nc); rpPutVar(o, nw);
    const double Q = std::ldexp(1.0, bits) - 1.0;
    std::vector<float> vmax(nc, 0.0f);
    for (size_t c = 0; c < nc; ++c) {
        for (size_t w = 0; w < nw; ++w) vmax[c] = std::max(vmax[c], std::fabs(values[w*nc + c]));
        rpPutVar(o, names[c].size()); o.insert(o.end(), names[c].begin(), names[c].end());
        rpPutF(o, vmax[c]);
    }
    for (size_t c = 0; c < nc; ++c) {
        const double vm = vmax[c], e = vm*kRpFloorRatio, den = std::log1p(vm/std::max(e, 1.0e-30));
        int64_t prev = 0;
        for (size_t w = 0; w < nw; ++w) {
            const double v = std::fabs(values[w*nc + c]);
            const int64_t q = vm > 0 ? (int64_t)std::llround(std::log1p(v/e)/den*Q) : 0;
            const int64_t d = q - prev; prev = q;
            rpPutVar(o, d >= 0 ? (uint64_t)d << 1 : (((uint64_t)(-d)) << 1) - 1);
        }
    }
    return o;
}

inline bool unpackWindows(const std::vector<uint8_t>& in, std::vector<std::string>& names, std::vector<float>& values)
{
    if (in.size() < 6 || in[0] != 'R' || in[1] != 'P' || in[2] != 'K' || in[3] != 1) return false;
    const uint8_t* p = in.data() + 4; const uint8_t* e = in.data() + in.size();
    const int bits = *p++;
    uint64_t nc, nw;
    if (!rpGetVar(p, e, nc) || !rpGetVar(p, e, nw)) return false;
    names.assign((size_t)nc, {}); std::vector<float> vmax((size_t)nc);
    for (size_t c = 0; c < nc; ++c) {
        uint64_t len; if (!rpGetVar(p, e, len) || p + len + 4 > e) return false;
        names[c].assign((const char*)p, (size_t)len); p += len;
        std::memcpy(&vmax[c], p, 4); p += 4;
    }
    values.assign((size_t)(nc*nw), 0.0f);
    const double Q = std::ldexp(1.0, bits) - 1.0;
    for (size_t c = 0; c < nc; ++c) {
        const double vm = vmax[c], e2 = vm*kRpFloorRatio, den = std::log1p(vm/std::max(e2, 1.0e-30));
        int64_t q = 0;
        for (size_t w = 0; w < nw; ++w) {
            uint64_t z; if (!rpGetVar(p, e, z)) return false;
            q += (z & 1) ? -(int64_t)((z + 1) >> 1) : (int64_t)(z >> 1);
            values[w*nc + c] = vm > 0 ? (float)(std::expm1((double)q/Q*den)*e2) : 0.0f;
        }
    }
    return true;
}

// a stand-in for the gzip size when no zlib is at hand: order-0 entropy of the bytes (gzip of this kind of data lands within a few percent of it)
inline size_t estimateCompressedBytes(const std::vector<uint8_t>& b)
{
    size_t cnt[256] = {}; for (uint8_t x : b) ++cnt[x];
    double bitsTotal = 0; for (int i = 0; i < 256; ++i) if (cnt[i]) bitsTotal -= (double)cnt[i]*std::log2((double)cnt[i]/(double)b.size());
    return (size_t)(bitsTotal/8.0) + 32;
}

} // namespace f8k
