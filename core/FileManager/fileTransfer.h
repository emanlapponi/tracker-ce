
#ifndef FILEMANAGER_FILETRANSFER_H_
#define FILEMANAGER_FILETRANSFER_H_

#include "AudioDataUtilities.h"
#include "FileManagerOperation.h"
#include "Platform.h"
#include "wavHeaderReader.h"

#include <limits>
#include <stdint.h>

enum enTranferFileStatus
{
    fileTransferIdle = 0,
    fileTransferInProgress,
    fileTransferEnd,
    fileTransferError,
    fileTransferFileNoExist,
    fileTransferFileNoValid,

};

enum enTranferFileMode
{
    fileWholeOnce = 0,
    fileDivIntoParts,
    fileDivIntoMaxParts,
};

#define READ_WRITE_BUFOR_SIZE 32640

extern "C"
{
    extern void* memcpy(void* dst, const void* src, size_t count);
}

const uint16_t memoryReadPerPart = 512;
// const uint16_t READ_WRITE_BUFOR_SIZE = 32768;

constexpr uint32_t defaultSampleRate = 44100;

class cFileTransfer
{
  public:
    void init(FileSystem* fileSystem);
    static bool copyFile(FileSystem* fileSystem, const char* srcFilepath, const char* dstFilepath);

    void clear();

    uint8_t getFileTransferStatus();
    uint8_t loadFileToMemory(const char* file,
                             uint8_t* memory,
                             uint32_t memSize,
                             uint32_t memOffset,
                             uint8_t mode);
    uint8_t loadFileToMemory(const char* file, uint8_t* memory, uint32_t memSize, uint8_t mode);
    uint8_t loadSampleToMemory(const char* file,
                               int16_t* memory,
                               uint32_t maxSampleCount,
                               uint32_t& outSampleCount);
    uint8_t saveMemoryToFile(uint8_t* memory, const char* file, uint32_t memSize);

    uint8_t saveMemoryToWav(int16_t* memoryChannelL,
                            int16_t* memoryChannelR,
                            uint32_t samplesCount,
                            const char* file);

    uint8_t loadSampleDataToMemory(FileManagerOperation::SampleLoadData* sampleImportData,
                                   int16_t* memoryAddress,
                                   uint32_t targetSampleRate);

    uint8_t convertWaveToInstrumentFile(FileManagerOperation::SampleLoadData* sampleImportData,
                                        const char* destFile,
                                        uint32_t destFileOffset,
                                        uint32_t targetSampleRate);

    uint8_t copyRawDataBetweenFiles(const char* srcFile,
                                    uint32_t srcOffset,
                                    const char* destFile,
                                    uint32_t destOffset,
                                    uint32_t sizeBytes);

    // load dla nowych instrumentow
    uint8_t loadInstrumentRawSampleDataToMemory(const char* file,
                                                uint32_t fileOffset,
                                                uint8_t* memory,
                                                uint32_t sizeLimitBytes,
                                                uint32_t& outSampleCount);

    // save dla nowych instrumentow
    uint8_t saveInstrumentStructToInstrumentFile(uint8_t* memory,
                                                 uint32_t memSize,
                                                 char* file,
                                                 uint32_t* outFileSize);

    uint8_t saveSampleRawDataToFile(
      uint8_t* memory,
      uint32_t memSize,
      const char* file,
      uint32_t fileOffset,
      uint8_t* extraMemory = nullptr); // extraMemory adding 2nd channel at the end

    // copy dla nowych instrumentów
    uint8_t copySampleToInstrumentFile(const char* src,
                                       const char* dest,
                                       uint16_t startSample,
                                       uint16_t endSample,
                                       uint32_t sampleRate,
                                       uint32_t fileOffset);

    uint8_t copyFile(const char* src, const char* dest);
    uint8_t copyInstrumentHeader(const char* src, const char* dest);
    uint8_t copySample(const char* src,
                       const char* dest,
                       uint16_t startSample = 0,
                       uint16_t endSample = std::numeric_limits<uint16_t>::max(),
                       uint32_t sampleRate = defaultSampleRate);

    void fillHeader(bool stereo, uint32_t size);

    uint8_t getFileProgress() { return (sourceMemComplited * 100) / sourceMemTotal; }
    uint32_t getBytesComplited() { return transferBytesComplited; }
    void clearBytesComplited() { transferBytesComplited = 0; }

    uint32_t getConvertedSampleSize() { return convertedDataSize; }
    uint8_t getTransferStep() { return transferStep; }
    void resetConvertedSampleSize() { convertedDataSize = 0; }
    void endTransfer();

  private:
    FileSystem* fileSystem = nullptr;

    uint8_t transferStep = 0;

    uint32_t memStep;
    uint32_t sourceMemComplited;
    uint32_t sourceMemTotal;
    uint32_t destSampleCount;
    uint32_t convertedDataSize;
    uint32_t destDataOffset;

    uint32_t transferBytesComplited;

    uint8_t* srcDataBuff;

    uint8_t conversionEnabled;
    uint8_t channelPass = 0;
    uint8_t* batchBufLBase = nullptr;
    uint8_t* batchBufRBase = nullptr;
    uint8_t* batchBufL = nullptr;
    uint8_t* batchBufR = nullptr;
    uint32_t batchCapacity = 0;
    uint32_t batchFilled = 0;
    uint32_t batchWriteOffset = 0;
    uint32_t channelBytesFlushed = 0;
    bool conversionInPlace;

    ConversionConfig conversionConfig;
};

extern cFileTransfer fileTransfer;

#endif /* FILEMANAGER_FILETRANSFER_H_ */
