#include "Audio.h"
#include "play_memory.h"
#include "play_memory_read.h"
#include "utility/dspinst.h"
#include <cmath>
#include <cr_section_macros.h>

uint16_t AudioPlayMemory::gaussTab[GRANULAR_LOOKUP_SIZE];
uint16_t AudioPlayMemory::squareTab[GRANULAR_LOOKUP_SIZE];
uint16_t AudioPlayMemory::triangleTab[GRANULAR_LOOKUP_SIZE];

AUDIO_CORE_FLASH void AudioPlayMemory::initGranularShapeLookup()
{
    for (int idx = 0; idx < GRANULAR_LOOKUP_SIZE; idx++)
    {
        squareTab[idx] = std::numeric_limits<uint16_t>::max();
        gaussTab[idx] = std::numeric_limits<uint16_t>::max() *
                        sinf((float)M_PI * (idx / (float)GRANULAR_LOOKUP_SIZE));

        if (idx < GRANULAR_LOOKUP_SIZE / 2)
        {
            uint16_t linearValue =
              std::numeric_limits<uint16_t>::max() * (idx / ((float)GRANULAR_LOOKUP_SIZE / 2));

            triangleTab[idx] = linearValue;
            triangleTab[GRANULAR_LOOKUP_SIZE - 1 - idx] = linearValue;
        }
    }
}

AUDIO_CORE_FLASH void AudioPlayMemory::playGranular(uint8_t instrIdx, int8_t note)
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

    granularLoopType = mtProject.instrument[instrIdx].granular.type;

    switch (mtProject.instrument[instrIdx].granular.shape)
    {
        case granularShapeSquare: granularEnvelopeTab = squareTab; break;
        case granularShapeGauss: granularEnvelopeTab = gaussTab; break;
        case granularShapeTriangle: granularEnvelopeTab = triangleTab; break;
        default: break;
    }

    currentGranularPosition = granularForcedFlag
                                ? forcedGranularPosition
                                : mtProject.instrument[instrIdx].granular.currentPosition;
    currentGranularLength = granularLengthForcedFlag
                              ? forcedGranularLength
                              : mtProject.instrument[instrIdx].granular.grainLength;

    auto [startGranular, loopPoint1Granular, loopPoint2Granular, endGranular] =
      calculateGranularPoints(currentGranularPosition,
                              currentGranularLength,
                              mtProject.instrument[currentInstrIdx].sample.length);

    pointsInSamples.start =
      (uint32_t)((float)startGranular * ((float)currentSampleLength / MAX_16BIT));
    pointsInSamples.end = (uint32_t)((float)endGranular * ((float)currentSampleLength / MAX_16BIT));

    pointsInSamples.loop1 =
      (uint32_t)((float)loopPoint1Granular * ((float)currentSampleLength / MAX_16BIT));
    pointsInSamples.loop2 =
      (uint32_t)((float)loopPoint2Granular * ((float)currentSampleLength / MAX_16BIT));

    constrainsInSamples.loopPoint1 = pointsInSamples.loop1 - pointsInSamples.start;
    constrainsInSamples.loopPoint2 = pointsInSamples.loop2 - pointsInSamples.start;
    constrainsInSamples.loopLength = pointsInSamples.loop2 - pointsInSamples.loop1;
    constrainsInSamples.endPoint = pointsInSamples.end - pointsInSamples.start;

    next = currentStartAddress + pointsInSamples.start;
    length = currentSampleLength - pointsInSamples.start;
    iPitchCounter = reverseDirectionFlag ? constrainsInSamples.endPoint - 1 : 0;

    granularPositionRefreshFlag = 0;
    playing = 1;
    //	__enable_irq();
    AudioInterrupts();
}

AUDIO_CORE_FLASH audio_block_t* AudioPlayMemory::updateGranular()
{
    switch (granularLoopType)
    {
        case granularLoopForward: return updateGranularLoopForward(); break;
        case granularLoopBackward: return updateGranularLoopBackward(); break;
        case granularLoopPingPong: return updateGranularLoopPingPong(); break;
        default: return nullptr; break;
    }
}

AUDIO_CORE_FLASH audio_block_t* AudioPlayMemory::updateGranularLoopForward()
{
    if (reverseDirectionFlag)
        return updateGranularLoopForwardReverse();
    else
        return updateGranularLoopForwardNormal();
}
AUDIO_CORE_FLASH audio_block_t* AudioPlayMemory::updateGranularLoopBackward()
{
    loopBackwardFlag = 1;
    if (reverseDirectionFlag)
        return updateGranularLoopBackwardReverse();
    else
        return updateGranularLoopBackwardNormal();
}
AUDIO_CORE_FLASH audio_block_t* AudioPlayMemory::updateGranularLoopPingPong()
{
    if (reverseDirectionFlag)
        return updateGranularLoopPingPongReverse();
    else
        return updateGranularLoopPingPongNormal();
}

AUDIO_CORE_FLASH audio_block_t* AudioPlayMemory::updateGranularLoopForwardNormal()
{
    audio_block_t* block = nullptr;
    int16_t* in = nullptr;
    int16_t* out = nullptr;
    int32_t castPitchControl;
    float pitchFraction;
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

        if (enableInterpolation)
            interpolationCondition = ((iPitchCounter + 128 * pitchControl) < length) ? 0 : 1;
        else
            interpolationCondition = 1;

        int16_t* in_interpolation = in + 1;

        int32_t currentFractionPitchCounter = fPitchCounter * MAX_16BIT;
        int32_t currentFractionPitchControl = pitchFraction * MAX_16BIT;
        const bool isSquareEnv = (granularEnvelopeTab == squareTab);

        // Fast path (not streamed, no glide): the playhead state lives in locals for the whole block and the
        // members are written back once, so the int16 stores through out cannot alias it. Same arithmetic as below.
        if (!constrainsInSamples.pitchSmoothing)
        {
            uint32_t length = this->length;
            int32_t iPitchCounter = this->iPitchCounter;
            int32_t interpolationDif = this->interpolationDif;
            int16_t currentSampelValue = this->currentSampelValue;
            uint32_t interpolationCondition = this->interpolationCondition;

            for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
            {
                // in_interpolation is in + 1 in this variant (also after refreshGranularPosition)
                pm_read::readForward(in, iPitchCounter, interpolationCondition, currentSampelValue, interpolationDif);
                int32_t interpSample =
                  currentSampelValue +
                  (int32_t)(((currentFractionPitchCounter >> 2) * interpolationDif) >> 14);
                if (isSquareEnv)
                {
                    *out++ = (interpSample * 65535) >> 16;
                }
                else
                {
                    uint16_t volIndex = mapClamped<int>(iPitchCounter,
                                                        constrainsInSamples.loopPoint1,
                                                        constrainsInSamples.loopPoint2,
                                                        0,
                                                        GRANULAR_LOOKUP_SIZE - 1);
                    *out++ = (interpSample * granularEnvelopeTab[volIndex]) >> 16;
                }
                iPitchCounter += castPitchControl;
                currentFractionPitchCounter += currentFractionPitchControl;
                if (currentFractionPitchCounter >= MAX_16BIT)
                {
                    currentFractionPitchCounter -= MAX_16BIT;
                    iPitchCounter++;
                }
                if (iPitchCounter >= loopEndPoint)
                {
                    uint32_t exceedConstrainValue = iPitchCounter - loopEndPoint;
                    if (granularPositionRefreshFlag)
                    {
                        refreshGranularPosition();
                        length = this->length;
                        loopEndPoint =
                          std::min((int32_t)constrainsInSamples.loopPoint2,
                                   (int32_t)std::min<int>(length, constrainsInSamples.endPoint));
                        if (enableInterpolation)
                            interpolationCondition =
                              ((iPitchCounter + 128 * pitchControl) < length) ? 0 : 1;
                        else
                            interpolationCondition = 1;
                        in = (int16_t*)currentStartAddress + pointsInSamples.start;
                        in_interpolation = in + 1;
                    }
                    iPitchCounter = constrainsInSamples.loopPoint1 + exceedConstrainValue;
                    iPitchCounter =
                      keepInLoop(iPitchCounter, (int32_t)constrainsInSamples.loopPoint1, loopEndPoint);
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

                //***************************************************************************************
                // poczatek przetwarzania pitchCountera
                currentSampelValue = *(in + iPitchCounter);

                if (interpolationCondition)
                    interpolationDif = 0;
                else
                    interpolationDif = (*(in_interpolation + iPitchCounter) - currentSampelValue);

                //**************************************************************************
                int32_t interpSample =
                  currentSampelValue +
                  (int32_t)(((currentFractionPitchCounter >> 2) * interpolationDif) >> 14);
                if (isSquareEnv)
                {
                    *out++ = (interpSample * 65535) >> 16;
                }
                else
                {
                    uint16_t volIndex = mapClamped<int>(iPitchCounter,
                                                        constrainsInSamples.loopPoint1,
                                                        constrainsInSamples.loopPoint2,
                                                        0,
                                                        GRANULAR_LOOKUP_SIZE - 1);
                    *out++ = (interpSample * granularEnvelopeTab[volIndex]) >> 16;
                }

                iPitchCounter += castPitchControl;

                currentFractionPitchCounter += currentFractionPitchControl;
                if (currentFractionPitchCounter >= MAX_16BIT)
                {
                    currentFractionPitchCounter -= MAX_16BIT;
                    iPitchCounter++;
                }
                //**************************************************************************

                //***************************************************************************************
                // koniec przetwarzania pitchCountera

                //***************************************************************************************
                // warunki pętli
                if (iPitchCounter >= loopEndPoint)
                {
                    uint32_t exceedConstrainValue = iPitchCounter - loopEndPoint;
                    if (granularPositionRefreshFlag)
                    {
                        refreshGranularPosition();
                        // aktualizacja po zmianie pozycji
                        loopEndPoint =
                          std::min((int32_t)constrainsInSamples.loopPoint2,
                                   (int32_t)std::min<int>(length, constrainsInSamples.endPoint));

                        if (enableInterpolation)
                            interpolationCondition =
                              ((iPitchCounter + 128 * pitchControl) < length) ? 0 : 1;
                        else
                            interpolationCondition = 1;

                        in = (int16_t*)currentStartAddress + pointsInSamples.start;
                        in_interpolation = in + 1;
                    }

                    iPitchCounter = constrainsInSamples.loopPoint1 + exceedConstrainValue;
                    iPitchCounter =
                      keepInLoop(iPitchCounter, (int32_t)constrainsInSamples.loopPoint1, loopEndPoint);
                }
                //***************************************************************************************
                // warunki pętli
            }
        }
        next = currentStartAddress + pointsInSamples.start;
        fPitchCounter = (float)currentFractionPitchCounter / MAX_16BIT;

        return block;
    }

    return nullptr;
}
AUDIO_CORE_FLASH audio_block_t* AudioPlayMemory::updateGranularLoopForwardReverse()
{
    audio_block_t* block = nullptr;
    int16_t* in = nullptr;
    int16_t* out = nullptr;
    int32_t castPitchControl;
    float pitchFraction;
    int32_t loopStartPoint = std::max<int>((int32_t)constrainsInSamples.loopPoint1, 0);

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

        if (enableInterpolation)
            interpolationCondition = ((int)(iPitchCounter - 128 * pitchControl) > 0) ? 0 : 1;
        else
            interpolationCondition = 1;

        int16_t* in_interpolation = in - 1;

        int32_t currentFractionPitchCounter = fPitchCounter * MAX_16BIT;
        int32_t currentFractionPitchControl = pitchFraction * MAX_16BIT;
        const bool isSquareEnv = (granularEnvelopeTab == squareTab);

        // Fast path (not streamed, no glide): the playhead state lives in locals for the whole block and the
        // members are written back once, so the int16 stores through out cannot alias it. Same arithmetic as below.
        if (!constrainsInSamples.pitchSmoothing)
        {
            int32_t iPitchCounter = this->iPitchCounter;
            int32_t interpolationDif = this->interpolationDif;
            int16_t currentSampelValue = this->currentSampelValue;
            uint32_t interpolationCondition = this->interpolationCondition;

            for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
            {
                currentSampelValue = *(in + iPitchCounter);
                if (interpolationCondition)
                    interpolationDif = 0;
                else
                    interpolationDif = (*(in_interpolation + iPitchCounter) - currentSampelValue);
                int32_t interpSample =
                  currentSampelValue +
                  (int32_t)((((-currentFractionPitchCounter) >> 2) * interpolationDif) >> 14);
                if (isSquareEnv)
                {
                    *out++ = (interpSample * 65535) >> 16;
                }
                else
                {
                    uint16_t volIndex = mapClamped<int>(iPitchCounter,
                                                        constrainsInSamples.loopPoint1,
                                                        constrainsInSamples.loopPoint2,
                                                        0,
                                                        GRANULAR_LOOKUP_SIZE - 1);
                    *out++ = (interpSample * granularEnvelopeTab[volIndex]) >> 16;
                }
                iPitchCounter += castPitchControl;
                currentFractionPitchCounter += currentFractionPitchControl;
                if (currentFractionPitchCounter <= -MAX_16BIT)
                {
                    currentFractionPitchCounter += MAX_16BIT;
                    iPitchCounter--;
                }
                if (iPitchCounter <= loopStartPoint)
                {
                    uint32_t exceedConstrainValue = loopStartPoint - iPitchCounter;
                    if (granularPositionRefreshFlag)
                    {
                        refreshGranularPosition();
                        loopStartPoint = std::max<int>((int32_t)constrainsInSamples.loopPoint1, 0);
                        if (enableInterpolation)
                            interpolationCondition =
                              ((int)(iPitchCounter - 128 * pitchControl) > 0) ? 0 : 1;
                        else
                            interpolationCondition = 1;
                        in = (int16_t*)currentStartAddress + pointsInSamples.start;
                        in_interpolation = in - 1;
                    }
                    iPitchCounter = constrainsInSamples.loopPoint2 - exceedConstrainValue;
                    iPitchCounter = keepInLoop(
                      iPitchCounter, loopStartPoint, (int32_t)constrainsInSamples.loopPoint2);
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
                currentSampelValue = *(in + iPitchCounter);

                if (interpolationCondition)
                    interpolationDif = 0;
                else
                    interpolationDif = (*(in_interpolation + iPitchCounter) - currentSampelValue);

                //**************************************************************************
                int32_t interpSample =
                  currentSampelValue +
                  (int32_t)((((-currentFractionPitchCounter) >> 2) * interpolationDif) >> 14);
                if (isSquareEnv)
                {
                    *out++ = (interpSample * 65535) >> 16;
                }
                else
                {
                    uint16_t volIndex = mapClamped<int>(iPitchCounter,
                                                        constrainsInSamples.loopPoint1,
                                                        constrainsInSamples.loopPoint2,
                                                        0,
                                                        GRANULAR_LOOKUP_SIZE - 1);
                    *out++ = (interpSample * granularEnvelopeTab[volIndex]) >> 16;
                }

                iPitchCounter += castPitchControl;
                currentFractionPitchCounter += currentFractionPitchControl;

                if (currentFractionPitchCounter <= -MAX_16BIT)
                {
                    currentFractionPitchCounter += MAX_16BIT;
                    iPitchCounter--;
                }
                //**************************************************************************

                //***************************************************************************************
                // koniec przetwarzania pitchCountera

                //***************************************************************************************
                // warunki pętli
                if (iPitchCounter <= loopStartPoint)
                {
                    uint32_t exceedConstrainValue = loopStartPoint - iPitchCounter;
                    if (granularPositionRefreshFlag)
                    {
                        refreshGranularPosition();

                        // aktualizacja po zmianie pozycji
                        loopStartPoint = std::max<int>((int32_t)constrainsInSamples.loopPoint1, 0);

                        if (enableInterpolation)
                            interpolationCondition =
                              ((int)(iPitchCounter - 128 * pitchControl) > 0) ? 0 : 1;
                        else
                            interpolationCondition = 1;

                        in = (int16_t*)currentStartAddress + pointsInSamples.start;
                        in_interpolation = in - 1;
                    }
                    iPitchCounter = constrainsInSamples.loopPoint2 - exceedConstrainValue;
                    iPitchCounter = keepInLoop(
                      iPitchCounter, loopStartPoint, (int32_t)constrainsInSamples.loopPoint2);
                }
                //***************************************************************************************
                // warunki pętli
            }
        }

        next = currentStartAddress + pointsInSamples.start;
        fPitchCounter = (float)currentFractionPitchCounter / MAX_16BIT;

        return block;
    }

    return nullptr;
}
AUDIO_CORE_FLASH audio_block_t* AudioPlayMemory::updateGranularLoopBackwardNormal()
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
            interpolationCondition = (((iPitchCounter + 128 * pitchControl) < length)) ? 0 : 1;
        else
            interpolationCondition = 1;

        int16_t* in_interpolation = loopBackwardFlag ? in - 1 : in + 1;
        int32_t currentInterpolationFraction;
        const bool isSquareEnv = (granularEnvelopeTab == squareTab);
        // Fast path (not streamed, no glide): the playhead state lives in locals for the whole block and the
        // members are written back once, so the int16 stores through out cannot alias it. Same arithmetic as below.
        if (!constrainsInSamples.pitchSmoothing)
        {
            uint32_t length = this->length;
            int32_t iPitchCounter = this->iPitchCounter;
            int32_t interpolationDif = this->interpolationDif;
            int16_t currentSampelValue = this->currentSampelValue;
            uint32_t interpolationCondition = this->interpolationCondition;
            uint8_t loopBackwardFlag = this->loopBackwardFlag;

            for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
            {
                currentSampelValue = *(in + iPitchCounter);
                if (interpolationCondition)
                    interpolationDif = 0;
                else
                    interpolationDif = (*(in_interpolation + iPitchCounter) - currentSampelValue);
                currentInterpolationFraction = (currentFractionPitchCounter < 0)
                                                 ? -currentFractionPitchCounter
                                                 : currentFractionPitchCounter;
                int32_t interpSample =
                  currentSampelValue +
                  (int32_t)(((currentInterpolationFraction >> 2) * interpolationDif) >> 14);
                if (isSquareEnv)
                {
                    *out++ = (interpSample * 65535) >> 16;
                }
                else
                {
                    uint16_t volIndex = mapClamped<int>(iPitchCounter,
                                                        constrainsInSamples.loopPoint1,
                                                        constrainsInSamples.loopPoint2,
                                                        0,
                                                        GRANULAR_LOOKUP_SIZE - 1);
                    *out++ = (interpSample * granularEnvelopeTab[volIndex]) >> 16;
                }
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
                if (iPitchCounter <= loopStartPoint)
                {
                    uint32_t exceedConstrainValue = loopStartPoint - iPitchCounter;
                    if (granularPositionRefreshFlag)
                    {
                        refreshGranularPosition();
                        length = this->length;
                        loopStartPoint = std::max<int>((int32_t)constrainsInSamples.loopPoint1, 0);
                        loopEndPoint =
                          std::min((int32_t)constrainsInSamples.loopPoint2,
                                   (int32_t)std::min<int>(length, constrainsInSamples.endPoint));
                        if (enableInterpolation)
                            interpolationCondition =
                              (((iPitchCounter + 128 * pitchControl) < length)) ? 0 : 1;
                        else
                            interpolationCondition = 1;
                        in = (int16_t*)currentStartAddress + pointsInSamples.start;
                        in_interpolation = loopBackwardFlag ? in - 1 : in + 1;
                    }
                    iPitchCounter = constrainsInSamples.loopPoint2 - exceedConstrainValue;
                    iPitchCounter = keepInLoop(iPitchCounter, loopStartPoint, loopEndPoint);
                }
                else if (iPitchCounter >= loopEndPoint)
                {
                    uint32_t exceedConstrainValue = iPitchCounter - loopEndPoint;
                    if (granularPositionRefreshFlag)
                    {
                        refreshGranularPosition();
                        length = this->length;
                        loopStartPoint = std::max<int>((int32_t)constrainsInSamples.loopPoint1, 0);
                        loopEndPoint =
                          std::min((int32_t)constrainsInSamples.loopPoint2,
                                   (int32_t)std::min<int>(length, constrainsInSamples.endPoint));
                        if (enableInterpolation)
                            interpolationCondition =
                              (((iPitchCounter + 128 * pitchControl) < length)) ? 0 : 1;
                        else
                            interpolationCondition = 1;
                        in = (int16_t*)currentStartAddress + pointsInSamples.start;
                        in_interpolation = loopBackwardFlag ? in - 1 : in + 1;
                    }
                    iPitchCounter = constrainsInSamples.loopPoint2 - 1 - exceedConstrainValue;
                    iPitchCounter = keepInLoop(iPitchCounter, loopStartPoint, loopEndPoint);
                    loopBackwardFlag = 1;
                    in_interpolation = in + 1;
                    currentFractionPitchCounter = -currentFractionPitchCounter;
                }
            }

            this->iPitchCounter = iPitchCounter;
            this->interpolationDif = interpolationDif;
            this->currentSampelValue = currentSampelValue;
            this->loopBackwardFlag = loopBackwardFlag;
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
                //**********************************************
                //***************************************************************************************
                // poczatek przetwarzania pitchCountera
                currentSampelValue = *(in + iPitchCounter);

                if (interpolationCondition)
                    interpolationDif = 0;
                else
                    interpolationDif = (*(in_interpolation + iPitchCounter) - currentSampelValue);

                //**************************************************************************
                currentInterpolationFraction = (currentFractionPitchCounter < 0)
                                                 ? -currentFractionPitchCounter
                                                 : currentFractionPitchCounter;
                int32_t interpSample =
                  currentSampelValue +
                  (int32_t)(((currentInterpolationFraction >> 2) * interpolationDif) >> 14);
                if (isSquareEnv)
                {
                    *out++ = (interpSample * 65535) >> 16;
                }
                else
                {
                    uint16_t volIndex = mapClamped<int>(iPitchCounter,
                                                        constrainsInSamples.loopPoint1,
                                                        constrainsInSamples.loopPoint2,
                                                        0,
                                                        GRANULAR_LOOKUP_SIZE - 1);
                    *out++ = (interpSample * granularEnvelopeTab[volIndex]) >> 16;
                }

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
                //**************************************************************************

                //***************************************************************************************
                // koniec przetwarzania pitchCountera

                //***************************************************************************************
                // warunki pętli
                if (iPitchCounter <= loopStartPoint)
                {
                    uint32_t exceedConstrainValue = loopStartPoint - iPitchCounter;
                    if (granularPositionRefreshFlag)
                    {
                        refreshGranularPosition();

                        // aktualizacja po zmianie pozycji
                        loopStartPoint = std::max<int>((int32_t)constrainsInSamples.loopPoint1, 0);
                        loopEndPoint =
                          std::min((int32_t)constrainsInSamples.loopPoint2,
                                   (int32_t)std::min<int>(length, constrainsInSamples.endPoint));

                        if (enableInterpolation)
                            interpolationCondition =
                              (((iPitchCounter + 128 * pitchControl) < length)) ? 0 : 1;
                        else
                            interpolationCondition = 1;

                        in = (int16_t*)currentStartAddress + pointsInSamples.start;
                        in_interpolation = loopBackwardFlag ? in - 1 : in + 1;
                    }
                    iPitchCounter = constrainsInSamples.loopPoint2 - exceedConstrainValue;
                    iPitchCounter = keepInLoop(iPitchCounter, loopStartPoint, loopEndPoint);
                }
                else if (iPitchCounter >= loopEndPoint)
                {
                    uint32_t exceedConstrainValue = iPitchCounter - loopEndPoint;
                    if (granularPositionRefreshFlag)
                    {
                        refreshGranularPosition();

                        // aktualizacja po zmianie pozycji
                        loopStartPoint = std::max<int>((int32_t)constrainsInSamples.loopPoint1, 0);
                        loopEndPoint =
                          std::min((int32_t)constrainsInSamples.loopPoint2,
                                   (int32_t)std::min<int>(length, constrainsInSamples.endPoint));

                        if (enableInterpolation)
                            interpolationCondition =
                              (((iPitchCounter + 128 * pitchControl) < length)) ? 0 : 1;
                        else
                            interpolationCondition = 1;

                        in = (int16_t*)currentStartAddress + pointsInSamples.start;
                        in_interpolation = loopBackwardFlag ? in - 1 : in + 1;
                    }
                    iPitchCounter = constrainsInSamples.loopPoint2 - 1 - exceedConstrainValue;
                    iPitchCounter = keepInLoop(iPitchCounter, loopStartPoint, loopEndPoint);
                    loopBackwardFlag = 1;
                    in_interpolation = in + 1;
                    currentFractionPitchCounter = -currentFractionPitchCounter;
                }
                //***************************************************************************************
                // warunki pętli
            }
        }

        next = currentStartAddress + pointsInSamples.start;
        fPitchCounter = (float)currentFractionPitchCounter / MAX_16BIT;

        return block;
    }
    return nullptr;
}

AUDIO_CORE_FLASH audio_block_t* AudioPlayMemory::updateGranularLoopBackwardReverse()
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

        int16_t* in_interpolation = loopBackwardFlag ? in + 1 : in - 1;
        int32_t currentInterpolationFraction;
        const bool isSquareEnv = (granularEnvelopeTab == squareTab);
        // Fast path (not streamed, no glide): the playhead state lives in locals for the whole block and the
        // members are written back once, so the int16 stores through out cannot alias it. Same arithmetic as below.
        if (!constrainsInSamples.pitchSmoothing)
        {
            uint32_t length = this->length;
            int32_t iPitchCounter = this->iPitchCounter;
            int32_t interpolationDif = this->interpolationDif;
            int16_t currentSampelValue = this->currentSampelValue;
            uint32_t interpolationCondition = this->interpolationCondition;
            uint8_t loopBackwardFlag = this->loopBackwardFlag;

            for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
            {
                currentSampelValue = *(in + iPitchCounter);
                if (interpolationCondition)
                    interpolationDif = 0;
                else
                    interpolationDif = (*(in_interpolation + iPitchCounter) - currentSampelValue);
                currentInterpolationFraction = (currentFractionPitchCounter < 0)
                                                 ? -currentFractionPitchCounter
                                                 : currentFractionPitchCounter;
                int32_t interpSample =
                  currentSampelValue +
                  (int32_t)(((currentInterpolationFraction >> 2) * interpolationDif) >> 14);
                if (isSquareEnv)
                {
                    *out++ = (interpSample * 65535) >> 16;
                }
                else
                {
                    uint16_t volIndex = mapClamped<int>(iPitchCounter,
                                                        constrainsInSamples.loopPoint1,
                                                        constrainsInSamples.loopPoint2,
                                                        0,
                                                        GRANULAR_LOOKUP_SIZE - 1);
                    *out++ = (interpSample * granularEnvelopeTab[volIndex]) >> 16;
                }
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
                if (iPitchCounter >= loopEndPoint)
                {
                    uint32_t exceedConstrainValue = iPitchCounter - loopEndPoint;
                    if (granularPositionRefreshFlag)
                    {
                        refreshGranularPosition();
                        length = this->length;
                        loopStartPoint = std::max<int>((int32_t)constrainsInSamples.loopPoint1, 0);
                        loopEndPoint =
                          std::min((int32_t)constrainsInSamples.loopPoint2,
                                   (int32_t)std::min<int>(length, constrainsInSamples.endPoint));
                        if (enableInterpolation)
                            interpolationCondition =
                              (((int)(iPitchCounter - 128 * pitchControl) > 0)) ? 0 : 1;
                        else
                            interpolationCondition = 1;
                        in = (int16_t*)currentStartAddress + pointsInSamples.start;
                        in_interpolation = loopBackwardFlag ? in + 1 : in - 1;
                    }
                    iPitchCounter = constrainsInSamples.loopPoint1 ? constrainsInSamples.loopPoint1 : 1;
                    iPitchCounter += exceedConstrainValue;
                    iPitchCounter = keepInLoop(iPitchCounter, loopStartPoint, loopEndPoint);
                }
                else if (iPitchCounter <= loopStartPoint)
                {
                    uint32_t exceedConstrainValue = loopStartPoint - iPitchCounter;
                    if (granularPositionRefreshFlag)
                    {
                        refreshGranularPosition();
                        length = this->length;
                        loopStartPoint = std::max<int>((int32_t)constrainsInSamples.loopPoint1, 0);
                        loopEndPoint =
                          std::min((int32_t)constrainsInSamples.loopPoint2,
                                   (int32_t)std::min<int>(length, constrainsInSamples.endPoint));
                        if (enableInterpolation)
                            interpolationCondition =
                              (((int)(iPitchCounter - 128 * pitchControl) > 0)) ? 0 : 1;
                        else
                            interpolationCondition = 1;
                        in = (int16_t*)currentStartAddress + pointsInSamples.start;
                        in_interpolation = loopBackwardFlag ? in + 1 : in - 1;
                    }
                    iPitchCounter =
                      constrainsInSamples.loopPoint1 ? constrainsInSamples.loopPoint1 + 1 : 1;
                    iPitchCounter += exceedConstrainValue;
                    iPitchCounter = keepInLoop(iPitchCounter, loopStartPoint, loopEndPoint);
                    loopBackwardFlag = 1;
                    in_interpolation = in + 1;
                    currentFractionPitchCounter = -currentFractionPitchCounter;
                }
            }

            this->iPitchCounter = iPitchCounter;
            this->interpolationDif = interpolationDif;
            this->currentSampelValue = currentSampelValue;
            this->loopBackwardFlag = loopBackwardFlag;
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
                //**********************************************
                //***************************************************************************************
                // poczatek przetwarzania pitchCountera
                currentSampelValue = *(in + iPitchCounter);

                if (interpolationCondition)
                    interpolationDif = 0;
                else
                    interpolationDif = (*(in_interpolation + iPitchCounter) - currentSampelValue);

                //**************************************************************************

                currentInterpolationFraction = (currentFractionPitchCounter < 0)
                                                 ? -currentFractionPitchCounter
                                                 : currentFractionPitchCounter;
                int32_t interpSample =
                  currentSampelValue +
                  (int32_t)(((currentInterpolationFraction >> 2) * interpolationDif) >> 14);
                if (isSquareEnv)
                {
                    *out++ = (interpSample * 65535) >> 16;
                }
                else
                {
                    uint16_t volIndex = mapClamped<int>(iPitchCounter,
                                                        constrainsInSamples.loopPoint1,
                                                        constrainsInSamples.loopPoint2,
                                                        0,
                                                        GRANULAR_LOOKUP_SIZE - 1);
                    *out++ = (interpSample * granularEnvelopeTab[volIndex]) >> 16;
                }

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
                //**************************************************************************

                //***************************************************************************************
                // koniec przetwarzania pitchCountera
                if (iPitchCounter >= loopEndPoint)
                {
                    uint32_t exceedConstrainValue = iPitchCounter - loopEndPoint;
                    if (granularPositionRefreshFlag)
                    {
                        refreshGranularPosition();

                        // aktualizacja po zmianie pozycji
                        loopStartPoint = std::max<int>((int32_t)constrainsInSamples.loopPoint1, 0);
                        loopEndPoint =
                          std::min((int32_t)constrainsInSamples.loopPoint2,
                                   (int32_t)std::min<int>(length, constrainsInSamples.endPoint));

                        if (enableInterpolation)
                            interpolationCondition =
                              (((int)(iPitchCounter - 128 * pitchControl) > 0)) ? 0 : 1;
                        else
                            interpolationCondition = 1;

                        in = (int16_t*)currentStartAddress + pointsInSamples.start;
                        in_interpolation = loopBackwardFlag ? in + 1 : in - 1;
                    }
                    iPitchCounter = constrainsInSamples.loopPoint1 ? constrainsInSamples.loopPoint1 : 1;
                    iPitchCounter += exceedConstrainValue;
                    iPitchCounter = keepInLoop(iPitchCounter, loopStartPoint, loopEndPoint);
                }
                else if (iPitchCounter <= loopStartPoint)
                {
                    uint32_t exceedConstrainValue = loopStartPoint - iPitchCounter;
                    if (granularPositionRefreshFlag)
                    {
                        refreshGranularPosition();

                        // aktualizacja po zmianie pozycji
                        loopStartPoint = std::max<int>((int32_t)constrainsInSamples.loopPoint1, 0);
                        loopEndPoint =
                          std::min((int32_t)constrainsInSamples.loopPoint2,
                                   (int32_t)std::min<int>(length, constrainsInSamples.endPoint));

                        if (enableInterpolation)
                            interpolationCondition =
                              (((int)(iPitchCounter - 128 * pitchControl) > 0)) ? 0 : 1;
                        else
                            interpolationCondition = 1;

                        in = (int16_t*)currentStartAddress + pointsInSamples.start;
                        in_interpolation = loopBackwardFlag ? in + 1 : in - 1;
                    }
                    iPitchCounter =
                      constrainsInSamples.loopPoint1 ? constrainsInSamples.loopPoint1 + 1 : 1;
                    iPitchCounter += exceedConstrainValue;
                    iPitchCounter = keepInLoop(iPitchCounter, loopStartPoint, loopEndPoint);
                    loopBackwardFlag = 1;
                    in_interpolation = in + 1;
                    currentFractionPitchCounter = -currentFractionPitchCounter;
                }
            }
        }

        next = currentStartAddress + pointsInSamples.start;
        fPitchCounter = (float)currentFractionPitchCounter / MAX_16BIT;

        return block;
    }

    return nullptr;
}
AUDIO_CORE_FLASH audio_block_t* AudioPlayMemory::updateGranularLoopPingPongNormal()
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
            interpolationCondition = (((iPitchCounter + 128 * pitchControl) < length)) ? 0 : 1;
        else
            interpolationCondition = 1;

        int16_t* in_interpolation = loopBackwardFlag ? in - 1 : in + 1;
        int32_t currentInterpolationFraction;
        const bool isSquareEnv = (granularEnvelopeTab == squareTab);

        // Fast path (not streamed, no glide): the playhead state lives in locals for the whole block and the
        // members are written back once, so the int16 stores through out cannot alias it. Same arithmetic as below.
        if (!constrainsInSamples.pitchSmoothing)
        {
            uint32_t length = this->length;
            int32_t iPitchCounter = this->iPitchCounter;
            int32_t interpolationDif = this->interpolationDif;
            int16_t currentSampelValue = this->currentSampelValue;
            uint32_t interpolationCondition = this->interpolationCondition;
            uint8_t loopBackwardFlag = this->loopBackwardFlag;

            for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
            {
                currentSampelValue = *(in + iPitchCounter);
                if (interpolationCondition)
                    interpolationDif = 0;
                else
                    interpolationDif = (*(in_interpolation + iPitchCounter) - currentSampelValue);
                currentInterpolationFraction = (currentFractionPitchCounter < 0)
                                                 ? -currentFractionPitchCounter
                                                 : currentFractionPitchCounter;
                int32_t interpSample =
                  currentSampelValue +
                  (int32_t)(((currentInterpolationFraction >> 2) * interpolationDif) >> 14);
                if (isSquareEnv)
                {
                    *out++ = (interpSample * 65535) >> 16;
                }
                else
                {
                    uint16_t volIndex = mapClamped<int>(iPitchCounter,
                                                        constrainsInSamples.loopPoint1,
                                                        constrainsInSamples.loopPoint2,
                                                        0,
                                                        GRANULAR_LOOKUP_SIZE - 1);
                    *out++ = (interpSample * granularEnvelopeTab[volIndex]) >> 16;
                }
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
                if (iPitchCounter <= loopStartPoint)
                {
                    uint32_t exceedConstrainValue = loopStartPoint - iPitchCounter;
                    if (granularPositionRefreshFlag)
                    {
                        refreshGranularPosition();
                        length = this->length;
                        loopStartPoint = std::max<int>((int32_t)constrainsInSamples.loopPoint1, 0);
                        loopEndPoint =
                          std::min((int32_t)constrainsInSamples.loopPoint2,
                                   (int32_t)std::min<int>(length, constrainsInSamples.endPoint));
                        if (enableInterpolation)
                            interpolationCondition =
                              (((iPitchCounter + 128 * pitchControl) < length)) ? 0 : 1;
                        else
                            interpolationCondition = 1;
                        in = (int16_t*)currentStartAddress + pointsInSamples.start;
                        in_interpolation = loopBackwardFlag ? in - 1 : in + 1;
                    }
                    iPitchCounter =
                      constrainsInSamples.loopPoint1 ? constrainsInSamples.loopPoint1 + 1 : 1;
                    iPitchCounter += exceedConstrainValue;
                    iPitchCounter = keepInLoop(iPitchCounter, loopStartPoint, loopEndPoint);
                    loopBackwardFlag = 0;
                    in_interpolation = in + 1;
                    currentFractionPitchCounter = -currentFractionPitchCounter;
                }
                else if (iPitchCounter >= loopEndPoint)
                {
                    uint32_t exceedConstrainValue = iPitchCounter - loopEndPoint;
                    if (granularPositionRefreshFlag)
                    {
                        refreshGranularPosition();
                        length = this->length;
                        loopStartPoint = std::max<int>((int32_t)constrainsInSamples.loopPoint1, 0);
                        loopEndPoint =
                          std::min((int32_t)constrainsInSamples.loopPoint2,
                                   (int32_t)std::min<int>(length, constrainsInSamples.endPoint));
                        if (enableInterpolation)
                            interpolationCondition =
                              (((iPitchCounter + 128 * pitchControl) < length)) ? 0 : 1;
                        else
                            interpolationCondition = 1;
                        in = (int16_t*)currentStartAddress + pointsInSamples.start;
                        in_interpolation = loopBackwardFlag ? in - 1 : in + 1;
                    }
                    iPitchCounter = constrainsInSamples.loopPoint2 - 1 - exceedConstrainValue;
                    iPitchCounter = keepInLoop(iPitchCounter, loopStartPoint, loopEndPoint);
                    loopBackwardFlag = 1;
                    in_interpolation = in - 1;
                    currentFractionPitchCounter = -currentFractionPitchCounter;
                }
            }

            this->iPitchCounter = iPitchCounter;
            this->interpolationDif = interpolationDif;
            this->currentSampelValue = currentSampelValue;
            this->loopBackwardFlag = loopBackwardFlag;
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
                //***************************************************************************************
                // poczatek przetwarzania pitchCountera
                currentSampelValue = *(in + iPitchCounter);

                if (interpolationCondition)
                    interpolationDif = 0;
                else
                    interpolationDif = (*(in_interpolation + iPitchCounter) - currentSampelValue);

                currentInterpolationFraction = (currentFractionPitchCounter < 0)
                                                 ? -currentFractionPitchCounter
                                                 : currentFractionPitchCounter;
                //**************************************************************************
                int32_t interpSample =
                  currentSampelValue +
                  (int32_t)(((currentInterpolationFraction >> 2) * interpolationDif) >> 14);
                if (isSquareEnv)
                {
                    *out++ = (interpSample * 65535) >> 16;
                }
                else
                {
                    uint16_t volIndex = mapClamped<int>(iPitchCounter,
                                                        constrainsInSamples.loopPoint1,
                                                        constrainsInSamples.loopPoint2,
                                                        0,
                                                        GRANULAR_LOOKUP_SIZE - 1);
                    *out++ = (interpSample * granularEnvelopeTab[volIndex]) >> 16;
                }

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
                //**************************************************************************

                //***************************************************************************************
                // koniec przetwarzania pitchCountera
                if (iPitchCounter <= loopStartPoint)
                {
                    uint32_t exceedConstrainValue = loopStartPoint - iPitchCounter;
                    if (granularPositionRefreshFlag)
                    {
                        refreshGranularPosition();

                        // aktualizacja po zmianie pozycji
                        loopStartPoint = std::max<int>((int32_t)constrainsInSamples.loopPoint1, 0);
                        loopEndPoint =
                          std::min((int32_t)constrainsInSamples.loopPoint2,
                                   (int32_t)std::min<int>(length, constrainsInSamples.endPoint));

                        if (enableInterpolation)
                            interpolationCondition =
                              (((iPitchCounter + 128 * pitchControl) < length)) ? 0 : 1;
                        else
                            interpolationCondition = 1;

                        in = (int16_t*)currentStartAddress + pointsInSamples.start;
                        in_interpolation = loopBackwardFlag ? in - 1 : in + 1;
                    }
                    iPitchCounter =
                      constrainsInSamples.loopPoint1 ? constrainsInSamples.loopPoint1 + 1 : 1;
                    iPitchCounter += exceedConstrainValue;
                    iPitchCounter = keepInLoop(iPitchCounter, loopStartPoint, loopEndPoint);
                    loopBackwardFlag = 0;
                    in_interpolation = in + 1;
                    currentFractionPitchCounter = -currentFractionPitchCounter;
                }
                else if (iPitchCounter >= loopEndPoint)
                {
                    uint32_t exceedConstrainValue = iPitchCounter - loopEndPoint;
                    if (granularPositionRefreshFlag)
                    {
                        refreshGranularPosition();

                        // aktualizacja po zmianie pozycji
                        loopStartPoint = std::max<int>((int32_t)constrainsInSamples.loopPoint1, 0);
                        loopEndPoint =
                          std::min((int32_t)constrainsInSamples.loopPoint2,
                                   (int32_t)std::min<int>(length, constrainsInSamples.endPoint));

                        if (enableInterpolation)
                            interpolationCondition =
                              (((iPitchCounter + 128 * pitchControl) < length)) ? 0 : 1;
                        else
                            interpolationCondition = 1;

                        in = (int16_t*)currentStartAddress + pointsInSamples.start;
                        in_interpolation = loopBackwardFlag ? in - 1 : in + 1;
                    }
                    iPitchCounter = constrainsInSamples.loopPoint2 - 1 - exceedConstrainValue;
                    iPitchCounter = keepInLoop(iPitchCounter, loopStartPoint, loopEndPoint);
                    loopBackwardFlag = 1;
                    in_interpolation = in - 1;
                    currentFractionPitchCounter = -currentFractionPitchCounter;
                }
            }
        }

        next = currentStartAddress + pointsInSamples.start;
        fPitchCounter = (float)currentFractionPitchCounter / MAX_16BIT;

        return block;
    }

    return nullptr;
}
AUDIO_CORE_FLASH audio_block_t* AudioPlayMemory::updateGranularLoopPingPongReverse()
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

        int16_t* in_interpolation = loopBackwardFlag ? in + 1 : in - 1;
        int32_t currentInterpolationFraction;
        const bool isSquareEnv = (granularEnvelopeTab == squareTab);

        // Fast path (not streamed, no glide): the playhead state lives in locals for the whole block and the
        // members are written back once, so the int16 stores through out cannot alias it. Same arithmetic as below.
        if (!constrainsInSamples.pitchSmoothing)
        {
            uint32_t length = this->length;
            int32_t iPitchCounter = this->iPitchCounter;
            int32_t interpolationDif = this->interpolationDif;
            int16_t currentSampelValue = this->currentSampelValue;
            uint32_t interpolationCondition = this->interpolationCondition;
            uint8_t loopBackwardFlag = this->loopBackwardFlag;

            for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
            {
                currentSampelValue = *(in + iPitchCounter);
                if (interpolationCondition)
                    interpolationDif = 0;
                else
                    interpolationDif = (*(in_interpolation + iPitchCounter) - currentSampelValue);
                currentInterpolationFraction = (currentFractionPitchCounter < 0)
                                                 ? -currentFractionPitchCounter
                                                 : currentFractionPitchCounter;
                int32_t interpSample =
                  currentSampelValue +
                  (int32_t)(((currentInterpolationFraction >> 2) * interpolationDif) >> 14);
                if (isSquareEnv)
                {
                    *out++ = (interpSample * 65535) >> 16;
                }
                else
                {
                    uint16_t volIndex = mapClamped<int>(iPitchCounter,
                                                        constrainsInSamples.loopPoint1,
                                                        constrainsInSamples.loopPoint2,
                                                        0,
                                                        GRANULAR_LOOKUP_SIZE - 1);
                    *out++ = (interpSample * granularEnvelopeTab[volIndex]) >> 16;
                }
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
                if (iPitchCounter >= loopEndPoint)
                {
                    uint32_t exceedConstrainValue = iPitchCounter - loopEndPoint;
                    if (granularPositionRefreshFlag)
                    {
                        refreshGranularPosition();
                        length = this->length;
                        loopStartPoint = std::max<int>((int32_t)constrainsInSamples.loopPoint1, 0);
                        loopEndPoint =
                          std::min((int32_t)constrainsInSamples.loopPoint2,
                                   (int32_t)std::min<int>(length, constrainsInSamples.endPoint));
                        if (enableInterpolation)
                            interpolationCondition =
                              (((int)(iPitchCounter - 128 * pitchControl) > 0)) ? 0 : 1;
                        else
                            interpolationCondition = 1;
                        in = (int16_t*)currentStartAddress + pointsInSamples.start;
                        in_interpolation = loopBackwardFlag ? in + 1 : in - 1;
                    }
                    iPitchCounter = constrainsInSamples.loopPoint2 - 1 - exceedConstrainValue;
                    loopBackwardFlag = 0;
                    in_interpolation = in - 1;
                    currentFractionPitchCounter = -currentFractionPitchCounter;
                }
                if (iPitchCounter <= loopStartPoint)
                {
                    uint32_t exceedConstrainValue = loopStartPoint - iPitchCounter;
                    if (granularPositionRefreshFlag)
                    {
                        refreshGranularPosition();
                        length = this->length;
                        loopStartPoint = std::max<int>((int32_t)constrainsInSamples.loopPoint1, 0);
                        loopEndPoint =
                          std::min((int32_t)constrainsInSamples.loopPoint2,
                                   (int32_t)std::min<int>(length, constrainsInSamples.endPoint));
                        if (enableInterpolation)
                            interpolationCondition =
                              (((int)(iPitchCounter - 128 * pitchControl) > 0)) ? 0 : 1;
                        else
                            interpolationCondition = 1;
                        in = (int16_t*)currentStartAddress + pointsInSamples.start;
                        in_interpolation = loopBackwardFlag ? in + 1 : in - 1;
                    }
                    iPitchCounter =
                      constrainsInSamples.loopPoint1 ? constrainsInSamples.loopPoint1 + 1 : 1;
                    iPitchCounter += exceedConstrainValue;
                    iPitchCounter = keepInLoop(iPitchCounter, loopStartPoint, loopEndPoint);
                    loopBackwardFlag = 1;
                    in_interpolation = in + 1;
                    currentFractionPitchCounter = -currentFractionPitchCounter;
                }
            }

            this->iPitchCounter = iPitchCounter;
            this->interpolationDif = interpolationDif;
            this->currentSampelValue = currentSampelValue;
            this->loopBackwardFlag = loopBackwardFlag;
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
                currentSampelValue = *(in + iPitchCounter);

                if (interpolationCondition)
                    interpolationDif = 0;
                else
                    interpolationDif = (*(in_interpolation + iPitchCounter) - currentSampelValue);

                //**************************************************************************

                currentInterpolationFraction = (currentFractionPitchCounter < 0)
                                                 ? -currentFractionPitchCounter
                                                 : currentFractionPitchCounter;
                int32_t interpSample =
                  currentSampelValue +
                  (int32_t)(((currentInterpolationFraction >> 2) * interpolationDif) >> 14);
                if (isSquareEnv)
                {
                    *out++ = (interpSample * 65535) >> 16;
                }
                else
                {
                    uint16_t volIndex = mapClamped<int>(iPitchCounter,
                                                        constrainsInSamples.loopPoint1,
                                                        constrainsInSamples.loopPoint2,
                                                        0,
                                                        GRANULAR_LOOKUP_SIZE - 1);
                    *out++ = (interpSample * granularEnvelopeTab[volIndex]) >> 16;
                }
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
                //**************************************************************************

                //***************************************************************************************
                // koniec przetwarzania pitchCountera

                //***************************************************************************************
                // warunki pointow

                if (iPitchCounter >= loopEndPoint)
                {
                    uint32_t exceedConstrainValue = iPitchCounter - loopEndPoint;
                    if (granularPositionRefreshFlag)
                    {
                        refreshGranularPosition();

                        // aktualizacja po zmianie pozycji
                        loopStartPoint = std::max<int>((int32_t)constrainsInSamples.loopPoint1, 0);
                        loopEndPoint =
                          std::min((int32_t)constrainsInSamples.loopPoint2,
                                   (int32_t)std::min<int>(length, constrainsInSamples.endPoint));

                        if (enableInterpolation)
                            interpolationCondition =
                              (((int)(iPitchCounter - 128 * pitchControl) > 0)) ? 0 : 1;
                        else
                            interpolationCondition = 1;

                        in = (int16_t*)currentStartAddress + pointsInSamples.start;
                        in_interpolation = loopBackwardFlag ? in + 1 : in - 1;
                    }
                    iPitchCounter = constrainsInSamples.loopPoint2 - 1 - exceedConstrainValue;
                    loopBackwardFlag = 0;
                    in_interpolation = in - 1;
                    currentFractionPitchCounter = -currentFractionPitchCounter;
                }
                if (iPitchCounter <= loopStartPoint)
                {
                    uint32_t exceedConstrainValue = loopStartPoint - iPitchCounter;

                    if (granularPositionRefreshFlag)
                    {
                        refreshGranularPosition();

                        // aktualizacja po zmianie pozycji
                        loopStartPoint = std::max<int>((int32_t)constrainsInSamples.loopPoint1, 0);
                        loopEndPoint =
                          std::min((int32_t)constrainsInSamples.loopPoint2,
                                   (int32_t)std::min<int>(length, constrainsInSamples.endPoint));

                        if (enableInterpolation)
                            interpolationCondition =
                              (((int)(iPitchCounter - 128 * pitchControl) > 0)) ? 0 : 1;
                        else
                            interpolationCondition = 1;

                        in = (int16_t*)currentStartAddress + pointsInSamples.start;
                        in_interpolation = loopBackwardFlag ? in + 1 : in - 1;
                    }
                    iPitchCounter =
                      constrainsInSamples.loopPoint1 ? constrainsInSamples.loopPoint1 + 1 : 1;
                    iPitchCounter += exceedConstrainValue;
                    iPitchCounter = keepInLoop(iPitchCounter, loopStartPoint, loopEndPoint);
                    loopBackwardFlag = 1;
                    in_interpolation = in + 1;
                    currentFractionPitchCounter = -currentFractionPitchCounter;
                }
            }
        }

        next = currentStartAddress + pointsInSamples.start;
        fPitchCounter = (float)currentFractionPitchCounter / MAX_16BIT;

        return block;
    }

    return nullptr;
}
