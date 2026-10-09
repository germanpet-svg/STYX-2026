/*
 * STYX 2026 — Win32 API pura (sin SDL2), con lógica portada del Styx DOS original
 *
 * Compila con w64devkit (ojo: ahora hace falta -lwinmm para la música):
 *   gcc -std=c11 -O2 styx.c -o styx.exe -mwindows -lgdi32 -luser32 -lwinmm -lm
 *
 * Controles: flechas/WASD mover, SHIFT + dirección = dibujar, M = música, ESC = salir.
 *
 * Lógica portada del original (desensamblado2):
 *  - Styx = "palo" de 8 segmentos en anillo, con ángulo, giro y largo variables.
 *    Rebota al chocar (reintenta hasta 3 veces, acortándose), y mata si toca tu línea.
 *    1 Styx en niveles 1-2, 2 Styx desde el nivel 3. Si el Styx #2 queda dentro del
 *    territorio conquistado, muere.
 *  - Sparx = chispas que patrullan los bordes siguiendo la pared (una por cada mano).
 *    1 en nivel 1, 2 desde el nivel 2. Nacen en el borde a la altura del Styx,
 *    esperan dormidas hasta que el jugador se aleja, y mueven doble en nivel >2
 *    cuando solo queda un Styx.
 *  - Mecha: si dibujas y te quedas quieto, la mecha se consume y pierdes la vida.
 *  - Barras de HUD: cercanía del Styx y mecha.
 */

/* GetTickCount64 requiere Vista o superior; debe definirse ANTES de windows.h */
#undef  _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ---------- Configuración ---------- */

#define WIN_W       960
#define WIN_H       640
#define HUD_H       40
#define CELL_W      4
#define CELL_H      4
#define GRID_COLS   (WIN_W / CELL_W)              /* 240 */
#define GRID_ROWS   ((WIN_H - HUD_H) / CELL_H)    /* 150 */

#define TARGET_PCT  65
#define BONUS_PCT   80
#define MAX_LIVES   3
#define MAX_SPARX   2
#define MAX_STYX    2
#define STYX_SEGS   8
#define TICK_MS     16
#define PLAYER_STEP_MS   16
#define SPARX_STEP_MS    21

#define STYX_STEP_TICKS  2       /* el Styx se actualiza cada 2 ticks (~32 ms) */
#define MAX_LINE_LEN     4000    /* trazo demasiado largo = pierdes la vida (el original corta a 2000) */
#define STYX_SPEED_K     34.0f   /* escala de velocidad (celdas por actualización) */
#define STYX_HALF_K      75.0f   /* escala de la mitad del largo del palo */
#define SPARX_KILL_DIST  2       /* distancia (celdas) a la que la sparx te mata */
#define SPARX_WAKE_DIST  30      /* la sparx despierta cuando te alejas esto */
#define FUSE_MAX         100.0f
#define INVULN_TICKS     90

#define DEG2RAD     0.01745329252f

/* ---------- Framebuffer RGB32 ---------- */

static uint32_t g_fb[WIN_W * WIN_H];

#undef RGB   /* windows.h define su propio RGB (orden BGR); aquí usamos 0x00RRGGBB */
#define RGB(r,g,b) ((((uint32_t)(r) & 0xffu) << 16) | (((uint32_t)(g) & 0xffu) << 8) | ((uint32_t)(b) & 0xffu))

static void fb_clear(uint32_t c) {
    int n = WIN_W * WIN_H;
    for (int i = 0; i < n; i++) g_fb[i] = c;
}

static void fb_rect(int x, int y, int w, int h, uint32_t c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > WIN_W) w = WIN_W - x;
    if (y + h > WIN_H) h = WIN_H - y;
    if (w <= 0 || h <= 0) return;
    for (int yy = y; yy < y + h; yy++) {
        uint32_t *row = &g_fb[yy * WIN_W + x];
        for (int xx = 0; xx < w; xx++) row[xx] = c;
    }
}

static void fb_line(int x0, int y0, int x1, int y1, uint32_t c) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (x0 >= 0 && x0 < WIN_W && y0 >= 0 && y0 < WIN_H)
            g_fb[y0 * WIN_W + x0] = c;
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* ---------- Paleta ---------- */

#define C_BG          RGB(6,   8,  18)
#define C_LINE        RGB(0, 255, 220)
#define C_FILL        RGB(20, 80, 120)
#define C_FILL_BONUS  RGB(40, 160, 180)
#define C_SPARX       RGB(255, 200, 40)
#define C_SPARX_SLEEP RGB(120, 100, 30)
#define C_PLAYER      RGB(220, 255, 220)
#define C_PLAYER_HOT  RGB(255, 255, 180)
#define C_HUD_BG      RGB(10, 14, 28)
#define C_HUD_TEXT    RGB(200, 220, 255)
#define C_HUD_ACCENT  RGB(0, 255, 200)
#define C_DANGER      RGB(255, 60, 60)
#define C_WHITE       RGB(255, 255, 255)
#define C_BAR_BG      RGB(30, 40, 60)

static uint32_t styx_color(int i) {
    switch (((i % 5) + 5) % 5) {
        case 0:  return RGB(255, 40, 120);
        case 1:  return RGB(255, 140, 40);
        case 2:  return RGB(80, 255, 120);
        case 3:  return RGB(90, 160, 255);
        default: return RGB(255, 255, 100);
    }
}

/* ---------- Tipos ---------- */

typedef enum { CELL_EMPTY=0, CELL_FILLED, CELL_LINE, CELL_BORDER } CellType;
typedef enum { STATE_TITLE, STATE_PLAYING, STATE_FILLING, STATE_LEVEL_CLEAR, STATE_GAME_OVER } GameState;
typedef enum { DIR_NONE, DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT } Dir;

typedef struct {
    int x, y;
    Dir dir;
    bool drawing;
    int step_timer;
    int line_len;          /* celdas de línea dibujadas en el trazo actual */
    int start_x, start_y;  /* dónde empezó el trazo (para cancelar) */
    int invuln;            /* ticks de invulnerabilidad tras perder una vida */
} Player;

typedef struct { int x0, y0, x1, y1; } Seg;

typedef struct {
    bool  active;
    float x, y;            /* centro */
    float vx, vy;
    int   angle, dangle;   /* grados */
    float len_div, len_target;
    Seg   seg[STYX_SEGS];
    int   seg_head, seg_count;
    int   color_idx;
    int   step_timer;
} Styx;

typedef struct {
    int x, y;
    Dir dir;
    int step_timer;
    bool active;
    bool awake;
    int hand;              /* +1 sigue la pared por la izquierda, -1 por la derecha */
    int stuck;
} Sparx;

typedef struct {
    CellType grid[GRID_ROWS][GRID_COLS];
    Player   player;
    Styx     styx[MAX_STYX];
    int      styx_total;
    Sparx    sparx[MAX_SPARX];
    int      sparx_count;
    int      filled_count;
    int      total_playable;
    int      score;
    int      lives;
    int      level;
    float    fuse;
    int      fill_count, fill_pos, fill_per_tick, fill_before;   /* relleno animado */
    float    bonus_multiplier;
    float    bonus_phase;
    GameState state;
    int      state_timer;
    char     message[128];
} Game;

static Game g_game;
static bool g_running = true;
static bool g_keys[256] = {0};
static HWND g_hwnd = NULL;
static ULONGLONG g_last_tick = 0;
static uint8_t g_stymask[GRID_ROWS][GRID_COLS];   /* celdas ocupadas por los palos de Styx */
static int g_fill_q[GRID_ROWS * GRID_COLS];       /* cola de celdas por rellenar (animación) */

/* ---------- Utilidades ---------- */

static inline int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static inline bool in_bounds(int x, int y) {
    return x >= 0 && y >= 0 && x < GRID_COLS && y < GRID_ROWS;
}
static inline int round_i(float v) { return (int)floorf(v + 0.5f); }
static inline int deg_wrap(int d) { d %= 360; return d < 0 ? d + 360 : d; }
/* aleatorio 0..n inclusive (como el rand del original) */
static inline int rand_n(int n) { return n <= 0 ? 0 : rand() % (n + 1); }

/* ---------- Audio: música y efectos por MIDI (winmm) ---------- */

typedef struct { uint8_t note; uint8_t dur; } SfxNote;

static HMIDIOUT g_midi = NULL;
static bool g_music_on = true;
static int  g_mu_tick = 0, g_mu_step = 0;
static int  g_n_arp = -1, g_n_bass = -1, g_n_lead = -1, g_lead_ttl = 0;
static const SfxNote *g_sfx = NULL;
static int  g_sfx_timer = 0, g_sfx_note = -1;

static const SfxNote SFX_CAPTURE[] = {{72,3},{76,3},{79,3},{84,6},{0,0}};
static const SfxNote SFX_LOSE[]    = {{67,5},{64,5},{60,5},{55,5},{48,12},{0,0}};
static const SfxNote SFX_LEVEL[]   = {{72,4},{76,4},{79,4},{84,4},{79,4},{84,12},{0,0}};
static const SfxNote SFX_OVER[]    = {{60,8},{57,8},{53,8},{48,24},{0,0}};

/* Progresión Am - F - C - G, 4 compases de 16 pasos */
static const int ARP_CH[4][4] = {{69,72,76,81},{65,69,72,77},{67,72,76,79},{67,71,74,79}};
static const int BASS_ROOT[4] = {45, 41, 48, 43};
static const int ARP_P[16]  = {0,1,2,1, 0,1,2,1, 0,1,2,1, 0,1,2,3};
static const int BASS_P[16] = {1,0,2,0, 1,0,2,0, 1,0,2,0, 1,0,2,2};
static const int LEAD[64] = {
    76,0,0,0, 0,0,79,0, 81,0,0,0, 79,0,76,0,
    77,0,0,0, 0,0,81,0, 84,0,0,0, 81,0,77,0,
    79,0,0,0, 0,0,84,0, 83,0,0,0, 79,0,76,0,
    74,0,0,0, 0,0,79,0, 83,0,81,0, 79,0,0,0
};

static void midi_send(int status, int d1, int d2) {
    if (g_midi) midiOutShortMsg(g_midi, (DWORD)(status | (d1 << 8) | (d2 << 16)));
}

static void midi_init(void) {
    if (midiOutOpen(&g_midi, MIDI_MAPPER, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        g_midi = NULL;
        return;
    }
    midi_send(0xC0 | 0, 80, 0);   /* arpegio: lead cuadrada */
    midi_send(0xC0 | 1, 38, 0);   /* bajo: synth bass */
    midi_send(0xC0 | 2, 81, 0);   /* melodía: lead sierra */
    midi_send(0xC0 | 3, 80, 0);   /* efectos: lead cuadrada */
    midi_send(0xB0 | 0, 7, 55);
    midi_send(0xB0 | 1, 7, 95);
    midi_send(0xB0 | 2, 7, 80);
    midi_send(0xB0 | 3, 7, 105);
    midi_send(0xB0 | 9, 7, 85);
}

static void midi_close(void) {
    if (g_midi) {
        midiOutReset(g_midi);
        midiOutClose(g_midi);
        g_midi = NULL;
    }
}

static void music_silence(void) {
    static const int chans[4] = {0, 1, 2, 9};
    for (int i = 0; i < 4; i++) midi_send(0xB0 | chans[i], 123, 0);   /* all notes off */
    g_n_arp = g_n_bass = g_n_lead = -1;
    g_lead_ttl = 0;
}

static void music_toggle(void) {
    g_music_on = !g_music_on;
    if (!g_music_on) music_silence();
    else g_mu_tick = 0;
}

static void music_update(int level) {
    if (!g_midi || !g_music_on) return;
    int len = 7 - (level - 1) / 2;          /* más rápido con el nivel */
    if (len < 4) len = 4;
    if (++g_mu_tick < len) return;
    g_mu_tick = 0;

    int bar = (g_mu_step >> 4) & 3, st = g_mu_step & 15;

    if (g_n_arp  >= 0) midi_send(0x80 | 0, g_n_arp, 0);
    if (g_n_bass >= 0) midi_send(0x80 | 1, g_n_bass, 0);

    int a = ARP_CH[bar][ARP_P[st]];
    midi_send(0x90 | 0, a, 62);
    g_n_arp = a;

    g_n_bass = -1;
    if (BASS_P[st]) {
        int b = BASS_ROOT[bar] + (BASS_P[st] == 2 ? 12 : 0);
        midi_send(0x90 | 1, b, 100);
        g_n_bass = b;
    }

    if (g_lead_ttl > 0) {
        g_lead_ttl--;
        if (g_lead_ttl == 0 && g_n_lead >= 0) {
            midi_send(0x80 | 2, g_n_lead, 0);
            g_n_lead = -1;
        }
    }
    int ln = LEAD[g_mu_step & 63];
    if (ln) {
        if (g_n_lead >= 0) midi_send(0x80 | 2, g_n_lead, 0);
        midi_send(0x90 | 2, ln, 85);
        g_n_lead = ln;
        g_lead_ttl = 3;
    }

    if (st == 0 || st == 8)  midi_send(0x99, 36, 110);   /* bombo */
    if (st == 4 || st == 12) midi_send(0x99, 38, 95);    /* caja */
    if ((st & 1) == 0)       midi_send(0x99, 42, 60);    /* hi-hat */

    g_mu_step = (g_mu_step + 1) & 63;
}

static void sfx_play(const SfxNote *s) {
    if (!g_midi) return;
    if (g_sfx_note >= 0) midi_send(0x83, g_sfx_note, 0);
    g_sfx = s;
    g_sfx_note = s->note;
    midi_send(0x93, s->note, 110);
    g_sfx_timer = s->dur;
}

static void sfx_update(void) {
    if (!g_sfx) return;
    if (g_sfx_timer > 0) { g_sfx_timer--; return; }
    midi_send(0x83, g_sfx_note, 0);
    g_sfx++;
    if (g_sfx->note == 0) { g_sfx = NULL; g_sfx_note = -1; return; }
    g_sfx_note = g_sfx->note;
    midi_send(0x93, g_sfx_note, 110);
    g_sfx_timer = g_sfx->dur;
}

/* ---------- Grid y territorio ---------- */

static void grid_reset(Game *g) {
    memset(g->grid, 0, sizeof(g->grid));
    for (int x = 0; x < GRID_COLS; x++) {
        g->grid[0][x] = CELL_BORDER;
        g->grid[GRID_ROWS - 1][x] = CELL_BORDER;
    }
    for (int y = 0; y < GRID_ROWS; y++) {
        g->grid[y][0] = CELL_BORDER;
        g->grid[y][GRID_COLS - 1] = CELL_BORDER;
    }
    g->filled_count = 0;
    g->total_playable = (GRID_COLS - 2) * (GRID_ROWS - 2);
}

static void recount_filled(Game *g) {
    int c = 0;
    for (int y = 1; y < GRID_ROWS - 1; y++)
        for (int x = 1; x < GRID_COLS - 1; x++)
            if (g->grid[y][x] != CELL_EMPTY) c++;
    g->filled_count = c;
}

static int active_styx_count(const Game *g) {
    int n = 0;
    for (int i = 0; i < MAX_STYX; i++) if (g->styx[i].active) n++;
    return n;
}

/*
 * Convierte el trazo en borde, etiqueta las regiones vacías y rellena todas
 * excepto la que contiene al Styx #0 (como en el original). Un Styx que
 * quede dentro del territorio rellenado muere. Aquí solo se prepara la cola
 * de relleno; el relleno animado y el cierre ocurren en fill_step/end_capture.
 */
static void begin_capture(Game *g) {
    static int label[GRID_ROWS][GRID_COLS];
    static int stack_x[GRID_ROWS * GRID_COLS];
    static int stack_y[GRID_ROWS * GRID_COLS];
    static int sizes[GRID_ROWS * GRID_COLS + 1];

    g->fill_before = g->filled_count;
    g->fill_count = 0;
    g->fill_pos = 0;
    g->fill_per_tick = 1;

    for (int y = 0; y < GRID_ROWS; y++)
        for (int x = 0; x < GRID_COLS; x++)
            if (g->grid[y][x] == CELL_LINE) g->grid[y][x] = CELL_BORDER;

    memset(label, 0, sizeof(label));
    int nlabels = 0;
    const int dx[4] = {1, -1, 0, 0};
    const int dy[4] = {0, 0, 1, -1};

    for (int y = 1; y < GRID_ROWS - 1; y++) {
        for (int x = 1; x < GRID_COLS - 1; x++) {
            if (g->grid[y][x] != CELL_EMPTY || label[y][x] != 0) continue;
            nlabels++;
            int sp = 0, count = 0;
            stack_x[sp] = x; stack_y[sp] = y; sp++;
            label[y][x] = nlabels;
            while (sp > 0) {
                sp--;
                int cx = stack_x[sp], cy = stack_y[sp];
                count++;
                for (int i = 0; i < 4; i++) {
                    int nx = cx + dx[i], ny = cy + dy[i];
                    if (!in_bounds(nx, ny)) continue;
                    if (g->grid[ny][nx] != CELL_EMPTY || label[ny][nx] != 0) continue;
                    label[ny][nx] = nlabels;
                    stack_x[sp] = nx; stack_y[sp] = ny; sp++;
                }
            }
            sizes[nlabels] = count;
        }
    }

    if (nlabels > 0) {
        int keep = 0;
        if (g->styx[0].active) {
            int sx = (int)g->styx[0].x, sy = (int)g->styx[0].y;
            if (in_bounds(sx, sy)) keep = label[sy][sx];
        }
        if (keep == 0) {                       /* sin Styx válido: conservar la región mayor */
            int best = 0;
            for (int i = 1; i <= nlabels; i++)
                if (sizes[i] > best) { best = sizes[i]; keep = i; }
        }
        for (int y = 1; y < GRID_ROWS - 1; y++)
            for (int x = 1; x < GRID_COLS - 1; x++)
                if (label[y][x] > 0 && label[y][x] != keep)
                    g_fill_q[g->fill_count++] = y * GRID_COLS + x;
    }

    /* El relleno dura entre ~0,13 s y ~0,7 s según el tamaño del pedazo */
    if (g->fill_count > 0) {
        int dur = g->fill_count / 300;
        if (dur < 8) dur = 8;
        if (dur > 45) dur = 45;
        g->fill_per_tick = (g->fill_count + dur - 1) / dur;
    }
}

/* ---------- Styx (palo de 8 segmentos) ---------- */

/* Recorre un segmento. mark=false: devuelve flags (1 = bloqueado, 2 = toca tu línea).
 * mark=true: pinta las celdas en g_stymask. */
static int seg_walk(Game *g, int x0, int y0, int x1, int y1, bool mark) {
    int flags = 0;
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (!in_bounds(x0, y0)) {
            flags |= 1;
        } else if (mark) {
            g_stymask[y0][x0] = 1;
        } else {
            CellType c = g->grid[y0][x0];
            if (c == CELL_BORDER || c == CELL_FILLED) flags |= 1;
            else if (c == CELL_LINE) flags |= 2;
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    return flags;
}

/* Equivale a FUN_1000_1ac3 del original: nueva dirección, velocidad y giro */
static void styx_new_direction(Game *g, Styx *s) {
    int lv = g->level < 6 ? g->level : 5;
    float a2 = (float)(rand_n(89) << 2) * DEG2RAD;
    s->vx = STYX_SPEED_K * cosf(a2) / (float)(25 - rand_n(lv + 12));
    s->vy = STYX_SPEED_K * sinf(a2) / (float)(25 - rand_n(lv + 10));
    s->len_target = (float)(3 + rand_n(3));
    if (g->styx_total == 2) s->len_target += 5.0f;
    s->dangle = 5 + rand_n(15);
    if (rand_n(1)) s->dangle = -s->dangle;
}

static void styx_init(Game *g, Styx *s) {
    memset(s, 0, sizeof(*s));
    s->active = true;
    s->x = GRID_COLS * 0.5f;
    s->y = GRID_ROWS * 0.3f;
    s->angle = 0;
    s->len_div = 10.0f;
    s->color_idx = 0;
    styx_new_direction(g, s);
}

static void lose_life(Game *g);

static void styx_update(Game *g, Styx *s) {
    if (rand_n(g->level + 30) > 28) styx_new_direction(g, s);
    s->angle = deg_wrap(s->angle + s->dangle);

    for (int tries = 3; tries > 0; tries--) {
        float nx = s->x + s->vx, ny = s->y + s->vy;
        float rad = (float)s->angle * DEG2RAD;
        float hx = cosf(rad) * STYX_HALF_K / s->len_div;
        float hy = sinf(rad) * STYX_HALF_K / s->len_div;
        int x0 = round_i(nx - hx), y0 = round_i(ny - hy);
        int x1 = round_i(nx + hx), y1 = round_i(ny + hy);
        int flags = seg_walk(g, x0, y0, x1, y1, false);

        if (!(flags & 1)) {
            s->x = nx; s->y = ny;
            Seg *sg = &s->seg[s->seg_head];
            sg->x0 = x0; sg->y0 = y0; sg->x1 = x1; sg->y1 = y1;
            s->seg_head++;
            if (s->seg_count < STYX_SEGS) s->seg_count++;
            if (s->seg_head >= STYX_SEGS) {
                s->seg_head = 0;
                s->color_idx = rand_n(4);
            }
            if (flags & 2) { lose_life(g); return; }    /* tocó tu línea */

            if (s->len_div > 10.0f) s->len_div = 10.0f;
            if (s->len_target < s->len_div) s->len_div -= 1.0f;
            if (s->len_div < s->len_target) s->len_div += 1.0f;
            return;
        }
        /* choque: no se mueve, nueva dirección y se acorta */
        styx_new_direction(g, s);
        if (s->len_div < 50.0f) s->len_div *= 2.0f;
    }
}

static void rebuild_styx_mask(Game *g) {
    memset(g_stymask, 0, sizeof(g_stymask));
    for (int i = 0; i < MAX_STYX; i++) {
        Styx *s = &g->styx[i];
        if (!s->active) continue;
        for (int k = 0; k < s->seg_count; k++)
            seg_walk(g, s->seg[k].x0, s->seg[k].y0, s->seg[k].x1, s->seg[k].y1, true);
    }
}

static void update_styx_all(Game *g) {
    for (int i = 0; i < MAX_STYX; i++) {
        Styx *s = &g->styx[i];
        if (!s->active) continue;
        if (++s->step_timer < STYX_STEP_TICKS) continue;
        s->step_timer = 0;
        styx_update(g, s);
        if (g->state != STATE_PLAYING) return;
    }
    rebuild_styx_mask(g);
    /* el palo del Styx toca la cabeza de tu trazo */
    if (g->player.drawing && g->player.line_len > 0 &&
        g_stymask[g->player.y][g->player.x])
        lose_life(g);
}

/* ---------- Sparx (patrullan los bordes siguiendo la pared) ---------- */

static Dir dir_left(Dir d) {
    switch (d) {
        case DIR_UP: return DIR_LEFT;   case DIR_LEFT: return DIR_DOWN;
        case DIR_DOWN: return DIR_RIGHT; case DIR_RIGHT: return DIR_UP;
        default: return d;
    }
}
static Dir dir_right(Dir d) {
    switch (d) {
        case DIR_UP: return DIR_RIGHT;  case DIR_RIGHT: return DIR_DOWN;
        case DIR_DOWN: return DIR_LEFT; case DIR_LEFT: return DIR_UP;
        default: return d;
    }
}
static Dir dir_opposite(Dir d) {
    switch (d) {
        case DIR_UP: return DIR_DOWN;   case DIR_DOWN: return DIR_UP;
        case DIR_LEFT: return DIR_RIGHT; case DIR_RIGHT: return DIR_LEFT;
        default: return d;
    }
}
static void dir_delta(Dir d, int *dx, int *dy) {
    *dx = 0; *dy = 0;
    switch (d) {
        case DIR_UP:    *dy = -1; break;
        case DIR_DOWN:  *dy =  1; break;
        case DIR_LEFT:  *dx = -1; break;
        case DIR_RIGHT: *dx =  1; break;
        default: break;
    }
}

/* Celda de borde que linda (incluso en diagonal) con la zona NO conquistada.
 * Solo por estas celdas se camina: lo ya conquistado no se puede pisar. */
static bool is_perimeter(Game *g, int x, int y) {
    if (!in_bounds(x, y) || g->grid[y][x] != CELL_BORDER) return false;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx, ny = y + dy;
            if (in_bounds(nx, ny) && g->grid[ny][nx] == CELL_EMPTY) return true;
        }
    }
    return false;
}

static bool sparx_walkable(Game *g, int x, int y) {
    return is_perimeter(g, x, y);
}

/* Nacen en el borde, a la altura del Styx (una por la izquierda, otra por la derecha) */
static void spawn_sparx(Game *g, int idx) {
    Sparx *s = &g->sparx[idx];
    /* Como en el original: parte desde la posición del Styx y avanza hasta topar con el borde */
    int sy = clampi((int)g->styx[0].y, 1, GRID_ROWS - 2);
    int step = (idx == 0) ? -1 : 1;
    int x = clampi((int)g->styx[0].x, 0, GRID_COLS - 1);
    while (in_bounds(x, sy) && g->grid[sy][x] != CELL_BORDER) x += step;
    if (!in_bounds(x, sy)) x = (idx == 0) ? 0 : GRID_COLS - 1;
    s->x = x;
    s->y = sy;
    s->dir = DIR_DOWN;
    s->step_timer = 0;
    s->active = true;
    s->awake = false;
    s->hand = (idx == 0) ? 1 : -1;
    s->stuck = 0;
}

static bool sparx_step_once(Game *g, Sparx *s) {
    Dir order[4];
    if (s->hand > 0) {
        order[0] = dir_left(s->dir);  order[1] = s->dir;  order[2] = dir_right(s->dir);
    } else {
        order[0] = dir_right(s->dir); order[1] = s->dir;  order[2] = dir_left(s->dir);
    }
    order[3] = dir_opposite(s->dir);
    for (int k = 0; k < 4; k++) {
        int dx, dy;
        dir_delta(order[k], &dx, &dy);
        int nx = s->x + dx, ny = s->y + dy;
        if (sparx_walkable(g, nx, ny)) {
            s->x = nx; s->y = ny; s->dir = order[k];
            return true;
        }
    }
    return false;
}

static void update_sparx(Game *g) {
    for (int i = 0; i < g->sparx_count; i++) {
        Sparx *s = &g->sparx[i];
        if (!s->active) continue;

        if (!s->awake) {
            int dx = abs(s->x - g->player.x), dy = abs(s->y - g->player.y);
            if ((dx > dy ? dx : dy) >= SPARX_WAKE_DIST) s->awake = true;
            else continue;
        }

        s->step_timer += TICK_MS;
        while (s->step_timer >= SPARX_STEP_MS) {
            s->step_timer -= SPARX_STEP_MS;
            int moves = (g->level > 2 && active_styx_count(g) == 1) ? 2 : 1;
            for (int m = 0; m < moves; m++) {
                if (sparx_step_once(g, s)) s->stuck = 0;
                else s->stuck++;
            }
            if (s->stuck > 100) { spawn_sparx(g, i); break; }
        }

        if (g->player.invuln == 0) {
            int dx = abs(s->x - g->player.x), dy = abs(s->y - g->player.y);
            if (dx <= SPARX_KILL_DIST && dy <= SPARX_KILL_DIST) {
                lose_life(g);
                return;
            }
        }
    }
}

/* ---------- Jugador y vidas ---------- */

static void player_to_start(Game *g) {
    g->player.x = GRID_COLS / 2;
    g->player.y = GRID_ROWS - 1;      /* sobre el borde inferior */
    g->player.dir = DIR_NONE;
    g->player.drawing = false;
    g->player.step_timer = 0;
    g->player.line_len = 0;
    g->player.start_x = g->player.x;
    g->player.start_y = g->player.y;
    g->player.invuln = 0;
}

static void clear_line(Game *g) {
    for (int y = 0; y < GRID_ROWS; y++)
        for (int x = 0; x < GRID_COLS; x++)
            if (g->grid[y][x] == CELL_LINE) g->grid[y][x] = CELL_EMPTY;
}

/* (is_perimeter se define arriba, junto a las sparx) */

static int sparx_clearance(Game *g, int x, int y) {
    int best = 9999;
    for (int i = 0; i < g->sparx_count; i++) {
        if (!g->sparx[i].active) continue;
        int dx = abs(g->sparx[i].x - x), dy = abs(g->sparx[i].y - y);
        int d = dx > dy ? dx : dy;
        if (d < best) best = d;
    }
    return best;
}

/* Tras perder una vida el puntero vuelve al perímetro de la zona sin conquistar:
 * al inicio del trazo si es seguro; si no, al punto del perímetro más lejos de las sparx. */
static void find_respawn(Game *g, int hx, int hy, int *ox, int *oy) {
    if (in_bounds(hx, hy) && is_perimeter(g, hx, hy) && sparx_clearance(g, hx, hy) >= 25) {
        *ox = hx; *oy = hy;
        return;
    }
    int best = -1, bx = GRID_COLS / 2, by = GRID_ROWS - 1;
    for (int y = 0; y < GRID_ROWS; y++) {
        for (int x = 0; x < GRID_COLS; x++) {
            if (!is_perimeter(g, x, y)) continue;
            int c = sparx_clearance(g, x, y);
            if (c > best) { best = c; bx = x; by = y; }
        }
    }
    *ox = bx; *oy = by;
}

static void lose_life(Game *g) {
    int hx = g->player.line_len > 0 ? g->player.start_x : g->player.x;
    int hy = g->player.line_len > 0 ? g->player.start_y : g->player.y;

    clear_line(g);
    g->player.drawing = false;
    g->player.line_len = 0;
    g->fuse = 0.0f;
    g->lives--;

    if (g->lives <= 0) {
        g->state = STATE_GAME_OVER;
        g->state_timer = 0;
        snprintf(g->message, sizeof(g->message), "PUNTOS %d", g->score);
        music_silence();
        sfx_play(SFX_OVER);
    } else {
        int rx, ry;
        find_respawn(g, hx, hy, &rx, &ry);
        g->player.x = rx; g->player.y = ry;
        g->player.start_x = rx; g->player.start_y = ry;
        g->player.step_timer = 0;
        g->player.dir = DIR_NONE;
        g->player.invuln = INVULN_TICKS;
        sfx_play(SFX_LOSE);
    }
}

static void cancel_line(Game *g) {
    if (g->player.line_len > 0) {
        clear_line(g);
        g->player.x = g->player.start_x;
        g->player.y = g->player.start_y;
    }
    g->player.drawing = false;
    g->player.line_len = 0;
    g->fuse = 0.0f;
}

static void update_bonus(Game *g) {
    g->bonus_phase += 0.03f;
    float t = (sinf(g->bonus_phase) + 1.0f) * 0.5f;
    g->bonus_multiplier = 1.0f + t * 9.0f;
}

/* Si el jugador quedó en una celda que ya no linda con la zona libre, va a la más cercana */
static void snap_player_to_perimeter(Game *g) {
    if (is_perimeter(g, g->player.x, g->player.y)) return;
    int best = 1 << 30, bx = g->player.x, by = g->player.y;
    for (int y = 0; y < GRID_ROWS; y++) {
        for (int x = 0; x < GRID_COLS; x++) {
            if (!is_perimeter(g, x, y)) continue;
            int d = abs(x - g->player.x) + abs(y - g->player.y);
            if (d < best) { best = d; bx = x; by = y; }
        }
    }
    g->player.x = bx; g->player.y = by;
    g->player.start_x = bx; g->player.start_y = by;
}

/* Cierre de la captura: Styx tragado muere, puntaje, nivel completo */
static void end_capture(Game *g) {
    for (int i = 1; i < MAX_STYX; i++) {
        Styx *s = &g->styx[i];
        if (!s->active) continue;
        int sx = (int)s->x, sy = (int)s->y;
        if (!in_bounds(sx, sy) || g->grid[sy][sx] != CELL_EMPTY) s->active = false;
    }

    recount_filled(g);
    int gained = g->filled_count - g->fill_before;
    if (gained < 0) gained = 0;
    int pct = (g->filled_count * 100) / g->total_playable;
    int mult = (int)g->bonus_multiplier; if (mult < 1) mult = 1;
    g->score += gained * mult;

    snap_player_to_perimeter(g);
    for (int i = 0; i < g->sparx_count; i++)       /* sparx atrapada en lo conquistado: renace */
        if (g->sparx[i].active && !sparx_walkable(g, g->sparx[i].x, g->sparx[i].y))
            spawn_sparx(g, i);

    if (pct >= TARGET_PCT) {
        if (pct >= BONUS_PCT) g->score += 500 * g->level;
        g->state = STATE_LEVEL_CLEAR;
        g->state_timer = 0;
        snprintf(g->message, sizeof(g->message), "NIVEL %d COMPLETADO", g->level);
        music_silence();
        sfx_play(SFX_LEVEL);
    } else {
        g->state = STATE_PLAYING;
        if (gained > 0) sfx_play(SFX_CAPTURE);
    }
}

static void finish_line(Game *g) {
    g->player.drawing = false;
    g->player.line_len = 0;
    g->fuse = 0.0f;
    begin_capture(g);
    if (g->fill_count == 0) {
        end_capture(g);
    } else {
        g->state = STATE_FILLING;
        g->state_timer = 0;
        music_silence();
        g_sfx = NULL;
        if (g_sfx_note >= 0) { midi_send(0x83, g_sfx_note, 0); g_sfx_note = -1; }
    }
}

/* Relleno animado: el pedazo se va "tomando" en barrido, con un tono que sube */
static void fill_step(Game *g) {
    int n = g->fill_per_tick;
    while (n-- > 0 && g->fill_pos < g->fill_count) {
        int idx = g_fill_q[g->fill_pos++];
        g->grid[idx / GRID_COLS][idx % GRID_COLS] = CELL_FILLED;
    }
    if ((g->state_timer & 1) == 0 && g->fill_count > 0) {
        float p = (float)g->fill_pos / (float)g->fill_count;
        int note = 48 + (int)(p * 36.0f);
        if (g_sfx_note >= 0) midi_send(0x83, g_sfx_note, 0);
        midi_send(0x93, note, 90);
        g_sfx_note = note;
    }
    if (g->fill_pos >= g->fill_count) {
        if (g_sfx_note >= 0) { midi_send(0x83, g_sfx_note, 0); g_sfx_note = -1; }
        end_capture(g);
    }
}

static void player_input(Game *g, Dir *out_dir) {
    *out_dir = DIR_NONE;
    if (g_keys[VK_UP]    || g_keys['W']) *out_dir = DIR_UP;
    else if (g_keys[VK_DOWN]  || g_keys['S']) *out_dir = DIR_DOWN;
    else if (g_keys[VK_LEFT]  || g_keys['A']) *out_dir = DIR_LEFT;
    else if (g_keys[VK_RIGHT] || g_keys['D']) *out_dir = DIR_RIGHT;

    bool shift = g_keys[VK_SHIFT] || g_keys[VK_LSHIFT] || g_keys[VK_RSHIFT];
    if (!g->player.drawing && shift) {
        CellType here = g->grid[g->player.y][g->player.x];
        if (here == CELL_BORDER) {
            g->player.drawing = true;
            g->player.line_len = 0;
            g->player.start_x = g->player.x;
            g->player.start_y = g->player.y;
        }
    } else if (g->player.drawing && !shift) {
        cancel_line(g);        /* soltar SHIFT en mitad del trazo lo cancela */
    }
}

/* Devuelve true si el jugador se movió en este tick */
static bool update_player(Game *g) {
    Dir d;
    player_input(g, &d);
    if (d == DIR_NONE) return false;

    g->player.step_timer += TICK_MS;
    if (g->player.step_timer < PLAYER_STEP_MS) return false;
    g->player.step_timer = 0;

    int nx = g->player.x, ny = g->player.y;
    switch (d) {
        case DIR_UP:    ny--; break;
        case DIR_DOWN:  ny++; break;
        case DIR_LEFT:  nx--; break;
        case DIR_RIGHT: nx++; break;
        default: return false;
    }
    nx = clampi(nx, 0, GRID_COLS - 1);
    ny = clampi(ny, 0, GRID_ROWS - 1);
    if (nx == g->player.x && ny == g->player.y) return false;

    CellType target = g->grid[ny][nx];
    g->player.dir = d;
    bool moved = false;

    if (g->player.drawing) {
        if (target == CELL_EMPTY) {
            g->grid[ny][nx] = CELL_LINE;
            g->player.x = nx; g->player.y = ny;
            g->player.line_len++;
            moved = true;
            if (g->player.line_len >= MAX_LINE_LEN) { lose_life(g); return false; }
        } else if (target == CELL_BORDER && (g->player.line_len > 0 || is_perimeter(g, nx, ny))) {
            g->player.x = nx; g->player.y = ny;   /* toca borde: cierra el trazo */
            moved = true;
            if (g->player.line_len > 0) {
                finish_line(g);
            } else {
                g->player.start_x = nx;
                g->player.start_y = ny;
            }
        } else if (target == CELL_LINE) {
            return false;                        /* no cruzar el propio trazo */
        } else {                                 /* BORDER o FILLED: cierra */
            return false;                        /* territorio conquistado: no se pisa */
        }
    } else if (is_perimeter(g, nx, ny)) {        /* solo se camina por el borde de la zona libre */
        g->player.x = nx; g->player.y = ny;
        moved = true;
    }
    return moved;
}

/* Mecha: si dibujas y te quedas quieto, se consume */
static void update_fuse(Game *g, bool moved) {
    if (g->player.drawing && g->player.line_len > 0) {
        g->fuse += 0.7f;
        if (moved) g->fuse -= 1.8f;
        if (g->fuse < 0.0f) g->fuse = 0.0f;
        if (g->fuse >= FUSE_MAX) lose_life(g);
    } else {
        g->fuse = 0.0f;
    }
}

/* ---------- Inicialización ---------- */

static void level_init(Game *g, int level) {
    grid_reset(g);
    g->level = level;
    player_to_start(g);
    g->fuse = 0.0f;

    g->styx_total = level < 3 ? 1 : 2;
    for (int i = 0; i < MAX_STYX; i++) g->styx[i].active = false;
    for (int i = 0; i < g->styx_total; i++) styx_init(g, &g->styx[i]);

    g->sparx_count = level < 2 ? 1 : 2;
    for (int i = 0; i < g->sparx_count; i++) spawn_sparx(g, i);

    g->bonus_multiplier = 1.0f;
    g->bonus_phase = 0.0f;
    g->state = STATE_PLAYING;
    g->state_timer = 0;
    snprintf(g->message, sizeof(g->message), "NIVEL %d", level);
    memset(g_stymask, 0, sizeof(g_stymask));
}

static void game_init(Game *g) {
    memset(g, 0, sizeof(*g));
    g->lives = MAX_LIVES;
    g->score = 0;
    snprintf(g->message, sizeof(g->message), "STYX 2026");
    level_init(g, 1);
    g->state = STATE_TITLE;
    g->state_timer = 0;
}

/* ---------- Update ---------- */

static void update_game(Game *g) {
    g->state_timer++;
    sfx_update();
    if (g->state == STATE_TITLE || g->state == STATE_PLAYING) music_update(g->level);

    switch (g->state) {
        case STATE_TITLE:
            if (g_keys[VK_SPACE] || g_keys[VK_RETURN]) level_init(g, 1);
            break;
        case STATE_PLAYING: {
            update_bonus(g);
            bool moved = update_player(g);
            if (g->state == STATE_PLAYING) update_fuse(g, moved);
            if (g->state == STATE_PLAYING) update_styx_all(g);
            if (g->state == STATE_PLAYING) update_sparx(g);
            if (g->player.invuln > 0) g->player.invuln--;
            break;
        }
        case STATE_FILLING:
            fill_step(g);
            break;
        case STATE_LEVEL_CLEAR:
            if (g->state_timer > 90) level_init(g, g->level + 1);
            break;
        case STATE_GAME_OVER:
            if (g_keys[VK_SPACE] || g_keys[VK_RETURN]) game_init(g);
            break;
    }
}

/* ---------- Fuente 5x7 ---------- */

static const uint8_t FONT[][7] = {
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, /* A */
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* B */
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, /* C */
    {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}, /* D */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* E */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* F */
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, /* G */
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* H */
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, /* I */
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, /* J */
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* K */
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, /* L */
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, /* M */
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, /* N */
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* O */
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* P */
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, /* Q */
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* R */
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, /* S */
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* T */
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, /* U */
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, /* V */
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, /* W */
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* X */
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, /* Y */
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, /* Z */
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, /* 0 */
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 1 */
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, /* 2 */
    {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, /* 3 */
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, /* 4 */
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, /* 5 */
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, /* 6 */
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, /* 7 */
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* 8 */
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, /* 9 */
    {0x00,0x04,0x00,0x00,0x04,0x00,0x00}, /* : */
    {0x04,0x04,0x04,0x04,0x04,0x00,0x04}, /* ! */
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, /* - */
    {0x18,0x19,0x02,0x04,0x08,0x13,0x03}, /* % */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}  /* space */
};

static void draw_char(char c, int x, int y, int scale, uint32_t col) {
    int idx = -1;
    if (c >= 'A' && c <= 'Z') idx = c - 'A';
    else if (c >= 'a' && c <= 'z') idx = c - 'a';
    else if (c >= '0' && c <= '9') idx = 26 + (c - '0');
    else if (c == ':') idx = 36;
    else if (c == '!') idx = 37;
    else if (c == '-') idx = 38;
    else if (c == '%') idx = 39;
    else if (c == ' ') idx = 40;
    else return;
    const uint8_t *gl = FONT[idx];
    for (int r = 0; r < 7; r++) {
        uint8_t bits = gl[r];
        for (int b = 0; b < 5; b++) {
            if (bits & (1 << (4 - b)))
                fb_rect(x + b * scale, y + r * scale, scale, scale, col);
        }
    }
}

static void draw_text(const char *s, int x, int y, int scale, uint32_t col) {
    int cx = x;
    for (; *s; s++) {
        if (*s == ' ') { cx += 6 * scale; continue; }
        draw_char(*s, cx, y, scale, col);
        cx += 6 * scale;
    }
}

/* ---------- Render ---------- */

static void draw_grid(Game *g) {
    for (int y = 0; y < GRID_ROWS; y++) {
        for (int x = 0; x < GRID_COLS; x++) {
            CellType c = g->grid[y][x];
            if (c == CELL_EMPTY) continue;
            uint32_t col;
            switch (c) {
                case CELL_FILLED: col = C_FILL; break;
                case CELL_BORDER: col = C_FILL_BONUS; break;
                case CELL_LINE:   col = C_LINE; break;
                default: continue;
            }
            fb_rect(x * CELL_W, HUD_H + y * CELL_H, CELL_W, CELL_H, col);
        }
    }
}

static void draw_player(Game *g) {
    if (g->player.invuln > 0 && ((g->player.invuln >> 2) & 1)) return;   /* parpadeo */
    int px = g->player.x * CELL_W;
    int py = HUD_H + g->player.y * CELL_H;
    fb_rect(px - 3, py - 3, CELL_W + 6, CELL_H + 6, RGB(40, 60, 40));
    fb_rect(px - 1, py - 1, CELL_W + 2, CELL_H + 2,
            g->player.drawing ? C_PLAYER_HOT : C_PLAYER);
}

static void draw_styx(Game *g) {
    for (int i = 0; i < MAX_STYX; i++) {
        Styx *s = &g->styx[i];
        if (!s->active) continue;
        for (int k = 0; k < s->seg_count; k++) {
            Seg *sg = &s->seg[k];
            int ax = sg->x0 * CELL_W + CELL_W / 2, ay = HUD_H + sg->y0 * CELL_H + CELL_H / 2;
            int bx = sg->x1 * CELL_W + CELL_W / 2, by = HUD_H + sg->y1 * CELL_H + CELL_H / 2;
            uint32_t col = styx_color(s->color_idx);
            fb_line(ax, ay, bx, by, col);
            fb_line(ax + 1, ay, bx + 1, by, col);
        }
    }
}

static void draw_sparx(Game *g) {
    for (int i = 0; i < g->sparx_count; i++) {
        Sparx *s = &g->sparx[i];
        if (!s->active) continue;
        int px = s->x * CELL_W;
        int py = HUD_H + s->y * CELL_H;
        uint32_t col = s->awake ? (((g->state_timer >> 2) & 1) ? C_WHITE : C_SPARX)
                                : C_SPARX_SLEEP;
        fb_rect(px - 2, py - 2, CELL_W + 4, CELL_H + 4, col);
    }
}

static void draw_bar(int x, int y, int w, int h, float frac, uint32_t col) {
    fb_rect(x, y, w, h, C_BAR_BG);
    int fw = (int)(w * clampf(frac, 0.0f, 1.0f));
    if (fw > 0) fb_rect(x, y, fw, h, col);
}

static void draw_hud(Game *g) {
    fb_rect(0, 0, WIN_W, HUD_H, C_HUD_BG);
    fb_rect(0, HUD_H - 1, WIN_W, 1, C_HUD_ACCENT);

    char buf[128];
    snprintf(buf, sizeof(buf), "PUNTOS %d", g->score);
    draw_text(buf, 12, 6, 2, C_HUD_TEXT);

    snprintf(buf, sizeof(buf), "NIVEL %d", g->level);
    draw_text(buf, 260, 6, 2, C_HUD_TEXT);

    int pct = (g->filled_count * 100) / g->total_playable;
    snprintf(buf, sizeof(buf), "TERRITORIO %d%%", pct);
    draw_text(buf, 440, 6, 2, C_HUD_TEXT);

    draw_text("VIDAS", 720, 6, 2, C_HUD_TEXT);
    for (int i = 0; i < g->lives; i++)
        fb_rect(830 + i * 22, 8, 16, 16, C_PLAYER);

    snprintf(buf, sizeof(buf), "X%d", (int)g->bonus_multiplier);
    draw_text(buf, 12, 26, 1, g->bonus_multiplier > 6.0f ? C_DANGER : C_HUD_ACCENT);

    /* Cercanía del Styx (distancia Manhattan, como en el original) */
    float near_frac = 0.0f;
    if (g->styx[0].active) {
        int d = abs((int)g->styx[0].x - g->player.x) + abs((int)g->styx[0].y - g->player.y);
        near_frac = 1.0f - (float)d / (float)(GRID_COLS + GRID_ROWS);
    }
    draw_text("STYX", 300, 26, 1, C_HUD_TEXT);
    draw_bar(335, 26, 100, 8, near_frac, near_frac > 0.7f ? C_DANGER : C_HUD_ACCENT);

    /* Mecha */
    float fuse_frac = g->fuse / FUSE_MAX;
    draw_text("MECHA", 470, 26, 1, C_HUD_TEXT);
    draw_bar(515, 26, 100, 8, fuse_frac, fuse_frac > 0.6f ? C_DANGER : C_SPARX);

    draw_text(g_music_on ? "M:MUSICA" : "M:MUDO", 700, 26, 1, C_HUD_TEXT);
}

static void render_game(Game *g) {
    fb_clear(C_BG);
    draw_grid(g);
    draw_sparx(g);
    draw_styx(g);
    draw_player(g);
    draw_hud(g);

    if (g->state == STATE_TITLE) {
        fb_rect(0, HUD_H, WIN_W, WIN_H - HUD_H, RGB(2, 3, 8));
        draw_text("STYX", WIN_W/2 - 90, 180, 8, C_LINE);
        draw_text("2026", WIN_W/2 - 90, 260, 8, RGB(255, 40, 120));
        draw_text("PULSA ESPACIO PARA EMPEZAR", WIN_W/2 - 210, 420, 2, C_HUD_TEXT);
        draw_text("SHIFT + DIRECCION PARA DIBUJAR", WIN_W/2 - 260, 460, 2, C_HUD_ACCENT);
        draw_text("M MUSICA - ESC SALIR", WIN_W/2 - 120, 500, 1, C_HUD_TEXT);
    } else if (g->state == STATE_LEVEL_CLEAR) {
        draw_text(g->message, WIN_W/2 - 200, 280, 3, C_HUD_ACCENT);
    } else if (g->state == STATE_GAME_OVER) {
        fb_rect(0, HUD_H, WIN_W, WIN_H - HUD_H, RGB(20, 0, 0));
        draw_text("GAME OVER", WIN_W/2 - 130, 240, 5, C_DANGER);
        draw_text(g->message, WIN_W/2 - 100, 340, 2, C_HUD_TEXT);
        draw_text("ESPACIO PARA REINICIAR", WIN_W/2 - 200, 420, 2, C_HUD_ACCENT);
    }
}

/* ---------- Win32 ---------- */

static void blit_to_screen(HWND hwnd);

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) { g_running = false; DestroyWindow(hwnd); return 0; }
            if (wp == 'M' && ((lp >> 30) & 1) == 0) music_toggle();   /* sin autorepetición */
            if (wp < 256) g_keys[wp] = true;
            return 0;
        case WM_KEYUP:
            if (wp < 256) g_keys[wp] = false;
            return 0;
        case WM_KILLFOCUS:
            memset(g_keys, 0, sizeof(g_keys));
            return 0;
        case WM_ERASEBKGND:
            return 1;                      /* evita parpadeo */
        case WM_PAINT:
            blit_to_screen(hwnd);
            return 0;
        case WM_DESTROY:
            g_running = false;
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static void blit_to_screen(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = WIN_W;
    bmi.bmiHeader.biHeight      = -WIN_H;  /* top-down */
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    SetDIBitsToDevice(hdc,
        0, 0, WIN_W, WIN_H,
        0, 0, 0, WIN_H,
        g_fb, &bmi, DIB_RGB_COLORS);

    EndPaint(hwnd, &ps);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    (void)hPrev; (void)lpCmd;
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "Styx2026";
    RegisterClassA(&wc);

    /* Ajustar tamaño de ventana para que el cliente sea WIN_W x WIN_H exactos */
    RECT r = {0, 0, WIN_W, WIN_H};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);

    g_hwnd = CreateWindowExA(0, "Styx2026", "Styx 2026",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top,
        NULL, NULL, hInst, NULL);
    if (!g_hwnd) return 1;

    ShowWindow(g_hwnd, nShow);
    srand((unsigned)GetTickCount());
    midi_init();
    game_init(&g_game);
    g_last_tick = GetTickCount64();

    while (g_running) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { g_running = false; break; }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!g_running) break;

        ULONGLONG now = GetTickCount64();
        if (now - g_last_tick >= TICK_MS) {
            g_last_tick = now;
            update_game(&g_game);
            render_game(&g_game);
            InvalidateRect(g_hwnd, NULL, FALSE);
        } else {
            Sleep(1);
        }
    }
    midi_close();
    return 0;
}
