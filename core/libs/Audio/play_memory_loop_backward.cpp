#include "Audio.h"
#include "play_memory.h"
#include "utility/dspinst.h"
#include <cmath>

AUDIO_CORE_FLASH void AudioPlayMemory::playLoopBackward(uint8_t instrIdx, int8_t note)
{
    playLoopForward(instrIdx, note);
}

AUDIO_CORE_RAM audio_block_t* AudioPlayMemory::updateLoopBackward()
{
    if (reverseDirectionFlag)
        return updateLoopBackwardReverse();
    else
        return updateLoopBackwardNormal();
}

AUDIO_CORE_RAM audio_block_t* AudioPlayMemory::updateLoopBackwardNormal()
{
    audio_block_t* block = nullptr;
    int16_t* in = nullptr;
    int16_t* out = nullptr;
    int32_t castPitchControl;
    float pitchFraction;

    int32_t loopStartPoint = std::max<int>((int32_t)constrainsInSamples.loopPoint1, 0);
    int32_t loopEndPoint = std::min((int32_t)constrainsInSamples.loopPoint2,
                                    (int32_t)std::min<int>(length, constrainsInSamples.endPoint));

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

        int32_t interpOffset = loopBackwardFlag ? -1 : 1;

        int32_t currentInterpolationFraction;

        // Fast path (not streamed, no glide): the playhead state lives in locals for the whole block and the
        // members are written back once, so the int16 stores through out cannot alias it. Same arithmetic as below.
        if (!currentIsStreamed && !constrainsInSamples.pitchSmoothing)
        {
            const uint32_t length = this->length;
            int32_t iPitchCounter = this->iPitchCounter;
            int32_t interpolationDif = this->interpolationDif;
            int16_t currentSampelValue = this->currentSampelValue;
            const uint32_t interpolationCondition = this->interpolationCondition;
            uint8_t loopBackwardFlag = this->loopBackwardFlag;
            uint8_t playing = this->playing;

            for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
            {
                if ((int32_t)length > iPitchCounter)
                {
                    currentSampelValue = readRaw(in, iPitchCounter);
                    if (interpolationCondition)
                        interpolationDif = 0;
                    else
                        interpolationDif =
                          (readRaw(in, iPitchCounter + interpOffset) - currentSampelValue);
                    currentInterpolationFraction = (currentFractionPitchCounter < 0)
                                                     ? -currentFractionPitchCounter
                                                     : currentFractionPitchCounter;
                    *out++ = (int32_t)currentSampelValue +
                             (int32_t)(((currentInterpolationFraction >> 2) * interpolationDif) >> 14);
                    if (!loopBackwardFlag)
                    {
                        iPitchCounter += castPitchControl;
                        currentFractionPitchCounter += currentFractionPitchControl;
                        if (currentFractionPitchCounter >= MAX_16BIT)
                        {
                            currentFractionPitchCounter -= MAX_16BIT;
                            iPitchCounter++;
                        }
                    }
                    else
                    {
                        iPitchCounter -= castPitchControl;
                        currentFractionPitchCounter -= currentFractionPitchControl;
                        if (currentFractionPitchCounter <= -MAX_16BIT)
                        {
                            currentFractionPitchCounter += MAX_16BIT;
                            iPitchCounter--;
                        }
                    }
                    if ((iPitchCounter <= loopStartPoint) && loopBackwardFlag)
                    {
                        iPitchCounter =
                          constrainsInSamples.loopPoint2 - (loopStartPoint - iPitchCounter);
                        iPitchCounter = keepInLoop(iPitchCounter, loopStartPoint, loopEndPoint);
                    }
                    else if (iPitchCounter >= loopEndPoint)
                    {
                        iPitchCounter =
                          constrainsInSamples.loopPoint2 - 1 - (iPitchCounter - loopEndPoint);
                        iPitchCounter = keepInLoop(iPitchCounter, loopStartPoint, loopEndPoint);
                        loopBackwardFlag = 1;
                        interpOffset = -1;
                        currentFractionPitchCounter = -currentFractionPitchCounter;
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
            this->loopBackwardFlag = loopBackwardFlag;
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
                    //***************************************************************************************
                    // poczatek przetwarzania pitchCountera
                    currentSampelValue = readSampleAtRel(in, iPitchCounter);

                    if (interpolationCondition)
                        interpolationDif = 0;
                    else
                        interpolationDif =
                          (readSampleAtRel(in, iPitchCounter + interpOffset) - currentSampelValue);

                    currentInterpolationFraction = (currentFractionPitchCounter < 0)
                                                     ? -currentFractionPitchCounter
                                                     : currentFractionPitchCounter;

                    *out++ = (int32_t)currentSampelValue +
                             (int32_t)(((currentInterpolationFraction >> 2) * interpolationDif) >> 14);

                    if (!loopBackwardFlag)
                    {
                        iPitchCounter += castPitchControl;
                        currentFractionPitchCounter += currentFractionPitchControl;
                        if (currentFractionPitchCounter >= MAX_16BIT)
                        {
                            currentFractionPitchCounter -= MAX_16BIT;
                            iPitchCounter++;
                        }
                    }
                    else
                    {
                        iPitchCounter -= castPitchControl;

                        currentFractionPitchCounter -= currentFractionPitchControl;
                        if (currentFractionPitchCounter <= -MAX_16BIT)
                        {
                            currentFractionPitchCounter += MAX_16BIT;
                            iPitchCounter--;
                        }
                    }
                    //***************************************************************************************
                    // koniec przetwarzania pitchCountera
                    //***************************************************************************************
                    // warunki pointow
                    if ((iPitchCounter <= loopStartPoint) && loopBackwardFlag)
                    {
                        iPitchCounter =
                          constrainsInSamples.loopPoint2 - (loopStartPoint - iPitchCounter);
                        iPitchCounter = keepInLoop(iPitchCounter, loopStartPoint, loopEndPoint);
                    }
                    else if (iPitchCounter >= loopEndPoint)
                    {
                        iPitchCounter =
                          constrainsInSamples.loopPoint2 - 1 - (iPitchCounter - loopEndPoint);
                        iPitchCounter = keepInLoop(iPitchCounter, loopStartPoint, loopEndPoint);
                        loopBackwardFlag = 1;
                        interpOffset = -1;
                        currentFractionPitchCounter = -currentFractionPitchCounter;
                    }
                    //***************************************************************************************
                    // warunki pętli
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
            updateStreamPlayhead(iPitchCounter, loopBackwardFlag ? -1 : 1);
        else if (currentIsStreamed && channel == 0 && voiceIndex >= 0)
            voiceStreamManager.notifyNoteOff((uint8_t)voiceIndex);

        return block;
    }

    return nullptr;
}
AUDIO_CORE_RAM audio_block_t* AudioPlayMemory::updateLoopBackwardReverse()
{
    audio_block_t* block = nullptr;
    int16_t* in = nullptr;
    int16_t* out = nullptr;
    int32_t castPitchControl;
    float pitchFraction;
    int32_t loopStartPoint = std::max<int>((int32_t)constrainsInSamples.loopPoint1, 0);
    int32_t loopEndPoint = std::min((int32_t)constrainsInSamples.loopPoint2,
                                    (int32_t)std::min<int>(length, constrainsInSamples.endPoint));

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

        int32_t interpOffset = loopBackwardFlag ? 1 : -1;
        int32_t currentInterpolationFraction;

        // Fast path (not streamed, no glide): the playhead state lives in locals for the whole block and the
        // members are written back once, so the int16 stores through out cannot alias it. Same arithmetic as below.
        if (!currentIsStreamed && !constrainsInSamples.pitchSmoothing)
        {
            int32_t iPitchCounter = this->iPitchCounter;
            int32_t interpolationDif = this->interpolationDif;
            int16_t currentSampelValue = this->currentSampelValue;
            const uint32_t interpolationCondition = this->interpolationCondition;
            uint8_t loopBackwardFlag = this->loopBackwardFlag;
            uint8_t playing = this->playing;

            for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
            {
                if ((int32_t)iPitchCounter > 0)
                {
                    currentSampelValue = readRaw(in, iPitchCounter);
                    if (interpolationCondition)
                        interpolationDif = 0;
                    else
                        interpolationDif =
                          (readRaw(in, iPitchCounter + interpOffset) - currentSampelValue);
                    currentInterpolationFraction = (currentFractionPitchCounter < 0)
                                                     ? -currentFractionPitchCounter
                                                     : currentFractionPitchCounter;
                    *out++ = (int32_t)currentSampelValue +
                             (int32_t)(((currentInterpolationFraction >> 2) * interpolationDif) >> 14);
                    if (!loopBackwardFlag)
                    {
                        iPitchCounter += castPitchControl;
                        currentFractionPitchCounter += currentFractionPitchControl;
                        if (currentFractionPitchCounter <= -MAX_16BIT)
                        {
                            currentFractionPitchCounter += MAX_16BIT;
                            iPitchCounter--;
                        }
                    }
                    else
                    {
                        iPitchCounter -= castPitchControl;
                        currentFractionPitchCounter -= currentFractionPitchControl;
                        if (currentFractionPitchCounter >= MAX_16BIT)
                        {
                            currentFractionPitchCounter -= MAX_16BIT;
                            iPitchCounter++;
                        }
                    }
                    if ((iPitchCounter >= loopEndPoint) && loopBackwardFlag)
                    {
                        uint32_t exceedConstrainValue = iPitchCounter - loopEndPoint;
                        iPitchCounter =
                          constrainsInSamples.loopPoint1 ? constrainsInSamples.loopPoint1 : 1;
                        iPitchCounter += exceedConstrainValue;
                        iPitchCounter =
                          keepInLoop(iPitchCounter, loopStartPoint ? loopStartPoint : 1, loopEndPoint);
                    }
                    else if (iPitchCounter <= loopStartPoint)
                    {
                        iPitchCounter =
                          constrainsInSamples.loopPoint1 ? constrainsInSamples.loopPoint1 + 1 : 1;
                        iPitchCounter += (loopStartPoint - iPitchCounter);
                        loopBackwardFlag = 1;
                        interpOffset = 1;
                        currentFractionPitchCounter = -currentFractionPitchCounter;
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
            this->loopBackwardFlag = loopBackwardFlag;
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
                    //***************************************************************************************
                    // poczatek przetwarzania pitchCountera
                    currentSampelValue = readSampleAtRel(in, iPitchCounter);

                    if (interpolationCondition)
                        interpolationDif = 0;
                    else
                        interpolationDif =
                          (readSampleAtRel(in, iPitchCounter + interpOffset) - currentSampelValue);

                    currentInterpolationFraction = (currentFractionPitchCounter < 0)
                                                     ? -currentFractionPitchCounter
                                                     : currentFractionPitchCounter;

                    *out++ = (int32_t)currentSampelValue +
                             (int32_t)(((currentInterpolationFraction >> 2) * interpolationDif) >> 14);

                    if (!loopBackwardFlag)
                    {
                        iPitchCounter += castPitchControl;
                        currentFractionPitchCounter += currentFractionPitchControl;
                        if (currentFractionPitchCounter <= -MAX_16BIT)
                        {
                            currentFractionPitchCounter += MAX_16BIT;
                            iPitchCounter--;
                        }
                    }
                    else
                    {
                        iPitchCounter -= castPitchControl;

                        currentFractionPitchCounter -= currentFractionPitchControl;
                        if (currentFractionPitchCounter >= MAX_16BIT)
                        {
                            currentFractionPitchCounter -= MAX_16BIT;
                            iPitchCounter++;
                        }
                    }
                    //***************************************************************************************
                    // koniec przetwarzania pitchCountera
                    //***************************************************************************************
                    // warunki pointow
                    if ((iPitchCounter >= loopEndPoint) && loopBackwardFlag)
                    {
                        uint32_t exceedConstrainValue = iPitchCounter - loopEndPoint;
                        iPitchCounter =
                          constrainsInSamples.loopPoint1 ? constrainsInSamples.loopPoint1 : 1;
                        iPitchCounter += exceedConstrainValue;
                        // Position 0 would end the note at the guard above.
                        iPitchCounter =
                          keepInLoop(iPitchCounter, loopStartPoint ? loopStartPoint : 1, loopEndPoint);
                    }
                    else if (iPitchCounter <= loopStartPoint)
                    {
                        iPitchCounter =
                          constrainsInSamples.loopPoint1 ? constrainsInSamples.loopPoint1 + 1 : 1;
                        iPitchCounter += (loopStartPoint - iPitchCounter);
                        loopBackwardFlag = 1;
                        interpOffset = 1;
                        currentFractionPitchCounter = -currentFractionPitchCounter;
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
            updateStreamPlayhead(iPitchCounter, loopBackwardFlag ? 1 : -1);
        else if (currentIsStreamed && channel == 0 && voiceIndex >= 0)
            voiceStreamManager.notifyNoteOff((uint8_t)voiceIndex);

        return block;
    }

    return nullptr;
}
