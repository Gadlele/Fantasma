/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Gadlele */
/* Fantasma 2.0: what drone.c asks of Niagara Drift's drone_fx.c, with its effects left out. In Fantasma the DRONE
 * channel's sound goes on to the channel's own effects (the INSERT, the SLICER, CH DELAY, CH REVERB, the sends:
 * fx.c, chfx.c) like the other three channels; Niagara's delay, drive and reverb need about 247 KB of RAM, which
 * Fantasma has no room for. Included by drone.c where drone_fx.c was (its one change). */
static uint32_t fx_quiet;                        /* (drone.c's rest after a silent stop: nothing to ring out here) */
static void fx_clear(void) {}
static void fx_rest(void) {}
static void fx_run(float *l, float *r, uint32_t n)
{
    (void)l;
    (void)r;
    (void)n;
}
