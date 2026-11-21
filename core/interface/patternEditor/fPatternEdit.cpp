#include "mtStructs.h"
#include "patternEditor/patternEditor.h"
#include <assert.h>
#include <limits.h>
#include <mtAudioExport.h>

#include "fileManager.h"
#include "mtAudioEngine.h"
#include "mtPadBoard.h"
#include "mtPadsBacklight.h"
#include "mtSequencer.h"

#include "core/interfaceDefs.h"
#include "core/interfaceMenuPopup.h"
#include "core/interfacePopups.h"
#include "core/songTimer.h"

#include "Platform.h"
#include "TrackerImpl.h"
#include "log.h"

#include <cr_section_macros.h>

strTrackerPattern cPatternEditor::trackerPattern;
cPatternEditor::strFill cPatternEditor::fillData[6];

cPatternEditor patternEditor;
static cPatternEditor* PTE = &patternEditor;

extern strMtProject mtProject;

// pod ekranem
static void functChangePattern(uint8_t state);
static void functChangePatternLength(uint8_t state);
static void functChangePatternEditStep(uint8_t state);
static void functFill();
static void functPreview();
static void functUndo(uint8_t state);
static void functRedo(uint8_t state);

static void functExportSelection();
static void functInvert();
static void functDuplicatePattern();
static void functExpandPattern();
static void functShrinkPattern();
static void functCopyPattern();
static void functPastePattern();

static void functNamePattern();
static void functNameTrack();

static void functClockWarningPopupButtons(uint8_t button, uint8_t state);

static void functNextActionButtonPage();

// pozostale
static void functUndoRedo(uint8_t state, bool undo);
static void functUndoRedoOnLeftRight(int8_t dir);

// step buttons
static void functNote(uint8_t state);
static void functInstrument(uint8_t state);
static void functFx1(uint8_t state);
static void functFx2(uint8_t state);

// modules
static void functPattern(uint8_t state);

static void functSwitchModule(uint8_t button);

// fill
static void functFillCancel();
static void functFillApply();
static void functFillAction1();
static void functFillAction2();
static void functFillAction3();
static void functFillAction4();
static void functFillAction5();
static void functFillAction6();

// export selection
static void functAutoNameKeyboard();
static void functCancelKeyboard();
static void functConfirmExport();
static void functConfirmExportLoad();
static void functEnterKeyExport();
static void functLeftKeyboard();
static void functRightKeyboard();
static void functUpKeyboard();
static void functDownKeyboard();
static void functDeleteKeyboard();
static void functInsertKeyboard();

static void functOverwriteExportFile();
static void functCancelOverwriteExportFile();

static void functConfirmNamePattern();
static void functConfirmNameTrack();

static void functLeft();
static void functRight();
static void functUp();
static void functDown();

static void functPlayAction();
static void functRecAction(uint8_t);

static void functShift(uint8_t state);

static void functInsertHome(uint8_t state);
static void functCopyPaste(uint8_t state);
static void functDeleteBackspace(uint8_t state);

static void functMiniMiddle(uint8_t state);

static void functEncoder(int16_t value);

static void functPads(uint8_t pad, uint8_t state, int16_t velo);

static void functMenu(uint8_t state);

// muty
static void functMuteTrack(uint8_t button, uint8_t state);
static void functRecArmTrack(uint8_t button, uint8_t state);

//--------------------------------------------
static uint8_t getSelectedElement();

char getHexFromInt(int16_t val, uint8_t index);

enum moveDirection
{
    moveUp = 0,
    moveDown,
    moveRight,
    moveLeft
};
static const uint8_t arrangementDirection[3][4] = {
    { moveUp, moveDown, moveRight, moveLeft },
    { moveUp, moveDown, moveRight, moveLeft },
    { moveLeft, moveRight, moveDown, moveUp },
};

//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------
cPatternEditor::cPatternEditor()
{
    seq = nullptr;
    lastPatternPosition = 0;
}

void cPatternEditor::initExternalRamState()
{
    trackerPattern.init();
    memset(fillData, 0, sizeof(fillData));
    for (size_t i = 0; i < sizeof(fillData) / sizeof(fillData[0]); i++)
    {
        fillData[i].density[Sequencer::fillStepRandom] = Sequencer::MAX_FILL_DENSITY / 2;
        fillData[i].density[Sequencer::fillStepByStep] = 1;
        fillData[i].density[Sequencer::fillStepEuclidean] = 3;
        fillData[i].fillStep = Sequencer::fillStepByStep;
    }
    fillData[5].from = 50;
    fillData[5].to = 50;
}

elapsedMillis patternRefreshTimer;
elapsedMillis playheadBlinkTimer;

void cPatternEditor::update()
{
    // zarzadznie renderowaniem zaznaczenia ---------------------------
    uint8_t managerStatus = newFileManager.getStatus();

    if (managerStatus == fmExportSoundEnd)
    {
        newFileManager.clearStatus();

        endExportSelection();

        interfaceGlobals.refreshFileExplorer = true;
    }
    else if (managerStatus == fmImportSamplesEnd)
    {
        ip->unblockAllInputs();
        newFileManager.clearStatus();

        interfaceGlobals.refreshFileExplorer = true;

        uint8_t button = interfaceButtonSampleLoad;
        PTE->firstFreeInstrumentSlotFound++;
        // The handler reads the options through a uint32_t pointer.
        uint32_t slot = static_cast<uint8_t>(PTE->firstFreeInstrumentSlotFound);
        PTE->eventFunct(eventSwitchModule, PTE, &button, &slot);
    }
    else if (managerStatus >= fmLoadEnd && managerStatus < fmError) // 	lapie cala reszte Endow
    {
        ip->unblockAllInputs();
        newFileManager.clearStatus();
        start(0);
    }
    else if (managerStatus >= fmError) // a tu wszelakie errory
    {
        ip->unblockAllInputs();
        newFileManager.clearStatus();
        start(0);
    }

    // cala reszta  wymagajaca odwiezania -----------------------------------
    if (patternRefreshTimer < 20)
    {
        return;
    }

    patternRefreshTimer = 0;

    if (refreshAfterReleaseFx == true)
    {
        refreshAfterReleaseFx = false;
        lightUpPadBoard();
    }

    // opozniania pokazywania step popupow
    for (uint8_t i = 0; i < 4; i++)
    {
        if (mtPopups.getStepPopupState() == stepPopupNone && PTE->stepButtonsState[i] == 1 &&
            (i == 0 ? stepButtonsTimer[i] > kStepNotePopupShowDelay
                    : stepButtonsTimer[i] > kStepPopupShowDelay))
        {
            switch (i)
            {
                case 0:
                {
#ifdef TRACKER_MINI
                    // just do nothing
#else
                    showNotePopup();
#endif
                }
                break;
                case 1: showInstrumentPopup(); break;
                case 2: showFx1Popup(); break;
                case 3: showFx2Popup(); break;
            }
        }
    }

    showPattern(0, buttons->isButtonPressed(interfaceButtonShift));

    readPatternState();

    if (sequencer.isStop())
    {
        // jesli ostatnio bylo odtwarzane zaznaczenie to cofnij playhed na 0
        if (trackerPattern.playheadStartTrack > 0 || trackerPattern.playheadEndTrack < 7)
        {
            trackerPattern.playheadStartTrack = 0;
            trackerPattern.playheadEndTrack = 7;
            trackerPattern.playheadPosition = 0;
            refreshPattern();
        }
        return;
    }

    showLength();

    if (!refreshPlayheadOnScreen() && editMode)
    {
        return;
    }

    if (mtPopups.getStepPopupState() != stepPopupNone)
    {
        return;
    }

    if (sequencer.isPlayingSelectionLooped() && sequencer.isLoopSelectionSingleRow())
    {
        if (playheadBlinkTimer > 500)
        {
            playheadBlinkTimer = 0;
            trackerPattern.showPlayhead = !trackerPattern.showPlayhead;
        }
        else
        {
            return;
        }
    }
    else
    {
        if (trackerPattern.showPlayhead == true &&
            trackerPattern.playheadPosition == lastPatternPosition)
        {
            return;
        }
        trackerPattern.showPlayhead = true;
    }

    if (!sequencer.isStop())
    {
        lightUpPadBoard();
    }

    refreshPattern();
    lastPatternPosition = trackerPattern.playheadPosition;
}

void cPatternEditor::start(uint32_t options)
{
    (void)options;

    moduleRefresh = 1;
    patternButtonReleaseActive = 0;

    undoRedoButtonReleaseBlocker = 1;

    if (fillState)
    {
        fillState = 0;
        PTE->hideFillPopup();
    }

    trackerPattern.stepDevider = (mtProject.values.patternDiv + 1);

    trackerPattern.showTrackNames = true;
    for (int nameIdx = 0; nameIdx < 16; nameIdx++)
    {
        trackerPattern.tracksNames[nameIdx] =
          nameIdx < kSampleTracks ? &mtProject.values.TrackNames[nameIdx][0]
                                  : &mtProject.values.midiTrackNames[nameIdx - kSampleTracks][0];
    }

    switch (mtConfig.general.patternInfo)
    {
        case 0: songTimer.show(); break;
        case 1: songTimer.hide(); break;
        default:
            trackerPattern.showTrackNames = false;
            songTimer.hide();
            break;
    }

    readPatternState();
    refreshPattern();

    setSwitchModuleFunct();

    showDefaultScreen();
    setDefaultScreenFunct();

    refreshEditState();

    focusOnPattern();
}

void cPatternEditor::stop()
{
    if (mtPopups.isNoteKeyboardPopupActive())
    {
        mtPopups.toggleNoteKeyboardPopup();
    }

    refreshLastUsedInstrument();

    undoRedoOpen = 0; // na wszelki

    if (sequencer.isPlayingSelectionLooped())
    {
        sequencer.stop();
    }

    sequencer.stopManualNotes();
    sequencer.sequencialSwitch_Reset();
    sequencer.setOnGlobalStep(nullCallback);

    newFileManager.autosaveProjectStruct_ASAP();

    if (fillState)
    {
        fillState = 0;
    }

    padsBacklight.clearGlobalPads(1, 1, 1);

    mtPopups.hideInfoPopup();
    mtPopups.hideProgressPopup();

    moduleRefresh = 0;
}

void cPatternEditor::hideControlsUnderMiniMenu()
{
    int trackStart = trackerPattern.firstVisibleTrack + 1;
    int trackEnd = 3;

    if (patternViewMode > 0)
    {
        trackStart = 2;
        trackEnd = 6;
    }

    for (; trackStart < trackEnd; trackStart++)
    {
        for (int row = 2; row < 14; row++)
        {
            trackerPattern.track[trackStart].row[row].note[0] = 0;
            trackerPattern.track[trackStart].row[row].instr[0] = 0;
            trackerPattern.track[trackStart].row[row].fx[0][0] = 0;
            trackerPattern.track[trackStart].row[row].fx[1][0] = 0;
        }
    }
    display.refreshControl(patternControl);
}

void cPatternEditor::setDefaultScreenFunct()
{
    // funkcje
    ip->clearAllPots();

    ip->setButtonObj(interfaceButtonPlay, buttonPress, functPlayAction);
    ip->setButtonObj(interfaceButtonCopy, functCopyPaste);
    ip->setButtonObj(interfaceButtonInsert, functInsertHome);
    ip->setButtonObj(interfaceButtonDelete, functDeleteBackspace);

    ip->setButtonObj(interfaceButtonRec, functRecAction);
    ip->setButtonObj(interfaceButtonShift, functShift);

    ip->setButtonObj(interfaceButtonLeft, buttonPress, functLeft);
    ip->setButtonObj(interfaceButtonRight, buttonPress, functRight);
    ip->setButtonObj(interfaceButtonUp, buttonPress, functUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functDown);

    ip->setButtonObj(interfaceButtonNote, functNote);
    ip->setButtonObj(interfaceButtonInstr, functInstrument);
    // ip->setButtonObj(interfaceButtonVol, functVolume);
    ip->setButtonObj(interfaceButtonVol, functFx1);
    ip->setButtonObj(interfaceButtonFx, functFx2);

    ip->setButtonObj(interfaceButtonPattern, functPattern);

    //	ip->setButtonObj(interfaceButton6, functRamTest);
    //	ip->setButtonObj(interfaceButton2, functChangePatternEditStep);

    // ip->setButtonObj(interfaceButton4, buttonPress, functFill);
    // ip->setButtonObj(interfaceButton5, buttonPress, functRandom);
    // ip->setButtonObj(interfaceButton6, buttonPress, functInvert);
    // ip->setButtonObj(interfaceButton7, buttonPress, functUndo);

    ip->setPotObj(interfacePot0, functEncoder);

    ip->setButtonObj(interfaceButtonMiniMiddle, functMiniMiddle);

    ip->setPadsGlobal(functPads);
}

void cPatternEditor::setSwitchModuleFunct()
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
    ip->setButtonObj(interfaceButtonGame, buttonPress, functSwitchModule);

    ip->setButtonObj(interfaceButtonMiniMenu, functMenu);
}

//==============================================================================================================
// odswiezanie paternu do uzywania w zewnetrznych modulach typu midi
void cPatternEditor::refreshPatternFromOutsideModule()
{
    if (!moduleRefresh)
        return;

    if (sequencer.isRec())
        return;

    refreshPattern();
}

//==============================================================================================================
// odswiezanie trybu edycji do uzywania w zewnetrznych modulach typu seq
void cPatternEditor::refreshEditStateFromOutsideModule()
{
    if (!moduleRefresh)
        return;

    refreshEditState();
}

//==============================================================================================================
// przeniesienie danych z sekewncji  do struktury wyswietlania
void cPatternEditor::refreshPattern()
{
    seq = sequencer.getPatternToUI();
    trackerPattern.patternLength = seq->track[0].length + 1;
    trackerPattern.selectedParam = editParam;

    //----------------------------------------------------------------------------
    // disable part of the pattern when needed

    if (fillState == 0)
    {
        trackerPattern.popupMode &= ~(1);
    }
    if (mtPopups.getStepPopupState() == stepPopupNone)
    {
        trackerPattern.popupMode &= ~(2 | 4);
    }

    //----------------------------------------------------------------------------
    // handle selection and playhead

    if (editMode == 0) // green mode
    {
        if (sequencer.isPlayingSelectionLooped())
        {
            trackerPattern.actualStep = trackerPattern.playheadPosition;
            trackerPattern.selectState = 1;
        }
        else if (sequencer.isPlayingSelection())
        {
            trackerPattern.playheadStartTrack = trackerPattern.selectStartTrack;
            trackerPattern.playheadEndTrack = trackerPattern.selectEndTrack;
        }
        else if (sequencer.isPlay())
        {
            trackerPattern.selectState = 1;
            trackerPattern.playheadStartTrack = 0;
            trackerPattern.playheadEndTrack = 7;
            trackerPattern.actualStep = trackerPattern.playheadPosition;
        }
        else if (!sequencer.isPlay())
        {
            if (trackerPattern.actualStep > trackerPattern.patternLength - 1)
            {
                trackerPattern.actualStep = 0;
            }
        }
    }
    else // red mode
    {
        if (sequencer.isPlayingSelectionLooped())
        {
        }
        else if (sequencer.isPlayingSelection())
        {
            trackerPattern.playheadStartTrack = trackerPattern.selectStartTrack;
            trackerPattern.playheadEndTrack = trackerPattern.selectEndTrack;
        }
        else if (sequencer.isPlay())
        {
            trackerPattern.playheadStartTrack = 0;
            trackerPattern.playheadEndTrack = 7;
        }
        else if (!sequencer.isPlay())
        {
            if (trackerPattern.actualStep > trackerPattern.patternLength - 1)
            {
                trackerPattern.actualStep = 0;
            }
        }
    }

    //----------------------------------------------------------------------------
    // disable the selection if changes have been detected

    if (lastPatternNumber != mtProject.values.actualPattern ||
        lastPatternLength != seq->track[0].length)
    {
        lastPatternNumber = mtProject.values.actualPattern;
        lastPatternLength = seq->track[0].length;

        if (trackerPattern.selectState == 2) // konczenie i dezaktywacja zaznaczania
        {
            PTE->isSelectingNow = 0;
            PTE->trackerPattern.selectColumn = 0;
            PTE->trackerPattern.selectState = 1;
        }
    }

    //----------------------------------------------------------------------------
    // alternative arrangement calculations

    int16_t patternPosition = trackerPattern.actualStep;

    if (mtConfig.general.patternArrangement == 2)
    {
        if (trackerPattern.middleVisibleStep + 6 < trackerPattern.actualStep)
        {
            trackerPattern.middleVisibleStep = trackerPattern.actualStep - 6;
        }
        else if (trackerPattern.middleVisibleStep - 5 > trackerPattern.actualStep)
        {
            trackerPattern.middleVisibleStep = trackerPattern.actualStep + 5;
        }
        else if (editMode == 0 && sequencer.isPlay() && !sequencer.isPlayingSelection())
        {
            if (trackerPattern.actualStep > trackerPattern.middleVisibleStep &&
                trackerPattern.actualStep < seq->track[0].length - 5)
            {
                trackerPattern.middleVisibleStep = trackerPattern.actualStep;
            }
        }

        patternPosition = trackerPattern.middleVisibleStep;
    }
    else
    {
        trackerPattern.middleVisibleStep = trackerPattern.actualStep;
    }

    for (uint8_t trackIdx = 0; trackIdx < kAllTracks; trackIdx++)
    {
        // sprawdzanie mutow
        trackerPattern.inactive[trackIdx] = isSampleTrack(trackIdx)
                                              ? mtProject.values.trackMute[trackIdx]
                                              : mtProject.values.midiTrackMute[trackIdx % 8];

        for (uint8_t rowIdx = 0; rowIdx < 15; rowIdx++)
        {
            strTrackerPattern::strTracks::strRow& row = trackerPattern.track[trackIdx].row[rowIdx];

            int16_t seqStepIdx = patternPosition - 7 + rowIdx;

            bool emptyStep = false;

            if (seqStepIdx < 0)
            {
                if (mtConfig.general.patternArrangement == PatternArrangement::kVerticalRevolver)
                {
                    seqStepIdx = trackerPattern.patternLength +
                                 ((seqStepIdx + 1) % trackerPattern.patternLength) - 1;
                }
                else
                {
                    emptyStep = true;
                }
            }
            else if (seqStepIdx >= trackerPattern.patternLength)
            {
                if (mtConfig.general.patternArrangement == PatternArrangement::kVerticalRevolver)
                {
                    seqStepIdx = seqStepIdx % trackerPattern.patternLength;
                }
                else
                {
                    emptyStep = true;
                }
            }

            if (emptyStep)
            {
                row.note[0] = 0;
                row.note[1] = 0;
                row.note[2] = 0;
                row.note[3] = 0;
                row.note[4] = 0;
                row.instr[0] = 0;
                row.instr[1] = 0;
                row.instr[2] = 0;
                row.instr[3] = 0;
                row.fx[0][0] = 0;
                row.fx[0][1] = 0;
                row.fx[0][2] = 0;
                row.fx[0][3] = 0;
                row.fx[0][4] = 0;
                row.fx[1][0] = 0;
                row.fx[1][1] = 0;
                row.fx[1][2] = 0;
                row.fx[1][3] = 0;
                row.fx[1][4] = 0;
                continue;
            }

            bool showInstrument = false;
            Sequencer::strPattern::strTrack::strStep& seqStep =
              seq->track[trackIdx].step[seqStepIdx];

            if (seqStep.note >= 0)
            {
                const char* noteName =
                  (mtConfig.general.noteDisplayMode == NoteDisplayMode::kNoteDisplayTraditional)
                    ? mtNotesTraditional[seqStep.note]
                    : mtNotes[seqStep.note];
                row.note[0] = noteName[0];
                row.note[1] = noteName[1];
                row.note[2] = noteName[2];
                row.note[3] = noteName[3];
                row.note[4] = noteName[4];
                showInstrument = true;
            }
            else if (seqStep.note == Sequencer::STEP_NOTE_OFF_DEFAULT)
            {
                row.note[0] = 'O';
                row.note[1] = 'F';
                row.note[2] = 'F';
                row.note[3] = 0;
                showInstrument = false;
            }
            else if (seqStep.note == Sequencer::STEP_NOTE_OFF_CUT)
            {
                row.note[0] = 'C';
                row.note[1] = 'U';
                row.note[2] = 'T';
                row.note[3] = 0;
                showInstrument = false;
            }
            else if (seqStep.note == Sequencer::STEP_NOTE_OFF_FADE)
            {
                row.note[0] = 'F';
                row.note[1] = 'A';
                row.note[2] = 'D';
                row.note[3] = 0;
                showInstrument = false;
            }
            else // (seqStep.note == Sequencer::STEP_NOTE_EMPTY) or trash value
            {
                row.note[0] = '-';
                row.note[1] = '-';
                row.note[2] = '-';
                row.note[3] = 0;
                if (seqStep.instrument >= kSampleInstrumentsCount)
                {
                    showInstrument = true;
                }
            }

            //--------------------------------------------------------------------------------------------
            if (showInstrument)
            {
                if (seqStep.instrument >= SYNTH_INSTRUMENTS_START_IDX)
                {
                    row.instr[0] = 'S';
                    row.instr[1] = 49 + (seqStep.instrument - SYNTH_INSTRUMENTS_START_IDX);
                    row.instr[2] = 0;
                    row.instr[3] = 0;
                }
                else if (seqStep.instrument >= MIDI_INSTRUMENTS_START_IDX)
                {
                    char inst0 = (seqStep.instrument - kSampleInstrumentsCount + 1) / 10;
                    char inst1 = (seqStep.instrument - kSampleInstrumentsCount + 1) % 10;

                    row.instr[0] = 'M';
                    row.instr[1] = inst0 + 48;
                    row.instr[2] = inst1 + 48;
                    row.instr[3] = 0;
                }
                else // normal instr
                {
                    char inst0 = (seqStep.instrument + 1) / 10;
                    char inst1 = (seqStep.instrument + 1) % 10;

                    row.instr[0] = inst0 + 48;
                    row.instr[1] = inst1 + 48;
                    row.instr[2] = 0;
                    row.instr[3] = 0;
                }
            }
            else
            {
                row.instr[0] = '-';
                row.instr[1] = '-';
                row.instr[2] = 0;
                row.instr[3] = 0;
            }

            //--------------------------------------------------------------------------------------------

            if (isMicVolView)
            {
                if (seqStep.micromove >= 0)
                {
                    row.fx[0][0] = 'm';
                    sequencer.makeFxValLabelWithAlgin(&row.fx[0][1],
                                                      4,
                                                      sequencer.fx.FX_TYPE_MICROMOVE,
                                                      seqStep.micromove,
                                                      Sequencer::fxValLabelAlign_right);
                }
                else
                {
                    row.fx[0][0] = 'm';
                    row.fx[0][1] = '-';
                    row.fx[0][2] = '-';
                    row.fx[0][3] = '-';
                    row.fx[0][4] = 0;
                }

                if (seqStep.volume >= 0)
                {
                    row.fx[1][0] = 'V';
                    sequencer.makeFxValLabelWithAlgin(&row.fx[1][1],
                                                      4,
                                                      sequencer.fx.FX_TYPE_VOLUME,
                                                      seqStep.volume,
                                                      Sequencer::fxValLabelAlign_right);
                }
                else if (seqStep.volume == Sequencer::STEP_VELO_OFF)
                {
                    row.fx[1][0] = '!';
                    row.fx[1][1] = 'O';
                    row.fx[1][2] = 'F';
                    row.fx[1][3] = 'F';
                    row.fx[1][4] = 0;
                }
                else
                {
                    row.fx[1][0] = 'V';
                    row.fx[1][1] = '-';
                    row.fx[1][2] = '-';
                    row.fx[1][3] = '-';
                    row.fx[1][4] = 0;
                }
            }
            else
            {
                uint8_t type_temp = (seqStep.fx[1].type != 0) ? interfaceGlobals.fxIdToName(seqStep.fx[1].type) : 0;
                if (type_temp > 0 && type_temp < FX_COUNT)
                {
                    row.fx[0][0] = 0;
                    row.fx[0][1] = 0;
                    row.fx[0][2] = 0;

                    row.fx[0][0] = interfaceGlobals.fxNames[type_temp][0];

                    sequencer.makeFxValLabelForSeqStep(&row.fx[0][1], 4, 1, trackIdx, seqStepIdx);
                }
                else
                {
                    row.fx[0][0] = '-';
                    row.fx[0][1] = '-';
                    row.fx[0][2] = '-';
                    row.fx[0][3] = '-';
                }
                //--------------------------------------------------------------------------------------------
                type_temp = (seqStep.fx[0].type != 0) ? interfaceGlobals.fxIdToName(seqStep.fx[0].type) : 0;
                if (type_temp > 0 && type_temp < FX_COUNT)
                {
                    row.fx[1][0] = 0;
                    row.fx[1][1] = 0;
                    row.fx[1][2] = 0;

                    row.fx[1][0] = interfaceGlobals.fxNames[type_temp][0];

                    sequencer.makeFxValLabelForSeqStep(&row.fx[1][1], 4, 0, trackIdx, seqStepIdx);
                }
                else
                {
                    row.fx[1][0] = '-';
                    row.fx[1][1] = '-';
                    row.fx[1][2] = '-';
                    row.fx[1][3] = '-';
                }
            }
        }
    }

    if (mtMenuPopup.isPopupVisible())
    {
        hideControlsUnderMiniMenu();
        return;
    }

    display.refreshControl(patternControl);
}

void cPatternEditor::showFxInfo()
{
    showActualParamOnBar();

    // "wylaczone" przez wykomentowanie

    //	if(editParam < 2) return;
    //	if(editMode == 0) return;
    //
    //	debugLog.setMaxLineCount(1);
    //
    //	seq = sequencer.getPatternToUI();
    //	uint8_t type_temp  =
    // interfaceGlobals.fxIdToName(seq->track[trackerPattern.actualTrack].step[trackerPattern.actualStep].fx[1-(editParam-2)].type);
    //	if(type_temp > 0 && type_temp < FX_MAX)
    //	{
    //		debugLog.addLine(&interfaceGlobals.fxNames[type_temp][0]);
    //	}
}

char getHexFromInt(int16_t val, uint8_t index)
{

    int16_t retVal;
    if (index == 0)
    {
        retVal = val % 16;
    }
    else
    {
        retVal = val / (16 * index);
    }

    if (retVal < 10)
    {
        return retVal + 48;
    }
    else if (retVal < 16)
    {
        return retVal + 97 - 10;
    }
    else
    {
        return 0;
    }
}

// odczytywanie dlugosci i pozycji odtwarzania aktualnej sekewncji
void cPatternEditor::readPatternState()
{
    seq = sequencer.getPatternToUI();

    trackerPattern.patternLength = seq->track[0].length + 1;
    trackerPattern.playheadPosition = sequencer.getActualPos();
}

uint8_t cPatternEditor::getStepFx()
{
    uint8_t selectedFx = 0;

    uint8_t fx_index = PTE->editParam == EditFx1 ? 1 : 0;

    uint8_t fx_type = interfaceGlobals.fxIdToName(sequencer.getPatternToUI()
                                                    ->track[trackerPattern.actualTrack]
                                                    .step[trackerPattern.actualStep]
                                                    .fx[fx_index]
                                                    .type);

    if (fx_type < FX_COUNT - FX_COUNT_HIDDEN_FXes)
        selectedFx = fx_type;

    if (selectedFx == 0)
        selectedFx = getPreviousUsedFx();

    return selectedFx;
}

int8_t cPatternEditor::getStepVol()
{
    //	int8_t show_vol =
    // sequencer.getPatternToUI()->track[trackerPattern.actualTrack].step[trackerPattern.actualStep].velocity;
    int8_t show_vol = -1;

    if (show_vol < 0)
        return -1;

    return mapClamped<int>(show_vol, 0, 127, 0, 47);
}

// focusowanie trackow na kursorze, jesli jest poza ekranem
void cPatternEditor::focusOnActual()
{
    int columnCounts = patternViewMode > 0 ? 8 : 4;

    if (trackerPattern.actualTrack >= trackerPattern.firstVisibleTrack + columnCounts)
    {
        trackerPattern.firstVisibleTrack = trackerPattern.actualTrack - (columnCounts - 1);
    }
    else if (trackerPattern.actualTrack < trackerPattern.firstVisibleTrack)
    {
        trackerPattern.firstVisibleTrack = trackerPattern.actualTrack;
    }

    trackerPattern.firstVisibleTrack =
      clamp<int8_t>(trackerPattern.firstVisibleTrack, 0, kAllTracks - columnCounts);
}

void cPatternEditor::moveCursorByStep()
{
    moveCursorByStep(0);
}

void cPatternEditor::moveCursorByStep(uint8_t val)
{
    if (mtProject.values.patternEditStep + val <= 0)
        return;

    int16_t patternLength = sequencer.getPatternToUI()->track[0].length;

    if (trackerPattern.actualStep + mtProject.values.patternEditStep + val <= patternLength)
    {
        trackerPattern.actualStep += mtProject.values.patternEditStep + val;
    }
    else
    {
        // The move can be longer than the pattern, as after pasting a tall block into a
        // short pattern, so wrapping once is not enough.
        trackerPattern.actualStep =
          (trackerPattern.actualStep + mtProject.values.patternEditStep + val) %
          (patternLength + 1);
    }

    PTE->trackerPattern.selectState = 1;
    PTE->trackerPattern.selectColumn = 0;
    PTE->isSelectingNow = 0;
}

void cPatternEditor::moveCursorByStepParallel()
{
    if (!isMultiSelectionOnOneLine())
    {
        moveCursorByStep();
        return;
    }

    if (mtProject.values.patternEditStep <= 0)
        return;

    int16_t patternLength = sequencer.getPatternToUI()->track[0].length;

    if (trackerPattern.actualStep + mtProject.values.patternEditStep <= patternLength)
    {
        trackerPattern.actualStep += mtProject.values.patternEditStep;
    }
    else
    {
        trackerPattern.actualStep =
          (trackerPattern.actualStep + mtProject.values.patternEditStep) % (patternLength + 1);
    }
    PTE->trackerPattern.selectStartStep = trackerPattern.actualStep;
    PTE->trackerPattern.selectEndStep = trackerPattern.actualStep;

    PTE->trackerPattern.selectState = 2;
    PTE->trackerPattern.selectColumn = 0;
    PTE->isSelectingNow = 0;
}

void cPatternEditor::cancelPopups()
{
    uint8_t popup_type = mtPopups.getStepPopupState();
    if (popup_type != stepPopupNone)
    {
        if (PTE->editMode == 1 && !fillState && !insertOnPopupHideDisabled)
        {
            switch (popup_type)
            {
                case stepPopupNote:
                    if (!isMultiSelection())
                    {
                        sendSelection();
                        sequencer.setSelectionNote(mtProject.values.lastUsedNote);
                    }
                    break;
                case stepPopupFx:
                    if (!isMultiSelection())
                    {
                        uint8_t fx_index = PTE->editParam == EditFx1 ? 1 : 0;

                        // uint8_t fx_name = interfaceGlobals.fxIdToName(
                        //		sequencer.getPatternToUI()->track[trackerPattern.actualTrack].step[trackerPattern.actualStep].fx[fx_index].type);
                        // if (fx_name > 0 && fx_name < FX_COUNT-FX_COUNT_HIDDEN_FXes) // tylko
                        // jesli na stepie jest juz jakis fx
                        //{
                        sendSelection();
                        sequencer.setSelectionFxType(fx_index,
                                                     interfaceGlobals.fxIDs[getPreviousUsedFx()]);
                        // todo: dostęp do interfaceGlobals.fxIDs w ten sposób może być
                        // niebezpieczny, lepiej użyć fxIdToName i odwrotnej
                        //}
                    }
                    break;
                case stepPopupInstr:
                case stepPopupOnlyMidiInstr:
                case stepPopupOnlySynthAndMidiInstr:
                {
                    sendSelection();
                    sequencer.setSelectionInstrument(mtProject.values.lastUsedInstrument);
                }
                break;

                default: break;
            }
        }

        mtPopups.hideStepPopups();
        setDefaultScreenFunct();
        showDefaultScreen();
        refreshEditState();

        if (mtConfig.general.patternInfo == 0)
        {
            songTimer.show();
        }
        else
        {
            // schowaj timer w przypadku wychodzenia z note popup
            songTimer.hide();
        }

        // wylacz ukrywanie czesci patternu pod popupem
        PTE->trackerPattern.popupMode &= ~(2 | 4);

        if (fillState)
        {
            if (popup_type == stepPopupInstr || popup_type == stepPopupOnlyMidiInstr ||
                popup_type == stepPopupOnlySynthAndMidiInstr)
            {
                if (editParam == EditInstr)
                {
                    if (PTE->fillPlace == 2)
                        PTE->fillData[1].from = mtProject.values.lastUsedInstrument;
                    else if (PTE->fillPlace == 3)
                        PTE->fillData[1].to = mtProject.values.lastUsedInstrument;
                }
            }
            functFill();
        }

        showActualParamOnBar(true);
        refreshPattern();
    }
}

void cPatternEditor::setKeyboardExportFunctions()
{
    // funkcje
    ip->clearAllButtons();

    ip->setButtonObj(interfaceButtonLeft, buttonPress, functLeftKeyboard);
    ip->setButtonObj(interfaceButtonRight, buttonPress, functRightKeyboard);
    ip->setButtonObj(interfaceButtonUp, buttonPress, functUpKeyboard);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functDownKeyboard);

    ip->setButtonObj(interfaceButton0, buttonPress, functEnterKeyExport);
    ip->setButtonObj(interfaceButton1, buttonPress, functAutoNameKeyboard);
    ip->setButtonObj(interfaceButton5, buttonPress, functCancelKeyboard);
    ip->setButtonObj(interfaceButton6, buttonPress, functConfirmExportLoad);
    ip->setButtonObj(interfaceButton7, buttonPress, functConfirmExport);

    ip->setButtonObj(interfaceButtonDelete, buttonPress, functDeleteKeyboard);
    ip->setButtonObj(interfaceButtonInsert, buttonPress, functInsertKeyboard);

    ip->setButtonObj(interfaceButtonMiniMiddle, buttonPress, functEnterKeyExport);
}

void cPatternEditor::setKeyboardNamePatternFunctions(void (*confirmFunct)(void),
                                                     bool autoNameEnabled)
{
    // funkcje
    ip->clearAllButtons();
    ip->clearAllPots();

    ip->setButtonObj(interfaceButtonLeft, buttonPress, functLeftKeyboard);
    ip->setButtonObj(interfaceButtonRight, buttonPress, functRightKeyboard);
    ip->setButtonObj(interfaceButtonUp, buttonPress, functUpKeyboard);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functDownKeyboard);

    ip->setButtonObj(interfaceButton0, buttonPress, functEnterKeyExport);

    if (autoNameEnabled)
    {
        ip->setButtonObj(interfaceButton1, buttonPress, functAutoNameKeyboard);
    }

    ip->setButtonObj(interfaceButton6, buttonPress, functCancelKeyboard);
    ip->setButtonObj(interfaceButton7, buttonPress, confirmFunct);

    ip->setButtonObj(interfaceButtonDelete, buttonPress, functDeleteKeyboard);
    ip->setButtonObj(interfaceButtonInsert, buttonPress, functInsertKeyboard);

    ip->setButtonObj(interfaceButtonMiniMiddle, buttonPress, functEnterKeyExport);
}

void cPatternEditor::setOverwriteRenderFunct()
{
    ip->clearButton(interfaceButton6);
    ip->clearButton(interfaceButton7);

    ip->setButtonObj(interfaceButton6, buttonPress, functCancelOverwriteExportFile);
    ip->setButtonObj(interfaceButton7, buttonPress, functOverwriteExportFile);

    ip->blockAllInputsExcept(interfaceButton6, interfaceButton7);
}

// sprawdza czy w danym momencie playhead sekwencji jest na ktoryms ze stepow wyswietlanych na
// ekranie
uint8_t cPatternEditor::refreshPlayheadOnScreen()
{
    if (lastPatternPosition == trackerPattern.patternLength - 1)
        return 1;
    if (lastPatternPosition == trackerPattern.patternLength)
        return 1;

    if (mtConfig.general.patternArrangement == PatternArrangement::kVerticalRevolver) // Revolver
    {
        if ((trackerPattern.playheadPosition > trackerPattern.middleVisibleStep - 6 &&
             trackerPattern.playheadPosition <= trackerPattern.middleVisibleStep + 7) ||
            (trackerPattern.playheadPosition >
             trackerPattern.patternLength - 6 + trackerPattern.middleVisibleStep) ||
            (trackerPattern.playheadPosition <=
             trackerPattern.patternLength + 7 - trackerPattern.middleVisibleStep))
            return 1;
    }
    else if (mtConfig.general.patternArrangement ==
             PatternArrangement::kVerticalOriginal) // Original
    {
        if ((trackerPattern.playheadPosition > trackerPattern.middleVisibleStep - 6 &&
             trackerPattern.playheadPosition <= trackerPattern.middleVisibleStep + 8))
            return 1;
    }
    else
    {
        if ((trackerPattern.playheadPosition > trackerPattern.middleVisibleStep - 6 &&
             trackerPattern.playheadPosition <= trackerPattern.middleVisibleStep + 7))
            return 1;
    }

    return 0;
}

// void cPatternEditor::changeActualTempo(int16_t value)
//{
//	Sequencer::strPattern * pattern = sequencer.getPatternToUI();
//
//	if(pattern->tempo+value < 10) pattern->tempo = 10;
//	else if(pattern->tempo+value > 1000) pattern->tempo = 400;
//	else  pattern->tempo += value;
//
////	showTempo();
//}

void cPatternEditor::changeActualPattern(int16_t value)
{
    // Uwaga: przy value == 0 nie powinny byc wykonywane zadne zmiany na kontrolkach UI
    // bo ta metoda jest wywolywana rowniez z Song Editora podczas wprowadzania zmian.
    sequencer.sequencialSwitch_Reset();
    sequencer.setOnGlobalStep(nullCallback);

    if (value && sequencer.getSeqState() != sequencer.SEQ_STATE_PLAY_PERFORMANCE)
    {
        newFileManager.saveWorkspacePatternNow(mtProject.values.actualPattern);
    }

    mtProject.values.actualPattern =
      clamp<int>(mtProject.values.actualPattern + value, PATTERN_INDEX_MIN, PATTERN_INDEX_MAX);

    newFileManager.loadWorkspacePatternNow(mtProject.values.actualPattern);
    sequencer.switchRamPatternsNow();

    if (value)
    {
        readPatternState();
        refreshPatternParams();
        refreshPattern();

        if (selectedPlace == 0)
        {
            loadPatternsStatus();
            lightUpPadBoard();
        }
    }
}

void cPatternEditor::setActualPattern(int16_t value)
{
    if (sequencer.getSeqState() != sequencer.SEQ_STATE_PLAY_PERFORMANCE)
    {
        newFileManager.saveWorkspacePatternNow(mtProject.values.actualPattern);
    }

    mtProject.values.actualPattern = clamp<int>(value, PATTERN_INDEX_MIN, PATTERN_INDEX_MAX);

    newFileManager.loadWorkspacePatternNow(mtProject.values.actualPattern);
    sequencer.switchRamPatternsNow();

    readPatternState();
    refreshPatternParams();
    refreshPattern();
}
const uint8_t changeLengthVals_elements = 11;
const uint8_t changeLengthVals[changeLengthVals_elements] = { 1,  2,  4,  8,  16, 32,
                                                              48, 64, 80, 96, 128 };
const uint8_t changeStepVals_elements = 7;
const uint8_t changeStepVals[changeLengthVals_elements] = { 0, 1, 2, 4, 8, 16, 32 };
void cPatternEditor::changeActualPatternLength(int16_t value)
{
    Sequencer::strPattern* pattern = sequencer.getPatternToUI();

    if (pattern->track[0].length + value < 0)
        pattern->track[0].length = 0;
    else if (pattern->track[0].length + value > Sequencer::MAXSTEP)
        pattern->track[0].length = Sequencer::MAXSTEP;
    else
        pattern->track[0].length += value;

    for (uint8_t i = 1; i < 8; i++)
        pattern->track[i].length = pattern->track[0].length;

    trackerPattern.patternLength = pattern->track[0].length + 1;

    if (trackerPattern.actualStep > trackerPattern.patternLength - 1)
        trackerPattern.actualStep = trackerPattern.patternLength - 1;

    showLength();

    refreshPattern();

    newFileManager.setActualPatternStructChanged();
}
void cPatternEditor::changeActualPatternLengthByDefinedValues(int16_t dir)
{
    Sequencer::strPattern* pattern = sequencer.getPatternToUI();

    if (dir > 0)
    {
        for (int8_t a = 0; a < changeLengthVals_elements; a++)
        {
            if (pattern->track[0].length < changeLengthVals[a] - 1)
            {
                pattern->track[0].length = changeLengthVals[a] - 1;
                break;
            }
        }
    }
    else if (dir < 0)
    {
        for (int8_t a = changeLengthVals_elements - 1; a >= 0; a--)
        {
            if (pattern->track[0].length > changeLengthVals[a] - 1)
            {
                pattern->track[0].length = changeLengthVals[a] - 1;
                break;
            }
        }
    }

    for (uint8_t i = 1; i < 8; i++)
        pattern->track[i].length = pattern->track[0].length;

    trackerPattern.patternLength = pattern->track[0].length + 1;

    if (trackerPattern.actualStep > trackerPattern.patternLength - 1)
        trackerPattern.actualStep = trackerPattern.patternLength - 1;

    showLength();

    refreshPattern();

    newFileManager.setActualPatternStructChanged();
}

void cPatternEditor::setActualPatternLength(int16_t value)
{
    Sequencer::strPattern* pattern = sequencer.getPatternToUI();

    pattern->track[0].length = clamp<int>(value, 0, Sequencer::MAXSTEP);

    for (uint8_t i = 1; i < 8; i++)
        pattern->track[i].length = pattern->track[0].length;

    trackerPattern.patternLength = pattern->track[0].length + 1;

    if (trackerPattern.actualStep > trackerPattern.patternLength - 1)
        trackerPattern.actualStep = trackerPattern.patternLength - 1;

    showLength();

    refreshPattern();

    newFileManager.setActualPatternStructChanged();
}

void cPatternEditor::changeActualPatternEditStep(int16_t value)
{
    mtProject.values.patternEditStep =
      clamp<int>(mtProject.values.patternEditStep + value, 0, PATTERN_EDIT_STEP_MAX);

    showStep();

    newFileManager.setProjectStructChanged();
}
void cPatternEditor::changeActualPatternEditStepByDefinedValues(int16_t value)
{

    if (value > 0)
    {
        for (int8_t a = 0; a < changeStepVals_elements; a++)
        {
            if (mtProject.values.patternEditStep < changeStepVals[a])
            {
                mtProject.values.patternEditStep = changeStepVals[a];
                break;
            }
        }
    }
    else if (value < 0)
    {
        for (int8_t a = changeStepVals_elements - 1; a >= 0; a--)
        {
            if (mtProject.values.patternEditStep > changeStepVals[a])
            {
                mtProject.values.patternEditStep = changeStepVals[a];
                break;
            }
        }
    }

    showStep();

    newFileManager.setProjectStructChanged();
}

void cPatternEditor::setActualPatternEditStep(int16_t value)
{

    mtProject.values.patternEditStep = clamp<int>(value, 0, PATTERN_EDIT_STEP_MAX);

    showStep();
}

//======================================================================================================================
static void functMuteTrack(uint8_t button, uint8_t state)
{
    if (state == buttonPress)
    {
        bool midiTrack = !isSampleTrack(PTE->trackerPattern.actualTrack);
        int trackIdx = midiTrack ? button + 8 : button;

        if (trackIdx >= kAllTracks)
        {
            return;
        }

        auto& mute =
          midiTrack ? mtProject.values.midiTrackMute[button] : mtProject.values.trackMute[button];

        mute = !mute;
        if (!midiTrack)
        {
            engine.muteTrack(button, mtProject.values.trackMute[button]);
        }
        PTE->trackerPattern.inactive[trackIdx] = mute;

        display.refreshControl(PTE->patternControl);

        PTE->showMuteButtons(1, midiTrack);

        PTE->setProjectSaveFlags();
    }
}

static void functRecArmTrack(uint8_t button, uint8_t state)
{
    if (state == buttonPress)
    {
        bool midiTrack = !isSampleTrack(PTE->trackerPattern.actualTrack);
        int trackIdx = midiTrack ? button + 8 : button;

        if (trackIdx >= kAllTracks)
        {
            return;
        }
        sequencer.toogleRecArmedOnTrack(trackIdx);

        display.refreshControl(PTE->patternControl);

        PTE->showRecordArmingButtons(1, midiTrack);
    }
}

void cPatternEditor::refreshEditState()
{
    if (editMode)
    {
        if (trackerPattern.selectState == 0)
        {
            trackerPattern.selectState = 1;
        }

        trackerPattern.selectedParam = editParam;

        focusOnActual();

        focusOnPattern();
    }
    else
    {
        if (trackerPattern.selectState == 0)
        {
            trackerPattern.selectState = 1;
        }

        focusOnActual();

        focusOnPattern();
    }

    if (fillState > 0)
    {
    }
    else if (mtPopups.isNoteKeyboardPopupActive())
    {
    }
    else
    {
        refreshActualActionButtonPage();
    }

    display.refreshControl(patternControl);
}

void cPatternEditor::setPlayheadRangeFromSelection()
{
    trackerPattern.playheadStartTrack = trackerPattern.selectStartTrack;
    trackerPattern.playheadEndTrack = trackerPattern.selectEndTrack;
}

void cPatternEditor::changeFillData(int16_t value, bool rollListOver)
{
    if (fillPlace < 0 && fillPlace > 5)
        return;

    int16_t* ptrVal;
    int16_t min = 0, max;
    uint8_t fillParam = fillPlace;
    uint8_t fillIdx = getFillDataIdx();
    uint8_t fillFx = interfaceGlobals.fxNameToId(fillData[fillIdx].param);

    switch (fillPlace)
    {
        case 1: fillParam = 5; break; // density
        case 2: fillParam = 4; break; // param
        case 3: fillParam = 1; break; // type
        case 4: fillParam = 2; break; // from
        case 5: fillParam = 3; break; // to
    }

    switch (fillParam)
    {
        case 0: // step
            ptrVal = &fillData[fillIdx].fillStep;
            min = Sequencer::fillStepNote;
            max = Sequencer::fillStepEnd__ - 1;
            break;
        case 1: // type
            ptrVal = &fillData[fillIdx].type;
            max = fillTypeList.length - 1;
            break;
        case 2: // from
            if (editParam == EditInstr)
            {
                value = clamp<int>(value, -5, 5);
            }
            ptrVal = &fillData[fillIdx].from;
            if (isMicVolView && editParam >= EditFx1)
            {
                min = 0;
                max = 100;
            }
            else
            {
                min = (editParam < EditFx1
                         ? 0
                         : sequencer.getFxMin(fillFx));
                max = (editParam == EditNote
                         ? 127
                         : (editParam == EditInstr
                              ? kTotalInstrumentsCount - 1
                              : sequencer.getFxMax(fillFx)));
            }
            break;
        case 3: // to
            if (fillData[fillIdx].type == 0)
            {
                return;
            }
            if (editParam == EditInstr)
            {
                value = clamp<int>(value, -5, 5);
            }
            ptrVal = &fillData[fillIdx].to;
            if (isMicVolView && editParam >= EditFx1)
            {
                min = 0;
                max = 100;
            }
            else
            {
                min = (editParam < EditFx1
                         ? 0
                         : sequencer.getFxMin(fillFx));
                max = (editParam == EditNote
                         ? 127
                         : (editParam == EditInstr
                              ? kTotalInstrumentsCount - 1
                              : sequencer.getFxMax(fillFx)));
            }
            break;
        case 4: // param
            if (isMicVolView && editParam >= EditFx1)
            {
                return;
            }
            value = clamp<int>(value, -5, 5);
            ptrVal = &fillData[fillIdx].param;
            min = (editParam == EditNote ? 0 : -1);
            max = (editParam == EditNote ? MAX_SCALE : FX_COUNT - 1 - FX_COUNT_HIDDEN_FXes);
            break;
        case 5: // density
            ptrVal = &fillData[fillIdx].density[fillData[fillIdx].fillStep];
            min = 1;
            max = Sequencer::MAX_FILL_DENSITY;
            break;

        default: return;
    }

    if (*ptrVal + value < min)
        *ptrVal = min;
    else if (*ptrVal + value > max)
        *ptrVal = (rollListOver ? min : max);
    else
        *ptrVal += value;

    switch (fillParam)
    {
        case 0: refreshFillStep(); break;
        case 1: refreshFillType(); break;
        case 2: refreshFillFrom(); break;
        case 3: refreshFillTo(); break;
        case 4: refreshFillParam(); break;
        case 5: refreshFillDensity(); break;
        default: return;
    }
}

void cPatternEditor::changneFillDataByPad(uint8_t pad)
{
    uint8_t fillParam = fillPlace;
    uint8_t fillIdx = getFillDataIdx();

    switch (fillPlace)
    {
        case 1: fillParam = 5; break; // density
        case 2: fillParam = 4; break; // param
        case 3: fillParam = 1; break; // type
        case 4: fillParam = 2; break; // from
        case 5: fillParam = 3; break; // to
    }

    switch (fillParam)
    {
        case 0: // step
        {
            if (pad < Sequencer::fillStepEnd__)
                fillData[fillIdx].fillStep = pad;
            break;
        }
        case 1: // type
        {
            if (pad < 3)
                fillData[fillIdx].type = pad;
            break;
        }
        case 2: // from
        {
            if (editParam == EditNote)
            {
                bool isBeatSlice =
                  mtProject.values.lastUsedInstrument < kSampleInstrumentsCount &&
                  mtProject.instrument[mtProject.values.lastUsedInstrument].playMode ==
                    playModeBeatSlice;

                fillData[fillIdx].from = mtPadBoard.getNoteFromPad(pad, isBeatSlice);
            }
            else if (editParam == EditInstr)
                fillData[fillIdx].from = pad;
            else if (isMicVolView)
                fillData[fillIdx].from = roundf(mapClamped<float>(pad, 0, 47, 0, 100));
            else
            {
                int16_t min_fx =
                  sequencer.getFxMin(interfaceGlobals.fxNameToId(fillData[fillIdx].param));
                int16_t max_fx =
                  sequencer.getFxMax(interfaceGlobals.fxNameToId(fillData[fillIdx].param));

                fillData[fillIdx].from = mapClamped<int>(pad, 0, 47, min_fx, max_fx);

                if (fillData[fillIdx].from < min_fx)
                    fillData[fillIdx].from = min_fx;
                if (fillData[fillIdx].from > max_fx)
                    fillData[fillIdx].from = max_fx;
            }
            break;
        }
        case 3: // to
        {
            if (editParam == EditNote)
            {
                bool isBeatSlice =
                  mtProject.values.lastUsedInstrument < kSampleInstrumentsCount &&
                  mtProject.instrument[mtProject.values.lastUsedInstrument].playMode ==
                    playModeBeatSlice;

                fillData[fillIdx].to = mtPadBoard.getNoteFromPad(pad, isBeatSlice);
            }
            else if (editParam == EditInstr)
                fillData[fillIdx].to = pad;
            else if (isMicVolView)
                fillData[fillIdx].to = roundf(mapClamped<float>(pad, 0, 47, 0, 100));
            else
            {
                int16_t min_fx =
                  sequencer.getFxMin(interfaceGlobals.fxNameToId(fillData[fillIdx].param));
                int16_t max_fx =
                  sequencer.getFxMax(interfaceGlobals.fxNameToId(fillData[fillIdx].param));

                fillData[fillIdx].to = mapClamped<int>(pad, 0, 47, min_fx, max_fx);

                if (fillData[fillIdx].to < min_fx)
                    fillData[fillIdx].to = min_fx;
                if (fillData[fillIdx].to > max_fx)
                    fillData[fillIdx].to = max_fx;
            }
            break;
        }
        case 4: // param
        {
            if (editParam == EditNote)
            {
                if (pad < MAX_SCALE)
                    fillData[fillIdx].param = pad;
            }
            else if (editParam >= 2 && !isMicVolView)
            {
                if (pad < FX_COUNT - FX_COUNT_HIDDEN_FXes)
                    fillData[fillIdx].param = pad;
            }
            break;
        }
        default: break;
    }

    switch (fillParam)
    {
        case 0: refreshFillStep(); break;
        case 1: refreshFillType(); break;
        case 2: refreshFillFrom(); break;
        case 3: refreshFillTo(); break;
        case 4: refreshFillParam(); break;
        case 5: refreshFillDensity(); break;
        default: return;
    }
}

void cPatternEditor::changeFillDataByActionButton(uint8_t place)
{
    if (place > 5)
        return;

    if (place == fillPlace && (place < 4 || editParam == EditInstr || editParam == EditNote))
    {
        changeFillData(1, true);
    }
}

bool cPatternEditor::isFillPlaceValid(uint8_t place)
{
    uint8_t fillIdx = getFillDataIdx();
    // place 2 not available in instrument mode or mic/vol mode
    // place 1 not available in some step modes
    return !(((editParam == EditInstr || (isMicVolView && editParam >= EditFx1)) && place == 2) ||
             (fillData[fillIdx].fillStep < Sequencer::fillStepRandom && place == 1) ||
             (fillData[fillIdx].type == 0 && place > 4));
}

void cPatternEditor::setFillPlace(uint8_t place, int8_t dir)
{
    if (place < 0 || place > 5)
        return;

    if (dir == 0)
    {
        if (isFillPlaceValid(place))
        {
            fillPlace = place;
        }
        return;
    }
    else if (dir > 0)
    {
        if ((place > 5))
            fillPlace = place - dir;
        else
            fillPlace = place;
    }
    else if (dir < 0)
    {
        fillPlace = place;
    }
}

void cPatternEditor::changeFillPlace(int8_t diff)
{
    while (!isFillPlaceValid(fillPlace + diff) && fillPlace + diff >= 0 && fillPlace + diff <= 5)
    {
        diff += (diff >= 0 ? 1 : -1);
    }

    setFillPlace(fillPlace + diff, diff);
}

void cPatternEditor::refreshFillPlace()
{
}

void cPatternEditor::setMuteFunct(uint8_t state)
{
    if (state == 0)
    {
    }
    else
    {
        ip->setButtonObj(interfaceButton0, functMuteTrack);
        ip->setButtonObj(interfaceButton1, functMuteTrack);
        ip->setButtonObj(interfaceButton2, functMuteTrack);
        ip->setButtonObj(interfaceButton3, functMuteTrack);
        ip->setButtonObj(interfaceButton4, functMuteTrack);
        ip->setButtonObj(interfaceButton5, functMuteTrack);
        ip->setButtonObj(interfaceButton6, functMuteTrack);
        ip->setButtonObj(interfaceButton7, functMuteTrack);
    }
}
void cPatternEditor::setRecArmingFunct(uint8_t state)
{
    if (state == 0)
    {
    }
    else
    {
        ip->setButtonObj(interfaceButton0, functRecArmTrack);
        ip->setButtonObj(interfaceButton1, functRecArmTrack);
        ip->setButtonObj(interfaceButton2, functRecArmTrack);
        ip->setButtonObj(interfaceButton3, functRecArmTrack);
        ip->setButtonObj(interfaceButton4, functRecArmTrack);
        ip->setButtonObj(interfaceButton5, functRecArmTrack);
        ip->setButtonObj(interfaceButton6, functRecArmTrack);
        ip->setButtonObj(interfaceButton7, functRecArmTrack);
    }
}

uint8_t cPatternEditor::isCursorInSelection()
{
    uint8_t x1 = 0, x2 = 0;
    uint16_t y1 = 0, y2 = 0;

    if (trackerPattern.selectEndStep > trackerPattern.selectStartStep)
    {
        y1 = trackerPattern.selectStartStep;
        y2 = trackerPattern.selectEndStep;
    }
    else
    {
        y2 = trackerPattern.selectStartStep;
        y1 = trackerPattern.selectEndStep;
    }

    if (trackerPattern.selectEndTrack > trackerPattern.selectStartTrack)
    {
        x1 = trackerPattern.selectStartTrack;
        x2 = trackerPattern.selectEndTrack;
    }
    else
    {
        x2 = trackerPattern.selectStartTrack;
        x1 = trackerPattern.selectEndTrack;
    }

    if (trackerPattern.actualStep < y1 || trackerPattern.actualStep > y2 ||
        trackerPattern.actualTrack < x1 || trackerPattern.actualTrack > x2)
        return 0;

    return 1;
}

bool cPatternEditor::isSelectionOrCursorInMidiTracks()
{
    if (trackerPattern.selectState == 2)
    {
        if (trackerPattern.selectStartTrack >= kSampleTracks ||
            trackerPattern.selectEndTrack >= kSampleTracks ||
            trackerPattern.actualTrack >= kSampleTracks)
        {
            return true;
        }
    }
    else
    {
        if (trackerPattern.actualTrack >= kSampleTracks)
        {
            return true;
        }
    }

    return false;
}

void cPatternEditor::setProjectSaveFlags()
{
    newFileManager.setProjectStructChanged();
}

void cPatternEditor::startExportSelection()
{
    if (PTE->saveClockConfig == 255)
    {
        PTE->saveClockConfig = mtConfig.midi.clkIn;
        mtConfig.midi.clkIn = 0;
    }

    keyboardManager.deactivateKeyboard();

    if (newFileManager.exportSoundRenderSelection(PTE->keyboardManager.getName()))
    {
        // PE->ip->setButtonObj(interfaceButton7, buttonPress, functExportCancel);
        // PE->ip->blockAllInputsExcept(interfaceButton7);
        PTE->ip->blockAllInputs();
    }
}

void cPatternEditor::endExportSelection()
{
    if (saveClockConfig > 0 && saveClockConfig < 3)
    {
        mtConfig.midi.clkIn = saveClockConfig;
    }

    if (isLoadAfterSave)
    {
        isLoadAfterSave = false;

        // check if its enough memory
        if ((mtProject.used_memory + exporter.getRenderLength() * 2) > mtProject.max_memory)
        {
            showFullMemoryInBank();
        }
        else
        {
            // check if there is free instrument slot
            PTE->firstFreeInstrumentSlotFound = -1;
            for (uint8_t i = 0; i < kSampleInstrumentsCount; i++)
            {
                if (!mtProject.instrument[i].isActive)
                {
                    PTE->firstFreeInstrumentSlotFound = i;
                    break;
                }
            }

            if (PTE->firstFreeInstrumentSlotFound == -1)
            {
                showFullInstrumentInBank();
            }
            else
            {
                // now import
                char localPatch[255];
                sprintf(localPatch, "Export/%s/Selection", newFileManager.getCurrentProjectName());
                if (newFileManager.importSampleToProject(localPatch,
                                                         PTE->keyboardManager.getName(),
                                                         PTE->firstFreeInstrumentSlotFound))
                {
                    return;
                }
            }
        }
    }

    // if not import or error just end here
    ip->unblockAllInputs();
    start(0);
}

//==============================================================================================================
//==============================================================================================================

void functEncoder(int16_t value)
{
    if (PTE->keyboardManager.getState())
    {
#ifdef TRACKER_MINI
        PTE->keyboardManager.confirmKey();
#endif
        return;
    }

    if (PTE->fillState > 0)
    {
        bool invertValForList = false;
        if (interfaceGlobals.invertValForList) // tracker mini
        {
            invertValForList =
              (PTE->fillPlace == 0 || PTE->fillPlace == 2 || PTE->fillPlace == 3) ||
              (PTE->editParam == cPatternEditor::EditInstr &&
               (PTE->fillPlace == 4 || PTE->fillPlace == 5));

            if (PTE->editParam == PTE->EditNote)
            {
                value = value == 10 ? 12 : (value == -10 ? -12 : value);
            }
        }

        PTE->changeFillData(invertValForList ? -value : value);
        return;
    }

    if (PTE->selectedPlace >= 0)
    {

        switch (PTE->selectedPlace)
        {
            case 0: PTE->changeActualPattern(value); break;
            case 1: PTE->changeActualPatternLength(value); break;
            case 2: PTE->changeActualPatternEditStep(value); break;
            case 5:
            case 6: functUndoRedoOnLeftRight(value); break;
        }

        PTE->lightUpPadBoard();

        return;
    }

    if (PTE->buttons->isButtonPressed(interfaceButtonPattern))
    {
        PTE->changeActualPattern(value);
        return;
    }

    if (PTE->editMode == 0)
    {
        value = interfaceGlobals.invertValForList ? -value : value;

        if (!sequencer.isStop())
        {
            return;
        }
        // przesuwanie lewo-prawo
        // if(PTE->trackerPattern.actualTrack + value < 0) PTE->trackerPattern.actualTrack = 0;
        // else if(PTE->trackerPattern.actualTrack + value > 7) PTE->trackerPattern.actualTrack = 7;
        // else  PTE->trackerPattern.actualTrack += value;

        if (PTE->isSelectingNow == 1) // konczenie zaznaczania
        {
            PTE->isSelectingNow = 0;
            PTE->trackerPattern.selectColumn = 0;
            PTE->trackerPattern.selectState = 1;
        }

        value = clamp<int>(value, -10, 10);

        // przesuwanie gora-dol
        if (PTE->trackerPattern.actualStep + value < 0)
            PTE->trackerPattern.actualStep = 0;
        else if (PTE->trackerPattern.actualStep + value > PTE->trackerPattern.patternLength - 1)
            PTE->trackerPattern.actualStep = PTE->trackerPattern.patternLength - 1;
        else
            PTE->trackerPattern.actualStep += value;

        PTE->focusOnActual();
        PTE->showFxInfo();
        PTE->refreshPattern();
        PTE->lightUpPadBoard();

        return;
    }

    // czy parametr widoczny jesli np. pokazywane tylko 2 parametry 8 trackow
    if (PTE->patternViewMode > 0 && !(PTE->patternViewMode & (1 << PTE->editParam)))
    {
        return;
    }

    sendSelection();
    changeSelectedStepValue(value);

    PTE->showActualParamOnBar();

    PTE->lightUpPadBoard();
    PTE->refreshPattern();
}

static void functShift(uint8_t state)
{
    if (PTE->fillState)
    {
        return;
    }

    if (state == buttonPress)
    {
        if (mtPopups.getStepPopupState() !=
            stepPopupNone) // ukrywa popup nuta...fx jesli jest wyswietlany
        {
            PTE->insertOnPopupHideDisabled =
              1; // dezaktywuje wstawianie danych na wyjecie z popupow
            PTE->cancelPopups();
            PTE->insertOnPopupHideDisabled = 0; // a tu aktywuje spowrotem
        }
        else if (mtPopups.isNoteKeyboardPopupActive())
        {
            mtPopups.toggleNoteKeyboardPopup(InterfacePopups::noteKeyboardOption::kShowEditButtons);
        }
        else
        {
            if (PTE->selectedPlace < 0)
            {
                PTE->showMuteButtons(1, !isSampleTrack(PTE->trackerPattern.actualTrack));
                PTE->setMuteFunct(1);
            }
        }
    }
    else if (state == buttonRelease)
    {
        //		if(PTE->isSelectingNow == 1) // konczenie zaznaczania po puszczeniu shifta
        //		{
        //			PTE->isSelectingNow = 0;
        //			PTE->trackerPattern.selectColumn = 0;
        //			PTE->trackerPattern.selectState = 1;
        //		}

        // PTE->showMuteButtons(0);
        PTE->refreshEditState(); // <<== zawiera PTE->refreshActualActionButtonPage();

        if (mtPopups.isNoteKeyboardPopupActive())
        {
            mtPopups.toggleNoteKeyboardPopup(InterfacePopups::noteKeyboardOption::kForceShow);
        }
    }
}

static void functLeft()
{
    if (PTE->fillState > 0)
    {
        PTE->changeFillPlace(-1);
        PTE->activateFillPopupBorder();
        return;
    }

    if (PTE->selectedPlace >= 0 && PTE->selectedPlace < 8)
    {
        switch (PTE->selectedPlace)
        {
            case 0:
            {
                if (sequencer.isPlay())
                {
                    sequencer.sequencialSwitch_changeNextPattern(-1);
                }
                else
                {
                    PTE->changeActualPattern(-1);
                }
                return;
            }
            case 1: PTE->changeActualPatternLengthByDefinedValues(-1); return;
            case 2: PTE->changeActualPatternEditStepByDefinedValues(-1); return;
            case 5:
            case 6: functUndoRedoOnLeftRight(-1); return;
            default: break;
        }
        return;
    }

    if (PTE->buttons->isButtonPressed(interfaceButtonPattern))
    {
        if (sequencer.isPlay())
        {
            sequencer.sequencialSwitch_changeNextPattern(-1);
        }
        else
        {
            PTE->changeActualPattern(-1);
        }
        return;
    }

    PTE->changeActualStep(moveLeft);
}

static void functRight()
{
    if (PTE->fillState > 0)
    {
        PTE->changeFillPlace(1);
        PTE->activateFillPopupBorder();
        return;
    }

    if (PTE->selectedPlace >= 0 && PTE->selectedPlace < 8)
    {
        switch (PTE->selectedPlace)
        {
            case 0:
            {
                if (sequencer.isPlay())
                {
                    sequencer.sequencialSwitch_changeNextPattern(1);
                }
                else
                {
                    PTE->changeActualPattern(1);
                }
                return;
            }
            case 1: PTE->changeActualPatternLengthByDefinedValues(1); return;
            case 2: PTE->changeActualPatternEditStepByDefinedValues(1); return;
            case 5:
            case 6: functUndoRedoOnLeftRight(1); return;
            default: break;
        }
        return;
    }

    if (PTE->buttons->isButtonPressed(interfaceButtonPattern))
    {
        if (sequencer.isPlay())
        {
            sequencer.sequencialSwitch_changeNextPattern(1);
        }
        else
        {
            PTE->changeActualPattern(1);
        }
        return;
    }

    PTE->changeActualStep(moveRight);
}

static void functUp()
{
    if (PTE->fillState > 0)
    {
        bool isList = (PTE->fillPlace == 0 || PTE->fillPlace == 2 || PTE->fillPlace == 3) ||
                      (PTE->editParam == cPatternEditor::EditInstr &&
                       (PTE->fillPlace == 4 || PTE->fillPlace == 5));
        PTE->changeFillData(isList ? -1 : 1);
        return;
    }

    if (PTE->selectedPlace >= 0 && PTE->selectedPlace < 8)
    {
        switch (PTE->selectedPlace)
        {
            // case 0: PTE->changeActualTempo(1); 			 return 1;
            case 0: PTE->changeActualPattern(1); return;
            case 1: PTE->changeActualPatternLength(1); return;
            case 2: PTE->changeActualPatternEditStep(1); return;
            default: break;
        }
        return;
    }

    if (PTE->buttons->isButtonPressed(interfaceButtonPattern))
    {
        PTE->changeActualPattern(1);
        return;
    }

    PTE->changeActualStep(moveUp);
}

static void functDown()
{
    if (PTE->fillState > 0)
    {
        bool isList = (PTE->fillPlace == 0 || PTE->fillPlace == 2 || PTE->fillPlace == 3) ||
                      (PTE->editParam == cPatternEditor::EditInstr &&
                       (PTE->fillPlace == 4 || PTE->fillPlace == 5));
        PTE->changeFillData(isList ? 1 : -1);
        return;
    }

    if (PTE->selectedPlace >= 0 && PTE->selectedPlace < 8)
    {
        switch (PTE->selectedPlace)
        {
            // case 0: PTE->changeActualTempo(-1); 			return 1;
            case 0: PTE->changeActualPattern(-1); return;
            case 1: PTE->changeActualPatternLength(-1); return;
            case 2: PTE->changeActualPatternEditStep(-1); return;
            default: break;
        }

        return;
    }

    if (PTE->buttons->isButtonPressed(interfaceButtonPattern))
    {
        PTE->changeActualPattern(-1);
        return;
    }

    PTE->changeActualStep(moveDown);
}

//=========================================================================================================
//=========================================================================================================
//=========================================================================================================

void cPatternEditor::changeActualStep(uint8_t direction)
{
    if (mtConfig.general.patternArrangement >= kPatternArrangementCount)
    {
        return;
    }
    if (direction >= 4)
    {
        return;
    }

    switch (arrangementDirection[mtConfig.general.patternArrangement][direction])
    {
        case moveUp: previousStep(); break;
        case moveDown: nextStep(); break;
        case moveRight: nextTrack(); break;
        case moveLeft: previousTrack(); break;
        default: break;
    }
}

void cPatternEditor::previousTrack()
{
    uint8_t shiftPressed = buttons->isButtonPressed(interfaceButtonShift);

    if (PTE->editMode || sequencer.isStop())
    {
        if (shiftPressed &&
            PTE->isSelectingNow == 0) // pierwsze wcisniecie kierunku po wcisnieciu shift
        {
            PTE->trackerPattern.selectStartStep = PTE->trackerPattern.actualStep;
            PTE->trackerPattern.selectStartTrack = PTE->trackerPattern.actualTrack;
            // PTE->trackerPattern.selectState = 1;
            PTE->isSelectingNow = 1;
        }

        bool cursorAtEdge = true;
        if (PTE->trackerPattern.actualTrack > 0)
        {
            PTE->trackerPattern.actualTrack--;
            cursorAtEdge = false;
        }

        PTE->focusOnActual();

        if (shiftPressed && PTE->trackerPattern.selectColumn == 1) // zaznaczanie kolumny
        {
            PTE->trackerPattern.selectEndStep = PTE->trackerPattern.patternLength - 1;
            PTE->trackerPattern.selectEndTrack = PTE->trackerPattern.actualTrack;
            if (cursorAtEdge)
            {
                PTE->trackerPattern.selectStartTrack = kAllTracks - 1;
            }
            PTE->trackerPattern.selectState = 2;
            PTE->isSelectingNow = 1;
        }
        else if (shiftPressed && PTE->trackerPattern.selectColumn == 0)
        {
            PTE->trackerPattern.selectEndStep = PTE->trackerPattern.actualStep;
            PTE->trackerPattern.selectEndTrack = PTE->trackerPattern.actualTrack;
            if (cursorAtEdge)
            {
                PTE->trackerPattern.selectStartTrack = kAllTracks - 1;
            }
            PTE->trackerPattern.selectState = 2;
        }
        else if (!shiftPressed)
        {
            PTE->trackerPattern.selectState = 1;
            PTE->trackerPattern.selectColumn = 0;
            PTE->isSelectingNow = 0;
            // display.refreshControl(PTE->patternControl);
        }
    }
    else
    {
        // if(PTE->patternViewMode == 0 && PTE->trackerPattern.firstVisibleTrack > 0 )
        // PTE->trackerPattern.firstVisibleTrack--;

        if (PTE->trackerPattern.actualTrack > 0)
            PTE->trackerPattern.actualTrack--;
        PTE->focusOnActual();
    }

    PTE->showFxInfo();

    PTE->lightUpPadBoard();
    display.refreshControl(PTE->patternControl);
}

void cPatternEditor::nextTrack()
{

    uint8_t shiftPressed = buttons->isButtonPressed(interfaceButtonShift);

    if (PTE->editMode || sequencer.isStop())
    {
        if (shiftPressed &&
            PTE->isSelectingNow == 0) // pierwsze wcisniecie kierunku po wcisnieciu shift
        {
            PTE->trackerPattern.selectStartStep = PTE->trackerPattern.actualStep;
            PTE->trackerPattern.selectStartTrack = PTE->trackerPattern.actualTrack;
            // PTE->trackerPattern.selectState = 1;
            PTE->isSelectingNow = 1;
        }

        bool cursorAtEdge = true;
        if (PTE->trackerPattern.actualTrack < kAllTracks - 1)
        {
            PTE->trackerPattern.actualTrack++;
            cursorAtEdge = false;
        }

        PTE->focusOnActual();

        if (shiftPressed && PTE->trackerPattern.selectColumn == 1) // zaznaczanie kolumny
        {
            PTE->trackerPattern.selectEndStep = PTE->trackerPattern.patternLength - 1;
            PTE->trackerPattern.selectEndTrack = PTE->trackerPattern.actualTrack;
            if (cursorAtEdge)
            {
                PTE->trackerPattern.selectStartTrack = kAllTracks - 1;
            }
            PTE->trackerPattern.selectState = 2;
            PTE->isSelectingNow = 1;
        }
        else if (shiftPressed && PTE->trackerPattern.selectColumn == 0)
        {
            PTE->trackerPattern.selectEndStep = PTE->trackerPattern.actualStep;
            PTE->trackerPattern.selectEndTrack = PTE->trackerPattern.actualTrack;
            if (cursorAtEdge)
            {
                PTE->trackerPattern.selectStartTrack = kAllTracks - 1;
            }
            PTE->trackerPattern.selectState = 2;
        }
        else if (!shiftPressed)
        {
            PTE->trackerPattern.selectState = 1;
            PTE->trackerPattern.selectColumn = 0;
            PTE->isSelectingNow = 0;
            // display.refreshControl(PTE->patternControl);
        }
    }
    else
    {
        // if(PTE->patternViewMode == 0 && PTE->trackerPattern.firstVisibleTrack < 4 )
        // PTE->trackerPattern.firstVisibleTrack++;

        if (PTE->trackerPattern.actualTrack < kAllTracks - 1)
            PTE->trackerPattern.actualTrack++;
        PTE->focusOnActual();
    }

    PTE->showFxInfo();

    PTE->lightUpPadBoard();
    display.refreshControl(PTE->patternControl);
}

void cPatternEditor::previousStep()
{
    uint8_t shiftPressed = buttons->isButtonPressed(interfaceButtonShift);

    if (PTE->editMode == 0)
    {
        if (!sequencer.isStop())
        {
            return;
        }
    }

    if (/*PTE->editMode == 1 &&*/ shiftPressed &&
        PTE->trackerPattern.actualStep == 0) // zaznaczanie calej kolumny
    {
        if (PTE->trackerPattern.selectColumn == 1) // zaznacz wszystko
        {
            PTE->trackerPattern.selectStartStep = 0;
            PTE->trackerPattern.selectStartTrack = 0;
            PTE->trackerPattern.selectEndStep = PTE->trackerPattern.patternLength - 1;
            PTE->trackerPattern.selectEndTrack = kAllTracks - 1;
            PTE->trackerPattern.selectState = 2;
            PTE->isSelectingNow = 1;
        }
        else // zaznacz kolumne
        {
            PTE->trackerPattern.selectColumn = 1;

            PTE->trackerPattern.selectStartStep = 0;
            PTE->trackerPattern.selectStartTrack = PTE->trackerPattern.actualTrack;
            PTE->trackerPattern.selectEndStep = PTE->trackerPattern.patternLength - 1;
            PTE->trackerPattern.selectEndTrack = PTE->trackerPattern.actualTrack;
            PTE->trackerPattern.selectState = 2;
            PTE->isSelectingNow = 1;
        }
    }

    if (/*PTE->editMode == 1 &&*/ shiftPressed && PTE->isSelectingNow == 0) // poczatek zaznaczenia
    {
        PTE->trackerPattern.selectStartStep = PTE->trackerPattern.actualStep;
        PTE->trackerPattern.selectStartTrack = PTE->trackerPattern.actualTrack;
        // PTE->trackerPattern.selectState = 1;
        PTE->isSelectingNow = 1;
    }

    if (PTE->trackerPattern.actualStep > 0)
        PTE->trackerPattern.actualStep--; // zmiana pozycji kursora
    else if (!shiftPressed)
        PTE->trackerPattern.actualStep = PTE->trackerPattern.patternLength - 1;

    if (/*PTE->editMode == 1 &&*/ shiftPressed &&
        PTE->trackerPattern.selectColumn == 0) // kontynuacja zaznaczania
    {
        PTE->trackerPattern.selectEndStep = PTE->trackerPattern.actualStep;
        PTE->trackerPattern.selectEndTrack = PTE->trackerPattern.actualTrack;
        PTE->trackerPattern.selectState = 2;
    }
    else if (!shiftPressed)
    {
        PTE->trackerPattern.selectColumn = 0;
        PTE->trackerPattern.selectState = 1;
        PTE->isSelectingNow = 0;
        // display.refreshControl(PTE->patternControl);
    }

    PTE->showFxInfo();

    PTE->refreshPattern();
    PTE->lightUpPadBoard();
}

void cPatternEditor::nextStep()
{
    uint8_t shiftPressed = buttons->isButtonPressed(interfaceButtonShift);

    if (PTE->editMode == 0)
    {
        if (!sequencer.isStop())
        {
            return;
        }
    }

    if (/*PTE->editMode == 1 &&*/ shiftPressed && PTE->isSelectingNow == 0)
    {
        PTE->trackerPattern.selectStartStep = PTE->trackerPattern.actualStep;
        PTE->trackerPattern.selectStartTrack = PTE->trackerPattern.actualTrack;
        // PTE->trackerPattern.selectState = 1;
        PTE->isSelectingNow = 1;
    }

    if (PTE->trackerPattern.actualStep < PTE->trackerPattern.patternLength - 1)
        PTE->trackerPattern.actualStep++;
    else if (!shiftPressed)
        PTE->trackerPattern.actualStep = 0;

    if (/*PTE->editMode == 1 &&*/ shiftPressed)
    {
        PTE->trackerPattern.selectEndStep = PTE->trackerPattern.actualStep;
        PTE->trackerPattern.selectEndTrack = PTE->trackerPattern.actualTrack;
        PTE->trackerPattern.selectState = 2;
        PTE->trackerPattern.selectColumn = 0;
    }
    else
    {
        PTE->trackerPattern.selectState = 1;
        PTE->trackerPattern.selectColumn = 0;
        PTE->isSelectingNow = 0;
        // display.refreshControl(PTE->patternControl);
    }

    PTE->showFxInfo();

    PTE->refreshPattern();
    PTE->lightUpPadBoard();
}

//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
static void functNote(uint8_t state)
{
    if (state == buttonPress)
    {
        PTE->toggledMicVolOnPress = false;
        if (PTE->editParam == cPatternEditor::EditNote && PTE->fillState == 0 &&
            PTE->patternViewMode == 0 && !PTE->buttons->isButtonPressed(interfaceButtonPattern) &&
            !PTE->buttons->isButtonPressed(interfaceButtonShift) &&
            !PTE->buttons->isButtonPressed(interfaceButtonDelete) &&
            !PTE->buttons->isButtonPressed(interfaceButtonCopy) &&
            !mtPopups.isNoteKeyboardPopupActive())
        {
            PTE->isMicVolView = !PTE->isMicVolView;
            PTE->toggledMicVolOnPress = true;
        }

        PTE->lightUpPadBoard();
        PTE->setStepButtonState(0, 1);
        PTE->wasNotesEditBefore = 0;
        PTE->previousEditParam = PTE->editParam;
        PTE->editParam = cPatternEditor::EditNote;
        PTE->trackerPattern.selectedParam = 0;
        if (PTE->toggledMicVolOnPress)
        {
            PTE->refreshPattern();
        }
        else
        {
            display.refreshControl(PTE->patternControl);
        }

        PTE->showActualParamOnBar();
        PTE->cancelPopups();

        if (PTE->buttons->isButtonPressed(interfaceButtonPattern)) // zmiana widoku na 8 trackow
        {
            PTE->setPatternViewMode(1);
        }
        else
        {
            PTE->changePatternViewMode(1);
        }

#ifdef TRACKER_MINI
        if (PTE->buttons->isButtonPressed(interfaceButtonShift) && PTE->fillState == 0)
        {
            bool popupShown = mtPopups.toggleNoteKeyboardPopup();
            if (!popupShown)
            {
                PTE->setDefaultScreenFunct();
                PTE->refreshEditState();
            }
        }
        else if (mtPopups.isNoteKeyboardPopupActive())
        {
            PTE->showNotePopup();
        }
#endif

        if (PTE->fillState > 0)
        {
            PTE->showFillPopup();
            PTE->trackerPattern.popupMode |= 1;
            PTE->lightUpPadBoard();
            return;
        }

        PTE->focusOnPattern();
    }
    else if (state == buttonHold)
    {
    }
    else if (state == buttonRelease)
    {
        PTE->toggledMicVolOnPress = false;
        PTE->setStepButtonState(0, 0);
        PTE->cancelPopups();
        PTE->dontShowPopupsUntilButtonRelease = 0;
    }
}

//-----------------------------------------------------------------------------------
static void functInstrument(uint8_t state)
{
    if (mtPopups.isNoteKeyboardPopupActive())
    {
        mtPopups.toggleNoteKeyboardPopup();
        PTE->setDefaultScreenFunct();
        PTE->refreshEditState();
    }

    if (state == buttonPress)
    {
        PTE->setStepButtonState(1, 1);
        if (PTE->editParam == cPatternEditor::EditNote)
        {
            PTE->wasNotesEditBefore = 1;
        }
        PTE->previousEditParam = PTE->editParam;
        PTE->editParam = cPatternEditor::EditInstr;
        PTE->trackerPattern.selectedParam = 1;
        display.refreshControl(PTE->patternControl);

        PTE->showActualParamOnBar();
        PTE->cancelPopups();

        if (PTE->buttons->isButtonPressed(interfaceButtonPattern)) // zmiana widoku na 8 trackow
        {
            PTE->setPatternViewMode(2);
        }
        else
        {
            PTE->changePatternViewMode(2);
        }

        if (PTE->fillState > 0)
        {
            //			PTE->wasNotesEditBefore = 0;
            PTE->showFillPopup();
            PTE->trackerPattern.popupMode |= 1;
            PTE->lightUpPadBoard();
            return;
        }

        sequencer.stopManualNotes();

        PTE->focusOnPattern();
    }
    else if (state == buttonHold)
    {
    }
    else if (state == buttonRelease)
    {
        PTE->setStepButtonState(1, 0);

        // powrot do nuty po wybraniu instrumentu
        if (PTE->fillState == 0 && PTE->wasNotesEditBefore &&
            mtPopups.getStepPopupState() != stepPopupNone &&
            (PTE->patternViewMode == 0 || PTE->patternViewMode == 3))
        {
            PTE->wasNotesEditBefore = 0;
            PTE->editParam = cPatternEditor::EditNote;
            PTE->trackerPattern.selectedParam = 0;
            display.refreshControl(PTE->patternControl);

            PTE->focusOnPattern();
        }

        PTE->cancelPopups();
        PTE->dontShowPopupsUntilButtonRelease = 0;

        sequencer.stopManualNotes();
    }
}

//-----------------------------------------------------------------------------------
static void functFx1(uint8_t state)
{
    if (state == buttonPress)
    {
        PTE->setStepButtonState(2, 1);
        PTE->wasNotesEditBefore = 0;
        PTE->previousEditParam = PTE->editParam;
        PTE->editParam = cPatternEditor::EditFx1;
        PTE->trackerPattern.selectedParam = 2;
        display.refreshControl(PTE->patternControl);

        PTE->showActualParamOnBar();
        PTE->cancelPopups();

        if (mtPopups.isNoteKeyboardPopupActive())
        {
            if (!mtPopups.toggleNoteKeyboardPopup())
            {
                PTE->setDefaultScreenFunct();
                PTE->refreshEditState();
            }
        }

        if (PTE->buttons->isButtonPressed(interfaceButtonPattern)) // zmiana widoku na 8 trackow
        {
            PTE->setPatternViewMode(3);
        }
        else
        {
            PTE->changePatternViewMode(3);
        }

        if (PTE->fillState > 0)
        {
            PTE->showFillPopup();
            PTE->trackerPattern.popupMode |= 1;
            return;
        }

        PTE->focusOnPattern();
    }
    else if (state == buttonHold)
    {
    }
    else if (state == buttonRelease)
    {
        PTE->setStepButtonState(2, 0);
        PTE->cancelPopups();
        PTE->dontShowPopupsUntilButtonRelease = 0;

        PTE->showFxInfo();
        PTE->refreshAfterReleaseFx = true;
    }
}

//-----------------------------------------------------------------------------------
static void functFx2(uint8_t state)
{
    if (state == buttonPress)
    {
        PTE->setStepButtonState(3, 1);
        //		PTE->wasNotesEditBefore = 0;
        PTE->previousEditParam = PTE->editParam;
        PTE->editParam = cPatternEditor::EditFx2;
        PTE->trackerPattern.selectedParam = 3;
        display.refreshControl(PTE->patternControl);

        PTE->showActualParamOnBar();
        PTE->cancelPopups();

        if (mtPopups.isNoteKeyboardPopupActive())
        {
            if (!mtPopups.toggleNoteKeyboardPopup())
            {
                PTE->setDefaultScreenFunct();
                PTE->refreshEditState();
            }
        }

        if (PTE->buttons->isButtonPressed(interfaceButtonPattern)) // zmiana widoku na 8 trackow
        {
            PTE->setPatternViewMode(4);
        }
        else
        {
            PTE->changePatternViewMode(4);
        }

        if (PTE->fillState > 0)
        {
            PTE->showFillPopup();
            PTE->trackerPattern.popupMode |= 1;
            return;
        }

        PTE->focusOnPattern();
    }
    else if (state == buttonHold)
    {
    }
    else if (state == buttonRelease)
    {
        PTE->setStepButtonState(3, 0);
        PTE->cancelPopups();
        PTE->dontShowPopupsUntilButtonRelease = 0;

        PTE->showFxInfo();
        PTE->refreshAfterReleaseFx = true;
    }
}

static void functPattern(uint8_t state)
{
    if (state == buttonPress)
    {
        if (PTE->fillState == 1)
        {
            return;
        }

        PTE->cancelPopups();
    }
    else if (state == buttonRelease && PTE->patternButtonReleaseActive)
    {
        PTE->setPatternViewMode(0);
        if (sequencer.sequencialSwitch_GetNext() > 0)
        {
            sequencer.sequencialSwitch_SetReady();
        }
    }
    else if (state == buttonRelease)
    {
        PTE->patternButtonReleaseActive = 1;
        if (sequencer.sequencialSwitch_GetNext() > 0)
        {
            sequencer.sequencialSwitch_SetReady();
        }
    }
}

//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
static void functPlayAction()
{
    if (sequencer.getSeqState() == Sequencer::SEQ_STATE_STOP)
    {
        if (PTE->buttons->isButtonPressed(interfaceButtonRec))
        {
            if (PTE->fillState > 0)
            {
                return; // w fill rec wylaczony
            }

            sequencer.recStart();

            if (!sequencer.isSetInternalClock())
            {
                mtPopups.show(0, "Live rec on external clock ready");
            }
            PTE->editMode = 0;

            PTE->refreshEditState();
        }
        else if (PTE->buttons->isButtonPressed(interfaceButtonShift))
        {
            if (isMultiSelection())
            {
                PTE->setPlayheadRangeFromSelection();
                sendSelection();
                sequencer.playSelection(true);
            }
            else
            {
                sequencer.playPattern((uint8_t)getActualStep());
            }
        }
        else
        {
            sequencer.playPattern();
        }
    }
    else if (PTE->buttons->isButtonPressed(interfaceButtonRec))
    {
        if (PTE->fillState > 0)
        {
            return; // w fill rec wylaczony
        }

        sequencer.recEnable(true);
        PTE->editMode = 0;

        PTE->refreshEditState();
    }
    else
    {
        sequencer.stop();

        PTE->trackerPattern.playheadPosition = 0;
        // PTE->trackerPattern.actualStep = 0;
        PTE->refreshPattern();

        // newFileManager.autoSaveWorkspace(1);
    }

    PTE->trackerPattern.selectColumn = 0;
    PTE->focusOnPattern();
}

static void functRecAction(uint8_t state)
{
    if (state == buttonPress)
    {
        if (PTE->fillState == 1)
        {
            return;
        }

        if (PTE->buttons->isButtonPressed(interfaceButtonShift))
        {
            if (mtPopups.getStepPopupState() ==
                stepPopupNone) // ukrywa popup nuta...fx jesli jest wyswietlany
            {
                PTE->showRecordArmingButtons(1, !isSampleTrack(PTE->trackerPattern.actualTrack));
                PTE->setRecArmingFunct(1);
            }
        }
        else
        {
            if (sequencer.isPlay() && !sequencer.isPlayingSelection())
            {
                PTE->trackerPattern.selectState = 1;
                PTE->trackerPattern.selectColumn = 0;
                PTE->isSelectingNow = 0;
            }

            if (sequencer.isRec())
            {
                sequencer.recEnable(false);
                PTE->editMode = 0;
            }
            else
            {
                PTE->editMode = !PTE->editMode;
            }
            PTE->refreshEditState();
        }
    }
    else if (state == buttonRelease)
    {
        //		PTE->refreshEditState();
    }
}

static void functInsertHome(uint8_t state)
{
    if (PTE->fillState > 0)
    {
        return; // w fill wylaczony
    }

    if (state == buttonPress || state == buttonHold)
    {
        if (PTE->buttons->isButtonPressed(interfaceButtonShift))
        {
            // HOME
            if (isMultiSelection())
            {
                PTE->trackerPattern.selectColumn = 0;
                PTE->trackerPattern.selectState = 1;
                PTE->isSelectingNow = 0;

                PTE->trackerPattern.selectStartStep = 0;
                PTE->trackerPattern.selectEndStep = 0;
                PTE->trackerPattern.selectStartTrack = PTE->trackerPattern.actualTrack;
                PTE->trackerPattern.selectEndTrack = PTE->trackerPattern.actualTrack;

                PTE->trackerPattern.actualStep = 0; // zmiana pozycji kursora
            }
            else
            {
                PTE->trackerPattern.actualStep = 0; // zmiana pozycji kursora
            }
        }
        else
        {
            if (PTE->editMode == 1)
            {
                // INSERT
                newFileManager.storePatternUndoRevision();

                sendSelection();
                sequencer.insert(&sequencer.selection);

                newFileManager.setActualPatternStructChanged();
            }
        }

        PTE->refreshPattern();
    }
}

static uint8_t getSelectedElement()
{
    if (PTE->buttons->isButtonPressed(interfaceButtonNote))
    {
        if (PTE->toggledMicVolOnPress)
        {
            PTE->isMicVolView = !PTE->isMicVolView;
            PTE->toggledMicVolOnPress = false;
        }
        return Sequencer::ELEMENTS_NOTES;
    }
    else if (PTE->buttons->isButtonPressed(interfaceButtonInstr))
    {
        return Sequencer::ELEMENTS_INSTRUMENTS;
    }
    else if (PTE->buttons->isButtonPressed(interfaceButtonFx1))
    {
        return PTE->isMicVolView ? Sequencer::ELEMENTS_MICROMOVE : Sequencer::ELEMENTS_FX1;
    }
    else if (PTE->buttons->isButtonPressed(interfaceButtonFx2))
    {
        return PTE->isMicVolView ? Sequencer::ELEMENTS_VOLUME : Sequencer::ELEMENTS_FX2;
    }
    else if (PTE->buttons->isButtonPressed(interfaceButtonPattern))
    {
        return Sequencer::ELEMENTS_ALL_WITH_PREFERENCES;
    }
    else
    {
        return Sequencer::ELEMENTS_ALL_NO_PREFERENCES;
    }
    return Sequencer::ELEMENTS_ALL_NO_PREFERENCES;
}

static void functCopyPaste(uint8_t state)
{
    if (PTE->fillState > 0)
    {
        return; // w fill wylaczony
    }

    if (state == buttonPress)
    {

        if (PTE->editMode == 1)
        {
            newFileManager.storePatternUndoRevision();

            if (PTE->buttons->isButtonPressed(interfaceButtonShift))
            {
                sendPasteSelection();
                sequencer.pasteFromBuffer(getSelectedElement());
                PTE->moveCursorByStep(sequencer.getCopySelectionHeight() - 1);

                newFileManager.setActualPatternStructChanged();
            }
            else
            {
                sendCopySelection();
                sequencer.copyToBuffer();
            }
        }

        PTE->refreshPattern();
        PTE->showPattern(0, PTE->buttons->isButtonPressed(interfaceButtonShift));
        PTE->showLength();
    }
}

static void functDeleteBackspace(uint8_t state)
{
    if (PTE->fillState > 0)
    {
        return; // w fill wylaczony
    }

    if (state == buttonPress || state == buttonHold)
    {
        if (PTE->editMode == 1)
        {
            newFileManager.storePatternUndoRevision();

            // backspace
            if (PTE->buttons->isButtonPressed(interfaceButtonShift))
            {
                if (state == buttonPress)
                {
                    sendSelection();
                    bool backspaceExecuted = sequencer.backspace();
                    if (backspaceExecuted)
                    {
                        if (isMultiSelection())
                        {
                            if (PTE->trackerPattern.selectStartStep > 0)
                            {
                                PTE->trackerPattern.selectStartStep--;
                                PTE->trackerPattern.selectEndStep--;
                                PTE->trackerPattern.actualStep--; // zmiana pozycji kursora
                            }
                        }
                        else
                        {
                            if (PTE->trackerPattern.actualStep > 0)
                                PTE->trackerPattern.actualStep--; // zmiana pozycji kursora
                        }
                    }
                }
            }
            // DELETE
            else
            {
                // wykomentowane bo chyba juz nie aktualne a robi problem
                //				PTE->dontShowPopupsUntilButtonRelease = 1;	// blokuje ponowne
                // pojawianie sie popupu
                // jesli ciagle wcisniety przycisk popupu
                // do czasu release note...fx
                //				if(mtPopups.getStepPopupState() != stepPopupNone) // ukrywa popup
                // nuta...fx jesli jest wyswietlany
                //				{
                //					PTE->insertOnPopupHideDisabled = 1;  // dezaktywuje wstawianie
                // danych na wyjecie z popupow 					PTE->cancelPopups();
                // PTE->insertOnPopupHideDisabled = 0;  // a tu aktywuje spowrotem
                //				}

                sendSelection();
                if (PTE->editParam == cPatternEditor::EditFx2)
                {
                    sequencer.clearSelected(PTE->isMicVolView ? Sequencer::ELEMENTS_VOLUME
                                                              : Sequencer::ELEMENTS_FX1);
                }
                else if (PTE->editParam == cPatternEditor::EditFx1)
                {
                    sequencer.clearSelected(PTE->isMicVolView ? Sequencer::ELEMENTS_MICROMOVE
                                                              : Sequencer::ELEMENTS_FX2);
                }
                else if (PTE->editParam == cPatternEditor::EditNote)
                {
                    sequencer.clearSelected(Sequencer::ELEMENTS_NOTES);
                }
                else
                {
                    sequencer.clearSelected(getSelectedElement());
                }
                PTE->moveCursorByStep();
            }

            newFileManager.setActualPatternStructChanged();
        }

        PTE->refreshPattern();
    }
}

void changeSelectedStepValue(int16_t value)
{
    switch (PTE->editParam)
    {
        case cPatternEditor::EditNote:
            value = value == 10 ? 12 : (value == -10 ? -12 : value);
            sequencer.changeSelectionNote(value,
                                          PTE->buttons->isButtonPressed(interfaceButtonShift));
            break;

        case cPatternEditor::EditInstr: sequencer.changeSelectionInstrument(value); break;

        case cPatternEditor::EditFx1:
        case cPatternEditor::EditFx2:
        {
            if (PTE->isMicVolView)
            {
                if (PTE->editParam == cPatternEditor::EditFx1)
                {
                    sequencer.changeSelectionMicromove(value);
                }
                else
                {
                    sequencer.changeSelectionVolume(value);
                }
                break;
            }

            uint8_t fx_index = PTE->editParam == cPatternEditor::EditFx1 ? 1 : 0;

            if (PTE->buttons->isButtonPressed(interfaceButtonFx1) ||
                PTE->buttons->isButtonPressed(interfaceButtonFx2))
            {
                sequencer.changeSelectionFxType(fx_index, value);
            }
            else
            {
                sequencer.changeSelectionFxValue(fx_index, value);
            }
            break;
        }
    }
}

void sendSelection()
{
    if (isMultiSelection())
    {
        if (PTE->trackerPattern.selectColumn)
        {
            sequencer.setSelection(0,
                                   PTE->trackerPattern.selectStartTrack,
                                   sequencer.getPatternLength() - 1,
                                   PTE->trackerPattern.selectEndTrack);
        }
        else
        {
            sequencer.setSelection(PTE->trackerPattern.selectStartStep,
                                   PTE->trackerPattern.selectStartTrack,
                                   PTE->trackerPattern.selectEndStep,
                                   PTE->trackerPattern.selectEndTrack);
        }
    }
    else
    {
        sequencer.setSelection(PTE->trackerPattern.actualStep,
                               PTE->trackerPattern.actualTrack,
                               PTE->trackerPattern.actualStep,
                               PTE->trackerPattern.actualTrack);
    }
}

void sendCopySelection()
{
    if (isMultiSelection())
    {
        sequencer.setCopySelection(PTE->trackerPattern.selectStartStep,
                                   PTE->trackerPattern.selectStartTrack,
                                   PTE->trackerPattern.selectEndStep,
                                   PTE->trackerPattern.selectEndTrack);
    }
    else
    {
        sequencer.setCopySelection(PTE->trackerPattern.actualStep,
                                   PTE->trackerPattern.actualTrack,
                                   PTE->trackerPattern.actualStep,
                                   PTE->trackerPattern.actualTrack);
    }
}

void sendPasteSelection()
{
    if (isMultiSelection())
    {
        sequencer.setPasteSelection(PTE->trackerPattern.selectStartStep,
                                    PTE->trackerPattern.selectStartTrack,
                                    PTE->trackerPattern.selectEndStep,
                                    PTE->trackerPattern.selectEndTrack);
    }
    else
    {
        sequencer.setPasteSelection(PTE->trackerPattern.actualStep,
                                    PTE->trackerPattern.actualTrack,
                                    PTE->trackerPattern.actualStep,
                                    PTE->trackerPattern.actualTrack);
    }
}

uint8_t isMultiSelection()
{
    return PTE->trackerPattern.selectState == 2;
}

uint8_t isMultiSelectionOnOneLine()
{
    if (isMultiSelection() &&
        PTE->trackerPattern.selectStartStep == PTE->trackerPattern.selectEndStep)
    {
        return 1;
    }
    return 0;
}

int16_t getActualStep()
{
    return PTE->trackerPattern.actualStep;
}

int16_t getActualTrack()
{
    return PTE->trackerPattern.actualTrack;
}

uint8_t isEditMode()
{
    return PTE->editMode == 1;
}

// ##############################################################################################
// ##########################             PATTERN PARAMS             ############################
// ##############################################################################################
static void functChangePattern(uint8_t state)
{
    if (state == buttonPress)
    {
        PTE->selectedPlace = 0;
        PTE->loadPatternsStatus();
        PTE->unfocusPattern();
        PTE->activateLabelsBorder();
        display.synchronizeRefresh();
    }
    else if (state == buttonRelease)
    {
        if (PTE->selectedPlace == 0)
        {
            PTE->focusOnPattern();
        }

        if (sequencer.sequencialSwitch_GetNext() > 0)
        {
            sequencer.sequencialSwitch_SetReady();
        }
    }
}

static void functChangePatternLength(uint8_t state)
{
    if (state == buttonPress)
    {
        PTE->selectedPlace = 1;
        PTE->unfocusPattern();
        PTE->activateLabelsBorder();
        display.synchronizeRefresh();
    }
    else if (state == buttonRelease)
    {
        if (PTE->selectedPlace == 1)
        {
            PTE->focusOnPattern();
        }
    }
}

static void functChangePatternEditStep(uint8_t state)
{
    if (state == buttonPress)
    {
        PTE->selectedPlace = 2;
        PTE->unfocusPattern();
        PTE->activateLabelsBorder();
        display.synchronizeRefresh();
    }
    else if (state == buttonRelease)
    {
        if (PTE->selectedPlace == 2)
        {
            PTE->focusOnPattern();
        }
    }
}

// ##############################################################################################
// ###############################             PREVIEW          #################################
// ##############################################################################################

static void functPreview()
{
    PTE->refreshEditState();

    PTE->setPlayheadRangeFromSelection();
    sendSelection();
    sequencer.playSelection();
}

// ##############################################################################################
// ###############################             FILL             #################################
// ##############################################################################################

static void functFill()
{
    PTE->fillState = 1;
    PTE->undoRedoButtonReleaseBlocker = 1;
    PTE->trackerPattern.popupMode |= 1;
    PTE->focusOnPattern();

    PTE->showFillPopup();

    PTE->ip->clearButtonsRange(interfaceButton0, interfaceButton7);

    PTE->ip->setButtonObj(interfaceButton6, buttonPress, functFillCancel);
    PTE->ip->setButtonObj(interfaceButton7, buttonPress, functFillApply);

    PTE->ip->setButtonObj(interfaceButton0, buttonPress, functFillAction1);
    PTE->ip->setButtonObj(interfaceButton1, buttonPress, functFillAction2);
    PTE->ip->setButtonObj(interfaceButton2, buttonPress, functFillAction3);
    PTE->ip->setButtonObj(interfaceButton3, buttonPress, functFillAction4);
    PTE->ip->setButtonObj(interfaceButton4, buttonPress, functFillAction5);
    PTE->ip->setButtonObj(interfaceButton5, buttonPress, functFillAction6);
}

static void functFillCancel()
{
    PTE->fillState = 0;
    PTE->selectedPlace = -1;

    PTE->hideFillPopup();

    PTE->start(0);
}

static void functFillApply()
{
    // zatwierdzanie wypelnienia
    if (PTE->fillState)
    {
        newFileManager.storePatternUndoRevision();
        cPatternEditor::strFill* fillData = &PTE->fillData[PTE->getFillDataIdx()];
        //(void) PTE->fillData[PTE->editParam];
        //(void) PTE->fillStep;
        // PTE->fillData[x]		<= przechowuje dane do konfiguracji fill
        // x - (0-3)(PTE->editParam) - co wypelnia - nuta , instr , vol , fx
        // 							type  	 = typ wypelninia (0-3);
        //							from	 = pierwsza wartosc (0-127 - nuty/vol/fx_val , 0-48
        // instr) 							to		 = druga wartosc (jesli potrzebna)
        // param = scala yes/no (0-1) albo typ efektu (0-xx)
        //
        // PTE->fillStep 		<= krok dla filowania
        // PTE->editParam		<= nuta / instr / vol / fx
        //--------------------------------------------------------
        // TU
        sendSelection();
        switch (PTE->editParam)
        {
            case cPatternEditor::EditNote:
                if (fillData->type == 2)
                {
                    sequencer.fillRandomNotes(fillData->fillStep,
                                              fillData->param,
                                              fillData->from,
                                              fillData->to,
                                              fillData->density[fillData->fillStep]);
                }
                else if (fillData->type == 1)
                {
                    sequencer.fillLinearNotes(fillData->fillStep,
                                              fillData->param,
                                              fillData->from,
                                              fillData->to,
                                              fillData->density[fillData->fillStep]);
                }
                else if (fillData->type == 0)
                {
                    sequencer.fillLinearNotes(fillData->fillStep,
                                              fillData->param,
                                              fillData->from,
                                              fillData->from,
                                              fillData->density[fillData->fillStep]);
                }

                break;
            case cPatternEditor::EditInstr:
                if (fillData->type == 0)
                {
                    sequencer.fillLinearInstruments(fillData->fillStep,
                                                    fillData->from,
                                                    fillData->from,
                                                    fillData->density[fillData->fillStep]);
                }
                else if (fillData->type == 1)
                {
                    sequencer.fillLinearInstruments(fillData->fillStep,
                                                    fillData->from,
                                                    fillData->to,
                                                    fillData->density[fillData->fillStep]);
                }
                else if (fillData->type == 2)
                {
                    sequencer.fillRandomInstruments(fillData->fillStep,
                                                    fillData->from,
                                                    fillData->to,
                                                    fillData->density[fillData->fillStep]);
                }

                break;

            case cPatternEditor::EditFx1:
            case cPatternEditor::EditFx2:
            {
                if (PTE->isMicVolView)
                {
                    if (PTE->editParam == cPatternEditor::EditFx1)
                    {
                        if (fillData->type == 0)
                        {
                            sequencer.fillLinearMicromove(fillData->fillStep,
                                                          fillData->from,
                                                          fillData->from,
                                                          fillData->density[fillData->fillStep]);
                        }
                        else if (fillData->type == 1)
                        {
                            sequencer.fillLinearMicromove(fillData->fillStep,
                                                          fillData->from,
                                                          fillData->to,
                                                          fillData->density[fillData->fillStep]);
                        }
                        else if (fillData->type == 2)
                        {
                            sequencer.fillRandomMicromove(fillData->fillStep,
                                                          fillData->from,
                                                          fillData->to,
                                                          fillData->density[fillData->fillStep]);
                        }
                    }
                    else
                    {
                        if (fillData->type == 0)
                        {
                            sequencer.fillLinearVolume(fillData->fillStep,
                                                       fillData->from,
                                                       fillData->from,
                                                       fillData->density[fillData->fillStep]);
                        }
                        else if (fillData->type == 1)
                        {
                            sequencer.fillLinearVolume(fillData->fillStep,
                                                       fillData->from,
                                                       fillData->to,
                                                       fillData->density[fillData->fillStep]);
                        }
                        else if (fillData->type == 2)
                        {
                            sequencer.fillRandomVolume(fillData->fillStep,
                                                       fillData->from,
                                                       fillData->to,
                                                       fillData->density[fillData->fillStep]);
                        }
                    }
                    break;
                }

                uint8_t fx_index = PTE->editParam == cPatternEditor::EditFx1 ? 1 : 0;

                if (fillData->type == 0)
                {
                    sequencer.fillLinearFx(fx_index,
                                           fillData->fillStep,
                                           fillData->param,
                                           fillData->from,
                                           fillData->from,
                                           fillData->density[fillData->fillStep]);
                }
                else if (fillData->type == 1)
                {
                    sequencer.fillLinearFx(fx_index,
                                           fillData->fillStep,
                                           fillData->param,
                                           fillData->from,
                                           fillData->to,
                                           fillData->density[fillData->fillStep]);
                }
                else if (fillData->type == 2)
                {
                    sequencer.fillRandomFx(fx_index,
                                           fillData->fillStep,
                                           fillData->param,
                                           fillData->from,
                                           fillData->to,
                                           fillData->density[fillData->fillStep]);
                }

                break;
            }
            default: break;
        }

        //--------------------------------------------------------
        functFillCancel();
        newFileManager.setActualPatternStructChanged();
    }
}

static void functFillAction1()
{
    PTE->changeFillDataByActionButton(0);
    PTE->setFillPlace(0);
    PTE->activateFillPopupBorder();
}

static void functFillAction2()
{
    PTE->changeFillDataByActionButton(1);
    PTE->setFillPlace(1);
    PTE->activateFillPopupBorder();
}

static void functFillAction3()
{
    PTE->changeFillDataByActionButton(2);
    PTE->setFillPlace(2);
    PTE->activateFillPopupBorder();
}

static void functFillAction4()
{
    PTE->changeFillDataByActionButton(3);
    PTE->setFillPlace(3);
    PTE->activateFillPopupBorder();
}

static void functFillAction5()
{
    PTE->changeFillDataByActionButton(4);
    PTE->setFillPlace(4);
    PTE->activateFillPopupBorder();
}

static void functFillAction6()
{
    PTE->changeFillDataByActionButton(5);
    PTE->setFillPlace(5);
    PTE->activateFillPopupBorder();
}

// ##############################################################################################
// ###############################       EXPORT SELECTION       #################################
// ##############################################################################################
//  Eksport do pliku zaznaczenia - wywolanie klawiatury i podpiecie funkcji klawiatury i exportu
static void functExportSelection()
{
    if (mtConfig.midi.clkIn != 0)
    {
        PTE->showClockWarning();
        PTE->setClockWarningScreenFunct();
        return;
    }
    else
    {
        PTE->saveClockConfig = 0;
    }

    PTE->exportSelection();
}

void cPatternEditor::setClockWarningScreenFunct()
{
    ip->setButtonObj(interfaceButton6, functClockWarningPopupButtons);
    ip->setButtonObj(interfaceButton7, functClockWarningPopupButtons);
}

static void functClockWarningPopupButtons(uint8_t button, uint8_t state)
{
    if (state != buttonPress)
    {
        return;
    }

    if (button == interfaceButton7)
    {
        functCancelKeyboard();
    }
    else if (button == interfaceButton6)
    {
        PTE->saveClockConfig = 255;
        PTE->exportSelection();
    }
}

void cPatternEditor::exportSelection()
{
    char localPatch[255];
    uint16_t cnt = 1;
    char keyboardName[MAX_NAME_LENGTH];

    FileSystem* fileSystem = TrackerImpl::getInstance()->getPlatform()->getFileSystem();

    do
    {
        sprintf(keyboardName, "Selection%04d", cnt);
        sprintf(localPatch,
                "Export/%s/Selection/%s.wav",
                newFileManager.getCurrentProjectName(),
                keyboardName);
        cnt++;
        if (cnt > 9999)
        {
            break;
        }
    } while (fileSystem->exists(localPatch));

    PTE->keyboardManager.fillName(keyboardName);
    PTE->keyboardManager.activateKeyboard(PROJECT_NAME_SIZE - 1);
    PTE->showKeyboardExport();
    PTE->setKeyboardExportFunctions();
}

// Auto name klawiatury exportu
static void functAutoNameKeyboard()
{
    PTE->keyboardManager.setRandomName();
}

// Cancel Export - wraca do podstawowego okna
static void functCancelKeyboard()
{
    PTE->setDefaultScreenFunct();
    PTE->setSwitchModuleFunct();
    PTE->hideKeyboardExport();
    PTE->refreshEditState();
    PTE->keyboardManager.deactivateKeyboard();
}

// Zatwierdzenie nazwy - rozpoczęcie exportu
static void functConfirmExport()
{
    char localPatch[255];

    sprintf(localPatch, "Export/%s/Selection", newFileManager.getCurrentProjectName());

    FileSystem* fileSystem = TrackerImpl::getInstance()->getPlatform()->getFileSystem();

    if (!fileSystem->exists(localPatch))
    {
        fileSystem->mkdir(0, localPatch);
    }

    sprintf(localPatch,
            "Export/%s/Selection/%s.wav",
            newFileManager.getCurrentProjectName(),
            PTE->keyboardManager.getName());

    if (fileSystem->exists(localPatch))
    {
        PTE->setOverwriteRenderFunct();
        PTE->showOverwriteExportDialog();
    }
    else
    {
        PTE->startExportSelection();
    }
}

// Zatwierdzenie nazwy - rozpoczęcie exportu - po nim zaladowanie instrumentu
static void functConfirmExportLoad()
{
    functConfirmExport();
    PTE->isLoadAfterSave = true;
}

// Zatwierdzenie znaku w klawiaturze
static void functEnterKeyExport()
{
    PTE->keyboardManager.confirmKey();
}

// Modyfikacja zaznaczenia znaku w klawiaturze
static void functLeftKeyboard()
{
    PTE->keyboardManager.makeMove('a');
}

// Modyfikacja zaznaczenia znaku w klawiaturze
static void functRightKeyboard()
{
    PTE->keyboardManager.makeMove('d');
}

// Modyfikacja zaznaczenia znaku w klawiaturze
static void functUpKeyboard()
{
    PTE->keyboardManager.makeMove('w');
}

// Modyfikacja zaznaczenia znaku w klawiaturze
static void functDownKeyboard()
{
    PTE->keyboardManager.makeMove('s');
}

// Backspace znaku z przycisku delete
static void functDeleteKeyboard()
{
    PTE->keyboardManager.makeBackspace();
}

// Zatwierdzenie znaku z przycisku insert
static void functInsertKeyboard()
{
    PTE->keyboardManager.confirmKey();
}

// Nadpisz istniejący plik
static void functOverwriteExportFile()
{
    PTE->hideOverwriteExportDialog();
    PTE->startExportSelection();
}

// Nie nadpisuj istniejącego pliku - zmien nazwę
static void functCancelOverwriteExportFile()
{
    char localPatch[PATCH_SIZE];
    char keyboardName[33];
    uint16_t cnt = 1;
    strcpy(keyboardName, PTE->keyboardManager.getName());

    FileSystem* fileSystem = TrackerImpl::getInstance()->getPlatform()->getFileSystem();

    if (strlen(keyboardName) <= 27) // 33 - 1(na zero) - 5 znakow na _%04d
    {
        do
        {
            sprintf(keyboardName, "%s_%04d", PTE->keyboardManager.getName(), cnt);
            sprintf(localPatch,
                    "Export/%s/Selection/%s.wav",
                    newFileManager.getCurrentProjectName(),
                    keyboardName);
            if (++cnt > 9999)
                break;
        } while (fileSystem->exists(localPatch));
    }

    PTE->isLoadAfterSave = false;
    PTE->keyboardManager.fillName(keyboardName);
    PTE->keyboardManager.activateKeyboard(PROJECT_NAME_SIZE - 1);

    PTE->hideOverwriteExportDialog();
    PTE->ip->unblockAllInputs();
    PTE->setKeyboardExportFunctions();
}

// ##############################################################################################
// ###############################          NAME PATTERN        #################################
// ##############################################################################################

static void functConfirmNamePattern()
{
    newFileManager.getPatternsMetadata()->setPatternName(mtProject.values.actualPattern,
                                                         PTE->keyboardManager.getName());
    functCancelKeyboard();
}

static void functConfirmNameTrack()
{
    if (PTE->trackerPattern.actualTrack < kSampleTracks)
    {
        strncpy(mtProject.values.TrackNames[PTE->trackerPattern.actualTrack],
                PTE->keyboardManager.getName(),
                TRACK_NAME_LENGTH);
        mtProject.values.TrackNames[PTE->trackerPattern.actualTrack][TRACK_NAME_LENGTH] = 0;

        newFileManager.setProjectStructChanged();
    }
    else if (PTE->trackerPattern.actualTrack < kAllTracks)
    {
        strncpy(mtProject.values.midiTrackNames[PTE->trackerPattern.actualTrack - kSampleTracks],
                PTE->keyboardManager.getName(),
                TRACK_NAME_LENGTH);
        mtProject.values
          .midiTrackNames[PTE->trackerPattern.actualTrack - kSampleTracks][TRACK_NAME_LENGTH] = 0;

        newFileManager.setProjectStructChanged();
    }

    functCancelKeyboard();
}

static void functNamePattern()
{
    snprintf(PTE->cTitlePattern,
             sizeof(PTE->cTitlePattern),
             "Edit Pattern %d Name",
             mtProject.values.actualPattern);

    PTE->titleBar->setTitle(PTE->cTitlePattern);

    char* patternName =
      newFileManager.getPatternsMetadata()->getPatternName(mtProject.values.actualPattern);

    PTE->keyboardManager.fillName(patternName);
    PTE->keyboardManager.activateKeyboard(PATTERN_NAME_LENGTH);
    PTE->showKeyboardNameEdit(PTE->cTitlePattern, true);
    PTE->setKeyboardNamePatternFunctions(functConfirmNamePattern, true);
}

static void functNameTrack()
{
    if (PTE->trackerPattern.actualTrack >= kAllTracks)
    {
        return;
    }

    snprintf(PTE->cTitlePattern,
             sizeof(PTE->cTitlePattern),
             "Edit Track %d Name",
             PTE->trackerPattern.actualTrack + 1);

    PTE->titleBar->setTitle(PTE->cTitlePattern);

    char* trackName =
      PTE->trackerPattern.actualTrack < kSampleTracks
        ? mtProject.values.TrackNames[PTE->trackerPattern.actualTrack]
        : mtProject.values.midiTrackNames[PTE->trackerPattern.actualTrack - kSampleTracks];

    PTE->keyboardManager.fillName(trackName);
    PTE->keyboardManager.activateKeyboard(TRACK_NAME_LENGTH);
    PTE->showKeyboardNameEdit(PTE->cTitlePattern, false);
    PTE->setKeyboardNamePatternFunctions(functConfirmNameTrack, false);
}
// ##############################################################################################
// ###############################            INVERT            #################################
// ##############################################################################################
static void functInvert()
{
    newFileManager.storePatternUndoRevision();

    sendSelection();
    sequencer.invertSelectedSteps(getSelectedElement());

    PTE->refreshPattern();

    newFileManager.setActualPatternStructChanged();
}

// ##############################################################################################
// ###############################       Duplicate pattern      #################################
// ##############################################################################################
static void functDuplicatePattern()
{
    if (sequencer.getPatternToUI()->track[0].length > (Sequencer::MAXSTEP / 2))
    {
        mtPopups.show(0, "Pattern is too long to make this operation.");
        return;
    }

    newFileManager.storePatternUndoRevision();

    // sendSelection();
    if (!sequencer.duplicatePattern())
    {
        // bląd
    }

    PTE->refreshPattern();

    newFileManager.setActualPatternStructChanged();
}

// ##############################################################################################
// ###############################         Expand pattern       #################################
// ##############################################################################################
static void functExpandPattern()
{
    if (sequencer.getPatternToUI()->track[0].length > (Sequencer::MAXSTEP / 2))
    {
        mtPopups.show(0, "Pattern is too long to make this operation.");
        return;
    }

    newFileManager.storePatternUndoRevision();

    // sendSelection();
    if (!sequencer.expandPattern())
    {
        // bląd
    }

    PTE->refreshPattern();

    newFileManager.setActualPatternStructChanged();
}

// ##############################################################################################
// ###############################         Shrink pattern       #################################
// ##############################################################################################
static void functShrinkPattern()
{
    if (sequencer.getPatternToUI()->track[0].length < 1)
    {
        mtPopups.show(0, "Pattern is too short to make this operation.");
        return;
    }

    newFileManager.storePatternUndoRevision();

    // sendSelection();
    if (!sequencer.shrinkPattern())
    {
        // bląd
    }

    PTE->refreshPattern();

    newFileManager.setActualPatternStructChanged();
}

// ##############################################################################################
// ###############################          Copy pattern		   #################################
// ##############################################################################################
static void functCopyPattern()
{
    sendCopySelection(); // < tylko w celu zapelnienia struktury zaznaczenia zrodla
    sequencer.copyToBuffer();
}

// ##############################################################################################
// ###############################          Paste pattern	   #################################
// ##############################################################################################
static void functPastePattern()
{
    newFileManager.storePatternUndoRevision();

    sendPasteSelection();
    sequencer.pasteFromBuffer(Sequencer::ELEMENTS_ALL_WITH_PREFERENCES);

    PTE->refreshPattern();

    newFileManager.setActualPatternStructChanged();
}

// ##############################################################################################
// ###############################            TRANSPOSE		   #################################
// ##############################################################################################
//  static void functTranspose(uint8_t state)
//{
//	//--------------------------------------------------------
//	//TU
////	if(state == buttonPress) functRamTest(1);
////	else if(state == buttonRelease) functRamTest(0);
//
//
//	//--------------------------------------------------------
//	return 1;
//}

// ##############################################################################################
// ###############################                         	   #################################
// ##############################################################################################

// ##############################################################################################
// ###############################            UNDO		   #################################
// ##############################################################################################
static void functUndo(uint8_t state)
{
    return functUndoRedo(state, true);
}

static void functRedo(uint8_t state)
{
    return functUndoRedo(state, false);
}

static void functUndoRedo(uint8_t state, bool undo)
{
    if (state == buttonPress)
    {
        PTE->selectedPlace = undo ? 5 : 6;
        PTE->unfocusPattern();
        PTE->activateLabelsBorder();
        display.synchronizeRefresh();

        PTE->undoRedoButtonReleaseBlocker = 0;
    }
    else if (state == buttonRelease)
    {

        if (PTE->undoRedoButtonReleaseBlocker)
        {
            PTE->undoRedoButtonReleaseBlocker = 0;
            return;
        }

        if (PTE->selectedPlace == 5 || PTE->selectedPlace == 6)
        {
            PTE->focusOnPattern();
        }

        if (PTE->undoRedoOpen)
        {
            PTE->undoRedoOpen = 0; // zerujemy na release
        }
        else if (undo)
        {
            newFileManager.undoPattern();
        }
        else
        {
            newFileManager.redoPattern();
        }

        PTE->refreshPattern();
        PTE->showPattern();
        PTE->showLength();
    }
}

static void functUndoRedoOnLeftRight(int8_t dir)
{
    if (dir > 0)
    {
        newFileManager.redoPattern();
        PTE->undoRedoOpen = 1;
    }
    else if (dir < 0)
    {
        newFileManager.undoPattern();
        PTE->undoRedoOpen = 1;
    }

    PTE->refreshPattern();
    PTE->showPattern();
    PTE->showLength();
}

// ##############################################################################################
// ###############################          PAD BOARD           #################################
// ##############################################################################################

void cPatternEditor::loadPatternsStatus()
{
    patternOccupyMap = 0;

    for (size_t pattern = 0; pattern < interfacePadsCount; pattern++)
    {
#ifdef OLD_VERSISION
        uint16_t data = mtProject.values.allPatternsBitmask[pattern] |
                        (mtProject.values.allPatternsBitmaskMidi[pattern] << 8);

        for (size_t track = 0; track < kAllTracks; track++)
        {
            if ((data & (1 << track)))
            {
                patternOccupyMap |= uint64_t(1) << pattern;
                break;
            }
        }
#endif
        uint8_t midiPatternBitmask = mtProject.values.allPatternsBitmaskMidi[pattern];

        if (mtProject.values.allPatternsBitmask[pattern] > 0 ||
            (midiPatternBitmask > 0 && midiPatternBitmask < (1 << kAllTracks)))
        {
            patternOccupyMap |= uint64_t(1) << pattern;
        }
    }
}

void cPatternEditor::lightUpPadBoard()
{
    padsBacklight.clearGlobalPads(1, 1, 0);

    //--------------------------------------------

    if (selectedPlace >= 0)
    {
        switch (selectedPlace)
        {
            case 0:
            {
                for (int pad = 0; pad < interfacePadsCount; pad++)
                {
                    if (pad == mtProject.values.actualPattern - 1)
                    {
                        padsBacklight.setBackLayer(1, mtConfig.values.padsLightFront, pad);
                    }
                    else if ((patternOccupyMap & (uint64_t(1) << pad)))
                    {
                        padsBacklight.setBackLayer(1, mtConfig.values.padsLightBack, pad);
                    }
                }
                break;
            }
            case 1:
            {
                uint8_t temp_length = sequencer.getPatternToUI()->track[0].length;
                if (temp_length < 3 || temp_length > 191)
                    break;
                padsBacklight.setBackLayer(
                  1, mtConfig.values.padsLightBack, mapClamped<int>(temp_length, 3, 191, 0, 47));
                break;
            }
            case 2:
            {
                padsBacklight.setBackLayer(
                  1, mtConfig.values.padsLightBack, mtProject.values.patternEditStep);
                break;
            }
        }

        return;
    }

    //--------------------------------------------

    if (editMode > 0 || sequencer.isRec())
    {
        Sequencer::strPattern* seq = sequencer.getPatternToUI();

        int8_t show_note =
          seq->track[trackerPattern.actualTrack].step[trackerPattern.actualStep].note;

        switch (editParam)
        {
            case EditNote:
            {
                bool isBeatSlice =
                  mtProject.values.lastUsedInstrument < kSampleInstrumentsCount &&
                  mtProject.instrument[mtProject.values.lastUsedInstrument].playMode ==
                    playModeBeatSlice;

                if (isBeatSlice)
                {
                    for (uint8_t pad = 0;
                         pad <
                         mtProject.instrument[mtProject.values.lastUsedInstrument].sliceNumber;
                         pad++)
                    {
                        padsBacklight.setBackLayer(1, mtConfig.values.padsLightBack, pad);
                    }
                }

                if (show_note < 0)
                    break;

                if (isBeatSlice)
                {
                    padsBacklight.setFrontLayer(1,
                                                mtConfig.values.padsLightFront,
                                                padToBeatNoteConverter.getPadForNote(show_note));
                }
                else
                {
                    if (mtPadBoard.countPadsWithNote(show_note, padsTempData))
                    {
                        for (uint8_t i = 0; i < 48; i++)
                        {
                            if (padsTempData[i])
                            {
                                padsBacklight.setBackLayer(1, mtConfig.values.padsLightBack, i);
                            }
                        }
                    }
                }

                break;
            }

            case EditInstr:
            {
                if (show_note < 0)
                    break;

                int8_t show_instr =
                  seq->track[trackerPattern.actualTrack].step[trackerPattern.actualStep].instrument;

                if (show_instr >= 0 && show_instr <= 47)
                {
                    padsBacklight.setBackLayer(1, mtConfig.values.padsLightBack, show_instr);
                }

                break;
            }

            case EditFx1:
            case EditFx2:
            {
                uint8_t show_fx = 0;

                if (isMicVolView)
                {
                    Sequencer::strPattern::strTrack::strStep& tempStep =
                      seq->track[trackerPattern.actualTrack].step[trackerPattern.actualStep];
                    int8_t val = (editParam == EditFx1) ? tempStep.micromove : tempStep.volume;
                    if (val >= 0 && val <= 100)
                    {
                        show_fx = roundf(map<float>((float)val, 0.0f, 100.0f, 0.0f, 47.0f));
                        padsBacklight.setBackLayer(1, mtConfig.values.padsLightBack, show_fx);
                    }
                    break;
                }

                if (mtPopups.getStepPopupState() != stepPopupNone)
                {
                    show_fx = mtPopups.getStepPopupValue();

                    if (show_fx < 0 || show_fx > FX_MAX)
                        break;

                    padsBacklight.setBackLayer(1, mtConfig.values.padsLightBack, show_fx);
                }
                else
                {
                    uint8_t fx_index = PTE->editParam == EditFx1 ? 1 : 0;

                    if (seq->track[trackerPattern.actualTrack]
                          .step[trackerPattern.actualStep]
                          .fx[fx_index]
                          .type != 0)
                    {
                        // co pokazywac na padach:
                        if (buttons->isButtonPressed(interfaceButtonFx1) ||
                            buttons->isButtonPressed(interfaceButtonFx2))
                        {
                            show_fx =
                              interfaceGlobals.fxIdToName(seq->track[trackerPattern.actualTrack]
                                                            .step[trackerPattern.actualStep]
                                                            .fx[fx_index]
                                                            .type); // typ
                            if (show_fx > FX_MAX)
                                break;
                        }
                        else
                        {
                            Sequencer::strPattern::strTrack::strStep& tempStep =
                              seq->track[trackerPattern.actualTrack]
                                .step[trackerPattern.actualStep];

                            show_fx = tempStep.fx[fx_index].value; // wartosc
                            if (show_fx > sequencer.getFxMax(tempStep.fx[fx_index].type))
                            {
                                break;
                            }

                            int lastHandledPad =
                              std::min(47,
                                       sequencer.getFxMax(tempStep.fx[fx_index].type) -
                                         sequencer.getFxMin(tempStep.fx[fx_index].type));

                            float showFxTemp =
                              map<float>((float)show_fx,
                                         sequencer.getFxMin(tempStep.fx[fx_index].type),
                                         sequencer.getFxMax(tempStep.fx[fx_index].type),
                                         0,
                                         lastHandledPad);

                            show_fx = roundf(showFxTemp);
                        }

                        padsBacklight.setBackLayer(1, mtConfig.values.padsLightBack, show_fx);
                    }
                }
                break;
            }
            default: break;
        }
    }
    else if (mtConfig.general.padPreviewInPatternEdit != padPreviewModeOff)
    {
        Sequencer::strPattern* seq = sequencer.getPatternToUI();
        bool patternMode = (mtConfig.general.padPreviewInPatternEdit == padPreviewModePattern);

        for (int track = patternMode ? 0 : trackerPattern.actualTrack;
             track < (patternMode ? Sequencer::TRACKS : trackerPattern.actualTrack + 1);
             track++)
        {
            int8_t show_note = seq->track[track].step[trackerPattern.actualStep].note;

            if (show_note < 0)
            {
                continue;
            }

            switch (editParam)
            {
                case EditNote:

                    if (mtPadBoard.countPadsWithNote(show_note, padsTempData))
                    {
                        for (uint8_t i = 0; i < 48; i++)
                        {
                            if (padsTempData[i])
                            {
                                padsBacklight.setBackLayer(1, mtConfig.values.padsLightBack, i);
                            }
                        }
                    }

                    break;
                case EditInstr:
                    int8_t show_instr =
                      seq->track[track].step[trackerPattern.actualStep].instrument;

                    if (show_instr >= 0 && show_instr <= 47)
                    {
                        padsBacklight.setBackLayer(1, mtConfig.values.padsLightBack, show_instr);
                    }

                    break;
            }
        }
    }
}

// tracker mini middle button
static void functMiniMiddle(uint8_t state)
{
    // oblsuga klawiatury
    if (PTE->keyboardManager.getState())
    {
        return;
    }

    // obsluga przyciskow pod ekranem
    if (PTE->selectedPlace >= 0)
    {
        return;
    }

    // obluga popupu fill
    if (PTE->fillState > 0)
    {
        return;
    }

    // tryb zielony = prewiew stepa
    if (PTE->editMode != 1 && !sequencer.isRec())
    {
        if (state != buttonPress)
        {
            return;
        }

        functPreview();

        return;
    }

    if (sequencer.isRec())
    {
        return;
    }

    switch (PTE->editParam)
    {
        case cPatternEditor::EditNote: // nuta
        {
            // xxx niedorobione
            bool isBeatSlice = mtProject.values.lastUsedInstrument < kSampleInstrumentsCount &&
                               mtProject.instrument[mtProject.values.lastUsedInstrument].playMode ==
                                 playModeBeatSlice;
            uint8_t noteFromPad = isBeatSlice
                                    ? /*padToBeatNoteConverter.getNoteFromPad(pad)*/ mtProject
                                        .values.lastUsedNote // <<<< tu moze byc zle
                                    : mtProject.values.lastUsedNote;

            sendSelection();
            if (isMultiSelectionOnOneLine())
            {
                if (state == buttonPress)
                {
                    sequencer.handleNoteOn(Sequencer::GRID_OUTSIDE_PATTERN,
                                           noteFromPad,
                                           127,
                                           mtProject.values.lastUsedNote + 100);
                }
                else if (state == buttonRelease)
                {
                    sequencer.handleNoteOff(Sequencer::GRID_OUTSIDE_PATTERN,
                                            noteFromPad,
                                            0,
                                            mtProject.values.lastUsedNote + 100);
                }
            }
            else
            {
                if (state == buttonPress)
                {
                    sequencer.setSelectionNote(noteFromPad);
                }
            }

            break;
        }

        case cPatternEditor::EditInstr: // instrument
        {
            if (state == buttonPress)
            {
                sendSelection();
                sequencer.setSelectionInstrument(mtProject.values.lastUsedInstrument);
            }
            break;
        }

        case cPatternEditor::EditFx1: // fx1
        case cPatternEditor::EditFx2: // fx2
        {
            if (state == buttonPress)
            {
                sendSelection();

                if (PTE->isMicVolView)
                {
                    if (PTE->editParam == cPatternEditor::EditFx1)
                    {
                        sequencer.changeSelectionMicromove(0);
                    }
                    else
                    {
                        sequencer.changeSelectionVolume(0);
                    }
                    break;
                }

                uint8_t fx_index = PTE->editParam == cPatternEditor::EditFx1 ? 1 : 0;

                if ((PTE->buttons->isButtonPressed(interfaceButtonFx1) ||
                     PTE->buttons->isButtonPressed(
                       interfaceButtonFx2))) //&& mtPopups.getStepPopupState() == stepPopupNone)
                {
                    PTE->dontShowPopupsUntilButtonRelease = 1;
                    sequencer.setSelectionFxType(fx_index,
                                                 interfaceGlobals.fxIDs[PTE->getPreviousUsedFx()]);
                }
                else
                {
                    sequencer.changeSelectionFxValue(fx_index, 0);
                }
            }
            break;
        }

        default: break;
    }

    if (state == buttonPress)
    {
        if (!isMultiSelection())
            PTE->moveCursorByStep();

        PTE->lightUpPadBoard();
        PTE->refreshPattern();
    }
    else if (state == buttonRelease)
    {
        if (isMultiSelectionOnOneLine() && sequencer.noMoreRecOpen())
        {
            PTE->moveCursorByStepParallel();
        }

        PTE->showActualParamOnBar();
        PTE->lightUpPadBoard();
        PTE->refreshPattern();
    }
}

// The pad board has more pads than there are FX; the last pads select nothing.
static void setSelectionFxTypeFromPad(uint8_t fxIndex, uint8_t pad)
{
    if (pad >= FX_COUNT)
    {
        return;
    }

    sequencer.setSelectionFxType(fxIndex, interfaceGlobals.fxIDs[pad]);
}

static void functPads(uint8_t pad, uint8_t state, int16_t velo)
{
    (void)velo;

    if (PTE->keyboardManager.getState())
    {
        PTE->keyboardManager.onPadChange(pad, state);
        return;
    }

    // obsługa przycisków pod ekranem (na poczatku bo dziala tez bez editmode)
    if (PTE->selectedPlace >= 0)
    {
        switch (PTE->selectedPlace)
        {
            case 0:
                PTE->setActualPattern(pad + 1);
                PTE->showPattern();
                PTE->loadPatternsStatus();
                break;
            case 1:
                PTE->setActualPatternLength(mapClamped<int>(pad, 0, 47, 3, 191));
                newFileManager.setActualPatternStructChanged();
                PTE->showLength();
                break;
            case 2:
                PTE->setActualPatternEditStep(pad);
                newFileManager.setProjectStructChanged();
                PTE->showStep();
                break;
        }

        PTE->lightUpPadBoard();

        return;
    }

    // obsluga podswietlenia
    if (state == buttonRelease)
    {
        padsBacklight.setFrontLayer(0, 0, pad);
    }
    else if (state == buttonPress)
    {
        padsBacklight.setFrontLayer(1, mtConfig.values.padsLightFront, pad);
    }
    else
    {
        return;
    }

    // tryb zielony = granie na padach
    if (PTE->editMode != 1 && !sequencer.isRec())
    {
        sendSelection();

        bool isBeatSlice =
          mtProject.values.lastUsedInstrument < kSampleInstrumentsCount &&
          mtProject.instrument[mtProject.values.lastUsedInstrument].playMode == playModeBeatSlice;

        uint8_t noteToPlay = mtPadBoard.getNoteFromPad(pad, isBeatSlice);

        if (PTE->editParam == cPatternEditor::EditInstr)
        {
            mtProject.values.lastUsedInstrument = pad;
            noteToPlay = sequencer.getSelectionFirstNote();
        }

        if (state == buttonPress)
        {
            sequencer.handleNoteOn(Sequencer::GRID_INSIDE_PATTERN, noteToPlay, -1, pad);
        }
        else if (state == buttonRelease)
        {
            //			uint8_t noteFromPad = mtPadBoard.getNoteFromPad(pad);
            sequencer.handleNoteOff(Sequencer::GRID_INSIDE_PATTERN, noteToPlay, 0, pad);
        }
        return;
    }

    // obluga popupow fill/randomise
    if (PTE->fillState > 0)
    {
        if (state == buttonPress)
        {
            PTE->changneFillDataByPad(pad);
        }
        return;
    }

    // zabezpeczenie przed modyfikowaniem nie widocznych parametrow
    if (PTE->patternViewMode > 0 && !(PTE->patternViewMode & (1 << PTE->editParam)))
    {
        return;
    }

    // tryb czerwony = wprowadzanie danych
    if (PTE->editMode == 1 || sequencer.isRec())
    {

        switch (PTE->editParam)
        {
            case cPatternEditor::EditNote: // nuta
            {
                bool isBeatSlice =
                  mtProject.values.lastUsedInstrument < kSampleInstrumentsCount &&
                  mtProject.instrument[mtProject.values.lastUsedInstrument].playMode ==
                    playModeBeatSlice;
                uint8_t noteFromPad = mtPadBoard.getNoteFromPad(pad, isBeatSlice);
                if (sequencer.isRec())
                {
                    sendSelection();
                    if (state == buttonPress)
                    {
                        sequencer.handleNoteOn(
                          Sequencer::GRID_OUTSIDE_PATTERN, noteFromPad, 127, pad);
                    }
                    else if (state == buttonRelease)
                    {
                        sequencer.handleNoteOff(
                          Sequencer::GRID_OUTSIDE_PATTERN, noteFromPad, 0, pad);
                    }
                }
                else
                {
                    sendSelection();
                    if (isMultiSelectionOnOneLine())
                    {
                        if (state == buttonPress)
                        {
                            sequencer.handleNoteOn(
                              Sequencer::GRID_OUTSIDE_PATTERN, noteFromPad, 127, pad);
                        }
                        else if (state == buttonRelease)
                        {
                            sequencer.handleNoteOff(
                              Sequencer::GRID_OUTSIDE_PATTERN, noteFromPad, 0, pad);
                        }
                    }
                    else
                    {
                        if (state == buttonPress)
                        {
                            sequencer.setSelectionNote(noteFromPad);
                        }
                    }
                }
                break;
            }

            case cPatternEditor::EditInstr: // instrument
            {

                if (sequencer.isRec())
                {
                    sendSelection();
                    if (state == buttonPress)
                    {
                        //					sequencer.setSelectionInstrument(pad);

                        sequencer.handleNoteOn(Sequencer::GRID_INSIDE_PATTERN_INSTRUMENT,
                                               Sequencer::STEP_NOTE_DEFAULT,
                                               127,
                                               pad);
                    }
                    else if (state == buttonRelease)
                    {

                        sequencer.handleNoteOff(Sequencer::GRID_INSIDE_PATTERN_INSTRUMENT,
                                                Sequencer::STEP_NOTE_DEFAULT,
                                                0,
                                                pad);
                    }
                }
                else
                {
                    if (state == buttonPress)
                    {
                        sendSelection();

                        int instrumentToSet =
                          PTE->isSelectionOrCursorInMidiTracks()
                            ? MIDI_INSTRUMENTS_START_IDX + (pad % kMidiInstrumentsCount)
                            : pad;
                        sequencer.setSelectionInstrument(instrumentToSet);
                    }
                }
                break;
            }

            case cPatternEditor::EditFx1: // fx1
            case cPatternEditor::EditFx2: // fx2
            {
                if (state == buttonPress)
                {
                    sendSelection();

                    if (PTE->isMicVolView)
                    {
                        if (PTE->editParam == cPatternEditor::EditFx1)
                        {
                            sequencer.setSelectionMicromoveByPad(pad);
                        }
                        else
                        {
                            sequencer.setSelectionVolumeByPad(pad);
                        }
                        break;
                    }

                    uint8_t fx_index = PTE->editParam == cPatternEditor::EditFx1 ? 1 : 0;

                    if ((PTE->buttons->isButtonPressed(interfaceButtonFx1) ||
                         PTE->buttons->isButtonPressed(
                           interfaceButtonFx2))) //&& mtPopups.getStepPopupState() == stepPopupNone)
                    {
                        PTE->dontShowPopupsUntilButtonRelease = 1;
                        setSelectionFxTypeFromPad(fx_index, pad);
                    }
                    else
                    {
                        sequencer.setSelectionFxValueByPad(fx_index, pad);
                    }
                }
                break;
            }

            default: break;
        }

        if (state == buttonPress)
        {
            if (!isMultiSelection())
            {
                PTE->moveCursorByStep();
            }
            PTE->lightUpPadBoard();

            PTE->refreshPattern();
        }
        else if (state == buttonRelease)
        {
            if (isMultiSelectionOnOneLine() && sequencer.noMoreRecOpen())
            {
                PTE->moveCursorByStepParallel();
            }

            PTE->showActualParamOnBar();
            PTE->lightUpPadBoard();
            PTE->refreshPattern();
        }
    }
}

// ##############################################################################################
// ###############################       STEP POPUPS            #################################
// ##############################################################################################

void cPatternEditor::setStepButtonState(uint8_t button, uint8_t state)
{
    if (state)
    {
        if (mtPopups.getStepPopupState() != stepPopupNone ||
            buttons->isButtonPressed(interfaceButtonShift) ||
            buttons->isButtonPressed(interfaceButtonPattern) ||
            PTE->dontShowPopupsUntilButtonRelease)
        {
            return;
        }

        if (stepButtonsState[0] || stepButtonsState[1] || stepButtonsState[2] ||
            stepButtonsState[3])
        {
            return;
        }

        stepButtonsState[button] = 1;
        stepButtonsTimer[button] = 0;
    }
    else
    {
        stepButtonsState[button] = 0;
    }
}

void cPatternEditor::showNotePopup()
{
    if (PTE->fillState == 1)
    {
        return;
    }

    if (PTE->toggledMicVolOnPress)
    {
        PTE->isMicVolView = !PTE->isMicVolView;
        PTE->toggledMicVolOnPress = false;
        PTE->refreshPattern();
    }

    int8_t noteIdxSelect =
#ifdef TRACKER_MINI
      mtPopups.getNoteKeyboardFirstPadIdx();
#else
      sequencer.getPatternToUI()
        ->track[PTE->trackerPattern.actualTrack]
        .step[PTE->trackerPattern.actualStep]
        .note;
    if (noteIdxSelect >= 0 && mtPadBoard.countPadsWithNote(noteIdxSelect, PTE->padsTempData))
    {
        for (uint8_t i = 0; i < 48; i++)
        {
            if (PTE->padsTempData[i])
            {
                noteIdxSelect = i;
            }
        }
    }
    else
    {
        noteIdxSelect = -1;
    }
#endif

    mtPopups.showStepPopup(stepPopupNote, noteIdxSelect);
}

void cPatternEditor::showInstrumentPopup()
{
    if (PTE->fillState == 1)
    {
        return;
    }

    songTimer.hide();
    hideActualParamOnBar();

    // odswiezenie paternu bez danych zakrytych przez popup
    PTE->trackerPattern.popupMode |= 2;
    display.refreshControl(PTE->patternControl);

    if (trackerPattern.actualTrack >= kSampleTracks)
    {
        mtPopups.showStepPopup(stepPopupOnlySynthAndMidiInstr, mtProject.values.lastUsedInstrument);
    }
    else
    {
        mtPopups.showStepPopup(stepPopupInstr, mtProject.values.lastUsedInstrument);
    }
}

void cPatternEditor::showFx1Popup()
{
    if (PTE->fillState == 1 || PTE->isMicVolView)
    {
        return;
    }

    songTimer.hide();
    hideActualParamOnBar();

    PTE->ip->clearButton(interfaceButtonFx1);

    // odswiezenie paternu bez danych zakrytych przez popup
    PTE->trackerPattern.popupMode |= 2;
    if (mtConfig.interface.fxPopupDescription)
    {
        PTE->trackerPattern.popupMode |= 4;
    }
    display.refreshControl(PTE->patternControl);

    setPreviousUsedFx(PTE->getStepFx());
    mtPopups.showStepPopup(stepPopupFx, getPreviousUsedFx()); // PTE->getStepFx()
}

void cPatternEditor::showFx2Popup()
{
    if (PTE->fillState == 1 || PTE->isMicVolView)
    {
        return;
    }

    songTimer.hide();
    hideActualParamOnBar();

    PTE->ip->clearButton(interfaceButtonFx2);

    // odswiezenie paternu bez danych zakrytych przez popup
    PTE->trackerPattern.popupMode |= 2;
    if (mtConfig.interface.fxPopupDescription)
        PTE->trackerPattern.popupMode |= 4;
    display.refreshControl(PTE->patternControl);

    setPreviousUsedFx(PTE->getStepFx());
    mtPopups.showStepPopup(stepPopupFx, getPreviousUsedFx()); // PTE->getStepFx()
}

void cPatternEditor::focusOnPattern()
{
    PTE->selectedPlace = -1;
    PTE->activateLabelsBorder();

    PTE->lightUpPadBoard();

    if (!PTE->editMode)
    {
        if (sequencer.isRec())
        {
            activateRedSelection();
        }
        else
        {
            activateGreenSelection();
        }
    }
    else
    {
        activateRedSelection();
    }

    display.refreshControl(patternControl);
}

void cPatternEditor::unfocusPattern()
{
    lightUpPadBoard();

    deactivateSelection();

    display.refreshControl(patternControl);
}

// ##############################################################################################
// ###############################                              #################################
// ##############################################################################################
void cPatternEditor::setPatternViewMode(uint8_t param)
{
    if (param == 0) // wroc do starego widoku domyslnego
    {
        if (PTE->patternViewChanged == 0)
        {
            PTE->patternViewMode = 0;
            PTE->focusOnActual();
            display.setControlValue(PTE->patternControl, PTE->patternViewMode);
            display.refreshControl(PTE->patternControl);
        }
        else
        {
            PTE->patternViewChanged = 0;
        }

        selectedParams[0] = 0;
        selectedParams[1] = 0;

        return;
    }

    if (selectedParams[0] == 0 ||
        (selectedParams[0] > 0 &&
         buttons->isButtonPressed((interfaceButtonNote - 1) + selectedParams[0]) == 0))
    {
        selectedParams[0] = param;
        selectedParams[1] = 0;
    }
    else if (selectedParams[1] == 0 && selectedParams[0] != param)
    {
        selectedParams[1] = param;
    }
    else if (selectedParams[1] != param)
    {
        selectedParams[0] = selectedParams[1];
        selectedParams[1] = param;
    }

    PTE->patternViewMode = 0;
    if (selectedParams[0] > 0)
        PTE->patternViewMode = (1 << (selectedParams[0] - 1));
    if (selectedParams[1] > 0)
        PTE->patternViewMode |= (1 << (selectedParams[1] - 1));

    PTE->patternViewChanged = 1;
    PTE->focusOnActual();
    display.setControlValue(PTE->patternControl, PTE->patternViewMode);
    display.refreshControl(PTE->patternControl);
}

void cPatternEditor::changePatternViewMode(uint8_t param)
{
    if (patternViewMode == 0 || param == 0)
        return; // standardowy widok 4 parametrow
    if (patternViewMode & (1 << (param - 1)))
        return; // juz widac wybrany parametr to nic nie rob

    uint8_t param_count = (patternViewMode & 1) + ((patternViewMode >> 1) & 1) +
                          ((patternViewMode >> 2) & 1) + ((patternViewMode >> 3) & 1);

    if (param_count > 1)
        patternViewMode = (1 << (param - 1)) | (1 << (previousEditParam));
    else if (param_count == 1)
        patternViewMode = (1 << (param - 1));
    else
        return;

    display.setControlValue(PTE->patternControl, PTE->patternViewMode);
    display.refreshControl(PTE->patternControl);
}

void cPatternEditor::refreshLastUsedInstrument()
{
    Sequencer::strPattern* seq = sequencer.getPatternToUI();
    Sequencer::strPattern::strTrack::strStep* actualStep =
      &seq->track[PTE->trackerPattern.actualTrack].step[PTE->trackerPattern.actualStep];

    if (actualStep->note >= 0)
    {
        mtProject.values.lastUsedNote = actualStep->note;

#if GUI_HANDLE_SYNTH
        int kMaxInstruments =
          kSampleInstrumentsCount + kMidiInstrumentsCount + kSynthInstrumentsCount - 1;
#else
        int kMaxInstruments = kSampleInstrumentsCount + kMidiInstrumentsCount - 1;
#endif
        mtProject.values.lastUsedInstrument =
          clamp<int>(actualStep->instrument, 0, kMaxInstruments);
    }
}

static void functSwitchModule(uint8_t button)
{
    newFileManager.saveWorkspacePatternNow(mtProject.values.actualPattern);

    PTE->eventFunct(eventSwitchModule, PTE, &button, 0);
}

// ##############################################################################################
// ########################        ROZSZEZONE PRZYCISKI POD EKRANEM         #####################
// ##############################################################################################

// pierwsza strona akcji
void cPatternEditor::setActionButtonPage1Funct()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton6);

    ip->setButtonObj(interfaceButton0, functChangePattern);
    ip->setButtonObj(interfaceButton1, functChangePatternLength);
    ip->setButtonObj(interfaceButton2, functChangePatternEditStep);

    if (editMode)
    {
        ip->setButtonObj(interfaceButton3, buttonPress, functFill);
        ip->setButtonObj(interfaceButton4, buttonPress, functPreview);
        ip->setButtonObj(interfaceButton5, functUndo);
        ip->setButtonObj(interfaceButton6, functRedo);
    }
    else
    {
        ip->setButtonObj(interfaceButton4, buttonPress, functPreview);
    }

    ip->setButtonObj(interfaceButton7, buttonPress, functNextActionButtonPage);
}

// druga strona akcji
void cPatternEditor::setActionButtonPage2Funct()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton6);

    if (editMode)
    {
        ip->setButtonObj(interfaceButton0, buttonPress, functExportSelection);
        ip->setButtonObj(interfaceButton1, buttonPress, functInvert);
        ip->setButtonObj(interfaceButton2, buttonPress, functDuplicatePattern);
        ip->setButtonObj(interfaceButton3, buttonPress, functExpandPattern);
        ip->setButtonObj(interfaceButton4, buttonPress, functShrinkPattern);
        ip->setButtonObj(interfaceButton5, buttonPress, functCopyPattern);
        ip->setButtonObj(interfaceButton6, buttonPress, functPastePattern);
    }
    else
    {
    }

    ip->setButtonObj(interfaceButton7, buttonPress, functNextActionButtonPage);
}

// third buttons page
void cPatternEditor::setActionButtonPage3Funct()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton6);

    ip->setButtonObj(interfaceButton0, buttonPress, functNamePattern);
    ip->setButtonObj(interfaceButton1, buttonPress, functNameTrack);

    ip->setButtonObj(interfaceButton7, buttonPress, functNextActionButtonPage);
}

// odsiwez przypisanie akcji oraz grafike przyciskow pod ekranem
void cPatternEditor::refreshActualActionButtonPage()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);

    switch (actualActionButtonsPage)
    {
        case 0:
        {
            setActionButtonPage1Funct();
            showActionButtonPage1Labels();
            break;
        }
        case 1:
        {
            setActionButtonPage2Funct();
            showActionButtonPage2Labels();
            break;
        }
        case 2:
        {
            setActionButtonPage3Funct();
            showActionButtonPage3Labels();
            break;
        }
        default: break;
    }
}

void cPatternEditor::setPreviousUsedFx(uint8_t newValue)
{
    mtProject.values.lastUsedFx = newValue;
}

uint8_t cPatternEditor::getPreviousUsedFx()
{
    return mtProject.values.lastUsedFx;
}

void cPatternEditor::setPreviousUsedFxValue(uint8_t newValue)
{
    previousFxValue = newValue;
}

uint8_t cPatternEditor::getPreviousUsedFxValue()
{
    return previousFxValue;
}

static void functNextActionButtonPage()
{
    bool anyPushed = false;
    for (uint8_t button = interfaceButton0; button < interfaceButton7; button++)
    {
        if (PTE->buttons->isButtonPressed(button))
        {
            anyPushed = true;
        }
    }

    if (anyPushed)
    {
        return;
    }

    PTE->actualActionButtonsPage++;
    if (PTE->actualActionButtonsPage > PTE->actionButtonsPagesCount - 1)
    {
        PTE->actualActionButtonsPage = 0;
    }

    PTE->refreshActualActionButtonPage();
}

// ##############################################################################################
// ###############################                              #################################
// ##############################################################################################

static void functMenu(uint8_t state)
{
    if (state == buttonPress)
    {
        if (mtPopups.isNoteKeyboardPopupActive())
        {
            mtPopups.toggleNoteKeyboardPopup();
            PTE->setDefaultScreenFunct();
            PTE->refreshEditState();
        }

        mtMenuPopup.showModulePopup(PTE);
    }
    else if (state == buttonRelease)
    {
        mtMenuPopup.hideModulePopup(PTE);
    }
}
