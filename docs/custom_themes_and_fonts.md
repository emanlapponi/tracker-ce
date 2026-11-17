# Polyend Tracker Custom Themes & Fonts Guide

## Table of Contents

1. [Overview](#1-overview)
2. [Installing Custom Themes](#2-installing-custom-themes)
   - [File Location](#file-location)
   - [Selecting a Theme](#selecting-a-theme)
3. [Web based Theme Editor](#3-web-based-visual-theme-editor)
   - [Running the Theme Editor](#running-the-theme-editor)
4. [Technical Specifications & Limitations for Creators](#4-technical-specifications--limitations-for-creators)
   - [File Naming & Display Names](#file-naming--display-names)
   - [Capacity Limits](#capacity-limits)
   - [Color Format Requirements](#color-format-requirements)
5. [Memory Footprint & System Impact](#5-memory-footprint--system-impact)
6. [Theme File Format Reference](#6-theme-file-format-reference)
   - [Minimal Theme (Overrides Only)](#minimal-theme-overrides-only)
   - [Key Reference Table](#key-reference-table)
7. [Installing Custom Fonts](#7-installing-custom-fonts)
   - [File Location](#file-location-1)
   - [Selecting a Font](#selecting-a-font)
8. [Converting TTF/OTF Fonts to `.ptfont`](#8-converting-ttfotf-fonts-to-ptfont)

---

## 1. Overview
The Polyend Tracker firmware supports fully customizable color themes (via JSON files on your SD card) and custom fonts (via `.ptfont` packages converted from any standard TTF/OTF font).

---

## 2. Installing Custom Themes

### File Location
Place your theme files in the `/Themes/` folder at the root of your SD card:
```
SD Card
└── Themes/
    ├── Amiga.json
    ├── BernsteinAmber.json
    ├── Cyberpunk.json
    ├── SolarizedDark.json
    ├── TerminalGreen.json
    └── MyCustomTheme.json
```
*(If the `/Themes/` folder doesn't exist, the Tracker creates it automatically upon entering the Config menu).*

### Selecting a Theme
1. On your Tracker, open **Config** (`Shift + Master` or via Menu).
2. Scroll to **Display theme**.
3. Turn the jog wheel to cycle through built-in themes (`Original`, `Monochrome`, `Anniversary`) and your custom themes from `/Themes/`.

---

## 3. Web based visual Theme-Editor

You can use the visual theme and font editor directly in your browser at **[`https://polyend.github.io/tracker-theme-editor/`](https://polyend.github.io/tracker-theme-editor/)** (source code available in the companion [`polyend/tracker-theme-editor`](https://github.com/polyend/tracker-theme-editor) repository):

* **Real-time 800×480 FT812 LCD Preview**: Accurately reproduces the Polyend Tracker hardware screen.
* **Multi-Screen Simulation**: Switch between **Pattern Editor**, **Song Arranger**, **Sample / Waveform Editor**, **Notes Grid**, and **Config / Dialogs**.
* **Click-to-Inspect**: Click directly on any element in the Tracker screen (notes, instruments, playhead, top bar, buttons, waveform) to immediately highlight and edit its color in the sidebar.
* **Built-in Presets**: Quick-load Amiga, Terminal Green (CRT Phosphor), Bernstein Amber (Warm CRT), Cyberpunk (Neon), Solarized Dark, Original Tracker, Monochrome, and more.
* **1-Click Import & Export**: Import existing `.json` theme files, or click **Export Theme JSON** to download a ready-to-use file directly into `/Themes/`.
* **Integrated Custom Font Converter (Font Editor)**: Upload any TTF or OTF font directly in the browser to preview all 5 FT812 font sizes, test live on the Tracker screen simulator, and compile `.ptfont` binary packages with 1 click.

### Running the Theme Editor Locally (Optional)
If you want to run or modify the editor locally, clone the [`polyend/tracker-theme-editor`](https://github.com/polyend/tracker-theme-editor) repository and run:
```bash
git clone https://github.com/polyend/tracker-theme-editor.git
cd tracker-theme-editor
npm install
npm run dev
```
Open the local URL (typically `http://localhost:5173`) in your browser to start customizing.

---

## 4. Technical Specifications & Limitations for Creators

### File Naming & Display Names
* **File Extensions**: Theme files must end in `.json` (case-insensitive: `.json` or `.JSON`). Font files must end in `.ptfont`.
* **Display Name**: The Tracker automatically strips the `.json` or `.ptfont` extension to display the theme/font name in the Config menu. For example, `Cyberpunk.json` appears as `Cyberpunk`.
* **Character Limit**: Filenames / theme display names can be up to **31 characters** long (plus the null terminator). Longer names will be safely truncated at 31 characters.
* **Supported Characters**: Standard ASCII alphanumeric characters, dashes (`-`), and underscores (`_`) are recommended. Spaces are supported on FAT32/exFAT SD cards.

### Capacity Limits
* **Maximum Custom Themes**: Up to **29 custom themes** can be indexed from `/Themes/` (allowing **32 total themes** in the menu including the 3 built-in presets: `Original`, `Monochrome`, and `Anniversary`).
* **Maximum Custom Fonts**: Up to **30 custom fonts** can be indexed from `/Fonts/` (allowing **32 total fonts** in the menu including `Original` and `New`).
* Files beyond the 32-entry limit are simply ignored; no crash or error occurs.
* **Theme File Size**: A complete theme file is typically ~2 to 4 KB. The firmware reads up to **8 KB** per theme file, which is more than enough for all color configurations.

### Color Format Requirements
* Colors must be 6-digit hex values prefixed with `#` (e.g. `"#FFAA25"`, `"#ff007f"`, `"#22D6B8"`).
* Both uppercase and lowercase hex characters (`a-f` / `A-F`) are accepted.
* Short `#RGB` (3-digit), raw decimal integers, and alpha values are **not permitted**.

---

## 5. Memory Footprint & System Impact

A common question is whether loading custom themes affects available audio sample RAM, synth voices, or real-time performance.

* **Audio Sample Memory & Polyphony Impact: 0%**
  * The Tracker reserves dedicated memory blocks for the audio pool (Sample RAM) and synthesizer voice engines.
  * Theme manager buffers reside strictly outside the audio buffers in external SDRAM.
* **Critical Fast SRAM (DTCM / ITCM): 0 Bytes used**
  * On-chip tightly-coupled memory is used strictly for low-latency DSP and audio rendering. The theme manager uses none of it.
* **SDRAM Footprint (No Dynamic Heap Allocation):**
  * Theme indexing structures, filename strings, pointers, and the theme read buffer live statically in `__NOINIT(EXTERNAL_RAM)` (see [`docs/memory_map.md`](memory_map.md#5-subsystem--asset-capacity-reference) for exact sizes), meaning **zero dynamic heap allocation (`malloc`/`new`)** occurs during theme loading and zero heap fragmentation is possible.
* **CPU / DSP Impact: None during playback**
  * Themes are parsed and loaded only when selected in the Config menu or upon device boot. During playback and editing, widgets draw using directly cached integer color arrays with zero parsing overhead.

---

## 6. Theme File Format Reference

Themes are standard JSON text files with human-readable keys and `#RRGGBB` hex colors.

### Minimal Theme (Overrides Only)
You do not need to specify every color. Any omitted color will automatically inherit from the `"base"` theme (`"Original"`, `"Monochrome"`, or `"Anniversary"`):

```json
{
  "name": "Minimal Blue",
  "base": "Original",
  "background": "#0c1017",
  "patternTracker": {
    "background": "#0e131d",
    "playhead": "#1f6feb",
    "selection": "#238636",
    "note": "#58a6ff",
    "instrument": "#e3b341"
  },
  "accents": {
    "red": "#f85149",
    "green": "#2ea043",
    "cyan": "#388bfd"
  }
}
```

### Key Reference Table

| Section | Keys | Description |
| :--- | :--- | :--- |
| **Root** | `name`, `base`, `background` | Theme name, fallback base theme, main screen background color. |
| **`topBar`** | `text`, `background`, `frame`, `text2` | Header / title bar colors. |
| **`songTimer`** | `text`, `background`, `frame`, `text2` | Song timer widget. |
| **`labels`** | `active`, `inactive`, `activeButton`, `inactiveButton`, `highlightedButton`, `redButton`, `performActiveTrack`, `performMutedTrack`, `defaultText` | Buttons, text labels, and performance indicators across all screens. |
| **`patternTracker`** | `trackName`, `background`, `note`, `instrument`, `fx1`, `fx2`, `inactiveNote`, `inactiveInstrument`, `inactiveFx1`, `inactiveFx2`, `selection`, `gridDivider`, `inactiveTrack`, `playhead`, `selectionText`, `verticalDivider`, `stepNumber`, `greenMode`, `activeTrack`, `trackEnd` | Main tracker sequencing grid. |
| **`songPlayer`** | `text`, `activeBlock`, `scrollbar`, `background`, `inactiveText`, `editMode`, `scrollbarBackground`, `playhead`, `emptyTrack`, `playheadActive` | Song arranger screen. |
| **`lists`** | `active`, `inactive`, `projectActive`, `sampleLoader`, `sampleLoaderInactive` | Scrollable lists (samples, projects, files). |
| **`bars`** | `active`, `inactive`, `sampleLoaderNormal`, `sampleLoaderFull` | Progress bars and level meters. |
| **`popups`** | `stepTitle`, `stepBackground`, `stepDescription`, `note`, `noteKeyboard`, `dialog`, `progressPopup`, `keyboard`, `miniMenu` | Dialogs, keyboards, and overlays. |
| **`miniMenu`** | `text`, `boxBackground`, `selectedBox`, `background`, `frame`, `inactiveDot`, `activeDot` | Tracker Mini hardware Menu button overlay (4x3 modules grid). |
| **`waveform`** | `audio`, `background`, `progress`, `sliceMarker`, `activeSlice`, `slicePlayhead`, `granular`, `points` | Sample playback, sample editor, and granular screens. |
| **`accents`** | `white`, `red`, `orange`, `yellow`, `green`, `cyan`, `grey` | Primary accent palette colors. |

---

## 7. Installing Custom Fonts

### File Location
Place `.ptfont` font packages in the `/Fonts/` folder on your SD card:
```
SD Card
└── Fonts/
    ├── AdwaitaMono.ptfont
    └── HackMono.ptfont
```

### Selecting a Font
1. In the **Config** menu, navigate to **Display font**.
2. Turn the jog wheel to cycle through `Original`, `New`, and your custom `.ptfont` fonts from `/Fonts/`.

---

## 8. Converting TTF/OTF Fonts to `.ptfont`

The Tracker's display coprocessor (FT812) uses a custom 4-bit per pixel luminance format across 5 fixed sizes/weights:
* Size 14 Regular
* Size 17 Regular
* Size 14 Bold
* Size 17 Bold
* Size 12 Regular

Font conversion is built directly into the **[Web Theme & Font Editor](https://polyend.github.io/tracker-theme-editor/)** ([`polyend/tracker-theme-editor`](https://github.com/polyend/tracker-theme-editor)):

1. Open the **[Theme Editor](https://polyend.github.io/tracker-theme-editor/)** in your browser (or run `npm run dev` locally inside `tracker-theme-editor`).
2. Click **Font Editor** in the top navigation bar.
3. Drop your `.ttf` or `.otf` font file (supports single font files with synthetic bold generation or separate Regular and Bold files).
4. Preview the font across all 5 sizes and test live on simulated Tracker screens.
5. Click **Convert & Download .ptfont** and place the generated file into `/Fonts/` on your SD card.
