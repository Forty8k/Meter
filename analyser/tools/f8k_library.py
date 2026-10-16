#!/usr/bin/env python3
"""Reads a Meter F8K library (made by Meter F8K Analyser) for analysis.

    import f8k_library as f8k
    lib = f8k.Library("path/to/Meter F8K Library")
    lib.index                     # list of dicts, one per track (index.csv)
    t = lib.track(lib.index[0]["id"])
    t.meta                        # everything from <id>.meta.json
    t.core                        # numpy array, one row per original 42.7 ms window: window,time_s,peakL..deptR
    t.fine                        # numpy array, one row per ~5.8 ms block, columns named in t.cols
    t.windows(k)                  # rebuild plugin-style windows of k fine blocks each (exact for peaks, slews, zero-cross gaps,
                                  # beam and departure), returned as an array with the same ten columns as core
Needs numpy."""
import csv, gzip, json, pathlib
import numpy as np

CORE_COLS = ["window", "time_s", "peakL", "peakR", "slewL", "slewR", "zeroL", "zeroR", "beamL", "beamR", "deptL", "deptR"]

class Track:
    def __init__(self, folder, tid):
        p = pathlib.Path(folder) / "tracks"
        self.id = tid
        self.meta = json.loads((p / f"{tid}.meta.json").read_text())
        self.core = np.loadtxt(p / f"{tid}.core.csv", delimiter=",", skiprows=1, ndmin=2)
        self.cols = self.meta["fine_columns"]
        raw = gzip.decompress((p / f"{tid}.fine.f32.gz").read_bytes())
        self.fine = np.frombuffer(raw, dtype="<f4").reshape(-1, len(self.cols)).astype(np.float64)
        self.sr = float(self.meta["sample_rate"])

    def col(self, name):
        return self.fine[:, self.cols.index(name)]

    def windows(self, k):
        """Plugin-style windows made of k consecutive fine blocks (a trailing partial window is dropped)."""
        nb = (len(self.fine) // k) * k
        a, zs = 1.0 / self.sr, 44100.0 / self.sr
        out = np.zeros((nb // k, 10))
        n = self.col("n")[:nb].reshape(-1, k)
        N = n.sum(axis=1)
        offs = np.cumsum(n, axis=1) - n
        for ci, ch in enumerate("LR"):
            g = lambda name: self.col(f"{name}_{ch}")[:nb].reshape(-1, k)
            out[:, 0 + ci] = np.sqrt(g("peak").max(axis=1))
            out[:, 2 + ci] = g("slew").max(axis=1)
            out[:, 6 + ci] = g("beam_end")[:, -1]
            after = N[:, None] - offs - n
            out[:, 8 + ci] = (g("dept_local") * (1.0 - a) ** after).sum(axis=1)
            cnt, first, last, gap = g("zc_count"), g("zc_first"), g("zc_last"), g("zc_gapmax")
            longest = np.zeros(len(N))
            for w in range(len(N)):
                best, prev = 0.0, -1.0
                for b in range(k):
                    if cnt[w, b] > 0:
                        f, l = offs[w, b] + first[w, b], offs[w, b] + last[w, b]
                        best = max(best, f + 1 if prev < 0 else f - prev, gap[w, b])
                        prev = l
                longest[w] = N[w] if prev < 0 else max(best, N[w] - 1 - prev)
            out[:, 4 + ci] = longest * zs
        return out

class Library:
    def __init__(self, folder):
        self.folder = pathlib.Path(folder)
        with open(self.folder / "index.csv", newline="", encoding="utf-8") as f:
            self.index = list(csv.DictReader(f))

    def track(self, tid):
        return Track(self.folder, tid)

    def tracks(self):
        for row in self.index:
            yield self.track(row["id"])
