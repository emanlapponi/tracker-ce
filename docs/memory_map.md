# Firmware Memory Map, Budgets & Subsystem Limits

This document is the single canonical reference for hardware memory regions, linker headroom, SDRAM pools, shared scratch partitions, and subsystem capacity limits across **Original Polyend Tracker (`MK66`)**, **Polyend Tracker+ (`IMX`)**, and **Polyend Tracker Mini (`IMX`)**.

> [!NOTE]
> * **Live Per-Build Usage:** Exact linker byte counts for every commit are printed during `make all` (`-Xlinker -print-memory-usage`) and posted automatically in the GitHub Actions job summary and PR firmware comment ([`.github/workflows/build.yml`](../.github/workflows/build.yml)).
> * **Single Source of Truth:** Other docs link here instead of repeating volatile memory-usage figures or scratch buffer offset diagrams.

---

## Table of Contents

1. [Hardware Memory Regions & Linker Headroom](#1-hardware-memory-regions--linker-headroom)
2. [External SDRAM Pool & Reclaimable Sample RAM (`EXTERNAL_RAM`)](#2-external-sdram-pool--reclaimable-sample-ram-external_ram)
3. [Shared `768 KB` Scratch Buffer Map (`externalRamScratchBuff`)](#3-shared-768-kb-scratch-buffer-map-externalramscratchbuff)
4. [Synthesizer SDRAM Pool (`PSYN_RAM_POOL_SIZE` — IMX)](#4-synthesizer-sdram-pool-psyn_ram_pool_size--imx)
5. [Subsystem & Asset Capacity Reference](#5-subsystem--asset-capacity-reference)
6. [Memory Placement Rules & Developer Gotchas](#6-memory-placement-rules--developer-gotchas)

---

## 1. Hardware Memory Regions & Linker Headroom

Linker definitions live in [`platforms/mk66/3rdparty/teensy_core/mk66fx1m0.ld`](../platforms/mk66/3rdparty/teensy_core/mk66fx1m0.ld), [`platforms/imx/linker/linkerScript.ld`](../platforms/imx/linker/linkerScript.ld), [`platforms/imx/linker/TrackerPlus/memory.ld`](../platforms/imx/linker/TrackerPlus/memory.ld), and [`platforms/imx/linker/TrackerMini/memory.ld`](../platforms/imx/linker/TrackerMini/memory.ld).

| Platform & Region | Base Address | Total Capacity | Baseline Used | Baseline Free | What Lives Here & Developer Notes |
| :--- | :--- | ---: | ---: | ---: | :--- |
| **Original (`MK66`) — `FLASH`** | `0x00000000` | `1,024 KB` | `~911.2 KB` (`89.0%`) | **`~112.8 KB`** | Code, `.rodata`, `.audio_flash_functions`, `.audio_ram_functions`, compressed fonts & bitmaps, and `.data` load image. |
| **Original (`MK66`) — `RAM`** *(Internal SRAM)* | `0x1FFF0000` | `256 KB` | `~174.2 KB` (`68.0%`) | **`~81.8 KB`** | `.usbdescriptortable`, `.dmabuffers`, `.usbbuffers`, `.data`, `.noinit`, `.bss_sram_l` (`< 0x20000000`), and `libTracker.a` `.bss` (`>= 0x20000000`). **Remaining free SRAM (`_ebss.._estack`) is the entire runtime heap + C stack.** `mk66fx1m0.ld` asserts that `.bss_sram_l` never crosses `0x20000000` (`SRAM_L`/`SRAM_U` boundary). |
| **Original (`MK66`) — `EXTERNAL_RAM`** *(SDRAM)* | `0x70000000` | `16,384 KB` (`16 MB`) | `~15,296 KB` (`93.4%`) | **`~1,088 KB` (`1.06 MB`)** | `SAMPLE_MEMORY_SIZE` (`11.25 MB`, overlapping `7.5 MB` stereo Sample Editor buffers), `1.5 MB` delay, `768 KB` `externalRamScratchBuff`, `32 KB` `.reverb_memory` + `32 KB` `internalRamReverbBuff`, `64 KB` `g_wtEnvelopeBank`, `VoiceStreamManager` ring buffers, and cold UI/FileManager buffers. *(Additional space requires lowering `SAMPLE_MEMORY_SIZE` alongside `EDIT_BUFF_SIZE`; see [Section 2](#2-external-sdram-pool--reclaimable-sample-ram-external_ram).)* |
| **Plus / Mini (`IMX`) — `QSPI_FLASH`** | `0x60030000` *(Plus)*<br>`0x60020000` *(Mini)* | `16,192 KB` *(Plus)*<br>`16,256 KB` *(Mini)* | `~1,299.3–1,328.2 KB` (`8.0–8.2%`) | **`~14,893–14,928 KB`** | XIP Flash above the protected bootloader region (`0x60000000`–`0x6002FFFF` on Plus / `0x6001FFFF` on Mini). **Never modify `QSPI_FLASH` `ORIGIN`.** |
| **Plus / Mini (`IMX`) — `SRAM_ITC`** *(`RAM2`)* | `0x00000008` | `~128 KB` (`0x1FFF8`) | `~61.6 KB` (`48.1%`) | **`~66.4 KB`** | Zero-wait-state instruction TCM (`.ramfunc.$RAM2` / `.audio_ram_functions` hot DSP loops + firmware-wide [`fastmem.S`](../platforms/imx/source/fastmem.S) `memcpy`/`memmove`/`memset`/`memcmp`). First 8 bytes reserved as null-pointer guard. |
| **Plus / Mini (`IMX`) — `SRAM_DTC`** *(`RAM`)* | `0x20000000` | `352 KB` (`0x58000`) | `~335.8–337.4 KB` (`95.4–95.9%`) | **`~14.6–16.2 KB`** | `.data`, `.dmabuffers`, `.bss`, `NonCacheable`, `.noinit`, **plus statically reserved `.heap` (`135.5 KB`, `_HeapSize = 0x21E00`) and `.stack` (`20 KB`, `_StackSize = 0x5000`)**. Baseline usage already includes both heap and stack reservations; keep large/non-realtime arrays out of default `.bss`. |
| **Plus / Mini (`IMX`) — `SRAM_OC`** *(`RAM3`)* | `0x20200000` | `32 KB` (`0x8000`) | `~22.7 KB` (`70.8%`) | **`~9.3 KB`** | Non-cacheable OCRAM for USB/SDMA buffers and `audioPlayBuff` ([`platforms/imx/usb/audio_unified.c`](../platforms/imx/usb/audio_unified.c)). |
| **Plus / Mini (`IMX`) — `EXTERNAL_RAM`** *(`RAM4`)* | `0x80000000` | `65,536 KB` (`64 MB`) | `~65,515 KB` (`99.97%`) | **`~20.5 KB`** | `~53.84 MB` `SAMPLE_MEMORY_SIZE` + `6 MB` `PSYN_RAM_POOL_SIZE` + `1.5 MB` delay + `768 KB` `externalRamScratchBuff` + `32 KB` `.reverb_memory` + `64 KB` `g_wtEnvelopeBank` + display/stream/USB/UI buffers. **Up to `~22.84 MB`–`23 MB` reclaimable** by lowering `SAMPLE_MEMORY_SIZE` (see [Section 2](#2-external-sdram-pool--reclaimable-sample-ram-external_ram)). |

---

## 2. External SDRAM Pool & Reclaimable Sample RAM (`EXTERNAL_RAM`)

Core SDRAM constants and buffers are defined in [`core/source/mtStructs.h`](../core/source/mtStructs.h) and [`core/source/Tracker.cpp`](../core/source/Tracker.cpp):

| Symbol / Section | `MK66` Size | `IMX` Size | Location / Notes |
| :--- | ---: | ---: | :--- |
| `externalRamSampleMemory` (`.sample_memory`, `SAMPLE_MEMORY_SIZE`) | `11,520 KB` (`11.25 MB`) | `55,136 KB` (`~53.84 MB`) | Resident sample pool + pre-caches (`mtProject.max_memory`). `IMX` formula: `((60 * 1024 - 64 - 96) * 1024) - PSYN_RAM_POOL_SIZE`. |
| `externalRamEffectsBuff` *(overlaps top of `externalRamSampleMemory`)* | `7,680 KB` (`7.50 MB`) | `15,360 KB` (`15.00 MB`) | Starts at `EDIT_BUFF_OFFSET = SAMPLE_MEMORY_SIZE - (2 * SAMPLE_CHANNELS * EDIT_BUFF_SIZE)` (`SAMPLE_CHANNELS = 2` on both platforms). `EDIT_BUFF_SIZE` is `15 * 256 * 1024` (`3,932,160`) on `IMX` (`EDIT_BUFF_SIZE / 2 = 1,966,080` frames = `3.75 MB` per half) and `15 * 128 * 1024` (`1,966,080`) on `MK66` (`EDIT_BUFF_SIZE / 2 = 983,040` frames = `1.875 MB` per half): `confirmed` + `processed` per channel. |
| `s_psynRamPool` (`PSYN_RAM_POOL_SIZE`) | — | `6,144 KB` (`6.00 MB`) | `.psyn` executable arena, synth `SamplePool`, and PolySynth Master FX ([`core/synth/Synth.cpp`](../core/synth/Synth.cpp)); see [Section 4](#4-synthesizer-sdram-pool-psyn_ram_pool_size--imx). |
| `externalRamDelayBuff` (`.delay_memory`, `DELAY_MEMORY_SIZE`) | `1,536 KB` (`1.50 MB`) | `1,536 KB` (`1.50 MB`) | Master Delay audio buffer. |
| `externalRamScratchBuff` (`.scratch_memory`, `SHARED_SCRATCH_MEMORY_SIZE`) | `768 KB` | `768 KB` | Shared 4-region scratch buffer; see [Section 3](#3-shared-768-kb-scratch-buffer-map-externalramscratchbuff). |
| `externalRamReverbBuff` (`.reverb_memory`, `REVERB_MEMORY_SIZE`) | `32 KB` | `32 KB` | Master Reverb buffer (`32 KB`); on `MK66`, `Polyverb` also places `internalRamReverbBuff` (`32 KB`) in `__NOINIT(EXTERNAL_RAM)` ([`core/libs/Audio/effect_polyverb.cpp`](../core/libs/Audio/effect_polyverb.cpp)). |
| `g_wtEnvelopeBank` | `64 KB` | `64 KB` | `256` wavetable envelope/LFO shapes × `128` `int16_t` points ([`core/source/mtWavetableEnvelopes.cpp`](../core/source/mtWavetableEnvelopes.cpp)). |
| Display list buffers (`arenaWords`, `buildWords`, `frameWords`, `chipLists`) | — *(smaller)* | `88 KB` | MCU-built FT812 display-list arena (`14,336 + 2,048 + 2,048 + 2×2,048` `uint32_t` words in [`core/libs/NewDisplay/display.cpp`](../core/libs/NewDisplay/display.cpp)). |
| `VoiceStreamManager`, `FileManager`, `ThemeManager`, `audioRecBuff`, UI singletons | Remainder | Remainder | Streaming ring/bounce buffers, `decompressedFontsCommonBuffer` (`22,396 B`), `ThemeManager` tables/read buffer, `audioRecBuff` (`IMX`), pattern/project buffers. |

### Reclaiming `EXTERNAL_RAM` by Lowering `SAMPLE_MEMORY_SIZE`

Because [Hybrid SD Card Sample Streaming](developer_guide.md#1-copy-on-write-workspaceinstruments-incremental-project-saver--hybrid-sd-streaming) ([`core/AudioEngine/VoiceStreamManager.cpp`](../core/AudioEngine/VoiceStreamManager.cpp)) streams eligible samples `>= 256 KB` (`kStreamThresholdBytes`) from the microSD card while allocating only `kStreamPreCacheFrames = 32,768` frames (`64 KB` per channel) in `externalRamSampleMemory`, **IMX `EXTERNAL_RAM` headroom can be expanded significantly** by reducing `SAMPLE_MEMORY_SIZE` in [`core/source/mtStructs.h`](../core/source/mtStructs.h):

* **Tracker+ & Tracker Mini (`IMX`):** Lowering `SAMPLE_MEMORY_SIZE` from `~53.84 MB` (`55,136 KB`) down to `31 MB` (`31 * 1024 * 1024`) keeps the full `15.0 MB` Sample Editor region plus `16.0 MB` below `EDIT_BUFF_OFFSET` for short resident samples, stream pre-caches, and Wavetable/Granular instruments while freeing **`~22.84 MB` of `EXTERNAL_RAM`**.
* **Invariants When Adjusting `SAMPLE_MEMORY_SIZE` or `EDIT_BUFF_SIZE`:**
  1. `SAMPLE_MEMORY_SIZE` **must strictly exceed** `2 * SAMPLE_CHANNELS * EDIT_BUFF_SIZE` (`> 15.0 MB` on `IMX`, `> 7.5 MB` on `MK66`), otherwise the unsigned `uint32_t EDIT_BUFF_OFFSET` calculation underflows.
  2. When [`mtEffect::startLoad()`](../core/effector/mtEffect.cpp) loads a resident sample whose `confirmed` buffer overlaps its source address in `externalRamSampleMemory`, it temporarily `memmove`s the channel to `externalRamSampleMemoryBegin + EDIT_BUFF_SIZE * idx`. Keeping `EDIT_BUFF_OFFSET >= 15.0 MB` on `IMX` (`SAMPLE_MEMORY_SIZE >= 30.0 MB`) and `>= 3.75 MB` on `MK66` (`SAMPLE_MEMORY_SIZE >= 11.25 MB`) guarantees non-overlapping staging for max-length (`1,966,080`-frame on `IMX`, `983,040`-frame on `MK66`) editable samples without also reducing `EDIT_BUFF_SIZE`.

---

## 3. Shared `768 KB` Scratch Buffer Map (`externalRamScratchBuff`)

Defined in [`core/source/Tracker.cpp`](../core/source/Tracker.cpp) (`SHARED_SCRATCH_MEMORY_SIZE = 768 * 1024` in [`core/source/mtStructs.h`](../core/source/mtStructs.h)), `externalRamScratchBuff` sits in `.scratch_memory` (`EXTERNAL_RAM`, 32-byte aligned) and is shared across mutually exclusive or partitioned subsystems.

### Partitioned Layout (Sample Editor `.ptfx` + `GlobalFileBrowser` Active)

Offsets are defined in [`core/effector/ptfx_loader.h`](../core/effector/ptfx_loader.h) and [`core/interface/fileBrowser/GlobalFileBrowser.cpp`](../core/interface/fileBrowser/GlobalFileBrowser.cpp):

```text
Offset 0 KB               128 KB                  512 KB                 744 KB          768 KB
├─────────────────────────┼───────────────────────┼──────────────────────┼───────────────┤
│ .ptfx Code + .bss       │ .ptfx Working Scratch │ .ptfx Secondary WAV  │ Global File   │
│ (128KB IMX / 32KB MK66) │ (384 KB)              │ Buffer (232 KB)      │ Browser (24KB)│
└─────────────────────────┴───────────────────────┴──────────────────────┴───────────────┘
```

| Slice | Range | Size | Owner & Purpose |
| :--- | :--- | ---: | :--- |
| **`.ptfx` Executable Buffer** | `[0 .. 128 KB]` *(32 KB cap on MK66)* | `128 KB` (`IMX`)<br>`32 KB` (`MK66`) | Relocated `.ptfx` plugin binary (`.text` + `.rodata` + `.data` + `.bss`), capped by `PtfxLoader::getMaxCodeSize()`. Placed at `0x80000000` base on `IMX`. |
| **`.ptfx` Working Scratch (`scratch_mem`)** | `[128 KB .. 512 KB]` | `384 KB` (`393,216 B`) | Plugin `// PTFX_SCRATCH: <bytes>` allocation, capped at `kPtfxScratchTopOffset` (`512 KB`) by `PtfxLoader::getMaxScratchSize()`. |
| **`.ptfx` Secondary WAV Buffer** | `[512 KB .. 744 KB]` | `232 KB` (`237,568 B`) | Pre-decoded secondary `.wav` input (`PTFX_PARAM_FILE` / `get_secondary_sample()`, up to ~`118k` `int16_t` samples) at `kPtfxSecondarySampleOffset`. |
| **`GlobalFileBrowser` Scratch** | `[744 KB .. 768 KB]` | `24 KB` (`24,576 B`) | `cGlobalFileBrowser` directory name/pointer lists (`kBrowserScratchOffset`). Opening the file browser from inside the Sample Editor or an effect parameter never overwrites active `.ptfx` code, scratch, or secondary WAV data. |

### Modal Full-Buffer Consumers (While `.ptfx` Execution Is Idle)

Subsystems that only run outside Sample Editor `.ptfx` rendering reuse the base of `externalRamScratchBuff` directly (requiring `.ptfx` reload on return to Sample Editor if overwritten):
* **`sdStream` audio preview** ([`core/AudioEngine/mtAudioEngine.cpp`](../core/AudioEngine/mtAudioEngine.cpp)): uses `[0 .. 320 KB]` (`kSdStreamBufferSize = (256 + 64) * 1024`), leaving `[744 KB .. 768 KB]` for `GlobalFileBrowser` untouched during preview-enabled browsing.
* **`mtAudioExport` stem render buffer** ([`core/Exporter/mtAudioExport.cpp`](../core/Exporter/mtAudioExport.cpp)): reuses `externalRamScratchBuff` (`static_assert(sizeof(renderBuffer) <= SHARED_SCRATCH_MEMORY_SIZE)`).
* **`SynthEditor`** ([`core/interface/instrumentEditor/SynthEditor.cpp`](../core/interface/instrumentEditor/SynthEditor.cpp)): temporarily stages file-list strings/pointers during directory scans.

---

## 4. Synthesizer SDRAM Pool (`PSYN_RAM_POOL_SIZE` — IMX)

On `PLATFORM_IMX`, [`core/synth/Synth.cpp`](../core/synth/Synth.cpp) reserves a single static `6 MB` (`6,144 KB`) SDRAM pool (`__NOINIT(EXTERNAL_RAM) alignas(32) uint8_t s_psynRamPool[PSYN_RAM_POOL_SIZE]`) managed by a 32-byte-aligned bump allocator (`largeBuffersAlloc`):

| Sub-Allocation | Capacity | Source & Behavior |
| :--- | ---: | :--- |
| **`.psyn` Executable & Heap Arena (`PsynLoader::kArenaSize`)** | `1,920 KB` (`3 * 640 KB`) | Defined in [`synth/source/synths/PsynLoader.h`](../synth/source/synths/PsynLoader.h). Holds relocated `.psyn` `.text`/`.rodata`/`.data`/`.bss`, `.init_array`, `slot_alloc`, and `load_wav_file` allocations (capped at `kMaxWavFrames = (kArenaSize / 2) / sizeof(int16_t)` = `491,520` frames). Boot preloading caches up to `kMaxPreloadBytes = 1,024 KB`; additional `.psyn` engines load lazily with **LRU eviction** of inactive engines. |
| **Synth `SamplePool`** | `4,096 KB` (`4.00 MB`) | Initialized via `synth->initSamplePool(4 * 1024 * 1024, sampleStore)` in [`core/synth/Synth.cpp`](../core/synth/Synth.cpp). Reference-counted, content-deduplicated SDRAM pool shared by `SampleSlot` instruments and patches. |
| **PolySynth Master FX Buffers** | `128 KB` remainder | Delay, Reverb, and Modulation Master FX buffers allocated via `largeBuffersAlloc` in `SynthImpl` (`DelayVoice`, `ReverbVoice`, `ModulationFxVoice`). |

See [`docs/custom_synth_sdk.md`](custom_synth_sdk.md) for `.psyn` authoring guidelines.

---

## 5. Subsystem & Asset Capacity Reference

| Subsystem | Fixed Limit / Memory Footprint | Source of Truth | Guide |
| :--- | :--- | :--- | :--- |
| **Tracks & Instrument Slots** | `16` sequencer tracks (`8` sample + `8` MIDI); `48` sample instruments + `16` MIDI instruments + `3` synth parts (`8` shared synth voices on `IMX`). | [`core/source/mtStructs.h`](../core/source/mtStructs.h), [`core/config/config.h`](../core/config/config.h) | [`developer_guide.md`](developer_guide.md) |
| **Hybrid SD Streaming** | Streams samples `>= 256 KB` (`kStreamThresholdBytes`); allocates `32,768`-frame SDRAM pre-cache (`64 KB`/ch) + `16,384`-frame (`32 KB`/ch) voice ring buffers (`4,096`-frame / `8 KB` read chunks). | [`core/source/mtStructs.h`](../core/source/mtStructs.h), [`core/AudioEngine/VoiceStreamManager.h`](../core/AudioEngine/VoiceStreamManager.h) | [`developer_guide.md`](developer_guide.md#1-copy-on-write-workspaceinstruments-incremental-project-saver--hybrid-sd-streaming) |
| **Sample Editor & `.ptfx` Plugins** | Unlimited on-demand `.ptfx` plugins via `Load Effect...` (`GlobalFileBrowser`) + `17` built-in effects; max `12` parameters (`472 B` `PtfxHeader`); max editable sample `1,966,080` frames (`IMX`) / `983,040` frames (`MK66`) (`EDIT_BUFF_SIZE / 2`); max generator output `88,200` frames (`2.0 s` @ `44.1 kHz`). Active plugin state (`s_activeEntry`, `s_activeHeader`) lives in `EXTERNAL_RAM`. | [`core/effector/ptfx_manager.h`](../core/effector/ptfx_manager.h), [`core/effector/ptfx_loader.h`](../core/effector/ptfx_loader.h), [`core/effector/mtEffectModular.cpp`](../core/effector/mtEffectModular.cpp) | [`modular_effects_system.md`](modular_effects_system.md) |
| **`GlobalFileBrowser`** | Uses `[744 KB .. 768 KB]` (`24 KB`) of `externalRamScratchBuff` + `9 KB` display-label buffer (`s_displayLabelsBuffer`, `s_displayListPtrs` in `EXTERNAL_RAM`), `10` LRU contexts (`kMaxSavedContexts`), max `256` entries (`kMaxFilesListLength`), `255`-char paths (`kMaxFilePathLen`), `80`-char filenames (`kMaxFileNameLen`). | [`core/interface/fileBrowser/GlobalFileBrowser.cpp`](../core/interface/fileBrowser/GlobalFileBrowser.cpp), [`core/config/config.h`](../core/config/config.h) | [`global_file_browser.md`](global_file_browser.md) |
| **Custom Fonts (`.ptfont`)** | `kMaxFontBufferSize = 22,396` bytes (`decompressedFontsCommonBuffer` in `EXTERNAL_RAM`); max `32` font slots (`30` custom + `2` built-in). | [`core/libs/NewDisplay/display.cpp`](../core/libs/NewDisplay/display.cpp), [`core/source/ThemeManager.h`](../core/source/ThemeManager.h) | [`custom_themes_and_fonts.md`](custom_themes_and_fonts.md) |
| **Custom Themes (`.json`)** | Max `8 KB` file read buffer (`s_fileReadBuffer`); `~17 KB` static footprint in `__NOINIT(EXTERNAL_RAM)`; max `32` theme slots (`29` custom + `3` built-in); `31`-char display names. | [`core/source/ThemeManager.cpp`](../core/source/ThemeManager.cpp), [`core/source/ThemeManager.h`](../core/source/ThemeManager.h) | [`custom_themes_and_fonts.md`](custom_themes_and_fonts.md) |
| **Random Name Wordlists (`/Words/*.txt`)** | Streams via `96 B` stack buffer (`SdTextFile::readRandomWord`), `0 B` permanent word-table RAM; `30`-char max per word (`RANDOM_NAME_WORD_1_MAX_SIZE` / `2_MAX_SIZE`); `16`-word built-in fallback per list. | [`core/source/mtRandomNameGenerator.cpp`](../core/source/mtRandomNameGenerator.cpp), [`core/source/SdTextFile.cpp`](../core/source/SdTextFile.cpp) | — |
| **Wavetable Envelopes / LFOs** | `256` shapes (`128` factory + `128` user) × `128` `int16_t` samples = `64 KB` (`g_wtEnvelopeBank` in `EXTERNAL_RAM`). | [`core/source/mtWavetableEnvelopes.h`](../core/source/mtWavetableEnvelopes.h) | [`wavetable_envelopes.md`](wavetable_envelopes.md) |
| **SD Image Viewer** | Up to `48` Baseline JPEGs (`1.jpg`–`48.jpg`) per folder (`/Images/{Reference,Notes,Custom}/`), max `800 × 390` px, max `650 KB` per file. | [`core/interface/imageViewer/fImageViewer.cpp`](../core/interface/imageViewer/fImageViewer.cpp) | [`image_viewer.md`](image_viewer.md) |
| **USB Dev Console (`IMX`)** | `4 KB` (`kTxBufferSize = 4096`) ring buffer in `SRAM_DTC` only when compiled with `ENABLE_DEV_CONSOLE`. | [`platforms/imx/source/DevConsole.cpp`](../platforms/imx/source/DevConsole.cpp) | [`developer_guide.md`](developer_guide.md#6-development-console--logging-over-usb-tracker--tracker-mini) |
| **Firmware `.ptf` Filename** | Must be `15`–`31` characters (`PolyendTrac*`, `TrackerPlus*`, `TrackerMini*`) so `/Firmware/_fwinfo` fits the `MK66` 32-byte bootloader buffer with `\0`. | [`platforms/mk66/source/PlatformImpl.cpp`](../platforms/mk66/source/PlatformImpl.cpp) | [`developer_guide.md`](developer_guide.md#4-ui-ft812-display-config--bootloader-gotchas) |

---

## 6. Memory Placement Rules & Developer Gotchas

* **Default `.bss` vs. `__NOINIT(EXTERNAL_RAM)`:**
  * Unannotated globals land in internal `RAM` (`MK66`) or `SRAM_DTC` (`IMX`), both of which are tight and hold the runtime C stack and heap. Place large or non-real-time buffers in `__NOINIT(EXTERNAL_RAM)` (`.noinit_RAM4`).
  * Because `.noinit_RAM4` is `NOLOAD` (not zero-initialized by startup code before C++ static constructors run), any struct, array, or C++ singleton placed in `__NOINIT(EXTERNAL_RAM)` **must** be explicitly zeroed (`memset`) or placement-`new` constructed during initialization (`TrackerImpl::init()`) before use.
* **Reuse `externalRamScratchBuff` Before Adding New RAM:**
  * Modal tools, file scans, and offline exporters should carve temporary memory out of `externalRamScratchBuff` (respecting `GlobalFileBrowser`'s `[744 KB .. 768 KB]` slice if file browsing runs concurrently) instead of growing static `.bss` or `EXTERNAL_RAM`.
* **Real-Time Audio Thread Rules:**
  * Never allocate/free heap memory (`new`, `delete`, `malloc`, `free`, `std::vector::push_back`, `std::string`), perform FatFs SD card I/O, or format floats (`%f` in `LOGF()`, which triggers newlib heap allocation) inside audio-rate ISRs (`update()`, `renderVoiceAudio()`, `renderPostProcessAudio()`, `tickIdle()`, `processBlock()`).
* **`IMX` ITCM `fastmem.S` Constraint:**
  * `memcpy`, `memmove`, `memset`, and `memcmp` in [`platforms/imx/source/fastmem.S`](../platforms/imx/source/fastmem.S) execute from `SRAM_ITC` (`.ramfunc.$RAM2`) for arbitrary alignments in Normal memory (`SRAM_*`, `EXTERNAL_RAM`, `QSPI_FLASH`), but **must not** be called on `Device` peripheral registers (`0x40000000+`).
* **`IMX` `FatFs` + `USDHC` DMA Alignment Pitfall:**
  * On `IMX`, `f_read` bypasses its internal aligned DTCM bounce buffer whenever `btr >= 512` (`cc = btr / 512 > 0`) and DMAs directly to the caller's pointer. Any destination pointer that is not 4-byte aligned or sits in non-DMA-accessible TCM fails with `FR_DISK_ERR`. Always use 4-byte aligned buffers in `SRAM_DTC`/`EXTERNAL_RAM`, or read in `<= 256`-byte chunks (as `hostReadFile` / `hostWriteFile` do automatically).
* **Avoiding `libstdc++` / `newlib` Flash & `SRAM_DTC` Bloat:**
  * On `IMX`, [`synth/makefile`](../synth/makefile) builds `fmt` with `-DFMT_STATIC_THOUSANDS_SEPARATOR` so `std::locale` (facets, caches, and C-locale helpers) is not linked into `QSPI_FLASH` (`~125.5 KB`) or `SRAM_DTC` (`~4.2–6.2 KB`); avoid introducing locale-dependent format specifiers (`'L'`) or `std::locale` references.
  * On `MK66`, [`platforms/mk66/3rdparty/teensy_core/new.cpp`](../platforms/mk66/3rdparty/teensy_core/new.cpp) replaces the default verbose `libstdc++` terminate handler with `__builtin_trap()`, saving `~31.5 KB` of Flash (`cp-demangle` + newlib stdio `FILE`).
* **Live Heap & Stack Diagnostics (`/.develop`):**
  * Placing an empty `/.develop` file in the SD card root enables live audio ISR CPU (`min`/`max`/`avg`/`last %`) and `freeRamBytes()` heap telemetry on the `Config` screen across all 3 devices (pressing `Play` or `Stop` resets `Profiler` counters). `Profiler::maxStackBytes()` reports the measured stack watermark on `IMX` (`0` on `MK66`).
