/* Audio Library for Teensy 3.X
 * Copyright (c) 2014, Paul Stoffregen, paul@pjrc.com
 *
 * Development of this audio library was funded by PJRC.COM, LLC by sales of
 * Teensy and Audio Adaptor boards.  Please support PJRC's efforts to develop
 * open source software by purchasing Teensy or other PJRC products.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice, development funding notice, and this permission
 * notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "monoToStereo.h"
#include "dspApplyGain.h"
#include "dspApplyGainCopy.h"

AUDIO_CORE_FLASH void MonoToStereo::update(void)
{
    audio_block_t* in = receiveReadOnly(0);

    if (in == nullptr)
    {
#ifdef AUDIO_NULL_IS_SILENCE
        for (int channel = 0; channel < Channel::channelCount; channel++)
        {
            previous_multiplier[channel] = multiplier[channel];
        }
#endif
        return;
    }

    audio_block_t* out[Channel::channelCount] = { nullptr, nullptr };
    for (int channel = 0; channel < Channel::channelCount; channel++)
    {
        if (multiplier[channel] == MULTI_UNITYGAIN &&
            previous_multiplier[channel] == MULTI_UNITYGAIN)
            continue;
        out[channel] = allocate();
        if (out[channel] == nullptr)
        {
            if (out[Channel::left])
                release(out[Channel::left]);
            release(in);
            return;
        }
    }

    for (int channel = 0; channel < Channel::channelCount; channel++)
    {
        if (out[channel] == nullptr)
        {
            transmit(in, channel);
            continue;
        }
        gain_copy::applyGainCopy16<AUDIO_BLOCK_SAMPLES>(
          out[channel]->data, in->data, multiplier[channel], previous_multiplier[channel]);
        transmit(out[channel], channel);
        release(out[channel]);
    }
    release(in);
}
