#include "mux.h"
#include "Audio.h"
AUDIO_CORE_RAM void AudioMux::update(void)
{
    if (!muxEnabled)
    {
        for (uint8_t channel = 0; channel < MUX_SIZE; channel++)
        {
            if (audio_block_t* stale = receiveReadOnly(channel))
                release(stale);
        }
        if (!hasLaterSource)
            active = false;
        return;
    }

    audio_block_t* in = nullptr;

    for (uint8_t channel = 0; channel < MUX_SIZE; channel++)
    {
        in = receiveReadOnly(channel);
        if (in)
        {
            if (channel == outChannel)
                transmit(in);
            release(in);
            in = nullptr;
        }
    }
}

AUDIO_CORE_FLASH void AudioMux::setOut(uint8_t channel)
{
    if (channel >= MUX_SIZE)
        return;

    AudioNoInterrupts();
    outChannel = channel;
    AudioInterrupts();
}

AUDIO_CORE_FLASH void AudioMux::setEnabled(bool enabled)
{
    AudioNoInterrupts();
    muxEnabled = enabled;
    if (!enabled)
    {
        active = false;
        for (uint8_t channel = 0; channel < MUX_SIZE; channel++)
        {
            audio_block_t* stale = inputQueueArray[channel];
            if (stale != nullptr)
            {
                inputQueueArray[channel] = nullptr;
                release(stale);
            }
        }
    }
    else
    {
        active = numConnections > 0;
    }
    AudioInterrupts();
}
