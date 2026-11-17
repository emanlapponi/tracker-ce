#ifndef play_memory_read_h_
#define play_memory_read_h_

// Sample read paths of AudioPlayMemory (non-streamed voices). Pure functions of sample data
// and playhead with no class state, matching the scalar loop arithmetic bit-for-bit.
//
// Linear interpolation reads a sample and its neighbour. Both are adjacent int16_t values in memory,
// so load2() emits a single 32-bit load (ldr) instead of two ldrh instructions: even sample indices
// complete in one 32-bit aligned bus read, while odd sample indices are 2-byte-aligned unaligned
// word loads that Cortex-M4/M7 hardware splits into two bus transactions automatically (UNALIGN_TRP is clear).

#include <stdint.h>

static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "the word load unpacks the sample in the low half");

namespace pm_read
{
constexpr int32_t kMax16Bit = 65535;

// in[0] and in[1] with one 32 bit load
static inline __attribute__((always_inline)) void load2(const int16_t* in, int32_t& a, int32_t& b)
{
    uint32_t w;
    __builtin_memcpy(&w, in, 4);
    a = (int16_t)w;
    b = (int16_t)(w >> 16);
}

// Forward reading loops (loop forward, granular forward): the sample at idx and, when the voice interpolates,
// its right neighbour. cur = in[idx], dif = in[idx + 1] - in[idx] (0 when interpolationCondition != 0).
static inline __attribute__((always_inline)) void readForward(const int16_t* in,
                                                              int32_t idx,
                                                              uint32_t interpolationCondition,
                                                              int16_t& cur,
                                                              int32_t& dif)
{
    if (interpolationCondition)
    {
        cur = in[idx];
        dif = 0;
    }
    else
    {
        int32_t a, b;
        load2(in + idx, a, b);
        cur = (int16_t)a;
        dif = b - a;
    }
}

// Loop ping pong: the neighbour is on the right (interpOffset 1) or on the left (interpOffset -1).
static inline __attribute__((always_inline)) void readPingPong(const int16_t* in,
                                                               int32_t idx,
                                                               int32_t interpOffset,
                                                               uint32_t interpolationCondition,
                                                               int16_t& cur,
                                                               int32_t& dif)
{
    if (interpolationCondition)
    {
        cur = in[idx];
        dif = 0;
    }
    else
    {
        int32_t lo, hi;
        load2(in + idx + (interpOffset < 0 ? -1 : 0), lo, hi);
        const int32_t c = interpOffset < 0 ? hi : lo;
        cur = (int16_t)c;
        dif = (interpOffset < 0 ? lo : hi) - c;
    }
}

// One block of single shot, forward, not streamed and without glide: out[0..kBlockSamples) from playhead.
template<int kBlockSamples>
static inline __attribute__((always_inline)) void singleShotBlock(int16_t* out,
                                                                  const int16_t* in,
                                                                  uint32_t length,
                                                                  int32_t endPoint,
                                                                  int32_t castPitchControl,
                                                                  int32_t currentFractionPitchControl,
                                                                  uint32_t interpolationCondition,
                                                                  int32_t& iPitchCounter,
                                                                  int32_t& currentFractionPitchCounter,
                                                                  int32_t& interpolationDif,
                                                                  int16_t& currentSampelValue,
                                                                  uint8_t& playing)
{
    if (currentFractionPitchControl == 0 && currentFractionPitchCounter == 0 && castPitchControl == 1 &&
        iPitchCounter + kBlockSamples <= (int32_t)length && iPitchCounter + kBlockSamples < endPoint)
    {
        // unpitched 1.0x, whole block inside sample (no end-of-sample or endPoint wrap): 32-bit copy
        const int16_t* src = in + iPitchCounter;
        for (int k = 0; k < kBlockSamples / 2; k++)
        {
            uint32_t w;
            __builtin_memcpy(&w, src + 2 * k, 4);
            __builtin_memcpy(out + 2 * k, &w, 4);
        }
        currentSampelValue = src[kBlockSamples - 1];
        iPitchCounter += kBlockSamples;
    }
    else if (currentFractionPitchControl == 0 && currentFractionPitchCounter == 0)
    {
        for (int i = 0; i < kBlockSamples; i++)
        {
            if ((int32_t)length > iPitchCounter)
            {
                currentSampelValue = in[iPitchCounter];
                *out++ = currentSampelValue;
                iPitchCounter += castPitchControl;
                if (iPitchCounter >= endPoint)
                    iPitchCounter = length;
            }
            else
            {
                *out++ = 0;
                playing = 0;
            }
        }
    }
    else if (!interpolationCondition)
    {
        // interpolating, fractional pitch: sample and neighbour in one word load
        for (int i = 0; i < kBlockSamples; i++)
        {
            if ((int32_t)length > iPitchCounter)
            {
                int32_t a, b;
                load2(in + iPitchCounter, a, b);
                currentSampelValue = (int16_t)a;
                interpolationDif = b - a;
                *out++ = currentSampelValue +
                         (int32_t)(((currentFractionPitchCounter >> 2) * interpolationDif) >> 14);
                iPitchCounter += castPitchControl;
                currentFractionPitchCounter += currentFractionPitchControl;
                if (currentFractionPitchCounter >= kMax16Bit)
                {
                    currentFractionPitchCounter -= kMax16Bit;
                    iPitchCounter++;
                }
                if (iPitchCounter >= endPoint)
                    iPitchCounter = length;
            }
            else
            {
                *out++ = 0;
                playing = 0;
            }
        }
    }
    else
    {
        for (int i = 0; i < kBlockSamples; i++)
        {
            if ((int32_t)length > iPitchCounter)
            {
                currentSampelValue = in[iPitchCounter];
                interpolationDif = 0;
                *out++ = currentSampelValue +
                         (int32_t)(((currentFractionPitchCounter >> 2) * interpolationDif) >> 14);
                iPitchCounter += castPitchControl;
                currentFractionPitchCounter += currentFractionPitchControl;
                if (currentFractionPitchCounter >= kMax16Bit)
                {
                    currentFractionPitchCounter -= kMax16Bit;
                    iPitchCounter++;
                }
                if (iPitchCounter >= endPoint)
                    iPitchCounter = length;
            }
            else
            {
                *out++ = 0;
                playing = 0;
            }
        }
    }
}
} // namespace pm_read

#endif
