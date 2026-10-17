/*
 * fldigi-ifk-core unit tests, independent of the fldigi goldens: API and submode table,
 * transmitter -> receiver loopback for every submode (with and without DominoEX FEC),
 * secondary text, chunk invariance, reset, offset estimate and drift tolerance, buffer
 * limits, noise robustness.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define _POSIX_C_SOURCE 200809L
#include "test_util.h"

#define TEXT "CQ CQ de DG6FL/p <73> 0123456789 ?!"

static fifk_rx *make_rx(const char *submode, double carrier, int fec)
{
    fifk_rx_config c;
    memset(&c, 0, sizeof c);
    c.submode = submode;
    c.carrier_hz = carrier;
    c.dominoex_fec = fec;
    return fifk_rx_create(&c);
}

/* audio at the submode's rate with lead and tail silence */
static double *padded(const double *x, size_t n, int rate, double lead_s, double tail_s,
                      size_t *out_n)
{
    size_t lead = (size_t)(lead_s * rate), tail = (size_t)(tail_s * rate);
    double *y = (double *)calloc(lead + n + tail, sizeof(double));
    size_t i;
    for (i = 0; i < n; i++)
        y[lead + i] = 0.25 * x[i];
    *out_n = lead + n + tail;
    return y;
}

static void test_api(void)
{
    const fifk_submode *m;
    fifk_rx_config rc;
    fifk_tx_config tc;
    int i, thor = 0, dom = 0;
    CHECK(strcmp(fifk_version(), "fldigi-ifk-core " FIFK_VERSION_STRING
                                 " contract=ifk-c1 abi=1 tx=1") == 0, "version %s",
          fifk_version());
    CHECK(fifk_submode_count() == 24, "%d submodes", fifk_submode_count());
    for (i = 0; i < fifk_submode_count(); i++) {
        m = fifk_submode_at(i);
        thor += m->family == FIFK_THOR;
        dom += m->family == FIFK_DOMINOEX;
        CHECK(fabs(m->bandwidth - 18.0 * m->tonespacing) < 1e-9, "%s bandwidth", m->name);
        CHECK(fifk_submode_find(m->name) == m && fifk_submode_find(m->fldigi_name) == m,
              "%s lookup", m->name);
    }
    CHECK(thor == 15 && dom == 9, "families %d %d", thor, dom);
    CHECK(fifk_submode_at(24) == NULL && fifk_submode_at(-1) == NULL, "index range");
    m = fifk_submode_find("thor16");
    CHECK(m && m->samplerate == 8000 && m->symlen == 512 && fabs(m->tonespacing - 15.625) < 1e-12,
          "THOR16 parameters");
    m = fifk_submode_find("DOMX11");
    CHECK(m && m->samplerate == 11025 && m->symlen == 1024 && m->doublespaced == 1,
          "DOMINOEX11 parameters");
    m = fifk_submode_find("THOR100");
    CHECK(m && m->fec_k == 15 && m->idepth == 50 && m->flushlength == 40, "THOR100");
    CHECK(fifk_submode_find("THOR17") == NULL && fifk_submode_find(NULL) == NULL, "unknown");
    memset(&rc, 0, sizeof rc);
    rc.submode = "THOR16";
    rc.carrier_hz = 100.0;     /* signal would extend below 0 Hz */
    CHECK(fifk_rx_create(&rc) == NULL, "carrier too low accepted");
    rc.carrier_hz = 3990.0;
    CHECK(fifk_rx_create(&rc) == NULL, "carrier too high accepted");
    rc.submode = "PSK31";
    rc.carrier_hz = 1500.0;
    CHECK(fifk_rx_create(&rc) == NULL, "PSK31 accepted");
    CHECK(fifk_rx_create(NULL) == NULL && fifk_tx_create(NULL) == NULL, "NULL config");
    memset(&tc, 0, sizeof tc);
    tc.submode = "DOMINOEX88";
    tc.carrier_hz = 1500;
    {
        fifk_tx *tx = fifk_tx_create(&tc);
        double small[10];
        size_t bound = fifk_tx_bound(tx);
        double *buf = (double *)malloc(bound * sizeof(double));
        CHECK(tx != NULL && bound > 0, "tx create");
        CHECK(fifk_tx_put(tx, 'A', buf, bound) == 0, "put before begin");
        CHECK(fifk_tx_begin(tx, small, 10) == 0, "begin with a too small buffer");
        CHECK(fifk_tx_begin(tx, buf, bound) > 0, "begin");
        CHECK(fifk_tx_begin(tx, buf, bound) == 0, "begin twice");
        CHECK(fifk_tx_put(tx, 'A', buf, bound) > 0 && fifk_tx_idle(tx, buf, bound) > 0, "data");
        CHECK(fifk_tx_end(tx, buf, bound) > 0, "end");
        CHECK(fifk_tx_put(tx, 'A', buf, bound) == 0 && fifk_tx_end(tx, buf, bound) == 0,
              "after end");
        free(buf);
        fifk_tx_destroy(tx);
    }
    fifk_rx_destroy(NULL);
    fifk_tx_destroy(NULL);
}

/* AC-825.1 (core side): every submode, TX -> RX, the text comes back. Six secondary
 * characters lead in: fldigi's DominoEX receiver (and this port, bit for bit) needs a few
 * symbols to synchronise and loses the first characters of a burst without them
 * (wave-owl fldigi-core-rx-modes, task 6.3). */
static void test_loopback(void)
{
    int i, fec;
    for (i = 0; i < fifk_submode_count(); i++) {
        const fifk_submode *m = fifk_submode_at(i);
        for (fec = 0; fec <= (m->family == FIFK_DOMINOEX); fec++) {
            size_t n, nx, np;
            double *x = render_over(m->name, fec, TEXT, 12, 0, 1500.0, NULL, &n);
            double *y = padded(x, n, m->samplerate, 1.0, 12.0, &nx);
            fifk_rx *rx = make_rx(m->name, 1500.0, fec);
            unsigned char *out = decode(rx, y, nx, 512, &np, NULL, NULL);
            CHECK(contains(out, np, TEXT), "%s%s loopback: [%s]", m->name, fec ? " FEC" : "",
                  out);
            free(out);
            free(x);
            free(y);
            fifk_rx_destroy(rx);
        }
    }
}

/* AC-822.2: idle overs carry only secondary text; the primary text is the framing. */
static void test_secondary(void)
{
    static const char *subs[] = {"THOR16", "THOR100", "DOMINOEX11", "DOMINOEX88"};
    size_t i;
    for (i = 0; i < sizeof subs / sizeof subs[0]; i++) {
        const fifk_submode *m = fifk_submode_find(subs[i]);
        size_t n, nx, np, ns;
        unsigned char *sec = NULL, *out;
        double *x = render_over(m->name, 0, "", 40, 0, 1500.0, "dg6fl qrv ", &n);
        double *y = padded(x, n, m->samplerate, 1.0, 12.0, &nx);
        fifk_rx *rx = make_rx(m->name, 1500.0, 0);
        out = decode(rx, y, nx, 1000, &np, &sec, &ns);
        CHECK(contains(sec, ns, "dg6fl qrv dg6fl qrv"), "%s secondary [%s]", m->name, sec);
        CHECK(!contains(out, np, "dg6") && !contains(out, np, "qrv") && !contains(out, np, "fl "),
              "%s: secondary text in the primary text", m->name);
        free(out);
        free(sec);
        free(x);
        free(y);
        fifk_rx_destroy(rx);
    }
}

/* The output does not depend on how the input is split into calls; reset restarts. */
static void test_chunks_and_reset(void)
{
    static const char *subs[] = {"THOR22", "DOMINOEX16"};
    size_t i;
    for (i = 0; i < 2; i++) {
        const fifk_submode *m = fifk_submode_find(subs[i]);
        size_t n, nx, n1, n2, n3;
        double *x = render_over(m->name, 0, TEXT, 0, 0, 1500.0, NULL, &n);
        double *y = padded(x, n, m->samplerate, 0.5, 4.0, &nx);
        fifk_rx *rx = make_rx(m->name, 1500.0, 0);
        unsigned char *a = decode(rx, y, nx, 1, &n1, NULL, NULL);
        unsigned char *b, *c;
        fifk_rx_reset(rx);
        b = decode(rx, y, nx, 4099, &n2, NULL, NULL);
        fifk_rx_destroy(rx);
        rx = make_rx(m->name, 1500.0, 0);
        c = decode(rx, y, nx, nx, &n3, NULL, NULL);
        CHECK(n1 == n2 && n2 == n3 && memcmp(a, b, n1) == 0 && memcmp(b, c, n1) == 0,
              "%s: chunking/reset changed the output", m->name);
        free(a);
        free(b);
        free(c);
        free(x);
        free(y);
        fifk_rx_destroy(rx);
    }
}

/* R-822.4: no AFC, an offset estimate; IFK+ decodes a few Hz off frequency. */
static void test_offset_and_drift(void)
{
    static const char *subs[] = {"THOR16", "DOMINOEX11", "THOR8"};
    static const double offs[] = {-3.0, 2.5, 4.0};
    size_t i;
    for (i = 0; i < 3; i++) {
        const fifk_submode *m = fifk_submode_find(subs[i]);
        size_t n, nx, np;
        double *x = render_over(m->name, 0, TEXT, 6, 0, 1500.0 + offs[i], NULL, &n);
        double *y = padded(x, n, m->samplerate, 1.0, 8.0, &nx);
        fifk_rx *rx = make_rx(m->name, 1500.0, 0);
        unsigned char *out, *rest;
        size_t nsig = (size_t)m->samplerate + n, nr;
        double est;
        /* the estimate at the end of the over, then the rest of the decoder delay */
        out = decode(rx, y, nsig, 512, &np, NULL, NULL);
        est = fifk_rx_offset_hz(rx);
        rest = decode(rx, y + nsig, nx - nsig, 512, &nr, NULL, NULL);
        out = (unsigned char *)realloc(out, np + nr + 1);
        memcpy(out + np, rest, nr + 1);
        np += nr;
        free(rest);
        CHECK(contains(out, np, TEXT), "%s at %+.1f Hz: [%s]", m->name, offs[i], out);
        CHECK(fabs(est - offs[i]) < 0.35 * m->tonespacing, "%s offset estimate %.2f for %+.1f Hz",
              m->name, est, offs[i]);
        free(out);
        free(x);
        free(y);
        fifk_rx_destroy(rx);
    }
}

/* Small output buffers drop and count; pure noise and extreme input do not crash. */
static void test_limits_and_noise(void)
{
    const fifk_submode *m = fifk_submode_find("THOR100");
    size_t n, nx, i, total = 0;
    double *x = render_over(m->name, 0, TEXT TEXT, 0, 0, 1500.0, NULL, &n);
    double *y = padded(x, n, m->samplerate, 0.5, 6.0, &nx);
    fifk_rx *rx = make_rx(m->name, 1500.0, 0);
    unsigned char two[2];
    total = fifk_rx_process(rx, y, nx, two, 2, NULL, 0, NULL);
    CHECK(total == 2 && fifk_rx_dropped(rx) > 20, "dropped %zu", fifk_rx_dropped(rx));
    fifk_rx_destroy(rx);
    free(x);
    free(y);
    for (i = 0; i < (size_t)fifk_submode_count(); i++) {
        const fifk_submode *s = fifk_submode_at((int)i);
        size_t k, len = (size_t)s->samplerate * 3;
        double *z = (double *)malloc(len * sizeof(double));
        unsigned char out[4096], sec[4096];
        uint32_t st = 1234u + (uint32_t)i;
        fifk_rx *r = make_rx(s->name, 1500.0, 0);
        for (k = 0; k < len; k++) {
            st = st * 1664525u + 1013904223u;
            z[k] = (k % 5000 < 10) ? 1e6 : ((double)(int32_t)st) / 2147483648.0;
        }
        fifk_rx_process(r, z, len, out, sizeof out, sec, sizeof sec, NULL);
        CHECK(!isnan(fifk_rx_metric(r)), "%s: metric NaN after noise", s->name);
        fifk_rx_destroy(r);
        free(z);
    }
}

int main(void)
{
    test_api();
    test_loopback();
    test_secondary();
    test_chunks_and_reset();
    test_offset_and_drift();
    test_limits_and_noise();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("fldigi-ifk-core: unit tests passed (%s)\n", fifk_version());
    return 0;
}
