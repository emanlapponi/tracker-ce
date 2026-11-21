#include "SampleImport.h"
#include "AudioDataUtilities.h"
#include "Platform.h"
#include "VoiceStreamManager.h"
#include "fileManager.h"
#include "fileManagerDefs.h"
#include "fileTransfer.h"
#include "mtAudioEngine.h"
#include "util_string.h"

#include "log.h"

#include <algorithm>
#include <cstring>

constexpr uint32_t lofiSampleRate = targetSampleRate / 2;

__NOINIT(EXTERNAL_RAM)
static FileManagerOperation::SampleLoadData sampleImportDataBuff[48];

// The slots of an import are given their place in sample memory, and the instruments behind
// them are moved to make room, before the first file is loaded. A slot that then gets no sample
// is emptied here and the gap is closed, as after a delete. Left open, the gap would not be
// counted as used memory and a later import could be placed past the end of sample memory.
static void releaseSlotsWithoutSample(int firstSlot, int lastSlot)
{
    for (int slot = firstSlot; slot <= lastSlot; slot++)
    {
        uint32_t sampleSize = newFileManager.getSampleMemorySize(slot, slot);

        newFileManager.completeLoadedInstrumentStruct(slot, false);
        engine.clearBlockedInstrument(slot);

        newFileManager.moveSampleMemory(newFileManager.calcFirstSlotToMoveInMemory(slot + 1),
                                        -(int32_t)sampleSize);
    }
}

void SampleImport::update() // fmImportSamplesToWorkspace
{
    SampleImportStep nextStep = operationStep;

    switch (operationStep)
    {
        case SampleImportStep::initStep:
        {
            importSamplesToWorkspaceInit();
            memset(sampleImportDataBuff, 0, sizeof(sampleImportDataBuff));
            loadImportingSamplesData(sampleImportDataBuff,
                                     newFileManager.getExplorerPath(),
                                     newFileManager.getExplorerList());
            nextStep = SampleImportStep::moveMemory;
            break;
        }
        case SampleImportStep::moveMemory:
        {
            uint32_t newSamplesSize =
              newFileManager.getSampleMemorySize(importStartSlot, importEndSlot);

            newFileManager.moveSampleMemory(
              newFileManager.calcFirstSlotToMoveInMemory(importEndSlot + 1),
              newSamplesSize - oldSamplesSize);

            nextStep = SampleImportStep::loadSampleData;
            break;
        }
        case SampleImportStep::loadSampleData:
        {
            bool result =
              mtProject.sampleMemory[currentInstrument].isStreamed
                ? true
                : loadSampleDataToMemeory(currentInstrument,
                                          &sampleImportDataBuff[currentInstrument]);
            if (result)
            {
                memoryToImportDone += fileTransfer.getBytesComplited();
                fileTransfer.clearBytesComplited();
                nextStep = SampleImportStep::saveInstrumentStructToWorkspace;
            }
            break;
        }
        case SampleImportStep::saveInstrumentStructToWorkspace:
        {
            newFileManager.setInstrumentStructChanged(currentInstrument);
            newFileManager.saveInstrumentStructToWorkspace(currentInstrument);
            nextStep = SampleImportStep::saveInstrumentSampleToWorkspace;
            break;
        }
        case SampleImportStep::saveInstrumentSampleToWorkspace:
        {
            bool result =
              mtProject.sampleMemory[currentInstrument].isStreamed
                ? saveStreamedImportSampleToWorkspace(currentInstrument,
                                                      &sampleImportDataBuff[currentInstrument])
                : newFileManager.saveSampleDataToWorkspace(currentInstrument);
            if (result)
            {
                engine.clearBlockedInstrument(currentInstrument);
                if (currentInstrument < importEndSlot &&
                    currentInstrument < kSampleInstrumentsCount - 1)
                {
                    currentInstrument++;
                    nextStep = SampleImportStep::loadSampleData;
                }
                else
                {
                    nextStep = SampleImportStep::finishStep;
                }
                memoryToImportDone += fileTransfer.getBytesComplited();
                fileTransfer.clearBytesComplited();
            }
            else if (newFileManager.getCurrentOperation() != fmImportSamplesToWorkspace)
            {
                // the save failed and ended the import; this sample is loaded, the rest is not
                engine.clearBlockedInstrument(currentInstrument);
                releaseSlotsWithoutSample(currentInstrument + 1, importEndSlot);
            }
            break;
        }
        case SampleImportStep::finishStep:
        {
            importSamplesToWorkspaceFinish();
            break;
        }
        default: stopOperationWithError(fmImportSamplesError); break;
    }

    operationStep = nextStep;
}

float SampleImport::getProgress()
{
    float progress = 0;
    float complited = memoryToImportDone;

    switch (operationStep)
    {
        case initStep:
        {
            progress = 0.01;
            break;
        }
        case moveMemory:
        {
            progress = 0.05;
            break;
        }
        case saveInstrumentStructToWorkspace:
        case saveInstrumentSampleToWorkspace:
        case loadSampleData:
        {
            complited += fileTransfer.getBytesComplited();
            float memoryProgress =
              memoryToImportTotal > 0 ? (complited / (float)memoryToImportTotal * 0.9f) : 0;
            progress = 0.1 + memoryProgress;
            break;
        }
        default: break;
    }

    // LOGF("SImport op: %d, pr: %d | Done: %d / %d \n",
    //     operationStep,
    //     (int)(progress * 100),
    //     (int)complited,
    //     memoryToImportTotal);

    return progress < 1.0f ? progress : 1.0f;
}

//=================================================================================================
//=================================================================================================
void SampleImport::importSamplesToWorkspaceInit()
{
    for (int instrSlot = importStartSlot; instrSlot <= importEndSlot; instrSlot++)
    {
        engine.setBlockedInstrument(instrSlot);
    }

    oldSamplesSize = newFileManager.getSampleMemorySize(importStartSlot, importEndSlot);

    mtProject.used_memory -= oldSamplesSize;

    sampleLoadState = 0;

    memoryToImportTotal = 0;
    memoryToImportDone = 0;
}

void SampleImport::importSamplesToWorkspaceFinish()
{
    for (int instrSlot = importStartSlot; instrSlot <= importEndSlot; instrSlot++)
    {
        if (instrSlot >= 0 && instrSlot < kSampleInstrumentsCount &&
            mtProject.instrument[instrSlot].isActive)
        {
            if (sampleImportDataBuff[instrSlot].shouldStream &&
                !mtProject.sampleMemory[instrSlot].isStreamed)
            {
                char instrPath[PATCH_SIZE];
                snprintf(
                  instrPath, sizeof(instrPath), cWorkspaceInstrumentFileFormatNew, instrSlot + 1);
                if (newFileManager.demoteRamInstrumentToStream(
                      instrSlot, instrPath, sizeof(strInstrumentFile), true))
                {
                    newFileManager.saveInstrumentStructToWorkspace(instrSlot);
                }
            }
            else if (!sampleImportDataBuff[instrSlot].shouldStream)
            {
                mtProject.instrument[instrSlot].setStreamPreferred(false);
            }
        }
    }

    mtProject.used_memory = newFileManager.getSampleMemorySize(0, kSampleInstrumentsCount - 1);
    newFileManager.setStatus(fmImportSamplesEnd);
    newFileManager.setCurrentOperation(fmNoOperation);
}

//=================================================================================================
//=================================================================================================
bool SampleImport::importSamplesToProject(uint8_t fileFrom,
                                          uint8_t fileTo,
                                          uint8_t instrumentSlot,
                                          uint16_t dataFrom,
                                          uint16_t dataTo,
                                          bool isLofi,
                                          SampleChannelMode sampleMode,
                                          SampleLoadMode loadMode)
{
    if (newFileManager.getStatus() != fmIdle)
        return false;
    if (newFileManager.getCurrentOperation() != fmNoOperation)
        return false;
    if (dataFrom >= dataTo || dataFrom > std::numeric_limits<uint16_t>::max() ||
        dataTo > std::numeric_limits<uint16_t>::max())
        return false;
    if (instrumentSlot >= kSampleInstrumentsCount)
        return false;

    // the files that have no instrument slot left are not imported
    const int slotsAfterFirst = kSampleInstrumentsCount - 1 - instrumentSlot;
    if (fileTo - fileFrom > slotsAfterFirst)
    {
        fileTo = fileFrom + slotsAfterFirst;
    }

    sampleChannelMode = sampleMode;
    importLoadMode = loadMode;
    importCurrentFileIdx = fileFrom;
    importDataFrom = dataFrom;
    importDataTo = dataTo;
    importIsLofi = isLofi;

    importExplorerSlotFrom = fileFrom;
    importExplorerSlotTo = fileTo;

    currentInstrument = instrumentSlot;
    importStartSlot = instrumentSlot;
    importEndSlot = instrumentSlot + ((fileTo > fileFrom) ? (fileTo - fileFrom) : 0);
    if (importEndSlot >= kSampleInstrumentsCount)
    {
        importEndSlot = kSampleInstrumentsCount - 1;
    }

    // sprawdzenie czy wystarczy pamieci xxx

    newFileManager.setStatus(fmImportingSamplesToWorkspace);
    operationStep = SampleImportStep::initStep;
    newFileManager.setCurrentOperation(fmImportSamplesToWorkspace);
    return true;
}

static bool importedSampleFits(int slot, int lastSlot, uint16_t channelCount, uint32_t framesInRam);

// importowanie dowolnego wavaprzez podanie jego sciezki na dowolny slot instrumentu
bool SampleImport::importSampleToProject(char* filePath,
                                         char* fileName,
                                         uint8_t instrumentSlot,
                                         SampleChannelMode sampleMode)
{
    if (newFileManager.getStatus() != fmIdle)
        return false;
    if (newFileManager.getCurrentOperation() != fmNoOperation)
        return false;

    // sprawdz czy plik istnieje
    char test_path[PATCH_SIZE];
    snprintf(test_path, sizeof(test_path), "%s/%s.wav", filePath, fileName);
    if (!fileSystem->exists(test_path))
        return false;

    if (instrumentSlot >= kSampleInstrumentsCount)
    {
        return false;
    }

    strWavFileHeader sampleHead{};
    uint32_t fileWaveDataSize = newFileManager.getWaveSizeAndHederIfValid(test_path, sampleHead);
    if (fileWaveDataSize == 0 || sampleHead.bitsPerSample < 8 || sampleHead.numChannels == 0 ||
        sampleHead.sampleRate == 0)
    {
        return false;
    }

    uint32_t sourceAllChannelBytesPerSample =
      (sampleHead.bitsPerSample / 8) * sampleHead.numChannels;
    if (sourceAllChannelBytesPerSample == 0)
    {
        return false;
    }

    uint32_t sourceSizeBytes = (fileWaveDataSize <= sampleHead.subchunk2Size)
                                 ? fileWaveDataSize
                                 : sampleHead.subchunk2Size;
    uint32_t sourceTotalFrames = sourceSizeBytes / sourceAllChannelBytesPerSample;
    uint64_t framesInRam = ((uint64_t)sourceTotalFrames * targetSampleRate) / sampleHead.sampleRate;
    if (framesInRam == 0 || framesInRam > EDIT_BUFF_SIZE / 2)
    {
        return false;
    }

    uint16_t destChannels =
      ((sampleMode == FileManagerOperation::kLeftAndRight) && (sampleHead.numChannels > 1)) ? 2 : 1;
    if (!importedSampleFits(instrumentSlot, instrumentSlot, destChannels, (uint32_t)framesInRam))
    {
        return false;
    }

    // explorerCurrentPath, explorerList[importCurrentFile]
    // kozrysta z listy plikow explorera
    // ustawia plik do importu na 0 miejscu tablicy explorerList[]
    // i uruchamia import jak w sample laderze
    if (newFileManager.getExplorerList()[0] != nullptr)
    {
        delete[] newFileManager.getExplorerList()[0];
        newFileManager.getExplorerList()[0] = nullptr;
    }

    uint16_t len = strlen(fileName) + 5; // + .wav + \0
    newFileManager.getExplorerList()[0] = new char[len];
    snprintf(newFileManager.getExplorerList()[0], len, "%s.wav", fileName);

    // wymagane tez ustawienie aktualnego path explorera
    // na lokalizajce wczytywanego pliku
    polystrncpy(newFileManager.getExplorerPath(), filePath, PATCH_SIZE);

    // liczy sie tlyko 0 index w tablicy
    importCurrentFileIdx = 0;

    // default parametry importu
    sampleChannelMode = sampleMode;
    importLoadMode = SampleLoadMode::kRam;
    importDataFrom = 0;
    importDataTo = std::numeric_limits<uint16_t>::max();
    importIsLofi = false;

    importExplorerSlotTo = 0;
    importExplorerSlotFrom = 0;

    currentInstrument = instrumentSlot;
    importStartSlot = instrumentSlot;
    importEndSlot = instrumentSlot;

    // reset browsera przy nastepnym uzywaniu
    newFileManager.resetBrowse();

    newFileManager.setStatus(fmImportingSamplesToWorkspace);
    operationStep = SampleImportStep::initStep;
    newFileManager.setCurrentOperation(fmImportSamplesToWorkspace);
    return true;
}

//=================================================================================================
//=================================================================================================
// The estimate that lets an import start can be too low, so the room is checked again where
// each file is given its place in sample memory. The files of one import are placed one behind
// the other, and the instruments behind the imported slots move up by the same amount: a file
// has to fit between the instruments in front of it and those that follow the imported slots.
static bool importedSampleFits(int slot, int lastSlot, uint16_t channelCount, uint32_t framesInRam)
{
    uint64_t usedBytes = newFileManager.getSampleMemorySize(0, slot - 1);
    usedBytes += newFileManager.getSampleMemorySize(lastSlot + 1, kSampleInstrumentsCount - 1);

    uint64_t sampleBytes = (uint64_t)framesInRam * channelCount * sizeof(int16_t);

    return usedBytes + sampleBytes <= mtProject.max_memory;
}

//=================================================================================================
//=================================================================================================
void SampleImport::loadImportingSamplesData(FileManagerOperation::SampleLoadData* sampleImportData,
                                            char* sourceFilePath,
                                            char** sourceFileNameList)
{
    char srcSamplePath[PATCH_SIZE];
    strWavFileHeader sampleHead;
    uint32_t availableForStaging = (mtProject.max_memory > mtProject.used_memory)
                                     ? (mtProject.max_memory - mtProject.used_memory)
                                     : 0;

    for (int idx = 0; idx < (importExplorerSlotTo - importExplorerSlotFrom + 1); idx++)
    {
        int instrIdx = importStartSlot + idx;
        int fileIdx = importExplorerSlotFrom + idx;

        sampleImportData[instrIdx].filePath = sourceFilePath;
        sampleImportData[instrIdx].fileName = sourceFileNameList[fileIdx];

        snprintf(srcSamplePath,
                 sizeof(srcSamplePath),
                 "%s/%s",
                 sampleImportData[instrIdx].filePath,
                 sampleImportData[instrIdx].fileName);

        if (newFileManager.isFilePtiInstrument(srcSamplePath))
        {
            sampleImportData[instrIdx].ptiFile = true;

            strInstrumentFile instrumentFileBuff;
            uint32_t instrumentFileSize = fileSystem->getFileSize(srcSamplePath);

            uint8_t loadStatus = fileTransfer.loadFileToMemory(srcSamplePath,
                                                               (uint8_t*)&instrumentFileBuff,
                                                               sizeof(strInstrumentFile),
                                                               fileWholeOnce);
            if (loadStatus == fileTransferEnd &&
                newFileManager.loadInstrumentFormFileStruct(&mtProject.instrument[instrIdx],
                                                            &instrumentFileBuff,
                                                            instrumentFileSize,
                                                            sampleImportData[instrIdx].fileOffset))
            {
                newFileManager.removeWavExtensionFromName(
                  mtProject.instrument[instrIdx].sample.file_name);
                mtProject.instrument[instrIdx].isActive = 1;

                //-------------------------------------------------------------------------------------------
                bool ptiCorrect = true;
                uint32_t instrumentSampleDataSize =
                  instrumentFileSize - sampleImportData[instrIdx].fileOffset;

                // check if contains audio data
                if (instrumentFileSize <= sampleImportData[instrIdx].fileOffset)
                {
                    ptiCorrect = false;
                }
                // instruments with length != sample_data_size exception handling
                else if (mtProject.instrument[instrIdx].sample.length == 0 ||
                         (mtProject.instrument[instrIdx].sample.length * 2 !=
                            instrumentSampleDataSize &&
                          mtProject.instrument[instrIdx].sample.length * 4 !=
                            instrumentSampleDataSize))
                {
#ifdef FILEMANAGER_DEBUGLOG
                    LOGF("Instrument (%d) has incorrect length: %d vs %d, %s\n",
                         instrIdx,
                         mtProject.instrument[instrIdx].sample.length,
                         instrumentSampleDataSize / 2,
                         srcSamplePath);
#endif
                    mtProject.instrument[instrIdx].sample.length = instrumentSampleDataSize / 2;
                }

                //-------------------------------------------------------------------------------------------

                if (ptiCorrect)
                {
                    bool isStereo = false;

                    newFileManager.isPtiFileValid(srcSamplePath,
                                                  isStereo,
                                                  sampleImportData[instrIdx].fileOffset,
                                                  mtProject.instrument[instrIdx].sample.length);

                    isStereo = isStereo && newFileManager.getSampleMode() ==
                                             FileManagerOperation::kLeftAndRight;

                    uint32_t fullFrames = mtProject.instrument[instrIdx].sample.length;
                    uint32_t totalBytes = fullFrames * (isStereo ? 4 : 2);
                    bool canStream = (fullFrames > kStreamPreCacheFrames) &&
                                     isPlayModeStreamable(mtProject.instrument[instrIdx].playMode);
                    sampleImportData[instrIdx].shouldStream =
                      canStream && ((importLoadMode == SampleLoadMode::kStream) ||
                                    (importLoadMode == SampleLoadMode::kAuto &&
                                     totalBytes >= kStreamThresholdBytes) ||
                                    mtProject.instrument[instrIdx].isStreamPreferred());
                    mtProject.instrument[instrIdx].setStreamPreferred(
                      sampleImportData[instrIdx].shouldStream);

                    bool onlyPreCache =
                      sampleImportData[instrIdx].shouldStream && totalBytes > availableForStaging;
                    if (!importedSampleFits(
                          instrIdx,
                          importEndSlot,
                          isStereo ? 2 : 1,
                          onlyPreCache ? std::min<uint32_t>(fullFrames, kStreamPreCacheFrames)
                                       : fullFrames))
                    {
                        // Left empty, like a file that cannot be read.
                        newFileManager.completeLoadedInstrumentStruct(instrIdx, false);
                        continue;
                    }

                    if (sampleImportData[instrIdx].shouldStream && totalBytes > availableForStaging)
                    {
                        uint32_t preCache = std::min<uint32_t>(fullFrames, kStreamPreCacheFrames);
                        uint32_t preCacheBytes = preCache * (isStereo ? 4 : 2);
                        availableForStaging = (availableForStaging > preCacheBytes)
                                                ? (availableForStaging - preCacheBytes)
                                                : 0;
                        newFileManager.setInstrumentSampleAddress(
                          instrIdx, isStereo ? 2 : 1, preCache);
                        mtProject.instrument[instrIdx].sample.length = fullFrames;
                        mtProject.sampleMemory[instrIdx].isStreamed = true;
                        mtProject.sampleMemory[instrIdx].isStereo = isStereo;
                        mtProject.sampleMemory[instrIdx].ramFrames = preCache;
                        mtProject.sampleMemory[instrIdx].preCacheFrames = preCache;
                    }
                    else
                    {
                        availableForStaging = (availableForStaging > totalBytes)
                                                ? (availableForStaging - totalBytes)
                                                : 0;
                        newFileManager.setInstrumentSampleAddress(
                          instrIdx, isStereo ? 2 : 1, fullFrames);
                    }

                    memoryToImportTotal += totalBytes;
                    continue;
                }
            }
        }
        else
        {
            sampleImportData[instrIdx].ptiFile = false;

            auto [isOpen, fileHandle] =
              fileSystem->openFile(srcSamplePath, FileSystem::FileOpenMode::kRead);
            if (isOpen)
            {
                if (readHeader(&sampleHead, fileHandle) && checkSampleValid(sampleHead))
                {
                    if (mtProject.instrument[instrIdx].isActive == 0)
                    {
                        newFileManager.setDefaultActiveInstrument(&mtProject.instrument[instrIdx]);
                    }

                    if (importIsLofi)
                    {
                        mtProject.instrument[instrIdx].tuneResample = -12;

                        // trigger resampling in convertAudioData to target sampleRate
                        // by faking source rate in inverse proportions
                        sampleHead.sampleRate =
                          (uint64_t)sampleHead.sampleRate * targetSampleRate / lofiSampleRate;
                        sampleHead.byteRate =
                          (uint64_t)sampleHead.byteRate * targetSampleRate / lofiSampleRate;
                    }

                    uint32_t fileAvailableBytes = fileSystem->getFileAvailable(fileHandle);
                    sampleImportData[instrIdx].fileOffset =
                      fileSystem->getFileSize(fileHandle) - fileAvailableBytes;

                    fillImportStruct(instrIdx,
                                     sampleImportData,
                                     &sampleHead,
                                     fileAvailableBytes,
                                     sampleChannelMode,
                                     importDataFrom,
                                     importDataTo);

                    fileSystem->closeFile(fileHandle);

                    newFileManager.copySampleNameWithoutExtension(
                      mtProject.instrument[instrIdx].sample.file_name,
                      sampleImportData[instrIdx].fileName,
                      SAMPLE_NAME_SIZE);

                    uint32_t fullFrames = sampleImportData[instrIdx].destSampleCountPerChannel;
                    newFileManager.completeLoadedInstrumentStruct(instrIdx, true, fullFrames);

                    uint32_t totalBytes =
                      sampleImportData[instrIdx].destChannelCount * fullFrames * 2;
                    bool canStream = (fullFrames > kStreamPreCacheFrames) &&
                                     isPlayModeStreamable(mtProject.instrument[instrIdx].playMode);
                    sampleImportData[instrIdx].shouldStream =
                      canStream && ((importLoadMode == SampleLoadMode::kStream) ||
                                    (importLoadMode == SampleLoadMode::kAuto &&
                                     totalBytes >= kStreamThresholdBytes));
                    mtProject.instrument[instrIdx].setStreamPreferred(
                      sampleImportData[instrIdx].shouldStream);

                    bool onlyPreCache =
                      sampleImportData[instrIdx].shouldStream && totalBytes > availableForStaging;
                    if (!importedSampleFits(
                          instrIdx,
                          importEndSlot,
                          sampleImportData[instrIdx].destChannelCount,
                          onlyPreCache ? std::min<uint32_t>(fullFrames, kStreamPreCacheFrames)
                                       : fullFrames))
                    {
                        // Left empty, like a file that cannot be read.
                        newFileManager.completeLoadedInstrumentStruct(instrIdx, false);
                        continue;
                    }

                    if (sampleImportData[instrIdx].shouldStream && totalBytes > availableForStaging)
                    {
                        uint32_t preCache = std::min<uint32_t>(fullFrames, kStreamPreCacheFrames);
                        uint32_t preCacheBytes =
                          preCache * sampleImportData[instrIdx].destChannelCount * 2;
                        availableForStaging = (availableForStaging > preCacheBytes)
                                                ? (availableForStaging - preCacheBytes)
                                                : 0;
                        newFileManager.setInstrumentSampleAddress(
                          instrIdx, sampleImportData[instrIdx].destChannelCount, preCache);
                        mtProject.instrument[instrIdx].sample.length = fullFrames;
                        mtProject.sampleMemory[instrIdx].isStreamed = true;
                        mtProject.sampleMemory[instrIdx].isStereo =
                          (sampleImportData[instrIdx].destChannelCount == 2);
                        mtProject.sampleMemory[instrIdx].ramFrames = preCache;
                        mtProject.sampleMemory[instrIdx].preCacheFrames = preCache;

                        uint32_t sourceReadBytes =
                          sampleImportData[instrIdx].sourceTotalSamplesCountPerChannel *
                          sampleImportData[instrIdx].sourceAllChannelBytesPerSample;
                        memoryToImportTotal += sourceReadBytes + totalBytes;
                    }
                    else
                    {
                        availableForStaging = (availableForStaging > totalBytes)
                                                ? (availableForStaging - totalBytes)
                                                : 0;
                        newFileManager.setInstrumentSampleAddress(
                          instrIdx, sampleImportData[instrIdx].destChannelCount, fullFrames);

                        memoryToImportTotal += fileAvailableBytes; // load to memory size (stage 1)
                        memoryToImportTotal += totalBytes;         // save memory to file (stage 2)
                    }

                    continue;
                }

                fileSystem->closeFile(fileHandle);
            }
        }

        newFileManager.completeLoadedInstrumentStruct(instrIdx, false);
    }
}

void SampleImport::fillImportStruct(int idx,
                                    FileManagerOperation::SampleLoadData* sampleImportData,
                                    strWavFileHeader* sampleHead,
                                    uint32_t fileWaveDataSize,
                                    SampleChannelMode channelMode,
                                    uint16_t dataFrom,
                                    uint16_t dataTo)
{
    sampleImportData[idx].sourceFormat = sampleHead->AudioFormat;
    sampleImportData[idx].sourceChannelCount = sampleHead->numChannels;
    sampleImportData[idx].sourceBitsPerSample = sampleHead->bitsPerSample;
    sampleImportData[idx].sourceSampleRate = sampleHead->sampleRate;

    sampleImportData[idx].sourceAllChannelBytesPerSample =
      (sampleHead->bitsPerSample / 8) * sampleHead->numChannels;

    sampleImportData[idx].sourceBytesPerSec =
      sampleHead->sampleRate * sampleImportData[idx].sourceAllChannelBytesPerSample;

    sampleImportData[idx].highQualitySource =
      (sampleImportData[idx].sourceBytesPerSec >= targetSampleRate * 1 * 2);

    uint32_t sourceSizeBytes = (fileWaveDataSize <= sampleHead->subchunk2Size)
                                 ? fileWaveDataSize
                                 : sampleHead->subchunk2Size;

    sampleImportData[idx].sourceTotalSamplesCountPerChannel =
      sourceSizeBytes / sampleImportData[idx].sourceAllChannelBytesPerSample;

    // import sample range implementation
    if ((dataFrom > 0 || dataTo < std::numeric_limits<uint16_t>::max()) && dataTo > dataFrom)
    {
        float start = (float)dataFrom / std::numeric_limits<uint16_t>::max();
        float end = (float)dataTo / std::numeric_limits<uint16_t>::max();

        sampleImportData[idx].fileOffset +=
          ((uint32_t)(sampleImportData[idx].sourceTotalSamplesCountPerChannel * start)) *
          sampleImportData[idx].sourceAllChannelBytesPerSample;

        sampleImportData[idx].sourceTotalSamplesCountPerChannel =
          sampleImportData[idx].sourceTotalSamplesCountPerChannel * (end - start);
    }

    // conversion is always set if sample is not mono because even correct rate/format stereo
    // also need convert to two different buffers
    // TODO: optimize later conversion if sample is stereo but has correct other parameters to
    // only split between two buffers
    sampleImportData[idx].conversion =
      sampleHead->bitsPerSample != 16 || sampleHead->numChannels != 1 ||
      sampleHead->AudioFormat != 1 || sampleHead->sampleRate != targetSampleRate;

    uint64_t samplesCount = (uint64_t)sampleImportData[idx].sourceTotalSamplesCountPerChannel *
                            targetSampleRate / sampleHead->sampleRate;

    sampleImportData[idx].destSampleCountPerChannel = (uint32_t)samplesCount;

    sampleImportData[idx].destChannelCount =
      ((channelMode == kLeftAndRight) && (sampleHead->numChannels > 1)) ? 2 : 1;
}

bool SampleImport::loadSampleDataToMemeory(int idx,
                                           FileManagerOperation::SampleLoadData* sampleImportData)
{
    if (mtProject.instrument[idx].isActive != 1)
    {
        return true;
    }

    char srcSamplePath[PATCH_SIZE];
    snprintf(srcSamplePath,
             sizeof(srcSamplePath),
             "%s/%s",
             sampleImportData->filePath,
             sampleImportData->fileName);

    uint8_t loadStatus = fileTransferIdle;
    uint32_t currentSampleSamplesCount = 0;

    if (sampleImportData->ptiFile)
    {
        uint32_t offsetFromStruct =
          sampleImportData->fileOffset; // + (sizeof(strInstrumentFile) - sizeof(strInstrument));
        uint32_t sizeLimit =
          mtProject.instrument[idx].sample.length *
          ((newFileManager.isInstrumentStereo(idx) &&
            newFileManager.getSampleMode() == FileManagerOperation::kLeftAndRight)
             ? 4
             : 2);

        loadStatus = fileTransfer.loadInstrumentRawSampleDataToMemory(
          srcSamplePath,
          offsetFromStruct,
          (uint8_t*)mtProject.instrument[idx].sample.address,
          sizeLimit,
          currentSampleSamplesCount);
    }
    else
    {
        loadStatus = fileTransfer.loadSampleDataToMemory(
          sampleImportData, mtProject.instrument[idx].sample.address, targetSampleRate);
    }

    if (loadStatus == fileTransferEnd)
    {
        if (sampleImportData->ptiFile)
        {
            mtProject.used_memory += currentSampleSamplesCount * 2;
        }
        else
        {
            mtProject.used_memory += (sampleImportData->destSampleCountPerChannel * 2) *
                                     sampleImportData->destChannelCount;
        }
        return true;
    }
    else if (loadStatus == fileTransferFileNoExist || loadStatus == fileTransferFileNoValid)
    {
        releaseSlotsWithoutSample(idx, idx);

        // closing the gap released the slots behind this one, which are still to be loaded
        for (int slot = idx + 1; slot <= importEndSlot; slot++)
        {
            engine.setBlockedInstrument(slot);
        }
        return true;
    }
    else if (loadStatus >= fileTransferError)
    {
        releaseSlotsWithoutSample(idx, importEndSlot);
        stopOperationWithError(fmImportSamplesError);
    }

    return false;
}

bool SampleImport::saveStreamedImportSampleToWorkspace(
  int idx, FileManagerOperation::SampleLoadData* sampleImportData)
{
    if (mtProject.instrument[idx].isActive != 1)
    {
        return true;
    }

    char srcSamplePath[PATCH_SIZE];
    snprintf(srcSamplePath,
             sizeof(srcSamplePath),
             "%s/%s",
             sampleImportData->filePath,
             sampleImportData->fileName);

    char destPtiPath[PATCH_SIZE];
    snprintf(destPtiPath, sizeof(destPtiPath), cWorkspaceInstrumentFileFormatNew, idx + 1);

    uint8_t transferStatus = fileTransferIdle;
    bool stereo = mtProject.sampleMemory[idx].isStereo;

    if (sampleImportData->ptiFile)
    {
        uint32_t sizeBytes = mtProject.instrument[idx].sample.length * (stereo ? 4 : 2);
        transferStatus = fileTransfer.copyRawDataBetweenFiles(srcSamplePath,
                                                              sampleImportData->fileOffset,
                                                              destPtiPath,
                                                              sizeof(strInstrumentFile),
                                                              sizeBytes);
    }
    else
    {
        transferStatus = fileTransfer.convertWaveToInstrumentFile(
          sampleImportData, destPtiPath, sizeof(strInstrumentFile), targetSampleRate);
    }

    if (transferStatus == fileTransferEnd)
    {
        voiceStreamManager.setupInstrumentStream(idx,
                                                 destPtiPath,
                                                 sizeof(strInstrumentFile),
                                                 mtProject.instrument[idx].sample.length,
                                                 stereo);
        mtProject.used_memory +=
          mtProject.sampleMemory[idx].ramFrames * 2 * (stereo ? 2 : 1);
        return true;
    }
    else if (transferStatus == fileTransferFileNoExist ||
             transferStatus == fileTransferFileNoValid)
    {
        newFileManager.completeLoadedInstrumentStruct(idx, false);
        return true;
    }
    else if (transferStatus >= fileTransferError)
    {
        newFileManager.completeLoadedInstrumentStruct(idx, false);
        stopOperationWithError(fmImportSamplesError);
    }

    return false;
}
