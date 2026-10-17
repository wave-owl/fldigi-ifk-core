#!/usr/bin/env python3
"""Golden data from fldigi 4.2.13 itself for fldigi-ifk-core (stdlib only).

Drives the headless fldigi bench build (configure --enable-benchmark plus the TX
extension of wave-owl's archived change fix-psk-rtty-vs-fldigi, bench/BUILD.md) for every
THOR and DominoEX submode of fldigi 4.2.13 (src/globals/globals.cxx:83-91, 200-216), and
for DominoEX with MultiPSK FEC (DOMINOEXFEC=1) in four submodes:

* TX: fldigi renders the text (bench-tx, txlevel -12 dB, PCM16, modem rate). The FNV-1a 64
  hash of every vector goes to tx_hashes.txt; a subset is kept as raw little-endian int16
  in tx/<name>.s16.
* RX: fldigi decodes 1 s silence + the TX vector + 12 s silence, clean and buried in
  deterministic noise at -5 and -10 dB (2500 Hz reference bandwidth, see noisy()). The raw
  bytes fldigi hands to put_rx_char() go to rx/<name>_<snr>.txt.

    FLDIGI_BENCH_BIN=/path/to/src-bench/src/fldigi python3 tests/golden/make_fldigi_golden.py

The C tests (tests/test_ifk_fldigi.c) rebuild the same noisy inputs bit for bit.
SPDX-License-Identifier: GPL-3.0-or-later
"""

from __future__ import annotations

import array
import math
import os
import shutil
import subprocess
import sys
import tempfile
import wave
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
FLDIGI = os.environ.get("FLDIGI_BENCH_BIN", "fldigi")
CARRIER_HZ = 1500
TEXTS = {"cq": "CQ CQ DE DG6FL DG6FL K",
         "pangram": "the quick brown fox jumps over the lazy dog 0123456789 <test?> 73"}

# (stem, fldigi trx_mode id = index into mode_info[], sname, submode, fec, text key)
THOR = [("thormicro", 117, "THORM"), ("thor4", 118, "THOR4"), ("thor5", 119, "THOR5"),
        ("thor8", 120, "THOR8"), ("thor11", 121, "THOR11"), ("thor16", 122, "THOR16"),
        ("thor22", 123, "THOR22"), ("thor25", 124, "THOR25"), ("thor32", 125, "THOR32"),
        ("thor44", 126, "THOR44"), ("thor56", 127, "THOR56"), ("thor25x4", 128, "THOR25x4"),
        ("thor50x1", 129, "THOR50x1"), ("thor50x2", 130, "THOR50x2"), ("thor100", 131, "THOR100")]
DOMINOEX = [("dominoexmicro", 22, "DOMEXM"), ("dominoex4", 23, "DOMEX4"),
            ("dominoex5", 24, "DOMEX5"), ("dominoex8", 25, "DOMEX8"),
            ("dominoex11", 26, "DOMX11"), ("dominoex16", 27, "DOMX16"),
            ("dominoex22", 28, "DOMX22"), ("dominoex44", 29, "DOMX44"),
            ("dominoex88", 30, "DOMX88")]
FEC = {"dominoex8", "dominoex11", "dominoex22", "dominoex88"}
STORED = {"thormicro", "thor8", "thor16", "thor22", "thor25x4", "thor56", "thor100",
          "dominoexmicro", "dominoex11", "dominoex88", "dominoex16_fec"}
SNRS = ("clean", "m05", "m10")


def cases():
    out = []
    for stem, mid, sname in THOR:
        out.append((stem, mid, sname, 0, "cq"))
    for stem, mid, sname in DOMINOEX:
        out.append((stem, mid, sname, 0, "cq"))
    for stem, mid, sname in DOMINOEX:
        if stem in FEC or stem == "dominoex16":
            out.append((stem + "_fec", mid, sname, 1, "cq"))
    out.append(("thor16_pangram", 122, "THOR16", 0, "pangram"))
    out.append(("dominoex11_pangram", 26, "DOMX11", 0, "pangram"))
    return out


def run_fldigi(mid, sname, fec, args, env_extra=None):
    cfg = tempfile.mkdtemp(prefix="fldigi-ifk-golden-")
    body = "<DOMINOEXFEC>1</DOMINOEXFEC>\n" if fec else ""
    Path(cfg, "fldigi_def.xml").write_text(f"<FLDIGI_DEFS>\n{body}</FLDIGI_DEFS>\n")
    env = {k: v for k, v in os.environ.items() if not k.startswith("FLDIGI_BENCH_TX")}
    env.update(env_extra or {})
    try:
        p = subprocess.run([FLDIGI, "--config-dir", cfg, "--benchmark-modem", str(mid),
                            "--benchmark-frequency", str(CARRIER_HZ), *args],
                           env=env, capture_output=True, text=True, timeout=3600)
    finally:
        shutil.rmtree(cfg, ignore_errors=True)
    if f"mode={sname} " not in p.stderr:
        raise RuntimeError(f"fldigi did not run {sname}: {p.stderr[-400:]}")
    return p.stderr


def read_wav(path):
    with wave.open(str(path)) as w:
        assert w.getnchannels() == 1 and w.getsampwidth() == 2
        sr = w.getframerate()
        a = array.array("h", w.readframes(w.getnframes()))
    if sys.byteorder == "big":
        a.byteswap()
    return sr, a


def write_wav(path, sr, pcm):
    a = array.array("h", pcm)
    if sys.byteorder == "big":
        a.byteswap()
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(a.tobytes())


def fnv1a64(pcm):
    h = 0xCBF29CE484222325
    for v in pcm:
        u = v & 0xFFFF
        for byte in (u & 0xFF, u >> 8):
            h ^= byte
            h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


class Lcg:
    """32-bit LCG (Numerical Recipes); the same in tests/test_ifk_fldigi.c."""

    def __init__(self, seed):
        self.s = seed & 0xFFFFFFFF

    def uniform(self):
        self.s = (self.s * 1664525 + 1013904223) & 0xFFFFFFFF
        return self.s / 4294967296.0


def noisy(pcm, sr, snr, seed):
    """1 s + pcm + 12 s, plus Irwin-Hall Gaussian noise at -5 or -10 dB SNR in 2500 Hz.

    Only +, -, *, / and sqrt (all correctly rounded), so C reproduces it bit for bit.
    SNR = mean square of the burst / (noise variance * 2500 / (sr / 2)).
    """
    lead, tail = sr, 12 * sr
    padded = [0.0] * lead + [float(v) for v in pcm] + [0.0] * tail
    if snr == "clean":
        return [int(v) for v in padded]
    ps = 0.0
    for v in pcm:
        ps += float(v) * float(v)
    ps = ps / len(pcm)
    inv = 1.0 / math.sqrt(10.0) if snr == "m05" else 1.0 / 10.0
    s2 = ps / inv * (sr / 2.0) / 2500.0
    sd = math.sqrt(s2)
    gain = 3276.7 / math.sqrt(ps * len(pcm) / len(padded) + s2)
    rng = Lcg(seed)
    out = []
    for v in padded:
        z = 0.0
        for _ in range(12):
            z += rng.uniform()
        z -= 6.0
        y = (v + sd * z) * gain
        q = math.floor(y + 0.5)
        out.append(max(-32768, min(32767, int(q))))
    return out


def one(idx, case):
    stem, mid, sname, fec, key = case
    with tempfile.TemporaryDirectory() as td:
        wav = Path(td, stem + ".wav")
        run_fldigi(mid, sname, fec, ["--benchmark-input", "1"],
                   {"FLDIGI_BENCH_TX_TEXT": TEXTS[key], "FLDIGI_BENCH_TX_WAV": str(wav)})
        sr, pcm = read_wav(wav)
        line = (f"{stem} {sname} {fec} {key} {idx} {sr} {len(pcm)} {fnv1a64(pcm):016x}")
        if stem in STORED or key == "pangram":
            raw = array.array("h", pcm)
            if sys.byteorder == "big":
                raw.byteswap()
            Path(HERE, "tx", stem + ".s16").write_bytes(raw.tobytes())
        for i, snr in enumerate(SNRS):
            inp = Path(td, f"{stem}_{snr}.wav")
            write_wav(inp, sr, noisy(pcm, sr, snr, 1000 * idx + i))
            out = Path(td, f"rx_{snr}.txt")
            run_fldigi(mid, sname, fec, ["--benchmark-input", str(inp),
                                         "--benchmark-output", str(out)])
            text = out.read_bytes() if out.exists() else b""
            Path(HERE, "rx", f"{stem}_{snr}.txt").write_bytes(text)
    print(line, flush=True)
    return line


def main():
    (HERE / "tx").mkdir(exist_ok=True)
    (HERE / "rx").mkdir(exist_ok=True)
    only = set(sys.argv[1:])
    todo = [(i, c) for i, c in enumerate(cases()) if not only or c[0] in only]
    with ThreadPoolExecutor(max_workers=int(os.environ.get("JOBS", "8"))) as ex:
        lines = list(ex.map(lambda a: one(*a), todo))
    if only:
        return 0
    header = ("# stem fldigi-sname dominoex-fec text index samplerate samples fnv1a64(int16 LE)"
              " -- fldigi 4.2.13 bench TX, txlevel -12 dB; noise seed = 1000 * index + snr"
              " step\n")
    (HERE / "tx_hashes.txt").write_text(header + "\n".join(lines) + "\n")
    (HERE / "texts.txt").write_text("".join(f"{k} {v}\n" for k, v in TEXTS.items()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
