# Upcoming Changes (Beta / Unreleased)

> **ℹ️ Note:** The features and fixes listed below have been merged since the **[v1.0.0](CHANGELOG.md)** stable release. They are **not yet in the stable release download**, but you can try them today via the **[Latest Beta Builds](README.md#latest-beta-builds)**.
>
> Looking for what is included in the current stable release? See **[`CHANGELOG.md`](CHANGELOG.md)**.

---

### Added

1. **Loadable `.psyn` Synth Plugin SDK (Tracker+ & Tracker Mini)**
	* **SD-Card Synth Plugins (`/Synths/*.psyn`)**: Synthesizer engines now load dynamically from `/Synths/*.psyn` on the microSD card without recompiling firmware. Developers can build custom C++20 or C synth engines with [`synth-plugin-sdk/`](synth-plugin-sdk/).
	* **Sample-Based Synth Engines (`SampleSlot` & `4 MB` `SamplePool`)**: `.psyn` plugins can load `.wav` samples and wavetables from `/SynthSamples/<ENGINE>/` or anywhere on the SD card into a shared `4 MB` SDRAM sample pool, alongside a `1,920 KB` plugin code arena.
	* **100% Open-Source Firmware Link**: Moved the `PolySynth` host, `PolyDSP`, and Master FX (`Delay`, `Reverb`) to MIT-licensed source compilation with zero precompiled `.a` libraries linked into the firmware (only the prebuilt factory `.psyn` binaries in `synth/prebuilt/` remain under [`synth/prebuilt/LICENSE-PROPRIETARY.txt`](synth/prebuilt/LICENSE-PROPRIETARY.txt)).
	* See [`docs/custom_synth_sdk.md`](docs/custom_synth_sdk.md) and [`synth-plugin-sdk/README.md`](synth-plugin-sdk/README.md).

2. **Unified File Browser Across All Screens (All 3 Devices)**
	* Replaced the older single-folder file pickers with a unified folder-navigating browser across Project **`Open`** / **`Save As`**, **`.mod` / `.it` Import**, Synth Patch **`Load`** / **`Save`**, Synth **Sample Slot** `.wav` selection, and Sample Editor **Effect Loader (`Load Effect...`), File Parameters & Effect Presets** (with direct switching out to other screens via hardware module buttons).
	* See [`docs/global_file_browser.md`](docs/global_file_browser.md).

3. **Per-Sample SD Streaming Controls & RAM Default in Sample Loader (All 3 Devices)**
	* **RAM by Default**: Newly imported samples now default to loading fully into **RAM** (`Mem: RAM`) rather than streaming automatically, giving you full control over when SD streaming is used.
	* **Import Memory Mode Selector**: Added a memory mode button on the Sample Loader screen (`Mem: RAM` / `Mem: Auto` / `Mem: [S]`) to choose whether newly loaded samples go to RAM, auto-stream when `256 KB` or larger, or always stream when eligible.
	* **Per-Instrument RAM / Stream Toggle**: Highlighting an existing instrument in the right-hand slot list changes the button to **`Mode: RAM`** or **`Mode: [S]`**, allowing you to switch individual instruments between RAM and SD streaming on the fly.
	* **Crop & Stream**: Added an **`Import Stream`** option in the partial sample crop/import screen alongside `Import` and `Import Low quality`.

4. **"Add to Empty Slot" in Sample Loader (All 3 Devices)**
	* Holding **`[Shift]`** while browsing files in the Sample Loader changes `Add` to **`Add to Empty Slot`** (or `Next Slot`), automatically picking the first empty instrument slot and importing the sample there.

5. **38 New Chord Inversions in the `Chord` (`f`) Step FX (All 3 Devices)**
	* Expanded the **`Chord` (`f`)** effect from 30 to **68 chord voicings** by adding 38 inversions across major, minor, seventh, diminished, suspended, and `m7b5` chords.

6. **Sample Editor - New Effects, Presets & Plugin SDK Additions (All 3 Devices)**
	* **`Transient Shaper`**: New Sample Editor effect (`transient_shaper.ptfx`) with `Attack` (`-100%` to `+100%`) and `Sustain` (`-100%` to `+100%`) controls for shaping drum hits and loops.
	* **`Haas Effect`**: New stereo widener effect (`haas.ptfx`) that delays the Left or Right channel (`1–100 ms`) with a smooth tail fade-out.
	* **`Sample Cross-Mod`**: New reference effect (`cross_mod.ptfx`) that layers, ring-modulates, FM-scrubs, amplitude-follows, or sidechains a sample against a secondary `.wav` file selected from the SD card.
	* **`Convolution Reverb`**: New Sample Editor reverb (`convolution_reverb.ptfx`) that applies the acoustic space or character of any `.wav` impulse response (IR) from your SD card to your sample. Supporting both mono and stereo IRs (up to ~550 ms) and automatically extending the sample tail so the reverb rings out cleanly.
	* **On-Demand `.ptfx` Effect Loader**: Removed the 128-plugin startup scan limit in favor of **`Load Effect...`** at the bottom of the Sample Editor effect list, browsing `/Effects/` (including subfolders) in `GlobalFileBrowser` with pretty effect display names and keeping the currently loaded effect above `Load Effect...`.
	* **Effect Presets**: Load, save, and reset effect presets via **`More`** (stored under `/Effects/Presets/<PluginName>/*.ptp`), automatically saving and restoring knob values and selected files.
	* **Mono-to-Stereo Effect Support**: Applying a stereo effect plugin (such as `Haas Effect` or `Stereo Delay`) to a mono sample now automatically converts the instrument to stereo when the left and right channels differ.
	* **Plugin SDK - File Pickers & Secondary Audio**: `.ptfx` plugins can now declare input/output file parameters (`PTFX_PARAM_FILE` / `PTFX_PARAM_SAVE_FILE`) rendered with vertical rotated filenames, single-press browser open, `Delete` to clear, automatic secondary `.wav` decoding, and SD file I/O helpers. See [`docs/modular_effects_system.md`](docs/modular_effects_system.md).

7. **Clearer `[m | V]` vs `[Fx1 | Fx2]` Pattern Editor View & Volume Lane `!OFF` (All 3 Devices)**
	* Empty step slots in the **`[m | V]`** pattern view now show dimmed **`m--`** and **`V--`** indicators instead of `---`, making it obvious at a glance whether you are viewing `[m | V]` or `[Fx1 | Fx2]`.
	* **`!OFF` in the Dedicated Volume Lane**: Added **`!OFF`** as the leftmost value (before `0–100`) in the dedicated **`V--`** Volume lane (`[m | V]` view), allowing you to reset a sample track's volume back to its **Instrument Parameters** default volume directly from the Volume lane while keeping both `FX1` and `FX2` free.

8. **Stereo Samples on Original Tracker (Original Tracker + All 3 Devices)**
	* **Stereo Sample Support**: Original Tracker can now load, preview, record, edit, and stream stereo samples (up to ~22.3 seconds in the Sample Editor, with longer stereo samples streaming from the SD card).
	* **Smarter Mono/Stereo Playback**: Mono samples now use only a single voice channel under the hood across all 3 devices, saving CPU for busy projects while keeping full stereo playback and effects for stereo samples.

9. **USB Storage Mode & Stereo USB Audio (Original Tracker)**
	* **USB Storage Mode**: Added USB Storage mode to Original Tracker so you can mount the SD card on your computer over USB, with playback and streaming pausing safely while connected.
	* **2-in / 2-out USB Audio**: Added 44.1 kHz / 16-bit stereo USB Audio (`Off`, `Input (2 in)`, `Output (2 out)`, `In/Out (2 in, 2 out)`) so you can record from your computer or stream audio to your DAW over USB.

> [!NOTE]  
> **On Hardware Limitations:** The OG Tracker runs at 180 MHz with 16 MB of sample RAM. Heavy use of stereo samples, sample streaming, or USB audio streaming can max out the CPU and cause audio stuttering or UI lag. Since we managed to optimize the core of the system, we still decided to include these options so you can use them at your own discretion.

10. **Sample Recorder Source Improvements (All 3 Devices)**
	* **Clearer Mono & Stereo Input Choices**: Sample Recorder now lets you pick directly between **`Line In (L)`**, **`Line In (R)`**, **`Line In (M)`** (mono mixdown), **`Line In`** (stereo), **`Radio`** (mono), **`Mic`** / **`Mic LG`** / **`Mic HG`**, **`USB (M)`** (mono mixdown), and **`USB`** (stereo), with count-in metronome support when recording from USB and live monitoring restored when you stop recording.
	* **Simplified Settings**: Removed the redundant `Config -> General -> Line in channel` option now that mono/stereo Line In selection is built directly into the Sample Recorder.

11. **USB Serial Development Console & Fault Reporting (Tracker+ & Tracker Mini)**
	* Added an opt-in USB CDC serial console (`ENABLE_DEV_CONSOLE` when a `/.develop` file is present on the SD card) with host automation commands (`ping`, `storage on|off`, `flash <name>`, `reboot`, `help`) and a ring-buffered `ENABLE_LOG` build flag that preserves release optimization levels.
	* With `ENABLE_DEV_CONSOLE`, CPU faults report `@fault pc=... lr=...` and a rough call chain over the serial console instead of freezing silently. See [`docs/developer_guide.md`](docs/developer_guide.md).

12. **`BUS` Insert Effect Engine & Synth SDK Audio Input (Tracker+ & Tracker Mini)**
	* Added **`BUS`** (`/Synths/BUS.psyn`), an insert effect synth that takes over selected sample tracks and lower-numbered synth parts instead of mixing them directly, and runs them through a limiter (`Threshold`, `Attack`, `Release`).
	* **Sidechain Ducking**: A separate `Sidechain` section drives the limiter from other tracks or synths (summed to mono) while they stay in the normal mix, so a kick can duck a pad.
	* **Plugin Audio Input**: `.psyn` engines packaged with `--audio-input 1` now receive host audio through `voice_render_audio_in`, and choose their own routed sources from patch params. Existing plugins are unaffected. See [`docs/custom_synth_sdk.md`](docs/custom_synth_sdk.md).

13. **Pattern View Note Display Option (All 3 Devices)**
	* Added **`Config → General → Note display`** to choose how notes are displayed in Pattern View: **`Original`** (`C4`, `C#4`, default) or **`Traditional`** (`C-4`, `C#4`).

14. **SD-Card Random Name Wordlists (All 3 Devices)**
	* Moved the random name generator wordlists to `/Words/adjectives.txt` and `/Words/nouns.txt` on the microSD card (one word per line, with a built-in fallback list when the files are absent; initial files provided in [`sd-card/Words/`](sd-card/Words/)), freeing some Flash space on Original Tracker and letting users customize their own wordlists.

### Changed & Fixed

* **Faster UI Rendering & System Responsiveness (All 3 Devices)**:
	* **~3×–4× Faster Heavy Screen Updates**: Rebuilt the display engine so screens are composed directly on the main processor and only changed regions are sent to the display. Fast Pattern Editor scrolling, Song mode navigation, and switching screens feel **~3×–4× faster** without partial intermediate redraws.
	* **~5×–10× Lower Display CPU Overhead (Tracker+ & Tracker Mini) / ~3×–5× Lower (Original Tracker)**: Display transfers on Tracker+ and Tracker Mini now run in the background via hardware DMA (and idle screens no longer poll the display bus at all), freeing CPU headroom for tighter pad/encoder response, MIDI timing, and SD sample streaming.
	* **Instant Recovery After Screenshots & General Speedups**: Taking SD card screenshots no longer stalls to rebuild the screen afterwards, audio sequencer state and SD file loading were streamlined for faster project/sample loads, and leftover display video-stream code from the removed NES emulator was cleaned up.

* **Lower Real-Time Audio CPU Usage (All 3 Devices)**:
	* Average ~36% less Audio CPU consumption on OG Tracker
	* Average ~30% less on Tracker+ & Tracker Mini
	* Silent sample tracks, idle `PolySynth` slots (once voice envelopes and post-FX tails finish), disconnected USB Audio outputs, disabled Limiter sidechains, and decayed Master FX (`Delay`, `Reverb`, `Limiter`, filters) now sleep without allocating or mixing silent audio buffers, waking seamlessly on the next note.
	* Added per-patch `Voice Sleep` (`On` by default, in the synth `Voice` layout) and `.psyn` idle-sleep support (`PSYN_FLAG_SUPPORTS_IDLE_SLEEP`, `PSYN_FLAG_HAS_VOICE_TICK_IDLE` / `tickIdle()`): when `Voice Sleep` is `On`, idle voices enter full zero-CPU sleep once active voices and any post-FX tails finish (engines without global post-FX sleep immediately on voice release); when `Off`, engines providing `tickIdle()` (all bundled factory `.psyn` engines and `CHIP.psyn`) run lightweight phase/modulation advancement without full audio rendering or silent block allocation, while engines without `tickIdle()` run full legacy rendering (unflagged ABI v1 plugins stay active for backwards compatibility).
	* Faster Sample Playback, Envelopes, Master FX & `Q38.26` Sequencer Clock; DSP output remains within `<= 1 LSB` (synth soft-clip reciprocal), `<= 3 LSB` (SVF sub-LSB tail sleep), and `<= 7 LSB` (`Polyverb` tail sleep/wake) of `main`.

* **Factory & `CHIP` Synth Engine Performance Overhaul (Tracker+ & Tracker Mini)**:
	* Overhauled real-time oscillator, filter (`SVF`, `Ladder`, `Diode`, `Biquad`), envelope, and modulation math across all factory `.psyn` synth engines (`ACD`, `FAT`, `VAP`, `WTFM`, `PERC`, `DIRT`) and `CHIP` for **~1.5×–3.3× faster** active voice rendering, zero silent-tail denormals, improved low-cutoff `Diode` filter precision, and clean `PERC` voice sleep termination while preserving original patch sound (`<= 1 LSB` typical RMS difference).
	* Playing dense 8-voice chords and multi-synth arrangements (including `CHIP`) runs with significantly lower CPU load without audio dropouts or freezes.

* **Safer USB Storage Mode (Tracker+ & Tracker Mini)**:
	* Entering USB Storage mode now cleanly stops playback, armed recordings, held notes, and SD sample streaming, and saves any pending changes first so the Tracker and your computer never write to the SD card at the same time.
	* Leaving USB Storage mode now refreshes the SD card and any streamed samples automatically so file changes made on your computer are picked up right away.

* **USB Audio & MIDI Fixes (Tracker+ & Tracker Mini)**:
	* **Fixed Swapped `Input` / `Output` USB Audio Modes**: Fixed `Input` and `Output` being reversed in USB Audio settings, and updated the menu options to **`Off`**, **`Input (2 in)`**, **`Output (28 out)`**, and **`In/Out (2 in, 28 out)`** so direction and channel counts are explicit.
	* **Fixed Single-Direction USB Audio & MIDI Connections**: Fixed an issue where enabling only USB `Input` or only `Output` (or combining USB Audio with USB MIDI) could fail to appear properly on a computer or USB host.
	* **Better Portable USB Host Compatibility**: Lowered the Tracker's reported USB power requirement (`100 mA`) so battery-powered USB hosts (such as the Teenage Engineering OP-1 Field) accept the connection instead of rejecting it.

* **Audio Engine & Playback Fixes**:
	* **Fixed Delay/Reverb Echo Bleed After Stop (Tracker+ & Tracker Mini)**: Fixed a bug where an instrument with no Delay or Reverb send could briefly echo in the right channel on the first note played after stopping playback.
	* **Fixed Initial Delay Feedback & Reverb Damping on Boot (All 3 Devices)**: Fixed `audioEngine::init()` passing unscaled Delay feedback and un-inverted Reverb damping before a project load refreshes them (`mtProject.values` starts zeroed at boot).
	* **Delay Crossfading, Saturation & Project Reset (All 3 Devices)**: `AudioEffectShortDelay` now crossfades delay-time changes smoothly while sounding and switches immediately with zero blend when quiet, saturates feedback peaks with `clamp16` instead of wrapping at full scale, and clears its buffer on project load.
	* **Stale Block, Tail State & Tempo Safety Fixes (All 3 Devices / Tracker+ & Mini)**: Drains unselected `AudioOutput` channels when switching output sources, releases right-channel `Polyverb` input blocks when the left input is `nullptr`, settles/resets per-track filter state across silent/blocked instrument transitions, and guards `InternalTickClock` against non-positive or non-finite tempo values so invalid project tempos do not freeze the audio ISR or build catch-up bursts.
	* **Fixed Noise Bursts on Very Short Loops & Grains (All 3 Devices)**: Prevented high-pitched notes on tiny backward loops, ping-pong loops, and granular grains from stepping past the loop boundaries and producing noise bursts.
	* **Fixed Granular Mode on Long & Empty Samples (All 3 Devices)**: Enforced a minimum granular loop zone and clamped grain lengths on long samples (e.g., `1 ms` grains on samples longer than ~16 seconds) so `Gauss`/`Triangle` grains no longer go silent or wrap across the sample, and guarded against division by zero on empty sample slots.
	* **Fixed Out-of-Bounds Pitch, Glide, Wavetable & Delay Rate Lookups (All 3 Devices)**: Clamped note, tune, fine-tune, and glide calculations inside the 120-entry note table (fixing wrong pitches at extreme octaves or when previewing `OFF`/`CUT`/`FADE` steps), rejected unsupported wavetable window sizes (`< 32` or larger than the sample), clamped tempo-synced Delay rate indices (`0–18`) when loading projects, and hardened sample-player update paths and `SdStream` buffer rotation.
	* **Fixed Audio-Interrupt Heap Corruption During `.wav` Preview (Tracker+ & Tracker Mini / All 3 Devices)**: Removed per-block heap allocation (`std::vector`) inside the audio interrupt during `.wav` file preview on Tracker+ and Tracker Mini (preventing random crashes/hangs when browsing folders or switching screens while previewing), and cleared released audio block pointers on early allocation failure to prevent double-freeing blocks.
	* **Fixed Sequencer `Tune` Modulation (All 3 Devices)**: Fixed a bug where sequencer modulation assigned to `Tune` did not work unless `Wavetable Position` modulation was active.

* **Sequencer, MIDI & Pattern Editor Fixes (All 3 Devices)**:
	* **Fixed `!OFF` After Volume FX on Note Steps & Added Volume Lane `!OFF`**: Fixed a bug where placing a `V` (`Volume`), `RV` (`Random Volume`), or volume-ramping `R` (`Roll`) command on the same step as a note latched the step's velocity so a later `!OFF` (or another FX in that column) failed to reset the sample's volume back to the **Instrument Parameters** default volume (or the step's dedicated Volume lane value). Also added **`!OFF`** as the leftmost value before `0–100` in the dedicated **`V--`** Volume lane (`[m | V]` view).
	* **Fixed Freeze After Exiting Performance Mode (Tracker+ & Tracker Mini)**: Fixed a freeze that could happen when previewing steps or receiving `MIDI Stop` right after leaving Performance Mode.
	* **Smoother Song Playback & External `MIDI Start` Reliability**: Fixed freezes when auto-save overlapped a pattern change during Song playback or when receiving `MIDI Start` on the Song screen, and made playback stop cleanly if the next pattern cannot be loaded in time instead of playing or saving the wrong pattern.
	* **Full 255-Slot Song Fixes**: Fixed freezes and unexpected jumps back to Slot 1 when playing to the end of, or deleting slots from, a full 255-slot song, and ignored Song slot paste when nothing has been copied yet.
	* **Pattern Editor Cursor, Fill, Pad & Roll Fixes**: Fixed the cursor jumping past the end of a pattern after pasting with a large Edit Step, kept `Note Up` Rolls on the highest note (`G9`) instead of wrapping down to `C0`, fixed `Random Fx` and `Off` ranges in the Fill popup, ignored unused pads `47–48` in the `FX` pad shortcut, and kept pads in normal note layout when a MIDI or Synth instrument is selected.
	* **Fixed Synth FX MIDI CC Control in Performance Mode**: Restored external MIDI CC control over synth effect parameters in Performance Mode.
	* **Fixed Song Mode Block Copy/Paste**: Fixed copying and pasting song arrangement blocks smaller than 32 steps.
	* **Fixed Beat-Slice Pad Lights in Live Recording (Original Tracker & Tracker+)**: Beat-slice pad guide lights now stay active in the Pattern Editor during Live Recording (`Rec + Play`), matching step-record mode.

* **Performance Mode Fixes (All 3 Devices)**:
	* **Fixed Pattern Edits Resetting After Visiting `Perform` While Stopped**: Leaving `Instrument Parameters` or `Master` after opening them from `Perform` while playback is stopped now exits Performance Mode cleanly instead of reloading the pattern from the SD card and undoing recent edits on the next note preview.
	* **Fixed FX Slot Swapping, End-Point FX2 & MIDI CC Row Handling**: Swapping two FX slots (`Shift + Left/Right`) now moves their selected row, live value, MIDI CC assignment, and pad lights together, turning the encoder on a MIDI CC row no longer overwrites neighboring slot values, and Performance Mode sample end-point checks now honour the second FX slot alongside the first.
	* **Safe Missing Pattern Fallback**: Selecting a missing or unreadable pattern for a Performance track now plays silence instead of triggering unexpected notes.

* **Sample Loader, Sample Playback, Sample Editor & Sample Recorder Fixes (All 3 Devices)**:
	* **Sample Loader Folder, Import & `Delete Unused` Fixes**: Fixed opening folders with parentheses in their names (such as `Loops (120-140 BPM)`), handled unreadable or deeply nested folders safely, prevented multi-file imports from exceeding Slot 48 or available sample memory, fixed time labels above `100s` on the crop-import screen, fixed renaming instruments after a MIDI/Synth slot was active, and fixed `Delete unused` removing samples only used on the last step of a track, in Pattern 255, or in unsaved edits of the current pattern.
	* **Sample Playback & Sample Editor Fixes**: Fixed `Add` slice jumbling slice markers after `Slice Evenly` or `Auto Slice`, improved zoomed waveform rendering when switching from a long sample to a short one, fixed the Sample Playback progress cursor lingering or tracking a previous instrument's long release tail after switching instruments, fixed low-quality (resampled) samples playing at the wrong speed/pitch during Sample Editor effect preview, and prevented MIDI/Synth slots from interfering with Sample Playback controls and MIDI CC.
	* **Sample Recorder & FM Radio Fixes**: Pressing `Record` again now cancels a count-in waiting on a missing external clock, project samples reload reliably if recording is interrupted by standby or USB Storage, the previously selected instrument is always restored on exit, and FM radio tuning/scanning on Original Tracker and Tracker+ was hardened with tune timeouts, I2C bus recovery, seek state-machine guards, and exact channel stepping to prevent radio scanning freezes.
	* **`Analog Kick Gen` Click Fix**: Removed an audible click at the end of the kick decay in the `Analog Kick Gen` Sample Editor generator.

* **File Screen, Navigation & File Safety Fixes (All 3 Devices)**:
	* **File Screen & Project Saving Reliability**: Fixed `Save As` and `Export` freezing if a folder could not be created (such as on a full or write-protected SD card), prevented `Cancel` in `Save As` from triggering an earlier `New` or `Open` action later on, closed lingering file lists after `Overwrite` and `Save` prompts, and ensured renaming a pattern marks the project as modified so the new name is saved.
	* **Navigation & Synth Patch Editor Stability**: Preserved "return to previous screen" behavior after brief standby taps or USB Storage sessions, and prevented removing the SD card during `Init patch` on Tracker+ / Tracker Mini from locking up the controls.
	* **Sample Recorder & File Loading Hardening**: Fixed a rare freeze when stopping the Sample Recorder as sample memory fills up, improved `.wav` compatibility for custom wavetable envelopes with extra metadata headers, and added safety checks when loading damaged project, pattern, or instrument files so corrupted files fail gracefully instead of crashing.
	* **Workspace Power-Loss Recovery & Project/Pattern/Instrument Load Hardening**: A zero-length pattern file left behind by a power loss or empty battery during autosave now loads as an empty pattern instead of failing the project load and wiping the `Workspace` on the next boot. In addition, loading projects, patterns, and instruments now validates and clamps out-of-range tempos, pattern numbers, track lengths (`<= 128` steps), wavetable window sizes (`<= 2048`), synth part settings, legacy project structure versions, long project paths/names, unterminated instrument sample names, and low-byte-rate legacy `.wav` conversions so damaged files cannot cause boot hangs or memory corruption.
