#include "fileManagerDefs.h"
#include "mtStructs.h"

#include "fileManager.h"
#include "fileTransfer.h"
#include "mtAudioEngine.h"
#include "mtFileManagerOldVersionRecovery.h"
#include "mtMasterEnhancer.h"

#include "log.h"

#if FILEMANAGER_HANDLE_SYNTH == 1
#include "Synth.h"
#endif

#include <string.h>

__NOINIT(EXTERNAL_RAM)
static strRecentFile externalRamRecentProjectsList;

//------------------------------------------------------------------------------------------------------------------
//-----------------------------------------     SAVE
//-----------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------
void cFileManager::saveProjectFileToWorkspace()
{
    // sprawdza czy zaktualizowac patternBitmask
    strProjectFile fileManagerProjectBuffer;

    if (writeProjectFileToFileStruct(&mtProject, &fileManagerProjectBuffer))
    {

        uint8_t saveStatus = fileTransfer.saveMemoryToFile(
          (uint8_t*)&fileManagerProjectBuffer, cProjectFileNameInWorkspace, sizeof(strProjectFile));

        if (saveStatus == fileTransferEnd)
        {
            clearProjectChangedFlag();
            return;
        }
    }

    stopOperationWithError(fmSaveError);
}

//------------------------------------------------------------------------------------------------------------------
//-----------------------------------------     xxxx
//-----------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------

bool cFileManager::loadProjectFileFormFileStruct(strMtProject* project, strProjectFile* pFile)
{
    if (pFile->projectDataAndHeader.projectHeader.type != fileTypeProject)
        return false;

    // Structure versions start at 1. translateProject() calls one function per version
    // from a table whose first entry belongs to version 1.
    if (pFile->projectDataAndHeader.projectHeader.fileStructureVersion[0] == 0)
        return false;

    uint16_t projectFileSize = pFile->projectDataAndHeader.projectHeader.size;
    if (projectFileSize < sizeof(strProjectFileHeader) + sizeof(uint32_t) ||
        projectFileSize > sizeof(strProjectFile))
    {
        return false;
    }

    uint16_t projectDataSize = projectFileSize - sizeof(uint32_t);
    uint32_t storedCRC;
    memcpy(&storedCRC, (uint8_t*)pFile + projectDataSize, sizeof(storedCRC));
    uint32_t checkCRC =
      crcCalculator->calculate32bitCrc((uint8_t*)pFile, projectDataSize);
    // Older IMX firmware wrote zero because its CRC calculator was a stub.
    const bool hasStoredCRC = storedCRC != 0;

#ifdef FILEMANAGER_DEBUGLOG
    char line[60];
    uint8_t b_ver = pFile->projectDataAndHeader.projectHeader.fwVersion[3];
    sprintf(line,
            "opened project fw.ver: %u.%u.%u (%s%u), crc %s",
            pFile->projectDataAndHeader.projectHeader.fwVersion[0],
            pFile->projectDataAndHeader.projectHeader.fwVersion[1],
            pFile->projectDataAndHeader.projectHeader.fwVersion[2],
            b_ver > 0 && b_ver < 255 ? "beta " : "",
            b_ver,
            !hasStoredCRC ? "n/a" : checkCRC == storedCRC ? "ok" : "err");
    debugLog.addLine(line);
#endif

    if (pFile->projectDataAndHeader.projectHeader.fileStructureVersion[0] < PROJECT_FILE_VERSION)
    {
        //"older version of project file opened"

        versionRecovery.translateProject(
          pFile->projectDataAndHeader.projectHeader.fileStructureVersion[0],
          PROJECT_FILE_VERSION,
          &pFile->projectDataAndHeader.project);
    }
    else if (pFile->projectDataAndHeader.projectHeader.fileStructureVersion[0] >
             PROJECT_FILE_VERSION)
    {
        //"newer version of project file opened"
        // todo: informacja do interfejsu zeby wyswietlic popup
    }

    if (pFile->projectDataAndHeader.projectHeader.size != sizeof(strProjectFile))
    {
        // zmienil sie rozmiar pliku projektu (np. po update)
        // wiec rozjadą sie nazwa i patch projektu
        // ktore sa na koncu struktury pliku projektu
        strncpy(projectNamefromProjectFile, "Unsaved Project", PROJECT_NAME_SIZE - 1);
        projectNamefromProjectFile[PROJECT_NAME_SIZE - 1] = 0;
        projectPatchfromProjectFile[0] = 0;
        pFile->projectDataAndHeader.project.values.projectNotSavedFlag = 3;
    }
    else if (pFile->projectDataAndHeader.project.projectName[0] == 0)
    {
        // brak nazwy w pliku projektu
        // - jesli projekt czytany z workspace to nie mozna powiazac projektu z projektem w
        // folderze Projects
        // - jeslie projekt jest otierany z folderu Projects
        //   to nazwa zostanie przepisana na koncu operacji otwierania projektu

        projectNamefromProjectFile[0] = 0;
        projectPatchfromProjectFile[0] = 0;
    }
    else
    {
        // plik projektu zawiera nazwe - przepisuje nazwe i path projektu odczytane z pliku
        // projektu, dalej jest to i tak weryfikowane

        pFile->projectDataAndHeader.project.projectName[PROJECT_NAME_SIZE - 1] = 0;
        strncpy(projectNamefromProjectFile,
                pFile->projectDataAndHeader.project.projectName,
                PROJECT_NAME_SIZE - 1);
        projectNamefromProjectFile[PROJECT_NAME_SIZE - 1] = 0;

        pFile->projectDataAndHeader.project.projectPath[PATCH_SIZE - PROJECT_NAME_SIZE - 1] = 0;
        strncpy(projectPatchfromProjectFile,
                pFile->projectDataAndHeader.project.projectPath,
                PATCH_SIZE - PROJECT_NAME_SIZE - 1);
        projectPatchfromProjectFile[PATCH_SIZE - PROJECT_NAME_SIZE - 1] = 0;
    }

    // The audio interrupt runs the sequencer on these values from the moment they are
    // copied, before loadProjectValues() checks the project, and a tempo that is not a
    // positive number keeps it in its tick loop for ever. Patterns are numbered from 1;
    // pattern 0 becomes index 255 of the 255-entry pattern bitmasks.
    strMtValues& fileValues = pFile->projectDataAndHeader.project.values;
    if (!(fileValues.globalTempo > 0.0f && fileValues.globalTempo <= 1000.0f))
    {
        fileValues.globalTempo = DEFAULT_TEMPO;
    }
    if (fileValues.actualPattern < PATTERN_INDEX_MIN)
    {
        fileValues.actualPattern = PATTERN_INDEX_MIN;
    }

    memcpy(&project->song, &pFile->projectDataAndHeader.project.song, sizeof(strSong));
    memcpy(&project->values, &pFile->projectDataAndHeader.project.values, sizeof(strMtValues));
    //*proj=pFile.projectDataAndHeader.project;
    // mtProject.values=pFile.projectDataAndHeader.project.values;
    return true;
}

bool cFileManager::writeProjectFileToFileStruct(strMtProject* project, strProjectFile* pFile)
{
    memcpy(&pFile->projectDataAndHeader.project.song, &project->song, sizeof(strSong));
    memcpy(&pFile->projectDataAndHeader.project.values, &project->values, sizeof(strMtValues));
    strcpy(pFile->projectDataAndHeader.project.projectName, currentProjectName);
    // The path of the open project can be longer than its field in the project file.
    strncpy(pFile->projectDataAndHeader.project.projectPath,
            currentProjectPatch,
            PATCH_SIZE - PROJECT_NAME_SIZE - 1);
    pFile->projectDataAndHeader.project.projectName[PROJECT_NAME_SIZE - 1] = 0;
    pFile->projectDataAndHeader.project.projectPath[PATCH_SIZE - PROJECT_NAME_SIZE - 1] = 0;

    pFile->projectDataAndHeader.projectHeader.id_file[0] = 'M';
    pFile->projectDataAndHeader.projectHeader.id_file[1] = 'T';

    pFile->projectDataAndHeader.projectHeader.type = fileTypeProject;

    pFile->projectDataAndHeader.projectHeader.fwVersion[0] = CORE_VER_1;
    pFile->projectDataAndHeader.projectHeader.fwVersion[1] = CORE_VER_2;
    pFile->projectDataAndHeader.projectHeader.fwVersion[2] = CORE_VER_3;
    pFile->projectDataAndHeader.projectHeader.fwVersion[3] = CORE_BETA; // 255 == official release

    pFile->projectDataAndHeader.projectHeader.fileStructureVersion[0] = PROJECT_FILE_VERSION;
    pFile->projectDataAndHeader.projectHeader.fileStructureVersion[1] = PROJECT_FILE_VERSION;
    pFile->projectDataAndHeader.projectHeader.fileStructureVersion[2] = PROJECT_FILE_VERSION;
    pFile->projectDataAndHeader.projectHeader.fileStructureVersion[3] = PROJECT_FILE_VERSION;

    pFile->projectDataAndHeader.projectHeader.size = sizeof(strProjectFile);

    pFile->crc = crcCalculator->calculate32bitCrc((uint8_t*)&pFile->projectDataAndHeader,
                                                  sizeof(pFile->projectDataAndHeader));

    return true;
}

//----------------------------------------------------------------------------------------------------------------
//-----------------------------------------     xxxx
//--------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------
void cFileManager::modifyWorkspaceProjectFile()
{
    changesFlags.project = 1;

    moveToNextOperationStep();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void cFileManager::getDefaultProject(struct strMtProject* source)
{
    source->instruments_count = 0;
    source->patterns_count = 0;
    source->max_memory = SAMPLE_MEMORY_SIZE;
    source->used_memory = 0;

    for (uint32_t i = 0; i < kSampleInstrumentsCount; i++)
    {
        getEmptyInstrument(&source->instrument[i]);
    }

    for (uint32_t i = 0; i < kMaxAllTracksInTrackers; i++)
    {
        source->perfTracksPatterns[i] = 1;
    }

    getDefaultSong(&source->song);
    getDefaultValues(&source->values);

#if FILEMANAGER_HANDLE_SYNTH == 1
    for (int part = 0; part < kSynthInstrumentsCount; part++)
    {
        synth->initSynthPartSettings(part, source->values.synthPart[part]);
    }
#endif
}

void cFileManager::getDefaultSong(struct strSong* source)
{
    source->playlistPos = 0;

    memset(source->playlist, 0, SONG_MAX);

    source->playlist[0] = 1;
}

void cFileManager::getDefaultValues(struct strMtValues* source)
{
    source->lastUsedNote = 0;
    source->lastUsedInstrument = 0;
    source->lastUsedVolume = 0;
    source->lastUsedFx = 0;

    source->actualPattern = 1;

    source->padBoardScale = 0;
    source->padBoardNoteOffset = 11;
    source->padBoardRootNote = 24;
    source->padBoardMaxVoices = 8;

    source->oldVolume = 0;

    source->delayFeedback = 50;
    source->delayTime = 500;
    source->delayParams = 0;

    source->limiterMode = limiterModeSubtle;
    source->limiterAttack = 100;
    source->limiterRelease = 0.512;
    source->limiterThreshold = LIMITER_THRESHOLD_DEFAULT;

    source->bitDepth = 16;
    source->antialiasingEnable = 1;
    source->sendsMode = sendsModePostFader;

    source->patternDiv = 3;
    source->patternEditStep = 1;

    for (uint32_t i = 0; i < 8; i++)
    {
        source->trackMute[i] = 0;
        source->midiTrackMute[i] = 0;
    }

    //********************************* radio
    source->source = 0;
    source->gainLineIn = 0;
    source->gainMicLow = 0;
    source->gainMicHigh = 0;
    source->gainRadio = 0;
    source->monitor = 0;
    source->radioFreq = 87.5;

    source->projectNotSavedFlag = 0;

    // performance
    for (uint32_t i = 0; i < kSampleTracks; i++)
    {
        source->perfTracksPatterns[i] = 1;
        source->perfTracksState[i] = 0;
    }

    for (uint32_t i = 0; i < 12; i++)
    {
        source->perfSelectedValues[i] = 0;
        source->perfFxPlaces[i] = (i + 1);
        source->perfFxValues[i][0] = 0;
        source->perfFxValues[i][1] = 0;
        source->perfFxValues[i][2] = 0;
        source->perfFxValues[i][3] = 0;
    }

    // song
    source->globalTempo = DEFAULT_TEMPO;
    source->patternLength = 32;

    memset(source->instrumentsToSave, 0, kSampleInstrumentsCount);
    memset(source->patternsToSave, 0, PATTERN_INDEX_MAX);

    // instrumenty midi

    for (uint8_t channel = 0; channel < 16; channel++)
    {
        source->midiInstrument[channel].channel = channel;
        source->midiInstrument[channel].velocity = DEFAULT_MIDI_VELOCITY;
        source->midiInstrument[channel].offset = 0;
        source->midiInstrument[channel].ccOut[0] = MIDI_CC_MODWHEEL;
        source->midiInstrument[channel].ccOut[1] = MIDI_CC_GLIDE;
        source->midiInstrument[channel].ccOut[2] = MIDI_CC_CHAN_VOLUME;
        source->midiInstrument[channel].ccOut[3] = MIDI_CC_EXPRESSION;
        source->midiInstrument[channel].ccOut[4] = MIDI_CC_SUSTAIN;
        source->midiInstrument[channel].ccOut[5] = MIDI_CC_SPECIAL_PC;
        for (uint8_t i = 0; i < 7; i++)
        {
            source->midiInstrument[channel].reserved[i] = 0;
        }
    }

    for (uint8_t channel = 0; channel < (16 - 6); channel++)
    {
        // jest (16 - 6), bo 6 bajtow wzietych dla nowych (1.4.0b2+) line in
        source->oldMidiInstrument[channel] = 0;
    }
    source->oldLineInVolume = 0;
    source->oldLineInMute = 0;

    memset(source->allPatternsBitmask, 0, sizeof(source->allPatternsBitmask));
    memset(source->allPatternsBitmaskMidi, 0, sizeof(source->allPatternsBitmaskMidi));

    source->reverb.damp = DEFAULT_REVERB_DAMP;
    source->reverb.diffusion = DEFAULT_REVERB_DIFFUSION;
    source->reverb.predelay = DEFAULT_REVERB_PREDELAY;
    source->reverb.size = DEFAULT_REVERB_SIZE;

    for (uint8_t trackIdx = 0; trackIdx < kSampleTracks; trackIdx++)
    {
        sprintf(&source->TrackNames[trackIdx][0], "Track %d", trackIdx + 1);

        source->trackVolume[trackIdx] = DEFAULT_TRACK_VOLUME;
    }

    for (uint8_t trackIdx = kSampleTracks; trackIdx < kAllTracks; trackIdx++)
    {
        sprintf(&source->midiTrackNames[trackIdx - kSampleTracks][0], "Midi %d", trackIdx + 1);
    }

    source->reverbMute = mtAudioEngineConstans::MUTE_DISABLE;
    source->delayMute = mtAudioEngineConstans::MUTE_DISABLE;
    source->dryMixMute = mtAudioEngineConstans::MUTE_DISABLE;

    source->reverbVolume = DEFAULT_REVERB_VOLUME;
    source->delayVolume = DEFAULT_DELAY_VOLUME;
    source->dryMixVolume = DEFAULT_DELAY_VOLUME;

    for (uint8_t band = 0; band < 5; band++)
    {
        source->eqBandAdjustment[band] = BAND_ADJUSTMENT_DEFAULT;
    }
    source->bassBoost = 0;
    source->stereoBoost = 0;

    source->lineInVolume = DEFAULT_LINE_IN_VOLUME;
    source->lineInMute = mtAudioEngineConstans::MUTE_ENABLE;
    source->lineInDelaySend = 0;
    source->lineInDelaySendMute = mtAudioEngineConstans::MUTE_ENABLE;
    source->lineInReverbSend = 0;
    source->lineInReverbSendMute = mtAudioEngineConstans::MUTE_ENABLE;

    source->limiterSidechain = LIMITER_SIDECHAIN_DEFAULT;
    source->delayFilterFrequency = DELAY_FILTER_FREQ_MAX;
    source->isHeadroomEnabled = 1;
    source->saturationType = SaturationType::kSoftClip;
    source->isSnapToZeroEnabled = 1;

    memset(source->synthPart, 0, sizeof(source->synthPart));
    source->recorderGainUsb = 0;
}

void cFileManager::getCurrentProjectPathWithName(char* path, int maxLen)
{
    int pathLen = strlen(currentProjectPatch);

    assert(pathLen + 3 < maxLen);

    strncpy(path, currentProjectPatch, maxLen);

    if (pathLen > 0 && pathLen < maxLen - 1)
    {
        strcat(path, "/");
    }

    int nameMaxLen = (maxLen - 2) - strlen(path);

    assert(nameMaxLen > (int)strlen(currentProjectName));

    // strncat takes the limit as an unsigned number, so a negative one is no limit at all.
    if (nameMaxLen > 0)
    {
        strncat(path, currentProjectName, nameMaxLen);
    }
}

bool cFileManager::createCurrentProjectFolderTree()
{
    char fullProjectPath[PATCH_SIZE];
    getCurrentProjectPathWithName(fullProjectPath, PATCH_SIZE);
    char tempPath[PATCH_SIZE];

    // A pass creates the next missing folder of the path, so one pass per character is
    // always enough. The limit ends the loop for a path that never comes to exist even
    // though no mkdir fails.
    int passesLeft = strlen(fullProjectPath);
    bool mkdirFailed = false;

    while (1)
    {
        strncpy(tempPath, fullProjectPath, PATCH_SIZE);
        tempPath[PATCH_SIZE - 1] = 0;
        if (fileSystem->exists(tempPath))
        {
            return true;
        }

        if (mkdirFailed || passesLeft-- <= 0)
        {
            return false;
        }

        int pathLen = strlen(tempPath);
        int lastSlash = pathLen;
        int currentSlash = pathLen;

        while (pathLen > 0)
        {
            if (tempPath[pathLen] == '/' || pathLen <= 1)
            {
                lastSlash = currentSlash;
                currentSlash = pathLen;

                tempPath[currentSlash] = 0;

                if (pathLen <= 1 || fileSystem->exists(tempPath))
                {
                    strncpy(tempPath, fullProjectPath, PATCH_SIZE);
                    tempPath[lastSlash] = 0;
                    mkdirFailed |= !fileSystem->mkdir(0, tempPath);
                }
            }
            pathLen--;
        }
    }
}

// ====================================================================================
// =============================     RECENT    ========================================
// ====================================================================================
void cFileManager::addCurrentProjectToRecentList()
{
    if (!projectExist(currentProjectPatch, currentProjectName))
    {
        return;
    }

    readProjectRecentList();

    validateRecentListFile();

    char fullPath[PATCH_SIZE];
    getCurrentProjectPathWithName(fullPath, PATCH_SIZE);

    // remove duplicate if exist
    for (int idx = 0; idx < kRecentProjectListLength - 1; idx++)
    {
        if (strcmp(externalRamRecentProjectsList.project[idx].fullPath, fullPath) == 0)
        {
            for (int copyIdx = idx; copyIdx < kRecentProjectListLength - 1; copyIdx++)
            {
                externalRamRecentProjectsList.project[copyIdx] =
                  externalRamRecentProjectsList.project[copyIdx + 1];
            }

            memset(&externalRamRecentProjectsList.project[kRecentProjectListLength - 1],
                   0,
                   sizeof(externalRamRecentProjectsList.project[0]));
        }
    }

    // move each element down
    for (int idx = kRecentProjectListLength - 1; idx > 0; idx--)
    {
        externalRamRecentProjectsList.project[idx] = externalRamRecentProjectsList.project[idx - 1];
    }

    // set current project on the top
    strncpy(externalRamRecentProjectsList.project[0].fullPath, fullPath, PATCH_SIZE);
    externalRamRecentProjectsList.project[0].fullPath[PATCH_SIZE - 1] = 0;

    writeProjectRecentList();
}

bool cFileManager::getProjectRecentList(char** list, int& listLen)
{
    if (!readProjectRecentList())
    {
        listLen = 0;
        return false;
    }

    if (!validateRecentListFile())
    {
        writeProjectRecentList();
    }

    listLen = std::min(kRecentProjectListLength, listLen);

    for (int idx = 0; idx < listLen; idx++)
    {
        char* path = externalRamRecentProjectsList.project[idx].fullPath;

        if (path[0] == 0)
        {
            listLen = idx;
            break;
        }

        int pathLen = strlen(path);

        while (pathLen > 0 && path[pathLen] == '/')
        {
            pathLen--;
        }

        while (pathLen > 0 && path[pathLen] != '/')
        {
            pathLen--;
        }
        if (pathLen > 0)
        {
            pathLen++;
        }

        list[idx] = &path[pathLen];
    }

    return true;
}

bool cFileManager::readProjectRecentList()
{
    if (!fileSystem->exists(cProjectsRecentFilePath))
    {
        return false;
    }

    auto openFileInfo = fileSystem->openFile(cProjectsRecentFilePath);
    auto isOpen = std::get<0>(openFileInfo);
    auto file = std::get<1>(openFileInfo);

    if (!isOpen)
    {
        return false;
    }

    fileSystem->readFile(
      &externalRamRecentProjectsList, sizeof(externalRamRecentProjectsList), file);
    fileSystem->closeFile(file);

    return true;
}

void cFileManager::writeProjectRecentList()
{
    if (!fileSystem->exists(cDefaultProjectsPath))
    {
        fileSystem->mkdir(0, cDefaultProjectsPath);
    }

    auto openFileInfo =
      fileSystem->openFile(cProjectsRecentFilePath, FileSystem::FileOpenMode::kWriteOpen);
    auto isOpen = std::get<0>(openFileInfo);
    auto file = std::get<1>(openFileInfo);

    if (isOpen)
    {
        fileSystem->writeFile(
          &externalRamRecentProjectsList, sizeof(externalRamRecentProjectsList), file);
        fileSystem->closeFile(file);
    }
}

bool cFileManager::validateRecentListFile()
{
    bool correct = true;
    bool moveNext = false;
    int moveIdx = 0;
    int idx = 0;

    while (idx < kRecentProjectListLength)
    {
        char* path = externalRamRecentProjectsList.project[idx].fullPath;
        path[sizeof(strRecentFile::ProjectRecord::fullPath) - 1] = 0;

        if (path[0] == 0 || !fileSystem->exists(path))
        {
            moveNext = true;
            correct = false;
        }

        if (moveNext)
        {
            moveNext = false;

            if (idx < kRecentProjectListLength - 1)
            {
                for (int copyIdx = idx; copyIdx < kRecentProjectListLength - 1; copyIdx++)
                {
                    externalRamRecentProjectsList.project[copyIdx] =
                      externalRamRecentProjectsList.project[copyIdx + 1];
                }
            }

            memset(&externalRamRecentProjectsList.project[kRecentProjectListLength - 1],
                   0,
                   sizeof(externalRamRecentProjectsList.project[0]));

            moveIdx++;
            if (moveIdx >= kRecentProjectListLength - idx)
            {
                break;
            }
        }
        else
        {
            idx++;
        }
    }

    return correct;
}

bool cFileManager::openRecentProject(int indexOnRecentList)
{
    assert(indexOnRecentList >= 0 && indexOnRecentList < kRecentProjectListLength);

    char* projectFullPath = externalRamRecentProjectsList.project[indexOnRecentList].fullPath;

    char projectFilePath[PATCH_SIZE];
    volatile int pathSize = sizeof(projectFilePath);
    snprintf(projectFilePath, pathSize, "%s/%s", projectFullPath, cProjectFileName);

    if (!fileSystem->exists(projectFilePath))
    {
        return false;
    }

    int pathLen = strlen(projectFullPath);

    while (pathLen > 0 && projectFullPath[pathLen] == '/')
    {
        pathLen--;
    }

    while (pathLen > 0 && projectFullPath[pathLen] != '/')
    {
        pathLen--;
    }

    char projecName[PROJECT_NAME_SIZE];
    char projecPath[PATCH_SIZE];

    strncpy(projecName, &projectFullPath[pathLen > 0 ? pathLen + 1 : 0], PROJECT_NAME_SIZE);
    projecName[PROJECT_NAME_SIZE - 1] = 0;
    strncpy(projecPath, projectFullPath, PATCH_SIZE);
    projecPath[pathLen] = 0;

    return openProject(projecPath, projecName);
}
