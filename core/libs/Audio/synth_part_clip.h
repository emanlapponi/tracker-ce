#ifndef synth_part_clip_h_
#define synth_part_clip_h_

// Mono part soft-clip + int16 conversion in one pass directly from the rendered float block.
// Replaces the extra float buffer copy, dual saturateSoftClip pass, and float->int16 passes.
// For |x| <= 1.5 the cubic x - x^3 * (1.0f / 6.72f) peaks at ~0.99779, so the second [-1, 1]
// clamp is dead for all representable values (and NaN converts identically via static_cast<int16_t>).

#include <algorithm>
#include <stdint.h>

namespace part_clip
{
static inline __attribute__((always_inline)) int16_t clipToInt16(float x)
{
    float sample = std::clamp(x, -1.5f, 1.5f);
    sample = sample - (sample * sample * sample) * (1.0f / 6.72f);
    return static_cast<int16_t>(sample * INT16_MAX);
}

// send: block for both pre-FX send mixers (clip at 0 dB), post: part output (clip at postGainAmp).
// reuseUnmodifiedMono: part at 0 dB with mono output and no aux sends (one sample converts both).
template<int kBlockSamples>
static inline __attribute__((always_inline)) void monoPart(const float* src,
                                                           float postGainAmp,
                                                           bool reuseUnmodifiedMono,
                                                           int16_t* send,
                                                           int16_t* post)
{
    if (reuseUnmodifiedMono)
    {
        for (int i = 0; i < kBlockSamples; i++)
        {
            const int16_t v = clipToInt16(src[i]);
            send[i] = v;
            post[i] = v;
        }
    }
    else
    {
        for (int i = 0; i < kBlockSamples; i++)
        {
            send[i] = clipToInt16(src[i]);
            post[i] = clipToInt16(src[i] * postGainAmp);
        }
    }
}

// Cache dbToAmplitude(gainDb) per part so powf runs only when a part's gain changes.
// Initialize db entries to NaN so the first call always evaluates dbToAmplitude().
template<int kParts>
struct GainCache
{
    float db[kParts];
    float amp[kParts];

    template<typename F>
    inline float get(int part, float gainDb, F&& dbToAmplitude)
    {
        if (db[part] != gainDb)
        {
            db[part] = gainDb;
            amp[part] = dbToAmplitude(gainDb);
        }
        return amp[part];
    }
};
} // namespace part_clip

#endif
