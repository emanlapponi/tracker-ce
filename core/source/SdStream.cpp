#include "SdStream.h"
#include "fileManagerDefs.h"
#include "instrumentFileValidation.h"
#include "interruptsControl.h"
#include "log.h"
#include "string.h"
#include "wavHeaderReader.h"
#include <algorithm>
#include <cstring>
#include <limits>

void SdStream::init(FileSystem* fileSystem, void* buffer, int bufferSize)
{
    assert(bufferSize % (kWriteBuffers + kReadBuffers + kHelpingBuffers) == 0);

    uint8_t* wholeBuffer = (uint8_t*)buffer;
    SdStream::fileSystem = fileSystem;
    SdStream::bufferSize = bufferSize / (kWriteBuffers + kReadBuffers + kHelpingBuffers);

    readBuffer.address = &wholeBuffer[kWriteBuffers * SdStream::bufferSize];
    readBuffer.isFilled = false;
    readBuffer.storedDataSize = 0;

    for (int idx = 0; idx < kWriteBuffers; idx++)
    {
        writeBuffers[idx].address = &wholeBuffer[idx * SdStream::bufferSize];
        writeBuffers[idx].isFilled = false;
        writeBuffers[idx].storedDataSize = 0;
    }

    helpingBuffer.address = &wholeBuffer[(kWriteBuffers + kReadBuffers) * SdStream::bufferSize];
}

bool SdStream::startStream(const char* path,
                           strWavFileHeader* header,
                           uint16_t from,
                           uint16_t to,
                           uint32_t* currentPosition,
                           uint32_t* endPosition,
                           uint8_t* fileType)
{
    auto [isOpen, file] = fileSystem->openFile(path, FileSystem::FileOpenMode::kRead, &wavfile);

    if (!isOpen || file == nullptr)
    {
        return false;
    }

    isFileOpened = true;

    if (!handleFileDecapsulation(path, header, from, to, currentPosition, endPosition, fileType))
    {
        return false;
    }

    fillBuffer(true);
    if (!isFileOpened)
    {
        return false;
    }
    switchBuffer();

    isActive = true;

    return true;
}

void SdStream::endCurrentStream()
{
    if (fileSystem->isFileOpen(wavfile))
    {
        fileSystem->closeFile(wavfile, false);
    }

    for (int idx = 0; idx < kWriteBuffers; idx++)
    {
        writeBuffers[idx].isFilled = false;
        writeBuffers[idx].storedDataSize = 0;
    }
    isActive = false;
    isFileOpened = false;
}

void SdStream::update()
{
    if (isAnyEmptyBuffer() && isActive)
    {
        fillBuffer();
    }

    if (isBufferRead() && writeBuffers[kNextReadBufferIdx].isFilled)
    {
        switchBuffer();
    }
}

int SdStream::readFromStream(int bytes, void* buffer)
{
    if (isBufferRead())
    {
        if (writeBuffers[kNextReadBufferIdx].isFilled)
        {
            switchBuffer();
        }
        else
        {
            return 0;
        }
    }

    int bytesRemain = (readBuffer.storedDataSize - readIdx);
    int bytesToRead = std::min(bytesRemain, bytes);

    if (bytesToRead <= 0)
    {
        return 0;
    }

    bool willBeBufferFullyRead = (bytesRemain < bytes) && writeBuffers[kNextReadBufferIdx].isFilled;

    memcpy(buffer, &readBuffer.address[readIdx], bytesToRead);
    readIdx += bytesToRead;

    int bytesToRead2 = 0;

    if (willBeBufferFullyRead)
    {
        switchBuffer();

        int lackBytes = bytes - bytesToRead;
        bytesRemain = (readBuffer.storedDataSize - readIdx);
        bytesToRead2 = std::min(bytesRemain, lackBytes);

        uint8_t* dstBuffer = (uint8_t*)buffer;

        memcpy(&dstBuffer[bytesToRead], &readBuffer.address[readIdx], bytesToRead2);
        readIdx += bytesToRead2;
    }

    if (isBufferRead() && !isFileOpened && !writeBuffers[kNextReadBufferIdx].isFilled)
    {
        isActive = false;
    }

    return bytesToRead + bytesToRead2;
}

bool SdStream::isStreamActive()
{
    return isActive;
}

bool SdStream::handleFileDecapsulation(const char* path,
                                       strWavFileHeader* header,
                                       uint16_t from,
                                       uint16_t to,
                                       uint32_t* currentPosition,
                                       uint32_t* endPosition,
                                       uint8_t* fileType)
{
    uint32_t pathLength = strlen(path);

    if (((path[pathLength - 1] == 'V') || (path[pathLength - 1] == 'v')) &&
        ((path[pathLength - 2] == 'A') || (path[pathLength - 2] == 'a')) &&
        ((path[pathLength - 3] == 'W') || (path[pathLength - 3] == 'w')) &&
        (path[pathLength - 4] == '.'))
    {
        if (!readHeader(header, wavfile))
        {
            return false;
        }

        if (header->chunkId == 0x46464952 && header->waveId == WAVEID &&
            header->subchunk1Id == 0x20746D66)
        {
            uint32_t bytesPerSample = (header->bitsPerSample / 8) * header->numChannels;
            if (bytesPerSample == 0)
            {
                return false;
            }

            uint32_t totalSamplesCount = header->subchunk2Size / bytesPerSample;
            float from_pos = (float)from / std::numeric_limits<uint16_t>::max();

            *currentPosition = ((uint32_t)(totalSamplesCount * from_pos)) * bytesPerSample;
            if (!fileSystem->seekFileCur(*currentPosition, wavfile))
            {
                return false;
            }

            float to_pos = (float)to / std::numeric_limits<uint16_t>::max();
            *endPosition = ((uint32_t)(totalSamplesCount * to_pos)) * bytesPerSample;

            *fileType = 0;

            dataLength = totalSamplesCount;
            isSourceInterleaved = true;
            return true;
        }
    }
    else if (((path[pathLength - 1] == 'i') || (path[pathLength - 1] == 'I')) &&
             ((path[pathLength - 2] == 't') || (path[pathLength - 2] == 'T')) &&
             ((path[pathLength - 3] == 'p') || (path[pathLength - 3] == 'P')) &&
             (path[pathLength - 4] == '.'))
    {
        uint32_t fileSize = fileSystem->getFileSize(wavfile);
        strInstrumentFile instrFile;
        memset(&instrFile, 0, sizeof(instrFile));

        int32_t bytesRead = fileSystem->readFile(&instrFile, sizeof(instrFile), wavfile);

        InstrumentFileValidation::Result validation;

        if (bytesRead >= 0 &&
            InstrumentFileValidation::validate((const uint8_t*)&instrFile,
                                               bytesRead,
                                               fileSize,
                                               sizeof(strInstrument),
                                               INSTRUMENT_FILE_VERSION,
                                               validation))
        {
            strInstrument& instrumentData = instrFile.instrumentDataAndHeader.instrument;

            uint32_t instrumentSampleDataOffset = validation.dataOffset;

            *currentPosition = instrumentSampleDataOffset;

            fileSystem->seekFile(*currentPosition, wavfile);

            *endPosition = fileSize - *currentPosition;

            *fileType = 1;

            uint32_t instrumentFileSize = fileSystem->getFileSize(wavfile);
            uint32_t instrumentSampleDataSize = instrumentFileSize - instrumentSampleDataOffset;

            // check if contains audio data
            if (instrumentFileSize <= instrumentSampleDataOffset)
            {
                return false;
            }

            dataLength = instrumentData.sample.length;
            int channels =
              instrumentSampleDataSize / (instrumentData.sample.length * sizeof(int16_t));
            // instruments with length != sample_data_size exception handling
            if (instrumentData.sample.length == 0 ||
                (instrumentData.sample.length * 2 != instrumentSampleDataSize &&
                 instrumentData.sample.length * 4 != instrumentSampleDataSize))
            {
                channels = 1;
            }

            isSourceInterleaved = channels == 1;

            fillWavHeaderForInstrumentFile(header, instrumentData.sample.length, channels);
            return true;
        }
    }

    fileSystem->closeFile(wavfile, false);
    isFileOpened = false;
    return false;
}

void SdStream::fillWavHeaderForInstrumentFile(strWavFileHeader* header, size_t length, int channels)
{
    header->chunkId = 0x46464952; // "RIFF"
    header->chunkSize = length * sizeof(int16_t) * channels + 36;
    header->waveId = WAVEID;
    header->subchunk1Id = 0x20746d66; // "fmt "
    header->subchunk1Size = 16;
    header->AudioFormat = 1;
    header->numChannels = channels;
    header->sampleRate = 44100;
    header->bitsPerSample = 16;
    header->byteRate = header->sampleRate * header->numChannels * (header->bitsPerSample / 8);
    header->blockAlign = header->numChannels * (header->bitsPerSample / 8);
    header->subchunk2Id = 0x61746164; // "data"
    header->subchunk2Size = length * sizeof(int16_t) * channels;
}

void SdStream::makeBufferInterleaved(uint8_t*& buffer, int channelDataLength)
{
    int16_t* left = (int16_t*)buffer;
    int16_t* right = (int16_t*)(buffer + (bufferSize / 2));
    int16_t* dst = (int16_t*)helpingBuffer.address;
    for (int idx = 0; idx < channelDataLength; idx++)
    {
        *dst++ = *left++;
        *dst++ = *right++;
    }
    std::swap(buffer, helpingBuffer.address);
}

void SdStream::fillBuffer(bool align)
{
    if (!isFileOpened)
    {
        return;
    }

    assert(isAnyEmptyBuffer());
    uint32_t remainDataInFile = 0;
    uint32_t filePos = 0;

    if (align && isSourceInterleaved)
    {
        remainDataInFile = fileSystem->getFileAvailable(wavfile);
        filePos = fileSystem->getFileSize(wavfile) - remainDataInFile;
    }

    int bufferIdx = getFirstEmptyBufferIdx();
    bool readFailed = false;

    if (isSourceInterleaved)
    {
        writeBuffers[bufferIdx].storedDataSize = fileSystem->readFile(
          writeBuffers[bufferIdx].address, bufferSize - filePos % bufferSize, wavfile);
        readFailed = writeBuffers[bufferIdx].storedDataSize < 0;
    }
    else
    {
        int halfBufferSize = bufferSize / 2;
        uint8_t* leftAddress = writeBuffers[bufferIdx].address;
        uint8_t* rightAddress = leftAddress + halfBufferSize;

        fileSystem->seekFileCur(dataLength * sizeof(int16_t), wavfile);

        // right buffer is first to detect the file end
        int32_t rightChannelStoredData =
          fileSystem->readFile(rightAddress, halfBufferSize, wavfile);
        readFailed = rightChannelStoredData < 0;

        if (!readFailed && rightChannelStoredData > 0)
        {
            fileSystem->seekFileCur(
              -(dataLength * sizeof(int16_t) + rightChannelStoredData), wavfile);

            // read from left channel the result bytes from right channel (end file detection) to
            // avoid exceeding left channel area
            int32_t leftChannelStoredData =
              fileSystem->readFile(leftAddress, rightChannelStoredData, wavfile);
            readFailed = leftChannelStoredData < 0;

            if (!readFailed)
            {
                writeBuffers[bufferIdx].storedDataSize =
                  rightChannelStoredData + leftChannelStoredData;

                assert(writeBuffers[bufferIdx].storedDataSize == rightChannelStoredData * 2);

                makeBufferInterleaved(writeBuffers[bufferIdx].address,
                                      writeBuffers[bufferIdx].storedDataSize / 4);
            }
        }
        else
        {
            writeBuffers[bufferIdx].storedDataSize = 0;
        }
    }

    if (readFailed)
    {
        writeBuffers[bufferIdx].storedDataSize = 0;
    }

    if (readFailed || (writeBuffers[bufferIdx].storedDataSize < bufferSize && !align))
    {
        fileSystem->closeFile(wavfile, false);
        isFileOpened = false;
    }

    writeBuffers[bufferIdx].isFilled = true;
}

void SdStream::switchBuffer()
{
    DISABLE_AUDIO_INTERRUPTS();
    readIdx = 0;
    writeBuffers[kNextReadBufferIdx].isFilled = false;

    Buffer temp = readBuffer;
    readBuffer = writeBuffers[kNextReadBufferIdx];

    memmove(&writeBuffers[0], &writeBuffers[1], sizeof(Buffer) * (kWriteBuffers - 1));
    writeBuffers[kWriteBuffers - 1] = temp;

    ENABLE_AUDIO_INTERRUPTS();
}

bool SdStream::isAnyEmptyBuffer()
{
    for (int idx = 0; idx < kWriteBuffers; idx++)
    {
        if (!writeBuffers[idx].isFilled)
        {
            return true;
        }
    }

    return false;
}

int SdStream::getFirstEmptyBufferIdx()
{
    assert(isAnyEmptyBuffer());

    auto buffer = std::find_if(std::begin(writeBuffers),
                               std::end(writeBuffers),
                               [](Buffer& buffer) { return !buffer.isFilled; });

    return std::distance(writeBuffers, buffer);
}

bool SdStream::isBufferRead()
{
    return (readIdx == readBuffer.storedDataSize) && isActive;
}
