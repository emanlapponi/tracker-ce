# Changelog

All notable changes to the **Polyend Tracker Community Edition (CE)** firmware across **Original Tracker** (`MK66`), **Tracker+** and **Tracker Mini** (`IMX`) are documented in this file.

---

## [1.0.0] - Initial Community Edition Release

> **⚠️ Important :: Project Compatibility Notice:**
> To support dedicated per-step **Micro-move (`m`)** and **Volume (`V`)** columns alongside **`Fx1`** and **`Fx2`**, pattern files (`.mtp`) have been updated to a new format (**Version 6**).
> 
> - **Loading old projects works automatically**: Existing projects created on official/older firmwares load seamlessly and are upgraded in memory.
>   
> - **Saving updates the project to Version 6 (not backward-compatible with older firmware)**: Once you save a project on Community Edition firmware, its saved `.mtp` pattern files cannot be read by older firmwares. **Please make a backup of your `Projects/` folder on your SD card or use `Save As` to save under a new project name, if you ever plan to roll back to an older firmware.**

### Added

1. **Open Synth SDK & New `CHIP` + `DIRT` Synthesizer Engines (Tracker+ & Tracker Mini)**
   * Added a new Open Synth SDK allowing the community to add new synthesizer engines. `CHIP` is the first implementation of this open architecture.
   * Added **`DIRT`** (wave-shaping/distortion synth).
   * See [`docs/custom_synth_sdk.md`](docs/custom_synth_sdk.md) to learn more about developing your own engines.

2. **Modular Sample Editor Effect & Generator Plugins (All 3 Devices)**
   * Added hot-swappable DSP plugins loaded from `/Effects/*.ptfx` on the SD card. 
   * Supports up to **12 parameters** (across 3 pages) and up to **128 installed plugins**.
   * Added the complete `.ptfx` SDK, `ptfx_builder.py`, and 6 reference effects/generators (`Add Silence`, `Analog Kick Gen`, `Chiptune Voice Gen`, `Multi-Mode Filter`, `Stereo Delay`, `Stereo to Mono`) in [`editor-fx-sdk/`](editor-fx-sdk/).
   * See [`docs/modular_effects_system.md`](docs/modular_effects_system.md) to learn more about developing your own plugins.

3. **Wavetable Envelopes (`WT Env`), Wavetable LFOs (`WT LFO`) & Step FX (`w` / `y`) (All 3 Devices)**
   * Added `WT Env` and `WT LFO` modulation types across all 7 instrument parameters (`Volume`, `Panning`, `Cutoff`, `Wavetable Position`, `Granular Position`, `Granular Length`, `Finetune`) with **128 Factory (`F 001`–`F 128`)** and **128 User (`U 001`–`U 128`)** morphable contours and dual-axis `Warp X/Y` shaping.
   * Load your own wavetables to overwrite the 128 User shapes, by uploading them to `/Envelopes/*.wav`
   * Added `w` (WT Volume Env) and `y` (WT Filter Env) pattern step FX for per-step envelope duration/warp/ADSR shaping.
   * See [`docs/wavetable_envelopes.md`](docs/wavetable_envelopes.md).

4. **Custom Color Themes & Font Packages (All 3 Devices)**
   * You can now create your own themes and load them from `/Themes/*.json` on your SD Card (up to 29 custom themes alongside the 3 built-in themes) and custom antialiased font packages from `/Fonts/*.ptfont` (up to 30 custom fonts) selectable in `Config > General > Theme / Font`.
   * Create your own with the [Web Theme & Font Editor](https://polyend.github.io/tracker-theme-editor/) ([`polyend/tracker-theme-editor`](https://github.com/polyend/tracker-theme-editor)). For more information see: [`docs/custom_themes_and_fonts.md`](docs/custom_themes_and_fonts.md).

5. **Performance Mode Enhancement (All 3 Devices)**
   * Added page toggling between Sample Tracks and MIDI/Synth Tracks in Performance Mode. Just keep pressing `[Perform]` to toggle between the views.
   * Oh, for OG Tracker users, there's a small surprise that goes along well with this change.

6. **Hybrid SD Card Sample Streaming (All 3 Devices)**
   * Long samples  (`256 KB` and larger) now keep only their initial attack in sample memory while streaming the rest of the audio directly from the SD card in the background. This lets you load a massive amount of stems, loops, and recordings into a single project while using only a fraction of your sample RAM.
     
   * **What Streams on Each Device**:
     * **Tracker+ & Tracker Mini**: Streams **`One-Shot`**, **`Forward Loop`**, **`Backward Loop`**, **`Ping-Pong Loop`**, **`Slice`**, and **`Beat Slice`** instruments (`Wavetable` and `Granular` instruments remain fully in RAM).
       
     * **Original Tracker**: Streams **`One-Shot`**, **`Forward Loop`**, **`Backward Loop`**, and **`Ping-Pong Loop`** instruments, while keeping **`Slice`** and **`Beat Slice`** instruments (alongside `Wavetable` and `Granular`) fully in RAM to keep performance solid across multiple tracks.
       
   * **Works Transparently with the Sample Editor**: You can still edit streamed samples in the Sample Editor or switch play modes at any time. The Tracker automatically loads the full sample into RAM while you edit it and returns to streaming once your edits are applied. This excludes very long sample however, due to RAM restrictions. If you plan to stream multi-minutes samples, please prepare them outside of the device.
   * See [`docs/developer_guide.md`](docs/developer_guide.md#3-copy-on-write-workspaceinstruments-incremental-project-saver--hybrid-sd-streaming) for technical details.

7. **`Slice` & `Beat Slice` Instruments can now `Play Thru` (All 3 Devices)**
   * Added a per-instrument **`Play Thru`** (`Off` / `On`) setting on the **`More`** page of the **Sample Playback** screen for both **`Slice`** and **`Beat Slice`** modes.
   * When **`Play Thru`** is **`On`**, triggering any slice starts at that slice marker and continues playing through subsequent slices until the note is released (`Note Off` / `CUT` / `FAD`) or the end of the sample is reached.

8. **Dedicated Micro-move (`m`) & Volume (`V`) Step View (All 3 Devices)**
   * Each pattern step now stores dedicated **Micro-move (`m`)** (`0..100`) and **Volume / Velocity (`V`)** (`0..100`) values in addition to its two custom step FX slots (**`Fx1`** and **`Fx2`**), freeing both FX slots for creative use.
   * Pressing **`[Note]`** in Pattern View while the cursor is already on the **`Note`** column toggles the two effect columns between the standard **`[Fx1 | Fx2]`** view and the new **`[m | V]`** view.
   * Unquantized live recording (**Record Micro-move**) and velocity recording (**Record Velocity**) now write directly into the dedicated **`m`** and **`V`** step fields instead of occupying **`Fx1`** and **`Fx2`**.
   * Full support in **`[m | V]`** view for Jog-wheel / `±` editing, Pad entry, `Delete`, `Copy` / `Paste`, `Invert`, `Undo` / `Redo`, and the **`Fill`** tool (`Constant`, `From-To`, `Random`, `Euclidean`).

9. **Granular Length Automation & Step FX (`z`) (All 3 Devices)**
   * Added **`Granular Length`** as a modulatable parameter in **Instrument Automation**, allowing you to animate grain size over time using an Envelope, LFO and the new Wavetable Envelope or LFOs.
   * Added the **`z`** step FX (`0`–`100%`) to control grain length on a per-step basis (`0–50%` gives fine `1.0–100.0 ms` control for tight micro-grains, while `50–100%` sweeps `100.0–1000.0 ms` for lush clouds).
     
10. **More Chance Conditions (All 3 Devices)**
   * Spiced up the **`Chance` (`C`)** step FX beyond percentage probability (`C  0`–`C100`) with conditional and cycle-based modes inspired by the Polyend Play:
     * **Group with Last (`C GL`)**: Plays only if the previous conditional step evaluated on the same track played.
     * **Not Last (`C !L`)**: Plays only if the previous conditional step evaluated on the same track was skipped.
     * **Play N / Skip M (`C1:1`–`C6:6`)**: Plays the step for `N` consecutive pattern playthroughs, then skips it for `M` playthroughs (`N, M` in `1..6`).
     * **Skip N / Play M (`CS11`–`CS66`)**: Skips the step for `N` consecutive pattern playthroughs, then plays it for `M` playthroughs (`N, M` in `1..6`).
     
11. **Reactivated Multi-Folder SD Card Image Viewer (All 3 Devices)**
   * Reactivated and expanded the JPEG image viewer under `Config > Info` with three user folders (`Reference`, `Notes`, and `Custom` in `/Images/<Folder>/1.jpg` … `48.jpg`), supporting Left/Right, Jog-wheel, and direct Pad `1–48` selection.
   * See [`docs/image_viewer.md`](docs/image_viewer.md).

### Changed & Fixed

 * **Live Recording Improvements (All 3 Devices)**:
	* You can now play much tigther notes on single-armed tracks. As in: If you play a second note before releasing the first, it won't get lost.
	* Audible first note after preroll, and configurable first-note recording timing threshold (if you tend to play early).
	  
* **Exact `44100 Hz` Offline Audio Export (All 3 Devices)**:
	* Fixed sample rate drift during offline master and stem export so rendered `.wav` files match real-time `44100 Hz` playback pitch and timing.
	* Also removed excessive silence at end of renders.
	  
* **Removed clicks on rendered samples and editor effects (All 3 devices)**
	* No more clicks at the end of your samples.
	  
* **MIDI Overhaul (All 3 devices)**
	* Fixed USB MIDI enumeration for Windows 11 (March 2026 update).
	* Fixed long-term tempo drift and overhauled how MIDI Clock is sent out. Timing is now ~15× tighter and stays locked over hours of playback.
	* Fixed an issue where the Tracker could start slightly late (by one clock tick) when pressing Play on an external master clock or DAW. Step 1 now fires dead-on the downbeat.
	* The Tracker now responds to MIDI `Continue` and `Song Position Pointer` messages.
	* Improved how the Tracker follows incoming MIDI Clock when your computer or master device sends slightly uneven clock bursts, and fixed the `Clock Sync Delay` setting so fine-tuning your setup's latency works predictably.
	* Fixed low-level USB and TRS MIDI buffer issues that could occasionally drop clocks, notes, or CC automation during busy patterns.
	
* **Faster Project Loading & Saving (All 3 Devices)**:
	* Opening a project no longer duplicates all of its audio samples into a temporary folder on the SD card before loading them into memory. Instead, samples are loaded directly from your project folder, making even large, sample-heavy projects open in a fraction of the time while drastically reducing SD card wear.
	* Saving changes to your current project now only writes out samples whose audio was actually edited or newly added, while pattern edits and instrument parameter tweaks save almost instantly. (`Save As` still saves a complete, standalone copy of your project and all its samples).
	  
* **Major Internal Memory, SDRAM & Audio Engine Optimization (All 3 Devices)**:
	* Reclaimed internal SRAM and SDRAM across all three devices to power Hybrid SD Card Sample Streaming while keeping 100% of the original sample RAM pool intact (`11.25 MB` on Original Tracker / `60 MB` on Tracker+ & Tracker Mini), leaving generous internal RAM headroom (`~128 KB` free on Original Tracker, `~22.5 KB` `SRAM_ITC` + `~21.5 KB` `SRAM_DTC` + `~9.3 KB` `SRAM_OC` on Tracker+ & Tracker Mini) and up to `~29 MB` (`IMX`) / `~5.3 MB` (`MK66`) of reclaimable SDRAM for future community ideas.
	* Optimized real-time audio rendering for cleaner playback on heavy multi-track projects.
	  
* **Open-Source Build Consolidation (All 3 Devices)**:
	* Unified `core/`, `platforms/mk66/`, `platforms/imx/`, `synth/`, `util/`, and `editor-fx-sdk/` into a single monorepo built via a top-level [`Makefile`](Makefile) and [`version.mk`](version.mk).
	* Removed the proprietary NES emulator and repurposed its `EXTERNAL_RAM` region (`externalRamScratchBuff`, `768 KB`) as the shared scratch pool for `.ptfx` plugins, offline audio export, SD `.wav` preview, and modal file browsers. (Sorry gamers).
