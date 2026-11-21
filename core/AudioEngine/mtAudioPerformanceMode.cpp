#include "ampLogarythmicValues.h"
#include "mtAudioEngine.h"
//*******************************************change
void playerEngine ::changeVolumePerformanceMode(int8_t value)
{
    performanceMod.volume = value;
    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::volume] = 1;
    setRefreshBit(VOLUME_MASK);
}

void playerEngine ::changePanningPerformanceMode(int8_t value)
{
    performanceMod.panning = value;
    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::panning] = 1;
    setRefreshBit(PANNING_MASK);
}

void playerEngine ::changeBitDepthPerformanceMode(int8_t value)
{
    if ((trackControlParameter[(int)controlType::performanceMode][(int)parameterList::bitDepth] !=
         1) &&
        (value == 0))
        return;
    performanceMod.bitDepth = value;

    uint8_t bitDepth;

    if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::bitDepth] ||
        trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::bitDepth])
        bitDepth = currentSeqModValues.bitDepth;
    else
        bitDepth = mtProject.instrument[currentInstrument_idx].bitDepth;

    if (bitDepth + value > BIT_DEPTH_MAX)
        currentPerformanceValues.bitDepth = BIT_DEPTH_MAX;
    else if (bitDepth + value < BIT_DEPTH_MIN)
        currentPerformanceValues.bitDepth = BIT_DEPTH_MIN;
    else
        currentPerformanceValues.bitDepth = bitDepth + value;

    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::bitDepth] = 1;

    modBitDepth(currentPerformanceValues.bitDepth);
}

void playerEngine ::changeOverdrivePerformanceMode(int8_t value)
{
    if ((trackControlParameter[(int)controlType::performanceMode][(int)parameterList::overdrive] !=
         1) &&
        (value == 0))
        return;
    performanceMod.overdrive = value;

    uint8_t overdrive;

    if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::overdrive] ||
        trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::overdrive])
        overdrive = currentSeqModValues.overdrive;
    else
        overdrive = mtProject.instrument[currentInstrument_idx].overdrive;

    if (overdrive + value > OVERDRIVE_MAX)
        currentPerformanceValues.overdrive = OVERDRIVE_MAX;
    else if (overdrive + value < OVERDRIVE_MIN)
        currentPerformanceValues.overdrive = OVERDRIVE_MIN;
    else
        currentPerformanceValues.overdrive = overdrive + value;

    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::overdrive] = 1;

    modOverdrive(currentPerformanceValues.overdrive);
}

void playerEngine ::changeTunePerformanceMode(int8_t value)
{
    if ((trackControlParameter[(int)controlType::performanceMode][(int)parameterList::tune] != 1) &&
        (value == 0))
        return;

    performanceMod.tune = value;

    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setCurrentInstrIdx(
          currentInstrument_idx); // play mem dopiero aktualizuje index na play, a czasem korzysta
                                  // sie wczesniej z funkcji
    }
    int8_t localTune;

    if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::tune] ||
        trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::tune])
        localTune = currentSeqModValues.tune;
    else
        localTune = mtProject.instrument[currentInstrument_idx].tune;

    if (localTune + value > MAX_INSTRUMENT_TUNE)
        currentPerformanceValues.tune = MAX_INSTRUMENT_TUNE;
    else if (localTune + value < MIN_INSTRUMENT_TUNE)
        currentPerformanceValues.tune = MIN_INSTRUMENT_TUNE;
    else
        currentPerformanceValues.tune = localTune + value;

    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::tune] = 1;
    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setTuneForceFlag();
        playMemPtr[channel]->setForcedTune(currentPerformanceValues.tune);
        playMemPtr[channel]->setTune(currentPerformanceValues.tune, currentNote);
    }
}
void playerEngine ::changeDelaySendPerformanceMode(int8_t value)
{
    performanceMod.delaySend = value;
    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::delaySend] = 1;

    setRefreshBit(DELAY_SEND_MASK);
}

void playerEngine::changeReverbSendPerformanceMode(int8_t value)
{
    performanceMod.reverbSend = value;
    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::reverbSend] = 1;
    setRefreshBit(REVERB_SEND_MASK);
}

void playerEngine::changeStartPointPerformanceMode(int32_t value)
{
    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setCurrentInstrIdx(
          currentInstrument_idx); // play mem dopiero aktualizuje index na play, a czasem korzysta
                                  // sie wczesniej z funkcji
    }
    changePointsPerformanceMode(
      value,
      trackControlParameter[(int)controlType::performanceMode][(int)parameterList::endPoint]
        ? performanceMod.endPoint
        : 0);
}
void playerEngine::changeEndPointPerformanceMode(int32_t value)
{
    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setCurrentInstrIdx(
          currentInstrument_idx); // play mem dopiero aktualizuje index na play, a czasem korzysta
                                  // sie wczesniej z funkcji
    }
    int32_t localEndPointMod = (int32_t)std::roundf(
      mapClamped<float>((float)value, -100.0f, 100.0f, (float)-MAX_16BIT, (float)MAX_16BIT));
    changePointsPerformanceMode(
      trackControlParameter[(int)controlType::performanceMode][(int)parameterList::startPoint]
        ? performanceMod.startPoint
        : 0,
      localEndPointMod);
}

void playerEngine::changePointsPerformanceMode(int32_t spValue, int32_t epValue)
{
    if ((trackControlParameter[(int)controlType::performanceMode][(int)parameterList::endPoint] !=
         1) &&
        (epValue == 0) &&
        (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::startPoint] !=
         1) &&
        (spValue == 0))
        return;

    performanceMod.endPoint = epValue;
    performanceMod.startPoint = spValue;

    int32_t startPoint, loopPoint1, loopPoint2, endPoint;

    if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::startPoint] ||
        trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::startPoint])
        startPoint = currentSeqModValues.startPoint;
    else
        startPoint = mtProject.instrument[currentInstrument_idx].startPoint;

    if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::loopPoint1] ||
        trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::loopPoint1])
        loopPoint1 = currentSeqModValues.loopPoint1;
    else
        loopPoint1 = mtProject.instrument[currentInstrument_idx].loopPoint1;

    if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::loopPoint2] ||
        trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::loopPoint2])
        loopPoint2 = currentSeqModValues.loopPoint2;
    else
        loopPoint2 = mtProject.instrument[currentInstrument_idx].loopPoint2;

    if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::endPoint] ||
        trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::endPoint])
        endPoint = currentSeqModValues.endPoint;
    else
        endPoint = mtProject.instrument[currentInstrument_idx].endPoint;

    if (epValue != 0)
        trackControlParameter[(int)controlType::performanceMode][(int)parameterList::endPoint] = 1;
    if (spValue != 0)
        trackControlParameter[(int)controlType::performanceMode][(int)parameterList::startPoint] =
          1;

    if (mtProject.instrument[currentInstrument_idx].playMode != playModeSingleShot)
    {
        startPoint += spValue;
        if (startPoint < SAMPLE_POINT_POS_MIN)
            startPoint = SAMPLE_POINT_POS_MIN;
        else if (startPoint > SAMPLE_POINT_POS_MAX)
            startPoint = SAMPLE_POINT_POS_MAX;

        endPoint += epValue;
        if (endPoint < SAMPLE_POINT_POS_MIN)
            endPoint = SAMPLE_POINT_POS_MIN;
        else if (endPoint > SAMPLE_POINT_POS_MAX)
            endPoint = SAMPLE_POINT_POS_MAX;

        uint32_t loopSize = loopPoint2 - loopPoint1;

        if (endPoint > loopPoint2)
        {
            if (startPoint > loopPoint1)
            {
                if ((int32_t)(startPoint + 1 + loopSize) > (int32_t)endPoint)
                {
                    loopPoint2 = endPoint - 1;
                    loopPoint1 = loopPoint2 - loopSize;
                    startPoint = loopPoint1 > 0 ? loopPoint1 - 1 : 0;
                }
                else
                {
                    loopPoint1 = startPoint + 1;
                    loopPoint2 = loopPoint1 + loopSize;
                }
            }
            // else endPoint > od loopPoint2 , startPoint < od loopPoint1  - nic nie trzeba ruszac
        }
        else
        {
            if (startPoint > loopPoint1)
            {
                startPoint = loopPoint1 > 0 ? loopPoint1 - 1 : 0;
                endPoint =
                  loopPoint2 < SAMPLE_POINT_POS_MAX ? loopPoint2 + 1 : SAMPLE_POINT_POS_MAX;
            }
            else
            {
                if ((int32_t)(endPoint - 1 - loopSize) < (int32_t)startPoint)
                {
                    loopPoint1 = startPoint + 1;
                    loopPoint2 = startPoint + loopSize;
                    endPoint =
                      loopPoint2 < SAMPLE_POINT_POS_MAX ? loopPoint2 + 1 : SAMPLE_POINT_POS_MAX;
                }
                else
                {
                    loopPoint2 = endPoint - 1;
                    loopPoint1 = loopPoint2 - loopSize;
                }
            }
        }
    }
    else
    {
        startPoint += spValue;
        if (startPoint < SAMPLE_POINT_POS_MIN)
            startPoint = SAMPLE_POINT_POS_MIN;
        else if (startPoint > SAMPLE_POINT_POS_MAX)
            startPoint = SAMPLE_POINT_POS_MAX;

        endPoint += epValue;
        if (endPoint < SAMPLE_POINT_POS_MIN)
            endPoint = SAMPLE_POINT_POS_MIN;
        else if (endPoint > SAMPLE_POINT_POS_MAX)
            endPoint = SAMPLE_POINT_POS_MAX;
    }
    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setForcedPoints(startPoint, loopPoint1, loopPoint2, endPoint);
        playMemPtr[channel]->setPointsForceFlag();
    }
}

void playerEngine ::changeCutoffPerformanceMode(
  int8_t value) // przed ta funkcja musi byc wowołana funkcja changeFilterTypePerformanceMode
{
    if ((trackControlParameter[(int)controlType::performanceMode]
                              [(int)parameterList::filterCutoff] != 1) &&
        (value == 0))
        return;
    performanceMod.cutoff = value;

    float cutoff;

    if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::filterCutoff] ||
        trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::filterCutoff])
    {
        cutoff = currentSeqModValues.filterCutoff;
    }
    else
    {
        cutoff = mtProject.instrument[currentInstrument_idx].cutOff;
    }

    if (cutoff * 100 + value > 100)
        currentPerformanceValues.filterCutoff = 1.0;
    else if (cutoff * 100 + value < 0)
        currentPerformanceValues.filterCutoff = 0.0;
    else
        currentPerformanceValues.filterCutoff = cutoff + (value / 100.0);

    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::filterCutoff] = 1;
    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::filterEnable] = 1;
    currentPerformanceValues.filterEnable = 1;
    filterConnect();

    if (!isActiveEnvelope(envCutoff))
        modCutoff(currentPerformanceValues.filterCutoff);
}
void playerEngine ::changeFilterTypePerformanceMode(uint8_t mode)
{
    if ((trackControlParameter[(int)controlType::performanceMode][(int)parameterList::filterType] !=
         1) &&
        (mode == 0))
        return;
    if ((mode < 1) || (mode > 3))
        return;

    performanceMod.filterType = mode;

    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::filterType] = 1;
    currentPerformanceValues.filterType = mode - 1;
    changeFilterType(currentPerformanceValues.filterType);
}

void playerEngine ::changeSamplePlaybackPerformanceMode(uint8_t value)
{
    trackControlParameter[(int)controlType::performanceMode]
                         [(int)parameterList::samplePlaybeckDirection] = 1;

    performanceMod.reversePlayback = value;

    if (value)
    {
        if (trackControlParameter[(int)controlType::sequencerMode]
                                 [(int)parameterList::samplePlaybeckDirection] ||
            trackControlParameter[(int)controlType::sequencerMode2]
                                 [(int)parameterList::samplePlaybeckDirection])
        {
            for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
            {
                if (currentSeqModValues.reversePlayback)
                {
                    playMemPtr[channel]->clearReverse();
                }
                else
                {
                    playMemPtr[channel]->setReverse();
                }
            }
        }
        else
        {
            for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
            {
                playMemPtr[channel]->setReverse();
            }
        }
    }
    else
    {
        if (trackControlParameter[(int)controlType::sequencerMode]
                                 [(int)parameterList::samplePlaybeckDirection] ||
            trackControlParameter[(int)controlType::sequencerMode2]
                                 [(int)parameterList::samplePlaybeckDirection])
        {
            for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
            {
                if (currentSeqModValues.reversePlayback)
                {
                    playMemPtr[channel]->setReverse();
                }
                else
                {
                    playMemPtr[channel]->clearReverse();
                }
            }
        }
        else
        {
            for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
            {
                playMemPtr[channel]->clearReverse();
            }
        }
    }
}

void playerEngine::changeWavetableWindowPerformanceMode(int16_t value)
{
    if ((trackControlParameter[(int)controlType::performanceMode]
                              [(int)parameterList::wavetablePosition] != 1) &&
        (value == 0))
        return;
    performanceMod.wavetablePosition = value;

    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setCurrentInstrIdx(
          currentInstrument_idx); // play mem dopiero aktualizuje index na play, a czasem korzysta
                                  // sie wczesniej z funkcji
    }

    uint32_t wavetableWindow;

    if (trackControlParameter[(int)controlType::sequencerMode]
                             [(int)parameterList::wavetablePosition] ||
        trackControlParameter[(int)controlType::sequencerMode2]
                             [(int)parameterList::wavetablePosition])
        wavetableWindow = currentSeqModValues.wavetablePosition;
    else
        wavetableWindow = mtProject.instrument[currentInstrument_idx].wavetableCurrentWindow;

    int32_t valueMap =
      mapClamped<int>(value,
                      -100,
                      100,
                      -mtProject.instrument[currentInstrument_idx].sample.wavetableWindowNumber,
                      mtProject.instrument[currentInstrument_idx].sample.wavetableWindowNumber);

    if (wavetableWindow + valueMap >=
        mtProject.instrument[currentInstrument_idx].sample.wavetableWindowNumber)
        currentPerformanceValues.wavetablePosition =
          mtProject.instrument[currentInstrument_idx].sample.wavetableWindowNumber - 1;
    else if (wavetableWindow + valueMap < 0)
        currentPerformanceValues.wavetablePosition = 0;
    else
        currentPerformanceValues.wavetablePosition = wavetableWindow + valueMap;

    trackControlParameter[(int)controlType::performanceMode]
                         [(int)parameterList::wavetablePosition] = 1;

    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setWavetableWindowForceFlag();
        playMemPtr[channel]->setForcedWavetableWindow(currentPerformanceValues.wavetablePosition);

        playMemPtr[channel]->setWavetableWindow(currentPerformanceValues.wavetablePosition);
    }
}

void playerEngine::changeGranularPositionPerformanceMode(int16_t value)
{
    if ((trackControlParameter[(int)controlType::performanceMode]
                              [(int)parameterList::granularPosition] != 1) &&
        (value == 0))
        return;
    performanceMod.granularPosition = value;

    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setCurrentInstrIdx(
          currentInstrument_idx); // play mem dopiero aktualizuje index na play, a czasem korzysta
                                  // sie wczesniej z funkcji
    }

    uint32_t granularPosition;

    if (trackControlParameter[(int)controlType::sequencerMode]
                             [(int)parameterList::granularPosition] ||
        trackControlParameter[(int)controlType::sequencerMode2]
                             [(int)parameterList::granularPosition])
        granularPosition = currentSeqModValues.granularPosition;
    else
        granularPosition = mtProject.instrument[currentInstrument_idx].granular.currentPosition;

    int32_t valueMap = mapClamped<int>(value, -100, 100, -MAX_16BIT, MAX_16BIT);

    if (granularPosition + valueMap >= MAX_16BIT)
        currentPerformanceValues.granularPosition = MAX_16BIT;
    else if (granularPosition + valueMap < 0)
        currentPerformanceValues.granularPosition = 0;
    else
        currentPerformanceValues.granularPosition = granularPosition + valueMap;

    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::granularPosition] =
      1;

    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setGranularPosForceFlag();
        playMemPtr[channel]->setForcedGranularPos(currentPerformanceValues.granularPosition);

        playMemPtr[channel]->setGranularPosition(currentPerformanceValues.granularPosition);
    }
}

void playerEngine::changePositionPerformanceMode(int16_t value)
{
    if (mtProject.instrument[currentInstrument_idx].playMode == playModeGranular)
    {
        changeGranularPositionPerformanceMode(value);
    }
    else if (mtProject.instrument[currentInstrument_idx].playMode == playModeWavetable)
    {
        changeWavetableWindowPerformanceMode(value);
    }
    else if ((mtProject.instrument[currentInstrument_idx].playMode != playModeSlice) &&
             (mtProject.instrument[currentInstrument_idx].playMode != playModeBeatSlice))
    {
        changeStartPointPerformanceMode(mapClamped<int>(value, -100, 100, -MAX_16BIT, MAX_16BIT));
    }
}

void playerEngine::changeAmpLfoRatePerformanceMode(int8_t value)
{
    if ((trackControlParameter[(int)controlType::performanceMode][(int)parameterList::lfoAmp] !=
         1) &&
        (value == 0))
        return;
    performanceMod.lfoAmpRate = value;

    uint8_t localAmpRate = 0;

    if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::lfoAmp] ||
        trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::lfoAmp])
    {
        localAmpRate = currentSeqModValues.lfoAmpRate;
    }
    else
    {
        localAmpRate = mtProject.instrument[currentInstrument_idx].lfo[envAmp].speed;
    }

    if (localAmpRate + value > CONSTRAIN_SPEED_AMP_ENVELOPE)
        currentPerformanceValues.lfoAmpRate = CONSTRAIN_SPEED_AMP_ENVELOPE;
    else if (localAmpRate + value < 0)
        currentPerformanceValues.lfoAmpRate = 0;
    else
        currentPerformanceValues.lfoAmpRate = localAmpRate + value;

    uint8_t curAmpLoop = mtProject.instrument[currentInstrument_idx].envelope[envAmp].loop;
    uint8_t targetAmpLoop = (curAmpLoop == 3) ? 3 : 1;
    calcLfoBasedEnvelope(&lfoBasedEnvelope[envAmp],
                         &mtProject.instrument[currentInstrument_idx].lfo[envAmp],
                         currentPerformanceValues.lfoAmpRate,
                         1,
                         targetAmpLoop);
    initEnvelopesParameters(envAmp, &lfoBasedEnvelope[envAmp]);

    setSyncParamsLFO(envAmp);
    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::lfoAmp] = 1;
}
void playerEngine::changeCutoffLfoRatePerformanceMode(int8_t value)
{
    if ((trackControlParameter[(int)controlType::performanceMode][(int)parameterList::lfoCutoff] !=
         1) &&
        (value == 0))
        return;
    performanceMod.lfoCutoffRate = value;

    uint8_t localCutoffRate = 0;

    if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::lfoCutoff] ||
        trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::lfoCutoff])
    {
        localCutoffRate = currentSeqModValues.lfoCutoffRate;
    }
    else
    {
        localCutoffRate = mtProject.instrument[currentInstrument_idx].lfo[envCutoff].speed;
    }

    if (localCutoffRate + value > CONSTRAIN_SPEED_OTHER_ENVELOPE)
        currentPerformanceValues.lfoCutoffRate = CONSTRAIN_SPEED_OTHER_ENVELOPE;
    else if (localCutoffRate + value < 0)
        currentPerformanceValues.lfoCutoffRate = 0;
    else
        currentPerformanceValues.lfoCutoffRate = localCutoffRate + value;

    uint8_t curCutoffLoop = mtProject.instrument[currentInstrument_idx].envelope[envCutoff].loop;
    uint8_t targetCutoffLoop = (curCutoffLoop == 3) ? 3 : 1;
    calcLfoBasedEnvelope(&lfoBasedEnvelope[envCutoff],
                         &mtProject.instrument[currentInstrument_idx].lfo[envCutoff],
                         currentPerformanceValues.lfoCutoffRate,
                         0,
                         targetCutoffLoop);
    initEnvelopesParameters(envCutoff, &lfoBasedEnvelope[envCutoff]);
    setSyncParamsLFO(envCutoff);

    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::lfoCutoff] = 1;
}
void playerEngine::changePositionLfoRatePerformanceMode(int8_t value)
{
    performanceMod.lfoPositionRate = value;

    if (mtProject.instrument[currentInstrument_idx].playMode == playModeGranular)
    {
        if ((trackControlParameter[(int)controlType::performanceMode]
                                  [(int)parameterList::lfoGranularPosition] != 1) &&
            (value == 0))
            return;

        uint8_t localGranularRate = 0;

        if (trackControlParameter[(int)controlType::sequencerMode]
                                 [(int)parameterList::lfoGranularPosition] ||
            trackControlParameter[(int)controlType::sequencerMode2]
                                 [(int)parameterList::lfoGranularPosition])
        {
            localGranularRate = currentSeqModValues.lfoGranularPositionRate;
        }
        else
        {
            localGranularRate = mtProject.instrument[currentInstrument_idx].lfo[envGranPos].speed;
        }

        if (localGranularRate + value > CONSTRAIN_SPEED_OTHER_ENVELOPE)
            currentPerformanceValues.lfoGranularPositionRate = CONSTRAIN_SPEED_OTHER_ENVELOPE;
        else if (localGranularRate + value < 0)
            currentPerformanceValues.lfoGranularPositionRate = 0;
        else
            currentPerformanceValues.lfoGranularPositionRate = localGranularRate + value;

        uint8_t curGranLoop = mtProject.instrument[currentInstrument_idx].envelope[envGranPos].loop;
        uint8_t targetGranLoop = (curGranLoop == 3) ? 3 : 1;
        calcLfoBasedEnvelope(&lfoBasedEnvelope[envGranPos],
                             &mtProject.instrument[currentInstrument_idx].lfo[envGranPos],
                             currentPerformanceValues.lfoGranularPositionRate,
                             0,
                             targetGranLoop);

        initEnvelopesParameters(envGranPos, &lfoBasedEnvelope[envGranPos]);
        setSyncParamsLFO(envGranPos);

        trackControlParameter[(int)controlType::performanceMode]
                             [(int)parameterList::lfoGranularPosition] = 1;
    }

    else if (mtProject.instrument[currentInstrument_idx].playMode == playModeWavetable)
    {
        if ((trackControlParameter[(int)controlType::performanceMode]
                                  [(int)parameterList::lfoWavetablePosition] != 1) &&
            (value == 0))
            return;

        uint8_t localWavetablePosRate = 0;

        if (trackControlParameter[(int)controlType::sequencerMode]
                                 [(int)parameterList::lfoWavetablePosition] ||
            trackControlParameter[(int)controlType::sequencerMode2]
                                 [(int)parameterList::lfoWavetablePosition])
        {
            localWavetablePosRate = currentSeqModValues.lfoWavetablePositionRate;
        }
        else
        {
            localWavetablePosRate = mtProject.instrument[currentInstrument_idx].lfo[envWtPos].speed;
        }

        if (localWavetablePosRate + value > CONSTRAIN_SPEED_OTHER_ENVELOPE)
            currentPerformanceValues.lfoWavetablePositionRate = CONSTRAIN_SPEED_OTHER_ENVELOPE;
        else if (localWavetablePosRate + value < 0)
            currentPerformanceValues.lfoWavetablePositionRate = 0;
        else
            currentPerformanceValues.lfoWavetablePositionRate = localWavetablePosRate + value;

        uint8_t curWtLoop = mtProject.instrument[currentInstrument_idx].envelope[envWtPos].loop;
        uint8_t targetWtLoop = (curWtLoop == 3) ? 3 : 1;
        calcLfoBasedEnvelope(&lfoBasedEnvelope[envWtPos],
                             &mtProject.instrument[currentInstrument_idx].lfo[envWtPos],
                             currentPerformanceValues.lfoWavetablePositionRate,
                             0,
                             targetWtLoop);

        initEnvelopesParameters(envWtPos, &lfoBasedEnvelope[envWtPos]);
        setSyncParamsLFO(envWtPos);

        trackControlParameter[(int)controlType::performanceMode]
                             [(int)parameterList::lfoWavetablePosition] = 1;
    }
}
void playerEngine::changePanningLfoRatePerformanceMode(int8_t value)
{
    if ((trackControlParameter[(int)controlType::performanceMode][(int)parameterList::lfoPanning] !=
         1) &&
        (value == 0))
        return;
    performanceMod.lfoPanningRate = value;

    uint8_t localPanningRate = 0;

    if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::lfoPanning] ||
        trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::lfoPanning])
    {
        localPanningRate = currentSeqModValues.lfoPanningRate;
    }
    else
    {
        localPanningRate = mtProject.instrument[currentInstrument_idx].lfo[envPan].speed;
    }

    if (localPanningRate + value > CONSTRAIN_SPEED_OTHER_ENVELOPE)
        currentPerformanceValues.lfoPanningRate = CONSTRAIN_SPEED_OTHER_ENVELOPE;
    else if (localPanningRate + value < 0)
        currentPerformanceValues.lfoPanningRate = 0;
    else
        currentPerformanceValues.lfoPanningRate = localPanningRate + value;

    uint8_t curPanLoop = mtProject.instrument[currentInstrument_idx].envelope[envPan].loop;
    uint8_t targetPanLoop = (curPanLoop == 3) ? 3 : 1;
    calcLfoBasedEnvelope(&lfoBasedEnvelope[envPan],
                         &mtProject.instrument[currentInstrument_idx].lfo[envPan],
                         currentPerformanceValues.lfoPanningRate,
                         0,
                         targetPanLoop);
    initEnvelopesParameters(envPan, &lfoBasedEnvelope[envPan]);
    setSyncParamsLFO(envPan);

    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::lfoPanning] = 1;
}

void playerEngine::changeFinetuneLfoRatePerformanceMode(int8_t value)
{
    if ((trackControlParameter[(int)controlType::performanceMode]
                              [(int)parameterList::lfoFinetune] != 1) &&
        (value == 0))
        return;
    performanceMod.lfoFinetuneRate = value;

    uint8_t localFinetuneRate = 0;

    if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::lfoFinetune] ||
        trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::lfoFinetune])
    {
        localFinetuneRate = currentSeqModValues.lfoFinetuneRate;
    }
    else
    {
        localFinetuneRate = mtProject.instrument[currentInstrument_idx].lfo[envFinetune].speed;
    }

    if (localFinetuneRate + value > CONSTRAIN_SPEED_OTHER_ENVELOPE)
        currentPerformanceValues.lfoFinetuneRate = CONSTRAIN_SPEED_OTHER_ENVELOPE;
    else if (localFinetuneRate + value < 0)
        currentPerformanceValues.lfoFinetuneRate = 0;
    else
        currentPerformanceValues.lfoFinetuneRate = localFinetuneRate + value;

    uint8_t curFineLoop = mtProject.instrument[currentInstrument_idx].envelope[envFinetune].loop;
    uint8_t targetFineLoop = (curFineLoop == 3) ? 3 : 1;
    calcLfoBasedEnvelope(&lfoBasedEnvelope[envFinetune],
                         &mtProject.instrument[currentInstrument_idx].lfo[envFinetune],
                         currentPerformanceValues.lfoFinetuneRate,
                         0,
                         targetFineLoop);
    initEnvelopesParameters(envFinetune, &lfoBasedEnvelope[envFinetune]);
    setSyncParamsLFO(envFinetune);

    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::lfoFinetune] = 1;
}

//*******************************************end
void playerEngine::endVolumePerformanceMode()
{
    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::volume] = 0;
    setRefreshBit(VOLUME_MASK);
}
void playerEngine::endPanningPerformanceMode()
{
    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::panning] = 0;
    setRefreshBit(PANNING_MASK);
}

void playerEngine::endBitDepthPerformanceMode()
{
    uint8_t bitDepth;

    if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::bitDepth] ||
        trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::bitDepth])
        bitDepth = currentSeqModValues.bitDepth;
    else
        bitDepth = mtProject.instrument[currentInstrument_idx].bitDepth;

    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::bitDepth] = 0;

    modBitDepth(bitDepth);
}

void playerEngine::endOverdrivePerformanceMode()
{
    uint8_t overdrive;

    if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::overdrive] ||
        trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::overdrive])
        overdrive = currentSeqModValues.overdrive;
    else
        overdrive = mtProject.instrument[currentInstrument_idx].overdrive;

    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::overdrive] = 0;

    modOverdrive(overdrive);
}

void playerEngine::endTunePerformanceMode()
{
    int8_t tune;

    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setCurrentInstrIdx(
          currentInstrument_idx); // play mem dopiero aktualizuje index na play, a czasem korzysta
                                  // sie wczesniej z funkcji
    }
    if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::tune] ||
        trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::tune])
    {
        tune = currentSeqModValues.tune;
        for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
        {
            playMemPtr[channel]->setForcedTune(tune);
        }
    }
    else
    {
        tune = mtProject.instrument[currentInstrument_idx].tune;
        for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
        {
            playMemPtr[channel]->clearTuneForceFlag();
        }
    }

    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setTune(tune, currentNote);
    }
    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::tune] = 0;
}
void playerEngine::endDelaySendPerformanceMode()
{
    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::delaySend] = 0;
    setRefreshBit(DELAY_SEND_MASK);
}
void playerEngine::endReverbSendPerformanceMode()
{
    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::reverbSend] = 0;
    setRefreshBit(REVERB_SEND_MASK);
}

void playerEngine::endPointsPerformanceMode()
{
    uint16_t startPoint, loopPoint1, loopPoint2, endPoint;

    if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::startPoint] ||
        trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::startPoint])
        startPoint = currentSeqModValues.startPoint;
    else
        startPoint = mtProject.instrument[currentInstrument_idx].startPoint;

    if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::loopPoint1] ||
        trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::loopPoint1])
        loopPoint1 = currentSeqModValues.loopPoint1;
    else
        loopPoint1 = mtProject.instrument[currentInstrument_idx].loopPoint1;

    if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::loopPoint2] ||
        trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::loopPoint2])
        loopPoint2 = currentSeqModValues.loopPoint2;
    else
        loopPoint2 = mtProject.instrument[currentInstrument_idx].loopPoint2;

    if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::endPoint] ||
        trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::endPoint])
        endPoint = currentSeqModValues.endPoint;
    else
        endPoint = mtProject.instrument[currentInstrument_idx].endPoint;

    if ((!trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::startPoint]) &&
        (!trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::loopPoint1]) &&
        (!trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::loopPoint2]) &&
        (!trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::endPoint]) &&
        (!trackControlParameter[(int)controlType::sequencerMode2]
                               [(int)parameterList::startPoint]) &&
        (!trackControlParameter[(int)controlType::sequencerMode2]
                               [(int)parameterList::loopPoint1]) &&
        (!trackControlParameter[(int)controlType::sequencerMode2]
                               [(int)parameterList::loopPoint2]) &&
        (!trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::endPoint]))
    {
        for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
        {
            playMemPtr[channel]->clearPointsForceFlag();
        }
    }
    else
    {
        for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
        {
            playMemPtr[channel]->setForcedPoints(startPoint, loopPoint1, loopPoint2, endPoint);
            playMemPtr[channel]->setPointsForceFlag();
        }
    }

    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::startPoint] = 0;
    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::loopPoint1] = 0;
    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::loopPoint2] = 0;
    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::endPoint] = 0;
}
void playerEngine::endStartPointPerformanceMode()
{
    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setCurrentInstrIdx(
          currentInstrument_idx); // play mem dopiero aktualizuje index na play, a czasem korzysta
                                  // sie wczesniej z funkcji
    }
    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::endPoint])
    {
        trackControlParameter[(int)controlType::performanceMode][(int)parameterList::startPoint] =
          0;
        changePointsPerformanceMode(0, performanceMod.endPoint);
        return;
    }

    endPointsPerformanceMode();
}

void playerEngine::endCutoffPerformanceMode()
{
    if (!trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::filterCutoff] &&
        !trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::filterCutoff])
    {
        if (!mtProject.instrument[currentInstrument_idx].filterEnable)
            filterDisconnect();
        if (!isActiveEnvelope(envCutoff))
            modCutoff(mtProject.instrument[currentInstrument_idx].cutOff);
    }
    else
    {
        if (!isActiveEnvelope(envCutoff))
            modCutoff(currentSeqModValues.filterCutoff);
    }

    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::filterCutoff] = 0;
    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::filterEnable] = 0;
    currentPerformanceValues.filterEnable = 0;
}
void playerEngine::endFilterTypePerformanceMode()
{

    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::filterType] = 0;
    if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::filterType] ||
        trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::filterType])
        changeFilterType(currentSeqModValues.filterType);
    else
        changeFilterType(mtProject.instrument[currentInstrument_idx].filterType);
}
void playerEngine ::endSamplePlaybackPerformanceMode()
{
    trackControlParameter[(int)controlType::performanceMode]
                         [(int)parameterList::samplePlaybeckDirection] = 0;

    if (trackControlParameter[(int)controlType::sequencerMode]
                             [(int)parameterList::samplePlaybeckDirection] ||
        trackControlParameter[(int)controlType::sequencerMode2]
                             [(int)parameterList::samplePlaybeckDirection])
    {
        for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
        {
            if (currentSeqModValues.reversePlayback)
            {
                playMemPtr[channel]->setReverse();
            }
            else
            {
                playMemPtr[channel]->clearReverse();
            }
        }
    }
    else
    {
        for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
        {
            playMemPtr[channel]->clearReverse();
        }
    }
}

void playerEngine ::endEndPointPerformanceMode()
{
    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setCurrentInstrIdx(
          currentInstrument_idx); // play mem dopiero aktualizuje index na play, a czasem korzysta
                                  // sie wczesniej z funkcji
    }

    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::startPoint])
    {
        trackControlParameter[(int)controlType::performanceMode][(int)parameterList::endPoint] = 0;
        changePointsPerformanceMode(performanceMod.startPoint, 0);
        return;
    }

    endPointsPerformanceMode();
}
void playerEngine::endWavetableWindowPerformanceMode()
{
    uint32_t wavetableWindow;

    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setCurrentInstrIdx(
          currentInstrument_idx); // play mem dopiero aktualizuje index na play, a czasem korzysta
                                  // sie wczesniej z funkcji
    }

    if (trackControlParameter[(int)controlType::sequencerMode]
                             [(int)parameterList::wavetablePosition] ||
        trackControlParameter[(int)controlType::sequencerMode2]
                             [(int)parameterList::wavetablePosition])
    {
        wavetableWindow = currentSeqModValues.wavetablePosition;
        for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
        {
            playMemPtr[channel]->setForcedWavetableWindow(wavetableWindow);
        }
    }
    else
    {
        wavetableWindow = mtProject.instrument[currentInstrument_idx].wavetableCurrentWindow;
        for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
        {
            playMemPtr[channel]->clearWavetableWindowForceFlag();
        }
    }

    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setWavetableWindow(wavetableWindow);
    }
    trackControlParameter[(int)controlType::performanceMode]
                         [(int)parameterList::wavetablePosition] = 0;
}

void playerEngine::endGranularPositionPerformanceMode()
{
    uint32_t granularPos;

    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setCurrentInstrIdx(
          currentInstrument_idx); // play mem dopiero aktualizuje index na play, a czasem korzysta
                                  // sie wczesniej z funkcji
    }

    if (trackControlParameter[(int)controlType::sequencerMode]
                             [(int)parameterList::granularPosition] ||
        trackControlParameter[(int)controlType::sequencerMode2]
                             [(int)parameterList::granularPosition])
    {
        granularPos = currentSeqModValues.granularPosition;
        for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
        {
            playMemPtr[channel]->setForcedGranularPos(granularPos);
        }
    }
    else
    {
        granularPos = mtProject.instrument[currentInstrument_idx].granular.currentPosition;
        for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
        {
            playMemPtr[channel]->clearGranularPosForceFlag();
        }
    }
    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setGranularPosition(granularPos);
    }
    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::granularPosition] =
      0;
}

void playerEngine::endPositionPerformanceMode()
{
    if (trackControlParameter[(int)controlType::performanceMode]
                             [(int)parameterList::granularPosition])
    {
        endGranularPositionPerformanceMode();
    }
    else if (trackControlParameter[(int)controlType::performanceMode]
                                  [(int)parameterList::wavetablePosition])
    {
        endWavetableWindowPerformanceMode();
    }
    else if (trackControlParameter[(int)controlType::performanceMode]
                                  [(int)parameterList::startPoint])
    {
        endStartPointPerformanceMode();
    }
}

void playerEngine::endAmpLfoRatePerformanceMode()
{
    uint8_t curAmpLoop = mtProject.instrument[currentInstrument_idx].envelope[envAmp].loop;
    if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::lfoAmp] ||
        trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::lfoAmp])
    {
        uint8_t targetLoop = (curAmpLoop == 3) ? 3 : 1;
        calcLfoBasedEnvelope(&lfoBasedEnvelope[envAmp],
                             &mtProject.instrument[currentInstrument_idx].lfo[envAmp],
                             currentSeqModValues.lfoAmpRate,
                             1,
                             targetLoop);
        initEnvelopesParameters(envAmp, &lfoBasedEnvelope[envAmp]);
    }
    else
    {
        if (mtProject.instrument[currentInstrument_idx].envelope[envAmp].enable)
        {
            if (curAmpLoop == 1 || curAmpLoop == 3)
            {
                calcLfoBasedEnvelope(&lfoBasedEnvelope[envAmp],
                                     &mtProject.instrument[currentInstrument_idx].lfo[envAmp],
                                     mtProject.instrument[currentInstrument_idx].lfo[envAmp].speed,
                                     1,
                                     curAmpLoop);
                initEnvelopesParameters(envAmp, &lfoBasedEnvelope[envAmp]);
            }
            else
            {
                initEnvelopesParameters(
                  envAmp, &mtProject.instrument[currentInstrument_idx].envelope[envAmp]);
            }
        }
        else
        {
            initEnvelopesParameters(envAmp, (envelopeGenerator::strEnv*)&passEnvelope);
        }
    }
    setSyncParamsLFO(envAmp);
    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::lfoAmp] = 0;
}
void playerEngine::endCutoffLfoRatePerformanceMode()
{
    uint8_t curCutoffLoop = mtProject.instrument[currentInstrument_idx].envelope[envCutoff].loop;
    if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::lfoCutoff] ||
        trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::lfoCutoff])
    {
        uint8_t targetLoop = (curCutoffLoop == 3) ? 3 : 1;
        calcLfoBasedEnvelope(&lfoBasedEnvelope[envCutoff],
                             &mtProject.instrument[currentInstrument_idx].lfo[envCutoff],
                             currentSeqModValues.lfoCutoffRate,
                             0,
                             targetLoop);
        initEnvelopesParameters(envCutoff, &lfoBasedEnvelope[envCutoff]);
    }
    else
    {
        if (mtProject.instrument[currentInstrument_idx].envelope[envCutoff].enable)
        {
            if (curCutoffLoop == 1 || curCutoffLoop == 3)
            {
                calcLfoBasedEnvelope(
                  &lfoBasedEnvelope[envCutoff],
                  &mtProject.instrument[currentInstrument_idx].lfo[envCutoff],
                  mtProject.instrument[currentInstrument_idx].lfo[envCutoff].speed,
                  0,
                  curCutoffLoop);
                initEnvelopesParameters(envCutoff, &lfoBasedEnvelope[envCutoff]);
            }
            else
            {
                initEnvelopesParameters(
                  envCutoff, &mtProject.instrument[currentInstrument_idx].envelope[envCutoff]);
            }
        }
    }

    setSyncParamsLFO(envCutoff);

    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::lfoCutoff] = 0;
}
void playerEngine::endPositionLfoRatePerformanceMode()
{
    if (trackControlParameter[(int)controlType::performanceMode]
                             [(int)parameterList::lfoGranularPosition])
    {
        uint8_t curGranLoop = mtProject.instrument[currentInstrument_idx].envelope[envGranPos].loop;
        if (trackControlParameter[(int)controlType::sequencerMode]
                                 [(int)parameterList::lfoGranularPosition] ||
            trackControlParameter[(int)controlType::sequencerMode2]
                                 [(int)parameterList::lfoGranularPosition])
        {
            uint8_t targetLoop = (curGranLoop == 3) ? 3 : 1;
            calcLfoBasedEnvelope(&lfoBasedEnvelope[envGranPos],
                                 &mtProject.instrument[currentInstrument_idx].lfo[envGranPos],
                                 currentSeqModValues.lfoGranularPositionRate,
                                 0,
                                 targetLoop);
            initEnvelopesParameters(envGranPos, &lfoBasedEnvelope[envGranPos]);
        }
        else
        {
            if (mtProject.instrument[currentInstrument_idx].envelope[envGranPos].enable)
            {
                if (curGranLoop == 1 || curGranLoop == 3)
                {
                    calcLfoBasedEnvelope(
                      &lfoBasedEnvelope[envGranPos],
                      &mtProject.instrument[currentInstrument_idx].lfo[envGranPos],
                      mtProject.instrument[currentInstrument_idx].lfo[envGranPos].speed,
                      0,
                      curGranLoop);
                    initEnvelopesParameters(envGranPos, &lfoBasedEnvelope[envGranPos]);
                }
                else
                {
                    initEnvelopesParameters(
                      envGranPos,
                      &mtProject.instrument[currentInstrument_idx].envelope[envGranPos]);
                }
            }
        }

        setSyncParamsLFO(envGranPos);

        trackControlParameter[(int)controlType::performanceMode]
                             [(int)parameterList::lfoGranularPosition] = 0;
    }
    else if (trackControlParameter[(int)controlType::performanceMode]
                                  [(int)parameterList::lfoWavetablePosition])
    {
        uint8_t curWtLoop = mtProject.instrument[currentInstrument_idx].envelope[envWtPos].loop;
        if (trackControlParameter[(int)controlType::sequencerMode]
                                 [(int)parameterList::lfoWavetablePosition] ||
            trackControlParameter[(int)controlType::sequencerMode2]
                                 [(int)parameterList::lfoWavetablePosition])
        {
            uint8_t targetLoop = (curWtLoop == 3) ? 3 : 1;
            calcLfoBasedEnvelope(&lfoBasedEnvelope[envWtPos],
                                 &mtProject.instrument[currentInstrument_idx].lfo[envWtPos],
                                 currentSeqModValues.lfoWavetablePositionRate,
                                 0,
                                 targetLoop);
            initEnvelopesParameters(envWtPos, &lfoBasedEnvelope[envWtPos]);
        }
        else
        {
            if (mtProject.instrument[currentInstrument_idx].isEnvEnabled(envWtPos))
            {
                if (curWtLoop == 1 || curWtLoop == 3)
                {
                    calcLfoBasedEnvelope(
                      &lfoBasedEnvelope[envWtPos],
                      &mtProject.instrument[currentInstrument_idx].lfo[envWtPos],
                      mtProject.instrument[currentInstrument_idx].lfo[envWtPos].speed,
                      0,
                      curWtLoop);
                    initEnvelopesParameters(envWtPos, &lfoBasedEnvelope[envWtPos]);
                }
                else
                {
                    initEnvelopesParameters(
                      envWtPos, &mtProject.instrument[currentInstrument_idx].envelope[envWtPos]);
                }
            }
        }

        setSyncParamsLFO(envWtPos);
        trackControlParameter[(int)controlType::performanceMode]
                             [(int)parameterList::lfoWavetablePosition] = 0;
    }
}
void playerEngine::endPanningLfoRatePerformanceMode()
{
    uint8_t curPanLoop = mtProject.instrument[currentInstrument_idx].envelope[envPan].loop;
    if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::lfoPanning] ||
        trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::lfoPanning])
    {
        uint8_t targetLoop = (curPanLoop == 3) ? 3 : 1;
        calcLfoBasedEnvelope(&lfoBasedEnvelope[envPan],
                             &mtProject.instrument[currentInstrument_idx].lfo[envPan],
                             currentSeqModValues.lfoPanningRate,
                             0,
                             targetLoop);
        initEnvelopesParameters(envPan, &lfoBasedEnvelope[envPan]);
    }
    else
    {
        if (mtProject.instrument[currentInstrument_idx].envelope[envPan].enable)
        {
            if (curPanLoop == 1 || curPanLoop == 3)
            {
                calcLfoBasedEnvelope(&lfoBasedEnvelope[envPan],
                                     &mtProject.instrument[currentInstrument_idx].lfo[envPan],
                                     mtProject.instrument[currentInstrument_idx].lfo[envPan].speed,
                                     0,
                                     curPanLoop);
                initEnvelopesParameters(envPan, &lfoBasedEnvelope[envPan]);
            }
            else
            {
                initEnvelopesParameters(
                  envPan, &mtProject.instrument[currentInstrument_idx].envelope[envPan]);
            }
        }
    }

    setSyncParamsLFO(envPan);

    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::lfoPanning] = 0;
}
void playerEngine::endFinetuneLfoRatePerformanceMode()
{
    uint8_t curFineLoop = mtProject.instrument[currentInstrument_idx].envelope[envFinetune].loop;
    if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::lfoFinetune] ||
        trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::lfoFinetune])
    {
        uint8_t targetLoop = (curFineLoop == 3) ? 3 : 1;
        calcLfoBasedEnvelope(&lfoBasedEnvelope[envFinetune],
                             &mtProject.instrument[currentInstrument_idx].lfo[envFinetune],
                             currentSeqModValues.lfoFinetuneRate,
                             0,
                             targetLoop);
        initEnvelopesParameters(envFinetune, &lfoBasedEnvelope[envFinetune]);
    }
    else
    {
        if (mtProject.instrument[currentInstrument_idx].envelope[envFinetune].enable)
        {
            if (curFineLoop == 1 || curFineLoop == 3)
            {
                calcLfoBasedEnvelope(
                  &lfoBasedEnvelope[envFinetune],
                  &mtProject.instrument[currentInstrument_idx].lfo[envFinetune],
                  mtProject.instrument[currentInstrument_idx].lfo[envFinetune].speed,
                  0,
                  curFineLoop);
                initEnvelopesParameters(envFinetune, &lfoBasedEnvelope[envFinetune]);
            }
            else
            {
                initEnvelopesParameters(
                  envFinetune, &mtProject.instrument[currentInstrument_idx].envelope[envFinetune]);
            }
        }
    }

    setSyncParamsLFO(envFinetune);

    trackControlParameter[(int)controlType::performanceMode][(int)parameterList::lfoFinetune] = 0;
}
