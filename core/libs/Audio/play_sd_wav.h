#ifndef play_sd_wav_h_
#define play_sd_wav_h_

#include "AudioStream.h"
#include "wavHeaderReader.h"

constexpr uint16_t MAX_16BIT_ = 65535;

class SdStream;

#ifndef PLATFORM_IMX
#define MONO_STREAM
#endif

class AudioPlaySdWav : public AudioStream
{
  public:
    AudioPlaySdWav(void)
      : AudioStream(0, NULL)
    {
        begin();
    }

    void begin(void);
    void setSdStream(SdStream* sdStream);
    // bool play(const char *filename);
    bool play(const char* filename, uint16_t from = 0, uint16_t to = MAX_16BIT_);
    void stop(void);
    virtual void update(void);

  private:
    static constexpr int kOutputChannels = 2;
    static constexpr int kCommonChannelIdx = 0;
#ifdef MONO_STREAM
    static constexpr int kMaxChannels = 1;
#else
    static constexpr int kMaxChannels = 2;
#endif
    SdStream* sdStream;
    strWavFileHeader wavHeader;
    uint8_t fileType = 0;

    uint32_t currentPosition = 0;
    uint32_t endPosition = 0;
    audio_block_t* block[kMaxChannels];
    // buffer one block of data, larger to hold data before resampling to target sample rate
    // 128 samples (in update) * 32 bits per sample * 2 channels * max rate / target rate
    uint8_t buffer[(128 * 4 * 2 * 96000 / 44100) + 1];
    uint16_t buffer_length;
    uint8_t state;
#ifdef MONO_STREAM
    uint32_t convertAudioData(int16_t* outPtr, uint32_t outputSamples);
#else
    uint32_t convertAudioData(int16_t* outPtr[kMaxChannels], uint32_t outputSamples);
#endif
    int16_t getInputSample(int sampleIdx, uint32_t bytesPerSample, int channel = 0);
};

#endif
