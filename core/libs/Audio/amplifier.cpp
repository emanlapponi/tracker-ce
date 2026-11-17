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

#include "amplifier.h"
#include "dspApplyGain.h"
#include "dspApplyGainCopy.h"

AUDIO_CORE_RAM void AudioAmplifier::update(void)
{
    if (multiplier == MULTI_UNITYGAIN && previousMultiplier == MULTI_UNITYGAIN)
    {
        audio_block_t* block = receiveReadOnly(0);
        if (block)
        {
            transmit(block);
            release(block);
        }
        return;
    }

    audio_block_t* block = receiveReadOnly(0);
    if (block && block->ref_count > 1)
    {
        audio_block_t* out = allocate();
        if (out)
            gain_copy::applyGainCopy16<AUDIO_BLOCK_SAMPLES>(
              out->data, block->data, multiplier, previousMultiplier);
        release(block);
        block = out;
        if (block)
        {
            transmit(block);
            release(block);
        }
    }
    else if (block)
    {
        applyGain(block->data, multiplier, previousMultiplier);
        transmit(block);
        release(block);
    }
#ifdef AUDIO_NULL_IS_SILENCE
    if (!block)
    {
        previousMultiplier = multiplier;
    }
#endif
}
