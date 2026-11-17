
#include "mtSequencer.h"
#include "mtStructs.h"

#include "fileManager.h"
#include "fileManagerDefs.h"
#include "fileTransfer.h"
#include "patternFileValidation.h"

#include "polyassert.h"

#include "convertOldFileVersionVolume.h"

#include <string.h>
#include <tuple>

__NOINIT(EXTERNAL_RAM) Sequencer::strPattern fileManagerPatternBuffer;
__NOINIT(EXTERNAL_RAM) Sequencer::strPattern fileManagerPerformancePatternBuffer;

static_assert(PatternFileValidation::k12TracksPatternSizeV5 == k12TracksPatternSize_v5);
static_assert(PatternFileValidation::k16TracksPatternSizeV5 == k16TracksPatternSize_v5);
static_assert(PatternFileValidation::k12TracksPatternSize == k12TracksPatternSize);
static_assert(PatternFileValidation::k16TracksPatternSize == k16TracksPatternSize);
static_assert(PatternFileValidation::k16TracksPatternSize == sizeof(Sequencer::strPattern));

void cFileManager::updateLoadWorkspacePattern()
{
    switch (currentOperationStep)
    {
        case 0: savePatternToWorkspace(); break;
        case 1: loadPatternFromWorkspace(currentPattern); break;
        case 2: loadPatternFromWorkspaceFinish(); break;
        default: stopOperationWithError(fmLoadPatternError); break;
    }
}

//----------------------------------------------------------------------------------------
void cFileManager::loadPatternFromWorkspaceFinish()
{
    if (status == fmExportingSoundSong || status == fmExportingSoundSongStems)
    {
        currentOperationStep = 0;
        currentOperation = fmExportSound;
        return;
    }

    status = fmIdle;
    currentOperationStep = 0;
    currentOperation = fmNoOperation;
}
//------------------------------------------------------------------------------------------------------------------
//-----------------------------------------     LOAD
//-----------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------

void cFileManager::loadPatternFromWorkspace(uint8_t index)
{
    assert(index > 0);

    char patternFilename[PATCH_SIZE]{ 0 };
    snprintf(patternFilename, PATCH_SIZE, cWorkspacePatternFileFormat, index);
    loadedPattern = index;

    uint8_t loadStatus = fileTransfer.loadFileToMemory(patternFilename,
                                                       (uint8_t*)&fileManagerPatternBuffer,
                                                       sizeof(Sequencer::strPattern),
                                                       fileWholeOnce);

    // A pattern is saved by removing its file and writing a new one. When power is lost in
    // between, a file of zero length is left behind. It holds nothing, so it is loaded like
    // a pattern without a file instead of failing the load of the whole project.
    if (loadStatus == fileTransferEnd && fileSystem->getFileSize(patternFilename) == 0)
    {
        loadStatus = fileTransferFileNoExist;
    }

    if (loadStatus == fileTransferEnd)
    {
        size_t fileSize = fileSystem->getFileSize(patternFilename);
        if (validatePatternFileData(patternFilename, &fileManagerPatternBuffer, fileSize) &&
            sequencer.copyBufferToBufferPattern((uint8_t*)&fileManagerPatternBuffer))
        {
            preloadedPattern = loadedPattern;
            report(" no blocking pattern load - success ", loadedPattern);
            moveToNextOperationStep();
            return;
        }
    }
    else if (loadStatus == fileTransferFileNoExist) // brak pliku patternu
    {
        sequencer.clearPattern(&fileManagerPatternBuffer);
        sequencer.copyBufferToBufferPattern((uint8_t*)&fileManagerPatternBuffer);
        preloadedPattern = loadedPattern;

        report(" no blocking pattern load - file no exist ", patternFilename);
        sequencer.clearPattern(&fileManagerPatternBuffer);
        moveToNextOperationStep();
        return;
    }

    stopOperationWithError(fmLoadPatternError);
    return;
}

const Sequencer::strPattern* cFileManager::loadWorkspacePatternToTempBuffer(uint8_t index)
{
    char patternToLoad[PATCH_SIZE];
    snprintf(patternToLoad, PATCH_SIZE, cWorkspacePatternFileFormat, index);

    uint8_t loadStatus = fileTransfer.loadFileToMemory(patternToLoad,
                                                       (uint8_t*)&fileManagerPatternBuffer,
                                                       sizeof(Sequencer::strPattern),
                                                       fileWholeOnce);

    if (loadStatus == fileTransferEnd)
    {
        size_t fileSize = fileSystem->getFileSize(patternToLoad);
        if (!validatePatternFileData(patternToLoad, &fileManagerPatternBuffer, fileSize))
        {
            return nullptr;
        }

        checkVersionAndTranslate(&fileManagerPatternBuffer);
        return &fileManagerPatternBuffer;
    }
    else
    {
        return nullptr;
    }
}

//------------------------------------------------------------------------------------------------------------------
//-----------------------------------------     SAVE
//-----------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------
void cFileManager::savePatternToWorkspace()
{
    if (changesFlags.pattern[mtProject.values.actualPattern] ==
        0) // jesli flaga zmian nie ustawiona omin zapis
    {
        report(" no blocking pattern save - no changes - ", mtProject.values.actualPattern);
        moveToNextOperationStep();
        return;
    }

    updateActualPatternBitmask();

    if (!sequencer.copyCurrentPatternToBuff((uint8_t*)&fileManagerPatternBuffer))
    {
        stopOperationWithError(fmSaveError);
        return;
    }

    char patternToSave[PATCH_SIZE]{ 0 };
    snprintf(
      patternToSave, PATCH_SIZE, cWorkspacePatternFileFormat, mtProject.values.actualPattern);

    uint8_t saveStatus = fileTransfer.saveMemoryToFile(
      (uint8_t*)&fileManagerPatternBuffer, patternToSave, sizeof(Sequencer::strPattern));

    if (saveStatus == fileTransferEnd)
    {
        report(" no blocking pattern save - success ", mtProject.values.actualPattern);
        clearPatternChanged(mtProject.values.actualPattern);
        sequencer.saveToFileDone();

        moveToNextOperationStep();
        return;
    }

    stopOperationWithError(fmSaveError);
    return;
}

//------------------------------------------------------------------------------------------------------------------
//-----------------------------------------     XXXX
//-----------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------

bool cFileManager::continuePatternProcess()
{
    currentPattern++;
    if (currentPattern > PATTERN_INDEX_MAX)
    {
        currentPattern = 1;
        moveToNextOperationStep();
        return false;
    }
    return true;
}

//------------------------------------------------------------------------------------------------------------------
//-----------------------------------------     XXXX
//-----------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------

bool cFileManager::loadWorkspacePatternNow(uint8_t index)
{
    bool status = false;
    char patternToLoad[PATCH_SIZE]{ 0 };

    snprintf(patternToLoad, PATCH_SIZE, cWorkspacePatternFileFormat, index);
    loadedPattern = index;
    preloadedPattern = 0;
    Sequencer::strPattern* destPattern = sequencer.getPatternToLoadFromFile();

    if (!fileSystem->exists(patternToLoad))
    {
        report(" load patern now - file no exist ", patternToLoad);
        status = false;
    }
    else
    {
        status = readPatternFile(patternToLoad, destPattern);
    }

    if (status)
    {
        report(" load pattern now - success ", loadedPattern);
    }
    else
    {
        report(" load pattern now - failed ", loadedPattern);
        sequencer.clearPattern(destPattern);
    }

    return status;
}

bool cFileManager::isPatternEmpty(uint8_t index)
{
    char patternToLoad[PATCH_SIZE]{ 0 };

    snprintf(patternToLoad, PATCH_SIZE, cWorkspacePatternFileFormat, index);

    if (!fileSystem->exists(patternToLoad))
    {
        return true;
    }

    return false;
}

bool cFileManager::saveWorkspacePatternNow(uint8_t index)
{
    char patternToSave[PATCH_SIZE]{ 0 };
    bool status = false;

    if (changesFlags.pattern[index] == 1)
    {
        changesFlags.pattern[index] = 0;
        snprintf(patternToSave, PATCH_SIZE, cWorkspacePatternFileFormat, index);
        status = writePatternFile(patternToSave, sequencer.getPatternToSaveToFile());
        sequencer.saveToFileDone();

        mtProject.values.projectNotSavedFlag |= 1;

        updateActualPatternBitmask();
        changesFlags.project = 1;

        if (status == true)
        {
            report(" save pattern now - success ", mtProject.values.actualPattern);
        }
        else
        {
            report(" save pattern now - failed ", mtProject.values.actualPattern);
        }
    }
    else
    {
        report(" save pattern now - no changes ", mtProject.values.actualPattern);
    }

    return status;
}

//------------------------------------------------------------------------------------------------------------------
//-----------------------------------------     XXXX
//-----------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------

bool cFileManager::writePatternFile(const char* filePath, Sequencer::strPattern* sourcePattern)
{
    fileSystem->remove(filePath);

    sequencer.setPatternHeader(sourcePattern);

    sourcePattern->crc = crcCalculator->calculate32bitCrc(
      (uint8_t*)sourcePattern, sizeof(Sequencer::strPattern) - sizeof(uint32_t));

    auto [isOpen, file] = fileSystem->openFile(filePath, FileSystem::FileOpenMode::kWriteCreateNew);

    if (isOpen)
    {
        int32_t written;
        written = fileSystem->writeFile(sourcePattern, sizeof(Sequencer::strPattern), file);

        if (written == static_cast<int32_t>(sizeof(Sequencer::strPattern)))
        {
            fileSystem->closeFile(file);
            report("write pattern file - success ", filePath);
            return true;
        }

        fileSystem->closeFile(file);
    }

    report("write pattern file - write error ", filePath);
    return false;
}

bool cFileManager::validatePatternFileData(const char* filePath,
                                           const Sequencer::strPattern* pattern,
                                           size_t fileSize)
{
    PatternFileValidation::Layout layout;
    uint8_t version = pattern->header.fileStructureVersion[0];

    if (!PatternFileValidation::resolveLayout(version, pattern->header.size, fileSize, layout))
    {
        report("load pattern file - invalid size ", filePath);
        return false;
    }

    return true;
}

bool cFileManager::readPatternFile(const char* filePath, Sequencer::strPattern* destPattern)
{
    memset(destPattern, 0, sizeof(Sequencer::strPattern));

    // na końcu struktury jest crc
    auto [loadStatus, file] = fileSystem->openFile(filePath);

    if (!loadStatus)
    {
        report("load pattern file - read error ", filePath);
        return false;
    }

    size_t fileSize = fileSystem->getFileSize(file);
    if (fileSize > sizeof(Sequencer::strPattern))
    {
        fileSystem->closeFile(file);
        report("load pattern file - invalid size ", filePath);
        return false;
    }

    int32_t bytesRead = fileSystem->readFile(destPattern, fileSize, file);
    fileSystem->closeFile(file);

    if (bytesRead < 0 || static_cast<size_t>(bytesRead) != fileSize)
    {
        report("load pattern file - read error ", filePath);
        return false;
    }

    if (!validatePatternFileData(filePath, destPattern, fileSize))
    {
        return false;
    }

    checkVersionAndTranslate(destPattern);

    report("load pattern file - success ", filePath);
    return true;
}

// The player, the editors and the translations below index the steps of a track with its
// length, and nothing but the size of a pattern file is checked when it is read.
static void limitTrackLengths(Sequencer::strPattern* pattern)
{
    for (Sequencer::strPattern::strTrack& track : pattern->track)
    {
        if (track.length > Sequencer::MAXSTEP)
        {
            track.length = Sequencer::MAXSTEP;
        }
    }
}

void cFileManager::checkVersionAndTranslate(Sequencer::strPattern* pattern)
{

    if (pattern->header.fileStructureVersion[0] < PATTERN_FILE_VERSION)
    {
        if (pattern->header.fileStructureVersion[0] <= 5)
        {
            translatePatternFromFileVersion5ToFileVersion6(pattern);
        }

        limitTrackLengths(pattern);

        if (pattern->header.fileStructureVersion[0] <= 1)
        {
            translateVolumeFxInPatternFromFileVersion1ToFileVersion2(pattern);
        }

        if (pattern->header.fileStructureVersion[0] <= 2)
        {
            translateBeatSliceNoteInPatternFromFileVersionLessThan3(pattern);
        }

        if (pattern->header.fileStructureVersion[0] <= 3)
        {
            translateVolumeFxInPatternFromFileVersion3ToFileVersion4(pattern);
        }

        if (pattern->header.fileStructureVersion[0] <= 4)
        {
            translatePatternFromFileVersion4ToFileVersion5(pattern);
        }

        for (uint8_t i = 0; i < 4; i++)
        {
            pattern->header.fileStructureVersion[i] = PATTERN_FILE_VERSION;
        }
    }
    else
    {
        limitTrackLengths(pattern);
    }

    translatePatternWithDifferentTrackCount(pattern);
}

void cFileManager::translateVolumeFxInPatternFromFileVersion1ToFileVersion2(
  Sequencer::strPattern* patternToTranslate)
{
    constexpr uint8_t FX_NUMBER = 2;
    ConvertOldFileVersionVolume volumeConverter;

    for (uint8_t track_idx = 0; track_idx < 8; track_idx++)
    {
        uint8_t trackLengthInSteps = patternToTranslate->track[track_idx].length + 1;

        for (uint8_t step_idx = 0; step_idx < trackLengthInSteps; step_idx++)
        {
            bool isMidiInstrument =
              (patternToTranslate->track[track_idx].step[step_idx].instrument >
               kSampleInstrumentsCount - 1);

            if (!isMidiInstrument)
            {
                for (uint8_t fx_idx = 0; fx_idx < FX_NUMBER; fx_idx++)
                {
                    if (patternToTranslate->track[track_idx].step[step_idx].fx[fx_idx].type ==
                        Sequencer::strFxConsts::FX_TYPE_VOLUME)
                    {
                        patternToTranslate->track[track_idx].step[step_idx].fx[fx_idx].value =
                          volumeConverter.convertInstrumentVolumeVersion1117to1118(
                            patternToTranslate->track[track_idx].step[step_idx].fx[fx_idx].value);
                    }
                }
            }
        }
    }
}

void cFileManager::translateBeatSliceNoteInPatternFromFileVersionLessThan3(
  Sequencer::strPattern* patternToTranslate)
{

    constexpr uint8_t ROOT_NOTE_OFFSET = 12;
    int8_t noteShift =
      FIRST_BEAT_SLICE_NOTE - (mtProject.values.padBoardRootNote + ROOT_NOTE_OFFSET);

    for (uint8_t track_idx = 0; track_idx < 8; track_idx++)
    {
        uint8_t trackLengthInSteps = patternToTranslate->track[track_idx].length + 1;

        for (uint8_t step_idx = 0; step_idx < trackLengthInSteps; step_idx++)
        {
            bool isMidiInstrument =
              (patternToTranslate->track[track_idx].step[step_idx].instrument >
               kSampleInstrumentsCount - 1);

            if (!isMidiInstrument)
            {
                bool isBeatSliceMode =
                  mtProject
                    .instrument[patternToTranslate->track[track_idx].step[step_idx].instrument]
                    .playMode == playModeBeatSlice;
                bool isNoteOnStep = patternToTranslate->track[track_idx].step[step_idx].note >
                                    Sequencer::STEP_NOTE_EMPTY;

                if (isNoteOnStep && isBeatSliceMode)
                {
                    if (patternToTranslate->track[track_idx].step[step_idx].note + noteShift <
                        MIN_NOTE)
                    {
                        patternToTranslate->track[track_idx].step[step_idx].note = MIN_NOTE;
                    }
                    else if (patternToTranslate->track[track_idx].step[step_idx].note + noteShift >
                             MAX_NOTE)
                    {
                        patternToTranslate->track[track_idx].step[step_idx].note = MAX_NOTE;
                    }
                    else
                    {
                        patternToTranslate->track[track_idx].step[step_idx].note += noteShift;
                    }
                }
            }
        }
    }
}
void cFileManager::translateVolumeFxInPatternFromFileVersion3ToFileVersion4(
  Sequencer::strPattern* patternToTranslate)
{
    constexpr uint8_t FX_NUMBER = 2;
    ConvertOldFileVersionVolume volumeConverter;

    for (uint8_t track_idx = 0; track_idx < 8; track_idx++)
    {
        uint8_t trackLengthInSteps = patternToTranslate->track[track_idx].length + 1;

        for (uint8_t step_idx = 0; step_idx < trackLengthInSteps; step_idx++)
        {
            bool isMidiInstrument =
              (patternToTranslate->track[track_idx].step[step_idx].instrument >
               kSampleInstrumentsCount - 1);

            if (!isMidiInstrument)
            {
                for (uint8_t fx_idx = 0; fx_idx < FX_NUMBER; fx_idx++)
                {
                    if (patternToTranslate->track[track_idx].step[step_idx].fx[fx_idx].type ==
                        Sequencer::strFxConsts::FX_TYPE_VOLUME)
                    {
                        patternToTranslate->track[track_idx].step[step_idx].fx[fx_idx].value =
                          volumeConverter.convertInstrumentVolumeVersion1300to1302(
                            patternToTranslate->track[track_idx].step[step_idx].fx[fx_idx].value);
                    }
                }
            }
        }
    }
}

void cFileManager::translatePatternFromFileVersion4ToFileVersion5(
  Sequencer::strPattern* patternToTranslate)
{
    for (int trackIdx = kMidiTrackFirstIdx; trackIdx < kAllTracks; trackIdx++)
    {
        for (Sequencer::strPattern::strTrack::strStep& step :
             patternToTranslate->track[trackIdx].step)
        {
            sequencer.clearStep(&step);
        }
        patternToTranslate->track[trackIdx].length = patternToTranslate->track[0].length;
    }
}

void cFileManager::translatePatternFromFileVersion5ToFileVersion6(
  Sequencer::strPattern* patternToTranslate)
{
    struct strTrack_v5
    {
        uint8_t length;
        struct strStep_v5
        {
            int8_t note;
            uint8_t instrument;
            Sequencer::strPattern::strTrack::strStep::strFx fx[Sequencer::kStepFxSlots];
        } step[Sequencer::MAXSTEP + 1];
    };

    const strTrack_v5* srcTracks = reinterpret_cast<const strTrack_v5*>(patternToTranslate->track);

    for (int trackIdx = kAllTracks - 1; trackIdx >= 0; --trackIdx)
    {
        const strTrack_v5 srcTrack = srcTracks[trackIdx];
        patternToTranslate->track[trackIdx].length = srcTrack.length;

        for (int stepIdx = 0; stepIdx <= Sequencer::MAXSTEP; ++stepIdx)
        {
            auto& dstStep = patternToTranslate->track[trackIdx].step[stepIdx];
            const auto& srcStep = srcTrack.step[stepIdx];

            dstStep.note = srcStep.note;
            dstStep.instrument = srcStep.instrument;
            dstStep.fx[0] = srcStep.fx[0];
            dstStep.fx[1] = srcStep.fx[1];
            dstStep.volume = Sequencer::STEP_VELO_DEFAULT;
            dstStep.micromove = -1;
        }
    }
}

void cFileManager::translatePatternWithDifferentTrackCount(Sequencer::strPattern* pattern)
{
    if (pattern->header.size == sizeof(Sequencer::strPattern) ||
        pattern->header.size == k16TracksPatternSize_v5 ||
        pattern->header.size == k16TracksPatternSize)
    {
        pattern->header.size = sizeof(Sequencer::strPattern);
        return;
    }

    int clearAllFromTrackIdx = 8;

    if (pattern->header.size < k12TracksPatternSize_v5)
    {
        clearAllFromTrackIdx = 8;
    }
    else if (pattern->header.size == k12TracksPatternSize_v5 ||
             pattern->header.size == k12TracksPatternSize)
    {
        clearAllFromTrackIdx = 12;
    }

    for (int trackIdx = clearAllFromTrackIdx; trackIdx < kAllTracks; trackIdx++)
    {
        pattern->track[trackIdx].length = pattern->track[0].length;

        sequencer.clearSingleTrack(&pattern->track[trackIdx]);
    }

    pattern->header.size = sizeof(Sequencer::strPattern);
}

/// tu tylko ladujemy do bufora, podmieniamy w sequ
bool cFileManager::loadPerformanceTrackToBuffer(uint8_t pattIndex, uint8_t trackIndex)
{
    report(" load perf track to buff");

    assert(trackIndex < kPerformanceTracks);

    char patternToLoad[PATCH_SIZE]{ 0 };
    sprintf(patternToLoad, "Workspace/patterns/pattern_%02d.mtp", pattIndex);

    Sequencer::strPattern* patternFrom =
      (Sequencer::strPattern*)sequencer.getPatternToLoadFromFile();
    Sequencer::strPattern* patternTo = &fileManagerPerformancePatternBuffer;

    // The file is read through the buffer in which a preloaded next pattern waits for the
    // end of the current one. Ask for that pattern again.
    DISABLE_GLOBAL_INTERRUPTS();
    if (preloadedPattern != 0 && requestedPattern == 0)
    {
        requestedPattern = preloadedPattern;
    }
    preloadedPattern = 0;
    ENABLE_GLOBAL_INTERRUPTS();

    // A read that fails leaves zeroed or partly read steps behind, and a zeroed step is a
    // note of the first instrument, not an empty step.
    if (!fileSystem->exists(patternToLoad) ||
        !readPatternFile(patternToLoad, sequencer.getPatternToLoadFromFile()))
    {
        sequencer.clearPattern(patternFrom);
    }

    patternTo->track[trackIndex] = patternFrom->track[trackIndex];

    // lengthy tracka na potrzeby performance przepisuje do konretnego tracka
    patternTo->track[trackIndex].length = patternFrom->track[0].length;

    mtProject.perfTracksPatterns[trackIndex] = pattIndex;
    if (trackIndex < kSampleTracks)
    {
        mtProject.values.perfTracksPatterns[trackIndex] = pattIndex;
    }

    return true;
}

uint8_t cFileManager::getCurrentPatternIndex()
{
    return currentPattern;
}

uint8_t cFileManager::getLoadedPatternIndex()
{
    assert(loadedPattern > 0);
    return loadedPattern;
}

uint32_t cFileManager::calcProjectPatternsSize()
{
    char currentPatch[PATCH_SIZE];
    volatile int pathSize = sizeof(currentPatch);
    uint32_t size = 0;
    projectPatternsCount = 0;

    for (uint16_t i = 1; i < PATTERN_INDEX_MAX + 1; i++)
    {
        snprintf(currentPatch,
                 pathSize,
                 cProjectsPatternFileFormat,
                 currentProjectPatch,
                 currentProjectName,
                 i);
        if (fileSystem->exists(currentPatch))
        {
            size += sizeof(Sequencer::strPattern);
            projectPatternsCount++;
        }
    }

    return size;
}
