// PTFX_NAME: Convolution Reverb
// PTFX_AUTHOR: Emanuele Lapponi
// PTFX_FLAGS: EFFECT, STEREO
// PTFX_SCRATCH: 376832

#include "tracker_plugin.h"

#define FFT_N         32768u
#define FFT_M         16384u
#define BLOCK         8192u
#define IR_MAX        24576u
#define TWIDDLE_N     4096u
#define PRED_MAX_SAMP 2646u // 60 ms @ 44.1 kHz
#define TW_CHUNK      32u    // stage twiddles fetched once per chunk, not once per butterfly
#define PI_F          3.14159265358979323846f

const PluginParam PLUGIN_PARAMS[5] = {
    PTFX_PARAM_FILE("IR",    ".wav"),
    PTFX_PARAM_INT ("Decay",  10.0f,   100.0f,   100.0f, 1.0f,   "%"),
    PTFX_PARAM_INT ("Predly",  0.0f,    60.0f,     0.0f, 1.0f,   "ms"),
    PTFX_PARAM_INT ("Mix",     0.0f,   100.0f,    50.0f, 1.0f,   "%"),
    PTFX_PARAM_INT ("Tone",  500.0f, 20000.0f, 20000.0f, 100.0f, "Hz")
};

typedef struct {
    int16_t spec[2][FFT_M][2];   // Q15 IR spectra; slot 0 holds DC and Nyquist as real values
    float   fft[FFT_M][2];       // packed complex FFT working buffer, shared by both channels
    int16_t hist[2][IR_MAX];     // overlap-save input history per channel
    float   twiddle[TWIDDLE_N];  // sin(pi * n / 8192) for n in [0, 4096)
} ConvState;

static const PtfxHostApi* s_host = 0;
static ConvState*         s_state = 0;
static const int16_t*     s_sec = 0;
static uint32_t           s_sec_frames = 0;
static uint8_t            s_sec_chans = 0;
static uint8_t            s_ir_chans = 0;
static uint8_t            s_file_loaded = 0;
static uint8_t            s_prepared = 0;
static uint8_t            s_dirty = 0;
static uint8_t            s_first_block = 1;

static float    s_decay_pct = 100.0f;
static float    s_predly_ms = 0.0f;
static float    s_mix = 0.5f;
static float    s_mix_prev = 0.5f;
static float    s_tone_hz = 20000.0f;
static float    s_tone_coeff = 1.0f;
static float    s_tone_state[2] = { 0.0f, 0.0f };

static uint32_t s_predelay_samples = 0;
static uint32_t s_ir_len = 0;
static float    s_knee = 0.8f;
static float    s_pow = 2.0f;
static float    s_wet_scale[2] = { 0.0f, 0.0f }; // per channel: autogain and Q15 spectrum scale

static inline int32_t round_to_i32(float x) {
    return (int32_t)(x >= 0.0f ? (x + 0.5f) : (x - 0.5f));
}

// Quarter-wave lookup for sin(pi * idx / 8192), idx in [0, 8192].
static inline float sin_q8192(uint32_t idx) {
    if (idx == 0u) return 0.0f;
    if (idx < TWIDDLE_N) return s_state->twiddle[idx];
    if (idx == TWIDDLE_N) return 1.0f;
    return s_state->twiddle[8192u - idx];
}

// sin(pi * k / 16384) for k in [0, 8192], linearly interpolated at half steps.
static inline float sin_q16384(uint32_t k) {
    uint32_t idx = k >> 1;
    float s0 = sin_q8192(idx);
    if ((k & 1u) == 0u) return s0;
    return 0.5f * (s0 + sin_q8192(idx + 1u));
}

// Stage twiddle for the M-point complex FFT: angle = 2 * pi * idx / FFT_M.
static inline void twiddle_m(uint32_t idx, float* out_cos, float* out_sin) {
    *out_sin = sin_q8192(idx);
    if (idx <= TWIDDLE_N) {
        *out_cos = sin_q8192(TWIDDLE_N - idx);
    } else {
        *out_cos = -sin_q8192(idx - TWIDDLE_N);
    }
}

static void fft_complex(float (*buf)[2], int inverse) {
    for (uint32_t i = 1u, j = 0u; i < FFT_M; i++) {
        uint32_t bit = FFT_M >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            float tr = buf[i][0], ti = buf[i][1];
            buf[i][0] = buf[j][0];
            buf[i][1] = buf[j][1];
            buf[j][0] = tr;
            buf[j][1] = ti;
        }
    }

    for (uint32_t len = 2u; len <= FFT_M; len <<= 1) {
        uint32_t half = len >> 1;
        uint32_t step = FFT_M / len;
        for (uint32_t j0 = 0u; j0 < half; j0 += TW_CHUNK) {
            uint32_t n = (half - j0 < TW_CHUNK) ? (half - j0) : TW_CHUNK;
            float tc[TW_CHUNK], ts[TW_CHUNK];
            for (uint32_t j = 0u; j < n; j++) {
                twiddle_m((j0 + j) * step, &tc[j], &ts[j]);
                if (!inverse) ts[j] = -ts[j];
            }
            for (uint32_t start = 0u; start < FFT_M; start += len) {
                float (*p0)[2] = buf + start + j0;
                float (*p1)[2] = p0 + half;
                for (uint32_t j = 0u; j < n; j++) {
                    float ur = p0[j][0], ui = p0[j][1];
                    float vr = p1[j][0], vi = p1[j][1];
                    float tr = vr * tc[j] - vi * ts[j];
                    float ti = vr * ts[j] + vi * tc[j];
                    p0[j][0] = ur + tr;
                    p0[j][1] = ui + ti;
                    p1[j][0] = ur - tr;
                    p1[j][1] = ui - ti;
                }
            }
        }
    }
}

// In-place real FFT of FFT_N floats. Output: buf[0] = (X[0], X[M]), buf[k] = X[k] for k in [1, M).
static void rfft_forward(float (*buf)[2]) {
    fft_complex(buf, 0);

    float z0r = buf[0][0], z0i = buf[0][1];
    buf[0][0] = z0r + z0i;
    buf[0][1] = z0r - z0i;

    uint32_t quarter = FFT_M >> 1;
    for (uint32_t k = 1u; k < quarter; k++) {
        uint32_t mk = FFT_M - k;
        float ar = buf[k][0],  ai = buf[k][1];
        float br = buf[mk][0], bi = buf[mk][1];

        float er = 0.5f * (ar + br);
        float ei = 0.5f * (ai - bi);
        float or_ = 0.5f * (ai + bi);
        float oi  = 0.5f * (br - ar);

        float s = sin_q16384(k);
        float c = sin_q16384(quarter - k);
        float tr = or_ * c + oi * s;
        float ti = oi * c - or_ * s;

        buf[k][0]  = er + tr;
        buf[k][1]  = ei + ti;
        buf[mk][0] = er - tr;
        buf[mk][1] = -ei + ti;
    }

    buf[quarter][1] = -buf[quarter][1];
}

// Inverse of rfft_forward, scaled by 1 / FFT_M.
static void rfft_inverse(float (*buf)[2]) {
    float x0 = buf[0][0], xm = buf[0][1];
    buf[0][0] = 0.5f * (x0 + xm);
    buf[0][1] = 0.5f * (x0 - xm);

    uint32_t quarter = FFT_M >> 1;
    for (uint32_t k = 1u; k < quarter; k++) {
        uint32_t mk = FFT_M - k;
        float xr = buf[k][0],  xi = buf[k][1];
        float yr = buf[mk][0], yi = buf[mk][1];

        float er = 0.5f * (xr + yr);
        float ei = 0.5f * (xi - yi);
        float tr = 0.5f * (xr - yr);
        float ti = 0.5f * (xi + yi);

        float s = sin_q16384(k);
        float c = sin_q16384(quarter - k);
        float or_ = tr * c - ti * s;
        float oi  = tr * s + ti * c;

        buf[k][0]  = er - oi;
        buf[k][1]  = ei + or_;
        buf[mk][0] = er + oi;
        buf[mk][1] = or_ - ei;
    }

    buf[quarter][1] = -buf[quarter][1];
    fft_complex(buf, 1);
}

// Decay envelope: flat up to `knee`, then (1 - u)^pow down to zero at the end of the kept IR.
static inline float decay_envelope(float t, float knee, float pow_exp) {
    if (t <= knee) return 1.0f;
    float u = (t - knee) / (1.0f - knee);
    if (u >= 1.0f) return 0.0f;
    return s_host->powf(1.0f - u, pow_exp);
}

// One IR tap of channel ch at position n of the kept length, with envelope.
static inline float ir_tap(uint8_t ch, uint32_t n, uint32_t ir_len) {
    float t = (float)n / (float)ir_len;
    float x = (float)s_sec[n * s_sec_chans + ch] * (1.0f / 32768.0f);
    return x * decay_envelope(t, s_knee, s_pow);
}

// Fill the FFT buffer with one IR channel placed after the predelay, then transform it.
// Returns the IR energy. The DC bin is cleared so IRs with an offset cannot thump or rumble.
static float ir_channel_spectrum(uint8_t ch, uint32_t pred, uint32_t ir_len, float (*buf)[2]) {
    float* win = (float*)buf;
    float sum_sq = 0.0f;
    s_host->memset(win, 0, FFT_N * sizeof(float));
    for (uint32_t n = 0u; n < ir_len; n++) {
        float x = ir_tap(ch, n, ir_len);
        win[pred + n] = x;
        sum_sq += x * x;
    }
    rfft_forward(buf);
    buf[0][0] = 0.0f;
    return sum_sq;
}

static void update_tone_coeff(void) {
    float sr = (s_host->sample_rate > 0) ? (float)s_host->sample_rate : 44100.0f;
    if (s_tone_hz >= 19999.0f) {
        s_tone_coeff = 1.0f;
        return;
    }
    s_tone_coeff = 1.0f - s_host->expf(-2.0f * PI_F * s_tone_hz / sr);
}

static uint32_t tail_samples(void) {
    return s_ir_len ? (s_predelay_samples + s_ir_len) : 0u;
}

static void update_tail(void) {
    if (s_host->set_extra_samples) {
        s_host->set_extra_samples(0u, tail_samples());
    }
}

// Decay knob (10..100 %):
//   up to 46 %: IR shortened from 8 % to 100 % of its length, with a steeper taper
//   above 46 %: full IR with a gentle taper over its last 20 %
static void update_shape(void) {
    s_prepared = 0u;
    s_dirty = 1u;
    s_ir_len = 0u;

    if (!s_state || !s_file_loaded || !s_sec || s_sec_frames == 0u || s_sec_chans == 0u) {
        update_tail();
        return;
    }

    uint32_t pred = (uint32_t)(s_predly_ms * 44.1f + 0.5f);
    if (pred > PRED_MAX_SAMP) pred = PRED_MAX_SAMP;
    s_predelay_samples = pred;

    uint32_t max_avail = IR_MAX - pred;
    if (s_sec_frames < max_avail) max_avail = s_sec_frames;

    float u = (s_decay_pct * 0.01f - 0.1f) * (1.0f / 0.9f);
    if (u < 0.0f) u = 0.0f;
    if (u > 1.0f) u = 1.0f;

    if (u <= 0.4f) {
        float w = u * (1.0f / 0.4f);
        float frac = 0.08f + 0.92f * (w * w);
        s_ir_len = (uint32_t)(frac * (float)max_avail);
        s_knee = 0.8f * w;
        s_pow = 4.0f - 2.0f * w;
    } else {
        s_ir_len = max_avail;
        s_knee = 0.8f;
        s_pow = 2.0f;
    }
    if (s_ir_len == 0u) s_ir_len = 1u;
    update_tail();
}

// Expensive part: powf per tap and one FFT per channel. Runs lazily before a block renders.
static void build_spectra(void) {
    s_dirty = 0u;
    s_prepared = 0u;
    if (s_ir_len == 0u) return;

    uint32_t pred = s_predelay_samples;
    s_ir_chans = (s_sec_chans >= 2u) ? 2u : 1u;

    // Each channel's spectrum is stored as Q15 scaled to its own peak (full precision), and
    // the scale is folded into s_wet_scale. Autogain is the smallest 1/sqrt(energy) across
    // channels, so both stay balanced.
    float raw_wet[2] = { 0.0f, 0.0f };
    float min_gain = 1.0e30f;
    for (uint8_t ch = 0u; ch < s_ir_chans; ch++) {
        float (*fft)[2] = s_state->fft;
        float energy = ir_channel_spectrum(ch, pred, s_ir_len, fft);
        if (energy > 1.0e-12f) {
            float g = 1.0f / s_host->sqrtf(energy);
            if (g < min_gain) min_gain = g;
        }

        float peak = 0.0f;
        for (uint32_t k = 0u; k < FFT_M; k++) {
            float ar = fft[k][0] < 0.0f ? -fft[k][0] : fft[k][0];
            float ai = fft[k][1] < 0.0f ? -fft[k][1] : fft[k][1];
            if (ar > peak) peak = ar;
            if (ai > peak) peak = ai;
        }
        if (peak <= 1.0e-12f) {
            s_host->memset(s_state->spec[ch], 0, sizeof(s_state->spec[ch]));
            continue;
        }

        float scale = 32000.0f / peak;
        raw_wet[ch] = (peak * (1.0f / 32000.0f)) * (1.0f / (float)FFT_M);
        for (uint32_t k = 0u; k < FFT_M; k++) {
            s_state->spec[ch][k][0] = (int16_t)SATURATE_16BIT(round_to_i32(fft[k][0] * scale));
            s_state->spec[ch][k][1] = (int16_t)SATURATE_16BIT(round_to_i32(fft[k][1] * scale));
        }
    }
    if (min_gain >= 1.0e29f) return;

    s_wet_scale[0] = min_gain * raw_wet[0];
    s_wet_scale[1] = min_gain * raw_wet[s_ir_chans > 1u ? 1u : 0u];

    s_prepared = 1u;
}

void plugin_init(void* scratch_mem, uint32_t scratch_size, const PtfxHostApi* host) {
    s_host = host;
    s_state = 0;
    s_prepared = 0u;
    s_dirty = 0u;
    s_file_loaded = 0u;
    s_first_block = 1u;
    s_decay_pct = 100.0f;
    s_predly_ms = 0.0f;
    s_mix = 0.5f;
    s_mix_prev = 0.5f;
    s_tone_hz = 20000.0f;
    s_tone_coeff = 1.0f;
    s_tone_state[0] = 0.0f;
    s_tone_state[1] = 0.0f;
    s_ir_len = 0u;
    s_wet_scale[0] = 0.0f;
    s_wet_scale[1] = 0.0f;

    s_sec_frames = 0u;
    s_sec_chans = 0u;
    s_sec = host->get_secondary_sample ? host->get_secondary_sample(&s_sec_frames, &s_sec_chans) : 0;

    if (scratch_mem && scratch_size >= sizeof(ConvState)) {
        s_state = (ConvState*)scratch_mem;
        host->memset(s_state->hist, 0, sizeof(s_state->hist));
        for (uint32_t n = 0u; n < TWIDDLE_N; n++) {
            s_state->twiddle[n] = host->sinf(PI_F * (float)n / (float)(FFT_M >> 1));
        }
    }

    update_tail();
}

void plugin_set_param(uint8_t index, float value) {
    switch (index) {
        case 0:
            s_sec_frames = 0u;
            s_sec_chans = 0u;
            s_sec = s_host->get_secondary_sample ? s_host->get_secondary_sample(&s_sec_frames, &s_sec_chans) : 0;
            s_file_loaded = (value > 0.5f) ? 1u : 0u;
            update_shape();
            break;
        case 1:
            if (value < 10.0f) value = 10.0f;
            if (value > 100.0f) value = 100.0f;
            s_decay_pct = value;
            update_shape();
            break;
        case 2:
            if (value < 0.0f) value = 0.0f;
            if (value > 60.0f) value = 60.0f;
            s_predly_ms = value;
            update_shape();
            break;
        case 3:
            if (value < 0.0f) value = 0.0f;
            if (value > 100.0f) value = 100.0f;
            s_mix = value * 0.01f;
            break;
        case 4:
            if (value < 500.0f) value = 500.0f;
            if (value > 20000.0f) value = 20000.0f;
            s_tone_hz = value;
            update_tone_coeff();
            break;
        default:
            break;
    }
}

static void multiply_and_inverse(float (*buf)[2], const int16_t (*spec)[2]) {
    rfft_forward(buf);

    // Slot 0 holds two real values (DC and Nyquist), so multiply them separately.
    buf[0][0] *= (float)spec[0][0];
    buf[0][1] *= (float)spec[0][1];

    for (uint32_t k = 1u; k < FFT_M; k++) {
        float xr = buf[k][0], xi = buf[k][1];
        float hr = (float)spec[k][0], hi = (float)spec[k][1];
        buf[k][0] = xr * hr - xi * hi;
        buf[k][1] = xr * hi + xi * hr;
    }

    rfft_inverse(buf);
}

// Slide history left by count samples and append the new block (zeros when src is NULL).
static void shift_history(int16_t* hist, const int16_t* src, uint32_t count) {
    uint32_t keep = IR_MAX - count;
    for (uint32_t i = 0u; i < keep; i++) {
        hist[i] = hist[i + count]; // overlapping source, so no memcpy
    }
    if (src) {
        s_host->memcpy(&hist[keep], src, count * sizeof(int16_t));
    } else {
        s_host->memset(&hist[keep], 0, count * sizeof(int16_t));
    }
}

int32_t plugin_process_block(const int16_t** in, int16_t** out, uint32_t samples, uint8_t channels) {
    if (!out || !out[0] || samples == 0u) {
        return 0;
    }

    uint8_t nch = (channels > 2u) ? 2u : channels;
    if (nch == 2u && !out[1]) {
        nch = 1u;
    }

    if (s_dirty) {
        build_spectra();
    }

    if (!s_state || !s_prepared) {
        // Pass-through; in the appended tail the input is NULL, so output silence.
        for (uint8_t ch = 0u; ch < nch; ch++) {
            const int16_t* src = (in && in[ch]) ? in[ch] : 0;
            if (src) {
                s_host->memcpy(out[ch], src, samples * sizeof(int16_t));
            } else {
                s_host->memset(out[ch], 0, samples * sizeof(int16_t));
            }
        }
        return (int32_t)samples;
    }

    if (s_first_block) {
        s_mix_prev = s_mix;
        s_first_block = 0u;
    }

    float (*buf)[2] = s_state->fft;
    float* win = (float*)buf;
    float tone_a = s_tone_coeff;
    int bypass_tone = (s_tone_hz >= 19999.0f);

    uint32_t done = 0u;
    while (done < samples) {
        uint32_t cnt = samples - done;
        if (cnt > BLOCK) cnt = BLOCK;

        const int16_t* src0 = (in && in[0]) ? (in[0] + done) : 0;
        const int16_t* src1 = (in && nch > 1u && in[1]) ? (in[1] + done) : src0;

        for (uint32_t i = 0u; i < IR_MAX; i++) {
            win[i] = (float)s_state->hist[0][i];
        }
        for (uint32_t i = 0u; i < BLOCK; i++) {
            win[IR_MAX + i] = (i < cnt && src0) ? (float)src0[i] : 0.0f;
        }
        multiply_and_inverse(buf, s_state->spec[0]);

        // Channel 0 wet is in win[IR_MAX..]: copy it aside before channel 1 reuses the buffer.
        // The discarded front of hist[0] and hist[1] holds it (cnt floats = 2 * cnt int16 bytes).
        uint32_t half_bytes = cnt * sizeof(int16_t);
        if (nch == 2u) {
            for (uint32_t i = 0u; i < IR_MAX; i++) {
                win[i] = (float)s_state->hist[1][i];
            }
            s_host->memcpy(s_state->hist[0], &win[IR_MAX], half_bytes);
            s_host->memcpy(s_state->hist[1], (const uint8_t*)&win[IR_MAX] + half_bytes, half_bytes);
            for (uint32_t i = 0u; i < BLOCK; i++) {
                win[IR_MAX + i] = (i < cnt && src1) ? (float)src1[i] : 0.0f;
            }
            multiply_and_inverse(buf, s_state->spec[s_ir_chans > 1u ? 1u : 0u]);
            s_host->memcpy(&win[0], s_state->hist[0], half_bytes);
            s_host->memcpy((uint8_t*)&win[0] + half_bytes, s_state->hist[1], half_bytes);
        }

        float ty0 = s_tone_state[0];
        float ty1 = s_tone_state[1];
        float mix_cur = s_mix_prev;
        float mix_step = (s_mix - s_mix_prev) / (float)cnt;

        for (uint32_t i = 0u; i < cnt; i++) {
            // Stereo: channel 0 wet is restored to win[i], channel 1 wet is in win[IR_MAX + i].
            // Mono: the only wet is in win[IR_MAX + i].
            float wl = ((nch == 2u) ? win[i] : win[IR_MAX + i]) * s_wet_scale[0];
            float wr = (nch == 2u) ? (win[IR_MAX + i] * s_wet_scale[1]) : wl;

            if (!bypass_tone) {
                ty0 += tone_a * (wl - ty0);
                wl = ty0;
                ty1 += tone_a * (wr - ty1);
                wr = ty1;
            }

            mix_cur += mix_step;
            float inv_mix = 1.0f - mix_cur;
            float dry0 = src0 ? (float)src0[i] : 0.0f;
            out[0][done + i] = (int16_t)SATURATE_16BIT(round_to_i32(inv_mix * dry0 + mix_cur * wl));
            if (nch == 2u) {
                float dry1 = src1 ? (float)src1[i] : 0.0f;
                out[1][done + i] = (int16_t)SATURATE_16BIT(round_to_i32(inv_mix * dry1 + mix_cur * wr));
            }
        }

        s_tone_state[0] = ty0;
        s_tone_state[1] = ty1;
        s_mix_prev = s_mix;

        shift_history(s_state->hist[0], src0, cnt);
        if (nch == 2u) {
            shift_history(s_state->hist[1], src1, cnt);
        }
        done += cnt;
    }

    return (int32_t)samples;
}
