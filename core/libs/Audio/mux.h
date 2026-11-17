#ifndef audio_mux_h_
#define audio_mux_h_

#include "AudioStream.h"

#ifdef FEATURE_SYNTH
constexpr uint8_t MUX_SIZE = 10 + kSynthParts;
#else
constexpr uint8_t MUX_SIZE = 10;
#endif

class AudioMux : public AudioStream
{
  public:
    AudioMux(void)
      : AudioStream(MUX_SIZE, inputQueueArray)
    {
    }
    virtual void update(void);
    void setOut(uint8_t channel);
    void setEnabled(bool enabled);

  private:
    uint8_t outChannel = 0;
    bool muxEnabled = true;
    audio_block_t* inputQueueArray[MUX_SIZE];
};

#endif
