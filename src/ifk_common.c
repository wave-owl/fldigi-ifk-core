/*
 * fldigi-ifk-core: version, submode table and the THOR / DominoEX character codes of
 * fldigi 4.2.13.
 *
 * Copyright (C) 2026 Gerolf Ziegenhain
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ifk_internal.h"

#include <ctype.h>
#include <string.h>

const char *fifk_version(void)
{
    return "fldigi-ifk-core " FIFK_VERSION_STRING " contract=ifk-c1 abi=1 tx=1";
}

/* Submode parameters: THOR thor/thor.cxx:217-349 (defaults isize 4, idepth 10,
 * flushlength 4 at :223-225; K=15 for THOR25/25x4/50x1/50x2/100 at :398-407), DominoEX
 * dominoex/dominoex.cxx:217-278 (MultiPSK FEC 4 x 4 x 4 interleaver, K=7 at :327-332).
 * Order and short names as globals/globals.cxx:83-91, 200-216. */
#define TS(sr, ds, sl) ((double)(sr) * (ds) / (sl))
#define SM(n, f, fam, sr, sl, ds, k, d, fl, mi) \
    {n, f, fam, sr, sl, ds, k, d, fl, mi, TS(sr, ds, sl), 18.0 * TS(sr, ds, sl), \
     (double)(sr) / (sl)}
static const fifk_submode submodes[] = {
    SM("THORMICRO", "THORM", FIFK_THOR, 8000, 4000, 1, 7, 4, 4, 1),
    SM("THOR4", "THOR4", FIFK_THOR, 8000, 2048, 2, 7, 10, 4, 0),
    SM("THOR5", "THOR5", FIFK_THOR, 11025, 2048, 2, 7, 10, 4, 0),
    SM("THOR8", "THOR8", FIFK_THOR, 8000, 1024, 2, 7, 10, 4, 0),
    SM("THOR11", "THOR11", FIFK_THOR, 11025, 1024, 1, 7, 10, 8, 0),
    SM("THOR16", "THOR16", FIFK_THOR, 8000, 512, 1, 7, 10, 8, 0),
    SM("THOR22", "THOR22", FIFK_THOR, 11025, 512, 1, 7, 10, 16, 0),
    SM("THOR25", "THOR25", FIFK_THOR, 8000, 320, 1, 15, 25, 20, 0),
    SM("THOR32", "THOR32", FIFK_THOR, 8000, 256, 1, 7, 10, 20, 0),
    SM("THOR44", "THOR44", FIFK_THOR, 11025, 256, 1, 7, 10, 4, 0),
    SM("THOR56", "THOR56", FIFK_THOR, 16000, 290, 1, 7, 10, 20, 0),
    SM("THOR25X4", "THOR25x4", FIFK_THOR, 8000, 320, 4, 15, 50, 40, 0),
    SM("THOR50X1", "THOR50x1", FIFK_THOR, 8000, 160, 1, 15, 50, 40, 0),
    SM("THOR50X2", "THOR50x2", FIFK_THOR, 8000, 160, 2, 15, 50, 40, 0),
    SM("THOR100", "THOR100", FIFK_THOR, 8000, 80, 1, 15, 50, 40, 0),
    SM("DOMINOEXMICRO", "DOMEXM", FIFK_DOMINOEX, 8000, 4000, 1, 7, 4, 4, 1),
    SM("DOMINOEX4", "DOMEX4", FIFK_DOMINOEX, 8000, 2048, 2, 7, 4, 4, 0),
    SM("DOMINOEX5", "DOMEX5", FIFK_DOMINOEX, 11025, 2048, 2, 7, 4, 4, 0),
    SM("DOMINOEX8", "DOMEX8", FIFK_DOMINOEX, 8000, 1024, 2, 7, 4, 4, 0),
    SM("DOMINOEX11", "DOMX11", FIFK_DOMINOEX, 11025, 1024, 1, 7, 4, 4, 0),
    SM("DOMINOEX16", "DOMX16", FIFK_DOMINOEX, 8000, 512, 1, 7, 4, 4, 0),
    SM("DOMINOEX22", "DOMX22", FIFK_DOMINOEX, 11025, 512, 1, 7, 4, 4, 0),
    SM("DOMINOEX44", "DOMX44", FIFK_DOMINOEX, 11025, 256, 2, 7, 4, 4, 0),
    SM("DOMINOEX88", "DOMX88", FIFK_DOMINOEX, 11025, 128, 1, 7, 4, 4, 0),
};
#define NSUB ((int)(sizeof submodes / sizeof submodes[0]))

int fifk_submode_count(void)
{
    return NSUB;
}

const fifk_submode *fifk_submode_at(int index)
{
    return index >= 0 && index < NSUB ? &submodes[index] : NULL;
}

static int same_name(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b))
            return 0;
    return *a == *b;
}

const fifk_submode *fifk_submode_find(const char *name)
{
    int i;
    if (!name)
        return NULL;
    for (i = 0; i < NSUB; i++)
        if (same_name(name, submodes[i].name) || same_name(name, submodes[i].fldigi_name))
            return &submodes[i];
    return NULL;
}

/* ---------------------------------------------------------------------------------- */
/* THOR varicode, thor/thorvaricode.cxx:170-193                                        */
/* ---------------------------------------------------------------------------------- */

const char *fifk_thorvarienc(int c, int sec)
{
    /* thorvarienc, thor/thorvaricode.cxx:170-179 */
    if (sec == 0)
        return fdsp_mfsk_varienc(c);
    else if (c >= ' ' && c <= 'z')
        return fifk_thor_varicode[c - ' '];
    return fdsp_mfsk_varienc(0);
}

int fifk_thorvaridec(unsigned int symbol)
{
    /* thorvaridec, thor/thorvaricode.cxx:181-193 */
    int i;
    if (symbol < 0xB80)
        return fdsp_mfsk_varidec(symbol);
    for (i = 0; i < 91; i++)
        if (symbol == fifk_thor_varidecode[i])
            return (' ' + i + 0x100);
    return -1;
}

/* ---------------------------------------------------------------------------------- */
/* DominoEX varicode, dominoex/dominovar.cxx:366-377                                   */
/* ---------------------------------------------------------------------------------- */

const unsigned char *fifk_dominoex_varienc(unsigned char c, int secondary)
{
    /* dominoex_varienc, dominoex/dominovar.cxx:366-369 */
    return fifk_dominoex_varicode[c + ((secondary) ? 256 : 0)];
}

int fifk_dominoex_varidec(unsigned int symbol)
{
    /* dominoex_varidec, dominoex/dominovar.cxx:371-376 */
    return fifk_dominoex_varidecode[symbol & 0xFFF];
}

/* ---------------------------------------------------------------------------------- */
/* MultiPSK FEC character maps, dominoex/dominoex.cxx:150-191, 787-823                 */
/* ---------------------------------------------------------------------------------- */

/* MuPsk_sec2pri_init's table (dominoex/dominoex.cxx:152-182): groups of a primary
 * character followed by the characters that map to it, -1 ends a group. */
static const int sec2pri_chars[] = {
    'A', 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, -1,
    'B', 0xdf, -1,
    'C', 0xc7, 0xe7, 0xa9, -1,
    'D', 0xd0, 0xb0, -1,
    'E', 0xc6, 0xe6, 0xc8, 0xc9, 0xca, 0xcb, 0xe8, 0xe9, 0xea, 0xeb, -1,
    'F', 0x192, -1,
    'I', 0xcc, 0xcd, 0xce, 0xcf, 0xec, 0xed, 0xee, 0xef, 0xa1, -1,
    'L', 0xa3, -1,
    'N', 0xd1, 0xf1, -1,
    'O', 0xf4, 0xf6, 0xf2, 0xd6, 0xf3, 0xd3, 0xd4, 0xd2, 0xf5, 0xd5, -1,
    'R', 0xae, -1,
    'U', 0xd9, 0xda, 0xdb, 0xdc, 0xf9, 0xfa, 0xfb, 0xfc, -1,
    'X', 0xd7, -1,
    'Y', 0xff, 0xfd, 0xdd, -1,
    '0', 0xd8, -1,
    '1', 0xb9, -1,
    '2', 0xb2, -1,
    '3', 0xb3, -1,
    '?', 0xbf, -1,
    '!', 0xa1, -1,
    '<', 0xab, -1,
    '>', 0xbb, -1,
    '{', '(', -1,
    '}', ')', -1,
    '|', '\\'
};

static int sec2pri_lookup(int c)
{
    /* the std::map built by MuPsk_sec2pri_init (dominoex/dominoex.cxx:184-190); a later
     * entry for the same key replaces an earlier one (0xa1 maps to '!') */
    size_t i, n = sizeof sec2pri_chars / sizeof sec2pri_chars[0];
    int group = sec2pri_chars[0], found = -1;
    for (i = 1; i < n; i++) {
        if (sec2pri_chars[i] != -1) {
            if (sec2pri_chars[i] == c)
                found = group;
        } else {
            group = sec2pri_chars[++i];
        }
    }
    return found;
}

unsigned char fifk_mupsk_sec2pri(int c)
{
    /* MuPskSec2Pri, dominoex/dominoex.cxx:787-806 */
    int p;
    if (c >= 'a' && c <= 'z')
        c -= 32;
    p = sec2pri_lookup(c);
    if (p >= 0)
        c = p;
    if (c >= 'A' && c <= 'Z') c = c - 'A' + 127;
    else if (c >= '0' && c <= '9') c = c - '0' + 14;
    else if (c >= ' ' && c <= '"') c = c - ' ' + 1;
    else if (c == '_') c = 4;
    else if (c >= '$' && c <= '&') c = c - '$' + 5;
    else if (c >= '\'' && c <= '*') c = c - '\'' + 9;
    else if (c >= '+' && c <= '/') c = c - '+' + 24;
    else if (c >= ':' && c <= '<') c = c - ':' + 29;
    else if (c >= '=' && c <= '@') c = c - '=' + 153;
    else if (c >= '[' && c <= ']') c = c - '[' + 157;
    else c = '_';
    return (unsigned char)c;
}

unsigned int fifk_mupsk_prisecchar(unsigned int c)
{
    /* MuPskPriSecChar, dominoex/dominoex.cxx:810-823 */
    if (c >= 127 && c < 153) c += ('A' - 127) + 0x100;
    else if (c >= 14 && c < 24) c += ('0' - 14) + 0x100;
    else if (c >= 1 && c < 4) c += (' ' - 1) + 0x100;
    else if (c == 4) c = '_' + 0x100;
    else if (c >= 5 && c < 8) c += ('$' - 5) + 0x100;
    else if (c >= 9 && c < 13) c += ('\'' - 9) + 0x100;
    else if (c >= 24 && c < 29) c += ('+' - 24) + 0x100;
    else if (c >= 29 && c < 32) c += (':' - 29) + 0x100;
    else if (c >= 153 && c < 157) c += ('=' - 153) + 0x100;
    else if (c >= 157 && c < 160) c += ('[' - 157) + 0x100;
    return c;
}
