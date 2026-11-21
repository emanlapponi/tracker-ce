#include "core/interfaceMenuPopup.h"
#include "core/interfacePopups.h"
#include "instrumentEditor/instrumentEditor.h"
#include "mtAudioEngine.h"
#include "mtPadBoard.h"
#include "mtPadsBacklight.h"
#include "samplePlayback/samplePlayback.h"

#include "Platform.h"
#include "core/graphicProcessing.h"
#include "core/interface.h"
#include "fileManager.h"
#include "mtConfig.h"
#include "mtSliceManager.h"
#include "VoiceStreamManager.h"
#include <cmath>

#include <cr_section_macros.h>

__NOINIT(EXTERNAL_RAM) strTrackerSpectrum cSamplePlayback::spectrum[2];

cSamplePlayback samplePlayback;
static cSamplePlayback* SP = &samplePlayback;

extern strMtProject mtProject;

static void functPlayAction();
static void functRecAction();

static void functInstrument(uint8_t state);

static void functInsert();
static void functDelete();
static void functShift(uint8_t state);

static void functLeft();
static void functRight();
static void functUp();
static void functDown();

static void functMiniMiddle(uint8_t state);

static void functButton0(uint8_t state);
static void functButton1(uint8_t state);
static void functButton2(uint8_t state);
static void functButton3(uint8_t state);
static void functButton4(uint8_t state);
static void functButton5();
static void functPlayMode(uint8_t button);

static void functAddSlice();
static void functRemoveSlice();

static void functAutoSlice();
static void functEqualSlice();

static void functAutoSlicConfirmOverwrite();
static void functCancelOverwrite();

static void functPads(uint8_t pad, uint8_t state, int16_t velo);

static void functMenu(uint8_t state);

static void functEncoder(int16_t value);

static void functSwitchModule(uint8_t button);
static void functStepNote(uint8_t value);

static void modSliceSelect(int16_t value, bool set = false);
static void modSliceAdjust(int16_t value);

static void modWavetablePostion(int32_t value);
static void modWavetableWindowSize(int16_t value);
static void modStartPoint(int16_t value);
static void modEndPoint(int16_t value);
static void modLoopPoint1(int16_t value);
static void modLoopPoint2(int16_t value);
static void changeZoom(int16_t value);
static void changePlayModeSelection(int16_t value);

static void modGranularLength(int16_t value);
static void modGranularShape(int16_t value);
static void modGranularPosition(int16_t value);
static void modGranularLoopType(int16_t value);

constexpr uint32_t PLAY_REFRESH_US = 5000;

void cSamplePlayback::update()
{
    refreshLiveParams();

    // A MIDI or synth instrument has no waveform; the request waits for a sample instrument.
    if (refreshSpectrum && mtProject.values.lastUsedInstrument < kSampleInstrumentsCount)
    {
        strSampleMemory* editorSampleMemory =
          &mtProject.sampleMemory[mtProject.values.lastUsedInstrument];

        GP.processSpectrum(editorSampleMemory,
                           editorInstrument,
                           &zoom,
                           &spectrum[strSampleMemory::Channel::kLeft],
                           strSampleMemory::Channel::kLeft,
                           currentEnvelopeWtPos);

        if (newFileManager.isInstrumentStereo(mtProject.values.lastUsedInstrument))
        {
            GP.processSpectrum(editorSampleMemory,
                               editorInstrument,
                               &zoom,
                               &spectrum[strSampleMemory::Channel::kRight],
                               strSampleMemory::Channel::kRight,
                               currentEnvelopeWtPos);
        }

        display.refreshControl(spectrumControl[0]);
        display.refreshControl(spectrumControl[1]);

        if (refreshWavetablePosition)
        {
            if (editorInstrument->playMode == playModeWavetable)
            {
                showWavetablePosition();
            }
            refreshWavetablePosition = 0;
        }
        refreshSpectrum = 0;
    }

    if (refreshPoints)
    {
        processPoints();
        display.refreshControl(pointsControl);
        refreshPoints = 0;
    }

    currentAutoSlice = sliceManager.getAutoSliceState();
    if ((currentAutoSlice == 0) && (currentAutoSlice != lastAutoSlice))
    {
        if (mtProject.values.lastUsedInstrument < kSampleInstrumentsCount &&
            mtProject.sampleMemory[mtProject.values.lastUsedInstrument].isStreamed)
        {
            voiceStreamManager.refreshInstrumentSliceCache(mtProject.values.lastUsedInstrument);
        }
        // The selection could still be moved among the old slices while detection ran.
        if (editorInstrument->selectedSlice >= editorInstrument->sliceNumber)
        {
            editorInstrument->selectedSlice =
              (editorInstrument->sliceNumber > 0) ? editorInstrument->sliceNumber - 1 : 0;
        }
        refreshSlicePoints = 1;
        if (!sliceScreenMode2)
        {
            showSlicesSelectValue();
        }
        lightUpPadBoard();
        newFileManager.setInstrumentStructChanged(mtProject.values.lastUsedInstrument);
    }
    lastAutoSlice = currentAutoSlice;

    if (refreshSlicePoints)
    {
        if ((editorInstrument->playMode == playModeSlice) ||
            (editorInstrument->playMode == playModeBeatSlice))
        {
            processSlicePoints();
        }

        display.refreshControl(slicePointsControl);
        refreshSlicePoints = 0;
    }

    if (refreshSpectrumProgress)
    {
        display.setControlValue(progressCursor, playProgressInSpectrum);
        display.refreshControl(progressCursor);
        refreshSpectrumProgress = 0;
    }
}

void cSamplePlayback::start(uint32_t options)
{
    (void)options;
    moduleRefresh = 1;

    loadedInstrumentType = mtSampleTypeWaveFile;

    if (mtProject.values.lastUsedInstrument >= kSampleInstrumentsCount ||
        mtProject.values.lastUsedInstrument != displayedInstrument)
    {
        clearDisplayedTrack();
    }

    //--------------------------------------------------------------------
    if (mtProject.values.lastUsedInstrument < kSampleInstrumentsCount)
    {
        if (editorInstrument == nullptr ||
            editorInstrument->playMode !=
              mtProject.instrument[mtProject.values.lastUsedInstrument].playMode)
        {
            sliceScreenMode2 = false;
            SP->cancelMultiFrame();
            SP->clearAllNodes();
            SP->selectedPlace = 6;
            SP->points.selected = 0;
        }

        editorInstrument = &mtProject.instrument[mtProject.values.lastUsedInstrument];
        loadedInstrumentType = editorInstrument->sample.type;

        if (loadedInstrumentType == mtSampleTypeWaveFile)
        {
            // warunki
            newFileManager.checkLoopParameters(editorInstrument);
            /////////////////////////////////////////////////////////////////////

            // wykrywanie czy wczytywany inny niz poprzednio/nowy sampel
            //--------------------------------------------------------------------

            points.selected = (selectedPlace >= 1 && selectedPlace <= 4) ? selectedPlace : 0;

            slicePoints.selected = 0;

            currentEnvelopeGranPos = editorInstrument->granular.currentPosition;
            currentEnvelopeGranLen = editorInstrument->granular.grainLength;
        }

        if (loadedInstrumentType == mtSampleTypeWavetable)
        {
            if (selectedPlace == 0)
                selectedPlace = 1;
            else if ((selectedPlace == 3) || (selectedPlace == 4))
                selectedPlace = 2;
            else if (selectedPlace == 5)
                selectedPlace = 6;
            wavetableWindowsCounter = convertWavetableWindowsSizeToCounter(
              mtProject.instrument[mtProject.values.lastUsedInstrument]
                .sample.wavetable_window_size);

            processWavetableCursor(SP->editorInstrument->wavetableCurrentWindow);
        }

        // wykrywanie czy wczytywany inny niz poprzednio/nowy sampel
        // patrzy na dlugosc sampla
        if (editorInstrument->sample.length != lastSampleLength)
        {
            lastSampleLength = editorInstrument->sample.length;

            uint16_t start_end_width = editorInstrument->endPoint - editorInstrument->startPoint;

            if (start_end_width <
                MAX_16BIT / 4) // wysrodkowanie startPoint z zoomem jesli waski loop
            {
                start_end_width += start_end_width / 20; // dodatkowe po 10% po bokach

                GP.spectrumAutoZoomIn(editorInstrument->startPoint,
                                      start_end_width,
                                      editorInstrument->sample.length,
                                      &zoom);
            }
            else
            {
                GP.spectrumResetZoom(
                  editorInstrument->startPoint, editorInstrument->sample.length, &zoom);
            }
        }

        if ((SP->editorInstrument->playMode == playModeSlice) ||
            (SP->editorInstrument->playMode == playModeBeatSlice))
        {
            SP->zoom.zoomPosition =
              (SP->editorInstrument->sliceNumber > 0)
                ? SP->editorInstrument->slices[SP->editorInstrument->selectedSlice]
                : 0;
            processSlicePoints();
        }
        //--------------------------------------------------------------------
        strSampleMemory* editorSampleMemory =
          &mtProject.sampleMemory[mtProject.values.lastUsedInstrument];
        GP.processSpectrum(editorSampleMemory,
                           editorInstrument,
                           &zoom,
                           &spectrum[strSampleMemory::Channel::kLeft],
                           strSampleMemory::Channel::kLeft);
        GP.processSpectrum(editorSampleMemory,
                           editorInstrument,
                           &zoom,
                           &spectrum[strSampleMemory::Channel::kRight],
                           strSampleMemory::Channel::kRight);
        processPoints();
    }

    if (loadedInstrumentType == mtSampleTypeWaveFile)
    {
        selectCorrectPoints();
        clearAllNodes();
        cancelMultiFrame();
    }

    // ustawienie funkcji
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

    ip->setButtonObj(interfaceButtonMiniMenu, functMenu);

    if (mtProject.values.lastUsedInstrument >= kSampleInstrumentsCount)
    {
        ip->setButtonObj(interfaceButtonInstr, functInstrument);
        display.hideAllControls();
        showTitleBar();
        display.synchronizeRefresh();
        return;
    }

    listPlayMode();

    showDefaultScreen();
    setDefaultScreenFunct();

    activateLabelsBorder();

    sequencer.setMidiInVoiceMode(Sequencer::midiInVoiceMode_SamplePlayback);

    lightUpPadBoard();
}

void cSamplePlayback::stop()
{
    mtPopups.hideInfoPopup();

    if (mtPopups.isNoteKeyboardPopupActive())
    {
        mtPopups.toggleNoteKeyboardPopup();
    }

    clearDisplayedTrack();
    sequencer.stopManualNotes();
    sequencer.setMidiInVoiceMode(Sequencer::midiInVoiceMode_default);
    moduleRefresh = 0;
    //	mtPadBoard.releaseAllInstruments();
}

void cSamplePlayback::setDefaultScreenFunct()
{
    ip->unblockAllInputs();
    // funkcje
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearAllPots();

    ip->setButtonObj(interfaceButtonPlay, buttonPress, functPlayAction);
    ip->setButtonObj(interfaceButtonRec, buttonPress, functRecAction);
    ip->setButtonObj(interfaceButtonShift, functShift);

    ip->setButtonObj(interfaceButtonLeft, buttonPress, functLeft);
    ip->setButtonObj(interfaceButtonRight, buttonPress, functRight);
    ip->setButtonObj(interfaceButtonUp, buttonPress, functUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functDown);

    ip->setButtonObj(interfaceButtonMiniMiddle, functMiniMiddle);

    ip->setButtonObj(interfaceButton0, functButton0);
    ip->setButtonObj(interfaceButton1, functButton1);
    ip->setButtonObj(interfaceButton2, functButton2);
    ip->setButtonObj(interfaceButton3, functButton3);
    ip->setButtonObj(interfaceButton4, functButton4);
    ip->setButtonObj(interfaceButton5, buttonPress, functButton5);

    ip->setButtonObj(interfaceButton6, buttonPress, functPlayMode);
    ip->setButtonObj(interfaceButton7, buttonPress, functPlayMode);

    ip->setButtonObj(interfaceButtonInstr, functInstrument);
    ip->setButtonObj(interfaceButtonNote, functStepNote);

    ip->setButtonObj(interfaceButtonInsert, buttonPress, functInsert);
    ip->setButtonObj(interfaceButtonDelete, buttonPress, functDelete);

    ip->setPotObj(interfacePot0, functEncoder);

    ip->setPadsGlobal(functPads);
}

void cSamplePlayback::setAutoSlicePopupFunct()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearAllPots();

    ip->blockAllInputsExcept(interfaceButton6, interfaceButton7);

    ip->setButtonObj(interfaceButton6, buttonPress, functCancelOverwrite);
    ip->setButtonObj(interfaceButton7, buttonPress, functAutoSlicConfirmOverwrite);
}

//==============================================================================================================

void cSamplePlayback::processPoints()
{
    points.pointsType = editorInstrument->playMode;

    if (editorInstrument->startPoint >= zoom.zoomStart &&
        editorInstrument->startPoint <= zoom.zoomEnd)
    {
        points.startPoint =
          ((editorInstrument->startPoint - zoom.zoomStart) * 599) / zoom.zoomWidth;
    }
    else
        points.startPoint = -1;

    if (editorInstrument->endPoint >= zoom.zoomStart && editorInstrument->endPoint <= zoom.zoomEnd)
    {
        points.endPoint = ((editorInstrument->endPoint - zoom.zoomStart) * 599) / zoom.zoomWidth;
    }
    else
        points.endPoint = -1;

    if (editorInstrument->loopPoint1 >= zoom.zoomStart &&
        editorInstrument->loopPoint1 <= zoom.zoomEnd)
    {
        points.loopPoint1 =
          ((editorInstrument->loopPoint1 - zoom.zoomStart) * 599) / zoom.zoomWidth;
    }
    else
        points.loopPoint1 = -1;

    if (editorInstrument->loopPoint2 >= zoom.zoomStart &&
        editorInstrument->loopPoint2 <= zoom.zoomEnd)
    {
        points.loopPoint2 =
          ((editorInstrument->loopPoint2 - zoom.zoomStart) * 599) / zoom.zoomWidth;
    }
    else
        points.loopPoint2 = -1;
}

void cSamplePlayback::processSlicePoints()
{
    slicePoints.sliceCount = editorInstrument->sliceNumber;
    slicePoints.selected = editorInstrument->selectedSlice;

    for (uint8_t i = 0; i < slicePoints.sliceCount; i++)
    {
        if (editorInstrument->slices[i] >= zoom.zoomStart &&
            editorInstrument->slices[i] <= zoom.zoomEnd)
        {
            slicePoints.slice[i] =
              ((editorInstrument->slices[i] - zoom.zoomStart) * 599) / zoom.zoomWidth;
            slicePoints.isSliceZerocrossed[i] =
              playerEngine::isZeroInPointZone(editorInstrument->slices[i],
                                              editorInstrument->sample.length,
                                              editorInstrument->sample.address);
        }
        else
        {
            slicePoints.slice[i] = -1;
        }
    }
}

void cSamplePlayback::selectCorrectPoints()
{
    if (selectedPlace == 1)
    {
        points.selected = selectStart;
    }
    else if (selectedPlace == 2)
    {
        points.selected = selectLoop1;
    }
    else if (selectedPlace == 3)
    {
        points.selected = selectLoop2;
    }
    else if (selectedPlace == 4)
    {
        points.selected = selectEnd;
    }
}

void cSamplePlayback::refreshLiveParams()
{
    bool isAnyTrackToShow = displayedTrack != kNoTrack;

    if (!isAnyTrackToShow)
    {
        return;
    }

    if (mtProject.values.lastUsedInstrument >= kSampleInstrumentsCount ||
        mtProject.values.lastUsedInstrument != displayedInstrument)
    {
        clearDisplayedTrack();
        if (mtPopups.getStepPopupState() == stepPopupNone)
        {
            refreshSpectrumProgress = 1;
        }
        return;
    }

    calcPlayProgressValue();

    refreshGranularAutomation();
    refreshWavetableAutomation();

    refreshPreviewTimeLabel();

    if (instrumentPlayer[displayedTrack].getEndDisplayTrackFlag())
    {
        instrumentPlayer[displayedTrack].clearEndDisplayTrackFlag();
        onEndTrackDisplay();
    }
}

void cSamplePlayback::refreshGranularAutomation()
{
    if (editorInstrument->playMode == playModeGranular)
    {
        currentEnvelopeGranPos = instrumentPlayer[displayedTrack].getEnvelopeGranPosMod();
        currentEnvelopeGranLen = instrumentPlayer[displayedTrack].getEnvelopeGranLenMod();
        if (currentEnvelopeGranPos != lastEnvelopeGranPos ||
            currentEnvelopeGranLen != lastEnvelopeGranLen)
        {
            showGranularPositionValue();
        }
        lastEnvelopeGranPos = currentEnvelopeGranPos;
        lastEnvelopeGranLen = currentEnvelopeGranLen;
    }
}

void cSamplePlayback::refreshWavetableAutomation()
{
    if (editorInstrument->playMode == playModeWavetable)
    {
        currentEnvelopeWtPos = instrumentPlayer[displayedTrack].getEnvelopeWtPosMod();

        if (currentEnvelopeWtPos != lastEnvelopeWtPos)
        {
            refreshSpectrum = 1;
            refreshWavetablePosition = 1;

            processWavetableCursor(currentEnvelopeWtPos);
        }

        lastEnvelopeWtPos = currentEnvelopeWtPos;
    }
}

void cSamplePlayback::refreshPreviewTimeLabel()
{
    bool isPreviewTimeDisplayed = editorInstrument->playMode != playModeWavetable &&
                                  editorInstrument->playMode != playModeSlice &&
                                  editorInstrument->playMode != playModeBeatSlice;

    if (isPreviewTimeDisplayed)
    {
        showPreviewValue();
    }
}

void cSamplePlayback::listPlayMode()
{
    for (uint8_t i = 0; i < playModeCount; i++)
    {
        playModeNames[i] = (char*)&playModeFunctLabels[i][0];
    }
}

void cSamplePlayback::cancelPopups()
{
    if (mtPopups.getStepPopupState() != stepPopupNone)
    {
        mtPopups.hideStepPopups();

        start(0);
    }
}

void cSamplePlayback::clearDisplayedTrack()
{
    if (displayedTrack != kNoTrack)
    {
        instrumentPlayer[displayedTrack].clearTrackIsDisplayed();
        instrumentPlayer[displayedTrack].clearEndDisplayTrackFlag();
        displayedTrack = kNoTrack;
    }

    displayedInstrument = kNoInstrument;
    playProgressValue = 0;
    playProgressInSpectrum = 0;
    refreshSpectrumProgress = 0;

    if (progressCursor >= 0)
    {
        display.setControlValue(progressCursor, 0);
    }
}

void cSamplePlayback::onEndTrackDisplay()
{
    instrumentPlayer[displayedTrack].clearTrackIsDisplayed();

    if (editorInstrument->playMode == playModeWavetable)
    {
        currentEnvelopeWtPos = editorInstrument->wavetableCurrentWindow;
        showWavetablePosition();
        processWavetableCursor(currentEnvelopeWtPos);
    }
    else
    {
        playProgressValue = 0;
        playProgressInSpectrum = 0;
        refreshSpectrumProgress = 1;

        if ((SP->editorInstrument->playMode != playModeSlice) &&
            (SP->editorInstrument->playMode != playModeBeatSlice))
        {
            hidePreviewValue();
        }

        if (editorInstrument->playMode == playModeGranular)
        {
            currentEnvelopeGranPos = editorInstrument->granular.currentPosition;
            currentEnvelopeGranLen = editorInstrument->granular.grainLength;
            showGranularPositionValue();
        }
    }

    displayedTrack = kNoTrack;
    displayedInstrument = kNoInstrument;
}

void cSamplePlayback::noteOnHandle(uint8_t channel, uint8_t note, uint8_t velocity, int16_t source)
{
    sequencer.handleNoteOn(channel, note, velocity, source);

    if (mtProject.values.lastUsedInstrument >= kSampleInstrumentsCount)
    {
        return;
    }

    if (displayedTrack != kNoTrack && displayedInstrument != mtProject.values.lastUsedInstrument)
    {
        clearDisplayedTrack();
    }

    if (displayedTrack == engine.getLastUsedVoice())
    {
        instrumentPlayer[displayedTrack].setTrackIsDisplayed();
        instrumentPlayer[displayedTrack].clearEndDisplayTrackFlag();
        displayedInstrument = mtProject.values.lastUsedInstrument;
        // akcja ustawia sie w audio engine - sequencer jej nie clearuje a tu tak - wiec seq gasi
        // progres a druga nuta na tym samym vocie z pada/midi nie
    }
    else if (displayedTrack == kNoTrack)
    {
        displayedTrack = engine.getLastUsedVoice();
        instrumentPlayer[displayedTrack].setTrackIsDisplayed();
        instrumentPlayer[displayedTrack].clearEndDisplayTrackFlag();
        displayedInstrument = mtProject.values.lastUsedInstrument;
    }
}

void cSamplePlayback::noteOffHandle(uint8_t channel, uint8_t note, uint8_t velocity, int16_t source)
{
    (void)velocity;
    sequencer.handleNoteOff(channel, note, 0, source);
}

void cSamplePlayback::lightUpPadBoard()
{
    padsBacklight.clearGlobalPads(0, 1, 1);

    if (mtProject.values.lastUsedInstrument >= kSampleInstrumentsCount)
    {
        return;
    }

    bool isBeatSlice =
      mtProject.instrument[mtProject.values.lastUsedInstrument].playMode == playModeBeatSlice;

    if (isBeatSlice && mtProject.instrument[mtProject.values.lastUsedInstrument].sliceNumber > 0)
    {
        for (uint8_t pad = 0;
             pad < mtProject.instrument[mtProject.values.lastUsedInstrument].sliceNumber;
             pad++)
        {
            padsBacklight.setBackLayer(1, mtConfig.values.padsLightBack, pad);
        }
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

    if (mtProject.values.lastUsedInstrument < kSampleInstrumentsCount &&
        mtProject.instrument[mtProject.values.lastUsedInstrument].playMode == playModeBeatSlice)
    {
        int note = mtProject.instrument[mtProject.values.lastUsedInstrument].selectedSlice +
                   FIRST_BEAT_SLICE_NOTE;

        if (state == buttonPress)
        {
            SP->noteOnHandle(Sequencer::GRID_OUTSIDE_PATTERN, note, 127, 12);
        }
        else if (state == buttonRelease)
        {
            SP->noteOffHandle(Sequencer::GRID_OUTSIDE_PATTERN, note, 0, 12);
        }
    }
    else
    {
        functPads(12, state, 0);
    }
}

//==============================================================================================================
static void functPads(uint8_t pad, uint8_t state, int16_t velo)
{
    (void)velo;

    bool isBeatSlice =
      mtProject.values.lastUsedInstrument < kSampleInstrumentsCount &&
      mtProject.instrument[mtProject.values.lastUsedInstrument].playMode == playModeBeatSlice;

    if (state == buttonPress)
    {
        padsBacklight.setFrontLayer(1, mtConfig.values.padsLightFront, pad);

        uint8_t noteFromPad = mtPadBoard.getNoteFromPad(pad, isBeatSlice);

        SP->noteOnHandle(Sequencer::GRID_OUTSIDE_PATTERN, noteFromPad, 127, pad);

        if (isBeatSlice)
        {
#ifndef TRACKER_MINI
            modSliceSelect(pad, true);
#endif
        }
    }
    else if (state == buttonRelease)
    {
        padsBacklight.setFrontLayer(0, 0, pad);

        uint8_t noteFromPad = mtPadBoard.getNoteFromPad(pad, isBeatSlice);

        SP->noteOffHandle(Sequencer::GRID_OUTSIDE_PATTERN, noteFromPad, 0, pad);
    }
}

static void functButton0(uint8_t state)
{
    if ((SP->editorInstrument->playMode == playModeBeatSlice) ||
        (SP->editorInstrument->playMode == playModeSlice))
    {
        if (state == buttonPress)
        {
            if (SP->sliceScreenMode2)
            {
                functAutoSlice();
            }
            else
            {
                SP->selectedPlace = 0;
                SP->activateLabelsBorder();
            }
        }
        return;
    }

    if (state == buttonPress)
    {
        SP->noteOnHandle(Sequencer::GRID_OUTSIDE_PATTERN, 60, 127, INTERFACE_BUTTON_PREVIEW);
    }
    else if (state == buttonRelease)
    {
        SP->noteOffHandle(Sequencer::GRID_OUTSIDE_PATTERN, 60, 0, INTERFACE_BUTTON_PREVIEW);
    }
}

static void functButton1(uint8_t state)
{
    if ((state > buttonPress) && (state != UINT8_MAX))
    {
        return;
    }

    if ((SP->editorInstrument->playMode == playModeBeatSlice) ||
        (SP->editorInstrument->playMode == playModeSlice))
    {
        if (state == buttonPress)
        {
            if (SP->sliceScreenMode2)
            {
                functEqualSlice();
                SP->refreshSlicePoints = 1;
                return;
            }
            else
            {
                SP->selectedPlace = 1;
            }
        }
    }
    else if (SP->editorInstrument->playMode == playModeGranular ||
             SP->editorInstrument->playMode == playModeWavetable)
    {
        if (state == buttonPress || state == UINT8_MAX)
        {
            SP->selectedPlace = 1;
        }
    }
    else
    {
        if (state == UINT8_MAX || state == buttonPress) // called from inside of this module
        {
            if (SP->zoom.zoomValue > 1.0 && SP->zoom.lastChangedPoint != 1)
            {
                SP->zoom.lastChangedPoint = 1;
                SP->zoom.zoomPosition = SP->editorInstrument->startPoint;
                SP->refreshSpectrum = 1;
            }

            if (state == buttonPress)
            {
                SP->addNode(modStartPoint, 0);

                SP->frameData.multisel[1].frameNum = 1;
                SP->frameData.multisel[1].isActive = 1;
                SP->frameData.multiSelActiveNum += 1;
            }

            if (SP->frameData.multiSelActiveNum < 2)
            {
                SP->points.selected = 0;
            }

            SP->points.selected |= selectStart;
            SP->selectedPlace = 1;
        }
        else if (state == buttonRelease)
        {
            if (SP->frameData.multiSelActiveNum)
            {
                SP->points.selected &= ~selectStart;

                if (SP->frameData.multisel[1].isActive)
                {
                    SP->removeNode(0);
                    SP->frameData.multiSelActiveNum -= 1;

                    SP->frameData.multisel[1].isActive = 0;

                    if (SP->frameData.multiSelActiveNum == 0)
                    {
                        SP->selectedPlace = 1;
                        SP->points.selected |= selectStart;
                    }
                }
            }
        }

        SP->refreshPoints = 1;
    }

    SP->activateLabelsBorder();
}

static void functButton2(uint8_t state)
{
    if ((state > buttonPress) && (state != UINT8_MAX))
    {
        return;
    }

    if (SP->editorInstrument->playMode == playModeBeatSlice ||
        SP->editorInstrument->playMode == playModeSlice)
    {
        if (state == buttonPress || state == UINT8_MAX)
        {
            if (SP->sliceScreenMode2)
            {
                SP->selectedPlace = 2;
            }
            else
            {
                functAddSlice();
                return;
            }
        }
    }
    else if (SP->editorInstrument->playMode == playModeWavetable ||
             SP->editorInstrument->playMode == playModeGranular)
    {
        if (state == buttonPress || state == UINT8_MAX)
        {
            SP->selectedPlace = 2;
        }
    }
    else if (SP->editorInstrument->playMode == playModeSingleShot)
    {
        return;
    }
    else
    {
        if (state == UINT8_MAX || state == buttonPress)
        {
            if (SP->zoom.zoomValue > 1.0 && SP->zoom.lastChangedPoint != 3)
            {
                SP->zoom.lastChangedPoint = 3;
                SP->zoom.zoomPosition = SP->editorInstrument->loopPoint1;
                SP->refreshSpectrum = 1;
            }

            if (state == buttonPress)
            {
                SP->addNode(modLoopPoint1, 1);

                SP->frameData.multisel[2].frameNum = 2;
                SP->frameData.multisel[2].isActive = 1;
                SP->frameData.multiSelActiveNum += 1;
            }

            if (SP->frameData.multiSelActiveNum < 2)
            {
                SP->points.selected = 0;
            }

            SP->points.selected |= selectLoop1;
            SP->selectedPlace = 2;
        }
        else if (state == buttonRelease)
        {
            if ((SP->editorInstrument->playMode == playModeSlice) ||
                (SP->editorInstrument->playMode == playModeBeatSlice))
            {
                return;
            }
            if (SP->frameData.multiSelActiveNum)
            {
                SP->points.selected &= ~selectLoop1;

                if (SP->frameData.multisel[2].isActive)
                {
                    SP->removeNode(1);
                    SP->frameData.multiSelActiveNum -= 1;

                    SP->frameData.multisel[2].isActive = 0;

                    if (SP->frameData.multiSelActiveNum == 0)
                    {
                        SP->selectedPlace = 2;
                        SP->points.selected |= selectLoop1;
                    }
                }
            }
        }
    }

    SP->refreshPoints = 1;
    SP->activateLabelsBorder();
}

static void functButton3(uint8_t state)
{
    if ((state > buttonPress) && (state != UINT8_MAX))
    {
        return;
    }

    if (SP->editorInstrument->playMode == playModeBeatSlice ||
        SP->editorInstrument->playMode == playModeSlice)
    {
        if (SP->sliceScreenMode2)
        {
            if (state == buttonPress || state == UINT8_MAX)
            {
                if (state == buttonPress && SP->selectedPlace == 3)
                {
                    SP->modSlicePlayThru(SP->editorInstrument->slicePlayThru ? -1 : 1);
                }
                SP->selectedPlace = 3;
                SP->activateLabelsBorder();
            }
            return;
        }
        if (state == buttonPress)
        {
            functRemoveSlice();
        }
        return;
    }
    else if (SP->editorInstrument->playMode == playModeGranular)
    {
        if (state == buttonPress || state == UINT8_MAX)
        {
            SP->selectedPlace = 3;
        }
    }
    else if (SP->editorInstrument->playMode == playModeWavetable)
    {
        return;
    }
    else if (SP->editorInstrument->playMode == playModeSingleShot)
    {
        return;
    }
    else
    {
        if (state == UINT8_MAX || state == buttonPress)
        {
            if (SP->zoom.zoomValue > 1.0 && SP->zoom.lastChangedPoint != 4)
            {
                SP->zoom.lastChangedPoint = 4;
                SP->zoom.zoomPosition = SP->editorInstrument->loopPoint2;
                SP->refreshSpectrum = 1;
            }

            if (state == buttonPress)
            {
                SP->addNode(modLoopPoint2, 2);

                SP->frameData.multisel[3].frameNum = 3;
                SP->frameData.multisel[3].isActive = 1;
                SP->frameData.multiSelActiveNum += 1;
            }

            if (SP->frameData.multiSelActiveNum < 2)
            {
                SP->points.selected = 0;
            }

            SP->points.selected |= selectLoop2;
            SP->selectedPlace = 3;
        }
        else if (state == buttonRelease)
        {
            if (SP->frameData.multiSelActiveNum)
            {
                SP->points.selected &= ~selectLoop2;

                if (SP->frameData.multisel[3].isActive)
                {
                    SP->removeNode(2);
                    SP->frameData.multiSelActiveNum -= 1;

                    SP->frameData.multisel[3].isActive = 0;

                    if (SP->frameData.multiSelActiveNum == 0)
                    {
                        SP->selectedPlace = 3;
                        SP->points.selected |= selectLoop2;
                    }
                }
            }
        }

        SP->refreshPoints = 1;
    }

    SP->activateLabelsBorder();
}

static void functButton4(uint8_t state)
{
    if ((state > buttonPress) && (state != UINT8_MAX))
    {
        return;
    }

    if (SP->editorInstrument->playMode == playModeSlice ||
        SP->editorInstrument->playMode == playModeBeatSlice)
    {
        if (SP->sliceScreenMode2)
        {
            return;
        }

        if (state == buttonPress || state == UINT8_MAX)
        {
            // zoom
            SP->cancelMultiFrame();
            SP->clearAllNodes();

            SP->refreshPoints = 1;
            SP->refreshSlicePoints = 1;
            SP->points.selected = 0;

            SP->selectedPlace = 4;
        }
    }
    else if (SP->editorInstrument->playMode == playModeGranular)
    {
        if (state == buttonPress || state == UINT8_MAX)
        {
            SP->selectedPlace = 4;
        }
    }
    else if (SP->editorInstrument->playMode == playModeWavetable)
    {
        return;
    }
    else
    {
        if (state == UINT8_MAX || state == buttonPress)
        {
            if (SP->zoom.zoomValue > 1.0 && SP->zoom.lastChangedPoint != 2)
            {
                SP->zoom.lastChangedPoint = 2;
                SP->zoom.zoomPosition = SP->editorInstrument->endPoint;
                SP->refreshSpectrum = 1;
            }

            if (state == buttonPress)
            {
                SP->addNode(modEndPoint, 3);

                SP->frameData.multisel[4].frameNum = 4;
                SP->frameData.multisel[4].isActive = 1;
                SP->frameData.multiSelActiveNum += 1;
            }

            if (SP->frameData.multiSelActiveNum < 2)
            {
                SP->points.selected = 0;
            }

            SP->points.selected |= selectEnd;
            SP->selectedPlace = 4;
        }
        else if (state == buttonRelease)
        {
            if (SP->frameData.multiSelActiveNum)
            {
                SP->points.selected &= ~selectEnd;

                if (SP->frameData.multisel[4].isActive)
                {
                    SP->removeNode(3);
                    SP->frameData.multiSelActiveNum -= 1;

                    SP->frameData.multisel[4].isActive = 0;

                    if (SP->frameData.multiSelActiveNum == 0)
                    {
                        SP->selectedPlace = 4;
                        SP->points.selected |= selectEnd;
                    }
                }
            }
        }

        SP->refreshPoints = 1;
    }

    SP->activateLabelsBorder();
}

static void functButton5()
{
    if (SP->editorInstrument->playMode == playModeWavetable)
    {
        return;
    }
    if (SP->editorInstrument->playMode == playModeGranular)
    {
        return;
    }
    else if (SP->editorInstrument->playMode == playModeBeatSlice ||
             SP->editorInstrument->playMode == playModeSlice)
    {
        SP->sliceScreenMode2 = !SP->sliceScreenMode2;

        if (!SP->sliceScreenMode2)
        {
            SP->selectedPlace = 0;
        }

        SP->showDefaultScreen();
        return;
    }

    // zoom
    SP->cancelMultiFrame();
    SP->clearAllNodes();

    SP->refreshPoints = 1;
    SP->refreshSlicePoints = 1;
    SP->points.selected = 0;

    SP->selectedPlace = 5;
    SP->activateLabelsBorder();
}

static void functPlayMode(uint8_t button)
{
    SP->cancelMultiFrame();
    SP->clearAllNodes();

    if (SP->selectedPlace == 6)
    {
        if (button == interfaceButton6)
        {
            changePlayModeSelection(-1);
        }
        else if (button == interfaceButton7)
        {
            changePlayModeSelection(1);
        }
    }

    SP->selectedPlace = 6;
    SP->activateLabelsBorder();

    SP->points.selected = 0;
    SP->refreshPoints = 1;
    SP->refreshSlicePoints = 1;
}

static void functEncoder(int16_t value)
{
    if (SP->frameData.multiSelActiveNum != 0)
    {
        SP->stepThroughNodes(value);
    }
    else
    {
        switch (SP->selectedPlace)
        {
            case 0:
            {
                if ((SP->editorInstrument->playMode == playModeSlice) ||
                    (SP->editorInstrument->playMode == playModeBeatSlice))
                {
                    modSliceSelect(value);
                }
                break;
            }
            case 1:
            {
                if (SP->loadedInstrumentType == mtSampleTypeWavetable)
                {
                    modWavetablePostion(value);
                }
                else if (SP->loadedInstrumentType == mtSampleTypeWaveFile)
                {
                    if ((SP->editorInstrument->playMode == playModeSlice) ||
                        (SP->editorInstrument->playMode == playModeBeatSlice))
                        modSliceAdjust(value);
                    else if (SP->editorInstrument->playMode == playModeGranular)
                        modGranularPosition(value);
                    else
                        modStartPoint(value);
                }
                break;
            }
            case 2:
            {
                if (SP->slice2ndScreenActive())
                {
                    SP->modEqualSliceNumber(value);
                    return;
                }

                if (SP->loadedInstrumentType == mtSampleTypeWavetable)
                {
                    modWavetableWindowSize(value);
                }
                else if (SP->loadedInstrumentType == mtSampleTypeWaveFile)
                {
                    if ((SP->editorInstrument->playMode != playModeSlice) &&
                        (SP->editorInstrument->playMode != playModeBeatSlice) &&
                        (SP->editorInstrument->playMode != playModeGranular))
                        modLoopPoint1(value);
                    else if (SP->editorInstrument->playMode == playModeGranular)
                        modGranularLength(value);
                }
                break;
            }
            case 3:
            {
                if (SP->slice2ndScreenActive())
                {
                    SP->modSlicePlayThru(value);
                    return;
                }
                if (SP->editorInstrument->playMode != playModeGranular)
                    modLoopPoint2(value);
                else
                    modGranularShape((value > 0) ? 1 : -1);
                break;
            }
            case 4:
            {
                if ((SP->editorInstrument->playMode == playModeSlice) ||
                    (SP->editorInstrument->playMode == playModeBeatSlice))
                {
                    changeZoom(value);
                }
                else if (SP->editorInstrument->playMode == playModeGranular)
                {
                    modGranularLoopType((value > 0) ? 1 : -1);
                }
                else
                {
                    modEndPoint(value);
                }
                break;
            }
            case 5:
            {
                if ((SP->editorInstrument->playMode != playModeSlice) &&
                    (SP->editorInstrument->playMode != playModeBeatSlice))
                {
                    changeZoom(value);
                }
                break;
            }
            case 6:
            {
                changePlayModeSelection(interfaceGlobals.invertValForList ? -value : value);
                break;
            }
        }
    }
}

static void functLeft()
{
    if (SP->frameData.multiSelActiveNum != 0)
    {
        return;
    }

    switch (SP->selectedPlace)
    {
        case 1:
        {
            if ((SP->editorInstrument->playMode == playModeSlice) ||
                (SP->editorInstrument->playMode == playModeBeatSlice))
            {
                if (!SP->sliceScreenMode2)
                {
                    SP->selectedPlace = 0;
                }
            }
            break;
        }
        case 2:
        {
            if (SP->editorInstrument->playMode == playModeGranular)
            {
                SP->selectedPlace = 1;
            }
            else
            {
                functButton1(UINT8_MAX);
            }
            break;
        }
        case 3:
        {
            if (SP->editorInstrument->playMode == playModeSingleShot)
            {
                functButton1(UINT8_MAX);
            }
            else if (SP->editorInstrument->playMode == playModeGranular)
            {
                SP->selectedPlace = 2;
            }
            else
            {
                functButton2(UINT8_MAX);
            }
            break;
        }
        case 4:
        {
            if (SP->editorInstrument->playMode == playModeSingleShot)
            {
                functButton1(UINT8_MAX);
            }
            else if (SP->editorInstrument->playMode == playModeSlice ||
                     SP->editorInstrument->playMode == playModeBeatSlice)
            {
                if (SP->sliceScreenMode2)
                {
                    SP->selectedPlace = 2;
                }
                else
                {
                    SP->selectedPlace = 1;
                }
            }
            else if (SP->editorInstrument->playMode == playModeGranular)
            {
                SP->selectedPlace = 3;
            }
            else
            {
                functButton3(UINT8_MAX);
            }
            break;
        }
        case 5:
        {
            if ((SP->editorInstrument->playMode == playModeSlice) ||
                (SP->editorInstrument->playMode == playModeBeatSlice))
            {
                SP->selectedPlace = 1;
            }
            else if (SP->editorInstrument->playMode == playModeWavetable)
            {
                SP->selectedPlace = 2;
            }
            else
            {
                functButton4(UINT8_MAX);
            }
            break;
        }
        case 6:
        {
            if (SP->editorInstrument->playMode == playModeSlice ||
                SP->editorInstrument->playMode == playModeBeatSlice)
            {
                if (SP->sliceScreenMode2)
                {
                    SP->selectedPlace = 3;
                }
                else
                {
                    SP->selectedPlace = 4;
                }
            }
            else if (SP->loadedInstrumentType == mtSampleTypeWavetable)
            {
                SP->selectedPlace = 2;
            }
            else if (SP->editorInstrument->playMode == playModeGranular)
            {
                SP->selectedPlace = 4;
            }
            else
            {
                SP->selectedPlace = 5;
            }
            break;
        }
        default: return;
    }

    SP->activateLabelsBorder();
}

static void functRight()
{
    if (SP->frameData.multiSelActiveNum != 0)
    {
        return;
    }

    switch (SP->selectedPlace)
    {
        case 0:
        {
            if ((SP->editorInstrument->playMode == playModeSlice) ||
                (SP->editorInstrument->playMode == playModeBeatSlice))
            {
                SP->selectedPlace = 1;
            }
            else
            {
                functButton1(UINT8_MAX);
            }
            break;
        }
        case 1:
        {
            if (SP->editorInstrument->playMode == playModeSingleShot)
            {
                functButton4(UINT8_MAX);
            }
            else if ((SP->editorInstrument->playMode == playModeSlice) ||
                     (SP->editorInstrument->playMode == playModeBeatSlice))
            {
                functButton4(UINT8_MAX);
            }
            else if (SP->editorInstrument->playMode == playModeGranular)
            {
                SP->selectedPlace = 2;
            }
            else
            {
                functButton2(UINT8_MAX);
            }
            break;
        }
        case 2:
        {
            if (SP->editorInstrument->playMode == playModeSingleShot)
            {
                functButton4(UINT8_MAX);
            }
            else if ((SP->editorInstrument->playMode == playModeSlice) ||
                     (SP->editorInstrument->playMode == playModeBeatSlice))
            {
                SP->selectedPlace = SP->sliceScreenMode2 ? 3 : 6;
            }
            else if (SP->editorInstrument->playMode == playModeGranular)
            {
                SP->selectedPlace = 3;
            }
            else if (SP->editorInstrument->playMode == playModeWavetable)
            {
                SP->selectedPlace = 6;
            }
            else
            {
                functButton3(UINT8_MAX);
            }
            break;
        }
        case 3:
        {
            if ((SP->editorInstrument->playMode == playModeSlice) ||
                (SP->editorInstrument->playMode == playModeBeatSlice))
            {
                SP->selectedPlace = 6;
            }
            else if (SP->editorInstrument->playMode == playModeGranular)
            {
                SP->selectedPlace = 4;
            }
            else
            {
                functButton4(UINT8_MAX);
            }
            break;
        }
        case 4:
        {
            if ((SP->editorInstrument->playMode == playModeSlice) ||
                (SP->editorInstrument->playMode == playModeBeatSlice))
            {
                SP->selectedPlace = 6;
            }
            else if (SP->editorInstrument->playMode == playModeGranular)
            {
                SP->selectedPlace = 6;
            }
            else
            {
                functButton5();
            }
            break;
        }
        case 5:
        {
            SP->selectedPlace = 6;
            SP->points.selected = 0;
            break;
        }
    }

    SP->activateLabelsBorder();
}

static void functUp()
{
    if (SP->frameData.multiSelActiveNum != 0)
    {
        SP->stepThroughNodes(1);
    }
    else
    {
        switch (SP->selectedPlace)
        {
            case 0:
            {
                if ((SP->editorInstrument->playMode == playModeBeatSlice) ||
                    (SP->editorInstrument->playMode == playModeSlice))
                    if (!SP->slice2ndScreenActive())
                    {
                        modSliceSelect(1);
                    }
                break;
            }
            case 1:
            {
                if (SP->loadedInstrumentType == mtSampleTypeWavetable)
                {
                    modWavetablePostion(1);
                }
                else if (SP->loadedInstrumentType == mtSampleTypeWaveFile)
                {
                    if ((SP->editorInstrument->playMode == playModeSlice) ||
                        (SP->editorInstrument->playMode == playModeBeatSlice))
                    {
                        if (!SP->slice2ndScreenActive())
                        {
                            modSliceAdjust(1);
                        }
                    }
                    else if (SP->editorInstrument->playMode == playModeGranular)
                        modGranularPosition(1);
                    else
                        modStartPoint(1);
                }
                break;
            }
            case 2:
            {
                if (SP->loadedInstrumentType == mtSampleTypeWavetable)
                {
                    modWavetableWindowSize(1);
                }
                else if (SP->loadedInstrumentType == mtSampleTypeWaveFile)
                {
                    if ((SP->editorInstrument->playMode == playModeSlice) ||
                        (SP->editorInstrument->playMode == playModeBeatSlice))
                    {
                        if (SP->slice2ndScreenActive())
                        {
                            SP->modEqualSliceNumber(1);
                        }
                    }
                    else if (SP->editorInstrument->playMode == playModeGranular)
                        modGranularLength(1);
                    else
                        modLoopPoint1(1);
                }
                break;
            }
            case 3:
            {
                if (SP->slice2ndScreenActive())
                    SP->modSlicePlayThru(1);
                else if (SP->editorInstrument->playMode == playModeGranular)
                    modGranularShape(1);
                else
                    modLoopPoint2(1);
                break;
            }
            case 4:
            {
                if ((SP->editorInstrument->playMode == playModeSlice) ||
                    (SP->editorInstrument->playMode == playModeBeatSlice))
                {
                    if (!SP->slice2ndScreenActive())
                    {
                        changeZoom(1);
                    }
                }
                else if (SP->editorInstrument->playMode == playModeGranular)
                {
                    modGranularLoopType(1);
                }
                else
                {
                    modEndPoint(1);
                }
                break;
            }
            case 5: changeZoom(1); break;
            case 6: changePlayModeSelection(-1); break;
        }
    }
}

static void functDown()
{
    if (SP->frameData.multiSelActiveNum != 0)
    {
        SP->stepThroughNodes(-1);
    }
    else
    {
        switch (SP->selectedPlace)
        {
            case 0:
            {
                if ((SP->editorInstrument->playMode == playModeSlice) ||
                    (SP->editorInstrument->playMode == playModeBeatSlice))
                {
                    if (!SP->slice2ndScreenActive())
                    {
                        modSliceSelect(-1);
                    }
                }
                break;
            }
            case 1:
            {
                if (SP->loadedInstrumentType == mtSampleTypeWavetable)
                {
                    modWavetablePostion(-1);
                }
                else if (SP->loadedInstrumentType == mtSampleTypeWaveFile)
                {
                    if ((SP->editorInstrument->playMode == playModeSlice) ||
                        (SP->editorInstrument->playMode == playModeBeatSlice))
                    {
                        if (!SP->slice2ndScreenActive())
                        {
                            modSliceAdjust(-1);
                        }
                    }
                    else if (SP->editorInstrument->playMode == playModeGranular)
                        modGranularPosition(-1);
                    else
                        modStartPoint(-1);
                }
                break;
            }
            case 2:
            {
                if (SP->loadedInstrumentType == mtSampleTypeWavetable)
                {
                    modWavetableWindowSize(-1);
                }
                else if (SP->loadedInstrumentType == mtSampleTypeWaveFile)
                {
                    if ((SP->editorInstrument->playMode == playModeSlice) ||
                        (SP->editorInstrument->playMode == playModeBeatSlice))
                    {
                        if (SP->slice2ndScreenActive())
                        {
                            SP->modEqualSliceNumber(-1);
                        }
                    }
                    else if (SP->editorInstrument->playMode == playModeGranular)
                        modGranularLength(-1);
                    else
                        modLoopPoint1(-1);
                }
                break;
            }
            case 3:
            {
                if (SP->slice2ndScreenActive())
                    SP->modSlicePlayThru(-1);
                else if (SP->editorInstrument->playMode == playModeGranular)
                    modGranularShape(-1);
                else
                    modLoopPoint2(-1);
                break;
            }
            case 4:
            {
                if ((SP->editorInstrument->playMode == playModeSlice) ||
                    (SP->editorInstrument->playMode == playModeBeatSlice))
                {
                    if (!SP->slice2ndScreenActive())
                    {
                        changeZoom(-1);
                    }
                }
                else if (SP->editorInstrument->playMode == playModeGranular)
                {
                    modGranularLoopType(-1);
                }
                else
                {
                    modEndPoint(-1);
                }
                break;
            }
            case 5: changeZoom(-1); break;
            case 6: changePlayModeSelection(1); break;
        }
    }
}

static void functPlayAction()
{

    if (sequencer.getSeqState() == Sequencer::SEQ_STATE_STOP)
    {
        for (uint8_t i = 0; i < 8; i++)
        {
            instrumentPlayer[i].noteOff(Sequencer::STEP_NOTE_OFF_CUT); // POTRZEBNE TO TU? xxx
        }

        if (SP->buttons->isButtonPressed(interfaceButtonShift))
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

static void functRecAction()
{
}

static void functSwitchModule(uint8_t button)
{
    SP->eventFunct(eventSwitchModule, SP, &button, 0);
}

//======================================================================================================================
static void changeZoom(int16_t value)
{
    GP.spectrumChangeZoom(value, SP->editorInstrument->sample.length, &SP->zoom);

    SP->refreshSpectrum = 1;
    SP->refreshPoints = 1;
    SP->refreshSlicePoints = 1;

    SP->showZoomValue();
}

static void changePlayModeSelection(int16_t value)
{
    if (SP->isPlayingSample)
    {
        mtPadBoard.cutAllInstrument();
        SP->playProgressValue = 0;
        SP->playProgressInSpectrum = 0;
        SP->refreshSpectrumProgress = 1;
        SP->isPlayingSample = 0;
        SP->hidePreviewValue();
    }
    if (SP->isPlayingWavetable)
    {
        mtPadBoard.cutAllInstrument();
        SP->isPlayingWavetable = 0;
    }

    value = clamp<int>(value, -4, 4);
    uint8_t prevPlayMode = SP->editorInstrument->playMode;

    if (SP->editorInstrument->playMode + value < 0)
        SP->editorInstrument->playMode = 0;
    else if (SP->editorInstrument->playMode + value > playModeCount - 1)
        SP->editorInstrument->playMode = playModeCount - 1;
    else
        SP->editorInstrument->playMode += value;

    if (!isPlayModeStreamable(SP->editorInstrument->playMode) &&
        mtProject.values.lastUsedInstrument < kSampleInstrumentsCount &&
        mtProject.sampleMemory[mtProject.values.lastUsedInstrument].isStreamed)
    {
        if (!newFileManager.promoteStreamedInstrumentToRam(mtProject.values.lastUsedInstrument))
        {
            SP->editorInstrument->playMode = prevPlayMode;
        }
    }
    else if (SP->editorInstrument->playMode != prevPlayMode &&
             mtProject.values.lastUsedInstrument < kSampleInstrumentsCount &&
             mtProject.sampleMemory[mtProject.values.lastUsedInstrument].isStreamed)
    {
        voiceStreamManager.refreshInstrumentSliceCache(mtProject.values.lastUsedInstrument);
    }

    if (SP->editorInstrument->playMode == playModeWavetable)
    {
        SP->showWavetablePositionCursor();
    }
    else
    {
        SP->hideWavetablePositionCursor();
    }

    if (SP->editorInstrument->playMode == playModeWavetable)
    {
        if (SP->editorInstrument->sample.type == 0)
            SP->refreshSpectrum = 1;
        SP->editorInstrument->sample.type = 1;
        SP->editorInstrument->sample.wavetable_window_size =
          (SP->editorInstrument->sample.length >=
           SP->editorInstrument->sample.wavetable_window_size)
            ? SP->editorInstrument->sample.wavetable_window_size
            : SP->editorInstrument->sample.length;
        //*******************************wavetable window size moze byc tylko potęgą 2
        // Jezeli length nie jest potega 2 trzeba go zrownac do najwiekszej mozliwej potegi 2
        uint16_t localMask = 2048;
        // A size above the largest window, as a damaged or foreign instrument file can
        // hold, may have no bit that the mask reaches, and the loop below would not end.
        if (SP->editorInstrument->sample.wavetable_window_size > localMask)
        {
            SP->editorInstrument->sample.wavetable_window_size = localMask;
        }
        while (!(SP->editorInstrument->sample.wavetable_window_size & localMask))
        {
            if ((SP->editorInstrument->sample.wavetable_window_size == 0))
                break;
            localMask >>= 1;
        }

        SP->editorInstrument->sample.wavetable_window_size &= localMask;
        //**************************************************************************
        SP->wavetableWindowsCounter = SP->convertWavetableWindowsSizeToCounter(
          SP->editorInstrument->sample.wavetable_window_size);
        SP->editorInstrument->sample.wavetableWindowNumber =
          SP->editorInstrument->sample.wavetable_window_size
            ? SP->editorInstrument->sample.length /
                SP->editorInstrument->sample.wavetable_window_size
            : 0;
        SP->loadedInstrumentType = SP->editorInstrument->sample.type;
    }
    else if (SP->editorInstrument->playMode != playModeWavetable)
    {
        if (SP->editorInstrument->sample.type == 1)
            SP->refreshSpectrum = 1;
        SP->editorInstrument->sample.type = 0;
        SP->loadedInstrumentType = SP->editorInstrument->sample.type;
    }

    if (((SP->editorInstrument->playMode == playModeSingleShot) && (value < 0)) ||
        ((SP->editorInstrument->playMode == playModeSlice) && (value > 0)))
    {
        SP->hideLoopPoints();
        if (SP->editorInstrument->playMode == playModeSingleShot)
            SP->refreshPoints = 1;
    }
    else
    {
        if (SP->editorInstrument->loopPoint1 >= SP->editorInstrument->loopPoint2)
            SP->editorInstrument->loopPoint1 = SP->editorInstrument->loopPoint2 - 1;

        if (((SP->editorInstrument->loopPoint1 >= SP->editorInstrument->endPoint) &&
             (SP->editorInstrument->loopPoint2 >= SP->editorInstrument->endPoint)) ||
            ((SP->editorInstrument->loopPoint1 <= SP->editorInstrument->startPoint) &&
             (SP->editorInstrument->loopPoint2 <= SP->editorInstrument->startPoint)) ||
            ((SP->editorInstrument->loopPoint1 <= SP->editorInstrument->startPoint) &&
             (SP->editorInstrument->loopPoint2 >= SP->editorInstrument->endPoint)))
        {
            SP->editorInstrument->loopPoint1 = SP->editorInstrument->startPoint + 1;
            SP->editorInstrument->loopPoint2 = SP->editorInstrument->endPoint - 1;
        }
        else if ((SP->editorInstrument->loopPoint1 >= SP->editorInstrument->startPoint) &&
                 (SP->editorInstrument->loopPoint1 <= SP->editorInstrument->endPoint) &&
                 (SP->editorInstrument->loopPoint2 >= SP->editorInstrument->endPoint))
        {
            SP->editorInstrument->loopPoint2 = SP->editorInstrument->endPoint - 1;
        }
        else if ((SP->editorInstrument->loopPoint2 >= SP->editorInstrument->startPoint) &&
                 (SP->editorInstrument->loopPoint2 <= SP->editorInstrument->endPoint) &&
                 (SP->editorInstrument->loopPoint1 <= SP->editorInstrument->startPoint))
        {
            SP->editorInstrument->loopPoint1 = SP->editorInstrument->startPoint + 1;
        }

        SP->showLoopPoints();
        SP->refreshPoints = 1;
    }

    if ((SP->editorInstrument->playMode == playModeSlice) ||
        (SP->editorInstrument->playMode == playModeBeatSlice))
    {

        SP->zoom.zoomPosition =
          (SP->editorInstrument->sliceNumber > 0)
            ? SP->editorInstrument->slices[SP->editorInstrument->selectedSlice]
            : 0;

        SP->refreshSlicePoints = 1;
    }

    SP->sliceScreenMode2 = false;

    newFileManager.setInstrumentStructChanged(mtProject.values.lastUsedInstrument);

    display.setControlValue(SP->playModeListControl,
                            SP->editorInstrument->playMode + SP->editorInstrument->sample.type);
    display.refreshControl(SP->playModeListControl);

    SP->showDefaultScreen();
    SP->lightUpPadBoard();
}

static void modStartPoint(int16_t value)
{
    // obliczenie kroku przesuniecia w zaleznosci od ilosci widzianych probek na wyswietlaczu
    int32_t move_step = SP->zoom.zoomWidth / 600;
    int32_t dif;
    int32_t postValue = value * move_step;

    if (SP->editorInstrument->startPoint + postValue < SAMPLE_POINT_POS_MIN)
        SP->editorInstrument->startPoint = 0;
    else if (SP->editorInstrument->startPoint + postValue > SAMPLE_POINT_POS_MAX)
        SP->editorInstrument->startPoint = SAMPLE_POINT_POS_MAX;
    else
        SP->editorInstrument->startPoint += postValue;

    if (SP->editorInstrument->startPoint >= SP->editorInstrument->endPoint)
    {
        SP->editorInstrument->startPoint = SP->editorInstrument->endPoint - 2;
    }

    if ((SP->editorInstrument->playMode != playModeSingleShot) &&
        (SP->editorInstrument->playMode != playModeWavetable))
    {
        if (SP->editorInstrument->startPoint >= SP->editorInstrument->loopPoint1)
        {
            dif = SP->editorInstrument->loopPoint2 - SP->editorInstrument->loopPoint1;
            SP->editorInstrument->loopPoint1 = SP->editorInstrument->startPoint + 1;

            if (SP->editorInstrument->loopPoint1 + dif > SP->editorInstrument->endPoint)
            {
                SP->editorInstrument->loopPoint2 = SP->editorInstrument->endPoint - 1;
                SP->editorInstrument->loopPoint1 = SP->editorInstrument->loopPoint2 - dif;
                SP->editorInstrument->startPoint = SP->editorInstrument->loopPoint1 - 1;
                for (uint8_t i = 0; i < 8; i++)
                {
                    instrumentPlayer[i].setRefreshBit(LP1_MASK);
                    instrumentPlayer[i].setRefreshBit(LP2_MASK);
                }
            }
            else
            {
                SP->editorInstrument->loopPoint2 = SP->editorInstrument->loopPoint1 + dif;
                for (uint8_t i = 0; i < 8; i++)
                {
                    instrumentPlayer[i].setRefreshBit(LP2_MASK);
                }
            }

            SP->showLoopPoint1Value();
            SP->showLoopPoint2Value();
        }
    }

    // odswiez spektrum tylko jesli: zoom wiekszy niz 1, ostatnio modyfikowany inny punkt, punkt
    // jest poza widocznym obszarem
    if (SP->zoom.zoomValue > 1 &&
        (SP->zoom.lastChangedPoint != 1 || (SP->editorInstrument->startPoint < SP->zoom.zoomStart ||
                                            SP->editorInstrument->startPoint > SP->zoom.zoomEnd)))
    {
        SP->refreshSpectrum = 1;
    }

    SP->zoom.zoomPosition = SP->editorInstrument->startPoint;
    SP->zoom.lastChangedPoint = 1;
    SP->refreshPoints = 1;

    newFileManager.setInstrumentStructChanged(mtProject.values.lastUsedInstrument);

    SP->showStartPointValue();
}

static void modEndPoint(int16_t value)
{
    uint16_t move_step = SP->zoom.zoomWidth / 600;
    uint16_t dif;
    value = value * move_step;

    if (SP->editorInstrument->endPoint + value < SAMPLE_POINT_POS_MIN)
        SP->editorInstrument->endPoint = 0;
    else if (SP->editorInstrument->endPoint + value > SAMPLE_POINT_POS_MAX)
        SP->editorInstrument->endPoint = SAMPLE_POINT_POS_MAX;
    else
        SP->editorInstrument->endPoint += value;

    if (SP->editorInstrument->endPoint <= SP->editorInstrument->startPoint)
    {
        SP->editorInstrument->endPoint = SP->editorInstrument->startPoint + 2;
    }

    if (SP->editorInstrument->playMode != playModeSingleShot)
    {
        if (SP->editorInstrument->endPoint < SP->editorInstrument->loopPoint2)
        {
            dif = SP->editorInstrument->loopPoint2 - SP->editorInstrument->loopPoint1;

            SP->editorInstrument->loopPoint2 = SP->editorInstrument->endPoint - 1;

            if (SP->editorInstrument->loopPoint2 - dif < SP->editorInstrument->startPoint)
            {
                SP->editorInstrument->loopPoint1 = SP->editorInstrument->startPoint + 1;

                SP->editorInstrument->loopPoint2 = SP->editorInstrument->loopPoint1 + dif;
                SP->editorInstrument->endPoint = SP->editorInstrument->loopPoint2 + 1;

                for (uint8_t i = 0; i < 8; i++)
                {
                    instrumentPlayer[i].setRefreshBit(LP1_MASK);
                    instrumentPlayer[i].setRefreshBit(LP2_MASK);
                }
            }
            else
            {
                SP->editorInstrument->loopPoint1 = SP->editorInstrument->loopPoint2 - dif;
                for (uint8_t i = 0; i < 8; i++)
                {
                    instrumentPlayer[i].setRefreshBit(LP1_MASK);
                }
            }

            SP->showLoopPoint1Value();
            SP->showLoopPoint2Value();
        }
    }

    if (SP->zoom.zoomValue > 1 &&
        (SP->zoom.lastChangedPoint != 2 || (SP->editorInstrument->endPoint < SP->zoom.zoomStart ||
                                            SP->editorInstrument->endPoint > SP->zoom.zoomEnd)))
        SP->refreshSpectrum = 1;

    SP->zoom.zoomPosition = SP->editorInstrument->endPoint;
    SP->zoom.lastChangedPoint = 2;
    SP->refreshPoints = 1;

    newFileManager.setInstrumentStructChanged(mtProject.values.lastUsedInstrument);

    SP->showEndPointValue();
}

static void modLoopPoint1(int16_t value)
{
    uint16_t move_step = SP->zoom.zoomWidth / 600;
    value = value * move_step;

    if (SP->editorInstrument->loopPoint1 + value < SAMPLE_POINT_POS_MIN)
        SP->editorInstrument->loopPoint1 = 0;
    else if (SP->editorInstrument->loopPoint1 + value > SAMPLE_POINT_POS_MAX)
        SP->editorInstrument->loopPoint1 = SAMPLE_POINT_POS_MAX;
    else
        SP->editorInstrument->loopPoint1 += value;

    if (SP->editorInstrument->loopPoint1 <= SP->editorInstrument->startPoint)
        SP->editorInstrument->loopPoint1 = SP->editorInstrument->startPoint + 1;
    if (SP->editorInstrument->loopPoint1 >= SP->editorInstrument->loopPoint2)
        SP->editorInstrument->loopPoint1 = SP->editorInstrument->loopPoint2 - 1;

    if (SP->zoom.zoomValue > 1 &&
        (SP->zoom.lastChangedPoint != 3 || (SP->editorInstrument->loopPoint1 < SP->zoom.zoomStart ||
                                            SP->editorInstrument->loopPoint1 > SP->zoom.zoomEnd)))
        SP->refreshSpectrum = 1;

    for (uint8_t i = 0; i < 8; i++)
    {
        instrumentPlayer[i].setRefreshBit(LP1_MASK);
    }

    SP->zoom.zoomPosition = SP->editorInstrument->loopPoint1;
    SP->zoom.lastChangedPoint = 3;
    SP->refreshPoints = 1;

    newFileManager.setInstrumentStructChanged(mtProject.values.lastUsedInstrument);

    SP->showLoopPoint1Value();
}

static void modLoopPoint2(int16_t value)
{
    uint16_t move_step = SP->zoom.zoomWidth / 600;
    value = value * move_step;

    if (SP->editorInstrument->loopPoint2 + value < SAMPLE_POINT_POS_MIN)
        SP->editorInstrument->loopPoint2 = 0;
    else if (SP->editorInstrument->loopPoint2 + value > SAMPLE_POINT_POS_MAX)
        SP->editorInstrument->loopPoint2 = SAMPLE_POINT_POS_MAX;
    else
        SP->editorInstrument->loopPoint2 += value;

    if (SP->editorInstrument->loopPoint2 >= SP->editorInstrument->endPoint)
        SP->editorInstrument->loopPoint2 = SP->editorInstrument->endPoint - 1;
    if (SP->editorInstrument->loopPoint2 <= SP->editorInstrument->loopPoint1)
        SP->editorInstrument->loopPoint2 = SP->editorInstrument->loopPoint1 + 1;

    if (SP->zoom.zoomValue > 1 &&
        (SP->zoom.lastChangedPoint != 4 || (SP->editorInstrument->loopPoint2 < SP->zoom.zoomStart ||
                                            SP->editorInstrument->loopPoint2 > SP->zoom.zoomEnd)))
        SP->refreshSpectrum = 1;

    for (uint8_t i = 0; i < 8; i++)
    {
        instrumentPlayer[i].setRefreshBit(LP2_MASK);
    }

    SP->zoom.zoomPosition = SP->editorInstrument->loopPoint2;
    SP->zoom.lastChangedPoint = 4;
    SP->refreshPoints = 1;

    newFileManager.setInstrumentStructChanged(mtProject.values.lastUsedInstrument);

    SP->showLoopPoint2Value();
}

void cSamplePlayback::processWavetableCursor(uint32_t position)
{
    wtPosition.position = position;
    wtPosition.window = SP->editorInstrument->sample.wavetable_window_size;
    wtPosition.positionMax = SP->editorInstrument->sample.wavetableWindowNumber;

    showWavetablePositionCursor();
}

static void modWavetablePostion(int32_t value)
{
    if ((int32_t)(SP->editorInstrument->wavetableCurrentWindow + value) < 0)
        SP->editorInstrument->wavetableCurrentWindow = 0;
    else if ((SP->editorInstrument->wavetableCurrentWindow + value) >
             (SP->editorInstrument->sample.wavetableWindowNumber - 1))
        SP->editorInstrument->wavetableCurrentWindow =
          SP->editorInstrument->sample.wavetableWindowNumber - 1;
    else
        SP->editorInstrument->wavetableCurrentWindow += value;

    //	instrumentPlayer[0].instrumentBasedMod.wtPos =
    // SP->editorInstrument->wavetableCurrentWindow;
    for (uint8_t i = 0; i < 8; i++)
    {
        instrumentPlayer[i].setRefreshBit(WT_POS_SEND_MASK);
    }

    bool isParamVisible = mtInterface.isSamplePlayerScreenActive() &&
                          (SP->editorInstrument->playMode == instrumentPlayMode::playModeWavetable);

    if (isParamVisible)
    {
        SP->currentEnvelopeWtPos = SP->editorInstrument->wavetableCurrentWindow;
        SP->processWavetableCursor(SP->editorInstrument->wavetableCurrentWindow);
        SP->showWavetablePosition();
        SP->refreshSpectrum = 1;
    }

    newFileManager.setInstrumentStructChanged(mtProject.values.lastUsedInstrument);
}

static void modWavetableWindowSize(int16_t value)
{
    if (SP->isPlayingWavetable)
    {
        mtPadBoard.cutAllInstrument();
        SP->isPlayingWavetable = 0;
    }

    uint8_t lastWavetableWindowsCounter = SP->wavetableWindowsCounter;
    int8_t localDif;
    uint8_t localMaxWavetableWindowsCounter = MAX_WAVETABLE_WINDOWS_COUNTER;

    uint16_t tmpWindowSize = 2048;

    while ((tmpWindowSize > SP->editorInstrument->sample.length) &&
           (localMaxWavetableWindowsCounter > MIN_WAVETABLE_WINDOWS_COUNTER))
    {
        tmpWindowSize >>= 1;
        localMaxWavetableWindowsCounter--;
    }

    if (SP->wavetableWindowsCounter + value < MIN_WAVETABLE_WINDOWS_COUNTER)
        SP->wavetableWindowsCounter = MIN_WAVETABLE_WINDOWS_COUNTER;
    else if (SP->wavetableWindowsCounter + value > localMaxWavetableWindowsCounter)
        SP->wavetableWindowsCounter = localMaxWavetableWindowsCounter;
    else
        SP->wavetableWindowsCounter += value;

    localDif = lastWavetableWindowsCounter - SP->wavetableWindowsCounter;

    if (localDif > 0)
        SP->editorInstrument->wavetableCurrentWindow <<= localDif;
    else if (localDif < 0)
        SP->editorInstrument->wavetableCurrentWindow >>= (-localDif);

    bool isParamVisible = mtInterface.isSamplePlayerScreenActive() &&
                          (SP->editorInstrument->playMode == instrumentPlayMode::playModeWavetable);

    if (localDif != 0 && isParamVisible)
        SP->showWavetablePosition();
    AudioNoInterrupts();
    SP->editorInstrument->sample.wavetable_window_size =
      SP->convertWavetableWindowsCounterToSize(SP->wavetableWindowsCounter);
    SP->editorInstrument->sample.wavetableWindowNumber =
      SP->editorInstrument->sample.wavetable_window_size
        ? SP->editorInstrument->sample.length / SP->editorInstrument->sample.wavetable_window_size
        : 0;

    if (SP->editorInstrument->wavetableCurrentWindow >=
        SP->editorInstrument->sample.wavetableWindowNumber)
    {
        SP->editorInstrument->wavetableCurrentWindow =
          SP->editorInstrument->sample.wavetableWindowNumber - 1;
        if (isParamVisible)
            SP->showWavetablePosition();
    }
    AudioInterrupts();

    if (isParamVisible)
    {
        SP->processWavetableCursor(SP->editorInstrument->wavetableCurrentWindow);
        SP->showWavetableWindowSize();
        SP->refreshSpectrum = 1;
    }
    newFileManager.setInstrumentStructChanged(mtProject.values.lastUsedInstrument);
}

static void modSliceSelect(int16_t value, bool set)
{
    if (set)
    {
        if (value != 0 && value > SP->editorInstrument->sliceNumber - 1)
        {
            return;
        }
        SP->editorInstrument->selectedSlice = value;
    }
    else
    {
        if (SP->editorInstrument->selectedSlice + value < 0)
            SP->editorInstrument->selectedSlice = 0;
        else if (SP->editorInstrument->selectedSlice + value >
                 SP->editorInstrument->sliceNumber - 1)
        {
            SP->editorInstrument->selectedSlice =
              (SP->editorInstrument->sliceNumber == 0) ? 0 : SP->editorInstrument->sliceNumber - 1;
        }
        else
            SP->editorInstrument->selectedSlice += value;
    }

    if ((SP->editorInstrument->playMode == playModeSlice) ||
        (SP->editorInstrument->playMode == playModeBeatSlice))
    {
        SP->zoom.zoomPosition =
          (SP->editorInstrument->sliceNumber > 0)
            ? SP->editorInstrument->slices[SP->editorInstrument->selectedSlice]
            : 0;
        if ((SP->zoom.zoomPosition > SP->zoom.zoomEnd) ||
            (SP->zoom.zoomPosition < SP->zoom.zoomStart))
            SP->refreshSpectrum = 1;
    }

    newFileManager.setInstrumentStructChanged(mtProject.values.lastUsedInstrument);

    if (!SP->slice2ndScreenActive())
    {
        SP->showSlicesSelectValue();
        SP->showSlicesAdjustValue();
    }
    SP->refreshSlicePoints = 1;
}

static void modSliceAdjust(int16_t value)
{
    uint16_t move_step = SP->zoom.zoomWidth / 600;
    value *= move_step;
    sliceManager.adjustSlice(SP->editorInstrument, value);
    if (mtProject.values.lastUsedInstrument < kSampleInstrumentsCount &&
        mtProject.sampleMemory[mtProject.values.lastUsedInstrument].isStreamed)
    {
        voiceStreamManager.refreshInstrumentSingleSliceCache(
          mtProject.values.lastUsedInstrument, SP->editorInstrument->selectedSlice);
    }

    if ((SP->editorInstrument->playMode == playModeSlice) ||
        (SP->editorInstrument->playMode == playModeBeatSlice))
    {
        SP->zoom.zoomPosition =
          (SP->editorInstrument->sliceNumber > 0)
            ? SP->editorInstrument->slices[SP->editorInstrument->selectedSlice]
            : 0;
        if ((SP->zoom.zoomPosition > SP->zoom.zoomEnd) ||
            (SP->zoom.zoomPosition < SP->zoom.zoomStart))
            SP->refreshSpectrum = 1;
    }

    newFileManager.setInstrumentStructChanged(mtProject.values.lastUsedInstrument);

    SP->showSlicesAdjustValue();
    SP->refreshSlicePoints = 1;
}

static void functAddSlice()
{
    sliceManager.addSlice(SP->editorInstrument);
    if (mtProject.values.lastUsedInstrument < kSampleInstrumentsCount &&
        mtProject.sampleMemory[mtProject.values.lastUsedInstrument].isStreamed)
    {
        voiceStreamManager.refreshInstrumentSliceCache(mtProject.values.lastUsedInstrument);
    }
    if ((SP->editorInstrument->playMode == playModeSlice) ||
        (SP->editorInstrument->playMode == playModeBeatSlice))
    {
        SP->zoom.zoomPosition =
          (SP->editorInstrument->sliceNumber > 0)
            ? SP->editorInstrument->slices[SP->editorInstrument->selectedSlice]
            : 0;
        if ((SP->zoom.zoomPosition > SP->zoom.zoomEnd) ||
            (SP->zoom.zoomPosition < SP->zoom.zoomStart))
        {
            SP->refreshSpectrum = 1;
        }
    }

    SP->showSlicesSelectValue();
    SP->showSlicesAdjustValue();
    SP->refreshSlicePoints = 1;

    SP->selectedPlace = 1;
    SP->activateLabelsBorder();

    newFileManager.setInstrumentStructChanged(mtProject.values.lastUsedInstrument);
}

static void functRemoveSlice()
{
    sliceManager.removeSlice(SP->editorInstrument);
    if (mtProject.values.lastUsedInstrument < kSampleInstrumentsCount &&
        mtProject.sampleMemory[mtProject.values.lastUsedInstrument].isStreamed)
    {
        voiceStreamManager.refreshInstrumentSliceCache(mtProject.values.lastUsedInstrument);
    }
    if ((SP->editorInstrument->playMode == playModeSlice) ||
        (SP->editorInstrument->playMode == playModeBeatSlice))
    {
        SP->zoom.zoomPosition =
          (SP->editorInstrument->sliceNumber > 0)
            ? SP->editorInstrument->slices[SP->editorInstrument->selectedSlice]
            : 0;
        if ((SP->zoom.zoomPosition > SP->zoom.zoomEnd) ||
            (SP->zoom.zoomPosition < SP->zoom.zoomStart))
        {
            SP->refreshSpectrum = 1;
        }
    }
    SP->showSlicesSelectValue();
    SP->showSlicesAdjustValue();
    SP->refreshSlicePoints = 1;

    newFileManager.setInstrumentStructChanged(mtProject.values.lastUsedInstrument);
}

static void functAutoSlice()
{
    SP->overwriteSliceSource = cSamplePlayback::overvwriteAutoSlice;

    if (SP->editorInstrument->sliceNumber > 0)
    {
        SP->showAutoSlicePopup();
        SP->setAutoSlicePopupFunct();
        return;
    }

    functAutoSlicConfirmOverwrite();
}

static void functEqualSlice()
{
    SP->overwriteSliceSource = cSamplePlayback::overwriteEqualSlice;

    if (SP->editorInstrument->sliceNumber > 0)
    {
        SP->showAutoSlicePopup();
        SP->setAutoSlicePopupFunct();
        return;
    }

    functAutoSlicConfirmOverwrite();
}

static void functAutoSlicConfirmOverwrite()
{
    if ((SP->editorInstrument->playMode == playModeSlice) ||
        (SP->editorInstrument->playMode == playModeBeatSlice))
    {
        SP->zoom.zoomPosition =
          (SP->editorInstrument->sliceNumber > 0)
            ? SP->editorInstrument->slices[SP->editorInstrument->selectedSlice]
            : 0;
        if ((SP->zoom.zoomPosition > SP->zoom.zoomEnd) ||
            (SP->zoom.zoomPosition < SP->zoom.zoomStart))
            SP->refreshSpectrum = 1;
    }

    if (SP->overwriteSliceSource == cSamplePlayback::overvwriteAutoSlice)
    {
        sliceManager.autoSlice(SP->editorInstrument);
    }
    else
    {
        sliceManager.equalSlice(SP->editorInstrument, mtConfig.common.equalSliceNumber);
        if (mtProject.values.lastUsedInstrument < kSampleInstrumentsCount &&
            mtProject.sampleMemory[mtProject.values.lastUsedInstrument].isStreamed)
        {
            voiceStreamManager.refreshInstrumentSliceCache(mtProject.values.lastUsedInstrument);
        }
        newFileManager.setInstrumentStructChanged(mtProject.values.lastUsedInstrument);
    }

    SP->showDefaultScreen();
    SP->setDefaultScreenFunct();
    SP->lightUpPadBoard();

    SP->refreshSlicePoints = 1;
}

static void functCancelOverwrite()
{
    SP->showDefaultScreen();
    SP->setDefaultScreenFunct();
}

static void modGranularLength(int16_t value)
{
    value *= MIN_GRANULAR_LENGTH / 4;

    int32_t grainConstrain = SP->editorInstrument->sample.length < MAX_GRANULAR_LENGTH
                               ? SP->editorInstrument->sample.length
                               : MAX_GRANULAR_LENGTH;

    if (SP->editorInstrument->granular.grainLength + value > grainConstrain)
        SP->editorInstrument->granular.grainLength = grainConstrain;
    else if (SP->editorInstrument->granular.grainLength + value < MIN_GRANULAR_LENGTH)
        SP->editorInstrument->granular.grainLength = MIN_GRANULAR_LENGTH;
    else
        SP->editorInstrument->granular.grainLength += value;

    SP->currentEnvelopeGranLen = SP->isPlayingSample
                                   ? instrumentPlayer[0].getEnvelopeGranLenMod()
                                   : SP->editorInstrument->granular.grainLength;

    bool isParamVisible = mtInterface.isSamplePlayerScreenActive() &&
                          (SP->editorInstrument->playMode == instrumentPlayMode::playModeGranular);

    if (isParamVisible)
        SP->showGrainLengthValue();

    for (uint8_t i = 0; i < 8; i++)
    {
        instrumentPlayer[i].setRefreshBit(GRANULAR_LEN_SEND_MASK);
    }

    newFileManager.setInstrumentStructChanged(mtProject.values.lastUsedInstrument);
}

static void modGranularShape(int16_t value)
{
    if (SP->editorInstrument->granular.shape + value > granularTypeCount - 1)
        SP->editorInstrument->granular.shape = granularTypeCount - 1;
    else if (SP->editorInstrument->granular.shape + value < 0)
        SP->editorInstrument->granular.shape = 0;
    else
        SP->editorInstrument->granular.shape += value;

    bool isParamVisible = mtInterface.isSamplePlayerScreenActive() &&
                          (SP->editorInstrument->playMode == instrumentPlayMode::playModeGranular);

    if (isParamVisible)
        SP->showShapeText();

    for (uint8_t i = 0; i < 8; i++)
    {
        instrumentPlayer[i].setRefreshBit(GRANULAR_WAVE_SEND_MASK);
    }

    newFileManager.setInstrumentStructChanged(mtProject.values.lastUsedInstrument);
}

static void modGranularLoopType(int16_t value)
{
    if (SP->editorInstrument->granular.type + value > granularLoopCount - 1)
        SP->editorInstrument->granular.type = granularLoopCount - 1;
    else if (SP->editorInstrument->granular.type + value < 0)
        SP->editorInstrument->granular.type = 0;
    else
        SP->editorInstrument->granular.type += value;

    bool isParamVisible = mtInterface.isSamplePlayerScreenActive() &&
                          (SP->editorInstrument->playMode == instrumentPlayMode::playModeGranular);

    if (isParamVisible)
        SP->showLoopTypeText();

    for (uint8_t i = 0; i < 8; i++)
    {
        instrumentPlayer[i].setRefreshBit(GRANULAR_LOOP_SEND_MASK);
    }

    newFileManager.setInstrumentStructChanged(mtProject.values.lastUsedInstrument);
}

static void modGranularPosition(int16_t value)
{
    int long_value = value * (MAX_16BIT / 1000);
    if ((int32_t)(SP->editorInstrument->granular.currentPosition + long_value) > MAX_16BIT)
        SP->editorInstrument->granular.currentPosition = MAX_16BIT;
    else if ((int32_t)(SP->editorInstrument->granular.currentPosition + long_value) < 0)
        SP->editorInstrument->granular.currentPosition = 0;
    else
        SP->editorInstrument->granular.currentPosition += long_value;

    SP->currentEnvelopeGranPos = SP->isPlayingSample
                                   ? instrumentPlayer[0].getEnvelopeGranPosMod()
                                   : SP->editorInstrument->granular.currentPosition;

    bool isParamVisible = mtInterface.isSamplePlayerScreenActive() &&
                          (SP->editorInstrument->playMode == instrumentPlayMode::playModeGranular);

    if (isParamVisible)
        SP->showGranularPositionValue();

    //	instrumentPlayer[0].instrumentBasedMod.granPos =
    // SP->editorInstrument->granular.currentPosition;
    for (uint8_t i = 0; i < 8; i++)
    {
        instrumentPlayer[i].setRefreshBit(GRANULAR_POS_SEND_MASK);
    }

    newFileManager.setInstrumentStructChanged(mtProject.values.lastUsedInstrument);
}

void cSamplePlayback::calcPlayProgressValue()
{
    if (refreshPlayProgressValue > PLAY_REFRESH_US)
    {
        refreshPlayProgressValue = 0;
        playProgressValue = instrumentPlayer[displayedTrack].getWavePosition();

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

        refreshSpectrumProgress = 1;
    }
}

void cSamplePlayback::modEqualSliceNumber(int16_t val)
{
    if (mtConfig.common.equalSliceNumber + val > 48)
    {
        mtConfig.common.equalSliceNumber = 48;
    }
    else if (mtConfig.common.equalSliceNumber + val < 1)
    {
        mtConfig.common.equalSliceNumber = 1;
    }
    else
    {
        mtConfig.common.equalSliceNumber += val;
    }

    showSlicesEqualNumberValue();

    saveConfig();
}

void cSamplePlayback::modSlicePlayThru(int16_t val)
{
    if (val > 0)
    {
        editorInstrument->slicePlayThru = 1;
    }
    else if (val < 0)
    {
        editorInstrument->slicePlayThru = 0;
    }

    showSlicesPlayThruValue();

    newFileManager.setInstrumentStructChanged(mtProject.values.lastUsedInstrument);
}

void cSamplePlayback::setDefault(uint8_t pos)
{
    if (slice2ndScreenActive())
    {
        if (SP->selectedPlace == 2)
        {
            setDefaultEqualSliceNumber();
        }
        else if (SP->selectedPlace == 3)
        {
            modSlicePlayThru(-1);
        }
        return;
    }

    switch (pos)
    {
        case 0:
            if ((editorInstrument->playMode == playModeSlice) ||
                (editorInstrument->playMode == playModeBeatSlice))
                setDefaultSelectedSlice();
            break;
        case 1:
            if (loadedInstrumentType == mtSampleTypeWavetable)
            {
                setDefaultWavetablePosition();
            }
            else if (loadedInstrumentType == mtSampleTypeWaveFile)
            {
                if ((editorInstrument->playMode == playModeSlice) ||
                    (editorInstrument->playMode == playModeBeatSlice))
                    ;
                else if (editorInstrument->playMode == playModeGranular)
                    setDefaultGranularPosition();
                else
                    setDefaultStartPoint();
            }
            break;
        case 2:
            if (loadedInstrumentType == mtSampleTypeWavetable)
            {
                setDefaultWavetableWindow();
            }
            else if (loadedInstrumentType == mtSampleTypeWaveFile)
            {
                if ((editorInstrument->playMode != playModeSlice) &&
                    (editorInstrument->playMode != playModeBeatSlice) &&
                    (editorInstrument->playMode != playModeGranular))
                    setDefaultLoopPoint1();
                else if (editorInstrument->playMode == playModeGranular)
                    setDefaultGranularLength();
            }
            break;
        case 3:
            if (editorInstrument->playMode != playModeGranular)
                setDefaultLoopPoint2();
            else
                setDefaultGranularShape();
            break;
        case 4:
        {
            if ((SP->editorInstrument->playMode == playModeSlice) ||
                (SP->editorInstrument->playMode == playModeBeatSlice))
            {
                setDefaultZoom();
            }
            else if (SP->editorInstrument->playMode == playModeGranular)
            {
                setDefaultGranularLoop();
            }
            else
            {
                setDefaultEndPoint();
            }
            break;
        }
        case 5: setDefaultZoom(); break;
        case 6: break;
    }
}

void cSamplePlayback::setDefaultStartPoint()
{
    uint16_t dif;
    editorInstrument->startPoint = defaultInstrumentParams.startPoint;

    if ((editorInstrument->playMode != playModeSingleShot) &&
        (editorInstrument->playMode != playModeWavetable))
    {
        if (editorInstrument->startPoint >= editorInstrument->loopPoint1)
        {
            dif = editorInstrument->loopPoint2 - editorInstrument->loopPoint1;
            editorInstrument->loopPoint1 = editorInstrument->startPoint + 1;

            if (editorInstrument->loopPoint1 + dif > editorInstrument->endPoint)
            {
                editorInstrument->loopPoint2 = editorInstrument->endPoint - 1;
                editorInstrument->loopPoint1 = editorInstrument->loopPoint2 - dif;
                editorInstrument->startPoint = editorInstrument->loopPoint1 - 1;
                for (uint8_t i = 0; i < 8; i++)
                {
                    instrumentPlayer[i].setRefreshBit(LP1_MASK);
                    instrumentPlayer[i].setRefreshBit(LP2_MASK);
                }
            }
            else
            {
                editorInstrument->loopPoint2 = editorInstrument->loopPoint1 + dif;
                for (uint8_t i = 0; i < 8; i++)
                {
                    instrumentPlayer[i].setRefreshBit(LP2_MASK);
                }
            }

            showLoopPoint1Value();
            showLoopPoint2Value();
        }
    }

    // odswiez spektrum tylko jesli: zoom wiekszy niz 1, ostatnio modyfikowany inny punkt, punkt
    // jest poza widocznym obszarem
    if (zoom.zoomValue > 1 &&
        (zoom.lastChangedPoint != 1 || (editorInstrument->startPoint < zoom.zoomStart ||
                                        editorInstrument->startPoint > zoom.zoomEnd)))
    {
        refreshSpectrum = 1;
    }

    zoom.zoomPosition = editorInstrument->startPoint;
    zoom.lastChangedPoint = 1;
    refreshPoints = 1;

    newFileManager.setInstrumentStructChanged(mtProject.values.lastUsedInstrument);
    showStartPointValue();
}

void cSamplePlayback::setDefaultLoopPoint1()
{
    SP->editorInstrument->loopPoint1 = defaultInstrumentParams.loopPoint1;

    if (SP->editorInstrument->loopPoint1 <= SP->editorInstrument->startPoint)
        SP->editorInstrument->loopPoint1 = SP->editorInstrument->startPoint + 1;
    if (SP->editorInstrument->loopPoint1 >= SP->editorInstrument->loopPoint2)
        SP->editorInstrument->loopPoint1 = SP->editorInstrument->loopPoint2 - 1;

    if (SP->zoom.zoomValue > 1 &&
        (SP->zoom.lastChangedPoint != 3 || (SP->editorInstrument->loopPoint1 < SP->zoom.zoomStart ||
                                            SP->editorInstrument->loopPoint1 > SP->zoom.zoomEnd)))
        SP->refreshSpectrum = 1;

    for (uint8_t i = 0; i < 8; i++)
    {
        instrumentPlayer[i].setRefreshBit(LP1_MASK);
    }

    SP->zoom.zoomPosition = SP->editorInstrument->loopPoint1;
    SP->zoom.lastChangedPoint = 3;
    SP->refreshPoints = 1;

    newFileManager.setInstrumentStructChanged(mtProject.values.lastUsedInstrument);

    SP->showLoopPoint1Value();
}

void cSamplePlayback::setDefaultLoopPoint2()
{
    SP->editorInstrument->loopPoint2 = defaultInstrumentParams.loopPoint2;

    if (SP->editorInstrument->loopPoint2 >= SP->editorInstrument->endPoint)
        SP->editorInstrument->loopPoint2 = SP->editorInstrument->endPoint - 1;
    if (SP->editorInstrument->loopPoint2 <= SP->editorInstrument->loopPoint1)
        SP->editorInstrument->loopPoint2 = SP->editorInstrument->loopPoint1 + 1;

    if (SP->zoom.zoomValue > 1 &&
        (SP->zoom.lastChangedPoint != 4 || (SP->editorInstrument->loopPoint2 < SP->zoom.zoomStart ||
                                            SP->editorInstrument->loopPoint2 > SP->zoom.zoomEnd)))
        SP->refreshSpectrum = 1;

    for (uint8_t i = 0; i < 8; i++)
    {
        instrumentPlayer[i].setRefreshBit(LP2_MASK);
    }

    SP->zoom.zoomPosition = SP->editorInstrument->loopPoint2;
    SP->zoom.lastChangedPoint = 4;
    SP->refreshPoints = 1;

    newFileManager.setInstrumentStructChanged(mtProject.values.lastUsedInstrument);

    SP->showLoopPoint2Value();
}

void cSamplePlayback::setDefaultEndPoint()
{
    uint16_t dif;

    SP->editorInstrument->endPoint = defaultInstrumentParams.endPoint;

    if (SP->editorInstrument->playMode != playModeSingleShot)
    {
        if (SP->editorInstrument->endPoint < SP->editorInstrument->loopPoint2)
        {
            dif = SP->editorInstrument->loopPoint2 - SP->editorInstrument->loopPoint1;

            SP->editorInstrument->loopPoint2 = SP->editorInstrument->endPoint - 1;

            if (SP->editorInstrument->loopPoint2 - dif < SP->editorInstrument->startPoint)
            {
                SP->editorInstrument->loopPoint1 = SP->editorInstrument->startPoint + 1;

                SP->editorInstrument->loopPoint2 = SP->editorInstrument->loopPoint1 + dif;
                SP->editorInstrument->endPoint = SP->editorInstrument->loopPoint2 + 1;

                for (uint8_t i = 0; i < 8; i++)
                {
                    instrumentPlayer[i].setRefreshBit(LP1_MASK);
                    instrumentPlayer[i].setRefreshBit(LP2_MASK);
                }
            }
            else
            {
                SP->editorInstrument->loopPoint1 = SP->editorInstrument->loopPoint2 - dif;
                for (uint8_t i = 0; i < 8; i++)
                {
                    instrumentPlayer[i].setRefreshBit(LP1_MASK);
                }
            }

            SP->showLoopPoint1Value();
            SP->showLoopPoint2Value();
        }
    }

    if (SP->zoom.zoomValue > 1 &&
        (SP->zoom.lastChangedPoint != 2 || (SP->editorInstrument->endPoint < SP->zoom.zoomStart ||
                                            SP->editorInstrument->endPoint > SP->zoom.zoomEnd)))
        SP->refreshSpectrum = 1;

    SP->zoom.zoomPosition = SP->editorInstrument->endPoint;
    SP->zoom.lastChangedPoint = 2;
    SP->refreshPoints = 1;

    newFileManager.setInstrumentStructChanged(mtProject.values.lastUsedInstrument);

    SP->showEndPointValue();
}

void cSamplePlayback::setDefaultZoom()
{
    GP.spectrumChangeZoom(MIN_SIGNED_16BIT, SP->editorInstrument->sample.length, &SP->zoom);

    SP->refreshSpectrum = 1;
    SP->refreshPoints = 1;
    SP->refreshSlicePoints = 1;

    SP->showZoomValue();
}

void cSamplePlayback::setDefaultSelectedSlice()
{
    SP->editorInstrument->selectedSlice = defaultInstrumentParams.selectedSlice;

    if ((SP->editorInstrument->playMode == playModeSlice) ||
        (SP->editorInstrument->playMode == playModeBeatSlice))
    {
        SP->zoom.zoomPosition =
          (SP->editorInstrument->sliceNumber > 0)
            ? SP->editorInstrument->slices[SP->editorInstrument->selectedSlice]
            : 0;
        if ((SP->zoom.zoomPosition > SP->zoom.zoomEnd) ||
            (SP->zoom.zoomPosition < SP->zoom.zoomStart))
            SP->refreshSpectrum = 1;
    }

    newFileManager.setInstrumentStructChanged(mtProject.values.lastUsedInstrument);
    SP->showSlicesSelectValue();
    SP->showSlicesAdjustValue();
    SP->refreshSlicePoints = 1;
}

void cSamplePlayback::setDefaultEqualSliceNumber()
{
    mtConfig.common.equalSliceNumber = 8;
    showSlicesEqualNumberValue();

    saveConfig();
}

void cSamplePlayback::setDefaultWavetablePosition()
{
    SP->editorInstrument->wavetableCurrentWindow = defaultInstrumentParams.wavetableCurrentWindow;

    for (uint8_t i = 0; i < 8; i++)
    {
        instrumentPlayer[i].setRefreshBit(WT_POS_SEND_MASK);
    }

    SP->currentEnvelopeWtPos = SP->editorInstrument->wavetableCurrentWindow;
    SP->processWavetableCursor(SP->editorInstrument->wavetableCurrentWindow);
    SP->showWavetablePosition();
    SP->refreshSpectrum = 1;

    newFileManager.setInstrumentStructChanged(mtProject.values.lastUsedInstrument);
}

void cSamplePlayback::setDefaultWavetableWindow()
{
    if (SP->isPlayingWavetable)
    {
        mtPadBoard.cutAllInstrument();
        SP->isPlayingWavetable = 0;
    }

    uint8_t lastWavetableWindowsCounter = SP->wavetableWindowsCounter;
    int8_t localDif;
    uint8_t localMaxWavetableWindowsCounter = MAX_WAVETABLE_WINDOWS_COUNTER;

    uint16_t tmpWindowSize = 2048;

    while ((tmpWindowSize > SP->editorInstrument->sample.length) &&
           (localMaxWavetableWindowsCounter > MIN_WAVETABLE_WINDOWS_COUNTER))
    {
        tmpWindowSize >>= 1;
        localMaxWavetableWindowsCounter--;
    }

    SP->wavetableWindowsCounter = localMaxWavetableWindowsCounter;

    localDif = lastWavetableWindowsCounter - SP->wavetableWindowsCounter;

    if (localDif > 0)
        SP->editorInstrument->wavetableCurrentWindow <<= localDif;
    else if (localDif < 0)
        SP->editorInstrument->wavetableCurrentWindow >>= (-localDif);

    if (localDif != 0)
        SP->showWavetablePosition();

    SP->editorInstrument->sample.wavetable_window_size =
      SP->convertWavetableWindowsCounterToSize(SP->wavetableWindowsCounter);
    SP->editorInstrument->sample.wavetableWindowNumber =
      SP->editorInstrument->sample.wavetable_window_size
        ? SP->editorInstrument->sample.length / SP->editorInstrument->sample.wavetable_window_size
        : 0;

    if (SP->editorInstrument->wavetableCurrentWindow >=
        SP->editorInstrument->sample.wavetableWindowNumber)
    {
        SP->editorInstrument->wavetableCurrentWindow =
          SP->editorInstrument->sample.wavetableWindowNumber - 1;
        SP->showWavetablePosition();
    }

    SP->processWavetableCursor(SP->editorInstrument->wavetableCurrentWindow);
    SP->showWavetableWindowSize();
    SP->refreshSpectrum = 1;

    newFileManager.setInstrumentStructChanged(mtProject.values.lastUsedInstrument);
}

void cSamplePlayback::setDefaultGranularPosition()
{
    SP->editorInstrument->granular.currentPosition =
      defaultInstrumentParams.granular.currentPosition;
    SP->currentEnvelopeGranPos = SP->isPlayingSample
                                   ? instrumentPlayer[0].getEnvelopeGranPosMod()
                                   : SP->editorInstrument->granular.currentPosition;
    SP->showGranularPositionValue();
    for (uint8_t i = 0; i < 8; i++)
    {
        instrumentPlayer[i].setRefreshBit(GRANULAR_POS_SEND_MASK);
    }

    newFileManager.setInstrumentStructChanged(mtProject.values.lastUsedInstrument);
}

void cSamplePlayback::setDefaultGranularLength()
{
    SP->editorInstrument->granular.grainLength = defaultInstrumentParams.granular.grainLength;
    SP->currentEnvelopeGranLen = SP->isPlayingSample
                                   ? instrumentPlayer[0].getEnvelopeGranLenMod()
                                   : SP->editorInstrument->granular.grainLength;

    SP->showGrainLengthValue();

    for (uint8_t i = 0; i < 8; i++)
    {
        instrumentPlayer[i].setRefreshBit(GRANULAR_LEN_SEND_MASK);
    }

    newFileManager.setInstrumentStructChanged(mtProject.values.lastUsedInstrument);
}

void cSamplePlayback::setDefaultGranularShape()
{
    SP->editorInstrument->granular.shape = defaultInstrumentParams.granular.shape;
    SP->showShapeText();
    for (uint8_t i = 0; i < 8; i++)
    {
        instrumentPlayer[i].setRefreshBit(GRANULAR_WAVE_SEND_MASK);
    }
    newFileManager.setInstrumentStructChanged(mtProject.values.lastUsedInstrument);
}

void cSamplePlayback::setDefaultGranularLoop()
{
    SP->editorInstrument->granular.type = defaultInstrumentParams.granular.type;
    SP->showLoopTypeText();
    for (uint8_t i = 0; i < 8; i++)
    {
        instrumentPlayer[i].setRefreshBit(GRANULAR_LOOP_SEND_MASK);
    }
    newFileManager.setInstrumentStructChanged(mtProject.values.lastUsedInstrument);
}

bool cSamplePlayback::slice2ndScreenActive()
{
    return (((SP->editorInstrument->playMode == playModeSlice) ||
             (SP->editorInstrument->playMode == playModeBeatSlice)) &&
            (SP->sliceScreenMode2));
}

static void functInstrument(uint8_t state)
{
    if (state == buttonRelease)
    {
        SP->cancelPopups();
        SP->lightUpPadBoard();

        if (mtPopups.isNoteKeyboardPopupActive())
        {
            mtPopups.toggleNoteKeyboardPopup(InterfacePopups::noteKeyboardOption::kForceShow);
        }
    }
    else if (state == buttonPress)
    {
        SP->hideUselessControls();
        mtPopups.showStepPopup(stepPopupInstr, mtProject.values.lastUsedInstrument);
    }
}

static void functInsert()
{
    if (SP->editorInstrument->playMode == playModeSlice ||
        SP->editorInstrument->playMode == playModeBeatSlice)

    {
        if (!SP->sliceScreenMode2)
        {
            functAddSlice();
        }
        return;
    }
}

static void functShift(uint8_t state)
{
    if (state == buttonPress)
    {
        if (mtPopups.isNoteKeyboardPopupActive())
        {
            mtPopups.toggleNoteKeyboardPopup(InterfacePopups::noteKeyboardOption::kShowEditButtons);
        }
    }
    else if (state == buttonRelease)
    {
        if (mtPopups.isNoteKeyboardPopupActive())
        {
            mtPopups.toggleNoteKeyboardPopup(InterfacePopups::noteKeyboardOption::kForceShow);
        }
    }
}

static void functDelete()
{
    if (SP->editorInstrument->playMode == playModeSlice ||
        SP->editorInstrument->playMode == playModeBeatSlice)

    {
        if (SP->selectedPlace < 2 && !SP->sliceScreenMode2)
        {
            functRemoveSlice();
            return;
        }
        else if (SP->sliceScreenMode2)
        {
            SP->setDefault(SP->selectedPlace);
            return;
        }
    }

    if (SP->frameData.multiSelActiveNum)
    {
        for (uint8_t i = 1; i < 5; i++)
        {
            if (SP->frameData.multisel[i].isActive)
            {
                SP->setDefault(i);
            }
        }
        // Jezeli endPoint i loopPoint2 sa razem zaznaczone to najpierw musi sie wykonac
        // endPoint potem loopPoint2
        if (SP->frameData.multisel[3].isActive && SP->frameData.multisel[4].isActive)
        {
            SP->setDefault(3);
        }
    }
    else
    {
        SP->setDefault(SP->selectedPlace);
    }
}

static void functStepNote(uint8_t value)
{
    if (SP->slice2ndScreenActive())
    {
        return;
    }
#ifdef TRACKER_MINI
    if (value == buttonPress)
    {
        if (SP->buttons->isButtonPressed(interfaceButtonShift))
        {
            bool popupShown = mtPopups.toggleNoteKeyboardPopup();
            if (!popupShown)
            {
                SP->start(0);
            }
        }
        else if (mtPopups.isNoteKeyboardPopupActive())
        {
            int padIdx = mtPopups.getNoteKeyboardFirstPadIdx();
            mtPopups.showStepPopup(stepPopupNote, padIdx);
        }
    }
    else if (value == buttonRelease)
    {
        if (mtPopups.getStepPopupState() != stepPopupNone)
        {
            SP->cancelPopups();

            if (mtPopups.isNoteKeyboardPopupActive())
            {
                mtPopups.toggleNoteKeyboardPopup(InterfacePopups::noteKeyboardOption::kForceShow);
            }
        }
    }
#else
    if (value == buttonRelease)
    {
        SP->cancelPopups();
    }
    else if (value == buttonPress)
    {
        mtPopups.showStepPopup(stepPopupNote, -1);
    }
#endif
}

/*/////////// MultiSelect Functions ////////////////*/
void cSamplePlayback::addNode(editFunct_t funct, uint8_t nodeNum)
{
    if (selectNodes[nodeNum].isActive == 0)
    {
        selectNodes[nodeNum].isActive = 1;
        selectNodes[nodeNum].editFunct = funct;
    }
}

void cSamplePlayback::removeNode(uint8_t nodeNum)
{
    selectNodes[nodeNum].isActive = 0;
    selectNodes[nodeNum].editFunct = NULL;
}

void cSamplePlayback::stepThroughNodes(int16_t value)
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

void cSamplePlayback::clearAllNodes()
{
    for (uint8_t node = 0; node < MAX_SELECT_NODES; node++)
    {
        selectNodes[node].isActive = 0;
        selectNodes[node].editFunct = NULL;
    }
}

void cSamplePlayback::cancelMultiFrame()
{
    for (uint8_t i = 0; i < MAX_SELECT_NODES; i++)
    {
        SP->frameData.multisel[i].isActive = 0;
    }

    SP->frameData.multiSelActiveNum = 0;
}

uint16_t cSamplePlayback::convertWavetableWindowsCounterToSize(uint8_t cnt)
{
    return (1 << cnt);
}

uint8_t cSamplePlayback::convertWavetableWindowsSizeToCounter(uint16_t size)
{
    if (!size)
    {
        return 0;
    }

    uint8_t counter = 0;

    while (size != 1)
    {
        size >>= 1;
        counter++;
    }

    return counter;
}

void cSamplePlayback::onControlChangeReceive(int param, int value)
{
    extern cInstrumentEditor instrumentEditor;
    if (mtProject.values.lastUsedInstrument >= kSampleInstrumentsCount)
    {
        if (instrumentEditor.instListPos < 0)
        {
            instrumentEditor.instListPos = 0;
        }
        else if (instrumentEditor.instListPos >= kSampleInstrumentsCount)
        {
            instrumentEditor.instListPos = 0;
        }

        editorInstrument = &mtProject.instrument[instrumentEditor.instListPos];
    }
    else
    {
        editorInstrument = &mtProject.instrument[mtProject.values.lastUsedInstrument];
    }

    changeControlChangeParam((ParamsCC)param, getControlChangeDelta((ParamsCC)param, value));
}

int cSamplePlayback::getControlChangeDelta(ParamsCC param, int cc_value)
{
    switch (param)
    {
        case kWavetablePosition_GranularPosition:
            if (editorInstrument->playMode == instrumentPlayMode::playModeWavetable)
            {
                int destValue = mapClamped<int>(
                  cc_value, 0, 127, 0, SP->editorInstrument->sample.wavetableWindowNumber - 1);

                return destValue - (int)SP->editorInstrument->wavetableCurrentWindow;
            }
            else if (editorInstrument->playMode == instrumentPlayMode::playModeGranular)
            {
                int destValue = mapClamped(cc_value, 0, 127, 0, (int)MAX_16BIT);

                return (
                  int)((float)(destValue - (int)SP->editorInstrument->granular.currentPosition) /
                       (float)(MAX_16BIT / 1000));
            }
            break;
        case kWavetableWindow_GranularPositionLength:
            if (editorInstrument->playMode == instrumentPlayMode::playModeWavetable)
            {
                uint8_t windowSizeIdxMax = MAX_WAVETABLE_WINDOWS_COUNTER;
                uint16_t tempWindowSize = 2048;

                while ((tempWindowSize > SP->editorInstrument->sample.length) &&
                       (windowSizeIdxMax > MIN_WAVETABLE_WINDOWS_COUNTER))
                {
                    tempWindowSize >>= 1;
                    windowSizeIdxMax--;
                }

                int destValue = mapClamped<int>(
                  cc_value, 0, 127, MIN_WAVETABLE_WINDOWS_COUNTER, windowSizeIdxMax);

                int windowSizeIdx = SP->convertWavetableWindowsSizeToCounter(
                  SP->editorInstrument->sample.wavetable_window_size);

                return destValue - windowSizeIdx;
            }
            else if (editorInstrument->playMode == instrumentPlayMode::playModeGranular)
            {
                int32_t grainConstrain = SP->editorInstrument->sample.length < MAX_GRANULAR_LENGTH
                                           ? SP->editorInstrument->sample.length
                                           : MAX_GRANULAR_LENGTH;
                int destValue =
                  mapClamped<int>(cc_value, 0, 127, MIN_GRANULAR_LENGTH, grainConstrain);
                int delta = destValue - (int)SP->editorInstrument->granular.grainLength;

                return delta / (MIN_GRANULAR_LENGTH / 4);
            }
            break;
        case kGranularShape:
            if (editorInstrument->playMode == instrumentPlayMode::playModeGranular)
            {
                int destValue = mapClamped(cc_value, 0, 127, 0, granularTypeCount - 1);

                return destValue - (int)SP->editorInstrument->granular.shape;
            }
            break;
        case kGranularLoop:
            if (editorInstrument->playMode == instrumentPlayMode::playModeGranular)
            {
                int destValue = mapClamped(cc_value, 0, 127, 0, granularLoopCount - 1);

                return destValue - (int)SP->editorInstrument->granular.type;
            }
            break;
        default: return 0;
    }
    return 0;
}

void cSamplePlayback::changeControlChangeParam(ParamsCC param, int delta)
{
    switch (param)
    {
        case kWavetablePosition_GranularPosition:
            if (editorInstrument->playMode == instrumentPlayMode::playModeWavetable)
            {
                modWavetablePostion(delta);
            }
            else if (editorInstrument->playMode == instrumentPlayMode::playModeGranular)
            {
                modGranularPosition(delta);
            }
            break;
        case kWavetableWindow_GranularPositionLength:
            if (editorInstrument->playMode == instrumentPlayMode::playModeWavetable)
            {
                modWavetableWindowSize(delta);
            }
            else if (editorInstrument->playMode == instrumentPlayMode::playModeGranular)
            {
                modGranularLength(delta);
            }
            break;
        case kGranularShape:
            if (editorInstrument->playMode == instrumentPlayMode::playModeGranular)
            {
                modGranularShape(delta);
            }
            break;
        case kGranularLoop:
            if (editorInstrument->playMode == instrumentPlayMode::playModeGranular)
            {
                modGranularLoopType(delta);
            }
            break;
        default: break;
    }
}

///////////////////////////////////////////////////////////////////////////
static void functMenu(uint8_t state)
{
    if (state == buttonPress)
    {
        SP->hideUselessControls();
        mtMenuPopup.showModulePopup(SP);
    }
    else if (state == buttonRelease)
    {
        mtMenuPopup.hideModulePopup(SP);
    }
}
