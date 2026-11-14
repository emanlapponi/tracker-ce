# Firmware Architecture, Hardware Limits & Developer Gotchas

When extending the **Polyend Tracker Community Edition (CE)** firmware, adding new synthesizer engines, or writing Sample Editor effects, keep the following hardware constraints and non-obvious architectural details in mind.

---

## Table of Contents

1. [Memory Budget & Free Headroom](#1-memory-budget--free-headroom)
2. [Shared `768 KB` Scratch Buffer Layout (`externalRamScratchBuff`)](#2-shared-768-kb-scratch-buffer-layout-externalramscratchbuff)
3. [Copy-on-Write `/Workspace/instruments/`, Incremental Project Saver & Hybrid SD Streaming](#3-copy-on-write-workspaceinstruments-incremental-project-saver--hybrid-sd-streaming)
4. [Synthesizer Engine Architecture & Guidelines (`.psyn` Plugins)](#4-synthesizer-engine-architecture--guidelines-psyn-plugins)
5. [Sample Editor & `.ptfx` Plugin Constraints](#5-sample-editor--ptfx-plugin-constraints)
6. [UI, FT812 Display, Config & Bootloader Gotchas](#6-ui-ft812-display-config--bootloader-gotchas)
7. [Conditional Compilation: Targeting Specific Platforms or Devices](#7-conditional-compilation-targeting-specific-platforms-or-devices)
8. [Development Console & Logging over USB (Tracker+ & Tracker Mini)](#8-development-console--logging-over-usb-tracker--tracker-mini)
9. [Global File Browser (`GlobalFileBrowser`)](#9-global-file-browser-globalfilebrowser)

---

## 1. Memory Budget & Free Headroom

| Platform & Memory Region | Total Capacity | Current Usage | Immediately Free | Theoretically Reclaimable | What Lives Here & Developer Notes |
| :--- | ---: | ---: | ---: | ---: | :--- |
| **Original (MK66) - `FLASH`** | `1,024 KB` | `~871 KB` (`85.1%`) | **`~153 KB`** | — | Code, `.rodata`, `.audio_ram_functions`, compressed fonts & bitmaps. |
| **Original (MK66) - `RAM` (Internal SRAM)** | `256 KB` | `~128 KB` (`50.1%`) | **`~128 KB`** | — | Stack, heap, `.data`, and real-time `.bss`. Cold UI/FileManager singletons live in `EXTERNAL_RAM`. |
| **Original (MK66) - `EXTERNAL_RAM` (SDRAM)** | `16,384 KB` | `~15,222 KB` (`92.9%`) | **`~1,162 KB` (`1.13 MB`)** | **`+3.5–4.0 MB`** *(up to `~5.1 MB` total)* | `11.25 MB` `SAMPLE_MEMORY_SIZE` (`7.5 MB` mono sample pool + `3.75 MB` FX edit buffer) + `1.5 MB` delay + `768 KB` shared `externalRamScratchBuff` + `VoiceStreamManager` ring buffers + cold UI singletons. |
| **Plus / Mini (IMX) - `QSPI_FLASH`** | `~16,200 KB` | `~1,620–1,650 KB` (`10.0–10.2%`) | **`~14,570 KB`** | — | XIP Flash; plenty of room for code, lookup tables, and assets. |
| **Plus / Mini (IMX) - `SRAM_ITC` (`RAM2`)** | `128 KB` | `~105.4 KB` (`82.4%`) | **`~22.5 KB`** | — | Zero-wait-state instruction TCM (`.ramfunc.$RAM2` / `.audio_ram_functions` hot DSP loops). |
| **Plus / Mini (IMX) - `SRAM_DTC` (`RAM`)** | `352 KB` | `~330–331 KB` (`93.9–94.0%`) | **`~21–22 KB`** | — | Main stack, heap, `.data`, and real-time `.bss`. Keep large or non-realtime buffers in `EXTERNAL_RAM`. |
| **Plus / Mini (IMX) - `SRAM_OC` (`RAM3`)** | `32 KB` | `~22.7 KB` (`70.8%`) | **`~9.3 KB`** | — | Non-cacheable OCRAM used for USB/SDMA buffers and `audioRecBuff`. |
| **Plus / Mini (IMX) - `EXTERNAL_RAM` (`RAM4`)** | `65,536 KB` | `~65,528 KB` (`99.99%`) | **`~8 KB`** | **`+23.0 MB`** *(up to `~23.0 MB` total)* | `~54 MB` `SAMPLE_MEMORY_SIZE` (`~39 MB` stereo sample pool + `15 MB` FX edit buffer) + `6 MB` `PSYN_RAM_POOL_SIZE` (`1.92 MB` `.psyn` arena + `4 MB` synth `SamplePool`) + `1.5 MB` delay + `768 KB` `externalRamScratchBuff` + `VoiceStreamManager` ring buffers + `64 KB` `g_wtEnvelopeBank`. |

### Theoretically Available `EXTERNAL_RAM` via `SAMPLE_MEMORY_SIZE` (Hybrid SD Streaming)

By default, `SAMPLE_MEMORY_SIZE` in [`core/source/mtStructs.h`](../core/source/mtStructs.h) maximizes sample RAM after accounting for `PSYN_RAM_POOL_SIZE` on IMX:
* **Original Tracker (`MK66`)**: `SAMPLE_MEMORY_SIZE = 45 * 256 * 1024` (`11.25 MB`, comprising a `7.5 MB` resident mono sample pool plus `3.75 MB` `EDIT_BUFF_SIZE` reserved at the top for offline Sample Editor effects).
* **Tracker+ & Tracker Mini (`IMX`)**: `PSYN_RAM_POOL_SIZE = 6 MB` (`6 * 1024 * 1024`) is dedicated to loadable `.psyn` synth engines (`1.92 MB` executable arena) and the synth `SamplePool` (`4 MB`). `SAMPLE_MEMORY_SIZE = ((60 * 1024 - 64 - 96) * 1024) - PSYN_RAM_POOL_SIZE` (`~54 MB`, comprising a `~39 MB` resident stereo sample pool plus `15 MB` `2 * SAMPLE_CHANNELS * EDIT_BUFF_SIZE` reserved at the top for offline Sample Editor effects).

However, because **Hybrid SD Card Sample Streaming** ([`core/AudioEngine/VoiceStreamManager.cpp`](../core/AudioEngine/VoiceStreamManager.cpp)) streams samples `>= 256 KB` (`kStreamThresholdBytes`) directly from the SD card while only allocating a **`32,768`-frame (`64 KB` per channel) pre-cache** in `externalRamSampleMemory`:
* **Future developers can chip away at `SAMPLE_MEMORY_SIZE` whenever a new feature requires additional `EXTERNAL_RAM`**, simply by lowering `SAMPLE_MEMORY_SIZE` in [`core/source/mtStructs.h`](../core/source/mtStructs.h):
  * On **Tracker+ / Tracker Mini (`IMX`)**, reducing `SAMPLE_MEMORY_SIZE` from `~54 MB` down to `31 MB` (`31 * 1024 * 1024`, keeping the `15 MB` FX edit buffer + a `16 MB` resident sample pool for short samples, pre-caches, and Wavetable/Granular instruments) immediately frees **`~23 MB` of `EXTERNAL_RAM`**.
  * On **Original Tracker (`MK66`)**, reducing `SAMPLE_MEMORY_SIZE` from `11.25 MB` (`45 * 256 * 1024`) down to `7.75 MB` (`31 * 256 * 1024`, keeping the `3.75 MB` FX edit buffer + a `4.0 MB` resident sample pool) frees **`3.5 MB` of additional `EXTERNAL_RAM`** on top of the **`1.13 MB`** already unassigned (**`~4.63 MB` total free `EXTERNAL_RAM`**).
* **Important when adjusting `SAMPLE_MEMORY_SIZE`**: Always keep `SAMPLE_MEMORY_SIZE > 2 * SAMPLE_CHANNELS * EDIT_BUFF_SIZE` (`> 3.75 MB` on `MK66`, `> 15 MB` on `IMX`) because `EDIT_BUFF_OFFSET` (`externalRamEffectsBuff`) is carved out of the top of `externalRamSampleMemory`.

> **Important (`SRAM_DTC` / Internal `RAM` vs `EXTERNAL_RAM`)**:
> * Default `.bss` lands in **`RAM`** on Original Tracker (`MK66`) and **`SRAM_DTC`** on Tracker+ / Tracker Mini (`IMX`), while `__NOINIT(EXTERNAL_RAM)` lands in **`EXTERNAL_RAM`** (`.noinit_RAM4`).
> * Because `.noinit_RAM4` is `NOLOAD` (not zeroed by startup code before C++ static constructors run), any C++ singleton or state struct moved to `__NOINIT(EXTERNAL_RAM)` must be explicitly zeroed (`memset`) or Placement-`new` constructed in `TrackerImpl::init()` before use.
> * Modal or mutually-exclusive features that only run when SD audio preview, offline stem export, and `.ptfx` offline execution are idle can share **`externalRamScratchBuff`** (`768 KB`) rather than declaring a new static buffer in `.bss` or `EXTERNAL_RAM`.

---

## 2. Shared `768 KB` Scratch Buffer Layout (`externalRamScratchBuff`)

Defined in [`core/source/Tracker.cpp`](../core/source/Tracker.cpp) (`SHARED_SCRATCH_MEMORY_SIZE = 768 * 1024` in [`core/source/mtStructs.h`](../core/source/mtStructs.h)), the `768 KB` `externalRamScratchBuff` array in `EXTERNAL_RAM` (`.scratch_memory`) is shared cleanly across subsystems (`PtfxLoader` / `PtfxManager`, `mtAudioExport` `renderBuffer`, `mtAudioEngine` `sdStream`, and `GlobalFileBrowser`).

When `.ptfx` Modular Sample Editor plugins and [`GlobalFileBrowser`](global_file_browser.md) run concurrently, the `768 KB` region is partitioned into four non-overlapping ranges:

```text
Offset 0 KB               128 KB                  512 KB                 744 KB          768 KB
├─────────────────────────┼───────────────────────┼──────────────────────┼───────────────┤
│ .ptfx Code              │ .ptfx Working Scratch │ .ptfx Secondary WAV  │ Global File   │
│ (128KB IMX / 32KB MK66) │ (384 KB)              │ Buffer (232 KB)      │ Browser (24KB)│
└─────────────────────────┴───────────────────────┴──────────────────────┴───────────────┘
```

* `kPtfxScratchTopOffset` (`512 KB` in [`core/effector/ptfx_loader.h`](../core/effector/ptfx_loader.h)) caps `.ptfx` plugin `scratch_mem` at `384 KB` (`[128 KB .. 512 KB]`), leaving `[512 KB .. 744 KB]` (`232 KB`) for pre-decoded secondary WAV files (`PTFX_PARAM_FILE`) and `[744 KB .. 768 KB]` (`24 KB`) reserved for `cGlobalFileBrowser`. Opening `GlobalFileBrowser` from inside the Sample Editor or an effect parameter never overwrites active plugin code, working scratch RAM, or loaded secondary audio, while `g_wtEnvelopeBank` (`64 KB`) resides in its own dedicated `EXTERNAL_RAM` array in [`core/source/mtWavetableEnvelopes.cpp`](../core/source/mtWavetableEnvelopes.cpp).

---

## 3. Copy-on-Write `/Workspace/instruments/`, Incremental Project Saver & Hybrid SD Streaming

To avoid copying 13–45 MB of PCM sample payloads from `/Projects/<Path>/<Name>/instruments/*.pti` to `/Workspace/instruments/instrument_%02d.pti` on every project open and save, the `FileManager` uses a **Header-Only Copy-on-Write (CoW)** model paired with **Hybrid SD Card Sample Streaming**:

1. **Project Open (`ProjectOpen::copyInstruments`)**:
   * Copies only the **392-byte `strInstrumentFile` header** (`cFileTransfer::copyInstrumentHeader`) from `/Projects/<Path>/<Name>/instruments/*.pti` into `/Workspace/instruments/instrument_%02d.pti`.
2. **Sample Streaming & Reload (`WorkspaceProjectLoad` & `SampleMemoryReload`)**:
   * In `cFileManager::startNewInstrumentSampleLoad()` and `cFileManager::loadSampleData()`, if a `/Workspace/instruments/instrument_%02d.pti` file has `fileSize <= sizeof(strInstrumentFile)` (header-only), the loader resolves its backing `.pti` file in `/Projects/<Path>/<Name>/instruments/` via `cFileManager::resolveProjectInstrumentPtiPath()` and loads or streams the PCM sample data directly from the project folder (respecting `cFileManager::getPtiFileDataOffset()` for both current 392-byte headers and legacy firmware `< v8` smaller headers).
3. **Copy-on-Write Promotion (`WorkspaceProjectSave` & Sample Edits)**:
   * Parameter tweaks (`AutoSave` / `WorkspaceProjectSave`) only rewrite the 392-byte header in `/Workspace/instruments/instrument_%02d.pti` when `!isInstrumentSampleChanged(idx)`, preserving its header-only status.
   * Whenever an instrument's audio is newly imported, recorded, rendered, copied (`InstrumentCopy`), or modified in the Sample Editor (`cFileManager::saveSampleDataToWorkspace`), a **full `.pti` file** (header + PCM audio) is written to `/Workspace/instruments/instrument_%02d.pti`.
4. **Incremental `Save Project` vs. Full `Save As` (`ProjectSave::copyInstruments`)**:
   * **Same-Project Save (`isSavingToSameProject == true`)**: Deletes deactivated slots, copies full `.pti` files from `/Workspace/` only for slots whose audio was modified, and for unmodified header-only slots updates the 392-byte `strInstrumentFile` header in-place (skipping the write entirely if all 392 bytes are identical) and renames the file in-place if `sample.file_name` changed.
   * **`Save As` to a New Project (`isSavingToSameProject == false`)**: Copies full `.pti` files from `/Workspace/` where present, and writes the updated `strInstrumentFile` header plus the raw PCM sample payload directly from `externalRamSampleMemory` (or streams it in chunks from the backing `.pti` file for SD-streamed instruments) for header-only slots so the new project folder is completely self-contained.
5. **Hybrid SD Card Sample Streaming Architecture (`VoiceStreamManager` & `isPlayModeStreamable`)**:
   * **Threshold & SDRAM Pre-Cache**: Instruments whose PCM payload is `>= 256 KB` (`kStreamThresholdBytes` in [`core/source/mtStructs.h`](../core/source/mtStructs.h)) and whose play mode satisfies `isPlayModeStreamable(playMode)` allocate only **`kStreamPreCacheFrames = 32768` frames (`~743 ms` at `44.1 kHz`)** per channel (`64 KB` mono / `128 KB` stereo) in `externalRamSampleMemory`.
   * **Zero-Latency Trigger & Background Ring Buffers**: When a note is triggered, the audio ISR (`AudioPlayMemory::update()` in [`core/libs/Audio/play_memory.cpp`](../core/libs/Audio/play_memory.cpp)) immediately plays from the SDRAM pre-cache while `VoiceStreamManager::update()` ([`core/AudioEngine/VoiceStreamManager.cpp`](../core/AudioEngine/VoiceStreamManager.cpp)) refills the active voice's **`16,384`-frame (`kRingFrames`) ring buffer** in **`4,096`-frame (`kChunkFrames`) chunks** on the main thread using FatFs FastSeek cluster link maps (`CLMT`) and direct LBA sector reads.
   * **Loop Wrap & Direction Support**: Supports forward, backward, and ping-pong looping (`VoiceStreamManager::prepareLoopWrap` pre-fetches the loop boundary before the playhead reaches the wrap point) as well as `R` (Reverse) and `P` (Position) pattern step FX.
   * **Platform Differences (`PLATFORM_IMX` vs. `MK66`)**:
     * **Tracker+ & Tracker Mini (`PLATFORM_IMX`)**: `isPlayModeStreamable()` returns `true` for **`One-Shot`**, **`Forward Loop`**, **`Backward Loop`**, **`Ping-Pong Loop`**, **`Slice`**, and **`Beat Slice`**, and `false` only for `Wavetable` and `Granular`. For `Slice` and `Beat Slice` instruments, the `32,768`-frame SDRAM pre-cache is partitioned into a `16,384`-frame linear attack region (`[0 .. 16383]`) plus per-slice SDRAM pre-cache windows across the upper `16,384` frames (`VoiceStreamManager::refreshInstrumentSliceCache` / `refreshInstrumentSingleSliceCache`), allowing any slice to start immediately from SDRAM while the `600 MHz` Cortex-M7 refills the voice ring buffer.
     * **Original Tracker (`MK66`)**: `isPlayModeStreamable()` returns `true` for **`One-Shot`**, **`Forward Loop`**, **`Backward Loop`**, and **`Ping-Pong Loop`**, and `false` for **`Slice`**, **`Beat Slice`**, **`Wavetable`**, and **`Granular`**. Because partitioning `16,384` frames across `10–48` slices yields smaller per-slice pre-cache windows (`~7.6–37 ms`), and the `180 MHz` Cortex-M4 spends `~20–35 ms` per step on FT812 SPI pattern redraws and Song Mode pattern reads, keeping `Slice` and `Beat Slice` instruments 100% resident in SDRAM on `MK66` guarantees dropout-free multi-track slice playback while non-sliced instruments enjoy the full `743 ms` linear pre-cache.
   * **Automatic Promotion & Demotion (`Sample Editor` & `Play Mode` Switching)**:
     * Entering the Sample Editor (`cSampleEditor`) or switching an instrument to a non-streamable play mode (`!isPlayModeStreamable(newPlayMode)` in `fSamplePlay.cpp`) calls `cFileManager::promoteStreamedInstrumentToRam(idx)`, loading the full PCM payload into `externalRamSampleMemory`.
     * Applying an offline Sample Editor effect or saving sample edits (`cFileManager::saveSampleDataToWorkspace`) writes the updated full `.pti` to `/Workspace/instruments/instrument_%02d.pti`, recomputes the `600`-point waveform peak cache (`WaveformPeakPoint`), and calls `cFileManager::demoteRamInstrumentToStream(idx)` to return the instrument to hybrid SD streaming.
6. **Binary `.pti` Header Compatibility (`sizeof(strInstrument) == 372`) & Shared `envWtPos` / `envGranLen` Automation Slot**:
   * Because `strInstrumentFile` (`392` bytes) is serialized directly into `.pti` files and Copy-on-Write `/Workspace/instruments/` headers, `sizeof(strInstrument)` in [`core/source/mtStructs.h`](../core/source/mtStructs.h) is enforced at `372` bytes via `static_assert(sizeof(strInstrument) == 372)`.
   * Instrument structure versions `1`–`7` may use the legacy `368`-byte metadata layout; versions `8`–`9` use `372` bytes. The 4-byte CRC field immediately follows the serialized metadata (though metadata-only legacy files may omit it). Loaders validate the header magic, version bounds, and metadata/file sizes before version translation, rejecting truncated, oversized, malformed, or newer-version metadata.
   * **Wavetable (`playModeWavetable`) and Granular (`playModeGranular`) are mutually exclusive playback modes** on an instrument. Therefore, **Granular Length** automation (`envGranLen`) shares envelope/LFO hardware storage slot `3` (`envWtPos = 3`, `envGranLen = envWtPos`) with **Wavetable Position**:
     * In the UI (`Instrument Automation 2/2`), the 7 destinations are indexed by `enAutomationDest` (`autoDestVolume`, `autoDestPanning`, `autoDestCutoff`, `autoDestWtPos`, `autoDestGranPos`, `autoDestGranLen`, `autoDestFinetune`, `autoDestCount = 7`) and mapped to the 6 underlying `envelope[6]` / `lfo[6]` storage slots via `automationDestToEnvSlot(uint8_t destIdx)`.
     * Enable state in `envelope[3].enable` uses separate bitmask flags (`kEnvEnableWtPosBit = 0x01` for `Wavetable Position`, `kEnvEnableGranLenBit = 0x02` for `Granular Length`) via `strInstrument::isWtPosEnvEnabled()`, `strInstrument::isGranLenEnvEnabled()`, `strInstrument::isEnvEnabled(envIdx)`, `strInstrument::isAutomationDestEnabled(destIdx)`, and `strInstrument::setAutomationDestEnabled(destIdx, enabled)`.
     * Because all legacy `.pti` files only ever wrote `0` or `1` (`0x01`) to `envelope[3].enable`, existing `.pti` files load with `isGranLenEnvEnabled() == false` (`Off`) while preserving legacy Wavetable Position automation 100%. Always use the `envGranLen` alias and `isGranLenEnvEnabled()` helper in granular code paths rather than raw `envWtPos` or `.enable` checks.

---

## 4. Synthesizer Engine Architecture & Guidelines (`.psyn` Plugins)

Full details in [`docs/custom_synth_sdk.md`](custom_synth_sdk.md) and [`synth-plugin-sdk/README.md`](../synth-plugin-sdk/README.md).

* **Loadable SD-Card `.psyn` Architecture (`PsynLoader`)**:
  * All synth engines (built-in and community) are distributed as standalone ARMv7E-M Thumb-2 relocatable `.psyn` plugins in `/Synths/*.psyn` on the microSD card and loaded dynamically by [`PsynLoader`](../synth/source/synths/PsynLoader.cpp) (`PSYN_RAM_POOL_SIZE = 6 MB` in [`core/source/mtStructs.h`](../core/source/mtStructs.h)).
  * `PsynLoader` reserves **`1,920 KB` (`kArenaSize`)** for relocated `.psyn` code/heap (`kMaxPreloadBytes = 1,024 KB` preloaded at boot, lazy load on demand, LRU eviction of inactive engines when full) alongside a shared **`4 MB` `SamplePool`** for sample-based engines (`WAVS`, `GRAIN`, `DWA`, and third-party `PSYN_FLAG_HAS_SAMPLE_SLOTS` plugins).
  * **Zero firmware recompilation or bitmap edits required:** `SynthEditor` populates the Synth model list dynamically from `synth->getSynthTypes()`, and third-party engines receive deterministic non-colliding IDs (`0x100+`) when packed with `--type-id-wave 0 --type-id-play-tracker 0`.
* **Auto-Created Sample Folders & Read Fallback Chain**:
  * On boot, `PsynLoader` creates `/SynthSamples/<CODE_NAME>/` for every sample-based engine (avoiding clutter in user `/Samples/` folders), while [`core/synth/FileSystemAdapter.cpp`](../core/synth/FileSystemAdapter.cpp) resolves sample reads across `/SynthSamples/`, `/Samples/`, `/`, and `/Patches/`.
* **Keep Voice State & Arena Usage Compact (`CustomSynthVoice` / `ParamHelper`)**:
  * Inherit from `polysynth::sdk::CustomSynthVoice<VoiceClass, ParamIdEnum>` ([`synth/include/PolySynth/sdk/SynthVoiceSDK.h`](../synth/include/PolySynth/sdk/SynthVoiceSDK.h)) bridged via `PSYN_REGISTER_CPP_VOICE` ([`synth-plugin-sdk/include/psyn/psyn_cpp_bridge.h`](../synth-plugin-sdk/include/psyn/psyn_cpp_bridge.h)) or write a Pure C `psyn_plugin_entry` plugin ([`synth-plugin-sdk/include/psyn/psyn_plugin.h`](../synth-plugin-sdk/include/psyn/psyn_plugin.h)).
  * Never perform blocking `PsynHostApi` SD/WAV calls (`read_file`, `write_file`, `load_wav_file`, `free_wav_file`) or heap allocations inside real-time audio callbacks (`renderVoiceAudio` / `voice_render_audio`).

---

## 5. Sample Editor & `.ptfx` Plugin Constraints

Full details in [`docs/modular_effects_system.md`](modular_effects_system.md).

* **Plugin, Parameter, File & Preset Limits**:
  * Up to **128 `.ptfx` plugins** (`kMaxModularPlugins = 128` in [`core/effector/ptfx_manager.h`](../core/effector/ptfx_manager.h)) are indexed from `/Effects/` alongside the **17 built-in** Sample Editor effects (`kBuiltInEffectsCount = 17`, `145` total list items in `s_effectDisplayNames`).
  * Each `.ptfx` plugin supports up to **12 parameters** (`PtfxHeader::params[12]` in [`core/effector/tracker_plugin.h`](../core/effector/tracker_plugin.h) and [`editor-fx-sdk/tracker_plugin.h`](../editor-fx-sdk/tracker_plugin.h)), including continuous float, stepped integer, pipe-separated string lists, and SD file pickers/savers (`PTFX_PARAM_FILE` / `PTFX_PARAM_SAVE_FILE`).
  * Any `.ptfx` plugin with `>= 1` parameter automatically gains a **Presets** page via the `More` screen button (`Load` / `Save` `.ptp` presets in `/Effects/Presets/<SanitizedPluginName>/` via [`GlobalFileBrowser`](global_file_browser.md), plus `Reset`).
* **`.ptfx` Code, Scratch & Secondary WAV Regions (`externalRamScratchBuff`)**:
  * Both platforms execute `.ptfx` plugin code from the base of `externalRamScratchBuff` (`[0 .. 32 KB]` on Original Tracker `MK66` with the Kinetis System MPU disabled via `MPU_CESR = 0`, and `[0 .. 128 KB]` on Tracker+ / Tracker Mini `IMX` after cleaning L1 D-Cache and invalidating I-Cache in `PtfxLoader::syncInstructionCache()`).
  * Working scratch memory (`scratch_mem`, up to **`384 KB`**) occupies `[128 KB .. 512 KB]`, leaving **`232 KB`** at `[512 KB .. 744 KB]` for pre-decoded secondary `.wav` inputs (`get_secondary_sample()`, up to ~`118k` `int16_t` samples) and **`24 KB`** at `[744 KB .. 768 KB]` for `GlobalFileBrowser`. Plugins can also read/write non-WAV assets via `host->read_file()` and `host->write_file()`.
  * Plugins with `code_size + bss_size > 32 KB` (or requiring 600 MHz Cortex-M7 speeds) should declare `PTFX_FLAG_IMX_ONLY`.
* **SD Card DMA Alignment Pitfall on IMX (`FatFs` + `USDHC`)**:
  * On IMX (`platforms/imx/`), `FatFs` (`f_read`) bypasses its internal aligned DTCM sector buffer whenever a read request is `>= 512` bytes (`cc = btr / 512 > 0`) and passes the caller's destination pointer directly to the USDHC DMA controller.
  * If the destination buffer is not 4-byte aligned or lies in a non-DMA-accessible TCM region, `f_read` fails with `FR_DISK_ERR`. Always use 4-byte aligned buffers in `SRAM_DTC`/`EXTERNAL_RAM`, or read in `<= 256`-byte chunks to force `FatFs` through its internal bounce buffer (`hostReadFile` and `hostWriteFile` use `256`-byte chunking automatically).

---

## 6. UI, FT812 Display, Config & Bootloader Gotchas

* **Do Not Shift `Interface::modules[]` Indices ([`core/interface/core/interface.cpp`](../core/interface/core/interface.cpp))**:
  * UI navigation references `mtInterface.modules[]` by hardcoded index (`modules[9]` = `performanceMode`, `modules[10]` = `imageViewer`, `modules[12]` = `masterParams`). Slot `modules[11]` (formerly the NES emulator `gameModule`) is intentionally kept as `nullptr` so higher indices never shift.
* **FT812 Font Buffer Cap (`22,396` Bytes) & Embedded Hardware Icons**:
  * `kMaxFontBufferSize` in [`core/libs/NewDisplay/display.cpp`](../core/libs/NewDisplay/display.cpp) is `22,396` bytes (`decompressedFontsCommonBuffer`). Every individual font slot (both built-in RLE-compressed fonts and `.ptfont` slots on SD) must decompress to `<= 22,396` bytes.
  * **Font Slot 4 (`17px Bold`, FT812 Handle 4)** embeds 8 custom hardware icons at ASCII `0x18..0x1F` (`24..31`: `Pause`, `Stop`, `Play`, `Rec`, `Folder`, `Up-dir`, `Loop`, `Arrow`). If these are missing from a font, transport bar and file browser icons will appear blank.
  * **Font Slot 5 (`12px Regular`, FT812 Handle 5)** embeds Polish diacritics at ASCII `0x11..0x1F` (`17..31`).
* **Bootloader `.ptf` Filename Filter & 31-Character Limit**:
  * The firmware updater (`enListFileFilter_firmware` in [`platforms/mk66/3rdparty/SD/SD.cpp`](../platforms/mk66/3rdparty/SD/SD.cpp) and [`platforms/imx/sdmmc/sd/SD.cpp`](../platforms/imx/sdmmc/sd/SD.cpp)) only lists files in `/Firmware/` that end with `.ptf`, have total length `>= 15`, and start with:
    * `PolyendTrac` (Original Tracker)
    * `TrackerPlus` (Tracker+)
    * `TrackerMini` (Tracker Mini)
  * **31-Character Maximum on Original Tracker**: `PlatformImpl::runBootloader()` ([`platforms/mk66/source/PlatformImpl.cpp`](../platforms/mk66/source/PlatformImpl.cpp) / [`platforms/imx/source/PlatformImpl.cpp`](../platforms/imx/source/PlatformImpl.cpp)) writes the selected `.ptf` filename to `/Firmware/_fwinfo`. The Original Tracker's on-chip bootloader reads `/Firmware/_fwinfo` into a 32-byte `char[32]` buffer. Filenames of 32+ characters overwrite the trailing `\0` byte and cause garbage symbols on the bootloader flashing screen. Always keep `.ptf` filenames `<= 31` characters.

---

## 7. Conditional Compilation: Targeting Specific Platforms or Devices

All three devices share the same `core/` codebase and are differentiated at compile time by preprocessor defines set in [`core/makefile`](../core/makefile), [`platforms/mk66/makefile`](../platforms/mk66/makefile), and [`platforms/imx/makefile`](../platforms/imx/makefile).

| Target Scope | Preprocessor Condition | Matching Hardware & Typical Use Case |
| :--- | :--- | :--- |
| **IMX Only** (Tracker+ & Mini) | `#ifdef PLATFORM_IMX` | `600 MHz` Cortex-M7, `64 MB` SDRAM, stereo samples (`SAMPLE_CHANNELS == 2`), 16 tracks. |
| **MK66 Only** (Original Tracker) | `#ifndef PLATFORM_IMX` | `180 MHz` Cortex-M4F, `16 MB` SDRAM, mono samples (`SAMPLE_CHANNELS == 1`), 16 tracks. |
| **Tracker Mini Only** | `#ifdef TRACKER_MINI` | Handheld controls (`interfaceButtonMiniMiddle`, D-pad, battery gauge, internal mic, no pad grid). |
| **Desktop Form Factor** (Original & Plus) | `#ifndef TRACKER_MINI` | Shared desktop hardware: 48 RGB silicone pads, rotary jog-wheel, FM radio, line/mic inputs. |
| **Tracker+ Exclusively** | `#if defined(PLATFORM_IMX) && !defined(TRACKER_MINI)` | Features requiring both the IMX CPU/RAM and the 48-pad desktop interface. |
| **Synth Audio Engine** | `#ifdef FEATURE_SYNTH` | Defined when `PLATFORM=IMX`; enables `PolySynth` and sets `kSynthParts = 3` in [`core/config/config.h`](../core/config/config.h). |
| **Synth UI Screens** | `#if GUI_HANDLE_SYNTH` | Defined as `1` (on `PLATFORM_IMX`) or `0` (on `MK66`) in [`core/interface/core/interfaceDefs.h`](../core/interface/core/interfaceDefs.h). |

```cpp
#if defined(PLATFORM_IMX)
    // Runs on Tracker+ and Tracker Mini (600 MHz Cortex-M7)
    #if defined(TRACKER_MINI)
        // Tracker Mini only
    #else
        // Tracker+ only
    #endif
#else
    // Runs exclusively on Original Tracker (180 MHz Kinetis MK66)
#endif
```

* **Gotchas with Preprocessor Guards**:
  1. **Never use `#ifdef __MK66FX1M0__` inside `core/`**: For legacy Teensy Audio library compatibility, [`core/makefile`](../core/makefile) defines `-D__MK66FX1M0__` in its base `DEFINES` for **both** `MK66` and `IMX` builds. Always use `#ifndef PLATFORM_IMX` (or `#if !defined(PLATFORM_IMX)`) when writing code exclusively for the Original Tracker.
  2. **Use `#if GUI_HANDLE_SYNTH` (not `#ifdef GUI_HANDLE_SYNTH`)**: In [`core/interface/core/interfaceDefs.h`](../core/interface/core/interfaceDefs.h), `GUI_HANDLE_SYNTH` is always defined (`1` on `PLATFORM_IMX`, `0` on `MK66`), so `#ifdef GUI_HANDLE_SYNTH` will mistakenly evaluate to true on the Original Tracker.
  3. **Custom Build Flags (`CUSTOM_DEFINES`)**: You can pass ad-hoc feature flags from the command line without editing makefiles via `make all CUSTOM_DEFINES="-DMY_EXPERIMENTAL_FEATURE"`.
  4. **Platform-Specific `.ptfx` Plugins**: `.ptfx` Sample Editor plugins are compiled once for all devices, but can restrict themselves to Tracker+ / Tracker Mini by setting `PTFX_FLAG_IMX_ONLY` in their `PtfxHeader::flags` ([`editor-fx-sdk/tracker_plugin.h`](../editor-fx-sdk/tracker_plugin.h)), or adapt dynamically at runtime by checking `channels` (`1` vs `2`) in `plugin_process_block()`.

---

## 8. Development Console & Logging over USB (Tracker+ & Tracker Mini)

Tracker+ and Tracker Mini (`PLATFORM_IMX`) can expose a USB serial port that carries the firmware's log output and a small command console. Together with the existing USB storage mode, it lets a host script copy a new `.ptf` to the SD card and start the firmware update without touching the device or removing the card.

It is **off unless you ask for it twice**: the code is only compiled in with a build flag, and the port is only enumerated when the SD card is marked for development. A normal build on a normal card is unaffected.

| Switch | Where | Effect |
| :--- | :--- | :--- |
| `ENABLE_DEV_CONSOLE` | Build flag | Compiles the console ([`platforms/imx/source/DevConsole.cpp`](../platforms/imx/source/DevConsole.cpp)) and its `4 KB` output buffer in `SRAM_DTC`. Without it the console is replaced by empty inline stubs. |
| `ENABLE_LOG` | Build flag | Compiles the `LOG()` / `LOGF()` call sites ([`core/source/log.h`](../core/source/log.h)) at the normal `-O2` optimization level. `DEBUG=1` also enables them, but additionally switches to `-Og` and changes boot behavior. |
| `/.develop` | Empty file in the SD card root | Develop mode (`interfaceGlobals.devMode`). With `ENABLE_DEV_CONSOLE`, the firmware adds the serial port to its USB configuration shortly after boot. Also shows the diagnostics on the Config screen. |

```bash
# Build a Tracker Mini image with the console and logging:
make mini CUSTOM_DEFINES="-DENABLE_DEV_CONSOLE -DENABLE_LOG"
```

The makefiles do not track compiler flags, so run `make clean` when switching `CUSTOM_DEFINES`.

### Logging

```cpp
#include "log.h"

LOG("sequencer stop");                              // appends a newline
LOGF("sequencer play from step %d\n", fromPos);     // printf-style, no newline added
```

* Output is queued in a ring buffer and sent from the main loop, so `LOG()` / `LOGF()` may be called from the audio interrupt. If the buffer overflows, the excess is dropped and reported as `@log dropped N bytes`.
* **Do not format floats (`%f`) from interrupt context**: newlib allocates from the heap to format them. Log scaled integers there instead.
* Messages logged before the host opens the port are kept (up to the buffer size) and delivered when it does, so the boot log is not lost.

### Console Protocol

The port is a plain CDC-ACM serial port (baud rate is irrelevant). The host sends one command per line, terminated by `\n` or `\r`. Every reply is a single line that **starts with `@`**; anything else on the port is log output. A reply always begins on a fresh line, even if a log message was mid-line.

| Command | Reply | Action |
| :--- | :--- | :--- |
| `ping` | `@tracker-dev <mini\|plus> <build id>` | Identifies the device and the running build (`BUILD_ID`, see `make version`). |
| `storage on` / `storage off` | `@ok storage on\|off`, or `@err storage is still on\|off` | Enters or leaves **Config > USB > Storage** mode. Refused while the interface is not in its normal run state (for example during project load). |
| `flash <name>` | `@ok flash pending`, then the device resets; or `@err ...` | Remounts the SD card, checks that `/Firmware/<name>` exists, then starts the same bootloader hand-off as **Config > Firmware > Firmware update**. `<name>` is a bare filename of at most `31` characters. |
| `reboot` | `@ok reboot pending` | Resets the MCU (and makes a Tracker Mini start again without the power button). |
| `help` | `@ok commands: ...` | Lists the commands. |

Anything else answers `@err unknown command: ...`; an over-long line answers `@err line too long`.

A scripted deployment is therefore:

1. `ping`, to find the device and note the running build.
2. `storage on`, then **close the serial port**. The device waits for the port to close before it re-enumerates with the storage class.
3. Copy the `.ptf` into `/Firmware/` on the mounted volume and eject it.
4. Reopen the port and send `flash <name>`. The device resets into the bootloader, which flashes the file.
5. When the port reappears, `ping` again and compare the build id.

> **Keep the port closed when using USB storage mode from the device's own menu.** While a host holds the serial port open (for example to follow the log), the device postpones the USB re-enumeration that entering or leaving storage mode needs.

> **Recovery**: a build that fails to boot has no console. Keep a known-good `.ptf` in `/Firmware/` and use the recessed reset button as described in the [README](../README.md#4-flashing-on-hardware--emergency-recovery).

### Fault Reports

With `ENABLE_DEV_CONSOLE`, a CPU fault is reported on the console instead of freezing the device silently. This covers the hard fault and the memory-management, bus and usage faults: an invalid pointer, a jump to a bad address, an undefined instruction, a `__builtin_trap()`.

```text
@fault pc=6008d28c lr=6008d283 xpsr=61000000 cfsr=00000082 hfsr=00000000 mmfar=f0000000 bfar=f0000000 sp=20057f28 handler=4 build=dbfa648
@fault stack 600920cb 600c385d 60093125 600976fd 60090d35 60090d3d 6008f885 6007dde9 6010e789 600203a9
```

| Field | Meaning |
| :--- | :--- |
| `pc` | The instruction that faulted. After a jump to a bad address this is that address, and `lr` is where the jump came from. |
| `lr` | The return address of the function that faulted. |
| `xpsr` | Its low 9 bits are the exception that was running when the fault came: `0` is the main loop, `16` and up are interrupts (`16 + IRQ number`). |
| `cfsr`, `hfsr`, `mmfar`, `bfar` | The Cortex-M fault status and fault address registers (`SCB->CFSR` and so on). |
| `sp` | The stack pointer at the fault. |
| `handler` | The exception that caught the fault: `3` hard fault, `4` memory management, `5` bus fault, `6` usage fault. |
| `build` | The build id of the running firmware, as in the `ping` reply. The addresses only mean something in the image of that build. |
| `@fault stack` | The words on the stack above the fault that look like return addresses, nearest first: a rough call chain. Addresses left over from earlier calls can appear in it. |

Turn the addresses into functions and source lines with the image the build left next to the `.ptf`:

```bash
arm-none-eabi-addr2line -f -C -e platforms/imx/build-MINI/TrackerMini.axf 0x6008d28c 0x6008d283
```

* The firmware does not continue after a fault. It keeps the USB serial port alive from inside the fault handler, repeats the report every few seconds, and answers every console command with it, so a host that opens the port later still gets it. **Power-cycle the device afterwards.**
* Log lines queued before the fault are delivered before the report.
* If the stack pointer itself is invalid, a shorter `@fault cfsr=... no usable stack` line is sent.
* The report needs the serial port, so it needs develop mode (`/.develop`). Without `ENABLE_DEV_CONSOLE` the fault handlers are the stock ones.
* A semihosting breakpoint (`BKPT 0xAB`) is still stepped over, as the stock hard fault handler does.

---

## 9. Global File Browser (`GlobalFileBrowser`)

Full developer reference and C++ examples live in [`docs/global_file_browser.md`](global_file_browser.md) ([`core/interface/fileBrowser/GlobalFileBrowser.h`](../core/interface/fileBrowser/GlobalFileBrowser.h), [`GlobalFileBrowser.cpp`](../core/interface/fileBrowser/GlobalFileBrowser.cpp)).

* **One Shared Module Across Subsystems:** Covers 6 modes (`kLoadWav`, `kSaveWav`, `kLoadGenericFile`, `kSaveGenericFile`, `kLoadFolderPackage`, `kSaveFolderPackage`), used by Project Open / Save As / MOD & IT import, Synth Patch Load / Save As & Sample Slots, Sample Editor `.ptfx` file parameters, and `.ptp` effect presets. The multi-selection **Sample Loader** intentionally remains separate.
* **Non-Overlapping `24 KB` Scratch Allocation (`[744 KB .. 768 KB]`):** Shares the top slice of `externalRamScratchBuff` above `.ptfx` code, working scratch, and secondary WAV buffers, preserving up to `10` LRU directory/cursor contexts (`kMaxSavedContexts = 10`) keyed by `contextKey`.
* **Navigation & FatFs Hygiene:** Supports `Left`-arrow jump-to-top in the Explorer pane, protected system folders (`kProtectedFolders`), inline metadata (`onDescribeItem`), and pad auditioning (`onPadEvent`). Callbacks inspecting files on cursor highlight must close FatFs handles immediately (`closeFile(fh)` with `deleteOnClose = true`) and silence preview voices (`silencePart` / `onPreviewStop`) before loading the next asset.
