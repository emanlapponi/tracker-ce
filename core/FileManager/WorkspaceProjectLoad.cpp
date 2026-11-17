#include "WorkspaceProjectLoad.h"

#include "fileManager.h"
#include "fileManagerDefs.h"
#include "fileTransfer.h"

#if FILEMANAGER_HANDLE_SYNTH == 1
#include "Synth.h"
#endif

#include "log.h"

#include <cstring>

void WorkspaceProjectLoad::update()
{
    WorkspaceProjectLoadStep nextStep = operationStep;

    switch (operationStep)
    {
        case WorkspaceProjectLoadStep::initStep:
        {
            loadProjectFromWorkspaceInit();
            nextStep = WorkspaceProjectLoadStep::loadProjectFile;
            break;
        }
        case WorkspaceProjectLoadStep::loadProjectFile:
        {
            loadProjectFileFromWorkspace();
            nextStep = WorkspaceProjectLoadStep::loadInstrumentStruct;
            break;
        }
        case WorkspaceProjectLoadStep::loadInstrumentStruct:
        {
            bool result = loadInstrStruct(currentInstrument, currentInstrumentFileDataOffset);
            if (result)
            {
                memoryToLoadDone += sizeof(strInstrumentFile);
                fileTransfer.clearBytesComplited();
                nextStep = WorkspaceProjectLoadStep::loadInstrumentSample;
            }
            else
            {
                if (currentInstrument < kSampleInstrumentsCount - 1)
                {
                    currentInstrument++;
                }
                else
                {
                    nextStep = WorkspaceProjectLoadStep::loadPatternFile;
                }
            }
            break;
        }
        case WorkspaceProjectLoadStep::loadInstrumentSample:
        {
            bool result = newFileManager.loadSampleData(
              currentInstrument, sampleLoadStep, currentInstrumentFileDataOffset, false);
            if (result)
            {
                if (currentInstrument < kSampleInstrumentsCount - 1)
                {
                    currentInstrument++;
                    sampleLoadStep = 0;
                    nextStep = WorkspaceProjectLoadStep::loadInstrumentStruct;
                }
                else
                {
                    nextStep = WorkspaceProjectLoadStep::loadPatternFile;
                }

                memoryToLoadDone += fileTransfer.getBytesComplited();
                fileTransfer.clearBytesComplited();
            }
            break;
        }
        case WorkspaceProjectLoadStep::loadPatternFile:
        {
            newFileManager.loadPatternFromWorkspace(mtProject.values.actualPattern);
            nextStep = WorkspaceProjectLoadStep::finishStep;
            break;
        }
        case WorkspaceProjectLoadStep::finishStep:
        {
            loadProjectFromWorkspaceFinish();
            break;
        }
        default: stopOperationWithError(fmLoadError); break;
    }

    operationStep = nextStep;
}

float WorkspaceProjectLoad::getProgress()
{
    float progress = 0;
    float complited = memoryToLoadDone;

    switch (operationStep)
    {
        case initStep:
        {
            progress = 0.05;
            break;
        }
        case loadProjectFile:
        {
            progress = 0.1;
            break;
        }
        case loadInstrumentSample:
        {
            complited += fileTransfer.getBytesComplited();
            [[fallthrough]];
        }
        case loadInstrumentStruct:
        {
            float memoryProgress =
              memoryToLoadTotal > 0 ? (complited / (float)memoryToLoadTotal * 0.9f) : 0;
            progress = 0.1 + memoryProgress;
            break;
        }
        default: break;
    }

    // LOGF("Load op: %d, pr: %.2f | Done: %0f / %d \n",
    //     operationStep,
    //     progress,
    //     complited,
    //     memoryToLoadTotal);

    return progress < 1.0f ? progress : 1.0f;
}

//=================================================================================================
//=================================================================================================
void WorkspaceProjectLoad::loadProjectFromWorkspaceInit()
{
    // tu czysci flagi zmian plikow bo podczas ladowania moga one zostac ustawione
    // jezeli np ladowany jest starsza wersja pliku i struktura jest modyfikwoana
    newFileManager.clearChangeFlags();

    currentInstrument = 0;
    sampleLoadStep = 0;

    memoryToLoadTotal = calcWorkspaceInstrumentsSize();
    memoryToLoadDone = 0;

    mtProject.used_memory = 0;
}

void WorkspaceProjectLoad::loadProjectFromWorkspaceFinish()
{
    if (newFileManager.getCurrentProjectName()[0] == 0)
    {
        // jezeli projekt jest otwierany bezposrednio z workspace np. na start urzadzenia to
        // pobierz nazwe z pliku projektu
        strcpy(newFileManager.getCurrentProjectName(),
               newFileManager.getProjectNamefromProjectFile());
        strcpy(newFileManager.getCurrentProjectPath(),
               newFileManager.getProjectPathfromProjectFile());
    }

    sequencer.switchRamPatternsNow();
    newFileManager.startAudioEngine();

    newFileManager.getPatternsMetadata()->loadPatternsMetadata();

#if FILEMANAGER_HANDLE_SYNTH == 1
    // updateToSettings() hands the voice count from the project file to the synth as it is,
    // and loadProjectValues() below validates the synth settings only after that.
    for (int part = 0; part < kSynthParts; part++)
    {
        newFileManager.getSynth()->validateSynthPartSettings(part,
                                                             mtProject.values.synthPart[part]);
    }
    newFileManager.getSynth()->updateToSettings(mtProject.values, true);
    for (int part = 0; part < kSynthParts; part++)
    {
        if (!newFileManager.getSynth()->loadPatchFromWorkspace(part))
        {
            newFileManager.getSynth()->initSynthPartMacrosValues(part,
                                                                 mtProject.values.synthPart[part]);
        }
    }
#endif

    newFileManager.loadProjectValues();

    if (newFileManager.needImportModFileAfterNewProject())
    {
        newFileManager.clearImportModFileAfterNewProject();
        newFileManager.setStatus(fmImportingMod);
        newFileManager.setCurrentOperation(fmImportModFile);
        newFileManager.resetCurrentOperationStep();
    }
    else
    {
        newFileManager.setStatus(fmLoadEnd);
        newFileManager.setCurrentOperation(fmNoOperation);
    }
}

//=================================================================================================
//=================================================================================================
bool WorkspaceProjectLoad::openProjectFromWorkspace()
{
    if (newFileManager.getStatus() != fmIdle)
        return false;
    if (newFileManager.getCurrentOperation() != fmNoOperation)
        return false;

    if (!fileSystem->exists(cWorkspacePath))
        return false;
    if (!fileSystem->exists(cProjectFileNameInWorkspace))
        return false;

    newFileManager.clearCurrentProjectName();
    newFileManager.clearCurrentProjectPath();

    operationStep = WorkspaceProjectLoad::initStep;
    newFileManager.setStatus(fmLoadingProjectfromWorkspace);
    newFileManager.setCurrentOperation(fmLoadWorkspaceProject);

    return true;
}

//=================================================================================================
//=================================================================================================
void WorkspaceProjectLoad::loadProjectFileFromWorkspace()
{
    strProjectFile* fileManagerProjectBuffer = new strProjectFile{};

    uint8_t loadStatus = fileTransfer.loadFileToMemory(cProjectFileNameInWorkspace,
                                                       (uint8_t*)fileManagerProjectBuffer,
                                                       sizeof(strProjectFile),
                                                       fileWholeOnce); // fileDivIntoParts

    if (loadStatus == fileTransferEnd)
    {
        if (!newFileManager.loadProjectFileFormFileStruct(&mtProject, fileManagerProjectBuffer))
        {
            stopOperationWithError(fmLoadError);
        }
        else
        {
            if (newFileManager.getCurrentProjectName()[0] == 0)
            {
                strcpy(newFileManager.getCurrentProjectName(),
                       newFileManager.getProjectNamefromProjectFile());
                strcpy(newFileManager.getCurrentProjectPath(),
                       newFileManager.getProjectPathfromProjectFile());
            }
            memoryToLoadTotal = calcWorkspaceInstrumentsSize();
        }
    }
    else if (loadStatus >= fileTransferError)
    {
        stopOperationWithError(fmLoadError);
    }

    delete fileManagerProjectBuffer;
}

//=================================================================================================
//=================================================================================================
// return value: true = success, false = fail
bool WorkspaceProjectLoad::loadInstrStruct(int idx, uint32_t& instrumentFileDataOffsetOut)
{
    if (idx >= kSampleInstrumentsCount)
    {
        return false;
    }

    char instrumentToLoad[PATCH_SIZE];
    volatile int pathSize = sizeof(instrumentToLoad);

    snprintf(instrumentToLoad, pathSize, cWorkspaceInstrumentFileFormatNew, idx + 1);

    if (!fileSystem->exists(instrumentToLoad))
    {
        char instrumentToConvert[PATCH_SIZE];
        sprintf(instrumentToConvert, cWorkspaceInstrumentFileFormatOld, idx + 1);

        if (fileSystem->exists(instrumentToConvert))
        {
            fileSystem->rename(instrumentToConvert, instrumentToLoad);
        }
    }

    strInstrumentFile fileManagerInstrumentBuffer;
    uint32_t instrumentFileSize = fileSystem->getFileSize(instrumentToLoad);

    uint8_t loadStatus = fileTransfer.loadFileToMemory(instrumentToLoad,
                                                       (uint8_t*)&fileManagerInstrumentBuffer,
                                                       sizeof(strInstrumentFile),
                                                       fileWholeOnce);

    if (loadStatus == fileTransferEnd)
    {
        if (newFileManager.loadInstrumentFormFileStruct(&mtProject.instrument[idx],
                                                        &fileManagerInstrumentBuffer,
                                                        instrumentFileSize,
                                                        instrumentFileDataOffsetOut))
        {
            newFileManager.removeWavExtensionFromName(mtProject.instrument[idx].sample.file_name);
            mtProject.instrument[idx].isActive = 1;
            return true;
        }
    }

    memset(mtProject.instrument[idx].sample.file_name, 0, SAMPLE_NAME_SIZE);
    mtProject.instrument[idx].isActive = 0;
    return false;
}

//=================================================================================================
//=================================================================================================
uint32_t WorkspaceProjectLoad::calcWorkspaceInstrumentsSize()
{
    char currentPatch[PATCH_SIZE];
    volatile int pathSize = sizeof(currentPatch);
    uint32_t size = 0;

    for (uint8_t i = 0; i < kSampleInstrumentsCount; i++)
    {
        snprintf(currentPatch, pathSize, cWorkspaceInstrumentFileFormatNew, i + 1);
        if (fileSystem->exists(currentPatch))
        {
            uint32_t fileSize = fileSystem->getFileSize(currentPatch);
            char backingPath[PATCH_SIZE];
            if (fileSize <= sizeof(strInstrumentFile) &&
                newFileManager.resolveProjectInstrumentPtiPath(i, backingPath, sizeof(backingPath)))
            {
                size += fileSystem->getFileSize(backingPath);
            }
            else
            {
                size += fileSize;
            }
        }
        else
        {
            snprintf(currentPatch, pathSize, cWorkspaceInstrumentFileFormatOld, i + 1);
            if (fileSystem->exists(currentPatch))
            {
                size += sizeof(strInstrumentFile);
            }

            snprintf(currentPatch, pathSize, cWorkspaceSamplesFilesFormat, i + 1);
            size += (2 * newFileManager.getWaveSizeIfValid(currentPatch)); // 2 steps = LOAD > SAVE
        }
    }
    return size;
}
