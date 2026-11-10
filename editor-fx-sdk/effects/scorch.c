// PTFX_NAME: Scorch
// PTFX_AUTHOR: Emanuele Lapponi
// PTFX_FLAGS: EFFECT, STEREO
// PTFX_SCRATCH: 0
//
// Distortion with high/lo passthrough. Drive sets input gain (0..+31 dB), bass tightening,
// pre/de-emphasis, level-dependent asymmetry and a post LPF against aliasing.
// Output level is auto-compensated. 0 % drive = passthrough.
// Mode: Warm (Pade tanh), Grit (asymmetric diode), Fold (triangle folder).
// Split + Target: only the Target band (Low/High) of a crossover at Split Hz
// is driven. Split 0 + High = whole signal driven.

#include "tracker_plugin.h"

const PluginParam PLUGIN_PARAMS[4] = {
    PTFX_PARAM_LIST  ("Mode",   0, "Warm|Grit|Fold"),
    PTFX_PARAM_FLOAT ("Drive",  0.0f, 100.0f, 0.0f, 1.0f, "%"),
    PTFX_PARAM_LIST  ("Split",  0, "0|60|90|130|200|300|450|650|1k|1.5k|2.2k|3.3k|5k|7.5k|11k|16k"),
    PTFX_PARAM_LIST  ("Target", 1, "Low|High")
};

// List params (max 16 items): ~half-octave steps
static const float s_splitHz[16] = {
    0, 60, 90, 130, 200, 300, 450, 650, 1000, 1500, 2200, 3300, 5000, 7500, 11000, 16000
};

static const PtfxHostApi* s_host = 0;

// Drive-derived values
static float s_inputGain = 1.0f, s_outputGain = 1.0f, s_mix = 0.0f;
static float s_bias = 0.0f, s_sag = 0.0f, s_emph = 0.0f, s_deemph = 0.0f;
// One-pole coefficients: tighten HPF, emphasis split, post LPF, envelope
static float s_aTight = 0.0f, s_aEmph = 0.0f, s_aPost = 1.0f, s_aEnv = 0.0f;
static float s_dcR = 0.999f;

// Crossover (TPT SVF) coefficients, glided at ~10 ms
static float s_crossA[3], s_crossAt[3];
static float s_crossFc = 0.0f;
// 0 = crossover off, 1 = on
static float s_lowMix = 0.0f, s_lowMixAt = 0.0f;
// 0 = drive high band, 1 = drive low band
static float s_tgt = 0.0f, s_tgtAt = 0.0f;

// Per-channel state
static float s_c1[2], s_c2[2];          // crossover
static float s_tl[2], s_el[2], s_dl[2]; // tighten / emphasis / de-emphasis
static float s_pl[2], s_env[2];         // post LPF / envelope
static float s_dx[2], s_dy[2];          // DC blocker

static int      s_mode = 0, s_prevMode = 0;
static uint32_t s_fadeRemaining = 0, s_fadeLength = 1;

static float s_hostRate(void) {
    return (s_host && s_host->sample_rate > 0) ? (float)s_host->sample_rate : 44100.0f;
}

static float s_expf(float x) {
    return (s_host && s_host->expf) ? s_host->expf(x) : 1.0f + x;
}

// One-pole smoothing coefficient for a cutoff in Hz
static float s_onePole(float fc) {
    const float sr = s_hostRate();
    if (fc > sr * 0.45f) fc = sr * 0.45f;
    return 1.0f - s_expf(-6.2831853f * fc / sr);
}

static void s_svfCoeffs(float fc, float sr, float* a) {
    if (fc < 5.0f) fc = 5.0f;
    if (fc > sr * 0.45f) fc = sr * 0.45f;
    float g = 3.14159265f * (fc / sr);
    if (s_host && s_host->tanf) g = s_host->tanf(g);
    a[0] = 1.0f / (1.0f + g * (g + 1.41421356f));
    a[1] = g * a[0];
    a[2] = g * a[1];
}

// ---- Shapers ---------------------------------------------------------------

// Pade approximant of tanh, exact 1 at |x| = 3
static float s_pade(float x) {
    if (x > 3.0f)  return 1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// Asymmetric diode: negative half clips earlier and lower => even harmonics
static float s_grit(float x) {
    return (x >= 0.0f) ? s_pade(x) : 0.75f * s_pade(1.5f * x);
}

// Triangle wavefolder, cubic-softened corners
static float s_fold(float x) {
    if (x > 64.0f)  x = 64.0f;
    if (x < -64.0f) x = -64.0f;
    float u = x * 0.25f + 0.25f;
    int n = (int)u;
    if (u < (float)n) n--;
    u -= (float)n;
    const float t = 4.0f * (u < 0.5f ? u : 1.0f - u) - 1.0f;
    return t * (1.5f - 0.5f * t * t);
}

// noinline: keeps -O3 from inlining all three shapers at each call site
__attribute__((noinline)) static float s_shape(int mode, float x) {
    switch (mode) {
        case 1:  return s_grit(x);
        case 2:  return s_fold(x);
        default: return s_pade(x);
    }
}

// ---- Parameter mapping -----------------------------------------------------

static float s_drive = 0.0f;

// Output gain: match the AC level of a 0.3-amplitude sine through the shaper
// to the input's. Param change only.
static float s_autoGain(void) {
    if (!s_host || !s_host->sinf) return 1.0f;
    const float e = 0.6366f * 0.3f * s_inputGain;
    const float off = s_bias + s_sag * e / (1.0f + e);
    float sum = 0.0f, sum2 = 0.0f;
    for (int n = 0; n < 256; n++) {
        const float y = s_shape(s_mode, 0.3f * s_inputGain * s_host->sinf(6.2831853f * n / 256.0f) + off);
        sum += y;
        sum2 += y * y;
    }
    const float mean = sum * (1.0f / 256.0f);
    const float var = sum2 * (1.0f / 256.0f) - mean * mean;
    if (var < 1e-6f || !s_host->sqrtf) return 1.0f;
    // Fold is bright; the post LPF removes part of it, so compensate
    return 0.2121f / s_host->sqrtf(var) * (s_mode == 2 ? 1.0f + 0.8f * s_drive * s_drive : 1.0f);
}

static void s_applyDrive(float d) {
    if (d < 0.0f) d = 0.0f;
    if (d > 1.0f) d = 1.0f;
    s_drive = d;

    s_inputGain = 1.0f + 37.0f * d * d;               // 0..+31 dB
    s_mix       = d * 25.0f;
    if (s_mix > 1.0f) s_mix = 1.0f;

    s_bias   = 0.12f * d;                             // static asymmetry
    s_sag    = 0.30f * d;                             // level-dependent asymmetry
    // Emphasis: +8 dB up to 60 % drive, fading to +6 dB at 100 %
    s_emph   = 1.5f * (d < 0.6f ? d / 0.6f : 1.0f - 0.9f * (d - 0.6f));
    s_deemph = s_emph / (1.0f + s_emph);
    s_outputGain = s_autoGain();

    s_aTight = s_onePole(25.0f + 125.0f * d);         // 25..150 Hz
    s_aEmph  = s_onePole(900.0f);
    s_aPost  = s_onePole(18000.0f - 12300.0f * d);    // 18 kHz..5.7 kHz
}

static void s_applyCross(float fc) {
    const float sr = s_hostRate();
    if (fc < 0.0f) fc = 0.0f;
    if (fc > sr * 0.45f) fc = sr * 0.45f;
    s_crossFc = fc;
    s_lowMixAt = (fc > 0.0f) ? 1.0f : 0.0f;
    // Off: park the crossover wide open (its output is faded out)
    if (fc < 20.0f) fc = (fc > 0.0f) ? 20.0f : sr * 0.45f;
    s_svfCoeffs(fc, sr, s_crossAt);
}

// ---- Plugin entry points ---------------------------------------------------

void plugin_init(void* scratch_mem, uint32_t scratch_size, const PtfxHostApi* host) {
    (void)scratch_mem;
    (void)scratch_size;
    s_host = host;
    const float sr = s_hostRate();

    s_mode = 0;
    s_prevMode = 0;
    s_fadeRemaining = 0;
    s_fadeLength = (uint32_t)(sr * 0.02f);
    if (s_fadeLength < 1) s_fadeLength = 1;

    s_aEnv = 1.0f - s_expf(-1.0f / (sr * 0.03f));     // ~30 ms envelope
    s_dcR  = 1.0f - 6.2831853f * 10.0f / sr;           // ~10 Hz DC blocker

    for (int ch = 0; ch < 2; ch++) {
        s_c1[ch] = s_c2[ch] = 0.0f;
        s_tl[ch] = s_el[ch] = s_dl[ch] = 0.0f;
        s_pl[ch] = s_env[ch] = 0.0f;
        s_dx[ch] = s_dy[ch] = 0.0f;
    }

    s_applyDrive(0.0f);
    s_applyCross(s_crossFc);
    for (int i = 0; i < 3; i++)
        s_crossA[i] = s_crossAt[i];
    s_lowMix = s_lowMixAt;
    s_tgt = s_tgtAt;
}

void plugin_set_param(uint8_t index, float value) {
    switch (index) {
        case 1:
            s_applyDrive(value * 0.01f);
            break;
        case 0: {
            int m = (int)(value + 0.5f);
            if (m < 0) m = 0;
            if (m > 2) m = 2;
            if (m != s_mode) {
                s_prevMode = s_mode;
                s_mode = m;
                s_fadeRemaining = s_fadeLength;
                s_outputGain = s_autoGain();
            }
            break;
        }
        case 2: {
            int n = (int)(value + 0.5f);
            if (n < 0) n = 0;
            if (n > 15) n = 15;
            s_applyCross(s_splitHz[n]);
            break;
        }
        case 3:
            s_tgtAt = (value > 0.5f) ? 0.0f : 1.0f; // list: 0 = Low, 1 = High
            break;
        default:
            break;
    }
}

int32_t plugin_process_block(const int16_t** in, int16_t** out, uint32_t samples, uint8_t channels) {
    if (!in || !out || !in[0] || !out[0]) {
        return 0;
    }

    const uint8_t num_ch = (channels > 2) ? 2 : channels;
    const float k = 1.41421356f;
    const float glide = 1.0f - s_expf(-1.0f / (s_hostRate() * 0.010f));

    const int16_t* src[2] = { in[0], (in[1] ? in[1] : in[0]) };
    int16_t* dst[2] = { out[0], out[1] };
    const uint32_t fade0 = s_fadeRemaining;

    for (uint32_t i = 0; i < samples; i++) {
        s_crossA[0] += glide * (s_crossAt[0] - s_crossA[0]);
        s_crossA[1] += glide * (s_crossAt[1] - s_crossA[1]);
        s_crossA[2] += glide * (s_crossAt[2] - s_crossA[2]);
        s_lowMix    += glide * (s_lowMixAt - s_lowMix);
        s_tgt       += glide * (s_tgtAt - s_tgt);
        const float ca0 = s_crossA[0], ca1 = s_crossA[1], ca2 = s_crossA[2];
        const float lowMix = s_lowMix, tgt = s_tgt;
        const float fadeT = (i < fade0) ? (float)(fade0 - i) / (float)s_fadeLength : 0.0f;

        #pragma GCC unroll 1 // unrolling only grows code
        for (uint8_t ch = 0; ch < num_ch; ch++) {
            int16_t* d = dst[ch];
            if (!d) continue;

            if (s_mix <= 0.0f) { // 0 % drive = true bypass
                d[i] = src[ch][i];
                continue;
            }

            const float dry = (float)src[ch][i] * (1.0f / 32768.0f);

            // Crossover: low = LPF + BPF stays untouched, the rest is driven
            const float cv3 = dry - s_c2[ch];
            const float cv1 = ca0 * s_c1[ch] + ca1 * cv3;
            const float cv2 = s_c2[ch] + ca1 * s_c1[ch] + ca2 * cv3;
            s_c1[ch] = 2.0f * cv1 - s_c1[ch];
            s_c2[ch] = 2.0f * cv2 - s_c2[ch];
            const float low = lowMix * (cv2 + k * cv1);
            // Band to drive: the high band (dry - low) or the low band
            const float sel = dry - low + tgt * (2.0f * low - dry);

            float x = sel * s_inputGain;

            // Tighten: one-pole HPF keeps lows from flubbing the shaper
            s_tl[ch] += s_aTight * (x - s_tl[ch]);
            x -= (1.0f - tgt) * s_tl[ch]; // bass stays intact when driving the low band

            // Pre-emphasis: highs hit the shaper harder
            s_el[ch] += s_aEmph * (x - s_el[ch]);
            x += s_emph * (x - s_el[ch]);

            // Asymmetry grows with level (soft-limited envelope)
            const float ax = x < 0.0f ? -x : x;
            s_env[ch] += s_aEnv * (ax - s_env[ch]);
            x += s_bias + s_sag * s_env[ch] / (1.0f + s_env[ch]);

            float y = s_shape(s_mode, x);
            if (fadeT > 0.0f)
                y += fadeT * (s_shape(s_prevMode, x) - y);

            // De-emphasis restores the tonal balance
            s_dl[ch] += s_aEmph * (y - s_dl[ch]);
            y -= s_deemph * (y - s_dl[ch]);

            // Post LPF darkens with drive to tame aliasing fizz
            s_pl[ch] += s_aPost * (y - s_pl[ch]);
            y = s_pl[ch];

            // DC blocker removes the asymmetry offset
            const float yo = y - s_dx[ch] + s_dcR * s_dy[ch];
            s_dx[ch] = y;
            s_dy[ch] = yo;

            y = dry + (yo * s_outputGain - sel) * s_mix; // = sel blended + untouched rest
            d[i] = SATURATE_16BIT((int32_t)(y * 32767.0f));
        }
    }

    s_fadeRemaining = (fade0 > samples) ? fade0 - samples : 0;
    return samples;
}
