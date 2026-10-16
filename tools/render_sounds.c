/* SPDX-License-Identifier: GPL-3.0-only
 * Every factory sound in a small piece (about 8 s), rendered by the firmware's own engines (tests/hostsim.c), for the
 * website's Sounds section (web/site/make_pages.py): OUT/E_K.wav (engine E, preset K) and OUT/sounds.json (the
 * engines in the order PRESETS shows them, each sound's name and category). The piece follows the category: chords
 * for a pad, a line for a lead, a bass line, arpeggios for a pluck, chords and a tune for keys, a groove for a kit, a
 * bed for a texture (AMBI, NOISE), a few notes for another effect; compose() below.
 *   cc -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/render_sounds tools/render_sounds.c -lm
 *   build/host/render_sounds OUT          (tools/render_sounds.sh: the clips as MP3) */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include "category.c"                               /* preset_cat */
static uint32_t preset_orig(const engine_t *e, uint32_t k)   /* as ui.c: a retired SAMPLE set is an alias */
{
    return e->presets == SMP_PRESET_TABLE && k < SMP_NSETS ? SMP_SET_ORIG[k] : k;
}

static FILE *w;
static uint32_t frames;
static void run_ms(uint32_t ms)
{
    int32_t o[2 * CTL];
    uint32_t n = ms * (FS / 1000u) / CTL, i;
    while (n--) {
        mix_block(o, CTL);
        if (!w)
            continue;
        for (i = 0; i < CTL; i++)
            wav_put(w, o[2u * i], o[2u * i + 1u]);
        frames += CTL;
    }
}
/* A small piece per sound (80 BPM, a 16th 187.5 ms): Dmaj7 Bm9 Gmaj7 Aadd9, two beats each, then the tails. The
 * sound plays what its category is for; a soft ANALOG pad (track 2) accompanies the ones that are not chords. */
#define SIXT 1875u                                  /* a 16th, in 0.1 ms */
#define LEN 32u                                     /* 16ths: four chords of eight */
typedef struct { uint16_t t; uint8_t trk, note, on; } ev_t;
static ev_t ev[512];
static uint32_t nev;
static void add(uint32_t t, uint32_t tr, uint32_t note, uint32_t dur)
{
    if (nev + 2u > NELEM(ev))
        return;
    ev[nev++] = (ev_t){(uint16_t)(t * 2u + 1u), (uint8_t)tr, (uint8_t)note, 1};   /* (2t + 1: an on after an off at t) */
    ev[nev++] = (ev_t){(uint16_t)((t + dur) * 2u), (uint8_t)tr, (uint8_t)note, 0};
}
static int ev_cmp(const void *a, const void *b) { return (int)((const ev_t *)a)->t - (int)((const ev_t *)b)->t; }
static const uint8_t CH[4][4] = {{50, 57, 61, 64}, {47, 54, 57, 61}, {43, 50, 54, 59}, {45, 52, 57, 59}};
static const uint8_t ROOT[4] = {38, 35, 31, 33};
static const uint8_t MEL[8] = {78, 76, 73, 71, 73, 69, 66, 69};
static void pad(uint32_t tr, int up)                /* the four chords, held */
{
    uint32_t c, i;
    for (c = 0; c < 4u; c++)
        for (i = 0; i < 4u; i++)
            add(c * 8u, tr, (uint32_t)(CH[c][i] + up), 8u);
}
static void compose(char cat, int texture)
{
    uint32_t c, i;
    nev = 0;
    switch (cat) {
    case 'P':
        pad(0, 0);
        break;
    case 'L':                                       /* a slow line over the pad */
        for (i = 0; i < 8u; i++)
            add(i * 4u, 0, MEL[i] - 12u, i % 2u ? 4u : 3u);
        pad(1, -12);
        break;
    case 'U':                                       /* the chords as arpeggios */
        for (c = 0; c < 4u; c++)
            for (i = 0; i < 8u; i++) {
                static const uint8_t ORD[8] = {0, 1, 2, 3, 2, 1, 3, 2};
                add(c * 8u + i, 0, CH[c][ORD[i]] + 12u, 1u);
            }
        pad(1, -12);
        break;
    case 'B':                                       /* a bass line on the roots */
        for (c = 0; c < 4u; c++) {
            add(c * 8u, 0, ROOT[c], 2u);
            add(c * 8u + 3u, 0, ROOT[c], 1u);
            add(c * 8u + 4u, 0, ROOT[c] + 12u, 1u);
            add(c * 8u + 6u, 0, ROOT[c] + 7u, 2u);
        }
        pad(1, 0);
        break;
    case 'D': {                                     /* two bars of a groove (kick, snare, hats, an open hat) */
        static const uint8_t K[] = {0, 6, 10, 16, 22, 26}, S[] = {8, 24};
        for (i = 0; i < NELEM(K); i++) add(K[i], 0, 36, 1u);
        for (i = 0; i < NELEM(S); i++) add(S[i], 0, 38, 1u);
        for (i = 0; i < LEN; i += 2u) add(i, 0, i == 30u ? 46u : 42u, 1u);
        add(14, 0, 37, 1u);
        pad(1, 0);
        break;
    }
    case 'F':
        if (texture) {                              /* a bed under the chords */
            add(0, 0, 50, LEN);
        } else {                                    /* now and then, on the chords' notes */
            for (c = 0; c < 4u; c++) {
                add(c * 8u, 0, CH[c][3] + 12u, 3u);
                add(c * 8u + 5u, 0, CH[c][1] + 12u, 2u);
            }
        }
        pad(1, 0);
        break;
    default:                                        /* keys (and the rest): chords under a tune */
        for (c = 0; c < 4u; c++)
            for (i = 0; i < 3u; i++)
                add(c * 8u, 0, CH[c][i + 1u], 7u);
        for (i = 0; i < 8u; i++)
            add(i * 4u, 0, MEL[i], 3u);
    }
    qsort(ev, nev, sizeof ev[0], ev_cmp);
}
static void play(void)                              /* the events, then the tails */
{
    uint32_t i, now = 0;                            /* in 0.1 ms */
    for (i = 0; i < nev; i++) {
        uint32_t at = ev[i].t / 2u * SIXT;
        if (at > now) {
            run_ms((at - now) / 10u);
            now = at;
        }
        if (ev[i].on)
            input_on(&trk[ev[i].trk], ev[i].note, ev[i].trk ? 80 : 96);
        else
            input_off(&trk[ev[i].trk], ev[i].note);
    }
    run_ms(2400);
}

int main(int argc, char **argv)
{
    static const char L[] = "?BLPUKDFO";
    const char *dir = argc > 1 ? argv[1] : ".";
    char p[512];
    uint32_t n, e, k;
    FILE *js;
    snprintf(p, sizeof p, "%s/sounds.json", dir);
    js = fopen(p, "w");
    if (!js)
        return 1;
    fprintf(js, "[");
    for (n = 0; n < NENG_SHOWN; n++) {
        const engine_t *en;
        int first = 1;
        e = eng_vis(n);
        if (!eng_ok(e))
            continue;
        en = ENGINES[e];
        fprintf(js, "%s\n {\"engine\": %u, \"name\": \"%s\", \"sounds\": [", n ? "," : "", e, en->name);
        for (k = 0; k < en->npresets; k++) {
            char cat;
            if (preset_orig(en, k) != k || (en->presets == SMP_PRESET_TABLE && k >= SMP_NPRESETS))   /* an alias, PERC */
                continue;
            cat = L[preset_cat(e, k) % (sizeof L - 1u)];
            fprintf(js, "%s{\"k\": %u, \"name\": \"%s\", \"cat\": \"%c\"}", first ? "" : ", ", k, en->presets[k].name, cat);
            first = 0;
            host_tracks_init();
            song.g[G_RTYPE] = 2; song.g[G_RSIZE] = 95; song.g[G_RDAMP] = 70;   /* HALL, long and dark */
            song.g[G_DTIME] = 1; song.g[G_DFDBK] = 45; song.g[G_BPM] = 80;     /* a delay of 1/8 */
            host_preset(&trk[0], e, k);
            if (en->presets[k].mono)
                trk[0].p[P_VOICE] = V_LEGATO;
            host_preset(&trk[1], 0, 1);             /* the accompaniment: ANALOG's SOFT PAD, under it */
            trk[1].p[P_LEVEL] = 82;
            compose(cat, e == ENGI_AMBI || e == ENGI_NOISE);
            run_ms(60);
            snprintf(p, sizeof p, "%s/%u_%u.wav", dir, e, k);
            w = fopen(p, "wb");
            frames = 0;
            wav_hdr(w, 0);
            play();
            fseek(w, 0, SEEK_SET);
            wav_hdr(w, frames);
            fclose(w);
            w = 0;
        }
        fprintf(js, "]}");
    }
    fprintf(js, "\n]\n");
    fclose(js);
    return 0;
}
