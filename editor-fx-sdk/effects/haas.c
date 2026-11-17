// PTFX_NAME: Haas Effect
// PTFX_AUTHOR: Emanuele Lapponi
// PTFX_FLAGS: EFFECT, STEREO
// PTFX_SCRATCH: 8820

#include "tracker_plugin.h"

// Knobs: delay amount in ms, and which channel (L or R) gets the delay.
// The selected channel is shifted later by the delay amount; the output is
// extended by the tail and the delayed channel fades out over its final
// ~10 ms so it ends on a zero crossing (no click).
const PluginParam PLUGIN_PARAMS[2] = {
    PTFX_PARAM_INT("Delay", 1.0f, 100.0f, 10.0f, 1.0f, " ms"),
    PTFX_PARAM_LIST("Chan", 0, "Left|Right")
};

#define MAX_DELAY_SAMPLES 4410 // 100 ms @ 44.1 kHz

static const PtfxHostApi* s_host = 0;
static int16_t* s_delay = 0;       // delay line (ring buffer in scratch_mem)
static uint32_t s_idx = 0;         // ring buffer read/write index [0, s_delay_len)
static uint32_t s_delay_len = 441; // 10 ms default
static uint8_t  s_chan = 0;        // 0 = Left, 1 = Right
static uint32_t s_tail_count = 0;  // samples processed in the appended tail

static void update_tail(void) {
    if (s_host && s_host->set_extra_samples) {
        // Append the delay tail so the whole shifted channel fits in the output.
        s_host->set_extra_samples(0, s_delay_len);
    }
}

void plugin_init(void* scratch_mem, uint32_t scratch_size, const PtfxHostApi* host) {
    s_host = host;
    s_idx = 0;
    s_delay_len = 441;
    s_chan = 0;
    s_tail_count = 0;
    s_delay = (scratch_mem && scratch_size >= MAX_DELAY_SAMPLES * sizeof(int16_t))
                ? (int16_t*)scratch_mem
                : 0;
    update_tail();
}

void plugin_set_param(uint8_t index, float value) {
    if (index == 0) {
        uint32_t len = (uint32_t)(value * 44.1f + 0.5f);
        if (len < 1) len = 1;
        if (len > MAX_DELAY_SAMPLES) len = MAX_DELAY_SAMPLES;
        s_delay_len = len;
    } else if (index == 1) {
        s_chan = (value >= 0.5f) ? 1 : 0;
    }
    update_tail();
}

int32_t plugin_process_block(const int16_t** in, int16_t** out, uint32_t samples, uint8_t channels) {
    if (!out || !out[0] || samples == 0 || !s_host) {
        return 0;
    }

    // Fallback pass-through if mono platform or scratch allocation failed.
    if (channels < 2 || !out[1] || !s_delay) {
        for (uint8_t ch = 0; ch < channels && ch < 2; ch++) {
            if (!out[ch]) continue;
            const int16_t* src = (in && in[ch]) ? in[ch] : 0;
            if (src) {
                s_host->memcpy(out[ch], src, samples * sizeof(int16_t));
            } else {
                s_host->memset(out[ch], 0, samples * sizeof(int16_t));
            }
        }
        return (int32_t)samples;
    }

    const uint8_t delayed_ch = (s_chan < 2) ? s_chan : 0;
    const uint8_t dry_ch     = 1 - delayed_ch;

    // Undelayed channel: unchanged during body, silence in the appended tail.
    const int16_t* dry_src = (in && in[dry_ch]) ? in[dry_ch] : 0;
    int16_t*       dry_dst = out[dry_ch];
    if (dry_src) {
        s_host->memcpy(dry_dst, dry_src, samples * sizeof(int16_t));
    } else {
        s_host->memset(dry_dst, 0, samples * sizeof(int16_t));
    }

    // Delayed channel: ring buffer of exact length dlen (read slot == write slot).
    const int16_t* wet_src   = (in && in[delayed_ch]) ? in[delayed_ch] : 0;
    int16_t*       wet_dst   = out[delayed_ch];
    int16_t*       delay_buf = s_delay;
    const uint32_t dlen      = s_delay_len;
    uint32_t       idx       = s_idx;

    if (wet_src) {
        for (uint32_t i = 0; i < samples; i++) {
            wet_dst[i] = delay_buf[idx];
            delay_buf[idx] = wet_src[i];
            if (++idx >= dlen) idx = 0;
        }
    } else {
        // In the appended tail: drain the delay line and fade the final ~10 ms to 0.
        uint32_t fade = (s_host && s_host->sample_rate > 0) ? s_host->sample_rate / 100 : 441;
        if (fade >= dlen) fade = (dlen > 1) ? dlen - 1 : 0;
        const float inv_fade = (fade > 1) ? (1.0f / (float)(fade - 1)) : 0.0f;
        const uint32_t fade_start = dlen - fade;
        uint32_t t = s_tail_count;

        for (uint32_t i = 0; i < samples; i++, t++) {
            int32_t v = delay_buf[idx];
            delay_buf[idx] = 0;
            if (++idx >= dlen) idx = 0;

            if (fade > 1 && t >= fade_start && t < dlen) {
                v = (int32_t)((float)v * (float)(dlen - 1 - t) * inv_fade);
            } else if (t >= dlen - 1) {
                v = 0;
            }
            wet_dst[i] = (int16_t)v;
        }
        s_tail_count = t;
    }

    s_idx = idx;
    return (int32_t)samples;
}
