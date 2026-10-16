/* SPDX-License-Identifier: GPL-3.0-only
 * Every factory sound, a few seconds of each, rendered by the firmware's own engines (tests/hostsim.c), for the
 * website's Sounds section (web/site/make_pages.py): OUT/E_K.wav (engine E, preset K) and OUT/sounds.json (the
 * engines in the order PRESETS shows them, each sound's name and category). What a clip plays follows the category:
 * a chord for a pad, a line for a lead, a bass line, an arpeggio for a pluck, chords and a tune for keys, a beat for
 * a kit, one long note for an effect.
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
static void on(track_t *t, uint8_t n) { input_on(t, n, 96); }
static void off(track_t *t, uint8_t n) { input_off(t, n); }
static void notes(track_t *t, const uint8_t *n, uint32_t k, uint32_t ms, uint32_t gap)
{
    uint32_t i;
    for (i = 0; i < k; i++) {
        on(t, n[i]);
        run_ms(ms);
        off(t, n[i]);
        run_ms(gap);
    }
}
static void phrase(track_t *t, char cat)
{
    static const uint8_t CH[] = {48, 55, 60, 64}, LEAD[] = {67, 69, 72, 74, 72, 67}, BASS[] = {36, 36, 43, 39, 36, 41},
                         ARP[] = {60, 64, 67, 71, 72, 71, 67, 64}, KEYS[] = {72, 71, 67, 69}, BEAT[] = {36, 42, 38, 42, 36, 36, 38, 42};
    uint32_t i;
    switch (cat) {
    case 'P':                                       /* a pad: one chord, held */
        for (i = 0; i < 4u; i++) on(t, CH[i]);
        run_ms(2600);
        for (i = 0; i < 4u; i++) off(t, CH[i]);
        run_ms(1600);
        break;
    case 'L': notes(t, LEAD, 6, 330, 40); run_ms(1200); break;
    case 'B': notes(t, BASS, 6, 230, 70); run_ms(700); break;
    case 'U': notes(t, ARP, 8, 150, 60); run_ms(1400); break;
    case 'K':                                       /* keys: a chord under a short tune */
        for (i = 0; i < 3u; i++) on(t, CH[i]);
        notes(t, KEYS, 4, 380, 40);
        for (i = 0; i < 3u; i++) off(t, CH[i]);
        run_ms(1200);
        break;
    case 'D': notes(t, BEAT, 8, 120, 130); run_ms(800); break;
    default:                                        /* an effect or a texture: one long note */
        on(t, 48);
        run_ms(3000);
        off(t, 48);
        run_ms(1200);
    }
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
            song.g[G_RTYPE] = 2;                    /* HALL, as a fresh start would have it nearly */
            host_preset(&trk[0], e, k);
            if (en->presets[k].mono)
                trk[0].p[P_VOICE] = V_LEGATO;
            run_ms(60);
            snprintf(p, sizeof p, "%s/%u_%u.wav", dir, e, k);
            w = fopen(p, "wb");
            frames = 0;
            wav_hdr(w, 0);
            phrase(&trk[0], cat);
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
