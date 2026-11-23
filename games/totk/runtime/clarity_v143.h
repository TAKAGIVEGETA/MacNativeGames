/* Tears of the Kingdom 1.4.3 UltraCam "Clarity (MaxLastBreath)" engine palette hook.
   Hooks EnvPalette (main+0x694f1c) and BigPalette (main+0x6a3ccc) to apply the exact
   in-engine weather and environmental lighting parameters from UltraCam 3.0's Clarity preset
   without altering UI or post-processing shaders. */
#pragma once
#include "runtime.h"
#include "text_padding_v143.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !TARGET_OS_IPHONE
static _Atomic int g_clarity_on;
extern void dm_gfx_filmic_set(int on);
extern int dm_gfx_filmic_on(void);

int runtime_clarity_on(void) {
    return atomic_load_explicit(&g_clarity_on, memory_order_relaxed);
}

void runtime_clarity_set(int on) {
    atomic_store_explicit(&g_clarity_on, !!on, memory_order_relaxed);
    fprintf(stderr, "[totk] UltraCam Clarity palette %s\n", on ? "on" : "off");
}

/* 4-way picture filter mode:
   0: Off (game original)
   1: Clarity (UltraCam in-engine weather/lighting palette)
   2: Filmic (Cemu Preset 6 post-processing tone map + film pass)
   3: Clarity + Filmic */
int runtime_filter_mode(void) {
    return (runtime_clarity_on() ? 1 : 0) | (dm_gfx_filmic_on() ? 2 : 0);
}

void runtime_filter_mode_set(int mode) {
    mode &= 3;
    runtime_clarity_set(mode & 1);
    dm_gfx_filmic_set((mode >> 1) & 1);
}

const char *runtime_filter_mode_word(int mode) {
    switch (mode & 3) {
    case 1: return "clarity";
    case 2: return "filmic";
    case 3: return "both";
    default: return "off";
    }
}

int runtime_filter_mode_parse(const char *word) {
    if (!word) return 0;
    if (!strncmp(word, "both", 4) || !strncmp(word, "3", 1)) return 3;
    if (!strncmp(word, "filmic", 6) || !strncmp(word, "2", 1)) return 2;
    if (!strncmp(word, "clarity", 7) || !strncmp(word, "on", 2) || !strncmp(word, "1", 1)) return 1;
    return 0;
}

typedef struct { float v[4]; } ClarityFloat4;
static void (*clarity_env_original)(void *, void *);
static ClarityFloat4 (*clarity_big_original)(void *, int, int);

static uint64_t clarity_u64(const void *p, size_t off) {
    uint64_t v; memcpy(&v, (const char *)p + off, sizeof v); return v;
}
static uint32_t clarity_u32(const void *p, size_t off) {
    uint32_t v; memcpy(&v, (const char *)p + off, sizeof v); return v;
}

enum {
    CLARITY_F_BG_COLOR = 0,
    CLARITY_F_FOG_COLOR,
    CLARITY_F_SUN_COLOR,
    CLARITY_F_SUN_INTENSITY,
    CLARITY_F_VOL_MASK_INTENSITY,
    CLARITY_F_FOG_START,
    CLARITY_F_AO_CONTRAST,
    CLARITY_F_BLOOM_STRENGTH,
    CLARITY_F_AO_SCALE,
    CLARITY_F_COUNT
};

typedef struct {
    char *pal;
    unsigned char modified[CLARITY_F_COUNT];
    float orig[CLARITY_F_COUNT][4];
    float written[CLARITY_F_COUNT][4];
} ClarityPalEntry;

static const struct {
    size_t val_off;
    size_t flag_off;
    unsigned count;
} clarity_specs[CLARITY_F_COUNT] = {
    [CLARITY_F_BG_COLOR]           = {0x5c, 0xf3,  4},
    [CLARITY_F_FOG_COLOR]          = {0x80, 0xf5,  4},
    [CLARITY_F_SUN_COLOR]          = {0xb0, 0xfd,  4},
    [CLARITY_F_SUN_INTENSITY]      = {0xc0, 0xfe,  1},
    [CLARITY_F_VOL_MASK_INTENSITY] = {0xd4, 0x10e, 1},
    [CLARITY_F_FOG_START]          = {0x94, 0xf6,  1},
    [CLARITY_F_AO_CONTRAST]        = {0x78, 0x10a, 1},
    [CLARITY_F_BLOOM_STRENGTH]     = {0x6c, 0xf4,  1},
    [CLARITY_F_AO_SCALE]           = {0x58, 0x111, 1},
};

static pthread_mutex_t clarity_mu = PTHREAD_MUTEX_INITIALIZER;
static ClarityPalEntry *clarity_entries;
static size_t clarity_count, clarity_cap;

static void clarity_compute_field(int idx, const float orig[4], float out[4]) {
    /* UltraCam 3.0 "Clarity (MaxLastBreath)" preset (BloodMoon is false):
       - BG Color: [0.5, 0.3, 0.5, 0] Multiply -> orig * (1 + v)
       - Fog Color: [-0.2, -0.2, 0.3, 0] Multiply -> orig * (1 + v)
       - Sun / VolumeMask Color: [0, 0, 0.6, 0] Multiply -> orig * (1 + v)
       - SunIntesity: 0.125 Multiply -> orig * 1.125
       - Volume Mask Intesity: 0.500 Multiply -> orig * 1.5
       - Fog Start: 0 Override -> 0.0
       - AO Contrast: 0.500 Add -> orig + 0.5
       - Bloom Strength: 0.150 Multiply -> orig * 1.15
       - AO Scale: -0.750 Multiply -> orig * 0.25 */
    memcpy(out, orig, 16);
    switch (idx) {
    case CLARITY_F_BG_COLOR:
        out[0] = orig[0] * 1.5f;
        out[1] = orig[1] * 1.3f;
        out[2] = orig[2] * 1.5f;
        break;
    case CLARITY_F_FOG_COLOR:
        out[0] = orig[0] * 0.8f;
        out[1] = orig[1] * 0.8f;
        out[2] = orig[2] * 1.3f;
        break;
    case CLARITY_F_SUN_COLOR:
        out[2] = orig[2] * 1.6f;
        break;
    case CLARITY_F_SUN_INTENSITY:
        out[0] = orig[0] * 1.125f;
        break;
    case CLARITY_F_VOL_MASK_INTENSITY:
        out[0] = orig[0] * 1.5f;
        break;
    case CLARITY_F_FOG_START:
        out[0] = 0.0f;
        break;
    case CLARITY_F_AO_CONTRAST:
        out[0] = orig[0] + 0.5f;
        break;
    case CLARITY_F_BLOOM_STRENGTH:
        out[0] = orig[0] * 1.15f;
        break;
    case CLARITY_F_AO_SCALE:
        out[0] = orig[0] * 0.25f;
        break;
    }
}

static void clarity_apply_big(char *pal) {
    int on = atomic_load_explicit(&g_clarity_on, memory_order_relaxed);
    pthread_mutex_lock(&clarity_mu);
    ClarityPalEntry *e = NULL;
    for (size_t i = 0; i < clarity_count; i++) {
        if (clarity_entries[i].pal == pal) { e = &clarity_entries[i]; break; }
    }
    if (!e) {
        if (!on) { pthread_mutex_unlock(&clarity_mu); return; }
        if (clarity_count == clarity_cap) {
            size_t ncap = clarity_cap ? clarity_cap * 2 : 64;
            ClarityPalEntry *next = realloc(clarity_entries, ncap * sizeof *next);
            if (!next) { pthread_mutex_unlock(&clarity_mu); return; }
            clarity_entries = next; clarity_cap = ncap;
        }
        e = &clarity_entries[clarity_count++];
        memset(e, 0, sizeof *e);
        e->pal = pal;
    }
    for (int i = 0; i < CLARITY_F_COUNT; i++) {
        size_t voff = clarity_specs[i].val_off;
        size_t foff = clarity_specs[i].flag_off;
        size_t bytes = clarity_specs[i].count * sizeof(float);
        unsigned char initialized = ((const unsigned char *)pal)[foff] & 1;
        if (!initialized) {
            e->modified[i] = 0;
            continue;
        }
        float cur[4] = {0};
        memcpy(cur, pal + voff, bytes);
        if (on) {
            if (!e->modified[i] || memcmp(cur, e->written[i], bytes) != 0) {
                memcpy(e->orig[i], cur, bytes);
                clarity_compute_field(i, e->orig[i], e->written[i]);
                memcpy(pal + voff, e->written[i], bytes);
                e->modified[i] = 1;
            }
        } else if (e->modified[i]) {
            if (memcmp(cur, e->written[i], bytes) == 0)
                memcpy(pal + voff, e->orig[i], bytes);
            e->modified[i] = 0;
        }
    }
    pthread_mutex_unlock(&clarity_mu);
}

static void clarity_env_palette(void *env_pal, void *a1) {
    clarity_env_original(env_pal, a1);
    if (atomic_load_explicit(&g_clarity_on, memory_order_relaxed) && env_pal) {
        /* Enviromental Color: [1.175, 1.025, 0.975, 0.675], Brightness 1, IsOn true.
           Note: env_pal + 0xa8 is the BloodMoon weight (only set when BloodMoon is true),
           so Clarity leaves +0xa8 untouched. */
        const float rgb[3] = {1.175f * 0.675f, 1.025f * 0.675f, 0.975f * 0.675f};
        memcpy((char *)env_pal + 0xb8, rgb, sizeof rgb);
    }
}

static ClarityFloat4 clarity_big_palette(void *this_ptr, int which, int a2) {
    char *pal = NULL;
    if (this_ptr) {
        uintptr_t p1 = clarity_u64(this_ptr, 8);
        if (p1) pal = (char *)clarity_u64((const void *)p1, 0x150);
    }
    if (pal) clarity_apply_big(pal);
    ClarityFloat4 res = clarity_big_original(this_ptr, which, a2);
    if (pal) clarity_apply_big(pal);
    return res;
}

static void clarity_palette_install(Module *m) {
    if (!m->ro_off || getenv("TOTK_EXPORT_IOS")) return;
    static const size_t env_calls[] = {0x693e60, 0x693e80, 0x6946d0, 0x6946f0, 0x695878, 0x695898};
    static const size_t big_calls[] = {
        0x697500, 0x69771c, 0x697948, 0x697bbc, 0x697d34, 0x697d6c, 0x697da4, 0x697dc8,
        0x69d718, 0x69d8e0, 0x69dab0, 0x69dc7c, 0x69de24, 0x69de50, 0x69df64, 0x69df94,
        0x69e0a8, 0x69e0d8, 0x69e210, 0x69e23c, 0x69f12c, 0x69f1f4, 0x8dc0fc, 0x8dc538,
        0x8dc564, 0x8dc754, 0x8dcc44, 0x8dcd40, 0x8dcf4c, 0x8dcfb8, 0x8dcfe4, 0x8dd0ec,
        0x8dd3d8, 0x8dd524, 0x8dd550, 0x21bcf48, 0x21bcf88, 0x21bcfb4, 0x21cae1c, 0x21caec4,
        0x21caef0, 0x21cb25c
    };
    const size_t env_orig = 0x694f1c, big_orig = 0x6a3ccc;
    const size_t pad_base = (m->text_end + 15) & ~15ULL;
    const size_t env_stub = pad_base + PAD_V143_CLARITY_ENV;
    const size_t big_stub = pad_base + PAD_V143_CLARITY_BIG;
    if (env_stub + 16 > m->ro_off || big_stub + 16 > m->ro_off)
        runtime_fail("clarity palette: no veneer space");
    for (unsigned i = 0; i < sizeof env_calls / sizeof *env_calls; i++) {
        uint32_t expected = 0x94000000u | ((uint32_t)((int64_t)env_orig - env_calls[i]) / 4 & 0x03ffffff);
        if (clarity_u32(m->base, env_calls[i]) != expected)
            runtime_fail("clarity palette: unexpected 1.4.3 EnvPalette call");
    }
    for (unsigned i = 0; i < sizeof big_calls / sizeof *big_calls; i++) {
        uint32_t expected = 0x94000000u | ((uint32_t)((int64_t)big_orig - big_calls[i]) / 4 & 0x03ffffff);
        if (clarity_u32(m->base, big_calls[i]) != expected)
            runtime_fail("clarity palette: unexpected 1.4.3 BigPalette call");
    }
    for (unsigned i = 0; i < 16; i++) {
        if (m->base[env_stub + i] || m->base[big_stub + i])
            runtime_fail("clarity palette: veneer space occupied");
    }
    clarity_env_original = (void (*)(void *, void *))(m->base + env_orig);
    clarity_big_original = (ClarityFloat4 (*)(void *, int, int))(m->base + big_orig);

    uint32_t env_code[4] = {0x58000050, 0xd61f0200, 0, 0};
    void *env_hook = runtime_hle_wrap_named(clarity_env_palette, "UltraCam Clarity EnvPalette");
    memcpy(env_code + 2, &env_hook, sizeof env_hook);
    memcpy(m->base + env_stub, env_code, sizeof env_code);
    for (unsigned i = 0; i < sizeof env_calls / sizeof *env_calls; i++) {
        uint32_t br = 0x94000000u | ((uint32_t)((int64_t)env_stub - env_calls[i]) / 4 & 0x03ffffff);
        memcpy(m->base + env_calls[i], &br, sizeof br);
    }

    uint32_t big_code[4] = {0x58000050, 0xd61f0200, 0, 0};
    void *big_hook = runtime_hle_wrap_named(clarity_big_palette, "UltraCam Clarity BigPalette");
    memcpy(big_code + 2, &big_hook, sizeof big_hook);
    memcpy(m->base + big_stub, big_code, sizeof big_code);
    for (unsigned i = 0; i < sizeof big_calls / sizeof *big_calls; i++) {
        uint32_t br = 0x94000000u | ((uint32_t)((int64_t)big_stub - big_calls[i]) / 4 & 0x03ffffff);
        memcpy(m->base + big_calls[i], &br, sizeof br);
    }
    fprintf(stderr, "[totk] UltraCam Clarity palette hooks installed (1.4.3)\n");
}
#else
static void clarity_palette_install(Module *m) { (void)m; }
#endif
