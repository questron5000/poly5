/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1 in a browser: the app (firmware/src/poly5.c) runs unchanged as WebAssembly. platform.c includes this
 * file instead of the hardware when DINGHY_WEB is set (DINGHY_HOST too, for the app's few host switches). Like
 * tests/host.c it is a fake FM-1, but driven from JavaScript (site/play/): the page passes the keys, buttons and
 * knobs in, and takes the sound (44.1 kHz stereo floats), the screen (240 x 240 RGB565) and the lights out.
 *
 *   web/build.sh   -> site/play/poly5.wasm
 *
 * One call of web_run() advances the FM-1 to a time in milliseconds, a millisecond a step, as the host does: the
 * startup animation first, then the app's main loop, one pass a step (poly5.c: DINGHY_WEB). The sound is made in
 * the I2S's blocks of 256 frames and queued for the page. */
#include <stdint.h>

#define WEB_EXPORT(name) __attribute__((export_name(#name)))

#ifndef APP_NAME
#define APP_NAME "POLY5"
#endif
#ifndef APP_VER
#define APP_VER "web"
#endif
#define FS 44100u
#define HALF_FRAMES 256u

#include "libc.c"                               /* memset, memcpy (freestanding, as on the FM-1) */

static volatile uint32_t fm1_ms;
#define HOST_NENC 7
static struct { uint32_t notes, buttons; } fm1_in;
static uint32_t host_edges, host_rel, host_rel_next;
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t b = host_edges;
    host_edges = 0;
    *released = host_rel;
    host_rel = host_rel_next;
    host_rel_next = 0;
    return b;
}
static uint32_t fm1_input_note_edges(void) { return 0; }
static int32_t host_enc[HOST_NENC];
static int32_t fm1_enc_take(uint32_t e) { int32_t v = host_enc[e % HOST_NENC]; host_enc[e % HOST_NENC] = 0; return v; }
static uint64_t host_leds, host_glow;           /* bit id: 0..13 buttons, 14..40 keys; the glow is the dim layer */
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static void fm1_led_key(uint32_t id, int on) { if (on) host_leds |= (uint64_t)1 << id; else host_leds &= ~((uint64_t)1 << id); }
static void fm1_led_key_glow(uint32_t id, int on) { if (on) host_glow |= (uint64_t)1 << id; else host_glow &= ~((uint64_t)1 << id); }

/* the screen: 240 x 240 RGB565 */
static uint16_t fb[240 * 240];
static uint32_t fb_gen;                         /* counts changes: the page redraws when it moved */
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = c;
    fb_gen++;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u) {
                uint16_t c = px[j * w + i];
                fb[(y + j) * 240u + x + i] = (uint16_t)(c >> 8 | c << 8);
            }
    fb_gen++;
}
#ifndef CV_MAX
#define CV_MAX (240u * 64u)
#endif
#include "gfx.c"

/* storage: in memory, as the FM-1's sectors; the page keeps a copy (localStorage) and gives it back at the start */
#include "store.h"
static uint8_t host_set[SECTOR_MAX + 4], host_blob[NBLOB][SECTOR_MAX + 4], host_slot[NSLOT][SLOT_MAX + 4];
static uint32_t store_gen;                      /* counts saves: the page copies the store when it moved */
static int host_load(uint8_t *a, uint32_t max, void *p, uint32_t n)
{
    if (!a[0] || n > max || (uint32_t)(a[1] | a[2] << 8 | a[3] << 16) != n) return -1;
    memcpy(p, a + 4, n);
    return 0;
}
static int host_save(uint8_t *a, uint32_t max, const void *p, uint32_t n)
{
    if (n > max) return -1;
    a[0] = 1; a[1] = (uint8_t)n; a[2] = (uint8_t)(n >> 8); a[3] = (uint8_t)(n >> 16);
    memcpy(a + 4, p, n);
    store_gen++;
    return 0;
}
static int settings_load(void *p, uint32_t n) { return host_load(host_set, SECTOR_MAX, p, n); }
static int settings_save(const void *p, uint32_t n) { return host_save(host_set, SECTOR_MAX, p, n); }
static int blob_load(uint32_t i, void *p, uint32_t n) { return i < NBLOB ? host_load(host_blob[i], SECTOR_MAX, p, n) : -1; }
static int blob_save(uint32_t i, const void *p, uint32_t n) { return i < NBLOB ? host_save(host_blob[i], SECTOR_MAX, p, n) : -1; }
static int slot_load(uint32_t i, void *p, uint32_t n) { return i < NSLOT ? host_load(host_slot[i], SLOT_MAX, p, n) : -1; }
static int slot_save(uint32_t i, const void *p, uint32_t n) { return i < NSLOT ? host_save(host_slot[i], SLOT_MAX, p, n) : -1; }

/* MIDI in from the page (Web MIDI); MIDI out goes nowhere */
static uint8_t mq[256][3];
static uint32_t mq_w, mq_r;
static int midi_get(uint8_t *st, uint8_t *d1, uint8_t *d2)
{
    if (mq_r == mq_w) return 0;
    *st = mq[mq_r % 256u][0]; *d1 = mq[mq_r % 256u][1]; *d2 = mq[mq_r % 256u][2];
    mq_r++;
    return 1;
}
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }

static void app_audio(int32_t *out, uint32_t n);
static void app_main(void);
static int intro(void);
static int web_intro;

/* the sound for the page: a ring of stereo floats, -1 .. 1 */
#define AQ 16384u                               /* frames (0.37 s) */
static float aq[AQ * 2u];
static uint32_t aq_w, aq_r;
static float web_gain = 1.0f;

static void platform_poll(void)                 /* one call = one millisecond, as tests/host.c */
{
    static uint32_t owed;
    static int32_t blk[2u * HALF_FRAMES];
    fm1_ms++;
    owed += FS;
    while (owed >= 1000u * HALF_FRAMES) {
        uint32_t i;
        owed -= 1000u * HALF_FRAMES;
        memset(blk, 0, sizeof blk);
        app_audio(blk, HALF_FRAMES);
        for (i = 0; i < HALF_FRAMES; i++) {
            if (aq_w - aq_r >= AQ) aq_r++;      /* (the page fell behind: the oldest goes) */
            aq[(aq_w % AQ) * 2u] = (float)blk[2u * i] * (web_gain / 32768.0f);
            aq[(aq_w % AQ) * 2u + 1u] = (float)blk[2u * i + 1u] * (web_gain / 32768.0f);
            aq_w++;
        }
    }
}

/* ---- what the page calls ---- */
static int web_started;
static void web_step(void)                      /* the FM-1 a little further: at least a millisecond */
{
    uint32_t t = fm1_ms;
    if (!web_started) { web_started = 1; app_main(); return; }   /* the start: settings, the patch; then the animation */
    if (web_intro) { if (!intro()) web_intro = 0; }
    else app_main();
    if (fm1_ms == t) platform_poll();
}
WEB_EXPORT(web_run) void web_run(uint32_t until_ms) { while ((int32_t)(fm1_ms - until_ms) < 0) web_step(); }
WEB_EXPORT(web_fill) uint32_t web_fill(uint32_t frames)   /* run until this much sound is waiting; returns it */
{
    uint32_t guard = 0;
    while (aq_w - aq_r < frames && guard++ < 2000u) web_step();
    return aq_w - aq_r;
}
WEB_EXPORT(web_audio) float *web_audio(void) { return aq; }
WEB_EXPORT(web_audio_size) uint32_t web_audio_size(void) { return AQ; }
WEB_EXPORT(web_audio_read) uint32_t web_audio_read(void) { return aq_r; }
WEB_EXPORT(web_audio_take) void web_audio_take(uint32_t n) { aq_r += n; }
WEB_EXPORT(web_ms) uint32_t web_ms(void) { return fm1_ms; }
WEB_EXPORT(web_screen) uint16_t *web_screen(void) { return fb; }
WEB_EXPORT(web_screen_gen) uint32_t web_screen_gen(void) { return fb_gen; }
WEB_EXPORT(web_leds_lo) uint32_t web_leds_lo(void) { return (uint32_t)host_leds; }
WEB_EXPORT(web_leds_hi) uint32_t web_leds_hi(void) { return (uint32_t)(host_leds >> 32); }
WEB_EXPORT(web_glow_lo) uint32_t web_glow_lo(void) { return (uint32_t)host_glow; }
WEB_EXPORT(web_keys) void web_keys(uint32_t mask) { fm1_in.notes = mask; }
WEB_EXPORT(web_button) void web_button(uint32_t id, uint32_t down)
{
    if (id >= 14u) return;
    if (down) { host_edges |= 1u << id; fm1_in.buttons |= 1u << id; }
    else { host_rel |= 1u << id; fm1_in.buttons &= ~(1u << id); }
}
WEB_EXPORT(web_turn) void web_turn(uint32_t enc, int32_t detents) { if (enc < HOST_NENC) host_enc[enc] += detents; }
WEB_EXPORT(web_midi) void web_midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (mq_w - mq_r >= 256u) return;
    mq[mq_w % 256u][0] = (uint8_t)st; mq[mq_w % 256u][1] = (uint8_t)d1; mq[mq_w % 256u][2] = (uint8_t)d2;
    mq_w++;
}
WEB_EXPORT(web_volume) void web_volume(float g) { web_gain = g; }
/* the store, for the page to keep: settings, then the blobs, then the slots, one buffer */
#define STORE_BYTES ((SECTOR_MAX + 4u) * (1u + NBLOB) + (SLOT_MAX + 4u) * NSLOT)
static uint8_t store_io[STORE_BYTES];
WEB_EXPORT(web_store) uint8_t *web_store(void) { return store_io; }
WEB_EXPORT(web_store_size) uint32_t web_store_size(void) { return STORE_BYTES; }
WEB_EXPORT(web_store_gen) uint32_t web_store_gen(void) { return store_gen; }
WEB_EXPORT(web_store_out) void web_store_out(void)   /* the store into store_io */
{
    uint32_t i, o = 0;
    memcpy(store_io + o, host_set, sizeof host_set); o += sizeof host_set;
    for (i = 0; i < NBLOB; i++) { memcpy(store_io + o, host_blob[i], sizeof host_blob[i]); o += sizeof host_blob[i]; }
    for (i = 0; i < NSLOT; i++) { memcpy(store_io + o, host_slot[i], sizeof host_slot[i]); o += sizeof host_slot[i]; }
}
WEB_EXPORT(web_store_in) void web_store_in(void)     /* store_io into the store: before the first web_run */
{
    uint32_t i, o = 0;
    memcpy(host_set, store_io + o, sizeof host_set); o += sizeof host_set;
    for (i = 0; i < NBLOB; i++) { memcpy(host_blob[i], store_io + o, sizeof host_blob[i]); o += sizeof host_blob[i]; }
    for (i = 0; i < NSLOT; i++) { memcpy(host_slot[i], store_io + o, sizeof host_slot[i]); o += sizeof host_slot[i]; }
}
