/* Audio Library for Teensy 3.X
 * Copyright (c) 2017, Paul Stoffregen, paul@pjrc.com
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

#include "Audio.h"
#include "Platform.h"
#include "dspinst.h"
#include "effect_envelope_steady.h"
#include "mtSequencer.h"
#include "play_memory.h"
#include "util_math.h"
#include "mtWavetableEnvelopes.h"

#define SAMPLES_PER_MSEC (AUDIO_SAMPLE_RATE_EXACT / 1000.0)

AUDIO_CORE_FLASH void AudioPlayMemory::envelopeSetSustain()
{
    envelope.state = 5;
    envelope.count = 0xFFFF;
    envelope.mult_hires = envelope.sustain_mult;
    envelope.inc_hires = 0;
}

AUDIO_CORE_FLASH void AudioPlayMemory::envelopeReleaseNoteOn(float milliseconds)
{
    envelope.release_forced_count = envelopeMilliseconds2count(milliseconds);
    if (envelope.release_forced_count == 0)
        envelope.release_forced_count = 1;
}
AUDIO_CORE_FLASH void AudioPlayMemory::envelopeSetLoop(uint8_t state)
{
    envelope.loopFlag = state;
}

AUDIO_CORE_FLASH void AudioPlayMemory::envelopeSetWavetable(uint8_t mode, uint8_t shape, uint8_t bend, uint8_t sust)
{
    uint8_t newMode = (mode >= 2) ? mode : 0;
    if (envelope.wtMode != newMode)
    {
        envelope.wtPhase = 0;
        envelope.wtReleaseStartPhase = 0;
    }
    envelope.wtMode = newMode;
    envelope.wtShape = shape;
    envelope.wtBend = bend;
    envelope.wtSust = sust;
}

AUDIO_CORE_FLASH void AudioPlayMemory::envelopeSetPassFlag(uint8_t state)
{
    envelope.passFlag = state;
}

AUDIO_CORE_FLASH void AudioPlayMemory::envelopeSetIsRandom(bool value)
{
    envelope.isRandom = value;
}

AUDIO_CORE_FLASH void AudioPlayMemory::envelopeDelay(float milliseconds)
{
    //	__disable_irq();
    AudioNoInterrupts();
    uint16_t lastDelayCount = envelope.delay_count;
    envelope.delay_count = envelopeMilliseconds2count(milliseconds);
    if (envelope.delay_count == 0)
        envelope.delay_count = 1;

    if (envelope.delay_count == lastDelayCount)
    {
        AudioInterrupts();
        //		__enable_irq();
        return;
    }

    if (envelope.state == envelopePhaseDelay)
    {
        int32_t dif = envelope.delay_count - lastDelayCount;

        if ((int)(envelope.count + dif) < 1)
            envelope.count = 1;
        else if ((int)(envelope.count + dif) > 65535)
            envelope.count = 65535;
        else
            envelope.count += dif;
    }
    AudioInterrupts();
    //	__enable_irq();
}
AUDIO_CORE_FLASH void AudioPlayMemory::envelopeAttack(float milliseconds)
{
    //	__disable_irq();
    AudioNoInterrupts();

    uint16_t lastAttackCount = envelope.attack_count;

    envelope.attack_count = envelopeMilliseconds2count(milliseconds);
    if (envelope.attack_count == 0)
        envelope.attack_count = 1;

    if (envelope.attack_count == lastAttackCount)
    {
        AudioInterrupts();
        //		__enable_irq();
        return;
    }

    if (envelope.state == envelopePhaseAttack)
    {
        int32_t dif = envelope.attack_count - lastAttackCount;

        if ((int)(envelope.count + dif) < 1)
            envelope.count = 1;
        else if ((int)(envelope.count + dif) > 65535)
            envelope.count = 65535;
        else
            envelope.count += dif;

        int32_t y = 0x3FFFFFFF - envelope.mult_hires;
        int32_t x = envelope.count;

        if (x < 0)
            x = 0;

        envelope.inc_hires = x ? y / x : y;
    }
    AudioInterrupts();
    //	__enable_irq();
}
AUDIO_CORE_FLASH void AudioPlayMemory::envelopeHold(float milliseconds)
{
    //	__disable_irq();
    AudioNoInterrupts();
    uint16_t lastHoldCount = envelope.hold_count;
    envelope.hold_count = envelopeMilliseconds2count(milliseconds);
    if (envelope.hold_count == 0)
        envelope.hold_count = 1;
    if (envelope.hold_count == lastHoldCount)
    {
        AudioInterrupts();
        //		__enable_irq();
        return;
    }

    if (envelope.state == envelopePhaseHold)
    {
        int32_t dif = envelope.hold_count - lastHoldCount;

        if ((int)(envelope.count + dif) < 1)
            envelope.count = 1;
        else if ((int)(envelope.count + dif) > 65535)
            envelope.count = 65535;
        else
            envelope.count += dif;
    }
    AudioInterrupts();
    //	__enable_irq();
}
AUDIO_CORE_FLASH void AudioPlayMemory::envelopeDecay(float milliseconds)
{
    //	__disable_irq();
    AudioNoInterrupts();
    uint16_t lastDecayCount = envelope.decay_count;
    envelope.decay_count = envelopeMilliseconds2count(milliseconds);
    if (envelope.decay_count == 0)
        envelope.decay_count = 1;

    if (envelope.decay_count == lastDecayCount)
    {
        AudioInterrupts();
        //		__enable_irq();
        return;
    }

    if (envelope.state == envelopePhaseDecay)
    {
        int32_t dif = envelope.decay_count - lastDecayCount;

        if ((int)(envelope.count + dif) < 1)
            envelope.count = 1;
        else if ((int)(envelope.count + dif) > 65535)
            envelope.count = 65535;
        else
            envelope.count += dif;

        envelope.inc_hires = envelope.count
                               ? (envelope.sustain_mult - envelope.mult_hires) / envelope.count
                               : (envelope.sustain_mult - envelope.mult_hires);
    }
    AudioInterrupts();
    //	__enable_irq();
}

AUDIO_CORE_FLASH void AudioPlayMemory::envelopeSustain(float level)
{
    //	__disable_irq();
    AudioNoInterrupts();
    if (level < 0.0)
        level = 0;
    else if (level > 1.0)
        level = 1.0;
    envelope.sustain_mult = level * 1073741823.0;
    if (envelope.state == envelopePhaseSustain)
        envelope.mult_hires = envelope.sustain_mult;
    AudioInterrupts();
    //	__enable_irq();
}
AUDIO_CORE_FLASH void AudioPlayMemory::envelopeRelease(float milliseconds)
{
    //	__disable_irq();
    AudioNoInterrupts();
    uint16_t lastReleaseCount = envelope.release_count;
    envelope.release_count = envelopeMilliseconds2count(milliseconds);
    if (envelope.release_count == 0)
        envelope.release_count = 1;

    if (envelope.release_count == lastReleaseCount)
    {
        AudioInterrupts();
        //		__enable_irq();
        return;
    }

    if (envelope.state == envelopePhaseRelease)
    {
        int32_t dif = envelope.release_count - lastReleaseCount;

        if ((int)(envelope.count + dif) < 1)
            envelope.count = 1;
        else if ((int)(envelope.count + dif) > 65535)
            envelope.count = 65535;
        else
            envelope.count += dif;

        envelope.inc_hires =
          envelope.count ? (-envelope.mult_hires) / envelope.count : -envelope.mult_hires;
    }
    AudioInterrupts();
    //	__enable_irq();
}

AUDIO_CORE_FLASH void AudioPlayMemory::envelopeNoteOn(uint8_t instr_idx, int8_t note)
{
    //	__disable_irq();
    AudioNoInterrupts();
    envelope.pressedFlag = 1;
    envelope.wtPhase = 0;
    envelope.wtReleaseStartPhase = 0;

    if (envelope.state == envelopePhaseIdle || envelope.state == envelopePhaseDelay)
    {
        if (envelope.passFlag)
            envelopeSwitchPhase(envelopePhaseSustain);
        else
            envelopeSwitchPhase(envelopePhaseDelay);

        play(instr_idx, note);
    }
    else if (envelope.state != envelopePhaseForced)
    {
        envelopeSwitchPhase(envelopePhaseForced);
        stackedPlay.enable = true;
        stackedPlay.instr_idx = instr_idx;
        stackedPlay.note = note;
        stackedPlay.isPrev = false;
    }
    else
    {
        envelope.endKillReleaseFlag = 1;
        envelopeSwitchPhase(envelopePhaseDelay);
        if (stackedPlay.enable)
            stackedPlay.enable = false;
        play(instr_idx, note);
    }
    AudioInterrupts();
    //	__enable_irq();
}

AUDIO_CORE_FLASH void AudioPlayMemory::envelopeNoteOnForPrev(uint8_t instr_idx, int8_t note)
{
    //	__disable_irq();
    AudioNoInterrupts();
    envelope.pressedFlag = 1;
    envelope.wtPhase = 0;
    envelope.wtReleaseStartPhase = 0;

    if (envelope.state == envelopePhaseIdle || envelope.state == envelopePhaseDelay)
    {
        if (envelope.passFlag)
            envelopeSwitchPhase(envelopePhaseSustain);
        else
            envelopeSwitchPhase(envelopePhaseDelay);
        playForPrev(instr_idx, note);
    }
    else if (envelope.state != envelopePhaseForced)
    {
        envelopeSwitchPhase(envelopePhaseForced);
        stackedPlay.enable = true;
        stackedPlay.instr_idx = instr_idx;
        stackedPlay.note = note;
        stackedPlay.isPrev = true;
    }
    else
    {
        if (stackedPlay.enable)
            stackedPlay.enable = false;
        envelopeSwitchPhase(envelopePhaseDelay);
        playForPrev(instr_idx, note);
        envelope.endKillReleaseFlag = 1;
    }
    // nie ma dla force bo i tak wyliczy taka sama prosta wygaszania jak byla
    //	__enable_irq();
    AudioInterrupts();
}

AUDIO_CORE_FLASH void AudioPlayMemory::envelopeNoteOnForPrev(int16_t* addr, uint32_t len, uint8_t type)
{
    //	__disable_irq();
    AudioNoInterrupts();

    envelope.pressedFlag = 1;
    envelope.wtPhase = 0;
    envelope.wtReleaseStartPhase = 0;

    if (envelope.passFlag)
    {
        envelopeSwitchPhase(envelopePhaseSustain);
    }
    playForPrev(addr, len, type);
    // nie ma dla force bo i tak wyliczy taka sama prosta wygaszania jak byla
    //	__enable_irq();
    AudioInterrupts();
}
AUDIO_CORE_FLASH void AudioPlayMemory::envelopeNoteOnForPrev(int16_t* addr,
                                                       uint32_t len,
                                                       uint8_t n,
                                                       uint8_t type)
{
    //	__disable_irq();
    AudioNoInterrupts();

    envelope.pressedFlag = 1;
    envelope.wtPhase = 0;
    envelope.wtReleaseStartPhase = 0;

    if (envelope.passFlag)
    {
        envelopeSwitchPhase(envelopePhaseSustain);
    }
    playForPrev(addr, len, n, type);
    //	__enable_irq();
    AudioInterrupts();
}

AUDIO_CORE_FLASH void AudioPlayMemory::envelopeNoteOff(void)
{
    AudioNoInterrupts();

    envelope.pressedFlag = 0;
    if (stackedPlay.enable)
        stackedPlay.enable = false;

    if (envelope.passFlag)
    {
        envelopeSwitchPhase(envelopePhaseIdle);
        stop();
        return;
    }

    if (envelope.state != envelopePhaseIdle)
    {
        if (envelope.loopFlag == 1 || envelope.loopFlag == 3)
        {
            envelopeSwitchPhase(envelopePhaseIdle);
            stop();
        }
        else if (envelope.wtMode == 2)
        {
            if (envelope.wtSust > 0)
            {
                envelope.wtReleaseStartPhase = envelope.wtPhase;
                envelope.wtPhase = 0;
                envelopeSwitchPhase(envelopePhaseRelease);
            }
        }
        else
        {
            if (envelope.state == envelopePhaseDelay)
            {
                envelopeSwitchPhase(envelopePhaseIdle);
                stop();
            }
            else if (envelope.state != envelopePhaseForced)
            {
                envelopeSwitchPhase(envelopePhaseRelease);
            }
            else
                stop();
        }
    }
    else
    {
        stop();
    }
    //	__enable_irq();
    AudioInterrupts();
}

AUDIO_CORE_FLASH void AudioPlayMemory::envelopeSetIdle(void)
{
    //	__disable_irq();
    AudioNoInterrupts();
    envelope.pressedFlag = 0;
    envelopeSwitchPhase(envelopePhaseIdle);
    AudioInterrupts();
    //	__enable_irq();
}

AUDIO_CORE_RAM void AudioPlayMemory::envelopeUpdate(audio_block_t* block)
{
    //	__disable_irq();
    //	AudioNoInterrupts();
    uint32_t *p, *end;

    if (envelope.state == envelopePhaseIdle)
    {
#ifdef AUDIO_NULL_IS_SILENCE
        // an idle voice transmits nothing: downstream DSP, mixers and export taps treat missing blocks as silence
        if (block)
            AudioStream::release(block);
        return;
#else
        if (!block)
        {
            block = allocate();
            if (!block)
                return;
        }
        memset(block->data, 0, AUDIO_BLOCK_SAMPLES * 2);
        transmit(block);
        AudioStream::release(block);
        return;
#endif
    }

    if (!block)
    {
        block = allocate();
        if (!block)
            return;
        else
            memset(block->data, 0, AUDIO_BLOCK_SAMPLES * 2);
    }
    p = (uint32_t*)(block->data);
    end = p + AUDIO_BLOCK_SAMPLES / 2;

    uint32_t wtStep = 1;
    uint32_t wtRelStep = 1;
    uint32_t wtSustPhase = 0;
    if (envelope.wtMode == 2)
    {
        uint32_t durCount = (envelope.decay_count > 0) ? envelope.decay_count : (envelope.attack_count > 0 ? envelope.attack_count : 1);
        if (durCount <= 1)
        {
            durCount = envelopeMilliseconds2count(1000.0f);
            if (durCount == 0) durCount = 1;
        }
        wtStep = 65536u / durCount;
        if (wtStep == 0) wtStep = 1;
        wtRelStep = (envelope.release_count > 0) ? (65536u / envelope.release_count) : wtStep;
        if (wtRelStep == 0) wtRelStep = 1;
        if (envelope.wtSust > 0)
        {
            wtSustPhase = ((uint32_t)envelope.wtSust * 65536u) / 100u;
        }
    }
    else if (envelope.wtMode >= 3)
    {
        wtStep = (envelope.attack_count > 0) ? (65536u / envelope.attack_count) : 65536u;
        if (wtStep == 0) wtStep = 1;
    }

    // steady segment: the whole block with the gain in registers (effect_envelope_steady.h), the loop below keeps every other case
    if (envelope.wtMode < 2 &&
        env_steady::processBlock<AUDIO_BLOCK_SAMPLES>(p,
                                                      envelope.mult_hires,
                                                      envelope.inc_hires,
                                                      envelope.count,
                                                      envelope.state == envelopePhaseSustain && envelope.loopFlag &&
                                                        envelope.pressedFlag))
    {
        transmit(block);
        AudioStream::release(block);
        return;
    }

    while (p < end)
    {
        if (envelope.wtMode >= 2)
        {
            if (envelope.state == envelopePhaseForced)
            {
                if (envelope.count == 0)
                {
                    envelopeSwitchPhase(envelopePhaseDelay);
                    envelope.endKillReleaseFlag = 1;
                    if (stackedPlay.enable)
                    {
                        while (p < end)
                        {
                            *p++ = 0;
                            *p++ = 0;
                            *p++ = 0;
                            *p++ = 0;
                        }
                        if (stackedPlay.isPrev)
                            playForPrev(stackedPlay.instr_idx, stackedPlay.note);
                        else
                            play(stackedPlay.instr_idx, stackedPlay.note);
                        stackedPlay.enable = false;
                    }
                    continue;
                }
            }
            else if (envelope.wtMode == 2)
            {
                if (envelope.state != envelopePhaseRelease)
                {
                    envelope.wtPhase += wtStep;
                    if (envelope.wtSust > 0)
                    {
                        if (envelope.pressedFlag && envelope.wtPhase >= wtSustPhase)
                        {
                            envelope.wtPhase = wtSustPhase;
                        }

                        if (!envelope.pressedFlag)
                        {
                            envelope.wtReleaseStartPhase = envelope.wtPhase;
                            envelope.wtPhase = 0;
                            envelopeSwitchPhase(envelopePhaseRelease);
                        }
                    }
                    else if (envelope.wtPhase >= 65536u)
                    {
                        envelope.wtPhase = 65536u;
                        envelopeSwitchPhase(envelopePhaseIdle);
                        envelope.endReleaseFlag = 1;
                        stop();
                        while (p < end)
                        {
                            *p++ = 0;
                            *p++ = 0;
                            *p++ = 0;
                            *p++ = 0;
                        }
                        break;
                    }
                }
                else
                {
                    envelope.wtPhase += wtRelStep;
                    if (envelope.wtPhase >= 65536u)
                    {
                        envelopeSwitchPhase(envelopePhaseIdle);
                        envelope.endReleaseFlag = 1;
                        stop();
                        while (p < end)
                        {
                            *p++ = 0;
                            *p++ = 0;
                            *p++ = 0;
                            *p++ = 0;
                        }
                        break;
                    }
                }

                int32_t target_mult;
                if (envelope.state == envelopePhaseRelease)
                {
                    uint32_t relP = (envelope.wtPhase > 65536u) ? 65536u : envelope.wtPhase;
                    uint32_t startP = (envelope.wtReleaseStartPhase > 65536u) ? 65536u : envelope.wtReleaseStartPhase;
                    uint32_t curPhaseQ16 = startP + (uint32_t)(((uint64_t)(65536u - startP) * relP) >> 16);
                    int32_t envQ30 = evalWavetableEnvelopeFixed30(curPhaseQ16, envelope.wtShape, envelope.wtBend);
                    target_mult = (int32_t)(((int64_t)envQ30 * (65536u - relP)) >> 16);
                }
                else
                {
                    target_mult = evalWavetableEnvelopeFixed30(envelope.wtPhase, envelope.wtShape, envelope.wtBend);
                }
                envelope.inc_hires = 0;
                envelope.mult_hires = target_mult;
            }
            else
            {
                envelope.wtPhase = (envelope.wtPhase + wtStep) & 0xFFFF;
                int32_t raw_mult = evalWavetableEnvelopeFixed30(envelope.wtPhase, envelope.wtShape, envelope.wtBend);
                int64_t diff = 0x3FFFFFFF - raw_mult;
                int32_t target_mult = 0x3FFFFFFF - (int32_t)((diff * envelope.sustain_mult) >> 30);
                if (target_mult < 0) target_mult = 0;
                else if (target_mult > 0x3FFFFFFF) target_mult = 0x3FFFFFFF;
                envelope.inc_hires = 0;
                envelope.mult_hires = target_mult;
            }
        }
        else
        {
            // we only care about the state when completing a region
            if (envelope.state == envelopePhaseSustain)
            {
                if (envelope.loopFlag && envelope.pressedFlag)
                {
                    envelopeSwitchPhase(envelopePhaseRelease);
                }
            }

            if (envelope.count == 0)
            {
                if (envelope.state == envelopePhaseAttack)
                {
                    envelopeSwitchPhase(envelopePhaseHold);
                    if (envelope.isRandom)
                    {
                        utilMathRandom::randomSeed(timer->micros());
                        envelope.mult_hires = utilMathRandom::random(0, 0x3FFFFFFF);
                    }
                    continue;
                }
                else if (envelope.state == envelopePhaseHold)
                {
                    if (envelope.isRandom)
                    {
                        envelopeSwitchPhase(envelopePhaseHold);
                        utilMathRandom::randomSeed(timer->micros());
                        envelope.mult_hires = utilMathRandom::random(0, 0x3FFFFFFF);
                    }
                    else
                        envelopeSwitchPhase(envelopePhaseDecay);
                    continue;
                }
                else if (envelope.state == envelopePhaseDecay)
                {
                    if (envelope.loopFlag && envelope.pressedFlag)
                    {
                        envelopeSwitchPhase(envelopePhaseRelease);
                        continue;
                    }
                    else if (envelope.sustain_mult == 0 && !envelope.passFlag)
                    {
                        envelopeSwitchPhase(envelopePhaseIdle);
                        envelope.endReleaseFlag = 1;
                        stop();
                        while (p < end)
                        {
                            *p++ = 0;
                            *p++ = 0;
                            *p++ = 0;
                            *p++ = 0;
                        }
                        break;
                    }
                    else
                    {
                        envelopeSwitchPhase(envelopePhaseSustain);
                    }
                }
                else if (envelope.state == envelopePhaseSustain)
                {
                    envelopeSwitchPhase(envelopePhaseSustain);
                }
                else if (envelope.state == envelopePhaseRelease)
                {
                    if (envelope.loopFlag && envelope.pressedFlag)
                    {
                        envelopeSwitchPhase(envelopePhaseDelay);
                        continue;
                    }
                    else
                    {
                        envelopeSwitchPhase(envelopePhaseIdle);
                        envelope.endReleaseFlag = 1;
                        stop();
                        while (p < end)
                        {

                            *p++ = 0;
                            *p++ = 0;
                            *p++ = 0;
                            *p++ = 0;
                        }
                        break;
                    }
                }
                else if (envelope.state == envelopePhaseForced)
                {
                    if (envelope.passFlag)
                        envelopeSwitchPhase(envelopePhaseSustain);
                    else
                        envelopeSwitchPhase(envelopePhaseDelay);
                    envelope.endKillReleaseFlag = 1;
                    if (stackedPlay.enable)
                    {
                        while (p < end)
                        {
                            *p++ = 0;
                            *p++ = 0;
                            *p++ = 0;
                            *p++ = 0;
                        }
                        if (stackedPlay.isPrev)
                            playForPrev(stackedPlay.instr_idx, stackedPlay.note);
                        else
                        {
                            play(stackedPlay.instr_idx, stackedPlay.note);
                        }
                        stackedPlay.enable = false;
                    }
                    continue;
                }
                else if (envelope.state == envelopePhaseDelay)
                {
                    envelopeSwitchPhase(envelopePhaseAttack);
                    continue;
                }
            }
        }

        int32_t mult =
          envelope.mult_hires >> 14; // przeskalowanie na 16 bitow bo 0x3FFFFFFF zajmuje 30 bitow
        int32_t inc = envelope.inc_hires >>
                      17; // podejrzewam ze przeskalowanie na 13 bitow aby suma takich osmiu (3bity)
                          // obslugiwala 8 probek (inc_hires wyliczone dla 8 bitowego cyklu)

        envelope.sample12 = *p;
        envelope.sample34 = *(p + 1);
        envelope.sample56 = *(p + 2);
        envelope.sample78 = *(p + 3);
        //		p -= 4;
        mult += inc;
        envelope.tmp1 = signed_multiply_32x16b(mult, envelope.sample12);
        mult += inc;
        envelope.tmp2 = signed_multiply_32x16t(mult, envelope.sample12);
        envelope.sample12 = pack_16b_16b(envelope.tmp2, envelope.tmp1);
        mult += inc;
        envelope.tmp1 = signed_multiply_32x16b(mult, envelope.sample34);
        mult += inc;
        envelope.tmp2 = signed_multiply_32x16t(mult, envelope.sample34);
        envelope.sample34 = pack_16b_16b(envelope.tmp2, envelope.tmp1);
        mult += inc;
        envelope.tmp1 = signed_multiply_32x16b(mult, envelope.sample56);
        mult += inc;
        envelope.tmp2 = signed_multiply_32x16t(mult, envelope.sample56);
        envelope.sample56 = pack_16b_16b(envelope.tmp2, envelope.tmp1);
        mult += inc;
        envelope.tmp1 = signed_multiply_32x16b(mult, envelope.sample78);
        mult += inc;
        envelope.tmp2 = signed_multiply_32x16t(mult, envelope.sample78);
        envelope.sample78 = pack_16b_16b(envelope.tmp2, envelope.tmp1);

        *p++ = envelope.sample12;
        *p++ = envelope.sample34;
        *p++ = envelope.sample56;
        *p++ = envelope.sample78;

        // adjust the long-term gain using 30 bit resolution (fix #102)
        // https://github.com/PaulStoffregen/Audio/issues/102
        envelope.mult_hires += envelope.inc_hires;
        if (envelope.mult_hires < 0)
        {
            envelope.mult_hires = 0;
            envelope.count = 1;
            //			debugLog.setMaxLineCount(9);
            //			debugLog.addLine("MH<0 ");
            //			debugLog.addValue(envelope.state);
            //			debugLog.addText(" ");
            //			debugLog.addValue(envelope.count);
        }
        else if (envelope.mult_hires > 0x3FFFFFFF)
        {
            envelope.mult_hires = 0x3FFFFFFF;
            envelope.count = 1;
            //			debugLog.setMaxLineCount(9);
            //			debugLog.addLine("MH>MAX ");
            //			debugLog.addValue(envelope.state);
            //			debugLog.addText(" ");
            //			debugLog.addValue(envelope.count);
        }

        if (envelope.count > 0)
        {
            envelope.count--;
        }
        else
        {
            //			debugLog.setMaxLineCount(10);
            //			debugLog.addLine("count = 0 ");
            //			debugLog.addValue(envelope.state);
        }
    }
    transmit(block);
    AudioStream::release(block);
}

AUDIO_CORE_FLASH uint8_t AudioPlayMemory::envelopeEndRelease()
{
    return envelope.endReleaseFlag;
}
AUDIO_CORE_FLASH void AudioPlayMemory::envelopeClearEndReleaseFlag()
{
    envelope.endReleaseFlag = 0;
}

AUDIO_CORE_FLASH uint8_t AudioPlayMemory::envelopeGetEndReleaseKill()
{
    return envelope.endKillReleaseFlag;
}
AUDIO_CORE_FLASH void AudioPlayMemory::envelopeClearEndReleaseKill()
{
    envelope.endKillReleaseFlag = 0;
}

AUDIO_CORE_FLASH uint8_t AudioPlayMemory::envelopeGetState()
{
    return envelope.state;
}

AUDIO_CORE_FLASH void AudioPlayMemory::envelopeSyncTrackerSeq(uint32_t val, float seqSpeed)
{
    if (envelope.state == envelopePhaseForced)
        return;

    AudioNoInterrupts();
    uint16_t ticksOnPeriod = (6912 / 12) * envelope.syncRate;

    if (ticksOnPeriod == 0)
    {
        AudioInterrupts();
        return;
    }

    uint16_t stepShift = (envelope.startStep % 36) * (6912 / 12);

    val += stepShift;

    uint16_t currentPointInPhase = val % ticksOnPeriod;

    if (envelope.wtMode == 3)
    {
        if (ticksOnPeriod > 0)
        {
            envelope.wtPhase = ((uint32_t)currentPointInPhase * 65535u) / ticksOnPeriod;
            float lfoFreq = (seqSpeed / 15.0f);
            float periodMs = (1000.0f / lfoFreq) * envelope.syncRate;
            envelope.attack_count = envelopeMilliseconds2count(periodMs);
            if (envelope.attack_count == 0) envelope.attack_count = 1;
        }
        AudioInterrupts();
        return;
    }

    float lfoFrequency = (seqSpeed / 15.0);
    float periodTime = (1000 / lfoFrequency) * envelope.syncRate;

    if (envelope.phaseNumber[0] != -1 && envelope.phaseNumber[1] != -1)
    {
        float fullPhaseTime = periodTime / 2.0f;
        uint16_t fullPhaseCount = envelopeMilliseconds2count(fullPhaseTime);

        if (currentPointInPhase > ticksOnPeriod / 2)
        {
            envelope.state = envelope.phaseNumber[1];
            envelope.count = envelopeMilliseconds2count(
              (fullPhaseTime) * (float)((float)(currentPointInPhase - ticksOnPeriod / 2.0f) /
                                        (ticksOnPeriod / 2.0f)));
            if (envelope.count == 0)
                envelope.count = 1;

            if (envelope.phaseNumber[1] == envelopePhaseDecay)
            {
                envelope.decay_count = fullPhaseCount;
                if (envelope.decay_count == 0)
                    envelope.decay_count = 1;
                envelope.inc_hires = fullPhaseCount ? -(0x3FFFFFFF / fullPhaseCount) : 0;
                envelope.mult_hires =
                  0x3FFFFFFF + (envelope.inc_hires * (fullPhaseCount - envelope.count));
            }
            else if (envelope.phaseNumber[1] == envelopePhaseRelease)
            {
                envelope.release_count = fullPhaseCount;
                if (envelope.release_count == 0)
                    envelope.release_count = 1;
                envelope.inc_hires = 0;
                envelope.mult_hires = 0;
                // w normalnym przypadku powinno byc od ostatniego mult_hires - ale  w lfo stan
                // poczatkowy release = 1.0
            }
        }
        else
        {
            if (envelope.phaseNumber[0] == envelopePhaseAttack)
            {
                envelope.attack_count = fullPhaseCount;
                if (envelope.attack_count == 0)
                    envelope.attack_count = 1;
                envelope.inc_hires = fullPhaseCount ? 0x3FFFFFFF / fullPhaseCount : 0;
            }
            else if (envelope.phaseNumber[0] == envelopePhaseHold)
            {
                envelope.hold_count = fullPhaseCount;
                if (envelope.hold_count == 0)
                    envelope.hold_count = 1;
            }

            envelope.state = envelope.phaseNumber[0];
            envelope.count = envelopeMilliseconds2count(
              (fullPhaseTime) * (float)((float)(currentPointInPhase) / (ticksOnPeriod / 2.0f)));
            if (envelope.count == 0)
                envelope.count = 1;
            if (envelope.phaseNumber[0] == envelopePhaseAttack)
            {
                envelope.mult_hires = envelope.inc_hires * (fullPhaseCount - envelope.count);
            }
            else if (envelope.phaseNumber[0] == envelopePhaseHold)
            {
                envelope.mult_hires = 0x3FFFFFFF;
                envelope.inc_hires = 0;
            }
        }
    }
    else if (envelope.phaseNumber[0] != -1)
    {
        uint16_t fullPhaseCount = envelopeMilliseconds2count(periodTime);

        envelope.state = envelope.phaseNumber[0];
        envelope.count = envelopeMilliseconds2count(
          (periodTime) * (float)((float)currentPointInPhase / ticksOnPeriod));
        if (envelope.count == 0)
            envelope.count = 1;
        if (envelope.phaseNumber[0] == envelopePhaseAttack)
        {
            envelope.attack_count = fullPhaseCount;
            if (envelope.attack_count == 0)
                envelope.attack_count = 1;
            envelope.inc_hires = fullPhaseCount ? 0x3FFFFFFF / fullPhaseCount : 0;
            envelope.mult_hires = envelope.inc_hires * (fullPhaseCount - envelope.count);
        }
        else if (envelope.phaseNumber[0] == envelopePhaseDecay)
        {
            envelope.decay_count = fullPhaseCount;
            if (envelope.decay_count == 0)
                envelope.decay_count = 1;
            envelope.inc_hires = fullPhaseCount ? -(0x3FFFFFFF / fullPhaseCount) : 0;
            envelope.mult_hires =
              0x3FFFFFFF + (envelope.inc_hires * (fullPhaseCount - envelope.count));
        }
    }
    AudioInterrupts();
}
AUDIO_CORE_FLASH void AudioPlayMemory::envelopeSetSyncStartStep(uint16_t n)
{
    envelope.startStep = n;
}
AUDIO_CORE_FLASH void AudioPlayMemory::envelopeSetPhaseNumbers(int8_t n1, int8_t n2)
{
    envelope.phaseNumber[0] = n1;
    envelope.phaseNumber[1] = n2;
}
AUDIO_CORE_FLASH void AudioPlayMemory::envelopeSetSyncRate(float sync)
{
    envelope.syncRate = sync;
}

AUDIO_CORE_RAM void AudioPlayMemory::envelopeSwitchPhase(uint8_t nextPhase)
{
    switch (nextPhase)
    {
        case envelopePhaseIdle:
            envelope.state = envelopePhaseIdle;
            envelope.mult_hires = 0;
            envelope.inc_hires = 0;
            envelope.count = 0xFFFF;
            envelope.wtPhase = 0;
            envelope.wtReleaseStartPhase = 0;
            break;
        case envelopePhaseDelay:
            envelope.state = envelopePhaseDelay;
            envelope.count = envelope.delay_count;
            envelope.mult_hires = 0;
            envelope.inc_hires = 0;
            break;

        case envelopePhaseAttack:
            envelope.state = envelopePhaseAttack;
            envelope.count = envelope.attack_count;
            envelope.inc_hires = envelope.count ? 0x3FFFFFFF / envelope.count : 0x3FFFFFFF;
            envelope.mult_hires = 0;
            break;
        case envelopePhaseHold:
            envelope.state = envelopePhaseHold;
            envelope.count = envelope.hold_count;
            envelope.inc_hires = 0;
            envelope.mult_hires = 0x3FFFFFFF;
            break;
        case envelopePhaseDecay:
            envelope.state = envelopePhaseDecay;
            envelope.count = envelope.decay_count;
            envelope.inc_hires = envelope.count
                                   ? (envelope.sustain_mult - 0x3FFFFFFF) / envelope.count
                                   : (envelope.sustain_mult - 0x3FFFFFFF);
            envelope.mult_hires = 0x3FFFFFFF;
            break;
        case envelopePhaseSustain:
            envelope.state = envelopePhaseSustain;
            envelope.count = 0xFFFF;
            envelope.inc_hires = 0;
            envelope.mult_hires = envelope.sustain_mult;
            break;
        case envelopePhaseRelease:
            envelope.state = envelopePhaseRelease;
            envelope.count = envelope.release_count;
            envelope.inc_hires =
              envelope.count ? (-envelope.mult_hires) / envelope.count : (-envelope.mult_hires);
            // mult bez zmian
            break;
        case envelopePhaseForced:
            envelope.state = envelopePhaseForced;
            envelope.count = envelope.release_forced_count;
            envelope.inc_hires =
              envelope.count ? (-envelope.mult_hires) / envelope.count : (-envelope.mult_hires);
            // mult bez zmian
            break;
        default: break;
    }
}

AUDIO_CORE_FLASH uint16_t AudioPlayMemory::envelopeMilliseconds2count(float milliseconds)
{
    if (milliseconds < 0.0)
        milliseconds = 0.0;
    uint32_t c = ((uint32_t)(milliseconds * SAMPLES_PER_MSEC) + 7) >> 3;
    if (c > 65535)
        c = 65535; // allow up to 11.88 seconds
    return c;
}

#undef SAMPLES_PER_MSEC
