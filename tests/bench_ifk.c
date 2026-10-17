/*
 * fldigi-ifk-core CPU benchmark: receiver and transmitter speed per submode, in multiples
 * of real time on one core (fldigi's default 3 paths).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define _POSIX_C_SOURCE 200809L
#include "test_util.h"

#include <time.h>

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

int main(void)
{
    int i;
    printf("%-14s %8s %10s %10s %8s\n", "submode", "rate", "RX x real", "TX x real", "pipe MiB");
    for (i = 0; i < fifk_submode_count(); i++) {
        const fifk_submode *m = fifk_submode_at(i);
        fifk_rx_config c;
        fifk_rx *rx;
        size_t n, np, k, len = (size_t)m->samplerate * 20;
        double *x, *y, t0, t_tx, t_rx;
        unsigned char *out;
        t0 = now();
        x = render_over(m->name, 0, "CQ CQ DE DG6FL DG6FL K", 0, 0, 1500.0, NULL, &n);
        t_tx = now() - t0;
        y = (double *)calloc(len, sizeof(double));
        for (k = 0; k < len; k++)
            y[k] = 0.25 * x[k % n];
        memset(&c, 0, sizeof c);
        c.submode = m->name;
        c.carrier_hz = 1500.0;
        rx = fifk_rx_create(&c);
        t0 = now();
        out = decode(rx, y, len, 512, &np, NULL, NULL);
        t_rx = now() - t0;
        printf("%-14s %8d %10.0f %10.0f %8.1f\n", m->name, m->samplerate,
               (double)len / m->samplerate / t_rx, (double)n / m->samplerate / t_tx,
               (2.0 * m->symlen * 3 * (18 + 8) * m->doublespaced * 16) / 1048576.0);
        free(out);
        free(x);
        free(y);
        fifk_rx_destroy(rx);
    }
    return 0;
}
