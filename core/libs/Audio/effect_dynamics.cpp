/* Audio Library for Teensy 3.X
 * Dynamics Processor (Gate, Compressor & Limiter)
 * Copyright (c) 2017, Marc Paquette (marc@dacsystemes.com)
 * Based on analyse_rms & mixer objects by Paul Stoffregen
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

#include "effect_dynamics.h"
#include "dspApplyGain.h"
#include "fast_log.h"
#include "utility/dspinst.h"
#include "utility/sqrt_integer.h"

AUDIO_CORE_FLASH static float analyse_rms(int16_t* data)
{

    uint32_t* p = (uint32_t*)data;
    const uint32_t* end = p + AUDIO_BLOCK_SAMPLES / 2;
    int64_t sum = 0;
    do
    {
        uint32_t n1 = *p++;
        uint32_t n2 = *p++;
        uint32_t n3 = *p++;
        uint32_t n4 = *p++;
        sum = multiply_accumulate_16tx16t_add_16bx16b(sum, n1, n1);
        sum = multiply_accumulate_16tx16t_add_16bx16b(sum, n2, n2);
        sum = multiply_accumulate_16tx16t_add_16bx16b(sum, n3, n3);
        sum = multiply_accumulate_16tx16t_add_16bx16b(sum, n4, n4);

    } while (p < end);
    if (sum == 0)
        return 0;
    int32_t meansq = sum / AUDIO_BLOCK_SAMPLES;
    return sqrt_uint32(meansq) / 32767.0f;
}

AUDIO_CORE_FLASH void AudioEffectDynamics::update(void)
{

    audio_block_t* block;

#ifdef AUDIO_NULL_IS_SILENCE
    const bool unread = outputIsUnread();
    block = unread ? receiveReadOnly(0) : receiveWritable(0);
#else
    block = receiveWritable(0);
#endif

    if (!block)
        return;

    if (!gateEnabled && !compEnabled && !limiterEnabled)
    {
#ifdef AUDIO_NULL_IS_SILENCE
        if (unread)
        {
            release(block);
            return;
        }
#endif

        // Transmit & release
        transmit(block);
        release(block);
        return;
    }

    // Analyze received block
    float rms = analyse_rms(block->data);

    // Compute block RMS level in Db
    float inputdb = MIN_DB;
    if (rms > 0)
        inputdb = unitToDb(rms);

    // Gate
    if (gateEnabled)
    {
        if (inputdb >= gateThresholdOpen)
            gatedb = (aGateAttack * gatedb) + (aOneMinusGateAttack * MAX_DB);
        else if (inputdb < gateThresholdClose)
            gatedb = (aGateRelease * gatedb) + (aOneMinusGateRelease * MIN_DB);
    }
    else
        gatedb = MAX_DB;

    // Compressor
    if (compEnabled)
    {
        float attdb = MAX_DB; // Below knee
        if (inputdb >= aLowKnee)
        {
            if (inputdb <= aHighKnee)
            {
                // Knee transition
                float knee = inputdb - aLowKnee;
                attdb = aKneeRatio * knee * knee * aTwoKneeWidth;
            }
            else
            {
                // Above knee
                attdb = compThreshold + ((inputdb - compThreshold) * compRatio) - inputdb;
            }
        }
        if (attdb <= compdb)
            compdb = (aCompAttack * compdb) + (aOneMinusCompAttack * attdb);
        else
            compdb = (aCompRelease * compdb) + (aOneMinusCompRelease * attdb);
    }
    else
        compdb = MAX_DB;

    // Brickwall Limiter
    if (limiterEnabled)
    {
        float outdb = inputdb + compdb + makeupdb;
        if (outdb >= limitThreshold)
            limitdb = (aLimitAttack * limitdb) + (aOneMinusLimitAttack * (limitThreshold - outdb));
        else
            limitdb *= aLimitRelease;
    }
    else
        limitdb = MAX_DB;

    // Compute linear gain
    float totalGain = gatedb + compdb + makeupdb + limitdb;
#ifdef AUDIO_NULL_IS_SILENCE
    if (unread)
    {
        pendingGain = totalGain;
        lastMultPending = true;
        release(block);
        return;
    }
    if (lastMultPending)
    {
        int32_t pendingMult = dbToUnit(pendingGain) * 65536.0f;
        last_mult = pendingMult;
        lastMultPending = false;
    }
#endif
    int32_t mult = dbToUnit(totalGain) * 65536.0f;

    // Apply gain to block
    applyGain(block->data, mult, last_mult);
    last_mult = mult;

    // Transmit & release
    transmit(block);
    release(block);
}
