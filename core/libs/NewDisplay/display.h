#ifndef LIBS_NEWDISPLAY_DISPLAY_H_
#define LIBS_NEWDISPLAY_DISPLAY_H_

#include <stdint.h>
#include <typeinfo>

#include "CompressionRLE.h"
#include "displayControls.h"

#include "barControl.h"
#include "batteryControl.h"
#include "boxMenuControl.h"
#include "commonControls.h"
#include "gridEditorControl.h"
#include "horizontalBarControl.h"
#include "horizotnalTrackerControl.h"
#include "imgControl.h"
#include "imgGridControl.h"
#include "keyboardControl.h"
#include "lineIndicatorControl.h"
#include "list.h"
#include "multiLabelControl.h"
#include "newListControl.h"
#include "notePopoutControl.h"
#include "notesKeyboard.h"
#include "paramValueList.h"
#include "pointsControl.h"
#include "progressCursorControl.h"
#include "simpleBox.h"
#include "simpleTextControl.h"
#include "sliceControl.h"
#include "songPlayerControl.h"
#include "spectrumControl.h"
#include "startScreenControl.h"
#include "textBoxControl.h"
#include "textPopupControl.h"
#include "trackerControl.h"
#include "wtProgress.h"

#include "Themes.h"
#include "displayStructs.h"

#include "DisplayDriver.h"

// #########################################################################
//							KLASY
// #########################################################################

struct CompressedFontData
{
    const CompressedData* data;
    int count;
};

class FileSystem;

class cDisplay
{
    friend cDebugLog;

  public:
    cDisplay();
    void begin(DisplayDriver* displayDriver, Timer* timer);
    void update();

    template<typename controlClass>
    [[gnu::noinline]] hControl createControl(strControlProperties* properties)
    {
        cDisplayControl** newControlSlot = &controlsTable[0];

        uint8_t limit = 0;

        while (nullptr != *newControlSlot)
        {
            newControlSlot++;
            if (++limit >= controlsCount)
                return -1;
        }

        if (((properties->style >> 8) & 15) > displayFontCount)
        {
            properties->style &= ~(15 << 8);
        }

        *newControlSlot = new controlClass(properties);
        (*newControlSlot)->fragment = takeFragment();

        return limit;
    }

    void destroyControl(hControl handle);
    void refreshControl(hControl handle);
    void synchronizeRefresh();
    void resetControlQueue();
    uint8_t isIdle();
    void forceAppedStage();
    void processDisplayUpdateImmediate();
    bool isControlsRefreshProcessIdle();

    void setControlPosition(hControl handle, int16_t x, int16_t y);
    void setControlSize(hControl handle, int16_t w, int16_t h);
    void setControlStyle(hControl handle, uint32_t style);
    void setControlStyle2(hControl handle, uint32_t style);
    void setControlShow(hControl handle);
    void setControlHide(hControl handle);
    void setAddControlStyle(hControl handle, uint32_t style);
    void setRemoveControlStyle(hControl handle, uint32_t style);
    void setControlText(hControl handle, const char* text);
    void setControlText2(hControl handle, const char* text);

    void setControlValue(hControl handle, int value);
    void setControlColors(hControl handle, const uint32_t colorsTable[]);
    void setControlDefaultColors(hControl handle, uint32_t colorsTable[]);
    void setControlData(hControl handle, void* data);

    void setClearRefreshFlag(hControl handle);

    void setModulesControlDestroyPoint();
    void destroyModulesControls();
    void freeControlResources(int8_t controlsTableIndex);

    // jpg/png
    uint8_t readImgFromSd(char* path, uint32_t address, int16_t max_w, int16_t max_h);
    void readImgFromMemory(uint8_t* data, uint32_t size);
    uint8_t isImgLoaded() { return img.status == 3; }
    uint8_t getImgLoadState() { return img.status; }

    void scheduleScreenShot(int timeSec);
    void doScreenShot();

    void hideAllControls();

    // debug
    void printActualControlTableContent();
    void controlIdToString(uint8_t id, char* string);

    // hardware
    void setBacklightBrightness(uint8_t value);
    void setRotate(uint8_t value);

    void disable() { state = 0; }
    void enable() { state = 1; }
    void clear();

    void turnOn();
    void turnOff();

    void setTheme(uint8_t index);
    void setCustomTheme(const Theme& customTheme);
    void setFont(uint8_t index);
    bool loadCustomFontFromSD(FileSystem* fs, const char* fontFilePath);

    void showBatteryScreen(bool empty, bool charging, int8_t level, float voltage);

    // A frame drawn outside the controls, such as the test screens: the
    // API_* calls between the two make up the frame, and showScreen() puts
    // it on screen before it returns.
    void beginScreen();
    void showScreen();

    DisplayDriver* getDisplayDriver() { return displayDriver; }
    // The frame that was composed last, for checking what reached the chip.
    const uint32_t* composedFrame(uint32_t& words) const;

    Theme& getTheme() { return *((Theme*)theme); }


  private:

    bool readyForFrame();
    void renderFrame(uint32_t buildBudgetUs, uint32_t uploadWords);
    void buildControl(hControl handle);
    uint32_t composeFrame();
    void showFrame(uint32_t count, uint32_t uploadWords);
    void continueUpload(uint32_t maxWords);
    void findChangedRuns(uint32_t count);
    void forgetChipLists();
    void waitForChip();
    void bindFonts();
    int8_t takeFragment();
    void releaseFragment(cDisplayControl* control);
    static const uint8_t* fontMetrics(uint8_t handle);

    void loadFont(const CompressedFontData* fontsData, uint8_t fontCount);
    uint8_t* decompressFont(const CompressedFontData* compressedFontData);

    DisplayDriver* displayDriver;
    Timer* timer;
    const Theme* theme;

    uint8_t state = 1;

    cDisplayControl* controlsTable[controlsCount] = { nullptr };
    // Set when the frame has to be composed again although no control is
    // waiting to be built.
    bool composePending = false;
    uint16_t composedWords = 0;
    // A frame is spread over several passes of the main loop: first the
    // queued controls up to frameQueueEnd are built, then the changed
    // stretches of the composed frame are sent, and then the chip is told to
    // swap its lists.
    static constexpr uint8_t kMaxUploadRuns = 24;
    struct UploadRun
    {
        uint16_t start;
        uint16_t end;
    };
    bool frameBuilding = false;
    uint8_t frameQueueEnd = 0;
    UploadRun uploadRuns[kMaxUploadRuns];
    uint8_t uploadRunCount = 0;
    uint8_t uploadRun = 0;
    uint16_t uploadNext = 0;
    // The frame in frameWords is still on its way to the chip.
    bool uploadSwaps = false;
    bool swapInFlight = false;
    uint32_t swapPolledAt = 0;
    // Which of the chip's two display-list buffers the next frame goes to,
    // and how many words of each are known (see chipLists in display.cpp).
    uint8_t backList = 0;
    uint16_t chipListWords[2] = { 0, 0 };

    hControl refreshQueue[controlsRefreshQueueSize]; // FIFO
    uint8_t refreshQueueTop;
    uint8_t refreshQueueBott;

    int8_t destroyPoint = 0;

    uint32_t memoryUsed = 0;
    uint32_t lastMemoryUsed = 0;

    uint8_t fontsCount = displayFontCount;
    int8_t controls_count = 0;

    uint8_t updateDisplaySettings = 0;

    uint8_t backlightBrightness = 255;
    uint8_t lastBacklightBrightness = 255;

    uint8_t rotateValue = 1;
    uint8_t lastRotateValue = 1;

    // png/jpg
    uint8_t loadImage = 0;

    struct imgLoader
    {
        uint8_t status = 0; // 0-1-2-3
        uint8_t loadProgress = 0;
        uint32_t progressMax = 0;
        uint8_t type;
        uint8_t* data;
        uint32_t size;

        uint32_t address;

    } img;

    bool screenShotscheduled = false;
    uint32_t screenShotTimer = false;
};

extern uint32_t ImageViewerJpegAddress;
extern cDisplay display;

#endif
