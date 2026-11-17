#include "effect_limiter.h"
#include "ampLogarythmicValues.h"
#include "mtStructs.h"
#include "util_math.h"
#include <cmath>

int16_t zeroblock[AUDIO_BLOCK_SAMPLES];

AUDIO_CORE_FLASH void AudioEffectLimiter::begin()
{
    setAttack(mtProject.values.limiterAttack);
    setRelease(mtProject.values.limiterRelease);
    setThreshold(mtProject.values.limiterThreshold);
    isSideChainEnable = (bool)mtProject.values.limiterSidechain;
    isAverageGain = false;

    memset(inputStorageBuffer, 0, 10);
}

AUDIO_CORE_FLASH void AudioEffectLimiter::setThreshold(uint16_t ts)
{
    uint8_t quantizedThresholdIndex = mapClamped<int>(ts, 0, MAX_SIGNED_16BIT, 0, 100);
    threshold = limiterThresholdValues[quantizedThresholdIndex];
#ifdef AUDIO_NULL_IS_SILENCE
    __atomic_signal_fence(__ATOMIC_SEQ_CST);
    relaxed = false;
#endif
}
AUDIO_CORE_FLASH void AudioEffectLimiter::setAttack(uint16_t a)
{
    if (mtProject.values.limiterMode == limiterModeSubtle)
        attackTime = 1 - expf(-2.2f * (1000.0f / (44100.0f * a)));
    else if (mtProject.values.limiterMode == limiterModeHard)
        attackTime = a / 1000.0f;
#ifdef AUDIO_NULL_IS_SILENCE
    __atomic_signal_fence(__ATOMIC_SEQ_CST);
    relaxed = false;
#endif
}
AUDIO_CORE_FLASH void AudioEffectLimiter::setRelease(float r)
{
    if (mtProject.values.limiterMode == limiterModeSubtle)
        releaseTime = 1 - expf(-2.2f * (1000.0f / (44100.0f * r)));
    else if (mtProject.values.limiterMode == limiterModeHard)
        releaseTime = r / 1000.0f;
#ifdef AUDIO_NULL_IS_SILENCE
    __atomic_signal_fence(__ATOMIC_SEQ_CST);
    relaxed = false;
#endif
}

AUDIO_CORE_FLASH void AudioEffectLimiter::setSidechainEnable(bool isEnable)
{
    isSideChainEnable = isEnable;
#ifdef AUDIO_NULL_IS_SILENCE
    __atomic_signal_fence(__ATOMIC_SEQ_CST);
    relaxed = false;
#endif
}

AUDIO_CORE_FLASH float AudioEffectLimiter::getAverageGainChange()
{
    isAverageGain = false;
    return averageGain;
}
AUDIO_CORE_FLASH bool AudioEffectLimiter::isAvailableGainToGet()
{
    return isAverageGain;
}

AUDIO_CORE_RAM void AudioEffectLimiter::update(void)
{
    if (isBlocked)
        return;

    if (isSideChainEnable)
        processSidechain();
    else
        processMainStream();
}

#ifdef AUDIO_NULL_IS_SILENCE
// The peak detector is 0 and nothing is left in the look-ahead delay (the lines of the sidechain mode included). With silent input the
// output is then zero whatever the gain does.
AUDIO_CORE_RAM bool AudioEffectLimiter::isSteadyOnSilence(bool sidechain) const
{
    if (xpeak != 0)
        return false;
    for (uint8_t i = 0; i < LIMITER_DELAY; i++)
    {
        if (inputStorageBuffer[i] != 0 || (sidechain && outputStorageBuffer[i] != 0))
            return false;
    }
    return true;
}
#endif

AUDIO_CORE_RAM void AudioEffectLimiter::processSidechain()
{
    audio_block_t* inputBlock;
    audio_block_t* sidechainBlock;
    int16_t* inputData;
    int16_t* outputData;

    inputBlock = receiveWritable(0);
    sidechainBlock = receiveReadOnly(1);
#ifdef AUDIO_NULL_IS_SILENCE
    bool silentIn = false;
    float gainBefore = 0.0f;
#endif
    if (!inputBlock)
    {
#ifdef AUDIO_NULL_IS_SILENCE
        // Silence in: the limiter still has to run (gain release, look-ahead delay, sidechain) until a block of silence leaves its whole
        // state as it found it (relaxed). From then on every block of silence gives the same state, the same zero output and the same
        // meter value, so it is skipped.
        if (!sidechainBlock && relaxed)
        {
            isAverageGain = true;
            return;
        }
        inputBlock = allocate();
        if (inputBlock)
        {
            memset(inputBlock->data, 0, sizeof(inputBlock->data));
            silentIn = !sidechainBlock && isSteadyOnSilence(true);
            gainBefore = g;
        }
        else
#endif
        {
            if (sidechainBlock)
                release(sidechainBlock);
            return;
        }
    }

#ifdef AUDIO_NULL_IS_SILENCE
    relaxed = false;
#endif
    inputData = sidechainBlock ? sidechainBlock->data : zeroblock;
    outputData = inputBlock->data;

    {
        // State in locals and the look-ahead delay in registers, written back once. While the peak is at or under the threshold
        // threshold / xpeak is at least 1 and f is clamped to 1 anyway (xpeak 0 gives inf or NaN, f is 1 there too), so the divide is skipped.
        // Same results bit for bit as the member version.
        uint16_t lxpeak = xpeak;
        uint16_t la = a;
        float lg = g, lcoeff = coeff, lf = f, lsum = sumGain;
        const float att = attackTime, rel = releaseTime;
        const uint16_t thr = threshold;
        const float thrf = (float)thr;
        int16_t i0 = inputStorageBuffer[0], i1 = inputStorageBuffer[1], i2 = inputStorageBuffer[2], i3 = inputStorageBuffer[3],
                i4 = inputStorageBuffer[4];
        int16_t o0 = outputStorageBuffer[0], o1 = outputStorageBuffer[1], o2 = outputStorageBuffer[2], o3 = outputStorageBuffer[3],
                o4 = outputStorageBuffer[4];
        static_assert(LIMITER_DELAY == 5, "the look-ahead delay is kept in five registers");
        for (uint8_t n = 0; n < AUDIO_BLOCK_SAMPLES; n++)
        {
            const int16_t in = inputData[n];
            const int16_t out = outputData[n];
            la = (in < 0) ? -in : in;
            lcoeff = (la > lxpeak) ? att : rel;
            lxpeak = (1 - lcoeff) * lxpeak + lcoeff * la;
            if (lxpeak <= thr)
                lf = 1.0f;
            else
            {
                const float localf = thrf / (float)lxpeak;
                lf = (localf < 1.0f) ? localf : 1.0f;
            }
            lcoeff = (lf < lg) ? att : rel;
            lg = (1 - lcoeff) * lg + lcoeff * lf;
            outputData[n] = lg * o4 + (1 - lg) * in;
            lsum += lg;
            i4 = i3; i3 = i2; i2 = i1; i1 = i0; i0 = in;
            o4 = o3; o3 = o2; o2 = o1; o1 = o0; o0 = out;
        }
        inputStorageBuffer[0] = i0; inputStorageBuffer[1] = i1; inputStorageBuffer[2] = i2; inputStorageBuffer[3] = i3;
        inputStorageBuffer[4] = i4;
        outputStorageBuffer[0] = o0; outputStorageBuffer[1] = o1; outputStorageBuffer[2] = o2; outputStorageBuffer[3] = o3;
        outputStorageBuffer[4] = o4;
        xpeak = lxpeak; a = la; g = lg; coeff = lcoeff; f = lf; sumGain = lsum;
    }

    averageGain = sumGain / AUDIO_BLOCK_SAMPLES;
    sumGain = 0.0f;
    isAverageGain = true;
#ifdef AUDIO_NULL_IS_SILENCE
    relaxed = silentIn && g == gainBefore && isSteadyOnSilence(true);
#endif

    transmit(inputBlock, 0);
    release(inputBlock);
    if (sidechainBlock)
        release(sidechainBlock);
}
AUDIO_CORE_RAM void AudioEffectLimiter::processMainStream()
{
    audio_block_t* inputBlock;
    audio_block_t* sidechainBlock;
    int16_t* inputData;

    inputBlock = receiveWritable(0);
    sidechainBlock = receiveReadOnly(1);
#ifdef AUDIO_NULL_IS_SILENCE
    bool silentIn = false;
    float gainBefore = 0.0f;
#endif
    if (!inputBlock)
    {
#ifdef AUDIO_NULL_IS_SILENCE
        if (relaxed)
        {
            if (sidechainBlock)
                release(sidechainBlock);
            isAverageGain = true;
            return;
        }
        inputBlock = allocate();
        if (inputBlock)
        {
            memset(inputBlock->data, 0, sizeof(inputBlock->data));
            silentIn = isSteadyOnSilence(false);
            gainBefore = g;
        }
        else
#endif
        {
            if (sidechainBlock)
                release(sidechainBlock);
            return;
        }
    }

    if (sidechainBlock)
    {
        release(sidechainBlock);
        sidechainBlock = nullptr;
    }
#ifdef AUDIO_NULL_IS_SILENCE
    relaxed = false;
#endif
    inputData = inputBlock->data;

    {
        // Same as the sidechain loop: locals, the delay in registers, no divide at or under the threshold.
        uint16_t lxpeak = xpeak;
        uint16_t la = a;
        float lg = g, lcoeff = coeff, lf = f, lsum = sumGain;
        const float att = attackTime, rel = releaseTime;
        const uint16_t thr = threshold;
        const float thrf = (float)thr;
        int16_t d0 = inputStorageBuffer[0], d1 = inputStorageBuffer[1], d2 = inputStorageBuffer[2], d3 = inputStorageBuffer[3],
                d4 = inputStorageBuffer[4];
        static_assert(LIMITER_DELAY == 5, "the look-ahead delay is kept in five registers");
        for (uint8_t n = 0; n < AUDIO_BLOCK_SAMPLES; n++)
        {
            const int16_t in = inputData[n];
            la = (in < 0) ? -in : in;
            lcoeff = (la > lxpeak) ? att : rel;
            lxpeak = (1 - lcoeff) * lxpeak + lcoeff * la;
            if (lxpeak <= thr)
                lf = 1.0f;
            else
            {
                const float localf = thrf / (float)lxpeak;
                lf = (localf < 1.0f) ? localf : 1.0f;
            }
            lcoeff = (lf < lg) ? att : rel;
            lg = (1 - lcoeff) * lg + lcoeff * lf;
            inputData[n] = lg * d4;
            lsum += lg;
            d4 = d3; d3 = d2; d2 = d1; d1 = d0; d0 = in;
        }
        inputStorageBuffer[0] = d0; inputStorageBuffer[1] = d1; inputStorageBuffer[2] = d2; inputStorageBuffer[3] = d3;
        inputStorageBuffer[4] = d4;
        xpeak = lxpeak; a = la; g = lg; coeff = lcoeff; f = lf; sumGain = lsum;
    }

    averageGain = sumGain / AUDIO_BLOCK_SAMPLES;
    sumGain = 0.0f;
    isAverageGain = true;
#ifdef AUDIO_NULL_IS_SILENCE
    relaxed = silentIn && g == gainBefore && isSteadyOnSilence(false);
#endif

    transmit(inputBlock, 0);
    release(inputBlock);
    if (sidechainBlock)
        release(sidechainBlock);
}

AUDIO_CORE_FLASH void AudioEffectLimiter::blockUpdate()
{
    isBlocked = true;
}
AUDIO_CORE_FLASH void AudioEffectLimiter::unblockUpdate()
{
    isBlocked = false;
}
