# Global File Browser Developer Guide (`GlobalFileBrowser`)

**Target Audience:** Firmware and UI contributors building features in `tracker-ce` that need to browse, preview, load, or save SD card files, `.wav` audio, or structured package folders (such as Tracker projects).

The **Global File Browser** ([`core/interface/fileBrowser/GlobalFileBrowser.h`](../core/interface/fileBrowser/GlobalFileBrowser.h), [`core/interface/fileBrowser/GlobalFileBrowser.cpp`](../core/interface/fileBrowser/GlobalFileBrowser.cpp)) provides a single, globally reusable UI module (`cGlobalFileBrowser`) backed by a dedicated `24 KB` scratch window so any subsystem can open a full-featured SD browser in one function call without adding persistent SDRAM/DTC overhead or re-implementing scrolling, confirmation, deletion, keyboard entry, or preview controls.

> [!NOTE]
> **Why the Instrument Sample Loader stays separate:** The Tracker Sample Loader (`Sample Loader` mode for multi-selecting samples into the instrument pool) intentionally remains its own dedicated UI because its multi-selection queue, pool memory telemetry, and batch import workflow differ from single-target file/folder selection. Everything else that loads or saves a single `.wav`, generic file, preset, patch, or package folder uses `GlobalFileBrowser`.

---

## Table of Contents

1. [Overview & Supported Modes](#1-overview--supported-modes)
2. [UI Layout, Navigation & Keybindings](#2-ui-layout-navigation--keybindings)
3. [One-Call Helper API (`GlobalFileBrowser::*`)](#3-one-call-helper-api-globalfilebrowser)
4. [Full Configuration Reference (`GlobalFileBrowserConfig`)](#4-full-configuration-reference-globalfilebrowserconfig)
5. [Memory Budget & FatFs Safety](#5-memory-budget--fatfs-safety)
6. [Existing Firmware Integrations](#6-existing-firmware-integrations)
7. [Implementation Examples](#7-implementation-examples)

---

## 1. Overview & Supported Modes

`GlobalFileBrowser` supports six operational modes via the `GlobalFileBrowserConfig::Mode` enum:

| Mode | Purpose | Selection / Save Target |
| :--- | :--- | :--- |
| `GlobalFileBrowserConfig::Mode::kLoadWav` | Browse and preview `.wav` audio files | `.wav` file |
| `GlobalFileBrowserConfig::Mode::kSaveWav` | Choose a folder and enter a `.wav` filename | Output `.wav` path |
| `GlobalFileBrowserConfig::Mode::kLoadGenericFile` | Browse files matching an extension filter (e.g. `.ptp`, `.mod`, `.it`, `.ir`, or unfiltered) | Matching file |
| `GlobalFileBrowserConfig::Mode::kSaveGenericFile` | Choose a folder and enter a filename with target extension | Output file path |
| `GlobalFileBrowserConfig::Mode::kLoadFolderPackage` | Browse directories containing a specific marker file (such as a Tracker project folder) | Package directory path |
| `GlobalFileBrowserConfig::Mode::kSaveFolderPackage` | Choose a parent folder and enter a new package directory name | Output package directory path |

### Key Built-in Capabilities
* **Per-Context Path & Cursor Memory (`kMaxSavedContexts = 10`):** Pass a short `contextKey` (for example `"fx_ir_load"`, `"synth_patch"`, `"proj_open"`) and the browser automatically remembers the last directory and cursor position across visits (up to 10 LRU contexts).
* **Inline File / Package Inspector:** Displays the highlighted item name plus up to three custom metadata lines returned by `onDescribeItem()`, or built-in WAV header metadata for audio modes.
* **Optional Secondary List (`kMaxSecondaryListItems = 32`):** Displays an alternate quick-pick pane (such as `Recent` items) beside the explorer.
* **Protected Folder & Delete Hooks:** System folders in `kProtectedFolders` are protected from deletion, and callers can gate deletion with `canDelete()` and handle cleanup in `onDelete()`.
* **Audio Auditioning:** `onPreviewStart()`, `onPreviewStop()`, `onHighlightChange()`, and `onPadEvent()` allow patch/WAV auditioning (including Tracker pad auditioning) directly inside the browser.

---

## 2. UI Layout, Navigation & Keybindings

`cGlobalFileBrowser` organizes the screen into two or three focusable panes depending on the active mode:

1. **Explorer (Left/Main Pane):** Scrollable list of folders, package folders, and matching files on the SD card.
   * **`Left` Arrow:** Immediately jumps the cursor back to the top of the current directory list (`browserListPosition = 0` in `cGlobalFileBrowser::handleLeftRight`), matching the quick-return ergonomics of the Sample Loader.
2. **Details or Secondary List (Middle/Right Pane):**
   * When `secondaryListItems` is empty, shows the **Details** inspector (`inspectorText[0..3]`: item title on row 0, and up to 3 lines split on `\n` from `onDescribeItem()` or WAV metadata on rows 1–3).
   * When `secondaryListItems` is populated (up to `32` items), displays the navigable **Secondary List** (title set by `secondaryListTitle`, starting at `initialSecondaryIndex`).
3. **Preview Vol (`kLoadWav` / `kSaveWav` only):** Pressing `Right` moves focus into the preview volume control for WAV auditioning (`kLoadWav` and `kSaveWav` modes only).
4. **Save Here + On-Screen Keyboard (Save Modes):** In `kSaveWav`, `kSaveGenericFile`, and `kSaveFolderPackage`, choosing **Save Here** opens the full-screen text entry keyboard (`openSaveFileKeyboard()`) pre-populated with the default name before invoking `onConfirm(fullPath)`.
5. **Direct Screen Switching (Hardware Module Buttons):** Pressing any top-level screen button (`File`, `Config`, `Song`, `Pattern`, `Instrument Parameters`, `Performance`, `Master`, `Sample Playback`, `Sample Editor`, `Sample Recorder`, `Sample Loader`, `Image Viewer`, or the Tracker Mini `Menu` combo) cleanly cancels the file browser (stopping previews, saving folder/cursor context, and firing `onCancel()`) and switches directly to that screen.

---

## 3. One-Call Helper API (`GlobalFileBrowser::*`)

For common cases where you do not need custom preview/inspector/delete callbacks, include [`core/interface/fileBrowser/GlobalFileBrowser.h`](../core/interface/fileBrowser/GlobalFileBrowser.h) and use one of the convenience helpers in `namespace GlobalFileBrowser`:

```cpp
// Generic entry point with full configuration struct:
void open(const GlobalFileBrowserConfig& config);
void openGlobalFileBrowser(const GlobalFileBrowserConfig& config); // top-level helper

// 1. Load a .wav file
void loadWav(
    std::string title,
    std::string initialDirectory,
    ModuleBase* returnModule,
    std::function<void(const std::string& fullPath)> onConfirm,
    std::string contextKey = "",
    uint32_t returnModuleOptions = 0);

// 2. Save a .wav file
void saveWav(
    std::string title,
    std::string initialDirectory,
    std::string defaultFileName,
    ModuleBase* returnModule,
    std::function<void(const std::string& fullPath)> onConfirm,
    std::string contextKey = "",
    uint32_t returnModuleOptions = 0);

// 3. Load a file filtered by extension (e.g. ".ptp", ".ir", or "" for all files)
void loadFile(
    std::string title,
    std::string initialDirectory,
    std::string extensionFilter,
    ModuleBase* returnModule,
    std::function<void(const std::string& fullPath)> onConfirm,
    std::string contextKey = "",
    uint32_t returnModuleOptions = 0);

// 4. Save a file with extension filter
void saveFile(
    std::string title,
    std::string initialDirectory,
    std::string defaultFileName,
    std::string extensionFilter,
    ModuleBase* returnModule,
    std::function<void(const std::string& fullPath)> onConfirm,
    std::string contextKey = "",
    uint32_t returnModuleOptions = 0);

// 5. Load a folder package identified by a marker file
void loadPackageFolder(
    std::string title,
    std::string initialDirectory,
    std::string markerFile,
    ModuleBase* returnModule,
    std::function<void(const std::string& fullPath)> onConfirm,
    std::string contextKey = "",
    uint32_t returnModuleOptions = 0);

// 6. Save a new folder package
void savePackageFolder(
    std::string title,
    std::string initialDirectory,
    std::string defaultName,
    std::string markerFile,
    ModuleBase* returnModule,
    std::function<void(const std::string& fullPath)> onConfirm,
    std::string contextKey = "",
    uint32_t returnModuleOptions = 0);
```

Notice the parameter order in every convenience helper: **`title`** comes first, followed by **`initialDirectory`**, the mode-specific default/filter/marker string (if applicable), **`returnModule`**, **`onConfirm`**, optional **`contextKey`**, and optional **`returnModuleOptions`**.

---

## 4. Full Configuration Reference (`GlobalFileBrowserConfig`)

Pass a `GlobalFileBrowserConfig` struct to `GlobalFileBrowser::open(cfg)` (or `openGlobalFileBrowser(cfg)`) when you need custom previews, dynamic inspector details, pad auditioning, a secondary list, or deletion handling:

### Core Navigation & Filtering
* `mode`: One of `GlobalFileBrowserConfig::Mode::kLoadWav`, `kSaveWav`, `kLoadGenericFile`, `kSaveGenericFile`, `kLoadFolderPackage`, or `kSaveFolderPackage`.
* `title`: Top header text shown in the browser UI.
* `confirmButtonLabel`: Optional label override for the primary confirm action button (defaults to context-appropriate `Load` / `Save` / etc.).
* `initialDirectory`: Starting SD path (overridden when a saved directory exists for `contextKey`).
* `initialSelectedName`: Optional leaf filename or item name to highlight initially on open.
* `notifyHighlightOnOpen`: Controls whether opening immediately triggers `onHighlightChange` for the initial selection.
* `contextKey`: Unique identifier (up to `10` active LRU contexts stored) to remember the user's last folder and cursor index across browser openings.
* `extensionFilter`: Extension string for generic file modes (for example `".ptp"`).
* `markerFile`: Filename checked inside subdirectories when running in package folder modes.
* `defaultFileName`: Pre-filled filename when opening the Save keyboard.
* `returnModule` / `returnModuleOptions`: Destination `ModuleBase*` and flags passed to the module switcher upon confirm or cancel.

### Secondary List Options
* `secondaryListTitle`: Header title for the optional secondary list pane.
* `secondaryListItems`: List of up to `32` (`kMaxSecondaryListItems`) shortcut/recent labels.
* `initialSecondaryIndex`: Initially highlighted item index in the secondary list.
* `onSecondaryConfirm(int index, const std::string& item)`: Invoked if the user confirms a secondary list entry instead of an Explorer item.

### Callbacks & Hooks
* `onConfirm(const std::string& fullPath)`: Called when the user confirms a file, folder package, or Save destination path.
* `onCancel()`: Called when the user exits without confirming.
* `onHighlightChange(const std::string& fullPath, bool isSelectable)`: Called whenever cursor navigation highlights a new entry (`isSelectable` distinguishes selectable files/packages from regular directories).
* `onPreviewStart(const std::string& fullPath)` / `onPreviewStop()`: Triggered for audio/patch preview start and cleanup (always stop active voices/previews in `onPreviewStop()` and before loading the next asset so notes never hang).
* `onPadEvent(uint8_t pad, uint8_t state, int16_t velo)`: Receives Tracker pad presses while the browser is open (used, for example, by Synth Patch loading to let users audition patches across notes on the pad grid).
* `onDescribeItem(const std::string& fullPath, bool isFolder, bool isPackage) -> std::string`: Returns up to 3 `\n`-separated metadata lines displayed in rows `1..3` of the Details pane (row `0` renders the item name).
* `onFormatItemLabel(const std::string& fullPath, const char* rawName, bool isFolder, bool isPackage) -> std::string`: Optional display-label formatter for Explorer entries (for example, showing a `.ptfx` plugin's pretty `display_name` while preserving the real filename on disk; non-folder entries are sorted alphabetically by their formatted labels).
* `canDelete(const std::string& fullPath, bool isFolder, bool isPackage, std::string& outDenyReason) -> bool`: Return `false` and populate `outDenyReason` to block deletion of active/in-use items (note that protected system directories in `kProtectedFolders` are always guarded).
* `onDelete(const std::string& fullPath, bool isFolder, bool isPackage)`: Called after user confirmation when an item is deleted.

---

## 5. Memory Budget & FatFs Safety

### Shared Scratch Partitioning
`cGlobalFileBrowser` does not allocate permanent large directory buffers in internal SRAM. Instead, it uses a dedicated `kBrowserScratchBytes` slice at the very top of `externalRamScratchBuff` (`SHARED_SCRATCH_MEMORY_SIZE` in [`core/source/mtStructs.h`](../core/source/mtStructs.h)), cleanly above `.ptfx` executable code, working scratch (`kPtfxScratchTopOffset`), and the secondary `.wav` sample buffer — see [`docs/memory_map.md`](memory_map.md#3-shared-768-kb-scratch-buffer-map-externalramscratchbuff) for the full partition map. Because its slice never overlaps any active `.ptfx` buffer, a Sample Editor `.ptfx` effect can open `GlobalFileBrowser` to load an `.ir`, `.wav`, or `.ptp` preset without corrupting the active plugin binary, working scratch RAM, or loaded secondary WAV audio.

> [!IMPORTANT]
> **FatFs File-Handle Lifetime in `onDescribeItem` & `onPreviewStart`:**
> Scrolling quickly through large directories triggers `onDescribeItem()` and `onPreviewStart()` dozens of times in rapid succession. Every callback that opens a file on SD **must close the handle immediately on completion** (using `closeFile(fh)` / `deleteOnClose = true` in the firmware FatFs wrapper so file-descriptor slots are recycled immediately). Never leave handles open across highlights, and consider a small cache (such as the 1-entry `PatchWireInfo` cache used when previewing synth patches) so inspecting and immediately previewing the same file avoids redundant SD re-opens.
>
> **Audio Silencing Before Loading Patches/Samples:**
> Always silence active preview voices (`onPreviewStop()`, or stopping the preview engine/part) **before** parsing and loading the next patch or sample from SD so sustained voices do not glitch or hold frozen tails while synchronous FatFs reads occur.

---

## 6. Existing Firmware Integrations

`GlobalFileBrowser` currently powers all major single-file and package workflows across the firmware:

| Subsystem | Source Location | Browser Modes Used |
| :--- | :--- | :--- |
| **Project Open, Save As & MOD/IT Import** | [`core/interface/projectEditor/fProjEdit.cpp`](../core/interface/projectEditor/fProjEdit.cpp) | `kLoadFolderPackage`, `kSaveFolderPackage`, `kLoadGenericFile` (`.mod` / `.it`) |
| **Synth Patch Load / Save As & Sample Slots** | [`core/interface/instrumentEditor/SynthPatchEditor.cpp`](../core/interface/instrumentEditor/SynthPatchEditor.cpp), [`SynthEditor.cpp`](../core/interface/instrumentEditor/SynthEditor.cpp) | Generic `.patch` load/save with metadata inspector, `onPadEvent` C4 auditioning, voice silencing, and `kLoadWav` for sample-based synth engine slots |
| **Modular `.ptfx` Effect Loader, File Params & `.ptp` Presets** | [`core/interface/sampleEditor/fSampleEdit.cpp`](../core/interface/sampleEditor/fSampleEdit.cpp) | `kLoadGenericFile` (`.ptfx` plugins from `/Effects` with header inspector via **`Load Effect...`**), `kLoadWav` / `kSaveWav` and `kLoadGenericFile` / `kSaveGenericFile` for `PTFX_PARAM_FILE` / `PTFX_PARAM_SAVE_FILE`, plus `.ptp` preset load/save under `/Effects/Presets/<SanitizedPluginName>/` |

---

## 7. Implementation Examples

### Example 1: One-Line Generic File Load
```cpp
#include "core/interface/fileBrowser/GlobalFileBrowser.h"

void openIrPicker(ModuleBase* caller) {
    GlobalFileBrowser::loadFile(
        "Load Impulse Response",
        "/Effects/IRs",
        ".ir",
        caller,
        [](const std::string& fullPath) {
            loadIrFromSd(fullPath);
        },
        "fx_ir_load");
}
```

### Example 2: Save File with Default Filename & Context Key
```cpp
GlobalFileBrowser::saveFile(
    "Save Effect Preset",
    "/Effects/Presets/MyPlugin",
    "InitPreset",
    ".ptp",
    caller,
    [](const std::string& fullPath) {
        writePresetFile(fullPath);
    },
    "ptfx_preset_save");
```

### Example 3: Full Config with Inspector Details, Preview Stop, and Pad Auditioning
```cpp
GlobalFileBrowserConfig cfg;
cfg.mode = GlobalFileBrowserConfig::Mode::kLoadGenericFile;
cfg.title = "Load Synth Patch";
cfg.initialDirectory = "/Patches";
cfg.extensionFilter = ".patch";
cfg.contextKey = "synth_patch_load";
cfg.returnModule = returnMod;

cfg.onDescribeItem = [](const std::string& fullPath, bool isFolder, bool isPackage) -> std::string {
    if (isFolder || isPackage) {
        return {};
    }
    // Always inspect quickly and close FatFs handles immediately before returning
    return inspectPatchSummaryLines(fullPath); // e.g. "Engine: WTFM\nAuthor: Polyend\nSize: 412 B"
};

cfg.onHighlightChange = [](const std::string& fullPath, bool isSelectable) {
    // Stop any running preview voice before loading metadata or next patch
    stopPreviewVoice();
    if (isSelectable) {
        triggerAutoPreview(fullPath);
    }
};

cfg.onPadEvent = [](uint8_t pad, uint8_t state, int16_t velo) {
    forwardPadNoteToPreviewVoice(pad, state, velo);
};

cfg.onPreviewStop = []() {
    stopPreviewVoice();
};

cfg.onConfirm = [](const std::string& fullPath) {
    stopPreviewVoice();
    commitPatchToInstrument(fullPath);
};

GlobalFileBrowser::open(cfg);
```
