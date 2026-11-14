# Polyend Custom Synth Engine & `.psyn` Plugin SDK - Developer Guide

The **Polyend Community Edition Synth Plugin SDK** ([`synth-plugin-sdk/`](../synth-plugin-sdk/) + [`synth/include/PolySynth/sdk/`](../synth/include/PolySynth/sdk/)) lets developers create standalone, relocatable `.psyn` synthesizer engines loaded directly from `/Synths/*.psyn` on the microSD card, **without recompiling the firmware** and with full cross-device binary compatibility across IMX powered Polyend devices (`ARMv7E-M` Cortex-M7 @ 600 MHz, Thumb-2, VFPv5 hard-float).

---

## Table of Contents

1. [Architecture Overview (`PsynLoader` & `/Synths/*.psyn`)](#1-architecture-overview-psynloader--synthspsyn)
2. [SDK Headers & Two Authoring Modes](#2-sdk-headers--two-authoring-modes)
3. [Voice Lifecycle & Audio Rendering Model](#3-voice-lifecycle--audio-rendering-model)
4. [Step-by-Step Tutorial: Building & Packaging a `.psyn` Engine](#4-step-by-step-tutorial-building--packaging-a-psyn-engine)
5. [Sample-Based Synth Engines (`SampleSlot` & `SamplePool`)](#5-sample-based-synth-engines-sampleslot--samplepool)
6. [Direct SD Card & WAV File I/O (`PsynHostApi`)](#6-direct-sd-card--wav-file-io-psynhostapi)
7. [Global File Browser Integration (Patches & Sample Slots)](#7-global-file-browser-integration-patches--sample-slots)
8. [Patch Metadata, Curves, Macros & UI Layout Reference](#8-patch-metadata-curves-macros--ui-layout-reference)
9. [DSP Utilities & Value Formatters](#9-dsp-utilities--value-formatters)
10. [Real-Time Audio, CPU & Memory Guidelines](#10-real-time-audio-cpu--memory-guidelines)
11. [Reference Implementations (`chip.cpp` & `simple_sine.c`)](#11-reference-implementations-chipcpp--simple_sinec)

---

## 1. Architecture Overview (`PsynLoader` & `/Synths/*.psyn`)

All synthesizer engines in `tracker-ce` (built-in factory engines `ACD`, `FAT`, `VAP`, `WTFM`, `PERC`, `DIRT`, the open-source reference engine `CHIP`, as well as any third-party community engine) are distributed and loaded dynamically as standalone **`.psyn`** plugins on the SD card:

```text
┌────────────────────────────────────────────────────────────────────────────┐
│                           Tracker Application                              │
│  ┌────────────────────────────────┐  ┌──────────────────────────────────┐  │
│  │ core/synth/Synth.cpp           │  │ SynthEditor / SynthPatchEditor   │  │
│  │ core/synth/FileSystemAdapter   │  │ GlobalFileBrowser (patches/WAVs) │  │
│  └───────────────┬────────────────┘  └─────────────────┬────────────────┘  │
│                  │                                     │                   │
│  ┌───────────────▼─────────────────────────────────────▼────────────────┐  │
│  │                    libPolySynth.a + PsynLoader                       │  │
│  │  • Scans /Synths/*.psyn headers at boot                              │  │
│  │  • Loads up to 1,024 KB into the 1,920 KB SDRAM plugin arena at boot │  │
│  │  • Loads additional engines on demand & LRU-evicts inactive engines  │  │
│  │  • Shares 4 MB SamplePool across all active sample-based voices      │  │
│  └──────────────────────────────────┬───────────────────────────────────┘  │
└─────────────────────────────────────┼──────────────────────────────────────┘
                                      │
                   ┌──────────────────▼──────────────────┐
                   │     SD Card (/Synths/*.psyn)        │
                   │  • Built using synth-plugin-sdk/    │
                   │  • C++20 (psyn_cpp_bridge.h)        │
                   │  • or Pure C (psyn_plugin.h)        │
                   └─────────────────────────────────────┘
```

### Key Architectural Facts
* **Zero Firmware Changes Needed:** Drop any compiled `MYENGINE.psyn` onto the SD card under `/Synths/`. `PsynLoader` ([`synth/source/synths/PsynLoader.h`](../synth/source/synths/PsynLoader.h), [`PsynLoader.cpp`](../synth/source/synths/PsynLoader.cpp)) discovers `.psyn` headers automatically at boot and [`SynthEditor`](../core/interface/instrumentEditor/SynthEditor.cpp) populates the model selector dynamically from `synth->getSynthTypes()`.
* **Dedicated `6 MB` Synth SDRAM Pool (`PSYN_RAM_POOL_SIZE`):** Defined in [`core/source/mtStructs.h`](../core/source/mtStructs.h):
  * **`1,920 KB` `.psyn` Executable & Heap Arena (`PsynLoader::kArenaSize = 3 * 640 KB`):** Holds relocated plugin code, `.rodata`, `.data`, `.bss`, `.init_array` constructors, per-engine `slot_alloc` allocations, and `load_wav_file` buffers. Boot preloading caches engines up to `kMaxPreloadBytes = 1,024 KB`; remaining `.psyn` plugins load lazily when selected, with **LRU eviction** of inactive engines when headroom is needed.
  * **`4 MB` Synth `SamplePool` (`initSamplePool(4 * 1024 * 1024)`):** Shared reference-counted SDRAM pool for engine `SampleSlot` assets (custom sample-based `.psyn` engines) with content deduplication across instruments and patches.
* **Cross-Device Compatibility:** Every `.psyn` header carries both `type_id_wave` (Polyend Synth) and `type_id_play_tracker` (Play/Tracker), or `0` to auto-assign deterministic non-colliding dynamic runtime IDs (`0x100+`).

---

## 2. SDK Headers & Two Authoring Modes

You can implement a `.psyn` plugin in either **C++20** or **Pure C**:

### Mode A: C++20 (`psyn_cpp_bridge.h` + `PolySynth/sdk/`)
Recommended for full-featured engines sharing code cleanly with Polyend's `Patch`, `Metadata`, `CustomSynthVoice`, and value formatters.

| Header | Purpose |
| :--- | :--- |
| [`synth-plugin-sdk/include/psyn/psyn_cpp_bridge.h`](../synth-plugin-sdk/include/psyn/psyn_cpp_bridge.h) | Translates any `polysynth::InstrumentVoice` subclass into the C `.psyn` plugin ABI via `PSYN_REGISTER_CPP_VOICE(VoiceClass)`. |
| [`synth/include/PolySynth/sdk/SynthVoiceSDK.h`](../synth/include/PolySynth/sdk/SynthVoiceSDK.h) | Header-only C++20 base class `CustomSynthVoice`, `ParamHelper`, math helpers, standard `MidiCCs`, and formatters. |
| [`synth/include/PolySynth/sdk/InstrumentVoice.h`](../synth/include/PolySynth/sdk/InstrumentVoice.h) | Pure virtual voice interface (`onMidiEvent`, `onParamChange`, `renderVoiceAudio`, `renderPostProcessAudio`, note query methods). |
| [`synth/include/PolySynth/sdk/InstrumentType.h`](../synth/include/PolySynth/sdk/InstrumentType.h) | Engine factory interface (`createMetadata`, `createVoice`). |
| [`synth/include/PolySynth/Core.h`](../synth/include/PolySynth/Core.h) | Core types (`sample_t = float`, `note_t = uint8_t`, `AudioBufferRef`, `AudioBufferStereoRef`, `kNoteA4 = 69`, `kMaxMacroCount = 8`, `kUserMacroCount = 6`). |
| [`synth/include/PolySynth/Patch.h`](../synth/include/PolySynth/Patch.h) | `Patch`, `Patch::Metadata`, `Param`, `Macro`, `SampleSlot`, `EditorLayout`, `EditorElement`, `OutputChannels`. |
| [`synth/include/PolySynth/SampleStore.h`](../synth/include/PolySynth/SampleStore.h) | `Sample` / `SamplePool` data types (`data`, `format`, `waveSize`, `ready`, `basePitch`). |
| [`synth/include/PolySynth/MidiEvent.h`](../synth/include/PolySynth/MidiEvent.h) | `MidiEvent` structure delivered to `onMidiEvent`. |

### Mode B: Pure C (`psyn_plugin.h`)
| Header | Purpose |
| :--- | :--- |
| [`synth-plugin-sdk/include/psyn/psyn_plugin.h`](../synth-plugin-sdk/include/psyn/psyn_plugin.h) | Self-contained 128-byte `PsynHeader` (`PSYN_MAGIC = 0x4E595350`, `PSYN_API_VERSION = 1`, `PSYN_FLAG_HAS_SAMPLE_SLOTS = 0x0001`), `PsynHostApi` callbacks, `PsynPluginApi` vtable, and `psyn_plugin_entry(const PsynHostApi* host)`. |

---

## 3. Voice Lifecycle & Audio Rendering Model

The Tracker audio engine supports **3 active synth slots** (`SYN 1`, `SYN 2`, `SYN 3`) sharing up to **8 voices total**:

1. **Discovery & Initialization**: On SD mount, `PsynLoader` reads the 128-byte `PsynHeader` from every `.psyn` file in `/Synths/` (falling back to `/Synth/`), registers its `InstrumentType`, and creates `/SynthSamples/<CODE_NAME>/` if `PSYN_FLAG_HAS_SAMPLE_SLOTS` is set.
2. **Activation & Instantiation**: When a user selects the engine or loads a `.patch`, `PsynLoader` relocates the `.psyn` ELF sections into the `1,920 KB` SDRAM arena (if not already resident), calls static `.init_array` constructors, and constructs active voices (`create_voice`).
3. **Parameter Synchronization**: Changing a knob, macro, or loading a patch triggers `onParamChange(paramId, newValue)` (or `voice_on_param_change` in C).
4. **MIDI Delivery**: Pattern notes, pads, and external MIDI trigger `onMidiEvent(ctx, event)` (or `CustomSynthVoice::onNoteOn` / `onNoteOff`).
5. **128-Sample Audio Rendering**: Every audio block (`block_size = 128` samples at `44,100 Hz`), active voices render normalized floating-point audio (`[-1.0f, +1.0f]`) via `renderVoiceAudio` (`voice_render_audio`).
   - Stereo voices receive both Left (`outputBuffer[0]`) and Right (`outputBuffer[1]`).
   - Mono voices render into `outputBuffer[0]`.
   - Voices may optionally write stereo post-processing (`renderPostProcessAudio`) or scope waveform previews (`renderGraph` / `voice_render_graph`).

### Audio Input & Routing (Insert Effect Engines like `BUS`)

Engines packaged with `--audio-input 1` (`PSYN_FLAG_AUDIO_INPUT`) receive host audio and can act as insert effects. [`synth-plugin-sdk/engines/bus.cpp`](../synth-plugin-sdk/engines/bus.cpp) is the reference implementation.

- **Source params**: Set `Patch::Metadata::Param::inputSource` (`input_source` in `PsynParamDesc`) on an on/off param. While its value is above `0.5`, the host sums that source into the engine input and removes it from its normal mix. Source ids are `Param::kInputSourceSynthBase + n` for synth part `n`.
- **Render order**: A part only receives synth parts before it (`SYN 1` can feed `SYN 2` or `SYN 3`, never the reverse). Params naming unavailable sources are ignored.
- **Input buffer**: Delivered as the `inputBuffer` argument of `renderVoiceAudio` (C++) or `PsynAudioInput` of `voice_render_audio_in` (Pure C, appended to `PsynPluginApi`). It is empty when nothing is routed.
- **Voices**: Set `metadata.maxVoices = 1` (and `--max-voices 1`), otherwise the input is processed once per voice.
- **Sends**: A routed source keeps its delay/reverb sends.

---

## 4. Step-by-Step Tutorial: Building & Packaging a `.psyn` Engine

### Step 1: Write Your Voice Class (`my_synth.cpp`)

Include `SynthVoiceSDK.h` and `psyn/psyn_cpp_bridge.h`, define your `ParamId` enum, implement `CustomSynthVoice<MySynthVoice, MyParamId>`, and export with `PSYN_REGISTER_CPP_VOICE`:

```cpp
#include "PolySynth/sdk/SynthVoiceSDK.h"
#include "psyn/psyn_cpp_bridge.h"
#include <cmath>

enum class MyParamId : int
{
    kCutoff = 0,
    kResonance,
    kAttack,
    kRelease,
    kPlayMode,
};

namespace polysynth
{

class MySynthVoice : public sdk::CustomSynthVoice<MySynthVoice, MyParamId>
{
  public:
    MySynthVoice() = default;

    bool isActive() final { return envLevel > 0.0001f || gate; }

    void onNoteOn(const InstrumentVoiceContext&, note_t note, float velocity, bool isLegato) final
    {
        noteFreq = sdk::noteToFreq(note);
        velGain = velocity;
        gate = true;
        if (!isLegato)
        {
            phase = 0.0f;
        }
    }

    void onNoteOff(const InstrumentVoiceContext&, note_t) final
    {
        gate = false;
    }

    void renderVoiceAudio(const InstrumentVoiceContext&, AudioBufferStereoRef out) final
    {
        auto left = out[0];
        auto right = out[1];
        const float dt = noteFreq / sdk::kSampleRate;

        for (size_t i = 0; i < left.size(); ++i)
        {
            envLevel += ((gate ? 1.0f : 0.0f) - envLevel) * (gate ? attackCoeff : releaseCoeff);
            phase += dt;
            if (phase >= 1.0f) phase -= 1.0f;

            float sig = (2.0f * phase - 1.0f) * envLevel * velGain * 0.4f;
            left[i] = sig;
            right[i] = sig;
        }
    }

    static void createMetadata(const Patch&, Patch::Metadata& metadata)
    {
        using namespace sdk;
        const size_t n = 5;
        metadata.params.resize(n);
        if (paramHandlers.empty()) paramHandlers.resize(n);

        ParamHelper<MySynthVoice> helper(metadata, paramHandlers);
        helper.addParam(*MyParamId::kCutoff, {
            .displayName = "Cutoff",
            .defaultValue = 2000.0f,
            .minValue = 40.0f,
            .maxValue = 18000.0f,
            .editCurve = Patch::Metadata::Param::Curve::kExponential,
            .midiCC = *MidiCCs::kFilterCutoff,
            .valueFormatter = createFrequencyFormatter(18000.0f),
        }, [](auto& v, Patch::optional_paramvalue_t val) { v.cutoffHz = *val; });
        // ... register remaining parameters and metadata.layoutFlat sections ...
    }

  private:
    static inline std::vector<ParamHandler> paramHandlers;
    float phase = 0.0f;
    float noteFreq = 440.0f;
    float velGain = 1.0f;
    float envLevel = 0.0f;
    float cutoffHz = 2000.0f;
    float attackCoeff = 0.01f;
    float releaseCoeff = 0.002f;
    bool gate = false;
};

} // namespace polysynth

PSYN_REGISTER_CPP_VOICE(polysynth::MySynthVoice);
```

### Step 2: Compile ELF & Package `.psyn`

Compile with `arm-none-eabi-g++` targeting Cortex-M7 hard-float with relocations (`-mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16 -mword-relocations -Wl,--emit-relocs -T synth-plugin-sdk/tools/psyn.ld`), then pack with [`synth-plugin-sdk/tools/psyn_builder.py`](../synth-plugin-sdk/tools/psyn_builder.py):

```bash
# Quick test build of the bundled reference engine (CHIP.psyn -> synth-plugin-sdk/build/CHIP.psyn):
make -C synth-plugin-sdk

# Or bundle all factory + open-source .psyn engines into build/Synths/:
# (also downloadable ready-to-use from the `polyend-tracker-ce-synths` CI artifact / GitHub Releases)
make psyn

# Packaging a custom engine ELF:
python3 synth-plugin-sdk/tools/psyn_builder.py \
    --elf build/MySynth.elf \
    --out build/MYSYN.psyn \
    --code-name "MYSYN" \
    --display-name "My Synth" \
    --patch-folder "MYSYN" \
    --type-id-wave 0 \
    --type-id-play-tracker 0 \
    --default-voices 4 \
    --max-voices 8 \
    --output-channels 2 \
    --renders-aux 0 \
    --has-sample-slots 0
```

> [!TIP]
> Always set `--type-id-wave 0` and `--type-id-play-tracker 0` for new third-party engines. `PsynLoader` hashes `code_name` into a deterministic runtime `InstrumentTypeId` (`0x100+`) so community engines coexist safely without colliding with factory IDs.

### Step 3: Deploy onto SD Card
Copy the `.psyn` binary to `/Synths/MYSYN.psyn` and any preset patches to `/Patches/MYSYN/*.patch`. Reboot or re-enter the Synth menu — your new engine appears automatically in the Synth model selector (`SynthEditor`).

---

## 5. Sample-Based Synth Engines (`SampleSlot` & `SamplePool`)

Engines can play `.wav` samples or wavetables directly from the shared **`4 MB` `SamplePool`** while taking advantage of firmware sample caching, deduplication, and `GlobalFileBrowser` WAV browsing.

### 1. Declaring Sample Slots
* **In C++20 (`Patch::Metadata`)**: Populate `metadata.sampleSlots` and reference each slot index using `EditorElement::Type::kSampleSlot`:
  ```cpp
  metadata.sampleSlots.push_back({
      .displayName = "Sample A",
      .type = Patch::Metadata::SampleSlot::Type::kSample, // or kWaveTable
      .section = 0,
      .subsection = 0,
      .defaultSamplePath = "/SynthSamples/MYSYN/Init.wav",
  });

  metadata.layoutFlat.sections[0].elements.push_back({
      .type = Patch::Metadata::EditorElement::Type::kSampleSlot,
      .id = 0,
  });
  ```
* **In Pure C (`psyn_plugin.h`)**: Register via `host->meta_add_sample_slot(meta_builder, &slot_desc)` and add `PsynLayoutElement{.type = 2 /* SampleSlot */, .reserved = 0, .id = 0}`.
* **Packager flag**: Pass `--has-sample-slots 1` to `psyn_builder.py` so `PsynHeader::flags` includes `PSYN_FLAG_HAS_SAMPLE_SLOTS` (`0x0001`).

### 2. Reading Loaded Sample Audio Inside Your Voice
* **C++20**: Query `patch->getSample(slotId)` inside `setPatch()` or `renderVoiceAudio()`:
  ```cpp
  if (patch != nullptr)
  {
      const polysynth::Sample* s = patch->getSample(0);
      if (s != nullptr && s->ready && !s->data.empty())
      {
          // s->data is std::span<sample_store_t> (16-bit signed int16_t PCM)
          // s->format.channels (1 or 2), s->format.sampleRate, s->waveSize, s->basePitch
      }
  }
  ```
* **Pure C**: Call `PsynSampleData sd; if (host->patch_get_sample(host_patch, slot_id, &sd) && sd.ready)` to access `sd.data` (`const int16_t*`), `sd.size`, `sd.channels`, `sd.sample_rate`, and `sd.wave_size`.

### 3. Auto-Created Sample Directories & Path Fallback
* **On Tracker+ / Tracker Mini & Play / Play+:** Whenever `PsynLoader` discovers any built-in sample engine or any `.psyn` engine carrying `PSYN_FLAG_HAS_SAMPLE_SLOTS`, it automatically ensures `/SynthSamples/<CODE_NAME>/` exists on the SD card (`PsynLoader::ensurePluginSampleFolder`). Keeping synth samples under `/SynthSamples/` prevents cluttering users' `/Samples/` tracker instrument libraries.
* **On Polyend Synth:** Auto-created engine folders live under `/Samples/<CODE_NAME>/`.
* **Path Resolution:** [`core/synth/FileSystemAdapter.cpp`](../core/synth/FileSystemAdapter.cpp) resolves sample reads starting with `/SynthSamples/` and falls back cleanly across `/Samples/`, `/`, and `/Patches/`.

---

## 6. Direct SD Card & WAV File I/O (`PsynHostApi`)

For custom lookup tables, alternative bank formats, or plugin-managed assets beyond `SampleSlot`, `PsynHostApi` exposes four direct SD card and WAV helper callbacks (implemented in [`synth/source/synths/PsynLoader.cpp`](../synth/source/synths/PsynLoader.cpp)):

```c
int32_t (*read_file)(const char* path, void* buffer, uint32_t max_len);
int32_t (*write_file)(const char* path, const void* data, uint32_t len);
bool    (*load_wav_file)(const char* path,
                         int16_t** out_pcm_data,
                         uint32_t* out_sample_count,
                         uint8_t* out_channels,
                         uint32_t* out_sample_rate);
void    (*free_wav_file)(int16_t* pcm_data);
```

* `read_file` / `write_file`: Perform chunked FatFs transfers and return bytes transferred (or `< 0` / `-1` on error).
* `load_wav_file`: Decodes mono or stereo 8/16/24/32-bit PCM and 32-bit float `.wav` files into an `int16_t` buffer allocated from the `.psyn` SDRAM arena (`kArenaSize`, returning `false` on decode/allocation failure). Always release allocated buffers with `free_wav_file(*out_pcm_data)`.

> [!CAUTION]
> **Never Call SD / WAV I/O on the Audio Thread!**
> FatFs operations block for milliseconds and are not interrupt-safe. Only call `read_file()`, `write_file()`, `load_wav_file()`, or `free_wav_file()` during plugin initialization (`psyn_plugin_entry`), `voice_set_patch`, or voice destruction (`destroy_voice`) — **never** inside `renderVoiceAudio()` / `voice_render_audio()` or `renderPostProcessAudio()`.

---

## 7. Global File Browser Integration (Patches & Sample Slots)

Both synth **Patch Load / Save As** ([`SynthEditor.cpp`](../core/interface/instrumentEditor/SynthEditor.cpp)) and **Sample Slot `.wav` Pickers** ([`SynthPatchEditor.cpp`](../core/interface/instrumentEditor/SynthPatchEditor.cpp)) run through the unified [`GlobalFileBrowser`](global_file_browser.md):

* **Synth Patch Browser (`/Patches/<patch_folder>/*.patch`):**
  * Displays parsed `.patch` metadata in the Details pane via `onDescribeItem()` using immediate FatFs handle cleanup (`closeFile(fh)` with `deleteOnClose = true`) and a 1-entry `PatchWireInfo` cache so scrolling rapidly never leaks FatFs file handles.
  * Silences active synth voices (`silencePart(syntPartIdx)`) cleanly before loading the next patch so sustained tails never freeze while reading SD data.
  * Forwards Tracker pad events (`onPadEvent`) for polyphonic pad auditioning directly inside the browser.
* **Sample Slot WAV Browser (`kLoadWav`):**
  * Selecting any `SampleSlot` in `SynthPatchEditor` opens `GlobalFileBrowser` in `kLoadWav` mode (defaulting to `/SynthSamples/<CODE_NAME>/`), providing built-in `.wav` metadata, preview playback with adjustable Preview Vol, and `Left`-arrow jump-to-top navigation.

---

## 8. Patch Metadata, Curves, Macros & UI Layout Reference

### Parameter Definition (`Patch::Metadata::Param`)

Every parameter in `metadata.params` is configured via [`Patch::Metadata::Param`](../synth/include/PolySynth/Patch.h):

| Field | Type | Description |
| :--- | :--- | :--- |
| `displayName` | `std::string` | Label shown in the Patch Editor parameter list (keep `<= 18` chars for clean UI fitting). |
| `defaultValue` | `optional_paramvalue_t` | Initial value on `Init Patch` and when resetting via **Delete** button. |
| `minValue` | `__fp16` / `float` | Minimum parameter value (stored as IEEE 754 half-precision `__fp16` on ARM, max magnitude `65504`). |
| `maxValue` | `__fp16` / `float` | Maximum parameter value (keep within `[-65500, +65500]`). |
| `editCurve` | `Param::Curve` | Response curve for knob turning and macro modulation (`kLinear`, `kExponential`, `kExponentialSteep`, `kPower`). |
| `macroOperation` | `Param::MacroOperation` | How macro modifiers combine with the parameter (`kAdd` = additive in normalized curve space; `kMult` = multiplicative scaling). |
| `isEnum` | `bool` | Set `true` for discrete integer options (`0..N`). The UI steps by `1` on knob turns. |
| `midiCC` | `tiny::optional<uint8_t, 0xff>` | Optional MIDI CC number (see `sdk::MidiCCs`). |
| `invalidatesOtherMetadata` | `bool` | Set `true` if changing this parameter alters `metadata.maxVoices` or dynamic parameter names (triggers `createMetadata` refresh). |
| `valueFormatter` | `std::function<std::string(const Patch&, float)>` | Formats the parameter value into a human-readable string for the UI. |

### Patch Editor Section Icons (`layoutFlat.sections`)

The Tracker Patch Editor displays a **2x6 grid of tiles**: the **top 6 tiles** correspond to your engine's parameter sections (`metadata.layoutFlat.sections`, up to 6 sections), and the **bottom 6 tiles** correspond to the 6 user Macros. [`SynthPatchEditor.cpp`](../core/interface/instrumentEditor/SynthPatchEditor.cpp) selects tile icons by matching substrings in `EditorSection::name`:

| `EditorSection::name` Substring | Tile Short Label | Tile Icon | Typical Use |
| :--- | :--- | :--- | :--- |
| `"Oscillator"` | `Osc` | `kBitmapOsc` | Main oscillators, wavetables/samples, tuning |
| `"Modulation"` | `Mod` | `kBitmapMod` | LFOs, PWM, vibrato, pitch sweeps, mod matrix |
| `"Amplifier"` / `"Amp Envelope"` | `Amp` / `Amp Env` | `kBitmapEnv` | Amp ADSR envelope, velocity sensitivity |
| `"Envelope"` | `Env 1`, `Env 2`... | `kBitmapEnv` | Auxiliary/modulation envelopes |
| `"LFO"` | `LFO 1`, `LFO 2`... | `kBitmapLfo` | Dedicated LFO sections |
| `"Filter"` | `Filter` | `kBitmapFilter` | Lowpass/highpass/bandpass filter, resonance |
| `"Shaper"` | `Shp` | `kBitmapShaper` | Distortion, wavefolder, drive (`DIRT`) |
| `"Voice"` | `Voice` | `kBitmapGear` | Play mode (`Mono`/`Legato`/`Poly`), glide, gain |
| `"FX"` | `FX` | `kBitmapFX` | Built-in voice/synth effects |
| `"Mixer"` | `Mixer` | `kBitmapMixer` | Oscillator or drum mixer |
| `"Kick"`, `"Tom"`, `"Snare"`, `"Hihat"`, `"Cymbal"`, `"Percussion"` | Same as name | Drum icons | Drum synth voices (`PERC`) |

> [!TIP]
> Include one of the substrings above in each `EditorSection::name` and keep `metadata.layoutFlat.sections.size() <= 6` so every tile in the 2x6 Patch Editor grid receives an icon.

---

## 9. DSP Utilities & Value Formatters

[`synth/include/PolySynth/sdk/SynthVoiceSDK.h`](../synth/include/PolySynth/sdk/SynthVoiceSDK.h) provides header-only helpers in `namespace polysynth::sdk`:

### Constants & Math Helpers
* `sdk::kSampleRate` (`44100.0f`): Audio engine sample rate in Hz.
* `sdk::kSilenceGainDb` (`-90.0f`): Silence threshold in dB.
* `sdk::dbToAmplitude(float db)`: Converts dB to linear amplitude (`0.0f` at or below `-90 dB`).
* `sdk::amplitudeToDb(float amplitude)`: Converts linear amplitude to dB.
* `sdk::noteToFreq(note_t note, float detuneSemitones = 0.0f)`: Converts MIDI note + fractional semitone offset to Hz ($A_4 = 440\text{ Hz}$).
* `sdk::detuneMult(float detuneSemitones)`: Returns $2^{\Delta / 12}$ frequency multiplier.
* `sdk::saturateSoftClip(std::span<sample_t> buf, float gainDb)`: Applies pre-gain in dB followed by cubic soft-clipping ($y = x - x^3 / 6.72$, clamped to $[-1, 1]$).

### Built-In Value Formatters
* `sdk::createPercentFormatter(bool showPercentSign = true)`: Formats `[0.0, 1.0]` as `"0 %"` .. `"100 %"`.
* `sdk::createTimeFormatter()`: Formats seconds as `"0.05 s"` (`< 1 s`) or `"2.5 s"` (`>= 1 s`).
* `sdk::createFrequencyFormatter(float max)`: Formats Hz/kHz automatically (`"8.50 Hz"`, `"440 Hz"`, `"12.5 kHz"`).
* `sdk::createSemitoneFormatter()`: Formats integer semitones (`"-12 st"`, `"0 st"`, `"+7 st"`).
* `sdk::createCentsFormatter()`: Formats integer cents (`"-50 ct"`, `"0 ct"`, `"+25 ct"`).

---

## 10. Real-Time Audio, CPU & Memory Guidelines

1. **Stateless Parameter Handlers (`static_assert(std::is_empty_v<Fn>)`):**
   `sdk::ParamHelper::addParam` stores each handler as a 4-byte stateless function pointer (`void (*)(VoiceType&, Patch::optional_paramvalue_t)`). Handler lambdas must not capture variables (`[]` only), keeping `.bss` overhead near zero (`4 bytes * numParams`).
2. **Zero Allocations or Blocking Calls in Audio Loops:**
   Never allocate heap memory (`new`, `malloc`, `std::vector::push_back`, `std::string`) or perform SD card I/O inside `renderVoiceAudio()`, `renderPostProcessAudio()`, `onNoteOn()`, `onNoteOff()`, or `onParamChange()`.
3. **Budget Within the `.psyn` SDRAM Arena (`1,920 KB` Total):**
   Keep `.text` + `.rodata` + `.data` + `.bss` + `slot_alloc` usage compact so multiple engines remain co-resident in `PsynLoader::kArenaSize` (`1,920 KB`) without triggering frequent LRU evictions when switching synth parts (`SYN 1`–`SYN 3`). Keep `__fp16` parameter ranges within `[-65500, +65500]`.
4. **Hardware Single-Precision FPU (`VFPv5`):**
   Use single-precision `float` (`0.0f`, `std::sinf`, `std::powf`, `std::fabsf`), never `double`. Hoist per-block filter/envelope/frequency computations outside the `128`-sample audio loop, and band-limit sharp waveform discontinuities (e.g. PolyBLEP).

---

## 11. Reference Implementations (`chip.cpp` & `simple_sine.c`)

Study both reference plugins included in `synth-plugin-sdk/`:

* **C++20 Production Example (`CHIP`)**: [`synth-plugin-sdk/engines/chip.cpp`](../synth-plugin-sdk/engines/chip.cpp)
  * Uses `sdk::CustomSynthVoice<ChiptuneSynthVoice, ChipParamId>` and exports via `PSYN_REGISTER_CPP_VOICE(polysynth::ChiptuneSynthVoice)`.
  * Implements 8 synthesis models (`2A03 Pulse`, `SID Saw/Tri`, `Noise / LFSR`, `FM Bell`, `Super Square`, `Zap / Laser`, `Arp Chord`, `Chip Drum`), dynamic voice count switching (`Mono` / `Legato` / `Poly` via `invalidatesOtherMetadata = true`), 5 Patch Editor sections, and 6 default macros.
* **Pure C ABI Example (`Simple Sine`)**: [`synth-plugin-sdk/examples/simple_sine.c`](../synth-plugin-sdk/examples/simple_sine.c)
  * Demonstrates raw `PsynPluginApi`, `PsynHostApi` callbacks, and `psyn_plugin_entry()` without C++ runtime dependencies.
