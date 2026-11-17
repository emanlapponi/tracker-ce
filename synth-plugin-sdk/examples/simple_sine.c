#include <math.h>
#include <psyn/psyn_plugin.h>
#include <string.h>

/**
 * Minimal pure-C Polyend .psyn synth plugin example (Simple Sine Synth).
 */

static const PsynHostApi* g_host = NULL;

enum ParamId
{
    kParamPitch = 0,
    kParamDetune = 1,
    kParamDecay = 2,
    kParamGain = 3,
    kParamCount = 4
};

typedef struct SimpleSineVoice
{
    const void* host_patch;
    float phase;
    float env;
    float velocity;
    int note;
    bool gate;
} SimpleSineVoice;

static void create_metadata(const void* host_patch, void* meta_builder)
{
    (void)host_patch;
    g_host->meta_set_info(meta_builder, 2, true, false, 8);
    g_host->meta_set_param_count(meta_builder, kParamCount);

    const PsynParamDesc params[kParamCount] = {
        {"Pitch", 0.0f, -24.0f, 24.0f, 1, 0, 0, 0, 20, 0, 0},
        {"Detune", 0.0f, -1.0f, 1.0f, 1, 0, 0, 0, 21, 0, 0},
        {"Decay", 0.5f, 0.01f, 4.0f, 1, 1, 0, 0, 22, 0, 0},
        {"Gain", 0.0f, -36.0f, 6.0f, 1, 0, 0, 0, 23, 0, 0},
    };
    for (int i = 0; i < kParamCount; ++i)
    {
        g_host->meta_set_param(meta_builder, i, &params[i]);
    }

    const PsynLayoutElement elems[kParamCount] = {
        {1, 0, kParamPitch},
        {1, 0, kParamDetune},
        {1, 0, kParamDecay},
        {1, 0, kParamGain},
    };
    g_host->meta_add_flat_section(meta_builder, "Oscillator", "Osc", elems, kParamCount);
}

static int format_param(const void* host_patch,
                        int param_id,
                        float value,
                        char* out_buf,
                        int out_buf_len)
{
    (void)host_patch;
    (void)param_id;
    if (!out_buf || out_buf_len < 8)
    {
        return 0;
    }
    int iv = (int)roundf(value * 10.0f);
    int sign = iv < 0 ? -1 : 1;
    if (iv < 0)
        iv = -iv;
    int whole = iv / 10;
    int frac = iv % 10;
    int pos = 0;
    if (sign < 0 && pos < out_buf_len - 1)
        out_buf[pos++] = '-';
    if (whole >= 10 && pos < out_buf_len - 1)
        out_buf[pos++] = (char)('0' + (whole / 10) % 10);
    if (pos < out_buf_len - 1)
        out_buf[pos++] = (char)('0' + (whole % 10));
    if (pos < out_buf_len - 1)
        out_buf[pos++] = '.';
    if (pos < out_buf_len - 1)
        out_buf[pos++] = (char)('0' + frac);
    out_buf[pos] = '\0';
    return pos;
}

static void* create_voice(int voice_idx)
{
    (void)voice_idx;
    SimpleSineVoice* v = (SimpleSineVoice*)g_host->slot_alloc(sizeof(SimpleSineVoice), 8);
    if (v)
    {
        memset(v, 0, sizeof(*v));
        v->note = -1;
    }
    return v;
}

static void destroy_voice(void* voice)
{
    if (voice)
    {
        g_host->slot_free(voice);
    }
}

static void voice_set_patch(void* voice, void* host_patch)
{
    SimpleSineVoice* v = (SimpleSineVoice*)voice;
    v->host_patch = host_patch;
}

static void voice_on_midi_event(void* voice,
                                const PsynVoiceContext* ctx,
                                const PsynMidiEvent* event)
{
    (void)ctx;
    SimpleSineVoice* v = (SimpleSineVoice*)voice;
    if (event->type == PSYN_MIDI_NOTE_ON && event->data2 > 0)
    {
        v->note = event->data1;
        v->velocity = (float)event->data2 / 127.0f;
        v->env = 1.0f;
        v->gate = true;
    }
    else if (event->type == PSYN_MIDI_NOTE_OFF ||
             (event->type == PSYN_MIDI_NOTE_ON && event->data2 == 0))
    {
        if (v->note == event->data1)
        {
            v->gate = false;
        }
    }
}

static void voice_on_param_change(void* voice, int param_id, bool has_value, float value)
{
    (void)voice;
    (void)param_id;
    (void)has_value;
    (void)value;
}

static void voice_render_audio(void* voice,
                               const PsynVoiceContext* ctx,
                               float* out_left,
                               float* out_right,
                               float* const* aux_buffers,
                               int aux_count)
{
    (void)ctx;
    (void)aux_buffers;
    (void)aux_count;
    SimpleSineVoice* v = (SimpleSineVoice*)voice;
    if (v->env <= 0.0001f || v->note < 0)
    {
        v->env = 0.0f;
        return;
    }

    float pitch_offset = 0.0f;
    float detune = 0.0f;
    float decay = 0.5f;
    if (v->host_patch)
    {
        g_host->patch_get_param_final_value(v->host_patch, kParamPitch, &pitch_offset);
        g_host->patch_get_param_final_value(v->host_patch, kParamDetune, &detune);
        g_host->patch_get_param_final_value(v->host_patch, kParamDecay, &decay);
    }

    float note_f = (float)v->note + pitch_offset + detune;
    float freq = 440.0f * powf(2.0f, (note_f - 69.0f) / 12.0f);
    float phase_inc = freq / g_host->sample_rate;
    float env_mul = expf(-1.0f / (fmaxf(0.01f, decay) * g_host->sample_rate));

    uint32_t frames = g_host->block_size;
    for (uint32_t i = 0; i < frames; ++i)
    {
        float sample = sinf(v->phase * 6.283185307f) * v->env * v->velocity * 0.25f;
        v->phase += phase_inc;
        if (v->phase >= 1.0f)
            v->phase -= 1.0f;
        if (!v->gate)
            v->env *= env_mul;
        out_left[i] += sample;
        out_right[i] += sample;
    }
}

static void voice_render_post_process(void* voice,
                                      const PsynVoiceContext* ctx,
                                      float* out_left,
                                      float* out_right)
{
    (void)voice;
    (void)ctx;
    (void)out_left;
    (void)out_right;
}

static void voice_render_graph(void* voice, float* out_points, int count)
{
    (void)voice;
    for (int i = 0; i < count; ++i)
    {
        out_points[i] = sinf(((float)i / (float)count) * 6.283185307f);
    }
}

static bool voice_is_active(void* voice)
{
    SimpleSineVoice* v = (SimpleSineVoice*)voice;
    return v->env > 0.0001f;
}

static bool voice_handles_note(void* voice, uint8_t note)
{
    SimpleSineVoice* v = (SimpleSineVoice*)voice;
    return v->env > 0.0001f && v->note == (int)note;
}

static bool voice_handles_any_note(void* voice)
{
    SimpleSineVoice* v = (SimpleSineVoice*)voice;
    return v->env > 0.0001f;
}

static int voice_get_last_note(void* voice)
{
    SimpleSineVoice* v = (SimpleSineVoice*)voice;
    return v->note;
}

static bool voice_get_current_velocity(void* voice, float* out_vel)
{
    SimpleSineVoice* v = (SimpleSineVoice*)voice;
    if (v->env > 0.0001f)
    {
        *out_vel = v->velocity;
        return true;
    }
    return false;
}

static void voice_clear_previous_notes(void* voice)
{
    (void)voice;
}

static void voice_reset(void* voice)
{
    SimpleSineVoice* v = (SimpleSineVoice*)voice;
    v->env = 0.0f;
    v->gate = false;
}

static const PsynPluginApi kPluginApi = {
    .create_metadata = create_metadata,
    .format_param = format_param,
    .create_voice = create_voice,
    .destroy_voice = destroy_voice,
    .voice_set_patch = voice_set_patch,
    .voice_on_midi_event = voice_on_midi_event,
    .voice_on_param_change = voice_on_param_change,
    .voice_render_audio = voice_render_audio,
    .voice_render_post_process = voice_render_post_process,
    .voice_render_graph = voice_render_graph,
    .voice_is_active = voice_is_active,
    .voice_handles_note = voice_handles_note,
    .voice_handles_any_note = voice_handles_any_note,
    .voice_get_last_note = voice_get_last_note,
    .voice_get_current_velocity = voice_get_current_velocity,
    .voice_clear_previous_notes = voice_clear_previous_notes,
    .voice_reset = voice_reset,
    /* Optional: populate alongside PSYN_FLAG_HAS_VOICE_TICK_IDLE when PSYN_FLAG_SUPPORTS_IDLE_SLEEP is used */
    .voice_tick_idle = NULL,
};

const PsynPluginApi* psyn_plugin_entry(const PsynHostApi* host)
{
    g_host = host;
    return &kPluginApi;
}
