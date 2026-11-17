#ifndef INTERFACE_SAMPLEEDITOR_H_
#define INTERFACE_SAMPLEEDITOR_H_

#include "core/modulesBase.h"
#include "mtSampleEditorEngine.h"
#include "mtStructs.h"
#include "tracker_plugin.h"

static constexpr int kMaxParametersInEffect = 12;

char* const effectNamesLabels[editorEffectMax] = {
    (char*)"Normalizer",       (char*)"Crop",
    (char*)"Reverse",          (char*)"Amplifier",
    (char*)"Overdrive",        (char*)"Delay",
    (char*)"Bitcrusher",       (char*)"Chorus",
    (char*)"Flanger",          (char*)"Equalizer",
    (char*)"Limiter",          (char*)"Compressor",
    (char*)"WT Smoother",      (char*)"Timestretch Beat",
    (char*)"Timestretch Note", (char*)"Fade In",
    (char*)"Fade Out",         (char*)"Modular"
};

struct strEffectDisplayParams
{
    float parameter[kMaxParametersInEffect];
};

struct strEffectDisplayConstantParams
{
    uint8_t paramsNumber;
    const char* const* labelsText;
    float* upConstrain;
    float* downConstrain;
    float* changeStep;
    float* displayMult;
    const char* const* afterValueText;
    const char* displayType; //'f' = float 'd' = int
};

struct strEffectDefaultParams
{
    float parameter[kMaxParametersInEffect];
};

const strEffectDisplayConstantParams effectDisplayConstantParams[editorEffectMax] = {
    { 1,
      normalizerParams::labelText,
      normalizerParams::upConstrain,
      normalizerParams::downConstrain,
      normalizerParams::changeStep,
      normalizerParams::displayMult,
      normalizerParams::afterValueText,
      normalizerParams::displayType },
    {}, // cut - brak drugiego ekranu
    {}, // reverse - brak drugiego ekranu
    { 1,
      amplifierParams::labelText,
      amplifierParams::upConstrain,
      amplifierParams::downConstrain,
      amplifierParams::changeStep,
      amplifierParams::displayMult,
      amplifierParams::afterValueText,
      amplifierParams::displayType },
    { 2,
      overdriveParams::labelText,
      overdriveParams::upConstrain,
      overdriveParams::downConstrain,
      overdriveParams::changeStep,
      overdriveParams::displayMult,
      overdriveParams::afterValueText,
      overdriveParams::displayType },
    { 2,
      delayParams::labelText,
      delayParams::upConstrain,
      delayParams::downConstrain,
      delayParams::changeStep,
      delayParams::displayMult,
      delayParams::afterValueText,
      delayParams::displayType },
    { 2,
      bitcrusherParams::labelText,
      bitcrusherParams::upConstrain,
      bitcrusherParams::downConstrain,
      bitcrusherParams::changeStep,
      bitcrusherParams::displayMult,
      bitcrusherParams::afterValueText,
      bitcrusherParams::displayType },
    { 2,
      chorusParams::labelText,
      chorusParams::upConstrain,
      chorusParams::downConstrain,
      chorusParams::changeStep,
      chorusParams::displayMult,
      chorusParams::afterValueText,
      chorusParams::displayType },
    { 4,
      flangerParams::labelText,
      flangerParams::upConstrain,
      flangerParams::downConstrain,
      flangerParams::changeStep,
      flangerParams::displayMult,
      flangerParams::afterValueText,
      flangerParams::displayType },
    { 9,
      eqParams::labelText,
      eqParams::upConstrain,
      eqParams::downConstrain,
      eqParams::changeStep,
      eqParams::displayMult,
      eqParams::afterValueText,
      eqParams::displayType },
    { 3,
      limiterParams::labelText,
      limiterParams::upConstrain,
      limiterParams::downConstrain,
      limiterParams::changeStep,
      limiterParams::displayMult,
      limiterParams::afterValueText,
      limiterParams::displayType },
    { 4,
      compressorParams::labelText,
      compressorParams::upConstrain,
      compressorParams::downConstrain,
      compressorParams::changeStep,
      compressorParams::displayMult,
      compressorParams::afterValueText,
      compressorParams::displayType },
    { 2,
      wavetableSmootherParams::labelText,
      wavetableSmootherParams::upConstrain,
      wavetableSmootherParams::downConstrain,
      wavetableSmootherParams::changeStep,
      wavetableSmootherParams::displayMult,
      wavetableSmootherParams::afterValueText,
      wavetableSmootherParams::displayType },
    { 4,
      timestretchBeatParams::labelText,
      timestretchBeatParams::upConstrain,
      timestretchBeatParams::downConstrain,
      timestretchBeatParams::changeStep,
      timestretchBeatParams::displayMult,
      timestretchBeatParams::afterValueText,
      timestretchBeatParams::displayType },
    { 2,
      timestretchNoteParams::labelText,
      timestretchNoteParams::upConstrain,
      timestretchNoteParams::downConstrain,
      timestretchNoteParams::changeStep,
      timestretchNoteParams::displayMult,
      timestretchNoteParams::afterValueText,
      timestretchNoteParams::displayType },
    {}, // fade in  -  brak drugiego ekranu
    {}, // fade out - brak drugiego ekranu
    {}  // modular - dynamiczne parametry
};

const strEffectDefaultParams effectDefaultParamsTable[editorEffectMax] = {
    { { 100.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } }, // normalizer
    {},                                                             // cut - brak drugiego ekranu
    {},                                                           // reverse - brak drugiego ekranu
    { { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } }, // amplifier
    { { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } }, // overdrive
    { { 0.95f, 50.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },      // delay
    { { 4.0f, 44100.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },    // bitcrusher
    { { 128.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },      // chorus
    { { 128.0f, 50.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },     // flanger
    { { 0.0f, 80.0f, 1.0f, 0.0f, 800.0f, 1.0f, 0.0f, 12000.0f, 1.0f } }, // eq
    { { -6.0f, 3.0f, 300.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },     // limiter
    { { -12.0f, 4.0f, 10.0f, 50.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },    // compressor
    { { 256.0f, 2048.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },   // wt smoother
    { { 0.0f, 16.0f, 0.0f, 4.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },       // timestretch beat
    { { 0.0f, 100.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },      // timestretch note
    {}, // fade in - brak drugiego ekranu
    {}, // fade out - brak drugiego ekranu
    {}  // modular
};

class cSampleEditor : public ModuleBase
{

  public:
    enum enScreenType
    {
        mainScreen,
        effectScreen,
        screenTypeCount
    };

    enum enMainScreenButtons
    {
        startPointButton,
        endPointButton,
        zoomButton,
        effectButton
    };

    enum enPreviewState
    {
        previewStatePreview, // :)
        previewStatePlay,
        previewStateStop
    } previewState;

    //*******************CORE MODULE FUNCTIONS
    virtual void update() override;
    virtual void start(uint32_t options) override;
    virtual void stop();
    void restoreCurrentScreenFunctions();
    void setCommonFunctions();
    void setMainScreenFunctions();
    void setParamsScreenFunctions();
    void switchScreen(enScreenType s);
    void restoreFunctions();
    void setStopSeqFunctions();
    void setSaveChangesFunctions();
    void setTooLongSampleFunctions();
    void setTooLongProcessedSampleFunctions();
    bool isParametersScreen();
    bool canApplyCurrentEffect() const;

    void noteOnHandle(uint8_t channel, uint8_t note, uint8_t velocity, int16_t source);
    void noteOffHandle(uint8_t channel, uint8_t note, uint8_t velocity, int16_t source);

    //******************* OPERATION FOR GRAPHIC
    void hideUselessControls();

    bool isCurrentSampleStereo();
    void prepareDisplayDataMainScreen();

    void reloadFrameData(enScreenType s);
    void reloadPointsData();
    void reloadStartPointText();
    void reloadEndPointText();
    void reloadZoomText();
    void reloadPlayheadValue();
    void reloadCurrentEffect();
    void reloadSpectrumData();
    // popups
    void reloadApplyingProgress();
    void reloadProcessingProgress();
    void reloadPlayingProgress();
    // params screen
    void prepareDisplayDataParamsScreen();
    void reloadParameterValueText(uint8_t n);
    void reloadParameterBarValue(uint8_t n);
    //*******************
    //*******************PREVIEW/PLAY/STOP
    void setPreviewFunction();
    void setPlayFunction();
    void setStopFunction();

    void showPreviewLabel();
    void showPlayLabel();
    void showStopLabel();
    //*******************
    //*******************CORE GRAPHIC FUNCTIONS
    virtual void initDisplayControls() override;
    virtual void destroyDisplayControls() {}
    void showMainScreen();
    void showEffectParamsScreen();
    //*******************
    //******************* GRAPHIC OPERATIONS
    void showTitleBar(const char* title);
    void showStartPointText();
    void showEndPointText();
    void showZoomText();
    void showSpectrumPoints();
    void hideSpectrumPoints();
    void showFrame();
    void resizeWaveform();
    void showSpectrum();
    void hideSpectrum();
    void showPlayhead();
    void hidePlayhead();
    void showEffectList();
    void hideEffectList();

    void showParameterBar(uint8_t n);
    void showParameterLabel(uint8_t n);
    // Popups
    // applying
    void showPopupApplying();
    void showProgressApplying();
    // processing
    void showPopupProcessing();
    void showProgressProcessing();
    // playing
    void showPopupPlaying();
    void showProgressPlaying();

    void hideProgressPopup();

    void cancelGlobalPopups();

    // Stop Seq
    void showPopupStopSequencer();
    void showPopupSeqWindow();
    // Too Long instrument
    void showPopupTooLongSample();
    void showPopupTooLongSampleWindow();
    // Too Long processed sample
    void showPopupTooLongProcessedSample();
    void showPopupTooLongProcessedSampleWindow();

    void showStaticPopupCommonWindow();
    void hideStaticPopup();
    //*******************
    //******************* REFRESH - RELOAD + DISPLAY
    void refreshSpectrumPoints();
    void refreshStartPoint();
    void refreshEndPoint();
    void refreshZoom();
    void refreshEffectList();
    void refreshSpectrum();
    void refreshPlayhead();
    void refreshUndoState();
    void refreshParameter(uint8_t n);

    // popups
    void refreshApplyingProgress();
    void refreshProcessingProgress();
    void refreshPlayingProgress();
    //*******************
    //******************* PARAMETERS MODIFICATORS
    // MainScreen
    void modStartPoint(int16_t val);
    void modEndPoint(int16_t val);
    void modZoom(int16_t val);
    void resetZoom();
    void modSelectedEffect(int16_t val);

    void setDefaultStartPoint();
    void setDefaultEndPoint();
    void setDefaultZoom();

    // ParameterScreen
    void modParameter(int16_t val, uint8_t n);
    void setDefaultParameter(uint8_t n);
    int getSelectedParameterIdx();
    int getParamsPagesCount();
    int getCurrentPageParams();
    bool areThereMoreThanOneParamsPage();
    bool isModularPresetPage() const;
    void openLoadEffectBrowser();
    void openModularPresetLoadBrowser();
    void openModularPresetSaveBrowser();
    void resetModularEffectParams();

    //*******************
    float samplePosToTime(uint32_t pos);
    void handleStartupWithPopupAndMenu();

    static constexpr int kEffectPageParams = 4;

    //*******************
    cSampleEditor()
    {
        label[8] = { -1 };
        bar[8] = { -1 };
        for (int i = 0; i < kEffectPageParams; i++)
        {
            paramList[i] = -1;
        }
        effectList = -1;
        spectrum[0] = -1;
        spectrum[1] = -1;
        spectrumPoints = -1;
        playhead = -1;
        frame = -1;
        popupProgressBar = -1;
        bgLabel = -1;
        blankSpaceCover = -1;
    }
    virtual ~cSampleEditor() {}

    // DISPLAY CONTROLS
    hControl label[8];
    hControl bar[8];
    hControl paramList[kEffectPageParams];
    hControl effectList;
    hControl blankSpaceCover;
    hControl spectrum[2];
    hControl spectrumPoints;
    hControl playhead;
    hControl frame;
    hControl popupProgressBar;
    hControl bgLabel;

    // DISPLAY CONTROLS DATA
    strList effectListData;
    strList paramListData[kEffectPageParams];
    strLabelData labelDataMore;

    static strTrackerSpectrum spectrumData[2];
    strTrackerPoints spectrumPointsData = { 0, 0, 600, 0, 0, 0 };
    strFrameData frameData;
    strLabelData labelArrow;

    uint8_t paramsBarValue[kMaxParametersInEffect];
    // applying
    uint8_t applyingProgress;
    uint8_t applyingSteps;
    bool applyingInProgress;
    // processing
    uint8_t processingProgress;
    uint8_t processingSteps;
    bool processingInProgress;
    // playing
    uint8_t playingProgress;
    bool playingInProgress;

    bool isLoadedData;
    bool isProcessedData;

    bool confirmedDataIsChanged;
    bool reloadOnEndSaving;
    uint8_t afterReloadInstrumentIdx;
    uint8_t beforeReloadInstrumentIdx;

    uint8_t moduleToChange;

    bool avoidStop;
    // labelsTxt
    char currentInstrumentName[SAMPLE_NAME_SIZE + 6];
    char startPointText[11];
    char endPointText[11];
    char zoomText[7];
    char paramsScreenLabel[40];

    // params screen
    char parameterValueLabel[kMaxParametersInEffect][16];
    char parameterFileText[kMaxParametersInEffect][40] = {};

    char* const parameterValueLabelPtr[kMaxParametersInEffect] = {
        &parameterValueLabel[0][0], &parameterValueLabel[1][0],  &parameterValueLabel[2][0],
        &parameterValueLabel[3][0], &parameterValueLabel[4][0],  &parameterValueLabel[5][0],
        &parameterValueLabel[6][0], &parameterValueLabel[7][0],  &parameterValueLabel[8][0],
        &parameterValueLabel[9][0], &parameterValueLabel[10][0], &parameterValueLabel[11][0]
    };
    //*********************

    //********************SPECTRUM PROCESSING
    strSpectrumParams spectrumParams[2];
    strZoomParams zoom;

    struct strSelection
    {
        uint16_t startPoint = 0;
        uint16_t endPoint = MAX_16BIT;
    } selection;

    uint8_t needRefreshSpectrum;
    uint8_t needRefreshPoints;
    uint8_t needRefreshPlayhead; // globalnie w update
    uint8_t needShowPlayhead;    // wewnetrznie w odswiezaniu
    uint16_t playheadValue;
    elapsedMicros refreshPlayheadTimer;

    enScreenType screenType = mainScreen;
    uint8_t selectedEffect = editorEffectDelay;
    uint8_t selectedPlace[enScreenType::screenTypeCount];
    uint8_t effectScreenPage = 0;
    uint8_t mainScreenButtonPage = 0;

    const strEffectDefaultParams* effectDefaultParamsPtr = effectDefaultParamsTable;
    const strEffectDisplayConstantParams* effectDisplayConstantParamsPtr =
      effectDisplayConstantParams;

    static strEffectDisplayParams effectDisplayParamsToAdjust[editorEffectMax];

    static strInstrument sampleEditInstrument;
    strSampleMemory sampleEditorSampleMemory;
    strInstrument* const editorInstrument = &sampleEditInstrument;
    uint8_t currentEffectIdx;
    mtEffect* currentEffect = nullptr;
    mtEffect* lastEffect = nullptr;

    // Modular effect state & dynamic UI definitions
    uint16_t selectedEffectListIdx = 0;
    strEffectDisplayConstantParams s_modularEffectParams = {};
    char s_modularLabelStorage[kMaxParametersInEffect][8] = {};
    char s_modularUnitStorage[kMaxParametersInEffect][8] = {};
    const char* s_modularLabels[kMaxParametersInEffect] = {};
    float s_modularUpConstrain[kMaxParametersInEffect] = {};
    float s_modularDownConstrain[kMaxParametersInEffect] = {};
    float s_modularChangeStep[kMaxParametersInEffect] = {};
    float s_modularDisplayMult[kMaxParametersInEffect] = {};
    const char* s_modularUnits[kMaxParametersInEffect] = {};
    char s_modularDisplayType[kMaxParametersInEffect + 1] = {};

    void loadModularEffectUI(const PtfxHeader& header);
    const strEffectDisplayConstantParams& getCurrentEffectDisplayParams() const;
    bool isParameterList(uint8_t paramIdx) const;
    bool isParameterFile(uint8_t paramIdx) const;
    const char* getParameterFileText(uint8_t paramIdx) const;
    uint8_t getParameterListCount(uint8_t paramIdx) const;
    char** getParameterListItems(uint8_t paramIdx) const;
};

extern cSampleEditor sampleEditor;

#endif /* INTERFACE_SAMPLEEDITOR_H_ */
