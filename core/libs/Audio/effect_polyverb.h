// Copyright 2020 Wojciech Jakóbczyk (jakobczyk.woj@gmail.com)

#include "AudioStream.h"

#include "reverb.h"

class AudioEffectPolyverb : public AudioStream
{
  public:
    AudioEffectPolyverb();
    ~AudioEffectPolyverb();
    virtual void update(void);

    // Reset/clear buffers
    void reset();

    // Does reverb still produce sound
    bool isActive();

    // Set threshold of activity check (sample value)
    void setActiveThreshold(float threshold);

    // Wet/dry ratio, 1.0 full wet
    void setAmount(float amount) { reverb.SetAmount(amount); }

    // Decay time, 1.0 infinite
    void setTime(float time) { reverb.SetTime(time); }

    // High frequency damping, 0-1
    void setDamp(float damp) { reverb.SetLP(damp); }

    // Delay lines diffusion ratio, 0-1
    void SetDiffusion(float diffusion) { reverb.SetDiffusion(diffusion); }

    // Predelay length, 0-1
    void SetPredelayLength(float length) { reverb.SetPredelayLength(length); }

    void blockUpdate()
    {
        isBlockedUpdate = true;
        __atomic_signal_fence(__ATOMIC_SEQ_CST);
    }

    void unblockUpdate()
    {
        __atomic_signal_fence(__ATOMIC_SEQ_CST);
        isBlockedUpdate = false;
    }

  private:
    audio_block_t* inputQueueArray[2];
    Reverb reverb;
    uint16_t* buffer;
    uint16_t* buffer2;
    volatile bool isBlockedUpdate;
};
