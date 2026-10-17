# fldigi-ifk-core

[![CI](https://github.com/wave-owl/fldigi-ifk-core/actions/workflows/ci.yml/badge.svg)](https://github.com/wave-owl/fldigi-ifk-core/actions/workflows/ci.yml)

A C99 library that receives and transmits **THOR** and **DominoEX** exactly the way fldigi
4.2.13 does: audio in, characters out, and characters in, audio out. Both modes use IFK+
incremental frequency keying (18 tones, each symbol `previous tone + 2 + nibble`), and
fldigi implements them as near copies of each other, so one library serves both. It is a
line-by-line port of `src/thor/thor.cxx`, `src/thor/thorvaricode.cxx`,
`src/dominoex/dominoex.cxx` and `src/dominoex/dominovar.cxx`, built on the shared
[fldigi-dsp-core](https://github.com/wave-owl/fldigi-dsp-core). On the reference build
the transmitter reproduces fldigi's TX audio sample for sample and the receiver prints
fldigi's characters byte for byte, for all 24 submodes (see [Verification](#verification)).
It is the only THOR/DominoEX implementation of [wave-owl](https://github.com/wave-owl/wave-owl).

| | |
|---|---|
| **Is** | A single-channel **receiver** for every THOR (15) and DominoEX (9) submode of fldigi 4.2.13, including THOR's K=7 and K=15 FEC, the secondary (idle) text of both modes and DominoEX's optional MultiPSK-compatible FEC: audio at the submode's rate in, the bytes fldigi prints out, secondary text separately, plus fldigi's s/n, squelch metric and FEC confidence. |
| **Is** | The matching **transmitter** (capability tag `tx=1`): preamble, framing (CR STX CR ... CR EOT CR), keyboard characters, secondary text while idle, flush and soft stop, as fldigi's `tx_process`. |
| **Is not** | A skimmer or carrier search: each instance decodes one submode at one audio carrier. There is no AFC in fldigi for these modes (IFK+ tolerates drift); the library reports an offset estimate instead. No THOR picture/avatar transfer, no RSID, no ARQ/KISS. |
| | An I/O layer, threaded or allocating at run time: no sound card or file access, no threads, all memory allocated in `*_create()`, no global mutable state. |

## Contents

- [Signal chain: receiver](#signal-chain-receiver)
- [Signal chain: transmitter](#signal-chain-transmitter)
- [Submodes](#submodes)
- [Differences from fldigi](#differences-from-fldigi)
- [API reference](#api-reference)
- [Usage examples](#usage-examples)
- [Contract and versioning](#contract-and-versioning)
- [Verification](#verification)
- [Performance and memory](#performance-and-memory)
- [Building](#building)
- [Maintenance and fldigi updates](#maintenance-and-fldigi-updates)
- [License and credits](#license-and-credits)

## Signal chain: receiver

All citations refer to fldigi tag `v4.2.13` (mirror `wave-owl/fldigi`, commit
`e7c3ab3709ad62a7d91829364eb48a05fa325c0d`); every C function names the lines it ports.
Settings are fldigi's defaults (`configuration.h`): filter on with 2 x the signal bandwidth,
`slowcpu` (3 paths), THOR soft symbols, soft bits and preamble detection on, CWI threshold 0,
DominoEX FEC off.

```
 audio x[n] at the submode rate (8000, 11025 or 16000 Hz; int16 / 32768 scale)
      v
 analytic signal: 37-tap Hilbert pair (C_FIR_filter::init_hilbert, filters.cxx:152-158)
      v
 mixer 0: carrier -> first IF (THOR 2000 Hz, DominoEX 1500 Hz)   thor.cxx:431-448
      v
 fftfilt band pass, 1024 points, IF +- bandwidth                 thor.cxx:1137-1141
      v                                                          dominoex.cxx:602-610
 3 paths, each shifted by a fraction of a DFT bin (THOR: (samplerate / symlen) as an
 integer quotient x n / paths; DominoEX: tonespacing x (n - 1) / paths), each into a
 sliding DFT one symbol long over lotone .. hitone; the bins interleave into one vector
 per sample (index bin x paths + path) in a two-symbol pipe      thor.cxx:1147-1158
      v  once per symbol (synccounter)
 THOR:  soft symbol choice with CWI and repeat masking (softdecode, :782-855),
        s/n (eval_s2n, :983-1039), IFK+ difference -> nibble, soft bits weighted by the
        "doppler" confidence, preamble detection that flushes the pipeline
        (softdecodesymbol, :608-729, :857-899), de-interleaver 4 x 4 x depth,
        Viterbi K=7 (0x6d, 0x4f, traceback 45) or K=15 (044735, 063057, traceback 180),
        THOR varicode (decodePairs, :525-563; thorvaricode.cxx:170-193)
 DominoEX: hard symbol with fldigi's CWI test and static-burst ratio (harddecode,
        dominoex.cxx:427-469), nibble -> DominoEX varicode (:374-401); MultiPSK FEC
        branch: 4 x 4 x 4 de-interleaver, Viterbi K=7, MFSK varicode (:829-874)
 both:  symbol timing from the previous tone's peak over two symbols (synchronize,
        thor.cxx:940-967, dominoex.cxx:513-540)
      v
 primary bytes (put_rx_char, NUL included) and secondary bytes (put_sec_char)
```

## Signal chain: transmitter

`fifk_tx_begin` / `_put` / `_idle` / `_end` are the steps of fldigi's `tx_process` state
machine (`thor.cxx:1313-1393`, `dominoex.cxx:726-771`):

| Call | fldigi state | THOR | DominoEX |
|---|---|---|---|
| `fifk_tx_begin` | `TX_STATE_PREAMBLE` + `TX_STATE_START` | fill the interleaver (`Clearbits`), 16 preamble symbols, a NUL, CR STX CR (Micro: CR) | MultiPSK FEC only: fill the interleaver; a secondary NUL, CR STX CR (Micro: CR) |
| `fifk_tx_put(c)` | `TX_STATE_DATA` with a character | THOR varicode, K=7/K=15 encoder, interleaver, IFK+ symbols | DominoEX varicode nibbles (or the MultiPSK FEC chain) |
| `fifk_tx_idle` | `TX_STATE_DATA`, no keyboard data | next character of the secondary text | same |
| `fifk_tx_end` | `TX_STATE_END` + `TX_STATE_FLUSH` | CR EOT CR, `flushlength` NULs, soft-stop tone | CR EOT CR, 4 secondary NULs, soft-stop tone |

Each tone is `cos` of a running phase at `(tone + 0.5) x tonespacing + carrier - bandwidth/2`;
THOR shapes every symbol's edges with a 4 ms raised cosine (`THOR_RISETIME`), and the first
and last 100 ms of an over are ramped as in `modem::ModulateXmtr` (`modem.cxx:611-637`). The
output is the signal before fldigi's TX level; multiply by `10^(-12/20)` and clip at +-0.95 to
get fldigi's sound-card samples.

## Submodes

| Name | fldigi | Rate | symlen | Tone spacing | Bandwidth | Baud | FEC | Interleaver | Flush |
|---|---|---|---|---|---|---|---|---|---|
| THORMICRO | THORM | 8000 | 4000 | 2.0 Hz | 36 Hz | 2.0 | K=7 | 4x4x4 | 4 |
| THOR4 | THOR4 | 8000 | 2048 | 7.8 Hz | 141 Hz | 3.9 | K=7 | 4x4x10 | 4 |
| THOR5 | THOR5 | 11025 | 2048 | 10.8 Hz | 194 Hz | 5.4 | K=7 | 4x4x10 | 4 |
| THOR8 | THOR8 | 8000 | 1024 | 15.6 Hz | 281 Hz | 7.8 | K=7 | 4x4x10 | 4 |
| THOR11 | THOR11 | 11025 | 1024 | 10.8 Hz | 194 Hz | 10.8 | K=7 | 4x4x10 | 8 |
| THOR16 | THOR16 | 8000 | 512 | 15.6 Hz | 281 Hz | 15.6 | K=7 | 4x4x10 | 8 |
| THOR22 | THOR22 | 11025 | 512 | 21.5 Hz | 388 Hz | 21.5 | K=7 | 4x4x10 | 16 |
| THOR25 | THOR25 | 8000 | 320 | 25.0 Hz | 450 Hz | 25.0 | K=15 | 4x4x25 | 20 |
| THOR32 | THOR32 | 8000 | 256 | 31.3 Hz | 563 Hz | 31.3 | K=7 | 4x4x10 | 20 |
| THOR44 | THOR44 | 11025 | 256 | 43.1 Hz | 775 Hz | 43.1 | K=7 | 4x4x10 | 4 |
| THOR56 | THOR56 | 16000 | 290 | 55.2 Hz | 993 Hz | 55.2 | K=7 | 4x4x10 | 20 |
| THOR25X4 | THOR25x4 | 8000 | 320 | 100 Hz | 1800 Hz | 25.0 | K=15 | 4x4x50 | 40 |
| THOR50X1 | THOR50x1 | 8000 | 160 | 50 Hz | 900 Hz | 50 | K=15 | 4x4x50 | 40 |
| THOR50X2 | THOR50x2 | 8000 | 160 | 100 Hz | 1800 Hz | 50 | K=15 | 4x4x50 | 40 |
| THOR100 | THOR100 | 8000 | 80 | 100 Hz | 1800 Hz | 100 | K=15 | 4x4x50 | 40 |
| DOMINOEXMICRO | DOMEXM | 8000 | 4000 | 2.0 Hz | 36 Hz | 2.0 | (MultiPSK K=7) | (4x4x4) | 4 |
| DOMINOEX4 | DOMEX4 | 8000 | 2048 | 7.8 Hz | 141 Hz | 3.9 | " | " | 4 |
| DOMINOEX5 | DOMEX5 | 11025 | 2048 | 10.8 Hz | 194 Hz | 5.4 | " | " | 4 |
| DOMINOEX8 | DOMEX8 | 8000 | 1024 | 15.6 Hz | 281 Hz | 7.8 | " | " | 4 |
| DOMINOEX11 | DOMX11 | 11025 | 1024 | 10.8 Hz | 194 Hz | 10.8 | " | " | 4 |
| DOMINOEX16 | DOMX16 | 8000 | 512 | 15.6 Hz | 281 Hz | 15.6 | " | " | 4 |
| DOMINOEX22 | DOMX22 | 11025 | 512 | 21.5 Hz | 388 Hz | 21.5 | " | " | 4 |
| DOMINOEX44 | DOMX44 | 11025 | 256 | 86.1 Hz | 1550 Hz | 43.1 | " | " | 4 |
| DOMINOEX88 | DOMX88 | 11025 | 128 | 86.1 Hz | 1550 Hz | 86.1 | " | " | 4 |

Sources: `thor.cxx:217-349, 398-411`, `dominoex.cxx:217-278, 327-332`,
`globals/globals.cxx:83-91, 200-216`. Lookup is case-insensitive by either name.

## Differences from fldigi

None of these changes decoded text for a single receiver; each is named in the code.

- **Function statics.** fldigi keeps THOR's soft-decision state (`thor.cxx:542, 612-613,
  784-785, 859-860`) and DominoEX's secondary-text position (`dominoex.cxx:647`) in function
  statics shared by all instances; here they belong to one receiver/transmitter.
- **THOR s/n outside the table.** `eval_s2n` reads one entry past its table when the ratio is
  outside 12.6 .. 32.8 dB (`thor.cxx:997-1004`); the port extrapolates the last segment. Only
  the displayed s/n is affected.
- **Viterbi metrics.** fldigi renormalises the path metrics only on calls without traceback,
  so with chunk size 1 they overflow after about 8.4 million symbol pairs; fldigi-dsp-core
  renormalises on every call (same decisions).
- **Left out.** The THOR picture/avatar receiver and transmitter: after a `pic%` header fldigi
  stops printing text until the image ends, this port keeps decoding text. Scopes, waterfall,
  GUI status, PSKmail s/n reports.
- **Kept on purpose** (they change decoded text): THOR's integer quotient
  `samplerate / symlen` in the path mixers (`thor.cxx:440`), DominoEX's CWI test reading
  `pipe[j]` instead of `pipe[p]` and its doubly accumulated average (`dominoex.cxx:435-465`),
  DominoEX's start-of-burst acquisition (it loses the first characters of a burst that starts
  without idle characters, as fldigi does).

## API reference

All declarations are in `include/fldigi_ifk.h`.

| Function | Description |
|---|---|
| `const char *fifk_version(void)` | `"fldigi-ifk-core 0.1.0 contract=ifk-c1 abi=1 tx=1"` |
| `int fifk_submode_count(void)`, `fifk_submode_at(i)`, `fifk_submode_find(name)` | The 24 submodes with rate, symbol length, tone spacing, bandwidth, FEC and framing parameters. |
| `fifk_rx *fifk_rx_create(const fifk_rx_config *)` | `submode`, `carrier_hz` (audio centre, 1500), `fast_paths` (0: fldigi's 3 paths), `dominoex_fec`, `squelch` (0 off, else the 0..100 metric threshold). NULL for an unknown submode or a carrier that puts the signal outside 0 .. rate/2. |
| `size_t fifk_rx_process(rx, x, n, primary, max_p, secondary, max_s, &n_s)` | Feed samples; returns the primary bytes (fldigi's `put_rx_char`, NUL included), secondary bytes in the second buffer. Overflow is dropped and counted (`fifk_rx_dropped`). |
| `fifk_rx_snr_db`, `fifk_rx_metric`, `fifk_rx_quality`, `fifk_rx_offset_hz` | fldigi's s/n estimate and squelch metric, THOR's FEC confidence (0..100), and an offset estimate (not in fldigi) of the signal from the carrier within +- half a tone spacing. |
| `fifk_rx_take_nibbles(rx, out, max)` | Diagnostics (not in fldigi): the IFK+ nibbles (tone step - 2, mod 18) of the symbols decided since the last call, at most the last 256. |
| `fifk_rx_reset`, `fifk_rx_destroy` | |
| `fifk_tx *fifk_tx_create(const fifk_tx_config *)` | `submode`, `carrier_hz`, `dominoex_fec`, `secondary_text` (NULL: fldigi's `"fldigi 4.2.13 "`), `no_soft_edges`, `risetime_ms` (< 0: 4 ms). |
| `size_t fifk_tx_bound(tx)` | The most samples one of the four calls below can produce; give every call a buffer at least that large. |
| `fifk_tx_begin`, `fifk_tx_put(c)`, `fifk_tx_idle`, `fifk_tx_end` | The state steps above; each returns the samples written (0: buffer too small or out of order). |

## Usage examples

### C

```c
#include <stdio.h>
#include <stdlib.h>
#include "fldigi_ifk.h"

int main(void)
{
    fifk_tx_config tc = {"THOR16", 1500.0, 0, NULL, 0, -1.0};
    fifk_rx_config rc = {"THOR16", 1500.0, 0, 0, 0.0};
    fifk_tx *tx = fifk_tx_create(&tc);
    fifk_rx *rx = fifk_rx_create(&rc);
    size_t bound = fifk_tx_bound(tx), n, i;
    double *buf = malloc(bound * sizeof *buf);
    unsigned char out[256];
    const char *text = "CQ DE DG6FL K";

    n = fifk_tx_begin(tx, buf, bound);
    fifk_rx_process(rx, buf, n, out, sizeof out, NULL, 0, NULL);
    for (i = 0; text[i]; i++) {
        n = fifk_tx_put(tx, (unsigned char)text[i], buf, bound);
        n = fifk_rx_process(rx, buf, n, out, sizeof out, NULL, 0, NULL);
        fwrite(out, 1, n, stdout);
    }
    n = fifk_tx_end(tx, buf, bound);
    n = fifk_rx_process(rx, buf, n, out, sizeof out, NULL, 0, NULL);
    fwrite(out, 1, n, stdout);   /* THOR's interleaver delays the last characters */
    free(buf);
    fifk_tx_destroy(tx);
    fifk_rx_destroy(rx);
    return 0;
}
```

### Python (ctypes)

wave-owl binds the library in `src/ifk_native.py`; the minimal form:

```python
import ctypes, numpy as np
lib = ctypes.CDLL("build/libfldigi_ifk.dylib")
class RxConfig(ctypes.Structure):
    _fields_ = [("submode", ctypes.c_char_p), ("carrier_hz", ctypes.c_double),
                ("fast_paths", ctypes.c_int), ("dominoex_fec", ctypes.c_int),
                ("squelch", ctypes.c_double)]
lib.fifk_rx_create.restype = ctypes.c_void_p
lib.fifk_rx_process.restype = ctypes.c_size_t
lib.fifk_rx_process.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_double),
                                ctypes.c_size_t, ctypes.c_char_p, ctypes.c_size_t,
                                ctypes.c_char_p, ctypes.c_size_t,
                                ctypes.POINTER(ctypes.c_size_t)]
rx = lib.fifk_rx_create(ctypes.byref(RxConfig(b"DOMINOEX11", 1500.0, 0, 0, 0.0)))
audio = np.zeros(11025)                      # 1 s at the DominoEX11 rate
out = ctypes.create_string_buffer(4096)
n = lib.fifk_rx_process(rx, audio.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
                        len(audio), out, 4096, None, 0, None)
print(out.raw[:n].decode("latin-1"))
```

## Contract and versioning

`fifk_version()` reports `contract=ifk-c1 abi=1` and the capability tag `tx=1`. The contract
changes only with an incompatible change of the structs or functions; additions keep it and
raise the minor version. wave-owl refuses a library with another contract or ABI.

## Verification

`make test` runs two programs:

- `tests/test_ifk.c` (independent of fldigi): API and submode table, TX -> RX loopback of a
  text with every printable class for all 24 submodes and DominoEX FEC in all 9, secondary
  text only (primary empty between the framing), chunk invariance (1 sample, 4099 samples,
  everything) and reset, offset estimate and decoding at -3 .. +4 Hz off, buffer overflow
  counting, pure noise and impulses in every submode.
- `tests/test_ifk_fldigi.c`: goldens from the fldigi 4.2.13 bench build
  (`tests/golden/make_fldigi_golden.py`; headless fldigi with the TX extension of wave-owl's
  archived change `fix-psk-rtty-vs-fldigi`): 31 overs (24 submodes with `CQ CQ DE DG6FL DG6FL
  K`, DominoEX FEC in 5 submodes, two pangram overs). TX: the FNV-1a hash of the int16 output
  equals fldigi's and stored vectors match to 1 LSB. RX: 93 decodes (clean, -5 dB, -10 dB in
  2500 Hz, deterministic noise rebuilt bit for bit in C) equal fldigi's output byte for byte.

| | Result (macOS arm64, Apple clang, `-O3` and the `-O1` sanitizer build) |
|---|---|
| TX overs identical to fldigi | 31 of 31 |
| RX decodes identical to fldigi | 93 of 93 |

On other platforms (another libm, x86 without FMA) the results are not bit-identical; there
the clean decodes must stay within 1 + 5 % edit distance of fldigi's, and the noisy ones in
total within 20 % + 10 errors, with no more than 3 fewer complete texts. The reference build
is reproduced bit for bit because the library is compiled with `-ffp-contract=on` (fusing
the same scalar expressions as fldigi's Apple clang -O2 build), keeps `std::complex`
products unfused and evaluates `sin`/`cos` pairs with one `sincos` call (fldigi-dsp-core).
CI runs both programs on Ubuntu and macOS and again under AddressSanitizer and
UndefinedBehaviorSanitizer.

## Performance and memory

`make bench` (Apple M4, one core, fldigi's 3 paths), multiples of real time:

| | THOR (K=7) | THOR (K=15) | DominoEX |
|---|---|---|---|
| Receiver | 409 .. 892 | 149 .. 232 | 402 .. 892 |
| Transmitter | 7 900 .. 19 500 | 5 500 .. 13 500 | 12 200 .. 33 000 |

Memory per receiver: the two-symbol pipe of complex DFT vectors (0.2 MiB for THOR100 up to
9.8 MiB for the 2 to 5 baud modes) plus the Viterbi trellis (128 KiB for K=7, 32 MiB for
K=15: fldigi's 256-step path memory times 16384 states).

## Building

```
git submodule update --init      # dsp/ = fldigi-dsp-core, pinned in dsp.lock
make                             # build/libfldigi_ifk.{a,dylib|so}
make test                        # unit tests and fldigi goldens
make sanitize                    # the same under ASan + UBSan
make bench                       # CPU benchmark
make BUILD=/abs/dir shared       # out-of-tree build (wave-owl: make build-ifk-core)
```

Plain make, C99, `-Wall -Wextra -Werror -pedantic`, no dependencies besides libm. The build
first checks the `dsp/` submodule against `dsp.lock` (version, commit, content checksum;
wave-owl R-920.3) and refuses to compile against anything else. `src/ifk_tables.c` is
generated from fldigi's sources: `python3 tools/gen_tables.py /path/to/fldigi`.

## Maintenance and fldigi updates

wave-owl's `scripts/fldigi_upstream_check.py` (skill `fldigi-upstream-sync`) checks every
`file:line` citation of this repository against a new fldigi tag. After a bump: regenerate
`src/ifk_tables.c`, rerun `tests/golden/make_fldigi_golden.py` with the new bench build and
`make test`.

## License and credits

GPL-3.0-or-later, as fldigi. THOR by Dave Freese W1HKJ and John Douyere VK2ETA (the K=15
modes), DominoEX by Murray Greenman ZL1BPU (design) and Dave Freese W1HKJ, MFSK varicode by
Nino Porcino IZ8BLY, all in fldigi 4.2.13. The C port and the tests: Gerolf Ziegenhain DG6FL,
2026. See `CHANGELOG.md`.
