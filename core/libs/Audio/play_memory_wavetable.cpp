#include "Audio.h"
#include "play_memory.h"
#include "utility/dspinst.h"

AUDIO_CORE_FLASH void AudioPlayMemory::playWavetable(uint8_t instrIdx, int8_t note)
{
    //	__disable_irq();
    AudioNoInterrupts();

    if (instrIdx > kSampleInstrumentsCount - 1)
        instrIdx = kSampleInstrumentsCount - 1;
    if (note > MAX_NOTE)
        note = MAX_NOTE;

    lastInstrIdxInPlay = currentInstrIdxInPlay;
    currentInstrIdxInPlay = instrIdx;
    currentInstrIdx = instrIdx;

    uint16_t windowSize = mtProject.instrument[instrIdx].sample.wavetable_window_size;
    const float* newWtNotes = nullptr;
    switch (windowSize)
    {
        case 32: newWtNotes = wt32Note; break;
        case 64: newWtNotes = wt64Note; break;
        case 128: newWtNotes = wt128Note; break;
        case 256: newWtNotes = wt256Note; break;
        case 512: newWtNotes = wt512Note; break;
        case 1024: newWtNotes = wt1024Note; break;
        case 2048: newWtNotes = wt2048Note; break;
        default: break;
    }

    refreshStartParameters();

    // The playhead is wrapped by one window per sample, which only holds while the pitch step is
    // shorter than the window. That is true for the window sizes that have a pitch table. With
    // any other size, 0 in particular, the playhead would leave the sample.
    if (newWtNotes == nullptr || windowSize > currentSampleLength)
    {
        AudioInterrupts();
        return;
    }

    wt_notes = newWtNotes;

    constrainCurrentTune(note);

    setStartPitch(note);

    applyFinetuneOnPitch(note);

    calculateGlidePitch(note);

    lastNote = note;

    wavetableWindowSize = windowSize;

    currentWindow = wavetableWindowForceFlag
                      ? forcedWavetableWindow
                      : mtProject.instrument[instrIdx].wavetableCurrentWindow;

    constrainsInSamples.endPoint =
      wavetableWindowSize * mtProject.instrument[instrIdx].sample.wavetableWindowNumber;
    constrainsInSamples.loopPoint1 = 0;
    constrainsInSamples.loopPoint2 = wavetableWindowSize;
    constrainsInSamples.loopLength = wavetableWindowSize;

    pointsInSamples.start = 0;

    next = currentStartAddress;
    length = currentSampleLength;
    iPitchCounter = 0;

    playing = 1;
    //	__enable_irq();
    AudioInterrupts();
}

AUDIO_CORE_FLASH audio_block_t* AudioPlayMemory::updateWavetable()
{
    audio_block_t* block = nullptr;
    int16_t* in = nullptr;
    int16_t* out = nullptr;
    int32_t castPitchControl;
    float pitchFraction;

    block = allocate();
    if (!block)
        return nullptr;

    if (!playing)
    {
        release(block);
        return nullptr;
    }
    else if (playing == 1)
    {
        out = block->data;

        waveTablePosition = wavetableWindowSize * currentWindow;

        uint32_t maxlength = length - length % wavetableWindowSize - wavetableWindowSize;

        if (waveTablePosition > maxlength)
        {
            waveTablePosition = maxlength;
        }

        in = (int16_t*)next + waveTablePosition;
        castPitchControl = (int32_t)pitchControl;
        pitchFraction = pitchControl - (int32_t)pitchControl;

        int32_t currentFractionPitchCounter = fPitchCounter * MAX_16BIT;
        int32_t currentFractionPitchControl = pitchFraction * MAX_16BIT;

        if (enableInterpolation)
            interpolationCondition =
              (((iPitchCounter + waveTablePosition + 128 * pitchControl) < length)) ? 0 : 1;
        else
            interpolationCondition = 1;

        int16_t* in_interpolation = in + 1;

        // Fast path (not streamed, no glide): the playhead state lives in locals for the whole block and the
        // members are written back once, so the int16 stores through out cannot alias it. Same arithmetic as below.
        if (!constrainsInSamples.pitchSmoothing)
        {
            int32_t iPitchCounter = this->iPitchCounter;
            int32_t interpolationDif = this->interpolationDif;
            int16_t currentSampelValue = this->currentSampelValue;
            const uint32_t interpolationCondition = this->interpolationCondition;
            const uint16_t wavetableWindowSize = this->wavetableWindowSize;

            for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
            {
                currentSampelValue = *(in + iPitchCounter);
                if (interpolationCondition)
                    interpolationDif = 0;
                else
                    interpolationDif = (*(in_interpolation + iPitchCounter) - currentSampelValue);
                *out++ = currentSampelValue +
                         (int32_t)(((currentFractionPitchCounter >> 2) * interpolationDif) >> 14);
                iPitchCounter += castPitchControl;
                currentFractionPitchCounter += currentFractionPitchControl;
                if (currentFractionPitchCounter >= MAX_16BIT)
                {
                    currentFractionPitchCounter -= MAX_16BIT;
                    iPitchCounter++;
                }
                if (iPitchCounter >= wavetableWindowSize)
                {
                    iPitchCounter -= wavetableWindowSize;
                }
            }

            this->iPitchCounter = iPitchCounter;
            this->interpolationDif = interpolationDif;
            this->currentSampelValue = currentSampelValue;
        }
        else
        {
            for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
            {
                //*********************************** GLIDE HANDLE
                if (constrainsInSamples.pitchSmoothing)
                {
                    if (pitchSmoothingControlCounter <= constrainsInSamples.pitchSmoothing)
                    {
                        pitchControl += pitchSmoothingControl;
                        if (pitchControl > wt_notes[MAX_NOTE - 1])
                        {
                            pitchControl = wt_notes[MAX_NOTE - 1];
                            pitchSmoothingControlCounter = constrainsInSamples.pitchSmoothing;
                        }
                        else if (pitchControl < wt_notes[MIN_NOTE])
                        {
                            pitchControl = wt_notes[MIN_NOTE];
                            pitchSmoothingControlCounter = constrainsInSamples.pitchSmoothing;
                        }
                        castPitchControl = (int32_t)pitchControl;
                        pitchFraction = pitchControl - (int32_t)pitchControl;

                        currentFractionPitchControl = pitchFraction * MAX_16BIT;

                        pitchSmoothingControlCounter++;
                    }
                }

                //***************************************************************************************
                // poczatek przetwarzania pitchCountera
                currentSampelValue = *(in + iPitchCounter);

                if (interpolationCondition)
                    interpolationDif = 0;
                else
                    interpolationDif = (*(in_interpolation + iPitchCounter) - currentSampelValue);

                *out++ = currentSampelValue +
                         (int32_t)(((currentFractionPitchCounter >> 2) * interpolationDif) >> 14);

                iPitchCounter += castPitchControl;
                currentFractionPitchCounter += currentFractionPitchControl;
                if (currentFractionPitchCounter >= MAX_16BIT)
                {
                    currentFractionPitchCounter -= MAX_16BIT;
                    iPitchCounter++;
                }

                if (iPitchCounter >= wavetableWindowSize)
                {
                    iPitchCounter -= wavetableWindowSize;
                }
            }
        }
        next = currentStartAddress + pointsInSamples.start;
        fPitchCounter = (float)currentFractionPitchCounter / MAX_16BIT;

        return block;
    }

    release(block);
    return nullptr;
}
