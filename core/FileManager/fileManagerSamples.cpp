#include "fileManagerDefs.h"
#include "mtAudioEngine.h"
#include "mtStructs.h"

#include "AudioDataUtilities.h"
#include "VoiceStreamManager.h"
#include "fileManager.h"
#include "fileTransfer.h"
#include "instrumentFileValidation.h"

#include "elapsedMillis.h"
#include "log.h"

#include "interruptsControl.h"

#include <algorithm>
#include <string.h>

/*
#define READ_WRITE_BUFOR_SIZE 32640
extern uint8_t externalRamFileManagerSrcBuff[READ_WRITE_BUFOR_SIZE];
*/

//-------------------------------------------------------------------------------------------------
//-----------------------------------------   SAVE  -----------------------------------------------
//-------------------------------------------------------------------------------------------------
bool cFileManager::saveSampleDataToWorkspace(int idx)
{
    if (idx < 0 || idx >= kSampleInstrumentsCount)
    {
        return true;
    }
    if (mtProject.instrument[idx].isActive == 0)
    {
        return true;
    }

    sprintf(currentCopyDestPath, cWorkspaceInstrumentFileFormatNew, idx + 1);

    uint8_t* dataSourceAddress = (uint8_t*)mtProject.instrument[idx].sample.address;
    uint32_t dataSizeBytes = 0;

    if (isInstrumentStereo(idx))
    {
        dataSizeBytes = mtProject.instrument[idx].sample.length * 2 * 2;
    }
    else
    {
        dataSizeBytes = mtProject.instrument[idx].sample.length * 2;
    }

    if (mtProject.sampleMemory[idx].isStreamed)
    {
        const char* srcPath = voiceStreamManager.getInstrumentStreamPath(idx);
        if (srcPath && srcPath[0] != 0)
        {
            if (strcmp(srcPath, currentCopyDestPath) == 0 && isWorkspaceInstrumentPtiFileValid(idx))
            {
                return true;
            }

            if (strcmp(srcPath, currentCopyDestPath) != 0 && fileSystem->exists(srcPath))
            {
                if (!fileSystem->exists(currentCopyDestPath) ||
                    fileSystem->getFileSize(currentCopyDestPath) < sizeof(strInstrumentFile))
                {
                    changesFlags.instrument[idx] = 1;
                    saveInstrumentStructToWorkspace(idx);
                }

                auto [srcOpen, srcHandle] =
                  fileSystem->openFile(srcPath, FileSystem::FileOpenMode::kRead);
                if (srcOpen)
                {
                    auto [dstOpen, dstHandle] =
                      fileSystem->openFile(currentCopyDestPath, FileSystem::FileOpenMode::kWriteOpen);
                    if (dstOpen)
                    {
                        fileSystem->seekFile(mtProject.sampleMemory[idx].fileDataOffset, srcHandle);
                        fileSystem->seekFile(sizeof(strInstrumentFile), dstHandle);

                        uint8_t copyBuf[4096];
                        uint32_t remaining = dataSizeBytes;
                        while (remaining > 0)
                        {
                            uint32_t toRead = std::min<uint32_t>(remaining, sizeof(copyBuf));
                            int32_t bytesRead = fileSystem->readFile(copyBuf, toRead, srcHandle);
                            if (bytesRead <= 0)
                            {
                                break;
                            }
                            int32_t bytesWritten =
                              fileSystem->writeFile(copyBuf, bytesRead, dstHandle);
                            if (bytesWritten != bytesRead)
                            {
                                break;
                            }
                            remaining -= static_cast<uint32_t>(bytesRead);
                        }
                        fileSystem->closeFile(dstHandle);
                    }
                    fileSystem->closeFile(srcHandle);

                    if (mtProject.sampleMemory[idx].hasWaveformPeaks &&
                        mtProject.sampleMemory[idx].preCacheFrames > 0)
                    {
                        voiceStreamManager.rebindInstrumentStreamFile(
                          idx, currentCopyDestPath, sizeof(strInstrumentFile));
                    }
                    else
                    {
                        voiceStreamManager.setupInstrumentStream(
                          idx,
                          currentCopyDestPath,
                          sizeof(strInstrumentFile),
                          mtProject.instrument[idx].sample.length,
                          isInstrumentStereo(idx));
                    }
                    return true;
                }
            }
        }
        return true;
    }

    uint8_t saveStatus = fileTransfer.saveSampleRawDataToFile(
      dataSourceAddress, dataSizeBytes, currentCopyDestPath, sizeof(strInstrumentFile));

    if (saveStatus == fileTransferEnd)
    {
        return true;
    }
    else if (saveStatus == fileTransferFileNoValid)
    {
        changesFlags.instrument[idx] = 1;

        // do instrument struct save
        bool result = saveInstrumentStructToWorkspace(idx);
        (void)result;
    }
    else if (saveStatus >= fileTransferError)
    {
        stopOperationWithError(fmSaveError);
    }

    return false;
}

//------------------------------------------------------------------------------------------------------------------
//-----------------------------------------     XXXX
//-----------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------
void cFileManager::moveSampleMemory(int firstSlotToMoveInMemory, int32_t memory_offset)
{
    if (firstSlotToMoveInMemory < 0 ||
        firstSlotToMoveInMemory >= kSampleInstrumentsCount) // nie ma sampli do przesuniecia w pamieci
    {
        return;
    }

    // jak nic sie nie przesuwa to kończ
    if (memory_offset == 0)
        return;

    uint8_t last_instrument_to_move = firstSlotToMoveInMemory;
    uint8_t instr = firstSlotToMoveInMemory;

    // znajdz ostatni instrument do przesuniecia
    // wycisz tez przesuwane instrumenty
    while (instr < kSampleInstrumentsCount)
    {
        if (mtProject.instrument[instr].isActive == 1)
        {
            engine.setBlockedInstrument(instr);
            last_instrument_to_move = instr;
        }

        instr++;
    }

#ifdef FILEMANAGER_DEBUGLOG
    LOGF("first slot: %d adress: %d \n",
         firstSlotToMoveInMemory,
         (uint8_t*)mtProject.instrument[firstSlotToMoveInMemory].sample.address);
    LOGF("last slot: %d adress: %d length: %d \n",
         last_instrument_to_move,
         (uint8_t*)mtProject.instrument[last_instrument_to_move].sample.address,
         mtProject.instrument[last_instrument_to_move].sample.length * 2);
#endif

    // wyznacz blok pamieci do przesuniecia
    uint8_t* begining_address =
      (uint8_t*)mtProject.instrument[firstSlotToMoveInMemory].sample.address;

    uint32_t lastRamFrames =
      mtProject.sampleMemory[last_instrument_to_move].isStreamed
        ? mtProject.sampleMemory[last_instrument_to_move].ramFrames
        : (mtProject.sampleMemory[last_instrument_to_move].ramFrames > 0
             ? mtProject.sampleMemory[last_instrument_to_move].ramFrames
             : mtProject.instrument[last_instrument_to_move].sample.length);

    uint8_t* end_address = (uint8_t*)mtProject.instrument[last_instrument_to_move].sample.address +
                           lastRamFrames * 2 *
                             (isInstrumentStereo(last_instrument_to_move) ? 2 : 1);

#ifdef FILEMANAGER_DEBUGLOG
    LOGF("Move memory: %d ", memory_offset);
    LOGF("begin: %d ", begining_address);
    LOGF("end: %d \n", end_address);
#endif

    // przesuń sample w pamięci
    moveMemory(begining_address, end_address, memory_offset);

    // zmien adresy przesunietch instrumentów
    // odcisz przesuniete instrumenty
    for (uint8_t idx = firstSlotToMoveInMemory; idx <= last_instrument_to_move; idx++)
    {
        if (mtProject.instrument[idx].isActive)
        {
            bool stereo = isInstrumentStereo(idx);
            uint32_t ramFrames =
              mtProject.sampleMemory[idx].isStreamed
                ? mtProject.sampleMemory[idx].ramFrames
                : (mtProject.sampleMemory[idx].ramFrames > 0
                     ? mtProject.sampleMemory[idx].ramFrames
                     : mtProject.instrument[idx].sample.length);

            mtProject.instrument[idx].sample.address =
              (int16_t*)(((uint8_t*)mtProject.instrument[idx].sample.address) + memory_offset);

            mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kLeft] =
              mtProject.instrument[idx].sample.address;

            if (stereo)
            {
                assert(mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kLeft] +
                         ramFrames <
                       externalRamSampleMemory + SAMPLE_MEMORY_SIZE / 2);

                mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kRight] =
                  mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kLeft] + ramFrames;
            }
            else
            {
                mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kRight] =
                  mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kLeft];
            }
        }

        engine.clearBlockedInstrument(idx);
    }
}

void cFileManager::moveMemory(uint8_t* memoryStart, uint8_t* memoryEnd, int32_t memoryOffset)
{
    assert(memoryEnd > memoryStart);

#ifdef FILEMANAGER_DEBUGLOG
    elapsedMillis startTime = 0;
#endif

    // DISABLE_AUDIO_INTERRUPTS();
    memmove(memoryStart + memoryOffset, memoryStart, memoryEnd - memoryStart);
    // ENABLE_AUDIO_INTERRUPTS();
/*
    volatile int32_t memory_size = memoryEnd - memoryStart;
    int32_t stepSize = READ_WRITE_BUFOR_SIZE;

    if (memoryOffset > 0)
    {
        if (stepSize > memoryOffset)
            stepSize = memoryOffset;

        int stepCount = memory_size / stepSize;

        // przesuń do góry
        do
        {
#ifdef FILEMANAGER_DEBUGLOG
            LOGF("Moving mem up: %d / %d\n", stepCount - (memory_size / stepSize), stepCount);
#endif

            if (memory_size >= stepSize)
            {
                memcpy(
                  externalRamFileManagerSrcBuff, (memoryStart + memory_size) - stepSize, stepSize);
                memcpy((memoryStart + memory_size + memoryOffset) - stepSize,
                       externalRamFileManagerSrcBuff,
                       stepSize);
                memory_size -= stepSize;
            }
            else
            {
                stepSize = memory_size;
                memcpy(
                  externalRamFileManagerSrcBuff, (memoryStart + memory_size) - stepSize, stepSize);
                memcpy((memoryStart + memoryOffset + memory_size) - stepSize,
                       externalRamFileManagerSrcBuff,
                       stepSize);
                memory_size -= stepSize;
            }
        } while (memory_size > 0);
    }
    else if (memoryOffset < 0)
    {
        if (stepSize > -memoryOffset)
            stepSize = -memoryOffset;

        int stepCount = memory_size / stepSize;
        int32_t total_moved = 0;
        // przesuń w dól
        do
        {
#ifdef FILEMANAGER_DEBUGLOG
            LOGF("Moving mem down: %d / %d\n", total_moved / stepSize, stepCount);
#endif

            if (memory_size - total_moved >= stepSize)
            {
                memcpy(externalRamFileManagerSrcBuff, memoryStart + total_moved, stepSize);
                memcpy(memoryStart + memoryOffset + total_moved,
                       externalRamFileManagerSrcBuff,
                       stepSize);
                total_moved += stepSize;
            }
            else
            {
                stepSize = memory_size - total_moved;
                memcpy(externalRamFileManagerSrcBuff, memoryStart + total_moved, stepSize);
                memcpy(memoryStart + memoryOffset + total_moved,
                       externalRamFileManagerSrcBuff,
                       stepSize);
                total_moved += stepSize;
            }
        } while (total_moved < memory_size);
    }
*/
#ifdef FILEMANAGER_DEBUGLOG
    LOGF("Moving mem: %.3f bytes, time: %.2f \n",
         ((float)(memoryEnd - memoryStart)) / (1024 * 1024),
         (float)startTime / 1000.0);
#endif
}

//------------------------------------------------------------------------------------------------------------------
//-----------------------------------------     XXXX
//-----------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------
void cFileManager::completeLoadedInstrumentStruct(int idx, bool success, uint32_t samplesCount)
{
    if (success == false || samplesCount == 0)
    {
        voiceStreamManager.clearInstrumentMetadata(idx);
        memset(mtProject.instrument[idx].sample.file_name, 0, SAMPLE_NAME_SIZE);
        mtProject.instrument[idx].isActive = 0;
        mtProject.instrument[idx].sample.length = 0;
        mtProject.instrument[idx].sample.wavetable_window_size = 0;

        return;
    }

    mtProject.instrument[idx].isActive = 1;
    mtProject.instrument[idx].sample.length = samplesCount;

    if (!mtProject.sampleMemory[idx].isStreamed && mtProject.sampleMemory[idx].ramFrames == 0)
    {
        mtProject.sampleMemory[idx].ramFrames = samplesCount;
        mtProject.sampleMemory[idx].preCacheFrames = samplesCount;
    }

    if (mtProject.instrument[idx].granular.grainLength > mtProject.instrument[idx].sample.length)
    {
        mtProject.instrument[idx].granular.grainLength = mtProject.instrument[idx].sample.length;
    }

    if (mtProject.instrument[idx].playMode == playModeWavetable)
    {
        mtProject.instrument[idx].sample.type = mtSampleTypeWavetable;
        uint32_t lastCurrentWindow = mtProject.instrument[idx].wavetableCurrentWindow;
        uint32_t lastWindowNumber = mtProject.instrument[idx].sample.wavetableWindowNumber;
        mtProject.instrument[idx].sample.wavetable_window_size =
          (mtProject.instrument[idx].sample.length >=
           mtProject.instrument[idx].sample.wavetable_window_size)
            ? mtProject.instrument[idx].sample.wavetable_window_size
            : mtProject.instrument[idx].sample.length;

        //*******************************wavetable window size moze byc tylko potęgą 2
        // Jezeli length nie jest potega 2 trzeba go zrownac do najwiekszej mozliwej potegi 2
        uint16_t localMask = 2048;
        // A size above the largest window, as a damaged or foreign instrument file can
        // hold, may have no bit that the mask reaches, and the loop below would not end.
        if (mtProject.instrument[idx].sample.wavetable_window_size > localMask)
        {
            mtProject.instrument[idx].sample.wavetable_window_size = localMask;
        }
        while (!(mtProject.instrument[idx].sample.wavetable_window_size & localMask))
        {
            if ((mtProject.instrument[idx].sample.wavetable_window_size == 0))
            {
                break;
            }
            localMask >>= 1;
        }

        mtProject.instrument[idx].sample.wavetable_window_size &= localMask;
        //**************************************************************************

        mtProject.instrument[idx].sample.wavetableWindowNumber =
          mtProject.instrument[idx].sample.wavetable_window_size
            ? mtProject.instrument[idx].sample.length /
                mtProject.instrument[idx].sample.wavetable_window_size
            : 0;

        mtProject.instrument[idx].wavetableCurrentWindow =
          mapClamped<int>(lastCurrentWindow,
                          0,
                          lastWindowNumber - 1,
                          0,
                          mtProject.instrument[idx].sample.wavetableWindowNumber - 1);
    }
}

uint32_t cFileManager::getSampleMemorySize(int fromSlot, int toSlot)
{
    if (fromSlot > toSlot)
    {
        return 0;
    }

    uint32_t samplesSize = 0;

    for (int instrSlot = fromSlot; instrSlot <= toSlot; instrSlot++)
    {
        if (mtProject.instrument[instrSlot].isActive == 1)
        {
            uint32_t ramFrames =
              mtProject.sampleMemory[instrSlot].isStreamed
                ? mtProject.sampleMemory[instrSlot].ramFrames
                : (mtProject.sampleMemory[instrSlot].ramFrames > 0
                     ? mtProject.sampleMemory[instrSlot].ramFrames
                     : mtProject.instrument[instrSlot].sample.length);

            if (isInstrumentStereo(instrSlot))
            {
                // stereo sample
                samplesSize += ramFrames * 2 * 2;
            }
            else
            {
                // mono sample
                samplesSize += ramFrames * 2;
            }
        }
    }

    return samplesSize;
}

uint32_t cFileManager::getWaveSizeAndHederIfValid(const char* fileName, strWavFileHeader& header)
{
    header = {};

    uint8_t nameLen = strlen(fileName);
    if (nameLen < 5)
    {
        return 0;
    }

    if (((fileName[nameLen - 1] != 'V') && (fileName[nameLen - 1] != 'v')) ||
        ((fileName[nameLen - 2] != 'A') && (fileName[nameLen - 2] != 'a')) ||
        ((fileName[nameLen - 3] != 'W') && (fileName[nameLen - 3] != 'w')) ||
        (fileName[nameLen - 4] != '.'))
    {
        return 0;
    }

    if (!fileSystem->exists(fileName))
    {
        return 0;
    }

    auto [isOpen, fileHandle] = fileSystem->openFile(fileName, FileSystem::FileOpenMode::kRead);

    if (!isOpen)
    {
        return 0;
    }

    if (!readHeader(&header, fileHandle))
    {
        fileSystem->closeFile(fileHandle);
        return 0;
    }

    uint32_t fileWaveDataSize = fileSystem->getFileAvailable(fileHandle);

    fileSystem->closeFile(fileHandle);

    if (!checkSampleValid(header)) // stereo is valid too
    {
        return 0;
    }

    return fileWaveDataSize;
}

// gives raw size of data part of wave file
uint32_t cFileManager::getWaveSizeIfValid(const char* filename)
{
    strWavFileHeader sampleHeader;

    return getWaveSizeAndHederIfValid(filename, sampleHeader);
}

uint32_t cFileManager::getWaveSizeAfterConversion(const char* filename, bool countStereo)
{
    strWavFileHeader sampleHeader = {};

    uint32_t wavDataSize = getWaveSizeAndHederIfValid(filename, sampleHeader);
    if (wavDataSize == 0 || sampleHeader.sampleRate == 0)
    {
        return 0;
    }

    uint16_t allChannelBytesPerOneSample =
      (sampleHeader.bitsPerSample / 8) * sampleHeader.numChannels;
    if (allChannelBytesPerOneSample == 0)
    {
        return 0;
    }

    uint32_t wavDataSizeBytes =
      (wavDataSize <= sampleHeader.subchunk2Size) ? wavDataSize : sampleHeader.subchunk2Size;

    uint32_t totalSamplesCountPerChannel = wavDataSizeBytes / allChannelBytesPerOneSample;

    uint64_t samplesCount =
      (uint64_t)totalSamplesCountPerChannel * targetSampleRate / sampleHeader.sampleRate;

    return samplesCount * ((countStereo && sampleHeader.numChannels >= 2) ? 4 : 2);
}

uint32_t cFileManager::getInstrumentSampleSizeIfValid(const char* filename)
{
    uint8_t wav_len = strlen(filename);
    if (wav_len < 5)
        return 0;

    if (((filename[wav_len - 1] != 'I') && (filename[wav_len - 1] != 'i')) ||
        ((filename[wav_len - 2] != 'T') && (filename[wav_len - 2] != 't')) ||
        ((filename[wav_len - 3] != 'P') && (filename[wav_len - 3] != 'p')) ||
        (filename[wav_len - 4] != '.'))
        return 0;

    if (!fileSystem->exists(filename))
        return 0;

    auto [isOpen, fileHandle] =
      fileSystem->openFile(filename, FileSystem::FileOpenMode::kRead, &wavfile);
    if (!isOpen)
    {
        return 0;
    }

    uint32_t fileSize = fileSystem->getFileSize(wavfile);
    strInstrumentFile instrumentFile;
    memset(&instrumentFile, 0, sizeof(instrumentFile));
    int32_t bytesRead = fileSystem->readFile(&instrumentFile, sizeof(instrumentFile), wavfile);
    fileSystem->closeFile(wavfile, false);

    if (bytesRead < 0)
        return 0;

    InstrumentFileValidation::Result validation;
    if (!InstrumentFileValidation::validate((const uint8_t*)&instrumentFile,
                                            bytesRead,
                                            fileSize,
                                            sizeof(strInstrument),
                                            INSTRUMENT_FILE_VERSION,
                                            validation))
    {
        return 0;
    }

    return fileSize - validation.dataOffset;
}
