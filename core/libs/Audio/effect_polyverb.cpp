// Copyright 2020 Wojciech Jakóbczyk (jakobczyk.woj@gmail.com)

#include "effect_polyverb.h"
#include "cr_mtb_buffer.h"
#include <cr_section_macros.h>

extern uint16_t externalRamReverbBuff[];
#ifdef PLATFORM_IMX
// Place buffer E (8 of 12 reads and 8 of 11 writes per reverb sample) in internal SRAM on i.MX.
static uint16_t internalRamReverbBuff[16384] __attribute__((section(".noinit")));
#else
// On MK66 keep buffer E in external SDRAM to preserve internal heap/stack headroom.
static __NOINIT(EXTERNAL_RAM) uint16_t internalRamReverbBuff[16384];
#endif

AudioEffectPolyverb::AudioEffectPolyverb()
  : AudioStream(2, inputQueueArray)
{
    buffer = internalRamReverbBuff;
    buffer2 = externalRamReverbBuff;
    reverb.Init(buffer, buffer2);

    reverb.SetInputGain(0.1f);
    reverb.SetDiffusion(0.625f);
    setTime(0.7f);
    setDamp(0.2f);
    setAmount(0.8f);
    blockUpdate();
}

AudioEffectPolyverb::~AudioEffectPolyverb()
{
}

#ifdef AUDIO_NULL_IS_SILENCE
static bool isSilentOrMissing(const audio_block_t* block)
{
    if (block == NULL)
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

AUDIO_CORE_RAM void AudioEffectPolyverb::update(void)
{
    if (isBlockedUpdate)
        return;

    audio_block_t* block_left;
    audio_block_t* block_right;

#ifdef AUDIO_NULL_IS_SILENCE
    // Peek read-only first so shared zero blocks while asleep never allocate or copy memory.
    audio_block_t* ro_left = receiveReadOnly(0);
    audio_block_t* ro_right = receiveReadOnly(1);
    const bool silentIn = isSilentOrMissing(ro_left) && isSilentOrMissing(ro_right);
    if (silentIn && reverb.IsQuiet())
    {
        // Silent input and a reverb that has decayed below 1 LSB everywhere (the line buffers are zero): nothing to compute, nothing to
        // transmit. The next block with sound starts from a clean reverb.
        if (ro_left)
            release(ro_left);
        if (ro_right)
            release(ro_right);
        reverb.SkipQuiet(AUDIO_BLOCK_SAMPLES);
        return;
    }
    auto toWritable = [this](audio_block_t*& ro) -> audio_block_t*
    {
        if (ro == NULL)
        {
            audio_block_t* blk = allocate();
            if (blk)
                memset(blk->data, 0, sizeof(blk->data));
            return blk;
        }
        if (ro->ref_count == 1)
        {
            audio_block_t* blk = ro;
            ro = NULL;
            return blk;
        }
        audio_block_t* blk = allocate();
        if (blk)
        {
            memcpy(blk->data, ro->data, sizeof(blk->data));
            release(ro);
            ro = NULL;
        }
        return blk;
    };
    block_left = toWritable(ro_left);
    block_right = block_left ? toWritable(ro_right) : NULL;
    if (block_left == NULL || block_right == NULL)
    {
        if (ro_left)
            release(ro_left);
        if (ro_right)
            release(ro_right);
        if (block_left)
            release(block_left);
        if (block_right)
            release(block_right);
        return;
    }
#else
    block_left = receiveWritable(0);
    if (block_left == NULL)
        return;
    block_right = receiveWritable(1);
    if (block_right == NULL)
    {
        release(block_left);
        return;
    }
#endif

    reverb.Process(block_left->data, block_right->data, AUDIO_BLOCK_SAMPLES);
#ifdef AUDIO_NULL_IS_SILENCE
    if (!silentIn)
        reverb.ClearQuiet();
#endif

    transmit(block_left, 0);
    transmit(block_right, 1);

    release(block_left);
    release(block_right);
}

AUDIO_CORE_FLASH void AudioEffectPolyverb::reset(void)
{
    blockUpdate();
    reverb.Reset();
    unblockUpdate();
}

AUDIO_CORE_FLASH bool AudioEffectPolyverb::isActive(void)
{
    return reverb.IsActive();
}

AUDIO_CORE_FLASH void AudioEffectPolyverb::setActiveThreshold(float threshold)
{
    reverb.SetActiveThreshold(threshold);
}
