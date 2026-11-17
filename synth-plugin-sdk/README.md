# Polyend Loadable Synth Plugin SDK (`.psyn`)

Build standalone relocatable SD-card synthesizer engines (`/Synths/<NAME>.psyn`) that run across all synth-capable Polyend Community Edition devices without recompiling firmware:

All `.psyn` binaries target **ARMv7E-M (`Cortex-M7`), Thumb-2, VFPv5 hard-float** (`-mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16 -mword-relocations`) and execute directly from the external SDRAM plugin arena ([`PsynLoader::kArenaSize`](../docs/memory_map.md#4-synthesizer-sdram-pool-psyn_ram_pool_size--imx) with automatic LRU eviction of inactive engines).

---

## SDK Layout

```text
synth-plugin-sdk/
├── Makefile                    # Builds reference CHIP.psyn using arm-none-eabi-g++
├── include/psyn/
│   ├── psyn_plugin.h           # Pure C plugin ABI (PsynHeader, PsynHostApi, PsynPluginApi)
│   └── psyn_cpp_bridge.h       # C++20 bridge (PSYN_REGISTER_CPP_VOICE) for PolySynth/sdk/
├── tools/
│   ├── psyn_builder.py         # Relocatable ELF -> .psyn binary packager
│   └── psyn.ld                 # Linker script linked at 0x00000000 with --emit-relocs
├── engines/
│   └── chip.cpp                # Reference C++20 engine (CHIP / ChiptuneSynthVoice)
└── examples/
    └── simple_sine.c           # Reference pure C engine (PSYN_PLUGIN_ENTRY example)
```

---

## Two Authoring Paths

1. **C++20 (`include/psyn/psyn_cpp_bridge.h`) — Recommended**
   Write a voice subclass using `polysynth::sdk::CustomSynthVoice` / `polysynth::InstrumentVoice` (see [`engines/chip.cpp`](engines/chip.cpp)) and export it in one macro:
   ```cpp
   PSYN_REGISTER_CPP_VOICE(MySynthVoice);
   ```
2. **Pure C (`include/psyn/psyn_plugin.h`)**
   Implement `const PsynPluginApi* psyn_plugin_entry(const PsynHostApi* host)` directly (see [`examples/simple_sine.c`](examples/simple_sine.c)) with zero C++ STL dependencies.

---

## Building & Packaging

From `synth-plugin-sdk/` (or from the repository root via `make -C synth-plugin-sdk`):

```bash
make -C synth-plugin-sdk
```

Output `.psyn` binaries are written to `build/` (for example `synth-plugin-sdk/build/CHIP.psyn`).

### Packaging with `tools/psyn_builder.py`
[`tools/psyn_builder.py`](tools/psyn_builder.py) converts an ARM Cortex-M7 ELF (`-Wl,--emit-relocs -T tools/psyn.ld`) into a `.psyn` image with a 128-byte `PsynHeader`:

```bash
python3 tools/psyn_builder.py \
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
    --has-sample-slots 0 \
    --supports-idle-sleep 1 \
    --has-voice-tick-idle 0
```

* Pass `--type-id-wave 0` and `--type-id-play-tracker 0` for community engines so the firmware assigns a deterministic runtime ID (`0x100+` from `PsynLoader`) that never collides with factory synth IDs.
* Pass `--has-sample-slots 1` whenever the engine declares sample or wavetable slots (`PSYN_FLAG_HAS_SAMPLE_SLOTS = 0x0001` in `PsynHeader::flags`).
* Pass `--supports-idle-sleep 1` (`PSYN_FLAG_SUPPORTS_IDLE_SLEEP = 0x0002`) when your voice accurately reports `isActive() == false` once silent; this enables zero-CPU idle sleep and adds the per-patch `Voice Sleep` (`On` / `Off`, default `On`) switch to the `Voice` layout.
* Pass `--has-voice-tick-idle 1` (`PSYN_FLAG_HAS_VOICE_TICK_IDLE = 0x0004`) if your voice also implements `InstrumentVoice::tickIdle()` / `voice_tick_idle()` to advance free-running oscillator/LFO/modulation state lightly while `Voice Sleep` is `Off` and `!isActive()`.

---

## SD Card Deployment & Sample-Based Engines

Copy the compiled `.psyn` file and any patches/samples onto your microSD card:

* **Plugin binary**: `/Synths/<CODE_NAME>.psyn`
* **Patches**: `/Patches/<patch_folder>/*.patch`
* **Sample folders (engines with `--has-sample-slots 1`)**:
  * **Tracker+ / Tracker Mini & Play / Play+**: Firmware automatically creates `/SynthSamples/<CODE_NAME>/` on boot so synth libraries stay separate from `/Samples/`. Sample reads also fall back across `/SynthSamples/`, `/Samples/`, `/`, and `/Patches/`.
  * **Polyend Synth**: Uses `/Samples/<CODE_NAME>/`.

### Sample Slots & Direct SD/WAV I/O
* **Sample Slots (shared [`SamplePool`](../docs/memory_map.md#4-synthesizer-sdram-pool-psyn_ram_pool_size--imx) across active engines)**:
  * *C++20*: Add entries to `metadata.sampleSlots` (`displayName`, `defaultSamplePath`, `type` Sample/WaveTable, `section`, `subsection`) and include `SampleSlot` layout elements; read PCM inside the voice via `patch->getSample(slotId)` (`const polysynth::Sample*`).
  * *Pure C*: Register slots with `host->meta_add_sample_slot(meta_builder, &desc)` + `PsynLayoutElement{.type = 2, .id = slot_id}` and inspect via `host->patch_get_sample(host_patch, slot_id, &sample_data)`.
  * On hardware, Tracker opens [`GlobalFileBrowser`](../docs/global_file_browser.md) (`kLoadWav`, with audio preview and `Left`-arrow jump-to-top) whenever the user picks a file for a sample slot.
* **Direct SD & WAV Host Callbacks (`PsynHostApi`)**:
  Plugins can also use `host->read_file()`, `host->write_file()`, `host->load_wav_file()` (decodes 8/16/24/32-bit PCM & 32-bit float WAVs into an arena buffer), and `host->free_wav_file()` during **non-audio-thread** setup/teardown. Never call FatFs/SD I/O from `renderVoiceAudio()` or `voice_render_audio()`.

For the full step-by-step tutorial, API tables, and firmware workflow details, see [`docs/custom_synth_sdk.md`](../docs/custom_synth_sdk.md).
