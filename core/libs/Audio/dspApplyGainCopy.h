#ifndef LIBS_AUDIO_DSPAPPLYGAINCOPY_H_
#define LIBS_AUDIO_DSPAPPLYGAINCOPY_H_

// applyGain (dspApplyGain.cpp) on a copy: dst = gain ramp applied to src in one pass without
// memcpy first. Arithmetic is identical to memcpy(dst, src) followed by applyGain(dst), yielding
// the same output samples and previous_mult. dst and src must not overlap.

#include "dspinst.h"
#include <stdint.h>
#include <string.h>

namespace gain_copy
{
template<int kBlockSamples>
inline void applyGainCopy16(int16_t* dst, const int16_t* src, int32_t& target_mult, int32_t& previous_mult)
{
    constexpr int32_t kUnityGain = 65536;
    if (target_mult == kUnityGain && previous_mult == kUnityGain)
    {
        memcpy(dst, src, kBlockSamples * sizeof(int16_t));
        return;
    }
    if (target_mult == 0 && previous_mult == 0)
    {
        memset(dst, 0, kBlockSamples * sizeof(int16_t));
        return;
    }

    const uint32_t* s = (const uint32_t*)src;
    uint32_t* p = (uint32_t*)dst;
    const uint32_t* end = (uint32_t*)(dst + kBlockSamples);

    int32_t multStep = (target_mult - previous_mult) / kBlockSamples;
    int32_t multToApply = previous_mult;

    do
    {
        uint32_t tmp32 = *s++;
        int32_t val1 = signed_multiply_32x16b(multToApply, tmp32);
        multToApply += multStep;
        int32_t val2 = signed_multiply_32x16t(multToApply, tmp32);
        multToApply += multStep;
        val1 = signed_saturate_rshift(val1, 16, 0);
        val2 = signed_saturate_rshift(val2, 16, 0);
        *p++ = pack_16b_16b(val2, val1);
    } while (p < end);

    previous_mult = target_mult;
}
} // namespace gain_copy

#endif
