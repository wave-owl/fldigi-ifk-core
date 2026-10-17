/*
 * fldigi-ifk-core receiver: fldigi 4.2.13's thor::rx_process and dominoex::rx_process.
 *
 * Copyright (C) 2026 Gerolf Ziegenhain
 * Original C++: thor/thor.cxx, dominoex/dominoex.cxx (c) 2006-2020 Dave Freese W1HKJ,
 * John Douyere VK2ETA, Murray Greenman ZL1BPU and the fldigi authors.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Settings are fldigi's defaults (configuration.h): THOR_FILTER / DOMINOEX_FILTER on with
 * THOR_BW / DOMINOEX_BW 2.0, slowcpu (3 paths), THOR_SOFTSYMBOLS, THOR_SOFTBITS and
 * THOR_PREAMBLE on, ThorCWI 0, DOMINOEX_FEC off, AFC none (the modes have no AFC),
 * reverse off. fldigi's quirks that change decoded text are kept on purpose:
 * - THOR's path mixers use the integer quotient samplerate / symlen (thor.cxx:440);
 * - DominoEX's CWI test reads pipe[j] instead of pipe[p] (dominoex.cxx:446-450), and
 *   its average is accumulated twice (:435-465);
 * - THOR's soft decisions keep their state in function statics shared by all THOR
 *   instances in fldigi (thor.cxx:542, 612-613, 784-785, 859-860); here they belong to
 *   one receiver, which equals fldigi for a single receiver.
 * Left out: the THOR picture/avatar receiver (thor.cxx:456-493, 1041-1113: after a
 * "pic%" header fldigi stops printing text until the image ends; this port keeps
 * decoding text), scopes, waterfall and GUI status, the PSKmail s/n report.
 */
#include "ifk_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------------- */
/* helpers                                                                             */
/* ---------------------------------------------------------------------------------- */

static double cabs_(fdsp_cmpx z)
{
    return hypot(z.re, z.im);   /* std::abs(std::complex<double>) */
}

static fdsp_cmpx *vec(fifk_rx *rx, unsigned int idx)
{
    return rx->pipe + (size_t)idx * (size_t)rx->nvec;
}

static void put_primary(fifk_rx *rx, int c)
{
    if (rx->np < rx->maxp)
        rx->outp[rx->np++] = (unsigned char)c;
    else
        rx->dropped++;
}

static void put_secondary(fifk_rx *rx, int c)
{
    if (rx->ns < rx->maxs)
        rx->outs[rx->ns++] = (unsigned char)c;
    else
        rx->dropped++;
}

static int squelched(const fifk_rx *rx)
{
    /* progStatus.sqlonoff && metric < progStatus.sldrSquelchValue */
    return rx->squelch > 0 && rx->metric < rx->squelch;
}

/* ---------------------------------------------------------------------------------- */
/* mixers, thor/thor.cxx:431-448 and dominoex/dominoex.cxx:341-359                     */
/* ---------------------------------------------------------------------------------- */

static fdsp_cmpx mixer(fifk_rx *rx, int n, fdsp_cmpx in)
{
    /* thor::mixer (thor/thor.cxx:431-448), dominoex::mixer (dominoex/dominoex.cxx:341-359) */
    double f, c, s;
    fdsp_cmpx z;
    const fifk_submode *m = rx->m;
    if (rx->thor) {
        if (n == 0)
            f = rx->frequency - THOR_FIRSTIF;
        else
            f = THOR_FIRSTIF - THOR_BASEFREQ - m->bandwidth * 0.5 +
                (m->samplerate / m->symlen) * ((double)n / rx->paths);
    } else {
        if (n == 0)
            f = rx->frequency - DOM_FIRSTIF;
        else
            f = DOM_FIRSTIF - DOM_BASEFREQ - m->bandwidth / 2.0 +
                m->tonespacing * (1.0 * (n - 1) / rx->paths);
    }
    {
        /* cmplx z(cos(phase[n]), sin(phase[n])); z *= in; -- one sincos call as in
         * fldigi's optimised build, then std::complex multiply with the four products in
         * separate statements (no fused multiply-add, as in fldigi) */
        double ac, bd, ad, bc;
        fdsp_sincos(rx->phase[n], &s, &c);
        ac = c * in.re;
        bd = s * in.im;
        ad = c * in.im;
        bc = s * in.re;
        z.re = ac - bd;
        z.im = ad + bc;
    }
    rx->phase[n] -= IFK_TWOPI * f / m->samplerate;
    if (rx->phase[n] < 0)
        rx->phase[n] += IFK_TWOPI;
    return z;
}

/* ---------------------------------------------------------------------------------- */
/* THOR per-symbol decoding, thor/thor.cxx:495-967                                     */
/* ---------------------------------------------------------------------------------- */

static void thor_recvchar(fifk_rx *rx, int c)
{
    /* thor::recvchar, thor/thor.cxx:495-519 (picture header and PSKmail hooks left out) */
    if (c == -1)
        return;
    if (c & 0x100)
        put_secondary(rx, c & 0xFF);
    else
        put_primary(rx, c & 0xFF);
}

static void thor_decode_pairs(fifk_rx *rx, unsigned char symbol)
{
    /* thor::decodePairs, thor/thor.cxx:525-563 */
    int c, ch, met = 0;
    rx->symbolpair[0] = rx->symbolpair[1];
    rx->symbolpair[1] = symbol;
    rx->symcounter = rx->symcounter ? 0 : 1;
    if (rx->symcounter)
        return;
    c = fdsp_viterbi_decode(&rx->dec, rx->symbolpair, &met);
    if (c == -1)
        return;
    if (met < 255 / 2)
        rx->fec_confidence -= 2 + rx->fec_confidence / 2;
    else
        rx->fec_confidence += 2;
    if (rx->fec_confidence < 0)
        rx->fec_confidence = 0;
    if (rx->fec_confidence > 100)
        rx->fec_confidence = 100;
    if (squelched(rx))
        return;
    rx->datashreg = (rx->datashreg << 1) | (unsigned int)!!c;
    if ((rx->datashreg & 7) == 1) {
        ch = fifk_thorvaridec(rx->datashreg >> 1);
        thor_recvchar(rx, ch);
        rx->datashreg = 1;
    }
}

static int thor_preambledetect(fifk_rx *rx, int c)
{
    /* thor::preambledetect, thor/thor.cxx:857-881 */
    if (rx->twocount > 14)
        rx->twocount = 0;
    if (-16 == c && rx->twocount > 2)
        rx->neg16seen = 1;
    else if (2 != c)
        rx->neg16seen = 0;
    else if (2 == c)
        rx->twocount++;
    if (-16 != c && 2 != c)
        if (rx->twocount > 1)
            rx->twocount -= 2;
    if (rx->twocount > 4 && rx->neg16seen) {
        if (++rx->preamblecheck > 4)
            return 1;
    } else {
        rx->preamblecheck = 0;
    }
    return 0;
}

static void thor_softflushrx(fifk_rx *rx)
{
    /* thor::softflushrx, thor/thor.cxx:884-899 */
    unsigned char puncture[2], flushsymbols[4];
    int i;
    puncture[0] = 128;
    puncture[1] = 128;
    for (i = 0; i < 4; i++)
        flushsymbols[i] = 128;
    for (i = 0; i < 90; i++)
        fdsp_interleave_symbols(&rx->rxinlv, flushsymbols);
    for (i = 0; i < 128; i++)
        fdsp_viterbi_decode(&rx->dec, puncture, NULL);
}

static void thor_softdecodesymbol(fifk_rx *rx)
{
    /* thor::softdecodesymbol, thor/thor.cxx:608-729 */
    unsigned char one, zero, lastsymbols[4];
    int c, nextmag = 127, rawdoppler = 0, period, i;
    int outofrange = 0;
    double fdiff = rx->currsymbol - rx->prev1symbol;

    fdiff /= rx->paths;
    fdiff /= rx->m->doublespaced;
    c = (int)floor(fdiff + .5);
    if (c < -16 || 0 == c || 1 == c || c > 17)
        outofrange = 1;
    if (thor_preambledetect(rx, c)) {
        thor_softflushrx(rx);
        rx->lastmag = 0;
        return;
    }
    c -= 2;
    if (c < 0)
        c += IFK_NUMTONES;

    period = rx->paths * rx->m->doublespaced;
    rawdoppler = (rx->currsymbol - rx->prev1symbol) % period;
    if (0 == rawdoppler) {
        rx->nowdoppler = 1.0;
    } else {
        if (-1 * rx->prev1rawdoppler == rawdoppler) {
            rawdoppler = 0;
            rx->lastdoppler = 1.0;
        }
        if (abs(rawdoppler) <= period / 2)
            rx->nowdoppler = 1.0 - ((1.0 / period) * abs(rawdoppler));
        else
            rx->nowdoppler = 0.0 + ((1.0 / period) * abs(rawdoppler));
    }
    rx->prev1rawdoppler = rawdoppler;

    if (outofrange) {
        rx->lastmag /= 2;
        rx->nowmag = 0;
        nextmag /= 2;
    }
    if (0 == rx->currsymbol) {
        rx->nowmag = 0;
        nextmag = 0;
    }
    if (squelched(rx))
        rx->nowmag = 0;
    if (rx->staticburst) {
        rx->nowmag /= 16;
        nextmag /= 16;
    }
    rx->lastmag = (int)(rx->lastmag * rx->lastdoppler);

    if (rx->lastmag <= 0) {
        one = 128;
        zero = 128;
    } else if (rx->lastmag > 127) {
        one = 255;
        zero = 0;
    } else {
        one = (unsigned char)(rx->lastmag + 128);
        zero = (unsigned char)(127 - rx->lastmag);
    }
    {
        int lc = rx->lastc;
        lastsymbols[3] = (lc & 1) == 1 ? one : zero; lc /= 2;
        lastsymbols[2] = (lc & 1) == 1 ? one : zero; lc /= 2;
        lastsymbols[1] = (lc & 1) == 1 ? one : zero; lc /= 2;
        lastsymbols[0] = (lc & 1) == 1 ? one : zero;
    }
    fdsp_interleave_symbols(&rx->rxinlv, lastsymbols);
    for (i = 0; i < 4; i++)
        thor_decode_pairs(rx, lastsymbols[i]);

    rx->lastc = c;
    rx->lastmag = rx->nowmag;
    rx->nowmag = nextmag;
    rx->lastdoppler = rx->nowdoppler;
}

static int thor_softdecode(fifk_rx *rx)
{
    /* thor::softdecode, thor/thor.cxx:782-855 */
    const int SoftBailout = 6;
    double x, max = 0.0, avg = 0.0;
    int symbol = 0, avgcount = 0, soft_symbol_trycount = 0, i;
    int lowest_tone = 0, highest_tone = rx->nvec;
    unsigned char *lastCWI = rx->lastCWI, *nextCWI = rx->nextCWI;
    fdsp_cmpx *v = vec(rx, rx->pipeptr);

    memset(nextCWI, 0, IFK_MAXPATHS);
    if (rx->prev2symbol && (rx->prev2symbol < IFK_MAXPATHS - 1))
        lastCWI[rx->prev2symbol - 1] = lastCWI[rx->prev2symbol] =
            lastCWI[rx->prev2symbol + 1] = 0;
    lowest_tone += 1;
    highest_tone -= 1;
    for (i = lowest_tone; i < highest_tone; i++) {
        if (!lastCWI[i]) {
            avg += cabs_(v[i]);
            avgcount++;
        }
    }
    avg /= avgcount;
    if (avg < 1e-10)
        avg = 1e-10;
    do {
        soft_symbol_trycount++;
        max = 0.0;
        for (i = lowest_tone; i < highest_tone; i++) {
            x = cabs_(v[i]);
            if (x > max && !nextCWI[i - 1] && !nextCWI[i] && !nextCWI[i + 1]) {
                max = x;
                symbol = i;
            }
        }
        if (symbol && (symbol < IFK_MAXPATHS - 1)) {
            if (abs(rx->prev1symbol - symbol) < rx->paths)
                nextCWI[symbol - 1] = nextCWI[symbol] = nextCWI[symbol + 1] = 1;
            else if (lastCWI[symbol - 1] || lastCWI[symbol] || lastCWI[symbol + 1])
                nextCWI[symbol - 1] = nextCWI[symbol] = nextCWI[symbol + 1] = 1;
        }
    } while (nextCWI[symbol] && soft_symbol_trycount < SoftBailout);
    for (i = lowest_tone - 1; i < highest_tone + 1; i++)
        lastCWI[i] = nextCWI[i];
    rx->staticburst = (max / avg < 1.2);
    if (soft_symbol_trycount >= SoftBailout)
        return 0;
    return symbol;
}

/* s/n calibration of thor::eval_s2n, thor/thor.cxx:969-981 */
static const float thor_snvals[10][2] = {
    {12.6f, -9}, {15.2f, -6}, {18.4f, -3}, {21.5f, 0}, {23.5f, 3},
    {26.1f, 6}, {27.3f, 9}, {30.1f, 12}, {32.0f, 16}, {32.8f, 20}
};

static void thor_eval_s2n(fifk_rx *rx)
{
    /* thor::eval_s2n, thor/thor.cxx:983-1039 (status display left out) */
    double s = cabs_(vec(rx, rx->pipeptr)[rx->currsymbol]);
    double n = cabs_(vec(rx, (rx->pipeptr + (unsigned int)rx->m->symlen) % rx->twosym)
                         [rx->currsymbol]);
    double val;
    float v0, v1, s0, s1;
    size_t i;
    rx->sig = fdsp_decayavg(rx->sig, s, 128);
    rx->noise = fdsp_decayavg(rx->noise, n, 128);
    if (rx->noise == 0)
        rx->noise = rx->sig / 1000.0;
    val = 20 * log10(rx->sig / rx->noise);
    for (i = 0; i < 10 - 1; i++)
        if (val >= thor_snvals[i][0] && val < thor_snvals[i + 1][0])
            break;
    /* Outside the table fldigi's loop ends with i = 9 and reads snvals[10], past the
     * array (thor/thor.cxx:997-1004); this port extrapolates the last segment instead. */
    if (i == 10 - 1)
        i = 10 - 2;
    v0 = thor_snvals[i][0];
    v1 = thor_snvals[i + 1][0];
    s0 = thor_snvals[i][1];
    s1 = thor_snvals[i + 1][1];
    rx->s2n = (int)ceil(s0 + (s1 - s0) * (val - v0) / (v1 - v0));
    rx->metric = 3 * val < 0.0 ? 0.0 : (3 * val > 100.0 ? 100.0 : 3 * val);
}

static void synchronize(fifk_rx *rx)
{
    /* thor::synchronize (thor/thor.cxx:940-967), dominoex::synchronize
     * (dominoex/dominoex.cxx:513-540); the sync scope is left out */
    double syn = -1, val, max = 0.0;
    unsigned int i, j;
    if (rx->staticburst)
        return;
    if (rx->currsymbol == rx->prev1symbol)
        return;
    if (rx->prev1symbol == rx->prev2symbol)
        return;
    for (i = 0, j = rx->pipeptr; i < rx->twosym; i++) {
        val = cabs_(vec(rx, j)[rx->prev1symbol]);
        if (val > max) {
            max = val;
            syn = i;
        }
        j = (j + 1) % rx->twosym;
    }
    syn = fdsp_movavg_run(&rx->syncfilter, syn);
    rx->synccounter += (int)floor(1.0 * (syn - rx->m->symlen) / IFK_NUMTONES + 0.5);
}

/* ---------------------------------------------------------------------------------- */
/* DominoEX per-symbol decoding, dominoex/dominoex.cxx:361-579, 829-874                */
/* ---------------------------------------------------------------------------------- */

static void dom_recvchar(fifk_rx *rx, int c)
{
    /* dominoex::recvchar, dominoex/dominoex.cxx:361-372 */
    if (!(rx->squelch > 0) || rx->metric > rx->squelch) {
        if (c == -1)
            return;
        if (c & 0x100)
            put_secondary(rx, c & 0xFF);
        else
            put_primary(rx, c & 0xFF);
    }
}

static void dom_decode_domino(fifk_rx *rx, int c)
{
    /* dominoex::decodeDomino, dominoex/dominoex.cxx:374-401 */
    int sym, ch, i;
    if (!(c & 0x8)) {
        if (rx->dsymcounter <= DOM_MAX_VARICODE_LEN) {
            sym = 0;
            for (i = 0; i < rx->dsymcounter; i++)
                sym |= rx->symbolbuf[i] << (4 * i);
            ch = fifk_dominoex_varidec((unsigned int)sym);
            if (!rx->dominoex_fec)
                if (!rx->staticburst && !rx->outofrange)
                    dom_recvchar(rx, ch);
        }
        rx->dsymcounter = 0;
    }
    for (i = DOM_MAX_VARICODE_LEN - 1; i > 0; i--)
        rx->symbolbuf[i] = rx->symbolbuf[i - 1];
    rx->symbolbuf[0] = (unsigned char)c;
    rx->dsymcounter++;
    if (rx->dsymcounter > DOM_MAX_VARICODE_LEN + 1)
        rx->dsymcounter = DOM_MAX_VARICODE_LEN + 1;
}

static void dom_decode_mupsk_symbol(fifk_rx *rx, unsigned char symbol)
{
    /* dominoex::decodeMuPskSymbol, dominoex/dominoex.cxx:829-855 */
    int c, ch, met = 0;
    rx->symbolpair[0] = rx->symbolpair[1];
    rx->symbolpair[1] = symbol;
    rx->symcounter = rx->symcounter ? 0 : 1;
    if (rx->symcounter)
        return;
    c = fdsp_viterbi_decode(&rx->dec, rx->symbolpair, &met);
    if (c == -1)
        return;
    if (squelched(rx))
        return;
    rx->datashreg = (rx->datashreg << 1) | (unsigned int)!!c;
    if ((rx->datashreg & 7) == 1) {
        ch = fdsp_mfsk_varidec(rx->datashreg >> 1);
        if (rx->dominoex_fec)
            dom_recvchar(rx, (int)fifk_mupsk_prisecchar((unsigned int)ch));
        rx->datashreg = 1;
    }
}

static void dom_decode_mupsk_ex(fifk_rx *rx, int ch)
{
    /* dominoex::decodeMuPskEX, dominoex/dominoex.cxx:857-874 */
    unsigned char symbols[4];
    int c = ch, i;
    for (i = 0; i < 4; i++) {
        if ((c & 1) == 1)
            symbols[3 - i] = 255;
        else
            symbols[3 - i] = 1;
        c = c / 2;
    }
    if (rx->staticburst || rx->outofrange)
        symbols[3] = symbols[2] = symbols[1] = symbols[0] = 0;
    fdsp_interleave_symbols(&rx->rxinlv, symbols);
    for (i = 0; i < 4; i++)
        dom_decode_mupsk_symbol(rx, symbols[i]);
}

static void dom_decodesymbol(fifk_rx *rx)
{
    /* dominoex::decodesymbol, dominoex/dominoex.cxx:403-425 */
    int c;
    double fdiff = rx->currsymbol - rx->prev1symbol;
    fdiff /= rx->m->doublespaced;
    fdiff /= rx->paths;
    rx->outofrange = 0;
    c = (int)floor(fdiff + .5) - 2;
    if (c < 0)
        c += IFK_NUMTONES;
    dom_decode_domino(rx, c);
    dom_decode_mupsk_ex(rx, c);
}

static int dom_harddecode(fifk_rx *rx)
{
    /* dominoex::harddecode, dominoex/dominoex.cxx:427-469 */
    double x, max = 0.0, avg = 0.0, cwmag;
    int symbol = 0, i, j, count, numtests = 10;
    int n = rx->nvec;
    fdsp_cmpx *v = vec(rx, rx->pipeptr);
    unsigned char *cwi = rx->nextCWI;

    for (i = 0; i < n; i++)
        avg += cabs_(v[i]);
    avg /= n;
    if (avg < 1e-10)
        avg = 1e-10;
    for (i = 0; i < n; i++) {
        count = 0;
        for (j = 1; j <= numtests; j++) {
            /* fldigi computes p = pipeptr - j but reads pipe[j] (:447-449) */
            cwmag = cabs_(vec(rx, (unsigned int)j)[i]) / numtests;
            if (cwmag >= 50.0 * (1.0 - 0.0) * avg)
                count++;
        }
        cwi[i] = (count == numtests);
    }
    for (i = 0; i < n; i++) {
        if (!cwi[i]) {
            x = cabs_(v[i]);
            avg += x;
            if (x > max) {
                max = x;
                symbol = i;
            }
        }
    }
    avg /= n;
    rx->staticburst = (max / avg < 1.2);
    return symbol;
}

static void dom_eval_s2n(fifk_rx *rx)
{
    /* dominoex::eval_s2n, dominoex/dominoex.cxx:542-580 (status display left out) */
    double s = cabs_(vec(rx, rx->pipeptr)[rx->currsymbol]);
    double n = (IFK_NUMTONES - 1) *
               cabs_(vec(rx, (rx->pipeptr + (unsigned int)rx->m->symlen) % rx->twosym)
                         [rx->currsymbol]);
    rx->sig = fdsp_decayavg(rx->sig, s, fabs(s - rx->sig) > 4 ? 4 : 32);
    rx->noise = fdsp_decayavg(rx->noise, n, 64);
    if (rx->noise == 0)
        rx->noise = rx->sig / 1000.0;
    else
        rx->s2n = 20 * log10(rx->sig / rx->noise) - 6;
    rx->metric = 6 * (rx->s2n + 18.4);
    rx->metric = rx->metric < 0 ? 0 : rx->metric > 100 ? 100 : rx->metric;
}

/* ---------------------------------------------------------------------------------- */
/* offset estimate (not in fldigi)                                                     */
/* ---------------------------------------------------------------------------------- */

static void update_offset(fifk_rx *rx)
{
    /* Frequency of the chosen symbol index from the bin and path layout of rx_process
     * (bin lotone + i / paths, path mixer i % paths + 1), relative to the nearest tone
     * of a signal centred on carrier_hz. */
    const fifk_submode *m = rx->m;
    int i = rx->currsymbol, n = i % rx->paths + 1, t;
    double binw = (double)m->samplerate / m->symlen, f, base, d;
    if (rx->staticburst || i <= 0)
        return;
    if (rx->thor) {
        f = (rx->lotone + i / rx->paths) * binw +
            (m->samplerate / m->symlen) * ((double)n / rx->paths);
        base = THOR_BASEFREQ;
    } else {
        f = (rx->lotone + i / rx->paths) * binw + m->tonespacing * (1.0 * (n - 1) / rx->paths);
        base = DOM_BASEFREQ;
    }
    t = (int)floor((f - base) / m->tonespacing - 0.5 + 0.5);
    d = f - (base + (t + 0.5) * m->tonespacing);
    if (!rx->offset_valid) {
        rx->offset = d;
        rx->offset_valid = 1;
    } else {
        rx->offset = fdsp_decayavg(rx->offset, d, 16);
    }
}

/* ---------------------------------------------------------------------------------- */
/* rx_process, thor/thor.cxx:1115-1191 and dominoex/dominoex.cxx:582-640                */
/* ---------------------------------------------------------------------------------- */

static void symbol_step(fifk_rx *rx)
{
    if (rx->thor) {
        /* thor/thor.cxx:1159-1179; harddecode() (:1162) is skipped: with
         * THOR_SOFTSYMBOLS its symbol and staticburst are overwritten by softdecode() */
        rx->currsymbol = thor_softdecode(rx);
        rx->currmag = cabs_(vec(rx, rx->pipeptr)[rx->currsymbol]);
        thor_eval_s2n(rx);
        thor_softdecodesymbol(rx);
        synchronize(rx);
        update_offset(rx);
        rx->prev2symbol = rx->prev1symbol;
        rx->prev1symbol = rx->currsymbol;
        rx->prev2mag = rx->prev1mag;
        rx->prev1mag = rx->currmag;
    } else {
        /* dominoex/dominoex.cxx:621-630 */
        rx->currsymbol = dom_harddecode(rx);
        dom_decodesymbol(rx);
        synchronize(rx);
        dom_eval_s2n(rx);
        update_offset(rx);
        rx->prev2symbol = rx->prev1symbol;
        rx->prev1symbol = rx->currsymbol;
    }
}

size_t fifk_rx_process(fifk_rx *rx, const double *x, size_t n,
                       unsigned char *primary, size_t max_primary,
                       unsigned char *secondary, size_t max_secondary, size_t *n_secondary)
{
    /* thor::rx_process (thor/thor.cxx:1115-1191), dominoex::rx_process
     * (dominoex/dominoex.cxx:582-640) */
    size_t s;
    int i, k, nout;
    fdsp_cmpx zref;
    const fdsp_cmpx *zp;

    rx->outp = primary;
    rx->maxp = primary ? max_primary : 0;
    rx->outs = secondary;
    rx->maxs = secondary ? max_secondary : 0;
    rx->np = rx->ns = 0;
    for (s = 0; s < n; s++) {
        zref.re = x[s];
        zref.im = x[s];
        fdsp_fir_run(&rx->hilbert, zref, &zref);
        zref = mixer(rx, 0, zref);
        nout = fdsp_fftfilt_run(&rx->fft, zref, &zp);
        for (i = 0; i < nout; i++) {
            fdsp_cmpx *pv = vec(rx, rx->pipeptr);
            for (k = 0; k < rx->paths; k++) {
                fdsp_cmpx z = mixer(rx, k + 1, zp[i]);
                fdsp_sfft_run(&rx->binsfft[k], z, pv + k, rx->paths);
            }
            if (--rx->synccounter <= 0) {
                rx->synccounter = rx->m->symlen;
                symbol_step(rx);
            }
            rx->pipeptr++;
            if (rx->pipeptr >= rx->twosym)
                rx->pipeptr = 0;
        }
    }
    if (n_secondary)
        *n_secondary = rx->ns;
    rx->outp = rx->outs = NULL;
    return rx->np;
}

/* ---------------------------------------------------------------------------------- */
/* construction, thor/thor.cxx:81-158, 217-426 and dominoex/dominoex.cxx:71-121, 217-337 */
/* ---------------------------------------------------------------------------------- */

static void rx_free_parts(fifk_rx *rx)
{
    int k;
    fdsp_fir_free(&rx->hilbert);
    fdsp_fftfilt_free(&rx->fft);
    for (k = 0; k < IFK_FASTPATHS; k++)
        fdsp_sfft_free(&rx->binsfft[k]);
    fdsp_viterbi_free(&rx->dec);
    fdsp_interleave_free(&rx->rxinlv);
    free(rx->pipe);
    rx->pipe = NULL;
}

static int rx_build(fifk_rx *rx)
{
    /* thor::thor + reset_filters + rx_init (thor/thor.cxx:217-426, 107-158, 81-105),
     * dominoex::dominoex + reset_filters + rx_init (dominoex/dominoex.cxx:217-337,
     * 93-121, 71-91) */
    const fifk_submode *m = rx->m;
    double firstif = rx->thor ? THOR_FIRSTIF : DOM_FIRSTIF;
    double basefreq = rx->thor ? THOR_BASEFREQ : DOM_BASEFREQ;
    int k;

    rx->basetone = (int)floor(basefreq * m->symlen / m->samplerate + 0.5);
    rx->lotone = rx->basetone - rx->extones * m->doublespaced;
    rx->hitone = rx->basetone + IFK_NUMTONES * m->doublespaced + rx->extones * m->doublespaced;
    rx->numbins = rx->hitone - rx->lotone;
    rx->nvec = rx->paths * rx->numbins;
    rx->twosym = 2u * (unsigned int)m->symlen;
    if (rx->lotone < 0 || rx->hitone > m->symlen || rx->nvec > IFK_MAXPATHS)
        return -1;
    if (fdsp_fir_init_hilbert(&rx->hilbert, 37, 1) < 0)
        return -1;
    if (fdsp_fftfilt_init(&rx->fft, (firstif - 0.5 * IFK_FILTER_BW * m->bandwidth) / m->samplerate,
                          (firstif + 0.5 * IFK_FILTER_BW * m->bandwidth) / m->samplerate,
                          1024) < 0)
        return -1;
    for (k = 0; k < rx->paths; k++)
        if (fdsp_sfft_init(&rx->binsfft[k], m->symlen, rx->lotone, rx->hitone) < 0)
            return -1;
    rx->pipe = (fdsp_cmpx *)calloc((size_t)rx->twosym * (size_t)rx->nvec, sizeof(fdsp_cmpx));
    if (!rx->pipe)
        return -1;
    if (fdsp_movavg_init(&rx->syncfilter, rx->thor ? 8 : 16) < 0)
        return -1;
    if (rx->thor) {
        /* thor/thor.cxx:398-411 */
        if (m->fec_k == 15) {
            if (fdsp_viterbi_init(&rx->dec, 15, 044735, 063057) < 0)
                return -1;
            fdsp_viterbi_set_traceback(&rx->dec, 15 * 12);
        } else {
            if (fdsp_viterbi_init(&rx->dec, 7, 0x6d, 0x4f) < 0)
                return -1;
            fdsp_viterbi_set_traceback(&rx->dec, 45);
        }
        if (fdsp_interleave_init(&rx->rxinlv, 4, m->idepth, FDSP_INTERLEAVE_REV) < 0)
            return -1;
    } else {
        /* dominoex/dominoex.cxx:327-335 */
        if (fdsp_viterbi_init(&rx->dec, 7, 0x6d, 0x4f) < 0)
            return -1;
        fdsp_viterbi_set_traceback(&rx->dec, 45);
        if (fdsp_interleave_init(&rx->rxinlv, 4, 4, FDSP_INTERLEAVE_REV) < 0)
            return -1;
    }
    fdsp_viterbi_set_chunksize(&rx->dec, 1);
    rx->datashreg = 1;
    rx->sig = rx->noise = 6;
    return 0;
}

fifk_rx *fifk_rx_create(const fifk_rx_config *cfg)
{
    const fifk_submode *m;
    fifk_rx *rx;
    if (!cfg)
        return NULL;
    m = fifk_submode_find(cfg->submode);
    if (!m)
        return NULL;
    if (!(cfg->carrier_hz > m->bandwidth / 2 &&
          cfg->carrier_hz < m->samplerate / 2.0 - m->bandwidth / 2))
        return NULL;
    rx = (fifk_rx *)calloc(1, sizeof *rx);
    if (!rx)
        return NULL;
    rx->m = m;
    rx->thor = m->family == FIFK_THOR;
    rx->frequency = cfg->carrier_hz;
    rx->dominoex_fec = !rx->thor && cfg->dominoex_fec;
    rx->squelch = cfg->squelch > 0 ? cfg->squelch : 0.0;
    /* reset_filters: slowcpu -> 3 paths, 4 extra tones (thor/thor.cxx:122-128) */
    if (cfg->fast_paths) {
        rx->paths = IFK_FASTPATHS;
        rx->extones = IFK_NUMTONES / 2;
    } else {
        rx->paths = IFK_SLOWPATHS;
        rx->extones = 4;
    }
    if (rx_build(rx) < 0) {
        fifk_rx_destroy(rx);
        return NULL;
    }
    return rx;
}

void fifk_rx_destroy(fifk_rx *rx)
{
    if (!rx)
        return;
    rx_free_parts(rx);
    free(rx);
}

void fifk_rx_reset(fifk_rx *rx)
{
    fifk_rx keep;
    if (!rx)
        return;
    keep = *rx;
    rx_free_parts(rx);
    memset(rx, 0, sizeof *rx);
    rx->m = keep.m;
    rx->thor = keep.thor;
    rx->frequency = keep.frequency;
    rx->dominoex_fec = keep.dominoex_fec;
    rx->squelch = keep.squelch;
    rx->paths = keep.paths;
    rx->extones = keep.extones;
    if (rx_build(rx) < 0)
        rx_free_parts(rx);   /* only on out of memory; process() must not be called */
}

double fifk_rx_snr_db(const fifk_rx *rx)
{
    return rx ? rx->s2n : 0.0;
}

double fifk_rx_metric(const fifk_rx *rx)
{
    return rx ? rx->metric : 0.0;
}

int fifk_rx_quality(const fifk_rx *rx)
{
    return rx ? rx->fec_confidence : 0;
}

double fifk_rx_offset_hz(const fifk_rx *rx)
{
    return rx && rx->offset_valid ? rx->offset : 0.0;
}

size_t fifk_rx_dropped(const fifk_rx *rx)
{
    return rx ? rx->dropped : 0;
}
