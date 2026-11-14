#include <string.h>

#include "DisplayList.h"
#include "FT812.h"

#include "DisplayDriver.h"
#include "Platform.h"
#include "Themes.h"
#include "TrackerImpl.h"
#include "display.h"
#include "elapsedMillis.h"

#include "polyassert.h"

// FONTS
#include "RobotoMono-Bold_17_L4.h"
#include "RobotoMono-Light_14_L4.h"
#include "RobotoMono-Light_17_L4.h"
#include "RobotoMono-Regular_12_L4.h"
#include "RobotoMono-Regular_14_L4.h"

#include "LiberationSans-Bold_14_L4.h"
#include "LiberationSans-Bold_17_L4.h"
#include "LiberationSans-Regular_12_L4.h"
#include "LiberationSans-Regular_14_L4.h"
#include "LiberationSans-Regular_17_L4.h"

// BITMAPS
#ifndef TRACKER_MINI
#include "images/poly_logo_160x172_L8.h"
#else
#include "images/poly_logo_172x172_L8.h"
#endif
#include "CompressionRLE.h"
#include "images/arrowIcons.h"
#include "images/battery_28x13_L4.h"
#include "images/battery_ch_28x19_L4.h"
#include "images/songIcons.h"
#include "log.h"
#include <cr_section_macros.h>

#ifdef FEATURE_SYNTH
#include "images/synth_env_18x14_L4.h"
#include "images/synth_filter_18x13_L4.h"
#include "images/synth_gear_18x18_L4.h"
#include "images/synth_lfo_20x13_L4.h"
#include "images/synth_macro_14x14_L4.h"
#include "images/synth_mod_22x12_L4.h"
#include "images/synth_osc_20x12_L4.h"

#include "images/synth_FX_20x18_L4.h"
#include "images/synth_cymbal_20x16_L4.h"
#include "images/synth_hihat_20x16_L4.h"
#include "images/synth_kick_16x19_L4.h"
#include "images/synth_mixer_16x14_L4.h"
#include "images/synth_percussion_16x16_L4.h"
#include "images/synth_snare_20x16_L4.h"
#include "images/synth_tom_20x16_L4.h"

#include "images/synth_shaper_18x18_L4.h"
#endif

static constexpr int kMaxFontBufferSize = 22396;
__NOINIT(EXTERNAL_RAM) uint8_t poly_logo_160x172_l8[27520];
__NOINIT(EXTERNAL_RAM) uint8_t decompressedFontsCommonBuffer[kMaxFontBufferSize];

const uint8_t displayFontCount = 5;

// clang-format off

const CompressedFontData fontsData2[displayFontCount] =
{
    {
      LiberationSans_Regular_14_L4_compressed,
      sizeof(LiberationSans_Regular_14_L4_compressed) / sizeof(LiberationSans_Regular_14_L4_compressed[0])
    },
    {
      LiberationSans_Regular_17_L4_compressed,
      sizeof(LiberationSans_Regular_17_L4_compressed) / sizeof(LiberationSans_Regular_17_L4_compressed[0])
    },
    {
      LiberationSans_Bold_14_L4_compressed,
      sizeof(LiberationSans_Bold_14_L4_compressed) / sizeof(LiberationSans_Bold_14_L4_compressed[0])
    },
    {
      LiberationSans_Bold_17_L4_compressed,
      sizeof(LiberationSans_Bold_17_L4_compressed) / sizeof(LiberationSans_Bold_17_L4_compressed[0])
    },
    {
      LiberationSans_Regular_12_L4_compressed,
      sizeof(LiberationSans_Regular_12_L4_compressed) / sizeof(LiberationSans_Regular_12_L4_compressed[0])
    },
};

const CompressedFontData fontsData1[displayFontCount] =
{
    {
      Roboto_Mono_Light_14_L4_compressed,
      sizeof(Roboto_Mono_Light_14_L4_compressed) / sizeof(Roboto_Mono_Light_14_L4_compressed[0])
    },
    {
      Roboto_Mono_Light_17_L4_compressed,
      sizeof(Roboto_Mono_Light_17_L4_compressed) / sizeof(Roboto_Mono_Light_17_L4_compressed[0])
    },
    {
      Roboto_Mono_Regular_14_L4_compressed,
      sizeof(Roboto_Mono_Regular_14_L4_compressed) / sizeof(Roboto_Mono_Regular_14_L4_compressed[0])
    },
    {
      Roboto_Mono_Bold_17_L4_compressed,
      sizeof(Roboto_Mono_Bold_17_L4_compressed) / sizeof(Roboto_Mono_Bold_17_L4_compressed[0])
    },
    {
      Roboto_Mono_Regular_12_L4_compressed,
      sizeof(Roboto_Mono_Regular_12_L4_compressed) / sizeof(Roboto_Mono_Regular_12_L4_compressed[0])
    },
};

// clang-format on
__NOINIT(EXTERNAL_RAM)
strFont fonts[displayFontCount];

const strBitmap bitmaps[displayBitmapsCount] = {
#ifndef TRACKER_MINI
    {
      poly_logo_160x172_l8,
      80000,
      27520,
      160,
      172,
      L8,
      160,
    },
#else
    {
      poly_logo_172x172_l8,
      80000,
      29584,
      172,
      172,
      L8,
      172,
    },
#endif

    { playIcon20x20, 110000, 200, 20, 20, L4, 10 },
    { loopIcon20x20, 110500, 200, 20, 20, L4, 10 },

    { doubleArrowL, 111000, 135, 18, 15, L4, 9 },
    { doubleArrowR, 111150, 135, 18, 15, L4, 9 },
    { arrowU, 111300, 64, 16, 8, L4, 8 },
    { arrowD, 111400, 64, 16, 8, L4, 8 },
    { battery_28x13_L4, 111500, 182, 28, 13, L4, 14 },
    { battery_ch_28x19_L4, 111700, 266, 28, 19, L4, 14 },

#ifdef FEATURE_SYNTH
    { synth_env_18x14_L4, 112000, 126, 18, 14, L4, 9 },
    { synth_filter_18x13_L4, 112200, 117, 18, 13, L4, 9 },
    { synth_gear_18x18_L4, 112400, 162, 18, 18, L4, 9 },
    { synth_lfo_20x13_L4, 112600, 130, 20, 13, L4, 10 },
    { synth_macro_14x14_L4, 112800, 98, 14, 14, L4, 7 },
    { synth_mod_22x12_L4, 113000, 132, 22, 12, L4, 11 },
    { synth_osc_20x12_L4, 113200, 120, 20, 12, L4, 10 },

    { synth_FX_20x18_L4, 113400, 180, 20, 18, L4, 10 },
    { synth_cymbal_20x16_L4, 113600, 160, 20, 16, L4, 10 },
    { synth_hihat_20x16_L4, 113800, 160, 20, 16, L4, 10 },
    { synth_kick_16x19_L4, 114000, 152, 16, 19, L4, 8 },
    { synth_mixer_16x14_L4, 114200, 112, 16, 14, L4, 8 },
    { synth_percussion_16x16_L4, 114400, 128, 16, 16, L4, 8 },
    { synth_snare_20x16_L4, 114600, 160, 20, 16, L4, 10 },
    { synth_tom_20x16_L4, 114800, 160, 20, 16, L4, 10 },

    { synth_shaper_18x18_L4, 115000, 162, 18, 18, L4, 9 },
#endif
};

uint32_t ImageViewerJpegAddress = 350000;

namespace
{
constexpr uint32_t kFragmentWords = 2048;
constexpr uint32_t kFrameWords = 2048;
// The most the controls of one frame may take, as before: the chip's list
// holds 8192 bytes and the frame adds four words of its own.
constexpr uint32_t kFrameControlBytes = 8172;
// Limits on what one pass of the main loop spends on the display: under
// half a millisecond of sending, or no new control build once a millisecond
// has gone into building.
constexpr uint32_t kUploadWordsPerPass = 320;
constexpr uint32_t kBuildBudgetUs = 1000;
// A stretch of at least this many words is worth sending in the background
// where the driver can do that.
constexpr uint32_t kBackgroundWords = 48;
constexpr uint8_t kFirstRomFont = 16;
constexpr uint8_t kLastRomFont = 31;
constexpr uint8_t kRomFontsKept = 2;
constexpr uint32_t kRomFontTablePointer = 0x2FFFFC;
constexpr uint32_t kRomFontBlockSize = 148;
// Room for the display lists of all controls that exist at one time, shown
// or not. A full frame is at most 2043 words; the rest is for hidden controls
// and for the holes that lists leave behind when they grow.
#ifndef DISPLAY_ARENA_WORDS
#define DISPLAY_ARENA_WORDS 14336
#endif
constexpr uint32_t kArenaWords = DISPLAY_ARENA_WORDS;

// External RAM because DTC RAM is full; all of this is only touched from the
// main loop.
__NOINIT(EXTERNAL_RAM) uint32_t arenaWords[kArenaWords];
__NOINIT(EXTERNAL_RAM) uint32_t buildWords[kFragmentWords];
__NOINIT(EXTERNAL_RAM) uint32_t frameWords[kFrameWords];
// What the chip's two display-list buffers hold. It keeps both, shows one
// and lets the other be written, so a new frame only has to send what
// differs from the frame before the one on screen.
__NOINIT(EXTERNAL_RAM) uint32_t chipLists[2][kFrameWords];
uint8_t romFontMetrics[kRomFontsKept][displayList::kFontMetricsSize];
uint8_t romFontKept[kRomFontsKept];
uint8_t romFontNext = 0;

// The display lists of the controls, packed into one block of memory. A list
// that grows beyond its place moves to the end; when the end is reached, the
// lists are packed again.
class FragmentArena
{
  public:
    int8_t take()
    {
        for (uint8_t i = 0; i < controlsCount; i++)
        {
            if (!taken[i])
            {
                taken[i] = true;
                size[i] = 0;
                capacity[i] = 0;
                return i;
            }
        }
        return -1;
    }

    void release(int8_t slot)
    {
        if (slot >= 0)
        {
            taken[slot] = false;
            size[slot] = 0;
            capacity[slot] = 0;
        }
    }

    // Returns false when the list cannot be kept; the slot is then empty.
    bool store(int8_t slot, const uint32_t* words, uint16_t count)
    {
        if (slot < 0)
        {
            return false;
        }
        if (count > capacity[slot])
        {
            capacity[slot] = 0;
            size[slot] = 0;
            // Lists that grew once tend to grow again, by a little.
            uint32_t wanted = count + count / 8;
            if (used + wanted > kArenaWords)
            {
                pack();
            }
            if (used + wanted > kArenaWords)
            {
                wanted = count;
            }
            if (used + wanted > kArenaWords)
            {
                return false;
            }
            offset[slot] = used;
            capacity[slot] = wanted;
            used += wanted;
        }
        memcpy(&arenaWords[offset[slot]], words, count * sizeof(uint32_t));
        size[slot] = count;
        return true;
    }

    const uint32_t* words(int8_t slot) const { return &arenaWords[offset[slot]]; }
    uint16_t count(int8_t slot) const { return slot >= 0 ? size[slot] : 0; }

  private:
    void pack()
    {
        uint32_t packed = 0;
        uint32_t from = 0;
        while (true)
        {
            int8_t next = -1;
            for (uint8_t i = 0; i < controlsCount; i++)
            {
                bool live = taken[i] && capacity[i] != 0 && offset[i] >= from;
                if (live && (next < 0 || offset[i] < offset[next]))
                {
                    next = i;
                }
            }
            if (next < 0)
            {
                break;
            }
            from = offset[next] + capacity[next];
            memmove(&arenaWords[packed], &arenaWords[offset[next]], size[next] * sizeof(uint32_t));
            offset[next] = packed;
            capacity[next] = size[next];
            packed += size[next];
        }
        used = packed;
    }

    uint32_t used = 0;
    bool taken[controlsCount] = {};
    uint16_t offset[controlsCount] = {};
    uint16_t size[controlsCount] = {};
    uint16_t capacity[controlsCount] = {};
};

FragmentArena fragments;
} // namespace

strGetProps getProps;
cDisplay display;
FileSystem::FileHandle imgFile;

cDisplay::cDisplay()
{
    theme = &themeDefault;
}

void cDisplay::loadFont(const CompressedFontData* fontsData, uint8_t fontCount)
{
    uint16_t char_num;
    for (uint8_t i = 0; i < fontCount; i++)
    {
        uint8_t* decompressedFontData = decompressFont(&fontsData[i]);

        char_num = 0;
        for (int cha = 0; cha < 128; cha++)
        {
            if (decompressedFontData[cha])
            {
                char_num++;
            }
        }

        memcpy(fonts[i].data, decompressedFontData, sizeof(fonts[i].data));
        fonts[i].format = decompressedFontData[128];
        fonts[i].linestride = decompressedFontData[132];
        fonts[i].width = decompressedFontData[136];
        fonts[i].height = decompressedFontData[140];

        fonts[i].size = char_num * fonts[i].linestride * fonts[i].height + 148;
        fonts[i].handle = i + 1;

        fonts[i].address = (i == 0 ? 0 : fonts[i - 1].address + fonts[i - 1].size);
        int rest = fonts[i].address % 4;
        fonts[i].address = fonts[i].address + (4 - rest);

        fonts[i].source = fonts[i].address + 148;

        displayDriver->writeFontRAMG(decompressedFontData, fonts[i].size, fonts[i].address);
    }

    bindFonts();
}

// Points the bitmap handles of the fonts at their glyphs. The chip keeps
// what a displayed list sets for a handle, so one blank frame does it.
void cDisplay::bindFonts()
{
    beginScreen();

    API_CLEAR_COLOR(0x000000);
    API_CLEAR(1, 1, 1);

    for (uint8_t i = 0; i < displayFontCount; i++)
    {
        uint8_t firstCharacter = 0;
        while (!fonts[i].data[firstCharacter] && firstCharacter < 127)
        {
            firstCharacter++;
        }

        // The same words as the coprocessor's CMD_SETFONT2 writes: the
        // source is moved back by the glyphs the font leaves out at its
        // start, so that a cell number is a character code.
        const int32_t source =
          fonts[i].source - firstCharacter * fonts[i].linestride * fonts[i].height;
        API_BITMAP_HANDLE(fonts[i].handle);
        API_BITMAP_SOURCE((uint32_t)source);
        API_BITMAP_LAYOUT_H(0, 0);
        API_BITMAP_LAYOUT(fonts[i].format, fonts[i].linestride, fonts[i].height);
        API_BITMAP_SIZE_H(0, 0);
        API_BITMAP_SIZE(NEAREST, BORDER, BORDER, fonts[i].width, fonts[i].height);
    }

    showScreen();

    for (hControl handle = 0; handle < controlsCount; handle++)
    {
        if (controlsTable[handle] != nullptr && controlsTable[handle]->refresh)
        {
            refreshControl(handle);
        }
    }
}

void cDisplay::beginScreen()
{
    while (!displayDriver->isDisplayListWriteDone())
    {
    }
    if (uploadSwaps)
    {
        // A frame of the controls was on its way. Part of it may have
        // reached the chip, so what its list holds is no longer known.
        uploadSwaps = false;
        uploadRun = uploadRunCount;
        forgetChipLists();
        composePending = true;
    }
    displayList::capture(frameWords, kFrameWords - 1);
}

void cDisplay::showScreen()
{
    uint32_t count = displayList::finish();
    frameWords[count++] = FT_DISPLAY();
    composedWords = count;

    waitForChip();
    displayDriver->writeDisplayList(0, frameWords, count);
    memcpy(chipLists[backList], frameWords, count * 4);
    if (count > chipListWords[backList])
    {
        chipListWords[backList] = count;
    }
    displayDriver->swapDisplayList();
    swapInFlight = true;
    swapPolledAt = timer->millis();
    backList ^= 1;
}

// Blocks until the list memory may be written: the coprocessor is done with
// any list of its own and the last swap has happened.
void cDisplay::waitForChip()
{
    // A swap happens at the panel's next frame. With the panel switched off
    // there is none; do not hang then.
    constexpr uint32_t kSwapTimeoutMs = 100;

    if (displayList::coprocessorDrew)
    {
        displayDriver->awaitCoProEmpty();
        displayList::coprocessorDrew = false;
        forgetChipLists();
        swapInFlight = true;
    }
    const uint32_t startedMs = timer->millis();
    while (swapInFlight && displayDriver->isDisplayListSwapPending() &&
           timer->millis() - startedMs < kSwapTimeoutMs)
    {
    }
    swapInFlight = false;
}

void cDisplay::begin(DisplayDriver* displayDriver, Timer* timer)
{
    this->displayDriver = displayDriver;
    cDisplayControl::setDisplayDriver(displayDriver);

    this->timer = timer;

    refreshQueueTop = 0;
    refreshQueueBott = 0;
    composePending = false;
    frameBuilding = false;
    uploadNext = 0;
    uploadRun = 0;
    uploadRunCount = 0;
    uploadSwaps = false;
    swapInFlight = false;
    swapPolledAt = 0;
    backList = 0;
    forgetChipLists();
    displayList::setFontMetrics(fontMetrics);
    setTheme(0);

    // ####################################
#ifndef TRACKER_MINI
    decompress(poly_logo_160x172_l8_compressed,
               sizeof(poly_logo_160x172_l8_compressed) / sizeof(poly_logo_160x172_l8_compressed[0]),
               poly_logo_160x172_l8);
#endif

    for (uint8_t i = 0; i < displayBitmapsCount; i++)
    {
        displayDriver->writeDataRAMG(bitmaps[i].data, bitmaps[i].source, bitmaps[i].address);
    }

    loadFont(fontsData1, displayFontCount);

    // ####################################

    beginScreen();

    API_CLEAR_COLOR(0x000000);
    API_CLEAR(1, 1, 1);

    API_VERTEX_FORMAT(0);

    constexpr int bitmap_index = displayPolyLogoBitmap;

    API_SAVE_CONTEXT();

    API_BITMAP_HANDLE(0);
    API_BITMAP_SOURCE(bitmaps[bitmap_index].address);
    API_BITMAP_LAYOUT(
      bitmaps[bitmap_index].format, bitmaps[bitmap_index].linestride, bitmaps[bitmap_index].height);
    API_BITMAP_SIZE(
      NEAREST, BORDER, BORDER, bitmaps[bitmap_index].width, bitmaps[bitmap_index].height);

    API_BEGIN(BITMAPS);
    API_VERTEX2F((800 / 2) - (bitmaps[bitmap_index].width / 2), 100);
    API_END();

    API_RESTORE_CONTEXT();

    showScreen();
}

//=================================================================================================
//=================================================================================================

// elapsedMillis printTajner;

void cDisplay::update()
{
    if (!state)
        return;

    if (updateDisplaySettings)
    {
        updateDisplaySettings = 0;

        if (backlightBrightness != lastBacklightBrightness)
        {
            lastBacklightBrightness = backlightBrightness;
            displayDriver->setBrightness(backlightBrightness);
        }
        if (rotateValue != lastRotateValue)
        {
            lastRotateValue = rotateValue;
            displayDriver->setRotation(rotateValue);
        }
    }

    if (screenShotscheduled && timer->millis() > screenShotTimer)
    {
        screenShotscheduled = false;
        processDisplayUpdateImmediate();
        doScreenShot();
    }

    if (loadImage) // todo
    {
        FileSystem* fileSystem = TrackerImpl::getInstance()->getPlatform()->getFileSystem();

        if (!displayDriver->isCoProEmpty())
            return;

        if (img.status == 1)
        {
            if (fileSystem->getFileAvailable(imgFile))
            {
                int32_t readBytes = 0;
                uint8_t currentBuffer[imgBufforSize];
                readBytes = fileSystem->readFile(currentBuffer, imgBufforSize, imgFile);

                if (readBytes < 0)
                {
                    fileSystem->closeFile(imgFile, false);
                    img.status = 0;
                    img.loadProgress = 0;
                    loadImage = 0;
                    return;
                }

                if (img.loadProgress == 0)
                {
                    displayDriver->beginCoProListNoCheck();
                    API_CMD_LOADIMAGE(img.address, OPT_NODL);
                    displayDriver->endCoProList();
                }

                if (readBytes < imgBufforSize)
                {
                    // eof
                    displayDriver->writeDataToCMD(currentBuffer, readBytes);
                    // API_LIB_WriteDataRAMG(currentBuffer, readBytes, img.address +
                    // img.loadProgress*imgBufforSize);
                    fileSystem->closeFile(imgFile, false);
                    img.status = 2;
                }
                else
                {
                    displayDriver->writeDataToCMD(currentBuffer, imgBufforSize);
                    // API_LIB_WriteDataRAMG(currentBuffer, imgBufforSize, img.address +
                    // img.loadProgress*imgBufforSize);
                }
                // API_LIB_WriteDataToCMD(currentBuffer,imgBufforSize);
                // API_CMD_LOADIMAGE(img.address, OPT_NODL);

                img.loadProgress++;

                return;
            }
            else
            {
                fileSystem->closeFile(imgFile, false);
                img.status = 0;
                img.loadProgress = 0;
                loadImage = 0;
            }
        }
        else if (img.status == 2)
        {
            displayDriver->beginCoProList();
            API_CMD_GETPROPS(0, 0, 0); // 3 dummy 32-bit values
            displayDriver->endCoProList();
            displayDriver->awaitCoProEmpty();

            displayDriver->getImgProps(getProps.End_Address, getProps.Width, getProps.Height);

            img.status = 3;
            loadImage = 0;
        }
    }

    //=================================================================================================
    //=================================================================================================


    if (uploadSwaps)
    {
        continueUpload(kUploadWordsPerPass);
        return;
    }
    // Nothing to draw: leave the chip alone.
    if (!frameBuilding && refreshQueueTop == refreshQueueBott && !composePending)
    {
        return;
    }
    if (!frameBuilding && !readyForFrame())
    {
        return;
    }
    // Sending starts on the next pass, so that this one is not held up by
    // both building and sending.
    renderFrame(kBuildBudgetUs, 0);
}

const uint8_t* cDisplay::fontMetrics(uint8_t handle)
{
    if (handle >= 1 && handle <= displayFontCount)
    {
        return fonts[handle - 1].data;
    }
    if (handle < kFirstRomFont || handle > kLastRomFont)
    {
        return nullptr;
    }
    for (uint8_t i = 0; i < kRomFontsKept; i++)
    {
        if (romFontKept[i] == handle)
        {
            return romFontMetrics[i];
        }
    }
    uint8_t kept = romFontNext;
    romFontNext = (romFontNext + 1) % kRomFontsKept;
    uint8_t pointer[4];
    display.displayDriver->readDataRAMG(pointer, sizeof(pointer), kRomFontTablePointer);
    uint32_t table =
      pointer[0] | (pointer[1] << 8) | (pointer[2] << 16) | ((uint32_t)pointer[3] << 24);
    display.displayDriver->readDataRAMG(romFontMetrics[kept],
                                        sizeof(romFontMetrics[kept]),
                                        table + (handle - kFirstRomFont) * kRomFontBlockSize);
    romFontKept[kept] = handle;
    return romFontMetrics[kept];
}


const uint32_t* cDisplay::composedFrame(uint32_t& words) const
{
    words = composedWords;
    return frameWords;
}

void cDisplay::forgetChipLists()
{
    chipListWords[0] = 0;
    chipListWords[1] = 0;
}

// The list memory may be written once the coprocessor is done with any list
// of its own and the previous swap has happened.
bool cDisplay::readyForFrame()
{
    if (displayList::coprocessorDrew)
    {
        if (!displayDriver->isCoProEmpty())
        {
            return false;
        }
        displayList::coprocessorDrew = false;
        forgetChipLists();
        swapInFlight = true;
    }
    if (swapInFlight)
    {
        // A swap waits for the panel's next frame, up to 20 ms away; asking
        // the chip on every pass of the main loop would cost more than the
        // rest of the loop.
        uint32_t nowMs = timer->millis();
        if (nowMs == swapPolledAt)
        {
            return false;
        }
        swapPolledAt = nowMs;
        bool pending = displayDriver->isDisplayListSwapPending();
        if (pending)
        {
            return false;
        }
        swapInFlight = false;
    }
    return true;
}

// Builds the controls that wait in the queue, composes the frame and starts
// to send it. The work is spread over passes of the main loop so that no pass
// is held up for long; pass no limits to do it all at once.
void cDisplay::renderFrame(uint32_t buildBudgetUs, uint32_t uploadWords)
{
    if (!frameBuilding)
    {
        // A control that asks to be refreshed again while it is built (an
        // animation) waits for the next frame.
        frameQueueEnd = refreshQueueTop;
        frameBuilding = true;
    }
    const uint32_t startedUs = timer->micros();
    while (refreshQueueBott != frameQueueEnd)
    {
        if (timer->micros() - startedUs >= buildBudgetUs)
        {
            return;
        }
        hControl handle = refreshQueue[refreshQueueBott];
        refreshQueueBott++;
        if (refreshQueueBott >= controlsRefreshQueueSize)
        {
            refreshQueueBott = 0;
        }
        if (handle >= 0 && handle < controlsCount && controlsTable[handle] != nullptr)
        {
            buildControl(handle);
        }
    }
    frameBuilding = false;
    composePending = false;

    // The chip may have become busy while the controls were built.
    if (displayList::coprocessorDrew)
    {
        composePending = true;
        return;
    }

    uint32_t words = composeFrame();
    showFrame(words, uploadWords);
}

void cDisplay::buildControl(hControl handle)
{
    cDisplayControl* control = controlsTable[handle];

    displayList::capture(buildWords, kFragmentWords);
    control->update();
    if (control->stagesCount > 1)
    {
        control->update2();
    }
    if (control->stagesCount > 2)
    {
        control->update3();
    }
    if (control->stagesCount > 3)
    {
        control->update4();
    }
    if (control->stagesCount > 4)
    {
        control->update5();
    }
    uint16_t words = displayList::finish();
    if (!fragments.store(control->fragment, buildWords, words))
    {
        displayList::overflows++;
        words = 0;
    }
    control->ramSize = words * 4;
    control->refresh = 1;

    bool shown = (control->style & controlStyleShow) != 0;
    if (control->highPrioRefresh || (control->autoRefresh && shown))
    {
        refreshControl(handle);
    }
}

uint32_t cDisplay::composeFrame()
{
    uint32_t count = 0;
    frameWords[count++] = (2UL << 24) | (theme->backgroundColor & 0xFFFFFF); // CLEAR_COLOR_RGB
    frameWords[count++] = CLEAR(1, 1, 1);
    frameWords[count++] = VERTEX_FORMAT(0);

    // Controls are drawn in the reverse of the order they were created in.
    uint32_t controlBytes = 0;
    for (int8_t i = controlsCount - 1; i >= 0; i--)
    {
        cDisplayControl* control = controlsTable[i];
        if (control == nullptr || !(control->style & controlStyleShow) || !control->refresh)
        {
            continue;
        }
        if (controlBytes + control->ramSize > kFrameControlBytes)
        {
            // Left out, as before; smaller controls after it may still fit.
            displayList::overflows++;
            continue;
        }
        memcpy(&frameWords[count], fragments.words(control->fragment), control->ramSize);
        count += control->ramSize / 4;
        controlBytes += control->ramSize;
    }

    frameWords[count++] = FT_DISPLAY();
    composedWords = count;
    return count;
}

void cDisplay::showFrame(uint32_t count, uint32_t uploadWords)
{
    const uint8_t frontList = backList ^ 1;
    if (chipListWords[frontList] >= count &&
        memcmp(frameWords, chipLists[frontList], count * 4) == 0)
    {
        // Already on screen. Animated controls often rebuild to the same
        // picture.
        return;
    }

    findChangedRuns(count);
    uploadRun = 0;
    uploadNext = uploadRunCount > 0 ? uploadRuns[0].start : 0;
    uploadSwaps = true;
    continueUpload(uploadWords);
}

// Lists the stretches of the frame that differ from what the chip's back
// list holds, and brings the record of that list up to date. Stretches with
// only a few equal words between them are sent as one: starting another
// costs about as much as sending four words.
void cDisplay::findChangedRuns(uint32_t count)
{
    constexpr uint32_t kJoinGap = 6;
    uint32_t* known = chipLists[backList];
    const uint32_t knownWords = chipListWords[backList];

    uploadRunCount = 0;
    uint32_t i = 0;
    while (i < count)
    {
        if (i < knownWords && frameWords[i] == known[i])
        {
            i++;
            continue;
        }
        uint32_t end = i + 1;
        uint32_t equalWords = 0;
        const bool lastRun = uploadRunCount == kMaxUploadRuns - 1;
        for (uint32_t j = end; j < count && (lastRun || equalWords < kJoinGap); j++)
        {
            if (j < knownWords && frameWords[j] == known[j])
            {
                equalWords++;
            }
            else
            {
                equalWords = 0;
                end = j + 1;
            }
        }
        uploadRuns[uploadRunCount].start = i;
        uploadRuns[uploadRunCount].end = end;
        uploadRunCount++;
        memcpy(&known[i], &frameWords[i], (end - i) * 4);
        i = end;
    }
    if (count > knownWords)
    {
        chipListWords[backList] = count;
    }
}

// Sends the next part of the frame to the list the chip does not show, and
// swaps the lists after the last part.
void cDisplay::continueUpload(uint32_t maxWords)
{
    if (displayList::coprocessorDrew)
    {
        // The coprocessor has written to the same list in the meantime.
        // Start over once it is done.
        uploadRun = uploadRunCount;
        uploadSwaps = false;
        forgetChipLists();
        composePending = true;
        return;
    }

    if (!displayDriver->isDisplayListWriteDone())
    {
        return;
    }

    bool background = displayDriver->writesDisplayListInBackground();
    while (uploadRun < uploadRunCount && maxWords > 0)
    {
        const uint32_t end = uploadRuns[uploadRun].end;
        uint32_t words = end - uploadNext;
        if (background && words >= kBackgroundWords)
        {
            // Goes out on its own; the next pass looks whether it is done.
            displayDriver->startDisplayListWrite(uploadNext * 4, &frameWords[uploadNext], words);
            maxWords = 0;
        }
        else
        {
            if (words > maxWords)
            {
                words = maxWords;
            }
            displayDriver->writeDisplayList(uploadNext * 4, &frameWords[uploadNext], words);
            maxWords -= words;
        }
        uploadNext += words;
        if (uploadNext == end)
        {
            uploadRun++;
            uploadNext = uploadRun < uploadRunCount ? uploadRuns[uploadRun].start : 0;
        }
    }
    if (uploadRun == uploadRunCount && uploadSwaps && displayDriver->isDisplayListWriteDone())
    {
        uploadSwaps = false;
        displayDriver->swapDisplayList();
        swapInFlight = true;
        swapPolledAt = timer->millis();
        backList ^= 1;
    }
}

void cDisplay::setControlPosition(hControl handle, int16_t x, int16_t y)
{
    if (handle < 0 || handle >= controlsCount || controlsTable[handle] == nullptr)
        return;

    if (x >= 0)
        controlsTable[handle]->posX = x;
    if (y >= 0)
        controlsTable[handle]->posY = y;
}

void cDisplay::setControlSize(hControl handle, int16_t w, int16_t h)
{
    if (handle < 0 || handle >= controlsCount || controlsTable[handle] == nullptr)
        return;

    if (w >= 0)
        controlsTable[handle]->width = w;
    if (h >= 0)
        controlsTable[handle]->height = h;
}

void cDisplay::setControlStyle(hControl handle, uint32_t style)
{
    if (handle < 0 || handle >= controlsCount || controlsTable[handle] == nullptr)
        return;
    // sprawdzenie porpawnosci czcionki
    if (((style >> 8) & 15) > displayFontCount)
    {
        style &= ~(15 << 8);
    }

    // controlsTable[handle]->cStyle = style;
    controlsTable[handle]->setStyle(style);
}

void cDisplay::setControlStyle2(hControl handle, uint32_t style)
{
    if (handle < 0 || handle >= controlsCount || controlsTable[handle] == nullptr)
        return;
    // sprawdzenie porpawnosci czcionki
    if (((style >> 8) & 15) > displayFontCount)
    {
        style &= ~(15 << 8);
    }

    // controlsTable[handle]->cStyle = style;
    ((cLabel*)controlsTable[handle])->setStyle2(style);
}

void cDisplay::setControlShow(hControl handle)
{
    if (handle < 0 || handle >= controlsCount || controlsTable[handle] == nullptr)
        return;

    controlsTable[handle]->style |= (controlStyleShow);

    // uint32_t temp_style = controlsTable[handle]->style;
    // temp_style &= ~(controlStyleShow);
    // controlsTable[handle]->setStyle(style);
}

void cDisplay::setControlHide(hControl handle)
{
    if (handle < 0 || handle >= controlsCount || controlsTable[handle] == nullptr)
        return;

    controlsTable[handle]->style &= ~(controlStyleShow);
}

void cDisplay::setAddControlStyle(hControl handle, uint32_t style)
{
    if (handle < 0 || handle >= controlsCount || controlsTable[handle] == nullptr)
        return;

    uint32_t temp_style = controlsTable[handle]->style;
    temp_style |= (style);
    controlsTable[handle]->setStyle(temp_style);
}

void cDisplay::setRemoveControlStyle(hControl handle, uint32_t style)
{
    if (handle < 0 || handle >= controlsCount || controlsTable[handle] == nullptr)
        return;

    uint32_t temp_style = controlsTable[handle]->style;
    temp_style &= ~(style);
    controlsTable[handle]->setStyle(temp_style);
}

void cDisplay::setControlText(hControl handle, const char* text)
{
    if (handle < 0 || handle >= controlsCount || controlsTable[handle] == nullptr)
        return;
    controlsTable[handle]->setText(text);
}

void cDisplay::setControlText2(hControl handle, const char* text)
{
    if (handle < 0 || handle >= controlsCount || controlsTable[handle] == nullptr)
        return;
    ((cLabel*)controlsTable[handle])->setText2(text);
}

void cDisplay::setControlValue(hControl handle, int value)
{
    if (handle < 0 || handle >= controlsCount || controlsTable[handle] == nullptr)
        return;
    controlsTable[handle]->setValue(value);
}

void cDisplay::setControlColors(hControl handle, const uint32_t colorsTable[])
{
    if (handle < 0 || handle >= controlsCount || controlsTable[handle] == nullptr)
        return;
    for (uint8_t i = 0; i < controlsTable[handle]->colorsCount; i++)
        if (colorsTable + i == nullptr || colorsTable[i] > 0xFFFFFF)
            return;

    controlsTable[handle]->setColors(colorsTable);
}

void cDisplay::setControlData(hControl handle, void* data)
{
    if (handle < 0 || handle >= controlsCount || controlsTable[handle] == nullptr)
        return;
    controlsTable[handle]->setData(data);
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cDisplay::setModulesControlDestroyPoint()
{
    int8_t found = -1;

    for (uint8_t i = 0; i < controlsCount; i++)
    {
        found = i; // punkt jako nastepna kontrolka w tablicy
        if (controlsTable[i] == nullptr)
            break;
    }

    destroyPoint = found;
}

void cDisplay::destroyModulesControls()
{
    if (destroyPoint < 0 || destroyPoint >= controlsCount)
        return;

    for (uint8_t i = destroyPoint; i < controlsCount; i++)
    {
        releaseFragment(controlsTable[i]);
        delete controlsTable[i];
        controlsTable[i] = nullptr;
    }
}

void cDisplay::freeControlResources(int8_t controlsTableIndex)
{
    if (controlsTableIndex < 0 || controlsTableIndex > controlsCount - 1)
        return;

    // The controls above this one move down a place.
    releaseFragment(controlsTable[controlsTableIndex]);
    delete controlsTable[controlsTableIndex];
    controlsTable[controlsTableIndex] = nullptr;

    // przesun wszystkie wskazniki w tablicy powyzej kasowanego
    for (int8_t i = controlsTableIndex; i < controlsCount - 1; i++)
    {
        controlsTable[i] = controlsTable[i + 1];
    }

    controlsTable[controlsCount - 1] = nullptr; // ostatnia pozycja tablicy moze byc zerowana
}
int8_t cDisplay::takeFragment()
{
    return fragments.take();
}

void cDisplay::releaseFragment(cDisplayControl* control)
{
    if (control != nullptr)
    {
        fragments.release(control->fragment);
    }
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cDisplay::destroyControl(hControl handle)
{
    if (handle < 0 || handle >= controlsCount || controlsTable[handle] == nullptr)
        return;

    freeControlResources(handle);
}

void cDisplay::refreshControl(hControl handle)
{
    if (handle < 0 || handle >= controlsCount || controlsTable[handle] == nullptr)
        return;

    // dorzuc kontrolke do kolejki fifo odtwarzania
    // jesli juz jest w kolejce to nic nie rob
    // jesli aktualnie odswiezana - zresetuj proces odswiezania

    // przeszukaj kolejke
    uint8_t i = refreshQueueBott;
    //
    if (refreshQueue[i] == handle)
    {
        if (refreshQueueTop != i)
        {
            i++;
            if (i >= controlsRefreshQueueSize)
                i = 0;
        }
    }
    // przeszukaj reszte kolejki
    while (i != refreshQueueTop) // ryzykowne ale optymalne
    {
        if (handle == refreshQueue[i])
        {
            return; // znaleziono w kolejce - wyjdz
        }
        i++;
        if (i >= controlsRefreshQueueSize)
            i = 0;
    }

    // dodaj kontorlke do kolejki
    refreshQueue[refreshQueueTop] = handle;
    refreshQueueTop++;
    if (refreshQueueTop >= controlsRefreshQueueSize)
    {
        refreshQueueTop = 0;
    }
}

// All queued controls are built before a frame is composed, so there is no
// partial state for callers to hold back any more.
void cDisplay::synchronizeRefresh() {}

void cDisplay::setClearRefreshFlag(hControl handle)
{
    if (handle < 0 || handle >= controlsCount || controlsTable[handle] == nullptr)
        return;

    controlsTable[handle]->refresh = 0;
}

void cDisplay::resetControlQueue()
{
    refreshQueueTop = 0;
    refreshQueueBott = 0;
    frameBuilding = false;
}

uint8_t cDisplay::isIdle()
{
    return isControlsRefreshProcessIdle();
}

void cDisplay::forceAppedStage()
{
    composePending = true;
}

// Puts everything that is waiting on screen before returning, for callers
// that are about to keep the main loop busy for a while.
void cDisplay::processDisplayUpdateImmediate()
{
    if (!state || isControlsRefreshProcessIdle())
    {
        return;
    }
    while (uploadSwaps)
    {
        continueUpload(kFrameWords);
    }
    if (!frameBuilding && refreshQueueTop == refreshQueueBott && !composePending)
    {
        return;
    }
    waitForChip();
    renderFrame(UINT32_MAX, kFrameWords);
    while (uploadSwaps)
    {
        continueUpload(kFrameWords);
    }
}

bool cDisplay::isControlsRefreshProcessIdle()
{
    return refreshQueueTop == refreshQueueBott && !composePending && !frameBuilding &&
           !uploadSwaps;
}
//=====================================================================================================
// jpg/png
//=====================================================================================================
uint8_t cDisplay::readImgFromSd(char* path, uint32_t address, int16_t max_w, int16_t max_h)
{
    FileSystem* fileSystem = TrackerImpl::getInstance()->getPlatform()->getFileSystem();

    strcat(path, ".jpg");

    if (!fileSystem->exists(path))
    {
        path[strlen(path) - 3] = 0;
        strcat(path, "jpeg");

        if (!fileSystem->exists(path))
        {
            return 1;
            // path[strlen(path)-4] = 0;
            // strcat(path,"png");
            // if(!SD.exists(path))
            //{
            //	return;
            //}
        }
    }

    if (img.status == 1 || img.status == 2)
    {
        return 1;
    }

    auto [isOpen, fileHandle] =
      fileSystem->openFile(path, FileSystem ::FileOpenMode::kRead, &imgFile);

    if (!isOpen)
    {
        return 1;
    }

    int32_t readBytes = 0;
    uint8_t currentBuffer[5000];
    readBytes = fileSystem->readFile(currentBuffer, 5000, imgFile);

    if (readBytes < 0)
    {
        fileSystem->closeFile(imgFile, false);
        return 1;
    }

    uint16_t width = 0;
    uint16_t height = 0;

    if (get_jpeg_size(currentBuffer, readBytes, &width, &height) == 0)
    {
        fileSystem->closeFile(imgFile, false);
        return 1;
    }

    if (width == 0 || width > max_w || height == 0 || height > max_h)
    {
        fileSystem->closeFile(imgFile, false);
        return 1;
    }

    img.size = fileSystem->getFileSize(imgFile);
    img.address = address;

    if (img.size > 1000000 - address)
    {
        fileSystem->closeFile(imgFile, false);
        return 1;
    }

    fileSystem->seekFile(0, imgFile);

    img.type = 0;
    img.loadProgress = 0;
    img.progressMax = img.size / imgBufforSize;
    img.status = 1;

    loadImage = 1;

    return 0;
}

void cDisplay::readImgFromMemory(uint8_t* data, uint32_t size) // todo
{
    loadImage = 1;

    img.type = 0;
    img.loadProgress = 0;
    img.data = data;
    img.size = size;
    img.progressMax = img.size / imgBufforSize;
    img.status = 1;
}

//=====================================================================================================
// grupowe
//=====================================================================================================
void cDisplay::hideAllControls()
{
    for (uint8_t i = 0; i < controlsCount; i++)
    {
        if (controlsTable[i] != nullptr)
        {
            controlsTable[i]->style &= ~(controlStyleShow);
        }
    }
}

void cDisplay::clear()
{
    beginScreen();

    API_CLEAR_COLOR(0x000000);
    API_CLEAR(1, 1, 1);

    showScreen();
}
//=====================================================================================================
// debug
//=====================================================================================================

void cDisplay::printActualControlTableContent()
{
    LOG("");
    LOG("-----------------------------------------------------------");
    LOGF("%s", "   CONTROLS TABLE");
    LOGF("%s", "       DP: ");
    LOGF("%d\n", destroyPoint);
    LOG("-----------------------------------------------------------");
    LOG("      id                 show  style     ram size    refresh");

    char idString[20];
    char logString[100];

    for (uint8_t i = 0; i < controlsCount; i++)
    {
        if (controlsTable[i] == nullptr)
        {
            LOG("");
            continue;
        }

        controlIdToString(controlsTable[i]->id, idString);

        sprintf(logString,
                " %-3u| %-17s| %-4u| %-8u| %-10u| %-5u|",
                i,
                idString,
                (unsigned int)controlsTable[i]->style & 1,
                (unsigned int)controlsTable[i]->style,
                (unsigned int)controlsTable[i]->ramSize,
                (unsigned int)controlsTable[i]->refresh);

        LOG(logString);
    }

    LOG("");
}

void cDisplay::controlIdToString(uint8_t id, char* string)
{
    switch (id)
    {
        case controlIdBar: strcpy(string, "bar"); break;
        case controlIdBgLabel: strcpy(string, "bgLabel"); break;
        case controlIdFrame: strcpy(string, "frame"); break;
        case controlIdEdit: strcpy(string, "edit"); break;
        case controlIdImg: strcpy(string, "img"); break;
        case controlIdLabel: strcpy(string, "label"); break;
        case controlIdHorizontalBar: strcpy(string, "horizontalBar"); break;
        case controlIdLineIndicator: strcpy(string, "lineIndicator"); break;
        case controlIdList: strcpy(string, "list"); break;
        case controlIdKeyboard: strcpy(string, "keyboard"); break;
        case controlIdNotePopup: strcpy(string, "notePopup"); break;
        case controlIdMultiLabel: strcpy(string, "multiLabel"); break;
        case controlIdParamValueList: strcpy(string, "paramValueList"); break;
        case controlIdPoints: strcpy(string, "waveformPoints"); break;
        case controlIdProcessingPopup: strcpy(string, "processingPopup"); break;
        case controlIdProgressCursor: strcpy(string, "progressCoursor"); break;
        case controlIdSimpleText: strcpy(string, "simpleText"); break;
        case controlIdSlicePoints: strcpy(string, "slicePoints"); break;
        case controlIdSongPlayer: strcpy(string, "player"); break;
        case controlIdSpectrum: strcpy(string, "waveform"); break;
        case controlIdStartScreen: strcpy(string, "startScreen"); break;
        case controlIdTemplate: strcpy(string, "template"); break;
        case controlIdTextBox: strcpy(string, "textBox"); break;
        case controlIdTextPopup: strcpy(string, "textPopup"); break;
        case controlIdTracker: strcpy(string, "tracker"); break;
        case controlIdWtProgress: strcpy(string, "progress"); break;
        case controlIdNotesKeyboardLabel: strcpy(string, "notesKeyboard"); break;
        default: strcpy(string, "undefined"); break;
    }
}

//=====================================================================================================
// hardware
//=====================================================================================================
void cDisplay::setBacklightBrightness(uint8_t value)
{
    //
    backlightBrightness = value;
    updateDisplaySettings = 1;
}

void cDisplay::setRotate(uint8_t value)
{
    //
    rotateValue = value;
    updateDisplaySettings = 1;
}

void cDisplay::turnOn()
{
    displayDriver->setPowerState(true);
}

void cDisplay::turnOff()
{
    displayDriver->setPowerState(false);
}

void cDisplay::setTheme(uint8_t index)
{
    switch (index)
    {
        case 0: theme = &themeDefault; break;
        case 1: theme = &themeWhiteout; break;
        case 2: theme = &themeAnniversary; break;
        default: theme = &themeDefault; break;
    }
}

void cDisplay::setCustomTheme(const Theme& customTheme)
{
    themeCustom = customTheme;
    theme = &themeCustom;
}

#pragma pack(push, 1)
struct PtFontHeader
{
    char magic[4];       // "PTFN"
    uint16_t version;    // 1
    uint16_t fontCount;  // 5
    uint8_t reserved[8];
};

struct PtFontEntry
{
    uint32_t offset;
    uint32_t size;
    uint16_t handle;
    uint16_t format;
};
#pragma pack(pop)

bool cDisplay::loadCustomFontFromSD(FileSystem* fs, const char* fontFilePath)
{
    if (!fs || !fontFilePath)
        return false;

    auto [isOpen, file] = fs->openFile(fontFilePath, FileSystem::FileOpenMode::kRead);
    if (!isOpen || !file)
    {
        const char* alt = (fontFilePath[0] == '/') ? fontFilePath + 1 : fontFilePath;
        std::tie(isOpen, file) = fs->openFile(alt, FileSystem::FileOpenMode::kRead);
    }
    if (!isOpen || !file)
        return false;

    PtFontHeader header;
    if (fs->readFile(&header, sizeof(header), file) != static_cast<int32_t>(sizeof(header)))
    {
        fs->closeFile(file);
        return false;
    }

    if (memcmp(header.magic, "PTFN", 4) != 0 || header.version != 1 || header.fontCount != displayFontCount)
    {
        fs->closeFile(file);
        return false;
    }

    PtFontEntry entries[displayFontCount];
    if (fs->readFile(entries, sizeof(entries), file) != static_cast<int32_t>(sizeof(entries)))
    {
        fs->closeFile(file);
        return false;
    }

    for (uint8_t i = 0; i < displayFontCount; i++)
    {
        if (entries[i].size < 148 || entries[i].size > kMaxFontBufferSize)
        {
            fs->closeFile(file);
            return false;
        }
        if (entries[i].size < sizeof(fonts[i].data))
        {
            memset(decompressedFontsCommonBuffer, 0, sizeof(fonts[i].data));
        }

        fs->seekFile(entries[i].offset, file);
        if (fs->readFile(decompressedFontsCommonBuffer, entries[i].size, file) !=
            static_cast<int32_t>(entries[i].size))
        {
            fs->closeFile(file);
            return false;
        }

        uint16_t char_num = 0;
        for (int cha = 0; cha < 128; cha++)
        {
            if (decompressedFontsCommonBuffer[cha])
            {
                char_num++;
            }
        }

        memcpy(fonts[i].data, decompressedFontsCommonBuffer, sizeof(fonts[i].data));
        fonts[i].format = decompressedFontsCommonBuffer[128];
        fonts[i].linestride = decompressedFontsCommonBuffer[132];
        fonts[i].width = decompressedFontsCommonBuffer[136];
        fonts[i].height = decompressedFontsCommonBuffer[140];

        fonts[i].size = char_num * fonts[i].linestride * fonts[i].height + 148;
        fonts[i].handle = i + 1;

        fonts[i].address = (i == 0 ? 0 : fonts[i - 1].address + fonts[i - 1].size);
        int rest = fonts[i].address % 4;
        if (rest != 0)
        {
            fonts[i].address = fonts[i].address + (4 - rest);
        }

        fonts[i].source = fonts[i].address + 148;

        displayDriver->writeFontRAMG(decompressedFontsCommonBuffer, fonts[i].size, fonts[i].address);
    }

    fs->closeFile(file);

    bindFonts();

    return true;
}

void cDisplay::setFont(uint8_t index)
{
    switch (index)
    {
        case 0: loadFont(fontsData1, displayFontCount); break;
        case 1: loadFont(fontsData2, displayFontCount); break;
        default: loadFont(fontsData1, displayFontCount); break;
    }
}

void cDisplay::showBatteryScreen(bool empty, bool charging, int8_t level, float voltage)
{
    beginScreen();

    API_CLEAR_COLOR(0x000000);
    API_CLEAR(1, 1, 1);

    API_VERTEX_FORMAT(0);

    API_COLOR(0xFFFFFF);

    if (empty)
    {
        API_CMD_TEXT(
          400, 285, 27, OPT_CENTER, charging ? "Low battery, charging..." : "Low battery");
    }
    else
    {
        if (charging)
        {
            API_CMD_TEXT(400, 285, 27, OPT_CENTER, "Charging...");
        }
#if defined(DEBUG)
        char text[50];
        sprintf(text, "Battery level: %d%%, voltage: %.2fV", level, voltage);
        API_CMD_TEXT(400, 320, 29, OPT_CENTER, text);
#endif
    }

    const uint16_t batW = 75;
    const uint16_t batH = 30;
    const uint16_t batX = (800 - batW) / 2;
    const uint16_t batY = (480 - batH) / 2;

    API_COLOR(level >= 20 ? 0x00FF00 : 0xFF0000);
    API_BEGIN(RECTS);
    API_VERTEX2F(batX, batY + 1);
    API_VERTEX2F(batX + (((batW - 4) * level) / 100), batY + (batH - 2));
    API_END();

    API_COLOR(0x888888);
    API_BEGIN(LINE_STRIP);
    API_VERTEX2F(batX, batY);
    API_VERTEX2F(batX + batW, batY);
    API_VERTEX2F(batX + batW, batY + batH);
    API_VERTEX2F(batX, batY + batH);
    API_VERTEX2F(batX, batY);
    API_END();
    API_BEGIN(RECTS);
    API_VERTEX2F(batX + batW, batY + (batH / 3));
    API_VERTEX2F(batX + batW + (batH / 6), batY + batH - (batH / 3));
    API_END();

    showScreen();
}

uint8_t* cDisplay::decompressFont(const CompressedFontData* compressedFontData)
{
    decompress(compressedFontData->data, compressedFontData->count, decompressedFontsCommonBuffer);

    return decompressedFontsCommonBuffer;
}
