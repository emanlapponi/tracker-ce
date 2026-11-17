#include "mtSequencer.h"
#include "mtStructs.h"

#include "Platform.h"
#include "VoiceStreamManager.h"
#include "fileManager.h"
#include "fileManagerDefs.h"
#include "fileTransfer.h"
#include "instrumentFileValidation.h"
#include "log.h"
#include "mtFileManagerOldVersionRecovery.h"
#include "util_string.h"

#include <algorithm>
#include <cstring>

bool cFileManager::startNewInstrumentSampleLoad(int idx,
                                                char* samplePath,
                                                uint32_t& instrumentFileDataOffset)
{
    assert(idx >= 0 && idx < kSampleInstrumentsCount);

    voiceStreamManager.clearInstrumentMetadata(idx);

    int activeBeforeIdx = idx - 1;

    while (activeBeforeIdx >= 0 && mtProject.instrument[activeBeforeIdx].isActive == 0)
    {
        activeBeforeIdx--;
    }

    if (activeBeforeIdx >= 0)
    {
        assert(mtProject.instrument[activeBeforeIdx].sample.address >= externalRamSampleMemory &&
               mtProject.instrument[activeBeforeIdx].sample.address <
                 externalRamSampleMemory + (SAMPLE_MEMORY_SIZE / 2));

        uint32_t prevRamFrames =
          mtProject.sampleMemory[activeBeforeIdx].isStreamed
            ? mtProject.sampleMemory[activeBeforeIdx].ramFrames
            : (mtProject.sampleMemory[activeBeforeIdx].ramFrames > 0
                 ? mtProject.sampleMemory[activeBeforeIdx].ramFrames
                 : mtProject.instrument[activeBeforeIdx].sample.length);

        if (isInstrumentStereo(activeBeforeIdx))
        {
            // before is stereo sample
            assert(prevRamFrames * 2 < SAMPLE_MEMORY_SIZE / 2);

            mtProject.instrument[idx].sample.address =
              mtProject.instrument[activeBeforeIdx].sample.address + prevRamFrames * 2;
        }
        else
        {
            // before is mono sample
            assert(prevRamFrames < SAMPLE_MEMORY_SIZE / 2);

            mtProject.instrument[idx].sample.address =
              mtProject.instrument[activeBeforeIdx].sample.address + prevRamFrames;
        }
    }
    else
    {
        mtProject.instrument[idx].sample.address = externalRamSampleMemory;
    }

    mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kLeft] =
      mtProject.instrument[idx].sample.address;
    mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kRight] =
      mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kLeft];

    //----------------------------------------------------------------------------

    snprintf(samplePath,
             PATCH_SIZE,
             cWorkspaceInstrumentFileFormatNew,
             idx + 1);

    //----------------------------------------------------------------------------
    // old instruments with length = 0 exception handling
    bool ptiCorrect = true;

    if (fileSystem->exists(samplePath))
    {
        uint32_t instrumentFileSize = fileSystem->getFileSize(samplePath);

        if (instrumentFileSize <= instrumentFileDataOffset ||
            instrumentFileSize <= sizeof(strInstrumentFile))
        {
            char backingPath[PATCH_SIZE];
            uint32_t backingDataOffset = instrumentFileDataOffset;
            if (resolveProjectInstrumentPtiPath(idx, backingPath, sizeof(backingPath)) &&
                getPtiFileDataOffset(backingPath, backingDataOffset))
            {
                uint32_t backingFileSize = fileSystem->getFileSize(backingPath);
                if (backingFileSize > backingDataOffset)
                {
                    strncpy(samplePath, backingPath, PATCH_SIZE);
                    samplePath[PATCH_SIZE - 1] = 0;
                    instrumentFileDataOffset = backingDataOffset;
                    instrumentFileSize = backingFileSize;
                }
            }
        }

        uint32_t instrumentSampleDataSize = instrumentFileSize - instrumentFileDataOffset;

        // check if contains audio data
        if (instrumentFileSize <= instrumentFileDataOffset)
        {
            ptiCorrect = false;
        }
        //----------------------------------------------------------------------------
        // instruments with length != sample_data_size exception handling
        else if (mtProject.instrument[idx].sample.length == 0 ||
                 (mtProject.instrument[idx].sample.length * 2 != instrumentSampleDataSize &&
                  mtProject.instrument[idx].sample.length * 4 != instrumentSampleDataSize))
        {
#ifdef FILEMANAGER_DEBUGLOG
            LOGF("Instrument (%d) has incorrect length: %d vs %d, %s\n",
                 idx,
                 mtProject.instrument[idx].sample.length,
                 instrumentSampleDataSize / 2,
                 samplePath);
#endif
            mtProject.instrument[idx].sample.length =
              (instrumentFileSize - instrumentFileDataOffset) / 2;
            setInstrumentStructChanged(idx);
        }
    }
    else
    {
        ptiCorrect = false;
    }
    //----------------------------------------------------------------------------

    bool isStereo = false; // stereo is only available for new pti instruments

    if (ptiCorrect &&
        isPtiFileValid(
          samplePath, isStereo, instrumentFileDataOffset, mtProject.instrument[idx].sample.length))
    {
        bool stereo = isStereo && stereoSampleMode;
        uint32_t channels = stereo ? 2 : 1;
        uint32_t totalBytes = mtProject.instrument[idx].sample.length * channels * sizeof(int16_t);
        uint32_t freeRam = (mtProject.max_memory > mtProject.used_memory)
                             ? (mtProject.max_memory - mtProject.used_memory)
                             : 0;
        bool shouldStream = (mtProject.instrument[idx].sample.length > kStreamPreCacheFrames) &&
                            isPlayModeStreamable(mtProject.instrument[idx].playMode) &&
                            (mtProject.instrument[idx].isStreamPreferred() || totalBytes > freeRam);
        mtProject.instrument[idx].setStreamPreferred(shouldStream);

        if (shouldStream)
        {
            uint32_t preCache =
              std::min<uint32_t>(mtProject.instrument[idx].sample.length, kStreamPreCacheFrames);
            mtProject.sampleMemory[idx].isStreamed = true;
            mtProject.sampleMemory[idx].isStereo = stereo;
            mtProject.sampleMemory[idx].ramFrames = preCache;
            mtProject.sampleMemory[idx].preCacheFrames = preCache;
            mtProject.sampleMemory[idx].fileDataOffset = instrumentFileDataOffset;

            if (stereo)
            {
                assert(mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kLeft] +
                         preCache <
                       externalRamSampleMemory + SAMPLE_MEMORY_SIZE / 2);

                mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kRight] =
                  mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kLeft] + preCache;
            }
        }
        else
        {
            mtProject.sampleMemory[idx].isStreamed = false;
            mtProject.sampleMemory[idx].isStereo = stereo;
            mtProject.sampleMemory[idx].ramFrames = mtProject.instrument[idx].sample.length;
            mtProject.sampleMemory[idx].preCacheFrames = mtProject.instrument[idx].sample.length;
            mtProject.sampleMemory[idx].fileDataOffset = instrumentFileDataOffset;

            if (stereo)
            {
                assert(mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kLeft] +
                         mtProject.instrument[idx].sample.length <
                       externalRamSampleMemory + SAMPLE_MEMORY_SIZE / 2);

                mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kRight] =
                  mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kLeft] +
                  mtProject.instrument[idx].sample.length;
            }
        }
        return true;
    }

    // if pti file not contains audio data it means there must be a mono .wav sample file in
    // workspace
    snprintf(
      samplePath, WORKSPACE_INSTRUMENTS_FORMAT_MAX_LENGTH, cWorkspaceSamplesFilesFormat, idx + 1);

    if (ptiCorrect == false)
    {
        // read .wav sample size to load
        auto [isOpen, fileHandle] =
          fileSystem->openFile(samplePath, FileSystem::FileOpenMode::kRead);

        if (isOpen)
        {
            strWavFileHeader sampleHead;
            if (!readHeader(&sampleHead, fileHandle) || !checkSampleValid(sampleHead))
            {
                fileSystem->closeFile(fileHandle);
                return false;
            }

            uint32_t sourceMemTotal = fileSystem->getFileAvailable(
              fileHandle); // pobiera wielkosc dopiero po przesciu przez header

            fileSystem->closeFile(fileHandle);

            mtProject.instrument[idx].sample.length = sourceMemTotal > sampleHead.subchunk2Size
                                                        ? sampleHead.subchunk2Size / 2
                                                        : sourceMemTotal / 2;
            mtProject.sampleMemory[idx].isStreamed = false;
            mtProject.sampleMemory[idx].isStereo = false;
            mtProject.sampleMemory[idx].ramFrames = mtProject.instrument[idx].sample.length;
            mtProject.sampleMemory[idx].preCacheFrames = mtProject.instrument[idx].sample.length;
            setInstrumentStructChanged(idx);
        }
    }

    return false;
}

bool cFileManager::loadSampleData(int idx,
                                  uint8_t& sampleLoadStep,
                                  uint32_t instrumentFileDataOffset,
                                  bool loadPtiOnly)
{
    if (idx >= kSampleInstrumentsCount)
    {
        return true; // no more samples to load
    }

    static char samplePath[PATCH_SIZE];
    static uint32_t resolvedDataOffset = 0;
    uint8_t loadStatus = fileTransferIdle;
    uint32_t sampleDataSizeMax = 0;

    if (sampleLoadStep == 0)
    {
        resolvedDataOffset = instrumentFileDataOffset;
        // do this stage only once per sample/instrument
        if (newFileManager.startNewInstrumentSampleLoad(idx, samplePath, resolvedDataOffset))
        {
            sampleLoadStep = 1; // new instrument
        }
        else
        {
            if (loadPtiOnly)
            {
                loadStatus = fileTransferFileNoValid;
            }
            else
            {
                sampleLoadStep = 2; // old instrument/sample
            }
        }

        if (mtProject.sampleMemory[idx].isStreamed)
        {
            sampleDataSizeMax = mtProject.sampleMemory[idx].ramFrames *
                                (mtProject.sampleMemory[idx].isStereo ? 4 : 2);
        }
        else
        {
            sampleDataSizeMax = mtProject.instrument[idx].sample.length *
                                ((newFileManager.isInstrumentStereo(idx) &&
                                  newFileManager.getSampleMode() ==
                                    FileManagerOperation::kLeftAndRight)
                                   ? 4
                                   : 2);
        }

        // memory usage check
        if (sampleDataSizeMax > mtProject.max_memory - mtProject.used_memory)
        {
            sampleLoadStep = 0; // skip loading
            loadStatus = fileTransferFileNoValid;
        }
    }

    uint32_t currentSampleSamplesCount = 0;

    if (sampleLoadStep == 1) // new instrument sample load
    {
        if (mtProject.sampleMemory[idx].isStreamed)
        {
            if (voiceStreamManager.setupInstrumentStream(idx,
                                                         samplePath,
                                                         resolvedDataOffset,
                                                         mtProject.instrument[idx].sample.length,
                                                         mtProject.sampleMemory[idx].isStereo))
            {
                uint32_t ramBytes = mtProject.sampleMemory[idx].ramFrames *
                                    (mtProject.sampleMemory[idx].isStereo ? 4 : 2);
                mtProject.used_memory += ramBytes;
                newFileManager.completeLoadedInstrumentStruct(
                  idx, true, mtProject.instrument[idx].sample.length);
                sampleLoadStep = 0;
                return true;
            }
            else
            {
                loadStatus = fileTransferFileNoValid;
            }
        }
        else
        {
            loadStatus = fileTransfer.loadInstrumentRawSampleDataToMemory(
              samplePath,
              resolvedDataOffset,
              (uint8_t*)mtProject.instrument[idx].sample.address,
              sampleDataSizeMax,
              currentSampleSamplesCount);
        }
    }
    else if (sampleLoadStep == 2) // old instrument/sample load
    {
        loadStatus = fileTransfer.loadSampleToMemory(samplePath,
                                                     mtProject.instrument[idx].sample.address,
                                                     mtProject.instrument[idx].sample.length,
                                                     currentSampleSamplesCount);
    }
    else if (sampleLoadStep == 3) // save old sample type to new instrument in workspace
    {
        bool saveResult = newFileManager.saveSampleDataToWorkspace(idx);
        if (saveResult)
        {
            uint32_t channels = isInstrumentStereo(idx) ? 2 : 1;
            uint32_t totalBytes =
              mtProject.instrument[idx].sample.length * channels * sizeof(int16_t);
            uint32_t freeRam = (mtProject.max_memory > mtProject.used_memory)
                                 ? (mtProject.max_memory - mtProject.used_memory)
                                 : 0;
            if (mtProject.instrument[idx].isStreamPreferred() || totalBytes > freeRam)
            {
                char instrPath[PATCH_SIZE];
                snprintf(instrPath, sizeof(instrPath), cWorkspaceInstrumentFileFormatNew, idx + 1);
                demoteRamInstrumentToStream(idx, instrPath, sizeof(strInstrumentFile), true);
            }

            // after save old sample as new instrument all is done
            sampleLoadStep = 0;
            return true;
        }
    }

    if (loadStatus == fileTransferEnd && currentSampleSamplesCount > 0)
    {
        bool result = false;
        if (sampleLoadStep == 2)
        {
            // after load old type instrument sample
            // save it to new instrument
            sampleLoadStep = 3;
        }
        else
        {
            sampleLoadStep = 0;
            result = true;
        }

        mtProject.used_memory += currentSampleSamplesCount * 2;

        if (newFileManager.isInstrumentStereo(idx))
        {
            // if stereo divide by 2 for length calculation
            currentSampleSamplesCount /= 2;
        }

        mtProject.sampleMemory[idx].ramFrames = currentSampleSamplesCount;
        mtProject.sampleMemory[idx].preCacheFrames = currentSampleSamplesCount;

        newFileManager.completeLoadedInstrumentStruct(idx, true, currentSampleSamplesCount);

        return result;
    }
    else if (loadStatus == fileTransferFileNoExist || loadStatus == fileTransferFileNoValid ||
             loadStatus == fileTransferEnd)
    {
        newFileManager.clearSampleChanged(idx);
        newFileManager.completeLoadedInstrumentStruct(idx, false);
        sampleLoadStep = 0;
        return true;
    }
    else if (loadStatus >= fileTransferError)
    {
        stopOperationWithError(fmLoadError);
    }

    return false;
}

void cFileManager::setInstrumentSampleAddress(int idx, uint16_t channelCount, uint32_t samplesCount)
{
    assert(idx >= 0 && idx < kSampleInstrumentsCount);

    voiceStreamManager.clearInstrumentMetadata(idx);

    // for safety
    mtProject.instrument[idx].sample.length = samplesCount;
    mtProject.sampleMemory[idx].isStreamed = false;
    mtProject.sampleMemory[idx].isStereo = (channelCount == 2);
    mtProject.sampleMemory[idx].ramFrames = samplesCount;
    mtProject.sampleMemory[idx].preCacheFrames = samplesCount;

    int activeBeforeIdx = idx - 1;

    while (activeBeforeIdx >= 0 && mtProject.instrument[activeBeforeIdx].isActive == 0)
    {
        activeBeforeIdx--;
    }

    if (activeBeforeIdx >= 0)
    {
        assert(mtProject.instrument[activeBeforeIdx].sample.address >= externalRamSampleMemory &&
               mtProject.instrument[activeBeforeIdx].sample.address <
                 externalRamSampleMemory + (SAMPLE_MEMORY_SIZE / 2));

        uint32_t prevRamFrames =
          mtProject.sampleMemory[activeBeforeIdx].isStreamed
            ? mtProject.sampleMemory[activeBeforeIdx].ramFrames
            : (mtProject.sampleMemory[activeBeforeIdx].ramFrames > 0
                 ? mtProject.sampleMemory[activeBeforeIdx].ramFrames
                 : mtProject.instrument[activeBeforeIdx].sample.length);

        if (newFileManager.isInstrumentStereo(activeBeforeIdx))
        {
            // before is stereo sample
            assert(prevRamFrames * 2 < SAMPLE_MEMORY_SIZE / 2);

            mtProject.instrument[idx].sample.address =
              mtProject.instrument[activeBeforeIdx].sample.address + prevRamFrames * 2;
        }
        else
        {
            // before is mono sample
            assert(prevRamFrames < SAMPLE_MEMORY_SIZE / 2);

            mtProject.instrument[idx].sample.address =
              mtProject.instrument[activeBeforeIdx].sample.address + prevRamFrames;
        }
    }
    else
    {
        mtProject.instrument[idx].sample.address = externalRamSampleMemory;
    }

    mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kLeft] =
      mtProject.instrument[idx].sample.address;

    if (channelCount == 2)
    {
        assert(mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kLeft] +
                 mtProject.instrument[idx].sample.length <
               externalRamSampleMemory + SAMPLE_MEMORY_SIZE / 2);

        mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kRight] =
          mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kLeft] +
          mtProject.instrument[idx].sample.length;
    }
    else
    {
        mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kRight] =
          mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kLeft];
    }
}

bool cFileManager::promoteStreamedInstrumentToRam(int idx)
{
    if (idx < 0 || idx >= kSampleInstrumentsCount || !mtProject.sampleMemory[idx].isStreamed)
    {
        return true;
    }

    uint32_t fullFrames = mtProject.instrument[idx].sample.length;
    uint32_t curRamFrames = mtProject.sampleMemory[idx].ramFrames;
    bool stereo = isInstrumentStereo(idx);
    uint32_t channels = stereo ? 2 : 1;

    if (fullFrames > curRamFrames)
    {
        uint32_t extraFramesPerChannel = fullFrames - curRamFrames;
        uint32_t extraBytes = extraFramesPerChannel * channels * sizeof(int16_t);

        if (mtProject.used_memory + extraBytes > mtProject.max_memory)
        {
            return false;
        }

        uint8_t firstSlotToMove = calcFirstSlotToMoveInMemory(idx + 1);
        moveSampleMemory(firstSlotToMove, (int32_t)extraBytes);

        if (stereo)
        {
            mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kRight] =
              mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kLeft] + fullFrames;
        }

        // Disable pre-cache shortcut so both channels (including relocated kRight) are read
        // completely from the backing file instead of copying from the new kRight address.
        mtProject.sampleMemory[idx].preCacheFrames = 0;

        voiceStreamManager.readInstrumentFramesBlocking(
          idx,
          0,
          0,
          fullFrames,
          mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kLeft]);

        if (stereo)
        {
            voiceStreamManager.readInstrumentFramesBlocking(
              idx,
              1,
              0,
              fullFrames,
              mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kRight]);
        }
    }

    bool hadPeaks = mtProject.sampleMemory[idx].hasWaveformPeaks;
    voiceStreamManager.clearInstrumentMetadata(idx);
    mtProject.sampleMemory[idx].isStreamed = false;
    mtProject.sampleMemory[idx].isStereo = stereo;
    mtProject.sampleMemory[idx].hasWaveformPeaks = hadPeaks;
    mtProject.sampleMemory[idx].ramFrames = fullFrames;
    mtProject.sampleMemory[idx].preCacheFrames = fullFrames;
    mtProject.instrument[idx].setStreamPreferred(false);
    setInstrumentStructChanged(idx);
    mtProject.used_memory = getSampleMemorySize(0, kSampleInstrumentsCount - 1);
    return true;
}

bool cFileManager::demoteRamInstrumentToStream(int idx,
                                               const char* backingPtiPath,
                                               uint32_t dataOffset,
                                               bool forceStream)
{
    if (idx < 0 || idx >= kSampleInstrumentsCount || !mtProject.instrument[idx].isActive ||
        mtProject.sampleMemory[idx].isStreamed)
    {
        return false;
    }

    uint32_t fullFrames = mtProject.instrument[idx].sample.length;
    bool stereo = isInstrumentStereo(idx);
    uint32_t channels = stereo ? 2 : 1;
    uint32_t totalBytes = fullFrames * channels * sizeof(int16_t);

    bool shouldStream =
      (forceStream ? (fullFrames > kStreamPreCacheFrames) : (totalBytes >= kStreamThresholdBytes)) &&
      isPlayModeStreamable(mtProject.instrument[idx].playMode);
    if (!shouldStream)
    {
        return false;
    }

    uint32_t preCache = std::min<uint32_t>(fullFrames, kStreamPreCacheFrames);
    if (fullFrames <= preCache)
    {
        return false;
    }

    char resolvedPath[PATCH_SIZE];
    resolvedPath[0] = 0;
    if (backingPtiPath != nullptr)
    {
        strncpy(resolvedPath, backingPtiPath, sizeof(resolvedPath));
        resolvedPath[sizeof(resolvedPath) - 1] = 0;
    }
    uint32_t resolvedOffset = dataOffset;
    uint32_t fileSize =
      (resolvedPath[0] != 0 && fileSystem->exists(resolvedPath))
        ? fileSystem->getFileSize(resolvedPath)
        : 0;

    if (fileSize <= resolvedOffset || fileSize <= sizeof(strInstrumentFile))
    {
        char projectPtiPath[PATCH_SIZE];
        uint32_t projectDataOffset = resolvedOffset;
        if (resolveProjectInstrumentPtiPath(idx, projectPtiPath, sizeof(projectPtiPath)) &&
            getPtiFileDataOffset(projectPtiPath, projectDataOffset))
        {
            uint32_t projectFileSize = fileSystem->getFileSize(projectPtiPath);
            if (projectFileSize > projectDataOffset)
            {
                strncpy(resolvedPath, projectPtiPath, sizeof(resolvedPath));
                resolvedPath[sizeof(resolvedPath) - 1] = 0;
                resolvedOffset = projectDataOffset;
                fileSize = projectFileSize;
            }
        }
    }

    if (fileSize < resolvedOffset + totalBytes)
    {
        return false;
    }

    if (stereo)
    {
        mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kRight] =
          mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kLeft] + preCache;
    }

    if (!voiceStreamManager.setupInstrumentStream(
          idx, resolvedPath, resolvedOffset, fullFrames, stereo))
    {
        if (stereo)
        {
            mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kRight] =
              mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kLeft] + fullFrames;
        }
        return false;
    }

    mtProject.instrument[idx].setStreamPreferred(true);
    setInstrumentStructChanged(idx);
    int32_t freedBytes = (int32_t)((fullFrames - preCache) * channels * sizeof(int16_t));
    uint8_t firstSlotToMove = calcFirstSlotToMoveInMemory(idx + 1);
    moveSampleMemory(firstSlotToMove, -freedBytes);
    mtProject.used_memory = getSampleMemorySize(0, kSampleInstrumentsCount - 1);
    return true;
}

//-------------------------------------------------------------------------------------------------
//-----------------------------------------    SAVE   ---------------------------------------------
//-------------------------------------------------------------------------------------------------

// return value: true = success saved, false = fail or skipped
bool cFileManager::saveInstrumentStructToWorkspace(int idx)
{
    if (idx >= kSampleInstrumentsCount || changesFlags.instrument[idx] == 0)
    {
        return false;
    }
    if (mtProject.instrument[idx].isActive == 0) // jesli flaga zmian nie ustawiona omin zapis
    {
        clearInstrumentChanged(idx);
        return false;
    }

    strInstrumentFile fileManagerInstrumentBuffer;
    writeInstrumentToFileStruct(&mtProject.instrument[idx], &fileManagerInstrumentBuffer);

    char instrumentToSave[PATCH_SIZE];
    volatile int pathSize = sizeof(instrumentToSave);
    snprintf(instrumentToSave, pathSize, cWorkspaceInstrumentFileFormatNew, idx + 1);

    uint32_t actualInstrumentFileSize = 0;

    uint8_t saveStatus =
      fileTransfer.saveInstrumentStructToInstrumentFile((uint8_t*)&fileManagerInstrumentBuffer,
                                                        sizeof(strInstrumentFile),
                                                        instrumentToSave,
                                                        &actualInstrumentFileSize);

    if (saveStatus == fileTransferEnd)
    {
        clearInstrumentChanged(idx);
        return true;
    }

    return false;
}

// na potrzeby innych modułów - modów
uint8_t cFileManager::saveInstrument(uint8_t idx)
{
    strInstrumentFile fileManagerInstrumentBuffer;

    writeInstrumentToFileStruct(&mtProject.instrument[idx], &fileManagerInstrumentBuffer);

    char instrumentToSave[PATCH_SIZE];
    volatile int pathSize = sizeof(instrumentToSave);
    snprintf(instrumentToSave,
             pathSize,
             cWorkspaceInstrumentFileFormatNew,
             idx + 1); // nazwy instrumentow od numeru 1

    uint32_t outFileSize = 0;
    uint8_t saveStatus =
      fileTransfer.saveInstrumentStructToInstrumentFile((uint8_t*)&fileManagerInstrumentBuffer,
                                                        sizeof(strInstrumentFile),
                                                        instrumentToSave,
                                                        &outFileSize);

    //	fileTransfer.saveMemoryToFile(
    //			(uint8_t*) &fileManagerInstrumentBuffer,
    //			instrumentToSave,
    //			sizeof(strInstrumentFile));

    if (saveStatus == fileTransferEnd)
    {
        return 1;
    }
    else // if(saveStatus >= fileTransferError)
    {
        return 0;
    }

    return 0;
}

//------------------------------------------------------------------------------------------------------------------
//-----------------------------------------     XXXX
//-----------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------

bool cFileManager::loadInstrumentFormFileStruct(strInstrument* instrument,
                                                strInstrumentFile* instrumentFile,
                                                uint32_t instrumentFileSize,
                                                uint32_t& instrumentFileDataOffsetOut)
{
    instrumentFileDataOffsetOut = 0;

    InstrumentFileValidation::Result validation;
    const uint32_t loadedSize =
      std::min(instrumentFileSize, (uint32_t)sizeof(strInstrumentFile));
    if (!InstrumentFileValidation::validate((const uint8_t*)instrumentFile,
                                            loadedSize,
                                            instrumentFileSize,
                                            sizeof(strInstrument),
                                            INSTRUMENT_FILE_VERSION,
                                            validation))
    {
        return false;
    }

    if (validation.instrumentSize < sizeof(strInstrument))
    {
        memset((uint8_t*)&instrumentFile->instrumentDataAndHeader.instrument +
                 validation.instrumentSize,
               0,
               sizeof(strInstrument) - validation.instrumentSize);
    }

    const uint8_t fileVersion =
      instrumentFile->instrumentDataAndHeader.instrHeader.fileStructureVersion[0];
    if (fileVersion < INSTRUMENT_FILE_VERSION)
    {
        //"older version of instrument file opened"

        versionRecovery.translateinstrument(fileVersion, INSTRUMENT_FILE_VERSION, instrumentFile);
    }

    // The name is copied and printed as a string everywhere, and nothing else makes sure that
    // the 32 bytes read from the file end in a terminator.
    instrumentFile->instrumentDataAndHeader.instrument.sample.file_name[SAMPLE_NAME_SIZE] = 0;

    instrumentFileDataOffsetOut = validation.dataOffset;

    memcpy(instrument, &instrumentFile->instrumentDataAndHeader.instrument, sizeof(strInstrument));

    // The slice table and everything sized after it have MAX_SLICE_NUMBER entries, and the
    // code that walks them takes these two counters as they are.
    if (instrument->sliceNumber > MAX_SLICE_NUMBER)
    {
        instrument->sliceNumber = MAX_SLICE_NUMBER;
    }
    if (instrument->selectedSlice >= instrument->sliceNumber)
    {
        instrument->selectedSlice = (instrument->sliceNumber > 0) ? instrument->sliceNumber - 1 : 0;
    }

    return true;
}

void cFileManager::writeInstrumentToFileStruct(strInstrument* instrument,
                                               strInstrumentFile* instrumentFile)
{

    memcpy(&instrumentFile->instrumentDataAndHeader.instrument, instrument, sizeof(strInstrument));

    instrumentFile->instrumentDataAndHeader.instrHeader.id_file[0] = 'T';
    instrumentFile->instrumentDataAndHeader.instrHeader.id_file[1] = 'I';
    instrumentFile->instrumentDataAndHeader.instrHeader.type = fileTypeInstrument;
    instrumentFile->instrumentDataAndHeader.instrHeader.fwVersion[0] = CORE_VER_1;
    instrumentFile->instrumentDataAndHeader.instrHeader.fwVersion[1] = CORE_VER_2;
    instrumentFile->instrumentDataAndHeader.instrHeader.fwVersion[2] = CORE_VER_3;
    instrumentFile->instrumentDataAndHeader.instrHeader.fwVersion[3] = CORE_VER_1;
    instrumentFile->instrumentDataAndHeader.instrHeader.fileStructureVersion[0] =
      INSTRUMENT_FILE_VERSION;
    instrumentFile->instrumentDataAndHeader.instrHeader.fileStructureVersion[1] =
      INSTRUMENT_FILE_VERSION;
    instrumentFile->instrumentDataAndHeader.instrHeader.fileStructureVersion[2] =
      INSTRUMENT_FILE_VERSION;
    instrumentFile->instrumentDataAndHeader.instrHeader.fileStructureVersion[3] =
      INSTRUMENT_FILE_VERSION;
    instrumentFile->instrumentDataAndHeader.instrHeader.size = sizeof(strInstrument);

    instrumentFile->crc =
      crcCalculator->calculate32bitCrc((uint8_t*)&instrumentFile->instrumentDataAndHeader,
                                       sizeof(instrumentFile->instrumentDataAndHeader));
}

//------------------------------------------------------------------------------------------------------------------
//-----------------------------------------     XXXX
//-----------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------

uint8_t cFileManager::calcFirstSlotToMoveInMemory(uint8_t calcStartSlot)
{
    uint8_t slot;
    // oblicz od ktorego instrumentu trzeba bedzie przesunac pamiec
    // potem zostanie obliczony offset o jaki trzeba bedzie przesunąć
    // jesli 48 to znaczy ze nie ma sampli do przesuniecia
    slot = calcStartSlot;

    while (slot < kSampleInstrumentsCount && mtProject.instrument[slot].isActive == 0)
    {
        slot++;

        if (slot >= kSampleInstrumentsCount)
        {
            break;
        }
    }

    return slot;
}

void cFileManager::getEmptyInstrument(struct strInstrument* source)
{
    memset((uint8_t*)source, 0, sizeof(strInstrument));

    source->sample.wavetable_window_size = 2048;
}

void cFileManager::setDefaultActiveInstrument(struct strInstrument* targetInstrument)
{
    memset((uint8_t*)targetInstrument, 0, sizeof(strInstrument));

    targetInstrument->isActive = 1;

    targetInstrument->startPoint = 0;
    targetInstrument->loopPoint1 = 1;
    targetInstrument->loopPoint2 = MAX_16BIT - 1;
    targetInstrument->endPoint = MAX_16BIT;

    targetInstrument->wavetableCurrentWindow = 0;
    targetInstrument->sample.wavetable_window_size = 2048;
    targetInstrument->playMode = 0;

    targetInstrument->envelope[envAmp].delay = 0;
    targetInstrument->envelope[envAmp].attack = 0;
    targetInstrument->envelope[envAmp].hold = 0;
    targetInstrument->envelope[envAmp].decay = 0;
    targetInstrument->envelope[envAmp].sustain = 1.0;
    targetInstrument->envelope[envAmp].release = 1000;
    targetInstrument->envelope[envAmp].amount = 1.0;
    targetInstrument->envelope[envAmp].loop = 0;
    targetInstrument->envelope[envAmp].enable = 1;

    targetInstrument->envelope[envCutoff].delay = 0;
    targetInstrument->envelope[envCutoff].attack = 3000;
    targetInstrument->envelope[envCutoff].hold = 0;
    targetInstrument->envelope[envCutoff].decay = 0;
    targetInstrument->envelope[envCutoff].sustain = 1.0;
    targetInstrument->envelope[envCutoff].release = 1000;
    targetInstrument->envelope[envCutoff].amount = 1.0;
    targetInstrument->envelope[envCutoff].loop = 0;
    targetInstrument->envelope[envCutoff].enable = 0;

    targetInstrument->envelope[envWtPos].delay = 0;
    targetInstrument->envelope[envWtPos].attack = 3000;
    targetInstrument->envelope[envWtPos].hold = 0;
    targetInstrument->envelope[envWtPos].decay = 0;
    targetInstrument->envelope[envWtPos].sustain = 1.0;
    targetInstrument->envelope[envWtPos].release = 1000;
    targetInstrument->envelope[envWtPos].amount = 1.0;
    targetInstrument->envelope[envWtPos].loop = 0;
    targetInstrument->envelope[envWtPos].enable = 0;

    targetInstrument->envelope[envGranPos].delay = 0;
    targetInstrument->envelope[envGranPos].attack = 3000;
    targetInstrument->envelope[envGranPos].hold = 0;
    targetInstrument->envelope[envGranPos].decay = 0;
    targetInstrument->envelope[envGranPos].sustain = 1.0;
    targetInstrument->envelope[envGranPos].release = 1000;
    targetInstrument->envelope[envGranPos].amount = 1.0;
    targetInstrument->envelope[envGranPos].loop = 0;
    targetInstrument->envelope[envGranPos].enable = 0;

    targetInstrument->envelope[envPan].delay = 0;
    targetInstrument->envelope[envPan].attack = 3000;
    targetInstrument->envelope[envPan].hold = 0;
    targetInstrument->envelope[envPan].decay = 0;
    targetInstrument->envelope[envPan].sustain = 1.0;
    targetInstrument->envelope[envPan].release = 1000;
    targetInstrument->envelope[envPan].amount = 1.0;
    targetInstrument->envelope[envPan].loop = 0;
    targetInstrument->envelope[envPan].enable = 0;

    targetInstrument->envelope[envFinetune].delay = 0;
    targetInstrument->envelope[envFinetune].attack = 3000;
    targetInstrument->envelope[envFinetune].hold = 0;
    targetInstrument->envelope[envFinetune].decay = 0;
    targetInstrument->envelope[envFinetune].sustain = 1.0;
    targetInstrument->envelope[envFinetune].release = 1000;
    targetInstrument->envelope[envFinetune].amount = 1.0;
    targetInstrument->envelope[envFinetune].loop = 0;
    targetInstrument->envelope[envFinetune].enable = 0;

    targetInstrument->cutOff = 1.0;
    targetInstrument->filterEnable = 0;
    targetInstrument->filterType = lowPass;
    targetInstrument->resonance = 0;
    targetInstrument->panning = 50;
    targetInstrument->glide = 0;
    targetInstrument->volume = 50;
    targetInstrument->tune = 0;
    targetInstrument->fineTune = 0;

    targetInstrument->delaySend = 0;

    targetInstrument->overdrive = 0;
    targetInstrument->bitDepth = 16;
    targetInstrument->tuneResample = 0;

    targetInstrument->granular.grainLength = 441;

    for (uint8_t i = 0; i < envMax; i++)
    {
        targetInstrument->lfo[i].amount = 0.5;
        targetInstrument->lfo[i].shape = lfoShapeDefault;
        targetInstrument->lfo[i].speed = 0;
        targetInstrument->lfo[i].wtBend = 0;
        targetInstrument->lfo[i].wtSust = 0;
    }
}

void cFileManager::checkLoopParameters(struct strInstrument* instr)
{
    if (instr->loopPoint1 >= instr->loopPoint2)
        instr->loopPoint1 = instr->loopPoint2 - 1;

    if (((instr->loopPoint1 >= instr->endPoint) && (instr->loopPoint2 >= instr->endPoint)) ||
        ((instr->loopPoint1 <= instr->startPoint) && (instr->loopPoint2 <= instr->startPoint)) ||
        ((instr->loopPoint1 <= instr->startPoint) && (instr->loopPoint2 >= instr->endPoint)))
    {
        instr->loopPoint1 = instr->startPoint + 1;
        instr->loopPoint2 = instr->endPoint - 1;
    }
    else if ((instr->loopPoint1 >= instr->startPoint) && (instr->loopPoint1 <= instr->endPoint) &&
             (instr->loopPoint2 >= instr->endPoint))
    {
        instr->loopPoint2 = instr->endPoint - 1;
    }
    else if ((instr->loopPoint2 >= instr->startPoint) && (instr->loopPoint2 <= instr->endPoint) &&
             (instr->loopPoint1 <= instr->startPoint))
    {
        instr->loopPoint1 = instr->startPoint + 1;
    }
}

void cFileManager::removeWavExtensionFromName(char* fileName)
{
    if (hasFileExtension(fileName, "wav"))
    {
        fileName[strlen(fileName) - 4] = 0;
    }
}

void cFileManager::copySampleNameWithoutExtension(char* sampleName, char* fileName, int maxLength)
{
    strncpy(sampleName, fileName, maxLength);
    sampleName[maxLength] = 0;

    if (hasFileExtension(fileName, "wav"))
    {
        int fileNameLen = strlen(fileName);
        if ((fileNameLen - 4) <= maxLength)
        {
            sampleName[fileNameLen - 4] = 0;
        }
    }
}

bool cFileManager::isFilePtiInstrument(char* file)
{
    return hasFileExtension(file, "pti");
}
