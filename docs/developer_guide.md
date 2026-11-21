# Firmware Architecture, Hardware Limits & Developer Gotchas

When extending the **Polyend Tracker Community Edition (CE)** firmware, adding new synthesizer engines, or writing Sample Editor effects, keep the following hardware constraints and non-obvious architectural details in mind.

> **Memory Map & Resource Limits:** All hardware memory region budgets (`FLASH`, `RAM`, `SRAM_DTC`, `SRAM_ITC`, `SRAM_OC`, `EXTERNAL_RAM`), `SAMPLE_MEMORY_SIZE` reclamation rules, the `768 KB` `externalRamScratchBuff` partition map, `PSYN_RAM_POOL_SIZE`, and subsystem capacity limits are consolidated in **[`docs/memory_map.md`](memory_map.md)**.

---

## Table of Contents

1. [Copy-on-Write `/Workspace/instruments/`, Incremental Project Saver & Hybrid SD Streaming](#1-copy-on-write-workspaceinstruments-incremental-project-saver--hybrid-sd-streaming)
2. [Synthesizer Engine Architecture & Guidelines (`.psyn` Plugins)](#2-synthesizer-engine-architecture--guidelines-psyn-plugins)
3. [Sample Editor & `.ptfx` Plugin Constraints](#3-sample-editor--ptfx-plugin-constraints)
4. [UI, FT812 Display, Config & Bootloader Gotchas](#4-ui-ft812-display-config--bootloader-gotchas)
5. [Conditional Compilation: Targeting Specific Platforms or Devices](#5-conditional-compilation-targeting-specific-platforms-or-devices)
6. [Development Console & Logging over USB (Tracker+ & Tracker Mini)](#6-development-console--logging-over-usb-tracker--tracker-mini)
7. [Global File Browser (`GlobalFileBrowser`)](#7-global-file-browser-globalfilebrowser)
8. [Real-Time Audio Graph (`AUDIO_NULL_IS_SILENCE`), DSP Sleep & IMX `fastmem.S`](#8-real-time-audio-graph-audio_null_is_silence-dsp-sleep--imx-fastmems)

---

<a id="3-copy-on-write-workspaceinstruments-incremental-project-saver--hybrid-sd-streaming"></a>
## 1. Copy-on-Write `/Workspace/instruments/`, Incremental Project Saver & Hybrid SD Streaming

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
   * **Threshold & SDRAM Pre-Cache**: Instruments whose PCM payload is `>= 256 KB` (`kStreamThresholdBytes` in [`core/source/mtStructs.h`](../core/source/mtStructs.h)) and whose play mode satisfies `isPlayModeStreamable(playMode)` allocate only **`kStreamPreCacheFrames = 32768` frames (`~743 ms` at `44.1 kHz`)** per channel (`64 KB` mono / `128 KB` stereo) in [`externalRamSampleMemory`](memory_map.md#2-external-sdram-pool--reclaimable-sample-ram-external_ram).
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

## 2. Synthesizer Engine Architecture & Guidelines (`.psyn` Plugins)

Full details in [`docs/custom_synth_sdk.md`](custom_synth_sdk.md), [`synth-plugin-sdk/README.md`](../synth-plugin-sdk/README.md), and [`docs/memory_map.md`](memory_map.md#4-synthesizer-sdram-pool-psyn_ram_pool_size--imx).

* **Loadable SD-Card `.psyn` Architecture (`PsynLoader`)**:
  * All synth engines (built-in and community) are distributed as standalone ARMv7E-M Thumb-2 relocatable `.psyn` plugins in `/Synths/*.psyn` on the microSD card and loaded dynamically by [`PsynLoader`](../synth/source/synths/PsynLoader.cpp) inside the dedicated `PSYN_RAM_POOL_SIZE` SDRAM pool (see [`docs/memory_map.md`](memory_map.md#4-synthesizer-sdram-pool-psyn_ram_pool_size--imx) for arena, boot-preload, and shared `SamplePool` capacities).
  * `PsynLoader` preloads built-in/initial engines up to `kMaxPreloadBytes` at boot, lazily loads additional engines on demand, and applies **LRU eviction** of inactive engines when headroom is needed, alongside the shared `SamplePool` for sample-based engines (`WAVS`, `GRAIN`, `DWA`, and third-party `PSYN_FLAG_HAS_SAMPLE_SLOTS` plugins).
  * **Zero firmware recompilation or bitmap edits required:** `SynthEditor` populates the Synth model list dynamically from `synth->getSynthTypes()`, and third-party engines receive deterministic non-colliding IDs (`0x100+`) when packed with `--type-id-wave 0 --type-id-play-tracker 0`.
* **Auto-Created Sample Folders & Read Fallback Chain**:
  * On boot, `PsynLoader` creates `/SynthSamples/<CODE_NAME>/` for every sample-based engine (avoiding clutter in user `/Samples/` folders), while [`core/synth/FileSystemAdapter.cpp`](../core/synth/FileSystemAdapter.cpp) resolves sample reads across `/SynthSamples/`, `/Samples/`, `/`, and `/Patches/`.
* **Keep Voice State & Arena Usage Compact (`CustomSynthVoice` / `ParamHelper`)**:
  * Inherit from `polysynth::sdk::CustomSynthVoice<VoiceClass, ParamIdEnum>` ([`synth/include/PolySynth/sdk/SynthVoiceSDK.h`](../synth/include/PolySynth/sdk/SynthVoiceSDK.h)) bridged via `PSYN_REGISTER_CPP_VOICE` ([`synth-plugin-sdk/include/psyn/psyn_cpp_bridge.h`](../synth-plugin-sdk/include/psyn/psyn_cpp_bridge.h)) or write a Pure C `psyn_plugin_entry` plugin ([`synth-plugin-sdk/include/psyn/psyn_plugin.h`](../synth-plugin-sdk/include/psyn/psyn_plugin.h)).
  * Never perform blocking `PsynHostApi` SD/WAV calls (`read_file`, `write_file`, `load_wav_file`, `free_wav_file`) or heap allocations inside real-time audio callbacks (`renderVoiceAudio` / `voice_render_audio`).

---

## 3. Sample Editor & `.ptfx` Plugin Constraints

Full details in [`docs/modular_effects_system.md`](modular_effects_system.md).

* **Plugin, Parameter, File & Preset Limits**:
  * External `.ptfx` plugins under `/Effects/` (including subfolders) are browsed and loaded on demand via **`Load Effect...`** in `GlobalFileBrowser` (with no plugin-count cap); the currently loaded plugin (`s_activeEntry`) is listed immediately above `Load Effect...` alongside the `kBuiltInEffectsCount` built-in Sample Editor effects in `s_effectDisplayNames` ([`core/effector/ptfx_manager.h`](../core/effector/ptfx_manager.h)).
  * Each `.ptfx` plugin supports up to **12 parameters** (`PtfxHeader::params[12]` in [`core/effector/tracker_plugin.h`](../core/effector/tracker_plugin.h) and [`editor-fx-sdk/tracker_plugin.h`](../editor-fx-sdk/tracker_plugin.h)), including continuous float, stepped integer, pipe-separated string lists, and SD file pickers/savers (`PTFX_PARAM_FILE` / `PTFX_PARAM_SAVE_FILE`).
  * Any `.ptfx` plugin with `>= 1` parameter automatically gains a **Presets** page via the `More` screen button (`Load` / `Save` `.ptp` presets in `/Effects/Presets/<SanitizedPluginName>/` via [`GlobalFileBrowser`](global_file_browser.md), plus `Reset`).
* **`.ptfx` Code, Scratch & Secondary WAV Regions (`externalRamScratchBuff`)**:
  * Both platforms execute `.ptfx` plugin code from the base of `externalRamScratchBuff` (on Original Tracker `MK66`, `PtfxLoader::syncInstructionCache()` disables the Kinetis System MPU via `MPU_CESR = 0`; on Tracker+ / Tracker Mini `IMX`, it cleans L1 D-Cache and invalidates I-Cache).
  * Working scratch memory (`scratch_mem`, capped by `kPtfxScratchTopOffset` in [`core/effector/ptfx_loader.h`](../core/effector/ptfx_loader.h)), the pre-decoded secondary `.wav` input (`get_secondary_sample()`), and `GlobalFileBrowser` occupy non-overlapping slices of `externalRamScratchBuff` — see [`docs/memory_map.md`](memory_map.md#3-shared-768-kb-scratch-buffer-map-externalramscratchbuff) for exact byte offsets and platform limits. Plugins can also read/write non-WAV assets via `host->read_file()` and `host->write_file()`.
  * Plugins exceeding the `MK66` executable limit (`PtfxLoader::getMaxCodeSize()`, `32 KB`) or requiring `600 MHz` Cortex-M7 speeds should declare `PTFX_FLAG_IMX_ONLY`.
* **SD Card DMA Alignment Pitfall on IMX (`FatFs` + `USDHC`)**:
  * On IMX (`platforms/imx/`), `FatFs` (`f_read`) bypasses its internal aligned DTCM sector buffer whenever a read request is `>= 512` bytes (`cc = btr / 512 > 0`) and passes the caller's destination pointer directly to the USDHC DMA controller.
  * If the destination buffer is not 4-byte aligned or lies in a non-DMA-accessible TCM region, `f_read` fails with `FR_DISK_ERR`. Always use 4-byte aligned buffers in `SRAM_DTC`/`EXTERNAL_RAM`, or read in `<= 256`-byte chunks to force `FatFs` through its internal bounce buffer (`hostReadFile` and `hostWriteFile` use `256`-byte chunking automatically).

---

## 4. UI, FT812 Display, Config & Bootloader Gotchas

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
* **Reading Text Files from SD (`SdTextFile`)**:
  * Use [`SdTextFile`](../core/include/SdTextFile.h) ([`core/source/SdTextFile.cpp`](../core/source/SdTextFile.cpp)) when reading SD text/JSON/wordlist files instead of duplicating `FileSystem::openFile` + leading-`/` fallback + `closeFile` boilerplate: `SdTextFile::openWithFallback()` retries without a leading `/`, `SdTextFile::readAll()` reads and null-terminates into a caller buffer (used by `ThemeManager`), `SdTextFile::readRandomWord()` samples a word from a wordlist file using a 96-byte stack buffer (`mtRandomNameGenerator`), and `SdScopedFile` provides RAII `closeFile()` cleanup.

---

## 5. Conditional Compilation: Targeting Specific Platforms or Devices

All three devices share the same `core/` codebase and are differentiated at compile time by preprocessor defines set in [`core/makefile`](../core/makefile), [`platforms/mk66/makefile`](../platforms/mk66/makefile), and [`platforms/imx/makefile`](../platforms/imx/makefile).

| Target Scope | Preprocessor Condition | Matching Hardware & Typical Use Case |
| :--- | :--- | :--- |
| **IMX Only** (Tracker+ & Mini) | `#ifdef PLATFORM_IMX` | `600 MHz` Cortex-M7, `64 MB` SDRAM, stereo samples (`SAMPLE_CHANNELS == 2`), 16 tracks. |
| **MK66 Only** (Original Tracker) | `#ifndef PLATFORM_IMX` | `180 MHz` Cortex-M4F, `16 MB` SDRAM, stereo samples (`SAMPLE_CHANNELS == 2`), 16 tracks. |
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

## 6. Development Console & Logging over USB (Tracker+ & Tracker Mini)

Tracker+ and Tracker Mini (`PLATFORM_IMX`) can expose a USB serial port that carries the firmware's log output and a small command console. Together with the existing USB storage mode, it lets a host script copy a new `.ptf` to the SD card and start the firmware update without touching the device or removing the card.

It is **off unless you ask for it twice**: the code is only compiled in with a build flag, and the port is only enumerated when the SD card is marked for development. A normal build on a normal card is unaffected.

| Switch | Where | Effect |
| :--- | :--- | :--- |
| `ENABLE_DEV_CONSOLE` | Build flag | Compiles the console ([`platforms/imx/source/DevConsole.cpp`](../platforms/imx/source/DevConsole.cpp)) and its `SRAM_DTC` ring buffer ([`docs/memory_map.md`](memory_map.md#5-subsystem--asset-capacity-reference)). Without it the console is replaced by empty inline stubs. |
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

## 7. Global File Browser (`GlobalFileBrowser`)

Full developer reference and C++ examples live in [`docs/global_file_browser.md`](global_file_browser.md) ([`core/interface/fileBrowser/GlobalFileBrowser.h`](../core/interface/fileBrowser/GlobalFileBrowser.h), [`GlobalFileBrowser.cpp`](../core/interface/fileBrowser/GlobalFileBrowser.cpp)).

* **One Shared Module Across Subsystems:** Covers 6 modes (`kLoadWav`, `kSaveWav`, `kLoadGenericFile`, `kSaveGenericFile`, `kLoadFolderPackage`, `kSaveFolderPackage`), used by Project Open / Save As / MOD & IT import, Synth Patch Load / Save As & Sample Slots, Sample Editor `.ptfx` file parameters, and `.ptp` effect presets. The multi-selection **Sample Loader** intentionally remains separate.
* **Shared Scratch Partition:** Runs inside a dedicated non-overlapping slice at the top of `externalRamScratchBuff` above `.ptfx` code, working scratch, and secondary WAV buffers ([`docs/memory_map.md`](memory_map.md#3-shared-768-kb-scratch-buffer-map-externalramscratchbuff)), preserving up to `10` LRU directory/cursor contexts (`kMaxSavedContexts = 10`) keyed by `contextKey`.
* **Navigation & FatFs Hygiene:** Supports `Left`-arrow jump-to-top in the Explorer pane, protected system folders (`kProtectedFolders`), inline metadata (`onDescribeItem`), and pad auditioning (`onPadEvent`). Callbacks inspecting files on cursor highlight must close FatFs handles immediately (`closeFile(fh)` with `deleteOnClose = true`) and silence preview voices (`silencePart` / `onPreviewStop`) before loading the next asset.

---

## 8. Real-Time Audio Graph (`AUDIO_NULL_IS_SILENCE`), DSP Sleep & IMX `fastmem.S`

* **`AUDIO_NULL_IS_SILENCE` Stream Contract ([`core/libs/Audio/AudioStream.h`](../core/libs/Audio/AudioStream.h), [`core/makefile`](../core/makefile))**:
  * Defined in [`AudioStream.h`](../core/libs/Audio/AudioStream.h) (`#ifndef AUDIO_NULL_IS_SILENCE`, defaulting to `1`, alongside `-DAUDIO_NULL_IS_SILENCE=1` in [`core/makefile`](../core/makefile)) and tested via `#ifdef AUDIO_NULL_IS_SILENCE`. `receiveReadOnly()` / `receiveWritable()` returning `nullptr` represents `128` samples of digital silence (`0`). Idle voices, idle `PolySynth` parts (`isIdle()`), muted mixers/muxes, and decayed effects omit `transmit()` rather than allocating and zero-filling `audio_block_t` blocks. `AudioAmplifier` and `AudioEffectMonoToStereo` skip work on `nullptr`, but still zero-fill and `transmit()` when passed a non-null block at gain `0`; active `PolySynth` parts continue rendering pre-FX sends while active and omit `transmit()` once `isIdle()`.
  * **Stateful DSP Rule**: Stateful `AudioStream` nodes must drain across `nullptr` inputs before sleeping and wake on the next non-null block. Specifically, `AudioFilterStateVariable` sleeps once `|state_lowpass|, |state_bandpass| < (1 << 12)` (`1` output LSB, `kSubLsbStateLimit`) and `|passSum| < (1 << 13)` on `nullptr` input; `AudioEffectLimiter` sleeps via `relaxed` (cleared after writing threshold/gain settings); `AudioEffectPolyverb` tracks `16,384` quiet feedback samples and releases right-channel blocks if the left input is `nullptr`; `AudioEffectDynamics` returns immediately on `nullptr` without continuing across silence (latent while line input transmits continuously).
  * **Feedback Order & Send Silence Queries**: `AudioStream::hasLaterSource` is set in `AudioConnection::connect()` when a source lies after its destination in `AudioStream::update_all()` order (monotonic, never cleared), and `AudioMixerBase::isInputSilenced(channel)` lets upstream producers (`AudioInput` gates, `PolySynth` pre-FX sends) skip work when downstream send gains are `0`.
* **`IMX` ITCM Memory Routines ([`platforms/imx/source/fastmem.S`](../platforms/imx/source/fastmem.S))**:
  * Places Thumb-2 32-byte burst (`ldmia`/`stmia`, `ldmdb`/`stmdb`) and unaligned-safe word `memcpy`, `memmove`, `memset`, and `memcmp` in `.ramfunc.$RAM2` (`SRAM_ITC`, landing near `__end_data_RAM2`) **firmware-wide** (replacing `newlib-nano`'s flash `memmove`, `memset`, and `memcmp`; `memcpy` was already placed in ITCM on `main`). Safe for arbitrary alignments in Normal memory (`SRAM_*`, `EXTERNAL_RAM`, `QSPI_FLASH`); do not call `memcpy`/`memset` on `Device` peripheral registers (`0x40000000+`).
* **`MK66` Reverb `EXTERNAL_RAM`, `.bss` Split & `Q38.26` Tick Clock**:
  * On Original Tracker (`MK66`), [`core/libs/Audio/effect_polyverb.cpp`](../core/libs/Audio/effect_polyverb.cpp) places the `32 KB` `internalRamReverbBuff` in `EXTERNAL_RAM` (`.reverb_memory`) to preserve internal SRAM heap/stack headroom, and [`platforms/mk66/3rdparty/teensy_core/mk66fx1m0.ld`](../platforms/mk66/3rdparty/teensy_core/mk66fx1m0.ld) splits `.bss_sram_l` (`< 0x20000000`) and `.bss` (`>= 0x20000000`) so no symbol straddles the `SRAM_L`/`SRAM_U` boundary (`0x20000000`).
  * [`core/source/internalTickClock.h`](../core/source/internalTickClock.h) (`InternalTickClock`) drives `Sequencer::tick()` in `Q38.26` (`2^-26` µs) fixed-point integers instead of software `double` (`softfp`) math on Cortex-M4F/M7, and treats non-positive or non-finite tempo periods as `kNever` (never advancing or accumulating catch-up ticks).
* **Live Audio CPU & Heap/Stack Diagnostics (`/.develop`)**:
  * Placing an empty `/.develop` file on the SD card enables live audio ISR CPU (`min`/`max`/`avg`/`last %`) and `freeRamBytes()` heap telemetry on the `Config` screen across **all 3 devices** (pressing `Play` or `Stop` while on `Config` resets `Profiler` counters). `Profiler::maxStackBytes()` reports the measured stack watermark on `IMX` and returns `0` on `MK66`.
