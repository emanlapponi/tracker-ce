// PTFX_NAME: Sample Cross-Mod
// PTFX_AUTHOR: Polyend
// PTFX_FLAGS: EFFECT, STEREO
// PTFX_SCRATCH: 0

#include "tracker_plugin.h"

const PluginParam PLUGIN_PARAMS[4] = {
    PTFX_PARAM_FILE("Mod WAV", ".wav"),
    PTFX_PARAM_LIST("Mode",    0, "Layer Mix|Ring Mod|FM Scrub|Amp Follow|Sidechain"),
    PTFX_PARAM_INT ("Pitch",   -24.0f, 24.0f, 0.0f,  1.0f, " st"),
    PTFX_PARAM_INT ("Mix",     0.0f,   100.0f, 75.0f, 5.0f, " %")
};

static const PtfxHostApi* s_host = 0;
static const int16_t* s_sec = 0;
static uint32_t s_sec_frames = 0;
static uint8_t s_sec_channels = 0;

static int s_mode = 0;
static float s_pitch = 0.0f;
static float s_rate = 1.0f;
static float s_mix = 0.75f;

static float s_phase = 0.0f;
static float s_env[2];
static uint32_t s_write_idx = 0;
static float s_mod_line[2][512];

static void refresh_secondary_sample(void) {
    if (s_host && s_host->get_secondary_sample) {
        s_sec = s_host->get_secondary_sample(&s_sec_frames, &s_sec_channels);
    } else {
        s_sec = 0;
        s_sec_frames = 0;
        s_sec_channels = 0;
    }
}

void plugin_init(void* scratch_mem, uint32_t scratch_size, const PtfxHostApi* host) {
    (void)scratch_mem;
    (void)scratch_size;
    s_host = host;

    s_mode = 0;
    s_pitch = 0.0f;
    s_rate = 1.0f;
    s_mix = 0.75f;

    s_phase = 0.0f;
    s_env[0] = 0.0f;
    s_env[1] = 0.0f;
    s_write_idx = 0;

    for (uint8_t ch = 0; ch < 2; ch++) {
        for (uint32_t i = 0; i < 512; i++) {
            s_mod_line[ch][i] = 0.0f;
        }
    }

    refresh_secondary_sample();
}

void plugin_set_param(uint8_t index, float value) {
    if (index == 0) {
        refresh_secondary_sample();
    } else if (index == 1) {
        s_mode = (int)(value + 0.5f);
    } else if (index == 2) {
        s_pitch = value;
        s_rate = (s_host && s_host->powf) ? s_host->powf(2.0f, s_pitch * (1.0f / 12.0f)) : 1.0f;
    } else if (index == 3) {
        s_mix = value * 0.01f;
        if (s_mix < 0.0f) s_mix = 0.0f;
        if (s_mix > 1.0f) s_mix = 1.0f;
    }
}

int32_t plugin_process_block(const int16_t** in, int16_t** out, uint32_t samples, uint8_t channels) {
    if (!in || !out || !in[0] || !out[0] || samples == 0) {
        return 0;
    }

    uint8_t num_ch = (channels > 2) ? 2 : channels;

    if (!s_sec || s_sec_frames == 0 || s_sec_channels == 0) {
        for (uint8_t ch = 0; ch < num_ch; ch++) {
            const int16_t* src = in[ch] ? in[ch] : in[0];
            int16_t* dst = out[ch];
            if (!dst) continue;
            if (s_host && s_host->memcpy) {
                s_host->memcpy(dst, src, samples * sizeof(int16_t));
            } else {
                for (uint32_t i = 0; i < samples; i++) {
                    dst[i] = src[i];
                }
            }
        }
        return (int32_t)samples;
    }

    const int16_t* src0 = in[0];
    const int16_t* src1 = (num_ch > 1 && in[1]) ? in[1] : src0;
    int16_t* dst0 = out[0];
    int16_t* dst1 = (num_ch > 1) ? out[1] : 0;

    const float f_frames = (float)s_sec_frames;
    float phase = s_phase;
    float env0 = s_env[0];
    float env1 = s_env[1];
    uint32_t write_idx = s_write_idx;
    const float dry_mix = 1.0f - s_mix;
    const float wet_mix = s_mix;
    const float rate = s_rate;
    const int mode = s_mode;

    for (uint32_t i = 0; i < samples; i++) {
        if (phase >= f_frames) {
            phase -= f_frames;
            if (phase >= f_frames) {
                phase = 0.0f;
            }
        }

        uint32_t idx0 = (uint32_t)phase;
        if (idx0 >= s_sec_frames) idx0 = 0;
        uint32_t idx1 = idx0 + 1u;
        if (idx1 >= s_sec_frames) idx1 = 0;
        float frac = phase - (float)idx0;

        for (uint8_t ch = 0; ch < num_ch; ch++) {
            const int16_t* src = (ch == 0) ? src0 : src1;
            int16_t* dst = (ch == 0) ? dst0 : dst1;
            if (!dst) continue;

            uint8_t sec_ch = (ch < s_sec_channels) ? ch : 0;
            float s0 = (float)s_sec[idx0 * s_sec_channels + sec_ch] * (1.0f / 32768.0f);
            float s1 = (float)s_sec[idx1 * s_sec_channels + sec_ch] * (1.0f / 32768.0f);
            float mod = s0 + (s1 - s0) * frac;
            float dry = (float)src[i] * (1.0f / 32768.0f);
            float wet = dry;

            if (mode == 0) {
                // Layer Mix
                wet = (dry + mod) * 0.707f;
            } else if (mode == 1) {
                // Ring Mod
                wet = dry * mod * 2.0f;
                if (s_host && s_host->tanhf) {
                    wet = s_host->tanhf(wet);
                } else {
                    if (wet > 1.0f) wet = 1.0f;
                    if (wet < -1.0f) wet = -1.0f;
                }
            } else if (mode == 2) {
                // FM Scrub
                s_mod_line[ch][write_idx] = dry;
                float delay_samples = 128.0f + mod * 120.0f;
                if (delay_samples < 1.0f) delay_samples = 1.0f;
                if (delay_samples > 510.0f) delay_samples = 510.0f;
                float read_pos = (float)write_idx - delay_samples;
                if (read_pos < 0.0f) read_pos += 512.0f;
                uint32_t d0 = (uint32_t)read_pos & 511u;
                uint32_t d1 = (d0 + 1u) & 511u;
                float df = read_pos - (float)((uint32_t)read_pos);
                wet = s_mod_line[ch][d0] * (1.0f - df) + s_mod_line[ch][d1] * df;
            } else if (mode == 3) {
                // Amp Follow
                float abs_mod = (mod < 0.0f) ? -mod : mod;
                float env = (ch == 0) ? env0 : env1;
                float coeff = (abs_mod > env) ? 0.02f : 0.0015f;
                env += coeff * (abs_mod - env);
                if (ch == 0) env0 = env;
                else env1 = env;
                float gain = env * 2.5f;
                if (gain > 2.0f) gain = 2.0f;
                wet = dry * gain;
            } else if (mode == 4) {
                // Sidechain
                float abs_mod = (mod < 0.0f) ? -mod : mod;
                float env = (ch == 0) ? env0 : env1;
                float coeff = (abs_mod > env) ? 0.02f : 0.0015f;
                env += coeff * (abs_mod - env);
                if (ch == 0) env0 = env;
                else env1 = env;
                float duck = env * 2.5f;
                if (duck > 1.0f) duck = 1.0f;
                wet = dry * (1.0f - duck);
            }

            float out_val = dry * dry_mix + wet * wet_mix;
            int32_t pcm = (int32_t)(out_val * 32767.0f);
            dst[i] = (int16_t)SATURATE_16BIT(pcm);
        }

        write_idx = (write_idx + 1u) & 511u;
        phase += rate;
    }

    s_phase = phase;
    s_env[0] = env0;
    s_env[1] = env1;
    s_write_idx = write_idx;

    return (int32_t)samples;
}
