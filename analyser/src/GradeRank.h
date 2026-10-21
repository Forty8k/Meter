// Ranking of the parts of a grade, for filtering. Pure C++ (no JUCE) so the self-test can check it.
//
// A grade like  30-Abbd  is: bonus number (1-80, LOWER is better, often absent), overall letter (A best ... Z worst), then authority, power, detail.
// Each of authority / power / detail is ONE ordered scale: lowercase little ... plenty (b, a), then capitals A, B, C ... (exceptionally strong).
// The Meter versions start and stop their scales at different letters (0.5.5 authority runs s...J, 0.5.6 runs p...M; detail starts at v or t), but the middle is the
// same, so every sub-grade letter gets ONE rank that does not depend on the version:  ... c = -2, b = -1, a = 0, A = 1, B = 2, C = 3 ...  Higher is better.
// The overall letter: A = 25 ... Z = 0.
#pragma once
#include <string>
#include <vector>
#include <cctype>
#include <cstdlib>

namespace mdark {

inline std::string gradeTrim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

// authority / power / detail letter -> rank (false if it is not one letter)
inline bool subGradeRank(const std::string& text, int& rank) {
    const std::string s = gradeTrim(text);
    if (s.size() != 1 || !std::isalpha((unsigned char)s[0])) return false;
    const char c = s[0];
    rank = (c >= 'a' && c <= 'z') ? -(c - 'a') : (c - 'A' + 1);
    return true;
}

// the overall letter A..Z -> rank (A highest)
inline bool letterRank(const std::string& text, int& rank) {
    const std::string s = gradeTrim(text);
    if (s.size() != 1) return false;
    char c = s[0];
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c < 'A' || c > 'Z') return false;
    rank = 'Z' - c;
    return true;
}

// the letters offered when picking, best first
inline std::vector<std::string> subGradeScaleBestFirst() { // M ... A, a ... v
    std::vector<std::string> v;
    for (char c = 'M'; c >= 'A'; --c) v.push_back(std::string(1, c));
    for (char c = 'a'; c <= 'v'; ++c) v.push_back(std::string(1, c));
    return v;
}
inline std::vector<std::string> letterScaleBestFirst() {
    std::vector<std::string> v;
    for (char c = 'A'; c <= 'Z'; ++c) v.push_back(std::string(1, c));
    return v;
}

// "b to A", "b-A", "1 to 40", "1-40" -> the two ends
inline bool splitRange(const std::string& text, std::string& lo, std::string& hi) {
    const std::string s = gradeTrim(text);
    size_t p = std::string::npos, skip = 1;
    for (size_t i = 0; i + 4 <= s.size(); ++i)
        if (std::tolower((unsigned char)s[i]) == ' ' && std::tolower((unsigned char)s[i + 1]) == 't' && std::tolower((unsigned char)s[i + 2]) == 'o' && s[i + 3] == ' ') {p = i; skip = 4; break;}
    if (p == std::string::npos) {p = s.find('-', 1); skip = 1;}
    if (p == std::string::npos) {p = s.find(' ', 1); skip = 1;}
    if (p == std::string::npos) return false;
    lo = gradeTrim(s.substr(0, p)); hi = gradeTrim(s.substr(p + skip));
    return !lo.empty() && !hi.empty();
}

// Filters saved before the ranked fields stored authority / power / detail as a NUMBER: the position in the newest (0.5.6) scale, 0 = its lowest letter.
// This turns such a number back into its letter ("" if it is outside the scale).
inline std::string legacySubGradeLetter(const std::string& field, int value) {
    const char* scale = field == "authority" ? "ponmlkjihgfedcbaABCDEFGHIJKLM" : field == "power" ? "qponmlkjihgfedcbaABCDEFGHIJKL" : field == "detail" ? "tsrqponmlkjihgfedcbaABCDEFGHIJKLM" : "";
    const int n = (int)std::string(scale).size();
    return (value >= 0 && value < n) ? std::string(1, scale[value]) : std::string();
}

// ---- Sorting grades by their TEXT, the way Chris does in a Finder column (21 Oct).
// compareGradeText: < 0 if a sorts above b (best first), > 0 if below, 0 if the same.
//  - the bonus number (before the "-") counts as a NUMBER: 1 before 2 before 10 before 80; a grade with a number comes before one without
//  - then the letters left to right, case-insensitive (A before B)
//  - only if the letters are equal ignoring case: a capital comes before a lowercase letter at the first difference (A is stronger than a)
// An empty grade is "not graded": see compareGradeTextDir, which keeps it last in both directions.
inline int compareGradeText(const std::string& a0, const std::string& b0) {
    const std::string a = gradeTrim(a0), b = gradeTrim(b0);
    auto split = [](const std::string& s, long& num, std::string& letters) {
        size_t i = 0; while (i < s.size() && std::isdigit((unsigned char)s[i])) ++i;
        num = -1;
        if (i > 0 && i < s.size() && s[i] == '-') { num = std::atol(s.substr(0, i).c_str()); letters = s.substr(i + 1); }
        else letters = s;
    };
    long na, nb; std::string la, lb;
    split(a, na, la); split(b, nb, lb);
    if ((na >= 0) != (nb >= 0)) return na >= 0 ? -1 : 1;
    if (na != nb) return na < nb ? -1 : 1;
    const size_t n = la.size() < lb.size() ? la.size() : lb.size();
    for (size_t i = 0; i < n; ++i) {
        const int x = std::tolower((unsigned char)la[i]), y = std::tolower((unsigned char)lb[i]);
        if (x != y) return x < y ? -1 : 1;
    }
    if (la.size() != lb.size()) return la.size() < lb.size() ? -1 : 1;
    for (size_t i = 0; i < n; ++i)
        if (la[i] != lb[i]) return std::isupper((unsigned char)la[i]) ? -1 : 1;
    return 0;
}
// the same with the direction: forwards = best first; an empty (ungraded) grade is last either way
inline int compareGradeTextDir(const std::string& a, const std::string& b, bool forwards) {
    const bool ea = gradeTrim(a).empty(), eb = gradeTrim(b).empty();
    if (ea || eb) return ea == eb ? 0 : (ea ? 1 : -1);
    const int c = compareGradeText(a, b);
    return forwards ? c : -c;
}

} // namespace mdark
