#include "Audio.h"
#include "play_memory.h"
#include "play_memory_read.h"
#include "utility/dspinst.h"

AUDIO_CORE_FLASH void AudioPlayMemory::playSingleShot(uint8_t instrIdx, int8_t note)
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

    refreshStartParameters();

    constrainCurrentTune(note);

    setStartPitch(note);

    applyFinetuneOnPitch(note);

    calculateGlidePitch(note);

    lastNote = note;

    uint16_t startPoint =
      pointsForceFlag ? forcedStartPoint : mtProject.instrument[currentInstrIdx].startPoint;
    uint16_t endPoint =
      pointsForceFlag ? forcedEndPoint : mtProject.instrument[currentInstrIdx].endPoint;

    if (startPoint >= endPoint)
    {
        //		__enable_irq();
        AudioInterrupts();
        return;
    }

    if (samplePointsSource != nullptr)
    {
        pointsInSamples.start = samplePointsSource->pointsInSamples.start;
        pointsInSamples.end = samplePointsSource->pointsInSamples.end;
    }
    else
    {
        pointsInSamples.start = calculatePointPositionAndSnapToZero(
          startPoint, (uint32_t)currentSampleLength, (int16_t*)currentStartAddress);
        pointsInSamples.end = calculatePointPositionAndSnapToZero(
          endPoint, (uint32_t)currentSampleLength, (int16_t*)currentStartAddress);
    }
    pointsInSamples.loop1 = 0;
    pointsInSamples.loop2 = 0;

    if ((pointsInSamples.start >= currentSampleLength) ||
        (pointsInSamples.end > currentSampleLength))
    {
        //		__enable_irq();
        AudioInterrupts();
        return;
    }

    constrainsInSamples.endPoint = pointsInSamples.end - pointsInSamples.start;

    next = currentStartAddress + pointsInSamples.start;
    length = currentSampleLength - pointsInSamples.start;
    iPitchCounter = reverseDirectionFlag ? constrainsInSamples.endPoint - 1 : 0;

    if (currentIsStreamed && channel == 0 && voiceIndex >= 0)
    {
        voiceStreamManager.notifyNoteOn((uint8_t)voiceIndex,
                                        instrIdx,
                                        pointsInSamples.start,
                                        pointsInSamples.start,
                                        pointsInSamples.end,
                                        pointsInSamples.end,
                                        currentPlayMode,
                                        reverseDirectionFlag != 0,
                                        pitchControl);
    }

    playing = 1;

    //	__enable_irq();
    AudioInterrupts();
}

AUDIO_CORE_RAM audio_block_t* AudioPlayMemory::updateSingleShot()
{
    if (reverseDirectionFlag)
        return updateSingleShotReverse();
    else
        return updateSingleShotNormal();
}

AUDIO_CORE_RAM audio_block_t* AudioPlayMemory::updateSingleShotNormal()
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
        in = (int16_t*)next;

        castPitchControl = (int32_t)pitchControl;
        pitchFraction = pitchControl - (int32_t)pitchControl;

        int32_t currentFractionPitchCounter = fPitchCounter * MAX_16BIT;
        int32_t currentFractionPitchControl = pitchFraction * MAX_16BIT;

        if (enableInterpolation)
            interpolationCondition = ((iPitchCounter + 128 * pitchControl) < length) ? 0 : 1;
        else
            interpolationCondition = 1;

        // Fast path (not streamed, no glide): the playhead state lives in locals for the whole block and the
        // members are written back once, so the int16 stores through out cannot alias it. Same arithmetic as below.
        if (!currentIsStreamed && !constrainsInSamples.pitchSmoothing)
        {
            const uint32_t length = this->length;
            int32_t iPitchCounter = this->iPitchCounter;
            int32_t interpolationDif = this->interpolationDif;
            int16_t currentSampelValue = this->currentSampelValue;
            const uint32_t interpolationCondition = this->interpolationCondition;
            uint8_t playing = this->playing;

            // the read paths (integer pitch, pitch 1.0, interpolation with one word load) are in play_memory_read.h
            pm_read::singleShotBlock<AUDIO_BLOCK_SAMPLES>(out,
                                                          in,
                                                          length,
                                                          (int32_t)constrainsInSamples.endPoint,
                                                          castPitchControl,
                                                          currentFractionPitchControl,
                                                          interpolationCondition,
                                                          iPitchCounter,
                                                          currentFractionPitchCounter,
                                                          interpolationDif,
                                                          currentSampelValue,
                                                          playing);

            this->iPitchCounter = iPitchCounter;
            this->interpolationDif = interpolationDif;
            this->currentSampelValue = currentSampelValue;
            if (!playing)
                this->playing = 0;
        }
        else
        {
            for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
            {
                if ((int32_t)length > iPitchCounter)
                {
                    //*********************************** GLIDE HANDLE
                    if (constrainsInSamples.pitchSmoothing)
                    {
                        if (pitchSmoothingControlCounter <= constrainsInSamples.pitchSmoothing)
                        {
                            pitchControl += pitchSmoothingControl;
                            if (pitchControl > notes[MAX_NOTE - 1])
                            {
                                pitchControl = notes[MAX_NOTE - 1];
                                pitchSmoothingControlCounter = constrainsInSamples.pitchSmoothing;
                            }
                            else if (pitchControl < notes[MIN_NOTE])
                            {
                                pitchControl = notes[MIN_NOTE];
                                pitchSmoothingControlCounter = constrainsInSamples.pitchSmoothing;
                            }
                            castPitchControl = (int32_t)pitchControl;
                            pitchFraction = pitchControl - (int32_t)pitchControl;

                            currentFractionPitchControl = pitchFraction * MAX_16BIT;

                            pitchSmoothingControlCounter++;
                        }
                    }
                    //***********************************************
                    currentSampelValue = readSampleAtRel(in, iPitchCounter);

                    if (interpolationCondition)
                        interpolationDif = 0;
                    else
                        interpolationDif = (readSampleAtRel(in, iPitchCounter + 1) - currentSampelValue);

                    *out++ = currentSampelValue +
                             (int32_t)(((currentFractionPitchCounter >> 2) * interpolationDif) >> 14);
                    iPitchCounter += castPitchControl;
                    currentFractionPitchCounter += currentFractionPitchControl;
                    if (currentFractionPitchCounter >= MAX_16BIT)
                    {
                        currentFractionPitchCounter -= MAX_16BIT;
                        iPitchCounter++;
                    }

                    if (iPitchCounter >= (int32_t)(constrainsInSamples.endPoint))
                    {
                        iPitchCounter = length;
                    }
                }
                else
                {
                    *out++ = 0;
                    playing = 0;
                }
            }
        }
        next = currentStartAddress + pointsInSamples.start;
        fPitchCounter = (float)currentFractionPitchCounter / MAX_16BIT;
        if (playing)
            updateStreamPlayhead(iPitchCounter, 1);
        else if (currentIsStreamed && channel == 0 && voiceIndex >= 0)
            voiceStreamManager.notifyNoteOff((uint8_t)voiceIndex);
        return block;
    }
}
AUDIO_CORE_RAM audio_block_t* AudioPlayMemory::updateSingleShotReverse()
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
        in = (int16_t*)next;

        castPitchControl = (int32_t)-pitchControl;
        pitchFraction = -(pitchControl - (int32_t)pitchControl);

        int32_t currentFractionPitchCounter = fPitchCounter * MAX_16BIT;
        int32_t currentFractionPitchControl = pitchFraction * MAX_16BIT;

        if (enableInterpolation)
            interpolationCondition = (((int)(iPitchCounter - 128 * pitchControl) > 0)) ? 0 : 1;
        else
            interpolationCondition = 1;

        // Fast path (not streamed, no glide): the playhead state lives in locals for the whole block and the
        // members are written back once, so the int16 stores through out cannot alias it. Same arithmetic as below.
        if (!currentIsStreamed && !constrainsInSamples.pitchSmoothing)
        {
            int32_t iPitchCounter = this->iPitchCounter;
            int32_t interpolationDif = this->interpolationDif;
            int16_t currentSampelValue = this->currentSampelValue;
            const uint32_t interpolationCondition = this->interpolationCondition;
            uint8_t playing = this->playing;

            for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
            {
                if ((int32_t)iPitchCounter > 0)
                {
                    currentSampelValue = readRaw(in, iPitchCounter);
                    if (interpolationCondition)
                        interpolationDif = 0;
                    else
                        interpolationDif = (readRaw(in, iPitchCounter - 1) - currentSampelValue);
                    *out++ =
                      currentSampelValue +
                      (int32_t)((((-currentFractionPitchCounter) >> 2) * interpolationDif) >> 14);
                    iPitchCounter += castPitchControl;
                    currentFractionPitchCounter += currentFractionPitchControl;
                    if (currentFractionPitchCounter <= -MAX_16BIT)
                    {
                        currentFractionPitchCounter += MAX_16BIT;
                        iPitchCounter--;
                    }
                    if ((int32_t)(iPitchCounter - castPitchControl) <= 0)
                    {
                        iPitchCounter = 0;
                    }
                }
                else
                {
                    *out++ = 0;
                    playing = 0;
                }
            }

            this->iPitchCounter = iPitchCounter;
            this->interpolationDif = interpolationDif;
            this->currentSampelValue = currentSampelValue;
            if (!playing)
                this->playing = 0;
        }
        else
        {
            for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
            {
                if ((int32_t)iPitchCounter > 0)
                {
                    //*********************************** GLIDE HANDLE
                    if (constrainsInSamples.pitchSmoothing)
                    {
                        if (pitchSmoothingControlCounter <= constrainsInSamples.pitchSmoothing)
                        {
                            pitchControl += pitchSmoothingControl;
                            if (pitchControl > notes[MAX_NOTE - 1])
                            {
                                pitchControl = notes[MAX_NOTE - 1];
                                pitchSmoothingControlCounter = constrainsInSamples.pitchSmoothing;
                            }
                            else if (pitchControl < notes[MIN_NOTE])
                            {
                                pitchControl = notes[MIN_NOTE];
                                pitchSmoothingControlCounter = constrainsInSamples.pitchSmoothing;
                            }
                            castPitchControl = (int32_t)-pitchControl;
                            pitchFraction = -(pitchControl - (int32_t)pitchControl);

                            currentFractionPitchControl = pitchFraction * MAX_16BIT;

                            pitchSmoothingControlCounter++;
                        }
                    }
                    //***********************************************
                    currentSampelValue = readSampleAtRel(in, iPitchCounter);

                    if (interpolationCondition)
                        interpolationDif = 0;
                    else
                        interpolationDif = (readSampleAtRel(in, iPitchCounter - 1) - currentSampelValue);

                    *out++ =
                      currentSampelValue +
                      (int32_t)((((-currentFractionPitchCounter) >> 2) * interpolationDif) >> 14);
                    iPitchCounter += castPitchControl;
                    currentFractionPitchCounter += currentFractionPitchControl;

                    if (currentFractionPitchCounter <= -MAX_16BIT)
                    {
                        currentFractionPitchCounter += MAX_16BIT;
                        iPitchCounter--;
                    }

                    if ((int32_t)(iPitchCounter - castPitchControl) <= 0)
                    {
                        iPitchCounter = 0;
                    }
                }
                else
                {
                    *out++ = 0;
                    playing = 0;
                }
            }
        }
        next = currentStartAddress + pointsInSamples.start;
        fPitchCounter = (float)currentFractionPitchCounter / MAX_16BIT;
        if (playing)
            updateStreamPlayhead(iPitchCounter, -1);
        else if (currentIsStreamed && channel == 0 && voiceIndex >= 0)
            voiceStreamManager.notifyNoteOff((uint8_t)voiceIndex);
        return block;
    }
}
