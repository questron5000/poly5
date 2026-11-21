/* SPDX-License-Identifier: GPL-3.0-only */
/* POLY5 for the M-VAVE FM-1: the 5-channel synth of poly5.html, on Dinghy's platform.
 *
 * Phase 1: the voice. Each of the 5 channels is Poly5's voice, in fixed point:
 *   oscillator (polyBLEP saw / square, triangle, sine) + pink noise -> lowpass (TPT state-variable filter, the same
 *   response as Web Audio's biquad with Q in dB) with an envelope on the cutoff -> VCA (attack, 80 ms decay to 70 %,
 *   release), all as poly5.html's noteOn / releaseV shape them. 12 voices are shared by the 5 channels.
 *   ROUND ROBIN is the default: each note goes to the next synth. Also UNISON and POLY (multitimbral).
 *
 *   The panel: see app_main (hold FX / SEL / ENV / LFO / EDIT for synth 1..5, ALGORITHM = NOISE, SEQ = RANDOM).
 *   MIDI in    USB and TRS: note on / off, channel N plays the channels listening on N
 *
 * Integers only (no FPU, no libgcc): see docs/RULES.md. Constants: tools/gen_poly5_tables.py. */
#define APP_NAME "POLY5"
#define UA_SLOW_RENDER 1                        /* USB audio: a deep ring (usb.c): a chord takes ~3 ms to render */
/* the USB name (usb.c): "POLY5 FM-1" in a computer's lists. It must keep "FM-1" in it: the installers and the
 * Firmware Hub only speak to MIDI ports whose name looks like an FM-1 (tools/fm1_install.py PORT_RE), and a unit
 * they cannot find cannot be updated or put back to the official firmware from a running state */
#define USB_PRODUCT_STR {22, 3, 'P', 0, 'O', 0, 'L', 0, 'Y', 0, '5', 0, ' ', 0, 'F', 0, 'M', 0, '-', 0, '1', 0}
#define STORE_MAGIC 0x35594C50u                 /* "PLY5": POLY5's own patches and settings in flash */
static void app_idle(void);
#define LCD_IDLE app_idle
#define CV_MAX (240u * 174u)                    /* the shapes, or the edit page, as one canvas (84 KB of the pool) */
#include "platform.c"
#include "poly5_tables.h"

/* ---- the patch (the main loop writes it, the audio interrupt reads it; bytes, so a read is never half done) */
enum { W_SAW, W_SQUARE, W_TRI, W_SINE, W_FM, W_METAL, W_N };   /* FM: a sine bent by a sine; METAL: two squares, ringing */
enum { M_POLY, M_RR, M_UNISON, M_RANDOM, M_N };
enum { E_SKIP, E_ON, E_OFF };
#define MSHAPE(c) ((chan[c].lfo_bits >> 2) & 3u)
#define MSYNC(c) (chan[c].lfo_bits >> 7)
#define FTYPE(c) (chan[c].lfo_bits & 3u)
#define NCH 5u
typedef struct {
    uint8_t midi_ch;                            /* 1..16, 0 = all */
    uint8_t enabled, wave;                      /* enabled: E_SKIP (not in the cycle), E_ON, E_OFF (takes its turn, silent) */
    int8_t octave;                              /* -2..+2 */
    uint8_t noise, env_filt, cutoff, reso;      /* 0..100, 0..100 (Poly5's 0..1 x 100), 0..100, 0..30 */
    uint8_t attack, release, dly, rev, vol;     /* 0..100 each */
    /* since the second patch format: */
    uint8_t tone;                               /* FM: which ratio (clean .. clangy); METAL: the interval */
    uint8_t drop;                               /* the pitch starts this much higher and falls: a punch, a tom, a zap */
    uint8_t decay;                              /* 0: holds at 70 % as Poly5; else the note dies by itself, this slowly */
    uint8_t detune;                             /* a second copy of the wave, this much off (saw, square, triangle, sine) */
    /* since the third patch format: what moves the filter's cutoff. Three things, each by itself, together if you like:
     * the envelope (env_filt: how far; env_rate: 0 = Poly5's own shape, else how slowly it falls), an LFO, noise */
    uint8_t env_rate;
    uint8_t lfo_depth, lfo_rate;                /* how far (100: 2.5 octaves each way); 0.1 .. 20 Hz, or a note value */
    uint8_t lfo_bits;                           /* bits 2-3: the LFO's shape (sine, triangle, saw, square); bit 7: in time with the tempo;
                                                 * bits 0-1: the filter's type (lowpass, bandpass, highpass) */
    uint8_t nz_depth, nz_rate;                  /* noise on the cutoff: how far; a new random place once a second .. every 0.36 ms */
    uint8_t sat;                                /* this synth's own saturation, after its filter */
    uint8_t sus;                                /* with DECAY on: the level it falls to and holds (0: to nothing) */
} chan_t;
typedef struct {
    char name[16];
    chan_t ch[NCH];
    uint8_t mode, mvol;
    /* the master effects. dj: the DJ filter, -100 (lowpass) .. 0 (open) .. +100 (highpass), an int8 in a byte;
     * del_all, rev_all: how much of everything goes to the delay and the reverb, on top of each synth's own send */
    uint8_t dj, del_time, del_fb, del_all, rev_size, rev_damp, rev_all, drive, dj_reso, comp;   /* drive: distortion on everything (Poly5's saturation) */
    uint8_t ch_type, ch_rate, ch_depth, ch_mix; /* the modulation effect on everything: off, chorus, flanger, phaser; its speed, depth, how much */
    /* since the fifth patch format (comp, above: the compressor's amount): */
    uint8_t drv_type;                           /* the drive: soft, hard, fold */
    uint8_t lofi_bits, lofi_rate, lofi_mix;     /* lofi: fewer bits, a lower sample rate, how much of it */
    uint8_t cmp_spd, cmp_gain;                  /* the compressor: how slowly it lets go; make-up gain */
    /* (the last eight bytes of the fifth format: four in use since) */
    uint8_t tp_wow, tp_flut, tp_age, tp_fail;  /* LOFI, the tape: slow drift of the pitch, fast flutter, how worn (dull and thin), how often it fails (dropouts, hiss) */
    uint8_t fx_off;                             /* bits: an effect switched off: 0 saturation, 1 filter, 2 delay, 3 reverb, 4 bit reduction, 5 mastering */
    uint8_t arp;                                /* the arpeggiator's style (0: off) */
    uint8_t spare[2];
} preset_t;

/* The factory patches: one for every way of playing, two ambient, two industrial; each leans on the master effects.
 * They are described in tools/gen_poly5_patches.py, which writes this file. (Their names are longer than a user
 * patch's 15 letters: FACTORY_NAME.) */
#include "poly5_patches.h"
#define NFACTORY (sizeof FACTORY / sizeof FACTORY[0])
#define POLY5_VER "1.0"                        /* shown on the startup animation: raise it with every release */
static volatile chan_t chan[NCH];
/* LENGTH (KNOB 2 on PLAY): every synth's release, and its decay if it has one, this much longer or shorter than the
 * patch says: -100 .. 0 (as the patch) .. +100. Each stops at its own end (0, 100): some get there sooner */
static volatile int32_t len_off;
static uint32_t len_of(uint32_t v) { int32_t x = (int32_t)v + len_off; return (uint32_t)(x < 0 ? 0 : x > 100 ? 100 : x); }
static volatile uint32_t master_q15 = 22282;   /* MASTER.vol/100 * 0.85 */

/* ---- note events: the main loop posts, the audio interrupt takes (one writer each side) */
enum { EV_ON = 1, EV_OFF };
static volatile uint8_t evq[64][4];
static volatile uint32_t ev_w, ev_r;
static void post(uint32_t type, uint32_t ch, uint32_t note, uint32_t vel)
{
    uint32_t w = ev_w;
    if (w - ev_r >= 64u) return;                /* full: drop (never happens at human speed) */
    evq[w % 64u][0] = (uint8_t)type; evq[w % 64u][1] = (uint8_t)ch;
    evq[w % 64u][2] = (uint8_t)note; evq[w % 64u][3] = (uint8_t)vel;
    ev_w = w + 1u;                              /* last: the event is complete before the interrupt can see it */
}

/* ---- fixed-point helpers */
static int32_t sine(uint32_t ph)                /* a full cycle over 2^32 -> -32767..32767 (Dinghy's parabola) */
{
    int32_t x = (int32_t)ph >> 16, ax = x < 0 ? -x : x, y, ay;
    y = (x * (32768 - ax)) >> 13;
    ay = y < 0 ? -y : y;
    y += ((((y * ay) >> 15) - y) * 7373) >> 15;
    return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}
static int32_t log2_q12(uint32_t x)             /* log2(x) in Q12, x > 0 (no __builtin_clz: it may need libgcc) */
{
    int32_t n = 31;
    uint32_t m, i, r;
    if (!x) return -(32 << 12);
    while (!(x & 0x80000000u)) { x <<= 1; n--; }
    m = x << 1;                                 /* the mantissa's fraction, at the top */
    i = m >> 24; r = (m >> 16) & 255u;
    return n * 4096 + P5_LOG2_1PX[i] + (((int32_t)P5_LOG2_1PX[i + 1u] - (int32_t)P5_LOG2_1PX[i]) * (int32_t)r >> 8);
}
static uint32_t exp2_q30(int32_t y)             /* 2^(y / 4096) in Q30, y <= 0 */
{
    int32_t ip = y >> 12, sh;                   /* floor */
    uint32_t fr = (uint32_t)(y - ip * 4096), i = fr >> 4, v;
    v = P5_EXP2[i] + (((P5_EXP2[i + 1u] - P5_EXP2[i]) * (fr & 15u)) >> 4);   /* Q15, 1..2 */
    sh = 15 + ip;
    if (sh >= 0) return sh > 15 ? 0xFFFFFFFFu : v << sh;
    return sh <= -31 ? 0u : v >> -sh;
}
static int32_t polyblep(uint32_t t, uint32_t inc, uint32_t rcp)   /* Q15; t, inc over 2^32; rcp = 2^30 / (inc >> 12) */
{
    uint32_t u;
    int32_t x;
    if (t < inc) {
        x = (int32_t)(((t >> 12) * rcp) >> 15);
        if (x > 32767) x = 32767;
        return 2 * x - ((x * x) >> 15) - 32768;     /* 2x - x^2 - 1 */
    }
    u = 0u - t;
    if (u < inc) {
        x = (int32_t)(((u >> 12) * rcp) >> 15);
        if (x > 32767) x = 32767;
        x = 32768 - x;
        return (x * x) >> 15;                       /* (1 - y)^2 */
    }
    return 0;
}
#define MULS(a, b) ((int64_t)(a) * (int64_t)(b))   /* (64-bit multiply: fine; 64-bit divide: not there) */

/* ---- the voices (only the audio interrupt touches them) */
#define NV 12u
enum { S_IDLE, S_ATK, S_DEC, S_SUS, S_REL, S_KILL };
typedef struct {
    uint32_t ph, inc, rcp, age, noise;
    int32_t sus_q24;                            /* DECAY's floor */
    uint32_t fdec_q30, sh_cnt; int32_t sh_val;  /* the filter envelope's own fall (0: Poly5's); the cutoff's random steps */
    uint32_t ph2, r2_q12, drop_q16, dec_q30;    /* the second oscillator and its ratio; the pitch's fall; the decay a block */
    int32_t ic1, ic2, b0, b1, b2;               /* filter state, pink noise state */
    int32_t e, f;                               /* VCA and filter envelopes, Q24 (1.0 = 1 << 24) */
    uint32_t e_step, rel_q30, rel_left;
    int32_t amp, pk;                            /* the VCA now (Q15), its peak (Q15) */
    uint8_t stage, f_dec, ch, note;
    uint8_t p_ch, p_note, p_vel;                /* the note that takes over after a steal's fade */
} voice_t;
static voice_t voice[NV];
static uint32_t v_age;
static volatile uint8_t ch_active[NCH];         /* voices sounding per channel (for the screen) */
static volatile uint16_t ch_level[NCH];         /* each synth's output peak over the last buffer, Q15 (screen, diagnosis) */
static uint32_t pk_acc[NCH];
static uint32_t lfo_ph[NCH], lfo_inc[NCH], sh_per[NCH];   /* each synth's LFO (phase, step a block) and its noise's period in blocks */
static int32_t mono[HALF_FRAMES], dbus[HALF_FRAMES], rbus[HALF_FRAMES];   /* the dry mix, the delay and reverb sends */

/* ---- the master effects: a delay (one line, a lowpass in its feedback), a reverb (four combs, then two allpasses
 * a side: left and right take the combs with other signs, so it is wide), the DJ filter last. */
#define DL_N 65536u
#define CB0_N 1557u
#define CB1_N 1617u
#define CB2_N 1491u
#define CB3_N 1422u
#define AP0_N 556u
#define AP1_N 441u
#define AP2_N 579u
#define AP3_N 464u
static int16_t dl[DL_N] __attribute__((section(".pool")));
#define CH_N 2048u
static int16_t chb[CH_N] __attribute__((section(".pool")));   /* the chorus / flanger: a short line, read at two moving places */
static volatile uint32_t fx_ch_type, fx_ch_rate, fx_ch_depth, fx_ch_mix;
static uint32_t ch_w, ch_ph;
static int32_t ph_z[4];                         /* the phaser's four allpasses */
static volatile uint32_t fx_drv_type, fx_lofi_bits, fx_lofi_rate, fx_lofi_mix, fx_cmp_amt, fx_cmp_spd, fx_cmp_gain;
#define TP_N 1024u
static int16_t tpb[2][TP_N] __attribute__((section(".pool")));   /* LOFI: a short piece of tape a side, read a little behind, the distance wandering */
static volatile uint32_t fx_tp_wow, fx_tp_flut, fx_tp_age, fx_tp_fail;
static struct { uint32_t w, rng, wow_cnt, fl_ph, drop; int32_t wow, wow_t, d_q8, lp[2], hp[2], gain, gain_t; } tp = {0, 0x2545F491u, 0, 0, 0, 0, 0, 300 << 8, {0, 0}, {0, 0}, 32768, 32768};
static volatile uint32_t fx_gr = 32768;        /* the compressor's gain now, Q15 (the screen's meter) */
static struct { uint32_t cnt; int32_t l, r, env, g; } lf = {0, 0, 0, 0, 32768};
static int32_t cb0[CB0_N] __attribute__((section(".pool"))), cb1[CB1_N] __attribute__((section(".pool")));
static int32_t cb2[CB2_N] __attribute__((section(".pool"))), cb3[CB3_N] __attribute__((section(".pool")));
static int32_t ap0[AP0_N] __attribute__((section(".pool"))), ap1[AP1_N] __attribute__((section(".pool")));
static int32_t ap2[AP2_N] __attribute__((section(".pool"))), ap3[AP3_N] __attribute__((section(".pool")));
static volatile int32_t fx_dj;                  /* -100..100 */
static volatile uint32_t fx_drive;              /* 0..100: gain into a soft clipper */
static volatile uint32_t fx_mute;               /* bit 0: the delay takes nothing in; bit 1: the reverb */
static volatile uint32_t fx_reso, fx_dly, fx_rev, fx_dlen = 40000, fx_fb = 13107, fx_g = 26000, fx_damp = 10000;
static struct {
    uint32_t w, ci[4], ai[4], hp;
    int32_t lp, f[4], p, ic1[2], ic2[2];
} fx = {.p = 639 * 64};
#define COMB(buf, len, k) do { \
        int32_t y_ = buf[fx.ci[k]]; \
        fx.f[k] = y_ + (((fx.f[k] - y_) * damp4) >> 11); \
        buf[fx.ci[k]] = rin + ((fx.f[k] * g4) >> 11); \
        if (++fx.ci[k] >= len) fx.ci[k] = 0; \
        c##k = y_; \
    } while (0)
#define ALLP(buf, len, k, x) do { \
        int32_t b_ = buf[fx.ai[k]], o_ = b_ - (x); \
        buf[fx.ai[k]] = (x) + (b_ >> 1); \
        if (++fx.ai[k] >= len) fx.ai[k] = 0; \
        (x) = o_; \
    } while (0)

static uint32_t pow2_q12(int32_t e);
static void fx_render(int32_t *out, uint32_t n, uint32_t master)
{
    const int32_t dj = fx_dj;
    const uint32_t all_d = fx_dly, all_r = fx_rev, dlen = fx_dlen, fb = fx_fb, g = fx_g, damp = fx_damp, mute = fx_mute;
    const int32_t k = P5_KTAB[fx_reso * 30u / 100u], drv = (int32_t)fx_drive, dgain = 256 + drv * 20;
    /* (32-bit multiplies wherever the numbers allow: the chip's 64-bit ones cost several times as much) */
    const int32_t damp4 = (int32_t)(damp >> 4), g4 = (int32_t)(g >> 4), fb1 = (int32_t)(fb >> 1), master4 = (int32_t)(master >> 4);
    int32_t dj_open;
    int32_t a1 = 0, a2 = 0, a3 = 0, tp_, ch_base, ch_swing, ch_mixq, lmix, cthr, cratio, cmk, crel, twow, tflut, tlp, thp;
    uint32_t i, hp = dj > 0, ch_on, ch_fb, ch_inc, dtype, lbits, lhold, camt, cgain, tp_on, tfail;
    tp_ = dj < 0 ? (128 + 511 * (100 + dj) / 100) * 64 : dj > 0 ? dj * 448 / 100 * 64 : 639 * 64;
    if (hp != fx.hp) { fx.hp = hp; fx.p = tp_; }  /* (both ends are open: no sweep across the change) */
    dj_open = !dj && fx.p == tp_;               /* the DJ filter wide open: it is left out */
    dtype = fx_drv_type;
    {
        uint32_t w = fx_tp_wow > 100u ? 100u : fx_tp_wow, f = fx_tp_flut > 100u ? 100u : fx_tp_flut, a = fx_tp_age > 100u ? 100u : fx_tp_age;
        tfail = fx_tp_fail > 100u ? 100u : fx_tp_fail;
        tp_on = w | f | a | tfail;
        twow = (int32_t)(w * 410u);             /* up to 160 samples either way: the pitch some 2 % off */
        tflut = (int32_t)(f * 31u);             /* up to 12 samples: a tremble of 1.5 % */
        tlp = 32000 - (int32_t)a * 268;         /* a lowpass from 18 kHz down to 1.2 kHz */
        thp = 60 + (int32_t)a * 18;             /* a highpass from 15 Hz up to 400 Hz */
    }
    lmix = (int32_t)(fx_lofi_mix > 100u ? 128u : fx_lofi_mix * 128u / 100u);
    lbits = (fx_lofi_bits > 100u ? 100u : fx_lofi_bits) * 11u / 100u;       /* 0 .. 11 bits fewer */
    lhold = 1u + (fx_lofi_rate > 100u ? 100u : fx_lofi_rate) * 39u / 100u;  /* 44.1 kHz .. 1.1 kHz */
    camt = fx_cmp_amt > 100u ? 100u : fx_cmp_amt;
    cgain = fx_cmp_gain > 100u ? 100u : fx_cmp_gain;
    cthr = 24000 - (int32_t)camt * 200;                                      /* down to 4000 */
    cratio = 1 + (int32_t)camt / 12;                                         /* up to 9 : 1 */
    cmk = 256 + (int32_t)cgain * 8;                                          /* up to 4 times */
    crel = 32768 - (int32_t)(5000u / (2u + (fx_cmp_spd > 100u ? 100u : fx_cmp_spd)));   /* a block: 30 ms .. 1 s to let go */
    {
        uint32_t ty = fx_ch_type, dp = fx_ch_depth > 100u ? 100u : fx_ch_depth, rt = fx_ch_rate > 100u ? 100u : fx_ch_rate;
        ch_on = ty && fx_ch_mix ? ty : 0u; ch_fb = ty == 2u;
        ch_base = ty == 2u ? 110 << 8 : 660 << 8;                       /* in samples x 256 */
        ch_swing = (int32_t)(dp * (ty == 3u ? 240u : ty == 2u ? 230u : 900u));   /* up to 90 samples, or 350; the phaser: its corner */
        ch_inc = pow2_q12((int32_t)(54272u + rt * 259u));               /* 0.1 Hz .. 8 Hz */
        ch_mixq = (int32_t)(fx_ch_mix > 100u ? 128u : fx_ch_mix * 128u / 100u);
    }
    for (i = 0; i < n; i++) {
        int32_t dry = mono[i], din, dout, rin, c0, c1, c2, c3, l, r, wl, wr, chl = 0, chr = 0;
        if (!(i & (P5_BLK - 1u))) {             /* the DJ filter's cutoff glides; its coefficients once a block */
            int32_t d = tp_ - fx.p, gg, gk;
            uint32_t ix, fr;
            fx.p += d > 256 ? 256 : d < -256 ? -256 : d;
            ix = (uint32_t)fx.p >> 6; fr = (uint32_t)fx.p & 63u;
            gg = P5_GTAB[ix] + (((int32_t)P5_GTAB[ix + 1u] - (int32_t)P5_GTAB[ix]) * (int32_t)fr >> 6);
            gk = (gg * (gg + k)) >> 12;
            a1 = (int32_t)((1u << 27) / (uint32_t)(4096 + gk));
            a2 = (gg * a1) >> 12;
            a3 = (gg * a2) >> 12;
        }
        if (ch_on == 3u) {                      /* phaser: four allpasses, their corner swept: notches that move */
            int32_t a = 5000 + (int32_t)(MULS(sine(ch_ph += ch_inc) + 32768, ch_swing) >> 16), x = dry, y, k4;
            for (k4 = 0; k4 < 4; k4++) {
                y = ph_z[k4] - (int32_t)(MULS(a, x) >> 15);
                ph_z[k4] = x + (int32_t)(MULS(a, y) >> 15);
                x = y;
            }
            chl = chr = ((x - dry) >> 1) * ch_mixq >> 7;
        } else
        if (ch_on) {                            /* chorus (15 ms, swinging 8) or flanger (2.5 ms, swinging 2, fed back): left and right opposite */
            int32_t lf = sine(ch_ph += ch_inc), tap, i0, fr, a, b2;
            tap = ch_base + (int32_t)(MULS(lf, ch_swing) >> 15);
            i0 = (int32_t)((ch_w << 8) - (uint32_t)tap) >> 8; fr = (int32_t)(((ch_w << 8) - (uint32_t)tap) & 255u);
            a = chb[(uint32_t)i0 & (CH_N - 1u)]; b2 = chb[(uint32_t)(i0 + 1) & (CH_N - 1u)];
            chl = (a + ((b2 - a) * fr >> 8)) << 2;   /* (the line holds a quarter of the level: a chord goes over 16 bits) */
            tap = ch_base - (int32_t)(MULS(lf, ch_swing) >> 15);
            i0 = (int32_t)((ch_w << 8) - (uint32_t)tap) >> 8; fr = (int32_t)(((ch_w << 8) - (uint32_t)tap) & 255u);
            a = chb[(uint32_t)i0 & (CH_N - 1u)]; b2 = chb[(uint32_t)(i0 + 1) & (CH_N - 1u)];
            chr = (a + ((b2 - a) * fr >> 8)) << 2;
            a = (dry + (ch_fb ? chl * 5 >> 3 : 0)) >> 2;
            chb[ch_w++ & (CH_N - 1u)] = (int16_t)(a > 32767 ? 32767 : a < -32767 ? -32767 : a);
            chl = chl * ch_mixq >> 7; chr = chr * ch_mixq >> 7;
        }
        /* the delay */
        din = mute & 1u ? 0 : dbus[i] + (dry * (int32_t)all_d >> 7);
        dout = dl[(fx.w - dlen) & (DL_N - 1u)];
        fx.lp += ((dout - fx.lp) * 4450) >> 13;                         /* 5.5 kHz in the feedback, as Poly5 */
        din += (fx.lp * fb1) >> 14;
        dl[fx.w++ & (DL_N - 1u)] = (int16_t)(din > 32767 ? 32767 : din < -32767 ? -32767 : din);
        /* the reverb (the delay's repeats go into it a little too) */
        rin = mute & 2u ? 0 : (rbus[i] + (dry * (int32_t)all_r >> 7) + (dout >> 2)) >> 2;
        if (rin > 60000) rin = 60000; else if (rin < -60000) rin = -60000;
        COMB(cb0, CB0_N, 0); COMB(cb1, CB1_N, 1); COMB(cb2, CB2_N, 2); COMB(cb3, CB3_N, 3);
        wl = (c0 + c1 + c2 + c3) * 5 >> 3; wr = (c0 - c1 + c2 - c3) * 5 >> 3;
        ALLP(ap0, AP0_N, 0, wl); ALLP(ap1, AP1_N, 1, wl);
        ALLP(ap2, AP2_N, 2, wr); ALLP(ap3, AP3_N, 3, wr);
        dout = (dout * 13107) >> 14;                                    /* 0.8, as Poly5 */
        l = dry + dout + wl + chl; r = dry + dout + wr + chr;
        if (l > 1000000) l = 1000000; else if (l < -1000000) l = -1000000;
        if (r > 1000000) r = 1000000; else if (r < -1000000) r = -1000000;
        l = (l * master4) >> 11; r = (r * master4) >> 11;
        if (drv && dtype) {                     /* DRIVE, hard: cut off flat; fold: what goes over the top comes back down */
            int32_t x;
            x = l * dgain >> 8;
            if (dtype == 2u) { if (x > 18000) x = 36000 - x; else if (x < -18000) x = -36000 - x; }
            l = (x > 18000 ? 18000 : x < -18000 ? -18000 : x) * 3 >> 2;
            x = r * dgain >> 8;
            if (dtype == 2u) { if (x > 18000) x = 36000 - x; else if (x < -18000) x = -36000 - x; }
            r = (x > 18000 ? 18000 : x < -18000 ? -18000 : x) * 3 >> 2;
        } else
        if (drv) {                              /* DRIVE: up to 9 times louder into a clipper that rounds, then flattens */
            int32_t x, t3;
            x = l * dgain >> 8; if (x > 27000) x = 27000; else if (x < -27000) x = -27000;
            t3 = ((x * x) >> 15) * x;           /* x - x^3 / (3 a^2), a = 27000: flat at 18000 */
            l = x - (int32_t)(MULS(t3, 16089) >> 30);
            x = r * dgain >> 8; if (x > 27000) x = 27000; else if (x < -27000) x = -27000;
            t3 = ((x * x) >> 15) * x;
            r = x - (int32_t)(MULS(t3, 16089) >> 30);
            l = l * 3 >> 2; r = r * 3 >> 2;     /* (driven is loud: a little back) */
        }
        if (lmix) {                             /* LOFI: the sample held for a while (a lower rate), in coarse steps (fewer bits) */
            if (++lf.cnt >= lhold) {
                lf.cnt = 0;
                lf.l = ((l >> lbits) << lbits) + (1 << lbits >> 1);
                lf.r = ((r >> lbits) << lbits) + (1 << lbits >> 1);
            }
            l += (lf.l - l) * lmix >> 7; r += (lf.r - r) * lmix >> 7;
        }
        if (tp_on) {                            /* LOFI: tape. The pitch drifts (wow) and trembles (flutter); worn: dull and thin; it fails: drops out, crackling */
            int32_t k2;
            if (!(i & (P5_BLK - 1u))) {         /* where the tape is read from: once a block */
                if (!tp.wow_cnt--) {            /* wow: now and then a new place to drift to */
                    tp.rng ^= tp.rng << 13; tp.rng ^= tp.rng >> 17; tp.rng ^= tp.rng << 5;
                    tp.wow_t = (int32_t)tp.rng >> 16;
                    tp.wow_cnt = 600u + (tp.rng & 1023u);                    /* every 0.2 .. 0.6 s */
                    if (tfail && (tp.rng >> 10 & 255u) < tfail * 160u / 100u) {   /* and it lets go for a moment: at FAIL 10 about every 6 s, at 100 more than once a second */
                        tp.gain_t = 6000 + (int32_t)(tp.rng >> 20 & 8191u);
                        tp.drop = 60u + (tp.rng >> 24);                      /* 20 .. 110 ms */
                    }
                }
                tp.wow += (tp.wow_t - tp.wow) >> 9;
                tp.fl_ph += 14000000u;                                       /* 9 Hz */
                tp.d_q8 = (300 << 8) + (int32_t)(MULS(tp.wow, twow) >> 15) + (int32_t)(MULS(sine(tp.fl_ph), tflut) >> 15);
                if (tp.drop && !--tp.drop) tp.gain_t = 32768;
                tp.gain += (tp.gain_t - tp.gain) >> 3;
            }
            { int32_t tl = l >> 1, tr = r >> 1;   /* (half the level on the tape: loud chords are over 16 bits) */
              tpb[0][tp.w & (TP_N - 1u)] = (int16_t)(tl > 32767 ? 32767 : tl < -32767 ? -32767 : tl);
              tpb[1][tp.w & (TP_N - 1u)] = (int16_t)(tr > 32767 ? 32767 : tr < -32767 ? -32767 : tr); }
            for (k2 = 0; k2 < 2; k2++) {
                uint32_t pos = (tp.w << 8) - (uint32_t)tp.d_q8;
                int32_t a = tpb[k2][(pos >> 8) & (TP_N - 1u)], b2 = tpb[k2][((pos >> 8) + 1u) & (TP_N - 1u)];
                int32_t x = (a + ((b2 - a) * (int32_t)(pos & 255u) >> 8)) << 1;
                tp.lp[k2] += ((x - tp.lp[k2]) * (tlp >> 3)) >> 12;          /* dull */
                tp.hp[k2] += ((tp.lp[k2] - tp.hp[k2]) * thp) >> 15;
                x = ((tp.lp[k2] - tp.hp[k2]) * (tp.gain >> 3)) >> 12;       /* thin; and the dropout */
                if (tfail && tp.drop) { tp.rng ^= tp.rng << 13; tp.rng ^= tp.rng >> 17; tp.rng ^= tp.rng << 5; x += (int32_t)(MULS(x, (int32_t)tp.rng >> 16) >> 15) * (int32_t)tfail / 60; }   /* while it drops out the sound crackles (grit on the sound itself: silence stays silent, no noise floor) */
                if (k2) r = x; else l = x;
            }
            tp.w++;
        }
        if (dj_open) { fx.ic1[0] = fx.ic1[1] = 0; fx.ic2[0] = l; fx.ic2[1] = r; }   /* (ready for when it closes) */
        else {                                  /* the DJ filter: lowpass to the left, highpass to the right */
            int32_t v3 = l - fx.ic2[0];
            int32_t v1 = (int32_t)((MULS(a1, fx.ic1[0]) + MULS(a2, v3)) >> 15);
            int32_t v2 = fx.ic2[0] + (int32_t)((MULS(a2, fx.ic1[0]) + MULS(a3, v3)) >> 15);
            fx.ic1[0] = 2 * v1 - fx.ic1[0]; fx.ic2[0] = 2 * v2 - fx.ic2[0];
            l = hp ? l - (int32_t)(MULS(k, v1) >> 12) - v2 : v2;
            v3 = r - fx.ic2[1];
            v1 = (int32_t)((MULS(a1, fx.ic1[1]) + MULS(a2, v3)) >> 15);
            v2 = fx.ic2[1] + (int32_t)((MULS(a2, fx.ic1[1]) + MULS(a3, v3)) >> 15);
            fx.ic1[1] = 2 * v1 - fx.ic1[1]; fx.ic2[1] = 2 * v2 - fx.ic2[1];
            r = hp ? r - (int32_t)(MULS(k, v1) >> 12) - v2 : v2;
        }
        if (camt | cgain) {                     /* MASTERING: a compressor (what is over the threshold rises 1/ratio as fast), make-up gain */
            int32_t a = l < 0 ? -l : l, b2 = r < 0 ? -r : r;
            if (b2 > a) a = b2;
            if (a > lf.env) lf.env = a > 120000 ? 120000 : a;
            if (!(i & (P5_BLK - 1u))) {
                int32_t tg = 32768;
                if (camt && lf.env > cthr) tg = (int32_t)((((uint32_t)(cthr + (lf.env - cthr) / cratio)) << 15) / (uint32_t)lf.env);
                lf.g += (tg - lf.g) >> 1;
                lf.env = (int32_t)(MULS(lf.env, crel) >> 15);
            }
            if (l > 250000) l = 250000; else if (l < -250000) l = -250000;
            if (r > 250000) r = 250000; else if (r < -250000) r = -250000;
            l = ((l * (lf.g >> 3)) >> 12) * cmk >> 8;
            r = ((r * (lf.g >> 3)) >> 12) * cmk >> 8;
        }
        /* a soft ceiling: above 60 % the level rises a quarter as fast (five synths in unison stay clean): the limiter */
        if (l > 20000) l = 20000 + ((l - 20000) >> 2); else if (l < -20000) l = -20000 + ((l + 20000) >> 2);
        if (r > 20000) r = 20000 + ((r - 20000) >> 2); else if (r < -20000) r = -20000 + ((r + 20000) >> 2);
        out[2u * i] += l;
        out[2u * i + 1u] += r;
    }
    fx_gr = (uint32_t)lf.g;
}

static const uint32_t INC_C9[12] = {            /* MIDI 120..131; lower octaves shift right (Dinghy) */
    815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
    1153098554u, 1221665363u, 1294309365u, 1371273005u, 1452813141u, 1539201906u};

static void voice_start(voice_t *v, uint32_t ch, uint32_t note, uint32_t vel)
{
    int32_t n = (int32_t)note + 12 * chan[ch].octave;
    uint32_t atk = chan[ch].attack * P5_ATK_PER_UNIT_Q8 >> 8, d;
    while (n > 131) n -= 12;
    if (n < 0) n += 12 * ((-n + 11) / 12);
    v->inc = INC_C9[n % 12] >> (10 - n / 12);
    d = v->inc >> 12;
    v->rcp = (1u << 30) / (d ? d : 1u);
    v->ch = (uint8_t)ch; v->note = (uint8_t)note;
    v->pk = (int32_t)((chan[ch].vol * vel * P5_PK_Q15) >> 16);
    if (chan[ch].wave == W_SINE) v->pk = v->pk * 7 / 4;         /* the soft waves, as loud to the ear as saw and square */
    else if (chan[ch].wave == W_TRI || chan[ch].wave == W_FM) v->pk = v->pk * 3 / 2;
    else if (chan[ch].wave == W_METAL) v->pk = v->pk * 3 / 4;
    {
        /* FM: the modulator's ratio: 1 2 3 4 (clean), then 1.5 3.5 1.41 2.76 5.33 7.1 (bells, clangs), Q12 */
        static const uint16_t FM_RATIO[10] = {4096, 8192, 12288, 16384, 6144, 14336, 5793, 11305, 21832, 29082};
        uint32_t tone = chan[ch].tone > 100u ? 100u : chan[ch].tone, dec = chan[ch].decay ? len_of(chan[ch].decay) : 0u;
        if (chan[ch].decay && !dec) dec = 1;     /* (it still dies by itself) */
        v->r2_q12 = chan[ch].wave == W_FM ? FM_RATIO[tone * 10u / 101u]
                  : chan[ch].wave == W_METAL ? 4915u + tone * 131u          /* 1.2 .. 4.4 */
                  : 4096u + chan[ch].detune;                                /* up to 2.4 %: 42 cents */
        v->ph2 = 0;
        v->drop_q16 = chan[ch].drop * 1966u;                                /* 100: two octaves up, and down */
        if (chan[ch].env_rate) {                /* the filter's envelope at a speed of its own: to nothing in 20 ms .. 3 s */
            uint32_t r = chan[ch].env_rate, blocks = 55u + r * r * 4u / 5u;
            v->fdec_q30 = exp2_q30(-40822 / (int32_t)blocks);
        } else
            v->fdec_q30 = 0;
        v->sh_cnt = 0; v->sh_val = 0;
        v->sus_q24 = (int32_t)((chan[ch].sus > 100u ? 100u : chan[ch].sus) * 167772u);
        if (dec) {                              /* to a thousandth (-60 dB) in 30 ms .. 3 s */
            uint32_t blocks = 80u + dec * dec * 4u / 5u;
            v->dec_q30 = exp2_q30(-40822 / (int32_t)blocks);
        } else
            v->dec_q30 = 0;
    }
    if (atk < P5_ATK_MIN_BLOCKS) atk = P5_ATK_MIN_BLOCKS;
    v->e_step = (1u << 24) / atk;
    v->e = 0; v->f = 0; v->f_dec = 0; v->amp = 0;
    v->ic1 = v->ic2 = 0;
    if (!v->noise) v->noise = 0x9E3779B9u ^ (uint32_t)(v - voice) * 2654435761u;
    v->age = ++v_age;
    v->stage = S_ATK;
}

static void voice_release(voice_t *v)
{
    uint32_t c = v->ch, rel = len_of(chan[c].release) * P5_REL_PER_UNIT_Q8 >> 8;
    int32_t lv, step;
    if (rel < P5_REL_MIN_BLOCKS) rel = P5_REL_MIN_BLOCKS;
    /* Poly5: an exponential ramp from the level now to 0.0001 over the release time */
    lv = v->e > 0 && v->pk > 0 ? log2_q12((uint32_t)v->pk) + log2_q12((uint32_t)v->e) - 39 * 4096 : P5_LOG2_FLOOR_Q12;
    step = lv > P5_LOG2_FLOOR_Q12 ? (P5_LOG2_FLOOR_Q12 - lv) / (int32_t)rel : -4096;
    v->rel_q30 = exp2_q30(step);
    v->rel_left = rel;
    v->stage = S_REL;
}

static void note_on(uint32_t ch, uint32_t note, uint32_t vel)
{
    uint32_t i, pick = 0, best = 0xFFFFFFFFu;
    for (i = 0; i < NV; i++) {                  /* free, else the quietest releasing, else the oldest */
        voice_t *v = &voice[i];
        uint32_t score = v->stage == S_IDLE ? 0u
                       : v->stage == S_REL ? 0x10000000u + (uint32_t)v->amp
                       : 0x80000000u + (v->age & 0x7FFFFFFu);
        if (score < best) { best = score; pick = i; }
    }
    if (voice[pick].stage == S_IDLE || voice[pick].amp < 64)
        voice_start(&voice[pick], ch, note, vel);
    else {                                      /* fade the old note out over a block, then start (no click) */
        voice[pick].p_ch = (uint8_t)ch; voice[pick].p_note = (uint8_t)note; voice[pick].p_vel = (uint8_t)vel;
        voice[pick].stage = S_KILL;
    }
}

static void note_off(uint32_t ch, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NV; i++) {
        voice_t *v = &voice[i];
        if (v->ch == ch && v->note == note && (v->stage == S_ATK || v->stage == S_DEC || v->stage == S_SUS))
            voice_release(v);
        else if (v->stage == S_KILL && v->p_ch == ch && v->p_note == note)
            v->p_vel = 0;                       /* let go before it started: the fade just ends */
    }
}

/* the envelopes and the filter's coefficients, once a block */
static volatile uint32_t lfo_tempo = 118, lfo_restart;   /* the sequencer's tempo; its start: the synced LFOs begin again */
static int32_t lfo_val(uint32_t ph, uint32_t shape)   /* Q15: sine, triangle, saw (falling), square */
{
    int32_t x = (int32_t)(ph >> 16) - 32768;    /* -32768 .. 32767 over the cycle */
    switch (shape) {
    case 1: return (x < 0 ? -x : x) * 2 - 32768 < -32767 ? -32767 : 32767 - (x < 0 ? -x : x) * 2;
    case 2: return -x > 32767 ? 32767 : -x;
    case 3: return x < 0 ? 32767 : -32767;
    default: return sine(ph);
    }
}
static uint32_t pow2_q12(int32_t e)             /* 2^(e / 4096), e >= 0, as an integer */
{
    uint32_t ip = (uint32_t)e >> 12, fr = (uint32_t)e & 4095u, v = P5_EXP2[fr >> 4];
    return ip >= 15u ? v << (ip - 15u) : v >> (15u - ip);
}
static void voice_control(voice_t *v, int32_t *a1, int32_t *a2, int32_t *a3, int32_t *kq, uint32_t blk)
{
    const uint32_t c = v->ch;
    uint32_t env_filt = chan[c].env_filt, cutoff = chan[c].cutoff, reso = chan[c].reso, ld = chan[c].lfo_depth, nd = chan[c].nz_depth;
    int32_t p, g, k, gk;
    uint32_t i, fr;

    switch (v->stage) {
    case S_ATK:                                 /* linear to the peak; the filter envelope goes with it */
        v->e += (int32_t)v->e_step;
        if (v->e >= 1 << 24) { v->e = 1 << 24; v->stage = S_DEC; }
        v->f = v->e;
        break;
    case S_DEC:                                 /* exponential to 0.7 over 80 ms, then hold */
        if (v->dec_q30) {                       /* (DECAY: no hold: it dies by itself, and gives its voice back) */
            v->e = v->sus_q24 + (int32_t)(MULS(v->e - v->sus_q24, v->dec_q30) >> 30);   /* down to SUSTAIN: held there; to nothing: done */
            if (!v->sus_q24) { if (v->e < 4096) { v->e = 0; v->stage = S_IDLE; } }
            else if (v->e - v->sus_q24 < 4096) { v->e = v->sus_q24; v->stage = S_SUS; }
            break;
        }
        v->e = (int32_t)(MULS(v->e, P5_DEC_VCA_Q30) >> 30);
        if (v->e <= (int32_t)P5_SUS_VCA_Q24) { v->e = (int32_t)P5_SUS_VCA_Q24; v->stage = S_SUS; }
        break;
    case S_REL:
        v->e = (int32_t)(MULS(v->e, v->rel_q30) >> 30);
        if (!--v->rel_left) { v->e = 0; v->stage = S_IDLE; }
        break;
    default:
        break;
    }
    if (v->stage != S_ATK && v->fdec_q30)        /* the filter envelope at its own speed: all the way down */
        v->f = (int32_t)(MULS(v->f, v->fdec_q30) >> 30);
    else if (v->stage != S_ATK && !v->f_dec) {   /* Poly5's: to 0.4 over 80 ms, then hold (also in release) */
        v->f = (int32_t)(MULS(v->f, P5_DEC_FLT_Q30) >> 30);
        if (v->f <= (int32_t)P5_SUS_FLT_Q24) { v->f = (int32_t)P5_SUS_FLT_Q24; v->f_dec = 1; }
    }

    /* cutoff: cHz * (1 + envFilt * 0.9 * f), as log2 above 20 Hz in Q12 */
    p = (int32_t)(cutoff * P5_CUT_PER_UNIT_Q12);
    if (ld)                                      /* an LFO: up and down, as far as 2.5 octaves each way */
        p += (lfo_val(lfo_ph[c] + lfo_inc[c] * blk, MSHAPE(c)) * (int32_t)(ld * 102u)) >> 15;
    if (nd) {                                    /* noise: a new random place, again and again */
        if (!v->sh_cnt) {
            v->noise ^= v->noise << 13; v->noise ^= v->noise >> 17; v->noise ^= v->noise << 5;
            v->sh_val = (int32_t)v->noise >> 16;
            v->sh_cnt = sh_per[c];
        }
        v->sh_cnt--;
        p += (v->sh_val * (int32_t)(nd * 102u)) >> 15;
    }
    if (p < 0) p = 0;
    if (env_filt && v->f > 0) {                  /* the envelope: cHz * (1 + envFilt * 0.9 * f), as Poly5 */
        uint32_t x = ((uint32_t)v->f >> 8) * env_filt * 9u / 1000u;   /* Q16, <= 0.9 */
        i = x >> 8; fr = x & 255u;
        p += P5_LOG2_1PX[i] + (((int32_t)P5_LOG2_1PX[i + 1u] - (int32_t)P5_LOG2_1PX[i]) * (int32_t)fr >> 8);
    }
    if (p > 640 * 64 - 1) p = 640 * 64 - 1;
    i = (uint32_t)p >> 6; fr = (uint32_t)p & 63u;
    g = P5_GTAB[i] + (((int32_t)P5_GTAB[i + 1u] - (int32_t)P5_GTAB[i]) * (int32_t)fr >> 6);
    k = P5_KTAB[reso > 30u ? 30u : reso];
    gk = (g * (g + k)) >> 12;
    *a1 = (int32_t)((1u << 27) / (uint32_t)(4096 + gk));           /* Q15 */
    *a2 = (g * *a1) >> 12;
    *a3 = (g * *a2) >> 12;
    *kq = k;
}

static void voice_render(voice_t *v, int32_t *dst, uint32_t n)
{
    int32_t buf[P5_BLK];
    uint32_t b, i;
    for (b = 0; b < n; b += P5_BLK) {
        const uint32_t c = v->ch, wave = chan[c].wave, noise = chan[c].noise;
        int32_t a1, a2, a3, kq, tgt, da, amp;
        uint32_t ph = v->ph, inc = v->inc, rcp = v->rcp, ph2 = v->ph2, inc2;

        voice_control(v, &a1, &a2, &a3, &kq, b / P5_BLK);   /* (a steal's fade: the envelope stands still, the VCA goes to 0) */
        tgt = v->stage == S_KILL ? v->amp * 5 / 8      /* a steal: the old note down over some 4 ms (faster clicks) */
                                 : (int32_t)((MULS(v->pk, v->e >> 9)) >> 15);

        if (v->drop_q16) {                      /* DROP: the pitch comes down from above, fast */
            uint32_t d;
            inc += (uint32_t)(((uint64_t)inc * v->drop_q16) >> 16);
            if (inc > 0x60000000u) inc = 0x60000000u;
            d = inc >> 12;
            rcp = (1u << 30) / (d ? d : 1u);
            v->drop_q16 = (uint32_t)(((uint64_t)v->drop_q16 * 64553u) >> 16);   /* x 0.985 a block: 24 ms */
            if (v->drop_q16 < 48u) v->drop_q16 = 0;
        }
        inc2 = (uint32_t)(((uint64_t)inc * v->r2_q12) >> 12);
        if (inc2 > 0x70000000u) inc2 = 0x70000000u;

        switch (wave) {                         /* the oscillator, a block of it */
        case W_SAW:
            for (i = 0; i < P5_BLK; i++, ph += inc) {
                int32_t s = (int32_t)(ph >> 16) - 32768;
                if (ph < inc || (0u - ph) < inc) s -= polyblep(ph, inc, rcp);   /* (only next to the edge) */
                buf[i] = s;
            }
            break;
        case W_SQUARE:
            for (i = 0; i < P5_BLK; i++, ph += inc) {
                uint32_t p2 = ph ^ 0x80000000u;
                int32_t s = ph < 0x80000000u ? 32767 : -32768;
                if (ph < inc || (0u - ph) < inc) s += polyblep(ph, inc, rcp);
                if (p2 < inc || (0u - p2) < inc) s -= polyblep(p2, inc, rcp);
                buf[i] = s;
            }
            break;
        case W_TRI:
            for (i = 0; i < P5_BLK; i++, ph += inc) {
                int32_t s = (int32_t)(ph >> 16) - 32768;
                buf[i] = (s < 0 ? -s : s) * 2 - 32768;
            }
            break;
        case W_FM: {                            /* a sine, its phase pushed by another: the filter's envelope is the push */
            int32_t k = 5500 + (int32_t)((MULS(v->f >> 9, chan[c].env_filt * 590u)) >> 15);   /* 0.26 .. 3.1 radians */
            if (k > 64000) k = 64000;
            for (i = 0; i < P5_BLK; i++, ph += inc, ph2 += inc2)
                buf[i] = sine(ph + (uint32_t)(sine(ph2) * k));
            break;
        }
        case W_METAL:                           /* two squares against each other: where they differ */
            for (i = 0; i < P5_BLK; i++, ph += inc, ph2 += inc2)
                buf[i] = ((ph ^ ph2) & 0x80000000u) ? 28000 : -28000;
            break;
        default:
            for (i = 0; i < P5_BLK; i++, ph += inc)
                buf[i] = sine(ph);
            break;
        }
        if (wave <= W_SINE && v->r2_q12 != 4096u) {   /* DETUNE: the same wave again, a little off; half of each */
            uint32_t d = inc2 >> 12, rcp2 = (1u << 30) / (d ? d : 1u);
            for (i = 0; i < P5_BLK; i++, ph2 += inc2) {
                int32_t s2;
                if (wave == W_SAW) { s2 = (int32_t)(ph2 >> 16) - 32768; if (ph2 < inc2 || (0u - ph2) < inc2) s2 -= polyblep(ph2, inc2, rcp2); }
                else if (wave == W_SQUARE) {
                    uint32_t p2 = ph2 ^ 0x80000000u;
                    s2 = ph2 < 0x80000000u ? 32767 : -32768;
                    if (ph2 < inc2 || (0u - ph2) < inc2) s2 += polyblep(ph2, inc2, rcp2);
                    if (p2 < inc2 || (0u - p2) < inc2) s2 -= polyblep(p2, inc2, rcp2);
                }
                else if (wave == W_TRI) { s2 = (int32_t)(ph2 >> 16) - 32768; s2 = (s2 < 0 ? -s2 : s2) * 2 - 32768; }
                else s2 = sine(ph2);
                buf[i] = (buf[i] + s2) >> 1;
            }
        }
        v->ph2 = ph2;
        v->ph = ph;

        if (noise) {                            /* Poly5's pink noise, louder: Poly5's noise/100 * 0.5 is hard to hear */
            int32_t gn = (int32_t)(noise * 786u), b0 = v->b0, b1 = v->b1, b2 = v->b2;   /* 100 -> 2.4: the oscillator's level */
            uint32_t x = v->noise;
            for (i = 0; i < P5_BLK; i++) {
                int32_t w, pk;
                x ^= x << 13; x ^= x >> 17; x ^= x << 5;
                w = (int32_t)x >> 16;
                b0 = (b0 * 1022 + w * 101) >> 10;                   /* (Poly5's three poles, in 32 bits: Q10) */
                b1 = (b1 * 986 + w * 306) >> 10;
                b2 = (b2 * 584 + w * 1953) >> 10;
                pk = ((b0 + b1 + b2 + ((w * 184) >> 10)) * 113) >> 10;
                buf[i] += (pk * (gn >> 5)) >> 10;
            }
            v->noise = x; v->b0 = b0; v->b1 = b1; v->b2 = b2;
        }

        /* the filter (TPT state-variable lowpass, Q15 coefficients) and the VCA, ramped over the block */
        amp = v->amp;
        da = (tgt - amp) >> 4;                  /* (a block is 16) */
        {
            int32_t ic1 = v->ic1, ic2 = v->ic2, o, pkv = 0, sd = c < NCH ? chan[c].dly : 0, sr = c < NCH ? chan[c].rev : 0;
            int32_t sg = c < NCH && chan[c].sat ? 256 + chan[c].sat * 20 : 0;   /* saturation: up to 9 times into a clipper */
            const uint32_t ft = c < NCH ? FTYPE(c) : 0u;
            const int32_t q1 = a1 >> 2, q2 = a2 >> 2, q3 = a3 >> 2;
            for (i = 0; i < P5_BLK; i++) {
                int32_t x4 = buf[i] >> 2, v3 = x4 - ic2;
                int32_t v1 = ((q1 * ic1) >> 13) + ((q2 * v3) >> 13);
                int32_t v2 = ic2 + ((q2 * ic1) >> 13) + ((q3 * v3) >> 13), fo;
                ic1 = 2 * v1 - ic1;
                ic2 = 2 * v2 - ic2;
                if (ic1 > 250000) ic1 = 250000; else if (ic1 < -250000) ic1 = -250000;   /* (a resonance gone wild: held) */
                if (ic2 > 250000) ic2 = 250000; else if (ic2 < -250000) ic2 = -250000;
                amp += da;
                fo = !ft ? v2 : ft == 1u ? v1 : x4 - ((kq * v1) >> 12) - v2;   /* lowpass, bandpass, highpass */
                if (fo > 100000) fo = 100000; else if (fo < -100000) fo = -100000;
                if (sg) {                       /* this synth's saturation: rounds, then flattens; the level made up */
                    int32_t x = (fo << 2) * sg >> 8, t3;
                    if (x > 27000) x = 27000; else if (x < -27000) x = -27000;
                    t3 = ((x * x) >> 15) * x;
                    x = (x - (((t3 >> 15) * 16089) >> 15)) * 3 / 2;
                    o = (x * amp) >> 15;
                } else
                    o = (fo * amp) >> 13;       /* (>> 15, and the quarter back) */
                dst[b + i] += o;
                if (sd) dbus[b + i] += o * sd >> 7;                 /* this synth's own sends */
                if (sr) rbus[b + i] += o * sr >> 7;
                if (o < 0) o = -o;
                if (o > pkv) pkv = o;
            }
            v->ic1 = ic1; v->ic2 = ic2;
            if (c < NCH && (uint32_t)pkv > pk_acc[c]) pk_acc[c] = (uint32_t)pkv;
        }
        v->amp = tgt;

        if (v->stage == S_KILL && tgt < 48) {   /* faded out: the new note takes over (or nothing, if let go) */
            if (v->p_vel) voice_start(v, v->p_ch, v->p_note, v->p_vel);
            else v->stage = S_IDLE;
        }
        if (v->stage == S_IDLE) {
            v->amp = 0;
            return;
        }
    }
}

static volatile uint32_t perf_max_us, perf_sum_us, perf_n;   /* the audio interrupt's time for a buffer (of 5805 us): the worst, the sum, how many */
static void app_audio(int32_t *out, uint32_t n)
{
    uint32_t i, r = ev_r, c, m;
#ifndef DINGHY_HOST
    uint32_t t0 = fm1_ticks();
#endif
    uint8_t act[NCH] = {0};
    while (r != ev_w) {                         /* the notes posted since the last call */
        const volatile uint8_t *e = evq[r % 64u];
        if (e[0] == EV_ON) note_on(e[1], e[2], e[3]);
        else note_off(e[1], e[2]);
        r++;
    }
    ev_r = r;
    if (n > HALF_FRAMES) n = HALF_FRAMES;
    for (c = 0; c < NCH; c++) {                 /* each synth's LFO: 0.1 Hz .. 20 Hz; its noise: a step a second .. every block */
        uint32_t r = chan[c].lfo_rate > 100u ? 100u : chan[c].lfo_rate, nr = chan[c].nz_rate > 100u ? 100u : chan[c].nz_rate, per;
        static const uint8_t CPQ_Q4[9] = {1, 2, 4, 8, 16, 32, 48, 64, 128};   /* cycles a quarter note x 16: 4 bars .. 1/32 */
        lfo_ph[c] += lfo_inc[c] * (n / P5_BLK);
        if (lfo_restart) lfo_ph[c] = 0;
        lfo_inc[c] = MSYNC(c) ? CPQ_Q4[r * 9u / 101u] * lfo_tempo * 1623u   /* a note value of the tempo */
                              : pow2_q12((int32_t)(70656u + r * 313u));
        per = pow2_q12((int32_t)(46817u - nr * 458u));
        sh_per[c] = per ? per : 1u;
    }
    lfo_restart = 0;
    for (i = 0; i < n; i++) mono[i] = dbus[i] = rbus[i] = 0;
    for (c = 0; c < NCH; c++) pk_acc[c] = 0;
    for (i = 0; i < NV; i++)
        if (voice[i].stage != S_IDLE) {
            voice_render(&voice[i], mono, n);
            if (voice[i].stage != S_IDLE && voice[i].ch < NCH) act[voice[i].ch]++;
        }
    for (c = 0; c < NCH; c++) { ch_active[c] = act[c]; ch_level[c] = (uint16_t)(pk_acc[c] > 65535u ? 65535u : pk_acc[c]); }
    m = master_q15;
    fx_render(out, n, m);
#ifndef DINGHY_HOST
    {
        uint32_t us = (uint32_t)(fm1_ticks() - t0) / FM1_TICKS_PER_US;
        if (us > perf_max_us) perf_max_us = us;
        perf_sum_us += us; perf_n++;
    }
#endif
}

/* ---- routing (main loop): which synths a note goes to. ROUND ROBIN is the point of POLY5: each note goes to the
 * next synth in the cycle. RR and UNISON take every synth that is switched on, whatever its MIDI channel; POLY is
 * the multitimbral mode (MIDI channel N plays the synths listening on N, the keys play the focused synth). */
static uint32_t mode = M_RR, rr_idx, focus;
static uint8_t note_c[8], note_wave[8];        /* ... and to which synth, with which wave */
static uint32_t note_ms[8], note_w;             /* when the last notes were struck (the delay's picture repeats them) */
static uint32_t last_mask;                      /* the synths the latest note went to (the bar under their numbers) */
static uint32_t solo = NCH;                     /* a synth's page is open: the keys play that synth only */
static uint32_t rest_ms[NCH];                   /* when a synth that is OFF last took its turn: a rest */
static uint32_t hit_ms[NCH];                    /* when each synth was last struck (the screen's ripples) */
static uint8_t note_mask[128];                  /* the synths each note was started on */
static uint32_t rng_s, passes, rand_next = NCH; /* RANDOM mode: the synth the next note jumps to */
static uint32_t rnd(uint32_t n)
{
    rng_s ^= (fm1_ms + 1u) * 2654435761u + passes * 40503u;   /* when you play is the dice */
    if (!rng_s) rng_s = 0x9E3779B9u;
    rng_s ^= rng_s << 13; rng_s ^= rng_s >> 17; rng_s ^= rng_s << 5;
    return (rng_s >> 8) % n;
}

static uint32_t eligible(int from_keys, uint32_t mc, uint32_t *count)
{
    uint32_t c, el = 0, n = 0;
    for (c = 0; c < NCH; c++)
        if (chan[c].enabled && (mode != M_POLY || from_keys || !chan[c].midi_ch || chan[c].midi_ch == mc)) { el |= 1u << c; n++; }
    *count = n;
    return el;
}
static uint32_t rr_pick(uint32_t el, uint32_t n)   /* the synth the round robin is at (NCH = none) */
{
    uint32_t c, k;
    if (!n) return NCH;
    k = rr_idx % n;
    for (c = 0; c < NCH; c++)
        if (el >> c & 1u) { if (!k) return c; k--; }
    return NCH;
}
static uint32_t rand_pick(uint32_t el, uint32_t n)   /* RANDOM: the synth the next note jumps to: never the same twice */
{
    uint32_t c, k;
    if (!n) return NCH;
    if (rand_next < NCH && (el >> rand_next & 1u)) return rand_next;
    k = rnd(n);
    for (c = 0; c < NCH; c++)
        if (el >> c & 1u) { if (!k) break; k--; }
    return rand_next = c;
}
static uint32_t route_on(uint32_t note, uint32_t vel, int from_keys, uint32_t mc)   /* -> the synths it went to (bit 8+c: synth c rested) */
{
    uint32_t c, n, el = eligible(from_keys, mc, &n), mask = 0, rests = 0;
    if (solo < NCH) mask = 1u << solo;            /* a synth's page is open: whatever plays, plays that synth */
    else if (mode == M_UNISON) mask = el;
    else if (mode == M_RANDOM) {
        if ((c = rand_pick(el, n)) < NCH) {
            mask = 1u << c;
            rand_next = NCH;
            if (n > 1u) rand_pick(el & ~mask, n - 1u);   /* where the one after goes: decided now, so the arm can point */
        }
    }
    else if (mode == M_RR) {
        if ((c = rr_pick(el, n)) < NCH) { mask = 1u << c; rr_idx = (rr_idx + 1u) % n; }
    } else
        mask = from_keys ? 1u << focus : el;
    for (c = 0; c < NCH; c++)
        if (mask >> c & 1u) {
            if (chan[c].enabled != E_ON && solo >= NCH) {   /* OFF: its turn, and nothing sounds */
                rest_ms[c] = fm1_ms | 1u;
                mask &= ~(1u << c);
                rests |= 256u << c;
                continue;
            }
            post(EV_ON, c, note, vel); hit_ms[c] = fm1_ms | 1u;
        }
    note_mask[note & 127u] |= (uint8_t)mask;
    if (mask) {
        last_mask = mask;
        for (c = 0; c < NCH && !(mask >> c & 1u); c++) {}
        note_c[note_w & 7u] = (uint8_t)c; note_wave[note_w & 7u] = chan[c].wave;
        note_ms[note_w++ & 7u] = fm1_ms | 1u;
    }
    return mask | rests;
}
static void route_off(uint32_t note)
{
    uint32_t c, mask = note_mask[note & 127u];
    for (c = 0; c < NCH; c++)
        if (mask >> c & 1u) post(EV_OFF, c, note, 0);
    note_mask[note & 127u] = 0;
}
static void all_off(void)
{
    uint32_t k;
    for (k = 0; k < 128u; k++) if (note_mask[k]) route_off(k);
}

/* ---- the parameters the knobs reach */
typedef struct { const char *name, *abbr; uint8_t off, max, step; } param_t;
#define OFF(f) ((uint8_t)__builtin_offsetof(chan_t, f))
/* a synth's pages (ALGORITHM turns them, as it turns MAIN's): up to four dials each. A dial is a setting of the synth, one of the
 * patch's (the delay's time, the room's size: the same for all five), or one of the LFO's two switches */
enum { K_NONE, K_CH, K_FX, K_SHAPE, K_SYNC, K_LFOSPD, K_TIME, K_WAVE, K_OCT, K_FTYPE, K_CHTYPE, K_DJ, K_DRVTYPE, K_ONOFF };
#define IT_SW(bit) {"ON/OFF", K_ONOFF, bit, 1, 1}   /* the page's effect, on or off: the fourth knob */
typedef struct { const char *name; uint8_t kind, off, max, step; } item_t;
#define OFX(f) ((uint8_t)__builtin_offsetof(preset_t, f))
enum { PG_WAVE, PG_AMP, PG_FENV, PG_FILTER, PG_LFO, PG_NOISE, PG_DRIVE, PG_MOD, PG_DELAY, PG_REVERB, NPAGE };
static const char *const PAGE_NAME[NPAGE] = {"WAVE", "AMP ENVELOPE", "FILTER ENVELOPE", "FILTER", "LFO", "NOISE", "SATURATION", "MODULATION", "DELAY", "REVERB"};
#define IT_NONE {0, K_NONE, 0, 1, 0}
static const item_t PAGES[NPAGE][4] = {
    {{"WAVE", K_WAVE, 0, W_N - 1, 1}, {"OCTAVE", K_OCT, 0, 4, 1}, {"TONE", K_CH, OFF(tone), 100, 2}, {"DETUNE", K_CH, OFF(detune), 100, 2}},
    {{"ATTACK", K_CH, OFF(attack), 100, 2}, {"DECAY", K_CH, OFF(decay), 100, 2}, {"SUSTAIN", K_CH, OFF(sus), 100, 2}, {"RELEASE", K_CH, OFF(release), 100, 2}},
    {{"DEPTH", K_CH, OFF(env_filt), 100, 2}, {"SPEED", K_CH, OFF(env_rate), 100, 2}, {"PITCH", K_CH, OFF(drop), 100, 2}, IT_NONE},
    {{"CUTOFF", K_CH, OFF(cutoff), 100, 2}, {"RESO", K_CH, OFF(reso), 30, 1}, {"TYPE", K_FTYPE, 0, 2, 1}, IT_NONE},
    {{"SHAPE", K_SHAPE, 0, 3, 1}, {"SPEED", K_LFOSPD, OFF(lfo_rate), 100, 2}, {"DEPTH", K_CH, OFF(lfo_depth), 100, 2}, {"SYNC", K_SYNC, 0, 1, 1}},
    {{"LEVEL", K_CH, OFF(noise), 100, 2}, {"FILTER", K_CH, OFF(nz_depth), 100, 2}, {"SPEED", K_CH, OFF(nz_rate), 100, 2}, IT_NONE},
    {{"DRIVE", K_CH, OFF(sat), 100, 2}, {"VOLUME", K_CH, OFF(vol), 100, 2}, IT_NONE, IT_NONE},
    {{"TYPE", K_CHTYPE, OFX(ch_type), 3, 1}, {"SPEED", K_FX, OFX(ch_rate), 100, 2}, {"DEPTH", K_FX, OFX(ch_depth), 100, 2}, {"MIX", K_FX, OFX(ch_mix), 100, 2}},
    {{"SEND", K_CH, OFF(dly), 100, 2}, {"TIME", K_TIME, OFX(del_time), 57, 1}, {"REPEATS", K_FX, OFX(del_fb), 90, 2}, IT_NONE},
    {{"SEND", K_CH, OFF(rev), 100, 2}, {"SIZE", K_FX, OFX(rev_size), 100, 2}, {"DAMP", K_FX, OFX(rev_damp), 100, 2}, IT_NONE},
};
/* the pages of MAIN (ALGORITHM turns them): PLAY, then the effects on everything, four dials each */
enum { MP_PLAY, MP_SAT, MP_FILTER, MP_MOD, MP_DELAY, MP_REVERB, MP_BITS, MP_LOFI, MP_MASTER, NMPAGE };
static const char *const MPAGE_NAME[NMPAGE] = {"PLAY", "SATURATION", "FILTER", "MODULATION", "DELAY", "REVERB", "BIT REDUCTION", "LOFI", "MASTERING"};
static const item_t MPAGES[NMPAGE][4] = {
    {IT_NONE, IT_NONE, IT_NONE, IT_NONE},       /* (PLAY: the four quick knobs: filter, resonance, delay, reverb) */
    {{"DRIVE", K_FX, OFX(drive), 100, 2}, {"TYPE", K_DRVTYPE, OFX(drv_type), 2, 1}, {"VOLUME", K_FX, OFX(mvol), 100, 2}, IT_SW(0)},
    {{"SWEEP", K_DJ, OFX(dj), 200, 2}, {"RESO", K_FX, OFX(dj_reso), 100, 2}, IT_NONE, IT_SW(1)},
    {{"TYPE", K_CHTYPE, OFX(ch_type), 3, 1}, {"SPEED", K_FX, OFX(ch_rate), 100, 2}, {"DEPTH", K_FX, OFX(ch_depth), 100, 2}, {"MIX", K_FX, OFX(ch_mix), 100, 2}},
    {{"AMOUNT", K_FX, OFX(del_all), 100, 2}, {"TIME", K_TIME, OFX(del_time), 57, 1}, {"REPEATS", K_FX, OFX(del_fb), 90, 2}, IT_SW(2)},
    {{"AMOUNT", K_FX, OFX(rev_all), 100, 2}, {"SIZE", K_FX, OFX(rev_size), 100, 2}, {"DAMP", K_FX, OFX(rev_damp), 100, 2}, IT_SW(3)},
    {{"BITS", K_FX, OFX(lofi_bits), 100, 2}, {"RATE", K_FX, OFX(lofi_rate), 100, 2}, {"MIX", K_FX, OFX(lofi_mix), 100, 2}, IT_SW(4)},
    {{"WOW", K_FX, OFX(tp_wow), 100, 2}, {"FLUTTER", K_FX, OFX(tp_flut), 100, 2}, {"AGE", K_FX, OFX(tp_age), 100, 2}, {"FAIL", K_FX, OFX(tp_fail), 100, 2}},
    {{"COMP", K_FX, OFX(comp), 100, 2}, {"REL", K_FX, OFX(cmp_spd), 100, 2}, {"GAIN", K_FX, OFX(cmp_gain), 100, 2}, IT_SW(5)},
};
static uint32_t mpage;                          /* MAIN's page now */
static const param_t P_NOISE = {"NOISE", "NSE", OFF(noise), 100, 2};
#define ALL ((1u << NCH) - 1u)
static uint32_t pget(uint32_t c, const param_t *p) { return ((volatile uint8_t *)&chan[c])[p->off]; }
static void padd(uint32_t mask, const param_t *p, int32_t detents)
{
    uint32_t c;
    for (c = 0; c < NCH; c++)
        if (mask >> c & 1u) {
            int32_t v = (int32_t)pget(c, p) + detents * p->step;
            ((volatile uint8_t *)&chan[c])[p->off] = (uint8_t)(v < 0 ? 0 : v > p->max ? p->max : v);
        }
}

/* RANDOM: a new sound for the synths in mask (poly5.html's ranges; noise more often, it is what POLY5 is about) */
static void randomize(uint32_t mask)
{
    static const int8_t OCT[5] = {-1, 0, 0, 0, 1};
    uint32_t c;
    for (c = 0; c < NCH; c++)
        if (mask >> c & 1u) {
            chan[c].wave = (uint8_t)rnd(W_N);
            chan[c].octave = OCT[rnd(5)];
            if (chan[c].octave < 0 && (chan[c].wave == W_TRI || chan[c].wave == W_SINE)) chan[c].octave = 0;   /* a low sine is lost on small speakers */
            chan[c].noise = (uint8_t)(rnd(100) < 50u ? 10u + rnd(60) : 0u);
            chan[c].env_filt = (uint8_t)(20u + rnd(60));          /* a filter snap on every note */
            chan[c].cutoff = (uint8_t)(45u + rnd(52));           /* (Poly5: from 25, which can mute a note) */
            chan[c].reso = (uint8_t)rnd(18);
            chan[c].attack = (uint8_t)(rnd(100) < 75u ? 0u : rnd(15));   /* mostly percussive */
            chan[c].release = (uint8_t)(10u + rnd(55));
            chan[c].dly = (uint8_t)(rnd(100) < 40u ? rnd(70) : 0u);
            chan[c].rev = (uint8_t)rnd(55);
            chan[c].tone = (uint8_t)rnd(101);
            chan[c].drop = (uint8_t)(rnd(100) < 35u ? 10u + rnd(70) : 0u);       /* a third of them punch or fall */
            chan[c].decay = (uint8_t)(rnd(100) < 45u ? 15u + rnd(55) : 0u);      /* nearly half die by themselves */
            chan[c].detune = (uint8_t)(rnd(100) < 30u ? 5u + rnd(35) : 0u);
            /* what moves the cutoff: the envelope as before, mostly Poly5's own shape; sometimes an LFO, sometimes noise */
            chan[c].env_rate = (uint8_t)(rnd(100) < 60u ? 0u : rnd(101));
            chan[c].lfo_depth = (uint8_t)(rnd(100) < 25u ? 20u + rnd(60) : 0u);
            chan[c].lfo_rate = (uint8_t)rnd(101);
            chan[c].lfo_bits = (uint8_t)(rnd(4) << 2 | (rnd(100) < 40u ? 128u : 0u));   /* any shape; often in time */
            chan[c].nz_depth = (uint8_t)(rnd(100) < 15u ? 10u + rnd(50) : 0u);
            chan[c].nz_rate = (uint8_t)rnd(101);
            chan[c].sat = (uint8_t)(rnd(100) < 25u ? 15u + rnd(60) : 0u);      /* a quarter of them: driven */
        }
}

/* ---- the screen: line art in the five synths' colours.
 * MAIN: the round robin as a carousel. Five rings on an oval, an amber arm that swings to the synth that plays
 * next; a ring fills and throws a ripple when its synth is struck, its wave runs inside it, its noise is the sparks
 * around it. EDIT (a synth button held): that synth: its wave, its envelope and filter as drawings, its knobs. */
#define C_AMBER RGB(240, 160, 48)
#define C_SILK RGB(232, 228, 220)
#define C_GREY RGB(80, 88, 96)
#define C_SLOT RGB(37, 40, 48)
static const uint8_t CH_RGB[NCH][3] = {{106, 168, 240}, {240, 138, 112}, {125, 212, 152}, {240, 200, 112}, {216, 144, 168}};
static const char *const WAVE_NAME[W_N] = {"SAW", "SQUARE", "TRIANGLE", "SINE", "FM", "METAL"};
static const char *const MODE_NAME[M_N] = {"POLY", "ROUND ROBIN", "UNISON", "RANDOM"};
/* the arpeggiator's styles (the engine: further down, with the keys) */
enum { AR_OFF, AR_UP, AR_DOWN, AR_UPDOWN, AR_RANDOM, AR_PLAYED, AR_CLIMB, AR_CHORD, AR_N };
static const char *const ARP_NAME[AR_N] = {"ARP OFF", "ARP UP", "ARP DOWN", "ARP UP-DOWN", "ARP RANDOM", "ARP AS PLAYED", "ARP CLIMB", "ARP CHORD"};
static uint32_t arp;
static void arp_set(uint32_t style);
static const uint8_t SYNTH_BTN[NCH] = {BTN_FX, BTN_SEL, BTN_ENV, BTN_LFO, BTN_EDIT};

static uint16_t shade(uint32_t c, uint32_t k)   /* synth c's colour at k/255 */
{
    return RGB(CH_RGB[c][0] * k / 255u, CH_RGB[c][1] * k / 255u, CH_RGB[c][2] * k / 255u);
}
static int32_t isin(uint32_t a) { return sine(a << 16); }           /* a: 65536 a turn; Q15 */
static int32_t icos(uint32_t a) { return sine((a + 16384u) << 16); }

static char *s_put(char *p, const char *a) { while (*a) *p++ = *a++; *p = 0; return p; }
static char *s_num(char *p, int32_t v)
{
    char t[12];
    uint32_t n = 0, u = (uint32_t)(v < 0 ? -v : v);
    if (v < 0) *p++ = '-';
    do { t[n++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    while (n) *p++ = t[--n];
    *p = 0;
    return p;
}
/* ---- the sequence: every patch has one (the factory's: FSEQ; yours: saved with the patch). Up to 32 steps of up to
 * 4 notes (0 = none): the pitches only; who plays them is the play mode's business (see the sequencer, below). */
#define SQ_MAX 32u
#define SQ_NN 4u
typedef struct { uint8_t len, div, swing, bpm, gate, spare[3]; uint8_t note[SQ_MAX][SQ_NN]; } seq_t;   /* gate: 5 % of a step each (12: 60 %; 40: two steps) */
static seq_t sq = {6, 1, 0, 118, 12, {0}, {{0}}};       /* the one that plays: a working copy, as the patch is */
#define USEQ_VER 0x32533550u                    /* "P5S2" */
static struct { uint32_t ver; seq_t s[16]; } useq[2];   /* your patches' sequences: U01-U16 in blob 1, U17-U32 in blob 2 */
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static uint32_t sq_open, sq_play, sq_rec, sq_pos, sq_cur, sq_next_q8, sq_dirty, sq_beat_ms;
static uint8_t sq_who[SQ_MAX], sq_chord, sq_fresh;                          /* sq_who: 0..4 a synth, 16..20 it rested, 255 not played yet */
static struct { uint32_t off; uint8_t note; } sq_snd[12];                   /* the notes it holds now, and until when (a long gate: several steps') */
static uint8_t sq_who_init;
static uint32_t sq_gen;                         /* counts the edits the keys make (the page's text follows) */
static uint32_t sq_hit[SQ_MAX];                 /* when each step last sounded */

static void sq_step_clear(uint32_t st)
{
    uint32_t i;
    for (i = 0; i < SQ_NN; i++) sq.note[st][i] = 0;
}
static uint32_t sq_top(uint32_t st)             /* a step's highest note (0: none) */
{
    uint32_t i, n = 0;
    for (i = 0; i < SQ_NN; i++) if (sq.note[st][i] > n) n = sq.note[st][i];
    return n;
}
/* The factory patches' sequences: chord tones, in minor and in major; 6, 9 or 3 steps (against five synths the
 * round robin never falls the same way twice), eighths or quarters, 118 to the minute. A step: up to four notes.
 * (div: 0 1/4, 1 1/8; gate: 5 % of a step each) */
typedef struct { uint8_t len, div, gate, bpm; uint8_t n[9][SQ_NN]; } fseq_t;
#include "poly5_seqs.h"                        /* FSEQ[]: a sequence for every factory patch (tools/gen_poly5_patches.py) */
static void sq_from_patch(uint32_t i)           /* patch i's sequence becomes the working one (NFACTORY + u: yours) */
{
    uint32_t st, k;
    for (st = 0; st < SQ_MAX; st++) { sq_step_clear(st); sq_who[st] = 255; sq_hit[st] = 0; }
    sq_who_init = 1;
    if (i < NFACTORY) {
        const fseq_t *f = &FSEQ[i];
        sq.len = f->len; sq.div = f->div; sq.swing = 0; sq.bpm = f->bpm; sq.gate = f->gate;
        for (st = 0; st < 9u; st++) for (k = 0; k < SQ_NN; k++) sq.note[st][k] = f->n[st][k];
    } else {
        const seq_t *u = &useq[((i - NFACTORY) / 16u) & 1u].s[(i - NFACTORY) % 16u];
        if (useq[((i - NFACTORY) / 16u) & 1u].ver == USEQ_VER && u->len >= 1u && u->len <= SQ_MAX && u->div <= 5u && u->bpm >= 40u && u->bpm <= 240u) sq = *u;
        else { sq.len = 6; sq.div = 1; sq.swing = 0; sq.bpm = 118; sq.gate = 12; }   /* (a patch saved before sequences: an empty one) */
        if (sq.gate < 1u || sq.gate > 80u) sq.gate = 12;
    }
    sq_pos = 0; sq_cur = 0;
}

/* ---- patches, as the FM-1's other firmwares keep them: PRESETS picks one (the factory ones, then yours), SAVE
 * opens the save page, SAVE again does it. What you play is a working copy: nothing saves itself, a * after the
 * name says there are changes. 32 user patches, in one blob of the flash. */
#define NUSER 32u
#define NPATCH (NFACTORY + NUSER)
#define BANK_VER 0x35423550u                    /* "P5B5": two halves of 16 (blobs 0 and 3); a preset has 171 bytes (8 to spare) */
#define BANK_VER4 0x34423550u                   /* "P5B4": the same halves, a preset of 157 bytes: converted at power-on */
#define BANK_VER1 0x31423550u                   /* "P5B1": 32 in blob 0, 13 bytes a synth; "P5B2": 17: both converted at power-on */
#define BANK_VER2 0x32423550u
static struct { uint32_t ver; uint8_t used[16]; preset_t p[16]; } bankh[2];
#define B_USED(u) bankh[((u) >> 4) & 1u].used[(u) & 15u]
#define B_P(u) bankh[((u) >> 4) & 1u].p[(u) & 15u]
static int bank_save(uint32_t u)                /* the half slot u is in */
{
    uint32_t h = (u >> 4) & 1u;
    bankh[h].ver = BANK_VER;
    return blob_save(h ? 3u : 0u, &bankh[h], sizeof bankh[0]);
}
static preset_t cur;                            /* the loaded patch's name and the parts the panel does not reach yet */
static uint32_t patch_i, dirty;
static char cur_name[24];                       /* the loaded patch's name (a factory one may be longer than cur.name) */

static const preset_t *patch_ptr(uint32_t i)
{
    if (i < NFACTORY) return &FACTORY[i];
    return i < NPATCH && B_USED(i - NFACTORY) ? &B_P(i - NFACTORY) : 0;
}
static char *patch_tag(char *s, uint32_t i)     /* F01, U07 */
{
    uint32_t n = (i < NFACTORY ? i : i - NFACTORY) + 1u;
    *s++ = i < NFACTORY ? 'F' : 'U';
    *s++ = (char)('0' + n / 10u); *s++ = (char)('0' + n % 10u);
    *s = 0;
    return s;
}
static void fx_apply(void)                      /* the working copy's effects, to the audio interrupt (one switched off: as if at nothing) */
{
    uint32_t v, off = cur.fx_off;
    fx_dj = off & 2u ? 0 : (int8_t)cur.dj;
    master_q15 = (uint32_t)cur.mvol * 279u;      /* (the VOLUME dial) */
    fx_reso = cur.dj_reso > 100u ? 100u : cur.dj_reso;
    fx_dly = cur.del_all; fx_rev = cur.rev_all;
    fx_mute = (off >> 2) & 3u;                  /* bit 0: nothing goes into the delay; bit 1: nor into the reverb (what is in them rings out) */
    fx_drive = off & 1u ? 0u : cur.drive > 100u ? 100u : cur.drive;
    fx_ch_type = cur.ch_type > 3u ? 0u : cur.ch_type; fx_ch_rate = cur.ch_rate; fx_ch_depth = cur.ch_depth; fx_ch_mix = cur.ch_mix;
    fx_drv_type = cur.drv_type > 2u ? 0u : cur.drv_type;
    fx_lofi_bits = cur.lofi_bits; fx_lofi_rate = cur.lofi_rate; fx_lofi_mix = off & 16u ? 0u : cur.lofi_mix;
    fx_cmp_amt = off & 32u ? 0u : cur.comp; fx_cmp_spd = cur.cmp_spd; fx_cmp_gain = off & 32u ? 0u : cur.cmp_gain;
    fx_tp_wow = cur.tp_wow; fx_tp_flut = cur.tp_flut; fx_tp_age = cur.tp_age; fx_tp_fail = cur.tp_fail;
    v = 1764u + cur.del_time * 1102u;           /* Poly5: 0.04 s + delTime * 0.025 s */
    fx_dlen = v > 65000u ? 65000u : v;
    v = cur.del_fb * 328u + cur.del_all * 131u; /* more of it: more repeats too */
    fx_fb = v > 27850u ? 27850u : v;
    v = 23600u + cur.rev_size * 49u + cur.rev_all * 33u;   /* more of it: a bigger room */
    fx_g = v > 30500u ? 30500u : v;
    fx_damp = 3000u + cur.rev_damp * 200u;
}
static void patch_load(uint32_t i)
{
    const preset_t *p = patch_ptr(i);
    uint32_t c;
    if (!p) return;
    all_off();
    cur = *p;
    s_put(cur_name, i < NFACTORY ? FACTORY_NAME[i] : p->name);
    for (c = 0; c < NCH; c++) chan[c] = p->ch[c];
    master_q15 = (uint32_t)p->mvol * 279u;
    fx_apply();
    mode = p->mode < M_N ? p->mode : M_RR;
    arp_set(p->arp);
    len_off = 0;                                 /* LENGTH: as the patch */
    rr_idx = 0; rand_next = NCH; last_mask = 0;
    patch_i = i;
    if (!sq_play) sq_from_patch(i);              /* its sequence too; while one plays, that one goes on: the same notes, new sounds */
    dirty = 0;
}
static int patch_store(uint32_t u, const char *name)   /* the working copy into user slot u */
{
    uint32_t c;
    for (c = 0; c < NCH; c++) {                  /* LENGTH is kept: what you hear is what is saved; the knob is at 12 again */
        chan[c].release = (uint8_t)len_of(chan[c].release);
        if (chan[c].decay) chan[c].decay = (uint8_t)(len_of(chan[c].decay) ? len_of(chan[c].decay) : 1u);
    }
    len_off = 0;
    for (c = 0; c < NCH; c++) cur.ch[c] = chan[c];
    cur.mode = (uint8_t)mode;
    cur.arp = (uint8_t)arp;
    for (c = 0; c < sizeof cur.name - 1u && name[c]; c++) cur.name[c] = name[c];
    for (; c < sizeof cur.name; c++) cur.name[c] = 0;
    s_put(cur_name, cur.name);
    B_P(u) = cur;
    B_USED(u) = 1;
    if (bank_save(u)) return -1;
    useq[(u / 16u) & 1u].ver = USEQ_VER;        /* and its sequence */
    useq[(u / 16u) & 1u].s[u % 16u] = sq;
    if (blob_save(1u + ((u / 16u) & 1u), &useq[(u / 16u) & 1u], sizeof useq[0])) return -1;
    patch_i = NFACTORY + u;
    dirty = 0;
    return 0;
}
static void patch_step(int32_t d)               /* PRESETS: the next patch that exists */
{
    uint32_t i = patch_i, n;
    for (n = 0; n < NPATCH; n++) {
        i = (i + NPATCH + (d > 0 ? 1u : NPATCH - 1u)) % NPATCH;
        if (patch_ptr(i)) { patch_load(i); return; }
    }
}
static const char *const ADJ[16] = {"FUZZY", "GLASS", "NEON", "RUSTY", "TINY", "HOLLOW", "BRIGHT", "DUSTY",
                                    "WILD", "SOFT", "SHARP", "LAZY", "ODD", "BROKEN", "HAPPY", "COSMIC"};
static const char *const NOUN[16] = {"CUBE", "PRISM", "ROBIN", "SPARK", "MARBLE", "ORBIT", "PULSE", "CLOCK",
                                     "SWARM", "ECHO", "ROBOT", "TOY", "GARDEN", "ENGINE", "CHOIR", "STORM"};

static void cv_frame(int32_t x, int32_t y, int32_t w, int32_t h, int32_t t, uint16_t c)
{
    cv_rect(x, y, w, t, c); cv_rect(x, y + h - t, w, t, c);
    cv_rect(x, y, t, h, c); cv_rect(x + w - t, y, t, h, c);
}
static void cv_ring(int32_t cx, int32_t cy, int32_t r, uint16_t c)   /* a circle (midpoint) */
{
    int32_t x = r, y = 0, d = 1 - r;
    while (x >= y) {
        cv_pset(cx + x, cy + y, c); cv_pset(cx - x, cy + y, c); cv_pset(cx + x, cy - y, c); cv_pset(cx - x, cy - y, c);
        cv_pset(cx + y, cy + x, c); cv_pset(cx - y, cy + x, c); cv_pset(cx + y, cy - x, c); cv_pset(cx - y, cy - x, c);
        y++;
        if (d < 0) d += 2 * y + 1;
        else { x--; d += 2 * (y - x) + 1; }
    }
}
/* the shapes: a wave is a solid. Square: a cube; triangle: a pyramid; saw: a ramp; sine: a globe. Wireframes,
 * turning about their upright axis, seen a little from above. */
static const int8_t CUBE_V[8][3] = {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}, {-1, 1, -1}, {1, 1, -1}, {1, 1, 1}, {-1, 1, 1}};
static const uint8_t CUBE_E[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
static const int8_t PYR_V[5][3] = {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}, {0, 1, 0}};
static const uint8_t PYR_E[8][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {0, 4}, {1, 4}, {2, 4}, {3, 4}};
static const int8_t RAMP_V[6][3] = {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}, {1, 1, -1}, {1, 1, 1}};
static const uint8_t RAMP_E[9][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {1, 4}, {2, 5}, {0, 4}, {3, 5}};

static const int8_t DIA_V[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
static const uint8_t DIA_E[12][2] = {{0, 4}, {4, 1}, {1, 5}, {5, 0}, {0, 2}, {4, 2}, {1, 2}, {5, 2}, {0, 3}, {4, 3}, {1, 3}, {5, 3}};
static const int8_t HOUR_V[9][3] = {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}, {-1, 1, -1}, {1, 1, -1}, {1, 1, 1}, {-1, 1, 1}, {0, 0, 0}};
static const uint8_t HOUR_E[16][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4},
                                      {0, 8}, {1, 8}, {2, 8}, {3, 8}, {4, 8}, {5, 8}, {6, 8}, {7, 8}};
static void project(int32_t x, int32_t y, int32_t z, int32_t cs, int32_t sn, int32_t cx, int32_t cy, int32_t *sx, int32_t *sy)
{
    int32_t x1 = (x * cs + z * sn) >> 15, z1 = (z * cs - x * sn) >> 15;
    *sx = cx + x1;
    *sy = cy - ((y * 30791 - z1 * 11207) >> 15);    /* tilted 20 degrees towards you */
}
static void mesh(const int8_t (*v)[3], uint32_t nv, const uint8_t (*e)[2], uint32_t ne, int32_t sx, int32_t sy,
                 int32_t cx, int32_t cy, uint32_t ang, uint16_t c)
{
    int32_t px[9], py[9], cs = icos(ang), sn = isin(ang);
    uint32_t i;
    for (i = 0; i < nv; i++)
        project(v[i][0] * sx, v[i][1] * sy, v[i][2] * sx, cs, sn, cx, cy, &px[i], &py[i]);
    for (i = 0; i < ne; i++)
        cv_line(px[e[i][0]], py[e[i][0]], px[e[i][1]], py[e[i][1]], c);
}
static void shape3d(int32_t cx, int32_t cy, int32_t s, uint32_t wave, uint32_t ang, uint16_t c)
{
    switch (wave) {
    case W_SQUARE: mesh(CUBE_V, 8, CUBE_E, 12, s, s, cx, cy, ang, c); break;
    case W_TRI: mesh(PYR_V, 5, PYR_E, 8, s, s * 5 / 4, cx, cy, ang, c); break;
    case W_SAW: mesh(RAMP_V, 6, RAMP_E, 9, s, s, cx, cy, ang, c); break;
    case W_FM: mesh(DIA_V, 6, DIA_E, 12, s * 5 / 4, s * 3 / 2, cx, cy, ang, c); break;
    case W_METAL: mesh(HOUR_V, 9, HOUR_E, 16, s, s * 5 / 4, cx, cy, ang, c); break;
    default: {                                  /* the globe: its equator and two meridians */
        int32_t cs = icos(ang), sn = isin(ang), r = s * 5 / 4, x0 = 0, y0 = 0, x1, y1;
        uint32_t ring, k;
        for (ring = 0; ring < 3u; ring++)
            for (k = 0; k <= 12u; k++) {
                int32_t a = icos(k * 5461u) * r >> 15, b = isin(k * 5461u) * r >> 15;
                if (ring == 0u) project(a, 0, b, cs, sn, cx, cy, &x1, &y1);
                else if (ring == 1u) project(a, b, 0, cs, sn, cx, cy, &x1, &y1);
                else project(0, b, a, cs, sn, cx, cy, &x1, &y1);
                if (k) cv_line(x0, y0, x1, y1, c);
                x0 = x1; y0 = y1;
            }
        break;
    }
    }
}

/* the shapes as solids: their faces tinted in the synth's colour (the far ones darker, drawn first), and, when
 * fill is on, a brighter filling that rises from the bottom with the synth's level and drains as the note dies. */
static const uint8_t CUBE_F[6][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};
static const uint8_t PYR_F[5][4] = {{0, 1, 2, 3}, {0, 1, 4, 4}, {1, 2, 4, 4}, {2, 3, 4, 4}, {3, 0, 4, 4}};
static const uint8_t RAMP_F[5][4] = {{0, 1, 2, 3}, {1, 2, 5, 4}, {0, 3, 5, 4}, {0, 1, 4, 4}, {3, 2, 5, 5}};
static const uint8_t DIA_F[8][4] = {{0, 4, 2, 2}, {4, 1, 2, 2}, {1, 5, 2, 2}, {5, 0, 2, 2}, {0, 4, 3, 3}, {4, 1, 3, 3}, {1, 5, 3, 3}, {5, 0, 3, 3}};
static const uint8_t HOUR_F[10][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 8, 8}, {1, 2, 8, 8}, {2, 3, 8, 8}, {3, 0, 8, 8},
                                      {4, 5, 8, 8}, {5, 6, 8, 8}, {6, 7, 8, 8}, {7, 4, 8, 8}};
static void poly_fill(const int32_t *px, const int32_t *py, const uint8_t *f, int32_t ytop, uint16_t col)   /* convex, 4 corners; rows from ytop down */
{
    int32_t y, y0 = py[f[0]], y1 = y0, i;
    for (i = 1; i < 4; i++) { if (py[f[i]] < y0) y0 = py[f[i]]; if (py[f[i]] > y1) y1 = py[f[i]]; }
    if (y0 < ytop) y0 = ytop;
    for (y = y0; y <= y1; y++) {
        int32_t xl = 9999, xr = -9999;
        for (i = 0; i < 4; i++) {
            int32_t ax = px[f[i]], ay = py[f[i]], bx = px[f[(i + 1) & 3]], by = py[f[(i + 1) & 3]], x;
            if (ay == by) { if (ay != y) continue; if (ax < xl) xl = ax; if (ax > xr) xr = ax; if (bx < xl) xl = bx; if (bx > xr) xr = bx; continue; }
            if ((y < ay && y < by) || (y > ay && y > by)) continue;
            x = ax + (bx - ax) * (y - ay) / (by - ay);
            if (x < xl) xl = x;
            if (x > xr) xr = x;
        }
        if (xr >= xl) cv_rect(xl, y, xr - xl + 1, 1, col);
    }
}
static void shape_solid(int32_t cx, int32_t cy, int32_t sx, int32_t sy, uint32_t wave, uint32_t ang, uint32_t c,
                        uint32_t l, int fill, uint16_t edge)
{
    const int8_t (*v)[3]; const uint8_t (*e)[2]; const uint8_t (*f)[4];
    uint32_t nv, ne, nf, i, j, ord[10];
    int32_t px[9], py[9], pz[9], fz[10], wx = sx, cs = icos(ang), sn = isin(ang), top = 9999, bot = -9999, yfill, pass;
    if (wave == W_SINE) {                       /* the globe: a disc behind its rings */
        int32_t r = sx * 5 / 4, ry = sy * 5 / 4, y;
        yfill = cy + ry - 2 * ry * (int32_t)l / 255;
        for (y = -ry; y <= ry; y++) {
            int32_t w = isin((uint32_t)(16384 + y * 16384 / ry) & 65535u) * r >> 15;   /* cos: the disc's half width */
            if (w < 0) w = -w;
            if (fill >= 0) cv_rect(cx - w, cy + y, 2 * w + 1, 1, shade(c, fill && cy + y >= yfill && l ? 150u : 34u));
        }
        if (sy != sx) {                         /* a tall globe: its rings stretched (an egg) */
            int32_t x0 = 0, y0 = 0, x1, y1;
            uint32_t ring, k;
            for (ring = 0; ring < 3u; ring++)
                for (k = 0; k <= 12u; k++) {
                    int32_t a = icos(k * 5461u) * r >> 15, b = isin(k * 5461u) * ry >> 15, d = isin(k * 5461u) * r >> 15, g = icos(k * 5461u) * ry >> 15;
                    if (ring == 0u) project(a, 0, d, cs, sn, cx, cy, &x1, &y1);
                    else if (ring == 1u) project(a, b, 0, cs, sn, cx, cy, &x1, &y1);
                    else project(0, g, d, cs, sn, cx, cy, &x1, &y1);
                    if (k) cv_line(x0, y0, x1, y1, edge);
                    x0 = x1; y0 = y1;
                }
        } else
            shape3d(cx, cy, sx, wave, ang, edge);
        return;
    }
    if (wave == W_SQUARE) { v = CUBE_V; nv = 8; e = CUBE_E; ne = 12; f = CUBE_F; nf = 6; }
    else if (wave == W_TRI) { v = PYR_V; nv = 5; e = PYR_E; ne = 8; f = PYR_F; nf = 5; sy = sy * 5 / 4; }
    else if (wave == W_FM) { v = DIA_V; nv = 6; e = DIA_E; ne = 12; f = DIA_F; nf = 8; wx = sx * 5 / 4; sy = sy * 5 / 4; }
    else if (wave == W_METAL) { v = HOUR_V; nv = 9; e = HOUR_E; ne = 16; f = HOUR_F; nf = 10; }
    else { v = RAMP_V; nv = 6; e = RAMP_E; ne = 9; f = RAMP_F; nf = 5; }
    sx = wx;
    for (i = 0; i < nv; i++) {
        int32_t x = v[i][0] * sx, z = v[i][2] * sx;
        project(x, v[i][1] * sy, z, cs, sn, cx, cy, &px[i], &py[i]);
        pz[i] = (z * cs - x * sn) >> 15;
        if (py[i] < top) top = py[i];
        if (py[i] > bot) bot = py[i];
    }
    for (i = 0; i < nf; i++) {                  /* the faces, the far ones first */
        fz[i] = pz[f[i][0]] + pz[f[i][1]] + pz[f[i][2]] + pz[f[i][3]];
        ord[i] = i;
    }
    for (i = 1; i < nf; i++)
        for (j = i; j > 0 && fz[ord[j]] > fz[ord[j - 1]]; j--) { uint32_t t = ord[j]; ord[j] = ord[j - 1]; ord[j - 1] = t; }
    yfill = bot - (bot - top) * (int32_t)l / 255;
    for (pass = 0; pass < (fill < 0 ? 0 : fill && l ? 2 : 1); pass++)
        for (i = 0; i < nf; i++)
            poly_fill(px, py, f[ord[i]], pass ? yfill : -9999, shade(c, pass ? 96u + i * 156u / nf : 22u + i * 42u / nf));
    for (i = 0; i < ne; i++)
        cv_line(px[e[i][0]], py[e[i][0]], px[e[i][1]], py[e[i][1]], edge);
}

/* the particles a shape throws when its synth plays: its colour; white ones for its noise */
#define NPART 220u
static struct { int16_t x, y, vx, vy; uint8_t life, max, c, white; } part[NPART];
static uint32_t part_i, gr = 0x1234ABCDu;
static uint32_t grnd(void) { gr ^= gr << 13; gr ^= gr >> 17; gr ^= gr << 5; return gr; }
static void part_spawn(uint32_t c, int32_t cx, int32_t cy, uint32_t n, uint32_t white, int32_t speed)
{
    while (n--) {
        uint32_t r = grnd(), i = part_i++ % NPART;
        int32_t sp = speed / 3 + (int32_t)((r >> 8) % (uint32_t)speed);
        part[i].x = (int16_t)(cx * 16); part[i].y = (int16_t)(cy * 16);
        part[i].vx = (int16_t)(icos(r >> 16) * sp >> 15);
        part[i].vy = (int16_t)((isin(r >> 16) * sp >> 15) - 18);
        part[i].life = part[i].max = (uint8_t)(14u + (r & 15u));
        part[i].c = (uint8_t)c; part[i].white = (uint8_t)white;
    }
}
static void part_step_draw(void)
{
    uint32_t i;
    for (i = 0; i < NPART; i++) {
        uint32_t k;
        uint16_t col;
        if (!part[i].life) continue;
        part[i].x = (int16_t)(part[i].x + part[i].vx); part[i].y = (int16_t)(part[i].y + part[i].vy);
        part[i].vy = (int16_t)(part[i].vy + 2);
        part[i].life--;
        k = 60u + 195u * part[i].life / part[i].max;
        col = part[i].white ? RGB(k, k, k) : shade(part[i].c, k);
        if (part[i].life * 2u > part[i].max) cv_rect(part[i].x >> 4, part[i].y >> 4, 2, 2, col);
        else cv_pset(part[i].x >> 4, part[i].y >> 4, col);
    }
}

#define MAIN_Y 22u
#define MAIN_H 174
#define SHAPE_Y 82
static uint32_t lvl[NCH], rot[NCH], seen_hit[NCH];   /* the level the screen shows (0..255), each shape's turn */
/* the space the shapes stand in: a grid that goes off into the distance, faint, so that the shapes stay what you look
 * at. look: 0 none, 1 a floor to the horizon, 2 a room (floor, back wall, sides, ceiling), 3 a floor and a glow at the
 * horizon. It drifts towards you slowly; a little brighter while the synths sound. */
static uint32_t grid_look = 6;                  /* (the user's choice: floor and ceiling, white; CC 118 tries the others) */
static void g_grid(uint32_t t, uint32_t glow)
{
    const int32_t vx = 120, hy = grid_look == 2u ? 76 : 64, by = MAIN_H - 1;
    int32_t i, ph = (int32_t)(t / 40u % 16u);
    uint32_t b = 26u + glow / 8u;               /* how bright the nearest line is (of 255) */
    if (!grid_look) return;
    if (grid_look >= 4u) {                      /* a floor and a ceiling, meeting far off behind the shapes; a few stars. 4: warm; 5: cool; 6: white */
        const int32_t my = SHAPE_Y, half = MAIN_H - 1 - my;
        uint32_t warm = grid_look == 4u, white = grid_look >= 6u, r;
        b = 34u + glow / 10u;
        for (i = -9; i <= 9; i++) {             /* the lines from the vanishing point, down and up */
            uint16_t c = white ? RGB(b * 2u / 3u, b * 2u / 3u, b * 2u / 3u) : warm ? RGB(b, b / 3u, b / 10u) : RGB(b / 3u, b / 2u, b * 3u / 4u);
            cv_line(vx + i * 2, my + 3, vx + i * 78, my + half + 20, c);
            cv_line(vx + i * 2, my - 3, vx + i * 78, my - half - 20, c);
        }
        for (i = 1; i <= 9; i++) {              /* and across: nearer, brighter, further apart; drifting towards you */
            int32_t z8 = i * 128 - (int32_t)(t / 15u % 128u), d = 3 + half * 160 / (z8 + 112);   /* a cell in 1.9 s (it was 0.64 s: too busy), in eighths: it glides */
            uint32_t k = b * 352u / (uint32_t)(z8 + 240);
            uint16_t c = white ? RGB(k * 3u / 4u, k * 3u / 4u, k * 3u / 4u) : warm ? RGB(k, k / 3u, k / 10u) : RGB(k / 2u, k * 2u / 3u, k);
            if (d > half + 2) continue;
            cv_rect(0, my + d, 240, 1, c);
            if (my - d >= 0) cv_rect(0, my - d, 240, 1, c);
        }
        for (i = 0; i < 1; i++) {               /* every five seconds one strip of light runs, slowly, down a line to the point where they meet */
            uint32_t tt = t + 2000u, cyc = tt / 5000u, at = tt % 5000u, h = (cyc * 3u + 1u) * 2654435761u;
            int32_t li, up, x0, y0, x1, y1, u0, u1;
            h ^= h >> 15;
            if (at > 2800u) continue;
            li = (int32_t)((h >> 4) % 19u) - 9; up = (int32_t)((h >> 12) & 1u);
            x0 = vx + li * 78; y0 = up ? my - half - 20 : my + half + 20;   /* the line: from the edge ... */
            x1 = vx + li * 2; y1 = up ? my - 3 : my + 3;                  /* ... to the middle */
            u0 = (int32_t)at * 256 / 2800; u0 = u0 * u0 / 256;           /* slow, then faster: it falls away from you */
            u1 = u0 + 40 > 256 ? 256 : u0 + 40;
            cv_line(x0 + (x1 - x0) * u0 / 256, y0 + (y1 - y0) * u0 / 256, x0 + (x1 - x0) * u1 / 256, y0 + (y1 - y0) * u1 / 256,
                    white ? RGB(225, 225, 220) : warm ? RGB(230, 120, 50) : RGB(110, 160, 235));
            if (u0 > 30) cv_line(x0 + (x1 - x0) * (u0 - 30) / 256, y0 + (y1 - y0) * (u0 - 30) / 256, x0 + (x1 - x0) * u0 / 256, y0 + (y1 - y0) * u0 / 256,
                                 white ? RGB(90, 90, 88) : warm ? RGB(110, 50, 20) : RGB(50, 75, 115));   /* its tail */
        }
        for (i = 0, r = 12345u; i < 22; i++) {  /* stars: still, one or two twinkling */
            uint32_t k;
            r = r * 1664525u + 1013904223u;
            k = 60u + ((r >> 9) & 63u) + (((t >> 9) + (uint32_t)i) % 11u == 0u ? 90u : 0u);
            cv_pset((int32_t)((r >> 16) % 240u), (int32_t)((r >> 4) % (uint32_t)MAIN_H), RGB(k, k, k));
        }
        return;
    }
    if (grid_look == 3u)                        /* the glow: a band at the horizon, fading up and down */
        for (i = -10; i <= 10; i++) {
            uint32_t k = (uint32_t)(10 - (i < 0 ? -i : i)) * (uint32_t)(10 - (i < 0 ? -i : i)) / 4u + glow / 12u;
            cv_rect(0, hy + i, 240, 1, RGB(k * 3u / 2u, k / 3u, k * 2u));
        }
    if (grid_look == 2u) {                      /* the room: the back wall, and the lines to it from the screen's edges */
        const int32_t wx0 = 70, wx1 = 170, wy0 = 44, wy1 = 108;
        uint16_t c1 = RGB(b / 2u, b * 2u / 3u, b), c0 = RGB(b / 3u, b / 2u, b * 3u / 4u);
        for (i = 0; i <= 5; i++) {              /* the wall's grid */
            cv_line(wx0 + i * 20, wy0, wx0 + i * 20, wy1, c0);
            if (i <= 4) cv_line(wx0, wy0 + i * 16, wx1, wy0 + i * 16, c0);
        }
        for (i = 0; i <= 5; i++) {              /* floor and ceiling: lines running away */
            int32_t xw = wx0 + i * 20, xs = -120 + i * 96;
            cv_line(xs, by, xw, wy1, c1); cv_line(xs, 0, xw, wy0, c0);
        }
        for (i = 0; i <= 4; i++) {              /* the side walls */
            int32_t yw = wy0 + i * 16, ys = -40 + i * 64;
            cv_line(0, ys, wx0, yw, c0); cv_line(239, ys, wx1, yw, c0);
        }
        for (i = 1; i <= 3; i++) {              /* rings round the room, coming nearer */
            int32_t f = i * 64 + ph * 4, x0 = wx0 - (wx0 + 120) * f / 384 * f / 256, x1 = wx1 + (239 - wx1 + 120) * f / 384 * f / 256;
            int32_t y1 = wy1 + (by - wy1) * f / 256 * f / 256, y0 = wy0 - wy0 * f / 256 * f / 256;
            uint16_t c = RGB(b * (uint32_t)f / 512u, b * (uint32_t)f / 384u, b * (uint32_t)f / 256u);
            if (y1 > by) continue;
            cv_line(x0, y1, x1, y1, c); cv_line(x0, y0, x1, y0, c); cv_line(x0, y0, x0, y1, c); cv_line(x1, y0, x1, y1, c);
        }
        return;
    }
    for (i = -9; i <= 9; i++)                   /* the floor: lines from the vanishing point */
        cv_line(vx + i * 6, hy + 4, vx + i * 70, by, RGB(b / 3u, b / 2u, b * 3u / 4u));
    for (i = 1; i <= 9; i++) {                  /* and across: nearer, brighter, further apart; drifting towards you */
        int32_t z = i * 16 - ph, y = hy + 4 + (by - hy) * 20 / (z + 14);
        uint32_t k = b * 40u / (uint32_t)(z + 30);
        if (y > by || y < hy + 4) continue;
        cv_rect(0, y, 240, 1, RGB(k / 2u, k * 2u / 3u, k));
    }
}
static const char *const STATE_NAME[3] = {"SKIP", "ON", "SILENT"};
static void draw_main(uint32_t t, uint32_t next, uint32_t spin, uint32_t sel)   /* next: the synths with a note held; sel: SELECT's cursor (NCH: none) */
{
    uint32_t c;
    cv_begin(240, MAIN_H, C_BLACK);
    g_grid(t, lvl[0] + lvl[1] + lvl[2] + lvl[3] + lvl[4] > 400u ? 400u : lvl[0] + lvl[1] + lvl[2] + lvl[3] + lvl[4]);
    for (c = 0; c < NCH; c++) {
        int32_t x = 24 + (int32_t)c * 48, sz;
        uint32_t st = chan[c].enabled, l = lvl[c], wave = chan[c].wave % W_N;
        int on = st == E_ON, rest = rest_ms[c] && t - rest_ms[c] < 180u;
        uint16_t col = on ? shade(c, 150u + l * 105u / 255u) : st == E_OFF ? shade(c, rest ? 230u : 80u) : C_SLOT;
        char num[2] = {(char)('1' + c), 0};
        if (spin) wave = (wave + (t >> 6) + c) % W_N;          /* new sounds: the dice roll */
        if (on && seen_hit[c] != hit_ms[c]) {                   /* struck: a burst, and white for the noise */
            seen_hit[c] = hit_ms[c];
            part_spawn(c, x, SHAPE_Y, 26, 0, 80);
            part_spawn(c, x, SHAPE_Y, chan[c].noise * 30u / 100u, 1, 100);
        }
        if (on && l > 50u) {                                    /* and a trickle while it sounds */
            part_spawn(c, x, SHAPE_Y, 1, 0, 40);
            part_spawn(c, x, SHAPE_Y, chan[c].noise * 5u / 100u, 1, 60);
        }
        /* tall, of glass; it fills with the synth's level and drains again. OFF: its outline only, dim (it lights
         * for a moment when its turn passes: a rest); SKIP: a grey ghost. Under SELECT's cursor: bigger */
        sz = 12 + (int32_t)(l * 3u / 255u) + (c == sel ? 4 : 0);
        if (on) shape_solid(x, SHAPE_Y, sz, sz * 9 / 5, wave, rot[c], c, l, 1, col);
        else shape_solid(x, SHAPE_Y, sz, sz * 9 / 5, wave, rot[c], c, 0, -1, col);
        if (c == sel) {                                         /* the cursor: what it is set to, above; a frame of corners */
            const char *nm = STATE_NAME[st < 3u ? st : 0u];
            cv_text(x - text_w(&FONT_S, nm) / 2, 2, &FONT_S, nm, C_AMBER);
            cv_rect(x - 22, 22, 8, 2, C_AMBER); cv_rect(x - 22, 22, 2, 8, C_AMBER);
            cv_rect(x + 14, 22, 8, 2, C_AMBER); cv_rect(x + 20, 22, 2, 8, C_AMBER);
            cv_rect(x - 22, 130, 8, 2, C_AMBER); cv_rect(x - 22, 124, 2, 8, C_AMBER);
            cv_rect(x + 14, 130, 8, 2, C_AMBER); cv_rect(x + 20, 124, 2, 8, C_AMBER);
        }
        cv_text(x - 4, 138, &FONT_S, num, on ? shade(c, 255) : st == E_OFF ? shade(c, 110) : C_GREY);
        if (on && ((next >> c & 1u) || l > 40u))                /* a note is held here, or it still sounds */
            cv_rect(x - 9, 158, 18, 3, C_AMBER);
        else if (rest) cv_rect(x - 9, 158, 18, 1, C_AMBER);     /* a rest: a thin line */
        else if (mode == M_POLY && c == focus) cv_rect(x - 9, 158, 18, 3, C_SILK);   /* POLY: the one the keys play */
    }
    part_step_draw();
    cv_blit(0, MAIN_Y);
}

/* a synth's page. At the top, under the FM-1's four knobs, what they are set to: four dials. Below: the synth's
 * shape, big, turning, throwing particles when it plays; and its sound as a picture: the envelope filled in its
 * colour (attack, the fall, the release), the filter's curve across it, the noise as specks in it. */
static void dial(int32_t cx, int32_t cy, int32_t v, int32_t max, uint16_t col, const char *name, const char *txt)   /* txt: instead of the number */
{
    int32_t i, n = 28, lit = v * n / max;
    char s[8];
    for (i = 0; i <= n; i++) {                  /* from 7:30 round to 4:30, lit up to the value */
        uint32_t a = 24576u + (uint32_t)i * 49152u / (uint32_t)n;
        int32_t x = cx + (icos(a) * 17 >> 15), y = cy + (isin(a) * 17 >> 15);
        if (v && i <= lit) cv_rect(x - 1, y - 1, 3, 3, col);
        else cv_rect(x, y, 2, 2, C_SLOT);
    }
    if (txt) s_put(s, txt); else s_num(s, v);
    cv_text(cx - text_w(&FONT_S, s) / 2, cy - 8, &FONT_S, s, C_SILK);
    cv_text(cx - text_w(&FONT_S, name) / 2, cy + 19, &FONT_S, name, col);
}
static int32_t filt_h(int32_t x, int32_t fc, int32_t q)   /* a lowpass drawn: flat, a bump at fc, the fall (0..30 + q) */
{
    int32_t d = x - fc, ad = d < 0 ? -d : d, h = d <= 0 ? 30 : d > 26 ? 0 : 30 - d * 30 / 26;
    if (ad < 12) h += q * (12 - ad) / 12;
    return h;
}
static const char *const LFO_SHAPE[4] = {"SIN", "TRI", "SAW", "SQR"};
static void lfo_speed_text(char *s, uint32_t c)   /* a number, or the note value when the LFO goes with the tempo */
{
    static const char *const DIVN[9] = {"4BAR", "2BAR", "1BAR", "1/2", "1/4", "1/8", "1/8T", "1/16", "1/32"};
    if (MSYNC(c)) s_put(s, DIVN[(chan[c].lfo_rate > 100u ? 100u : chan[c].lfo_rate) * 9u / 101u]);
    else s_num(s, chan[c].lfo_rate);
}
static int32_t item_get(uint32_t c, const item_t *it)
{
    switch (it->kind) {
    case K_CH: case K_LFOSPD: return ((volatile uint8_t *)&chan[c])[it->off];
    case K_FX: case K_TIME: case K_CHTYPE: case K_DRVTYPE: return ((uint8_t *)&cur)[it->off];
    case K_DJ: return (int8_t)cur.dj + 100;      /* 0 .. 200: lowpass .. open .. highpass */
    case K_ONOFF: return !(cur.fx_off >> it->off & 1u);
    case K_SHAPE: return (int32_t)MSHAPE(c);
    case K_SYNC: return (int32_t)MSYNC(c);
    case K_WAVE: return chan[c].wave % W_N;
    case K_OCT: return chan[c].octave + 2;
    case K_FTYPE: return FTYPE(c) > 2u ? 0 : (int32_t)FTYPE(c);
    default: return 0;
    }
}
static const char *item_text(char *s, uint32_t c, const item_t *it)   /* what the dial says instead of its number (0: the number) */
{
    static const char *const WV[W_N] = {"SAW", "SQR", "TRI", "SIN", "FM", "MTL"};
    static const char *const FT[3] = {"LOW", "BAND", "HIGH"}, *const CT[4] = {"OFF", "CHOR", "FLNG", "PHAS"}, *const DT[3] = {"SOFT", "HARD", "FOLD"};
    switch (it->kind) {
    case K_SHAPE: return LFO_SHAPE[MSHAPE(c)];
    case K_SYNC: return MSYNC(c) ? "ON" : "OFF";
    case K_LFOSPD: lfo_speed_text(s, c); return s;
    case K_TIME: s_num(s, 40 + item_get(c, it) * 25); return s;     /* ms */
    case K_WAVE: return WV[chan[c].wave % W_N];
    case K_OCT: s[0] = chan[c].octave < 0 ? '-' : '+'; s[1] = (char)('0' + (chan[c].octave < 0 ? -chan[c].octave : chan[c].octave)); s[2] = 0; return s;
    case K_FTYPE: return FT[item_get(c, it)];
    case K_CHTYPE: return CT[item_get(c, it) & 3];
    case K_DRVTYPE: return DT[item_get(c, it) % 3];
    case K_ONOFF: return item_get(c, it) ? "ON" : "OFF";
    case K_DJ: {
        int32_t d = (int8_t)cur.dj;
        if (!d) return "OPEN";
        s[0] = d < 0 ? 'L' : 'H'; s_num(s + 1, d < 0 ? -d : d);
        return s;
    }
    default: return 0;
    }
}
static void item_add(uint32_t c, const item_t *it, int32_t e)
{
    int32_t v = item_get(c, it) + e * it->step;
    v = v < 0 ? 0 : v > it->max ? it->max : v;
    switch (it->kind) {
    case K_CH: case K_LFOSPD: ((volatile uint8_t *)&chan[c])[it->off] = (uint8_t)v; break;
    case K_FX: case K_TIME: case K_DRVTYPE: ((uint8_t *)&cur)[it->off] = (uint8_t)v; fx_apply(); break;   /* the patch's: the same for all five synths */
    case K_CHTYPE:                              /* (switched on: heard at once) */
        cur.ch_type = (uint8_t)v;
        if (v && !cur.ch_mix) { cur.ch_mix = 70; if (!cur.ch_depth) cur.ch_depth = 50; if (!cur.ch_rate) cur.ch_rate = 35; }
        fx_apply();
        break;
    case K_DJ: cur.dj = (uint8_t)(int8_t)(v - 100); fx_apply(); break;
    case K_ONOFF: cur.fx_off = (uint8_t)(e > 0 ? cur.fx_off & ~(1u << it->off) : cur.fx_off | 1u << it->off); fx_apply(); break;
    case K_SHAPE: chan[c].lfo_bits = (uint8_t)((chan[c].lfo_bits & ~12u) | (uint32_t)v << 2); break;
    case K_SYNC: chan[c].lfo_bits = (uint8_t)(e > 0 ? chan[c].lfo_bits | 128u : chan[c].lfo_bits & 127u); break;
    case K_WAVE: chan[c].wave = (uint8_t)v; break;
    case K_OCT: chan[c].octave = (int8_t)(v - 2); break;
    case K_FTYPE: chan[c].lfo_bits = (uint8_t)((chan[c].lfo_bits & ~3u) | (uint32_t)v); break;
    default: break;
    }
}

/* ---- a synth's pages, drawn: under the four dials, each page its own picture (x 4..236, y 70..150) */
#define GX 4
#define GW 232
#define GB 150
static int32_t wave_at(uint32_t c, uint32_t ph)   /* the synth's wave as it is set, one cycle over 2^32: -32767..32767 */
{
    int32_t x = (int32_t)(ph >> 16) - 32768;
    switch (chan[c].wave % W_N) {
    case W_SAW: return x;
    case W_SQUARE: return ph < 0x80000000u ? 30000 : -30000;
    case W_TRI: return (x < 0 ? -x : x) * 2 - 32768;
    case W_FM: {
        static const uint8_t R[10] = {1, 2, 3, 4, 2, 4, 1, 3, 5, 7};
        return sine(ph + (uint32_t)(sine(ph * R[(chan[c].tone > 100u ? 100u : chan[c].tone) * 10u / 101u]) * (6000 + (int32_t)chan[c].env_filt * 300)));
    }
    case W_METAL: return ((ph ^ (ph * 3u + (ph >> 1) * (chan[c].tone / 20u + 1u))) & 0x80000000u) ? 28000 : -28000;
    default: return sine(ph);
    }
}
static void g_wave(uint32_t c, uint32_t t, uint32_t l)   /* the wave, running; its second copy when detuned; the shape beside it */
{
    int32_t x, py = 0, py2 = 0, amp = 22 + (int32_t)(l * 10u / 255u);
    for (x = 0; x <= 150; x += 2) {
        uint32_t ph = (uint32_t)x * 57266230u + t * 2500000u;              /* two cycles across, drifting */
        int32_t y = 110 - wave_at(c, ph) * amp / 32768, y2 = 110 - wave_at(c, ph + (uint32_t)x * 80000u * chan[c].detune / 4u + t * 900000u) * amp / 32768;
        if (x && chan[c].detune && chan[c].wave <= W_SINE) cv_line(84 + x - 2, py2, 84 + x, y2, shade(c, 110));
        if (x) { cv_line(84 + x - 2, py, 84 + x, y, shade(c, 255)); cv_line(84 + x - 2, py + 1, 84 + x, y + 1, shade(c, 255)); }
        py = y; py2 = y2;
    }
    cv_rect(84, 110, 152, 1, C_SLOT);
    {
        int32_t sz = 14 + (int32_t)(l * 4u / 255u);
        shape_solid(40, 106, sz, sz * 9 / 5, chan[c].wave % W_N, rot[c], c, l, 1, shade(c, 170u + l * 85u / 255u));
    }
}
static void g_amp(uint32_t c, uint32_t t, uint32_t l)    /* attack, decay, sustain, release: filled */
{
    int32_t x, py = 0, top = 76, a = 4 + (int32_t)chan[c].attack * 50 / 100, d, s, r = 6 + (int32_t)chan[c].release * 60 / 100, xs;
    int legacy = !chan[c].decay;
    d = legacy ? 12 : 8 + (int32_t)chan[c].decay * 60 / 100;
    s = legacy ? GB - (GB - top) * 7 / 10 : GB - (GB - top) * (int32_t)chan[c].sus / 100;
    xs = a + d + 40;
    for (x = 0; x < GW; x++) {
        int32_t y;
        uint32_t k = 46u + l * 60u / 255u;
        if (x < a) y = GB - (GB - top) * x / a;
        else if (x < a + d) { int32_t q = (a + d - x) * 256 / d; y = s - (s - top) * q / 256 * q / 256; }   /* a curve: fast, then slower */
        else if (x < xs && (legacy || chan[c].sus)) y = s;
        else if (x < xs + r && (legacy || chan[c].sus)) y = s + (GB - s) * (x - xs) / r;
        else y = GB;
        if (l > 20u) { int32_t q = x - (int32_t)(t / 5u % (uint32_t)GW); if (q < 0) q = -q; if (q < 10) k += (uint32_t)(10 - q) * 9u; }
        if (y < GB) cv_rect(GX + x, y, 1, GB - y, shade(c, k));
        if (x) { cv_line(GX + x - 1, py, GX + x, y, shade(c, 255)); cv_line(GX + x - 1, py - 1, GX + x, y - 1, shade(c, 255)); }
        py = y;
    }
    cv_rect(GX + a, GB + 2, 1, 3, C_GREY); cv_rect(GX + a + d, GB + 2, 1, 3, C_GREY);
    if (legacy || chan[c].sus) cv_rect(GX + xs, GB + 2, 1, 3, C_GREY);
}
static void filt_curve(uint32_t c, int32_t fc, int32_t q, uint16_t col, int thick, int fill)   /* the filter of its type, the corner at fc (0..GW) */
{
    int32_t x, py = 0;
    uint32_t ft = FTYPE(c) > 2u ? 0u : FTYPE(c);
    for (x = 0; x <= GW; x += 2) {
        int32_t d = ft == 2u ? fc - x : x - fc, ad = d < 0 ? -d : d, h = d <= 0 ? 34 : d > 40 ? 0 : 34 - d * 34 / 40, y;
        if (ft == 1u) h = ad > 44 ? 0 : 34 - ad * 34 / 44;             /* a band: a hill */
        if (ad < 14) h += q * (14 - ad) / 14;
        y = GB - 4 - h;
        if (fill && h > 0) cv_rect(GX + x, y, 2, GB - 4 - y, shade(c, 44));
        if (x) { cv_line(GX + x - 2, py, GX + x, y, col); if (thick) cv_line(GX + x - 2, py + 1, GX + x, y + 1, col); }
        py = y;
    }
}
static int32_t cut_x(uint32_t c) { return 8 + (int32_t)chan[c].cutoff * (GW - 30) / 100; }
/* ---- the pictures an effect has, wherever it is shown: on its page of MAIN (in the five synths' colours) and on a
 * synth's own page (in that synth's colour, by that synth's settings). c: the synth, or NCH for all five. */
static uint16_t g_col(uint32_t c, uint32_t i, uint32_t k) { return shade(c < NCH ? c : i % NCH, k); }
static void g_ridges(uint32_t t, int32_t kind, int32_t fc, int32_t q, uint32_t c)   /* the filter: the sound's spectrum as ridges, one behind
                                                 * the other, cut where it cuts. kind: 0 open, 1 lowpass, 2 highpass, 3 bandpass; fc: 0..256 */
{
    enum { NL = 10, W = 150 };
    int32_t i, x;
    for (i = NL - 1; i >= 0; i--) {             /* from the back: each ridge hides what is behind it */
        int32_t ox = 6 + i * 7, oy = 168 - i * 7, py = 0;
        uint16_t col = c < NCH ? shade(c, 255u - (uint32_t)i * 16u) : shade((uint32_t)(i * (int32_t)NCH / NL), 255u - (uint32_t)i * 12u);
        for (x = 0; x <= W; x += 2) {
            int32_t f = x * 256 / W, d = kind == 2 ? fc - f : f - fc, ad = d < 0 ? -d : d, h = 22, y, m;
            if (kind == 3) h = ad > 50 ? 0 : 22 - ad * 22 / 50;
            else if (kind && d > 0) h = d > 44 ? 0 : 22 - d * 22 / 44;
            if (kind && ad < 20) h += q * (20 - ad) / 20;
            m = 150 + ((isin(t * 13u + (uint32_t)i * 7100u + (uint32_t)x * 900u) * 60        /* the sound in it: never the same twice */
                      + isin(t * 31u + (uint32_t)i * 12300u + (uint32_t)x * 2300u) * 46) >> 15);
            h = 2 + h * m / 256;
            y = oy - h;
            cv_rect(ox + x, y, 2, oy - y + 1, C_BLACK);
            if (x) cv_line(ox + x - 2, py, ox + x, y, col);
            py = y;
        }
    }
}
static void g_echoes(uint32_t t, int32_t v, uint32_t only)   /* the delay: each note played is its synth's shape at the left; a delay
                                                 * time later the shape again, a step on, smaller, fainter. only: one synth's notes, or NCH */
{
    int32_t period = (int32_t)(fx_dlen * 10u / 441u), fbq = (int32_t)(fx_fb >> 7), n = 0, lv = 256, h, dx, i;
    while (n < 8 && lv > 20) { lv = lv * fbq / 256; n++; }                 /* how many repeats before it is gone */
    if (n < 1) n = 1;
    dx = 176 / (n > 4 ? n : 4);
    for (i = 0; i <= n; i++)                                               /* where they will stand */
        cv_rect(40 + i * dx - 6, 152 - i * 4, 12, 1, C_SLOT);
    for (h = 0; h < 8; h++) {
        int32_t age, wv, sc;
        uint32_t cc;
        if (!note_ms[h] || (only < NCH && note_c[h] != only)) continue;
        age = (int32_t)(t - note_ms[h]);
        if (age < 0 || age > (n + 2) * period) continue;
        cc = note_c[h] < NCH ? note_c[h] : 0u; wv = note_wave[h] % W_N;
        sc = (int32_t)chan[cc].dly + v;                                    /* this synth's own send, and the amount on everything */
        if (sc > 100) sc = 100;
        for (i = n; i >= 0; i--) {                                         /* the far ones first */
            int32_t a = age - i * period, lev = 256, j, sz, pop, fade;
            if (a < 0) continue;
            for (j = 0; j < i; j++) lev = lev * fbq / 256;
            if (i) lev = lev * sc / 100;
            fade = a > 2 * period ? 0 : 256 - a * 200 / (2 * period);      /* it stands a while, then goes */
            if (fade <= 0 || lev < 6) continue;
            sz = 6 + 11 * lev / 256;
            pop = a < 140 ? (140 - a) * sz / 280 : 0;                      /* it lands with a bounce */
            shape_solid(40 + i * dx, 116 - i * 4 - pop, sz + pop, (sz + pop) * 9 / 5, (uint32_t)wv, t * 14u + (uint32_t)h * 8000u, cc,
                        (uint32_t)((80 + 175 * lev / 256) * fade / 256), 1, shade(cc, (uint32_t)(110 + 145 * lev / 256 * fade / 256)));
        }
    }
}
static void g_hall(uint32_t t, int32_t sz, int32_t v, uint32_t c)   /* the reverb: a hall you fly into; sz: its size; v: how much goes in */
{
    int32_t hw = 44 + sz * 74 / 100, hh = 22 + sz * 26 / 100, n = 3 + sz * 9 / 100, ph = (int32_t)(t / 5u % 120u), i;
    int32_t vx = 120, vy = 108, fw = hw * 256 / (256 + n * 120), fh = hh * 256 / (256 + n * 120);
    uint32_t k = 70u + (uint32_t)(v > 100 ? 100 : v) * 185u / 100u;
    cv_rect(vx - fw, vy - fh, fw * 2, fh * 2, g_col(c, (uint32_t)n, 40));   /* the far wall */
    cv_line(vx - hw, vy - hh, vx - fw, vy - fh, C_GREY); cv_line(vx + hw, vy - hh, vx + fw, vy - fh, C_GREY);
    cv_line(vx - hw, vy + hh, vx - fw, vy + fh, C_GREY); cv_line(vx + hw, vy + hh, vx + fw, vy + fh, C_GREY);
    for (i = n; i >= 0; i--) {                  /* the arches, coming at you */
        int32_t den = 256 + i * 120 - ph, w, h;
        if (den < 256) den = 256;
        w = hw * 256 / den; h = hh * 256 / den;
        cv_frame(vx - w, vy - h, w * 2, h * 2, i < 2 ? 2 : 1, g_col(c, (uint32_t)i, k - (uint32_t)(i * 120 - ph) * (k - 40u) / (uint32_t)(n * 120 + 1)));
    }
    cv_frame(vx - hw, vy - hh, hw * 2, hh * 2, 2, c < NCH ? shade(c, k) : C_SILK);
    for (i = 0; i < 4 + v / 5; i++) {           /* dust in the air */
        uint32_t r = (uint32_t)i * 2654435761u + 99u;
        int32_t px = vx + (int32_t)(r % (uint32_t)(hw * 2 - 4)) - hw + 2, py = vy + (int32_t)((r >> 12) % (uint32_t)(hh * 2 - 4)) - hh + 2;
        cv_pset(px + (isin(t * 12u + r) * 4 >> 15), py + (isin(t * 9u + (r >> 8)) * 4 >> 15), C_SILK);
    }
}
static void g_shaper(uint32_t t, int32_t drive, uint32_t type, uint16_t col)   /* saturation: a clean wave, and what the drive makes of it */
{
    int32_t x, py = 0, py2 = 0, g = 256 + drive * 20;
    cv_rect(GX, 110, GW, 1, C_SLOT);
    for (x = 0; x <= GW; x += 2) {
        int32_t in = sine((uint32_t)x * 37000000u + t * 3000000u) * 2 / 3, v = in * g >> 8, t3, y, y2;
        if (type == 2u) { if (v > 18000) v = 36000 - v; else if (v < -18000) v = -36000 - v; }
        if (type) v = v > 18000 ? 18000 : v < -18000 ? -18000 : v;
        else {
            if (v > 27000) v = 27000; else if (v < -27000) v = -27000;
            t3 = ((v * v) >> 15) * v;
            v = v - (int32_t)(MULS(t3, 16089) >> 30);
        }
        y = 110 - in * 36 / 32768; y2 = 110 - v * 36 / 18000;
        if (x) { cv_line(GX + x - 2, py, GX + x, y, C_GREY); cv_line(GX + x - 2, py2, GX + x, y2, col); cv_line(GX + x - 2, py2 + 1, GX + x, y2 + 1, col); }
        py = y; py2 = y2;
    }
}
static void g_lofi(uint32_t t)                  /* lofi: a wave held in steps (the rate) on coarse levels (the bits) */
{
    int32_t x, hold = 2 + (int32_t)cur.lofi_rate * 14 / 100, step = 1 + (int32_t)cur.lofi_bits * 14 / 100, py = 0, pc = 0, v = 0;
    cv_rect(GX, 110, GW, 1, C_SLOT);
    for (x = 0; x <= GW; x += 2) {
        int32_t clean = 110 - (sine((uint32_t)x * 30000000u + t * 2000000u) * 34 >> 15), y;
        if (!(x % (hold * 2))) v = ((clean - 110) / step) * step + 110;
        y = cur.lofi_mix ? v : clean;
        if (x) { cv_line(GX + x - 2, pc, GX + x, clean, C_GREY); cv_line(GX + x - 2, py, GX + x, y, shade((uint32_t)(x / (hold * 2)) % NCH, 90u + cur.lofi_mix * 165u / 100u)); }
        pc = clean; py = y;
    }
}
static void g_tape(uint32_t t)                  /* LOFI: a tape loop, line art: two reels turning slowly (unsteadily with wow), the tape
                                                 * round the guide rollers and under the head, trembling with flutter, duller with age,
                                                 * broken where it fails (the head's light goes red) */
{
    static const int32_t RX[2] = {62, 178}, PACK[2] = {25, 19};
    int32_t wow = cur.tp_wow, fl = cur.tp_flut, age = cur.tp_age, fail = cur.tp_fail, k, j, x, py = 0;
    uint32_t r = (t >> 5) * 2654435761u + 7u, drop = 0;
    uint16_t tape = RGB(240 - age * 110 / 100, 160 - age * 70 / 100, 48 + age * 20 / 100), pack = RGB(70, 52, 30);
    int32_t wob = isin(t * 7u) * wow / 40 + isin(t * 90u) * fl / 160;   /* the speed's unsteadiness */
    for (k = 0; k < 2; k++) {                   /* the reels */
        int32_t cx = RX[k], cy = 92;
        uint32_t a = t * (k ? 16u : 12u) + (uint32_t)(wob * (k ? 4 : 3));   /* ~0.2 turns a second; the smaller pack turns faster */
        cv_ring(cx, cy, 30, C_SILK);
        for (j = 12; j <= PACK[k]; j += 3) cv_ring(cx, cy, j, pack);   /* the tape wound on it */
        cv_ring(cx, cy, PACK[k], tape);
        cv_ring(cx, cy, 9, C_SILK); cv_ring(cx, cy, 5, C_GREY); cv_rect(cx - 1, cy - 1, 3, 3, C_SILK);   /* the hub */
        for (j = 0; j < 3; j++) {               /* its three teeth */
            uint32_t aa = a + (uint32_t)j * 21845u;
            cv_line(cx + (icos(aa) * 9 >> 15), cy + (isin(aa) * 9 >> 15), cx + (icos(aa) * 13 >> 15), cy + (isin(aa) * 13 >> 15), C_SILK);
        }
        a += 10923u;                            /* and a mark on the flange, so you see it turn */
        cv_line(cx + (icos(a) * (PACK[k] + 3) >> 15), cy + (isin(a) * (PACK[k] + 3) >> 15), cx + (icos(a) * 27 >> 15), cy + (isin(a) * 27 >> 15), C_SILK);
    }
    /* the loop sign between them */
    cv_line(111, 76, 129, 76, C_SILK); cv_line(129, 76, 132, 79, C_SILK); cv_line(132, 79, 132, 83, C_SILK); cv_line(132, 83, 129, 86, C_SILK);
    cv_line(129, 86, 111, 86, C_SILK); cv_line(111, 86, 108, 83, C_SILK); cv_line(108, 83, 108, 79, C_SILK);
    cv_line(111, 76, 115, 72, C_SILK); cv_line(111, 76, 115, 80, C_SILK);
    for (j = 0; j < 5; j++) cv_rect(102 + j * 9, 66, 1, j == 2 ? 5 : 3, C_GREY);   /* a little scale above it */
    /* the tape path: off the left reel, round the rollers, under the head, back up to the right reel */
    cv_line(RX[0] - (PACK[0] * 21 >> 5), 92 + (PACK[0] * 24 >> 5), 47, 133, tape);
    cv_line(50, 137, 84, 145, tape);
    cv_line(156, 145, 190, 137, tape);
    cv_line(193, 133, RX[1] + (PACK[1] * 21 >> 5), 92 + (PACK[1] * 24 >> 5), tape);
    for (x = 84; x <= 156; x += 2) {            /* the stretch under the head: flutter shakes it, failing breaks it */
        int32_t y = 145 + (isin(t * 90u + (uint32_t)x * 5000u) * fl / 100 * 2 >> 15) + (isin(t * 7u + (uint32_t)x * 900u) * wow / 100 >> 15);
        int gap;
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        gap = fail && (int32_t)((r >> 8) % 500u) < fail && ((x / 6 + (int32_t)(t >> 6)) & 3) == 0;
        if (gap && x > 110 && x < 130) drop = 1;
        if (x > 84 && !gap) cv_line(x - 2, py, x, y, tape);
        if (gap) cv_pset(x, 128 + (int32_t)((r >> 4) % 24u), C_SILK);   /* sparks where it breaks */
        py = y;
    }
    cv_ring(50, 133, 4, C_SILK);                /* the rollers */
    cv_ring(84, 140, 5, C_SILK); cv_rect(83, 139, 3, 3, C_SILK);
    cv_ring(156, 140, 5, C_SILK); cv_line(153, 143, 159, 137, C_SILK);
    cv_ring(190, 133, 4, C_SILK);
    cv_frame(113, 135, 15, 9, 1, C_SILK);      /* the head, its light */
    cv_rect(119, 128, 3, 4, drop ? RGB(240, 60, 70) : C_AMBER);
}
static void g_master(uint32_t t)                /* mastering: in against out, bent at the threshold; and how hard it is pulling down, now */
{
    int32_t amt = cur.comp > 100u ? 100 : cur.comp, thr = 70 - amt * 56 / 100, ratio = 1 + amt / 12, x, gr = 32768 - (int32_t)fx_gr, i;
    (void)t;
    cv_frame(20, 66, 86, 86, 1, C_SLOT);
    cv_line(20, 151, 105, 66, C_SLOT);                                      /* (1 : 1) */
    for (x = 0; x < 84; x += 2) {
        int32_t o = x <= thr ? x : thr + (x - thr) / ratio;
        cv_rect(21 + x, 150 - o, 2, 2, shade(2, 255));
    }
    cv_rect(21 + thr, 150 - thr - 3, 2, 8, C_AMBER);                         /* the threshold */
    cv_text(124, 70, &FONT_S, "GAIN REDUCTION", C_GREY);
    cv_rect(124, 92, 104, 12, C_SLOT);
    if (gr < 0) gr = 0;
    cv_rect(124, 92, gr * 104 / 32768 * 2 > 104 ? 104 : gr * 104 / 32768 * 2, 12, shade(1, 255));
    cv_text(124, 114, &FONT_S, "LIMITER ON", C_GREY);
    for (i = 0; i < NCH; i++)                                                /* the five synths' levels, for company */
        cv_rect(124 + (int32_t)i * 21, 150 - (int32_t)(lvl[i] * 16u / 255u), 17, (int32_t)(lvl[i] * 16u / 255u) + 1, shade((uint32_t)i, 200));
}

static void g_fenv(uint32_t c, uint32_t t, uint32_t l)    /* the filter where it rests, where the envelope throws it, and it falling back */
{
    int32_t fc = cut_x(c), push = (int32_t)chan[c].env_filt * 60 / 100, q = (int32_t)chan[c].reso * 32 / 30;
    uint32_t age = hit_ms[c] ? t - hit_ms[c] : 99999u, span = 150u + chan[c].env_rate * chan[c].env_rate / 4u * 3u;
    int32_t now = age < span ? push * (int32_t)(span - age) / (int32_t)span : chan[c].env_rate ? 0 : push * 2 / 5;
    (void)l;
    filt_curve(c, fc, q, C_GREY, 0, 0);
    if (push) filt_curve(c, fc + push, q, shade(c, 90), 0, 0);
    filt_curve(c, fc + now, q, shade(c, 255), 1, 1);
    if (chan[c].drop) {                                                     /* PITCH: the fall at the start of the note */
        int32_t h = (int32_t)chan[c].drop * 50 / 100, x;
        for (x = 0; x < 40; x += 2) cv_rect(GX + 4 + x, 74 + (50 - h) + h * x / 40 * x / 40, 2, 2, C_AMBER);
    }
}
static void g_lfo(uint32_t c, uint32_t t, uint32_t l)     /* its wave going by; a ball where it is now */
{
    int32_t x, py = 0, amp = 4 + (int32_t)chan[c].lfo_depth * 32 / 100, bx = GX + GW / 2;
    (void)t; (void)l;
    cv_rect(GX, 110, GW, 1, C_SLOT);
    for (x = 0; x <= GW; x += 2) {
        int32_t y = 110 - lfo_val(lfo_ph[c] + (uint32_t)(x - GW / 2) * 37000000u, MSHAPE(c)) * amp / 32768;
        if (x) { cv_line(GX + x - 2, py, GX + x, y, shade(c, chan[c].lfo_depth ? 255 : 90)); if (chan[c].lfo_depth) cv_line(GX + x - 2, py + 1, GX + x, y + 1, shade(c, 255)); }
        py = y;
    }
    {
        int32_t by = 110 - lfo_val(lfo_ph[c], MSHAPE(c)) * amp / 32768;
        cv_rect(bx - 4, by - 4, 9, 9, C_BLACK); cv_frame(bx - 4, by - 4, 9, 9, 2, C_AMBER);
        cv_rect(bx, 72, 1, 80, C_SLOT);
    }
}
static void g_noise(uint32_t c, uint32_t t, uint32_t l)   /* its level: a field of sparks; what it does to the filter: a jagged line */
{
    uint32_t r = 2463534242u + (t >> 5) * 40503u, n = chan[c].noise * 5u, i, per = sh_per[c] * 16u / 44u + 1u;
    int32_t x, py = 0, w = (int32_t)(per > 200u ? 40u : per / 5u + 3u);
    (void)l;
    for (i = 0; i < n; i++) {
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        cv_pset(GX + (int32_t)((r >> 8) % (uint32_t)GW), 72 + (int32_t)((r >> 20) % 78u), (r & 7u) ? C_SILK : shade(c, 255));
    }
    if (chan[c].nz_depth)
        for (x = 0; x <= GW; x += w) {
            uint32_t rr = ((uint32_t)(x / w) + t / per) * 2654435761u;
            int32_t y;
            rr ^= rr >> 15;
            y = 110 - ((int32_t)(rr & 255u) - 128) * (int32_t)chan[c].nz_depth * 34 / 100 / 128;
            if (x) { cv_line(GX + x - w, py, GX + x - w, y, shade(c, 255)); }
            cv_rect(GX + x, y, w, 2, shade(c, 255));
            py = y;
        }
}
static void g_mod(uint32_t c, uint32_t t, uint32_t l)     /* the voices of a chorus weaving; a flanger: closer, and a comb */
{
    int32_t x, k, py[3] = {0, 0, 0}, dp = cur.ch_type ? 3 + (int32_t)cur.ch_depth * 16 / 100 : 0;
    uint32_t sp = 4u + cur.ch_rate / 4u;
    (void)l;
    for (x = 0; x <= GW; x += 2)
        for (k = 0; k < 3; k++) {
            int32_t off = k == 1 ? 0 : (k ? -dp : dp) * (isin(t * sp * 3u + (uint32_t)x * 300u + (uint32_t)k * 20000u) + 32768) / 65536 * (cur.ch_type == 2u ? 1 : 2) / 2;
            int32_t y = 110 - (sine((uint32_t)(x + off * 2) * 55000000u) * 26 >> 15) + (k - 1) * dp / 2;
            if (x && (k == 1 || (cur.ch_type && cur.ch_mix))) cv_line(GX + x - 2, py[k], GX + x, y, k == 1 ? shade(c, 255) : shade((c + (uint32_t)k + 1u) % NCH, 90u + cur.ch_mix * 165u / 100u));
            py[k] = y;
        }
}
static void draw_edit(uint32_t c, uint32_t row, uint32_t count, uint32_t t)
{
    uint32_t i, l = lvl[c], wave = chan[c].wave % W_N;
    char s[32], *p;
    uint16_t col = shade(c, 255);
    cv_begin(240, MAIN_H, C_BLACK);

    for (i = 0; i < 4u; i++) {                  /* the four knobs, where the knobs are */
        const item_t *it = &PAGES[row % NPAGE][i];
        if (it->kind != K_NONE)
            dial(30 + (int32_t)i * 60, 21, item_get(c, it), it->max, col, it->name, item_text(s, c, it));
    }
    for (i = 0; i < NPAGE; i++)                 /* which page (ALGORITHM turns them): at the bottom, in the middle */
        cv_rect(120 - (int32_t)NPAGE * 6 + (int32_t)i * 12, 171, 9, 2, i == row ? col : C_SLOT);

    if (seen_hit[c] != hit_ms[c]) {             /* played: sparks, on every page */
        seen_hit[c] = hit_ms[c];
        part_spawn(c, row == PG_WAVE ? 40 : 120, 110, 20, 0, 80);
    }
    switch (row % NPAGE) {
    case PG_WAVE: g_wave(c, t, l); break;
    case PG_AMP: g_amp(c, t, l); break;
    case PG_FENV: g_fenv(c, t, l); break;
    case PG_FILTER: {                           /* as the filter of MAIN: the ridges, in this synth's colour, cut as its filter cuts */
        int32_t fc = (int32_t)chan[c].cutoff * 256 / 100;
        if (chan[c].lfo_depth) fc += (lfo_val(lfo_ph[c], MSHAPE(c)) * (int32_t)chan[c].lfo_depth * 60 / 100) >> 15;
        g_ridges(t, FTYPE(c) == 1u ? 3 : FTYPE(c) == 2u ? 2 : 1, fc < 4 ? 4 : fc > 252 ? 252 : fc, (int32_t)chan[c].reso * 24 / 30, c);
        break;
    }
    case PG_LFO: g_lfo(c, t, l); break;
    case PG_NOISE: g_noise(c, t, l); break;
    case PG_DRIVE: g_shaper(t, chan[c].sat, 0, col); break;
    case PG_MOD: g_mod(c, t, l); break;
    case PG_DELAY: g_echoes(t, cur.del_all, c); break;
    default: g_hall(t, cur.rev_size, (int32_t)chan[c].rev + (int32_t)cur.rev_all, c); break;
    }
    part_step_draw();
    if (count > 1u) cv_text(2, 66, &FONT_S, "+", C_AMBER);

    cv_text(2, 153, &FONT_S, PAGE_NAME[row % NPAGE], col);   /* the line under it: the page; the wave and the octave */
    p = s_put(s, WAVE_NAME[wave]);
    *p++ = ' ';
    if (chan[c].octave >= 0) *p++ = '+';
    s_num(p, chan[c].octave);
    cv_text(238 - text_w(&FONT_S, s), 153, &FONT_S, s, C_GREY);
    cv_blit(0, MAIN_Y);
}

static char popup[28];
static uint32_t popup_until;
static void say(const char *a, const char *b, const char *d)
{
    char *p = s_put(popup, a);
    if (b) p = s_put(p, b);
    if (d) s_put(p, d);
    popup_until = fm1_ms + 1500u;
}
/* ---- a knob of the main page turned: what it does takes the screen for a moment, with its number.
 * The filter and its resonance: the sound's spectrum as a landscape of ridges, one behind the other, in the five
 * synths' colours, cut where the filter cuts. The delay: the repeats as pillars marching off, a pulse running along
 * them at the delay's own time. The reverb: a hall you fly into, wider and deeper the more there is. */
enum { T_FILTER, T_LEN, T_DELAY, T_REVERB };
static uint32_t take, take_ms, len_stop_ms;
static int32_t dj_kind(void) { int32_t d = (int8_t)cur.dj; return d < 0 ? 1 : d > 0 ? 2 : 0; }
static int32_t dj_fc(void) { int32_t d = (int8_t)cur.dj; return d < 0 ? (100 + d) * 256 / 100 : d * 256 / 100; }
/* LENGTH: five tails, one a synth, in its colour: how long its note rings out. A white tick on each is where the patch
 * has it: the tail's end leaves the tick as you turn, and meets it again at 12 o'clock. Below, the knob as a scale:
 * a pointer, and the middle as a target that lights when you are back (the knob rests there for a moment, too). */
static void g_length(uint32_t t)
{
    const int32_t off = len_off, X0 = 26, SY = 156;
    uint32_t c;
    int32_t x, px;
    char s[8], *q = s;
    int home = !off;
    cv_text(4, 4, &FONT_S, "LENGTH", C_SILK);
    cv_text(4, 22, &FONT_S, home ? "AS THE PATCH" : off > 0 ? "LONGER" : "SHORTER", home ? C_AMBER : C_GREY);
    if (off > 0) *q++ = '+';
    if (off < 0) *q++ = '-';
    s_num(q, off < 0 ? -off : off);
    cv_text(236 - text_w(&FONT_L, s), 2, &FONT_L, s, home ? C_AMBER : C_SILK);
    for (c = 0; c < NCH; c++) {
        int32_t mid = 52 + (int32_t)c * 20, was = chan[c].release, now = (int32_t)len_of(chan[c].release), w = now * 2, wx;
        int on = chan[c].enabled == E_ON, end = !home && (now == 0 || now == 100) && was + off != now;
        char num[2] = {(char)('1' + c), 0};
        cv_text(6, mid - 8, &FONT_S, num, on ? shade(c, 255) : C_GREY);
        cv_rect(X0, mid, 201, 1, C_SLOT);                                   /* its track: nothing .. the longest */
        for (wx = 0; wx <= w; wx++) {                                       /* the tail: full at the start, dying away */
            int32_t r = w - wx, h = w ? 1 + 7 * r / w * r / w : 1;
            cv_rect(X0 + wx, mid - h, 1, 2 * h + 1, shade(c, on ? (home ? 150u : 215u) - (uint32_t)(wx * 70 / (w + 1)) : 60u));
        }
        if (end) cv_rect(X0 + w + (now ? 2 : -3), mid - 6, 2, 13, C_AMBER); /* at its end: it goes no further */
        px = X0 + was * 2;                                                  /* where the patch has it */
        cv_rect(px, mid - 8, 1, 17, home ? C_AMBER : C_SILK);
        cv_rect(px - 1, mid - 8, 3, 1, home ? C_AMBER : C_SILK); cv_rect(px - 1, mid + 8, 3, 1, home ? C_AMBER : C_SILK);
    }
    /* the knob: -100 .. +100 across; the middle is the patch */
    cv_rect(20, SY + 6, 201, 1, C_GREY);
    for (x = -100; x <= 100; x += 25) cv_rect(120 + x, SY + 4, 1, x ? 5 : 0, C_GREY);
    if (off) cv_rect(off > 0 ? 120 : 120 + off, SY + 5, off > 0 ? off : -off, 3, C_SILK);   /* how far from the patch */
    {
        uint16_t tc = home ? C_AMBER : shade(3, 110u + (uint32_t)((t >> 3) & 63u));          /* the target: it calls you back */
        cv_rect(119, SY - 2, 3, 17, tc);
        cv_rect(113, SY - 2, 15, 1, tc); cv_rect(113, SY + 14, 15, 1, tc);
        if (home) { cv_rect(112, SY - 2, 1, 17, tc); cv_rect(128, SY - 2, 1, 17, tc); }      /* back: the frame closes */
    }
    px = 120 + off;                                                          /* the pointer */
    if (!home) { cv_rect(px - 1, SY, 3, 13, C_SILK); cv_rect(px - 3, SY, 7, 2, C_SILK); }
}
static void draw_take(uint32_t t)               /* PLAY: one of the four quick knobs turned: its picture for a moment, its number big */
{
    int32_t dj = (int8_t)cur.dj, v;
    const char *l1, *l2 = "";
    char s[8];
    cv_begin(240, MAIN_H, C_BLACK);
    if (take == T_LEN) { g_length(t); cv_blit(0, MAIN_Y); return; }
    switch (take) {
    case T_FILTER: v = dj < 0 ? -dj : dj; l1 = "FILTER"; l2 = dj < 0 ? "LOWPASS" : dj > 0 ? "HIGHPASS" : "OPEN"; break;
    case T_DELAY: v = cur.del_all; l1 = "DELAY"; break;
    default: v = cur.rev_all; l1 = "REVERB"; break;
    }
    if (take == T_FILTER) g_ridges(t, dj_kind(), dj_fc(), (int32_t)cur.dj_reso * 24 / 100, NCH);
    else if (take == T_DELAY) g_echoes(t, v, NCH);
    else g_hall(t, ((int32_t)cur.rev_all * 2 + (int32_t)cur.rev_size) / 3, v, NCH);
    cv_text(4, 4, &FONT_S, l1, C_SILK);
    cv_text(4, 22, &FONT_S, l2, C_AMBER);
    s_num(s, v);
    cv_rect(232 - text_w(&FONT_L, s), 0, text_w(&FONT_L, s) + 8, 36, C_BLACK);
    cv_text(236 - text_w(&FONT_L, s), 2, &FONT_L, s, C_SILK);
    cv_blit(0, MAIN_Y);
}
static void draw_mpage(uint32_t t)              /* a page of MAIN (ALGORITHM): four dials under the four knobs, the effect's picture */
{
    uint32_t i;
    char s[16];
    cv_begin(240, MAIN_H, C_BLACK);
    for (i = 0; i < 4u; i++) {
        const item_t *it = &MPAGES[mpage % NMPAGE][i];
        if (it->kind != K_NONE)
            dial(30 + (int32_t)i * 60, 21, item_get(0, it), it->max, shade(i, 255), it->name, item_text(s, 0, it));
    }
    for (i = 0; i < NMPAGE; i++)                /* which page: at the bottom, in the middle */
        cv_rect(120 - (int32_t)NMPAGE * 6 + (int32_t)i * 12, 171, 9, 2, i == mpage ? C_AMBER : C_SLOT);
    if (MPAGES[mpage % NMPAGE][3].kind == K_ONOFF && (cur.fx_off >> MPAGES[mpage % NMPAGE][3].off & 1u)) {   /* switched off: says so */
        cv_text(120 - text_w(&FONT_L, "OFF") / 2, 92, &FONT_L, "OFF", C_GREY);
        goto foot;
    }
    switch (mpage) {
    case MP_SAT: g_shaper(t, cur.drive, cur.drv_type, shade(1, 255)); break;
    case MP_FILTER: g_ridges(t, dj_kind(), dj_fc(), (int32_t)cur.dj_reso * 24 / 100, NCH); break;
    case MP_MOD: g_mod(0, t, 0); break;
    case MP_DELAY: g_echoes(t, cur.del_all, NCH); break;
    case MP_REVERB: g_hall(t, cur.rev_size, cur.rev_all, NCH); break;
    case MP_BITS: g_lofi(t); break;
    case MP_LOFI: g_tape(t); break;
    default: g_master(t); break;
    }
foot:
    cv_text(2, 153, &FONT_S, MPAGE_NAME[mpage % NMPAGE], C_AMBER);
    cv_blit(0, MAIN_Y);
}

/* the save page (SAVE): the 32 slots; what to do; the name */
enum { A_SAVE, A_LOAD, A_ERASE, A_N };
static const char *const ACT_NAME[A_N] = {"SAVE", "LOAD", "ERASE"};
static uint32_t sv_open, sv_slot, sv_act, sv_confirm, sv_adj, sv_noun;
static char sv_name[16];
/* The name, three ways, quickest first. GLO: a random one (two words), as often as you like. KNOB 3 and 4: its two
 * words. The keys: letters, A on the lowest key up to Z, the top key a space (the screen shows which); OCT- takes
 * the last letter away, OCT+ all of it; ALGORITHM moves the cursor, SELECT turns the letter under it (digits too).
 * A name that came whole (random, words, the patch's own) is "fresh": the first letter typed replaces it. */
static uint32_t sv_cur, sv_fresh, sv_key_ms, sv_key = 99, sv_typed;
static uint32_t sv_len(void) { uint32_t n = 0; while (n < 15u && sv_name[n]) n++; return n; }
static void sv_words(void)
{
    char *p = s_put(sv_name, ADJ[sv_adj & 15u]);
    *p++ = ' ';
    s_put(p, NOUN[sv_noun & 15u]);
    sv_cur = sv_len(); sv_fresh = 1;
}
static void sv_type(uint32_t k)                 /* key k of the 27: a letter at the cursor */
{
    uint32_t n, i;
    char ch = k < 26u ? (char)('A' + k) : ' ';
    sv_key = k; sv_key_ms = fm1_ms | 1u;
    if (sv_fresh) { sv_name[0] = 0; sv_cur = 0; sv_fresh = 0; }
    n = sv_len();
    if (n >= 15u || (ch == ' ' && !sv_cur)) return;
    for (i = n + 1u; i > sv_cur; i--) sv_name[i] = sv_name[i - 1u];
    sv_name[sv_cur++] = ch;
}
static void sv_back(void)                       /* the letter before the cursor goes */
{
    uint32_t n = sv_len(), i;
    sv_fresh = 0;
    if (!sv_cur) return;
    for (i = sv_cur - 1u; i < n; i++) sv_name[i] = sv_name[i + 1u];
    sv_cur--;
}
static void sv_turn(int32_t d)                  /* the letter under the cursor: A..Z, 0..9, a space, round */
{
    static const char SET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ";
    uint32_t n = sv_len(), at = sv_cur < n ? sv_cur : n, i;
    int32_t v;
    sv_fresh = 0;
    if (at >= 15u) return;
    if (at == n) { sv_name[n] = d > 0 ? 'A' : ' '; sv_name[n + 1u] = 0; d += d > 0 ? -1 : 0; }
    for (i = 0; SET[i] && SET[i] != sv_name[at]; i++) {}
    v = ((int32_t)i + d) % 37; if (v < 0) v += 37;
    sv_name[at] = SET[v];
    sv_cur = at;
}
static void sv_tidy(void)                       /* before it is saved: no spaces at the ends; never empty */
{
    uint32_t n = sv_len(), i;
    while (n && sv_name[n - 1u] == ' ') sv_name[--n] = 0;
    while (sv_name[0] == ' ') for (i = 0; i < n; i++) sv_name[i] = sv_name[i + 1u];
    if (!sv_name[0]) sv_words();
}
static void draw_save(void)
{
    int32_t r, x = 0;
    uint32_t a, k, n = sv_len();
    char s[32], *p;
    int naming = sv_act == A_SAVE;
    cv_begin(240, MAIN_H, C_BLACK);
    if (naming) {                               /* the name, big; fresh: on amber (the next letter replaces it); else a cursor */
        const felucca_font_t *f = text_w(&FONT_L, sv_name) <= 228 ? &FONT_L : &FONT_S;
        int32_t w = text_w(f, sv_name), x0 = 120 - w / 2, y0 = f == &FONT_L ? 2 : 10, hgt = f == &FONT_L ? 32 : 16, cx;
        char part[16];
        if (sv_fresh && n) cv_rect(x0 - 5, y0 - 1, w + 10, hgt + 3, C_AMBER);
        cv_text(x0, y0, f, sv_name, sv_fresh ? C_BLACK : C_SILK);
        for (k = 0; k < sv_cur && k < n; k++) part[k] = sv_name[k];
        part[k] = 0;
        cx = x0 + text_w(f, part);
        if (!sv_fresh && (fm1_ms >> 8 & 1u)) cv_rect(cx, y0 + hgt + 1, sv_cur < n ? (f == &FONT_L ? 14 : 8) : 8, 2, C_AMBER);
    } else {
        const char *t = sv_act == A_LOAD ? "LOAD A PATCH" : "ERASE A PATCH";
        cv_text(120 - text_w(&FONT_S, t) / 2, 10, &FONT_S, t, C_GREY);
    }
    for (r = -1; r <= 1; r++) {                 /* the slots: the one it goes to in the middle */
        int32_t u = (int32_t)sv_slot + r, y = 46 + (r + 1) * 20;
        int here = u >= 0 && (uint32_t)u == patch_i - NFACTORY && patch_i >= NFACTORY;
        if (u < 0 || u >= (int32_t)NUSER) continue;
        p = patch_tag(s, NFACTORY + (uint32_t)u);
        *p++ = ' '; *p++ = ' ';
        p = s_put(p, B_USED((uint32_t)u) ? B_P((uint32_t)u).name : "-");
        if (here) s_put(p, dirty ? " *" : "");
        if (!r) cv_rect(0, y - 1, 240, 18, C_AMBER);
        cv_text(8, y, &FONT_S, s, !r ? C_BLACK : B_USED((uint32_t)u) ? (here ? C_SILK : RGB(150, 150, 142)) : C_GREY);
    }
    for (a = 0; a < A_N; a++) {                 /* what SAVE does */
        int32_t w = text_w(&FONT_S, ACT_NAME[a]);
        x = 28 + (int32_t)a * 72;
        if (a == sv_act) cv_frame(x - 6, 109, w + 12, 22, 1, C_AMBER);
        cv_text(x, 112, &FONT_S, ACT_NAME[a], a == sv_act ? C_AMBER : C_GREY);
    }
    {                                           /* the FM-1's 27 keys, F to G, and the letter each one types */
        const int32_t KY = 136;
        int32_t wi = 0;
        for (k = 0; k < 27u; k++) {
            static const uint8_t BLACK[12] = {0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0};   /* from F */
            char ch[2] = {k < 26u ? (char)('A' + k) : '_', 0};
            int hit = sv_key == k && sv_key_ms && fm1_ms - sv_key_ms < 220u;
            uint16_t ink = !naming ? C_SLOT : hit ? C_BLACK : BLACK[k % 12u] ? C_AMBER : C_SILK;
            if (BLACK[k % 12u]) {               /* a black key: above, between its neighbours */
                int32_t bx = wi * 15 - 6;
                cv_rect(bx, KY, 12, 17, hit ? C_AMBER : C_SLOT);
                cv_text(bx + 6 - text_w(&FONT_S, ch) / 2, KY, &FONT_S, ch, ink);
            } else {
                int32_t wx = wi * 15;
                if (hit) cv_rect(wx + 1, KY + 19, 13, 18, C_AMBER);
                cv_frame(wx, KY + 18, 15, 20, 1, naming ? C_GREY : C_SLOT);
                cv_text(wx + 8 - text_w(&FONT_S, ch) / 2, KY + 20, &FONT_S, ch, ink);
                wi++;
            }
        }
    }
    cv_blit(0, MAIN_Y);
}

static void draw_text(uint32_t hold, uint32_t first)   /* POLY5 left, the mode right; the lines below */
{
    static const char L[5][2] = {"P", "O", "L", "Y", "5"};
    char s[32], *p;
    uint32_t i;
    for (i = 0; i < 5u; i++)                    /* five letters, five synths */
        draw_text_box(i * 10u, 2, 10, &FONT_S, L[i], shade(i, 255), 0);
    if (sq_open && !sv_open && !hold) {         /* the sequencer: REC / PLAY / SEQ at the top; under the steps, the step and its notes */
        const uint16_t red = RGB(255, 70, 60);
        uint32_t j;
        draw_text_box(90, 2, 150, &FONT_S, sq_rec ? "REC" : sq_play ? "PLAY" : "SEQ", sq_rec ? red : C_AMBER, 2);
        p = s_put(s, "STEP ");
        p = s_num(p, (int32_t)sq_cur + 1);
        for (j = 0; j < SQ_NN; j++)
            if (sq.note[sq_cur][j]) {
                *p++ = ' '; if (!j) *p++ = ' ';
                p = s_put(p, NOTE_NAME[sq.note[sq_cur][j] % 12u]);
                p = s_num(p, (int32_t)sq.note[sq_cur][j] / 12 - 1);
            }
        if (!sq_top(sq_cur)) s_put(p, "  -");
        draw_text_box(0, 200, 240, &FONT_S, s, sq_rec ? red : C_SILK, 1);
        draw_text_box(0, 220, 240, &FONT_S, popup[0] ? popup : "", C_AMBER, 1);
        return;
    }
    if (sv_open) {
        draw_text_box(90, 2, 150, &FONT_S, "SAVE", C_AMBER, 2);
        (void)p;
        draw_text_box(0, 200, 240, &FONT_S, sv_act == A_SAVE ? "GLO RANDOM NAME  KEYS TYPE" : "K1 SLOT  K2 WHAT  HOME BACK", sv_act == A_SAVE ? RGB(150, 150, 142) : C_GREY, 1);
        if (popup[0]) draw_text_box(0, 220, 240, &FONT_S, popup, C_AMBER, 1);
        else draw_text_box(0, 220, 240, &FONT_S, sv_act == A_SAVE ? "K3 K4 WORDS  OCT- DELETE" : "", C_GREY, 1);
        return;
    }
    if (hold) {
        p = s_put(s, "SYNTH ");
        *p++ = (char)('1' + first); *p = 0;
        draw_text_box(90, 2, 150, &FONT_S, s, shade(first, 255), 2);
        draw_text_box(0, 200, 240, &FONT_S, chan[first].enabled == E_ON ? "ON" : chan[first].enabled == E_OFF ? "SILENT: A REST IN THE CYCLE" : "SKIP: NOT IN THE CYCLE", chan[first].enabled == E_ON ? C_GREY : C_AMBER, 1);
    } else {
        if (arp) {                              /* ARP UP  RR */
            static const char *const MSHORT[M_N] = {"POLY", "RR", "UNISON", "RANDOM"};
            char *q = s_put(s, ARP_NAME[arp]);
            q = s_put(q, " / ");
            s_put(q, MSHORT[mode]);
            draw_text_box(70, 2, 170, &FONT_S, s, C_AMBER, 2);
        } else
        draw_text_box(90, 2, 150, &FONT_S, MODE_NAME[mode], C_SILK, 2);
        p = patch_tag(s, patch_i);              /* the patch: F01 ROUND ROBIN, U03 FUZZY CUBE *, with unsaved changes */
        *p++ = ' '; *p++ = ' ';
        p = s_put(p, cur_name);
        if (dirty) s_put(p, " *");
        draw_text_box(0, 200, 240, &FONT_S, s, RGB(150, 150, 142), 1);
    }
    if (popup[0]) draw_text_box(0, 220, 240, &FONT_S, popup, C_AMBER, 1);
    else if (!hold && len_off && !sq_open && !sv_open) {   /* LENGTH is off the patch: said, so it is not forgotten */
        char t2[20], *q = s_put(t2, len_off > 0 ? "LENGTH +" : "LENGTH -");
        s_num(q, len_off < 0 ? -len_off : len_off);
        draw_text_box(0, 220, 240, &FONT_S, t2, RGB(150, 100, 30), 1);
    }
    else draw_text_box(0, 220, 240, &FONT_S, hold ? "ALG PAGE  GLO NEW  HOME BACK" : "", C_GREY, 1);
}

/* ---- the sequencer, as the FM-1's other firmwares have it (SEQ: its page; PLAY; REC stopped: step by step, REC
 * playing: live; REC held: clear), with POLY5's own turn: it keeps the pitches only. Each step goes through the play
 * mode like a key you press, so the round robin hands the pattern round the synths, RANDOM scatters it, a SILENT
 * synth makes a rest: the same notes, voiced anew every time. */
static const uint8_t SQ_SPQ[6] = {1, 2, 3, 4, 6, 8};                       /* steps a quarter note */
static const char *const SQ_DIV[6] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
static uint32_t sq_ext_ms, sq_ext_ticks, sq_ext_due;                        /* a MIDI clock: its last tick, ticks since a step */

static void sq_notes_off(void)                   /* all of them, now */
{
    uint32_t i;
    for (i = 0; i < 12u; i++)
        if (sq_snd[i].note) { route_off(sq_snd[i].note); sq_snd[i].note = 0; }
}
static void sq_notes_due(void)                   /* those whose gate has run out */
{
    uint32_t i;
    for (i = 0; i < 12u; i++)
        if (sq_snd[i].note && (int32_t)(fm1_ms - sq_snd[i].off) >= 0) { route_off(sq_snd[i].note); sq_snd[i].note = 0; }
}
static uint32_t sq_step_q8(uint32_t step)        /* a step's length, ms * 256; swing: the even ones longer, the odd shorter */
{
    uint32_t q8 = 60000u * 256u / ((uint32_t)sq.bpm * SQ_SPQ[sq.div % 6u]);
    uint32_t sw = q8 / 300u * sq.swing;
    return step & 1u ? q8 - sw : q8 + sw;
}
static void sq_fire(void)                        /* the step at sq_pos sounds; on to the next */
{
    uint32_t i, j, who = 0, any = 0;
    uint32_t off = (fm1_ms + ((sq_step_q8(sq_pos) >> 8) * sq.gate / 20u)) | 1u;   /* the gate: so much of a step, or of several */
    sq_notes_due();
    for (i = 0; i < SQ_NN; i++)
        if (sq.note[sq_pos][i]) {
            uint32_t n = sq.note[sq_pos][i], m, slot = 12;
            for (j = 0; j < 12u; j++) {          /* the same note still held: let go first, so it strikes again */
                if (sq_snd[j].note == n) { route_off(n); sq_snd[j].note = 0; }
                if (!sq_snd[j].note && slot == 12u) slot = j;
            }
            m = route_on(n, 110, 1, 0);
            if (slot < 12u) { sq_snd[slot].note = (uint8_t)n; sq_snd[slot].off = off; }
            else route_off(n);                   /* (no room to remember it: it is not held) */
            who |= m; any = 1;
        }
    if (any) {
        uint32_t c;
        sq_who[sq_pos] = 255;                    /* (none: all rested) */
        for (c = 0; c < NCH; c++) if (who >> c & 1u) { sq_who[sq_pos] = (uint8_t)c; break; }
        if (sq_who[sq_pos] == 255u)
            for (c = 0; c < NCH; c++) if (who >> (8u + c) & 1u) { sq_who[sq_pos] = (uint8_t)(16u + c); break; }
        sq_hit[sq_pos] = fm1_ms | 1u;
    }
    if (!(sq_pos % SQ_SPQ[sq.div % 6u])) sq_beat_ms = fm1_ms | 1u;
    sq_pos = (sq_pos + 1u) % (sq.len ? sq.len : 1u);
    sq_fresh = 0;
}
static void sq_start(void)
{
    lfo_restart = 1;
    sq_pos = 0; sq_play = 1; sq_ext_ticks = 0; sq_ext_due = 1;
    sq_next_q8 = fm1_ms << 8;
}
static void sq_stop(void)
{
    sq_play = 0;
    sq_notes_off();
}
static void sq_tick(void)                        /* from the main loop and from app_idle: often */
{
    uint32_t ext = sq_ext_ms && fm1_ms - sq_ext_ms < 500u;
    sq_notes_due();
    if (!sq_play) return;
    if (ext) {                                   /* a MIDI clock comes in: it is the tempo (24 ticks a quarter) */
        if (sq_ext_due) { sq_ext_due = 0; sq_fire(); }
        sq_next_q8 = fm1_ms << 8;
        return;
    }
    while ((int32_t)((fm1_ms << 8) - sq_next_q8) >= 0) {
        uint32_t st = sq_pos;
        sq_fire();
        sq_next_q8 += sq_step_q8(st);
        if ((int32_t)((fm1_ms << 8) - sq_next_q8) > (int32_t)(200u << 8)) sq_next_q8 = fm1_ms << 8;   /* (fell behind: not a burst) */
    }
}
static void sq_clock(uint32_t st)                /* 0xF8 clock, 0xFA start, 0xFB continue, 0xFC stop */
{
    if (st == 0xF8u) {
        uint32_t per = 24u / SQ_SPQ[sq.div % 6u];
        sq_ext_ms = fm1_ms | 1u;
        if (++sq_ext_ticks >= per) { sq_ext_ticks = 0; sq_ext_due = 1; }
    } else if (st == 0xFAu) sq_start();
    else if (st == 0xFBu) sq_play = 1;
    else if (st == 0xFCu) sq_stop();
}
static void sq_key(uint32_t note, int down)      /* a key, while REC is on */
{
    uint32_t st, i;
    if (!sq_rec) {                               /* on its page: a key switches its note in the step under the cursor on and off */
        if (!sq_open || sv_open || solo < NCH || !down) return;
        st = sq_cur;
        for (i = 0; i < SQ_NN; i++)
            if (sq.note[st][i] == note) {        /* it is in: out */
                for (; i + 1u < SQ_NN; i++) sq.note[st][i] = sq.note[st][i + 1u];
                sq.note[st][SQ_NN - 1u] = 0;
                sq_gen++;
                return;
            }
        for (i = 0; i < SQ_NN; i++)
            if (!sq.note[st][i]) { sq.note[st][i] = (uint8_t)note; break; }
        if (i == SQ_NN) sq.note[st][SQ_NN - 1u] = (uint8_t)note;   /* full: the last one makes room */
        sq_who[st] = 255;
        sq_gen++;
        return;
    }
    if (!down) {                                 /* step by step: all keys up: the next step */
        if (!sq_play && sq_chord && !--sq_chord) sq_cur = (sq_cur + 1u) % (sq.len ? sq.len : 1u);
        return;
    }
    if (sq_play) {                               /* live: into the step that is nearest now (the one just fired, or the next) */
        uint32_t len = sq.len ? sq.len : 1u, prev = (sq_pos + len - 1u) % len;
        st = (int32_t)(sq_next_q8 - (fm1_ms << 8)) < (int32_t)(sq_step_q8(prev) / 2u) ? sq_pos : prev;
        if (st != sq_cur || !sq_fresh) { sq_step_clear(st); sq_fresh = 1; }
        sq_cur = st;
    } else {
        st = sq_cur;
        if (!sq_chord) sq_step_clear(st);        /* a new chord replaces the step */
        sq_chord++;
    }
    for (i = 0; i < SQ_NN; i++)
        if (!sq.note[st][i]) { sq.note[st][i] = (uint8_t)note; break; }
    sq_who[st] = 255;
    sq_gen++;
}

/* its page: the steps as glass bars on a floor, as high as their note; each in the colour of the synth that played
 * it last (a rest: its outline only), bursting when it sounds; the cursor; under them the four knobs */
static void draw_seq(uint32_t t)
{
    if (!sq_who_init) { uint32_t z; sq_who_init = 1; for (z = 0; z < SQ_MAX; z++) sq_who[z] = 255; }

    uint32_t page = sq_cur / 16u, i;            /* the cursor's page: you can pick a step and change it while it plays */
    uint32_t playing = (sq_pos + (sq.len ? sq.len : 1u) - 1u) % (sq.len ? sq.len : 1u);
    char s[32], *p;
    int32_t bw = 224 / (int32_t)(sq.len < 4u ? 4u : sq.len > 16u ? 16u : sq.len);   /* a short pattern: wider bars */
    cv_begin(240, MAIN_H, C_BLACK);
    for (i = 0; i < 16u; i++) {
        uint32_t st = page * 16u + i, n = sq_top(st % SQ_MAX), who = sq_who[st % SQ_MAX], age = sq_hit[st % SQ_MAX] ? t - sq_hit[st % SQ_MAX] : 9999u;
        int32_t x = 8 + (int32_t)i * bw, base = 152, h, j, w = bw - 4;
        int in = st < sq.len, now = sq_play && st == playing && age < 400u;
        uint32_t c = who < NCH ? who : who >= 16u && who < 16u + NCH ? who - 16u : NCH, k;
        uint16_t edge;
        if ((int32_t)i * bw > 224 - bw) break;
        if (!(i & 3u)) cv_rect(x, base + 4, w + 1, 1, in ? C_GREY : C_SLOT);       /* the beats */
        if (!in) { cv_rect(x + w / 2 - 1, base - 1, 3, 1, C_SLOT); continue; }
        if (!n) { cv_rect(x + 1, base - 1, w - 1, 1, st == playing && sq_play ? C_SILK : C_GREY); goto cursor; }
        h = 8 + ((int32_t)n - 36) * 72 / 60;
        if (h < 6) h = 6; else if (h > 80) h = 80;
        k = age < 300u ? 255u - age * 150u / 300u : 105u;                         /* it flares when it sounds, then settles */
        edge = c < NCH ? shade(c, who >= 16u ? 90u : k + (k < 200u ? 50u : 0u)) : C_GREY;
        if (c < NCH && who < 16u) {                                                /* glass: a tint; brighter from the floor up while it sounds */
            cv_rect(x, base - h, w, h, shade(c, 40));
            if (age < 300u) cv_rect(x, base - h * (int32_t)(300u - age) / 300, w, h * (int32_t)(300u - age) / 300, shade(c, 190));
        }
        cv_frame(x, base - h, w, h, 1, edge);
        cv_line(x, base - h, x + 3, base - h - 3, edge); cv_line(x + w - 1, base - h, x + w + 2, base - h - 3, edge);   /* its top, in depth */
        cv_line(x + 3, base - h - 3, x + w + 2, base - h - 3, edge); cv_line(x + w + 2, base - h - 3, x + w + 2, base - 3, edge);
        cv_line(x + w - 1, base - 1, x + w + 2, base - 3, edge);
        for (j = 0; j < (int32_t)SQ_NN; j++)                                       /* a chord's other notes: marks at their height */
            if (sq.note[st][j] && sq.note[st][j] != n) {
                int32_t h2 = 8 + ((int32_t)sq.note[st][j] - 36) * 72 / 60;
                cv_rect(x + 2, base - (h2 < 6 ? 6 : h2 > 80 ? 80 : h2), w - 4, 2, C_SILK);
            }
        if (now && c < NCH && who < 16u && seen_hit[c] != sq_hit[st]) {            /* and it throws its sparks */
            seen_hit[c] = sq_hit[st];
            part_spawn(c, x + w / 2, base - h, 12, 0, 50);
        }
    cursor:
        if (st == sq_cur)                                                          /* the cursor: also while it plays */
            cv_rect(x - 1, base + 8, w + 3, 2, sq_rec ? RGB(255, 70, 60) : C_AMBER);
        if (st == playing && sq_play) cv_rect(x + w / 2 - 2, base + 13, 5, 3, C_SILK);   /* the playhead */
    }
    part_step_draw();
    /* the four knobs, where the knobs are: tempo, the step's length, swing, the pattern's length */
    dial(30, 21, (int32_t)sq.bpm - 40, 200, shade(0, 255), "TEMPO", s_num(s, sq.bpm) ? s : s);
    dial(90, 21, sq.div % 6u, 5, shade(1, 255), "STEP", SQ_DIV[sq.div % 6u]);
    {
        char g[8];
        s_put(s_num(g, (int32_t)sq.gate * 5), "%");
        dial(150, 21, sq.gate > 40u ? 40 : sq.gate, 40, shade(2, 255), "GATE", g);
    }
    dial(210, 21, sq.len, (int32_t)SQ_MAX, shade(3, 255), "LENGTH", 0);
    (void)p;
    cv_blit(0, MAIN_Y);
}

/* ---- the arpeggiator: the notes held (keys, MIDI) are played one after the other, a step of the sequencer's
 * tempo each, and every one goes through the play mode: round robin, random, unison, poly. So an arpeggio up
 * through a chord walks through the five synths as well. ARP tapped: the style; ARP held: the play mode. */
#define ARP_MAX 12u
static uint32_t arp_n, arp_pos, arp_next_q8, arp_last = 255, arp_hit_ms;
static uint8_t arp_note[ARP_MAX], arp_vel[ARP_MAX];   /* as they were played */
static struct { uint8_t note; uint32_t off; } arp_snd[4];
static void arp_off_all(void)
{
    uint32_t i;
    for (i = 0; i < 4u; i++) if (arp_snd[i].note) { route_off(arp_snd[i].note); arp_snd[i].note = 0; }
}
static void arp_set(uint32_t style)
{
    arp_off_all();
    if (!style) arp_n = 0;
    arp = style < AR_N ? style : AR_OFF; arp_pos = 0; arp_last = 255;
}
static int arp_takes(void) { return arp && !sq_rec && !(sq_open && !sv_open && solo >= NCH); }   /* (the sequencer's page: the keys write steps) */
static void arp_key(uint32_t note, uint32_t vel, int down)
{
    uint32_t i;
    for (i = 0; i < arp_n; i++)
        if (arp_note[i] == note) {               /* (out; or played again: to the end) */
            for (; i + 1u < arp_n; i++) { arp_note[i] = arp_note[i + 1u]; arp_vel[i] = arp_vel[i + 1u]; }
            arp_n--;
            break;
        }
    if (!down) return;
    if (arp_n >= ARP_MAX) return;
    if (!arp_n) { arp_pos = 0; arp_last = 255; arp_next_q8 = fm1_ms << 8; }   /* the first note: at once */
    arp_note[arp_n] = (uint8_t)note; arp_vel[arp_n++] = (uint8_t)vel;
}
static void arp_play(uint32_t note, uint32_t vel, uint32_t slot, uint32_t off)
{
    if (note > 127u) return;
    route_on(note, vel, 1, 0);
    arp_snd[slot].note = (uint8_t)note; arp_snd[slot].off = off;
}
static void arp_tick(void)                       /* with sq_tick: often */
{
    uint32_t i, j, n = arp_n, step, off;
    uint8_t s[ARP_MAX], v[ARP_MAX];
    for (i = 0; i < 4u; i++)
        if (arp_snd[i].note && (int32_t)(fm1_ms - arp_snd[i].off) >= 0) { route_off(arp_snd[i].note); arp_snd[i].note = 0; }
    if (!arp || !n) { if (!n) arp_off_all(); return; }
    if ((int32_t)((fm1_ms << 8) - arp_next_q8) < 0) return;
    step = sq_step_q8(arp_pos);
    arp_next_q8 += step;
    if ((int32_t)((fm1_ms << 8) - arp_next_q8) > (int32_t)(200u << 8)) arp_next_q8 = (fm1_ms << 8) + step;
    arp_off_all();                               /* (a gate of a step or more: until the next) */
    off = (fm1_ms + ((step >> 8) * (sq.gate > 19u ? 19u : sq.gate) / 20u)) | 1u;
    for (i = 0; i < n; i++) { s[i] = arp_note[i]; v[i] = arp_vel[i]; }
    if (arp != AR_PLAYED && arp != AR_CHORD)       /* by pitch */
        for (i = 1; i < n; i++)
            for (j = i; j && s[j - 1u] > s[j]; j--) {
                uint8_t t = s[j]; s[j] = s[j - 1u]; s[j - 1u] = t;
                t = v[j]; v[j] = v[j - 1u]; v[j - 1u] = t;
            }
    arp_hit_ms = fm1_ms | 1u;
    switch (arp) {
    case AR_DOWN: i = n - 1u - arp_pos % n; arp_play(s[i], v[i], 0, off); break;
    case AR_UPDOWN: {
        uint32_t per = n > 1u ? 2u * n - 2u : 1u, k = arp_pos % per;
        i = k < n ? k : per - k;
        arp_play(s[i], v[i], 0, off);
        break;
    }
    case AR_RANDOM:
        i = rnd(n);
        if (n > 1u && i == arp_last) i = (i + 1u) % n;
        arp_last = i;
        arp_play(s[i], v[i], 0, off);
        break;
    case AR_CLIMB: {                              /* up through the chord, and again an octave higher */
        uint32_t k = arp_pos % (2u * n);
        i = k % n;
        arp_play((uint32_t)s[i] + (k >= n ? 12u : 0u), v[i], 0, off);
        break;
    }
    case AR_CHORD:                                /* all of it, every step (the last four played) */
        for (i = n > 4u ? n - 4u : 0u, j = 0; i < n; i++, j++) arp_play(s[i], v[i], j, off);
        break;
    default: i = arp_pos % n; arp_play(s[i], v[i], 0, off); break;   /* up; as played */
    }
    arp_pos++;
}
/* a note from the keys or from MIDI: to the arpeggiator when it is on, else it sounds */
static void note_in(uint32_t note, uint32_t vel, int down, int from_keys, uint32_t mc)
{
    if (arp_takes() || (!down && arp)) arp_key(note, vel, down && arp_takes());
    if (arp_takes()) return;
    if (down) route_on(note, vel, from_keys, mc); else route_off(note);
}

/* ---- MIDI and the keys: from the main loop, and from app_idle while the screen draws (a frame takes some ms, and
 * a percussive synth must not wait for it). Notes for the MIDI out wait in a queue for the main loop. */
#define KEY0 53u                                /* key 0 = F3 */
static uint32_t idle_ok, dbg_max[NCH], dbg_dump, dbg_full, k_prev, outq_w, outq_r;
static int32_t k_oct;
static uint8_t k_held[27], outq[32][2];
static void keys_in(void)
{
    uint32_t keys = fm1_in.notes, down = keys & ~k_prev, up = k_prev & ~keys, k;
    uint32_t base = (uint32_t)((int32_t)KEY0 + 12 * k_oct);
    k_prev = keys;
    if (!(down | up)) return;
    for (k = 0; k < 27u; k++) {
        if (down >> k & 1u && sv_open && sv_act == A_SAVE) {   /* naming a patch: the keys are letters, and silent */
            sv_type(k); sv_typed = 1;
            k_held[k] = 0;
            continue;
        }
        if (up >> k & 1u && !k_held[k]) continue;   /* (a letter's key let go) */
        if (down >> k & 1u) {
            k_held[k] = (uint8_t)(base + k);
            note_in(k_held[k], 127, 1, 1, 0);
            sq_key(k_held[k], 1);
            outq[outq_w % 32u][0] = 0x90; outq[outq_w++ % 32u][1] = k_held[k];
        }
        if (up >> k & 1u) {
            note_in(k_held[k], 0, 0, 1, 0);
            sq_key(k_held[k], 0);
            outq[outq_w % 32u][0] = 0x80; outq[outq_w++ % 32u][1] = k_held[k];
        }
    }
}
static uint8_t ped, ped_held[128];             /* MIDI's sustain pedal (CC 64); the notes it is holding */
static void midi_in(void)
{
    uint8_t st, d1, d2;
    while (midi_get(&st, &d1, &d2)) {
        uint32_t ty = st & 0xF0u, mc = (st & 15u) + 1u;
        if (st >= 0xF0u) { sq_clock(st); continue; }   /* a clock: the sequencer follows it */
        if (ty == 0xB0u && d1 == 118u) { grid_look = d2 & 7u; continue; }   /* (for choosing: the space behind the shapes) */
        if (ty == 0xB0u && d1 == 119u) {         /* diagnosis: 1 = forget the peaks, 2 = report (app_main sends) */
            if (d2 == 1u) { uint32_t c; for (c = 0; c < NCH; c++) dbg_max[c] = 0; }
            if (d2 == 2u) dbg_dump = 1;
            if (d2 == 3u) dbg_full = 1;          /* 3 = the whole patch as it sounds now, and its sequence (app_main sends) */
            continue;
        }
        if (ty == 0xB0u && d1 == 64u) {          /* the sustain pedal: notes let go while it is down ring on until it comes up */
            uint32_t k;
            ped = d2 >= 64u;
            if (!ped)
                for (k = 0; k < 128u; k++) if (ped_held[k]) { ped_held[k] = 0; note_in(k, 0, 0, 0, mc); }
            continue;
        }
        if (ty == 0x90u && d2) { ped_held[d1 & 127u] = 0; note_in(d1, d2, 1, 0, mc); }
        else if (ty == 0x80u || ty == 0x90u) { if (ped) ped_held[d1 & 127u] = 1; else note_in(d1, 0, 0, 0, mc); }   /* (with the arpeggiator: the pedal holds the chord) */
    }
}
static void app_idle(void)
{
    if (idle_ok) { keys_in(); midi_in(); sq_tick(); arp_tick(); }
}

/* ---- the panel
 * MAIN (no synth button held): the five synths
 *   keys         play: each note to the next synth (ROUND ROBIN), to all (UNISON), to the focused one (POLY)
 *   SELECT       a cursor on the synths (it goes by itself after 1.5 s); in POLY: the synth the keys play
 *   ALGORITHM    with SELECT's cursor showing: that synth ON, SILENT (a rest in the cycle) or SKIP (not in it);
 *                while the delay has the screen: its time; while the reverb has it: the room's size
 *   GLO          RANDOM: five new sounds
 *   KNOB 1-4     everything: the DJ filter (left lowpass, right highpass), its resonance, delay, reverb;
 *                the screen shows the one you turn
 *   ARP          the play mode: ROUND ROBIN -> RANDOM (each note jumps to a random synth) -> UNISON -> POLY
 *   SEQ          the sequencer's page (KNOB 1-4: tempo, step length, swing, length; SELECT: a step; ALGORITHM: its note)
 *   PLAY         the sequence starts / stops. REC: stopped: step by step (a note or chord, let go: the next step);
 *                playing: what you play goes onto the steps; held 1.2 s: the sequence is cleared
 *   OCT- OCT+    the keys' octave
 *   PRESETS      the patches: the factory ones, then yours
 *   SAVE         the save page: KNOB 1 the slot, KNOB 2 SAVE / LOAD / ERASE, KNOB 3 + 4 the name's two words;
 *                SAVE again does it (so SAVE, SAVE keeps what you hear); HOME: back
 * A SYNTH'S PAGE (press FX / SEL / ENV / LFO / EDIT = synth 1..5; it stays; the same button again: back to MAIN):
 *   keys         play this synth only
 *   GLO          RANDOM: a new sound for this synth only
 *   ALGORITHM    its pages, round and round (as MAIN's)
 *   PRESETS      its NOISE, on every page
 *   SELECT       its wave
 *   KNOB 1-4     cutoff, reso, attack, release; HOME: the other row: envelope -> filter, volume, delay, reverb
 *   PLAY         ON -> OFF (a rest in the cycle) -> SKIP (not in the cycle)
 *   OCT- OCT+    its octave */
/* switched on: 1.3 seconds. The five shapes spring up one after the other out of the grid, spinning, full of light
 * that drains, each with its letter of POLY5 under it; then the home screen takes over (the same shapes, in place).
 * The keys play all the while. */
#ifdef DINGHY_WEB
static int intro(void)                          /* (the browser: a frame a call, 1 while it runs; web/web.c) */
#else
static void intro(void)
#endif
{
    static const char L[5][2] = {"P", "O", "L", "Y", "5"};
#ifdef DINGHY_WEB
    static uint32_t t0, c, born, last;
    static int on;
    if (!on) { on = 1; t0 = fm1_ms; born = 0; last = 0; }
#else
    uint32_t t0 = fm1_ms, c, born = 0, last = 0;
#endif
    for (;;) {
        uint32_t t = fm1_ms - t0;
        if (t >= 1300u) break;
        platform_poll();
        keys_in(); midi_in();
        if (last && t - last < 33u) continue;    /* 30 pictures a second */
        last = t | 1u;
        cv_begin(240, MAIN_H, C_BLACK);
        g_grid(t, t < 400u ? 400u - t : 0u);
        for (c = 0; c < NCH; c++) {
            int32_t a = (int32_t)t - (int32_t)c * 130, x = 24 + (int32_t)c * 48, sz, l;
            if (a < 0) continue;
            if (!(born >> c & 1u)) { born |= 1u << c; part_spawn(c, x, SHAPE_Y, 30, 0, 90); }
            sz = a < 220 ? 3 + a * 15 / 220 : a < 380 ? 18 - (a - 220) * 6 / 160 : 12;   /* up, over, and back to its size */
            l = a < 600 ? 255 - a * 255 / 600 : 0;
            shape_solid(x, SHAPE_Y, sz, sz * 9 / 5, chan[c].wave % W_N,
                        rot[c] + (a < 700 ? (uint32_t)((700 - a) * (700 - a) / 9) : 0u), c, (uint32_t)l, 1, shade(c, 150u + (uint32_t)l * 105u / 255u));
            if (a > 100) {
                uint32_t k = t > 1100u ? 255u - (t - 1100u) * 255u / 200u : 255u;   /* the letters go as the numbers come */
                cv_text(x - text_w(&FONT_L, L[c]) / 2, 130, &FONT_L, L[c], shade(c, k));
            }
        }
        part_step_draw();
        cv_blit(0, MAIN_Y);
        if (t > 500u) {                         /* the version, on the screen's bottom line: in with the letters, out with them */
            uint32_t k = t < 700u ? (t - 500u) * 255u / 200u : t > 1100u ? 255u - (t - 1100u) * 255u / 200u : 255u;
            draw_text_box(0, 220, 240, &FONT_S, "v" POLY5_VER, RGB(k * 150u / 255u, k * 156u / 255u, k * 170u / 255u), 1);
        }
#ifdef DINGHY_WEB
        return 1;
#endif
    }
    draw_text_box(0, 220, 240, &FONT_S, "", C_BLACK, 1);   /* (gone: the home screen's line) */
#ifdef DINGHY_WEB
    on = 0;
    return 0;
#endif
}
#ifdef DINGHY_WEB
static int web_intro;                           /* (the browser: the startup animation runs; web/web.c calls intro()) */
#endif
static void app_main(void)
{
#ifdef DINGHY_WEB                               /* (the browser: one pass of the loop a call, so its state is kept) */
    static struct { uint8_t patch; int8_t oct; uint8_t rev, spare; } set = {0, 0, 3, 0};
    static uint32_t k, c, rel, b, row = 0, rnd_ms = 0, set_ms = 0, set_dirty = 0;
    static uint32_t ui = 1, ui_drawn = 0, page_drawn = 99, frame_ms = 0, sel = 0, sel_ms = 0, rec_ms = 0, arp_ms = 0, sq_gen_seen = 0, sq_cur_seen = 0;
    static int32_t e;
    static int started;
    if (started) goto web_step;
    started = 1;
#else
    struct { uint8_t patch; int8_t oct; uint8_t rev, spare; } set = {0, 0, 3, 0};   /* rev: older settings are not loaded: the factory's start */
    uint32_t k, c, rel, b, row = 0, rnd_ms = 0, set_ms = 0, set_dirty = 0;
    uint32_t ui = 1, ui_drawn = 0, page_drawn = 99, frame_ms = 0, sel = 0, sel_ms = 0, rec_ms = 0, arp_ms = 0, sq_gen_seen = 0, sq_cur_seen = 0;
    int32_t e;
#endif

    {
        /* your patches: two halves. Older firmware kept all 32 in blob 0, with smaller synths: those are converted,
         * and both halves written at once (so that none is lost if only one half is saved later) */
        static uint8_t old[4084];
        uint8_t *z = (uint8_t *)bankh;
        uint32_t csz = 0, i, h;
        for (k = 0; k < sizeof bankh; k++) z[k] = 0;
        uint32_t got[2] = {0, 0};
        for (h = 0; h < 2u; h++) {
            uint8_t *d = (uint8_t *)&bankh[h];
            if (!blob_load(h ? 3u : 0u, &bankh[h], sizeof bankh[0]) && bankh[h].ver == BANK_VER) { got[h] = 1; continue; }
            for (k = 0; k < sizeof bankh[0]; k++) d[k] = 0;
            if (!blob_load(h ? 3u : 0u, old, 20u + 16u * 157u) && ((const uint32_t *)(const void *)old)[0] == BANK_VER4) {
                for (k = 0; k < 16u; k++) {     /* the fourth format: the same, without the newest settings */
                    bankh[h].used[k] = old[4u + k];
                    for (i = 0; i < 157u; i++) ((uint8_t *)&bankh[h].p[k])[i] = old[20u + k * 157u + i];
                }
                bankh[h].ver = BANK_VER;        /* (written in the new format at the next SAVE into this half) */
                got[h] = 1;
            }
        }
        if (!got[0] && !got[1]) {
            if (!blob_load(0, old, 36u + NUSER * (16u + NCH * 17u + 12u)) && ((const uint32_t *)(const void *)old)[0] == BANK_VER2) csz = 17;
            else if (!blob_load(0, old, 36u + NUSER * (16u + NCH * 13u + 12u)) && ((const uint32_t *)(const void *)old)[0] == BANK_VER1) csz = 13;
            if (csz) {
                const uint8_t *o = old + 4u + NUSER;
                uint32_t psz = 16u + NCH * csz + 12u, any = 0;
                for (k = 0; k < NUSER; k++, o += psz) {
                    preset_t *d = &B_P(k);
                    uint8_t *dst = &d->mode;
                    B_USED(k) = old[4u + k];
                    any |= old[4u + k];
                    for (i = 0; i < 16u; i++) d->name[i] = (char)o[i];
                    for (c = 0; c < NCH; c++)
                        for (i = 0; i < csz; i++) ((uint8_t *)&d->ch[c])[i] = o[16u + c * csz + i];
                    for (i = 0; i < 12u; i++) dst[i] = o[16u + NCH * csz + i];
                }
                if (any) for (h = 0; h < 2u; h++) bank_save(h * 16u);
            }
        }
    }
    if (settings_load(&set, sizeof set) || set.rev != 3u || !patch_ptr(set.patch) || set.oct < -2 || set.oct > 2)
        set.patch = 0, set.oct = 0, set.rev = 3;
    k_oct = set.oct;
    for (k = 0; k < 2u; k++)                    /* your patches' sequences */
        if (blob_load(1u + k, &useq[k], sizeof useq[0]) || useq[k].ver != USEQ_VER) {
            uint8_t *z = (uint8_t *)&useq[k];
            for (c = 0; c < sizeof useq[0]; c++) z[c] = 0;
        }
    patch_load(set.patch);                      /* as you left it: the patch and the keys' octave */
    lcd_fill(0, 0, 240, 240, C_BLACK);
    for (c = 0; c < NCH; c++) rot[c] = c * 9000u;
#ifdef DINGHY_WEB
    web_intro = 1;
    return;
#else
    intro();
#endif

    for (;;) {
#ifdef DINGHY_WEB
web_step: ;
#endif
        uint32_t hold = 0, first = NCH, spin, page;
        platform_poll();
        passes++;

        keys_in();
        sq_tick(); arp_tick();
        lfo_tempo = sq.bpm;
        while (outq_r != outq_w) {              /* the keys' notes to the MIDI out */
            midi_send(outq[outq_r % 32u][0], outq[outq_r % 32u][1], outq[outq_r % 32u][0] == 0x90 ? 127u : 0u);
            outq_r++;
        }

        b = fm1_input_edges(&rel);
        (void)rel;
        for (c = 0; c < NCH; c++)               /* a synth's button: its page opens and stays; again: back */
            if (b & (1u << SYNTH_BTN[c])) {
                if (solo == c && !sv_open) solo = NCH;
                else { solo = c; focus = c; }
                sv_open = 0; sq_open = 0;
                ui++;
            }
        if (b & (1u << BTN_SAVE)) {
            if (!sv_open) {                     /* the save page: at this patch's slot, or the first free one */
                sv_open = 1; solo = NCH; sv_act = A_SAVE; sv_confirm = 0; sq_open = 0;
                if (patch_i >= NFACTORY) {
                    sv_slot = patch_i - NFACTORY;
                    s_put(sv_name, cur.name);
                    sv_cur = sv_len(); sv_fresh = 1;
                } else {
                    for (sv_slot = 0; sv_slot < NUSER - 1u && B_USED(sv_slot); sv_slot++) {}
                    sv_adj = rnd(16); sv_noun = rnd(16);
                    sv_words();
                }
            } else if (sv_act == A_SAVE) {
                if (B_USED(sv_slot) && patch_i != NFACTORY + sv_slot && !sv_confirm) {
                    sv_confirm = 1;
                    say("SAVE AGAIN TO OVERWRITE", 0, 0);
                } else {
                    char t[4];
                    patch_tag(t, NFACTORY + sv_slot);
                    sv_tidy();
                    if (patch_store(sv_slot, sv_name)) say("NOT SAVED: FLASH ERROR", 0, 0);
                    else { say("SAVED ", t, 0); set_dirty = 1; set_ms = fm1_ms; }
                    sv_open = 0;
                }
            } else if (sv_act == A_LOAD) {
                if (!B_USED(sv_slot)) say("EMPTY", 0, 0);
                else { patch_load(NFACTORY + sv_slot); say(cur_name, 0, 0); sv_open = 0; set_dirty = 1; set_ms = fm1_ms; }
            } else {                            /* ERASE */
                if (!B_USED(sv_slot)) say("EMPTY", 0, 0);
                else if (!sv_confirm) { sv_confirm = 1; say("SAVE AGAIN TO ERASE", 0, 0); }
                else {
                    B_USED(sv_slot) = 0;
                    bank_save(sv_slot);
                    if (patch_i == NFACTORY + sv_slot) dirty = 1;   /* what you play is still here, unsaved now */
                    sv_confirm = 0;
                    say("ERASED", 0, 0);
                }
            }
            ui++;
        }
        if (solo < NCH) { hold = 1u << solo; first = solo; }

        /* the sequencer: SEQ its page; PLAY; REC (tapped: on / off; held: clear) */
        if (b & (1u << BTN_SEQ)) { sq_open = !sq_open; sv_open = 0; solo = NCH; hold = 0; first = NCH; ui++; }
        if (b & (1u << BTN_PLAY) && !hold) { if (sq_play) sq_stop(); else sq_start(); ui++; }
        if (b & (1u << BTN_REC)) { rec_ms = fm1_ms | 1u; }
        if (rec_ms && (fm1_in.buttons & (1u << BTN_REC)) && fm1_ms - rec_ms > 1200u) {
            for (k = 0; k < SQ_MAX; k++) sq_step_clear(k), sq_who[k] = 255, sq_hit[k] = 0;
            sq_cur = 0; rec_ms = 0; sq_gen++;
            say("SEQUENCE CLEARED", 0, 0);
            ui++;
        }
        if (rel & (1u << BTN_REC) && rec_ms) {
            rec_ms = 0;
            sq_rec = !sq_rec; sq_chord = 0;
            if (sq_rec && !sq_play) { sq_open = 1; sv_open = 0; solo = NCH; hold = 0; first = NCH; }   /* step by step: on its page */
            ui++;
        }
        if (sq_open && !sv_open && !hold) {     /* its page's knobs */
            int32_t d, v;
            if (b & (1u << BTN_HOME)) { sq_open = 0; ui++; }
            if ((d = fm1_enc_take(ENC_KNOB1)) != 0) { v = (int32_t)sq.bpm + d; sq.bpm = (uint8_t)(v < 40 ? 40 : v > 240 ? 240 : v); sq_dirty = 1; ui++; }
            if ((d = fm1_enc_take(ENC_KNOB2)) != 0) { v = (int32_t)sq.div + d; sq.div = (uint8_t)(v < 0 ? 0 : v > 5 ? 5 : v); sq_dirty = 1; ui++; }
            if ((d = fm1_enc_take(ENC_KNOB3)) != 0) { v = (int32_t)sq.gate + d; sq.gate = (uint8_t)(v < 1 ? 1 : v > 80 ? 80 : v); sq_dirty = 1; ui++; }   /* 5 % .. 400 % of a step */
            if ((d = fm1_enc_take(ENC_KNOB4)) != 0) {
                v = (int32_t)sq.len + d; sq.len = (uint8_t)(v < 1 ? 1 : v > (int32_t)SQ_MAX ? (int32_t)SQ_MAX : v);
                if (sq_pos >= sq.len) sq_pos = 0;
                if (sq_cur >= sq.len) sq_cur = sq.len - 1u;
                sq_dirty = 1; ui++;
            }
            if ((d = fm1_enc_take(ENC_SELECT)) != 0) { v = (int32_t)sq_cur + d; sq_cur = (uint32_t)(v < 0 ? 0 : v >= (int32_t)sq.len ? (int32_t)sq.len - 1 : v); ui++; }
            if ((d = fm1_enc_take(ENC_ALGORITHM)) != 0) {   /* the step's note: up and down; under the lowest: no note */
                if (!sq_top(sq_cur)) { if (d > 0) sq.note[sq_cur][0] = (uint8_t)(59 + d); }   /* (an empty step: a C4 to start from) */
                else {
                    uint32_t j, low = 255;
                    for (j = 0; j < SQ_NN; j++) if (sq.note[sq_cur][j] && sq.note[sq_cur][j] < low) low = sq.note[sq_cur][j];
                    if ((int32_t)low + d < 24) sq_step_clear(sq_cur);
                    else for (j = 0; j < SQ_NN; j++)
                        if (sq.note[sq_cur][j]) { v = (int32_t)sq.note[sq_cur][j] + d; sq.note[sq_cur][j] = (uint8_t)(v > 108 ? 108 : v); }
                }
                sq_dirty = 1; ui++;
            }
            if (sq_dirty) { dirty = 1; sq_dirty = 0; }   /* (the sequence is the patch's: SAVE keeps it) */
        }

        if (sv_open) {                          /* the save page's knobs */
            int32_t d;
            if (b & (1u << BTN_HOME)) { sv_open = 0; ui++; }
            if ((d = fm1_enc_take(ENC_KNOB1) + fm1_enc_take(ENC_PRESETS)) != 0) {
                int32_t v = (int32_t)sv_slot + d;
                sv_slot = (uint32_t)(v < 0 ? 0 : v > (int32_t)NUSER - 1 ? (int32_t)NUSER - 1 : v);
                if (B_USED(sv_slot) && sv_act == A_SAVE && patch_i == NFACTORY + sv_slot) { s_put(sv_name, cur.name); sv_cur = sv_len(); sv_fresh = 1; }
                sv_confirm = 0; ui++;
            }
            if ((d = fm1_enc_take(ENC_KNOB2)) != 0) {
                int32_t v = (int32_t)sv_act + d;
                sv_act = (uint32_t)(v < 0 ? 0 : v > (int32_t)A_N - 1 ? (int32_t)A_N - 1 : v);
                sv_confirm = 0; ui++;
            }
            if ((d = fm1_enc_take(ENC_KNOB3)) != 0) { sv_adj = (sv_adj + (uint32_t)d) & 15u; sv_words(); ui++; }
            if ((d = fm1_enc_take(ENC_KNOB4)) != 0) { sv_noun = (sv_noun + (uint32_t)d) & 15u; sv_words(); ui++; }
            if (sv_act == A_SAVE) {             /* the name: random; a letter back, all of it; the cursor, the letter under it */
                if (b & (1u << BTN_GLO)) { sv_adj = rnd(16); sv_noun = rnd(16); sv_words(); rnd_ms = fm1_ms | 1u; ui++; }
                if (b & (1u << BTN_OCT_DOWN)) { sv_back(); ui++; }
                if (b & (1u << BTN_OCT_UP)) { sv_name[0] = 0; sv_cur = 0; sv_fresh = 0; ui++; }
                if ((d = fm1_enc_take(ENC_ALGORITHM)) != 0) {
                    int32_t v = (int32_t)sv_cur + d, n2 = (int32_t)sv_len();
                    sv_cur = (uint32_t)(v < 0 ? 0 : v > n2 ? n2 : v); sv_fresh = 0; ui++;
                }
                if ((d = fm1_enc_take(ENC_SELECT)) != 0) { sv_turn(d); ui++; }
                if (sv_typed) { sv_typed = 0; ui++; }
                if (sv_key_ms && fm1_ms - sv_key_ms > 240u) { sv_key_ms = 0; ui++; }
                if (!sv_fresh) { static uint32_t bl; if ((fm1_ms >> 8) != bl) { bl = fm1_ms >> 8; ui++; } }   /* (the cursor blinks) */
            } else { (void)fm1_enc_take(ENC_ALGORITHM); (void)fm1_enc_take(ENC_SELECT); }
        } else {
            if (b & (1u << BTN_OCT_DOWN) || b & (1u << BTN_OCT_UP)) {
                int32_t d = b & (1u << BTN_OCT_UP) ? 1 : -1;
                if (hold) {
                    int32_t o = chan[first].octave + d;
                    chan[first].octave = (int8_t)(o < -2 ? -2 : o > 2 ? 2 : o);
                    dirty = 1;
                } else if (k_oct + d >= -2 && k_oct + d <= 2) {
                    char o[4] = {k_oct + d >= 0 ? '+' : '-', (char)('0' + (k_oct + d < 0 ? -(k_oct + d) : k_oct + d)), 0};
                    k_oct += d;
                    say("OCTAVE ", o, 0);
                    set_dirty = 1; set_ms = fm1_ms;
                }
                ui++;
            }
            if (b & (1u << BTN_GLO)) {          /* RANDOM: new sounds */
                randomize(hold ? hold : ALL);
                say(hold ? "NEW SOUND" : "NEW SOUNDS", 0, 0);
                rnd_ms = fm1_ms | 1u;
                dirty = 1; ui++;
            }
            if (b & (1u << BTN_PLAY) && hold) { /* in or out of the cycle */
                chan[first].enabled = (uint8_t)(chan[first].enabled == E_ON ? E_OFF : chan[first].enabled == E_OFF ? E_SKIP : E_ON);
                rr_idx = 0; rand_next = NCH;
                dirty = 1; ui++;
            }
            if (b & (1u << BTN_HOME) && hold) { solo = NCH; ui++; }   /* a synth's page: back to MAIN */
            else if (b & (1u << BTN_HOME) && !sq_open && mpage) { mpage = 0; ui++; }   /* a page of MAIN: back to PLAY */
            if (b & (1u << BTN_ARP) && !hold) arp_ms = fm1_ms | 1u;
            if (arp_ms && (fm1_in.buttons & (1u << BTN_ARP)) && fm1_ms - arp_ms > 500u) {   /* ARP held: the play mode */
                arp_ms = 0;
                all_off();
                mode = mode == M_RR ? M_RANDOM : mode == M_RANDOM ? M_UNISON : mode == M_UNISON ? M_POLY : M_RR;
                rr_idx = 0; rand_next = NCH;
                say(MODE_NAME[mode], 0, 0);
                dirty = 1; ui++;
            }
            if (rel & (1u << BTN_ARP) && arp_ms) {   /* ARP tapped: the arpeggiator's style */
                arp_ms = 0;
                arp_set((arp + 1u) % AR_N);
                say(ARP_NAME[arp], 0, 0);
                dirty = 1; ui++;
            }
            if (sel_ms && (hold || (int32_t)(fm1_ms - sel_ms) > 1500)) { sel_ms = 0; ui++; }   /* the cursor goes by itself */
            if ((e = fm1_enc_take(ENC_ALGORITHM)) != 0) {
                if (!hold && sel_ms) {          /* under the cursor: ON, OFF (a rest in the cycle), SKIP (not in it) */
                    static const uint8_t ORDER[3] = {E_ON, E_OFF, E_SKIP};
                    int32_t i = chan[sel].enabled == E_ON ? 0 : chan[sel].enabled == E_OFF ? 1 : 2;
                    i += e;
                    chan[sel].enabled = ORDER[i < 0 ? 0 : i > 2 ? 2 : i];
                    rand_next = NCH;
                    sel_ms = fm1_ms | 1u;
                    dirty = 1;
                } else if (hold) {              /* on a synth's page: its pages, as on MAIN: round and round */
                    int32_t v = ((int32_t)row + e) % (int32_t)NPAGE;
                    row = (uint32_t)(v < 0 ? v + (int32_t)NPAGE : v);
                } else if (!sq_open) {          /* MAIN: its pages: PLAY, then the effects on everything */
                    int32_t v = ((int32_t)mpage + e) % (int32_t)NMPAGE;   /* round and round */
                    mpage = (uint32_t)(v < 0 ? v + (int32_t)NMPAGE : v);
                    take_ms = 0;
                }
                ui++;
            }
            if ((e = fm1_enc_take(ENC_SELECT)) != 0) {
                if (hold) {
                    chan[first].wave = (uint8_t)(((int32_t)chan[first].wave + (e % (int32_t)W_N) + (int32_t)W_N) % (int32_t)W_N);
                    dirty = 1;
                } else if (!mpage) {            /* PLAY: a cursor on the synths; in POLY it is the one the keys play */
                    int32_t v = (int32_t)(sel_ms ? sel : focus) + (sel_ms ? e : 0);
                    sel = (uint32_t)(v < 0 ? 0 : v > (int32_t)NCH - 1 ? (int32_t)NCH - 1 : v);
                    if (mode == M_POLY) focus = sel;
                    sel_ms = fm1_ms | 1u;
                }
                ui++;
            }
            for (k = 0; k < 4u; k++)
                if ((e = fm1_enc_take(ENC_KNOB1 + k)) != 0) {
                    if (hold) { if (PAGES[row % NPAGE][k].kind != K_NONE) item_add(first, &PAGES[row % NPAGE][k], e); }   /* the page's dial */
                    else if (mpage) {           /* a page of MAIN: its dial */
                        if (MPAGES[mpage][k].kind != K_NONE) item_add(0, &MPAGES[mpage][k], e);
                    }
                    else {                      /* PLAY: the four quick knobs; the screen shows the one you turn */
                        int32_t v;
                        if (k == T_FILTER) { v = (int8_t)cur.dj + e * 2; cur.dj = (uint8_t)(int8_t)(v < -100 ? -100 : v > 100 ? 100 : v); }
                        else if (k == T_LEN) {  /* LENGTH: every synth longer or shorter. It never passes 12 o'clock in one
                                                 * go, and rests there 0.4 s: back to the patch by feel, not by eye */
                            int32_t was = len_off;
                            v = was + e * 2;
                            if ((was < 0 && v > 0) || (was > 0 && v < 0)) v = 0;
                            if (!was && len_stop_ms && fm1_ms - len_stop_ms < 400u) v = 0;
                            if (!v && was) len_stop_ms = fm1_ms | 1u;
                            len_off = v < -100 ? -100 : v > 100 ? 100 : v;
                        }
                        else {
                            uint8_t *f = k == T_DELAY ? &cur.del_all : &cur.rev_all;
                            v = (int32_t)*f + e * 2;
                            *f = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v);
                        }
                        fx_apply();
                        take = k; take_ms = fm1_ms | 1u;
                    }
                    dirty = 1; ui++;
                }
            if ((e = fm1_enc_take(ENC_PRESETS)) != 0) {
                if (hold) {                     /* on a synth's page: its NOISE, on every page */
                    padd(hold, &P_NOISE, e);
                    dirty = 1;
                } else {
                    patch_step(e);
                    set_dirty = 1; set_ms = fm1_ms;
                }
                ui++;
            }
        }

        midi_in();
        for (c = 0; c < NCH; c++) { uint32_t l = ch_level[c]; if (l > dbg_max[c]) dbg_max[c] = l; }
        if (dbg_full) {                         /* the working patch, byte for byte, then its sequence: pitch bends on channel
                                                 * 15 (7 bits, then the eighth), one every 2 ms; CC 118 = 127 ends it.
                                                 * So a sound made on the FM-1 can be read off it (tools/poly5_read.py) */
            static uint8_t full[sizeof(preset_t) + sizeof(seq_t)];
            static uint32_t full_ms;
            uint32_t n;
            if (dbg_full == 1u) {
                const uint8_t *a = (const uint8_t *)&cur, *b2 = (const uint8_t *)&sq;
                for (c = 0; c < NCH; c++) cur.ch[c] = chan[c];
                cur.mode = (uint8_t)mode; cur.arp = (uint8_t)arp;
                for (k = 0; k < sizeof(preset_t); k++) full[k] = a[k];
                for (k = 0; k < sizeof(seq_t); k++) full[sizeof(preset_t) + k] = b2[k];
                midi_send(0xBFu, 117, sizeof(preset_t)); midi_send(0xBFu, 116, sizeof(seq_t));
                dbg_full = 2; full_ms = fm1_ms;
            } else if (fm1_ms - full_ms >= 2u) {    /* one every 2 ms: four a millisecond lost a third of them on the device */
                full_ms = fm1_ms;
                for (n = 0; n < 1u && dbg_full - 2u < sizeof full; n++, dbg_full++)
                    midi_send(0xEEu, full[dbg_full - 2u] & 127u, full[dbg_full - 2u] >> 7);
                if (dbg_full - 2u >= sizeof full) { midi_send(0xBFu, 118, 127); dbg_full = 0; }
            }
        }
        if (dbg_dump) {                         /* each synth on its MIDI channel: the peak as a pitch bend, the patch as CCs */
            dbg_dump = 0;
            for (c = 0; c < NCH; c++) {
                uint32_t pk = dbg_max[c] > 16383u ? 16383u : dbg_max[c];
                midi_send(0xE0u | c, pk & 127u, pk >> 7);
                midi_send(0xB0u | c, 20, chan[c].wave); midi_send(0xB0u | c, 21, (uint32_t)(chan[c].octave + 2));
                midi_send(0xB0u | c, 22, chan[c].cutoff); midi_send(0xB0u | c, 23, chan[c].noise);
                midi_send(0xB0u | c, 24, chan[c].vol); midi_send(0xB0u | c, 25, chan[c].enabled);
                midi_send(0xB0u | c, 26, chan[c].reso); midi_send(0xB0u | c, 27, chan[c].env_filt);
                platform_poll();
            }
            midi_send(0xBFu, 28, mode); midi_send(0xBFu, 29, rr_idx); midi_send(0xBFu, 30, patch_i); midi_send(0xBFu, 31, dirty);
            {                                   /* the audio's time since the last report: the worst and the average, in 50 us (a buffer: 116) */
                uint32_t mx = perf_max_us / 50u, av = perf_n ? perf_sum_us / perf_n / 50u : 0u;
                midi_send(0xBFu, 40, mx > 127u ? 127u : mx); midi_send(0xBFu, 41, av > 127u ? 127u : av);
#ifndef DINGHY_HOST
                {
                static uint32_t ua_seen[3];
                /* USB audio since the last clear: the ring ran empty (a click), blocks dropped, packets missed; its low point now */
                midi_send(0xBFu, 42, uac.underruns - ua_seen[0] > 127u ? 127u : uac.underruns - ua_seen[0]);
                midi_send(0xBFu, 43, uac.overruns - ua_seen[1] > 127u ? 127u : uac.overruns - ua_seen[1]);
                midi_send(0xBFu, 44, uac.missed - ua_seen[2] > 127u ? 127u : uac.missed - ua_seen[2]);
                midi_send(0xBFu, 45, uac.fill_min / 4u > 127u ? 127u : uac.fill_min / 4u);
                midi_send(0xBFu, 46, (uint32_t)uac.alt | (uint32_t)uac.r48 << 1 | (uint32_t)uac.flowing << 2);
                ua_seen[0] = uac.underruns; ua_seen[1] = uac.overruns; ua_seen[2] = uac.missed;
                }
#endif
                perf_max_us = 0; perf_sum_us = 0; perf_n = 0;
            }
        }

        /* the lights: a synth's button is bright while it sounds, blinks while its page is open, dim while in the cycle */
        for (c = 0; c < NCH; c++) {
            fm1_led_key(SYNTH_BTN[c], (hold >> c & 1u ? (fm1_ms >> 8 & 3u) != 0u : 0u) || ch_active[c]);
            fm1_led_key_glow(SYNTH_BTN[c], chan[c].enabled == E_ON);
        }
        for (k = 0; k < 27u; k++)
{
            uint32_t on = k_prev >> k & 1u, nt = (uint32_t)((int32_t)KEY0 + 12 * k_oct) + k;
            if (sq_open && !sv_open && !hold && (sq.note[sq_cur][0] == nt || sq.note[sq_cur][1] == nt || sq.note[sq_cur][2] == nt || sq.note[sq_cur][3] == nt)) on = 1;
            fm1_led_key(KEY_LED(k), on);
        }
        fm1_led_key(BTN_OCT_DOWN, hold ? chan[first].octave < 0 : k_oct < 0);
        fm1_led_key(BTN_OCT_UP, hold ? chan[first].octave > 0 : k_oct > 0);
        fm1_led_key(BTN_HOME, 0);
        fm1_led_key(BTN_ARP, arp && !(arp_n && arp_hit_ms && fm1_ms - arp_hit_ms < 60u));   /* on: lit, a blink a step */
        fm1_led_key(BTN_SEQ, sq_open);
        fm1_led_key(BTN_PLAY, hold ? chan[first].enabled == E_ON : sq_play && sq_beat_ms && fm1_ms - sq_beat_ms < 70u);   /* on the beat */
        fm1_led_key_glow(BTN_PLAY, !hold && sq_play);
        fm1_led_key(BTN_REC, sq_rec);
        fm1_led_key(BTN_GLO, rnd_ms && fm1_ms - rnd_ms < 150u);
        fm1_led_key(BTN_SAVE, sv_open);
        fm1_led_key_glow(BTN_SAVE, dirty);      /* dim: changes not saved */

        if (popup[0] && (int32_t)(fm1_ms - popup_until) > 0) { popup[0] = 0; ui++; }
        if (sq_gen != sq_gen_seen) dirty = 1;
        if (sq_gen != sq_gen_seen || sq_cur != sq_cur_seen) { sq_gen_seen = sq_gen; sq_cur_seen = sq_cur; ui++; }
        spin = rnd_ms && fm1_ms - rnd_ms < 450u;

        /* the patch and the octave you are on, kept 2 s after the change, when nothing sounds (an erase stalls the sound) */
        if (set_dirty && fm1_ms - set_ms > 2000u) {
            for (c = 0, k = 0; c < NCH; c++) k |= ch_active[c];
            if (!k) {
                set.patch = (uint8_t)patch_i; set.oct = (int8_t)k_oct;
                settings_save(&set, sizeof set);
                set_dirty = 0;
            }
        }

        /* a frame: when something changed, and 30 times a second (the shapes turn) */
        if (take_ms && (sv_open || hold || (int32_t)(fm1_ms - take_ms) > (take == T_DELAY ? 4000 : 2500))) { take_ms = 0; ui++; }
        page = sv_open ? 10u : hold ? 1u + first : sq_open ? 30u : mpage ? 40u + mpage : take_ms ? 20u : 0u;
        if (ui != ui_drawn || page != page_drawn || fm1_ms - frame_ms >= 33u) {
            int text = ui != ui_drawn || page != page_drawn;
            frame_ms = fm1_ms;
            for (c = 0; c < NCH; c++) {                              /* the levels: up at once, down slowly */
                uint32_t l = (uint32_t)ch_level[c] * 255u / 5000u;
                if (l > 255u) l = 255u;
                lvl[c] = l > lvl[c] ? l : lvl[c] * 200u / 256u;
                rot[c] = (rot[c] + 260u + lvl[c] * 16u + (spin ? 6000u : 0u)) & 65535u;   /* faster while it sounds */
            }
            if (page != page_drawn && (page == 0u || page_drawn == 0u || page >= 10u || page_drawn >= 10u))
                lcd_fill(0, 20, 240, 178, C_BLACK);                  /* another kind of page */
            idle_ok = 1;
            if (sv_open) { if (text) draw_save(); }
            else if (!hold && sq_open) draw_seq(fm1_ms);
            else if (hold) draw_edit(first, row, 1, fm1_ms);
            else if (mpage) draw_mpage(fm1_ms);
            else if (take_ms) draw_take(fm1_ms);
            else {
                uint32_t hm = 0;
                for (k = 0; k < 128u; k++) hm |= note_mask[k];      /* every synth with a note held */
                draw_main(fm1_ms, hm, spin, sel_ms ? sel : NCH);
            }
            if (text) draw_text(hold, first);
            ui_drawn = ui; page_drawn = page;
            idle_ok = 0;
        }
#ifdef DINGHY_WEB
        return;
#endif
    }
}
