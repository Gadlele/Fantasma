/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Gadlele */
/* Fantasma 2.0's ambience layer (firmware/src/ambi_layer.c): off at power-on and silent; ON fades it in, OFF out to
 * silence; HOME's AMBI pages edit its values; the project keeps them (the FANTASMA 2 block in track 4's FM6 patch) and a
 * FUN10 without the block (Fantasma 1.x) loads with its defaults.
 *   cc -O1 -w -Ibuild/gen -Ifirmware/src (the HAL paths) -o build/host/ambi_layer_test tests/ambi_layer_test.c -lm */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int32_t peak_ms(uint32_t ms)              /* the mix's peak over ms (MASTER at its top) */
{
    int32_t o[2u * CTL], pk = 0;
    uint32_t b, i, nb = ms * 441u / 10u / CTL;
    for (b = 0; b < nb; b++) {
        memset(o, 0, sizeof o);
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++)
            pk = o[i] > pk ? o[i] : -o[i] > pk ? -o[i] : pk;
    }
    return pk;
}

int main(void)
{
    int bad = 0, ok;
    uint32_t i, pg = NPAGES;
    project_t a, b;
    static project_store_t st;
    int16_t *vp = 0;
    ui_power_on();
    song.master_q12 = 4096;
    ok = !al.v[AL_ON] && al.v[AL_LEVEL] == AL_P[AL_LEVEL].def && !al.amp && peak_ms(300) == 0;
    bad += check("ambi: off at power-on, silent", ok);
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].scope == SC_AMBI && str_eq(PAGES[i].title, "AMBI 2"))
            pg = i;
    ok = pg < NPAGES && PAGES[pg].fam == FAM_TRK && page_desc(&PAGES[pg], 0, &vp) == &AL_P[AL_ON] && vp == &al.v[AL_ON];
    bad += check("ambi: HOME's AMBI 2 page, its first knob AMBI (ON / OFF)", ok);
    al.v[AL_ON] = 1;
    {
        int32_t early = peak_ms(100), full = peak_ms(2000);
        bad += check("ambi: ON fades it in (quiet at first, then sounding)", early < full && full > 200);
        printf("ambi: peak %d in the first 100 ms, %d after\n", (int)early, (int)full);
    }
    for (i = 0; i < AL_NSCENE; i++) {             /* every scene sounds, none clips */
        int32_t pk;
        al.v[AL_SCENE] = (int16_t)i;
        pk = peak_ms(1500);
        ok = pk > 50 && pk < 32000;
        if (!ok)
            printf("ambi: scene %s peak %d\n", N_ALSCENE[i], (int)pk);
        bad += !ok;
    }
    bad += check("ambi: every scene sounds and none clips", 1);
    al.v[AL_ON] = 0;
    peak_ms(1500);
    {
        static int32_t lb[CTL];
        memset(lb, 0, sizeof lb);
        ok = !al.amp && !al_render(lb, CTL);      /* (the reverb's tail rings on: the layer itself is silent) */
    }
    bad += check("ambi: OFF fades it out to silence (the reverb's tail rings on)", ok);
    /* the project keeps the layer */
    al.v[AL_SCENE] = 8; al.v[AL_TONE] = -20; al.v[AL_MOTION] = 30; al.v[AL_INTENS] = -10;
    al.v[AL_ON] = 1; al.v[AL_LEVEL] = 77; al.v[AL_REV] = 101;
    project_capture(&a);
    ok = proj_pack(&st, &a) && st.raw[PROJ_F2_OFF] == 0xF2u && st.raw[PROJ_F2_OFF + 1u] == 0xA2u &&
         st.raw[PROJ_F2_OFF + 3u] == AL_N && proj_import(&b, &st, sizeof st) && !memcmp(b.ambi, al.v, sizeof b.ambi);
    bad += check("ambi: saved with the project (the FANTASMA 2 block) and read back", ok);
    {   /* a FUN10 of Fantasma 1.x: track 4's patch where the block is */
        uint32_t sum;
        memset(st.raw + PROJ_F2_OFF, 0x10, FM6_PACKED);
        sum = proj_hash(st.raw, PROJ_STORE_SIZE - 4u);
        memcpy(st.raw + PROJ_STORE_SIZE - 4u, &sum, 4);
        ok = proj_import(&b, &st, sizeof st) && b.ambi[AL_LEVEL] == AL_P[AL_LEVEL].def && !b.ambi[AL_ON] &&
             !b.ambi[AL_SCENE] && !memcmp(b.fm6[3], FM6_INIT, FM6_PACKED);
        bad += check("ambi: a project without the block (Fantasma 1.x): the layer's defaults, track 4's patch the init one", ok);
    }
    printf(bad ? "ambi: %d FAILED\n" : "ambi: all passed\n", bad);
    return bad != 0;
}
