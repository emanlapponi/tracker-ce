#include "fileManagerDefs.h"
#include "mtConfig.h"
#include "mtStructs.h"

#include "string.h"
#include "util_string.h"

#include "mtSequencer.h"

#include "AudioDataUtilities.h"
#include "fileManager.h"
#include "fileTransfer.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <inttypes.h>
#include <stdio.h>

void cFileManager::updateBrowseSamples() // fmBrowseSamples - 5
{
    switch (currentOperationStep)
    {
        case 0: browseCurrentLocation(); break;
        case 1: listOnlyWavFromActualPath(); break;
        case 2: processDirFileSizes(); break;
        default: stopOperationWithError(fmBrowseSamplesError); break;
    }
}

void cFileManager::updateBrowseFirmwares() // fmBrowseFirmwares - 6
{
    switch (currentOperationStep)
    {
        case 0: browseFirmwaresLocation(); break;
        default: stopOperationWithError(fmBrowseFirmwaresError); break;
    }
}

void cFileManager::updateBrowseGames()
{
    switch (currentOperationStep)
    {
        case 0: browseGamesLocation(); break;
        default: stopOperationWithError(fmBrowseGamesError); break;
    }
}

static bool folderExists(FileSystem* fileSystem, const char* path, int pathLength, const char* name)
{
    char fullPath[PATCH_SIZE];
    int length = snprintf(fullPath, sizeof(fullPath), "%.*s%s", pathLength, path, name);

    return length < (int)sizeof(fullPath) && fileSystem->exists(fullPath);
}

// A folder with more files than the list can hold is listed as several entries, "name (1-100)",
// "name (101-200)" and so on. A folder can be named like that itself, so an entry is only taken
// for such a page when no folder of exactly that name exists in path, which name points into.
void parseFolderName(FileSystem* fileSystem,
                     const char* path,
                     const char* input,
                     char* name,
                     uint16_t* offset)
{
    const char* start = strrchr(input, '(');
    uint16_t dummy;
    int items;

    if (start == NULL)
    {
        strcpy(name, input);
        *offset = 0;
        return;
    }

    items = sscanf(start, "(%" SCNd16 "-%" SCNd16 ")", offset, &dummy);

    if (items == 2 && !folderExists(fileSystem, path, name - path, input))
    {
        strncpy(name, input, start - input);
        name[start - input] = '\0';
        (*offset)--;
    }
    else
    {
        strcpy(name, input);
        *offset = 0;
    }
}

bool cFileManager::browseSdCard(uint8_t* index)
{
    stopAutosave();

    if (status != fmIdle && status != fmSavingProjectToWorkspace)
        return false;
    if (currentOperation != fmNoOperation && currentOperation != fmSaveWorkspaceProject)
        return false;

    // dla bezpieczenstwa reset sciezki jesli jestesmy na poziomie 0
    if (explorerDirLevel == 0)
    {
        strcpy(explorerCurrentPath, "/");
    }

    if (index == nullptr) // tylko odswiez
    {
        status = fmBrowsingSamples;
        currentOperationStep = 0;
        currentOperation = fmBrowseSamples;

        return true;
    }

    // Entering a folder adds its list entry to the path. A folder that would not fit is not
    // entered; the files in it could not be addressed anyway.
    size_t pathLength = explorerDirLevel == 0 ? 0 : strlen(explorerCurrentPath);

    if ((explorerDirLevel == 0 || *index > 0) &&
        pathLength + strlen(explorerList[*index]) >= sizeof(explorerCurrentPath))
    {
        return false;
    }

    if (explorerDirLevel == 0)
    {
        explorerDirLevel = 1;

        parseFolderName(fileSystem,
                        explorerCurrentPath,
                        explorerList[*index],
                        explorerCurrentPath,
                        &explorerCurrentOffset);
        explorerPositionTable[explorerCurrentPosition] = *index;
        explorerCurrentPosition++;

        *index = 0;
    }
    else
    {
        if (*index > 0)
        {
            if (explorerCurrentPosition < PREVIOUS_POSITION_LIFO)
            {
                explorerDirLevel++;

                explorerPositionTable[explorerCurrentPosition] = *index;
                explorerCurrentPosition++;
                parseFolderName(fileSystem,
                                explorerCurrentPath,
                                explorerList[*index],
                                explorerCurrentPath + strlen(explorerCurrentPath),
                                &explorerCurrentOffset);
                *index = 0;
            }
        }
        else
        {
            explorerDirLevel--;

            if (explorerDirLevel > 0)
            {
                goUpInActualPath();
            }
            else
            {
                strcpy(explorerCurrentPath, "/");
            }

            explorerCurrentPosition--;
            *index = explorerPositionTable[explorerCurrentPosition];
            explorerCurrentOffset = 0;
        }
    }

    status = fmBrowsingSamples;
    currentOperationStep = 0;
    currentOperation = fmBrowseSamples;

    return true;
}

// wymusza przy nastepnym wywolaniu browseSdCard()
// wyzerowanie pokazywanej lokalizaji do poczatku karty SD
void cFileManager::resetBrowse()
{
    explorerDirLevel = 0;
    explorerCurrentPosition = 0;
    // strcpy(explorerCurrentPath, "/");
}

char** cFileManager::getExplorerList()
{
    return explorerList;
}

int cFileManager::getExplorerListLength()
{
    return explorerListLength;
}

char* cFileManager::getExplorerPath()
{
    return explorerCurrentPath;
}

uint8_t cFileManager::getBrowsedFilesList(char*** list, uint32_t** memoryList)
{
    *list = explorerList;
    *memoryList = currentFolderMemoryFileUsage;

    return explorerListLength;
}

void cFileManager::browseCurrentLocation()
{
    explorerListLength = 0;
    listWavStage = 0;

    listOnlyFolderNames();
    moveToNextOperationStep();
}

void cFileManager::listOnlyFolderNames()
{
    int allFoldersNum;
    uint8_t parentFolder = explorerDirLevel == 0 ? 0 : 1;
    FileSystem::enListFileFilter folderSizeFilter = FileSystem::enListFileFilter_all;

    switch (currentOperation)
    {
        case fmBrowseSamples: folderSizeFilter = FileSystem::enListFileFilter_wave; break;
        case fmBrowseProjects: folderSizeFilter = FileSystem::enListFileFilter_folder; break;
        case fmBrowseMods: folderSizeFilter = FileSystem::enListFileFilter_mod; break;
        case fmBrowseFirmwares: folderSizeFilter = FileSystem::enListFileFilter_firmware; break;
    }

    allFoldersNum = fileSystem->createFilesList(explorerCurrentPath,
                                                parentFolder,
                                                explorerList,
                                                list_length_max,
                                                5000,
                                                FileSystem::enListFileFilter_folder,
                                                0,
                                                list_length_max - 1,
                                                folderSizeFilter);

    // A folder that cannot be opened is shown as an empty one, which leaves the way back up.
    // The list still holds the entries of the previous folder then and has to be cleared here.
    bool listFailed = allFoldersNum == FileSystem::kListError;

    explorerFolderCount = listFailed ? 0 : allFoldersNum - parentFolder;

    if (explorerCurrentOffset != 0 || listFailed)
    {
        allFoldersNum = parentFolder;

        for (int i = 0; i < list_length_max; i++)
        {
            if (explorerList[i] != nullptr)
            {
                delete explorerList[i];
                explorerList[i] = nullptr;
            }
        }
    }

    if (parentFolder)
    {
        explorerList[0] = new char[4];
        strcpy(explorerList[0], "/..");
    }

    std::sort(&explorerList[parentFolder],
              &explorerList[parentFolder] + allFoldersNum - parentFolder,
              [](char* a, char* b) { return compareStrings(a, b); });

    explorerListLength += allFoldersNum;
}

void cFileManager::listOnlyWavFromActualPath()
{

    if (listWavStage == 0) // stage 0
    {
        int filesFound =
          fileSystem->createFilesList(explorerCurrentPath,
                                      0,
                                      explorerList + explorerListLength,
                                      (list_length_max - explorerListLength),
                                      8000,
                                      FileSystem::enListFileFilter_wave_inst,
                                      std::max(explorerCurrentOffset - explorerFolderCount, 0));

        if (filesFound == 0 || // nie ma plikow wav w folderze - koncz listowanie
            filesFound == FileSystem::kListError)
        {
            browseFinish();
            return;
        }

        openCalcStart = explorerListLength;
        openCurrentPos = openCalcStart;
        explorerListLength += filesFound;
        openCalcEnd = explorerListLength;

        listWavStage = 1;
    }
    else if (listWavStage == 1) // stage 1 - przefiltruj znalezione wav o nie obslugiwane
    {
        char* fileName = explorerList[openCurrentPos];
        if (hasFileExtension(fileName, "wav"))
        {
            char wav_file[255];
            strcpy(wav_file, explorerCurrentPath);
            strcat(wav_file, "/");
            strcat(wav_file, explorerList[openCurrentPos]);

            auto [isOpen, fileHandle] = fileSystem->openFile(wav_file);
            if (isOpen)
            {
                strWavFileHeader localHeader;
                bool headerRead = readHeader(&localHeader, fileHandle);
                fileSystem->closeFile(fileHandle);

                if (headerRead && checkSampleValid(localHeader))
                {
                    openCurrentPos++;
                    if (openCurrentPos >= explorerListLength)
                    {
                        listWavStage = 2;
                    }
                    return;
                }
            }
        }
        else if (hasFileExtension(fileName, "pti"))
        {
            openCurrentPos++;
            if (openCurrentPos >= explorerListLength)
            {
                listWavStage = 2;
            }
            return;
        }

        if (openCurrentPos < explorerListLength - 1)
        {
            std::swap(explorerList[openCurrentPos], explorerList[explorerListLength - 1]);
        }
        else
        {
            // browseFinish();
            listWavStage = 2;
        }
        explorerListLength--;

        //		if(openCurrentPos > 1)	explorerListLength = openCurrentPos;
        //		else explorerListLength = 1;
        //		browseFinish();
        //		listWavStage = 2;
    }
    else //  stage 2 - posortuj alfabetycznie
    {
        if (openCalcStart < explorerListLength - 1)
        {
            std::sort(explorerList + openCalcStart,
                      explorerList + explorerListLength,
                      [](char* a, char* b) { return compareStrings(a, b); });
        }

        openCurrentPos = openCalcStart;
        moveToNextOperationStep();
        listWavStage = 0;
    }
}

void cFileManager::processDirFileSizes() // wykonywalne w petli
{
    char filePath[PATCH_SIZE];

    strcpy(filePath, explorerCurrentPath);
    if (explorerDirLevel > 0)
    {
        strcat(filePath, "/");
    }

    strcat(filePath, explorerList[openCurrentPos]);

    // if its wav file get size after conversion
    currentFolderMemoryFileUsage[openCurrentPos] =
      getWaveSizeAfterConversion(filePath, stereoSampleMode);

    // if its instrument file get pti sample data size
    if (currentFolderMemoryFileUsage[openCurrentPos] == 0)
    {
        currentFolderMemoryFileUsage[openCurrentPos] = getInstrumentSampleSizeIfValid(filePath);
    }

    openCurrentPos++;

    // uint8_t progress = (((openCurrentPos - openCalcStart) * 100) / (openCalcEnd-openCalcStart));
    // todo progress

    if (openCurrentPos >= explorerListLength)
    {
        browseFinish();
        // finisz
    }
}

void cFileManager::browseFinish()
{
    status = fmBrowseSamplesEnd;
    currentOperationStep = 0;
    currentOperation = fmNoOperation;
}

void cFileManager::goUpInActualPath()
{
    uint8_t length = strlen(explorerCurrentPath);

    explorerCurrentPath[length] = 0;

    char* ptr = strrchr(explorerCurrentPath, '/');

    if (ptr == nullptr)
    {
        strcpy(explorerCurrentPath, "/");
        return;
    }

    explorerCurrentPath[(ptr - explorerCurrentPath)] = 0;
}

///====================================================================================================
///====================================================================================================
///====================================================================================================
void cFileManager::browseFirmwaresLocation()
{

    int locationFileCount = fileSystem->createFilesList(
      "Firmware", 0, firmwaresList, list_length_max, 6000, fileSystem->enListFileFilter_firmware);

    if (locationFileCount == fileSystem->kListError)
    {
        stopOperationWithError(fmBrowseFirmwaresError);
        return;
    }

    std::sort(firmwaresList,
              firmwaresList + locationFileCount,
              [](char* a, char* b) { return compareStrings(b, a); });

    firmwaresListLength = locationFileCount;

    status = fmBrowseFirmwaresEnd;
    currentOperationStep = 0;
    currentOperation = fmNoOperation;
}

bool cFileManager::browseFirmwares()
{
    stopAutosave();

    status = fmBrowsingFirmwares;
    currentOperationStep = 0;
    currentOperation = fmBrowseFirmwares;

    return true;
}

uint8_t cFileManager::getFirmwaresList(char*** list)
{
    *list = firmwaresList;
    return firmwaresListLength;
}

///====================================================================================================
///====================================       GAMES ===============================================
///====================================================================================================
void cFileManager::browseGamesLocation()
{
    if (!fileSystem->exists(cGamesPath))
    {
        fileSystem->mkdir(0, cGamesPath);
    }

    uint8_t gamesfoundCount = fileSystem->createFilesList(
      cGamesPath, 0, projectsList, list_length_max, 3000, FileSystem::enListFileFilter_game);

    if (gamesfoundCount == FileSystem::kListError)
    {
        stopOperationWithError(fmBrowseGamesError);
        return;
    }

    std::sort(projectsList,
              projectsList + gamesfoundCount,
              [](char* a, char* b) { return compareStrings(a, b); });

    char lowCase[50];
    // usuwanie koncówek .nes
    for (uint8_t i = 0; i < gamesfoundCount; i++)
    {
        strcpy(lowCase, projectsList[i]);
        for (uint16_t a = 0; a < 50; a++)
        {
            lowCase[a] = tolower(lowCase[a]);
            if (lowCase[a] == 0)
                break;
        }

        char* nes = strstr(lowCase, ".nes\0");
        if (nes != nullptr)
        {
            *(projectsList[i] + (nes - lowCase)) = 0;
        }
    }

    projectsListLength = gamesfoundCount;

    status = fmBrowseGamesEnd;
    currentOperationStep = 0;
    currentOperation = fmNoOperation;
}

bool cFileManager::browseGames()
{
    stopAutosave();

    if (status != fmIdle && status != fmSavingProjectToWorkspace)
        return false;
    if (currentOperation != fmNoOperation && currentOperation != fmSaveWorkspaceProject)
        return false;

    status = fmBrowsingGames;
    currentOperationStep = 0;
    currentOperation = fmBrowseGames;

    return true;
}

uint8_t cFileManager::getGamesList(char*** list)
{
    *list = projectsList;
    return projectsListLength;
}

const char* cFileManager::getProjectsDefaultPath()
{
    return cDefaultProjectsPath;
}

const char* cFileManager::getModsDefaultPath()
{
    return cModsPath;
}

void cFileManager::createProjectsAndModFolders()
{
    if (!fileSystem->exists(cDefaultProjectsPath))
    {
        fileSystem->mkdir(0, cDefaultProjectsPath);
    }

    if (!fileSystem->exists(cModsPath))
    {
        fileSystem->mkdir(0, cModsPath);
    }
}

void cFileManager::createPatchesFolder()
{
    if (!fileSystem->exists(cDefaultPatchesPath))
    {
        fileSystem->mkdir(0, cDefaultPatchesPath);
    }
}
