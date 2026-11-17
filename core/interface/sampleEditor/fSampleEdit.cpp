#include "core/graphicProcessing.h"
#include "core/interfaceMenuPopup.h"
#include "core/interfacePopups.h"
#include "core/songTimer.h"
#include "fileBrowser/GlobalFileBrowser.h"
#include "fileManager.h"
#include "mtAudioEngine.h"
#include "mtPadBoard.h"
#include "mtPadsBacklight.h"
#include "mtSequencer.h"
#include "sampleEditor/sampleEditor.h"
#include "ptfx_manager.h"
#include "VoiceStreamManager.h"
#include "util_math.h"
#include "util_string.h"

#include <cr_section_macros.h>

__NOINIT(EXTERNAL_RAM) strTrackerSpectrum cSampleEditor::spectrumData[2];
__NOINIT(EXTERNAL_RAM) strEffectDisplayParams cSampleEditor::effectDisplayParamsToAdjust[editorEffectMax];
__NOINIT(EXTERNAL_RAM) strInstrument cSampleEditor::sampleEditInstrument;

cSampleEditor sampleEditor;

static cSampleEditor* SE = &sampleEditor;

static const uint16_t mainScreenFramePlaces[4][4] = {
    { (800 / 8) * 0 + 1, 424, 800 / 8 - 3, 55 },
    { (800 / 8) * 1 + 1, 424, 800 / 8 - 3, 55 },
    { (800 / 8) * 2 + 1, 424, 800 / 8 - 3, 55 },
    { (800 / 8) * 6 + 1, 31, 800 / 4 - 3, 391 },
};

static const uint16_t paramsScreenFramePlaces[6][4] = {
    { (800 / 8) * 0 + 1, 31, 800 / 8 - 3, 391 }, { (800 / 8) * 1 + 1, 31, 800 / 8 - 3, 391 },
    { (800 / 8) * 2 + 1, 31, 800 / 8 - 3, 391 }, { (800 / 8) * 3 + 1, 31, 800 / 8 - 3, 391 },
    { (800 / 8) * 4 + 1, 31, 800 / 8 - 3, 391 }, { (800 / 8) * 5 + 1, 31, 800 / 8 - 3, 391 },
};

// ACTION FUNCTIONS DECLARATIONS
static void functSwitchModule(uint8_t button);
static void functPads(uint8_t pad, uint8_t state, int16_t velo);
static void functMenu(uint8_t state);

static void functMiniMiddle(uint8_t state);

static void functInstrument(uint8_t state);

// Main screen functions
static void functMainScreenLeft();
static void functMainScreenRight();
static void functMainScreenUp();
static void functMainScreenDown();
static void functMainScreenEncoder(int16_t value);
static void functMainScreenDelete();

static void functSelectStartPoint();
static void functSelectEndPoint();
static void functSelectZoom();
static void functMainScreenMore();
static void functLoadWav();
static void functExportWav();
static void functListUp();
static void functListDown();
static void functUndo();
static void functApply();
// Parameter screen functions
static void functParamsScreenLeft();
static void functParamsScreenRight();
static void functParamsScreenUp();
static void functParamsScreenDown();
static void functParamsScreenEncoder(int16_t value);
static void functParamsScreenDelete();
static void functParamsScreenInsert();
static void functSelectParameter(uint8_t button);
static void functParamsScreenMore();
static void functBack();
static void functPreview();
static void functPlay();
static void functStop();
static void functSave();
// Stop Pattern Funct
static void functStopPatternYes();
static void functStopPatternNo();
// Too long instrument
static void functTooLongSampleOk();
// Too long processed instrument
static void functTooLongProcessedSampleOk();
//**************************************************************************

void cSampleEditor::start(uint32_t options)
{
    moduleRefresh = 1;

    if (mtProject.values.lastUsedInstrument < kSampleInstrumentsCount &&
        mtProject.instrument[mtProject.values.lastUsedInstrument].sample.length >
          EDIT_BUFF_SIZE / 2)
    {
        if (mtMenuPopup.isPopupVisible())
        {
            handleStartupWithPopupAndMenu();
            return;
        }

        showPopupTooLongSampleWindow();
        setTooLongSampleFunctions();
        return;
    }

    if (options == 1)
    {
        PtfxManager::initEffectList();
        effectListData.length = PtfxManager::getTotalEffectListCount();
        effectListData.data = PtfxManager::getEffectListNames();

        engine.blockDelayRefresh();
        engine.disableLineInImpact();
        songTimer.hide();
        sequencer.setMidiInVoiceMode(Sequencer::midiInVoiceMode_SampleEditor);
        setCommonFunctions();
        if (screenType == effectScreen)
        {
            showEffectParamsScreen();
            setParamsScreenFunctions();
        }
        else
        {
            prepareDisplayDataMainScreen();
            showMainScreen();
            setMainScreenFunctions();
        }
        return;
    }

    mainScreenButtonPage = 0;

    if (sequencer.getSeqState() != Sequencer::SEQ_STATE_STOP)
    {
        if (mtMenuPopup.isPopupVisible())
        {
            handleStartupWithPopupAndMenu();
            return;
        }

        showPopupSeqWindow();
        setStopSeqFunctions();
        return;
    }

    engine.blockDelayRefresh();
    engine.disableLineInImpact();
    songTimer.hide();
    lastEffect = nullptr;

    PtfxManager::initEffectList();
    effectListData.length = PtfxManager::getTotalEffectListCount();
    effectListData.data = PtfxManager::getEffectListNames();

    if (mtProject.values.lastUsedInstrument > kSampleInstrumentsCount - 1)
    {
        editorInstrument->isActive = 0;
        editorInstrument->sample.address = nullptr;
        sampleEditorSampleMemory.address[mtEffect::Channel::kLeft] = nullptr;
        sampleEditorSampleMemory.address[mtEffect::Channel::kRight] = nullptr;
        editorInstrument->sample.length = 0;
        editorInstrument->tuneResample = 0;
    }
    else
    {
        editorInstrument->isActive =
          mtProject.instrument[mtProject.values.lastUsedInstrument].isActive;
        editorInstrument->sample.address =
          mtProject.instrument[mtProject.values.lastUsedInstrument].sample.address;
        sampleEditorSampleMemory.address[mtEffect::Channel::kLeft] =
          mtProject.sampleMemory[mtProject.values.lastUsedInstrument]
            .address[mtEffect::Channel::kLeft];
        sampleEditorSampleMemory.address[mtEffect::Channel::kRight] =
          mtProject.sampleMemory[mtProject.values.lastUsedInstrument]
            .address[mtEffect::Channel::kRight];
        editorInstrument->sample.length =
          mtProject.instrument[mtProject.values.lastUsedInstrument].sample.length;
        editorInstrument->tuneResample =
          mtProject.instrument[mtProject.values.lastUsedInstrument].tuneResample;

        strcpy((char*)&editorInstrument->sample.file_name,
               (char*)&mtProject.instrument[mtProject.values.lastUsedInstrument].sample.file_name);
    }

    if (editorInstrument->isActive)
    {
        spectrumParams[0].address = mtProject.sampleMemory[mtProject.values.lastUsedInstrument]
                                      .address[mtEffect::Channel::kLeft];
        spectrumParams[0].length = editorInstrument->sample.length;

        spectrumParams[1].address = mtProject.sampleMemory[mtProject.values.lastUsedInstrument]
                                      .address[mtEffect::Channel::kRight];
        spectrumParams[1].length = editorInstrument->sample.length;
    }
    else
    {
        spectrumParams[0].address = nullptr;
        spectrumParams[0].length = 0;
        spectrumParams[1].address = nullptr;
        spectrumParams[1].length = 0;
    }

    if (selectedEffectListIdx >= effectListData.length)
    {
        selectedEffectListIdx = 0;
    }

    float savedModularParams[12]{};
    bool hasLoadedModular = PtfxManager::hasLoadedPlugin();
    if (hasLoadedModular)
    {
        for (uint8_t i = 0; i < 12; i++)
        {
            savedModularParams[i] = effectDisplayParamsToAdjust[editorEffectModular].parameter[i];
        }
    }

    assert(sizeof(effectDisplayParamsToAdjust) == sizeof(effectDefaultParamsTable));
    memcpy(effectDisplayParamsToAdjust, effectDefaultParamsTable, sizeof(effectDefaultParamsTable));

    if (hasLoadedModular)
    {
        for (uint8_t i = 0; i < 12; i++)
        {
            effectDisplayParamsToAdjust[editorEffectModular].parameter[i] = savedModularParams[i];
        }
    }

    if (selectedEffectListIdx < kBuiltInEffectsCount)
    {
        currentEffectIdx = (uint8_t)selectedEffectListIdx;
        currentEffect = sampleEditorEffect[currentEffectIdx];
    }
    else if (PtfxManager::isLoadedPluginListIndex(selectedEffectListIdx))
    {
        currentEffectIdx = editorEffectModular;
        currentEffect = sampleEditorEffect[editorEffectModular];
        if (PtfxManager::reloadActivePlugin(sampleEditorModular))
        {
            loadModularEffectUI(PtfxManager::getActiveHeader());
            for (uint8_t i = 0; i < 12; i++)
            {
                effectDisplayParamsToAdjust[editorEffectModular].parameter[i] =
                  savedModularParams[i];
            }
        }
    }
    else
    {
        currentEffectIdx = editorEffectModular;
        currentEffect = sampleEditorEffect[editorEffectModular];
    }
    lastEffect = currentEffect;
    currentEffect->undoParams.isEnable = false;

    screenType = mainScreen;
    previewState = previewStatePreview;
    confirmedDataIsChanged = 0;

    setMainScreenFunctions();
    setCommonFunctions();

    selection.startPoint = 0;
    selection.endPoint = MAX_16BIT;

    if (!editorInstrument->isActive)
    {
        selectedPlace[mainScreen] = effectButton;
    }

    currentEffect->clearLoadedData();
    currentEffect->clearProcessedData();
    currentEffect->setBaseSampleLength(editorInstrument->sample.length);

    if (editorInstrument->isActive &&
        mtProject.values.lastUsedInstrument < kSampleInstrumentsCount &&
        mtProject.sampleMemory[mtProject.values.lastUsedInstrument].isStreamed)
    {
        if (currentEffect->startLoad(mtProject.values.lastUsedInstrument) ==
            mtEffect::LoadingResult::loadingSuccessful)
        {
            currentEffect->update();
            reloadSpectrumData();
            isLoadedData = currentEffect->isDataLoaded();
        }
    }

    if (zoom.zoomValue > 1.0f)
    {
        // the zoom may have been set on a sample of another length
        GP.spectrumChangeZoom(0, editorInstrument->sample.length, &zoom);
    }

    prepareDisplayDataMainScreen();
    showMainScreen();

    if (!PtfxManager::isLoadEffectListIndex(selectedEffectListIdx))
    {
        for (uint8_t idx = 0; idx < getCurrentEffectDisplayParams().paramsNumber; idx++)
        {
            currentEffect->setParameter(
              effectDisplayParamsToAdjust[currentEffectIdx].parameter[idx], idx);
        }
    }

    currentEffect->changeSelectionRange(selection.startPoint, selection.endPoint);

    sequencer.setMidiInVoiceMode(Sequencer::midiInVoiceMode_SampleEditor);
}

void cSampleEditor::update()
{
    if (needRefreshSpectrum)
    {
        needRefreshSpectrum = 0;
        refreshSpectrum();
    }
    if (needRefreshPoints)
    {
        needRefreshPoints = 0;
        refreshSpectrumPoints();
    }

    if (needRefreshPlayhead)
    {
        refreshPlayhead();
    }

    if (!currentEffect->isDataLoaded() && currentEffect->getLoadingState())
    {
        currentEffect->update();
        if (!currentEffect->getLoadingState())
        {
            reloadSpectrumData();
            isLoadedData = currentEffect->isDataLoaded();
        }
    }
    else if (!currentEffect->isDataProcessed() && currentEffect->getProcessingSelectionState())
    {
        currentEffect->update();
        if (!currentEffect->getProcessingSelectionState())
        {
            if (currentEffect->isDataLoaded() || applyingProgress)
            {
                reloadSpectrumData();
                needRefreshSpectrum = 1;
            }

            isProcessedData = currentEffect->isDataProcessed();

            applyingInProgress = 0;

            if (processingInProgress)
            {
                //				processingInProgress = false;
                processingProgress = 0;
                showProgressProcessing();
                setPlayFunction();
            }

            if (applyingProgress)
            {
                if (currentEffectIdx == editorEffectModular)
                {
                    sampleEditorModular.onApplyCommitted(mtProject.values.lastUsedInstrument);
                }
                editorInstrument->isActive = 1;
                editorInstrument->volume = mtProject.instrument[mtProject.values.lastUsedInstrument].volume;
                editorInstrument->sample.length = currentEffect->getLengthToPolyphonicPlay();
                strncpy(editorInstrument->sample.file_name,
                        mtProject.instrument[mtProject.values.lastUsedInstrument].sample.file_name,
                        SAMPLE_NAME_SIZE);
                resetZoom();
                screenType = cSampleEditor::mainScreen;
                switchScreen(screenType);
                applyingProgress = 0;
            }
            showProgressApplying();

            hideProgressPopup();

            if (currentEffect->isDataLoaded())
            {
                selection.startPoint = currentEffect->getNewStartPoint();
                selection.endPoint = currentEffect->getNewEndPoint();

                refreshStartPoint();
                refreshEndPoint();
            }

            refreshUndoState();

            restoreFunctions();

            if (processingInProgress)
            {
                processingInProgress = false; // kasowanie dopiero  tutaj zeby moc odpalic play po
                                              // przeprocesowaniu wszystkich danych
                functPlay();
            }
        }
    }

    if (applyingInProgress)
    {
        refreshApplyingProgress();
    }
    else if (processingInProgress)
    {
        refreshProcessingProgress();
    }

    if (playingInProgress)
    {
        refreshPlayingProgress();
        if (instrumentPlayer[0].getInterfacePlayingEndFlag())
        {
            instrumentPlayer[0].clearInterfacePlayingEndFlag();
            playingInProgress = 0;
            mtPadBoard.clearVoice(0);
            hideProgressPopup();
            restoreFunctions();
            setPlayFunction();
        }
    }
    else if (instrumentPlayer[0].getInterfacePlayingEndFlag())
    {
        instrumentPlayer[0].clearInterfacePlayingEndFlag();
    }

    uint8_t fileManagerStatus = newFileManager.getStatus();

    if (fileManagerStatus == fmImportSampleFromSampleEditorEnd)
    {
        newFileManager.clearStatus();
        newFileManager.reloadSamplesFromWorkspace();
    }
    else if (fileManagerStatus == fmImportSamplesEnd)
    {
        ip->unblockAllInputs();
        newFileManager.clearStatus();
        if (mtProject.values.lastUsedInstrument < kSampleInstrumentsCount)
        {
            strInstrument& instr = mtProject.instrument[mtProject.values.lastUsedInstrument];
            instr.startPoint = 0;
            instr.loopPoint1 = 1;
            instr.loopPoint2 = MAX_16BIT - 1;
            instr.endPoint = MAX_16BIT;
        }
        mtPopups.show(0, "WAV loaded");
        start(0);
    }
    else if (fileManagerStatus == fmSaveRecordedSoundEnd)
    {
        ip->unblockAllInputs();
        newFileManager.clearStatus();
        mtPopups.show(0, "WAV exported");
    }
    else if (fileManagerStatus == fmReloadSamplesEnd)
    {
        ip->unblockAllInputs();
        newFileManager.clearStatus();
#ifdef TRACKER_MINI
        if (!buttons->isButtonPressed(interfaceButtonMiniMenu))
        {
            mtMenuPopup.hideModulePopup(SE);
        }
#endif
        if (reloadOnEndSaving)
        {
            reloadOnEndSaving = false;
            mtProject.values.lastUsedInstrument = afterReloadInstrumentIdx;
            start(0);
        }
        else
        {
            SE->eventFunct(eventSwitchModule, SE, &SE->moduleToChange, 0);
        }
    }
    else if (fileManagerStatus >= fmError)
    {
        ip->unblockAllInputs();
        newFileManager.clearStatus();
    }
}

void cSampleEditor::stop()
{
    moduleRefresh = 0;
    if (!avoidStop)
    {
        engine.unblockDelayRefresh();
        engine.clearDelay();
        engine.clearReverb();
        engine.enableLineInImpact();
    }
    else
    {
        avoidStop = false;
    }
    engine.resetLimiter();
    songTimer.show();
    sequencer.setMidiInVoiceMode(Sequencer::midiInVoiceMode_default);
}

void cSampleEditor::restoreCurrentScreenFunctions()
{
    setCommonFunctions();

    if (screenType == enScreenType::mainScreen)
    {
        setMainScreenFunctions();
    }
    else if (screenType == enScreenType::effectScreen)
    {
        setParamsScreenFunctions();
    }
}

void cSampleEditor::setCommonFunctions()
{
    ip->setButtonObj(interfaceButtonParams, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonPerformance, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonFile, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonConfig, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonMaster, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSamplePlay, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleEdit, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleRec, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleLoad, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSong, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonPattern, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonInstr, functInstrument);
    ip->setButtonObj(interfaceButtonGame, buttonPress, functSwitchModule);

    ip->setButtonObj(interfaceButtonMiniMenu, functMenu);

    if (mtProject.values.lastUsedInstrument > kSampleInstrumentsCount - 1)
        return;
    if (!editorInstrument->isActive)
        return;
    ip->setPadsGlobal(functPads);
    ip->setButtonObj(interfaceButtonMiniMiddle, functMiniMiddle);

    // todo: popup note
}
void cSampleEditor::setMainScreenFunctions()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearButton(interfaceButtonLeft);
    ip->clearButton(interfaceButtonRight);
    ip->clearButton(interfaceButtonUp);
    ip->clearButton(interfaceButtonDown);
    ip->clearButton(interfaceButtonDelete);
    ip->clearAllPots();

    if (mtProject.values.lastUsedInstrument > kSampleInstrumentsCount - 1)
        return;

    ip->setButtonObj(interfaceButtonLeft, buttonPress, functMainScreenLeft);
    ip->setButtonObj(interfaceButtonRight, buttonPress, functMainScreenRight);
    ip->setButtonObj(interfaceButtonUp, buttonPress, functMainScreenUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functMainScreenDown);
    ip->setButtonObj(interfaceButtonDelete, buttonPress, functMainScreenDelete);
    ip->setButtonObj(interfaceButton4, buttonPress, functMainScreenMore);

    if (mainScreenButtonPage == 0)
    {
        if (editorInstrument->isActive)
        {
            ip->setButtonObj(interfaceButton0, buttonPress, functSelectStartPoint);
            ip->setButtonObj(interfaceButton1, buttonPress, functSelectEndPoint);
            ip->setButtonObj(interfaceButton2, buttonPress, functSelectZoom);
            ip->setButtonObj(interfaceButton3, buttonPress, functUndo);
        }
    }
    else
    {
        ip->setButtonObj(interfaceButton0, buttonPress, functLoadWav);
        if (editorInstrument->isActive)
        {
            ip->setButtonObj(interfaceButton1, buttonPress, functExportWav);
            ip->setButtonObj(interfaceButton3, buttonPress, functUndo);
        }
    }
    ip->setButtonObj(interfaceButton5, buttonPress, functApply);
    ip->setButtonObj(interfaceButton6, buttonPress, functListUp);
    ip->setButtonObj(interfaceButton7, buttonPress, functListDown);

    ip->setPotObj(interfacePot0, functMainScreenEncoder);
}
void cSampleEditor::setParamsScreenFunctions()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearButton(interfaceButtonLeft);
    ip->clearButton(interfaceButtonRight);
    ip->clearButton(interfaceButtonUp);
    ip->clearButton(interfaceButtonDown);
    ip->clearButton(interfaceButtonDelete);
    ip->clearAllPots();

    if (mtProject.values.lastUsedInstrument > kSampleInstrumentsCount - 1)
        return;

    ip->setButtonObj(interfaceButtonLeft, buttonPress, functParamsScreenLeft);
    ip->setButtonObj(interfaceButtonRight, buttonPress, functParamsScreenRight);
    ip->setButtonObj(interfaceButtonUp, buttonPress, functParamsScreenUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functParamsScreenDown);
    ip->setButtonObj(interfaceButtonDelete, buttonPress, functParamsScreenDelete);
    ip->setButtonObj(interfaceButtonInsert, buttonPress, functParamsScreenInsert);
    ip->setButtonObj(interfaceButton4, buttonPress, functParamsScreenMore);

    if (isModularPresetPage())
    {
        for (uint8_t i = interfaceButton0; i <= interfaceButton2; i++)
        {
            ip->setButtonObj(i, buttonPress, functSelectParameter);
        }
    }
    else
    {
        int currentPageParams = getCurrentPageParams();
        for (uint8_t i = interfaceButton0; i <= interfaceButton3; i++)
        {
            if (i < currentPageParams)
            {
                ip->setButtonObj(i, buttonPress, functSelectParameter);
            }
            else
            {
                break;
            }
        }
    }
    ip->setButtonObj(interfaceButton5, buttonPress, functBack);
    switch (previewState)
    {
        case previewStatePreview:
            ip->setButtonObj(interfaceButton6, buttonPress, functPreview);
            break;
        case previewStatePlay: ip->setButtonObj(interfaceButton6, buttonPress, functPlay); break;
        case previewStateStop: ip->setButtonObj(interfaceButton6, buttonPress, functStop); break;
    }

    ip->setButtonObj(interfaceButton7, buttonPress, functSave);

    ip->setPotObj(interfacePot0, functParamsScreenEncoder);
}
void cSampleEditor::setStopSeqFunctions()
{
    ip->clearAll();
    ip->setButtonObj(interfaceButton6, buttonPress, functStopPatternNo);
    ip->setButtonObj(interfaceButton7, buttonPress, functStopPatternYes);
}
void cSampleEditor::setTooLongSampleFunctions()
{
    ip->clearAll();
    ip->setButtonObj(interfaceButton7, buttonPress, functTooLongSampleOk);
}
void cSampleEditor::setTooLongProcessedSampleFunctions()
{
    ip->clearAll();
    ip->setButtonObj(interfaceButton7, buttonPress, functTooLongProcessedSampleOk);
}
bool cSampleEditor::isParametersScreen()
{
    if (screenType == mainScreen && PtfxManager::isLoadEffectListIndex(selectedEffectListIdx))
    {
        return false;
    }
    if (currentEffectIdx == editorEffectModular)
    {
        return PtfxManager::hasLoadedPlugin() &&
               (getCurrentEffectDisplayParams().paramsNumber == 0);
    }
    return (currentEffectIdx == editorEffectCut) || (currentEffectIdx == editorEffectReverse) ||
           (currentEffectIdx == editorEffectFadeIn) || (currentEffectIdx == editorEffectFadeOut);
}

bool cSampleEditor::canApplyCurrentEffect() const
{
    if (screenType == mainScreen && PtfxManager::isLoadEffectListIndex(selectedEffectListIdx))
    {
        return true;
    }

    bool hasSampleData = (editorInstrument != nullptr &&
                          editorInstrument->isActive &&
                          editorInstrument->sample.length > 0) ||
                         (currentEffect != nullptr &&
                          currentEffect->isDataLoaded() &&
                          currentEffect->getLengthToPolyphonicPlay() > 0);
    if (hasSampleData)
    {
        if (currentEffectIdx == editorEffectModular && !PtfxManager::hasLoadedPlugin())
        {
            return false;
        }
        return true;
    }

    if (currentEffectIdx == editorEffectModular && PtfxManager::hasLoadedPlugin() &&
        (sampleEditorModular.getFlags() & PTFX_FLAG_GENERATOR) != 0)
    {
        return true;
    }

    return false;
}

void cSampleEditor::handleStartupWithPopupAndMenu()
{
    ip->setButtonObj(interfaceButtonMiniMenu, functMenu);

    prepareDisplayDataMainScreen();
    showMainScreen();
}

void cSampleEditor::switchScreen(enScreenType s)
{
    if (s == mainScreen)
    {
        prepareDisplayDataMainScreen();
        showMainScreen();
        setMainScreenFunctions();
    }
    else if (s == effectScreen)
    {
        prepareDisplayDataParamsScreen();
        showEffectParamsScreen();
        setParamsScreenFunctions();
    }
}

void cSampleEditor::restoreFunctions()
{
    setCommonFunctions();
    if (screenType == mainScreen)
        setMainScreenFunctions();
    else if (screenType == effectScreen)
        setParamsScreenFunctions();
}

void cSampleEditor::noteOnHandle(uint8_t channel,
                                 uint8_t note,
                                 [[maybe_unused]] uint8_t velocity,
                                 int16_t source)
{
    if (mtPadBoard.getEmptyVoice() == 0)
    {
        SE->needRefreshPlayhead = 1;
        SE->playheadValue = 0;
    }

    int16_t padboardSource = (channel == Sequencer::GRID_OUTSIDE_PATTERN) ? source : 100 + channel;

    uint32_t length = (uint32_t)((uint32_t)SE->selection.endPoint *
                                 (float)(SE->editorInstrument->sample.length) / MAX_16BIT);
    uint32_t addressShift = (uint32_t)((uint32_t)SE->selection.startPoint *
                                       (float)(SE->editorInstrument->sample.length) / MAX_16BIT);

    std::vector<int16_t*> addresses;

    for (int channel = 0; channel < strSampleMemory::Channel::kCount; channel++)
    {
        addresses.push_back(sampleEditorSampleMemory.address[channel] + addressShift);
    }

    uint8_t vol = mtProject.instrument[mtProject.values.lastUsedInstrument].volume;
    if (vol == 0)
    {
        vol = DEFAULT_INSTRUMENT_VOLUME;
    }

    mtPadBoard.startNoteWithoutInstrument(
      note + SE->editorInstrument->tuneResample,
      addresses,
      length - addressShift,
      vol,
      padboardSource);
}
void cSampleEditor::noteOffHandle(uint8_t channel,
                                  [[maybe_unused]] uint8_t note,
                                  [[maybe_unused]] uint8_t velocity,
                                  int16_t source)
{
    int16_t padboardSource = (channel == Sequencer::GRID_OUTSIDE_PATTERN) ? source : 100 + channel;

    if (mtPadBoard.getVoiceTakenByPad(padboardSource) == 0)
    {
        SE->needRefreshPlayhead = 0;
        SE->playheadValue = 0;
        SE->hidePlayhead();
    }
    mtPadBoard.stopInstrument(padboardSource);
}

bool cSampleEditor::isCurrentSampleStereo()
{
    if (confirmedDataIsChanged && currentEffect != nullptr)
    {
        if (currentEffectIdx == editorEffectModular &&
            (sampleEditorModular.getFlags() & PTFX_FLAG_CONVERT_TO_MONO))
        {
            return false;
        }

        if (!newFileManager.isInstrumentStereo(mtProject.values.lastUsedInstrument))
        {
            const int16_t* left  = currentEffect->getAddresToPolyphonicPlay(mtEffect::Channel::kLeft);
            const int16_t* right = currentEffect->getAddresToPolyphonicPlay(mtEffect::Channel::kRight);
            uint32_t       len   = currentEffect->getLengthToPolyphonicPlay();
            if (left != nullptr && right != nullptr && left != right && len > 0 &&
                memcmp(left, right, len * sizeof(int16_t)) != 0)
            {
                return true;
            }
        }
    }
    return newFileManager.isInstrumentStereo(mtProject.values.lastUsedInstrument);
}

void cSampleEditor::prepareDisplayDataMainScreen()
{
    reloadCurrentEffect();
    reloadStartPointText();
    reloadEndPointText();
    reloadZoomText();
    reloadPointsData();
    reloadFrameData(screenType);

    if (spectrumParams[0].address != nullptr && spectrumParams[0].length > 0)
    {
        GP.processSpectrum(&spectrumParams[0], &zoom, &spectrumData[0]);
    }
    else
    {
        for (uint16_t i = 0; i < 600; i++)
        {
            spectrumData[0].upperData[i] = 0;
            spectrumData[0].lowerData[i] = 0;
        }
        spectrumData[0].spectrumType = 1;
    }

    if (isCurrentSampleStereo() &&
        spectrumParams[1].address != nullptr && spectrumParams[1].length > 0)
    {
        GP.processSpectrum(&spectrumParams[1], &zoom, &spectrumData[1]);
    }
    else
    {
        for (uint16_t i = 0; i < 600; i++)
        {
            spectrumData[1].upperData[i] = 0;
            spectrumData[1].lowerData[i] = 0;
        }
        spectrumData[1].spectrumType = 1;
    }
}

void cSampleEditor::reloadStartPointText()
{
    float length = samplePosToTime(editorInstrument->sample.length);
    float localStartPoint = (length * selection.startPoint) / MAX_16BIT;

    sprintf(startPointText, "%.3fs", localStartPoint);
}
void cSampleEditor::reloadEndPointText()
{
    float length = samplePosToTime(editorInstrument->sample.length);
    float localEndPoint = (length * selection.endPoint) / MAX_16BIT;

    sprintf(endPointText, "%.3fs", localEndPoint);
}
void cSampleEditor::reloadZoomText()
{
    sprintf(zoomText, "%.2f", zoom.zoomValue);
}
void cSampleEditor::reloadPlayheadValue()
{
    constexpr uint16_t PLAYHEAD_REFRESH_CONSTRAIN_US = 3000;

    if (refreshPlayheadTimer > PLAYHEAD_REFRESH_CONSTRAIN_US)
    {
        refreshPlayheadTimer = 0;

        int16_t* currentPlayingStartAddress = instrumentPlayer[0].getWaveStartAddressIfPlaying(0);
        uint32_t currentPlayingLength = instrumentPlayer[0].getLengthIfPlaying();

        uint16_t startRelativeToRange = 0;
        if (currentPlayingStartAddress)
        {
            float addressOffset =
              (float)(currentPlayingStartAddress - SE->sampleEditorSampleMemory.address[0]);
            startRelativeToRange =
              (addressOffset / SE->editorInstrument->sample.length) * MAX_16BIT;
        }
        uint16_t endRelativeToRange = 0;
        if (currentPlayingLength)
        {
            float addressOffset =
              (float)((currentPlayingStartAddress - SE->sampleEditorSampleMemory.address[0]) +
                      currentPlayingLength);
            endRelativeToRange = (addressOffset / SE->editorInstrument->sample.length) * MAX_16BIT;
        }

        uint16_t startPoint =
          currentPlayingStartAddress ? startRelativeToRange : selection.startPoint;
        uint16_t endPoint = currentPlayingLength ? endRelativeToRange : selection.endPoint;

        uint16_t playheadValue_16BIT = (uint16_t)((instrumentPlayer[0].getWavePosition() *
                                                   ((endPoint - startPoint) / (float)MAX_16BIT)) +
                                                  startPoint);

        if (zoom.zoomValue == 1.0)
            playheadValue = (600 * playheadValue_16BIT) / MAX_16BIT;
        else if (zoom.zoomValue > 1.0)
        {
            if ((int32_t)playheadValue_16BIT < zoom.zoomStart ||
                (int32_t)playheadValue_16BIT > zoom.zoomEnd)
                playheadValue = 0;
            else
            {
                playheadValue =
                  mapClamped<int>(playheadValue_16BIT, zoom.zoomStart, zoom.zoomEnd, 0, 600);
            }
        }
        needShowPlayhead = 1;
    }
}

void cSampleEditor::reloadCurrentEffect()
{
    currentEffect = sampleEditorEffect[currentEffectIdx];

    if ((currentEffect != lastEffect) && (lastEffect != nullptr))
    {
        currentEffect->switchToThisEffect(lastEffect);
    }

    lastEffect = currentEffect;
}

void cSampleEditor::reloadSpectrumData()
{
    sampleEditorSampleMemory.address[mtEffect::Channel::kLeft] =
      currentEffect->getAddresToPolyphonicPlay(mtEffect::Channel::kLeft);
    sampleEditorSampleMemory.address[mtEffect::Channel::kRight] =
      currentEffect->getAddresToPolyphonicPlay(mtEffect::Channel::kRight);
    editorInstrument->sample.length = currentEffect->getLengthToPolyphonicPlay();
    spectrumParams[mtEffect::Channel::kLeft].address =
      sampleEditorSampleMemory.address[mtEffect::Channel::kLeft];
    spectrumParams[mtEffect::Channel::kLeft].length = editorInstrument->sample.length;

    spectrumParams[mtEffect::Channel::kRight].address =
      sampleEditorSampleMemory.address[mtEffect::Channel::kRight];
    spectrumParams[mtEffect::Channel::kRight].length = editorInstrument->sample.length;
}

// popups
void cSampleEditor::reloadApplyingProgress()
{
    if (applyingSteps == 1)
    {
        applyingProgress = currentEffect->getProcessingSelectionProgress();
    }
    else if (applyingSteps == 2)
    {
        if (!isLoadedData)
        {
            applyingProgress = currentEffect->getLoadingProgress() / 2;
        }
        else
        {
            applyingProgress = 50 + currentEffect->getProcessingSelectionProgress() / 2;
        }
    }
}

void cSampleEditor::reloadProcessingProgress()
{
    if (processingSteps == 1)
    {
        processingProgress = currentEffect->getProcessingSelectionProgress();
    }
    else if (processingSteps == 2)
    {
        if (!isLoadedData)
        {
            processingProgress = currentEffect->getLoadingProgress() / 2;
        }
        else
        {
            processingProgress = 50 + currentEffect->getProcessingSelectionProgress() / 2;
        }
    }
}

void cSampleEditor::reloadPlayingProgress()
{
    playingProgress = (instrumentPlayer[0].getWavePosition() * 100) / MAX_16BIT;
}

void cSampleEditor::reloadFrameData(enScreenType s)
{
    if (s == mainScreen)
    {
        frameData.placesCount = 4;

        for (uint8_t i = 0; i < frameData.placesCount; i++)
        {
            frameData.places[i] = &mainScreenFramePlaces[i][0];
        }
    }
    else if (s == effectScreen)
    {
        frameData.placesCount = getCurrentPageParams();

        for (uint8_t i = 0; i < frameData.placesCount; i++)
        {
            frameData.places[i] = &paramsScreenFramePlaces[i][0];
        }
    }
}
void cSampleEditor::reloadPointsData()
{
    if (selectedPlace[cSampleEditor::mainScreen] == cSampleEditor::startPointButton)
    {
        spectrumPointsData.selected = selectStart;
        // zoom.zoomPosition = selection.startPoint;

        bool isZoomActive = (zoom.zoomValue > 1);
        bool isPointDisplayed =
          (selection.startPoint < zoom.zoomStart || selection.startPoint > zoom.zoomEnd);

        if (isZoomActive && isPointDisplayed)
        {
            needRefreshSpectrum = 1;
        }
    }
    else if (selectedPlace[cSampleEditor::mainScreen] == cSampleEditor::endPointButton)
    {
        spectrumPointsData.selected = selectEnd;
        // zoom.zoomPosition = selection.endPoint;

        bool isZoomActive = (zoom.zoomValue > 1);
        bool isPointDisplayed =
          (selection.endPoint < zoom.zoomStart || selection.endPoint > zoom.zoomEnd);
        if (isZoomActive && isPointDisplayed)
        {
            needRefreshSpectrum = 1;
        }
    }
    else
    {
        spectrumPointsData.selected = 0;
    }

    /*
        if(needRefreshSpectrum)
        {
            zoom.zoomStart =  zoom.zoomPosition - zoom.zoomWidth/2;
            zoom.zoomEnd = zoom.zoomPosition + zoom.zoomWidth/2;
            if(zoom.zoomStart < 0)
            {
                zoom.zoomEnd = zoom.zoomWidth;
                zoom.zoomStart = 0;
            }
            else if(zoom.zoomEnd > MAX_16BIT)
            {
                zoom.zoomEnd = MAX_16BIT;
                zoom.zoomStart = MAX_16BIT-zoom.zoomWidth;
            }
        }
    */

    if (selection.startPoint >= zoom.zoomStart && selection.startPoint <= zoom.zoomEnd)
    {
        spectrumPointsData.startPoint =
          ((selection.startPoint - zoom.zoomStart) * (spectrumData[0].width - 1)) / zoom.zoomWidth;
    }
    else
        spectrumPointsData.startPoint = -1;

    if (selection.endPoint >= zoom.zoomStart && selection.endPoint <= zoom.zoomEnd)
    {
        spectrumPointsData.endPoint =
          ((selection.endPoint - zoom.zoomStart) * (spectrumData[0].width - 1)) / zoom.zoomWidth;
    }
    else
        spectrumPointsData.endPoint = -1;
}

// params screen
void cSampleEditor::prepareDisplayDataParamsScreen()
{
    selectedPlace[effectScreen] = 0;
    effectScreenPage = 0;

    if (currentEffectIdx == editorEffectModular)
    {
        snprintf(paramsScreenLabel, 40, "Sample Editor %s",
                 PtfxManager::getActivePluginDisplayName());
    }
    else
    {
        snprintf(paramsScreenLabel, 40, "Sample Editor %s", effectNamesLabels[currentEffectIdx]);
    }
    paramsScreenLabel[39] = 0;

    for (uint8_t i = 0; i < getCurrentEffectDisplayParams().paramsNumber; i++)
    {
        reloadParameterValueText(i);
        reloadParameterBarValue(i);
    }
    reloadFrameData(effectScreen);
}
void cSampleEditor::reloadParameterValueText(uint8_t idx)
{
    const auto& params = getCurrentEffectDisplayParams();
    if (idx >= params.paramsNumber)
    {
        return;
    }

    if (params.displayType[idx] == 'w' || params.displayType[idx] == 's')
    {
        const char* fullPath = PtfxLoader::getSelectedFilePath(idx);
        if (fullPath != nullptr && fullPath[0] != '\0')
        {
            const char* slash = strrchr(fullPath, '/');
            const char* name = (slash != nullptr) ? (slash + 1) : fullPath;
            strncpy(parameterFileText[idx], name, sizeof(parameterFileText[idx]) - 1);
            parameterFileText[idx][sizeof(parameterFileText[idx]) - 1] = '\0';
        }
        else
        {
            parameterFileText[idx][0] = '\0';
        }
        strcpy(parameterValueLabelPtr[idx],
               (params.displayType[idx] == 's') ? "Save..." : "Select...");
        return;
    }

    if (params.displayType[idx] == 'l')
    {
        uint8_t count = getParameterListCount(idx);
        char** items = getParameterListItems(idx);
        int selectedIdx = clamp<int>(
          (int)(effectDisplayParamsToAdjust[currentEffectIdx].parameter[idx] + 0.5f),
          0,
          count > 0 ? (int)count - 1 : 0);
        if (items && count > 0 && items[selectedIdx])
        {
            strncpy(parameterValueLabelPtr[idx], items[selectedIdx], 11);
            parameterValueLabelPtr[idx][11] = '\0';
        }
        else
        {
            sprintf(parameterValueLabelPtr[idx], "%d", selectedIdx);
        }
        return;
    }

    float displayedValue = effectDisplayParamsToAdjust[currentEffectIdx].parameter[idx] *
                           params.displayMult[idx];

    if (params.displayType[idx] == 'd')
    {
        sprintf(parameterValueLabelPtr[idx], "%d", (int)displayedValue);
    }
    else if (params.displayType[idx] == 'f')
    {
        sprintf(parameterValueLabelPtr[idx], "%.2f", displayedValue);
    }

    if (params.afterValueText[idx] != nullptr)
    {
        strcat(parameterValueLabelPtr[idx],
               params.afterValueText[idx]);
    }
}

void cSampleEditor::reloadParameterBarValue(uint8_t n)
{
    const auto& params = getCurrentEffectDisplayParams();
    if (n >= params.paramsNumber)
        return;

    float range = params.upConstrain[n] - params.downConstrain[n];
    if (range <= 0.0f)
    {
        paramsBarValue[n] = 0;
        return;
    }

    paramsBarValue[n] = (((effectDisplayParamsToAdjust[currentEffectIdx].parameter[n] -
                           params.downConstrain[n]) *
                          100) /
                         range);
}

//******************* REFRESH - RELOAD + DISPLAY
void cSampleEditor::refreshSpectrumPoints()
{
    if (screenType != mainScreen)
        return;
    reloadPointsData();
    showSpectrumPoints();
}

void cSampleEditor::refreshStartPoint()
{
    if (screenType != mainScreen)
        return;
    needRefreshPoints = 1;

    reloadStartPointText();
    showStartPointText();
}
void cSampleEditor::refreshEndPoint()
{
    if (screenType != mainScreen)
        return;
    needRefreshPoints = 1;

    reloadEndPointText();
    showEndPointText();
}
void cSampleEditor::refreshZoom()
{
    reloadZoomText();
    showZoomText();
}
void cSampleEditor::refreshEffectList()
{
    display.setControlValue(effectList, selectedEffectListIdx);
    showEffectList();

    if (SE->isParametersScreen())
    {
        display.setControlText(label[5], "Apply");
        display.setControlText2(label[5], "");
        display.setControlValue(label[5], 0);
    }
    else
    {
        display.setControlText(label[5], "Select");
        display.setControlText2(label[5], "effect");
        display.setControlValue(label[5], 1);
    }
    display.setControlColors(
      label[5],
      canApplyCurrentEffect() ? display.getTheme().activeButtonLabelsColors
                              : display.getTheme().inactiveLabelsColors);
    display.refreshControl(label[5]);

    if (SE->isParametersScreen())
    {
        strcpy(paramsScreenLabel, "Sample Editor");
    }
    else
    {
        strcpy(paramsScreenLabel, "Sample Editor 1/2");
    }

    showTitleBar(paramsScreenLabel);

    display.synchronizeRefresh();
}

void cSampleEditor::refreshSpectrum()
{
    if (screenType != mainScreen)
        return;

    GP.processSpectrum(&spectrumParams[0], &zoom, &spectrumData[0]);
    if (isCurrentSampleStereo())
    {
        GP.processSpectrum(&spectrumParams[1], &zoom, &spectrumData[1]);
    }

    showSpectrum();
}

void cSampleEditor::refreshPlayhead()
{
    if (screenType != mainScreen)
        return;

    reloadPlayheadValue();
    if (needShowPlayhead)
    {
        needShowPlayhead = 0;
        display.setControlValue(playhead, playheadValue);
        showPlayhead();
    }
}

void cSampleEditor::refreshUndoState()
{
    if ((editorInstrument->isActive) && (screenType == mainScreen))
    {
        if (!currentEffect->undoParams.isEnable)
            display.setControlColors(label[3], display.getTheme().inactiveLabelsColors);
        else
            display.setControlColors(label[3], display.getTheme().activeLabelsColors);

        display.refreshControl(label[3]);
    }
}

void cSampleEditor::refreshParameter(uint8_t n)
{
    reloadParameterBarValue(n);
    reloadParameterValueText(n);

    showParameterBar(n);
    showParameterLabel(n);
}

void cSampleEditor::refreshApplyingProgress()
{
    reloadApplyingProgress();
    showProgressApplying();
}

void cSampleEditor::refreshProcessingProgress()
{
    reloadProcessingProgress();
    showProgressProcessing();
}

void cSampleEditor::refreshPlayingProgress()
{
    reloadPlayingProgress();
    showProgressPlaying();
}

//******************* PARAMETERS MODIFICATORS
// MainScreen
void cSampleEditor::modStartPoint(int16_t val)
{
    uint16_t move_step = zoom.zoomWidth / 600;
    val *= move_step;

    if (selection.startPoint + val > MAX_16BIT)
        selection.startPoint = MAX_16BIT;
    else if (selection.startPoint + val < 0)
        selection.startPoint = 0;
    else
        selection.startPoint += val;

    if (selection.startPoint >= selection.endPoint)
    {
        selection.startPoint = selection.endPoint - 1;
    }

    bool isZoomActive = (zoom.zoomValue > 1);
    bool isNewPointChanged = zoom.lastChangedPoint != 1;
    bool isPointDisplayed =
      (selection.startPoint < zoom.zoomStart || selection.startPoint > zoom.zoomEnd);

    if (isZoomActive && (isNewPointChanged || isPointDisplayed))
    {
        needRefreshSpectrum = 1;
    }

    zoom.zoomPosition = selection.startPoint;
    zoom.lastChangedPoint = 1;

    currentEffect->changeSelectionRange(selection.startPoint, selection.endPoint);

    refreshStartPoint();
}

void cSampleEditor::modEndPoint(int16_t val)
{
    uint16_t move_step = zoom.zoomWidth / 600;
    val *= move_step;

    if (selection.endPoint + val > MAX_16BIT)
        selection.endPoint = MAX_16BIT;
    else if (selection.endPoint + val < 0)
        selection.endPoint = 0;
    else
        selection.endPoint += val;

    if (selection.startPoint >= selection.endPoint)
    {
        selection.endPoint = selection.startPoint + 1;
    }

    bool isZoomActive = (zoom.zoomValue > 1);
    bool isNewPointChanged = zoom.lastChangedPoint != 2;
    bool isPointDisplayed =
      (selection.endPoint < zoom.zoomStart || selection.endPoint > zoom.zoomEnd);

    if (isZoomActive && (isNewPointChanged || isPointDisplayed))
    {
        needRefreshSpectrum = 1;
    }

    zoom.zoomPosition = selection.endPoint;
    zoom.lastChangedPoint = 2;

    currentEffect->changeSelectionRange(selection.startPoint, selection.endPoint);

    refreshEndPoint();
}

void cSampleEditor::modZoom(int16_t val)
{
    GP.spectrumChangeZoom(val, editorInstrument->sample.length, &zoom);

    refreshZoom();
    needRefreshSpectrum = 1;
    needRefreshPoints = 1;
}

void cSampleEditor::resetZoom()
{
    if (zoom.zoomValue == 1.0f)
        return;

    GP.spectrumResetZoom(zoom.zoomPosition, editorInstrument->sample.length, &zoom);

    refreshZoom();
    needRefreshPoints = 1;
    needRefreshSpectrum = 1;
}

void cSampleEditor::modSelectedEffect(int16_t val)
{
    val = clamp<int>(val, -5, 5);

    uint16_t lastSelectedIdx = selectedEffectListIdx;
    uint16_t totalEffects = PtfxManager::getTotalEffectListCount();
    if (totalEffects == 0)
        totalEffects = kBuiltInEffectsCount;

    if ((int32_t)selectedEffectListIdx + val > (int32_t)(totalEffects - 1))
        selectedEffectListIdx = totalEffects - 1;
    else if ((int32_t)selectedEffectListIdx + val < 0)
        selectedEffectListIdx = 0;
    else
        selectedEffectListIdx += val;

    if (selectedEffectListIdx != lastSelectedIdx)
    {
        if (selectedEffectListIdx < kBuiltInEffectsCount)
        {
            currentEffectIdx = (uint8_t)selectedEffectListIdx;
            currentEffect = sampleEditorEffect[currentEffectIdx];
            reloadCurrentEffect();

            currentEffect->clearProcessedData();
            currentEffect->setBaseSampleLength(editorInstrument->sample.length);
            currentEffect->changeSelectionRange(selection.startPoint, selection.endPoint);

            for (uint8_t idx = 0; idx < getCurrentEffectDisplayParams().paramsNumber; idx++)
            {
                currentEffect->setParameter(
                  effectDisplayParamsToAdjust[currentEffectIdx].parameter[idx], idx);
            }
        }
        else if (PtfxManager::isLoadedPluginListIndex(selectedEffectListIdx))
        {
            currentEffectIdx = editorEffectModular;
            currentEffect = sampleEditorEffect[editorEffectModular];

            float savedParams[12]{};
            for (uint8_t i = 0; i < 12; i++)
            {
                savedParams[i] = effectDisplayParamsToAdjust[editorEffectModular].parameter[i];
            }

            if (PtfxManager::reloadActivePlugin(sampleEditorModular))
            {
                loadModularEffectUI(PtfxManager::getActiveHeader());
                for (uint8_t i = 0; i < getCurrentEffectDisplayParams().paramsNumber && i < 12; i++)
                {
                    effectDisplayParamsToAdjust[editorEffectModular].parameter[i] = savedParams[i];
                }
            }
            reloadCurrentEffect();

            currentEffect->clearProcessedData();
            currentEffect->setBaseSampleLength(editorInstrument->sample.length);
            currentEffect->changeSelectionRange(selection.startPoint, selection.endPoint);

            for (uint8_t idx = 0; idx < getCurrentEffectDisplayParams().paramsNumber; idx++)
            {
                currentEffect->setParameter(
                  effectDisplayParamsToAdjust[currentEffectIdx].parameter[idx], idx);
            }
        }
        else
        {
            // "Load Effect..." selected: refresh list UI without loading a plugin yet
        }

        refreshEffectList();
        previewState = previewStatePreview;
    }
}

void cSampleEditor::setDefaultStartPoint()
{
    selection.startPoint = 0;

    if (selection.startPoint >= selection.endPoint)
    {
        selection.startPoint = selection.endPoint - 1;
    }

    bool isZoomActive = (zoom.zoomValue > 1);
    bool isNewPointChanged = zoom.lastChangedPoint != 1;
    bool isPointDisplayed =
      (selection.startPoint < zoom.zoomStart || selection.startPoint > zoom.zoomEnd);

    if (isZoomActive && (isNewPointChanged || isPointDisplayed))
    {
        needRefreshSpectrum = 1;
    }

    zoom.zoomPosition = selection.startPoint;
    zoom.lastChangedPoint = 1;

    currentEffect->changeSelectionRange(selection.startPoint, selection.endPoint);

    refreshStartPoint();
}

void cSampleEditor::setDefaultEndPoint()
{
    selection.endPoint = MAX_16BIT;

    if (selection.startPoint >= selection.endPoint)
    {
        selection.endPoint = selection.startPoint + 1;
    }

    bool isZoomActive = (zoom.zoomValue > 1);
    bool isNewPointChanged = zoom.lastChangedPoint != 2;
    bool isPointDisplayed =
      (selection.endPoint < zoom.zoomStart || selection.endPoint > zoom.zoomEnd);

    if (isZoomActive && (isNewPointChanged || isPointDisplayed))
    {
        needRefreshSpectrum = 1;
    }

    zoom.zoomPosition = selection.endPoint;
    zoom.lastChangedPoint = 2;

    currentEffect->changeSelectionRange(selection.startPoint, selection.endPoint);

    refreshEndPoint();
}

void cSampleEditor::setDefaultZoom()
{
    resetZoom();
}

//*******************
// ParameterScreen
void cSampleEditor::modParameter(int16_t value, uint8_t idx)
{
    const auto& params = getCurrentEffectDisplayParams();
    float localParam = effectDisplayParamsToAdjust[currentEffectIdx].parameter[idx];
    float localVal = (value * params.changeStep[idx]);

    effectDisplayParamsToAdjust[currentEffectIdx].parameter[idx] =
      clamp<float>(localParam + localVal,
                   params.downConstrain[idx],
                   params.upConstrain[idx]);

    currentEffect->setParameter(effectDisplayParamsToAdjust[currentEffectIdx].parameter[idx], idx);

    setPreviewFunction();
    currentEffect->clearProcessedData();
    refreshParameter(idx);
}

void cSampleEditor::setDefaultParameter(uint8_t idx)
{
    if (currentEffectIdx == editorEffectModular)
    {
        const PtfxHeader& header = PtfxManager::getActiveHeader();
        if (idx < header.num_params)
        {
            if (header.params[idx].display_type == PARAM_FILE ||
                header.params[idx].display_type == PARAM_SAVE_FILE)
            {
                PtfxLoader::setSelectedFilePath(idx, "");
                parameterFileText[idx][0] = '\0';
                if (header.params[idx].display_type == PARAM_FILE)
                {
                    bool hasOtherWav = false;
                    uint8_t paramCount =
                      std::min<uint8_t>(header.num_params, kMaxParametersInEffect);
                    for (uint8_t j = 0; j < paramCount; j++)
                    {
                        if (j == idx || header.params[j].display_type != PARAM_FILE)
                        {
                            continue;
                        }
                        char extBuf[7]{};
                        strncpy(extBuf, header.params[j].unit, sizeof(header.params[j].unit));
                        const char* extStr = (extBuf[0] != '\0') ? extBuf : ".wav";
                        if (FileBrowser::checkFilenameExtension(extStr, ".wav"))
                        {
                            const char* otherPath = PtfxLoader::getSelectedFilePath(j);
                            if (otherPath != nullptr && otherPath[0] != '\0')
                            {
                                hasOtherWav = true;
                                break;
                            }
                        }
                    }
                    if (!hasOtherWav)
                    {
                        PtfxLoader::loadSecondaryWavFile("");
                    }
                }
                effectDisplayParamsToAdjust[currentEffectIdx].parameter[idx] = 0.0f;
            }
            else
            {
                effectDisplayParamsToAdjust[currentEffectIdx].parameter[idx] =
                  header.params[idx].default_val;
            }
        }
    }
    else
    {
        effectDisplayParamsToAdjust[currentEffectIdx].parameter[idx] =
          effectDefaultParamsPtr[currentEffectIdx].parameter[idx];
    }
    currentEffect->setParameter(effectDisplayParamsToAdjust[currentEffectIdx].parameter[idx], idx);

    setPreviewFunction();
    currentEffect->clearProcessedData();
    refreshParameter(idx);
}

int cSampleEditor::getSelectedParameterIdx()
{
    return effectScreenPage * kEffectPageParams + selectedPlace[enScreenType::effectScreen];
}

bool cSampleEditor::isModularPresetPage() const
{
    if (currentEffectIdx != editorEffectModular || getCurrentEffectDisplayParams().paramsNumber == 0)
    {
        return false;
    }
    int paramPages = ((getCurrentEffectDisplayParams().paramsNumber - 1) / kEffectPageParams) + 1;
    return effectScreenPage >= paramPages;
}

int cSampleEditor::getParamsPagesCount()
{
    if (getCurrentEffectDisplayParams().paramsNumber == 0)
        return 0;

    int paramPages = ((getCurrentEffectDisplayParams().paramsNumber - 1) / kEffectPageParams) + 1;
    if (currentEffectIdx == editorEffectModular)
    {
        return paramPages + 1;
    }
    return paramPages;
}

int cSampleEditor::getCurrentPageParams()
{
    if (isModularPresetPage())
    {
        return 0;
    }

    assert((int)effectScreenPage * kEffectPageParams <=
           (int)getCurrentEffectDisplayParams().paramsNumber);

    return clamp((int)getCurrentEffectDisplayParams().paramsNumber -
                   (int)effectScreenPage * kEffectPageParams,
                 0,
                 kEffectPageParams);
}

bool cSampleEditor::areThereMoreThanOneParamsPage()
{
    return getParamsPagesCount() > 1;
}

void cSampleEditor::setPreviewFunction()
{
    ip->clearButton(interfaceButton6);
    ip->setButtonObj(interfaceButton6, buttonPress, functPreview);

    previewState = previewStatePreview;

    showPreviewLabel();
}

void cSampleEditor::setPlayFunction()
{
    ip->clearButton(interfaceButton6);
    ip->setButtonObj(interfaceButton6, buttonPress, functPlay);

    previewState = previewStatePlay;

    showPlayLabel();
}

void cSampleEditor::setStopFunction()
{
    ip->clearAll();
    ip->clearButton(interfaceButton6);
    ip->setButtonObj(interfaceButton6, buttonPress, functStop);

    previewState = previewStateStop;

    showStopLabel();
}

// global popups
void cSampleEditor::cancelGlobalPopups()
{
    if (mtPopups.getStepPopupState() != stepPopupNone)
    {
        mtPopups.hideStepPopups();
        if (confirmedDataIsChanged)
        {
            reloadOnEndSaving = true;
            bool isStereo = isCurrentSampleStereo();

            if (newFileManager.importSampleFromSampleEditor(
                  SE->currentEffect->getAddresToPolyphonicPlay(mtEffect::Channel::kLeft),
                  SE->currentEffect->getAddresToPolyphonicPlay(isStereo ? mtEffect::Channel::kRight
                                                                        : mtEffect::Channel::kLeft),
                  SE->currentEffect->getLengthToPolyphonicPlay(),
                  mtProject.values.lastUsedInstrument))
            {
                SE->ip->blockAllInputs();
            }
        }
        else
        {
            start(0);
        }
    }
}

float cSampleEditor::samplePosToTime(uint32_t pos)
{
    return pos / (44100.0 / (1 + editorInstrument->tuneResample / (-12)));
}

//************************************************************************** ACTION FUNCTIONS
static void functSwitchModule(uint8_t button)
{

    if (button == interfaceButtonSampleEdit)
    {
        SE->restoreCurrentScreenFunctions();
    }
    else
    {
        if (SE->confirmedDataIsChanged)
        {
            SE->moduleToChange = button;

            bool isStereo = SE->isCurrentSampleStereo();

            if (newFileManager.importSampleFromSampleEditor(
                  SE->currentEffect->getAddresToPolyphonicPlay(mtEffect::Channel::kLeft),
                  SE->currentEffect->getAddresToPolyphonicPlay(isStereo ? mtEffect::Channel::kRight
                                                                        : mtEffect::Channel::kLeft),
                  SE->currentEffect->getLengthToPolyphonicPlay(),
                  mtProject.values.lastUsedInstrument))
            {
                SE->ip->blockAllInputs();
            }
            //			SE->showPopupSaveChangesWindow();
            //			SE->setSaveChangesFunctions();
            //			SE->moduleToChange = button;
        }
        else
            SE->eventFunct(eventSwitchModule, SE, &button, 0);
    }
}

static void functMiniMiddle(uint8_t state)
{
    static uint8_t lastState = buttonRelease;

    if (state == lastState)
    {
        return;
    }

    lastState = state;

    // Hold is set to button function with Press state . We have to handle it by rejecting
    // repetitive states if we don't want hold.

    const int pad = 12; // pad id is not relevant for mini

    if (state == buttonPress)
    {
        SE->noteOnHandle(Sequencer::GRID_OUTSIDE_PATTERN, kC5Note, 100, pad);
    }
    else if (state == buttonRelease)
    {
        SE->noteOffHandle(Sequencer::GRID_OUTSIDE_PATTERN, kC5Note, 0, pad);
    }
}

static void functPads(uint8_t pad, uint8_t state, [[maybe_unused]] int16_t velo)
{
    if (SE->screenType != cSampleEditor::enScreenType::mainScreen)
    {
        return;
    }

    if (state == buttonPress)
    {
        padsBacklight.setFrontLayer(1, mtConfig.values.padsLightFront, pad);
        uint8_t noteFromPad = mtPadBoard.getNoteFromPad(pad);
        SE->noteOnHandle(Sequencer::GRID_OUTSIDE_PATTERN, noteFromPad, 100, pad);
    }
    else if (state == buttonRelease)
    {
        padsBacklight.setFrontLayer(0, 0, pad);
        uint8_t noteFromPad = mtPadBoard.getNoteFromPad(pad);
        SE->noteOffHandle(Sequencer::GRID_OUTSIDE_PATTERN, noteFromPad, 0, pad);
    }
}

static void functInstrument(uint8_t state)
{
    if (state == buttonRelease)
    {
        SE->afterReloadInstrumentIdx = mtProject.values.lastUsedInstrument;
        if (SE->confirmedDataIsChanged)
            mtProject.values.lastUsedInstrument = SE->beforeReloadInstrumentIdx;
        SE->cancelGlobalPopups();
        SE->currentEffect->undoParams.isEnable = false;
    }
    else if (state == buttonPress)
    {
        SE->hideUselessControls();
        SE->beforeReloadInstrumentIdx = mtProject.values.lastUsedInstrument;
        mtPopups.showStepPopup(stepPopupInstr, mtProject.values.lastUsedInstrument);
    }
}

static void functMainScreenLeft()
{
    if (!SE->editorInstrument->isActive)
        return;

    if (SE->selectedPlace[cSampleEditor::mainScreen] > 0)
        SE->selectedPlace[cSampleEditor::mainScreen]--;

    SE->refreshSpectrumPoints();

    SE->showFrame();
}

static void functMainScreenRight()
{
    if (!SE->editorInstrument->isActive)
        return;

    if (SE->selectedPlace[cSampleEditor::mainScreen] < (SE->frameData.placesCount - 1))
        SE->selectedPlace[cSampleEditor::mainScreen]++;

    SE->refreshSpectrumPoints();

    SE->showFrame();
}

static void functMainScreenUp()
{
    switch (SE->selectedPlace[cSampleEditor::mainScreen])
    {
        case cSampleEditor::startPointButton: SE->modStartPoint(1); break;
        case cSampleEditor::endPointButton: SE->modEndPoint(1); break;
        case cSampleEditor::zoomButton: SE->modZoom(1); break;
        case cSampleEditor::effectButton: SE->modSelectedEffect(-1); break;
        default: break;
    }
}

static void functMainScreenDown()
{
    switch (SE->selectedPlace[cSampleEditor::mainScreen])
    {
        case cSampleEditor::startPointButton: SE->modStartPoint(-1); break;
        case cSampleEditor::endPointButton: SE->modEndPoint(-1); break;
        case cSampleEditor::zoomButton: SE->modZoom(-1); break;
        case cSampleEditor::effectButton: SE->modSelectedEffect(1); break;
        default: break;
    }
}

static void functMainScreenEncoder(int16_t value)
{
    switch (SE->selectedPlace[cSampleEditor::mainScreen])
    {
        case cSampleEditor::startPointButton: SE->modStartPoint(value); break;
        case cSampleEditor::endPointButton: SE->modEndPoint(value); break;
        case cSampleEditor::zoomButton: SE->modZoom(value); break;
        case cSampleEditor::effectButton:
        {
            SE->modSelectedEffect(interfaceGlobals.invertValForList ? -value : value);
            break;
        }
        default: break;
    }
}

static void functMainScreenDelete()
{
    if (!SE->editorInstrument->isActive)
        return;

    switch (SE->selectedPlace[cSampleEditor::mainScreen])
    {
        case cSampleEditor::startPointButton: SE->setDefaultStartPoint(); break;
        case cSampleEditor::endPointButton: SE->setDefaultEndPoint(); break;
        case cSampleEditor::zoomButton: SE->setDefaultZoom(); break;
        default: break;
    }
}

static void functSelectStartPoint()
{
    SE->selectedPlace[cSampleEditor::mainScreen] = cSampleEditor::startPointButton;

    SE->refreshSpectrumPoints();

    SE->showFrame();
}

static void functSelectEndPoint()
{
    SE->selectedPlace[cSampleEditor::mainScreen] = cSampleEditor::endPointButton;

    SE->refreshSpectrumPoints();

    SE->showSpectrumPoints();

    SE->showFrame();
}

static void functSelectZoom()
{
    SE->selectedPlace[cSampleEditor::mainScreen] = cSampleEditor::zoomButton;

    SE->refreshSpectrumPoints();

    SE->showFrame();
}

static void functMainScreenMore()
{
    SE->mainScreenButtonPage = 1 - SE->mainScreenButtonPage;
    SE->showMainScreen();
    SE->setMainScreenFunctions();
}

static void functLoadWav()
{
    if (mtProject.values.lastUsedInstrument >= kSampleInstrumentsCount)
    {
        return;
    }

    SE->avoidStop = true;

    GlobalFileBrowserConfig cfg;
    cfg.mode = GlobalFileBrowserConfig::Mode::kLoadWav;
    cfg.title = "Load WAV - Sample Editor";
    cfg.contextKey = "se_load_wav";
    cfg.initialDirectory = "/Samples";
    cfg.extensionFilter = ".wav";
    cfg.returnModule = SE;
    cfg.returnModuleOptions = 1;
    cfg.onConfirm = [](const std::string& fullPath)
    {
        auto slashPos = fullPath.find_last_of('/');
        std::string dirStr = (slashPos != std::string::npos && slashPos > 0)
                               ? fullPath.substr(0, slashPos)
                               : "";
        std::string fileStr = (slashPos != std::string::npos) ? fullPath.substr(slashPos + 1)
                                                              : fullPath;
        auto dotPos = fileStr.find_last_of('.');
        if (dotPos != std::string::npos)
        {
            fileStr = fileStr.substr(0, dotPos);
        }

        char dirBuf[kMaxFilePathLen + 1]{};
        char nameBuf[kMaxFileNameLen + 1]{};
        strncpy(dirBuf, dirStr.c_str(), kMaxFilePathLen);
        strncpy(nameBuf, fileStr.c_str(), kMaxFileNameLen);

        newFileManager.flushWorkspaceSave();
        if (newFileManager.importSampleToProject(
              dirBuf, nameBuf, mtProject.values.lastUsedInstrument))
        {
            SE->ip->blockAllInputs();
        }
        else
        {
            mtPopups.show(0, "Error loading WAV");
        }
    };

    openGlobalFileBrowser(cfg);
}

static void functExportWav()
{
    if (!SE->editorInstrument->isActive || SE->editorInstrument->sample.length == 0)
    {
        return;
    }

    SE->avoidStop = true;

    std::string defName = SE->editorInstrument->sample.file_name[0] != '\0'
                            ? SE->editorInstrument->sample.file_name
                            : "sample";

    GlobalFileBrowserConfig cfg;
    cfg.mode = GlobalFileBrowserConfig::Mode::kSaveWav;
    cfg.title = "Export WAV - Sample Editor";
    cfg.contextKey = "se_export_wav";
    cfg.initialDirectory = "/Samples";
    cfg.defaultFileName = defName;
    cfg.extensionFilter = ".wav";
    cfg.returnModule = SE;
    cfg.returnModuleOptions = 1;
    cfg.onConfirm = [](const std::string& fullPath)
    {
        auto slashPos = fullPath.find_last_of('/');
        std::string dirStr = (slashPos != std::string::npos && slashPos > 0)
                               ? fullPath.substr(0, slashPos)
                               : "/";
        std::string fileStr = (slashPos != std::string::npos) ? fullPath.substr(slashPos + 1)
                                                              : fullPath;
        auto dotPos = fileStr.find_last_of('.');
        if (dotPos != std::string::npos)
        {
            fileStr = fileStr.substr(0, dotPos);
        }

        char dirBuf[kMaxFilePathLen + 1]{};
        char nameBuf[kMaxFileNameLen + 1]{};
        strncpy(dirBuf, dirStr.c_str(), kMaxFilePathLen);
        strncpy(nameBuf, fileStr.c_str(), kMaxFileNameLen);

        bool isStereo = SE->isCurrentSampleStereo();
        const int16_t* leftPtr = nullptr;
        const int16_t* rightPtr = nullptr;
        uint32_t sampleLen = 0;

        if (SE->currentEffect != nullptr && SE->currentEffect->isDataLoaded())
        {
            leftPtr = SE->currentEffect->getAddresToPolyphonicPlay(mtEffect::Channel::kLeft);
            rightPtr = isStereo
                         ? SE->currentEffect->getAddresToPolyphonicPlay(mtEffect::Channel::kRight)
                         : leftPtr;
            sampleLen = SE->currentEffect->getLengthToPolyphonicPlay();
        }
        else
        {
            const uint8_t instrIdx = mtProject.values.lastUsedInstrument;
            if (instrIdx < kSampleInstrumentsCount && mtProject.sampleMemory[instrIdx].isStreamed &&
                SE->editorInstrument->sample.length > 0 &&
                SE->editorInstrument->sample.length <= EDIT_BUFF_SIZE / 2)
            {
                sampleLen = SE->editorInstrument->sample.length;
                int16_t* dstLeft = externalRamEffectsBuff;
                int16_t* dstRight = isStereo ? (externalRamEffectsBuff + EDIT_BUFF_SIZE) : dstLeft;
                bool ok = voiceStreamManager.readInstrumentFramesBlocking(
                  instrIdx, mtEffect::Channel::kLeft, 0, sampleLen, dstLeft);
                if (ok && isStereo)
                {
                    ok = voiceStreamManager.readInstrumentFramesBlocking(
                      instrIdx, mtEffect::Channel::kRight, 0, sampleLen, dstRight);
                }
                if (ok)
                {
                    leftPtr = dstLeft;
                    rightPtr = dstRight;
                }
            }
            else
            {
                leftPtr = SE->sampleEditorSampleMemory.address[mtEffect::Channel::kLeft];
                rightPtr = isStereo ? SE->sampleEditorSampleMemory.address[mtEffect::Channel::kRight]
                                    : leftPtr;
                sampleLen = SE->editorInstrument->sample.length;
            }
        }

        newFileManager.flushWorkspaceSave();
        if (leftPtr != nullptr && sampleLen > 0 &&
            newFileManager.saveRecordedSound(dirBuf,
                                             nameBuf,
                                             std::nullopt,
                                             const_cast<int16_t*>(leftPtr),
                                             const_cast<int16_t*>(rightPtr),
                                             sampleLen))
        {
            SE->ip->blockAllInputs();
        }
        else
        {
            mtPopups.show(0, "Error exporting WAV");
        }
    };

    openGlobalFileBrowser(cfg);
}

static void functListUp()
{
    if (SE->selectedPlace[cSampleEditor::mainScreen] != cSampleEditor::effectButton)
    {
        SE->selectedPlace[cSampleEditor::mainScreen] = cSampleEditor::effectButton;

        SE->refreshSpectrumPoints();

        SE->showFrame();
    }
    else
    {
        SE->modSelectedEffect(-1);
    }
}

static void functListDown()
{
    if (SE->selectedPlace[cSampleEditor::mainScreen] != cSampleEditor::effectButton)
    {
        SE->selectedPlace[cSampleEditor::mainScreen] = cSampleEditor::effectButton;

        SE->refreshSpectrumPoints();

        SE->showFrame();
    }
    else
    {
        SE->modSelectedEffect(1);
    }
}

static void functUndo()
{
    if (!SE->currentEffect->undoParams.isEnable)
    {
        return;
    }

    SE->currentEffect->undo();
    SE->refreshUndoState();

    SE->reloadSpectrumData();

    SE->selection.startPoint = SE->currentEffect->getNewStartPoint();
    SE->selection.endPoint = SE->currentEffect->getNewEndPoint();

    SE->refreshStartPoint();
    SE->refreshEndPoint();
    SE->needRefreshSpectrum = 1;
}

static void functApply()
{
    if (PtfxManager::isLoadEffectListIndex(SE->selectedEffectListIdx))
    {
        SE->openLoadEffectBrowser();
        return;
    }

    if (!SE->canApplyCurrentEffect())
    {
        return;
    }

    if (SE->isParametersScreen())
    {
        functSave();
        return;
    }

    SE->screenType = cSampleEditor::effectScreen;

    SE->switchScreen(SE->screenType);
}

// Parameter screen functions
static void functParamsScreenLeft()
{
    if (SE->isModularPresetPage())
    {
        return;
    }
    if (SE->selectedPlace[cSampleEditor::effectScreen] > 0)
        SE->selectedPlace[cSampleEditor::effectScreen]--;
    SE->showFrame();
}

static void functParamsScreenRight()
{
    if (SE->isModularPresetPage() || SE->frameData.placesCount == 0)
    {
        return;
    }
    if (SE->selectedPlace[cSampleEditor::effectScreen] < (SE->frameData.placesCount - 1))
        SE->selectedPlace[cSampleEditor::effectScreen]++;
    SE->showFrame();
}

static void functParamsScreenUp()
{
    if (SE->isModularPresetPage())
    {
        return;
    }
    int idx = SE->getSelectedParameterIdx();
    if (SE->isParameterList(idx))
        SE->modParameter(-1, idx);
    else
        SE->modParameter(1, idx);
}

static void functParamsScreenDown()
{
    if (SE->isModularPresetPage())
    {
        return;
    }
    int idx = SE->getSelectedParameterIdx();
    if (SE->isParameterList(idx))
        SE->modParameter(1, idx);
    else
        SE->modParameter(-1, idx);
}

static void functParamsScreenEncoder(int16_t value)
{
    if (SE->isModularPresetPage())
    {
        return;
    }
    int idx = SE->getSelectedParameterIdx();
    if (SE->isParameterList(idx))
        SE->modParameter(interfaceGlobals.invertValForList ? -value : value, idx);
    else
        SE->modParameter(value, idx);
}

static void functParamsScreenDelete()
{
    if (SE->isModularPresetPage())
    {
        return;
    }
    SE->setDefaultParameter(SE->getSelectedParameterIdx());
}

static std::string buildModularPresetDir()
{
    const PtfxEntry* entry = PtfxManager::getActivePlugin();
    const char* rawName =
      (entry != nullptr && entry->displayName[0] != '\0')
        ? entry->displayName
        : ((entry != nullptr && entry->filename[0] != '\0') ? entry->filename : "Plugin");

    std::string sanitized;
    sanitized.reserve(32);
    for (const char* p = rawName; *p != '\0'; ++p)
    {
        char c = *p;
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
            c == '>' || c == '|')
        {
            c = '_';
        }
        sanitized.push_back(c);
    }
    while (!sanitized.empty() && (sanitized.back() == ' ' || sanitized.back() == '.'))
    {
        sanitized.pop_back();
    }
    if (sanitized.empty())
    {
        sanitized = "Plugin";
    }
    return "/Effects/Presets/" + sanitized;
}

static bool reloadModularAfterBrowser(const float savedParams[12], uint8_t savedPage)
{
    if (SE->currentEffectIdx != editorEffectModular)
    {
        return false;
    }
    // Reload plugin code in case SD audio preview or browser scratch used externalRamScratchBuff
    if (!PtfxManager::reloadActivePlugin(sampleEditorModular))
    {
        sampleEditorModular.unloadPlugin();
        mtPopups.show(0, "Plugin reload failed");
        return false;
    }
    SE->loadModularEffectUI(PtfxManager::getActiveHeader());
    SE->effectScreenPage = savedPage;
    for (uint8_t i = 0; i < SE->getCurrentEffectDisplayParams().paramsNumber && i < 12; i++)
    {
        SE->effectDisplayParamsToAdjust[editorEffectModular].parameter[i] = savedParams[i];
        SE->currentEffect->setParameter(
          SE->effectDisplayParamsToAdjust[editorEffectModular].parameter[i], i);
        SE->reloadParameterValueText(i);
        SE->reloadParameterBarValue(i);
    }
    return true;
}

struct PtfxPresetFileHeader
{
    char magic[4];
    uint16_t version;
    char pluginFile[42];
    uint8_t numParams;
    uint8_t fileMask;
    float params[12];
};

static void openModularParamFileBrowser(int paramIdx)
{
    const auto& params = SE->getCurrentEffectDisplayParams();
    char dispType =
      (params.displayType != nullptr && paramIdx >= 0 && paramIdx < params.paramsNumber)
        ? params.displayType[paramIdx]
        : '\0';
    if (dispType != 'w' && dispType != 's')
    {
        return;
    }

    bool isSave = (dispType == 's');
    const char* extStr = (params.afterValueText[paramIdx] != nullptr &&
                          params.afterValueText[paramIdx][0] != '\0')
                           ? params.afterValueText[paramIdx]
                           : ".wav";
    bool isWav = FileBrowser::checkFilenameExtension(extStr, ".wav");

    std::string defName = "output";
    if (isSave)
    {
        const char* curPath = PtfxLoader::getSelectedFilePath(static_cast<uint8_t>(paramIdx));
        if (curPath != nullptr && curPath[0] != '\0')
        {
            const char* slash = strrchr(curPath, '/');
            const char* base = (slash != nullptr) ? (slash + 1) : curPath;
            const char* dot = strrchr(base, '.');
            std::string stem = (dot != nullptr && dot > base) ? std::string(base, dot - base)
                                                              : std::string(base);
            if (!stem.empty())
            {
                defName = stem;
            }
        }
    }

    float savedParams[12]{};
    for (uint8_t i = 0; i < 12; i++)
    {
        savedParams[i] = SE->effectDisplayParamsToAdjust[editorEffectModular].parameter[i];
    }
    uint8_t savedPage = SE->effectScreenPage;

    const PtfxEntry* activePlugin = PtfxManager::getActivePlugin();
    const char* pluginKey = (activePlugin != nullptr && activePlugin->filename[0] != '\0')
                              ? activePlugin->filename
                              : "ptfx";
    char keyBuf[48];
    snprintf(keyBuf, sizeof(keyBuf), "pf_%d_%c_%s", paramIdx, dispType, pluginKey);

    SE->avoidStop = true;

    GlobalFileBrowserConfig cfg;
    if (isSave)
    {
        cfg.mode = isWav ? GlobalFileBrowserConfig::Mode::kSaveWav
                         : GlobalFileBrowserConfig::Mode::kSaveGenericFile;
    }
    else
    {
        cfg.mode = isWav ? GlobalFileBrowserConfig::Mode::kLoadWav
                         : GlobalFileBrowserConfig::Mode::kLoadGenericFile;
    }
    cfg.title = std::string(isSave ? "Save " : "Load ") +
                (params.labelsText[paramIdx] ? params.labelsText[paramIdx] : "File");
    cfg.initialDirectory = isWav ? "/Samples" : "/";
    cfg.defaultFileName = defName;
    cfg.extensionFilter = extStr;
    cfg.contextKey = keyBuf;
    cfg.returnModule = SE;
    cfg.returnModuleOptions = 1;
    cfg.onConfirm = [paramIdx, isWav, isSave, savedParams, savedPage](const std::string& fullPath)
    {
        PtfxLoader::setSelectedFilePath(static_cast<uint8_t>(paramIdx), fullPath.c_str());
        bool wavOk = true;
        if (!isSave && isWav)
        {
            wavOk = PtfxLoader::loadSecondaryWavFile(fullPath.c_str());
        }

        if (reloadModularAfterBrowser(savedParams, savedPage))
        {
            float val = (isSave || !isWav || wavOk) ? 1.0f : 0.0f;
            SE->effectDisplayParamsToAdjust[editorEffectModular].parameter[paramIdx] = val;
            SE->currentEffect->setParameter(val, static_cast<uint8_t>(paramIdx));
            SE->reloadParameterValueText(static_cast<uint8_t>(paramIdx));
            SE->reloadParameterBarValue(static_cast<uint8_t>(paramIdx));
        }

        if (isSave)
        {
            mtPopups.show(0, "Output file set");
        }
        else if (isWav)
        {
            mtPopups.show(0, wavOk ? "Sample loaded" : "Invalid WAV file");
        }
        else
        {
            mtPopups.show(0, "File selected");
        }

        SE->setPreviewFunction();
        SE->currentEffect->clearProcessedData();
        SE->reloadFrameData(cSampleEditor::effectScreen);
        SE->showEffectParamsScreen();
        SE->setParamsScreenFunctions();
    };
    cfg.onCancel = [savedParams, savedPage]()
    {
        if (reloadModularAfterBrowser(savedParams, savedPage))
        {
            SE->reloadFrameData(cSampleEditor::effectScreen);
            SE->showEffectParamsScreen();
            SE->setParamsScreenFunctions();
        }
    };

    sampleEditorModular.unloadPlugin();
    openGlobalFileBrowser(cfg);
}

static void functParamsScreenInsert()
{
    if (SE->isModularPresetPage())
    {
        return;
    }
    openModularParamFileBrowser(SE->getSelectedParameterIdx());
}

static void functParamsScreenMore()
{
    if (!SE->areThereMoreThanOneParamsPage())
    {
        return;
    }
    SE->effectScreenPage = wrap(SE->effectScreenPage + 1, 0, SE->getParamsPagesCount() - 1);
    SE->selectedPlace[SE->enScreenType::effectScreen] = 0;

    SE->reloadFrameData(SE->enScreenType::effectScreen);
    SE->showEffectParamsScreen();
    SE->setParamsScreenFunctions();
}

static void functSelectParameter(uint8_t button)
{
    if (SE->isModularPresetPage())
    {
        switch (button)
        {
            case 0: SE->openModularPresetLoadBrowser(); break;
            case 1: SE->openModularPresetSaveBrowser(); break;
            case 2: SE->resetModularEffectParams(); break;
            default: break;
        }
        return;
    }

    if (button >= SE->getCurrentPageParams())
    {
        return;
    }

    int targetParamIdx = SE->effectScreenPage * cSampleEditor::kEffectPageParams + button;
    const auto& params = SE->getCurrentEffectDisplayParams();
    char dt =
      (params.displayType != nullptr && targetParamIdx >= 0 && targetParamIdx < params.paramsNumber)
        ? params.displayType[targetParamIdx]
        : '\0';
    if (dt == 'w' || dt == 's')
    {
        SE->selectedPlace[cSampleEditor::effectScreen] = button;
        SE->showFrame();
        openModularParamFileBrowser(targetParamIdx);
        return;
    }

    if (SE->selectedPlace[cSampleEditor::effectScreen] == button)
    {
        int paramIdx = SE->getSelectedParameterIdx();
        if (SE->isParameterList(paramIdx))
        {
            uint8_t count = SE->getParameterListCount(paramIdx);
            if (count > 1)
            {
                int currentIdx = (int)(SE->effectDisplayParamsToAdjust[SE->currentEffectIdx].parameter[paramIdx] + 0.5f);
                if (currentIdx + 1 < (int)count)
                    SE->modParameter(1, paramIdx);
                else
                    SE->modParameter(-currentIdx, paramIdx);
            }
            return;
        }
    }

    SE->selectedPlace[cSampleEditor::effectScreen] = button;
    SE->showFrame();
}

void cSampleEditor::openLoadEffectBrowser()
{
    avoidStop = true;

    GlobalFileBrowserConfig cfg;
    cfg.mode = GlobalFileBrowserConfig::Mode::kLoadGenericFile;
    cfg.title = "Load Effect (.ptfx)";
    cfg.initialDirectory = "/Effects";
    cfg.extensionFilter = ".ptfx";
    cfg.confirmButtonLabel = "Load";
    cfg.contextKey = "se_load_ptfx";
    cfg.returnModule = this;
    cfg.returnModuleOptions = 1;
    cfg.onFormatItemLabel = [](const std::string& fullPath,
                               const char* rawName,
                               bool isFolder,
                               bool isPackage) -> std::string
    {
        (void)rawName;
        if (isFolder || isPackage)
        {
            return {};
        }
        PtfxHeader hdr{};
        if (PtfxLoader::readHeader(fullPath.c_str(), hdr))
        {
            hdr.display_name[sizeof(hdr.display_name) - 1] = '\0';
            if (hdr.display_name[0] != '\0')
            {
                return std::string(hdr.display_name);
            }
        }
        return {};
    };
    cfg.onDescribeItem = [](const std::string& fullPath, bool isFolder, bool isPackage) -> std::string
    {
        if (isFolder || isPackage)
        {
            return {};
        }

        PtfxHeader hdr{};
        if (!PtfxLoader::readHeader(fullPath.c_str(), hdr))
        {
            return "Invalid .ptfx plugin";
        }

        hdr.display_name[sizeof(hdr.display_name) - 1] = '\0';
        hdr.author[sizeof(hdr.author) - 1] = '\0';

        const char* slash = strrchr(fullPath.c_str(), '/');
        const char* fileName = (slash != nullptr) ? (slash + 1) : fullPath.c_str();
        char line1[64];
        if (hdr.author[0] != '\0')
        {
            snprintf(line1, sizeof(line1), "%s (%s)", fileName, hdr.author);
        }
        else
        {
            snprintf(line1, sizeof(line1), "File: %s", fileName);
        }

        const char* typeStr = "Type: Audio Effect";
        if (hdr.flags & PTFX_FLAG_WAVETABLE)
        {
            typeStr = "Type: Wavetable Gen";
        }
        else if (hdr.flags & PTFX_FLAG_GENERATOR)
        {
            typeStr = "Type: Sample Generator";
        }
        else if (hdr.flags & PTFX_FLAG_CONVERT_TO_MONO)
        {
            typeStr = "Type: Stereo to Mono";
        }
        else if (hdr.flags & PTFX_FLAG_STEREO)
        {
            typeStr = "Type: Stereo Effect";
        }

        uint32_t ramKb = (hdr.scratch_size + 1023u) / 1024u;
        char line3[64];
        snprintf(line3,
                 sizeof(line3),
                 "Params: %u | RAM: %lu KB%s",
                 static_cast<unsigned>(hdr.num_params),
                 static_cast<unsigned long>(ramKb),
                 (hdr.flags & PTFX_FLAG_IMX_ONLY) ? " [IMX]" : "");

        return std::string(line1) + "\n" + typeStr + "\n" + line3;
    };
    cfg.onConfirm = [](const std::string& fullPath)
    {
        PtfxLoader::clearSelectedFiles();
        if (!PtfxManager::loadPluginFromPath(fullPath.c_str(), sampleEditorModular))
        {
            if (PtfxManager::hasLoadedPlugin())
            {
                PtfxManager::reloadActivePlugin(sampleEditorModular);
            }
            mtPopups.show(0, "Cannot load effect");
            SE->effectListData.length = PtfxManager::getTotalEffectListCount();
            SE->effectListData.data = PtfxManager::getEffectListNames();
            SE->screenType = cSampleEditor::mainScreen;
            SE->switchScreen(SE->screenType);
            return;
        }

        SE->effectListData.length = PtfxManager::getTotalEffectListCount();
        SE->effectListData.data = PtfxManager::getEffectListNames();
        SE->selectedEffectListIdx = kBuiltInEffectsCount;
        SE->currentEffectIdx = editorEffectModular;
        SE->currentEffect = sampleEditorEffect[editorEffectModular];

        SE->loadModularEffectUI(PtfxManager::getActiveHeader());
        SE->reloadCurrentEffect();
        SE->currentEffect->clearProcessedData();
        SE->currentEffect->setBaseSampleLength(SE->editorInstrument->sample.length);
        SE->currentEffect->changeSelectionRange(SE->selection.startPoint, SE->selection.endPoint);

        for (uint8_t idx = 0; idx < SE->getCurrentEffectDisplayParams().paramsNumber; idx++)
        {
            SE->currentEffect->setParameter(
              SE->effectDisplayParamsToAdjust[editorEffectModular].parameter[idx], idx);
            SE->reloadParameterValueText(idx);
            SE->reloadParameterBarValue(idx);
        }

        SE->previewState = cSampleEditor::previewStatePreview;
        if (SE->canApplyCurrentEffect() && !SE->isParametersScreen())
        {
            SE->screenType = cSampleEditor::effectScreen;
            SE->switchScreen(SE->screenType);
        }
        else
        {
            SE->screenType = cSampleEditor::mainScreen;
            SE->switchScreen(SE->screenType);
        }
    };
    cfg.onCancel = []()
    {
        SE->effectListData.length = PtfxManager::getTotalEffectListCount();
        SE->effectListData.data = PtfxManager::getEffectListNames();
        SE->screenType = cSampleEditor::mainScreen;
        SE->switchScreen(SE->screenType);
    };

    openGlobalFileBrowser(cfg);
}

void cSampleEditor::openModularPresetLoadBrowser()
{
    if (currentEffectIdx != editorEffectModular)
    {
        return;
    }

    float savedParams[12]{};
    for (uint8_t i = 0; i < 12; i++)
    {
        savedParams[i] = effectDisplayParamsToAdjust[editorEffectModular].parameter[i];
    }
    uint8_t savedPage = effectScreenPage;

    const PtfxEntry* activePlugin = PtfxManager::getActivePlugin();
    const char* pluginKey = (activePlugin != nullptr && activePlugin->filename[0] != '\0')
                              ? activePlugin->filename
                              : "ptfx";
    char keyBuf[48];
    snprintf(keyBuf, sizeof(keyBuf), "pp_%s", pluginKey);

    avoidStop = true;

    GlobalFileBrowserConfig cfg;
    cfg.mode = GlobalFileBrowserConfig::Mode::kLoadGenericFile;
    cfg.title = "Load Plugin Preset";
    cfg.initialDirectory = buildModularPresetDir();
    cfg.extensionFilter = ".ptp";
    cfg.confirmButtonLabel = "Load";
    cfg.contextKey = keyBuf;
    cfg.returnModule = this;
    cfg.returnModuleOptions = 1;
    cfg.onConfirm = [savedParams, savedPage](const std::string& fullPath)
    {
        FileSystem* fs = newFileManager.getFileSystem();
        const char* relPath =
          (!fullPath.empty() && fullPath[0] == '/') ? (fullPath.c_str() + 1) : fullPath.c_str();

        PtfxPresetFileHeader hdr{};
        bool readOk = false;
        FileSystem::FileHandle fh{};
        if (fs != nullptr)
        {
            auto [isOpen, openedFh] = fs->openFile(relPath, FileSystem::FileOpenMode::kRead);
            if (isOpen)
            {
                fh = openedFh;
                if (fs->readFile(&hdr, sizeof(hdr), fh) == static_cast<int32_t>(sizeof(hdr)) &&
                    memcmp(hdr.magic, "PTFP", 4) == 0 && hdr.version == 1)
                {
                    readOk = true;
                }
                else
                {
                    fs->closeFile(fh);
                }
            }
        }

        const PtfxEntry* entry = PtfxManager::getActivePlugin();
        bool pluginMatch =
          readOk && (entry != nullptr) && (strcasecmp(hdr.pluginFile, entry->filename) == 0);

        if (!readOk || !pluginMatch)
        {
            if (readOk && fs != nullptr)
            {
                fs->closeFile(fh);
            }
            reloadModularAfterBrowser(savedParams, savedPage);
            mtPopups.show(0, readOk ? "Wrong plugin preset" : "Invalid preset file");
            SE->reloadFrameData(cSampleEditor::effectScreen);
            SE->showEffectParamsScreen();
            SE->setParamsScreenFunctions();
            return;
        }

        PtfxLoader::clearSelectedFiles();
        for (uint8_t i = 0; i < 12; i++)
        {
            if ((hdr.fileMask & (1u << i)) != 0)
            {
                uint16_t pathLen = 0;
                if (fs->readFile(&pathLen, sizeof(pathLen), fh) !=
                      static_cast<int32_t>(sizeof(pathLen)) ||
                    pathLen > kMaxFilePathLen)
                {
                    break;
                }
                char pathBuf[kMaxFilePathLen + 1]{};
                if (pathLen > 0 &&
                    fs->readFile(pathBuf, pathLen, fh) != static_cast<int32_t>(pathLen))
                {
                    break;
                }
                pathBuf[pathLen] = '\0';
                PtfxLoader::setSelectedFilePath(i, pathBuf);
            }
        }
        fs->closeFile(fh);

        if (reloadModularAfterBrowser(savedParams, savedPage))
        {
            const auto& curParams = SE->getCurrentEffectDisplayParams();
            uint8_t applyCount = std::min<uint8_t>(hdr.numParams, curParams.paramsNumber);
            applyCount = std::min<uint8_t>(applyCount, 12);
            for (uint8_t i = 0; i < applyCount; i++)
            {
                float val = hdr.params[i];
                char dt = curParams.displayType ? curParams.displayType[i] : '\0';
                if (dt == 'w')
                {
                    const char* fp = PtfxLoader::getSelectedFilePath(i);
                    const char* extStr =
                      (curParams.afterValueText[i] != nullptr &&
                       curParams.afterValueText[i][0] != '\0')
                        ? curParams.afterValueText[i]
                        : ".wav";
                    bool isWav = FileBrowser::checkFilenameExtension(extStr, ".wav");
                    if (fp != nullptr && fp[0] != '\0')
                    {
                        val = isWav ? (PtfxLoader::loadSecondaryWavFile(fp) ? 1.0f : 0.0f) : 1.0f;
                    }
                    else
                    {
                        val = 0.0f;
                    }
                }
                else if (dt == 's')
                {
                    const char* fp = PtfxLoader::getSelectedFilePath(i);
                    val = (fp != nullptr && fp[0] != '\0') ? 1.0f : 0.0f;
                }
                SE->effectDisplayParamsToAdjust[editorEffectModular].parameter[i] = val;
                SE->currentEffect->setParameter(val, i);
                SE->reloadParameterValueText(i);
                SE->reloadParameterBarValue(i);
            }
        }

        SE->setPreviewFunction();
        SE->currentEffect->clearProcessedData();
        mtPopups.show(0, "Preset loaded");
        SE->reloadFrameData(cSampleEditor::effectScreen);
        SE->showEffectParamsScreen();
        SE->setParamsScreenFunctions();
    };
    cfg.onCancel = [savedParams, savedPage]()
    {
        if (reloadModularAfterBrowser(savedParams, savedPage))
        {
            SE->reloadFrameData(cSampleEditor::effectScreen);
            SE->showEffectParamsScreen();
            SE->setParamsScreenFunctions();
        }
    };

    sampleEditorModular.unloadPlugin();
    openGlobalFileBrowser(cfg);
}

void cSampleEditor::openModularPresetSaveBrowser()
{
    if (currentEffectIdx != editorEffectModular)
    {
        return;
    }

    float savedParams[12]{};
    for (uint8_t i = 0; i < 12; i++)
    {
        savedParams[i] = effectDisplayParamsToAdjust[editorEffectModular].parameter[i];
    }
    uint8_t savedPage = effectScreenPage;

    const PtfxEntry* activePlugin = PtfxManager::getActivePlugin();
    const char* pluginKey = (activePlugin != nullptr && activePlugin->filename[0] != '\0')
                              ? activePlugin->filename
                              : "ptfx";
    char keyBuf[48];
    snprintf(keyBuf, sizeof(keyBuf), "pp_%s", pluginKey);

    avoidStop = true;

    GlobalFileBrowserConfig cfg;
    cfg.mode = GlobalFileBrowserConfig::Mode::kSaveGenericFile;
    cfg.title = "Save Plugin Preset";
    cfg.initialDirectory = buildModularPresetDir();
    cfg.defaultFileName = "preset";
    cfg.extensionFilter = ".ptp";
    cfg.contextKey = keyBuf;
    cfg.returnModule = this;
    cfg.returnModuleOptions = 1;
    cfg.onConfirm = [savedParams, savedPage](const std::string& fullPath)
    {
        bool reloaded = reloadModularAfterBrowser(savedParams, savedPage);

        FileSystem* fs = newFileManager.getFileSystem();
        const char* relPath =
          (!fullPath.empty() && fullPath[0] == '/') ? (fullPath.c_str() + 1) : fullPath.c_str();
        const PtfxEntry* entry = PtfxManager::getActivePlugin();
        const auto& curParams = SE->getCurrentEffectDisplayParams();

        PtfxPresetFileHeader hdr{};
        memcpy(hdr.magic, "PTFP", 4);
        hdr.version = 1;
        if (entry != nullptr)
        {
            polystrncpy(hdr.pluginFile, entry->filename, sizeof(hdr.pluginFile));
        }
        hdr.numParams = std::min<uint8_t>(curParams.paramsNumber, 12);
        hdr.fileMask = 0;
        for (uint8_t i = 0; i < 12; i++)
        {
            hdr.params[i] = savedParams[i];
            if (i < hdr.numParams && curParams.displayType != nullptr &&
                (curParams.displayType[i] == 'w' || curParams.displayType[i] == 's'))
            {
                const char* fp = PtfxLoader::getSelectedFilePath(i);
                if (fp != nullptr && fp[0] != '\0')
                {
                    hdr.fileMask |= static_cast<uint8_t>(1u << i);
                }
            }
        }

        bool savedOk = false;
        if (fs != nullptr)
        {
            auto [isOpen, fh] = fs->openFile(relPath, FileSystem::FileOpenMode::kWriteCreateNew);
            if (isOpen)
            {
                savedOk = (fs->writeFile(&hdr, sizeof(hdr), fh) == static_cast<int32_t>(sizeof(hdr)));
                for (uint8_t i = 0; savedOk && i < 12; i++)
                {
                    if ((hdr.fileMask & (1u << i)) != 0)
                    {
                        const char* fp = PtfxLoader::getSelectedFilePath(i);
                        uint16_t pathLen = static_cast<uint16_t>(
                          std::min<size_t>(strlen(fp), kMaxFilePathLen));
                        savedOk =
                          (fs->writeFile(&pathLen, sizeof(pathLen), fh) ==
                           static_cast<int32_t>(sizeof(pathLen))) &&
                          (pathLen == 0 ||
                           fs->writeFile(fp, pathLen, fh) == static_cast<int32_t>(pathLen));
                    }
                }
                fs->closeFile(fh);
            }
        }

        mtPopups.show(0, savedOk ? "Preset saved" : "Error saving preset");
        if (reloaded)
        {
            SE->reloadFrameData(cSampleEditor::effectScreen);
            SE->showEffectParamsScreen();
            SE->setParamsScreenFunctions();
        }
    };
    cfg.onCancel = [savedParams, savedPage]()
    {
        if (reloadModularAfterBrowser(savedParams, savedPage))
        {
            SE->reloadFrameData(cSampleEditor::effectScreen);
            SE->showEffectParamsScreen();
            SE->setParamsScreenFunctions();
        }
    };

    sampleEditorModular.unloadPlugin();
    openGlobalFileBrowser(cfg);
}

void cSampleEditor::resetModularEffectParams()
{
    if (currentEffectIdx != editorEffectModular)
    {
        return;
    }

    const PtfxHeader& header = PtfxManager::getActiveHeader();
    uint8_t count = std::min<uint8_t>(header.num_params, kMaxParametersInEffect);
    PtfxLoader::clearSelectedFiles();

    for (uint8_t i = 0; i < count; i++)
    {
        float defVal =
          (header.params[i].display_type == PARAM_FILE ||
           header.params[i].display_type == PARAM_SAVE_FILE)
            ? 0.0f
            : header.params[i].default_val;
        effectDisplayParamsToAdjust[editorEffectModular].parameter[i] = defVal;
        currentEffect->setParameter(defVal, i);
        reloadParameterValueText(i);
        reloadParameterBarValue(i);
    }

    setPreviewFunction();
    currentEffect->clearProcessedData();
    mtPopups.show(0, "Parameters reset");
    showEffectParamsScreen();
    setParamsScreenFunctions();
}

static void functPreview()
{
    if (!SE->canApplyCurrentEffect())
    {
        return;
    }

    uint32_t processedLength = SE->currentEffect->getExpectedProcessedSampleLength();
    if (processedLength == 0)
    {
        return;
    }

    if (processedLength > EDIT_BUFF_SIZE / 2)
    {
        SE->showPopupTooLongProcessedSampleWindow();
        SE->setTooLongProcessedSampleFunctions();
        return;
    }

    uint32_t oldRamFrames =
      mtProject.sampleMemory[mtProject.values.lastUsedInstrument].isStreamed
        ? mtProject.sampleMemory[mtProject.values.lastUsedInstrument].ramFrames
        : std::min<uint32_t>(
            mtProject.instrument[mtProject.values.lastUsedInstrument].sample.length,
            kStreamPreCacheFrames);
    uint32_t newRamFrames = std::min<uint32_t>(processedLength, kStreamPreCacheFrames);
    int32_t memoryBankChange = (int32_t)newRamFrames - (int32_t)oldRamFrames;

    if (mtProject.used_memory + memoryBankChange > mtProject.max_memory)
    {
        SE->showPopupTooLongProcessedSampleWindow();
        SE->setTooLongProcessedSampleFunctions();
        return;
    }

    SE->processingSteps = 1;

    uint8_t instr_idx = mtProject.values.lastUsedInstrument;
    bool hasInstrumentSample = (instr_idx < kSampleInstrumentsCount) &&
                               mtProject.instrument[instr_idx].isActive &&
                               (mtProject.sampleMemory[instr_idx].address[0] != nullptr) &&
                               (mtProject.instrument[instr_idx].sample.length > 0);

    SE->isLoadedData = SE->currentEffect->isDataLoaded() || !hasInstrumentSample;
    SE->isProcessedData = SE->currentEffect->isDataProcessed();

    if (!SE->isLoadedData)
        SE->processingSteps++;

    if (SE->processingSteps)
    {
        SE->showPopupProcessing();
        SE->processingInProgress = 1;
        SE->ip->clearAll();
    }

    SE->processingProgress = 0;
    SE->showProgressProcessing();
    SE->currentEffect->processSelection();

    if (SE->processingSteps &&
        !SE->currentEffect->getLoadingState() &&
        !SE->currentEffect->getProcessingSelectionState())
    {
        SE->processingInProgress = 0;
        SE->hideProgressPopup();
        SE->restoreFunctions();
    }
}

static void functPlay()
{
    mtPadBoard.cutAllInstrument();

    std::vector<int16_t*> addresses;

    for (int channel = 0; channel < mtEffect::kChannels; channel++)
    {
        addresses.push_back(
          SE->currentEffect->getSampleAddresToPreview((mtEffect::Channel)channel));
    }

    uint8_t vol = mtProject.instrument[mtProject.values.lastUsedInstrument].volume;
    if (vol == 0)
    {
        vol = DEFAULT_INSTRUMENT_VOLUME;
    }

    mtPadBoard.startNoteWithoutInstrument(
      60,
      addresses,
      SE->currentEffect->getSampleLengthToPreview(),
      vol,
      INTERFACE_BUTTON_PREVIEW); // tego nie moze robic midi
    SE->playingInProgress = 1;
    SE->playingProgress = 0;
    SE->showPopupPlaying();

    SE->setStopFunction();
}

static void functStop()
{
    mtPadBoard.stopInstrument(INTERFACE_BUTTON_PREVIEW);
    SE->playingInProgress = 0;
    SE->hideProgressPopup();

    SE->restoreFunctions();
    SE->setPlayFunction();
}

static void functBack()
{
    SE->screenType = cSampleEditor::mainScreen;
    SE->switchScreen(SE->screenType);
}

static void functSave()
{
    if (!SE->canApplyCurrentEffect())
    {
        return;
    }

    uint32_t processedLength = SE->currentEffect->getExpectedProcessedSampleLength();
    if (processedLength == 0)
    {
        return;
    }

    if (processedLength > EDIT_BUFF_SIZE / 2)
    {
        SE->showPopupTooLongProcessedSampleWindow();
        SE->setTooLongProcessedSampleFunctions();
        return;
    }

    uint32_t oldRamFrames =
      mtProject.sampleMemory[mtProject.values.lastUsedInstrument].isStreamed
        ? mtProject.sampleMemory[mtProject.values.lastUsedInstrument].ramFrames
        : std::min<uint32_t>(
            mtProject.instrument[mtProject.values.lastUsedInstrument].sample.length,
            kStreamPreCacheFrames);
    uint32_t newRamFrames = std::min<uint32_t>(processedLength, kStreamPreCacheFrames);
    int32_t memoryBankChange = (int32_t)newRamFrames - (int32_t)oldRamFrames;

    if (mtProject.used_memory + memoryBankChange > mtProject.max_memory)
    {
        SE->showPopupTooLongProcessedSampleWindow();
        SE->setTooLongProcessedSampleFunctions();
        return;
    }

    SE->applyingSteps = 0;

    uint8_t instr_idx = mtProject.values.lastUsedInstrument;
    bool hasInstrumentSample = (instr_idx < kSampleInstrumentsCount) &&
                               mtProject.instrument[instr_idx].isActive &&
                               (mtProject.sampleMemory[instr_idx].address[0] != nullptr) &&
                               (mtProject.instrument[instr_idx].sample.length > 0);

    SE->isLoadedData = SE->currentEffect->isDataLoaded() || !hasInstrumentSample;
    SE->isProcessedData = SE->currentEffect->isDataProcessed();

    if (!SE->isLoadedData)
        SE->applyingSteps++;
    if (!SE->isProcessedData)
        SE->applyingSteps++;

    if (SE->applyingSteps)
    {
        SE->showPopupApplying();
        SE->applyingInProgress = 1;
        SE->applyingProgress = 0;
        SE->showProgressApplying();
        SE->ip->clearAll();
    }

    SE->applyingProgress = 0;
    SE->currentEffect->apply();

    if (SE->applyingSteps &&
        !SE->currentEffect->getLoadingState() &&
        !SE->currentEffect->getProcessingSelectionState())
    {
        SE->applyingInProgress = 0;
        SE->applyingProgress = 0;
        SE->hideProgressPopup();
        SE->restoreFunctions();
        return;
    }

    SE->confirmedDataIsChanged = 1;

    if (!SE->applyingSteps)
    {
        if (SE->currentEffectIdx == editorEffectModular)
        {
            sampleEditorModular.onApplyCommitted(mtProject.values.lastUsedInstrument);
        }

        SE->editorInstrument->isActive = 1;
        SE->editorInstrument->volume = mtProject.instrument[mtProject.values.lastUsedInstrument].volume;
        SE->editorInstrument->sample.length = SE->currentEffect->getLengthToPolyphonicPlay();
        strncpy(SE->editorInstrument->sample.file_name,
                mtProject.instrument[mtProject.values.lastUsedInstrument].sample.file_name,
                SAMPLE_NAME_SIZE);

        SE->selection.startPoint = SE->currentEffect->getNewStartPoint();
        SE->selection.endPoint = SE->currentEffect->getNewEndPoint();
        SE->reloadSpectrumData();
        SE->refreshStartPoint();
        SE->refreshEndPoint();
        SE->needRefreshSpectrum = 1;

        SE->resetZoom();

        SE->screenType = cSampleEditor::mainScreen;
        SE->switchScreen(SE->screenType);
        SE->setCommonFunctions();
    }

    SE->previewState = cSampleEditor::previewStatePreview;
    SE->currentEffect->clearProcessedData();
    SE->refreshUndoState();
}

// Stop Pattern Funct
static void functStopPatternYes()
{
    sequencer.stop();
    SE->hideStaticPopup();
    SE->start(0);
}

static void functStopPatternNo()
{
    SE->avoidStop = true;
    SE->hideStaticPopup();
    SE->eventFunct(eventSwitchToPreviousModule, SE, 0, 0);
}

// Too long instrument
static void functTooLongSampleOk()
{
    SE->avoidStop = true;
    SE->hideStaticPopup();
    SE->eventFunct(eventSwitchToPreviousModule, SE, 0, 0);
}

// Too long processed instrument
static void functTooLongProcessedSampleOk()
{
    SE->hideStaticPopup();
    SE->switchScreen(SE->screenType);
    SE->setCommonFunctions();
}

static void functMenu(uint8_t state)
{
    if (state == buttonPress)
    {
        SE->hideSpectrum();
        mtMenuPopup.showModulePopup(SE, functSwitchModule);
    }
    else if (state == buttonRelease)
    {
        SE->showSpectrum();
        mtMenuPopup.hideModulePopup(SE);
    }
}

static constexpr uint8_t kMaxListItemsPerParam = 16;

__NOINIT(EXTERNAL_RAM) static char* s_modularListItems[kMaxParametersInEffect][kMaxListItemsPerParam];
__NOINIT(EXTERNAL_RAM) static uint8_t s_modularListCounts[kMaxParametersInEffect];

void cSampleEditor::loadModularEffectUI(const PtfxHeader& header)
{
    uint8_t count = header.num_params;
    if (count > kMaxParametersInEffect)
        count = kMaxParametersInEffect;

    s_modularEffectParams.paramsNumber = count;

    for (uint8_t i = 0; i < kMaxParametersInEffect; i++)
    {
        s_modularListCounts[i] = 0;
        for (uint8_t j = 0; j < kMaxListItemsPerParam; j++)
        {
            s_modularListItems[i][j] = nullptr;
        }
    }

    for (uint8_t i = 0; i < count; i++)
    {
        const PtfxParamDef& p = header.params[i];

        strncpy(s_modularLabelStorage[i], p.label, sizeof(s_modularLabelStorage[i]) - 1);
        s_modularLabelStorage[i][sizeof(s_modularLabelStorage[i]) - 1] = '\0';
        s_modularLabels[i] = s_modularLabelStorage[i];

        if (p.display_type == PARAM_LIST)
        {
            uint16_t listOffset =
              (uint16_t)((uint8_t)p.unit[0] | ((uint16_t)(uint8_t)p.unit[1] << 8));
            uint8_t itemCount = (uint8_t)p.unit[2];
            if (itemCount > kMaxListItemsPerParam)
                itemCount = kMaxListItemsPerParam;

            uint8_t* codeBuf = PtfxLoader::getCodeBuffer();
            uint32_t codeSize = header.code_size;
            uint32_t pos = listOffset;
            uint8_t validItems = 0;

            while (validItems < itemCount && pos < codeSize)
            {
                char* strPtr = reinterpret_cast<char*>(codeBuf + pos);
                uint32_t maxLen = codeSize - pos;
                uint32_t len = 0;
                while (len < maxLen && strPtr[len] != '\0')
                {
                    len++;
                }
                if (len >= maxLen)
                    break;
                s_modularListItems[i][validItems++] = strPtr;
                pos += len + 1;
            }

            s_modularListCounts[i] = validItems;
            s_modularUnits[i] = nullptr;
            s_modularUpConstrain[i] = (validItems > 0) ? (float)(validItems - 1) : p.max_val;
            s_modularDownConstrain[i] = 0.0f;
            s_modularChangeStep[i] = 1.0f;
            s_modularDisplayMult[i] = 1.0f;
            s_modularDisplayType[i] = (validItems > 0) ? 'l' : 'd';
        }
        else if (p.display_type == PARAM_FILE || p.display_type == PARAM_SAVE_FILE)
        {
            strncpy(s_modularUnitStorage[i], p.unit, sizeof(s_modularUnitStorage[i]) - 1);
            s_modularUnitStorage[i][sizeof(s_modularUnitStorage[i]) - 1] = '\0';
            s_modularUnits[i] =
              (s_modularUnitStorage[i][0] != '\0') ? s_modularUnitStorage[i] : ".wav";

            s_modularUpConstrain[i] = 1.0f;
            s_modularDownConstrain[i] = 0.0f;
            s_modularChangeStep[i] = 0.0f;
            s_modularDisplayMult[i] = 1.0f;
            s_modularDisplayType[i] = (p.display_type == PARAM_SAVE_FILE) ? 's' : 'w';
        }
        else
        {
            strncpy(s_modularUnitStorage[i], p.unit, sizeof(s_modularUnitStorage[i]) - 1);
            s_modularUnitStorage[i][sizeof(s_modularUnitStorage[i]) - 1] = '\0';
            s_modularUnits[i] =
              (s_modularUnitStorage[i][0] != '\0') ? s_modularUnitStorage[i] : nullptr;

            s_modularUpConstrain[i] = p.max_val;
            s_modularDownConstrain[i] = p.min_val;
            s_modularChangeStep[i] = p.step;
            s_modularDisplayMult[i] = 1.0f;
            s_modularDisplayType[i] = (p.display_type == PARAM_INT) ? 'd' : 'f';
        }

        effectDisplayParamsToAdjust[editorEffectModular].parameter[i] = p.default_val;
    }
    s_modularDisplayType[count] = '\0';

    s_modularEffectParams.labelsText = s_modularLabels;
    s_modularEffectParams.upConstrain = s_modularUpConstrain;
    s_modularEffectParams.downConstrain = s_modularDownConstrain;
    s_modularEffectParams.changeStep = s_modularChangeStep;
    s_modularEffectParams.displayMult = s_modularDisplayMult;
    s_modularEffectParams.afterValueText = s_modularUnits;
    s_modularEffectParams.displayType = s_modularDisplayType;
}

const strEffectDisplayConstantParams& cSampleEditor::getCurrentEffectDisplayParams() const
{
    if (currentEffectIdx == editorEffectModular)
    {
        return s_modularEffectParams;
    }
    return effectDisplayConstantParams[currentEffectIdx];
}

bool cSampleEditor::isParameterList(uint8_t paramIdx) const
{
    const auto& params = getCurrentEffectDisplayParams();
    if (paramIdx >= params.paramsNumber || params.displayType == nullptr)
        return false;
    return params.displayType[paramIdx] == 'l' && getParameterListCount(paramIdx) > 0;
}

bool cSampleEditor::isParameterFile(uint8_t paramIdx) const
{
    const auto& params = getCurrentEffectDisplayParams();
    if (paramIdx >= params.paramsNumber || params.displayType == nullptr)
        return false;
    return params.displayType[paramIdx] == 'w' || params.displayType[paramIdx] == 's';
}

const char* cSampleEditor::getParameterFileText(uint8_t paramIdx) const
{
    if (paramIdx < kMaxParametersInEffect)
    {
        return parameterFileText[paramIdx];
    }
    return "";
}

uint8_t cSampleEditor::getParameterListCount(uint8_t paramIdx) const
{
    if (currentEffectIdx == editorEffectModular && paramIdx < kMaxParametersInEffect)
    {
        return s_modularListCounts[paramIdx];
    }
    return 0;
}

char** cSampleEditor::getParameterListItems(uint8_t paramIdx) const
{
    if (currentEffectIdx == editorEffectModular && paramIdx < kMaxParametersInEffect)
    {
        return &s_modularListItems[paramIdx][0];
    }
    return nullptr;
}

