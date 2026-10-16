/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* A song played block by block and hashed (tests/ui_test.c test_song_lanes): what every track's sequencer does (its step,
 * its sounding notes, LEN / DIV, the automated REV) and the song's row and repeats, every audio block, until the song
 * stops. Rows of one slot each, as every song before lanes (1.2) had: the same source compiles against the firmware of
 * before (src/ of 1.2 before lanes, chain_row_t {slot, repeat}), which gave SONG_PROBE_GOLDEN. Included after the UI
 * sources and ui_test.c's helpers (ui_power_on, project_save) */
#ifdef CHAIN_SILENT
#define PROBE_ROW(s, r) chain_row_of(s, r)
#else
#define PROBE_ROW(s, r) ((chain_row_t){s, r})
#endif
#define SONG_PROBE_GOLDEN 0x676A420Du              /* the firmware before lanes (bddbbd8), 11026 blocks */
#define SONG_PROBE_BLOCKS 11026u
static uint32_t song_probe_mix(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
static uint32_t song_probe(uint32_t *blocks)
{
    static const int16_t LEN[2][NTRK] = {{16, 12, 7, 5}, {8, 16, 10, 3}}, DIV[2][NTRK] = {{2, 1, 2, 3}, {1, 2, 0, 2}};
    uint32_t s, k, i, h = 2166136261u, n = 0;
    ui_power_on();
    trk[2].p[P_REV] = 20;                              /* (track 3's send as the golden was made: LOFI PULSE LD's; BELL's differs) */
    for (s = 0; s < 2u; s++) {
        for (k = 0; k < NTRK; k++) {
            track_t *t = &trk[k];
            track_defaults_steps(t);
            motion_clear(t);
            for (i = 0; i < NSTEP; i += 1u + (k + s) % 3u) {
                t->step[i].note[0] = (uint8_t)(36u + 12u * s + 5u * k + i % 7u);
                t->step[i].n = 1;
                t->step[i].time = ST_NOTE;
                t->step[i].vel = (uint8_t)(70u + i);
            }
            t->step[1].time = ST_TIE;
            t->p[P_SLEN] = LEN[s][k];
            t->p[P_SDIV] = DIV[s][k];
            t->p[P_SGATE] = (int16_t)(40 + 20 * (int16_t)k);
            motion_set_enabled(t, 1);
            motion_set_event(t, (uint32_t)(2u + s), P_REV, (int16_t)(20 + 30 * (int16_t)s + (int16_t)k));
        }
        project_save(s);
    }
    chain_config.count = 3;
    chain_config.row[0] = PROBE_ROW(0, 2);
    chain_config.row[1] = PROBE_ROW(1, 1);
    chain_config.row[2] = PROBE_ROW(0, 1);
    if (chain_prepare())
        return 0;
    while (n < 40000u && (n < 4u || chain.running)) {
        events_block(32);
        n++;
        h = song_probe_mix(h, (uint32_t)chain.running | (uint32_t)chain.row << 1 | (uint32_t)chain.remaining << 8);
        for (k = 0; k < NTRK; k++) {
            const track_t *t = &trk[k];
            h = song_probe_mix(h, t->seq_idx | (uint32_t)t->seq_n << 8 | (uint32_t)t->seq_notes[0] << 16);
            h = song_probe_mix(h, (uint32_t)t->p[P_SLEN] | (uint32_t)t->p[P_SDIV] << 8 | (uint32_t)(uint16_t)t->p[P_REV] << 16);
        }
    }
    *blocks = n;
    return h;
}
