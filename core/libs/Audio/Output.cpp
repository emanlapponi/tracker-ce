#include "Output.h"

void AudioOutputStream::begin(void)
{
}

AUDIO_CORE_RAM void AudioOutputStream::update()
{
    if (outBuffers[0] == nullptr)
    {
        return;
    }

    const int leftIdx = kOutChannels * source[0];
    const int rightIdx = kOutChannels * source[1] + 1;

    audio_block_t* blockLeft = receiveReadOnly(leftIdx);   // input 0 = left channel
    audio_block_t* blockRight = receiveReadOnly(rightIdx); // input 1 = right channel

    for (int ch = 0; ch < kOutChannels * kSources; ch++)
    {
        if (ch == leftIdx || ch == rightIdx)
            continue;
        if (audio_block_t* stale = receiveReadOnly(ch))
            release(stale);
    }

    if (blockLeft)
    {
        memcpy(outBuffers[0], blockLeft->data, sizeof(blockLeft->data));
    }
    else
    {
        memset(outBuffers[0], 0, sizeof(blockLeft->data));
    }
    if (blockRight)
    {
        memcpy(outBuffers[1], blockRight->data, sizeof(blockRight->data));
    }
    else
    {
        memset(outBuffers[1], 0, sizeof(blockLeft->data));
    }

    if (blockLeft)
        release(blockLeft);
    if (blockRight)
        release(blockRight);
}

void AudioOutputStream::selectSource(int channel, int idx)
{
    assert(channel < kOutChannels);
    assert(idx < kSources);

    source[channel] = idx;
}

AUDIO_CORE_FLASH void AudioOutputStream::setOutBuffers(void* left, void* right)
{
    outBuffers[0] = left;
    outBuffers[1] = right;
}
