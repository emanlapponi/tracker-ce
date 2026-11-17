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

#include "filter_variable.h"
#include "utility/dspinst.h"
#include <cmath>
#include <cr_section_macros.h>
#include <stdint.h>

// State Variable Filter (Chamberlin) with 2X oversampling
// http://www.musicdsp.org/showArchiveComment.php?ArchiveID=92

// The fast 32x32 with rshift32 discards 2 bits, which probably
// never matter.
//#define MULT(a, b) (int32_t)(((int64_t)(a) * (b)) >> 30)
#define MULT(a, b) (multiply_32x32_rshift32_rounded(a, b) << 2)

// It's very unlikely anyone could hear any difference, but if you
// care deeply about numerical precision in seldom-used cases,
// uncomment this to improve the control signal accuracy
//#define IMPROVE_HIGH_FREQUENCY_ACCURACY

// This increases the exponential approximation accuracy from
// about 0.341% error to only 0.012% error, which probably makes
// no audible difference.

bool AudioFilterStateVariable::isCutoffInitialize = false;
float AudioFilterStateVariable::filterFreq[kFrequencyLookupSize];
int32_t AudioFilterStateVariable::filterFmult[kFrequencyLookupSize];

AUDIO_CORE_FLASH void AudioFilterStateVariable::initCutoffLookup()
{
    for (int idx = 0; idx < kFrequencyLookupSize; idx++)
    {
        float freq = 20.0f * powf(2.0f, idx / 50.0f);
        filterFreq[idx] = freq;
        float clampedFreq = clamp<float>(freq, 20.0f, AUDIO_SAMPLE_RATE_EXACT / 2.5f);
        filterFmult[idx] =
          2.0f *
          sinf(clampedFreq * (3.141592654f / (AUDIO_SAMPLE_RATE_EXACT * kOversamplingMult))) *
          kUnityValue;
    }

    isCutoffInitialize = true;
}

AUDIO_CORE_RAM void AudioFilterStateVariable::update_overdrive(const int16_t* in, int16_t* out)
{
    const int16_t* end = in + AUDIO_BLOCK_SAMPLES;

    if (overdriveGain != 0)
    {
        do
        {
            const int32_t in_val = *in++ * overdriveGain / kOverdriveGainMult;
            const int32_t in_abs = in_val >= 0 ? in_val : -in_val;
            const int32_t in_sign = in_val >= 0 ? INT16_MAX : INT16_MIN;
            int16_t out_val;

            if (in_abs > (int32_t)INT16_MAX / 2)
            {
                // y = 1
                out_val = in_sign;
            }
            else if (in_abs > INT16_MAX / 4)
            {
                // y = (4 - (2 - 4x)^2) / 4
                // exponent of 3 gives a curve better aligned to 3x, but at a cost of an additional
                // mult
                int32_t temp = 2 * (int32_t)INT16_MAX - 4 * (int32_t)in_abs;
                out_val = (4 * (int32_t)INT16_MAX - temp * temp / 0x8000) / 4;
                if (in_sign < 0)
                {
                    out_val = -out_val;
                }
            }
            else
            {
                // y = 3x
                out_val = 3 * in_val;
            }

            *out++ = out_val;
        } while (in < end);
    }
}

AUDIO_CORE_FLASH void AudioFilterStateVariable::stabilize()
{
    static constexpr int kStabilizeBufferSize = 8;
    static int16_t buffer[kStabilizeBufferSize];

    lastupdate_fmult = setting_fmult;
    lastupdate_damp = setting_damp;

    state_lowpass = 0;
    state_bandpass = 0;
    for (int sample = 0; sample < kStabilizeBufferSize; sample++)
    {
        buffer[sample] = state_inputprev >> kInputShift;
    }
    update_fixed(buffer, buffer, kStabilizeBufferSize);
}

AUDIO_CORE_RAM void AudioFilterStateVariable::update_fixed(const int16_t* in, int16_t* out, int samples)
{
    const int16_t* end = in + samples;
    int32_t input, inputprev;
    int32_t lowpass, bandpass, highpass;
    int32_t lowpasstmp, bandpasstmp, highpasstmp;
    int32_t fmult, damp;
    int32_t delta_fmult, delta_damp;

    fmult = lastupdate_fmult;
    damp = lastupdate_damp;
    delta_fmult = (setting_fmult - fmult) / samples;
    delta_damp = (setting_damp - damp) / samples;
    inputprev = state_inputprev;
    lowpass = state_lowpass;
    bandpass = state_bandpass;

    const uint8_t type = filterType;

    do
    {
        input = (*in++) << kInputShift;
        lowpass = lowpass + MULT(fmult, bandpass);
        highpass = ((input + inputprev) >> 1) - lowpass - MULT(damp, bandpass);
        inputprev = input;
        bandpass = bandpass + MULT(fmult, highpass);
        lowpasstmp = lowpass;
        bandpasstmp = bandpass;
        highpasstmp = highpass;
        lowpass = lowpass + MULT(fmult, bandpass);
        highpass = input - lowpass - MULT(damp, bandpass);
        bandpass = bandpass + MULT(fmult, highpass);

        int32_t passSum;
        if (type == kFilterTypeLowPass)
        {
            passSum = lowpass + lowpasstmp;
        }
        else if (type == kFilterTypeBandPass)
        {
            passSum = bandpass + bandpasstmp;
        }
        else if (type == kFilterTypeHighPass)
        {
            passSum = highpass + highpasstmp;
        }
        else
        {
            passSum = 0;
        }

        *out++ = signed_saturate_rshift(passSum, 16, kInputShift + 1);

        fmult += delta_fmult;
        damp += delta_damp;
    } while (in < end);
    state_inputprev = inputprev;
    state_lowpass = lowpass;
    state_bandpass = bandpass;
    lastupdate_fmult = setting_fmult;
    lastupdate_damp = setting_damp;
}

#ifdef AUDIO_NULL_IS_SILENCE
AUDIO_CORE_RAM bool AudioFilterStateVariable::isSettledOnSilence() const
{
    if (lastupdate_fmult != setting_fmult || lastupdate_damp != setting_damp ||
        state_inputprev != 0)
    {
        return false;
    }
    constexpr int32_t kSubLsbStateLimit = 1 << kInputShift;
    constexpr int32_t kSubLsbPassLimit = 1 << (kInputShift + 1);
    if (state_lowpass > -kSubLsbStateLimit && state_lowpass < kSubLsbStateLimit &&
        state_bandpass > -kSubLsbStateLimit && state_bandpass < kSubLsbStateLimit)
    {
        return true;
    }

    const int32_t fmult = setting_fmult;
    const int32_t damp = setting_damp;
    int32_t lowpass = state_lowpass;
    int32_t bandpass = state_bandpass;

    lowpass = lowpass + MULT(fmult, bandpass);
    int32_t highpass = ((0 + state_inputprev) >> 1) - lowpass - MULT(damp, bandpass);
    bandpass = bandpass + MULT(fmult, highpass);
    const int32_t lowpasstmp = lowpass;
    const int32_t bandpasstmp = bandpass;
    const int32_t highpasstmp = highpass;
    lowpass = lowpass + MULT(fmult, bandpass);
    highpass = 0 - lowpass - MULT(damp, bandpass);
    bandpass = bandpass + MULT(fmult, highpass);

    int32_t passSum;
    if (filterType == kFilterTypeLowPass)
    {
        passSum = lowpass + lowpasstmp;
    }
    else if (filterType == kFilterTypeBandPass)
    {
        passSum = bandpass + bandpasstmp;
    }
    else if (filterType == kFilterTypeHighPass)
    {
        passSum = highpass + highpasstmp;
    }
    else
    {
        passSum = 0;
    }
    return lowpass == state_lowpass && bandpass == state_bandpass &&
           passSum > -kSubLsbPassLimit && passSum < kSubLsbPassLimit;
}
#endif

AUDIO_CORE_RAM void AudioFilterStateVariable::update(void)
{
    audio_block_t* input_block = nullptr;
    audio_block_t* out_block = nullptr;
    audio_block_t* filter_block = nullptr;

    input_block = receiveReadOnly(0);
    if (nullptr == input_block)
    {
#ifdef AUDIO_NULL_IS_SILENCE
        if (isEnabled == lastEnabled && filterType == lastFilterType)
        {
            if (!isEnabled)
            {
                if (overdriveGain == 0)
                {
                    state_inputprev = 0;
                }
                state_outputprev = 0;
                return;
            }
            if (state_inputprev == 0 && state_lowpass == 0 && state_bandpass == 0)
            {
                lastupdate_fmult = setting_fmult;
                lastupdate_damp = setting_damp;
                state_outputprev = 0;
                return;
            }
            if (isSettledOnSilence())
            {
                state_lowpass = 0;
                state_bandpass = 0;
                state_outputprev = 0;
                lastupdate_fmult = setting_fmult;
                lastupdate_damp = setting_damp;
                return;
            }
        }
        input_block = allocate();
        if (nullptr == input_block)
        {
            return;
        }
        memset(input_block->data, 0, sizeof(input_block->data));
#else
        return;
#endif
    }

    if (!isEnabled && overdriveGain == 0 && isEnabled == lastEnabled && filterType == lastFilterType)
    {
        int32_t lastSample = (int32_t)input_block->data[AUDIO_BLOCK_SAMPLES - 1];
        state_inputprev = lastSample << kInputShift;
        state_outputprev = lastSample;
        transmit(input_block, 0);
        release(input_block);
        return;
    }

    out_block = allocate();
    if (nullptr == out_block)
    {
        release(input_block);
        return;
    }

    if (overdriveGain != 0)
    {
        update_overdrive(input_block->data, out_block->data);
        filter_block = out_block;
    }
    else
    {
        filter_block = input_block;
    }

    if (isEnabled)
    {
        update_fixed(filter_block->data, out_block->data, AUDIO_BLOCK_SAMPLES);
    }
    else if (filter_block == input_block)
    {
        memcpy(out_block->data, input_block->data, sizeof(out_block->data));
        state_inputprev = (int32_t)out_block->data[AUDIO_BLOCK_SAMPLES - 1] << kInputShift;
    }

    if ((isEnabled != lastEnabled) || (filterType != lastFilterType))
    {
        const int32_t kBlockSamplesShift = log2f(AUDIO_BLOCK_SAMPLES);
        for (int32_t sampleIdx = 0; sampleIdx < AUDIO_BLOCK_SAMPLES; sampleIdx++)
        {
            out_block->data[sampleIdx] =
              (int16_t)(((int32_t)out_block->data[sampleIdx] * (sampleIdx + 1) +
                         state_outputprev * (AUDIO_BLOCK_SAMPLES - (sampleIdx + 1))) >>
                        kBlockSamplesShift);
        }

        lastEnabled = isEnabled;
        lastFilterType = filterType;
    }

    state_outputprev = (int32_t)out_block->data[AUDIO_BLOCK_SAMPLES - 1];

    release(input_block);
    transmit(out_block, 0);
    release(out_block);
    return;
}
