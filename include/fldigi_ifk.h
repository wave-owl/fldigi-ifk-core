/*
 * fldigi-ifk-core -- the THOR and DominoEX modems of fldigi 4.2.13 (IFK+ incremental
 * frequency keying), receiver and transmitter, ported to C99.
 *
 * Copyright (C) 2026 Gerolf Ziegenhain
 * Ported from fldigi 4.2.13 (https://github.com/wave-owl/fldigi, tag v4.2.13, commit
 * e7c3ab3709ad62a7d91829364eb48a05fa325c0d): src/thor/thor.cxx, src/thor/thorvaricode.cxx,
 * src/dominoex/dominoex.cxx, src/dominoex/dominovar.cxx, src/include/thor.h,
 * src/include/dominoex.h; (c) 2006-2020 Dave Freese W1HKJ, John Douyere VK2ETA,
 * Murray Greenman ZL1BPU, Nino Porcino IZ8BLY and the fldigi authors.
 *
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this
 * program. If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef FLDIGI_IFK_H
#define FLDIGI_IFK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FIFK_VERSION_STRING "0.1.0"

/* "fldigi-ifk-core 0.1.0 contract=ifk-c1 abi=1 tx=1". Static string. The contract tag
 * changes only with an incompatible API change; tx=1 announces the transmitter. */
const char *fifk_version(void);

/* ------------------------------------------------------------------------------------
 * Submodes: every THOR and DominoEX mode of fldigi 4.2.13 (globals/globals.cxx:83-91,
 * 200-216; parameters thor.cxx:217-349, dominoex.cxx:217-278).
 * ------------------------------------------------------------------------------------ */
#define FIFK_THOR 1
#define FIFK_DOMINOEX 2

typedef struct fifk_submode {
    const char *name;        /* "THOR16", "THORMICRO", "THOR25X4", "DOMINOEX11", ... */
    const char *fldigi_name; /* fldigi's short name (mode_info sname): "THOR16", "DOMX11" */
    int family;              /* FIFK_THOR or FIFK_DOMINOEX */
    int samplerate;          /* modem rate: 8000, 11025 or 16000 Hz */
    int symlen;              /* samples per symbol */
    int doublespaced;        /* tone spacing in DFT bins (1, 2 or 4) */
    int fec_k;               /* THOR: 7 or 15; DominoEX: 7 (MultiPSK FEC, off by default) */
    int idepth;              /* THOR interleaver depth (4 x 4 x idepth); DominoEX FEC: 4 */
    int flushlength;         /* idle characters after the text (thor.cxx:225, 1301) */
    int micro;               /* 1: Micro (no STX/EOT framing, thor.cxx:1331, 1361) */
    double tonespacing;      /* Hz */
    double bandwidth;        /* 18 x tonespacing, Hz */
    double baud;             /* samplerate / symlen */
} fifk_submode;

int fifk_submode_count(void);                       /* 24 */
const fifk_submode *fifk_submode_at(int index);     /* NULL outside 0 .. count-1 */
/* Case-insensitive lookup by name or fldigi short name; NULL when unknown. */
const fifk_submode *fifk_submode_find(const char *name);

/* ------------------------------------------------------------------------------------
 * Receiver: thor::rx_process (thor.cxx:1115-1191) / dominoex::rx_process
 * (dominoex.cxx:582-640) with fldigi's defaults.
 * ------------------------------------------------------------------------------------ */
typedef struct fifk_rx_config {
    const char *submode;     /* see fifk_submode_find */
    double carrier_hz;       /* audio centre of the signal (fldigi "frequency"), 1500 */
    int fast_paths;          /* 0: fldigi default slowcpu (3 paths); 1: 5 paths */
    int dominoex_fec;        /* DominoEX: 1 decodes MultiPSK FEC instead of the varicode
                                (progdefaults.DOMINOEX_FEC, default 0); THOR: ignored */
    double squelch;          /* 0: squelch off (fldigi bench default); else metric
                                threshold 0..100 (progStatus.sldrSquelchValue) */
} fifk_rx_config;

typedef struct fifk_rx fifk_rx;

/* NULL for an unknown submode, a carrier outside (bandwidth/2, samplerate/2 -
 * bandwidth/2) or out of memory. */
fifk_rx *fifk_rx_create(const fifk_rx_config *cfg);
void fifk_rx_destroy(fifk_rx *rx);
/* Back to the state after create (rx_init plus a fresh front end and decoder). */
void fifk_rx_reset(fifk_rx *rx);
/* Feed n samples at the submode's rate (fldigi's sound card scale: int16 / 32768).
 * The bytes fldigi hands to put_rx_char() go to primary (NUL included, as fldigi
 * prints them), the bytes of put_sec_char() (secondary/idle text) to secondary.
 * Returns the number of primary bytes; *n_secondary (may be NULL) gets the number of
 * secondary bytes. Bytes beyond max_primary / max_secondary are dropped and counted
 * (fifk_rx_dropped). Either buffer may be NULL with its max 0. */
size_t fifk_rx_process(fifk_rx *rx, const double *x, size_t n,
                       unsigned char *primary, size_t max_primary,
                       unsigned char *secondary, size_t max_secondary, size_t *n_secondary);
double fifk_rx_snr_db(const fifk_rx *rx);   /* fldigi's s/n estimate (eval_s2n) */
double fifk_rx_metric(const fifk_rx *rx);   /* squelch metric 0..100 */
int fifk_rx_quality(const fifk_rx *rx);     /* THOR FEC confidence 0..100 (decodePairs) */
/* Offset of the signal from carrier_hz in Hz, averaged over recent symbols, within
 * +- tonespacing/2. Not in fldigi (no AFC for these modes): an estimate for display. */
double fifk_rx_offset_hz(const fifk_rx *rx);
size_t fifk_rx_dropped(const fifk_rx *rx);

/* ------------------------------------------------------------------------------------
 * Transmitter: thor::tx_process (thor.cxx:1313-1393) / dominoex::tx_process
 * (dominoex.cxx:726-771), one call per state step. Output: the samples fldigi hands to
 * modem::ModulateXmtr after its soft start/stop ramp (modem.cxx:611-637), before the
 * TX level (multiply by 10^(txlevel/20), fldigi's -12 dB, to reproduce fldigi's WAV).
 * ------------------------------------------------------------------------------------ */
typedef struct fifk_tx_config {
    const char *submode;
    double carrier_hz;          /* audio centre (fldigi tx frequency), 1500 */
    int dominoex_fec;           /* DominoEX: MultiPSK FEC (default 0) */
    const char *secondary_text; /* idle text; NULL or "": fldigi's "fldigi 4.2.13 " */
    int no_soft_edges;          /* 1: no softTHOR/softDOMINOEX ramps (fldigi default on) */
    double risetime_ms;         /* THOR tone edge shaping; < 0: fldigi default 4.0 ms */
} fifk_tx_config;

typedef struct fifk_tx fifk_tx;

fifk_tx *fifk_tx_create(const fifk_tx_config *cfg);
void fifk_tx_destroy(fifk_tx *tx);
/* Largest number of samples one fifk_tx_begin/put/idle/end call can produce. */
size_t fifk_tx_bound(const fifk_tx *tx);
/* Each call returns the samples written to out, or 0 (nothing done) when out_max <
 * fifk_tx_bound(tx) or the call is out of order (begin once, then put/idle, then end). */
/* TX_STATE_PREAMBLE + TX_STATE_START: clear the coder, 16 preamble symbols (THOR),
 * an idle character, CR STX CR (CR only for Micro). */
size_t fifk_tx_begin(fifk_tx *tx, double *out, size_t out_max);
/* TX_STATE_DATA with a character from the keyboard: one byte of primary text. */
size_t fifk_tx_put(fifk_tx *tx, unsigned char c, double *out, size_t out_max);
/* TX_STATE_DATA without keyboard data: one character of the secondary text. */
size_t fifk_tx_idle(fifk_tx *tx, double *out, size_t out_max);
/* TX_STATE_END + TX_STATE_FLUSH: CR EOT CR (CR for Micro), flushlength idle characters
 * (4 for DominoEX) and the soft-stop tone. */
size_t fifk_tx_end(fifk_tx *tx, double *out, size_t out_max);

#ifdef __cplusplus
}
#endif

#endif /* FLDIGI_IFK_H */
