#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Polyend Community Edition Loadable Synth Plugin Binary Format (.psyn)
 *
 * Compatible across:
 *   - Polyend Synth (Wave)
 *   - Polyend Play+ & Original Polyend Play (IMX)
 *   - Polyend Tracker+ & Polyend Tracker Mini
 *
 * Architecture: ARMv7E-M (Cortex-M7), Thumb-2, VFPv5 hard-float (-mfloat-abi=hard).
 */

#define PSYN_MAGIC 0x4E595350u /* "PSYN" in little-endian */
#define PSYN_API_VERSION 1u
#define PSYN_FLAG_HAS_SAMPLE_SLOTS 0x0001u /* Set in PsynHeader::flags if engine uses SampleSlots */
#define PSYN_FLAG_AUDIO_INPUT      0x0002u /* Set in PsynHeader::flags if engine provides voice_render_audio_in */

/**
 * Binary header at byte offset 0 of every .psyn file on SD card (/Synths/<NAME>.psyn).
 * Total size: 128 bytes.
 */
typedef struct __attribute__((packed)) PsynHeader
{
    uint32_t magic;                /* PSYN_MAGIC (0x4E595350) */
    uint16_t api_version;          /* PSYN_API_VERSION (1) */
    uint16_t flags;                /* PSYN_FLAG_* bits */
    uint16_t type_id_wave;         /* InstrumentTypeId on Polyend Synth (or 0 for dynamic) */
    uint16_t type_id_play_tracker; /* InstrumentTypeId on Play/Tracker (or 0 for dynamic) */
    char code_name[8];             /* Short code name, e.g. "ACD", "CHIP" */
    char display_name[24];         /* Human-readable name, e.g. "Chiptune" */
    char patch_folder[16];         /* Folder under /Patches/<patch_folder>/ */
    uint8_t default_voices;        /* Default polyphony (1..8) */
    uint8_t max_voices;            /* Maximum polyphony (1..8) */
    uint8_t output_channels;       /* 0 = None, 1 = Mono, 2 = Stereo */
    uint8_t renders_aux_sends;     /* 1 if voice writes to aux_buffers (e.g. PERC), else 0 */
    uint32_t code_size;            /* Size of .text section in bytes (8-byte aligned) */
    uint32_t rodata_size;          /* Size of .rodata section in bytes (8-byte aligned) */
    uint32_t data_size;            /* Size of .data+.init_array in bytes (8-byte aligned) */
    uint32_t bss_size;             /* Size of .bss section in bytes (8-byte aligned) */
    uint32_t entry_offset;         /* Offset of psyn_plugin_entry from image base (| 1 for Thumb) */
    uint32_t init_array_offset;    /* Offset of .init_array from image base */
    uint32_t init_array_count;     /* Number of constructor pointers in .init_array */
    uint32_t reloc_count;          /* Number of uint32_t relocation offsets following image */
    uint32_t reserved[8];          /* Reserved for future expansion (zeroed) */
} PsynHeader;

#ifdef __cplusplus
static_assert(sizeof(PsynHeader) == 128, "PsynHeader must be 128 bytes");
#endif

/**
 * MIDI Event types matching polysynth::MidiEvent::Type.
 */
enum PsynMidiEventType
{
    PSYN_MIDI_CLOCK = 0,
    PSYN_MIDI_START = 1,
    PSYN_MIDI_STOP = 2,
    PSYN_MIDI_CONTINUE = 3,
    PSYN_MIDI_NOTE_ON = 4,
    PSYN_MIDI_NOTE_OFF = 5,
    PSYN_MIDI_CONTROL_CHANGE = 6,
    PSYN_MIDI_PROGRAM_CHANGE = 7,
    PSYN_MIDI_PITCH_BEND = 8,
    PSYN_MIDI_POLY_AFTERTOUCH = 9,
    PSYN_MIDI_CHANNEL_AFTERTOUCH = 10
};

typedef struct PsynMidiEvent
{
    uint8_t type;       /* PsynMidiEventType */
    uint8_t channel;    /* MIDI channel */
    uint8_t data1;      /* note or CC id */
    uint8_t data2;      /* velocity or CC value */
    int16_t pitch_bend; /* Pitch bend (-8192..8191) */
    uint16_t reserved;
} PsynMidiEvent;

typedef struct PsynVoiceContext
{
    float tempo;
    float pitchbend_range_semitones;
} PsynVoiceContext;

/**
 * Host audio input for engines flagged with PSYN_FLAG_AUDIO_INPUT.
 * A param with a non-zero PsynParamDesc::input_source routes that host source into the engine
 * while the param value is above 0.5. Source ids: 0..7 sample tracks, 8..10 synth parts
 * (only parts before the engine's own part are available).
 * Sidechain sources are summed separately and stay in the normal mix.
 * Buffers hold 128 samples. Pointers are NULL when nothing is routed.
 */
typedef struct PsynAudioInput
{
    const float* left;
    const float* right;
    const float* sidechain_left;
    const float* sidechain_right;
} PsynAudioInput;

typedef struct PsynSampleData
{
    const int16_t* data;
    uint32_t size;
    uint8_t channels;
    uint8_t ready;
    uint16_t wave_size;
    uint32_t sample_rate;
} PsynSampleData;

typedef struct PsynParamDesc
{
    const char* display_name;
    float default_value;
    float min_value;
    float max_value;
    uint8_t has_default;
    uint8_t curve;                /* 0=Linear, 1=Exponential, 2=ExponentialSteep, 3=Power */
    uint8_t macro_op;             /* 0=Add, 1=Mult */
    uint8_t is_enum;              /* 0 or 1 */
    uint8_t midi_cc;              /* 0xFF if none */
    uint8_t invalidates_metadata; /* 0 or 1 */
    uint8_t input_source;         /* 0 = none, else host input source id + 1 (see below) */
    uint8_t input_sidechain;      /* 1 = source feeds the sidechain instead of the main input */
} PsynParamDesc;

typedef struct PsynMacroModDesc
{
    int16_t param_id; /* -1 if none */
    uint16_t reserved;
    float amount;
} PsynMacroModDesc;

typedef struct PsynMacroDesc
{
    const char* display_name;
    uint8_t bipolar;
    uint8_t modifier_count;
    uint16_t reserved;
    PsynMacroModDesc modifiers[8];
} PsynMacroDesc;

typedef struct PsynSampleSlotDesc
{
    const char* display_name;
    const char* default_sample_path;
    uint8_t type; /* 0=Sample, 1=WaveTable */
    uint8_t section;
    uint8_t subsection;
    uint8_t reserved;
} PsynSampleSlotDesc;

typedef struct PsynLayoutElement
{
    uint8_t type; /* 0=Empty, 1=Param, 2=SampleSlot */
    uint8_t reserved;
    int16_t id;
} PsynLayoutElement;

/**
 * Host services table passed to psyn_plugin_entry().
 */
typedef struct PsynHostApi
{
    uint32_t api_version;
    float sample_rate;
    uint32_t block_size;

    /* Dynamic memory allocation from the external SDRAM plugin pool */
    void* (*slot_alloc)(size_t size, size_t alignment);
    void (*slot_free)(void* ptr);

    /* Host Patch accessors */
    bool (*patch_get_param_value)(const void* host_patch, int param_id, float* out_val);
    bool (*patch_get_param_final_value)(const void* host_patch, int param_id, float* out_val);
    void (*patch_set_param_value)(void* host_patch,
                                  int param_id,
                                  bool has_val,
                                  float val,
                                  int source);
    bool (*patch_get_sample)(const void* host_patch, int slot_id, PsynSampleData* out_sample);

    /* Host Metadata builder callbacks (used inside create_metadata) */
    void (*meta_set_info)(void* meta_builder,
                          int output_channels,
                          bool macros_affect_cc,
                          bool renders_aux_sends,
                          int max_voices);
    void (*meta_set_param_count)(void* meta_builder, int count);
    void (*meta_set_param)(void* meta_builder, int param_id, const PsynParamDesc* desc);
    void (*meta_add_sample_slot)(void* meta_builder, const PsynSampleSlotDesc* desc);
    void (*meta_add_flat_section)(void* meta_builder,
                                  const char* name,
                                  const char* short_name,
                                  const PsynLayoutElement* elements,
                                  int count);
    int (*meta_add_33_section)(void* meta_builder,
                               const char* name,
                               const char* short_name,
                               const PsynLayoutElement* elements,
                               int count);
    void (*meta_add_33_subsection)(void* meta_builder,
                                   int section_idx,
                                   const char* name,
                                   const PsynLayoutElement* elements,
                                   int count);
    void (*meta_set_default_macro)(void* meta_builder, int macro_idx, const PsynMacroDesc* desc);

    /* Direct SD Card File & WAV I/O (appended at end of PsynHostApi for v1 ABI compatibility) */
    int32_t (*read_file)(const char* path, void* buffer, uint32_t max_len);
    int32_t (*write_file)(const char* path, const void* data, uint32_t len);
    bool (*load_wav_file)(const char* path,
                          int16_t** out_pcm_data,
                          uint32_t* out_sample_count,
                          uint8_t* out_channels,
                          uint32_t* out_sample_rate);
    void (*free_wav_file)(int16_t* pcm_data);
} PsynHostApi;

/**
 * Plugin interface table returned by psyn_plugin_entry().
 */
typedef struct PsynPluginApi
{
    void (*create_metadata)(const void* host_patch, void* meta_builder);
    int (*format_param)(const void* host_patch,
                        int param_id,
                        float value,
                        char* out_buf,
                        int out_buf_len);

    void* (*create_voice)(int voice_idx);
    void (*destroy_voice)(void* voice);
    void (*voice_set_patch)(void* voice, void* host_patch);
    void (*voice_on_midi_event)(void* voice,
                                const PsynVoiceContext* ctx,
                                const PsynMidiEvent* event);
    void (*voice_on_param_change)(void* voice, int param_id, bool has_value, float value);
    void (*voice_render_audio)(void* voice,
                               const PsynVoiceContext* ctx,
                               float* out_left,
                               float* out_right,
                               float* const* aux_buffers,
                               int aux_count);
    void (*voice_render_post_process)(void* voice,
                                      const PsynVoiceContext* ctx,
                                      float* out_left,
                                      float* out_right);
    void (*voice_render_graph)(void* voice, float* out_points, int count);
    bool (*voice_is_active)(void* voice);
    bool (*voice_handles_note)(void* voice, uint8_t note);
    bool (*voice_handles_any_note)(void* voice);
    int (*voice_get_last_note)(void* voice);
    bool (*voice_get_current_velocity)(void* voice, float* out_vel);
    void (*voice_clear_previous_notes)(void* voice);
    void (*voice_reset)(void* voice);

    /* Appended for engines with PSYN_FLAG_AUDIO_INPUT; older engines end at voice_reset */
    void (*voice_render_audio_in)(void* voice,
                                  const PsynVoiceContext* ctx,
                                  const PsynAudioInput* in,
                                  float* out_left,
                                  float* out_right);
} PsynPluginApi;

typedef const PsynPluginApi* (*PsynEntryFn)(const PsynHostApi* host);

const PsynPluginApi* psyn_plugin_entry(const PsynHostApi* host);

#ifdef __cplusplus
}
#endif
