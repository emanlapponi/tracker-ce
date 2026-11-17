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

#ifndef filter_variable_h_
#define filter_variable_h_

#include "AudioStream.h"
#include "util_math.h"
#include <cmath>

class AudioFilterStateVariable : public AudioStream
{
  public:
    static constexpr float kCutoffMax = 1.0f;
    static constexpr float kCutoffMin = 0.0f;
    static constexpr int kFrequencyLookupSize = 478;
    static constexpr int kOverdriveGainMult = 256;
    static constexpr int kInputShift = 12;
    static constexpr float kUnityValue = 1073741824.0f;
    static constexpr float kOversamplingMult = 2.0f;

    static void initCutoffLookup();

    AudioFilterStateVariable()
      : AudioStream(1, inputQueueArray)
    {
        frequency(1000.0f);
        resonance(0.707f);
        state_inputprev = 0;
        state_lowpass = 0;
        state_bandpass = 0;
    }

    void resonance(float q)
    {
        q = clamp<float>(q, 0.7f, 5.0f);
        setting_damp = (1.0f / q) * kUnityValue;
    }

    void setCutoff(float cutoff)
    {
        if (!isCutoffInitialize)
        {
            return;
        }

        cutoff = clamp<float>(cutoff, kCutoffMin, kCutoffMax);

        int freqLookupIdx = cutoff * (kFrequencyLookupSize - 1);

        setting_fmult = filterFmult[freqLookupIdx];
    }
    void setOverdriveAmount(uint8_t overdrive)
    {
        if (overdrive == 0)
        {
            overdriveGain = 0;
        }
        else
        {
            overdriveGain = overdrive * 8 + kOverdriveGainMult / 2;
        }
    }

    void setType(uint8_t type)
    {
        if (type != filterType)
        {
            stabilize();
            lastFilterType = filterType;
        }

        filterType = type;
    }
    void connect()
    {
        lastEnabled = isEnabled;
        isEnabled = 1;
    }
    void disconnect()
    {
        lastEnabled = isEnabled;
        isEnabled = 0;
    }
    virtual void update(void);

  private:
    enum
    {
        kFilterTypeLowPass = 0,
        kFilterTypeHighPass,
        kFilterTypeBandPass
    };

    static float filterFreq[kFrequencyLookupSize];
    static int32_t filterFmult[kFrequencyLookupSize];
    static bool isCutoffInitialize;

    void frequency(float freq)
    {
        freq = clamp<float>(freq, 20.0f, AUDIO_SAMPLE_RATE_EXACT / 2.5f);
        setting_fmult =
          2.0f * sinf(freq * (3.141592654f / (AUDIO_SAMPLE_RATE_EXACT * kOversamplingMult))) *
          kUnityValue;
    }

#ifdef AUDIO_NULL_IS_SILENCE
    bool isSettledOnSilence() const;
#endif
    void update_fixed(const int16_t* in, int16_t* out, int samples);
    void update_overdrive(const int16_t* in, int16_t* out);
    void stabilize();
    int32_t lastupdate_fmult = 0;
    int32_t lastupdate_damp = 0;
    int32_t setting_fmult;
    int32_t setting_damp;
    int32_t state_inputprev;
    int32_t state_lowpass;
    int32_t state_bandpass;
    int32_t state_outputprev = 0;
    audio_block_t* inputQueueArray[1];
    uint8_t isEnabled;
    uint8_t filterType;
    int32_t overdriveGain;
    uint8_t lastFilterType;
    uint8_t lastEnabled;
};

#endif
