#include "Input.h"
#include "utility/dspinst.h"

AUDIO_CORE_FLASH void AudioInputStream::update()
{
#ifdef AUDIO_NULL_IS_SILENCE
    if (data == nullptr && silentWithoutData)
        return;
#endif
    audio_block_t* blockLeft = allocate();
    audio_block_t* blockRight = allocate();

    if (blockLeft == nullptr || blockRight == nullptr)
    {
        if (blockLeft)
        {
            release(blockLeft);
        }

        if (blockRight)
        {
            release(blockRight);
        }

        return;
    }

    if (data == nullptr)
    {
        for (int idx = 0; idx < AUDIO_BLOCK_SAMPLES; idx++)
        {
            blockLeft->data[idx] = 0;
            blockRight->data[idx] = 0;
        }
    }
    else
    {
#if defined(KINETISK)
        if ((reinterpret_cast<uintptr_t>(data) & 3) == 0)
        {
            // Two stereo frames per round: pack left samples and right samples into 32-bit words
            const uint32_t* inputWords = static_cast<const uint32_t*>(data);
            uint32_t* leftWords = reinterpret_cast<uint32_t*>(blockLeft->data);
            uint32_t* rightWords = reinterpret_cast<uint32_t*>(blockRight->data);
            for (int idx = 0; idx < AUDIO_BLOCK_SAMPLES / 2; idx++)
            {
                const uint32_t frame0 = inputWords[0];
                const uint32_t frame1 = inputWords[1];
                inputWords += 2;
                *leftWords++ = pack_16b_16b(frame1, frame0);
                *rightWords++ = pack_16t_16t(frame1, frame0);
            }
        }
        else
#endif
        {
            int16_t* inputData = (int16_t*)data;
            int16_t* leftData = blockLeft->data;
            int16_t* rightData = blockRight->data;

            for (int idx = 0; idx < AUDIO_BLOCK_SAMPLES; idx++)
            {
                *leftData++ = *inputData++;
                *rightData++ = *inputData++;
            }
        }
    }

    transmit(blockLeft, 0);
    transmit(blockRight, 1);

    release(blockLeft);
    release(blockRight);
}

AUDIO_CORE_FLASH void AudioInputStream::setInBuffer(void* data)
{
    AudioInputStream::data = data;
}
