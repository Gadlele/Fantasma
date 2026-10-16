/* SPDX-License-Identifier: GPL-3.0-only
 * The website's Sounds section (web/site/make_pages.py): eight short pieces that show what Fantasma does, each played
 * by the firmware's own engines (tests/hostsim.c) on the four tracks, as the FM-1 would play them: OUT/N.wav and
 * OUT/sounds.json (each piece's file, title, text and the sounds it uses). The scores are below, one function each.
 *   cc -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/render_sounds tools/render_sounds.c -lm
 *   build/host/render_sounds OUT          (tools/render_sounds.sh: as MP3) */
#define main hostsim_main
#include "hostsim.c"
#undef main

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

/* ---- a score: notes on the four tracks, in ms (a 16th of the piece's tempo: s16) ---- */
typedef struct { uint32_t t; uint8_t trk, note, vel, on; } ev_t;
static ev_t ev[2048];
static uint32_t nev;
static uint32_t s16;                                /* a 16th, in 0.01 ms */
static uint32_t at(uint32_t bar, uint32_t six) { return (bar * 16u + six) * s16 / 100u; }
static void nt_ms(uint32_t t, uint32_t tr, uint32_t note, uint32_t dur, uint32_t vel)
{
    if (nev + 2u > NELEM(ev) || dur < 2u)
        return;
    ev[nev++] = (ev_t){t * 2u + 1u, (uint8_t)tr, (uint8_t)note, (uint8_t)vel, 1};   /* (an on after an off at t) */
    ev[nev++] = (ev_t){(t + dur) * 2u, (uint8_t)tr, (uint8_t)note, 0, 0};
}
static void nt(uint32_t bar, uint32_t six, uint32_t tr, uint32_t note, uint32_t len, uint32_t vel)   /* len in 16ths */
{
    nt_ms(at(bar, six), tr, note, len * s16 / 100u - 8u, vel);
}
static void line(uint32_t bar, uint32_t tr, const uint8_t (*m)[2], uint32_t n, uint32_t vel)   /* {note, 16ths}, 0 = rest */
{
    uint32_t i, six = 0;
    for (i = 0; i < n; i++) {
        if (m[i][0])
            nt(bar + six / 16u, six % 16u, tr, m[i][0], m[i][1], vel);
        six += m[i][1];
    }
}
static void chord(uint32_t bar, uint32_t six, uint32_t tr, const uint8_t *c, uint32_t n, uint32_t len, uint32_t vel)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        nt(bar, six, tr, c[i], len, vel);
}
static void strum(uint32_t bar, uint32_t six, uint32_t tr, const uint8_t *c, uint32_t n, uint32_t len, uint32_t vel)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        nt_ms(at(bar, six) + i * 28u, tr, c[i], len * s16 / 100u - i * 28u, vel - i * 4u);
}
static uint32_t rnd_s = 12345u;
static uint32_t rnd(uint32_t n) { rnd_s = rnd_s * 1103515245u + 12345u; return (rnd_s >> 16) % n; }

/* the sounds: engine and preset by name */
static int find(uint32_t e, const char *name)
{
    uint32_t k;
    for (k = 0; k < ENGINES[e]->npresets; k++)
        if (!strcmp(ENGINES[e]->presets[k].name, name))
            return (int)k;
    return -1;
}
static const char *part_name[NTRK];
static void use(uint32_t tr, uint32_t e, const char *name, uint32_t level)
{
    int k = find(e, name);
    if (k < 0) {
        fprintf(stderr, "render_sounds: no %s on engine %u\n", name, e);
        exit(1);
    }
    host_preset(&trk[tr], e, (uint32_t)k);
    if (ENGINES[e]->presets[k].mono)
        trk[tr].p[P_VOICE] = V_LEGATO;
    trk[tr].p[P_LEVEL] = (int16_t)level;
    part_name[tr] = name;
}
static void song_fx(uint32_t bpm, uint32_t rsize, uint32_t dfdbk)
{
    song.g[G_BPM] = (int16_t)bpm;
    song.g[G_RTYPE] = 2; song.g[G_RSIZE] = (int16_t)rsize; song.g[G_RDAMP] = 70;   /* HALL */
    song.g[G_DTIME] = 1; song.g[G_DFDBK] = (int16_t)dfdbk;                         /* a delay of 1/8 */
    s16 = 1500000u / bpm;                                                          /* 60000 / bpm / 4, x 100 */
}

/* ---- the eight pieces ---- */
static const uint8_t DMAJ[4][4] = {{50, 57, 61, 64}, {47, 54, 57, 61}, {43, 50, 54, 59}, {45, 52, 57, 59}};

static void p_tape(void)                            /* 1.1's tape sounds */
{
    static const uint8_t BOX[8][4] = {{78, 76, 73, 71}, {73, 69, 66, 69}, {74, 73, 71, 66}, {69, 71, 73, 76},
                                      {78, 76, 73, 71}, {73, 69, 66, 69}, {74, 73, 71, 66}, {69, 73, 76, 78}};
    static const uint8_t LEAD[][2] = {{69, 8}, {66, 8}, {66, 8}, {61, 8}, {62, 8}, {66, 8}, {64, 16}};
    uint32_t b, i;
    song_fx(88, 95, 55);
    use(0, 0, "NOSTALGIA", 96); use(1, ENGI_BELL, "MUSIC BOX", 92); use(2, 0, "HAZY LEAD", 84); use(3, ENGI_AMBI, "VHS HISS", 78);
    for (b = 0; b < 8u; b++) {
        chord(b, 0, 0, DMAJ[b % 4u], 4, 16, 90);
        for (i = 0; i < 4u; i++)
            nt(b, i * 4u, 1, BOX[b][i], 3, 80);
    }
    line(4, 2, LEAD, NELEM(LEAD), 84);
    nt(0, 0, 3, 48, 8 * 16, 100);
}
static void p_drive(void)                           /* a kit, an acid bass, a saw lead, FM pads */
{
    static const uint8_t ROOT[4] = {45, 41, 36, 43}, PAD[4][3] = {{57, 60, 64}, {57, 60, 65}, {55, 60, 64}, {55, 59, 62}};
    static const int8_t ACID[16] = {0, -1, 12, 0, -1, 12, 0, 7, 0, -1, 12, 0, -1, 3, 12, 7};
    static const uint8_t LEAD[][2] = {{76, 8}, {74, 4}, {72, 4}, {69, 12}, {72, 4}, {67, 8}, {72, 4}, {74, 4}, {71, 16}};
    uint32_t b, i;
    song_fx(112, 70, 40);
    use(0, ENGI_DRUM, "DRUM KIT", 104); use(1, 0, "ACID", 98); use(2, 0, "SAW LEAD", 88); use(3, ENGI_FM6, "PAD", 82);
    for (b = 0; b < 8u; b++) {
        for (i = 0; i < 16u; i += 4u) nt(b, i, 0, 36, 1, 110);
        nt(b, 4, 0, b >= 4u ? 39 : 38, 1, 100); nt(b, 12, 0, 38, 1, 100);
        for (i = 2; i < 16u; i += 4u) nt(b, i, 0, (b % 4u == 3u && i == 14u) ? 46 : 42, 1, 76);
        for (i = 0; i < 16u; i++)
            if (ACID[i] >= 0) nt(b, i, 1, (uint32_t)(ROOT[b % 4u] + ACID[i]), 1, i % 4u ? 84 : 104);
        chord(b, 0, 3, PAD[b % 4u], 3, 16, 80);
    }
    line(4, 2, LEAD, NELEM(LEAD), 92);
}
static void p_garden(void)                          /* rain, a soft choir, bells like drops */
{
    static const uint8_t CHOIR[4][4] = {{50, 54, 57, 62}, {50, 55, 59, 62}, {47, 54, 59, 62}, {45, 52, 57, 61}};
    static const uint8_t PENT[6] = {74, 76, 78, 81, 83, 86};
    uint32_t b, i;
    song_fx(70, 115, 60);
    use(0, ENGI_AMBI, "RAIN", 84); use(1, 5, "SOFT CHOIR", 94); use(2, ENGI_BELL, "HALO", 88); use(3, 0, "SUB BASS", 84);
    nt(0, 0, 0, 48, 8 * 16, 100);
    rnd_s = 7u;
    for (b = 0; b < 8u; b++) {
        if (b % 2u == 0u) {
            chord(b, 0, 1, CHOIR[b / 2u], 4, 32, 84);
            nt(b, 0, 3, CHOIR[b / 2u][0] - 12u, 30, 80);
        }
        for (i = 0; i < 3u + b % 2u; i++)
            nt(b, rnd(16), 2, PENT[rnd(6)], 4, 70 + rnd(30));
    }
}
static void p_omni(void)                            /* strummed chords, as OMNI plays them */
{
    static const uint8_t CH[4][5] = {{48, 52, 55, 60, 64}, {45, 48, 52, 57, 60}, {41, 45, 48, 53, 57}, {43, 47, 50, 55, 59}};
    static const uint8_t ROOT[4] = {36, 33, 29, 31};
    static const uint8_t MEL[][2] = {{72, 4}, {74, 4}, {76, 4}, {79, 4}, {76, 4}, {72, 4}, {69, 4}, {72, 4},
                                     {69, 4}, {72, 4}, {77, 4}, {76, 4}, {74, 8}, {71, 4}, {74, 4}};
    uint32_t b, i;
    song_fx(96, 80, 35);
    use(0, 9, "HARP", 100); use(1, 0, "SUB BASS", 92); use(2, ENGI_DRUM, "DRUM KIT", 84); use(3, ENGI_FM6, "BELL", 82);
    for (b = 0; b < 8u; b++) {
        strum(b, 0, 0, CH[b % 4u], 5, 7, 100);
        strum(b, 8, 0, CH[b % 4u], 5, 6, 90);
        nt(b, 0, 1, ROOT[b % 4u], 6, 100); nt(b, 8, 1, ROOT[b % 4u], 4, 92); nt(b, 14, 1, ROOT[b % 4u] + 7u, 2, 84);
        for (i = 0; i < 16u; i += 2u) nt(b, i, 2, 42, 1, i % 4u ? 56 : 70);
        nt(b, 4, 2, 37, 1, 84); nt(b, 12, 2, 37, 1, 84); nt(b, 0, 2, 36, 1, 90);
    }
    line(4, 3, MEL, NELEM(MEL), 84);
}
static void p_keys(void)                            /* FM6's tine piano, an FM bass, the sampled sax */
{
    static const uint8_t EP[4][4] = {{52, 55, 59, 62}, {55, 57, 60, 64}, {53, 57, 60, 64}, {53, 57, 59, 64}};
    static const uint8_t WALK[4][4] = {{36, 40, 43, 45}, {45, 43, 40, 38}, {38, 41, 45, 43}, {43, 41, 40, 38}};
    static const uint8_t SAX[][2] = {{67, 4}, {69, 2}, {71, 2}, {72, 8}, {76, 6}, {74, 2}, {72, 4}, {69, 4},
                                     {74, 4}, {72, 2}, {69, 2}, {65, 8}, {67, 12}, {71, 4}};
    uint32_t b, i;
    song_fx(76, 75, 30);
    use(0, ENGI_FM6, "TINE EP", 96); use(1, ENGI_FM6, "FM BASS", 96); use(2, 4, "SAX", 92); use(3, ENGI_DRUM, "DRUM KIT", 78);
    for (b = 0; b < 8u; b++) {
        chord(b, 0, 0, EP[b % 4u], 4, 6, 88);
        chord(b, 6, 0, EP[b % 4u], 4, 8, 76);
        for (i = 0; i < 4u; i++) nt(b, i * 4u, 1, WALK[b % 4u][i], 3, 96);
        for (i = 0; i < 16u; i += 2u) nt(b, i, 3, 42, 1, 50);
        nt(b, 0, 3, 36, 1, 80); nt(b, 10, 3, 36, 1, 70); nt(b, 4, 3, 37, 1, 70); nt(b, 12, 3, 37, 1, 70);
    }
    line(4, 2, SAX, NELEM(SAX), 96);
}
static void p_chapel(void)                          /* a tonewheel organ, angels, bells */
{
    static const uint8_t ORG[4][4] = {{53, 57, 60, 65}, {50, 53, 57, 62}, {46, 50, 53, 58}, {48, 52, 55, 60}};
    static const uint8_t ANG[4][3] = {{65, 69, 72}, {62, 65, 69}, {62, 65, 70}, {64, 67, 72}};
    static const uint8_t BELLS[][2] = {{77, 8}, {76, 8}, {74, 16}, {74, 8}, {72, 8}, {72, 16}, {77, 8}, {79, 8}, {81, 16}};
    uint32_t b;
    song_fx(64, 120, 30);
    use(0, 7, "CHAPEL PAD", 92); use(1, 5, "ANGELS", 90); use(2, 9, "BELL TREE", 84);
    for (b = 0; b < 8u; b++) {
        chord(b, 0, 0, ORG[b % 4u], 4, 16, 84);
        if (b >= 2u) chord(b, 0, 1, ANG[b % 4u], 3, 16, 80);
    }
    line(2, 2, BELLS, NELEM(BELLS), 80);
}
static void p_frozen(void)                          /* grains, bowed metal, a drone, wind */
{
    static const uint8_t GR[4][3] = {{50, 57, 65}, {46, 53, 62}, {43, 50, 58}, {45, 52, 61}};
    static const uint8_t MET[4] = {74, 72, 69, 73};
    uint32_t b;
    song_fx(60, 127, 45);
    use(0, 8, "FROZEN", 96); use(1, 9, "BOWED METAL", 88); use(2, 0, "DRONE", 84); use(3, ENGI_AMBI, "WIND", 80);
    for (b = 0; b < 8u; b += 2u) {
        chord(b, 0, 0, GR[b / 2u], 3, 32, 86);
        nt(b, 4, 1, MET[b / 2u], 20, 80);
    }
    nt(0, 0, 2, 38, 8 * 16, 90);
    nt(0, 0, 3, 48, 8 * 16, 100);
}
static void p_arcade(void)                          /* TRIO's chip voices over the kit */
{
    static const uint8_t ARP[4][4] = {{64, 67, 71, 76}, {60, 64, 67, 72}, {62, 67, 71, 74}, {62, 66, 69, 74}};
    static const uint8_t ORD[16] = {0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 2, 1, 0, 1, 2, 3}, ROOT[4] = {40, 36, 43, 38};
    static const uint8_t LEAD[][2] = {{83, 4}, {79, 4}, {76, 4}, {79, 4}, {76, 8}, {72, 8}, {74, 4}, {79, 4}, {83, 8}, {81, 12}, {78, 4}};
    uint32_t b, i;
    song_fx(120, 60, 35);
    use(0, 6, "ARP LEAD", 86); use(1, 6, "FAT BASS", 98); use(2, ENGI_DRUM, "DRUM KIT", 100); use(3, 6, "SYNC LEAD", 88);
    for (b = 0; b < 8u; b++) {
        for (i = 0; i < 16u; i++) nt(b, i, 0, ARP[b % 4u][ORD[i]] + (i >= 8u ? 12u : 0u), 1, 80);
        for (i = 0; i < 16u; i += 2u) nt(b, i, 1, ROOT[b % 4u] + (i % 4u ? 12u : 0u), 2, 96);
        nt(b, 0, 2, 36, 1, 110); nt(b, 8, 2, 36, 1, 100); nt(b, 10, 2, 36, 1, 84);
        nt(b, 4, 2, 38, 1, 100); nt(b, 12, 2, 38, 1, 100);
        for (i = 0; i < 16u; i++) nt(b, i, 2, 42, 1, i % 2u ? 50 : 72);
    }
    line(4, 3, LEAD, NELEM(LEAD), 92);
}

typedef struct { const char *file, *title, *text; void (*score)(void); int isnew; } piece_t;
static const piece_t PIECES[] = {
    {"1_tape", "Tape Memories", "The 1.1 tape sounds together: NOSTALGIA's chords, MUSIC BOX, HAZY LEAD and the hiss of VHS HISS, all wobbling like a worn cassette.", p_tape, 1},
    {"2_drive", "Neon Drive", "Felucca's drive: the drum kit, an acid bass in sixteenths, a saw lead and FM6 pads.", p_drive, 0},
    {"3_garden", "Rain Garden", "The ambient side: AMBI's rain, a soft VOICE choir, BELL's drops and a sub bass, under a long reverb.", p_garden, 0},
    {"4_omni", "Omnichord", "Chords strummed across the keys, as OMNI plays them, on PHYS's harp, with a sub bass, a light groove and an FM bell.", p_omni, 0},
    {"5_keys", "Late Night Keys", "FM6's tine piano, an FM bass walking under it and the sampled sax on top.", p_keys, 0},
    {"6_chapel", "Chapel", "WHEEL's tonewheel organ, a choir of ANGELS and PHYS's bell tree.", p_chapel, 0},
    {"7_frozen", "Frozen", "GRAIN's clouds, bowed metal, a drone and the wind: textures that keep changing.", p_frozen, 0},
    {"8_arcade", "Arcade", "TRIO's chip voices: arpeggios, a fat bass and a sync lead over the kit.", p_arcade, 0},
};

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    char p[512];
    uint32_t n, i, a, b, now;
    FILE *js;
    snprintf(p, sizeof p, "%s/sounds.json", dir);
    js = fopen(p, "w");
    if (!js)
        return 1;
    fprintf(js, "[");
    for (n = 0; n < NELEM(PIECES); n++) {
        const piece_t *pc = &PIECES[n];
        int first = 1;
        host_tracks_init();
        for (i = 0; i < NTRK; i++) {
            part_name[i] = 0;
            trk[i].p[P_LEVEL] = 0;                  /* (a track the piece does not use: silent) */
        }
        nev = 0;
        pc->score();
        for (a = 1; a < nev; a++)                   /* in time (stable: an off before an on at the same time) */
            for (b = a; b > 0 && ev[b - 1u].t > ev[b].t; b--) {
                ev_t x = ev[b]; ev[b] = ev[b - 1u]; ev[b - 1u] = x;
            }
        run_ms(80);
        snprintf(p, sizeof p, "%s/%s.wav", dir, pc->file);
        w = fopen(p, "wb");
        frames = 0;
        wav_hdr(w, 0);
        for (i = 0, now = 0; i < nev; i++) {
            uint32_t t = ev[i].t / 2u;
            if (t > now) {
                run_ms(t - now);
                now = t;
            }
            if (ev[i].on)
                input_on(&trk[ev[i].trk], ev[i].note, ev[i].vel);
            else
                input_off(&trk[ev[i].trk], ev[i].note);
        }
        run_ms(3500);                                /* the tails */
        fseek(w, 0, SEEK_SET);
        wav_hdr(w, frames);
        fclose(w);
        w = 0;
        fprintf(js, "%s\n {\"file\": \"%s.mp3\", \"title\": \"%s\", \"text\": \"%s\", \"new\": %s, \"seconds\": %u, \"parts\": [",
                n ? "," : "", pc->file, pc->title, pc->text, pc->isnew ? "true" : "false", frames / FS);
        for (i = 0; i < NTRK; i++)
            if (part_name[i]) {
                fprintf(js, "%s\"%s %s\"", first ? "" : ", ", ENGINES[trk[i].eng_req]->name, part_name[i]);
                first = 0;
            }
        fprintf(js, "]}");
    }
    fprintf(js, "\n]\n");
    fclose(js);
    return 0;
}
