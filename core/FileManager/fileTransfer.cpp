#include "fileManagerDefs.h"
#include "mtStructs.h"

#include "mtSequencer.h"

#include "fileTransfer.h"
#include "instrumentFileValidation.h"
#include <string.h>

extern uint8_t externalRamFileManagerSrcBuff[READ_WRITE_BUFOR_SIZE];
extern uint8_t externalRamFileManagerBuffL[READ_WRITE_BUFOR_SIZE];
extern uint8_t externalRamFileManagerBuffR[READ_WRITE_BUFOR_SIZE];

cFileTransfer fileTransfer;

FileSystem::FileHandle transferFile = nullptr;
FileSystem::FileHandle transferFile2 = nullptr;

strWavFileHeader sampleHead;

//#define READ_WRITE_BUFOR_SIZE 9600
// extern uint8_t g_bufferReadWrite[READ_WRITE_BUFOR_SIZE];

void cFileTransfer::init(FileSystem* fileSystem)
{
    this->fileSystem = fileSystem;
}

void cFileTransfer::clear()
{
    sourceMemComplited = 0;
    transferStep = 0;
    sourceMemTotal = 0;
}

uint8_t cFileTransfer::loadFileToMemory(const char* file,
                                        uint8_t* memory,
                                        uint32_t memSize,
                                        uint8_t mode)
{
    if (transferStep == 0)
    {
        if (!fileSystem->exists(file))
            return fileTransferFileNoExist;

        auto [isOpen, fileHandle] =
          fileSystem->openFile(file, FileSystem::FileOpenMode::kRead, &transferFile);
        if (isOpen)
        {
            memset(memory, 0, memSize);

            uint32_t fileSize = fileSystem->getFileSize(transferFile);
            if (fileSize < memSize)
            {
                memSize = fileSize;
            }
            sourceMemTotal = memSize;
            memStep = (mode == fileWholeOnce)      ? sourceMemTotal
                      : (mode == fileDivIntoParts) ? memoryReadPerPart
                                                   : READ_WRITE_BUFOR_SIZE;
            sourceMemComplited = 0;
            transferStep = 1;
        }
    }

    if (transferStep == 1)
    {
        int32_t result = fileSystem->readFile(memory + sourceMemComplited, memStep, transferFile);

        if (result >= 0)
        {
            sourceMemComplited += result;

            if (result == 0) // koniec pliku
            {
                transferStep = 2;
            }
            else if (sourceMemComplited >=
                     memSize) // wczytany wiekszy plik << niebezpieczne przekroczenie pamieci
            {
                transferStep = 2;
            }
            else
            {
                if (mode != fileWholeOnce)
                {
                    return fileTransferInProgress;
                }
            }
        }
        // else wysypuje error na koncu funkcji \/
    }

    if (transferStep == 2)
    {
        transferStep = 0;
        fileSystem->closeFile(transferFile, false);
        return fileTransferEnd;
    }

    transferStep = 0;
    fileSystem->closeFile(transferFile, false);
    return fileTransferError;
}

uint8_t cFileTransfer::loadFileToMemory(const char* file,
                                        uint8_t* memory,
                                        uint32_t memSize,
                                        uint32_t memOffset,
                                        uint8_t mode)
{
    if (transferStep == 0)
    {
        auto [isOpen, fileHandle] =
          fileSystem->openFile(file, FileSystem::FileOpenMode::kRead, &transferFile);
        if (!isOpen)
            return fileTransferFileNoExist;

        sourceMemTotal = memSize;
        memStep = (mode == fileWholeOnce)      ? sourceMemTotal
                  : (mode == fileDivIntoParts) ? memoryReadPerPart
                                               : READ_WRITE_BUFOR_SIZE;
        sourceMemComplited = 0;
        transferStep = 1;
    }

    if (transferStep == 1)
    {
        bool result = fileSystem->seekFile(memOffset, transferFile);
        if (result)
        {
            transferStep = 2;
        }
        else
        {
            fileSystem->closeFile(transferFile, false);
            transferStep = 0;
            return fileTransferError;
        }
    }

    if (transferStep == 2)
    {
        int32_t result = fileSystem->readFile(memory + sourceMemComplited, memStep, transferFile);

        if (result >= 0)
        {
            sourceMemComplited += result;

            if (result == 0) // koniec pliku
            {
                transferStep = 3;
            }
            else if (sourceMemComplited >=
                     memSize) // wczytany wiekszy plik << niebezpieczne przekroczenie pamieci
            {
                transferStep = 3;
            }
            else
            {
                return fileTransferInProgress;
            }
        }
        // else wysypuje error na koncu funkcji \/
    }

    if (transferStep == 3)
    {
        if (sourceMemComplited < memSize)
        {
            memset(memory + sourceMemComplited, 0, memSize - sourceMemComplited);
        }
        transferStep = 0;
        fileSystem->closeFile(transferFile, false);
        return fileTransferEnd;
    }

    transferStep = 0;
    fileSystem->closeFile(transferFile, false);
    return fileTransferError;
}

// Returns how many of the bytes just read can be stored without passing samplesLeft samples at
// the destination: as they are, or after the conversion to the destination sample rate.
static uint32_t limitToDestination(uint32_t bytesRead,
                                   uint32_t samplesLeft,
                                   const ConversionConfig* conversion)
{
    uint64_t bytesThatFit = (uint64_t)samplesLeft * 2;

    if (conversion != nullptr)
    {
        const uint32_t bytesPerFrame =
          (conversion->sourceBitsPerSample / 8) * conversion->sourceChannelCount;
        const uint64_t framesThatFit =
          (uint64_t)samplesLeft * conversion->sourceSampleRate / conversion->destSampleRate;
        bytesThatFit = framesThatFit * bytesPerFrame;
    }

    return (bytesThatFit < bytesRead) ? (uint32_t)bytesThatFit : bytesRead;
}

uint8_t cFileTransfer::loadSampleToMemory(const char* file,
                                          int16_t* memory,
                                          uint32_t maxSampleCount,
                                          uint32_t& outSampleCount)
{
    if (transferStep == 0)
    {
        if (!fileSystem->exists(file))
        {
            return fileTransferFileNoExist;
        }

        auto [isOpen, fileHandle] =
          fileSystem->openFile(file, FileSystem::FileOpenMode::kRead, &transferFile);

        if (isOpen)
        {
            memStep =
              READ_WRITE_BUFOR_SIZE; // wielokrotnosc 4*6*8 (rozne probkowania stereo w bajtach)
            sourceMemComplited = 0;
            convertedDataSize = 0;

            if (!readHeader(&sampleHead, transferFile) || !checkSampleValid(sampleHead))
            {
                fileSystem->closeFile(transferFile, false);
                transferStep = 0;
                return fileTransferFileNoValid;
            }

            // wylacznie konwersji kiedy format juz sie zgadza
            if (sampleHead.bitsPerSample == 16 && sampleHead.numChannels == 1 &&
                sampleHead.AudioFormat == 1)
            {
                conversionEnabled = 0;
            }
            else
            {
                conversionEnabled = 1;
                conversionConfig.sourceChannelCount = sampleHead.numChannels;
                conversionConfig.sourceBitsPerSample = sampleHead.bitsPerSample;
                conversionConfig.sourceSampleRate = sampleHead.sampleRate;
                conversionConfig.sourceFormat = sampleHead.AudioFormat;
                conversionConfig.destSampleRate = 44100;
            }

            transferStep = 1;

            sourceMemTotal = fileSystem->getFileAvailable(
              fileHandle); // pobiera wielkosc dopiero po przesciu przez header
        }
    }

    if (transferStep == 1)
    {
        int32_t result = fileSystem->readFile(externalRamFileManagerBuffL, memStep, transferFile);

        if (result >= 0)
        {
            // The caller sized the destination from the length of the file, as if it held
            // 16 bit mono samples at 44.1 kHz. A file in a format that grows in the
            // conversion, or with data behind its data chunk, would be written past it.
            const uint32_t samplesLeft =
              (maxSampleCount > convertedDataSize) ? maxSampleCount - convertedDataSize : 0;
            const uint32_t usableBytes = limitToDestination(
              result, samplesLeft, conversionEnabled ? &conversionConfig : nullptr);

            if (conversionEnabled)
            {
                conversionConfig.inputData = externalRamFileManagerBuffL;
                conversionConfig.inputSize = usableBytes;
                conversionConfig.outPtrL = memory + convertedDataSize;
                conversionConfig.outPtrR = nullptr;

                convertedDataSize += convertAudioData(conversionConfig);
            }
            else
            {
                memcpy(memory + convertedDataSize, externalRamFileManagerBuffL, usableBytes);
                convertedDataSize += usableBytes / 2;
            }

            sourceMemComplited += result;
            transferBytesComplited += result;

            // przekroczenie mniejszego zatrzyma ladowanie
            if (sourceMemComplited >= sourceMemTotal ||
                sourceMemComplited >= sampleHead.subchunk2Size)
            {
                transferStep = 2;
            }
            else
            {
                return fileTransferInProgress;
            }
        }
        // else wysypuje error na koncu funkcji \/
    }

    if (transferStep == 2)
    {
        if (conversionEnabled)
        {
        }

        // wybiera wielkosc mniejsza z naglowka lub suma read
        uint32_t sizeFromHead = sampleHead.subchunk2Size / sampleHead.blockAlign;
        outSampleCount = (convertedDataSize < sizeFromHead) ? convertedDataSize : sizeFromHead;

        transferStep = 0;
        fileSystem->closeFile(transferFile, false);

        return fileTransferEnd;
    }

    transferStep = 0;
    fileSystem->closeFile(transferFile, false);
    return fileTransferError;
}

bool cFileTransfer::copyFile(FileSystem* fileSystem,
                             const char* srcFilepath,
                             const char* dstFilepath)
{
    if (!fileSystem->exists(srcFilepath))
    {
        return false;
    }

    if (fileSystem->exists(dstFilepath))
    {
        fileSystem->remove(dstFilepath);
    }

    bool result = false;
    bool destinationOpened = false;
    FileSystem::FileHandle sourceHandle = nullptr;
    FileSystem::FileHandle destinationHandle = nullptr;

    auto [isSourceOpen, openedSourceHandle] =
      fileSystem->openFile(srcFilepath, FileSystem::FileOpenMode::kRead, &transferFile);
    sourceHandle = openedSourceHandle;

    if (isSourceOpen)
    {
        auto [isDestinationOpen, openedDestinationHandle] =
          fileSystem->openFile(dstFilepath, FileSystem::FileOpenMode::kWriteCreateNew, &transferFile2);
        destinationHandle = openedDestinationHandle;
        destinationOpened = isDestinationOpen;

        if (isDestinationOpen)
        {
            const size_t sourceSize = fileSystem->getFileSize(sourceHandle);
            size_t bytesCopied = 0;
            result = true;

            while (bytesCopied < sourceSize)
            {
                const size_t bytesRemaining = sourceSize - bytesCopied;
                const size_t bytesToRead =
                  bytesRemaining < READ_WRITE_BUFOR_SIZE ? bytesRemaining : READ_WRITE_BUFOR_SIZE;
                const int32_t readResult =
                  fileSystem->readFile(externalRamFileManagerBuffL, bytesToRead, sourceHandle);

                if (readResult <= 0 || static_cast<size_t>(readResult) > bytesToRead)
                {
                    result = false;
                    break;
                }

                const size_t bytesRead = static_cast<size_t>(readResult);
                size_t bytesWritten = 0;
                while (bytesWritten < bytesRead)
                {
                    const size_t bytesToWrite = bytesRead - bytesWritten;
                    const int32_t writeResult = fileSystem->writeFile(
                      externalRamFileManagerBuffL + bytesWritten, bytesToWrite, destinationHandle);

                    if (writeResult <= 0 || static_cast<size_t>(writeResult) > bytesToWrite)
                    {
                        result = false;
                        break;
                    }

                    bytesWritten += static_cast<size_t>(writeResult);
                }

                if (!result)
                {
                    break;
                }

                bytesCopied += bytesRead;
            }
        }
    }

    if (sourceHandle != nullptr)
    {
        fileSystem->closeFile(sourceHandle, false);
    }
    if (destinationHandle != nullptr)
    {
        fileSystem->closeFile(destinationHandle, false);
    }
    if (destinationOpened && !result)
    {
        fileSystem->remove(dstFilepath);
    }

    return result;
}

uint8_t cFileTransfer::copyFile(const char* src, const char* dest)
{
    if (transferStep == 0)
    {
        if (!fileSystem->exists(src))
            return fileTransferFileNoExist;
        if (fileSystem->exists(dest))
        {
            fileSystem->remove(dest);
        }

        auto [isSrcOpen, srcFileHandle] =
          fileSystem->openFile(src, FileSystem::FileOpenMode::kRead, &transferFile);

        if (isSrcOpen)
        {
            auto [isDestOpen, destFileHandle] =
              fileSystem->openFile(dest, FileSystem::FileOpenMode::kWriteCreateNew, &transferFile2);

            if (isDestOpen)
            {
                sourceMemTotal = fileSystem->getFileSize(transferFile);

                memStep = READ_WRITE_BUFOR_SIZE; // caly mozliwy bufor
                sourceMemComplited = 0;
                transferStep = 1;
            }
        }
    }

    if (transferStep == 1)
    {
        int32_t read_result =
          fileSystem->readFile(externalRamFileManagerBuffL, memStep, transferFile);

        if (read_result == 0) // koniec pliku - koncz kopiowanie
        {
            transferStep = 2;
        }
        else if (read_result > 0)
        {
            int32_t write_result =
              fileSystem->writeFile(externalRamFileManagerBuffL, read_result, transferFile2);

            if (write_result >= 0)
            {
                sourceMemComplited += write_result;
                transferBytesComplited += write_result;

                if (sourceMemComplited >= sourceMemTotal) // koniec pliku
                {
                    transferStep = 2;
                }
                else if (read_result == write_result)
                {
                    return fileTransferInProgress;
                }
            }
        }
    }

    if (transferStep == 2)
    {
        uint32_t fileNameLen = strlen(src);
        bool invalidInstrument = false;

        if (fileNameLen >= 4 &&
            ((src[fileNameLen - 1] == 'i') || (src[fileNameLen - 1] == 'I')) &&
            ((src[fileNameLen - 2] == 't') || (src[fileNameLen - 2] == 'T')) &&
            ((src[fileNameLen - 3] == 'p') || (src[fileNameLen - 3] == 'P')) &&
            (src[fileNameLen - 4] == '.'))
        {
            fileSystem->seekFile(0, transferFile);
            strInstrumentFile instrumentFile;
            memset(&instrumentFile, 0, sizeof(instrumentFile));

            int32_t bytesRead =
              fileSystem->readFile(&instrumentFile, sizeof(instrumentFile), transferFile);
            InstrumentFileValidation::Result validation;

            if (bytesRead >= 0 &&
                InstrumentFileValidation::validate((const uint8_t*)&instrumentFile,
                                                   bytesRead,
                                                   sourceMemComplited,
                                                   sizeof(strInstrument),
                                                   INSTRUMENT_FILE_VERSION,
                                                   validation))
            {
                convertedDataSize = sourceMemComplited - validation.dataOffset;
            }
            else
            {
                invalidInstrument = true;
            }
        }

        transferStep = 0;

        fileSystem->closeFile(transferFile, false);
        fileSystem->closeFile(transferFile2, false);
        if (invalidInstrument)
        {
            fileSystem->remove(dest);
            return fileTransferFileNoValid;
        }
        return fileTransferEnd;
    }

    transferStep = 0;
    fileSystem->closeFile(transferFile, false);
    fileSystem->closeFile(transferFile2, false);
    return fileTransferError;
}

uint8_t cFileTransfer::copyInstrumentHeader(const char* src, const char* dest)
{
    if (!fileSystem->exists(src))
    {
        return fileTransferFileNoExist;
    }
    if (fileSystem->exists(dest))
    {
        fileSystem->remove(dest);
    }

    auto [isSrcOpen, srcFileHandle] =
      fileSystem->openFile(src, FileSystem::FileOpenMode::kRead, &transferFile);
    if (!isSrcOpen)
    {
        return fileTransferError;
    }

    int32_t readResult =
      fileSystem->readFile(externalRamFileManagerBuffL, sizeof(strInstrumentFile), transferFile);
    uint32_t sourceFileSize = fileSystem->getFileSize(transferFile);
    fileSystem->closeFile(transferFile, false);

    if (readResult < 0)
    {
        return fileTransferError;
    }

    InstrumentFileValidation::Result validation;
    if (!InstrumentFileValidation::validate(externalRamFileManagerBuffL,
                                            readResult,
                                            sourceFileSize,
                                            sizeof(strInstrument),
                                            INSTRUMENT_FILE_VERSION,
                                            validation))
    {
        return fileTransferFileNoValid;
    }
    uint32_t headerSize = validation.dataOffset;

    auto [isDestOpen, destFileHandle] =
      fileSystem->openFile(dest, FileSystem::FileOpenMode::kWriteCreateNew, &transferFile2);
    if (!isDestOpen)
    {
        return fileTransferError;
    }

    int32_t writeResult =
      fileSystem->writeFile(externalRamFileManagerBuffL, headerSize, transferFile2);
    fileSystem->closeFile(transferFile2, false);

    if (writeResult == (int32_t)headerSize)
    {
        transferBytesComplited += writeResult;
        return fileTransferEnd;
    }

    return fileTransferError;
}

uint8_t cFileTransfer::copySample(const char* src,
                                  const char* dest,
                                  uint16_t startSample,
                                  uint16_t endSample,
                                  uint32_t sampleRate)
{
    if (transferStep == 0)
    {
        if (!fileSystem->exists(src))
            return fileTransferFileNoExist;
        if (fileSystem->exists(dest))
        {
            fileSystem->remove(dest);
        }

        auto [isOpen, fileHandle] =
          fileSystem->openFile(src, FileSystem::FileOpenMode::kRead, &transferFile);

        if (isOpen)
        {
            if (!readHeader(&sampleHead, transferFile) || !checkSampleValid(sampleHead))
            {
                fileSystem->closeFile(transferFile, false);
                transferStep = 0;
                return fileTransferFileNoValid;
            }

            if (sampleRate != defaultSampleRate)
            {
                // trigger resampling in convertAudioData to target sampleRate
                // by faking source rate in inverse proportions
                sampleHead.sampleRate = (uint64_t)sampleHead.sampleRate * 44100 / sampleRate;
                sampleHead.byteRate = (uint64_t)sampleHead.byteRate * 44100 / sampleRate;
            }

            auto [isDestOpen, fileDestHandle] =
              fileSystem->openFile(dest, FileSystem::FileOpenMode::kWriteCreateNew, &transferFile2);
            if (isDestOpen)
            {
                int bytesPerSec =
                  sampleHead.sampleRate * sampleHead.numChannels * sampleHead.bitsPerSample / 8;

                conversionInPlace = (bytesPerSec >= 44100 * 1 * 2);
                sourceMemTotal = fileSystem->getFileAvailable(
                  transferFile); // pobiera wielkosc dopiero po przesciu przez header

                sourceMemTotal = (sourceMemTotal < sampleHead.subchunk2Size)
                                   ? sourceMemTotal
                                   : sampleHead.subchunk2Size;
                uint32_t seekPosition = 0;
                uint32_t endPosition = sourceMemTotal;
                uint32_t bytesPerSample = (sampleHead.bitsPerSample / 8) * sampleHead.numChannels;
                uint32_t totalSamplesCount = sourceMemTotal / bytesPerSample;

                if (startSample != 0)
                {
                    float from_pos = (float)startSample / MAX_16BIT;
                    seekPosition = ((uint32_t)(totalSamplesCount * from_pos)) * bytesPerSample;

                    fileSystem->seekFileCur(seekPosition, transferFile);
                }

                if (endSample != MAX_16BIT)
                {
                    float to_pos = (float)endSample / MAX_16BIT;
                    endPosition = ((uint32_t)(totalSamplesCount * to_pos)) * bytesPerSample;
                }

                sourceMemTotal = endPosition - seekPosition;

                memStep =
                  conversionInPlace ? READ_WRITE_BUFOR_SIZE
                                    : // wielokrotnosc 4*6*8 (rozne probkowania stereo w bajtach)
                    READ_WRITE_BUFOR_SIZE / 16; // wczytujemy 1/16 bufora, reszta na wynik konwersji
                sourceMemComplited = 0;

                fileSystem->seekFile(sizeof(sampleHead), transferFile2);

                // wylaczenie konwersji kiedy format juz sie zgadza
                conversionEnabled = sampleHead.bitsPerSample != 16 || sampleHead.numChannels != 1 ||
                                    sampleHead.AudioFormat != 1 || sampleHead.sampleRate != 44100;
                conversionConfig.sourceChannelCount = sampleHead.numChannels;
                conversionConfig.sourceBitsPerSample = sampleHead.bitsPerSample;
                conversionConfig.sourceSampleRate = sampleHead.sampleRate;
                conversionConfig.sourceFormat = sampleHead.AudioFormat;
                conversionConfig.destSampleRate = 44100;

                convertedDataSize = 0;

                // A chunk that is not converted in place is converted to 16 bit mono at
                // 44.1 kHz behind itself in the same buffer. Below this byte rate the result
                // of one chunk is longer than the rest of the buffer.
                const int minBytesPerSec =
                  (READ_WRITE_BUFOR_SIZE / 16) * (44100 * 2) /
                  (READ_WRITE_BUFOR_SIZE - 2 * (READ_WRITE_BUFOR_SIZE / 16));

                if (conversionInPlace || bytesPerSec >= minBytesPerSec)
                {
                    transferStep = 1;
                }
            }
        }
    }
    //--------------------------------------------------------
    if (transferStep == 1)
    {
        if (sourceMemTotal - sourceMemComplited < memStep)
        {
            memStep = sourceMemTotal - sourceMemComplited;
        }

        int32_t read_result =
          fileSystem->readFile(externalRamFileManagerBuffL, memStep, transferFile);

        if (read_result == 0) // koniec pliku - koncz kopiowanie
        {
            transferStep = 2;
        }
        else if (read_result > 0)
        {
            int32_t converted_bytes;
            int16_t* out_buffer;
            if (conversionEnabled)
            {
                out_buffer = (int16_t*)externalRamFileManagerBuffL +
                             (conversionInPlace ? 0 : READ_WRITE_BUFOR_SIZE / 16);

                conversionConfig.inputData = externalRamFileManagerBuffL;
                conversionConfig.inputSize = read_result;
                conversionConfig.outPtrL = out_buffer;
                conversionConfig.outPtrR = nullptr;

                uint32_t samplesConverted = convertAudioData(conversionConfig);
                converted_bytes = samplesConverted * 2; // liczba probek razy wielkosc probki !
            }
            else
            {
                out_buffer = (int16_t*)externalRamFileManagerBuffL;
                converted_bytes = read_result;
            }

            if (converted_bytes > 0)
            {
                int32_t write_result =
                  fileSystem->writeFile(out_buffer, converted_bytes, transferFile2);

                convertedDataSize += converted_bytes; // tu convertedDataSize to ilsoc bajtow
                sourceMemComplited += read_result;
                transferBytesComplited += read_result;

                if (write_result > 0)
                {
                    if (sourceMemComplited >= sourceMemTotal ||
                        sourceMemComplited >=
                          sampleHead.subchunk2Size) // przekroczenie mniejszego zatrzyma ladowanie
                    {
                        transferStep = 2;
                    }
                    else if (converted_bytes == write_result)
                    {
                        return fileTransferInProgress;
                    }
                }
            }
            else
            {
                transferStep = 2;
            }
        }
        // else wysypuje error na koncu funkcji \/
    }
    //--------------------------------------------------------
    if (transferStep == 2)
    {
        fillHeader(false, convertedDataSize);
        fileSystem->seekFile(0, transferFile2);
        fileSystem->writeFile(&sampleHead, sizeof(sampleHead), transferFile2);

        transferStep = 0;
        fileSystem->closeFile(transferFile, false);
        fileSystem->closeFile(transferFile2, false);
        return fileTransferEnd;
    }

    transferStep = 0;
    convertedDataSize = 0;
    fileSystem->closeFile(transferFile, false);
    fileSystem->closeFile(transferFile2, false);
    return fileTransferError;
}

uint8_t cFileTransfer::saveMemoryToFile(uint8_t* memory, const char* file, uint32_t memSize)
{
    if (transferStep == 0)
    {
        if (fileSystem->exists(file))
        {
            fileSystem->remove(file);
        }

        auto [isOpen, fileHandle] =
          fileSystem->openFile(file, FileSystem::FileOpenMode::kWriteOpen, &transferFile);
        if (isOpen)
        {
            sourceMemTotal = memSize;
            memStep = (memSize < READ_WRITE_BUFOR_SIZE)
                        ? memSize
                        : READ_WRITE_BUFOR_SIZE; // maksymalne paczki jak bufor mimo ze nie
                                                 // kozystanie z bufora
            sourceMemComplited = 0;
            transferStep = 1;
        }
    }

    if (transferStep == 1)
    {
        if (sourceMemTotal - sourceMemComplited < memStep)
        {
            memStep = sourceMemTotal - sourceMemComplited;
        }

        int32_t write_result =
          fileSystem->writeFile(memory + sourceMemComplited, memStep, transferFile);
        if (write_result >= 0)
        {
            sourceMemComplited += write_result;
            if (sourceMemComplited >= sourceMemTotal) // koniec pliku
            {
                transferStep = 2;
            }
            else if ((uint32_t)write_result == memStep)
            {
                return fileTransferInProgress;
            }
        }
    }

    if (transferStep == 2)
    {
        transferStep = 0;
        fileSystem->closeFile(transferFile, false);
        return fileTransferEnd;
    }

    transferStep = 0;
    fileSystem->closeFile(transferFile, false);
    return fileTransferError;
}

void cFileTransfer::endTransfer()
{
    transferStep = 0;
    fileSystem->closeFile(transferFile, false);
}

void cFileTransfer::fillHeader(bool stereo, uint32_t size)
{
    memset(&sampleHead, 0, sizeof(sampleHead));

    sampleHead.chunkId = 0x46464952; // "RIFF"
    sampleHead.chunkSize = size + 36;
    sampleHead.waveId = WAVEID;          // "WAVE"
    sampleHead.subchunk1Id = 0x20746d66; // "fmt "
    sampleHead.subchunk1Size = 16;
    sampleHead.AudioFormat = 1;
    sampleHead.numChannels = stereo ? 2 : 1;
    sampleHead.sampleRate = 44100;
    sampleHead.bitsPerSample = 16;
    sampleHead.byteRate =
      sampleHead.sampleRate * sampleHead.numChannels * (sampleHead.bitsPerSample / 8);
    sampleHead.blockAlign = sampleHead.numChannels * (sampleHead.bitsPerSample / 8);
    sampleHead.subchunk2Id = 0x61746164; // "data"
    sampleHead.subchunk2Size = size;
}

//--------------------------------------------------------------------------------------------------------------------------------------------------------------
//--------------------------------------------------------   obsluga nowych instrumentow
//--------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------------------
uint8_t cFileTransfer::loadInstrumentRawSampleDataToMemory(const char* file,
                                                           uint32_t fileOffset,
                                                           uint8_t* memory,
                                                           uint32_t sizeLimitBytes,
                                                           uint32_t& outSampleCount)
{
    if (transferStep == 0)
    {
        if (!fileSystem->exists(file))
            return fileTransferFileNoExist;

        auto [isOpen, fileHandle] =
          fileSystem->openFile(file, FileSystem::FileOpenMode::kRead, &transferFile);
        if (isOpen)
        {
            sourceMemTotal = 0;
            memStep = READ_WRITE_BUFOR_SIZE;
            sourceMemComplited = 0;

            int32_t result = fileSystem->seekFile(fileOffset, transferFile);

            if (result >= 0)
            {
                sourceMemTotal = fileSystem->getFileAvailable(transferFile);
                sourceMemTotal = std::min(sourceMemTotal, sizeLimitBytes);
                transferStep = 1;
            }
            else
            {
                fileSystem->closeFile(transferFile, false);
                transferStep = 0;
                return fileTransferError;
            }
        }
    }

    if (transferStep == 1)
    {
        if (sourceMemTotal - sourceMemComplited < memStep)
        {
            memStep = sourceMemTotal - sourceMemComplited;
        }

        int32_t result = fileSystem->readFile(memory + sourceMemComplited, memStep, transferFile);

        if (result >= 0)
        {
            sourceMemComplited += result;
            transferBytesComplited += result;

            if (result == 0) // koniec pliku
            {
                transferStep = 2;
            }
            else if (sourceMemComplited >=
                     sourceMemTotal) // wczytany wiekszy plik << niebezpieczne przekroczenie pamieci
            {
                transferStep = 2;
            }
            else
            {
                return fileTransferInProgress;
            }
        }
        // else wysypuje error na koncu funkcji \/
    }

    if (transferStep == 2)
    {
        outSampleCount =
          sourceMemComplited > sourceMemTotal ? sourceMemTotal / 2 : sourceMemComplited / 2;
        transferStep = 0;
        fileSystem->closeFile(transferFile, false);
        return fileTransferEnd;
    }

    transferStep = 0;
    fileSystem->closeFile(transferFile, false);
    return fileTransferError;
}

uint8_t cFileTransfer::saveSampleRawDataToFile(uint8_t* memory,
                                               uint32_t memSize,
                                               const char* file,
                                               uint32_t fileOffset,
                                               uint8_t* extraMemory)
{
    if (transferStep == 0)
    {
        if (!fileSystem->exists(file))
        {
            return fileTransferFileNoValid;
        }

        auto [isOpen, fileHandle] =
          fileSystem->openFile(file, FileSystem::FileOpenMode::kWriteOpen, &transferFile);
        if (isOpen)
        {
            sourceMemTotal = memSize;
            memStep = (memSize < READ_WRITE_BUFOR_SIZE)
                        ? memSize
                        : READ_WRITE_BUFOR_SIZE; // maksymalne paczki jak bufor mimo ze nie
                                                 // kozystanie z bufora
            sourceMemComplited = 0;
            srcDataBuff = memory;

            if (fileOffset > 0)
            {
                if (fileSystem->seekFile(fileOffset, transferFile))
                {
                    transferStep = 1;
                }
            }
            else
            {
                transferStep = 1;
            }
        }
    }

    if (transferStep == 1)
    {
        uint32_t stepSize = memStep;
        if (sourceMemTotal - sourceMemComplited < stepSize)
        {
            stepSize = sourceMemTotal - sourceMemComplited;
        }

        int32_t write_result =
          fileSystem->writeFile(srcDataBuff + sourceMemComplited, stepSize, transferFile);

        if (write_result >= 0)
        {
            sourceMemComplited += write_result;
            transferBytesComplited += write_result;

            if (sourceMemComplited >= sourceMemTotal) // koniec pliku
            {
                if (extraMemory != nullptr && srcDataBuff == memory)
                {
                    srcDataBuff = extraMemory;
                    sourceMemComplited = 0;
                    return fileTransferInProgress;
                }

                transferStep = 2;
            }
            else if ((uint32_t)write_result == memStep)
            {
                return fileTransferInProgress;
            }
        }
    }

    if (transferStep == 2)
    {
        transferStep = 0;

        fileSystem->endFile(transferFile);
        fileSystem->closeFile(transferFile, false);

        return fileTransferEnd;
    }

    transferStep = 0;
    fileSystem->closeFile(transferFile, false);
    return fileTransferError;
}

uint8_t cFileTransfer::saveInstrumentStructToInstrumentFile(uint8_t* memory,
                                                            uint32_t memSize,
                                                            char* file,
                                                            uint32_t* outFileSize)
{
    if (transferStep == 0)
    {
        auto [isOpen, fileHandle] =
          fileSystem->openFile(file, FileSystem::FileOpenMode::kWriteOpen, &transferFile);
        if (isOpen)
        {
            sourceMemTotal = memSize;
            memStep = (memSize < READ_WRITE_BUFOR_SIZE)
                        ? memSize
                        : READ_WRITE_BUFOR_SIZE; // maksymalne paczki jak bufor mimo ze nie
                                                 // kozystanie z bufora
            sourceMemComplited = 0;
            transferStep = 1;
        }
        else
        {
            transferStep = 0;
            if (transferFile != nullptr)
            {
                fileSystem->closeFile(transferFile, false);
            }
            return fileTransferError;
        }
    }

    if (transferStep == 1)
    {
        if (sourceMemTotal - sourceMemComplited < memStep)
        {
            memStep = sourceMemTotal - sourceMemComplited;
        }

        int32_t write_result =
          fileSystem->writeFile(memory + sourceMemComplited, memStep, transferFile);

        if (write_result >= 0)
        {
            sourceMemComplited += write_result;
            if (sourceMemComplited >= sourceMemTotal) // koniec pliku
            {
                transferStep = 2;
            }
            else if ((uint32_t)write_result == memStep)
            {
                return fileTransferInProgress;
            }
        }
    }

    if (transferStep == 2)
    {
        *outFileSize = fileSystem->getFileSize(transferFile);
        transferStep = 0;
        if (transferFile != nullptr)
        {
            fileSystem->closeFile(transferFile, false);
        }
        return fileTransferEnd;
    }

    transferStep = 0;
    if (transferFile != nullptr)
    {
        fileSystem->closeFile(transferFile, false);
    }
    return fileTransferError;
}

uint8_t cFileTransfer::copySampleToInstrumentFile(const char* src,
                                                  const char* dest,
                                                  uint16_t startSample,
                                                  uint16_t endSample,
                                                  uint32_t sampleRate,
                                                  uint32_t fileOffset)
{
    if (transferStep == 0)
    {
        if (!fileSystem->exists(src))
        {
            return fileTransferFileNoExist;
        }

        auto [isOpen, fileHandle] =
          fileSystem->openFile(src, FileSystem::FileOpenMode::kRead, &transferFile);
        if (isOpen)
        {
            if (!readHeader(&sampleHead, transferFile) || !checkSampleValid(sampleHead))
            {
                fileSystem->closeFile(transferFile, false);
                transferStep = 0;
                return fileTransferFileNoValid;
            }

            if (sampleRate != defaultSampleRate)
            {
                // trigger resampling in convertAudioData to target sampleRate
                // by faking source rate in inverse proportions
                sampleHead.sampleRate = (uint64_t)sampleHead.sampleRate * 44100 / sampleRate;
                sampleHead.byteRate = (uint64_t)sampleHead.byteRate * 44100 / sampleRate;
            }

            auto [isDestOpen, fileDestHandle] =
              fileSystem->openFile(dest, FileSystem::FileOpenMode::kWriteOpen, &transferFile2);

            if (isDestOpen)
            {
                uint32_t sourceBytesPerSample =
                  (sampleHead.bitsPerSample / 8) * sampleHead.numChannels;
                uint32_t sourceBytesPerSec = sampleHead.sampleRate * sourceBytesPerSample;

                conversionInPlace = (sourceBytesPerSec >= 44100 * 1 * 2);

                sourceMemTotal = fileSystem->getFileAvailable(
                  transferFile); // pobiera wielkość za nagłówkiem do końca
                sourceMemTotal = (sourceMemTotal <= sampleHead.subchunk2Size)
                                   ? sourceMemTotal
                                   : sampleHead.subchunk2Size;

                uint32_t seekPosition = 0;
                uint32_t endPosition = sourceMemTotal;
                uint32_t sourceTotalSamplesCount = sourceMemTotal / sourceBytesPerSample;

                if (startSample != 0)
                {
                    float from_pos = (float)startSample / MAX_16BIT;
                    seekPosition =
                      ((uint32_t)(sourceTotalSamplesCount * from_pos)) * sourceBytesPerSample;

                    fileSystem->seekFileCur(seekPosition, transferFile);
                }

                if (endSample != MAX_16BIT)
                {
                    float to_pos = (float)endSample / MAX_16BIT;
                    endPosition =
                      ((uint32_t)(sourceTotalSamplesCount * to_pos)) * sourceBytesPerSample;
                }

                sourceMemTotal = endPosition - seekPosition;
                sourceMemComplited = 0;
                memStep =
                  conversionInPlace
                    ? READ_WRITE_BUFOR_SIZE       //  4*6*8 (rozne probkowania stereo)
                    : READ_WRITE_BUFOR_SIZE / 16; // laduj 1/16 bufora, reszta na wynik konwersji

                destDataOffset = fileOffset;

                bool result = fileSystem->seekFile(fileOffset, transferFile2);
                if (result)
                {
                    // wylaczenie konwersji kiedy format juz sie zgadza
                    conversionEnabled =
                      sampleHead.bitsPerSample != 16 || sampleHead.numChannels != 1 ||
                      sampleHead.AudioFormat != 1 || sampleHead.sampleRate != 44100;
                    conversionConfig.sourceChannelCount = sampleHead.numChannels;
                    conversionConfig.sourceBitsPerSample = sampleHead.bitsPerSample;
                    conversionConfig.sourceSampleRate = sampleHead.sampleRate;
                    conversionConfig.sourceFormat = sampleHead.AudioFormat;
                    conversionConfig.destSampleRate = 44100;

                    destSampleCount = sourceTotalSamplesCount * 44100 / sampleHead.sampleRate;

                    convertedDataSize = 0;
                    transferStep = 1;
                }
            }
        }
    }
    //--------------------------------------------------------
    if (transferStep == 1)
    {
        if (sourceMemTotal - sourceMemComplited < memStep)
        {
            memStep = sourceMemTotal - sourceMemComplited;
        }

        int32_t bytesReadFromSource =
          fileSystem->readFile(externalRamFileManagerSrcBuff, memStep, transferFile);

        if (bytesReadFromSource == 0) // koniec pliku - koncz kopiowanie
        {
            transferStep = 2;
        }
        else if (bytesReadFromSource > 0)
        {
            int32_t bytesConverted = 0;
            int16_t* outBufferL = nullptr;
            int16_t* outBufferR = nullptr;

            //            switch (channelMode)
            //            {
            //                case ChannelMode::kLeftAndRight:
            //                    outBufferL = (int16_t*)externalRamFileManagerBuffL;
            //                    outBufferR = (int16_t*)externalRamFileManagerBuffR;
            //                    break;
            //                case ChannelMode::kLeft: outBufferL =
            //                (int16_t*)externalRamFileManagerBuffL; break; case
            //                ChannelMode::kRight: outBufferR =
            //                (int16_t*)externalRamFileManagerBuffR; break;
            //            }

            if (conversionEnabled)
            {
                conversionConfig.inputData = externalRamFileManagerSrcBuff;
                conversionConfig.inputSize = bytesReadFromSource;
                conversionConfig.outPtrL = outBufferL;
                conversionConfig.outPtrR = outBufferR;

                uint32_t samplesConverted = convertAudioData(conversionConfig);
                bytesConverted = 2 * samplesConverted; // liczba probek razy wielkosc probki !
            }
            else // no conversion = always mono left channel buffer
            {
                outBufferR = nullptr;
                outBufferL = (int16_t*)externalRamFileManagerSrcBuff;
                bytesConverted = bytesReadFromSource;
            }
            convertedDataSize += bytesConverted;

            if (bytesReadFromSource > 0)
            {
                int32_t bytesWroteL = 0;
                int32_t bytesWroteR = 0;

                if (outBufferL != nullptr)
                {
                    fileSystem->seekFile(destDataOffset + sourceMemComplited, transferFile2);
                    bytesWroteL = fileSystem->writeFile(outBufferL, bytesConverted, transferFile2);
                }

                if (outBufferR != nullptr)
                {
                    fileSystem->seekFile(destDataOffset + destSampleCount + sourceMemComplited,
                                         transferFile2);
                    bytesWroteR = fileSystem->writeFile(outBufferL, bytesConverted, transferFile2);
                }

                sourceMemComplited += bytesReadFromSource;

                if (bytesWroteL > 0 || bytesWroteR > 0)
                {
                    if (sourceMemComplited >= sourceMemTotal ||
                        sourceMemComplited >= sampleHead.subchunk2Size)
                    {
                        // przekroczenie mniejszego zatrzyma ladowanie
                        transferStep = 2;
                    }
                    else if (bytesConverted == bytesWroteL)
                    {
                        return fileTransferInProgress;
                    }
                }
            }
            else
            {
                transferStep = 2;
            }
        }
        // else wysypuje error na koncu funkcji \/
    }
    //--------------------------------------------------------
    if (transferStep == 2)
    {
        transferStep = 0;

        fileSystem->endFile(transferFile2);

        fileSystem->closeFile(transferFile, false);
        fileSystem->closeFile(transferFile2, false);

        return fileTransferEnd;
    }

    transferStep = 0;
    convertedDataSize = 0;
    fileSystem->closeFile(transferFile, false);
    fileSystem->closeFile(transferFile2, false);
    return fileTransferError;
}

uint8_t cFileTransfer::saveMemoryToWav(int16_t* memoryChannelL,
                                       int16_t* memoryChannelR,
                                       uint32_t samplesCount,
                                       const char* file)
{
    if (transferStep == 0)
    {
        if (fileSystem->exists(file))
        {
            fileSystem->remove(file);
        }

        auto [isOpen, fileHandle] =
          fileSystem->openFile(file, FileSystem::FileOpenMode::kWriteCreateNew, &transferFile);
        if (isOpen)
        {
            if (memoryChannelL == memoryChannelR)
            {
                sourceMemTotal = samplesCount * 2;
            }
            else
            {
                conversionInPlace = true; // combine two buffers into stereo file
                sourceMemTotal = samplesCount * 4;
            }

            memStep = READ_WRITE_BUFOR_SIZE;
            sourceMemComplited = 0;
            transferStep = 1;
        }
    }

    if (transferStep == 1)
    {
        if (sourceMemTotal - sourceMemComplited < memStep)
        {
            memStep = sourceMemTotal - sourceMemComplited;
        }

        int16_t* data = memoryChannelL + sourceMemComplited / 2;

        if (conversionInPlace)
        {
            uint32_t offset = sourceMemComplited / 4;
            for (uint32_t sample = 0; sample < memStep / 2; sample++)
            {
                ((int16_t*)externalRamFileManagerSrcBuff)[2 * sample] =
                  memoryChannelL[sample + offset];
                ((int16_t*)externalRamFileManagerSrcBuff)[2 * sample + 1] =
                  memoryChannelR[sample + offset];
            }

            data = (int16_t*)externalRamFileManagerSrcBuff;
        }

        int32_t write_result = fileSystem->writeFile((uint8_t*)data, memStep, transferFile);

        if (write_result >= 0)
        {
            sourceMemComplited += write_result;
            transferBytesComplited += write_result;

            if (sourceMemComplited >= sourceMemTotal)
            {
                transferStep = 2;
            }
            else if ((uint32_t)write_result == memStep)
            {
                return fileTransferInProgress;
            }
        }
    }

    if (transferStep == 2)
    {
        fillHeader(conversionInPlace, sourceMemComplited);

        fileSystem->seekFile(0, transferFile);
        fileSystem->writeFile(&sampleHead, sizeof(sampleHead), transferFile);

        transferStep = 0;
        fileSystem->closeFile(transferFile, false);
        return fileTransferEnd;
    }

    transferStep = 0;
    fileSystem->closeFile(transferFile, false);
    return fileTransferError;
}

uint8_t cFileTransfer::loadSampleDataToMemory(
  FileManagerOperation::SampleLoadData* sampleImportData,
  int16_t* memoryAddress,
  uint32_t targetSampleRate)
{
    if (transferStep == 0)
    {
        char srcFilePath[PATCH_SIZE];
        snprintf(srcFilePath,
                 sizeof(srcFilePath),
                 "%s/%s",
                 sampleImportData->filePath,
                 sampleImportData->fileName);

        if (!fileSystem->exists(srcFilePath))
        {
            return fileTransferFileNoExist;
        }

        auto [isOpen, fileHandle] =
          fileSystem->openFile(srcFilePath, FileSystem::FileOpenMode::kRead, &transferFile);

        if (isOpen)
        {
            memStep = READ_WRITE_BUFOR_SIZE;
            sourceMemComplited = 0;
            convertedDataSize = 0;
            conversionConfig.sourceChannelCount = sampleImportData->sourceChannelCount;
            conversionConfig.sourceBitsPerSample = sampleImportData->sourceBitsPerSample;
            conversionConfig.sourceSampleRate = sampleImportData->sourceSampleRate;
            conversionConfig.sourceFormat = sampleImportData->sourceFormat;
            conversionConfig.destSampleRate = targetSampleRate;

            fileSystem->seekFile(sampleImportData->fileOffset, transferFile);

            sourceMemTotal = fileSystem->getFileAvailable(transferFile);

            if (sourceMemTotal < sampleImportData->sourceAllChannelBytesPerSample *
                                   sampleImportData->sourceTotalSamplesCountPerChannel)
            {
                fileSystem->closeFile(transferFile, false);
                transferStep = 0;
                return fileTransferFileNoValid;
            }
            else
            {
                sourceMemTotal = sampleImportData->sourceAllChannelBytesPerSample *
                                 sampleImportData->sourceTotalSamplesCountPerChannel;
            }

            transferStep = 1;
        }
    }

    if (transferStep == 1)
    {
        if (sourceMemTotal - sourceMemComplited < memStep)
        {
            memStep = sourceMemTotal - sourceMemComplited;
        }

        int32_t bytesReadFromSource =
          fileSystem->readFile(externalRamFileManagerSrcBuff, memStep, transferFile);

        if (bytesReadFromSource == 0)
        {
            transferStep = 2;
        }
        else if (bytesReadFromSource > 0)
        {
            int32_t bytesConverted = 0;
            int16_t* outBufferL = nullptr;
            int16_t* outBufferR = nullptr;

            switch (sampleImportData->destChannelCount)
            {
                case 1: outBufferL = memoryAddress + convertedDataSize / 2; break;
                case 2:
                    outBufferL = memoryAddress + convertedDataSize / 2;
                    outBufferR = memoryAddress + sampleImportData->destSampleCountPerChannel +
                                 convertedDataSize / 2;
                    break;
                default: break;
            }

            // conversion means sample is not mono or has incorrect other parameters
            // stereo need conversion to split into two buffers
            if (sampleImportData->conversion)
            {
                conversionConfig.inputData = externalRamFileManagerSrcBuff;
                conversionConfig.inputSize = bytesReadFromSource;
                conversionConfig.outPtrL = outBufferL;
                conversionConfig.outPtrR = outBufferR;

                uint32_t samplesConverted = convertAudioData(conversionConfig);
                bytesConverted = samplesConverted * 2;
            }
            else // no conversion = always mono left channel buffer
            {
                memcpy((uint8_t*)outBufferL,
                       (uint8_t*)externalRamFileManagerSrcBuff,
                       bytesReadFromSource);
                bytesConverted = bytesReadFromSource;
            }
            convertedDataSize += bytesConverted;
            sourceMemComplited += bytesReadFromSource;
            transferBytesComplited += bytesReadFromSource;

            if (sourceMemComplited >= sourceMemTotal ||
                convertedDataSize >= sampleImportData->destSampleCountPerChannel * 2)
            {
                transferStep = 2;
            }
            else
            {
                return fileTransferInProgress;
            }
        }
    }

    if (transferStep == 2)
    {
        uint32_t samplesWritten = convertedDataSize / 2;
        if (samplesWritten < sampleImportData->destSampleCountPerChannel)
        {
            uint32_t samplesRemaining =
              sampleImportData->destSampleCountPerChannel - samplesWritten;
            memset(memoryAddress + samplesWritten, 0, samplesRemaining * sizeof(int16_t));
            if (sampleImportData->destChannelCount == 2)
            {
                memset(memoryAddress + sampleImportData->destSampleCountPerChannel + samplesWritten,
                       0,
                       samplesRemaining * sizeof(int16_t));
            }
        }

        transferStep = 0;
        fileSystem->closeFile(transferFile, false);

        return fileTransferEnd;
    }

    transferStep = 0;
    fileSystem->closeFile(transferFile, false);
    return fileTransferError;
}

extern int16_t externalRamSampleMemory[SAMPLE_MEMORY_SIZE / 2];

static constexpr uint32_t kAlignedWriteChunkSize = 256 * 1024;

static void findFreeSampleRamScratchStereo(uint32_t channels,
                                           uint8_t*& outBaseL,
                                           uint8_t*& outBaseR,
                                           uint32_t& outCapPerChannel)
{
    uint8_t* scratchStart = reinterpret_cast<uint8_t*>(externalRamSampleMemory);
    for (int i = 0; i < kSampleInstrumentsCount; i++)
    {
        if (mtProject.instrument[i].isActive && mtProject.instrument[i].sample.address != nullptr)
        {
            uint32_t frames = mtProject.sampleMemory[i].isStreamed
                                ? mtProject.sampleMemory[i].ramFrames
                                : (mtProject.sampleMemory[i].ramFrames > 0
                                     ? mtProject.sampleMemory[i].ramFrames
                                     : mtProject.instrument[i].sample.length);
            uint32_t ch = (mtProject.sampleMemory[i].isStereo ||
                           mtProject.sampleMemory[i].address[strSampleMemory::Channel::kRight] !=
                             mtProject.sampleMemory[i].address[strSampleMemory::Channel::kLeft])
                            ? 2
                            : 1;
            uint8_t* endPtr = reinterpret_cast<uint8_t*>(mtProject.instrument[i].sample.address) +
                              frames * ch * sizeof(int16_t);
            if (endPtr > scratchStart)
            {
                scratchStart = endPtr;
            }
        }
    }

    uintptr_t alignedStart = (reinterpret_cast<uintptr_t>(scratchStart) + 511u) & ~uintptr_t(511u);
    uint8_t* ramEnd = reinterpret_cast<uint8_t*>(externalRamSampleMemory) + SAMPLE_MEMORY_SIZE;

    if (channels == 2)
    {
        if (ramEnd > reinterpret_cast<uint8_t*>(alignedStart) + (READ_WRITE_BUFOR_SIZE * 2 + 1024))
        {
            uint32_t avail = static_cast<uint32_t>(ramEnd - reinterpret_cast<uint8_t*>(alignedStart));
            if (avail > 8 * 1024 * 1024)
            {
                avail = 8 * 1024 * 1024;
            }
            uint32_t half = ((avail / 2) - 512) & ~uint32_t(511);
            outBaseL = reinterpret_cast<uint8_t*>(alignedStart);
            outBaseR = outBaseL + half + 512;
            outCapPerChannel = half;
            return;
        }
        uint32_t half = ((READ_WRITE_BUFOR_SIZE / 2) - 512) & ~uint32_t(511);
        outBaseL = externalRamFileManagerBuffL;
        outBaseR = externalRamFileManagerBuffL + half + 512;
        outCapPerChannel = half;
    }
    else
    {
        if (ramEnd > reinterpret_cast<uint8_t*>(alignedStart) + (READ_WRITE_BUFOR_SIZE + 512))
        {
            uint32_t avail = static_cast<uint32_t>(ramEnd - reinterpret_cast<uint8_t*>(alignedStart));
            if (avail > 8 * 1024 * 1024)
            {
                avail = 8 * 1024 * 1024;
            }
            uint32_t cap = (avail - 512) & ~uint32_t(511);
            outBaseL = reinterpret_cast<uint8_t*>(alignedStart);
            outBaseR = nullptr;
            outCapPerChannel = cap;
            return;
        }
        uint32_t cap = (READ_WRITE_BUFOR_SIZE - 512) & ~uint32_t(511);
        outBaseL = externalRamFileManagerBuffL;
        outBaseR = nullptr;
        outCapPerChannel = cap;
    }
}

uint8_t cFileTransfer::convertWaveToInstrumentFile(
  FileManagerOperation::SampleLoadData* sampleImportData,
  const char* destFile,
  uint32_t destFileOffset,
  uint32_t targetSampleRate)
{
    uint32_t targetChannelBytes = sampleImportData->destSampleCountPerChannel * 2;

    if (transferStep == 0)
    {
        char srcFilePath[PATCH_SIZE];
        snprintf(srcFilePath,
                 sizeof(srcFilePath),
                 "%s/%s",
                 sampleImportData->filePath,
                 sampleImportData->fileName);

        if (!fileSystem->exists(srcFilePath))
        {
            return fileTransferFileNoExist;
        }
        if (!fileSystem->exists(destFile))
        {
            return fileTransferFileNoValid;
        }

        auto [isOpen, fileHandle] =
          fileSystem->openFile(srcFilePath, FileSystem::FileOpenMode::kRead, &transferFile);
        if (isOpen)
        {
            auto [isDestOpen, destHandle] =
              fileSystem->openFile(destFile, FileSystem::FileOpenMode::kWriteOpen, &transferFile2);
            if (!isDestOpen)
            {
                fileSystem->closeFile(transferFile, false);
                transferStep = 0;
                return fileTransferError;
            }

            sourceMemComplited = 0;
            convertedDataSize = 0;
            channelPass = 0;
            batchFilled = 0;
            batchWriteOffset = 0;
            channelBytesFlushed = 0;
            destDataOffset = destFileOffset;

            findFreeSampleRamScratchStereo(
              sampleImportData->destChannelCount, batchBufLBase, batchBufRBase, batchCapacity);

            uint32_t filePosL = destDataOffset;
            batchBufL = batchBufLBase + (filePosL & 3u);
            if (sampleImportData->destChannelCount == 2)
            {
                uint32_t filePosR = destDataOffset + targetChannelBytes;
                batchBufR = batchBufRBase + (filePosR & 3u);
            }
            else
            {
                batchBufR = nullptr;
            }

            conversionConfig.sourceChannelCount = sampleImportData->sourceChannelCount;
            conversionConfig.sourceBitsPerSample = sampleImportData->sourceBitsPerSample;
            conversionConfig.sourceSampleRate = sampleImportData->sourceSampleRate;
            conversionConfig.sourceFormat = sampleImportData->sourceFormat;
            conversionConfig.destSampleRate = targetSampleRate;

            uint32_t finalFileSize =
              destDataOffset + targetChannelBytes * sampleImportData->destChannelCount;
            if (!fileSystem->seekFile(sampleImportData->fileOffset, transferFile) ||
                !fileSystem->seekFile(finalFileSize, transferFile2) ||
                !fileSystem->seekFile(destDataOffset, transferFile2))
            {
                fileSystem->closeFile(transferFile, false);
                fileSystem->closeFile(transferFile2, false);
                transferStep = 0;
                return fileTransferError;
            }

            sourceMemTotal = fileSystem->getFileAvailable(transferFile);
            uint32_t expectedBytes = sampleImportData->sourceAllChannelBytesPerSample *
                                     sampleImportData->sourceTotalSamplesCountPerChannel;
            if (sourceMemTotal < expectedBytes)
            {
                fileSystem->closeFile(transferFile, false);
                fileSystem->closeFile(transferFile2, false);
                transferStep = 0;
                return fileTransferFileNoValid;
            }
            sourceMemTotal = expectedBytes;

            uint32_t maxOutPerStep = (batchCapacity < READ_WRITE_BUFOR_SIZE)
                                       ? batchCapacity
                                       : READ_WRITE_BUFOR_SIZE;
            if (sampleImportData->conversion)
            {
                uint32_t bps = sampleImportData->sourceAllChannelBytesPerSample;
                if (bps == 0)
                {
                    bps = 2;
                }
                uint32_t maxInputFrames =
                  (uint32_t)(((uint64_t)(maxOutPerStep / 2) * sampleImportData->sourceSampleRate) /
                             targetSampleRate);
                if (maxInputFrames == 0)
                {
                    maxInputFrames = 1;
                }
                uint32_t maxInputBytes = maxInputFrames * bps;
                uint32_t bufAligned = (READ_WRITE_BUFOR_SIZE / bps) * bps;
                memStep = (maxInputBytes < bufAligned) ? maxInputBytes : bufAligned;
                if (memStep == 0)
                {
                    memStep = bps;
                }
            }
            else
            {
                memStep = maxOutPerStep;
            }

            transferStep = 1;
        }
    }

    if (transferStep == 1)
    {
        uint32_t readSize = (sourceMemTotal - sourceMemComplited < memStep)
                              ? (sourceMemTotal - sourceMemComplited)
                              : memStep;

        int32_t bytesReadFromSource =
          (readSize > 0)
            ? fileSystem->readFile(externalRamFileManagerSrcBuff, readSize, transferFile)
            : 0;

        if (bytesReadFromSource < 0)
        {
            transferStep = 0;
            fileSystem->closeFile(transferFile, false);
            fileSystem->closeFile(transferFile2, false);
            return fileTransferError;
        }

        if (bytesReadFromSource > 0)
        {
            uint32_t bytesConverted = 0;

            if (sampleImportData->conversion)
            {
                conversionConfig.inputData = externalRamFileManagerSrcBuff;
                conversionConfig.inputSize = bytesReadFromSource;
                conversionConfig.outPtrL = reinterpret_cast<int16_t*>(batchBufL + batchFilled);
                conversionConfig.outPtrR =
                  (sampleImportData->destChannelCount == 2)
                    ? reinterpret_cast<int16_t*>(batchBufR + batchFilled)
                    : nullptr;

                uint32_t samplesConverted = convertAudioData(conversionConfig);
                bytesConverted = samplesConverted * 2;
            }
            else
            {
                bytesConverted = (uint32_t)bytesReadFromSource;
                memcpy(batchBufL + batchFilled, externalRamFileManagerSrcBuff, bytesConverted);
            }

            uint32_t remainingChannel = targetChannelBytes - convertedDataSize;
            uint32_t toKeep =
              (bytesConverted > remainingChannel) ? remainingChannel : bytesConverted;

            batchFilled += toKeep;
            convertedDataSize += toKeep;
            sourceMemComplited += (uint32_t)bytesReadFromSource;
            transferBytesComplited += (uint32_t)bytesReadFromSource;
        }

        bool eofReached = (bytesReadFromSource == 0 || sourceMemComplited >= sourceMemTotal ||
                           convertedDataSize >= targetChannelBytes);
        if (eofReached && convertedDataSize < targetChannelBytes)
        {
            uint32_t padRoom = batchCapacity - batchFilled;
            uint32_t padNeeded = targetChannelBytes - convertedDataSize;
            uint32_t padNow = (padNeeded < padRoom) ? padNeeded : padRoom;
            if (padNow > 0)
            {
                memset(batchBufL + batchFilled, 0, padNow);
                if (sampleImportData->destChannelCount == 2)
                {
                    memset(batchBufR + batchFilled, 0, padNow);
                }
                batchFilled += padNow;
                convertedDataSize += padNow;
            }
        }

        if (batchFilled + READ_WRITE_BUFOR_SIZE > batchCapacity || eofReached)
        {
            if (batchFilled > 0)
            {
                batchWriteOffset = 0;
                if (!fileSystem->seekFile(destDataOffset + channelBytesFlushed, transferFile2))
                {
                    transferStep = 0;
                    fileSystem->closeFile(transferFile, false);
                    fileSystem->closeFile(transferFile2, false);
                    return fileTransferError;
                }
                transferStep = 2;
                return fileTransferInProgress;
            }
            else
            {
                transferStep = 4;
            }
        }
        else
        {
            return fileTransferInProgress;
        }
    }

    if (transferStep == 2 || transferStep == 3)
    {
        uint8_t* activeBuf = (transferStep == 2) ? batchBufL : batchBufR;
        uint32_t baseFilePos =
          (transferStep == 2)
            ? (destDataOffset + channelBytesFlushed)
            : (destDataOffset + targetChannelBytes + channelBytesFlushed);

        uint32_t remaining = batchFilled - batchWriteOffset;
        if (remaining > 0)
        {
            uint32_t curFilePos = baseFilePos + batchWriteOffset;
            uint32_t chunk = remaining;
            if (batchWriteOffset == 0 && (curFilePos & 511u) != 0)
            {
                uint32_t headBytes = 512u - (curFilePos & 511u);
                if (chunk > headBytes)
                {
                    uint32_t alignedRest =
                      ((chunk - headBytes) > kAlignedWriteChunkSize)
                        ? kAlignedWriteChunkSize
                        : ((chunk - headBytes) & ~uint32_t(511u));
                    if (alignedRest > 0)
                    {
                        int32_t wHead =
                          fileSystem->writeFile(activeBuf, headBytes, transferFile2);
                        if (wHead != (int32_t)headBytes)
                        {
                            transferStep = 0;
                            fileSystem->closeFile(transferFile, false);
                            fileSystem->closeFile(transferFile2, false);
                            return fileTransferError;
                        }
                        batchWriteOffset += headBytes;
                        transferBytesComplited += headBytes;
                        remaining -= headBytes;
                        chunk = (remaining > kAlignedWriteChunkSize)
                                  ? kAlignedWriteChunkSize
                                  : remaining;
                        if (chunk > 512u && chunk < remaining)
                        {
                            chunk &= ~uint32_t(511u);
                        }
                    }
                }
            }
            else if (chunk > kAlignedWriteChunkSize)
            {
                chunk = kAlignedWriteChunkSize;
            }
            else if (chunk > 512u && chunk < remaining)
            {
                chunk &= ~uint32_t(511u);
            }

            int32_t written =
              fileSystem->writeFile(activeBuf + batchWriteOffset, chunk, transferFile2);
            if (written != (int32_t)chunk)
            {
                transferStep = 0;
                fileSystem->closeFile(transferFile, false);
                fileSystem->closeFile(transferFile2, false);
                return fileTransferError;
            }
            batchWriteOffset += chunk;
            transferBytesComplited += chunk;
        }

        if (batchWriteOffset < batchFilled)
        {
            return fileTransferInProgress;
        }

        if (transferStep == 2 && sampleImportData->destChannelCount == 2)
        {
            batchWriteOffset = 0;
            if (!fileSystem->seekFile(
                  destDataOffset + targetChannelBytes + channelBytesFlushed, transferFile2))
            {
                transferStep = 0;
                fileSystem->closeFile(transferFile, false);
                fileSystem->closeFile(transferFile2, false);
                return fileTransferError;
            }
            transferStep = 3;
            return fileTransferInProgress;
        }

        channelBytesFlushed += batchFilled;
        batchFilled = 0;
        batchWriteOffset = 0;

        if (channelBytesFlushed < targetChannelBytes)
        {
            uint32_t filePosL = destDataOffset + channelBytesFlushed;
            batchBufL = batchBufLBase + (filePosL & 3u);
            if (sampleImportData->destChannelCount == 2)
            {
                uint32_t filePosR = destDataOffset + targetChannelBytes + channelBytesFlushed;
                batchBufR = batchBufRBase + (filePosR & 3u);
            }
            transferStep = 1;
            return fileTransferInProgress;
        }

        transferStep = 4;
    }

    if (transferStep == 4)
    {
        uint32_t finalFileSize =
          destDataOffset + targetChannelBytes * sampleImportData->destChannelCount;
        fileSystem->seekFile(finalFileSize, transferFile2);
        fileSystem->endFile(transferFile2);
        transferStep = 0;
        fileSystem->closeFile(transferFile, false);
        fileSystem->closeFile(transferFile2, false);
        return fileTransferEnd;
    }

    transferStep = 0;
    fileSystem->closeFile(transferFile, false);
    fileSystem->closeFile(transferFile2, false);
    return fileTransferError;
}

uint8_t cFileTransfer::copyRawDataBetweenFiles(const char* srcFile,
                                               uint32_t srcOffset,
                                               const char* destFile,
                                               uint32_t destOffset,
                                               uint32_t sizeBytes)
{
    if (transferStep == 0)
    {
        if (!fileSystem->exists(srcFile))
        {
            return fileTransferFileNoExist;
        }
        if (!fileSystem->exists(destFile))
        {
            return fileTransferFileNoValid;
        }

        auto [isOpen, srcHandle] =
          fileSystem->openFile(srcFile, FileSystem::FileOpenMode::kRead, &transferFile);
        if (isOpen)
        {
            auto [isDestOpen, dstHandle] =
              fileSystem->openFile(destFile, FileSystem::FileOpenMode::kWriteOpen, &transferFile2);
            if (!isDestOpen)
            {
                fileSystem->closeFile(transferFile, false);
                transferStep = 0;
                return fileTransferError;
            }

            if (!fileSystem->seekFile(srcOffset, transferFile) ||
                !fileSystem->seekFile(destOffset, transferFile2))
            {
                fileSystem->closeFile(transferFile, false);
                fileSystem->closeFile(transferFile2, false);
                transferStep = 0;
                return fileTransferError;
            }

            sourceMemTotal = sizeBytes;
            sourceMemComplited = 0;
            batchFilled = 0;
            batchWriteOffset = 0;
            channelBytesFlushed = 0;
            destDataOffset = destOffset;
            findFreeSampleRamScratchStereo(1, batchBufLBase, batchBufRBase, batchCapacity);
            batchBufL = batchBufLBase + (destDataOffset & 3u);
            memStep = READ_WRITE_BUFOR_SIZE;
            transferStep = 1;
        }
    }

    if (transferStep == 1)
    {
        uint32_t readSize = (sourceMemTotal - sourceMemComplited < memStep)
                              ? (sourceMemTotal - sourceMemComplited)
                              : memStep;
        if (batchFilled + readSize > batchCapacity)
        {
            readSize = batchCapacity - batchFilled;
        }
        int32_t bytesRead =
          (readSize > 0)
            ? fileSystem->readFile(batchBufL + batchFilled, readSize, transferFile)
            : 0;

        if (bytesRead < 0)
        {
            transferStep = 0;
            fileSystem->closeFile(transferFile, false);
            fileSystem->closeFile(transferFile2, false);
            return fileTransferError;
        }

        if (bytesRead > 0)
        {
            batchFilled += (uint32_t)bytesRead;
            sourceMemComplited += (uint32_t)bytesRead;
            transferBytesComplited += (uint32_t)bytesRead;
        }

        bool eofReached = (bytesRead == 0 || sourceMemComplited >= sourceMemTotal);
        if (batchFilled + READ_WRITE_BUFOR_SIZE > batchCapacity || eofReached)
        {
            if (batchFilled > 0)
            {
                batchWriteOffset = 0;
                transferStep = 2;
                return fileTransferInProgress;
            }
            transferStep = 3;
        }
        else
        {
            return fileTransferInProgress;
        }
    }

    if (transferStep == 2)
    {
        uint32_t remaining = batchFilled - batchWriteOffset;
        if (remaining > 0)
        {
            uint32_t curFilePos = destDataOffset + channelBytesFlushed + batchWriteOffset;
            uint32_t chunk = remaining;
            if (batchWriteOffset == 0 && (curFilePos & 511u) != 0)
            {
                uint32_t headBytes = 512u - (curFilePos & 511u);
                if (chunk > headBytes)
                {
                    int32_t wHead =
                      fileSystem->writeFile(batchBufL, headBytes, transferFile2);
                    if (wHead != (int32_t)headBytes)
                    {
                        transferStep = 0;
                        fileSystem->closeFile(transferFile, false);
                        fileSystem->closeFile(transferFile2, false);
                        return fileTransferError;
                    }
                    batchWriteOffset += headBytes;
                    remaining -= headBytes;
                    chunk = (remaining > kAlignedWriteChunkSize)
                              ? kAlignedWriteChunkSize
                              : remaining;
                }
            }
            else if (chunk > kAlignedWriteChunkSize)
            {
                chunk = kAlignedWriteChunkSize;
            }

            int32_t written =
              fileSystem->writeFile(batchBufL + batchWriteOffset, chunk, transferFile2);
            if (written != (int32_t)chunk)
            {
                transferStep = 0;
                fileSystem->closeFile(transferFile, false);
                fileSystem->closeFile(transferFile2, false);
                return fileTransferError;
            }
            batchWriteOffset += chunk;
        }

        if (batchWriteOffset < batchFilled)
        {
            return fileTransferInProgress;
        }

        channelBytesFlushed += batchFilled;
        batchFilled = 0;
        batchWriteOffset = 0;

        if (sourceMemComplited < sourceMemTotal)
        {
            uint32_t filePosL = destDataOffset + channelBytesFlushed;
            batchBufL = batchBufLBase + (filePosL & 3u);
            transferStep = 1;
            return fileTransferInProgress;
        }
        transferStep = 3;
    }

    if (transferStep == 3)
    {
        transferStep = 0;
        fileSystem->endFile(transferFile2);
        fileSystem->closeFile(transferFile, false);
        fileSystem->closeFile(transferFile2, false);
        return fileTransferEnd;
    }

    transferStep = 0;
    fileSystem->closeFile(transferFile, false);
    fileSystem->closeFile(transferFile2, false);
    return fileTransferError;
}
