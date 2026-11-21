#include "fileManagerDefs.h"
#include "mtStructs.h"
#include <mtAudioExport.h>

#include "VoiceStreamManager.h"
#include "fileManager.h"
#include "fileTransfer.h"
#include <algorithm>

static volatile uint8_t exportStep;

static bool getSongExportFolderName(char* path);
static bool getSongStemsExportFolderName(char* path);
static bool getPatternExportFolderName(char* path);
static bool getPatternStemsExportFolderName(char* path);
static bool getRenderSelectionExportFileName(char* path);

void cFileManager::updateExportSound() // fmExportSound - 13
{
    switch (currentOperationStep)
    {
        case 0: exportSoundGetStatus(); break;
        case 1: exportSoundEnd(); break;
        default: stopOperationWithError(fmExportSoundError); break;
    }
}

void cFileManager::updateExportProjectSamples()
{
    switch (currentOperationStep)
    {
        case 0: exportSamplesStart(); break;
        case 1: exportSoundEnd(); break;
        default: stopOperationWithError(fmExportSoundError); break;
    }
}

bool cFileManager::exportSound(uint8_t mode)
{
    stopAutosave();

    if (status != fmIdle && status != fmSavingProjectToWorkspace)
        return false;
    if (currentOperation != fmNoOperation && currentOperation != fmSaveWorkspaceProject)
        return false;

    char currentExportPath[255];

    // make dirs boys
    if (!fileSystem->exists(cExportPath))
        fileSystem->mkdir(0, cExportPath);
    sprintf(currentExportPath, cExportProjectPathFormat, newFileManager.getCurrentProjectName());
    if (!fileSystem->exists(currentExportPath))
        fileSystem->mkdir(0, currentExportPath);

    uint8_t temp_status = fmIdle;

    switch (mode)
    {
        case exportPattern:
            temp_status = fmExportingSoundPattern;
            if (!getPatternExportFolderName(currentExportPath))
                return false;
            if (!fileSystem->exists(currentExportPath))
                fileSystem->mkdir(0, currentExportPath);
            break;

        case exportPatternStems:
            temp_status = fmExportingSoundPatternStems;
            if (!getPatternStemsExportFolderName(currentExportPath))
                return false;
            if (!fileSystem->exists(currentExportPath))
                fileSystem->mkdir(0, currentExportPath);
            break;

        case exportSong:
            temp_status = fmExportingSoundSong;
            if (!getSongExportFolderName(currentExportPath))
                return false;
            if (!fileSystem->exists(currentExportPath))
                fileSystem->mkdir(0, currentExportPath);
            break;
        case exportSongStems:
            temp_status = fmExportingSoundSongStems;
            if (!getSongStemsExportFolderName(currentExportPath))
                return false;
            if (!fileSystem->exists(currentExportPath))
                fileSystem->mkdir(0, currentExportPath);
            break;

        case exportPatternSelection:
            temp_status = fmExportingSoundRenderSelection;
            if (!getRenderSelectionExportFileName(currentExportPath))
                return false;
            // if(!fileSystem->exists("Render")) fileSystem->mkdir(0,"Render");
            break;
        default: return false;
    }

    exporter.start(currentExportPath, mode);

    status = temp_status;
    currentOperationStep = 0;
    currentOperation = fmExportSound;
    return true;
}

bool cFileManager::exportSoundRenderSelection(char* filePath)
{
    stopAutosave();

    if (status != fmIdle && status != fmSavingProjectToWorkspace)
        return false;
    if (currentOperation != fmNoOperation && currentOperation != fmSaveWorkspaceProject)
        return false;

    char currentExportPath[255];

    // make dirs boys
    if (!fileSystem->exists(cExportPath))
        fileSystem->mkdir(0, cExportPath);

    sprintf(currentExportPath, cExportProjectPathFormat, newFileManager.getCurrentProjectName());
    if (!fileSystem->exists(currentExportPath))
        fileSystem->mkdir(0, currentExportPath);

    strcat(currentExportPath, "/Selection");
    if (!fileSystem->exists(currentExportPath))
        fileSystem->mkdir(0, currentExportPath);

    strcat(currentExportPath, "/");
    strcat(currentExportPath, filePath);
    strcat(currentExportPath, ".wav");

    exporter.start(currentExportPath, exportPatternSelection);

    status = fmExportingSoundRenderSelection;
    currentOperationStep = 0;
    currentOperation = fmExportSound;
    return true;
}

void cFileManager::exportSoundEnd()
{
    status = fmExportSoundEnd;
    currentOperationStep = 0;
    currentOperation = fmNoOperation;
}

void cFileManager::exportSoundGetStatus()
{
    if (exporter.getStatus() == 0)
    {
        exportSoundEnd();
    }
}

bool cFileManager::exportSoundCancel()
{
    exporter.cancel();
    exportSoundEnd();
    return true;
}

uint8_t cFileManager::getExportProgress()
{
    return exporter.getProgress();
}

static bool getSongExportFolderName(char* path)
{
    uint16_t fileCounter = 1;

    sprintf(path,
            "Export/%s/%s_Song_%04d",
            newFileManager.getCurrentProjectName(),
            newFileManager.getCurrentProjectName(),
            fileCounter++);
    while (newFileManager.getFileSystem()->exists(path))
    {
        sprintf(path,
                "Export/%s/%s_Song_%04d",
                newFileManager.getCurrentProjectName(),
                newFileManager.getCurrentProjectName(),
                fileCounter++);
        if (fileCounter > 9999)
            return false; // jak ktos zapisze jeden projekt 10000 razy to należy mu się medal z
                          // ziemniaka todo: obsłużyć jakoś
    }

    return true;
}

static bool getSongStemsExportFolderName(char* path)
{
    uint16_t fileCounter = 1;

    sprintf(path,
            "Export/%s/%s_Song_S%04d",
            newFileManager.getCurrentProjectName(),
            newFileManager.getCurrentProjectName(),
            fileCounter++);
    while (newFileManager.getFileSystem()->exists(path))
    {
        sprintf(path,
                "Export/%s/%s_Song_S%04d",
                newFileManager.getCurrentProjectName(),
                newFileManager.getCurrentProjectName(),
                fileCounter++);
        if (fileCounter > 9999)
            return false; // jak ktos zapisze jeden projekt 10000 razy to należy mu się medal z
                          // ziemniaka todo: obsłużyć jakoś
    }

    return true;
}

static bool getPatternExportFolderName(char* path)
{
    uint16_t namePattern = mtProject.values.actualPattern;
    uint16_t fileCounter = 1;

    sprintf(path,
            "Export/%s/%s_P%d_%04d",
            newFileManager.getCurrentProjectName(),
            newFileManager.getCurrentProjectName(),
            namePattern,
            fileCounter++);
    while (newFileManager.getFileSystem()->exists(path))
    {
        sprintf(path,
                "Export/%s/%s_P%d_%04d",
                newFileManager.getCurrentProjectName(),
                newFileManager.getCurrentProjectName(),
                namePattern,
                fileCounter++);
        if (fileCounter > 9999)
            return 1; // jak ktos zapisze jeden projekt 10000 razy to należy mu się medal z
                      // ziemniaka todo: obsłużyć jakoś
    }

    return true;
}

static bool getPatternStemsExportFolderName(char* path)
{
    uint16_t namePattern = mtProject.values.actualPattern;
    uint16_t fileCounter = 1;

    sprintf(path,
            "Export/%s/%s_P%d_S%04d",
            newFileManager.getCurrentProjectName(),
            newFileManager.getCurrentProjectName(),
            namePattern,
            fileCounter++);
    while (newFileManager.getFileSystem()->exists(path))
    {
        sprintf(path,
                "Export/%s/%s_P%d_S%04d",
                newFileManager.getCurrentProjectName(),
                newFileManager.getCurrentProjectName(),
                namePattern,
                fileCounter++);
        if (fileCounter > 9999)
            return 1; // jak ktos zapisze jeden projekt 10000 razy to należy mu się medal z
                      // ziemniaka todo: obsłużyć jakoś
    }

    return true;
}

static bool getRenderSelectionExportFileName(char* path)
{
    uint16_t fileCounter = 1;

    sprintf(path,
            "Export/%s/%s_Sel%04d",
            newFileManager.getCurrentProjectName(),
            newFileManager.getCurrentProjectName(),
            fileCounter++);
    while (newFileManager.getFileSystem()->exists(path))
    {
        sprintf(path,
                "Export/%s/%s_Sel%04d",
                newFileManager.getCurrentProjectName(),
                newFileManager.getCurrentProjectName(),
                fileCounter++);
        if (fileCounter > 9999)
            return 1; // jak ktos zapisze jeden projekt 10000 razy to należy mu się medal z
                      // ziemniaka todo: obsłużyć jakoś
    }

    return true;
}

bool cFileManager::exportProjectSamples()
{
    stopAutosave();

    if (status != fmIdle && status != fmSavingProjectToWorkspace)
        return false;
    if (currentOperation != fmNoOperation && currentOperation != fmSaveWorkspaceProject)
        return false;

    if (!createCurrentProjectFolderTree())
    {
        return false;
    }

    char currentExportPath[PATCH_SIZE];
    volatile int pathSize = sizeof(currentExportPath);

    snprintf(currentExportPath,
             pathSize,
             cProjectExportPathFormat,
             currentProjectPatch,
             currentProjectName);

    if (!fileSystem->exists(currentExportPath))
    {
        fileSystem->mkdir(0, currentExportPath);
    }

    snprintf(currentExportPath,
             pathSize,
             cProjectSamplesExportPathFormat,
             currentProjectPatch,
             currentProjectName);

    if (!fileSystem->exists(currentExportPath))
    {
        fileSystem->mkdir(0, currentExportPath);
    }

    fileSystem->removeOnlyDirFiles(currentExportPath);

    currentInstrument = 0;
    exportStep = 0;

    status = fmExportingProjectSamples;
    currentOperationStep = 0;
    currentOperation = fmExportProjectSamples;

    totalMemoryToTranfser = mtProject.used_memory;

    return true;
}

void cFileManager::exportSamplesStart()
{
    if (currentInstrument >= kSampleInstrumentsCount)
    {
        moveToNextOperationStep();
        return;
    }
    if (mtProject.instrument[currentInstrument].isActive == 0)
    {
        currentInstrument++;
        return;
    }

    if (exportStep == 0)
    {
        uint8_t sampleNum = 0;
        char sampleNumText[4] = "";
        do
        {
            sampleNum++;

            if (sampleNum > 1)
            {
                sprintf(sampleNumText, "%d", sampleNum);
            }

            volatile int pathSize = sizeof(currentCopyDestPath);
            snprintf(currentCopyDestPath,
                     pathSize,
                     cProjectSamplesExportFilesFormat,
                     currentProjectPatch,
                     currentProjectName,
                     mtProject.instrument[currentInstrument].sample.file_name,
                     sampleNumText);

        } while (fileSystem->exists(currentCopyDestPath) && sampleNum < 48);

        exportStep = 1;
    }

    bool stereo = isInstrumentStereo(currentInstrument);
    int16_t* dataSourceChannelL =
      mtProject.sampleMemory[currentInstrument].address[strSampleMemory::Channel::kLeft];
    int16_t* dataSourceChannelR =
      (stereo ? mtProject.sampleMemory[currentInstrument].address[strSampleMemory::Channel::kRight]
              : mtProject.sampleMemory[currentInstrument].address[strSampleMemory::Channel::kLeft]);
    uint32_t dataLength = mtProject.instrument[currentInstrument].sample.length;

    if (mtProject.sampleMemory[currentInstrument].isStreamed)
    {
        if (fileSystem->exists(currentCopyDestPath))
        {
            fileSystem->remove(currentCopyDestPath);
        }

        auto [isOpen, fileHandle] =
          fileSystem->openFile(currentCopyDestPath, FileSystem::FileOpenMode::kWriteCreateNew);
        if (!isOpen)
        {
            exportStep = 0;
            stopOperationWithError(fmExportSoundError);
            return;
        }

        uint32_t channels = stereo ? 2 : 1;
        uint32_t dataSize = dataLength * channels * sizeof(int16_t);
        strWavFileHeader header;
        header.chunkId = 0x46464952; // "RIFF"
        header.chunkSize = dataSize + 36;
        header.waveId = WAVEID;          // "WAVE"
        header.subchunk1Id = 0x20746d66; // "fmt "
        header.subchunk1Size = 16;
        header.AudioFormat = 1;
        header.numChannels = channels;
        header.sampleRate = 44100;
        header.bitsPerSample = 16;
        header.byteRate = header.sampleRate * header.numChannels * (header.bitsPerSample / 8);
        header.blockAlign = header.numChannels * (header.bitsPerSample / 8);
        header.subchunk2Id = 0x61746164; // "data"
        header.subchunk2Size = dataSize;

        fileSystem->writeFile(&header, sizeof(header), fileHandle);

        static constexpr uint32_t kChunkFrames = 512;
        int16_t bufL[kChunkFrames];
        int16_t bufR[kChunkFrames];
        int16_t interleaved[kChunkFrames * 2];

        for (uint32_t offset = 0; offset < dataLength; offset += kChunkFrames)
        {
            uint32_t count = std::min<uint32_t>(kChunkFrames, dataLength - offset);
            voiceStreamManager.readInstrumentFramesBlocking(
              currentInstrument, 0, offset, count, bufL);
            if (stereo)
            {
                voiceStreamManager.readInstrumentFramesBlocking(
                  currentInstrument, 1, offset, count, bufR);
                for (uint32_t i = 0; i < count; ++i)
                {
                    interleaved[2 * i] = bufL[i];
                    interleaved[2 * i + 1] = bufR[i];
                }
                fileSystem->writeFile(interleaved, count * 4, fileHandle);
            }
            else
            {
                fileSystem->writeFile(bufL, count * 2, fileHandle);
            }
        }

        fileSystem->closeFile(fileHandle);
        currentInstrument++;
        exportStep = 0;
        return;
    }

    uint8_t saveStatus = fileTransfer.saveMemoryToWav(
      dataSourceChannelL, dataSourceChannelR, dataLength, currentCopyDestPath);

    if (saveStatus == fileTransferEnd)
    {
        currentInstrument++;
        exportStep = 0;
    }
    else if (saveStatus >= fileTransferError)
    {
        exportStep = 0;
        stopOperationWithError(fmExportSoundError);
    }
}

uint32_t cFileManager::getTotalSamplesMemoryExported()
{
    uint32_t exported = 0;
    int index = 0;
    for (index = currentInstrument; index > 0; index--)
    {
        // After the last slot, currentInstrument is one past the array until the export ends.
        if (index < kSampleInstrumentsCount && mtProject.instrument[index].isActive)
        {
            break;
        }
    }

    uint8_t* sampleAddress = (uint8_t*)externalRamSampleMemory;
    if (index > 0)
    {
        sampleAddress = (uint8_t*)(mtProject.instrument[index].sample.address);
    }

    exported =
      (sampleAddress - (uint8_t*)externalRamSampleMemory) + fileTransfer.getBytesComplited();

    return exported;
}

void cFileManager::exportProjectSamplesCancel()
{
    fileTransfer.endTransfer();
    exportSoundEnd();
}
