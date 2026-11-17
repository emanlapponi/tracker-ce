# Contributing to Polyend Tracker Community Edition

Thank you for contributing to the firmware! Whether you are fixing a bug, creating a new `.ptfx` Sample Editor plugin, adding an open-source synthesizer engine, or improving a musical workflow, following these guidelines keeps the firmware stable across all three hardware devices and makes reviews fast and collaborative.

---

## 1. Project Philosophy: Preserving the Spirit of the Tracker

> **Keep the Spirit of the Original Firmware. Build Upon It, Don't Replace It**
>
> * The goal of **Polyend Tracker CE** is **not** to completely change what the original firmware was or break the workflow that musicians rely on. Instead, we want to **preserve the spirit of the original firmware and thoughtfully improve upon it**.
> * **Broad Community Benefit**: Changes merged into the main Community Edition firmware should benefit the Tracker community at large and fit naturally into the existing UI, button ergonomics, and project structure.
> * **When to Open a Discussion vs. Create a Fork**:
>   * Because RAM (`SRAM_DTC` / `EXTERNAL_RAM`), Flash, and button combinations are shared finite resources, ideas that deviate wildly from the core Tracker workflow or target a very niche use case are often better suited for a **dedicated fork** (or a loadable `.ptfx` effect plugin) rather than the main firmware. **Unless a [GitHub Discussion](https://github.com/polyend/tracker-ce/discussions) or [Polyend Backstage](https://backstage.polyend.com/) discussion shows clear agreement from a majority of users** that it belongs in the main Community Edition.
>   * **Discuss First**: Before investing time coding a major feature, UI redesign, or architectural change, open a **[Polyend Backstage](https://backstage.polyend.com/) topic, [GitHub Discussion](https://github.com/polyend/tracker-ce/discussions), or Issue** first to gather community feedback and align on the approach. You can also connect with fellow users and developers on the unofficial Polyend Tracker Discord, **[The Tracker Overflow](https://discord.com/invite/uEPTnwckJV)**.

---

## 2. Core Pull Request Rules

1. **One Feature or Bug Fix per PR**
   * Keep every PR scoped to **one logical change**.
   * Do not combine multiple features, unrelated bug fixes, or codebase-wide formatting changes into a single PR.

2. **Explain the Goal, Behavior & Test Plan**
   * **Goal**: Why is this change needed? What bug or musical workflow does it address?
   * **Behavior**: How does the feature or fix work from the user's perspective? Document any button combos (including differences between Original/Plus pad/jogwheel controls and Tracker Mini handheld controls), screen layouts, or config options.
   * **How to Test**: Provide step-by-step instructions so any reviewer or community tester can reproduce and verify the behavior on hardware.

3. **Declare Target Devices**
   * Clearly state which devices the feature targets:
     * **Original Polyend Tracker** (`PLATFORM=MK66`, `PRODUCT=ORIGINAL`)
     * **Polyend Tracker+** (`PLATFORM=IMX`, `PRODUCT=PLUS`)
     * **Polyend Tracker Mini** (`PLATFORM=IMX`, `PRODUCT=MINI`)
   * Even if your feature is specific to `IMX` (`#ifdef PLATFORM_IMX`) or `MK66` (`#ifndef PLATFORM_IMX`), **all three firmware targets must compile cleanly via `make all`**.

4. **Declare Hardware Testing Status & Coordinate Community Testing**
   * Most contributors do not own all three hardware units. In your PR description, honestly mark:
     * Which devices you have **tested on physical hardware**.
     * Which devices **need hardware testing from the community**.
   * **Testing PR Builds via GitHub Actions**:
     * Every PR automatically triggers the [GitHub Actions build workflow](.github/workflows/build.yml), which compiles `.ptf` firmware files for all three devices and uploads them as the `polyend-tracker-ce-firmware` artifact on the PR's **Checks** tab.
     * Community testers who own the remaining devices can download the `.ptf` artifact, flash it via **Config > Firmware > Firmware update**, run through your **How to Test** steps, and leave a comment on the PR confirming their results.

---

## 3. Technical Requirements & Safety Checklist

Before submitting your PR, verify the following:

* **Full Build Verification**:
  * Run `make all` (which includes `make psyn`) and `make ptfx` locally and confirm zero compiler or linker errors across Original, Plus, and Mini.
* **Memory Headroom (`SRAM_DTC` / `RAM` & `EXTERNAL_RAM`)**:
  * Check the linker memory output from `make all` (`-print-memory-usage`, also posted automatically in the GitHub Actions job summary and PR firmware comment).
  * Keep large or non-realtime arrays out of default `.bss` (use `__NOINIT(EXTERNAL_RAM)` with explicit startup initialization, or reuse `externalRamScratchBuff` for modal/offline features). See [`docs/memory_map.md`](docs/memory_map.md) for per-region capacities, baseline headroom, scratch buffer partitions, and `SAMPLE_MEMORY_SIZE` reclamation rules.
* **Real-Time Audio Thread Safety**:
  * Never allocate/free heap memory (`new`, `delete`, `malloc`, `free`, `std::vector::push_back`, `std::string`), perform SD card I/O, or block inside audio-rate functions (`update()`, `renderVoiceAudio()`, `renderPostProcessAudio()`, `processBlock()`).
* **Backward Compatibility (`.mt`, `.pti`, `.pts`, `mtConfig`)**:
  * Never change the integer values or ordering of existing serialized enums (such as `InstrumentTypeId` in `synth/include/PolySynth/config.h` or pattern FX IDs) or shift existing fields in project/patch/config structs.
* **Both Desktop & Mini Input ergonomics**:
  * If adding a UI shortcut or screen interaction, verify how it operates on both the **Desktop form factor** (Original Tracker & Tracker+: 48 pads, rotary jog-wheel, dedicated function buttons) and **Tracker Mini** (`#ifdef TRACKER_MINI`: D-pad, `interfaceButtonMiniMiddle`, `[Shift]` combinations).
* **Code Style**:
  * Format only your modified lines/files using [`core/.clang-format`](core/.clang-format).
* **Human Commit Attribution (No AI Bot Trailers)**:
  * Commit authors and `Co-authored-by:` trailers must only reference human contributors. Please strip any `Co-authored-by:` lines added by AI coding tools (such as `Copilot`, `Claude`, `Cursor`, etc.) before opening a PR so the repository's Contributors list reflects human contributors only.
* **License Compliance**:
  * By submitting a PR, you agree that your contribution is licensed under the repository's [`LICENSE`](LICENSE) (MIT License with Wholesale Commercial Clone Restriction for the complete application, and pure MIT License for SDKs, open synth engines, and reusable modules), and that Polyend Sp. z o.o. may also use, distribute, and relicense contributed changes in official Polyend firmware and products. If you include permissively licensed third-party code, add its attribution to [`NOTICE.md`](NOTICE.md).
