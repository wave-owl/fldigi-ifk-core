/*
 * fldigi-ifk-core internals (not installed).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef IFK_INTERNAL_H
#define IFK_INTERNAL_H

#include "fldigi_ifk.h"
#include "fldigi_dsp.h"

#define IFK_NUMTONES 18          /* THORNUMTONES / NUMTONES, thor.h:59, dominoex.h:46 */
#define IFK_MAXFFTS 8            /* THORMAXFFTS / MAXFFTS, thor.h:60, dominoex.h:47 */
#define IFK_SLOWPATHS 3          /* THORSLOWPATHS, thor.h:66 */
#define IFK_FASTPATHS 5          /* THORFASTPATHS, thor.h:67 */
#define IFK_MAXPATHS (8 * IFK_FASTPATHS * IFK_NUMTONES)   /* MAXPATHS, thor.h:73 */
#define THOR_BASEFREQ 1500.0     /* THORBASEFREQ, thor.h:61 */
#define THOR_FIRSTIF 2000.0      /* THORFIRSTIF, thor.h:62 */
#define DOM_BASEFREQ 1000.0      /* BASEFREQ, dominoex.h:48 */
#define DOM_FIRSTIF 1500.0       /* FIRSTIF, dominoex.h:49 */
#define IFK_FILTER_BW 2.0        /* THOR_BW / DOMINOEX_BW default, configuration.h:820-822 */
#define IFK_SOFTSTART_MS 100     /* SoftStart default, configuration.h:222-224 */
#define THOR_RISETIME_MS 4.0     /* THOR_RISETIME default, configuration.h:832-834 */
#define DOM_MAX_VARICODE_LEN 3   /* MAX_VARICODE_LEN, dominovar.h:27 */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define IFK_TWOPI (2.0 * M_PI)   /* TWOPI, modem.h:45 */

extern const char *const fifk_thor_varicode[91];
extern const unsigned int fifk_thor_varidecode[91];
extern const unsigned char fifk_dominoex_varicode[512][3];
extern const short fifk_dominoex_varidecode[4096];

/* THOR varicode (thorvaricode.cxx:170-193) and DominoEX varicode (dominovar.cxx:366-377). */
const char *fifk_thorvarienc(int c, int sec);
int fifk_thorvaridec(unsigned int symbol);
const unsigned char *fifk_dominoex_varienc(unsigned char c, int secondary);
int fifk_dominoex_varidec(unsigned int symbol);
/* MultiPSK FEC character maps (dominoex.cxx:150-191, 787-823). */
unsigned char fifk_mupsk_sec2pri(int c);
unsigned int fifk_mupsk_prisecchar(unsigned int c);

struct fifk_rx {
    const fifk_submode *m;
    double frequency;
    int thor;
    int dominoex_fec;
    double squelch;          /* 0: off */
    /* front end */
    int paths, extones, basetone, lotone, hitone, numbins, nvec;
    unsigned int twosym;
    double phase[IFK_MAXFFTS + 1];
    fdsp_fir hilbert;
    fdsp_fftfilt fft;
    fdsp_sfft binsfft[IFK_FASTPATHS];
    fdsp_cmpx *pipe;         /* twosym x nvec */
    unsigned int pipeptr;
    int synccounter;
    fdsp_movavg syncfilter;
    /* per symbol */
    int currsymbol, prev1symbol, prev2symbol;
    double currmag, prev1mag, prev2mag;
    int staticburst;
    double sig, noise, s2n, metric;
    /* THOR soft decisions (statics of thor.cxx:612-613, 784-785, 859-860, 542) */
    unsigned char lastCWI[IFK_MAXPATHS + 2];
    unsigned char nextCWI[IFK_MAXPATHS + 2];
    int lastc, lastmag, nowmag, prev1rawdoppler;
    double lastdoppler, nowdoppler;
    int preamblecheck, twocount, neg16seen;
    int fec_confidence;
    /* THOR FEC / DominoEX MultiPSK FEC */
    fdsp_viterbi dec;
    fdsp_interleave rxinlv;
    unsigned char symbolpair[2];
    int symcounter;
    unsigned int datashreg;
    /* DominoEX varicode */
    unsigned char symbolbuf[DOM_MAX_VARICODE_LEN];
    int dsymcounter;
    int outofrange;
    /* diagnostics: decided IFK+ nibbles (ring) */
    int nibbles[256];
    size_t nib_head, nib_count;
    /* offset estimate */
    double offset;
    int offset_valid;
    /* output of the current process() call */
    unsigned char *outp, *outs;
    size_t maxp, maxs, np, ns, dropped;
};

struct fifk_tx {
    const fifk_submode *m;
    double frequency;
    int thor;
    int dominoex_fec;
    int soft;
    double risetime_ms;
    char sectext[256];
    size_t cptr;
    int state;               /* 0 new, 1 data, 2 ended */
    double txphase;
    int txprevtone;
    unsigned int bitshreg;
    int bitstate;
    fdsp_encoder enc;
    fdsp_interleave txinlv;
    /* modem::ModulateXmtr soft start/stop (modem.cxx:611-637) */
    int sig_start, sig_stop, sig_smpl;
    /* output of the current call */
    double *out;
    size_t n, max;
};

#endif
