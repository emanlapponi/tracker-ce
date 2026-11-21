#include "Recorder.h"
#include "TrackerImpl.h"
#include "core/graphicProcessing.h"
#include "fileManager.h"
#include "mtAudioEngine.h"
#include "mtGainLevelLogarithmicTab.h"
#include "mtPadBoard.h"
#include "mtPadsBacklight.h"
#include "mtRandomNameGenerator.h"
#include "sampleRecorder/sampleRecorder.h"

#include "core/interfaceMenuPopup.h"
#include "core/interfacePopups.h"
#include "core/songTimer.h"

#include "Platform.h"
#include "mtHardware.h"
#include "util_math.h"

constexpr uint16_t POP_TIME = 200; // czas po jakim nie ma pykniecia przy zmianie z lineIn na mic

constexpr uint32_t PLAY_REFRESH_US = 5000;
#include <cr_section_macros.h>

__NOINIT(EXTERNAL_RAM) strTrackerSpectrum cSampleRecorder::spectrum[SAMPLE_CHANNELS];

cSampleRecorder sampleRecorder;
static cSampleRecorder* SR = &sampleRecorder;

extern int16_t* externalRamEffectsBuff2;
extern int16_t* externalRamEffectsBuff;

extern strMtProject mtProject;

static void functMiniMiddle(uint8_t state);

static void functPads(uint8_t pad, uint8_t state, int16_t velo);

static void functMenu(uint8_t state);

static void functLeft();
static void functRight();
static void functUp();
static void functDown();

// static void functEnter();
static void functDeleteBackspace(uint8_t state);

static void functSelectButton0();
static void functSelectButton1(uint8_t state);
static void functSelectButton2(uint8_t state);
static void functSelectButton3();
static void functSelectButton4();
static void functSelectButton5();
static void functSelectButton6();
static void functSelectButton7();

static void functActionButton0(uint8_t s);
static void functActionButton1(uint8_t state);
static void functActionButton2(uint8_t state);
static void functActionButton3();
static void functActionButton4();
static void functActionButton5();
static void functActionButton6();
static void functActionButton7();

static void functStopPatternCancel();
static void functStopPatternYes();

static void functActionSource();
static void functActionGain();
static void functActionMonitor();
static void functActionRecord();
static void functActionRadioFreq();
static void functActionRadioLeft();
static void functActionRadioRight();
static void functActionGoToMetronomeConfig();
static void functActionPreview();
static void functActionStopPreview();
static void functActionCrop();
static void functActionUndo();
static void functActionGoBack();
static void functActionStopRec();
static void functActionSave();
static void functActionAutoName();
static void functActionConfirmSaveLoad();
static void functActionConfirmSave();
static void functActionEndPoint();
static void functActionStartPoint();
static void functActionZoom();
static void functConfirmKey();
static void functInsert();

static void functInstrument(uint8_t state);

static void functEncoder(int16_t value);

static void functSwitchModule(uint8_t button);
static void functPlay();
static void functRec();

static void modStartPoint(int16_t value);
static void modEndPoint(int16_t value);

static void onPreRollMetronomeEnd();

void seek_callback(void);

void turnOffRecorder();

char recordingDirPath[30];

static int calcMaxRadioChannel(float baseFreq, float spacing)
{
    if (spacing <= 0.0f)
    {
        return 0;
    }

    return (int)((Radio::kMaxAudioFrequencyMHz - baseFreq) / spacing + 0.001f);
}

static int calcRadioChannel(float freq, float baseFreq, float spacing)
{
    return (int)((freq - baseFreq) / spacing + 0.5f);
}

void cSampleRecorder::update()
{
    if (waitForPreRollEnd == true)
    {
        if (startRecordingOnPreRollEnd == true)
        {
            functActionRecord();
            startRecordingOnPreRollEnd = false;
            waitForPreRollEnd = false;
        }

        return;
    }

    if (fullMemoryDuringRecordFlag)
    {
        fullMemoryDuringRecordFlag = 0;
        functActionStopRec();

        fullMemoryWindowFlag = 1;
        showSelectionWindowFullMemory();
    }

    avoidPopOnStart.tryUnblockSound();

    if (refreshSpectrum)
    {
        for (int channel = 0; channel < SAMPLE_CHANNELS; channel++)
        {
            spectrumParams[channel].address =
              recorder.getAddress((AudioInput::InputChannels)channel);
            spectrumParams[channel].length = recorder.getLength();
            spectrumParams[channel].recordInProgressFlag = recordInProgressFlag;
            GP.processSpectrum(&spectrumParams[channel], &zoom, &spectrum[channel]);
            display.refreshControl(spectrumControl[channel]);
        }
        if (recordInProgressFlag)
            spectrumTimerConstrains += 50;
        refreshSpectrum = 0;
    }
    if (refreshSpectrumValue)
    {
        display.setControlValue(playhead, playProgressInSpectrum);
        display.refreshControl(playhead);
        refreshSpectrumValue = 0;
    }

    if (refreshPoints)
    {
        processPoints();
        display.refreshControl(SR->pointsControl);
        refreshPoints = 0;
    }

    if (recordInProgressFlag)
    {
        showRecTimeValue();
        if (refreshSpectrumTimer > spectrumTimerConstrains)
        {
            refreshSpectrumTimer = 0;
            refreshSpectrum = 1;
        }
    }

#ifndef TRACKER_MINI
    if (radio->updateRDS())
    {
        if (recorderConfig.source == sourceTypeRadio && currentScreen == screenTypeConfig)
        {
            refreshRDS();
        }
    }
    radio->updateSeek();
#endif

    uint8_t managerStatus = newFileManager.getStatus();

    if (managerStatus == fmSaveRecordedSoundEnd)
    {
        interfaceGlobals.refreshFileExplorer = true; // odwiez liste plikow w sample laderze
        newFileManager.clearStatus();
        setCurrentScreenType(cSampleRecorder::screenTypeConfig);
        showDefaultScreen();
        display.processDisplayUpdateImmediate();

        if (SR->saveOrSaveloadFlag == cSampleRecorder::saveTypeLoad)
        {
            newFileManager.reloadSamplesFromWorkspace();
        }
        else
        {
            refreshMonitorState();
            setDefaultScreenFunct();
            ip->unblockAllInputs();
        }
    }
    else if (managerStatus == fmReloadSamplesEnd)
    {
        ip->unblockAllInputs();

        newFileManager.clearStatus();
        wasSampleMemoryOverwritten = 0;

        if (SR->saveOrSaveloadFlag == cSampleRecorder::saveTypeLoad)
        {
            uint8_t button = interfaceButtonSampleLoad;
            SR->firstFreeInstrumentSlotFound++;
            // The handler reads the options through a uint32_t pointer.
            uint32_t slot = static_cast<uint8_t>(SR->firstFreeInstrumentSlotFound);
            SR->eventFunct(eventSwitchModule, SR, &button, &slot);
        }
        else
        {
            if ((SR->switchToMetronome == true) && (SR->moduleToSwitch == interfaceButtonConfig))
            {
                SR->eventFunct(eventSwitchModule, SR, &SR->moduleToSwitch, &SR->optionForMetronome);
                SR->switchToMetronome = false;
            }
            else
            {
                SR->eventFunct(eventSwitchModule, SR, &SR->moduleToSwitch, 0);
            }
        }
    }
    else if (managerStatus >= fmError)
    {
        setCurrentScreenType(cSampleRecorder::screenTypeConfig);

        showDefaultScreen();
        setDefaultScreenFunct();
        ip->unblockAllInputs();
        newFileManager.clearStatus();
    }

    if (notEnoughInstrumentsFlag)
    {
        notEnoughInstrumentsFlag = 0;
        showSelectionNotEnoughInstruments();
    }
    if (notEnoughMemoryFlag)
    {
        notEnoughMemoryFlag = 0;
        showSelectionNotEnoughMemory();
    }

    if (changeSourceToMicFlag == 1)
    {
        if (changeSourceToMicTimer > POP_TIME)
        {
            changeSourceToMicFlag = 0;
            codec->setHeadphoneMuted(false);
        }
    }

    if (playInProgressFlag)
    {
        calcPlayProgressValue();
        if (playInProgressFlag)
            showPreviewValue(); // w calcPlayProgress jest mozliwosc wyzerowania tej flagi wtedy nie
                                // chcemy wyswietlac wartosci;
        else
        {
            hidePreviewValue();
            mtPadBoard.clearVoice(0);
        }
    }

    changeLevelBar();
}

void cSampleRecorder::start(uint32_t options)
{
    recorder.setFullMemoryCallback([this]() { this->fullMemoryDuringRecordFlag = 1; });
    codec = platform->getCodecAudio();
    radio = platform->getRadio();
    fileSystem = platform->getFileSystem();
    isSwitchingFromMenu = false;

    if (sequencer.isPlay())
    {
        // stop() puts this instrument back, also when the recorder is left from here.
        globalLastUsedInstrumentIndex = mtProject.values.lastUsedInstrument;

        if (mtMenuPopup.isPopupVisible())
        {
            handleStartupWithPopupAndMenu();
            return;
        }

        patternIsPlayingFlag = 1;
        showSelectionStopPattern();
        setStopSeqFunctions();
        return;
    }

    sequencer.setMidiInVoiceMode(Sequencer::midiInVoiceMode_SampleRecorder);
    sequencer.setPreRollMetronomeEndCallback(onPreRollMetronomeEnd);
    avoidPopOnStart.blockSoundOnStart();

    moduleRefresh = 1;
    dontTurnOffRadio = 0;

    if (options == 0)
    {
        saveOrSaveloadFlag = cSampleRecorder::saveTypeNormal;
        setCurrentScreenType(cSampleRecorder::screenTypeConfig);
    }

    engine.blockAllNoteOn();
    sequencer.setSynthMute(Sequencer::MuteState::kMuted);

    engine.blockDelayRefresh();
    engine.clearDelay();
    engine.clearReverb();
    engine.disconnectMixerImpact();
    engine.disableLineInImpact();
    engine.setLimiterSidechain(0); // force limiter sidechain to not to be line in R/L

    processPoints();
    listMonitor();
    showMonitorList();
    showSourceList();
    activateAudioInputSource(recorderConfig.source);
    refreshGain();
    refreshOutputSource();
    // showZoomValue();

    points.selected = 0;
    clearAllNodes();
    cancelMultiFrame();

    ip->setButtonObj(interfaceButtonMiniMenu, functMenu);

    ip->setButtonObj(interfaceButtonInstr, functInstrument);
    ip->setButtonObj(interfaceButtonPlay, buttonPress, functPlay);

#ifndef TRACKER_MINI
    radio->enable(true);
    radio->setRegion((Radio::Region)mtConfig.general.radioRegion);

    radio->setSeekCallback(seek_callback);

    if (radio->isRegionChanged())
    {
        recorderConfig.radioFreq = radio->getBaseFrequency();
        radioFreqBarVal = 0;
    }
    else
    {
        float radioBottomFreq = radio->getBaseFrequency();
        float radioSpacing = radio->getSpacing();
        if (radioSpacing > 0.0f)
        {
            int maxChannel = calcMaxRadioChannel(radioBottomFreq, radioSpacing);
            int channel = clamp<int>(
              calcRadioChannel(recorderConfig.radioFreq, radioBottomFreq, radioSpacing),
              0,
              maxChannel);
            recorderConfig.radioFreq = radioBottomFreq + (channel * radioSpacing);
        }
    }

    radio->setFrequency(recorderConfig.radioFreq);
#endif

    if (currentScreen == screenType::screenTypeConfig)
    {
        refreshMonitorState();
    }
    else
    {
        engine.forceLineInStreamVolume(LINE_IN_VOLUME_MIN);
    }

    showDefaultScreen();
    setDefaultScreenFunct();

    activateLabelsBorder();

    globalLastUsedInstrumentIndex = mtProject.values.lastUsedInstrument;

    if (mtProject.values.lastUsedInstrument >= MIDI_INSTRUMENTS_START_IDX &&
        mtProject.values.lastUsedInstrument < MIDI_INSTRUMENTS_START_IDX + kMidiInstrumentsCount)
    {
        recorderMidiLastUsedInstrumentIndex = mtProject.values.lastUsedInstrument;
    }
    else
    {
        recorderMidiLastUsedInstrumentIndex =
          clamp<int>(recorderMidiLastUsedInstrumentIndex,
                     MIDI_INSTRUMENTS_START_IDX,
                     MIDI_INSTRUMENTS_START_IDX + kMidiInstrumentsCount - 1);

        mtProject.values.lastUsedInstrument = recorderMidiLastUsedInstrumentIndex;
    }

    // This can leave the recorder for another module. It has to come last, so that stop()
    // puts back the instrument replaced above before that module starts.
    if (options > 0 && options != interfaceButtonSampleRec)
    {
        functSwitchModule(options);
    }
}

void cSampleRecorder::stop()
{
    if (!dontTurnOffRadio)
    {
        engine.forceLineInStreamVolume(LINE_IN_VOLUME_MIN);
    }
    engine.refreshOutputSourceOutsideTheRecorder();
    engine.forceUsbGainOutsideTheRecorder();
    radio->clearRDS();
    radio->setSeekCallback(nullptr);
    radio->enable(false);

    if (recordInProgressFlag)
    {
        // The module can be taken off the screen in the middle of a recording (power button,
        // card removed). Nothing else would end the recording then.
        recordInProgressFlag = 0;
        recorder.stop();
    }

    moduleRefresh = 0;
    avoidPopOnStart.tryUnblockSoundOnExit();
    if (patternIsPlayingFlag)
    {
        patternIsPlayingFlag = 0;
    }
    else
    {
        if (sequencer.getSeqState() != Sequencer::SEQ_STATE_STOP)
        {
            sequencer.stop();
        }
        engine.clearDelay();
        engine.clearReverb();
        engine.unblockDelayRefresh();
        engine.connectMixerImpact();
        engine.enableLineInImpact();
        engine.unblockAllNoteOn();
    }

    engine.resetLimiter();
    engine.setInputConfigOutsideRecorderInterfaceModule();
    mtProject.values.lastUsedInstrument = globalLastUsedInstrumentIndex;

    sequencer.setMidiInVoiceMode(Sequencer::midiInVoiceMode_default);
    sequencer.setSynthMute(Sequencer::MuteState::kUnmuted);

    mtPopups.hideInfoPopup();
}

void cSampleRecorder::setDefaultScreenFunct()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearAllPots();

    ip->setButtonObj(interfaceButtonLeft, buttonPress, functLeft);
    ip->setButtonObj(interfaceButtonRight, buttonPress, functRight);
    ip->setButtonObj(interfaceButtonUp, buttonPress, functUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functDown);

    //	ip->setButtonObj(interfaceButtonEnter, buttonPress, functEnter);
    //	ip->setButtonObj(interfaceButtonEncoder, buttonPress, functEnter);

    ip->setButtonObj(interfaceButton0, functActionButton0);
    ip->setButtonObj(interfaceButton1, functActionButton1);
    ip->setButtonObj(interfaceButton2, functActionButton2);
    ip->setButtonObj(interfaceButton3, buttonPress, functActionButton3);
    ip->setButtonObj(interfaceButton4, buttonPress, functActionButton4);
    ip->setButtonObj(interfaceButton5, buttonPress, functActionButton5);
    ip->setButtonObj(interfaceButton6, buttonPress, functActionButton6);
    ip->setButtonObj(interfaceButton7, buttonPress, functActionButton7);

    ip->setButtonObj(interfaceButtonInsert, buttonPress, functInsert);
    ip->setButtonObj(interfaceButtonRec, buttonPress, functRec);
    ip->setButtonObj(interfaceButtonDelete, functDeleteBackspace);

    ip->setPotObj(interfacePot0, functEncoder);

    ip->setButtonObj(interfaceButtonMiniMiddle, functMiniMiddle);

    ip->setPadsGlobal(functPads);

    if (currentScreen == screenTypeConfig && (patternIsPlayingFlag != 1))
    {
        setConfigScreenFunct();
    }

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
    ip->setButtonObj(interfaceButtonGame, buttonPress, functSwitchModule);
}

void cSampleRecorder::setStopSeqFunctions()
{
    ip->clearAll();
    ip->setButtonObj(interfaceButton6, buttonPress, functStopPatternCancel);
    ip->setButtonObj(interfaceButton7, buttonPress, functStopPatternYes);
}

void cSampleRecorder::setConfigScreenFunct()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);

    ip->setButtonObj(interfaceButton0, buttonPress, functSelectButton0);
    ip->setButtonObj(interfaceButton0 + RADIO_COL1, buttonPress, functActionRadioFreq);
    ip->setButtonObj(interfaceButton0 + RADIO_COL2, buttonPress, functActionRadioLeft);
    ip->setButtonObj(interfaceButton0 + RADIO_COL3, buttonPress, functActionRadioRight);
    // ip->setButtonObj(interfaceButton4, buttonPress, functActionButton4);
    ip->setButtonObj(interfaceButton0 + GAIN_COL, buttonPress, functActionGain);
    ip->setButtonObj(interfaceButton0 + MONITOR_COL, buttonPress, functActionMonitor);
    ip->setButtonObj(interfaceButton7, buttonPress, functActionButton7);
}

//==============================================================================================================

void cSampleRecorder::handleStartupWithPopupAndMenu()
{
    ip->setButtonObj(interfaceButtonMiniMenu, functMenu);

    processPoints();
    listMonitor();

    showMonitorList();
    showSourceList();
    showDefaultScreen();
}

void cSampleRecorder::refreshGain()
{
    if (isLineInSource() || (recorderConfig.source == sourceTypeRadio))
    {
        uint8_t localMap = 0;
        if (isLineInSource())
        {
            localMap = recorderConfig.gainLineIn * 15 / 100;
        }
        else if (recorderConfig.source == sourceTypeRadio)
        {
            localMap = recorderConfig.gainRadio * 15 / 100;
        }

        codec->setLineInLevel(localMap);
    }
    else if ((recorderConfig.source == sourceTypeMicLG) ||
             (recorderConfig.source == sourceTypeMicHG))
    {
        float localMap = 0.0f;

        if ((recorderConfig.source == sourceTypeMicLG))
        {
#ifdef TRACKER_MINI
            localMap = recorderConfig.gainMicLow * 44.0f / 100.0f;
#else
            localMap = recorderConfig.gainMicLow * 25.0f / 100.0f;
#endif
        }
        else if (recorderConfig.source == sourceTypeMicHG)
        {
            localMap = recorderConfig.gainMicHigh * 44.0f / 100.0f;
        }
        codec->setMicGain(localMap);
    }
    else if (isUsbSource())
    {
        float resultGain = mapClamped<float>(recorderConfig.gainUsb, 0, 100, 1.0f, 3.98f);
        engine.getAudioInput()
          ->getUsbRecordingAmplifier(AudioInput::InputChannels::kLeft)
          ->gain(resultGain);
        engine.getAudioInput()
          ->getUsbRecordingAmplifier(AudioInput::InputChannels::kRight)
          ->gain(resultGain);
    }
}

void cSampleRecorder::refreshMonitorState()
{
    engine.forceLineInStreamVolume(recorderConfig.monitor ? LINE_IN_VOLUME_MIN
                                                          : DEFAULT_LINE_IN_VOLUME);
    if (isUsbSource())
    {
        refreshGain();
    }
}

void cSampleRecorder::processPoints()
{

    points.pointsType = 0;

    if (startPoint >= zoom.zoomStart && startPoint <= zoom.zoomEnd)
    {
        points.startPoint = ((startPoint - zoom.zoomStart) * 599) / zoom.zoomWidth;
    }
    else
    {
        points.startPoint = -1;
    }

    if (endPoint >= zoom.zoomStart && endPoint <= zoom.zoomEnd)
    {
        points.endPoint = ((endPoint - zoom.zoomStart) * 599) / zoom.zoomWidth;
    }
    else
    {
        points.endPoint = -1;
    }
}

void cSampleRecorder::listMonitor()
{

    for (uint8_t i = 0; i < monitorCount; i++)
    {
        monitorNames[i] = (char*)&monitorNamesLabels[i][0];
    }
}

void cSampleRecorder::noteOnHandle(uint8_t channel, uint8_t note, uint8_t velocity, int16_t source)
{
    if ((SR->currentScreen == SR->screenTypeRecord) && (!SR->recordInProgressFlag))
    {

        if (mtPadBoard.getEmptyVoice() < 0)
            return;

        if (mtPadBoard.getEmptyVoice() == 0)
        {
            SR->refreshPlayProgressValue = 0;
            SR->playInProgressFlag = 1;
        }

        uint32_t length =
          (uint32_t)((uint32_t)SR->endPoint * (float)(recorder.getLength()) / MAX_16BIT);
        uint32_t addressShift =
          (uint32_t)((uint32_t)SR->startPoint * (float)(recorder.getLength()) / MAX_16BIT);

        int16_t padboardSource =
          (channel == Sequencer::GRID_OUTSIDE_PATTERN) ? source : 100 + channel;

        std::vector<int16_t*> addresses;
        addresses.push_back(recorder.getAddress(AudioInput::InputChannels::kLeft) + addressShift);
        if (SR->isStereoSource())
        {
            addresses.push_back(recorder.getAddress(AudioInput::InputChannels::kRight) + addressShift);
        }
        mtPadBoard.startNoteWithoutInstrument(
          note, addresses, length - addressShift, DEFAULT_INSTRUMENT_VOLUME, padboardSource);
    }
    else
    {
        sequencer.handleNoteOn(Sequencer::GRID_OUTSIDE_PATTERN, note, velocity, source);
    }
}
void cSampleRecorder::noteOffHandle(uint8_t channel, uint8_t note, uint8_t velocity, int16_t source)
{
    if ((SR->currentScreen == SR->screenTypeRecord) && (!SR->recordInProgressFlag))
    {

        int16_t padboardSource =
          (channel == Sequencer::GRID_OUTSIDE_PATTERN) ? source : 100 + channel;

        if (mtPadBoard.getVoiceTakenByPad(padboardSource) == 0)
        {
            SR->playProgressValue = 0;
            SR->playProgressInSpectrum = 0;
            SR->playInProgressFlag = 0;
            SR->refreshSpectrumProgress = 1;
            SR->refreshSpectrumValue = 1;
            SR->hidePreviewValue();
        }

        mtPadBoard.stopInstrument(padboardSource);
    }
    else
    {
        sequencer.handleNoteOff(Sequencer::GRID_OUTSIDE_PATTERN, note, velocity, source);
    }
}

//==============================================================================================================
static void functSelectButton0()
{
    if (SR->recordInProgressFlag == 1)
    {
        return;
    }
    if (SR->selectionWindowFlag == 1)
    {
        return;
    }

    if (SR->currentScreen == cSampleRecorder::screenTypeConfig && SR->selectedPlace == 0)
    {
        SR->changeSourceSelection(1, true);
    }

    SR->selectedPlace = 0;
    SR->activateLabelsBorder();

    if (SR->currentScreen == cSampleRecorder::screenTypeRecord)
    {
        SR->points.selected = 0;
        SR->refreshPoints = 1;
    }
}

static void functSelectButton1(uint8_t state)
{
    if (SR->recordInProgressFlag == 1)
    {
        return;
    }
    if (SR->selectionWindowFlag == 1)
    {
        return;
    }
    if ((SR->currentScreen == cSampleRecorder::screenTypeConfig) &&
        (SR->recorderConfig.source != cSampleRecorder::sourceTypeRadio))
    {
        return;
    }

    if (SR->currentScreen == cSampleRecorder::screenTypeRecord)
    {
        if (state == buttonPress || state == UINT8_MAX)
        {
            if (SR->zoom.zoomValue > 1.0 && SR->zoom.lastChangedPoint != 1)
            {
                SR->refreshSpectrum = 1;
                SR->zoom.lastChangedPoint = 1;
                SR->zoom.zoomPosition = SR->startPoint;
            }

            if (state == buttonPress)
            {
                SR->frameData.multisel[1].frameNum = 1;
                SR->frameData.multisel[1].isActive = 1;
                SR->frameData.multiSelActiveNum += 1;
                SR->addNode(modStartPoint, 0);
            }

            if (SR->frameData.multiSelActiveNum < 2)
            {
                SR->points.selected = 0;
            }

            SR->points.selected |= selectStart;

            SR->selectedPlace = 1;
        }
        else if (state == buttonRelease)
        {
            if (SR->frameData.multiSelActiveNum)
            {
                SR->points.selected &= ~selectStart;

                if (SR->frameData.multisel[1].isActive)
                {
                    SR->removeNode(0);
                    SR->frameData.multiSelActiveNum -= 1;

                    SR->frameData.multisel[1].isActive = 0;

                    if (SR->frameData.multiSelActiveNum == 0)
                    {
                        SR->selectedPlace = 1;
                        SR->points.selected |= selectStart;
                    }
                }
            }
        }

        SR->refreshPoints = 1;
    }
    else
    {
        if (state == buttonPress)
        {
            SR->selectedPlace = 1;
        }
    }

    SR->activateLabelsBorder();
}

static void functSelectButton2(uint8_t state)
{
    if (SR->recordInProgressFlag == 1)
    {
        return;
    }
    if (SR->selectionWindowFlag == 1)
    {
        return;
    }
    if ((SR->currentScreen == cSampleRecorder::screenTypeConfig) &&
        (SR->recorderConfig.source != cSampleRecorder::sourceTypeRadio))
    {
        return;
    }

    if (SR->currentScreen == cSampleRecorder::screenTypeRecord)
    {
        if (state == buttonPress || state == UINT8_MAX)
        {
            if (SR->zoom.zoomValue > 1.0 && SR->zoom.lastChangedPoint != 2)
            {
                SR->refreshSpectrum = 1;
                SR->zoom.lastChangedPoint = 2;
                SR->zoom.zoomPosition = SR->endPoint;
            }

            if (state == buttonPress)
            {
                SR->frameData.multisel[2].frameNum = 2;
                SR->frameData.multisel[2].isActive = 1;
                SR->frameData.multiSelActiveNum += 1;
                SR->addNode(modEndPoint, 1);
            }

            if (SR->frameData.multiSelActiveNum < 2)
            {
                SR->points.selected = 0;
            }

            SR->points.selected |= selectEnd;

            SR->selectedPlace = 2;
        }
        else if (state == buttonRelease)
        {
            if (SR->frameData.multiSelActiveNum)
            {
                SR->points.selected &= ~selectEnd;

                if (SR->frameData.multisel[2].isActive)
                {
                    SR->removeNode(1);
                    SR->frameData.multiSelActiveNum -= 1;

                    SR->frameData.multisel[2].isActive = 0;

                    if (SR->frameData.multiSelActiveNum == 0)
                    {
                        SR->selectedPlace = 2;
                        SR->points.selected |= selectEnd;
                    }
                }
            }
        }

        SR->refreshPoints = 1;
    }
    else
    {
        if (state == buttonPress)
        {
            SR->selectedPlace = 2;
        }
    }

    SR->activateLabelsBorder();
}
static void functSelectButton3()
{
    if (SR->recordInProgressFlag == 1)
    {
        return;
    }
    if (SR->selectionWindowFlag == 1)
    {
        return;
    }
    if ((SR->currentScreen == cSampleRecorder::screenTypeConfig) &&
        (SR->recorderConfig.source != cSampleRecorder::sourceTypeRadio))
    {
        return;
    }

    SR->selectedPlace = 3;
    SR->activateLabelsBorder();

    if (SR->currentScreen == cSampleRecorder::screenTypeRecord)
    {
        SR->clearAllNodes();
        SR->cancelMultiFrame();
        SR->points.selected = 0;
        SR->refreshPoints = 1;
    }
}

static void functSelectButton4()
{
    if (SR->recordInProgressFlag == 1)
    {
        return;
    }
    if (SR->selectionWindowFlag == 1)
    {
        return;
    }
    if (SR->currentScreen == cSampleRecorder::screenTypeConfig)
    {
        return;
    }

    SR->selectedPlace = 4;
    SR->activateLabelsBorder();

    if (SR->currentScreen == cSampleRecorder::screenTypeRecord)
    {
        SR->points.selected = 0;
        SR->refreshPoints = 1;
    }
}

static void functSelectButton5()
{
    if (SR->recordInProgressFlag == 1)
    {
        return;
    }
    if (SR->selectionWindowFlag == 1)
    {
        return;
    }

    SR->selectedPlace = 5;
    SR->activateLabelsBorder();

    if (SR->currentScreen == cSampleRecorder::screenTypeRecord)
    {
        SR->points.selected = 0;
        SR->refreshPoints = 1;
    }
}

static void functSelectButton6()
{
    if (SR->recordInProgressFlag == 1)
    {
        return;
    }
    if (SR->selectionWindowFlag == 1)
    {
        return;
    }

    if (SR->currentScreen == cSampleRecorder::screenTypeConfig && SR->selectedPlace == 6)
    {
        SR->changeMonitorSelection(1, true);
    }

    SR->selectedPlace = 6;
    SR->activateLabelsBorder();
    if (SR->currentScreen == cSampleRecorder::screenTypeRecord)
    {
        SR->points.selected = 0;
        SR->refreshPoints = 1;
    }
}

static void functSelectButton7()
{
    if (SR->selectionWindowFlag == 1)
    {
        return;
    }
    SR->selectedPlace = 7;
    SR->activateLabelsBorder();
    if (SR->currentScreen == cSampleRecorder::screenTypeRecord)
    {
        SR->points.selected = 0;
        SR->refreshPoints = 1;
    }
}

static void functActionButton0(uint8_t s)
{
    if (SR->fullMemoryWindowFlag || SR->selectionWindowFlag || SR->patternIsPlayingFlag ||
        SR->selectionWindowSaveFlag)
    {
        return;
    }

    if (SR->currentScreen == cSampleRecorder::screenTypeKeyboard)
    {
        if (s == buttonPress)
        {
            functConfirmKey();
        }
        return;
    }

    if (s == buttonPress)
    {
        switch (SR->currentScreen)
        {
            case cSampleRecorder::screenTypeConfig: break;
            case cSampleRecorder::screenTypeRecord: functActionPreview(); break;
            case cSampleRecorder::screenTypeKeyboard: break;
            default: break;
        }
    }
    else if (s == buttonRelease)
    {
        switch (SR->currentScreen)
        {
            case cSampleRecorder::screenTypeConfig: break;
            case cSampleRecorder::screenTypeRecord: functActionStopPreview(); break;
            case cSampleRecorder::screenTypeKeyboard: break;
            default: break;
        }
    }
}

//==============================================================================================================
static void functActionButton1(uint8_t state)
{
    if (SR->fullMemoryWindowFlag || SR->selectionWindowFlag || SR->patternIsPlayingFlag ||
        SR->selectionWindowSaveFlag)
    {
        return;
    }

    if (state == buttonPress || state == buttonRelease)
    {
        if (SR->currentScreen != cSampleRecorder::screenTypeKeyboard)
        {
            functSelectButton1(state);
        }
    }

    if (state == buttonPress)
    {
        switch (SR->currentScreen)
        {
            case cSampleRecorder::screenTypeConfig: break;
            case cSampleRecorder::screenTypeRecord: functActionStartPoint(); break;
            case cSampleRecorder::screenTypeKeyboard: functActionAutoName(); break;
            default: break;
        }
    }
}

static void functActionButton2(uint8_t state)
{
    if (SR->fullMemoryWindowFlag || SR->selectionWindowFlag || SR->patternIsPlayingFlag ||
        SR->selectionWindowSaveFlag)
    {
        return;
    }

    if (state == buttonPress || state == buttonRelease)
    {
        if (SR->currentScreen != cSampleRecorder::screenTypeKeyboard)
        {
            functSelectButton2(state);
        }
    }

    if (state == buttonPress)
    {
        switch (SR->currentScreen)
        {
            case cSampleRecorder::screenTypeConfig: functActionRadioLeft(); break;
            case cSampleRecorder::screenTypeRecord: functActionEndPoint(); break;
            case cSampleRecorder::screenTypeKeyboard: break;
            default: break;
        }
    }
}

static void functActionButton3()
{
    if (SR->fullMemoryWindowFlag || SR->selectionWindowFlag || SR->patternIsPlayingFlag ||
        SR->selectionWindowSaveFlag)
    {
        return;
    }

    if (SR->currentScreen != cSampleRecorder::screenTypeKeyboard)
    {
        functSelectButton3();
    }

    switch (SR->currentScreen)
    {
        case cSampleRecorder::screenTypeConfig:
        {
            if (SR->recorderConfig.source == cSampleRecorder::sourceTypeRadio)
            {
                functActionRadioRight();
            }
            else
            {
                functActionGoToMetronomeConfig();
            }
            break;
        }
        case cSampleRecorder::screenTypeRecord: functActionZoom(); break;
        case cSampleRecorder::screenTypeKeyboard: break;
        default: break;
    }
}

static void functActionButton4()
{
    if (SR->fullMemoryWindowFlag || SR->selectionWindowFlag || SR->patternIsPlayingFlag ||
        SR->selectionWindowSaveFlag)
    {
        return;
    }

    switch (SR->currentScreen)
    {
        case cSampleRecorder::screenTypeConfig: break;
        case cSampleRecorder::screenTypeRecord: functActionCrop(); break;
        case cSampleRecorder::screenTypeKeyboard: break;
        default: break;
    }
}

static void functActionButton5()
{
    if (SR->fullMemoryWindowFlag || SR->selectionWindowFlag || SR->patternIsPlayingFlag ||
        SR->selectionWindowSaveFlag)
    {
        return;
    }

    if ((SR->currentScreen == cSampleRecorder::screenTypeConfig) &&
        (SR->currentScreen != cSampleRecorder::screenTypeKeyboard))
    {
        functSelectButton5();
    }

    switch (SR->currentScreen)
    {
        case cSampleRecorder::screenTypeConfig: functActionGain(); break;
        case cSampleRecorder::screenTypeRecord: functActionUndo(); break;
        case cSampleRecorder::screenTypeKeyboard: functActionGoBack(); break;
        default: break;
    }
}

static void functActionButton6()
{
    if (SR->fullMemoryWindowFlag)
    {
        return;
    }

    if (SR->patternIsPlayingFlag == 1)
    {
        return;
    }

    if (SR->selectionWindowFlag == 1)
    {
        SR->selectionWindowFlag = 0;
        // SR->selectedPlace = 6;

        SR->showDefaultScreen();
        SR->activateLabelsBorder();
        return;
    }

    if (SR->selectionWindowSaveFlag == 1)
    {
        FileSystem* fileSystem = TrackerImpl::getInstance()->getPlatform()->getFileSystem();
        char localPatch[70];
        uint16_t cnt = 1;
        char localName[MAX_NAME_LENGTH];
        strcpy(localName, SR->keyboardManager.getName());
        do
        {
            char keyboardName[MAX_NAME_LENGTH + 4];
            sprintf(keyboardName, "%s%d", localName, cnt);

            SR->keyboardManager.fillName(keyboardName);

            sprintf(localPatch, "Recorded/%s.wav", SR->keyboardManager.getName());

            cnt++;
            if (cnt > 9999)
            {
                memset(keyboardName, 0, 33);
                SR->keyboardManager.fillName(keyboardName);
                break;
            }
        } while (fileSystem->exists(localPatch));

        SR->keyboardManager.activateKeyboard(32);

        SR->selectionWindowSaveFlag = 0;

        SR->showDefaultScreen();
        return;
    }

    if ((SR->currentScreen != cSampleRecorder::screenTypeRecord) &&
        (SR->currentScreen != cSampleRecorder::screenTypeKeyboard))
    {
        functSelectButton6();
    }

    switch (SR->currentScreen)
    {
        case cSampleRecorder::screenTypeConfig: break;
        case cSampleRecorder::screenTypeRecord: functActionGoBack(); break;
        case cSampleRecorder::screenTypeKeyboard: functActionConfirmSaveLoad(); break;
        default: break;
    }
}

static void functActionButton7()
{
    if (SR->selectedPlace == 1)
    {
        SR->points.selected = selectStart;
    }
    else if (SR->selectedPlace == 2)
    {
        SR->points.selected = selectEnd;
    }

    if (SR->patternIsPlayingFlag == 1)
    {
        return;
    }

    if (SR->selectionWindowFlag == 1)
    {
        if (SR->changesLostSource == cSampleRecorder::enChangesLostSource::cancelFunct)
        {
            SR->clearAllNodes();
            SR->cancelMultiFrame();

            SR->selectionWindowFlag = 0;
            SR->setCurrentScreenType(cSampleRecorder::screenTypeConfig);
            SR->selectedPlace = 0;
            if (!SR->recorderConfig.monitor)
            {
                engine.forceLineInStreamVolume(DEFAULT_LINE_IN_VOLUME);
            }

            GP.spectrumResetZoom(0, recorder.getLength(), &SR->zoom);

            SR->showDefaultScreen();
            SR->setDefaultScreenFunct();
            SR->activateLabelsBorder();

            return;
        }
        else if (SR->changesLostSource == cSampleRecorder::enChangesLostSource::switchModuleFunct)
        {
            SR->ip->blockAllInputs();
            newFileManager.reloadSamplesFromWorkspace();
            SR->selectionWindowFlag = 0;
            return;
        }
    }

    if (SR->selectionWindowSaveFlag == 1)
    {
        SR->selectionWindowSaveFlag = 0;
        if (SR->saveOrSaveloadFlag == cSampleRecorder::saveTypeNormal)
        {
            if (newFileManager.saveRecordedSound(
                  recordingDirPath,
                  SR->keyboardManager.getName(),
                  std::nullopt,
                  recorder.getAddress(AudioInput::InputChannels::kLeft),
                  recorder.getAddress(AudioInput::InputChannels::kRight),
                  recorder.getLength()))
            {
                SR->ip->blockAllInputs();
            }
        }
        else if (SR->saveOrSaveloadFlag == cSampleRecorder::saveTypeLoad)
        {
            if ((mtProject.used_memory + recorder.getSize()) > mtProject.max_memory)
            {
                SR->notEnoughMemoryFlag = 1;
                return;
            }
            SR->firstFreeInstrumentSlotFound = -1;
            for (uint8_t i = 0; i < kSampleInstrumentsCount; i++)
            {
                if (!mtProject.instrument[i].isActive)
                {
                    SR->firstFreeInstrumentSlotFound = i;
                    break;
                }
            }
            if (SR->firstFreeInstrumentSlotFound == -1)
            {
                SR->notEnoughInstrumentsFlag = 1;
                return;
            }

            if (newFileManager.saveRecordedSound(
                  recordingDirPath,
                  SR->keyboardManager.getName(),
                  SR->firstFreeInstrumentSlotFound < 0
                    ? std::nullopt
                    : std::make_optional(SR->firstFreeInstrumentSlotFound),
                  recorder.getAddress(AudioInput::InputChannels::kLeft),
                  recorder.getAddress(AudioInput::InputChannels::kRight),
                  recorder.getLength()))
            {
                SR->ip->blockAllInputs();
            }
        }

        SR->keyboardManager.deactivateKeyboard();
        return;
    }

    if (SR->fullMemoryWindowFlag)
    {
        SR->fullMemoryWindowFlag = 0;
        SR->showDefaultScreen();
        SR->activateLabelsBorder();
        return;
    }
    if (SR->notEnoughInstrumentsFlag)
    {
        SR->showDefaultScreen();
        return;
    }
    if (SR->notEnoughMemoryFlag)
    {
        SR->showDefaultScreen();
        return;
    }
    if (SR->currentScreen != cSampleRecorder::screenTypeKeyboard)
    {
        functSelectButton7();
    }

    switch (SR->currentScreen)
    {
        case cSampleRecorder::screenTypeConfig:
        {
            if (SR->waitForPreRollEnd && !sequencer.isSetInternalClock())
            {
                // The count-in follows the clock, and an external clock may never come.
                // Everything but this button is locked while waiting for it, so a second
                // press has to give the wait up instead of arming it again.
                SR->waitForPreRollEnd = false;
                SR->recordInProgressFlag = 0;
                mtPopups.hideInfoPopup();
                break;
            }
#ifndef TRACKER_MINI
            if (sequencer.isPreRollConfigOn() &&
                SR->recorderConfig.source != cSampleRecorder::sourceTypeRadio)
#else
            if (sequencer.isPreRollConfigOn())
#endif
            {
                SR->waitForPreRollEnd = true;
                SR->startRecordingOnPreRollEnd = false;
                SR->recordInProgressFlag = 1;

                sequencer.showPreRollPopup();
            }
            else
            {
                functActionRecord();
            }
            break;
        }
        case cSampleRecorder::screenTypeRecord: functActionStopRec(); break;
        case cSampleRecorder::screenTypeKeyboard: functActionConfirmSave(); break;
        default: break;
    }
}

//==============================================================================================================
static void functStopPatternCancel()
{
    SR->eventFunct(eventSwitchToPreviousModule, SR, 0, 0);
}

static void functStopPatternYes()
{
    SR->patternIsPlayingFlag = 0;
    sequencer.stop();

    mtProject.values.lastUsedInstrument = SR->globalLastUsedInstrumentIndex;
    SR->start(0);
}

//==============================================================================================================
static void functDeleteBackspace(uint8_t state)
{
    if (SR->patternIsPlayingFlag == 1)
    {
        return;
    }
    if (SR->selectionWindowFlag == 1)
    {
        return;
    }

    if (SR->keyboardManager.getState())
    {
        if ((state == buttonPress) || (state == buttonHold))
        {
            SR->keyboardManager.makeBackspace();
        }
    }
    else
    {
        if (state == buttonPress)
        {
            if (SR->currentScreen == cSampleRecorder::screenTypeConfig)
            {
                switch (SR->selectedPlace)
                {
                    case 0: SR->setDefaultSource(); break;
                    case SR->RADIO_COL1: SR->setDefaultRadioFreq(); break;
                    case SR->GAIN_COL: SR->setDefaultGain(); break;
                    case SR->MONITOR_COL: SR->setDefaultMonitorEnable(); break;
                    default: break;
                }
            }
            else if (SR->currentScreen == cSampleRecorder::screenTypeRecord)
            {

                if (SR->frameData.multiSelActiveNum)
                {
                    if (SR->frameData.multisel[1].isActive)
                        SR->setDefaultStartPoint();
                    if (SR->frameData.multisel[2].isActive)
                        SR->setDefaultEndPoint();
                }
                else
                {
                    switch (SR->selectedPlace)
                    {
                        case 0: break;
                        case 1: SR->setDefaultStartPoint(); break;
                        case 2: SR->setDefaultEndPoint(); break;
                        case 3: SR->setDefaultZoom(); break;
                        case 5: break;
                        case 6: break;
                    }
                }
            }
        }
    }
}

//==============================================================================================================
// tracker mini middle button
static void functMiniMiddle(uint8_t state)
{
    if (state != buttonPress)
    {
        return;
    }

    // oblsuga klawiatury
    if (SR->keyboardManager.getState())
    {
        SR->keyboardManager.confirmKey();
        return;
    }
}

//==============================================================================================================
static void functPads(uint8_t pad, uint8_t state, int16_t velo)
{
    (void)velo;

    if (!SR->recordInProgressFlag)
    {
        if (SR->keyboardManager.getState())
        {
            if ((state == 1) || (state == 2))
            {
                SR->keyboardManager.onPadChange(pad, state);
            }
            return;
        }
    }

    if (state == buttonPress)
    {
        padsBacklight.setFrontLayer(1, mtConfig.values.padsLightFront, pad);
        uint8_t noteFromPad = mtPadBoard.getNoteFromPad(pad);
        SR->noteOnHandle(Sequencer::GRID_OUTSIDE_PATTERN, noteFromPad, 127, pad);
    }
    else if (state == buttonRelease)
    {
        padsBacklight.setFrontLayer(0, 0, pad);
        uint8_t noteFromPad = mtPadBoard.getNoteFromPad(pad);
        SR->noteOffHandle(Sequencer::GRID_OUTSIDE_PATTERN, noteFromPad, 0, pad);
    }
}

static void functActionGain()
{
    SR->selectedPlace = SR->GAIN_COL;
    SR->activateLabelsBorder();
}

static void functActionMonitor()
{
    if (SR->currentScreen == cSampleRecorder::screenTypeConfig &&
        SR->selectedPlace == SR->MONITOR_COL)
    {
        SR->changeMonitorSelection(1, true);
    }

    SR->selectedPlace = SR->MONITOR_COL;
    SR->activateLabelsBorder();
}

static void functActionRecord()
{
    SR->recordInProgressFlag = 1;
    SR->setCurrentScreenType(cSampleRecorder::screenTypeRecord);
    SR->cropCounter = 0;
    SR->wasSampleMemoryOverwritten = 1;
    SR->showDefaultScreen();
    SR->setDefaultScreenFunct();

    Recorder::Config config;
    config.destinationBufferAddress = externalRamSampleMemory;
    config.destinationBufferSize = SAMPLE_MEMORY_SIZE;

    if (SR->isStereoSource())
    {
        config.destinationChannels = 2;
        config.sourceChannels = 2;
    }
    else if (SR->recorderConfig.source == cSampleRecorder::sourceTypeUsbMix ||
             SR->recorderConfig.source == cSampleRecorder::sourceTypeLineInM)
    {
        config.destinationChannels = 1;
        config.sourceChannels = 2;
    }
    else
    {
        config.destinationChannels = 1;
        config.sourceChannels = 1;
        config.useRightChannelForMono =
          (SR->recorderConfig.source == cSampleRecorder::sourceTypeLineInR);
    }

    config.source = SR->isUsbSource()
                      ? Recorder::Config::Source::kUsb
                      : Recorder::Config::Source::kI2s;

    recorder.setConfig(config);
    recorder.record();

    GP.spectrumResetZoom(0, 0, &SR->zoom);
    SR->spectrumTimerConstrains = 100;

    for (int channel = 0; channel < SAMPLE_CHANNELS; channel++)
    {
        SR->spectrumParams[channel].address =
          recorder.getAddress((AudioInput::InputChannels)channel);
        SR->spectrumParams[channel].length = 0;
    }

    SR->refreshSpectrum = 1;
}

static void functActionRadioFreq()
{
    if ((SR->currentScreen == cSampleRecorder::screenTypeConfig) &&
        (SR->recorderConfig.source != cSampleRecorder::sourceTypeRadio))
    {
        return;
    }

    SR->selectedPlace = SR->RADIO_COL1;

    SR->activateLabelsBorder();
}

static void functActionRadioLeft()
{
    if (SR->recorderConfig.source != cSampleRecorder::sourceTypeRadio)
    {
        return;
    }

    if (SR->radio->isinitialized())
    {
        SR->displaySeeking();
        SR->radio->clearRDS();
        SR->radio->seek(Radio::SeekDirection::kDown);
    }
}

static void functActionRadioRight()
{
    if (SR->recorderConfig.source != cSampleRecorder::sourceTypeRadio)
    {
        functActionGoToMetronomeConfig();
        return;
    }

    if (SR->radio->isinitialized())
    {
        SR->displaySeeking();
        SR->radio->clearRDS();
        SR->radio->seek(Radio::SeekDirection::kUp);
    }
}

static void functActionGoToMetronomeConfig()
{
    SR->switchToMetronome = true;
    functSwitchModule(interfaceButtonConfig);
}

static void functActionPreview()
{
    if (SR->recordInProgressFlag == 1)
    {
        return;
    }

    SR->noteOnHandle(Sequencer::GRID_OUTSIDE_PATTERN, 60, 127, INTERFACE_BUTTON_PREVIEW);
}

static void functActionStopPreview()
{
    SR->noteOffHandle(Sequencer::GRID_OUTSIDE_PATTERN, 60, 0, INTERFACE_BUTTON_PREVIEW);
}

static void functActionCrop()
{
    if (SR->recordInProgressFlag == 1)
    {
        return;
    }

    if (SR->startPoint >= SR->endPoint)
    {
        return;
    }

    for (int8_t i = (undoCount - 2); i >= 0; i--)
    {
        SR->undo[i + 1] = SR->undo[i];
    }

    for (int channel = 0; channel < SAMPLE_CHANNELS; channel++)
    {
        SR->undo[0].address[channel] = recorder.getAddress((AudioInput::InputChannels)channel);
    }

    SR->undo[0].length = recorder.getLength();
    SR->undo[0].startPoint = SR->startPoint;
    SR->undo[0].endPoint = SR->endPoint;

    if (SR->cropCounter < undoCount)
        SR->cropCounter++;

    recorder.crop(SR->startPoint, SR->endPoint);

    SR->startPoint = 0;
    SR->endPoint = MAX_16BIT;

    GP.spectrumResetZoom(SR->startPoint, recorder.getLength(), &SR->zoom);

    SR->showZoomValue();
    SR->showEndPointValue();
    SR->showStartPointValue();
    SR->refreshSpectrum = 1;
    SR->refreshPoints = 1;
}

static void functActionUndo()
{
    if (SR->recordInProgressFlag == 1)
    {
        return;
    }
    if (!SR->cropCounter)
    {
        return;
    }

    SR->cropCounter--;
    std::vector<int16_t*> addresses;
    addresses.push_back(SR->undo[0].address[0]);
#ifdef PLATFORM_IMX
    addresses.push_back(SR->undo[0].address[1]);
#endif
    recorder.undo(addresses, SR->undo[0].length);
    SR->startPoint = SR->undo[0].startPoint;
    SR->endPoint = SR->undo[0].endPoint;
    for (uint8_t i = 0; i < (undoCount - 1); i++)
    {
        SR->undo[i] = SR->undo[i + 1];
    }

    SR->showEndPointValue();
    SR->showStartPointValue();
    SR->refreshSpectrum = 1;
    SR->refreshPoints = 1;
}

static void functActionGoBack()
{
    if (SR->recordInProgressFlag == 1)
    {
        return;
    }
    if (SR->selectionWindowFlag == 1)
    {
        return;
    }

    if (SR->currentScreen == cSampleRecorder::screenTypeRecord)
    {
        SR->selectionWindowFlag = 1;
        SR->changesLostSource = cSampleRecorder::enChangesLostSource::cancelFunct;
        SR->showSelectionWindow();
    }
    else if (SR->currentScreen == cSampleRecorder::screenTypeKeyboard)
    {
        SR->setCurrentScreenType(cSampleRecorder::screenTypeRecord);
        SR->keyboardManager.deactivateKeyboard();
        SR->showDefaultScreen();
        SR->setDefaultScreenFunct();
    }
}

static void functActionSave()
{
    FileSystem* fileSystem = TrackerImpl::getInstance()->getPlatform()->getFileSystem();
    SR->setCurrentScreenType(cSampleRecorder::screenTypeKeyboard);
    SR->setDefaultScreenFunct();

    strcpy(recordingDirPath, "Recordings");
    uint8_t dir_num = 0;
    uint8_t files_count = 0;

    // znajdz ostatni folder Recordings xx
    do
    {
        dir_num++;
        if (dir_num > 1)
            sprintf(recordingDirPath, "Recordings %d", dir_num);
    } while (fileSystem->exists(recordingDirPath));

    dir_num--;

    if (dir_num <= 1)
        strcpy(recordingDirPath, "Recordings");
    else
        sprintf(recordingDirPath, "Recordings %d", dir_num);

    // sprawdz czy nie jest pelny
    files_count = fileSystem->countContainedFiles(recordingDirPath);

    if (files_count >= 99) // jesli pelny to przygotuj nazwe nowego
    {
        dir_num++;
        sprintf(recordingDirPath, "Recordings %d", dir_num);
    }

    char localPatch[70];
    uint16_t cnt = 1;
    char keyboardName[MAX_NAME_LENGTH];

    do
    {
        sprintf(keyboardName, "recording%03d", cnt);
        sprintf(localPatch, "%s/%s.wav", recordingDirPath, keyboardName);
        cnt++;
        if (cnt > 999)
        {
            break;
        }
    } while (fileSystem->exists(localPatch));

    SR->keyboardManager.fillName(keyboardName);
    SR->keyboardManager.activateKeyboard(32);

    SR->showDefaultScreen();
}

static void functActionConfirmSave()
{
    FileSystem* fileSystem = TrackerImpl::getInstance()->getPlatform()->getFileSystem();

    SR->saveOrSaveloadFlag = cSampleRecorder::saveTypeNormal;

    char filePath[70];
    sprintf(filePath, "%s/%s.wav", recordingDirPath, SR->keyboardManager.getName());

    if (fileSystem->exists(filePath))
    {
        SR->selectionWindowSaveFlag = 1;
        SR->keyboardManager.deactivateKeyboard();
        SR->showSelectionWindowSave();
    }
    else
    {

        if (newFileManager.saveRecordedSound(recordingDirPath,
                                             SR->keyboardManager.getName(),
                                             std::nullopt,
                                             recorder.getAddress(AudioInput::InputChannels::kLeft),
                                             recorder.getAddress(AudioInput::InputChannels::kRight),
                                             recorder.getLength()))
        {
            SR->ip->blockAllInputs();
        }
        SR->keyboardManager.deactivateKeyboard();
    }
}

static void functActionAutoName()
{
    SR->keyboardManager.setRandomName();
}

static void functActionConfirmSaveLoad()
{
    SR->saveOrSaveloadFlag = cSampleRecorder::saveTypeLoad;

    FileSystem* fileSystem = TrackerImpl::getInstance()->getPlatform()->getFileSystem();

    if ((mtProject.used_memory + recorder.getSize()) > mtProject.max_memory)
    {
        SR->notEnoughMemoryFlag = 1;
        return;
    }
    SR->firstFreeInstrumentSlotFound = -1;

    for (uint8_t i = 0; i < kSampleInstrumentsCount; i++)
    {
        if (!mtProject.instrument[i].isActive)
        {
            SR->firstFreeInstrumentSlotFound = i;
            // mtProject.values.lastUsedInstrument = i;
            break;
        }
    }
    if (SR->firstFreeInstrumentSlotFound == -1)
    {
        SR->notEnoughInstrumentsFlag = 1;
        return;
    }
    else
    {
        char filePath[70];
        sprintf(filePath, "%s/%s.wav", recordingDirPath, SR->keyboardManager.getName());
        if (fileSystem->exists(filePath))
        {
            SR->selectionWindowSaveFlag = 1;
            SR->keyboardManager.deactivateKeyboard();
            SR->showSelectionWindowSave();
            return;
        }
        else
        {
            if (newFileManager.saveRecordedSound(
                  recordingDirPath,
                  SR->keyboardManager.getName(),
                  SR->firstFreeInstrumentSlotFound < 0
                    ? std::nullopt
                    : std::make_optional(SR->firstFreeInstrumentSlotFound),
                  recorder.getAddress(AudioInput::InputChannels::kLeft),
                  recorder.getAddress(AudioInput::InputChannels::kRight),
                  recorder.getLength()))
            {
                SR->ip->blockAllInputs();
            }
            SR->keyboardManager.deactivateKeyboard();
        }
        // SR->firstFreeInstrumentSlotFound++;
    }
}

static void functActionStopRec()
{
    if (SR->recordInProgressFlag == 1)
    {
        SR->recordInProgressFlag = 0;
        SR->refreshOutputSource();
        engine.clearDelay();
        engine.clearReverb();

        SR->selectedPlace = 1;

        SR->calculatePointsAfterRecording();

        recorder.stop();

        SR->showStartPointValue();
        SR->showEndPointValue();

        engine.forceLineInStreamVolume(LINE_IN_VOLUME_MIN);
        SR->refreshSpectrum = 1;
        SR->refreshPoints = 1;

        SR->showDefaultScreen();
        return;
    }

    FileSystem* fileSystem = TrackerImpl::getInstance()->getPlatform()->getFileSystem();
    if (!fileSystem->isSdCardInserted())
    {
        return;
    }

    functActionSave();
}

static void functActionEndPoint()
{
    if (SR->recordInProgressFlag == 1)
    {
        return;
    }
    // SR->points.selected = 2;
    if (SR->zoom.zoomValue > 1.0)
    {
        SR->refreshSpectrum = 1;
    }
    SR->refreshPoints = 1;
}

static void functActionStartPoint()
{
    if (SR->recordInProgressFlag == 1)
    {
        return;
    }
}

static void functActionZoom()
{
    if (SR->recordInProgressFlag == 1)
    {
        return;
    }
}

static void functEncoder(int16_t value)
{
#ifdef TRACKER_MINI
    if (SR->keyboardManager.getState())
    {
        SR->keyboardManager.confirmKey();
        return;
    }
#endif

    if (SR->selectionWindowFlag == 1)
    {
        return;
    }
    if (SR->patternIsPlayingFlag == 1)
    {
        return;
    }

    if (SR->currentScreen == cSampleRecorder::screenTypeConfig)
    {
        switch (SR->selectedPlace)
        {
            case 0:
            {
                SR->changeSourceSelection(interfaceGlobals.invertValForList ? -value : value);
                break;
            }
            case SR->RADIO_COL1: SR->changeRadioFreqBar(value); break;
            case SR->RADIO_COL2: break;
            case SR->RADIO_COL3: break;
            case SR->GAIN_COL: SR->changeGainBar(value); break;
            case SR->MONITOR_COL:
            {
                SR->changeMonitorSelection(interfaceGlobals.invertValForList ? -value : value);
                break;
            }
        }
    }
    if (SR->currentScreen == cSampleRecorder::screenTypeRecord)
    {

        switch (SR->selectedPlace)
        {
            case 0: break;
            case 1:
                if (SR->frameData.multiSelActiveNum)
                {
                    SR->stepThroughNodes(value);
                }
                else
                {
                    modStartPoint(value);
                }
                break;
            case 2:
                if (SR->frameData.multiSelActiveNum)
                {
                    SR->stepThroughNodes(value);
                }
                else
                {
                    modEndPoint(value);
                }
                break;
            case 3: SR->changeZoom(value); break;
            case 5: break;
            case 6: break;
        }
    }
}

static void functLeft()
{
    if (SR->selectionWindowFlag == 1)
    {
        return;
    }
    if (SR->patternIsPlayingFlag == 1)
    {
        return;
    }
    if (SR->frameData.multiSelActiveNum != 0)
    {
        return;
    }

    if (SR->keyboardManager.getState())
    {
        SR->keyboardManager.makeMove('a');
        return;
    }

    if (SR->recordInProgressFlag)
    {
        return;
    }

    // issue #77 zmieniona obsluga zaznaczenia przycisku
    if (SR->currentScreen == cSampleRecorder::screenTypeConfig)
    {
        switch (SR->selectedPlace)
        {
            case SR->LEVEL_COL:
            case SR->GAIN_COL:
            {
                if (SR->recorderConfig.source == cSampleRecorder::sourceTypeRadio)
                    SR->selectedPlace = SR->RADIO_COL1;
                else
                    SR->selectedPlace = 0;
                break;
            }
            case SR->RADIO_COL1:
            case SR->RADIO_COL2:
            case SR->RADIO_COL3: SR->selectedPlace = 0; break;
            case SR->MONITOR_COL: SR->selectedPlace = SR->GAIN_COL; break;
            default: SR->selectedPlace = 0; break;
        }

        SR->activateLabelsBorder();
    }
    else if (SR->currentScreen == cSampleRecorder::screenTypeRecord)
    {
        if (SR->selectedPlace > 1)
            SR->selectedPlace--;

        switch (SR->selectedPlace)
        {
            case 0: functSelectButton0(); break;
            case 1: functSelectButton1(UINT8_MAX); break;
            case 2: functSelectButton2(UINT8_MAX); break;
            case 3: functSelectButton3(); break;
            case 4: functSelectButton4(); break;
            case 5: functSelectButton5(); break;
            case 6: functSelectButton6(); break;
        }
    }
}

static void functRight()
{
    if (SR->selectionWindowFlag == 1)
    {
        return;
    }
    if (SR->patternIsPlayingFlag == 1)
    {
        return;
    }
    if (SR->frameData.multiSelActiveNum != 0)
    {
        return;
    }

    if (SR->keyboardManager.getState())
    {
        SR->keyboardManager.makeMove('d');
        return;
    }

    if (SR->recordInProgressFlag)
    {
        return;
    }

    // issue #77 zmieniona obsluga zaznaczenia przycisku
    if (SR->currentScreen == cSampleRecorder::screenTypeConfig)
    {
        switch (SR->selectedPlace)
        {
            case 0:
            {
                if (SR->recorderConfig.source == cSampleRecorder::sourceTypeRadio)
                    SR->selectedPlace = SR->RADIO_COL1;
                else
                    SR->selectedPlace = SR->GAIN_COL;
                break;
            }
            case SR->GAIN_COL:
            {
                if (SR->isMonitorListVisible())
                {
                    SR->selectedPlace = SR->MONITOR_COL;
                }
                break;
            }
            case SR->RADIO_COL1:
            case SR->RADIO_COL2:
            case SR->RADIO_COL3:
            case SR->LEVEL_COL: SR->selectedPlace = SR->GAIN_COL; break;
            case SR->MONITOR_COL: break;
            default: SR->selectedPlace = SR->MONITOR_COL; break;
        }

        SR->activateLabelsBorder();
    }
    else if (SR->currentScreen == cSampleRecorder::screenTypeRecord)
    {
        if (SR->selectedPlace < 3)
            SR->selectedPlace++;

        switch (SR->selectedPlace)
        {
            case 1: functSelectButton1(UINT8_MAX); break;
            case 2: functSelectButton2(UINT8_MAX); break;
            case 3: functSelectButton3(); break;
            case 4: functSelectButton4(); break;
            case 5: functSelectButton5(); break;
            case 6: functSelectButton6(); break;
            case 7: functSelectButton7(); break;
        }
    }
}

static void functUp()
{
    if (SR->selectionWindowFlag == 1)
    {
        return;
    }
    if (SR->patternIsPlayingFlag == 1)
    {
        return;
    }

    if (SR->keyboardManager.getState())
    {
        SR->keyboardManager.makeMove('w');
        return;
    }

    if (SR->currentScreen == cSampleRecorder::screenTypeRecord)
    {
        switch (SR->selectedPlace)
        {
            case 1: modStartPoint(1); break;
            case 2: modEndPoint(1); break;
            case 3: SR->changeZoom(1); break;
            default: break;
        }
    }
    else if (SR->currentScreen == cSampleRecorder::screenTypeConfig)
    {
        switch (SR->selectedPlace)
        {
            case 0: SR->changeSourceSelection(-1); break;
            case SR->RADIO_COL1: SR->changeRadioFreqBar(1); break;
            case SR->RADIO_COL2: break;
            case SR->RADIO_COL3: break;
            case SR->GAIN_COL: SR->changeGainBar(1); break;
            case SR->MONITOR_COL: SR->changeMonitorSelection(-1); break;
            default: break;
        }
    }
}

static void functDown()
{
    if (SR->selectionWindowFlag == 1)
    {
        return;
    }
    if (SR->patternIsPlayingFlag == 1)
    {
        return;
    }

    if (SR->keyboardManager.getState())
    {
        SR->keyboardManager.makeMove('s');
        return;
    }

    if (SR->currentScreen == cSampleRecorder::screenTypeRecord)
    {
        switch (SR->selectedPlace)
        {
            case 1: modStartPoint(-1); break;
            case 2: modEndPoint(-1); break;
            case 3: SR->changeZoom(-1); break;
            default: break;
        }
    }
    else if (SR->currentScreen == cSampleRecorder::screenTypeConfig)
    {
        switch (SR->selectedPlace)
        {
            case 0: SR->changeSourceSelection(1); break;
            case SR->RADIO_COL1: SR->changeRadioFreqBar(-1); break;
            case SR->RADIO_COL2: break;
            case SR->RADIO_COL3: break;
            case SR->GAIN_COL: SR->changeGainBar(-1); break;
            case SR->MONITOR_COL: SR->changeMonitorSelection(1); break;
        }
    }
}

static void functSwitchModule(uint8_t button)
{
    SR->setDefaultScreenFunct();

    if (SR->isModuleSwitchingBlocked())
    {
        return;
    }

    if (button == interfaceButtonSampleRec)
    {
        return;
    }

    SR->moduleToSwitch = button;

    // jesli ekran nagrania to popup
    if (SR->currentScreen == cSampleRecorder::screenTypeRecord)
    {
        SR->selectionWindowFlag = 1;
        SR->changesLostSource = cSampleRecorder::enChangesLostSource::switchModuleFunct;
        // SR->moduleToSwitch = button;
        SR->showSelectionWindow();
        return;
    }

    // do mastera mozna zawsze
    if (button == interfaceButtonMaster && !SR->isSwitchingFromMenu)
    {
        SR->dontTurnOffRadio = 1;
        // The handler reads the options through a uint32_t pointer.
        uint32_t option = interfaceButtonSampleRec;
        SR->eventFunct(eventSwitchModule, SR, &button, &option);
        return;
    }

    SR->isSwitchingFromMenu = false;

    // przeladowac pamiec czy nie
    if (SR->wasSampleMemoryOverwritten == 0)
    {
        if ((SR->switchToMetronome == true) && (button == interfaceButtonConfig))
        {
            SR->eventFunct(eventSwitchModule, SR, &button, &SR->optionForMetronome);
            SR->switchToMetronome = false;
        }
        else
        {
            SR->eventFunct(eventSwitchModule, SR, &button, 0);
        }
    }
    else
    {
        SR->ip->blockAllInputs();
        newFileManager.reloadSamplesFromWorkspace();
    }
}

//======================================================================================================================
void cSampleRecorder::calcRadioFreqBarVal()
{
    float radioBottomFreq = radio->getBaseFrequency();
    float radioSpacing = radio->getSpacing();
    int maxChannel = calcMaxRadioChannel(radioBottomFreq, radioSpacing);
    float radioTopFreq = (maxChannel > 0) ? (radioBottomFreq + maxChannel * radioSpacing)
                                          : (float)Radio::kMaxAudioFrequencyMHz;
    radioFreqBarVal = clamp<int>(
      (int)(((recorderConfig.radioFreq - radioBottomFreq) * 100.0f) /
              (radioTopFreq - radioBottomFreq) +
            0.5f),
      0,
      100);
}

void cSampleRecorder::calcLevelBarVal()
{
    AudioAnalyzeRMS* recordingRms[AudioInput::InputChannels::kChannelsCount];
    if (isUsbSource())
    {
        recordingRms[0] =
          engine.getAudioInput()->getRecordingRmsUsb(AudioInput::InputChannels::kLeft);
        recordingRms[1] =
          engine.getAudioInput()->getRecordingRmsUsb(AudioInput::InputChannels::kRight);
    }
    else
    {
        recordingRms[0] =
          engine.getAudioInput()->getRecordingRmsI2s(AudioInput::InputChannels::kLeft);
        recordingRms[1] =
          engine.getAudioInput()->getRecordingRmsI2s(AudioInput::InputChannels::kRight);
    }

    if (recordingRms[AudioInput::InputChannels::kLeft]->available() &&
        recordingRms[AudioInput::InputChannels::kRight]->available())
    {
        measureSum += (recordingRms[AudioInput::InputChannels::kLeft]->read() +
                       recordingRms[AudioInput::InputChannels::kRight]->read()) /
                      2.0f;
        levelBarMeasureCounter++;
    }
    if (levelBarMeasureCounter == 10)
    {
        levelBarMeasureCounter = 0;

        uint8_t localMeasureSum = measureSum / 0.0085f;

        if (localMeasureSum < 1)
            localMeasureSum = 1;

        uint8_t localLevel = logarithmicLevelTab[localMeasureSum - 1];
        /*
                bardziej rozbudowane obliczenia dla zrozumienia
                //float localMeasureSum = (measureSum/10)/0.85;
        //		if(localMeasureSum < 0.001f) localMeasureSum = 0.001f;
        //
        //		uint8_t localLevel = logarithmicLevelTab[ (uint8_t)(localMeasureSum *
        LOGHARITMIC_LEVEL_TAB_SIZE) - 1];
        */
        if (((levelBarTim > 500)) && (levelBarVal != 0))
        {
            levelBarTim = 0;
            levelBarVal--;
        }
        if (localLevel > levelBarVal)
            levelBarVal = localLevel;

        measureSum = 0;
    }
}
void cSampleRecorder::calcGainBarVal()
{
    if (isLineInSource())
    {
        gainBarVal = recorderConfig.gainLineIn;
    }
    else if (recorderConfig.source == sourceTypeRadio)
    {
        gainBarVal = recorderConfig.gainRadio;
    }
    else if (recorderConfig.source == sourceTypeMicLG)
    {
        gainBarVal = recorderConfig.gainMicLow;
    }
    else if (recorderConfig.source == sourceTypeMicHG)
    {
        gainBarVal = recorderConfig.gainMicHigh;
    }
    else if (isUsbSource())
    {
        gainBarVal = recorderConfig.gainUsb;
    }
}

void cSampleRecorder::changeRadioFreqBar(int16_t val)
{
    if (radio->isinitialized())
    {
        float radioBottomFreq = radio->getBaseFrequency();
        float radioSpacing = radio->getSpacing();
        if (radioSpacing <= 0.0f)
        {
            return;
        }

        int maxChannel = calcMaxRadioChannel(radioBottomFreq, radioSpacing);
        int currentChannel =
          calcRadioChannel(recorderConfig.radioFreq, radioBottomFreq, radioSpacing);
        int newChannel = clamp<int>(currentChannel + val, 0, maxChannel);
        float newFreq = radioBottomFreq + (newChannel * radioSpacing);

        if (newChannel != currentChannel || newFreq != recorderConfig.radioFreq)
        {
            if (radio->setFrequency(newFreq))
            {
                radio->clearRDS();
                SR->displayEmptyRDS();

                recorderConfig.radioFreq = newFreq;
                mtProject.values.radioFreq = newFreq;
                newFileManager.setProjectStructChanged();

                calcRadioFreqBarVal();
                drawRadioFreqBar();
            }
        }
    }
}
void cSampleRecorder::changeLevelBar()
{
    calcLevelBarVal();
    if (lastLevelBarVal != levelBarVal)
        drawLevelBar();

    lastLevelBarVal = levelBarVal;
}
void cSampleRecorder::changeGainBar(int16_t val)
{
    if (isLineInSource())
    {
        recorderConfig.gainLineIn = clamp<int>(recorderConfig.gainLineIn + val, 0, 100);
        refreshGain();
        mtProject.values.gainLineIn = sampleRecorder.recorderConfig.gainLineIn;
    }
    else if (recorderConfig.source == sourceTypeRadio)
    {
        recorderConfig.gainRadio = clamp<int>(recorderConfig.gainRadio + val, 0, 100);

        refreshGain();
        mtProject.values.gainRadio = sampleRecorder.recorderConfig.gainRadio;
    }
    else if (recorderConfig.source == sourceTypeMicLG)
    {
        recorderConfig.gainMicLow = clamp<int>(recorderConfig.gainMicLow + val, 0, 100);
        refreshGain();
        mtProject.values.gainMicLow = sampleRecorder.recorderConfig.gainMicLow;
    }
    else if (recorderConfig.source == sourceTypeMicHG)
    {
        recorderConfig.gainMicHigh = clamp<int>(recorderConfig.gainMicHigh + val, 0, 100);
        refreshGain();
        mtProject.values.gainMicHigh = sampleRecorder.recorderConfig.gainMicHigh;
    }
    else if (isUsbSource())
    {
        recorderConfig.gainUsb = clamp<int>(recorderConfig.gainUsb + val, 0, 100);
        refreshGain();
        mtProject.values.recorderGainUsb = sampleRecorder.recorderConfig.gainUsb;
    }

    calcGainBarVal();
    drawGainBar();
    showGain();

    newFileManager.setProjectStructChanged();
}

void cSampleRecorder::setDefaultSource()
{
    recorderConfig.source = sourceTypeLineInS;
    activateAudioInputSource(recorderConfig.source);

    display.setControlValue(sourceListControl, recorderConfig.source);
    display.refreshControl(sourceListControl);

    calcGainBarVal();
    drawGainBar();

    showSource();
    showGain();

    display.synchronizeRefresh();
    mtProject.values.source = sampleRecorder.recorderConfig.source;
    newFileManager.setProjectStructChanged();
}

void cSampleRecorder::setDefaultGain()
{
    if (isLineInSource())
    {
        recorderConfig.gainLineIn = defaultRecorderConfig.gainLineIn;
        refreshGain();
        mtProject.values.gainLineIn = sampleRecorder.recorderConfig.gainLineIn;
    }
    else if (recorderConfig.source == sourceTypeRadio)
    {
        recorderConfig.gainRadio = defaultRecorderConfig.gainRadio;
        refreshGain();
        mtProject.values.gainRadio = sampleRecorder.recorderConfig.gainRadio;
    }
    else if (recorderConfig.source == sourceTypeMicLG)
    {
        recorderConfig.gainMicLow = defaultRecorderConfig.gainMicLow;
        refreshGain();
        mtProject.values.gainMicLow = sampleRecorder.recorderConfig.gainMicLow;
    }
    else if (recorderConfig.source == sourceTypeMicHG)
    {
        recorderConfig.gainMicHigh = defaultRecorderConfig.gainMicHigh;
        refreshGain();
        mtProject.values.gainMicHigh = sampleRecorder.recorderConfig.gainMicHigh;
    }

    calcGainBarVal();
    drawGainBar();

    showGain();
    newFileManager.setProjectStructChanged();
}
void cSampleRecorder::setDefaultMonitorEnable()
{
    recorderConfig.monitor = defaultRecorderConfig.monitor;

    refreshMonitorState();

    display.setControlValue(monitorListControl, recorderConfig.monitor);
    display.refreshControl(monitorListControl);

    mtProject.values.monitor = sampleRecorder.recorderConfig.monitor;
    showMonitor();

    newFileManager.setProjectStructChanged();
}
void cSampleRecorder::setDefaultRadioFreq()
{
    if (radio->isinitialized())
    {
        recorderConfig.radioFreq = defaultRecorderConfig.radioFreq;

        calcRadioFreqBarVal();
        drawRadioFreqBar();

        radio->clearRDS();
        SR->displayEmptyRDS();

        radio->setFrequency(recorderConfig.radioFreq);
        mtProject.values.radioFreq = sampleRecorder.recorderConfig.radioFreq;
        newFileManager.setProjectStructChanged();
    }
}

void cSampleRecorder::setDefaultStartPoint()
{
    SR->startPoint = 0;

    // odswiez spektrum tylko jesli: zoom wiekszy niz 1, ostatnio modyfikowany inny punkt, punkt
    // jest poza widocznym obszarem
    if (SR->zoom.zoomValue > 1 &&
        (SR->zoom.lastChangedPoint != 1 ||
         (SR->startPoint < SR->zoom.zoomStart || SR->startPoint > SR->zoom.zoomEnd)))
    {
        SR->refreshSpectrum = 1;
    }

    SR->zoom.zoomPosition = SR->startPoint;
    SR->zoom.lastChangedPoint = 1;
    SR->refreshPoints = 1;

    SR->showStartPointValue();
}
void cSampleRecorder::setDefaultEndPoint()
{
    SR->endPoint = MAX_16BIT;

    if (SR->zoom.zoomValue > 1 &&
        (SR->zoom.lastChangedPoint != 2 ||
         (SR->endPoint < SR->zoom.zoomStart || SR->endPoint > SR->zoom.zoomEnd)))
    {
        SR->refreshSpectrum = 1;
    }

    SR->zoom.zoomPosition = SR->endPoint;
    SR->zoom.lastChangedPoint = 2;
    SR->refreshPoints = 1;

    SR->showEndPointValue();
}
void cSampleRecorder::setDefaultZoom()
{
    // max zmiana w lewo daje pewnosc ze bedzie to wartosc 1.0
    GP.spectrumChangeZoom(-MAX_SIGNED_16BIT, recorder.getLength(), &zoom);

    refreshSpectrum = 1;
    refreshPoints = 1;

    showZoomValue();
}

void cSampleRecorder::changeZoom(int16_t value)
{
    GP.spectrumChangeZoom(value, recorder.getLength(), &zoom);

    refreshSpectrum = 1;
    refreshPoints = 1;

    showZoomValue();
}

void cSampleRecorder::changeSourceSelection(int16_t value, bool rollListOver)
{
    if (recorderConfig.source + value < 0)
        recorderConfig.source = 0;
    else if (recorderConfig.source + value >= sourceCount)
        recorderConfig.source = (rollListOver ? 0 : sourceCount - 1);
    else
        recorderConfig.source += value;

    activateAudioInputSource(recorderConfig.source);

    display.setControlValue(sourceListControl, recorderConfig.source);
    display.refreshControl(sourceListControl);

    calcGainBarVal();
    drawGainBar();

    showSource();
    showGain();

    showMonitor();
    showMonitorList();

    display.synchronizeRefresh();

    mtProject.values.source = sampleRecorder.recorderConfig.source;
    newFileManager.setProjectStructChanged();
}

void cSampleRecorder::activateAudioInputSource(uint8_t source)
{
    if (currentScreen == screenTypeConfig && !isUsbSource())
    {
        refreshMonitorState();
    }

    switch (source)
    {
        case sourceTypeLineInL:
        case sourceTypeLineInR:
        case sourceTypeLineInM:
        case sourceTypeLineInS:
        {
#ifdef TRACKER_MINI
            // digitalWrite(SI4703_KLUCZ,HIGH);  // no more radio
            // old old //digitalWrite(MINI_MIC_SELECT,HIGH);
#else
            radio->enableLineInput(false);
#endif

            hideRadio();
            codec->setInputType(Sgtl5000::InputType::kLineIn);
            break;
        }
        case sourceTypeRadio:
        {
#ifdef TRACKER_MINI
            // digitalWrite(SI4703_KLUCZ,LOW);  // no more radio
            // old old //digitalWrite(MINI_MIC_SELECT,LOW);
#else
            radio->enableLineInput(true);
#endif

            showRadio();
            codec->setInputType(Sgtl5000::InputType::kLineIn);
            break;
        }
        case sourceTypeMicLG:
        case sourceTypeMicHG:
        {
#ifdef TRACKER_MINI
            // old old  digitalWrite(MINI_MIC_SELECT,LOW);
            // digitalWrite(SI4703_KLUCZ,LOW);   // no more radio
#else
            radio->enableLineInput(false);
#endif

            hideRadio();

            codec->setHeadphoneMuted(true);
            codec->setInputType(Sgtl5000::InputType::kMic);
            changeSourceToMicTimer = 0;
            changeSourceToMicFlag = 1;
            break;
        }
        case sourceTypeUsbMix:
        case sourceTypeUsbStereo: hideRadio(); break;
        default: break;
    }
    refreshOutputSource();

    refreshGain();
}

void cSampleRecorder::changeMonitorSelection(int16_t value, bool rollListOver)
{
    if (value > 0)
    {
        if (recorderConfig.monitor < 1)
            recorderConfig.monitor++;
        else if (rollListOver)
        {
            recorderConfig.monitor = 0;
        }
    }
    else if (value < 0)
    {
        if (recorderConfig.monitor > 0)
            recorderConfig.monitor--;
    }

    refreshMonitorState();

    display.setControlValue(monitorListControl, recorderConfig.monitor);
    display.refreshControl(monitorListControl);

    mtProject.values.monitor = sampleRecorder.recorderConfig.monitor;
    showMonitor();

    // fileManager.projectChangeFlag = 1;
    // mtProject.values.projectNotSavedFlag = 1;
    newFileManager.setProjectStructChanged();
}

static void modStartPoint(int16_t value)
{
    // obliczenie kroku przesuniecia w zaleznosci od ilosci widzianych probek na wyswietlaczu
    uint16_t move_step = SR->zoom.zoomWidth / 480;
    value = value * move_step;

    SR->startPoint = clamp<int>(SR->startPoint + value, 0, SR->endPoint);

    if (SR->startPoint == SR->endPoint)
    {
        if (SR->startPoint == 0)
        {
            SR->endPoint = 1;
        }
        else
        {
            SR->startPoint = SR->endPoint - 1;
        }
    }

    if (SR->zoom.zoomValue > 1 &&
        (SR->zoom.lastChangedPoint != 1 ||
         (SR->startPoint < SR->zoom.zoomStart || SR->startPoint > SR->zoom.zoomEnd)))
    {
        SR->refreshSpectrum = 1;
    }

    SR->zoom.zoomPosition = SR->startPoint;
    SR->zoom.lastChangedPoint = 1;
    SR->refreshPoints = 1;

    SR->showStartPointValue();
}

static void modEndPoint(int16_t value)
{
    uint16_t move_step = SR->zoom.zoomWidth / 480;
    value = value * move_step;

    SR->endPoint = clamp<int>(SR->endPoint + value, SR->startPoint, SAMPLE_POINT_POS_MAX);

    if (SR->startPoint == SR->endPoint)
    {
        if (SR->endPoint == SAMPLE_POINT_POS_MAX)
        {
            SR->startPoint = SAMPLE_POINT_POS_MAX - 1;
        }
        else
        {
            SR->endPoint = SR->startPoint + 1;
        }
    }

    if (SR->zoom.zoomValue > 1 &&
        (SR->zoom.lastChangedPoint != 2 ||
         (SR->endPoint < SR->zoom.zoomStart || SR->endPoint > SR->zoom.zoomEnd)))
    {
        SR->refreshSpectrum = 1;
    }

    SR->zoom.zoomPosition = SR->endPoint;
    SR->zoom.lastChangedPoint = 2;
    SR->refreshPoints = 1;

    SR->showEndPointValue();
}

void cSampleRecorder::calcPlayProgressValue()
{
    if (refreshPlayProgressValue > PLAY_REFRESH_US)
    {
        refreshPlayProgressValue = 0;

        playProgressValue = (uint16_t)((instrumentPlayer[0].getWavePosition() *
                                        ((endPoint - startPoint) / (float)MAX_16BIT)) +
                                       startPoint);

        if (zoom.zoomValue == 1.0)
            playProgressInSpectrum = (600 * playProgressValue) / MAX_16BIT;
        else if (zoom.zoomValue > 1.0)
        {
            if ((int32_t)playProgressValue < zoom.zoomStart ||
                (int32_t)playProgressValue > zoom.zoomEnd)
                playProgressInSpectrum = 0;
            else
            {
                playProgressInSpectrum =
                  mapClamped<int>(playProgressValue, zoom.zoomStart, zoom.zoomEnd, 0, 600);
            }
        }

        refreshSpectrumValue = 1;
    }
}

/*
static uint8_t stopPlaying(uint8_t value)
{
    if(SR->isPlayingSample) instrumentPlayer[0].noteOff();

    SR->isPlayingSample = 0;

    return 1;
}
*/

static void functInsert()
{
    if (SR->selectionWindowFlag)
    {
        return;
    }
    if (SR->patternIsPlayingFlag == 1)
    {
        return;
    }

    if (SR->currentScreen == cSampleRecorder::screenTypeKeyboard)
    {
        functConfirmKey();
    }
}

static void functPlay()
{
    if (sequencer.getSeqState() == Sequencer::SEQ_STATE_STOP)
    {
        if (SR->buttons->isButtonPressed(interfaceButtonShift))
        {
            sequencer.playSong();
        }
        else
        {
            sequencer.playPattern();
        }
    }
    else
    {
        sequencer.stop();
    }
}

static void functRec()
{
    if (SR->currentScreen == cSampleRecorder::screenTypeConfig ||
        (SR->currentScreen == cSampleRecorder::screenTypeRecord && SR->recordInProgressFlag == 1))
    {
        functActionButton7();
    }
}

static void onPreRollMetronomeEnd()
{
    SR->startRecordingOnPreRollEnd = true;
}

static void functInstrument(uint8_t state)
{
    if (state == buttonRelease)
    {
        if (mtPopups.getStepPopupState() != stepPopupNone)
        {
            mtPopups.hideStepPopups();

            SR->setDefaultScreenFunct();
            SR->showDefaultScreen();
            SR->recorderMidiLastUsedInstrumentIndex = mtProject.values.lastUsedInstrument;
        }
    }
    else if (state == buttonPress)
    {
        mtProject.values.lastUsedInstrument =
          clamp<uint8_t>(mtProject.values.lastUsedInstrument,
                         MIDI_INSTRUMENTS_START_IDX,
                         MIDI_INSTRUMENTS_START_IDX + kMidiInstrumentsCount - 1);

        mtPopups.showStepPopup(stepPopupOnlyMidiInstr, mtProject.values.lastUsedInstrument);
    }
}

static void functConfirmKey()
{
    SR->keyboardManager.confirmKey();
}

void seek_callback(void)
{
    SR->recorderConfig.radioFreq = SR->radio->getFrequency();

    mtProject.values.radioFreq = sampleRecorder.recorderConfig.radioFreq;
    SR->calcRadioFreqBarVal();
    SR->drawRadioFreqBar();

    SR->displayEmptyRDS();

    // fileManager.projectChangeFlag = 1;
    // mtProject.values.projectNotSavedFlag = 1;
    newFileManager.setProjectStructChanged();
}

/*/////////// MultiSelect Functions ////////////////*/
void cSampleRecorder::addNode(editFunct_t funct, uint8_t nodeNum)
{
    if (selectNodes[nodeNum].isActive == 0)
    {
        selectNodes[nodeNum].isActive = 1;
        selectNodes[nodeNum].editFunct = funct;
    }
}

void cSampleRecorder::removeNode(uint8_t nodeNum)
{
    selectNodes[nodeNum].isActive = 0;
    selectNodes[nodeNum].editFunct = NULL;
}

void cSampleRecorder::stepThroughNodes(int16_t value)
{
    // kolejnosc wykonywania funkcji w loop pointach ma znaczenie
    //
    if (value < 0)
    {
        for (uint8_t node = 0; node < MAX_SELECT_NODES; node++)
        {
            if (selectNodes[node].isActive)
            {
                if (selectNodes[node].editFunct != NULL)
                {
                    selectNodes[node].editFunct(value);
                }
            }
        }
    }
    else if (value > 0)
    {
        for (uint8_t node = MAX_SELECT_NODES; node > 0; node--)
        {
            if (selectNodes[node - 1].isActive)
            {
                if (selectNodes[node - 1].editFunct != NULL)
                {
                    selectNodes[node - 1].editFunct(value);
                }
            }
        }
    }
}

void cSampleRecorder::clearAllNodes()
{
    for (uint8_t node = 0; node < MAX_SELECT_NODES; node++)
    {
        selectNodes[node].isActive = 0;
        selectNodes[node].editFunct = NULL;
    }
}

void cSampleRecorder::cancelMultiFrame()
{
    for (uint8_t i = 0; i < MAX_SELECT_NODES; i++)
    {
        SR->frameData.multisel[i].isActive = 0;
    }

    SR->frameData.multiSelActiveNum = 0;
}

uint16_t cSampleRecorder::calculateStartPointFromRecording()
{
    static constexpr float attackMs = 1.0f;
    static constexpr float thresholdDB = -70.0f;
    static constexpr float timeAverageCoeff = 0.01f;

    uint32_t length = recorder.getLength();
    uint16_t filteredAbs[SAMPLE_CHANNELS] = {};

    uint16_t xpeak[SAMPLE_CHANNELS] = {};
    float attackCoeff = 1.0f - expf(-2.2f * (1000.0f / (44100.0f * attackMs)));
    uint16_t threshold = powf(10.0f, thresholdDB / 20.0f) * std::numeric_limits<int16_t>::max();

    int resultIdx = 0;

    for (int idx = 0; idx < (int)length; idx++)
    {
        bool resultIsFound = false;

        for (int channel = 0; channel < SAMPLE_CHANNELS; channel++)
        {
            int16_t* address = recorder.getAddress((AudioInput::InputChannels)channel);
            uint16_t inputAbs = (address[idx] < 0) ? -address[idx] : address[idx];

            filteredAbs[channel] =
              (1 - timeAverageCoeff) * filteredAbs[channel] + timeAverageCoeff * inputAbs;

            xpeak[channel] =
              (1 - attackCoeff) * xpeak[channel] + attackCoeff * filteredAbs[channel];

            resultIsFound = xpeak[channel] > threshold;

            if (resultIsFound)
            {
                break;
            }
        }

        if (resultIsFound)
        {
            resultIdx = idx;
            break;
        }
    }

    int attackSamplesShift = (44100.0f * attackMs) / 1000.0f;

    resultIdx = clamp<int>(resultIdx - attackSamplesShift, 0, length);

    return (uint16_t)((float)std::numeric_limits<uint16_t>::max() *
                      ((float)resultIdx / (float)(length - 1)));
}

uint16_t cSampleRecorder::calculateEndPointFromRecording()
{
    static constexpr float attackMs = 1.0f;
    static constexpr float thresholdDB = -70.0f;
    static constexpr float timeAverageCoeff = 0.01f;

    uint32_t length = recorder.getLength();
    uint16_t filteredAbs[SAMPLE_CHANNELS] = {};

    uint16_t xpeak[SAMPLE_CHANNELS] = {};
    float attackCoeff = 1.0f - expf(-2.2f * (1000.0f / (44100.0f * attackMs)));
    uint16_t threshold = powf(10.0f, thresholdDB / 20.0f) * std::numeric_limits<int16_t>::max();

    int resultIdx = length - 1;

    for (int idx = length - 1; idx >= 0; idx--)
    {
        bool resultIsFound = false;

        for (int channel = 0; channel < SAMPLE_CHANNELS; channel++)
        {
            int16_t* address = recorder.getAddress((AudioInput::InputChannels)channel);
            uint16_t inputAbs = (address[idx] < 0) ? -address[idx] : address[idx];

            filteredAbs[channel] =
              (1 - timeAverageCoeff) * filteredAbs[channel] + timeAverageCoeff * inputAbs;

            xpeak[channel] =
              (1 - attackCoeff) * xpeak[channel] + attackCoeff * filteredAbs[channel];

            resultIsFound = xpeak[channel] > threshold;

            if (resultIsFound)
            {
                break;
            }
        }

        if (resultIsFound)
        {
            resultIdx = idx;
            break;
        }
    }

    int attackSamplesShift = (44100.0f * attackMs) / 1000.0f;

    resultIdx = clamp<int>(resultIdx + attackSamplesShift, 0, length - 1);

    return (uint16_t)((float)std::numeric_limits<uint16_t>::max() *
                      ((float)resultIdx / (float)(length - 1)));
}

///////////////////////////////////////////////////////////////////////////

// void turnOffRecorder()
//{
//
//
//	audioShield.headphoneSourceSelect(0);
//	radio.clearRDS();
//	radio.resetSeekCallback();
//	engine.setHeadphonesVolume(mtConfig.audioCodecConfig.volume);
//}

void cSampleRecorder::AvoidPopOnStart::blockSoundOnStart()
{
    isStart = true;
    timer = 0;
    SR->codec->setMasterVolume(0.0f);
}

void cSampleRecorder::AvoidPopOnStart::tryUnblockSound()
{
    constexpr uint16_t AVOID_POP_SILENT_TIME_MS = 500;

    bool popOccured = (isStart && (timer > AVOID_POP_SILENT_TIME_MS));
    if (popOccured)
    {
        isStart = false;
        unblockSound();
    }
}
void cSampleRecorder::AvoidPopOnStart::tryUnblockSoundOnExit()
{
    if (isStart)
    {
        isStart = false;
        unblockSound();
    }
}

void cSampleRecorder::AvoidPopOnStart::unblockSound()
{
    engine.setHeadphonesVolume(mtConfig.audioCodecConfig.volume);
}

static void functMenu(uint8_t state)
{

    if (SR->isModuleSwitchingBlocked())
    {
        return;
    }

    if (state == buttonPress)
    {
        mtMenuPopup.showModulePopup(SR, functSwitchModule);
    }
    else if (state == buttonRelease && mtMenuPopup.isPopupVisible())
    {
        // There's a need to keep order of instructions, the flag  is used deep inside
        // popup callback - it would be better to pass it as a context to the show function but
        // it would complicate generic handling of module switching.
        SR->isSwitchingFromMenu = true;
        mtMenuPopup.hideModulePopup(SR);
    }
}

void cSampleRecorder::calculatePointsAfterRecording()
{
    endPoint = calculateEndPointFromRecording();
    endPoint = endPoint > 0 ? endPoint : 1;

    startPoint = clamp<uint16_t>(calculateStartPointFromRecording(), 0, endPoint - 1);
}

bool cSampleRecorder::isModuleSwitchingBlocked()
{
    if (selectionWindowFlag == 1)
    {
        return true;
    }
    if (recordInProgressFlag)
    {
        return true;
    }
    if (notEnoughInstrumentsFlag)
    {
        return true;
    }
    if (notEnoughMemoryFlag)
    {
        return true;
    }
    if (currentScreen == cSampleRecorder::screenTypeKeyboard)
    {
        return true;
    }

    return false;
}
