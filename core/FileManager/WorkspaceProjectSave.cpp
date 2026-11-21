#include "WorkspaceProjectSave.h"

#include "fileManager.h"
#include "fileManagerDefs.h"
#include "fileTransfer.h"
#include "log.h"

#if FILEMANAGER_HANDLE_SYNTH == 1
#include "Synth.h"
#endif

#include <cstring>

void WorkspaceProjectSave::update()
{
    WorkspaceProjectSaveStep nextStep = operationStep;

    switch (operationStep)
    {
        case WorkspaceProjectSaveStep::initStep:
        {
            saveProjectToWorkspaceInit();
            nextStep = WorkspaceProjectSaveStep::savePatternFile;
            break;
        }
        case WorkspaceProjectSaveStep::savePatternFile:
        {
            newFileManager.savePatternToWorkspace();
            nextStep = WorkspaceProjectSaveStep::saveInstrumentStruct;
            break;
        }
        case WorkspaceProjectSaveStep::saveInstrumentStruct:
        {
            if (currentInstrument >= kSampleInstrumentsCount) { nextStep = WorkspaceProjectSaveStep::saveProjectFile; break; }
            bool result = newFileManager.saveInstrumentStructToWorkspace(currentInstrument);

            if ((result && !newFileManager.isWorkspaceInstrumentPtiFileValid(currentInstrument)) ||
                newFileManager.isInstrumentSampleChanged(currentInstrument))
            {
                newFileManager.setInstrumentSampleChanged(currentInstrument);
                nextStep = WorkspaceProjectSaveStep::saveInstrumentSample;
            }
            else
            {
                if (currentInstrument < kSampleInstrumentsCount - 1)
                {
                    currentInstrument++;
                }
                else
                {
                    nextStep = WorkspaceProjectSaveStep::saveProjectFile;
                }
            }
            break;
        }
        case WorkspaceProjectSaveStep::saveInstrumentSample:
        {
            bool result = true;
            if (newFileManager.isInstrumentSampleChanged(currentInstrument))
            {
                result = newFileManager.saveSampleDataToWorkspace(currentInstrument);
            }
            if (result)
            {
                newFileManager.clearSampleChanged(currentInstrument);
                if (currentInstrument < kSampleInstrumentsCount - 1)
                {
                    currentInstrument++;
                    nextStep = WorkspaceProjectSaveStep::saveInstrumentStruct;
                }
                else
                {
                    nextStep = WorkspaceProjectSaveStep::saveProjectFile;
                }
            }

            break;
        }
        case WorkspaceProjectSaveStep::saveProjectFile:
        {
            newFileManager.updateActualPatternBitmask();

            if (newFileManager.isProjectStructChanged())
            {
                newFileManager.saveProjectFileToWorkspace();
            }
            nextStep = WorkspaceProjectSaveStep::finishStep;
            break;
        }
        case WorkspaceProjectSaveStep::finishStep:
        {
            saveProjectToWorkspaceFinish();
            break;
        }
        default: stopOperationWithError(fmSaveError); break;
    }

    operationStep = nextStep;
}

//=================================================================================================
//=================================================================================================
void WorkspaceProjectSave::saveProjectToWorkspaceInit()
{
    currentInstrument = 0;
    // znajduje pierwszy instrument z flaga zmian i jednoczesnie aktywny
    while ((!newFileManager.isInstrumentStructChanged(currentInstrument) &&
            !newFileManager.isInstrumentSampleChanged(currentInstrument)) ||
           mtProject.instrument[currentInstrument].isActive == 0)
    {
        currentInstrument++;
        if (currentInstrument >= kSampleInstrumentsCount)
        {
            break;
        }
    }
}

void WorkspaceProjectSave::saveProjectToWorkspaceFinish()
{
#if FILEMANAGER_HANDLE_SYNTH == 1
    newFileManager.getSynth()->saveDirtyWorkspacePatches();
#endif

    if (newFileManager.getStatus() == fmSavingProjectToProjects) // przejdz do kopiowania projektu
    {
        newFileManager.setCurrentOperation(fmCopyWorkspaceToProjects);
        return;
    }

    newFileManager.setStatus(fmIdle); // xxx zamiast fmSaveEnd
    newFileManager.setCurrentOperation(fmNoOperation);
}

//=================================================================================================
//=================================================================================================
bool WorkspaceProjectSave::saveProjectToWorkspace(bool forceSaveAll)
{
    if (newFileManager.getStatus() != fmIdle &&
        newFileManager.getStatus() != fmSavingProjectToProjects)
    {
        return false;
    }
    if (newFileManager.getCurrentOperation() != fmNoOperation)
    {
        return false;
    }

    if (forceSaveAll)
    {
        newFileManager.setAllChangeFlags();
    }
    else
    {
        bool saveSynthPatchesToWorkspace = false;
#if FILEMANAGER_HANDLE_SYNTH == 1
        if (newFileManager.getSynth()->isAnyPatchDirty())
        {
            saveSynthPatchesToWorkspace = true;
        }
#endif
        if (!saveSynthPatchesToWorkspace && !newFileManager.isProjectChanged())
        {
            return false; // nie sejwuj jesli nic nie jest zmodyfikowane
        }
    }

    if (newFileManager.getStatus() == fmSavingProjectToProjects)
    {
        // jezeli zapis przed zapisaniem do Projects to finalnie wszsytko bedzie aktualne
        mtProject.values.projectNotSavedFlag = 0;
    }
    else
    {
        // jezeli typowy autozapis to od teraz
        // workspace napewo sie rozni od zawartosci Projects
        mtProject.values.projectNotSavedFlag |= 1;
    }

    LOG("workspace save");

    operationStep = WorkspaceProjectSaveStep::initStep;
    newFileManager.setStatus(fmSavingProjectToWorkspace);
    newFileManager.setCurrentOperation(fmSaveWorkspaceProject);

    return true;
}

//=================================================================================================
//=================================================================================================
// stopping auto save allowing to start other operation without any conficts
void WorkspaceProjectSave::stop()
{
    if (newFileManager.getStatus() == fmSavingProjectToWorkspace &&
        newFileManager.getCurrentOperation() == fmSaveWorkspaceProject)
    {
        if (operationStep == WorkspaceProjectSaveStep::saveInstrumentSample)
        {
            // A failed write ends the operation without leaving this step.
            while (operationStep == WorkspaceProjectSaveStep::saveInstrumentSample &&
                   newFileManager.getCurrentOperation() == fmSaveWorkspaceProject)
            {
                update();
            }
        }

        newFileManager.setStatus(fmIdle);
        newFileManager.setCurrentOperation(fmNoOperation);
    }
}
