/*
 * fldigi-ifk-core transmitter: fldigi 4.2.13's thor::tx_process and dominoex::tx_process
 * with the soft start/stop of modem::ModulateXmtr.
 *
 * Copyright (C) 2026 Gerolf Ziegenhain
 * Original C++: thor/thor.cxx, dominoex/dominoex.cxx, trx/modem.cxx (c) 2006-2020 Dave
 * Freese W1HKJ, John Douyere VK2ETA and the fldigi authors.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Settings are fldigi's defaults: softTHOR / softDOMINOEX on, SoftStart 100 ms,
 * THOR_RISETIME 4 ms, DOMINOEX_FEC off, THORsecText / secText empty (so the idle text
 * is "fldigi 4.2.13 "), TxOffset 0, reverse off. The picture/avatar transmitter
 * (thor/thor.cxx:1373-1390, 1401-1589) is left out. fldigi's DominoEX keeps the position
 * in the secondary text in a function static shared by all modems
 * (dominoex/dominoex.cxx:647); here it belongs to one transmitter.
 */
#include "ifk_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FLDIGI_IDLE_TEXT "fldigi 4.2.13 "   /* "fldigi " PACKAGE_VERSION " " */

static int ramp_len(const fifk_tx *tx)
{
    /* modem::ModulateXmtr, trx/modem.cxx:611 */
    return ((tx->m->samplerate * IFK_SOFTSTART_MS / 1000) / tx->m->symlen + 1) * tx->m->symlen;
}

static void modulate(fifk_tx *tx, int len)
{
    /* modem::ModulateXmtr, trx/modem.cxx:600-637 (soft start/stop only; the TX level
     * and the sound card are the caller's) */
    int num = ramp_len(tx), i;
    double *buffer = tx->out + tx->n;   /* the samples sendtone just wrote */
    if (tx->sig_start && tx->sig_smpl < num) {
        for (i = 0; i < len; i++) {
            double coeff = (0.5 * (1.0 - cos(M_PI * tx->sig_smpl / num)));
            buffer[i] *= coeff;
            ++tx->sig_smpl;
            if (tx->sig_smpl >= num) {
                tx->sig_smpl = 0;
                tx->sig_start = 0;
                break;
            }
        }
    }
    if (tx->sig_stop && tx->sig_smpl < num) {
        for (i = 0; i < len; i++) {
            double coeff = 1.0 - (0.5 * (1.0 - cos(M_PI * tx->sig_smpl / num)));
            buffer[i] *= coeff;
            ++tx->sig_smpl;
            if (tx->sig_smpl >= num) {
                tx->sig_smpl = 0;
                tx->sig_stop = 0;
                break;
            }
        }
    }
    tx->n += (size_t)len;
}

static void sendtone(fifk_tx *tx, int tone, int duration)
{
    /* thor::sendtone (thor/thor.cxx:1206-1231), dominoex::sendtone
     * (dominoex/dominoex.cxx:655-668); THOR shapes the edges with a raised cosine */
    const fifk_submode *m = tx->m;
    int symlen = m->symlen, j, i, smpl_nbr;
    double coeff = 1.0;
    double f = (tone + 0.5) * m->tonespacing + tx->frequency - m->bandwidth / 2.0;
    double phaseincr = IFK_TWOPI * f / m->samplerate;
    double rise = tx->thor ? tx->risetime_ms : 0.0;
    int num = (int)round(m->samplerate * rise / 1000.0);
    int num2 = duration * symlen - num;
    double *outbuf = tx->out + tx->n;   /* rendered in place, then ramped by modulate() */

    for (j = 0; j < duration; j++) {
        for (i = 0; i < symlen; i++) {
            smpl_nbr = i + j * symlen;
            if (rise != 0 && ((smpl_nbr < num) || (smpl_nbr > num2))) {
                coeff = (smpl_nbr < num) ?
                        0.5 * (1.0 - cos(M_PI * smpl_nbr / num)) :
                        (0.5 * (1.0 - cos(M_PI * (duration * symlen - smpl_nbr) / num)));
            } else {
                coeff = 1.0;
            }
            outbuf[i] = cos(tx->txphase) * coeff;
            tx->txphase -= phaseincr;
            if (tx->txphase < 0)
                tx->txphase += IFK_TWOPI;
        }
        modulate(tx, symlen);
        outbuf = tx->out + tx->n;
    }
}

static void sendsymbol(fifk_tx *tx, int sym)
{
    /* thor::sendsymbol (thor/thor.cxx:1233-1243), dominoex::sendsymbol
     * (dominoex/dominoex.cxx:670-681) */
    int tone = (tx->txprevtone + 2 + sym) % IFK_NUMTONES;
    tx->txprevtone = tone;
    sendtone(tx, tone, 1);
}

static int get_secondary_char(fifk_tx *tx)
{
    /* thor::get_secondary_char (thor/thor.cxx:1197-1204), dominoex::get_secondary_char
     * (dominoex/dominoex.cxx:645-653) */
    size_t len = strlen(tx->sectext);
    char chr;
    if (tx->cptr >= len)
        tx->cptr = 0;
    chr = tx->sectext[tx->cptr++];
    return chr;
}

static void push_bits(fifk_tx *tx, int data, int send)
{
    /* the two code bits of one encoder step into the 4-bit symbol shift register
     * (thor/thor.cxx:1255-1264, 1285-1293; dominoex/dominoex.cxx:894-903, 927-943) */
    int i;
    for (i = 0; i < 2; i++) {
        tx->bitshreg = (tx->bitshreg << 1) | (unsigned int)((data >> i) & 1);
        tx->bitstate++;
        if (tx->bitstate == 4) {
            fdsp_interleave_bits(&tx->txinlv, &tx->bitshreg);
            if (send)
                sendsymbol(tx, (int)tx->bitshreg);
            tx->bitstate = 0;
            tx->bitshreg = 0;
        }
    }
}

static void send_code(fifk_tx *tx, const char *code)
{
    while (*code)
        push_bits(tx, fdsp_encoder_encode(&tx->enc, *code++ - '0'), 1);
}

static void send_mupsk_ex(fifk_tx *tx, unsigned char c, int secondary)
{
    /* dominoex::sendMuPskEX, dominoex/dominoex.cxx:909-945 */
    if (secondary == 1) {
        c = fifk_mupsk_sec2pri(c);
    } else {
        if (c == 10)
            return;
        if ((c >= 1 && c <= 7) || (c >= 9 && c <= 12) || (c >= 14 && c <= 31) ||
            (c >= 127 && c <= 159))
            c = '_';
    }
    send_code(tx, fdsp_mfsk_varienc(c));
}

static void sendchar(fifk_tx *tx, unsigned char c, int secondary)
{
    if (tx->thor) {
        /* thor::sendchar, thor/thor.cxx:1247-1268 */
        send_code(tx, fifk_thorvarienc(c, secondary));
    } else if (tx->dominoex_fec) {
        /* dominoex::sendchar, dominoex/dominoex.cxx:683-686 */
        send_mupsk_ex(tx, c, secondary);
    } else {
        /* dominoex::sendchar, dominoex/dominoex.cxx:687-697 */
        const unsigned char *code = fifk_dominoex_varienc(c, secondary);
        int sym;
        sendsymbol(tx, code[0]);
        for (sym = 1; sym < 3; sym++) {
            if (code[sym] & 0x8)
                sendsymbol(tx, code[sym]);
            else
                break;
        }
    }
}

static void sendidle(fifk_tx *tx)
{
    /* thor::sendidle (thor/thor.cxx:1270-1273): primary NUL; dominoex::sendidle
     * (dominoex/dominoex.cxx:702-705): secondary NUL */
    sendchar(tx, 0, tx->thor ? 0 : 1);
}

static void clearbits(fifk_tx *tx)
{
    /* thor::Clearbits (thor/thor.cxx:1281-1295), dominoex::MuPskClearbits
     * (dominoex/dominoex.cxx:890-905): fill the interleaver, nothing is sent */
    int data = fdsp_encoder_encode(&tx->enc, 0), k;
    int n = tx->thor ? 1400 : 100;
    for (k = 0; k < n; k++)
        push_bits(tx, data, 0);
}

static int ready(fifk_tx *tx, double *out, size_t out_max)
{
    if (!tx || !out || out_max < fifk_tx_bound(tx))
        return 0;
    tx->out = out;
    tx->n = 0;
    tx->max = out_max;
    return 1;
}

size_t fifk_tx_bound(const fifk_tx *tx)
{
    /* worst case of one call: 16 preamble symbols, then at most 7 symbols per character
     * (12-bit code, rate 1/2, 4 bits per symbol, plus a partly filled symbol) for the idle
     * character, the 3 framing characters and the flush, plus the soft-stop tone */
    const fifk_submode *m;
    if (!tx)
        return 0;
    m = tx->m;
    return (size_t)m->symlen * (size_t)(16 + 7 * (1 + 3 + m->flushlength + 1)) +
           (size_t)ramp_len(tx);
}

size_t fifk_tx_begin(fifk_tx *tx, double *out, size_t out_max)
{
    int j;
    if (!ready(tx, out, out_max) || tx->state != 0)
        return 0;
    if (tx->thor) {
        /* TX_STATE_PREAMBLE, thor/thor.cxx:1320-1328 */
        clearbits(tx);
        if (tx->soft)
            tx->sig_start = 1;
        for (j = 0; j < 16; j++)
            sendsymbol(tx, 0);
        sendidle(tx);
    } else {
        /* TX_STATE_PREAMBLE, dominoex/dominoex.cxx:732-738 */
        if (tx->dominoex_fec)
            clearbits(tx);
        if (tx->soft)
            tx->sig_start = 1;
        sendidle(tx);
    }
    /* TX_STATE_START, thor/thor.cxx:1329-1336, dominoex/dominoex.cxx:739-746 */
    sendchar(tx, '\r', 0);
    if (!tx->m->micro) {
        sendchar(tx, 2, 0);
        sendchar(tx, '\r', 0);
    }
    tx->state = 1;
    return tx->n;
}

size_t fifk_tx_put(fifk_tx *tx, unsigned char c, double *out, size_t out_max)
{
    /* TX_STATE_DATA with a character, thor/thor.cxx:1346-1352,
     * dominoex/dominoex.cxx:748-754 */
    if (!ready(tx, out, out_max) || tx->state != 1)
        return 0;
    sendchar(tx, c, 0);
    return tx->n;
}

size_t fifk_tx_idle(fifk_tx *tx, double *out, size_t out_max)
{
    /* TX_STATE_DATA without data: sendsecondary, thor/thor.cxx:1275-1279,
     * dominoex/dominoex.cxx:707-711 */
    int c;
    if (!ready(tx, out, out_max) || tx->state != 1)
        return 0;
    c = get_secondary_char(tx);
    sendchar(tx, (unsigned char)(c & 0xFF), 1);
    return tx->n;
}

size_t fifk_tx_end(fifk_tx *tx, double *out, size_t out_max)
{
    int i;
    if (!ready(tx, out, out_max) || tx->state != 1)
        return 0;
    /* TX_STATE_END, thor/thor.cxx:1358-1366, dominoex/dominoex.cxx:758-765 */
    if (tx->thor && tx->soft)
        tx->sig_start = 0;
    sendchar(tx, '\r', 0);
    if (!tx->m->micro) {
        sendchar(tx, 4, 0);
        sendchar(tx, '\r', 0);
    }
    /* TX_STATE_FLUSH: thor::flushtx (thor/thor.cxx:1297-1309), dominoex::flushtx
     * (dominoex/dominoex.cxx:713-724) */
    for (i = 0; i < (tx->thor ? tx->m->flushlength : 4); i++)
        sendidle(tx);
    if (tx->soft) {
        tx->sig_stop = 1;
        sendtone(tx, 0, (tx->m->samplerate * IFK_SOFTSTART_MS / 1000) / tx->m->symlen + 1);
    }
    tx->bitstate = 0;
    tx->state = 2;
    return tx->n;
}

fifk_tx *fifk_tx_create(const fifk_tx_config *cfg)
{
    /* thor::thor + tx_init (thor/thor.cxx:217-426, 67-79), dominoex::dominoex + tx_init
     * (dominoex/dominoex.cxx:217-337, 56-69) */
    const fifk_submode *m;
    fifk_tx *tx;
    if (!cfg)
        return NULL;
    m = fifk_submode_find(cfg->submode);
    if (!m)
        return NULL;
    if (!(cfg->carrier_hz > m->bandwidth / 2 &&
          cfg->carrier_hz < m->samplerate / 2.0 - m->bandwidth / 2))
        return NULL;
    tx = (fifk_tx *)calloc(1, sizeof *tx);
    if (!tx)
        return NULL;
    tx->m = m;
    tx->thor = m->family == FIFK_THOR;
    tx->frequency = cfg->carrier_hz;
    tx->dominoex_fec = !tx->thor && cfg->dominoex_fec;
    tx->soft = !cfg->no_soft_edges;
    tx->risetime_ms = cfg->risetime_ms < 0 ? THOR_RISETIME_MS : cfg->risetime_ms;
    if (cfg->secondary_text && cfg->secondary_text[0]) {
        strncpy(tx->sectext, cfg->secondary_text, sizeof tx->sectext - 1);
    } else {
        strcpy(tx->sectext, FLDIGI_IDLE_TEXT);
    }
    if (tx->thor) {
        int ok = m->fec_k == 15 ? fdsp_encoder_init(&tx->enc, 15, 044735, 063057)
                                : fdsp_encoder_init(&tx->enc, 7, 0x6d, 0x4f);
        if (ok < 0 || fdsp_interleave_init(&tx->txinlv, 4, m->idepth, FDSP_INTERLEAVE_FWD) < 0) {
            fifk_tx_destroy(tx);
            return NULL;
        }
    } else {
        if (fdsp_encoder_init(&tx->enc, 7, 0x6d, 0x4f) < 0 ||
            fdsp_interleave_init(&tx->txinlv, 4, 4, FDSP_INTERLEAVE_FWD) < 0) {
            fifk_tx_destroy(tx);
            return NULL;
        }
    }
    return tx;
}

void fifk_tx_destroy(fifk_tx *tx)
{
    if (!tx)
        return;
    fdsp_encoder_free(&tx->enc);
    fdsp_interleave_free(&tx->txinlv);
    free(tx);
}
