#include "mtSliceDetector.h"
#include <mtAudioExport.h>
#include <mtRandomNameGenerator.h>

#include "core/interfaceMenuPopup.h"
#include "core/interfacePopups.h"

#include "projectEditor/projectEditor.h"

#include "mtAudioEngine.h"
#include "mtPadBoard.h"
#include "sampleRecorder/sampleRecorder.h"

#include "performanceMode/performanceEngine.h"
#include "performanceMode/performanceMode.h"

#include "FileBrowser.h"
#include "fileBrowser/GlobalFileBrowser.h"
#include "fileManager.h"

#include "Platform.h"
#include "config.h"
#include "mtPadsBacklight.h"
#include "mtMasterEnhancer.h"
#include "util_math.h"
#include "util_string.h"

#ifdef GUI_HANDLE_SYNTH
#include "Synth.h"
#endif

#include <cr_section_macros.h>

extern uint8_t externalRamScratchBuff[];
static_assert(kMaxFilesListLength * kMaxFileNameLen <= SHARED_SCRATCH_MEMORY_SIZE,
              "fileBrowserData buffer exceeds shared scratch size");

cProjectEditor projectEditor;
cProjectEditor* PE = &projectEditor;

static void functMiniMiddle(uint8_t state);
static void functPads(uint8_t pad, uint8_t state, int16_t velo);
static void functMenu(uint8_t state);

//****************************************************
// Main screen
static void functNewProject();
static void functOpenProject();
static void functImportMod();
static void functSaveProject();
static void functSaveAsProject();
static void functExport();
//****************************************************
// New project
static void functSaveChangesCancelNewProject();
static void functSaveChangesDontSaveNewProject();
static void functSaveChangesSaveNewProject();
//****************************************************
// Save As
static void functCreateNewFolder();
static void functNewFolderConfirm();

static void functBrowserSave();
static void functKeyboardCancel();
static void functSaveAsConfirm();
static void functKeyboardAutoName();
static void functSaveAsOverwriteYes();
//****************************************************
// Open
static void functEnterBrowser();
static void functCloseBrowser();
static void functCancelBrowser();
static void functSaveChangesCancelOpen();
static void functSaveChangesDontSaveOpenBrowser();
static void functSaveChangesSaveOpen();
static void functOpenProjectMod();
//****************************************************
// Export
static void functExportSong();
static void functExportSongStems();
static void functExportPattern();
static void functExportPatternStems();
static void functExportToMOD();
static void functExportSamples();
static void functExportCancel();
static void functExportGoBack();
static void functExportProjectSamplesCancel();
static void functSaveChangesCancelExportMod();
static void functSaveChangesDontSaveExportMod();
static void functSaveChangesSaveExportMod();

static void functClockWarningPopupButtons(uint8_t button, uint8_t state);
//****************************************************
static void functSwitchModule(uint8_t button);

static void functStopPatternYes();
static void functStopPatternNo();

static void functLeft();
static void functRight();
static void functUp();
static void functDown();

static void functKeyboardConfirmKey();
static void functDeleteBackspace(uint8_t state);
static void functEncoder(int16_t value);
static void functDeleteFileFolder();
static void functDeleteMod();
static void functDeleteProject_Confirm();
static void functDeleteMod_Confirm();
static void functPlayAction();

static void makeMoveOnActiveList(int16_t moveDir);

void cProjectEditor::update()
{
    uint8_t managerStatus = newFileManager.getStatus();
    if (managerStatus == fmExportSoundEnd)
    {
        PE->showExportWindow();
        PE->setExportWindowFuncts();
        ip->unblockAllInputs();
        newFileManager.clearStatus();
        interfaceGlobals.refreshFileExplorer = true;

        if (saveClockConfig > 0 && saveClockConfig < 3)
        {
            mtConfig.midi.clkIn = saveClockConfig;
        }
    }
    else if (managerStatus == fmExportModEnd)
    {
        PE->showExportWindow();
        PE->setExportWindowFuncts();
        ip->unblockAllInputs();
        newFileManager.clearStatus();
    }
    else if (managerStatus == fmSaveEnd)
    {
        ip->unblockAllInputs();
        newFileManager.clearStatus();
        interfaceGlobals.refreshFileExplorer = true;

        AfterSaveOperation operation = afterSaveOperation;
        afterSaveOperation = noOperationAfterSave;

        if (operation == newProjectAfterSave)
        {
            createNewProject();
        }
        else if (operation == openProjectAfterSave)
        {
            if (newFileManager.openProject(afterSavePath, afterSaveName))
            {
                PE->ip->blockAllInputs();
            }
        }
        else if (operation == openRecentAfterSave)
        {
            if (newFileManager.openRecentProject(PE->recentProjectListPos))
            {
                PE->ip->blockAllInputs();
            }
        }
        else if (operation == importModAfterSave)
        {
            newFileManager.createNewProjectInWorkspace();
            if (newFileManager.openProjectFromWorkspace())
            {
                newFileManager.importModAfterLoadNewProject(afterSavePath, afterSaveName);
                PE->ip->blockAllInputs();
            }
        }
        else if (operation == exportModAfterSave)
        {
            functExportToMOD();
        }

        cleanupAfterSaveData();
    }
    else if (managerStatus >= fmError) // a tu wszelakie errory
    {
        setDefaultScreenFunct();
        showDefaultScreen();
        ip->unblockAllInputs();
        newFileManager.clearStatus();
    }
    else if (managerStatus >= fmLoadEnd) // catch rest of ends
    {
        setDefaultScreenFunct();
        showDefaultScreen();
        ip->unblockAllInputs();
        newFileManager.clearStatus();
    }

    if (refreshCover)
    {
        if (refreshCover == 1 && coverDelay > 2000)
        {
            char coverPath[PATCH_SIZE + 13];
            newFileManager.getCurrentProjectPathWithName(coverPath, PATCH_SIZE);
            strcat(coverPath, "/cover");

            if (display.readImgFromSd(coverPath, 670000, 400, 400))
            {
                refreshCover = 0;
            }
            else
            {
                refreshCover = 2;
            }
        }
        else if (refreshCover == 2)
        {
            if (display.isImgLoaded())
            {
                display.setControlShow(coverImg);
                display.refreshControl(coverImg);
                refreshCover = 0;
            }
        }
    }
}

void cProjectEditor::start([[maybe_unused]] uint32_t options)
{
    moduleRefresh = 1;

    showDefaultScreen();
    setDefaultScreenFunct();

    lightUpPadBoard();
}

void cProjectEditor::stop()
{
    cleanupListsData();
    if (preserveAfterSaveOnStop)
    {
        preserveAfterSaveOnStop = false;
    }
    else
    {
        cleanupAfterSaveData();
        // The path and name of a pending open are freed above; it must not run without them.
        afterSaveOperation = noOperationAfterSave;
    }

    sequencer.stopManualNotes();
    mtPopups.hideInfoPopup();

    currentBrowserMode = kNone;
    moduleRefresh = 0;
    refreshCover = 0;
}

void cProjectEditor::setDefaultScreenFunct()
{
    // funkcje
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearAllPots();
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

    ip->setButtonObj(interfaceButtonMiniMenu, functMenu);

    ip->setButtonObj(interfaceButton0, buttonPress, functNewProject);
    ip->setButtonObj(interfaceButton1, buttonPress, functOpenProject);
    ip->setButtonObj(interfaceButton2, buttonPress, functImportMod);
    ip->setButtonObj(interfaceButton3, buttonPress, functSaveProject);
    ip->setButtonObj(interfaceButton4, buttonPress, functSaveAsProject);
    ip->setButtonObj(interfaceButton5, buttonPress, functExport);

    ip->setButtonObj(interfaceButtonPlay, buttonPress, functPlayAction);

    ip->setButtonObj(interfaceButtonInsert, buttonPress, functKeyboardConfirmKey);
    ip->setButtonObj(interfaceButtonLeft, buttonPress, functLeft);
    ip->setButtonObj(interfaceButtonRight, buttonPress, functRight);
    ip->setButtonObj(interfaceButtonUp, buttonPress, functUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functDown);
    ip->setButtonObj(interfaceButtonDelete, functDeleteBackspace);

    ip->setPotObj(interfacePot0, functEncoder);
    ip->setButtonObj(interfaceButtonMiniMiddle, functMiniMiddle);

    ip->setPadsGlobal(functPads);
}

void cProjectEditor::setExportWindowFuncts()
{
    PE->ip->clearButtonsRange(interfaceButton0, interfaceButton7);

    PE->ip->setButtonObj(interfaceButton0, buttonPress, functExportSong);
    PE->ip->setButtonObj(interfaceButton1, buttonPress, functExportSongStems);
    PE->ip->setButtonObj(interfaceButton2, buttonPress, functExportPattern);
    PE->ip->setButtonObj(interfaceButton3, buttonPress, functExportPatternStems);
    PE->ip->setButtonObj(interfaceButton4, buttonPress, functExportToMOD);
    PE->ip->setButtonObj(interfaceButton5, buttonPress, functExportSamples);

    PE->ip->setButtonObj(interfaceButton7, buttonPress, functExportGoBack);

    PE->ip->clearButton(interfaceButtonPlay);
}

void cProjectEditor::setClockWarningScreenFunct()
{
    ip->setButtonObj(interfaceButton6, functClockWarningPopupButtons);
    ip->setButtonObj(interfaceButton7, functClockWarningPopupButtons);
}
//==============================================================================================================
//==============================================================================================================
static void functPlayAction()
{
    if (sequencer.getSeqState() == Sequencer::SEQ_STATE_STOP)
    {
        if (PE->buttons->isButtonPressed(interfaceButtonShift))
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

uint8_t cProjectEditor::loadProjectValues()
{

    if (mtProject.values.isSnapToZeroEnabled > 1)
    {
        mtProject.values.isSnapToZeroEnabled = 0;
    }

    if (mtProject.values.delayFilterFrequency < DELAY_FILTER_FREQ_MIN ||
        mtProject.values.delayFilterFrequency > DELAY_FILTER_FREQ_MAX)
    {
        mtProject.values.delayFilterFrequency = 20000;
    }

    if (mtProject.values.isHeadroomEnabled > 1)
    {
        mtProject.values.isHeadroomEnabled = 0;
    }

    if (mtProject.values.saturationType >= SaturationType::kSaturationTypeEnd)
    {
        mtProject.values.saturationType = SaturationType::kHardClip;
    }

    engine.setDelayFeedback(mtProject.values.delayFeedback);
    engine.setDelayTime(mtProject.values.delayTime);
    engine.setDelayParams(mtProject.values.delayParams);
    engine.setDelayFilterFrequency(mtProject.values.delayFilterFrequency);
    engine.resetDelay();
    engine.setHeadroomEnable((bool)mtProject.values.isHeadroomEnabled);
    engine.setSnapToZeroEnable((bool)mtProject.values.isSnapToZeroEnabled);
    engine.setSaturationType((SaturationType)mtProject.values.saturationType);
    engine.setLimiterAttack(mtProject.values.limiterAttack);
    engine.setLimiterRelease(mtProject.values.limiterRelease);
    engine.setLimiterThreshold(mtProject.values.limiterThreshold);
    engine.setLimiterSidechain(mtProject.values.limiterSidechain);
    engine.setReverbSize(mtProject.values.reverb.size);
    engine.setReverbDamp(mtProject.values.reverb.damp);
    engine.setReverbPredelay(mtProject.values.reverb.predelay);
    engine.setReverbDiffusion(mtProject.values.reverb.diffusion);
    engine.setInterpolationEnable(mtProject.values.antialiasingEnable);

    masterEnhancer.fiveBandsEq.change(mtProject.values.eqBandAdjustment);
    masterEnhancer.changeBassBoost(mtProject.values.bassBoost);
    masterEnhancer.changeStereoBoost(mtProject.values.stereoBoost);
    engine.refreshDelayVolume();
    engine.refreshReverbVolume();
    engine.refreshTrackVolume();
    engine.refreshLineInStreamVolume();
    engine.refreshLineInStreamDelaySend();
    engine.refreshLineInStreamReverbSend();

    //----------------------------------------------------------------------------------------------------

    mtPadBoard.setPadNotes(mtProject.values.padBoardScale,
                           mtProject.values.padBoardNoteOffset,
                           mtProject.values.padBoardRootNote);
    mtPadBoard.configureInstrumentPlayer(8);

    //----------------------------------------------------------------------------------------------------
    sampleRecorder.recorderConfig.gainLineIn = mtProject.values.gainLineIn;
    sampleRecorder.recorderConfig.gainMicHigh = mtProject.values.gainMicHigh;
    sampleRecorder.recorderConfig.gainMicLow = mtProject.values.gainMicLow;
    sampleRecorder.recorderConfig.gainRadio = mtProject.values.gainRadio;
    sampleRecorder.recorderConfig.monitor = mtProject.values.monitor;
    sampleRecorder.recorderConfig.radioFreq = mtProject.values.radioFreq;
    sampleRecorder.recorderConfig.source =
      mtProject.values.source >= sourceCount ? 0 : mtProject.values.source;

    //----------------------------------------------------------------------------------------------------
    // performance mode i inne
    for (uint8_t i = 0; i < kMaxAllTracksInTrackers; i++)
    {
        mtProject.perfTracksPatterns[i] = 1;
    }

    for (uint8_t i = 0; i < kSampleTracks; i++)
    {
        // paterny na trakach w performance mode
        if (mtProject.values.perfTracksPatterns[i] < 1 ||
            mtProject.values.perfTracksPatterns[i] > 255)
        {
            mtProject.values.perfTracksPatterns[i] = 1;
        }

        // globalne mute trackow
        if (mtProject.values.trackMute[i] >= trackMasterModeCount)
            mtProject.values.trackMute[i] = 0;

        engine.muteTrack(i, mtProject.values.trackMute[i]);
    }

    for (uint8_t i = 0; i < kMaxMidiTracksInTrackers; i++)
    {
        if (mtProject.values.midiTrackMute[i] >= trackMasterModeCount)
        {
            mtProject.values.midiTrackMute[i] = 0;
        }
    }

    for (uint8_t i = 0; i < 12; i++)
    {
        if (mtProject.values.perfFxPlaces[i] > performance.getFxCount() - 1)
        {
            mtProject.values.perfFxPlaces[i] = (i + 1 < performance.getFxCount()) ? i + 1 : 0;
        }
        if (mtProject.values.perfSelectedValues[i] > 3)
            mtProject.values.perfSelectedValues[i] = 0;

        // if(mtProject.values.perfFxValues[place][0] > 255 ||
        // mtProject.values.perfFxValues[place][0] < -255)
        if (mtProject.values.perfFxValues[i][0] != 0)
            mtProject.values.perfFxValues[i][0] = 0;
        if (mtProject.values.perfFxValues[i][1] > 255 || mtProject.values.perfFxValues[i][1] < -255)
            mtProject.values.perfFxValues[i][1] = 0;
        if (mtProject.values.perfFxValues[i][2] > 255 || mtProject.values.perfFxValues[i][2] < -255)
            mtProject.values.perfFxValues[i][2] = 0;
        if (mtProject.values.perfFxValues[i][3] > 255 || mtProject.values.perfFxValues[i][3] < -255)
            mtProject.values.perfFxValues[i][3] = 0;
    }
    //----------------------------------------------------------------------------------------------------
    // song pattern
    if (mtProject.values.globalTempo > 1000)
        mtProject.values.globalTempo = DEFAULT_TEMPO;
    if (mtProject.values.patternLength > 255)
        mtProject.values.patternLength = 32;
    //----------------------------------------------------------------------------------------------------
    // midi instruments
    for (uint8_t channel = 0; channel < 16; channel++)
    {
        if (mtProject.values.midiInstrument[channel].channel > 16)
        {
            mtProject.values.midiInstrument[channel].channel = channel;
        }

        if (mtProject.values.midiInstrument[channel].velocity > 127)
        {
            mtProject.values.midiInstrument[channel].velocity = 127;
        }

        for (uint8_t cc = 0; cc < 6; cc++)
        {
            if (mtProject.values.midiInstrument[channel].ccOut[cc] > 130)
            {
                mtProject.values.midiInstrument[channel].ccOut[cc] = 0;
            }
        }
    }

    // last used
    if (mtProject.values.lastUsedInstrument >= kSampleInstrumentsCount + 16)
        mtProject.values.lastUsedInstrument = 0;

    if (mtProject.values.lastUsedNote >= 128)
        mtProject.values.lastUsedNote = 48;

    if (mtProject.values.lastUsedFx > FX_MAX)
        mtProject.values.lastUsedFx = 0;

    if (mtProject.values.recorderGainUsb > 100)
    {
        mtProject.values.recorderGainUsb = 0;
    }

#ifdef FEATURE_SYNTH
    for (int part = 0; part < kSynthInstrumentsCount; part++)
    {
        synth->validateSynthPartSettings(part, mtProject.values.synthPart[part]);

        mtProject.values.synthPart[part].reverbSend =
          clamp<float>(mtProject.values.synthPart[part].reverbSend, 0.0f, 100.0f);
        mtProject.values.synthPart[part].delaySend =
          clamp<float>(mtProject.values.synthPart[part].delaySend, 0.0f, 100.0f);

        engine.setSynthParam(audioEngine::SynthParam::kReverbSend,
                             part,
                             mtProject.values.synthPart[part].reverbSend,
                             audioEngine::SynthValuesSource::kInstrument);
        engine.setSynthParam(audioEngine::SynthParam::kDelaySend,
                             part,
                             mtProject.values.synthPart[part].delaySend,
                             audioEngine::SynthValuesSource::kInstrument);
        engine.setSynthParam(audioEngine::SynthParam::kPanning,
                             part,
                             mtProject.values.synthPart[part].panning,
                             audioEngine::SynthValuesSource::kInstrument);
        engine.setSynthParam(audioEngine::SynthParam::kVolume,
                             part,
                             mtProject.values.synthPart[part].gainDb,
                             audioEngine::SynthValuesSource::kInstrument);

        for (int macroIdx = 0; macroIdx < kSynthMacrosCount; macroIdx++)
        {
            engine.setSynthParam(
              audioEngine::SynthParam(audioEngine::SynthParam::kMacro1 + macroIdx),
              part,
              mtProject.values.synthPart[part].macroValue[macroIdx],
              audioEngine::SynthValuesSource::kInstrument);
        }

        engine.refreshSynthInstrumentParams(mtProject.values.synthPart[part], part);
    }
#endif

    return 1;
}

//==============================================================================================================
//==============================================================================================================
void cProjectEditor::setStopPatternFunct()
{
    PE->ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    PE->ip->blockAllInputsExcept(interfaceButton6, interfaceButton7);

    ip->setButtonObj(interfaceButton6, buttonPress, functStopPatternNo);
    ip->setButtonObj(interfaceButton7, buttonPress, functStopPatternYes);
}

static void functStopPatternYes()
{
    PE->ip->unblockAllInputs();
    mtPopups.hideInfoPopup();

    sequencer.stop();
    switch (PE->stopAction)
    {
        case cProjectEditor::stopActionOpenProject: PE->executePendingBrowserOpen(); break;
        case cProjectEditor::stopActionNewProject: functNewProject(); break;
        case cProjectEditor::stopActionExport: functExport(); break;
        case cProjectEditor::stopActionExportIt: functExportToMOD(); break;
        case cProjectEditor::stopActionImportMod: PE->executePendingBrowserOpen(); break;
        default: break;
    }
}

static void functStopPatternNo()
{
    PE->ip->unblockAllInputs();
    mtPopups.hideInfoPopup();

    switch (PE->stopAction)
    {
        case cProjectEditor::stopActionOpenProject:
            PE->pendingBrowserAction = cProjectEditor::PendingBrowserAction::kNone;
            PE->openBrowser(cProjectEditor::kOpenProject);
            break;
        // case cProjectEditor::stopActionExport:
        case cProjectEditor::stopActionExportIt:
            PE->showExportWindow();
            PE->setExportWindowFuncts();
            break;
        case cProjectEditor::stopActionImportMod:
            PE->pendingBrowserAction = cProjectEditor::PendingBrowserAction::kNone;
            PE->openBrowser(cProjectEditor::kOpenMod);
            break;
        default:
            PE->setDefaultScreenFunct();
            PE->showDefaultScreen();
            break;
    }
}

//==============================================================================================================
//==============================================================================================================
//==============================================================================================================
void cProjectEditor::createNewProject()
{
    if (newFileManager.isProjectChanged() || (mtProject.values.projectNotSavedFlag & 1))
    {
        PE->functShowSaveLastWindow();
        return;
    }

    newFileManager.createNewProjectInWorkspace();

    if (newFileManager.openProjectFromWorkspace())
    {
        PE->ip->blockAllInputs();
    }

    display.processDisplayUpdateImmediate();
}

static void splitSdFullPath(const std::string& fullPath,
                            char* outDir,
                            size_t dirCap,
                            char* outName,
                            size_t nameCap)
{
    if (outDir != nullptr && dirCap > 0)
    {
        outDir[0] = '\0';
    }
    if (outName != nullptr && nameCap > 0)
    {
        outName[0] = '\0';
    }

    std::string rel = fullPath;
    while (!rel.empty() && rel.front() == '/')
    {
        rel.erase(rel.begin());
    }
    while (!rel.empty() && rel.back() == '/')
    {
        rel.pop_back();
    }

    auto slash = rel.find_last_of('/');
    if (slash == std::string::npos)
    {
        if (outName != nullptr && nameCap > 0)
        {
            polystrncpy(outName, rel.c_str(), nameCap);
        }
        return;
    }

    if (outDir != nullptr && dirCap > 0)
    {
        polystrncpy(outDir, rel.substr(0, slash).c_str(), dirCap);
    }
    if (outName != nullptr && nameCap > 0)
    {
        polystrncpy(outName, rel.substr(slash + 1).c_str(), nameCap);
    }
}

void cProjectEditor::executePendingBrowserOpen()
{
    if (pendingBrowserAction == PendingBrowserAction::kNone)
    {
        return;
    }

    if (sequencer.isPlay())
    {
        stopAction = (pendingBrowserAction == PendingBrowserAction::kImportMod)
                       ? stopActionImportMod
                       : stopActionOpenProject;
        showStopPatternWindow();
        setStopPatternFunct();
        return;
    }

    if (newFileManager.isProjectChanged() || (mtProject.values.projectNotSavedFlag & 1))
    {
        functShowSaveLastWindowBeforeOpen();
        return;
    }

    PendingBrowserAction act = pendingBrowserAction;
    pendingBrowserAction = PendingBrowserAction::kNone;
    currentBrowserMode = kNone;

    if (act == PendingBrowserAction::kOpenProject)
    {
        if (newFileManager.openProject(pendingBrowserDir, pendingBrowserName))
        {
            ip->blockAllInputs();
            display.processDisplayUpdateImmediate();
        }
    }
    else if (act == PendingBrowserAction::kOpenRecent)
    {
        if (newFileManager.openRecentProject(pendingRecentIdx))
        {
            ip->blockAllInputs();
            display.processDisplayUpdateImmediate();
        }
    }
    else if (act == PendingBrowserAction::kImportMod)
    {
        newFileManager.createNewProjectInWorkspace();
        if (newFileManager.openProjectFromWorkspace())
        {
            newFileManager.importModAfterLoadNewProject(pendingBrowserDir, pendingBrowserName);
            ip->blockAllInputs();
            display.processDisplayUpdateImmediate();
        }
    }
}

//----------------------------------------------------------------------------------------
void cProjectEditor::openBrowser(BrowserMode mode)
{
    if (mode != kOpenProject && mode != kSaveAsProject && mode != kOpenMod)
    {
        return;
    }

    cleanupListsData();
    currentBrowserMode = mode;

    newFileManager.createProjectsAndModFolders();
    engine.getSdPlayer()->stop();

    GlobalFileBrowserConfig cfg;
    cfg.returnModule = PE;
    cfg.returnModuleOptions = 0;

    if (mode == kOpenProject || mode == kSaveAsProject)
    {
        cfg.canDelete = [](const std::string& fullPath,
                           bool,
                           bool isPackage,
                           std::string& outDenyReason) -> bool
        {
            std::string relSelected = fullPath;
            while (!relSelected.empty() && relSelected.front() == '/')
            {
                relSelected.erase(relSelected.begin());
            }

            char activeProjBuf[kMaxFilePathLen + 1]{};
            newFileManager.getCurrentProjectPathWithName(activeProjBuf, sizeof(activeProjBuf));
            const char* activeProj =
              (activeProjBuf[0] == '/') ? (activeProjBuf + 1) : activeProjBuf;

            if (activeProj[0] != '\0' && !relSelected.empty())
            {
                size_t selLen = relSelected.size();
                if (!isPackage && strncasecmp(relSelected.c_str(), activeProj, selLen) == 0 &&
                    activeProj[selLen] == '/')
                {
                    outDenyReason = "You cannot delete folder containing active project.";
                    return false;
                }
                if (isPackage && strcasecmp(relSelected.c_str(), activeProj) == 0)
                {
                    outDenyReason = "You cannot delete active project.";
                    return false;
                }
            }
            return true;
        };

        cfg.onDelete = [](const std::string& fullPath, bool, bool) -> bool
        {
            const char* relPath =
              (!fullPath.empty() && fullPath[0] == '/') ? (fullPath.c_str() + 1) : fullPath.c_str();
            newFileManager.deleteProjectOrFolder(const_cast<char*>(relPath));
            return !platform->getFileSystem()->exists(relPath);
        };
    }

    if (mode == kOpenProject)
    {
        cfg.mode = GlobalFileBrowserConfig::Mode::kLoadFolderPackage;
        cfg.packageMarkerFile = "project.mt";
        cfg.title = "Open Project";
        cfg.confirmButtonLabel = "Open";
        cfg.initialDirectory = std::string("/") + newFileManager.getProjectsDefaultPath();
        cfg.contextKey = "pe_projects";

        int listLen = kRecentProjectListLength;
        newFileManager.getProjectRecentList(recentProjectNameList, listLen);
        if (listLen > 0)
        {
            cfg.secondaryListTitle = "Recent";
            for (int i = 0; i < listLen; ++i)
            {
                if (recentProjectNameList[i] != nullptr && recentProjectNameList[i][0] != '\0')
                {
                    cfg.secondaryListItems.emplace_back(recentProjectNameList[i]);
                }
            }
            if (!cfg.secondaryListItems.empty())
            {
                cfg.initialSecondaryIndex = clamp<int>(
                  recentProjectListPos, 0, static_cast<int>(cfg.secondaryListItems.size()) - 1);
                cfg.onSecondaryConfirm = [](int idx, const std::string&)
                {
                    PE->currentBrowserMode = cProjectEditor::kOpenProject;
                    PE->pendingBrowserAction = PendingBrowserAction::kOpenRecent;
                    PE->pendingRecentIdx = idx;
                    PE->recentProjectListPos = idx;
                    PE->executePendingBrowserOpen();
                };
            }
        }

        cfg.onConfirm = [](const std::string& fullPath)
        {
            PE->currentBrowserMode = cProjectEditor::kOpenProject;
            PE->pendingBrowserAction = PendingBrowserAction::kOpenProject;
            splitSdFullPath(fullPath,
                            PE->pendingBrowserDir,
                            sizeof(PE->pendingBrowserDir),
                            PE->pendingBrowserName,
                            sizeof(PE->pendingBrowserName));
            PE->executePendingBrowserOpen();
        };
        cfg.onCancel = []()
        {
            PE->currentBrowserMode = cProjectEditor::kNone;
        };
    }
    else if (mode == kSaveAsProject)
    {
        cfg.mode = GlobalFileBrowserConfig::Mode::kSaveFolderPackage;
        cfg.packageMarkerFile = "project.mt";
        cfg.title = "Save Project As";
        cfg.initialDirectory = std::string("/") + newFileManager.getProjectsDefaultPath();
        cfg.defaultFileName = newFileManager.getCurrentProjectName();
        cfg.maxNameLength = PROJECT_NAME_SIZE - 1;
        cfg.contextKey = "pe_projects";

        cfg.onConfirm = [](const std::string& fullPath)
        {
            PE->currentBrowserMode = cProjectEditor::kNone;
            char dirBuf[kMaxFilePathLen + 1]{};
            char nameBuf[kMaxFileNameLen + 1]{};
            splitSdFullPath(fullPath, dirBuf, sizeof(dirBuf), nameBuf, sizeof(nameBuf));
            if (newFileManager.saveProject(dirBuf, nameBuf))
            {
                PE->ip->blockAllInputs();
                display.processDisplayUpdateImmediate();
            }
        };
        cfg.onCancel = []()
        {
            PE->currentBrowserMode = cProjectEditor::kNone;
            PE->cleanupAfterSaveData();
            PE->afterSaveOperation = cProjectEditor::AfterSaveOperation::noOperationAfterSave;
        };
    }
    else if (mode == kOpenMod)
    {
        cfg.mode = GlobalFileBrowserConfig::Mode::kLoadGenericFile;
        cfg.extensionFilter = ".mod;.it";
        cfg.title = "Import MOD / IT";
        cfg.confirmButtonLabel = "Import";
        cfg.initialDirectory = std::string("/") + newFileManager.getModsDefaultPath();
        cfg.contextKey = "pe_mods";

        cfg.onDelete = [](const std::string& fullPath, bool isFolder, bool) -> bool
        {
            const char* relPath =
              (!fullPath.empty() && fullPath[0] == '/') ? (fullPath.c_str() + 1) : fullPath.c_str();
            if (isFolder)
            {
                return platform->getFileSystem()->removeDirWithFiles(relPath);
            }
            newFileManager.deleteMod(const_cast<char*>(relPath));
            return !platform->getFileSystem()->exists(relPath);
        };

        cfg.onConfirm = [](const std::string& fullPath)
        {
            PE->currentBrowserMode = cProjectEditor::kOpenMod;
            PE->pendingBrowserAction = PendingBrowserAction::kImportMod;
            splitSdFullPath(fullPath,
                            PE->pendingBrowserDir,
                            sizeof(PE->pendingBrowserDir),
                            PE->pendingBrowserName,
                            sizeof(PE->pendingBrowserName));
            PE->executePendingBrowserOpen();
        };
        cfg.onCancel = []()
        {
            PE->currentBrowserMode = cProjectEditor::kNone;
        };
    }

    preserveAfterSaveOnStop = (mode == kSaveAsProject);
    openGlobalFileBrowser(cfg);
}

void cProjectEditor::setProjectBrowserFunct()
{
    PE->ip->clearButtonsRange(interfaceButton0, interfaceButton7);

    PE->ip->setButtonObj(interfaceButton0, buttonPress, functEnterBrowser);
    PE->ip->setButtonObj(interfaceButton2, buttonPress, functDeleteFileFolder);
    PE->ip->setButtonObj(interfaceButton3, buttonPress, functCreateNewFolder);
    PE->ip->setButtonObj(interfaceButton6, buttonPress, functCancelBrowser);

    if (PE->currentBrowserMode == cProjectEditor::kSaveAsProject)
    {
        PE->ip->setButtonObj(interfaceButton7, buttonPress, functBrowserSave);
    }
    else
    {
        PE->ip->setButtonObj(interfaceButton7, buttonPress, functOpenProjectMod);
    }
}

void cProjectEditor::setModBrowserFunct()
{
    PE->ip->clearButtonsRange(interfaceButton0, interfaceButton7);

    PE->ip->setButtonObj(interfaceButton0, buttonPress, functEnterBrowser);
    PE->ip->setButtonObj(interfaceButton2, buttonPress, functDeleteMod);
    PE->ip->setButtonObj(interfaceButton6, buttonPress, functCloseBrowser);
    PE->ip->setButtonObj(interfaceButton7, buttonPress, functOpenProjectMod);
}

void cProjectEditor::processRecentList()
{
    int listLlen = kRecentProjectListLength;
    newFileManager.getProjectRecentList(recentProjectNameList, listLlen);

    if (recentProjectListPos >= listLlen)
    {
        recentProjectListPos = 0;
    }

    recentList.start = recentProjectListPos;
    recentList.length = listLlen;
    recentList.linesCount = 10;
    recentList.selectionActive = 0;
    recentList.data = recentProjectNameList;

    PE->showRecentList();
}
//----------------------------------------------------------------------------------------
void cProjectEditor::saveProject()
{
    if ((mtProject.values.projectNotSavedFlag & 2) ||
        !newFileManager.projectExist(newFileManager.getCurrentProjectPath(),
                                     newFileManager.getCurrentProjectName()))
    {
        functSaveAsProject();
        return;
    }

    display.processDisplayUpdateImmediate();

    if (newFileManager.saveProject())
    {
        PE->ip->blockAllInputs();
    }
}

//----------------------------------------------------------------------------------------
void cProjectEditor::cleanupListsData()
{
    if (fileBrowserData.browserList != nullptr)
    {
        delete[] fileBrowserData.browserList;
        fileBrowserData.browserList = nullptr;
    }

    if (fileBrowserData.selectionList != nullptr)
    {
        delete[] fileBrowserData.selectionList;
        fileBrowserData.selectionList = nullptr;
    }
}

void cProjectEditor::cleanupAfterSaveData()
{
    if (afterSavePath != nullptr)
    {
        delete[] afterSavePath;
        afterSavePath = nullptr;
    }
    if (afterSaveName != nullptr)
    {
        delete[] afterSaveName;
        afterSaveName = nullptr;
    }
}

//==============================================================================================================
//==============================================================================================================
//==============================================================================================================
// Nowe podejscie ekran główny
static void functNewProject()
{
    if (sequencer.isPlay())
    {
        PE->showStopPatternWindow();
        PE->setStopPatternFunct();
        PE->stopAction = cProjectEditor::stopActionNewProject;
        return;
    }

    PE->showDefaultScreen();

    PE->createNewProject();
}

//==============================================================================================================
//==============================================================================================================
//==============================================================================================================

static void functOpenProject()
{
    PE->openBrowser(cProjectEditor::kOpenProject);
}

static void functImportMod()
{
    PE->openBrowser(cProjectEditor::kOpenMod);
}

static void functSaveProject()
{
    PE->afterSaveOperation = cProjectEditor::AfterSaveOperation::noOperationAfterSave;

    PE->saveProject();
}

static void functSaveAsProject()
{
    PE->ip->unblockAllInputs();
    PE->openBrowser(cProjectEditor::kSaveAsProject);
}

static void functBrowserSave()
{
    mtPopups.hideInfoPopup();
    PE->ip->unblockAllInputs();
    PE->ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    PE->ip->setButtonObj(interfaceButton1, buttonPress, functKeyboardAutoName);
    PE->ip->setButtonObj(interfaceButton6, buttonPress, functKeyboardCancel);
    PE->ip->setButtonObj(interfaceButton7, buttonPress, functSaveAsConfirm);
    PE->ip->setButtonObj(interfaceButton0, buttonPress, functKeyboardConfirmKey);
    PE->ip->setButtonObj(interfaceButtonInsert, buttonPress, functKeyboardConfirmKey);
    PE->keyboardManager.fillName(newFileManager.getCurrentProjectName());
    PE->keyboardManager.activateKeyboard(PROJECT_NAME_SIZE - 1);
    PE->showKeyboard("Save");
}

static void functCreateNewFolder()
{
    if (PE->currentBrowserMode == cProjectEditor::kOpenProject && PE->selectedPlace == 1)
    {
        return;
    }

    PE->ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    PE->ip->setButtonObj(interfaceButton1, buttonPress, functKeyboardAutoName);
    PE->ip->setButtonObj(interfaceButton6, buttonPress, functKeyboardCancel);
    PE->ip->setButtonObj(interfaceButton7, buttonPress, functNewFolderConfirm);
    PE->ip->setButtonObj(interfaceButton0, buttonPress, functKeyboardConfirmKey);
    PE->ip->setButtonObj(interfaceButtonInsert, buttonPress, functKeyboardConfirmKey);
    PE->keyboardManager.fillName("New folder");
    PE->keyboardManager.activateKeyboard(32);
    PE->showKeyboard("Create");
}

static void functNewFolderConfirm()
{
    int targetPathLen =
      strlen(PE->fileBrowser.getCurrentPath()) + strlen(PE->keyboardManager.getName()) + 2;

    bool correctPathLen = targetPathLen < PATCH_SIZE - 2;
    bool created = false;

    if (!correctPathLen)
    {
        mtPopups.show(1, "Maximum number of characters in folder path exceeded.");
        PE->showProjectBrowser(false);
    }
    else
    {
        created = PE->fileBrowser.createFolderInCurrentPath(PE->keyboardManager.getName());
        if (!created)
        {
            mtPopups.show(1, "A folder with the specified name already exists.");
            PE->showProjectBrowser(false);
        }
    }

    if (correctPathLen && created)
    {
        PE->openBrowser(PE->currentBrowserMode);
    }

    PE->fileBrowser.selectSpecificFolderName(PE->keyboardManager.getName());
    PE->refreshProjectBrowser();

    PE->setProjectBrowserFunct();
}

static void functExport()
{
    if (sequencer.isPlay())
    {
        PE->showStopPatternWindow();
        PE->setStopPatternFunct();
        PE->stopAction = cProjectEditor::stopActionExport;
        return;
    }

    PE->setExportWindowFuncts();
    PE->showExportWindow();
}

//===============================================================================================================
// New Project
void cProjectEditor::functShowSaveLastWindow()
{
    PE->ip->blockAllInputsExceptScreenButtons();
    PE->ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    PE->ip->setButtonObj(interfaceButton5, buttonPress, functSaveChangesCancelNewProject);
    PE->ip->setButtonObj(interfaceButton6, buttonPress, functSaveChangesDontSaveNewProject);
    PE->ip->setButtonObj(interfaceButton7, buttonPress, functSaveChangesSaveNewProject);
    showSaveLastWindow();
}

// anuluj tworzenie nowego projektu
static void functSaveChangesCancelNewProject()
{
    PE->ip->unblockAllInputs();
    PE->setDefaultScreenFunct();
    PE->showDefaultScreen();
}

// nie zapisuj starego projektu i otworz nowy
static void functSaveChangesDontSaveNewProject()
{
    PE->ip->unblockAllInputs();
    PE->showDefaultScreen();
    PE->setDefaultScreenFunct();

    // ignorowanie neizapisanego
    mtProject.values.projectNotSavedFlag = 0;
    newFileManager.clearChangeFlags();

    PE->createNewProject();
}

// zapisz stary projekt przy otwieraniu nowego
static void functSaveChangesSaveNewProject()
{
    PE->afterSaveOperation = cProjectEditor::AfterSaveOperation::newProjectAfterSave;

    PE->ip->unblockAllInputs();
    PE->showDefaultScreen();
    PE->setDefaultScreenFunct();

    PE->saveProject();
}

//===============================================================================================================
// Save AS
static void functKeyboardCancel()
{
    PE->afterSaveOperation = cProjectEditor::AfterSaveOperation::noOperationAfterSave;

    PE->keyboardManager.deactivateKeyboard();

    if (PE->currentBrowserMode == cProjectEditor::kOpenProject)
    {
        PE->processRecentList();
    }
    PE->showProjectBrowser(false);
    PE->setProjectBrowserFunct();
}

static void functSaveAsConfirm()
{
    int nameLen = strlen(PE->keyboardManager.getName());
    if (nameLen <= 0 || nameLen >= PROJECT_NAME_SIZE)
    {
        return;
    }

    if (newFileManager.projectExist(PE->fileBrowser.getCurrentPath(),
                                    PE->keyboardManager.getName()))
    {
        PE->functShowOverwriteWindow();
        return;
    }

    if (newFileManager.saveProject(PE->fileBrowser.getCurrentPath(), PE->keyboardManager.getName()))
    {
        PE->ip->blockAllInputs();
        functCloseBrowser();
        display.processDisplayUpdateImmediate();
    }
}

static void functSaveAsOverwriteYes()
{
    // same as upper but without exist checking
    if (newFileManager.saveProject(PE->fileBrowser.getCurrentPath(), PE->keyboardManager.getName()))
    {
        PE->ip->blockAllInputs();
    }
    else
    {
        PE->ip->unblockAllInputs();
    }

    functCloseBrowser();
    display.processDisplayUpdateImmediate();
}

void cProjectEditor::functShowOverwriteWindow()
{
    PE->ip->blockAllInputsExceptScreenButtons();
    PE->ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    PE->ip->setButtonObj(interfaceButton6, buttonPress, functBrowserSave);
    PE->ip->setButtonObj(interfaceButton7, buttonPress, functSaveAsOverwriteYes);

    PE->keyboardManager.deactivateKeyboard();
    PE->showOverwriteWindow();
}

static void functKeyboardAutoName()
{
    PE->keyboardManager.setRandomName();
}

//===============================================================================================================
// open
static void functOpenProjectMod()
{
    if (PE->currentBrowserMode != cProjectEditor::kSaveAsProject &&
        PE->currentBrowserMode != cProjectEditor::kOpenProject &&
        PE->currentBrowserMode != cProjectEditor::kOpenMod)
    {
        return;
    }

    if (PE->selectedPlace == 1 && PE->recentList.length == 0)
    {
        return;
    }

    bool projectOrModSelectedInBrowser =
      PE->fileBrowser.isPositionOnSelectionList() && PE->selectedPlace == 0;
    bool projectSelectedInRecentList = PE->selectedPlace == 1 && PE->recentList.length > 0;

    // open project or mod
    if (projectOrModSelectedInBrowser || projectSelectedInRecentList)
    {
        if (PE->currentBrowserMode == cProjectEditor::kSaveAsProject)
        {
            return;
        }

        if (sequencer.isPlay())
        {
            PE->showStopPatternWindow();
            PE->setStopPatternFunct();

            switch (PE->currentBrowserMode)
            {
                case cProjectEditor::kOpenProject:
                    PE->stopAction = cProjectEditor::stopActionOpenProject;
                    break;
                case cProjectEditor::kOpenMod:
                    PE->stopAction = cProjectEditor::stopActionImportMod;
                    break;
                default: break;
            }
            return;
        }
        if (newFileManager.isProjectChanged() || mtProject.values.projectNotSavedFlag & 1)
        {
            PE->functShowSaveLastWindowBeforeOpen();
            return;
        }

        if (PE->currentBrowserMode == cProjectEditor::kOpenProject)
        {
            if (PE->selectedPlace == 0)
            {
                if (newFileManager.openProject(PE->fileBrowser.getCurrentPath(),
                                               PE->fileBrowser.getSelectedFolderName()))
                {
                    PE->ip->blockAllInputs();
                    functCloseBrowser();
                    display.processDisplayUpdateImmediate();
                }
            }
            else
            {
                if (newFileManager.openRecentProject(PE->recentProjectListPos))
                {
                    PE->ip->blockAllInputs();
                    functCloseBrowser();
                    display.processDisplayUpdateImmediate();
                }
            }
        }
        else if (PE->currentBrowserMode == cProjectEditor::kOpenMod)
        {
            newFileManager.createNewProjectInWorkspace();
            if (newFileManager.openProjectFromWorkspace())
            {
                newFileManager.importModAfterLoadNewProject(PE->fileBrowser.getCurrentPath(),
                                                            PE->fileBrowser.getSelectedItemName());

                PE->ip->blockAllInputs();
                functCloseBrowser();
                display.processDisplayUpdateImmediate();
            }
        }
    }
}

static void functEnterBrowser()
{
    PE->browserEnter();
}

static void functCloseBrowser()
{
    PE->currentBrowserMode = cProjectEditor::kNone;

    // The browser lists are not freed here: the list control is only hidden and can
    // still be refreshed. stop() and the next openBrowser() free them.
    PE->showDefaultScreen();
    PE->setDefaultScreenFunct();
}

static void functCancelBrowser()
{
    // Cancelling Save As abandons the save, and with it whatever was to follow the save.
    PE->afterSaveOperation = cProjectEditor::AfterSaveOperation::noOperationAfterSave;

    functCloseBrowser();
}

bool cProjectEditor::browserEnter()
{
    if (currentBrowserMode != cProjectEditor::kSaveAsProject &&
        currentBrowserMode != cProjectEditor::kOpenProject &&
        currentBrowserMode != cProjectEditor::kOpenMod)
    {
        return false;
    }

    if (selectedPlace == 1 && recentList.length == 0)
    {
        return false;
    }

    bool projectOrModSelectedInBrowser =
      fileBrowser.isPositionOnSelectionList() && selectedPlace == 0;
    bool projectSelectedInRecentList = selectedPlace == 1 && recentList.length > 0;

    // browse
    if (!projectOrModSelectedInBrowser && !projectSelectedInRecentList)
    {
        fileBrowser.browse();
        switch (currentBrowserMode)
        {
            case cProjectEditor::kOpenProject: showProjectBrowser(); break;
            case cProjectEditor::kOpenMod: showModBrowser(); break;
            case cProjectEditor::kSaveAsProject: showProjectBrowser(); break;
            default: break;
        }
        return true;
    }
    return false;
}

void cProjectEditor::functShowSaveLastWindowBeforeOpen()
{
    PE->ip->blockAllInputsExceptScreenButtons();
    PE->ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    PE->ip->setButtonObj(interfaceButton5, buttonPress, functSaveChangesCancelOpen);
    PE->ip->setButtonObj(interfaceButton6, buttonPress, functSaveChangesDontSaveOpenBrowser);
    PE->ip->setButtonObj(interfaceButton7, buttonPress, functSaveChangesSaveOpen);
    showSaveLastWindow();
}

void cProjectEditor::functShowSaveLastWindowBeforeExportToMod()
{
    PE->ip->blockAllInputsExceptScreenButtons();
    PE->ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    PE->ip->setButtonObj(interfaceButton5, buttonPress, functSaveChangesCancelExportMod);
    PE->ip->setButtonObj(interfaceButton6, buttonPress, functSaveChangesDontSaveExportMod);
    PE->ip->setButtonObj(interfaceButton7, buttonPress, functSaveChangesSaveExportMod);
    showSaveLastWindow();
}

static void functDeleteFileFolder()
{
    if (PE->selectedPlace != 0)
    {
        return;
    }

    // chceck that is project selected
    if (PE->fileBrowser.isPositionOnSelectionList() || PE->fileBrowser.isFolderSelected())
    {
        char selectedFolderPath[kMaxFilePathLen];
        PE->fileBrowser.getSelectedItemWithPath(selectedFolderPath, kMaxFilePathLen);

        char currentPrjectPath[kMaxFilePathLen];
        newFileManager.getCurrentProjectPathWithName(currentPrjectPath, kMaxFilePathLen);

        int pathLevelDiffrent = PE->fileBrowser.getPathFolderLevel(selectedFolderPath) -
                                PE->fileBrowser.getPathFolderLevel(currentPrjectPath);

        if (pathLevelDiffrent < 0 &&
            strncasecmp(selectedFolderPath, currentPrjectPath, strlen(selectedFolderPath)) == 0)
        {
            mtPopups.show(1, "You cannot delete folder containing active project.");
            return;
        }

        if (pathLevelDiffrent >= 0 &&
            strncasecmp(selectedFolderPath, currentPrjectPath, kMaxFilePathLen) == 0 &&
            PE->fileBrowser.isPositionOnSelectionList())
        {
            mtPopups.show(1, "You cannot delete active project.");
            return;
        }

        PE->ip->blockAllInputsExceptScreenButtons();
        PE->ip->clearButtonsRange(interfaceButton0, interfaceButton7);
        PE->ip->setButtonObj(interfaceButton6, buttonPress, functSaveChangesCancelOpen);
        PE->ip->setButtonObj(interfaceButton7, buttonPress, functDeleteProject_Confirm);

        PE->showDeleteProjectLastWindow(PE->fileBrowser.isPositionOnSelectionList());
    }
}

static void functDeleteMod()
{
    if (!PE->fileBrowser.isPositionOnSelectionList())
    {
        return;
    }

    PE->ip->blockAllInputsExceptScreenButtons();
    PE->ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    PE->ip->setButtonObj(interfaceButton6, buttonPress, functSaveChangesCancelOpen);
    PE->ip->setButtonObj(interfaceButton7, buttonPress, functDeleteMod_Confirm);
    PE->showDeleteModLastWindow();
}

static void functSaveChangesCancelOpen()
{
    PE->afterSaveOperation = cProjectEditor::AfterSaveOperation::noOperationAfterSave;

    PE->ip->unblockAllInputs();

    mtPopups.hideInfoPopup();

    auto mode =
      (PE->pendingBrowserAction == cProjectEditor::PendingBrowserAction::kImportMod)
        ? cProjectEditor::kOpenMod
        : cProjectEditor::kOpenProject;
    PE->pendingBrowserAction = cProjectEditor::PendingBrowserAction::kNone;
    PE->openBrowser(mode);
}

static void functSaveChangesDontSaveOpenBrowser()
{
    PE->afterSaveOperation = cProjectEditor::AfterSaveOperation::noOperationAfterSave;

    // ignore saveing
    mtProject.values.projectNotSavedFlag = 0;
    newFileManager.clearChangeFlags();

    mtPopups.hideInfoPopup();
    PE->ip->unblockAllInputs();
    PE->executePendingBrowserOpen();
}

static void functSaveChangesSaveOpen()
{
    PE->cleanupAfterSaveData();
    mtPopups.hideInfoPopup();

    switch (PE->pendingBrowserAction)
    {
        case cProjectEditor::PendingBrowserAction::kOpenProject:
            PE->afterSavePath = new char[PATCH_SIZE];
            PE->afterSaveName = new char[kMaxFileNameLen];
            polystrncpy(PE->afterSavePath, PE->pendingBrowserDir, PATCH_SIZE);
            polystrncpy(PE->afterSaveName, PE->pendingBrowserName, kMaxFileNameLen);
            PE->afterSaveOperation = cProjectEditor::AfterSaveOperation::openProjectAfterSave;
            break;
        case cProjectEditor::PendingBrowserAction::kOpenRecent:
            PE->recentProjectListPos = PE->pendingRecentIdx;
            PE->afterSaveOperation = cProjectEditor::AfterSaveOperation::openRecentAfterSave;
            break;
        case cProjectEditor::PendingBrowserAction::kImportMod:
            PE->afterSavePath = new char[PATCH_SIZE];
            PE->afterSaveName = new char[kMaxFileNameLen];
            polystrncpy(PE->afterSavePath, PE->pendingBrowserDir, PATCH_SIZE);
            polystrncpy(PE->afterSaveName, PE->pendingBrowserName, kMaxFileNameLen);
            PE->afterSaveOperation = cProjectEditor::AfterSaveOperation::importModAfterSave;
            break;
        default: break;
    }

    PE->pendingBrowserAction = cProjectEditor::PendingBrowserAction::kNone;
    PE->currentBrowserMode = cProjectEditor::kNone;
    PE->ip->unblockAllInputs();
    PE->showDefaultScreen();
    PE->setDefaultScreenFunct();

    PE->saveProject();
}

static void functDeleteProject_Confirm()
{
    char selectedFolderPath[kMaxFilePathLen];
    PE->fileBrowser.getSelectedItemWithPath(selectedFolderPath, kMaxFilePathLen);

    newFileManager.deleteProjectOrFolder(selectedFolderPath);

    mtPopups.hideInfoPopup();

    PE->ip->unblockAllInputs();
    PE->openBrowser(PE->currentBrowserMode);
}

static void functDeleteMod_Confirm()
{
    char selectedModPath[kMaxFilePathLen];
    PE->fileBrowser.getSelectedItemWithPath(selectedModPath, kMaxFilePathLen);

    newFileManager.deleteMod(selectedModPath);

    mtPopups.hideInfoPopup();

    PE->ip->unblockAllInputs();
    PE->openBrowser(cProjectEditor::kOpenMod);
}

//===============================================================================================================
// export
char currentExportPath[PATCH_SIZE];

static void functExportSong()
{
    if (PE->checkClockConfig(exportSong))
    {
        return;
    }

    PE->startExport(exportSong);
}

static void functExportSongStems()
{
    if (PE->checkClockConfig(exportSongStems))
    {
        return;
    }

    PE->startExport(exportSongStems);
}

static void functExportPattern()
{
    if (PE->checkClockConfig(exportPattern))
    {
        return;
    }

    PE->startExport(exportPattern);
}

static void functExportPatternStems()
{
    if (PE->checkClockConfig(exportPatternStems))
    {
        return;
    }

    PE->startExport(exportPatternStems);
}

bool cProjectEditor::checkClockConfig(uint8_t source)
{
    if (mtConfig.midi.clkIn != 0)
    {
        PE->saveExportSource = source;
        PE->showClockWarning();
        PE->setClockWarningScreenFunct();
        return true;
    }
    else
    {
        PE->saveClockConfig = 0;
    }

    return false;
}

void cProjectEditor::startExport(uint8_t source)
{
    if (newFileManager.exportSound(source))
    {
        PE->ip->setButtonObj(interfaceButton7, buttonPress, functExportCancel);
        PE->ip->blockAllInputsExcept(interfaceButton7);

        PE->showLabelDuringExport();
    }
}

static void functExportToMOD()
{
    if (sequencer.isPlay())
    {
        PE->showStopPatternWindow();
        PE->setStopPatternFunct();
        PE->stopAction = cProjectEditor::stopActionExportIt;
        return;
    }

    if (newFileManager.isProjectChanged() || (mtProject.values.projectNotSavedFlag & 1))
    {
        PE->functShowSaveLastWindowBeforeExportToMod();
        return;
    }

    PE->setExportWindowFuncts();
    PE->showExportWindow();
    display.processDisplayUpdateImmediate();

    if (newFileManager.exportItFile())
    {
        PE->ip->blockAllInputs();
    }
}

static void functExportSamples()
{
    if (sequencer.isPlay())
    {
        PE->showStopPatternWindow();
        PE->setStopPatternFunct();
        PE->stopAction = cProjectEditor::stopActionExport;
        return;
    }

    if (newFileManager.exportProjectSamples())
    {
        PE->ip->setButtonObj(interfaceButton7, buttonPress, functExportProjectSamplesCancel);
        PE->ip->blockAllInputsExcept(interfaceButton7);
        PE->showLabelDuringExport();
    }
}

static void functExportProjectSamplesCancel()
{
    newFileManager.exportProjectSamplesCancel();

    PE->showExportWindow();
}

static void functSaveChangesCancelExportMod()
{
    PE->ip->unblockAllInputs();
    PE->setExportWindowFuncts();
    PE->showExportWindow();
}

static void functExportCancel()
{
    newFileManager.exportSoundCancel();

    PE->showExportWindow();
}

static void functSaveChangesDontSaveExportMod()
{
    // ignore save - but no clear flags here because after export project is still open
    // mtProject.values.projectNotSavedFlag = 0;
    // newFileManager.clearChangeFlags();

    if (newFileManager.exportItFile())
    {
        PE->ip->blockAllInputs();
    }
    else
    {

        PE->ip->unblockAllInputs();
    }

    PE->setExportWindowFuncts();
    PE->showExportWindow();
    display.processDisplayUpdateImmediate();
}

static void functSaveChangesSaveExportMod()
{
    PE->afterSaveOperation = cProjectEditor::AfterSaveOperation::exportModAfterSave;

    PE->ip->unblockAllInputs();
    PE->setExportWindowFuncts();
    PE->showExportWindow();

    PE->saveProject();
}

static void functExportGoBack()
{
    PE->setDefaultScreenFunct();
    PE->showDefaultScreen();
}

static void functClockWarningPopupButtons(uint8_t button, uint8_t state)
{
    if (state != buttonPress)
    {
        return;
    }

    if (button == interfaceButton6)
    {
        PE->setExportWindowFuncts();
        PE->showExportWindow();
    }
    else if (button == interfaceButton7)
    {
        PE->saveClockConfig = mtConfig.midi.clkIn;
        mtConfig.midi.clkIn = 0;

        switch (PE->saveExportSource)
        {
            case exportSong: PE->startExport(exportSong); break;
            case exportSongStems: PE->startExport(exportSongStems); break;
            case exportPattern: PE->startExport(exportPattern); break;
            case exportPatternStems: PE->startExport(exportPatternStems); break;
            default: break;
        }
    }
}

//===============================================================================================================
static void functSwitchModule(uint8_t button)
{
    PE->eventFunct(eventSwitchModule, PE, &button, 0);
}

//======================================================================================================================
static void functLeft()
{
    if (PE->keyboardManager.getState())
    {
        PE->keyboardManager.makeMove('a');
        return;
    }

    if (PE->currentBrowserMode == cProjectEditor::kNone)
    {
        return;
    }

    if (PE->currentBrowserMode == cProjectEditor::kOpenProject && PE->selectedPlace == 1)
    {
        PE->selectedPlace = 0;

        PE->activateLabelsBorder();

        PE->refreshProjectBrowserLabels();
    }
    else if (PE->currentBrowserMode == cProjectEditor::kOpenProject ||
             PE->currentBrowserMode == cProjectEditor::kOpenMod ||
             PE->currentBrowserMode == cProjectEditor::kSaveAsProject)
    {
        PE->fileBrowserData.browserListPosition = 0;
        switch (PE->currentBrowserMode)
        {
            case cProjectEditor::kSaveAsProject:
            case cProjectEditor::kOpenProject: PE->refreshProjectBrowser(); break;
            case cProjectEditor::kOpenMod: PE->refreshModBrowser(); break;
            default: break;
        }
    }
}

static void functRight()
{
    if (PE->keyboardManager.getState())
    {
        PE->keyboardManager.makeMove('d');
        return;
    }

    if (PE->currentBrowserMode == cProjectEditor::kOpenProject && PE->selectedPlace == 0)
    {
        PE->selectedPlace = 1;

        PE->activateLabelsBorder();

        PE->refreshProjectBrowserLabels();
    }
}

static void functUp()
{
    if (PE->keyboardManager.getState())
    {
        PE->keyboardManager.makeMove('w');
        return;
    }

    if (PE->currentBrowserMode == cProjectEditor::kNone)
    {
        return;
    }

    makeMoveOnActiveList(-1);
    PE->changeRecentListPos(-1);
}

static void functDown()
{
    if (PE->keyboardManager.getState())
    {
        PE->keyboardManager.makeMove('s');
        return;
    }

    if (PE->currentBrowserMode == cProjectEditor::kNone)
    {
        return;
    }

    makeMoveOnActiveList(1);
    PE->changeRecentListPos(1);
}

static void makeMoveOnActiveList(int16_t moveDir)
{
    moveDir = clamp<int>(moveDir, -5, 5);

    if ((PE->currentBrowserMode == cProjectEditor::kOpenProject && PE->selectedPlace == 0) ||
        PE->currentBrowserMode == cProjectEditor::kOpenMod ||
        PE->currentBrowserMode == cProjectEditor::kSaveAsProject)
    {
        // An empty list has no entry to move to; the clamp below would give position -1.
        if (PE->fileBrowserData.browserListLength <= 0)
        {
            return;
        }

        PE->fileBrowserData.browserListPosition =
          clamp<int>(PE->fileBrowserData.browserListPosition + moveDir,
                     0,
                     PE->fileBrowserData.browserListLength - 1);

        switch (PE->currentBrowserMode)
        {
            case cProjectEditor::kSaveAsProject:
            case cProjectEditor::kOpenProject: PE->refreshProjectBrowser(); break;
            case cProjectEditor::kOpenMod: PE->refreshModBrowser(); break;
            default: break;
        }
    }
}

void cProjectEditor::changeRecentListPos(int value)
{
    if (PE->currentBrowserMode == cProjectEditor::kOpenProject && selectedPlace == 1)
    {

        recentProjectListPos =
          clamp<int>(recentProjectListPos + value, 0, kRecentProjectListLength - 1);

        display.setControlValue(recentListControl, recentProjectListPos);
        display.refreshControl(recentListControl);
    }
}

static void functKeyboardConfirmKey()
{
    if (PE->keyboardManager.getState())
    {
        PE->keyboardManager.confirmKey();
    }
}

static void functDeleteBackspace(uint8_t state)
{
    if (PE->keyboardManager.getState())
    {
        if ((state == buttonPress) || (state == buttonHold))
        {
            PE->keyboardManager.makeBackspace();
        }
        return;
    }

    if (state != buttonPress)
    {
        return;
    }

    if (PE->currentBrowserMode == cProjectEditor::kOpenProject ||
        PE->currentBrowserMode == cProjectEditor::kSaveAsProject)
    {
        functDeleteFileFolder();
    }
    else if (PE->currentBrowserMode == cProjectEditor::kOpenMod)
    {
        functDeleteMod();
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
    if (PE->keyboardManager.getState())
    {
        PE->keyboardManager.confirmKey();
        return;
    }

    if (PE->currentBrowserMode == cProjectEditor::kSaveAsProject ||
        PE->currentBrowserMode == cProjectEditor::kOpenProject ||
        PE->currentBrowserMode == cProjectEditor::kOpenMod)
    {
        bool browsed = PE->browserEnter();

        if (!browsed)
        {
            functOpenProjectMod();
        }
    }
}

//==============================================================================================================
static void functPads(uint8_t pad, uint8_t state, [[maybe_unused]] int16_t velo)
{
    if (PE->keyboardManager.getState())
    {
        PE->keyboardManager.onPadChange(pad, state);
    }
}

static void functEncoder(int16_t value)
{
    if (PE->keyboardManager.getState())
    {
#ifdef TRACKER_MINI
        PE->keyboardManager.confirmKey();
#endif
        return;
    }

    makeMoveOnActiveList(interfaceGlobals.invertValForList ? -value : value);

    PE->changeRecentListPos(interfaceGlobals.invertValForList ? -value : value);
}

void cProjectEditor::lightUpPadBoard()
{
    padsBacklight.clearGlobalPads(0, 1, 1);
}

void cProjectEditor::refreshProjectCover(uint16_t delay_ms)
{
    if (delay_ms > 2000)
    {
        delay_ms = 2000;
    }

    PE->refreshCover = 1;
    PE->coverDelay = 2000 - delay_ms;

    display.setControlHide(coverImg);
}

static void functMenu(uint8_t state)
{
    if (state == buttonPress)
    {
        mtMenuPopup.showModulePopup(PE);
    }
    else if (state == buttonRelease)
    {
        mtMenuPopup.hideModulePopup(PE);
    }
}
