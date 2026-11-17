#ifndef INTERFACE_SAMPLERECORDER_H_
#define INTERFACE_SAMPLERECORDER_H_

#include "core/modulesBase.h"
#include "mtKeyboardManager.h"
#include "mtStructs.h"

#ifndef TRACKER_MINI
const uint8_t sourceCount = 9;
const char* const ptrSourcesNamesLabels[9] = {
    (char* const)"Line In (L)", (char* const)"Line In (R)", (char* const)"Line In (M)",
    (char* const)"Line In",     (char* const)"Radio",       (char* const)"Mic LG",
    (char* const)"Mic HG",      (char* const)"USB (M)",     (char* const)"USB",
};
#else
const uint8_t sourceCount = 7;
const char* const ptrSourcesNamesLabels[9] = {
    (char* const)"Line In (L)", (char* const)"Line In (R)", (char* const)"Line In (M)",
    (char* const)"Line In",     (char* const)"Mic",         (char* const)"USB (M)",
    (char* const)"USB",
};
#endif

const uint8_t monitorCount = 2;
const char monitorNamesLabels[monitorCount][5] = {
    "On",
    "Off",
};

constexpr uint8_t undoCount = 5;

struct strRecorderConfig
{
    uint8_t source;
    uint8_t gainLineIn;
    uint8_t gainMicLow;
    uint8_t gainMicHigh;
    uint8_t gainRadio;
    uint8_t gainUsb;
    uint8_t monitor;
    float radioFreq = 87.5;
};

const strRecorderConfig defaultRecorderConfig = { .source = 0,
                                                  .gainLineIn = 0,
                                                  .gainMicLow = 0,
                                                  .gainMicHigh = 0,
                                                  .gainRadio = 0,
                                                  .gainUsb = 0,
                                                  .monitor = 1,
                                                  .radioFreq = 87.5f };

struct strUndoCrop
{
    int16_t* address[SAMPLE_CHANNELS];
    uint32_t length;
    uint16_t startPoint;
    uint16_t endPoint;
};

#undef MAX_SELECT_NODES
#define MAX_SELECT_NODES 2

class Sgtl5000;
class Radio;

class cSampleRecorder : public ModuleBase
{

  public:
    virtual void update();
    virtual void start(uint32_t options);
    virtual void stop();

    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}
    virtual bool wasLastSwitchingSuccessfull() { return selectionWindowFlag == 0; }

    cSampleRecorder()
    {
        //		inActiveInstrumentsCount = 0;
        //		inActiveInstrumentIndex = 0;
        //		openedInstrFromActive = 0;
        //		openedInstrumentIndex = 0;

        radioRdsLabel = -1;
        for (int channel = 0; channel < SAMPLE_CHANNELS; channel++)
        {
            spectrumControl[channel] = -1;
        }
        playhead = -1;
        std::fill(label, label + 8, -1);
        frameControl = -1;
        sourceListControl = -1;
        monitorListControl = -1;
        levelBarControl = -1;
        gainBarControl = -1;
        radioFreqBarControl = -1;
        pointsControl = -1;
    }
    virtual ~cSampleRecorder() {}

    void showDefaultScreen();
    void deactivateGui();
    void showZoomValue();
    void showSourceList();
    void showMonitorList();
    void handleStartupWithPopupAndMenu();

    void showEndPointValue();
    void showStartPointValue();
    void showRecTimeValue();
    void showPreviewValue();
    void hidePreviewValue();
    void showFreqValue();
    void showRadio();
    void hideRadio();

    void setDefaultScreenFunct();
    void setStopSeqFunctions();
    void setConfigScreenFunct();

    void processSpectrum1();
    void processPoints();
    void changeZoom(int16_t value);
    void changeMonitorSelection(int16_t value, bool rollListOver = false);
    void changeSourceSelection(int16_t value, bool rollListOver = false);

    void activateAudioInputSource(uint8_t source);

    void activateLabelsBorder();
    void refreshConfigLists();
    void refreshGain();
    void refreshMonitorState();

    void setPrevievFlag(uint8_t s);

    void refreshRDS();
    void displaySeeking();
    void displayEmptyRDS();
    void hideRDS();

    void noteOnHandle(uint8_t channel, uint8_t note, uint8_t velocity, int16_t source);
    void noteOffHandle(uint8_t channel, uint8_t note, uint8_t velocity, int16_t source);

    void calcPlayProgressValue();

    void listMonitor();

    void calcRadioFreqBarVal();
    void calcLevelBarVal();
    void calcGainBarVal();

    void drawRadioFreqBar();
    void drawLevelBar();
    void drawGainBar();

    void changeRadioFreqBar(int16_t val);
    void changeLevelBar();
    void changeGainBar(int16_t val);

    void setDefaultSource();
    void setDefaultGain();
    void setDefaultMonitorEnable();
    void setDefaultRadioFreq();

    void setDefaultStartPoint();
    void setDefaultEndPoint();
    void setDefaultZoom();

    void showSaveHorizontalBar();
    void hideSaveHorizontalBar();
    // selection window
    void showSelectionWindow();
    void showSelectionNotEnoughInstruments();
    void showSelectionNotEnoughMemory();
    void showSelectionStopPattern();

    void showSelectionWindowSave();
    void showSelectionWindowFullMemory();

    void showSource();
    void showMonitor();
    void showGain();

    void addNode(editFunct_t funct, uint8_t nodeNum);
    void removeNode(uint8_t nodeNum);
    void stepThroughNodes(int16_t value);
    void clearAllNodes();
    void cancelMultiFrame();
    uint16_t calculateStartPointFromRecording();
    uint16_t calculateEndPointFromRecording();
    void resizeSpectrum();
    bool isChannelShown(int channel);
    void calculatePointsAfterRecording();
    bool isModuleSwitchingBlocked();
    void formatTimeSeconds(char* destBuffer, size_t destBufferSize, float value);
    class AvoidPopOnStart
    {
      public:
        void blockSoundOnStart();
        void tryUnblockSound();
        void tryUnblockSoundOnExit();

      private:
        void unblockSound();
        elapsedMillis timer;
        bool isStart;
    } avoidPopOnStart;

    enum sourceType
    {
        sourceTypeLineInL,
        sourceTypeLineInR,
        sourceTypeLineInM,
        sourceTypeLineInS,
#ifndef TRACKER_MINI
        sourceTypeRadio,
        sourceTypeMicLG, // = tracker mini mic
        sourceTypeMicHG,
        sourceTypeUsbMix,
        sourceTypeUsbStereo,
#else
        sourceTypeMicLG, // = tracker mini mic
        sourceTypeUsbMix,
        sourceTypeUsbStereo,
        sourceTypeMicHG,
        sourceTypeRadio,
#endif
    };

    bool isLineInSource() const
    {
        return recorderConfig.source == sourceTypeLineInL ||
               recorderConfig.source == sourceTypeLineInR ||
               recorderConfig.source == sourceTypeLineInM ||
               recorderConfig.source == sourceTypeLineInS;
    }

    bool isUsbSource() const
    {
        return recorderConfig.source == sourceTypeUsbMix ||
               recorderConfig.source == sourceTypeUsbStereo;
    }

    bool isStereoSource() const
    {
        return recorderConfig.source == sourceTypeLineInS ||
               recorderConfig.source == sourceTypeUsbStereo;
    }

    enum screenType
    {
        screenTypeConfig,
        screenTypeRecord,
        screenTypeKeyboard
    };

    enum saveType
    {
        saveTypeNormal = 1,
        saveTypeLoad
    };

    enum struct enChangesLostSource
    {
        cancelFunct,
        switchModuleFunct
    } changesLostSource;

    enum ConfigScreenColumns
    {
#ifdef TRACKER_MINI
        RADIO_COL1 = 5,
        RADIO_COL2 = 6,
        RADIO_COL3 = 4,
        LEVEL_COL = 1,
        GAIN_COL = 2,
        MONITOR_COL = 3,
        METRONOME_COL = 4,
#else
        RADIO_COL1 = 1,
        RADIO_COL2 = 2,
        RADIO_COL3 = 3,
        METRONOME_COL = 3,
        LEVEL_COL = 4,
        GAIN_COL = 5,
        MONITOR_COL = 6,
#endif
    };
    void setCurrentScreenType(screenType screenType);
    void refreshOutputSource();
    strFrameData frameData;

    //*********************************************
    hControl spectrumControl[SAMPLE_CHANNELS];
    hControl label[8];
    hControl frameControl;
    hControl sourceListControl;
    hControl monitorListControl;
    hControl levelBarControl;
    hControl gainBarControl;
    hControl radioFreqBarControl;
    hControl pointsControl;
    hControl playhead;
    hControl radioRdsLabel;
    hControl bgLabel = -1;
    hControl blankSpaceCover = -1;
    hControl monitorListSpaceCover = -1;
    hControl radioSpaceCover = -1;

    //*********************************************
    uint8_t dontTurnOffRadio;
    uint8_t wasSampleMemoryOverwritten;

    uint8_t selectedPlace = 0;
    uint8_t currentScreen = 0;
    strRecorderConfig recorderConfig;
    uint32_t spectrumTimerConstrains = 100;
    char freqTextValue[10];
    char recTimeValueText[9];
    char playTimeValueText[9];
    char startPointValueText[9];
    char endPointValueText[9];
    uint32_t levelBarColors[5] = { 0 };
    float recTimeValue;

    //----------------------------------
    // spectrum + punkty
    uint8_t refreshSpectrum = 0;
    uint8_t refreshSpectrumValue = 0;
    uint8_t refreshPoints = 0;

    uint16_t startPoint;
    uint16_t endPoint = MAX_16BIT;

    uint32_t playProgressValue = 0;      // 0 - MAX_LEN_RECORD
    uint16_t playProgressInSpectrum = 0; // 0 - 600
    elapsedMillis playProgresValueTim;
    elapsedMicros refreshPlayProgressValue;

    uint8_t playInProgressFlag = 0;

    strZoomParams zoom;
    strSpectrumParams spectrumParams[SAMPLE_CHANNELS];

    char zoomTextValue[7];

    static strTrackerSpectrum spectrum[SAMPLE_CHANNELS];
    strTrackerPoints points;

    strLabelData doubleArrow[2];

    elapsedMillis refreshSpectrumTimer;

    strInstrument* editorInstrument;
    uint8_t refreshSpectrumProgress = 0;
    uint8_t loopDirection = 0;
    float playPitch = 1.0;
    elapsedMicros playProgresValueBackwardTim = 0;

    //----------------------------------
    // listy
    strList sourceList;
    strList monitorList;

    char* sourceNames[sourceCount];
    char* monitorNames[monitorCount];

    elapsedMillis changeSourceToMicTimer = 0;
    uint8_t changeSourceToMicFlag = 0;

    //----------------------------------
    // rec

    uint8_t recordInProgressFlag = 0;
    strUndoCrop undo[undoCount];
    uint8_t cropCounter = 0;

    //----------------------------------
    // bar
    uint8_t radioFreqBarVal;
    uint8_t levelBarVal;
    uint8_t lastLevelBarVal;
    elapsedMicros levelBarTim;
    float measureSum = 0;
    uint8_t levelBarMeasureCounter;
    uint8_t gainBarVal;
    elapsedMillis redColorTimer;

    int8_t firstFreeInstrumentSlotFound;
    uint8_t saveProgress;
    uint8_t saveOrSaveloadFlag = 0;
    uint8_t forceSwitchModule = 0;

    uint8_t notEnoughInstrumentsFlag = 0;
    uint8_t notEnoughMemoryFlag = 0;
    uint8_t patternIsPlayingFlag = 0;

    uint8_t moduleToSwitch;
    uint8_t selectionWindowFlag = 0;
    // selection window  - zapis
    uint8_t selectionWindowSaveFlag = 0;

    uint8_t fullMemoryDuringRecordFlag = 0;
    uint8_t fullMemoryWindowFlag = 0;

    char sourceName[8];
    char monitorVal[4];
    char gainVal[4];

    select_node_t selectNodes[MAX_SELECT_NODES];

    uint8_t globalLastUsedInstrumentIndex;
    uint8_t recorderMidiLastUsedInstrumentIndex = 48;

    Sgtl5000* codec;
    Radio* radio;
    FileSystem* fileSystem;
    bool startRecordingOnPreRollEnd = false;
    bool waitForPreRollEnd = false;
    bool switchToMetronome = false;
    uint32_t optionForMetronome = kOpenMetronomeConfig;
    bool isSwitchingFromMenu = true;

    bool isMonitorListVisible()
    {
        return !isUsbSource();
    }
};

extern cSampleRecorder sampleRecorder;

#endif /* INTERFACE_SAMPLERECORDER_H_ */
