// Copyright 2014 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Adapted for Polyverb by Wojciech Jakóbczyk (jakobczyk.woj@gmail.com)
//
// -----------------------------------------------------------------------------
//
// Reverb.

#ifndef POLYVERB_REVERB_H_
#define POLYVERB_REVERB_H_

#include "fx_engine.h"
#include "util.h"
#ifdef AUDIO_NULL_IS_SILENCE
#include <algorithm>
#endif

// The two writes that carry all the feedback energy of the loop also feed the quiet detector (AUDIO_NULL_IS_SILENCE, see Process()).
#ifdef AUDIO_NULL_IS_SILENCE
#define REVERB_WRITE_TRACKED(c, d, s)  c.WriteQ(d, s)
#define REVERB_WRITE2_TRACKED(c, d, s) c.Write2Q(d, s)
#else
#define REVERB_WRITE_TRACKED(c, d, s)  c.Write(d, s)
#define REVERB_WRITE2_TRACKED(c, d, s) c.Write2(d, s)
#endif

class Reverb
{
  public:
    Reverb() {}
    ~Reverb() {}

    void Init(uint16_t* buffer, uint16_t* buffer2)
    {
        engine_.Init(buffer, buffer2);
#ifdef AUDIO_NULL_IS_SILENCE
        quiet_samples_ = 0;
#endif
        engine_.SetLFOFrequency(LFO_1, 0.5f / 32000.0f);
        engine_.SetLFOFrequency(LFO_2, 0.3f / 32000.0f);
        lp_ = 0.7f;
        diffusion_ = 0.625f;
        predelay_length_ = 0;
    }

    static constexpr size_t GetBufferSize() { return E::GetSize() * sizeof(uint16_t); }

    inline __attribute__((always_inline)) void Process(int16_t* left, int16_t* right, size_t size)
    {
        // This is the Griesinger topology described in the Dattorro paper
        // (4 AP diffusers on the input, then a loop of 2x 2AP+1Delay).
        // Modulation is applied in the loop of the first diffuser AP for additional
        // smearing; and to the two long delays for a slow shimmer/chorus effect.
        typedef E::Reserve<
          150,
          E::Reserve<
            214,
            E::Reserve<
              319,
              E::Reserve<
                527,
                E::Reserve<
                  2182,
                  E::Reserve<2690, E::Reserve<4501, E::Reserve<predelay_buffer_size_>>>>>>>>
          Memory;
        typedef E2::Reserve<2525, E2::Reserve<2197, E2::Reserve<6312>>> Memory2;
        E::DelayLine<Memory, 0> ap1;
        E::DelayLine<Memory, 1> ap2;
        E::DelayLine<Memory, 2> ap3;
        E::DelayLine<Memory, 3> ap4;
        E::DelayLine<Memory, 4> dap1a;
        E::DelayLine<Memory, 5> dap1b;
        E::DelayLine<Memory, 6> del1;
        E::DelayLine<Memory, 7> del0;
        E2::DelayLine<Memory2, 0> dap2a;
        E2::DelayLine<Memory2, 1> dap2b;
        E2::DelayLine<Memory2, 2> del2;
        E::Context c;
#ifdef AUDIO_NULL_IS_SILENCE
        c.nonzero_ = 0;
#endif

        const float kap = diffusion_;
        const float klp = lp_;
        const float krt = reverb_time_;
        const float amount = amount_;
        const float gain = input_gain_;

        float lp_0 = lp_decay_0_;
        float lp_1 = lp_decay_1_;
        float lp_2 = lp_decay_2_;

        float delta;
        float delta_sum = 0.0f;

        for (size_t i = 0; i < size; i++)
        {
            float wet;
            float apout = 0.0f;
            engine_.Start(&c);

            // Smear AP1 inside the loop.
            // c.Interpolate(ap1, 10.0f, LFO_1, 80.0f, 1.0f);
            // c.Write(ap1, 100, 0.0f);

            c.Load(gain * ((float)*left + (float)*right) / 32768); // TODO better convertion

            c.Write(del0, 0.0f);
            c.Read(del0, predelay_length_, 1.0f);
            c.Lp(lp_0, klp);

            // Diffuse through 4 allpasses.
            c.Read(ap1 TAIL, kap);
            c.WriteAllPass(ap1, -kap);
            c.Read(ap2 TAIL, kap);
            c.WriteAllPass(ap2, -kap);
            c.Read(ap3 TAIL, kap);
            c.WriteAllPass(ap3, -kap);
            c.Read(ap4 TAIL, kap);
            c.WriteAllPass(ap4, -kap);
            c.Write(apout);

            // Main reverb loop.
            c.Load(apout);
            c.Interpolate2(del2, 6211.0f, LFO_2, 100.0f, krt);
            // c.Read2(del2 TAIL, krt);
            c.Lp(lp_1, klp);
            c.Read(dap1a TAIL, -kap);
            c.WriteAllPass(dap1a, kap);
            c.Read(dap1b TAIL, kap);
            c.WriteAllPass(dap1b, -kap);
            REVERB_WRITE_TRACKED(c, del1, 2.0f);
            c.Write(wet, 0.0f);

            delta = (wet * 32768 - *left) * amount;
            *left += delta;
            delta_sum += abs(delta);

            c.Load(apout);
            // c.Interpolate(del1, 4450.0f, LFO_1, 50.0f, krt);
            c.Read(del1 TAIL, krt);
            c.Lp(lp_2, klp);
            c.Read2(dap2a TAIL, kap);
            c.WriteAllPass2(dap2a, -kap);
            c.Read2(dap2b TAIL, -kap);
            c.WriteAllPass2(dap2b, kap);
            REVERB_WRITE2_TRACKED(c, del2, 2.0f);
            c.Write(wet, 0.0f);

            delta = (wet * 32768 - *right) * amount;
            *right += delta;
            delta_sum += abs(delta);

            ++left;
            ++right;
        }

        lp_decay_0_ = lp_0;
        lp_decay_1_ = lp_1;
        lp_decay_2_ = lp_2;
        last_update_delta_ = delta_sum / size;
#ifdef AUDIO_NULL_IS_SILENCE
        // Quiet block = the two long lines of the feedback loop (del1, del2: all the recirculating energy passes through them, at the
        // latest after the longest line, 6312 samples) were written with values below 1 LSB of 16 bit (they compress to 0), and the three
        // low pass states are below 1 LSB. AudioEffectPolyverb::update() resets the count when the input has sound. kQuietSamples such
        // samples in a row (more than the longest line of the input path, predelay included, 5000 + 1210 samples) and nothing above 1 LSB
        // is left to ring.
        constexpr float kTiny = 1.0f / 32768.0f;
        const bool tiny = lp_0 < kTiny && lp_0 > -kTiny && lp_1 < kTiny && lp_1 > -kTiny && lp_2 < kTiny && lp_2 > -kTiny;
        quiet_samples_ = (c.nonzero_ == 0 && tiny) ? std::min<uint32_t>(quiet_samples_ + size, kQuietSamples) : 0;
#endif
    }

#ifdef AUDIO_NULL_IS_SILENCE
    // true when the whole reverb state is below 1 LSB of 16 bit: a block of silent input then gives silent output. Process() can be
    // skipped, the output is zero (to within 1 LSB of the stock tail)
    inline bool IsQuiet() const { return quiet_samples_ >= kQuietSamples; }

    // a block with sound went in: the line buffers are about to fill
    inline void ClearQuiet() { quiet_samples_ = 0; }

    // The skipped block: the low pass states (below 1 LSB) and the activity meter go to zero, the line buffers are already zero. The
    // write position and the LFO phase move as Process() would move them, so the next block with sound modulates as the stock reverb does.
    inline void SkipQuiet(size_t size)
    {
        engine_.Skip(size);
        lp_decay_0_ = 0;
        lp_decay_1_ = 0;
        lp_decay_2_ = 0;
        last_update_delta_ = 0;
    }
#endif

    inline void SetAmount(float amount) { amount_ = amount; }

    inline void SetInputGain(float input_gain) { input_gain_ = input_gain; }

    inline void SetTime(float reverb_time) { reverb_time_ = reverb_time; }

    inline void SetDiffusion(float diffusion) { diffusion_ = diffusion; }

    inline void SetLP(float lp) { lp_ = lp; }

    inline void SetPredelayLength(float length)
    {
        predelay_length_ = length * predelay_buffer_size_;
    }

    inline void Reset()
    {
        engine_.Reset();
#ifdef AUDIO_NULL_IS_SILENCE
        quiet_samples_ = kQuietSamples;
#endif
        lp_decay_0_ = 0;
        lp_decay_1_ = 0;
        lp_decay_2_ = 0;
        last_update_delta_ = sound_delta_threshold_;
    }

    inline bool IsActive() { return last_update_delta_ > sound_delta_threshold_; }

    inline void SetActiveThreshold(float threshold) { sound_delta_threshold_ = threshold; }

  private:
    typedef FxEngine<16384, FORMAT_16_BIT> E;
    typedef FxEngine<16384, FORMAT_16_BIT> E2;
    E engine_;

    static constexpr size_t predelay_buffer_size_ = 5000;
#ifdef AUDIO_NULL_IS_SILENCE
    static constexpr uint32_t kQuietSamples = 16384; // one buffer length, the longest line is 15,591 (E) and 11,037 (E2) samples
    uint32_t quiet_samples_ = 0;
#endif

    float amount_;

    float input_gain_;
    float reverb_time_;
    float diffusion_;
    float lp_;

    float lp_decay_0_;
    float lp_decay_1_;
    float lp_decay_2_;

    int predelay_length_;

    float last_update_delta_;
    float sound_delta_threshold_ = 5.0f;

    DISALLOW_COPY_AND_ASSIGN(Reverb);
};

#endif // POLYVERB_REVERB_H_
