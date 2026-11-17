#include "effect_shortDelay.h"
#include <algorithm>
#include "Audio.h"
#include "mtSequencer.h"
#include "utility/dspinst.h"

static int16_t zeroblock[AUDIO_BLOCK_SAMPLES] = {};

const float tempoSyncRatesShortDelay[19] = {
    8,        6,    4,      3,        2,     1.5,      1,      0.75,     0.5,     0.375,
    0.333333, 0.25, 0.1875, 0.166667, 0.125, 0.083333, 0.0625, 0.041667, 0.03125,
};
extern int16_t externalRamDelayBuff[];

AUDIO_CORE_FLASH void AudioEffectShortDelay::begin(float feedback,
                                             uint32_t timeMs,
                                             bool pingPongEnabled,
                                             bool syncEnabled,
                                             uint8_t syncRateIdx,
                                             float frequency)
{
    setPingpongEnable(pingPongEnabled);
    setSyncEnable(syncEnabled);
    setTime(timeMs);
    setFeedback(feedback);
    setRate(syncRateIdx);
    setFilterFrequency(frequency);

    setBuffer(externalRamDelayBuff);
    unblockUpdate();
}

AUDIO_CORE_FLASH void AudioEffectShortDelay::setBuffer(int16_t* buffer)
{
    pingpongDelaylineRight = &buffer[0];
    pingpongDelaylineLeft = &buffer[MAX_DELAYLINE_LENGTH];
    monoDelayline = &buffer[2 * MAX_DELAYLINE_LENGTH];

    clear();
}

AUDIO_CORE_FLASH void AudioEffectShortDelay::setFeedback(float feedback)
{
    if (feedback > kMaxFeedback)
        feedback = kMaxFeedback;
    else if (feedback < 0.0f)
        feedback = 0.0f;

    //	__disable_irq();
    AudioNoInterrupts();
    this->feedback = UINT8_MAX * feedback;
    AudioInterrupts();
    //	__enable_irq();
}
AUDIO_CORE_FLASH void AudioEffectShortDelay::setTime(float time)
{
    if (time > MAX_SHORT_DELAY_TIME)
        time = MAX_SHORT_DELAY_TIME;
    //	__disable_irq();
    AudioNoInterrupts();
    timeInSamples = (uint32_t)(time * 44.1f);
    //	__enable_irq();
    AudioInterrupts();
}

AUDIO_CORE_FLASH int AudioEffectShortDelay::getTime()
{
    return int(timeInSamples / 44.1f);
}

AUDIO_CORE_FLASH void AudioEffectShortDelay::setRate(uint8_t syncRateIdx)
{
    if (!isSync)
        return;

    float tempo = sequencer.getCurrentTempo();

    float freq = (tempo / 15.0f); // *4,bo  beat = 4 step i /60, bo min = 60s
    float timeMs = (1000 / freq) * tempoSyncRatesShortDelay[syncRateIdx];

    setTime(timeMs);
}
AUDIO_CORE_FLASH void AudioEffectShortDelay::setPingpongEnable(bool pingPongEnabled)
{
    isPingpong = pingPongEnabled;
}
AUDIO_CORE_FLASH void AudioEffectShortDelay::setSyncEnable(bool syncEnabled)
{
    isSync = syncEnabled;
}

AUDIO_CORE_FLASH void AudioEffectShortDelay::setFilterFrequency(float frequency)
{
    float Qc = 2 * M_PI * frequency / 44100.0f;
    float y = (cosf(Qc) / (1 + sinf(Qc)));

    filterCoeffs.a0 = kMaxFilterCoeff * ((1.0f - y) / 2.0f);
    filterCoeffs.a1 = filterCoeffs.a0;
    filterCoeffs.b1 = kMaxFilterCoeff * (-y);

    lastFilterInLeft = 0;
    lastFilterInRight = 0;
    lastFilterInMono = 0;
    lastFilterOutLeft = 0;
    lastFilterOutRight = 0;
    lastFilterOutMono = 0;
}

AUDIO_CORE_FLASH void AudioEffectShortDelay::clear()
{
    blockUpdate();
    //    AudioNoInterrupts();
    headIdx = 0;
    lastFilterInLeft = 0;
    lastFilterInRight = 0;
    lastFilterInMono = 0;
    lastFilterOutLeft = 0;
    lastFilterOutRight = 0;
    lastFilterOutMono = 0;
    silentSamplesCount = MAX_DELAYLINE_LENGTH;
#ifdef AUDIO_NULL_IS_SILENCE
    quietSamplesCount = MAX_DELAYLINE_LENGTH;
    zeroedAllSamples = MAX_DELAYLINE_LENGTH;
#endif
    // the lines are zero from here on, the set of the current mode is the live one with the whole history valid
    ppLive = isPingpong != 0;
    monoLive = !ppLive;
    ppWeight = ppLive ? kFadeOne : 0;
    ppDepth = monoDepth = MAX_DELAYLINE_LENGTH;
    ppSeed = monoSeed = 0;

    memset(pingpongDelaylineRight, 0, MAX_DELAYLINE_LENGTH * sizeof(int16_t));
    memset(pingpongDelaylineLeft, 0, MAX_DELAYLINE_LENGTH * sizeof(int16_t));
    memset(monoDelayline, 0, MAX_DELAYLINE_LENGTH * sizeof(int16_t));
    //    AudioInterrupts();
    unblockUpdate();
}

AUDIO_CORE_FLASH void AudioEffectShortDelay::blockUpdate()
{
    noRefresh = true;
    __atomic_signal_fence(__ATOMIC_SEQ_CST);
}
AUDIO_CORE_FLASH void AudioEffectShortDelay::unblockUpdate()
{
    __atomic_signal_fence(__ATOMIC_SEQ_CST);
    noRefresh = false;
}

#ifdef AUDIO_NULL_IS_SILENCE
static bool isZeroOrMissing(const audio_block_t* block)
{
    if (block == nullptr)
        return true;
    const uint32_t* p = (const uint32_t*)block->data;
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES / 2; i++)
    {
        if (p[i] != 0)
            return false;
    }
    return true;
}
#endif

AUDIO_CORE_RAM void AudioEffectShortDelay::update(void)
{
    audio_block_t* input;
    audio_block_t* outputLeft;
    audio_block_t* outputRight;

    input = receiveReadOnly(0);
#ifdef AUDIO_NULL_IS_SILENCE
    const bool silentInput = isZeroOrMissing(input);
    if (!noRefresh && timeInSamples >= 1 && quietSamplesCount >= timeInSamples &&
        (lastFilterInRight | lastFilterInLeft | lastFilterInMono | lastFilterOutRight | lastFilterOutLeft | lastFilterOutMono) == 0 &&
        silentInput)
    {
        syncIdleMode(isPingpong != 0);
        // Silent input, the last timeInSamples values written to the lines were zero (so the whole delay window is) and the filters are
        // at rest: delayed samples, filter outputs and the values to write are zero, as in every one of the 128 steps of process().
        // What process() would do is write 128 zeros to each line that runs, move the head and count 128 silent samples. Same state, same
        // output (zero, so nothing is transmitted). The line that does not run is zeroed too, so it never replays an old echo.
        const uint32_t headSampleIdx = headIdx & delaylineLengthMask; // a multiple of 128, the block never wraps
        memset(&pingpongDelaylineRight[headSampleIdx], 0, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
        memset(&pingpongDelaylineLeft[headSampleIdx], 0, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
        memset(&monoDelayline[headSampleIdx], 0, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
        headIdx += AUDIO_BLOCK_SAMPLES;
        // the sets that run (one, or two during a mode switch) wrote this block of zeros: their history grows, the crossfade waits
        ppDepth = std::min<uint32_t>(ppDepth + AUDIO_BLOCK_SAMPLES, MAX_DELAYLINE_LENGTH);
        monoDepth = std::min<uint32_t>(monoDepth + AUDIO_BLOCK_SAMPLES, MAX_DELAYLINE_LENGTH);
        silentSamplesCount = std::min<uint32_t>(silentSamplesCount + AUDIO_BLOCK_SAMPLES, MAX_DELAYLINE_LENGTH);
        quietSamplesCount = std::min<uint32_t>(quietSamplesCount + AUDIO_BLOCK_SAMPLES, MAX_DELAYLINE_LENGTH);
        zeroedAllSamples = std::min<uint32_t>(zeroedAllSamples + AUDIO_BLOCK_SAMPLES, MAX_DELAYLINE_LENGTH);
        if (input != nullptr)
        {
            release(input);
        }
        return;
    }
#endif
    outputLeft = allocate();
    outputRight = allocate();

    bool processed = false;

    if (outputLeft != nullptr && outputRight != nullptr)
    {
#ifdef AUDIO_NULL_IS_SILENCE
        int16_t* const inputData = input != nullptr ? input->data : zeroblock;
        processed = silentInput
                      ? processBlock<true>(inputData, outputLeft->data, outputRight->data, AUDIO_BLOCK_SAMPLES)
                      : processBlock<false>(inputData, outputLeft->data, outputRight->data, AUDIO_BLOCK_SAMPLES);
#else
        processed = process(input != nullptr ? input->data : zeroblock,
                            outputLeft->data,
                            outputRight->data,
                            AUDIO_BLOCK_SAMPLES);
#endif
    }

    if (outputRight != nullptr)
    {
        if (processed)
        {
            transmit(outputRight, 0);
        }
        release(outputRight);
    }
    if (outputLeft != nullptr)
    {
        if (processed)
        {
            transmit(outputLeft, 1);
        }
        release(outputLeft);
    }
    if (input != nullptr)
    {
        release(input);
    }
}

AUDIO_CORE_RAM bool AudioEffectShortDelay::process(int16_t input[],
                                               int16_t outputLeft[],
                                               int16_t outputRight[],
                                               int sampleCount)
{
    return processBlock<false>(input, outputLeft, outputRight, sampleCount);
}

namespace
{
constexpr int32_t kFeedbackRoundBias = (1 << 8) - 1; // (1 << kFeedbackShift) - 1

inline int32_t clamp16(int32_t v)
{
    return v > 32767 ? 32767 : (v < -32768 ? -32768 : v);
}
} // namespace

// The set of the requested mode goes live (depth 0) next to the one that runs. Its filter states continue those of the old set so the
// first filtered sample is what the old mode would have produced, and its reads older than its depth come from the old set's lines.
__attribute__((noinline)) AUDIO_CORE_FLASH void AudioEffectShortDelay::startMode(bool pingpong)
{
    if (pingpong)
    {
        ppLive = true;
        ppDepth = 0;
        ppSeed = monoLive ? monoDepth : 0;
        // mono -> ping pong: the echoes in flight go to the left line, the right one starts empty and fills from the left (bounce)
        lastFilterInLeft = monoLive ? lastFilterInMono : 0;
        lastFilterOutLeft = monoLive ? lastFilterOutMono : 0;
        lastFilterInRight = 0;
        lastFilterOutRight = 0;
    }
    else
    {
        monoLive = true;
        monoDepth = 0;
        monoSeed = ppLive ? ppDepth : 0;
        // ping pong -> mono: left and right add up (a one sided echo keeps its level, both sides now carry it)
        lastFilterInMono = ppLive ? clamp16(lastFilterInLeft + lastFilterInRight) : 0;
        lastFilterOutMono = ppLive ? clamp16(lastFilterOutLeft + lastFilterOutRight) : 0;
    }
}

#ifdef AUDIO_NULL_IS_SILENCE
AUDIO_CORE_RAM void AudioEffectShortDelay::syncIdleMode(bool wantPingpong)
{
    if ((wantPingpong ? !ppLive : !monoLive) && timeInSamples >= 1 &&
        quietSamplesCount >= timeInSamples &&
        (lastFilterInRight | lastFilterInLeft | lastFilterInMono | lastFilterOutRight |
         lastFilterOutLeft | lastFilterOutMono) == 0)
    {
        const uint32_t count = std::min<uint32_t>(timeInSamples, MAX_DELAYLINE_LENGTH);
        if (zeroedAllSamples < count)
        {
            const uint32_t startIdx = (headIdx - count) & delaylineLengthMask;
            const uint32_t firstChunk =
              std::min<uint32_t>(count, MAX_DELAYLINE_LENGTH - startIdx);
            const uint32_t secondChunk = count - firstChunk;
            if (wantPingpong)
            {
                memset(&pingpongDelaylineLeft[startIdx], 0, firstChunk * sizeof(int16_t));
                memset(&pingpongDelaylineRight[startIdx], 0, firstChunk * sizeof(int16_t));
                if (secondChunk > 0)
                {
                    memset(&pingpongDelaylineLeft[0], 0, secondChunk * sizeof(int16_t));
                    memset(&pingpongDelaylineRight[0], 0, secondChunk * sizeof(int16_t));
                }
            }
            else
            {
                memset(&monoDelayline[startIdx], 0, firstChunk * sizeof(int16_t));
                if (secondChunk > 0)
                {
                    memset(&monoDelayline[0], 0, secondChunk * sizeof(int16_t));
                }
            }
        }
        ppLive = wantPingpong;
        monoLive = !wantPingpong;
        ppWeight = wantPingpong ? kFadeOne : 0;
        if (zeroedAllSamples >= MAX_DELAYLINE_LENGTH)
        {
            ppDepth = MAX_DELAYLINE_LENGTH;
            monoDepth = MAX_DELAYLINE_LENGTH;
        }
        else
        {
            ppDepth = wantPingpong ? count : 0;
            monoDepth = wantPingpong ? 0 : count;
            quietSamplesCount = count;
            zeroedAllSamples = count;
        }
        ppSeed = 0;
        monoSeed = 0;
    }
}
#endif

// True when the block can run one of the two fast loops: only the requested set is live and its stored lines cover the delay time.
// Otherwise (a mode switch crossfade, or a set that has not been written for timeInSamples yet) runSwitching() does the block.
AUDIO_CORE_RAM bool AudioEffectShortDelay::prepareBlock(bool wantPingpong)
{
#ifdef AUDIO_NULL_IS_SILENCE
    syncIdleMode(wantPingpong);
#endif
    if (wantPingpong ? !ppLive : !monoLive)
    {
        if (!ppLive && !monoLive)
        {
            // nothing runs (the object was never cleared): start the requested set with the whole history valid
            (wantPingpong ? ppLive : monoLive) = true;
            (wantPingpong ? ppDepth : monoDepth) = MAX_DELAYLINE_LENGTH;
            ppWeight = wantPingpong ? kFadeOne : 0;
        }
        else
        {
            startMode(wantPingpong);
        }
    }
    if (ppLive && monoLive)
    {
        return false;
    }
    return timeInSamples <= (ppLive ? ppDepth : monoDepth);
}

template<bool kTrackQuiet>
AUDIO_CORE_RAM int32_t AudioEffectShortDelay::runPingpong(int16_t input[],
                                                      int16_t outputLeft[],
                                                      int16_t outputRight[],
                                                      int sampleCount)
{
    int32_t anyNonZero = 0;
    // only the two ping pong lines are computed, state in locals
    const int32_t a0 = filterCoeffs.a0, a1 = filterCoeffs.a1, b1 = filterCoeffs.b1;
    const int32_t fb = feedback;
    uint32_t head = headIdx;
    const uint32_t tis = timeInSamples;
    uint32_t silent = silentSamplesCount;
    int32_t inR = lastFilterInRight, inL = lastFilterInLeft, outRs = lastFilterOutRight, outLs = lastFilterOutLeft;
    for (int sample = 0; sample < sampleCount; sample++)
    {
        uint32_t d = (head - tis) & delaylineLengthMask;
        int16_t dR = pingpongDelaylineRight[d];
        int16_t dL = pingpongDelaylineLeft[d];
        int32_t fR = (a0 * dR + a1 * inR - b1 * outRs) / (1 << kFilterCoeffsShift);
        int32_t fL = (a0 * dL + a1 * inL - b1 * outLs) / (1 << kFilterCoeffsShift);
        inR = dR;
        inL = dL;
        outRs = fR;
        outLs = fL;
        int16_t outL = fL, outR = fR;
        *outputLeft++ = outL;
        *outputRight++ = outR;
        int32_t inputShifted = ((*input++) << kFeedbackShift);
        int32_t nL = inputShifted + fR * fb;
        int32_t nR = fL * fb;
        int16_t sL = (int16_t)signed_saturate_rshift(nL + (nL < 0 ? kFeedbackRoundBias : 0), 16, kFeedbackShift);
        int16_t sR = (int16_t)signed_saturate_rshift(nR + (nR < 0 ? kFeedbackRoundBias : 0), 16, kFeedbackShift);
        if constexpr (kTrackQuiet)
        {
            anyNonZero |= sL | sR;
        }
        if (outL == 0 && outR == 0 && sL == 0 && sR == 0)
        {
            if (silent < MAX_DELAYLINE_LENGTH)
                silent++;
        }
        else
        {
            silent = 0;
        }
        uint32_t h = head++ & delaylineLengthMask;
        pingpongDelaylineLeft[h] = sL;
        pingpongDelaylineRight[h] = sR;
    }
    lastFilterInRight = inR;
    lastFilterInLeft = inL;
    lastFilterOutRight = outRs;
    lastFilterOutLeft = outLs;
    headIdx = head;
    silentSamplesCount = silent;
    return anyNonZero;
}

template<bool kTrackQuiet>
AUDIO_CORE_RAM int32_t AudioEffectShortDelay::runMono(int16_t input[],
                                                  int16_t outputLeft[],
                                                  int16_t outputRight[],
                                                  int sampleCount)
{
    int32_t anyNonZero = 0;
    // only the mono line is computed, state in locals
    const int32_t a0 = filterCoeffs.a0, a1 = filterCoeffs.a1, b1 = filterCoeffs.b1;
    const int32_t fb = feedback;
    uint32_t head = headIdx;
    const uint32_t tis = timeInSamples;
    uint32_t silent = silentSamplesCount;
    int32_t inM = lastFilterInMono, outMs = lastFilterOutMono;
    for (int sample = 0; sample < sampleCount; sample++)
    {
        uint32_t d = (head - tis) & delaylineLengthMask;
        int16_t dM = monoDelayline[d];
        int32_t fM = (a0 * dM + a1 * inM - b1 * outMs) / (1 << kFilterCoeffsShift);
        inM = dM;
        outMs = fM;
        int16_t o = fM;
        *outputLeft++ = o;
        *outputRight++ = o;
        int32_t inputShifted = ((*input++) << kFeedbackShift);
        int32_t nM = inputShifted + fM * fb;
        int16_t sM = (int16_t)signed_saturate_rshift(nM + (nM < 0 ? kFeedbackRoundBias : 0), 16, kFeedbackShift);
        if constexpr (kTrackQuiet)
        {
            anyNonZero |= sM;
        }
        if (o == 0 && sM == 0)
        {
            if (silent < MAX_DELAYLINE_LENGTH)
                silent++;
        }
        else
        {
            silent = 0;
        }
        monoDelayline[head++ & delaylineLengthMask] = sM;
    }
    lastFilterInMono = inM;
    lastFilterOutMono = outMs;
    headIdx = head;
    silentSamplesCount = silent;
    return anyNonZero;
}

// The block of a mode switch (and of the time after it until the new set has been written for timeInSamples): up to both sets run, the
// output is the crossfade of the two (ppWeight), a set that has no stored data for the delayed sample reads the other set's lines
// instead (or zero). Runs in flash since it is only active during the delay time after a mode switch.
__attribute__((noinline)) AUDIO_CORE_FLASH int32_t AudioEffectShortDelay::runSwitching(int16_t input[],
                                                         int16_t outputLeft[],
                                                         int16_t outputRight[],
                                                         int sampleCount,
                                                         bool wantPingpong)
{
    int32_t anyNonZero = 0;
    const int32_t a0 = filterCoeffs.a0, a1 = filterCoeffs.a1, b1 = filterCoeffs.b1;
    const int32_t fb = feedback;
    uint32_t head = headIdx;
    const uint32_t tis = timeInSamples;
    uint32_t silent = silentSamplesCount;
    bool pL = ppLive, mL = monoLive;
    uint32_t pD = ppDepth, mD = monoDepth;
    const uint32_t pSeed = ppSeed, mSeed = monoSeed;
    int32_t w = ppWeight;
    const int32_t target = wantPingpong ? kFadeOne : 0;
    constexpr int32_t kStep = kFadeOne / kFadeSamples;
    int32_t inR = lastFilterInRight, inL = lastFilterInLeft, inM = lastFilterInMono;
    int32_t outRs = lastFilterOutRight, outLs = lastFilterOutLeft, outMs = lastFilterOutMono;
    for (int sample = 0; sample < sampleCount; sample++)
    {
        const uint32_t d = (head - tis) & delaylineLengthMask;
        int16_t dM = 0, dL = 0, dR = 0;
        if (mL)
        {
            if (tis <= mD)
                dM = monoDelayline[d];
            else if (tis <= mD + mSeed) // before this set ran: the ping pong lines of that time
                dM = (int16_t)clamp16(pingpongDelaylineLeft[d] + pingpongDelaylineRight[d]);
        }
        if (pL)
        {
            if (tis <= pD)
            {
                dL = pingpongDelaylineLeft[d];
                dR = pingpongDelaylineRight[d];
            }
            else if (tis <= pD + pSeed) // before this set ran: the mono line of that time, on the left
                dL = monoDelayline[d];
        }
        int32_t fM = 0, fL = 0, fR = 0;
        if (mL)
        {
            fM = (a0 * dM + a1 * inM - b1 * outMs) / (1 << kFilterCoeffsShift);
            inM = dM;
            outMs = fM;
        }
        if (pL)
        {
            fR = (a0 * dR + a1 * inR - b1 * outRs) / (1 << kFilterCoeffsShift);
            fL = (a0 * dL + a1 * inL - b1 * outLs) / (1 << kFilterCoeffsShift);
            inR = dR;
            inL = dL;
            outRs = fR;
            outLs = fL;
        }
        int16_t outL, outR;
        if (mL && pL)
        {
            const int32_t m = (int16_t)fM;
            outL = (int16_t)((m * (kFadeOne - w) + (int16_t)fL * w) >> 15);
            outR = (int16_t)((m * (kFadeOne - w) + (int16_t)fR * w) >> 15);
        }
        else if (pL)
        {
            outL = fL;
            outR = fR;
        }
        else
        {
            outL = outR = fM;
        }
        *outputLeft++ = outL;
        *outputRight++ = outR;

        const int32_t inputShifted = ((*input++) << kFeedbackShift);
        int16_t sL = 0, sR = 0, sM = 0;
        const uint32_t h = head++ & delaylineLengthMask;
        if (mL)
        {
            int32_t nM = inputShifted + fM * fb;
            sM = (int16_t)signed_saturate_rshift(nM + (nM < 0 ? kFeedbackRoundBias : 0), 16, kFeedbackShift);
            monoDelayline[h] = sM;
            mD = std::min<uint32_t>(mD + 1, MAX_DELAYLINE_LENGTH);
        }
        if (pL)
        {
            int32_t nL = inputShifted + fR * fb;
            int32_t nR = fL * fb;
            sL = (int16_t)signed_saturate_rshift(nL + (nL < 0 ? kFeedbackRoundBias : 0), 16, kFeedbackShift);
            sR = (int16_t)signed_saturate_rshift(nR + (nR < 0 ? kFeedbackRoundBias : 0), 16, kFeedbackShift);
            pingpongDelaylineLeft[h] = sL;
            pingpongDelaylineRight[h] = sR;
            pD = std::min<uint32_t>(pD + 1, MAX_DELAYLINE_LENGTH);
        }
        anyNonZero |= sL | sR | sM;
        if (outL == 0 && outR == 0 && (sL | sR | sM) == 0)
        {
            if (silent < MAX_DELAYLINE_LENGTH)
                silent++;
        }
        else
        {
            silent = 0;
        }
        if (mL && pL)
        {
            // The set that is not heard any more keeps running until the heard one has been written for the whole delay time: until then
            // a switch back finds it complete (no echo of the old mode is lost), and the heard set reads its first timeInSamples from it.
            // Then it stops (its stored lines stay for the reads of the other).
            w = w < target ? std::min(w + kStep, target) : std::max(w - kStep, target);
            if (w == kFadeOne && pD >= tis)
            {
                mL = false;
                inM = outMs = 0;
            }
            else if (w == 0 && mD >= tis)
            {
                pL = false;
                inL = inR = outLs = outRs = 0;
            }
        }
    }
    ppLive = pL;
    monoLive = mL;
    ppDepth = pD;
    monoDepth = mD;
    ppWeight = w;
    lastFilterInRight = inR;
    lastFilterInLeft = inL;
    lastFilterInMono = inM;
    lastFilterOutRight = outRs;
    lastFilterOutLeft = outLs;
    lastFilterOutMono = outMs;
    headIdx = head;
    silentSamplesCount = silent;
    return anyNonZero;
}

template<bool kTrackQuiet>
AUDIO_CORE_RAM bool AudioEffectShortDelay::processBlock(int16_t input[],
                                                    int16_t outputLeft[],
                                                    int16_t outputRight[],
                                                    int sampleCount)
{
    if (noRefresh == true)
    {
        return false;
    }

    const bool wantPingpong = isPingpong != 0;
    int32_t anyNonZero;
    if (prepareBlock(wantPingpong))
    {
        if (wantPingpong)
        {
            anyNonZero = runPingpong<kTrackQuiet>(input, outputLeft, outputRight, sampleCount);
            ppDepth = std::min<uint32_t>(ppDepth + sampleCount, MAX_DELAYLINE_LENGTH);
        }
        else
        {
            anyNonZero = runMono<kTrackQuiet>(input, outputLeft, outputRight, sampleCount);
            monoDepth = std::min<uint32_t>(monoDepth + sampleCount, MAX_DELAYLINE_LENGTH);
        }
    }
    else
    {
        anyNonZero = runSwitching(input, outputLeft, outputRight, sampleCount, wantPingpong);
    }
#ifdef AUDIO_NULL_IS_SILENCE
    zeroedAllSamples = 0;
    if constexpr (kTrackQuiet)
    {
        quietSamplesCount =
          anyNonZero == 0 ? std::min<uint32_t>(quietSamplesCount + sampleCount, MAX_DELAYLINE_LENGTH) : 0;
    }
    else
    {
        quietSamplesCount = 0;
    }
#else
    (void)anyNonZero;
#endif

    return true;
}
