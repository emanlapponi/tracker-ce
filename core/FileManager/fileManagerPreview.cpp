#include "fileManagerDefs.h"
#include "mtStructs.h"

#include "fileManager.h"
#include "fileTransfer.h"

#include "mtAudioEngine.h"

bool cFileManager::previevSamplefromSD(uint8_t index, uint16_t from, uint16_t to)
{
    stopAutosave();

    if (status != fmIdle && status != fmSavingProjectToWorkspace)
        return false;
    if (currentOperation != fmNoOperation && currentOperation != fmSaveWorkspaceProject)
        return false;

    if (explorerList[index][0] == '/')
        return false;

    char filePath[255];
    strcpy(filePath, explorerCurrentPath);
    strcat(filePath, "/");
    strcat(filePath, explorerList[index]);

    if (!fileSystem->exists(filePath))
        return false;

    if (fileSystem->isFileOpen(wavfile))
    {
        fileSystem->closeFile(wavfile, false);
    }

    //	wavfile.open(filePath);
    //
    //	if(!wavfile)
    //	{
    //		wavfile.close();
    //		return false;
    //	}

    // strWavFileHeader header;
    // readHeader(&header,&wavfile);
    // wavfile.close();

    engine.getSdPlayer()->play(filePath, from, to);

    status = fmPreviewSampleFromSd;
    currentOperationStep = 0;
    currentOperation = fmPreviewSamplesFromSD;

    return true;
}

bool cFileManager::stopPrevievSamplefromSD()
{
    if (status != fmPreviewSampleFromSd && currentOperation != fmPreviewSamplesFromSD)
        return false;

    engine.getSdPlayer()->stop();

    status = fmIdle;
    currentOperationStep = 0;
    currentOperation = fmNoOperation;

    return true;
}
