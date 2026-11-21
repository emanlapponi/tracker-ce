#include "play_sd_wav.h"
#include "mtAudioUtil.h"
#include "spi_interrupt.h"

#include "SdStream.h"
#include "TrackerImpl.h"
#include "config.h"
#include "fileManagerDefs.h"
#include "mtStructs.h"
#include <cmath>

constexpr uint8_t STATE_STOP = 0;
constexpr uint8_t STATE_PLAY = 1;

void AudioPlaySdWav::begin(void)
{
    state = STATE_STOP;
    for (int channel = 0; channel < kMaxChannels; channel++)
    {
        if (block[channel] != NULL)
        {
            release(block[channel]);
            block[channel] = NULL;
        }
    }
}

void AudioPlaySdWav::setSdStream(SdStream* sdStream)
{
    AudioPlaySdWav::sdStream = sdStream;
}

AUDIO_CORE_FLASH bool AudioPlaySdWav::play(const char* filename, uint16_t from, uint16_t to)
{
    if (from >= to || from > MAX_16BIT_ || to > MAX_16BIT_)
    {
        return false;
    }

    stop();
    currentPosition = 0;

    if (!sdStream->startStream(
          filename, &wavHeader, from, to, &currentPosition, &endPosition, &fileType))
    {
        return false;
    }

    state = STATE_PLAY;

    return true;
}

AUDIO_CORE_FLASH void AudioPlaySdWav::stop(void)
{
    DISABLE_GLOBAL_INTERRUPTS();
    if (state == STATE_PLAY)
    {
        for (int channel = 0; channel < kMaxChannels; channel++)
        {
            if (block[channel] != NULL)
            {
                release(block[channel]);
                block[channel] = NULL;
            }
        }
        state = STATE_STOP;
        sdStream->endCurrentStream();
        ENABLE_GLOBAL_INTERRUPTS();
    }
    else
    {
        ENABLE_GLOBAL_INTERRUPTS();
    }
}

inline int16_t AudioPlaySdWav::getInputSample(int sampleIdx, uint32_t bytesPerSample, int channel)
{
    switch (wavHeader.AudioFormat)
    {
        case wavFormatPCM:
            switch (wavHeader.bitsPerSample)
            {
                case 8: return (((int16_t)buffer[sampleIdx * bytesPerSample + channel]) - 128) << 8;
                case 16: return *(int16_t*)(&buffer[sampleIdx * bytesPerSample + 2 * channel]);
                case 24: return *(int16_t*)(&buffer[sampleIdx * bytesPerSample] + 3 * channel + 1);
                case 32: return *(int16_t*)(&buffer[sampleIdx * bytesPerSample] + 4 * channel + 2);
                default: return 0; // unsupported format
            }
        case wavFormatFloat:
            return (int16_t)(*(float*)(&buffer[sampleIdx * bytesPerSample + 4 * channel]) *
                             INT16_MAX);
        default: return 0; // unsupported format
    }
}

// TODO extract to a separate class
// and reuse with cFileTransfer
#ifdef MONO_STREAM
uint32_t AudioPlaySdWav::convertAudioData(int16_t* outPtr, uint32_t outputSamples)
{
    uint32_t bytesPerSample = (wavHeader.bitsPerSample / 8) * wavHeader.numChannels;

    constexpr int32_t outputRate = 44100;
    uint32_t inputSamples = outputSamples * wavHeader.sampleRate / outputRate;

    for (uint32_t outIdx = 0; outIdx < outputSamples; outIdx++)
    {
        float inPos = (float)outIdx * wavHeader.sampleRate / outputRate;
        uint32_t inIdx = floorf(inPos);
        int16_t sample1 = inIdx < inputSamples ? getInputSample(inIdx, bytesPerSample)
                                               : getInputSample(inIdx - 1, bytesPerSample);
        int16_t sample0 = inIdx > 0 ? getInputSample(inIdx - 1, bytesPerSample) : sample1;
        int16_t sample2 =
          inIdx + 1 < inputSamples ? getInputSample(inIdx + 1, bytesPerSample) : sample1;
        int16_t sample3 =
          inIdx + 2 < inputSamples ? getInputSample(inIdx + 2, bytesPerSample) : sample1;

        outPtr[outIdx] = interpolateCubic(sample0, sample1, sample2, sample3, inPos - inIdx);
    }

    return outputSamples;
}
#else
AUDIO_CORE_FLASH uint32_t AudioPlaySdWav::convertAudioData(int16_t* outPtr[kMaxChannels],
                                                     uint32_t outputSamples)
{
    uint32_t bytesPerSample = (wavHeader.bitsPerSample / 8) * wavHeader.numChannels;

    constexpr int32_t outputRate = 44100;
    uint32_t inputSamples = outputSamples * wavHeader.sampleRate / outputRate;

    if (wavHeader.numChannels > kMaxChannels)
    {
        return 0;
    }

    float gapBetweenSamples =
      (float)wavHeader.numChannels * (float)wavHeader.sampleRate / (float)outputRate;

    for (int channel = 0; channel < wavHeader.numChannels; channel++)
    {
        for (uint32_t outIdx = 0; outIdx < outputSamples; outIdx++)
        {
            float inPos = (float)outIdx * wavHeader.sampleRate / outputRate;
            uint32_t inIdx = floorf(inPos);
            int16_t sample1 = inIdx < inputSamples
                                ? getInputSample(inIdx, bytesPerSample, channel)
                                : getInputSample(inIdx - 1, bytesPerSample, channel);
            int16_t sample0 =
              inIdx > 0 ? getInputSample(inIdx - 1, bytesPerSample, channel) : sample1;
            int16_t sample2 = inIdx + 1 < inputSamples
                                ? getInputSample(inIdx + 1, bytesPerSample, channel)
                                : sample1;
            int16_t sample3 = inIdx + 2 < inputSamples
                                ? getInputSample(inIdx + 2, bytesPerSample, channel)
                                : sample1;

            inPos = (float)outIdx * wavHeader.sampleRate / outputRate;
            inIdx = floorf(inPos);

            outPtr[channel][outIdx] =
              interpolateCubic(sample0, sample1, sample2, sample3, inPos - inIdx);
        }
    }

    if (wavHeader.numChannels < kMaxChannels)
    {
        // fill all channels from channel 0
        int kChannelToCopyFrom = 0;
        for (int channel = wavHeader.numChannels; channel < kMaxChannels; channel++)
        {
            memcpy(outPtr[channel], outPtr[kChannelToCopyFrom], outputSamples * sizeof(int16_t));
        }
    }

    return outputSamples;
}
#endif

AUDIO_CORE_FLASH void AudioPlaySdWav::update(void)
{
    if (state == STATE_STOP)
    {
        return;
    }
#ifdef MONO_STREAM
    if (fileType == 1) // pti
    {
        block[0] = allocate();

        if (block[0] == NULL)
        {
            return;
        }

        if (sdStream->isStreamActive() && (currentPosition < endPosition))
        {
            uint32_t length = sdStream->readFromStream(AUDIO_BLOCK_SAMPLES * 2, block[0]->data);
            if (length == 0)
            {
                stop();
                return;
            }
            currentPosition += length;
        }
        else
        {
            stop();
            return;
        }
    }
#else
    if (0)
    {
    }
#endif
    else // wav
    {
        constexpr int32_t outputRate = 44100;
        constexpr int32_t targetSamples = 128;
        int32_t sourceSamples = targetSamples * wavHeader.sampleRate / outputRate;

        uint32_t bytesPerSample = (wavHeader.bitsPerSample / 8) * wavHeader.numChannels;
        if (sourceSamples * bytesPerSample > sizeof(buffer))
        {
            // buffer is too small to play this sample, let's not play it at all
            return;
        }

        for (int channel = 0; channel < kMaxChannels; channel++)
        {
            block[channel] = allocate();
            if (block[channel] == NULL)
            {
                for (int channelToRelease = 0; channelToRelease < channel; channelToRelease++)
                {
                    release(block[channelToRelease]);
                    block[channelToRelease] = NULL;
                }
                return;
            }
        }

        if (sdStream->isStreamActive() && (currentPosition < endPosition))
        {
            uint32_t localLength = sdStream->readFromStream(sourceSamples * bytesPerSample, buffer);
            if (localLength == 0)
            {
                stop();
                return;
            }

            buffer_length = localLength / bytesPerSample;
            currentPosition += localLength;
            if (endPosition < currentPosition)
            {
                buffer_length -= (currentPosition - endPosition) / bytesPerSample;
            }
            if (buffer_length < sourceSamples)
            {
                memset(buffer + buffer_length * bytesPerSample,
                       0,
                       (sourceSamples - buffer_length) * bytesPerSample);
                stop(); // cuts last partial frame but avoids click sometimes
                return;
            }
        }
        else
        {
            stop();
            return;
        }
#ifdef MONO_STREAM
        convertAudioData(block[0]->data, targetSamples);
#else

        // This runs in the audio interrupt, where the heap must not be used.
        int16_t* data[kMaxChannels];
        for (int channel = 0; channel < kMaxChannels; channel++)
        {
            data[channel] = block[channel]->data;
        }

        convertAudioData(data, targetSamples);
#endif
    }

    for (int channel = 0; channel < kOutputChannels; channel++)
    {
        if (channel >= kMaxChannels)
        {
            if (block[kCommonChannelIdx] != NULL)
            {
                transmit(block[kCommonChannelIdx], channel);
            }
        }
        else if (block[channel] != NULL)
        {
            transmit(block[channel], channel);
        }
    }

    for (int channel = 0; channel < kMaxChannels; channel++)
    {
        if (block[channel] != NULL)
        {
            release(block[channel]);
            block[channel] = NULL;
        }
    }
}
