#include <algorithm>
#include <numeric>

#ifdef FEATURE_SYNTH
#include "Synth.h"
#endif
#include "fileManager.h"
#include "log.h"
#include "mtAudioEngine.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include "sampleRecorder/sampleRecorder.h"

#include "configEditor/configEditor.h"
#include "core/songTimer.h"

#include "patternEditor/patternEditor.h"

#include "core/interfacePopups.h"

#include "config.h"
#include "core/interface.h"
#include "util.h"
#include "util_math.h"

#include "polyassert.h"

#include <cr_section_macros.h>

Sequencer sequencer;

__NOINIT(EXTERNAL_RAM)
Sequencer::strPattern seq[2];

#include "playModeDefs.h"

extern audioEngine engine;
extern cPatternEditor patternEditor;

extern cSampleRecorder sampleRecorder;
cSampleRecorder* SR = &sampleRecorder;

#include "performanceMode/performanceMode.h"
extern cPerformanceMode performanceMode;
static cPerformanceMode* PM = &performanceMode;

extern void cbkSetCurrentExporterOnLastStep();

const float Sequencer::MAX_TEMPO = 800.0f;
const float Sequencer::MIN_TEMPO = 40.0f;

const float Sequencer::TEMPO_FX_STP = 4.0f;
const float Sequencer::MIN_TEMPO_FX = 5.0f;
const float Sequencer::MAX_TEMPO_FX = 200.0f;

Sequencer::Sequencer()
{
}

void Sequencer::init()
{
    currentPattern = &seq[0];
    bufferPattern = &seq[1];

    clearAllPatternSlots();
    clearCopyPasteBuffer();

    std::fill(
      std::begin(player.performance.trackLength), std::end(player.performance.trackLength), 8);
    liveRecHeldNotesCount = 0;
}

void Sequencer::update()
{
    reloadPatternAfterStop();
    updatePatternManagment();
}
void Sequencer::initAfterReadConfig()
{
    setExternalClockComp(mtConfig.midi.slaveClockComp);
}

void Sequencer::clearAllPatternSlots()
{
    for (strPattern& pattern : seq)
    {
        clearPattern(&pattern);
    }
}

AUDIO_CORE_RAM void Sequencer::tick(float intervalSeconds)
{

    updateExternalClockTempo((seqtimesec_t)intervalSeconds);

    if (isSetInternalClock() || player.selectionMode)
    {
        // Integer clock, same ticks and offsets as the double accumulator it replaced (internalTickClock.h)
        internalTickClock.advance(
          intervalSeconds,
          getCurrentTempo(),
          player.swing.isActive ? player.swing.getTempoCorrection() : 0.0f,
          [this]() { return getTimerPeriod_us(); },
          [this](uint32_t offsetUs)
          {
              midiSetClockSubBlockOffsetUs(offsetUs);
              tick_uStep();
          });
    }
    else
    {
        midiSetClockSubBlockOffsetUs(0U);
        static const int nanoTicksInClockPulse =
          kStepsPerBeat * kuStepsInStep * kNanoStepsInUStep / kExternalClockPpqn;

        if (externalClockPulses > 0)
        {
            externalClocksToPlay += externalClockPulses + clockSignalsToCompensate;
            clockSignalsToCompensate = 0;
            externalClockPulses = 0;
        }

        while (externalClocksToPlay > 0)
        {
            for (int tick = 0; tick < nanoTicksInClockPulse; tick++)
            {
                tick_uStep();
            }
            externalClocksToPlay--;
        }
    }
}

/* note: returns "false" if track only has one step */
bool Sequencer::isFirstPatternStep(void)
{
    int8_t trackLength = getPatternLengthWithSel();

    if (trackLength == 0)
        return false;
    if (player.selectionMode)
        return player.globalPos == 0;

    return player.track[0].actual_pos_absolute == 0;
}

bool Sequencer::isLastPatternStep(void)
{
    int8_t currentPos = 0;
    int8_t trackLength = getPatternLengthWithSel();
    if (player.selectionMode)
    {
        currentPos = player.globalPos;
    }
    else
    {
        currentPos = player.track[0].actual_pos_absolute;
    }

    return player.endOfPattern || currentPos >= trackLength;
}

AUDIO_CORE_RAM bool Sequencer::isMetronomeApplicableForActualRecordingSource(void)
{
    return SR->recorderConfig.source == SR->sourceTypeMicLG ||
           SR->recorderConfig.source == SR->sourceTypeMicHG ||
           SR->isLineInSource() ||
           SR->isUsbSource();
}

AUDIO_CORE_RAM void Sequencer::tick_uStep(void)
{
    /*
     1 step sekwencji = 48 x uStep = 48 x 12 nanoStep(576)
     6912 nanoStep = 12 stepów
     */

    if (SR->recordInProgressFlag == 1 || player.preRecMetronomeActive)
    {
        if (isMetronomeApplicableForActualRecordingSource() || (player.preRecMetronomeActive))
        {
            if (timerTicks % kTicksInStep == 0)
            {
                bool isActiveRecordingMetronome =
                  isMetronomeActive() || (SR->waitForPreRollEnd && !isMetronomeActive());

                if ((player.preRecMetronomeActive || isActiveRecordingMetronome) &&
                    player.extRecMetronomeStep %
                        (getMetronomeDenominator() * getMetronomeNumerator()) ==
                      0)
                {
                    engine.makeMetronomeTick(1);
                }
                else if ((player.preRecMetronomeActive || isActiveRecordingMetronome) &&
                         player.extRecMetronomeStep % getMetronomeDenominator() == 0)
                {
                    engine.makeMetronomeTick(0);
                }

                player.extRecMetronomeStep++;

                int preRollLengthStep = getMetronomeDenominator() * getMetronomeNumerator();

                if (player.extRecMetronomeStep > preRollLengthStep)
                {
                    if (player.preRecMetronomeActive)
                    {
                        player.preRecMetronomeActive = 0;
                        playPattern(false); // audio was cleared when metronome preroll started
                    }

                    if (SR->recordInProgressFlag)
                    {
                        if (player.preRollMetronomeEndCalback != nullptr)
                        {
                            player.preRollMetronomeEndCalback();
                        }
                    }
                }
            }

            if (!isPlay())
            {
                timerTicks++;
            }
        }
    }
    else
    {
        player.extRecMetronomeStep = 0;
    }

    if (player.isPlay)
    {
        timerTicks++;

        if (timerTicks % 12 == 1)
        {
            inc_uStepGlobal();
            for (int trackIdx = 0; trackIdx < kAllTracks; trackIdx++)
            {
                play_microStep(trackIdx);
                inc_uStep(trackIdx);
            }
        }
    }
}

AUDIO_CORE_RAM void Sequencer::play_microStep(int row)
{
    assert(row < kAllTracks);

    strPattern::strTrack& patternRow = currentPattern->track[row];
    strPlayer::strPlayerTrack& playerRow = player.track[row];

    strPlayer::strPlayerTrack::strSendStep& stepToSend = player.track[row].stepToSend;
    strPlayer::strPlayerTrack::strSendStep& stepSent = player.track[row].stepSent;

    bool forceFirstRollWhenNoNote = 0; // potrzebne jak rolka wchodzi jako efekt bez nuty

    if (row == 0 && playerRow.uStep == 1 && isRec())
    {
        if (isMetronomeActive() &&
            playerRow.actual_pos % (getMetronomeDenominator() * getMetronomeNumerator()) == 0)
        {
            engine.makeMetronomeTick(1);
        }
        else if (isMetronomeActive() && playerRow.actual_pos % getMetronomeDenominator() == 0)
        {
            engine.makeMetronomeTick(0);
        }
    }

    if (playerRow.uStep == 1)
    {
        memcpy((uint8_t*)&stepToSend,
               (uint8_t*)&patternRow.step[playerRow.actual_pos],
               sizeof(strPattern::strTrack::strStep));

        if (isCustomPlayMode(row))
        {
            if (playerRow.custom_fx)
            {
                if (stepToSend.fx[0].type == fx.FX_TYPE_NONE)
                {
                    stepToSend.fx[0].type = playerRow.custom_fx;
                    stepToSend.fx[0].value = playerRow.custom_fx_value;
                }
                else if (stepToSend.fx[1].type == fx.FX_TYPE_NONE)
                {
                    stepToSend.fx[1].type = playerRow.custom_fx;
                    stepToSend.fx[1].value = playerRow.custom_fx_value;
                }
            }
        }

        //		playerRow.micromove.isActive = 0;
        playerRow.cancelStep = 0;
    }

    //	******************************************************************************
    // 	odliczamy odpaloną nutę i stepa, step może zawierać kilka nut
    //	******************************************************************************

    if (playerRow.stepOpen)
    {
        playerRow.stepTimer++;

        //
        // wyłączamy nutę jeśli się skończyła
        //

        if ((playerRow.stepTimer >= playerRow.stepLength))
        {

            playerRow.stepOpen = 0;
            playerRow.roll.reset();
        }
    }
    if (playerRow.noteOpen)
    {
        playerRow.noteTimer++;
        int instrument = playerRow.stepSent.instrument;
        if ((playerRow.noteLength != -1 && playerRow.noteTimer >= playerRow.noteLength) ||
            (isTrackEngineMuted(row) &&
             (isMidiInstrument(instrument) || isSynthInstrument(instrument))))
        {
            sendNoteOff(row, &playerRow.stepSent);
            playerRow.noteOpen = 0;
        }
    }

    //	**************************
    // 	wysyłamy zegar
    //	**************************

    if ((playerRow.uStep > 0) && player.isPlay && row == 0 &&
        (!player.selectionMode || player.selectionModeLooped))
    {
        if (((playerRow.uStep - 1) % 8) == 0)
            sendMidiClock();
    }

    // jeśli ostatni step, zażądaj ładowania kolejnego patternu
    if (playerRow.uStep == 1 && player.isPlay && row == 0)
    {

        bool isLastStepInPattern = playerRow.actual_pos == patternRow.length;
        bool isLastStepInSelectionMode =
          player.selectionModeLooped && playerRow.actual_pos == player.selectionModeLastStep;

        if ((isLastStepInPattern || isLastStepInSelectionMode) &&
            player.sequencialSwitch_isArmed > 0)
        {
            if (player.sequencialSwitch_pattern > 0)
            {
                loadNextPattern(player.sequencialSwitch_pattern);
                player.sequencialSwitch_loaded = true;
            }
        }
        else if (((playerRow.actual_pos == patternRow.length)) && player.songMode)
        {
            loadNextPattern(newFileManager.getNextSongPattern());
        }
    }

    if (playerRow.micromove.isActive)
    {
        playerRow.micromove.timer++;
    }

    if (playerRow.arp.isOn)
    {
        playerRow.arp.timer++;
    }

    // ************************************
    // 			jesli zmutowany albo nieaktywny(playSelection)
    //			to reszta sie nie wykonuje
    // ************************************

    if (!playerRow.isActive)
        return;
    if (isTrackEngineMuted(row))
        return;

    // ************************************
    // 		 PRE EFEKTY i operacje na uStep == 1
    // ************************************

    uint8_t startStep = 0;

    int16_t randomisedValue = -1;
    int16_t gateLength = -1;

    if (playerRow.uStep == 1)
    {

        if (stepToSend.fx[0].type == fx.FX_TYPE_RANDOM_VALUE)
        {
            int16_t lowVal = clamp<int>(stepToSend.fx[1].value - stepToSend.fx[0].value,
                                        getFxMin(stepToSend.fx[1].type, true),
                                        getFxMax(stepToSend.fx[1].type, true));

            int16_t hiVal = clamp<int>(stepToSend.fx[1].value + stepToSend.fx[0].value,
                                       getFxMin(stepToSend.fx[1].type, true),
                                       getFxMax(stepToSend.fx[1].type, true));

            randomisedValue = utilMathRandom::random(lowVal, hiVal + 1);
        }
        else if (stepToSend.fx[1].type == fx.FX_TYPE_RANDOM_VALUE)
        {
            int16_t lowVal = clamp<int>(stepToSend.fx[0].value - stepToSend.fx[1].value,
                                        getFxMin(stepToSend.fx[0].type, true),
                                        getFxMax(stepToSend.fx[0].type, true));

            int16_t hiVal = clamp<int>(stepToSend.fx[0].value + stepToSend.fx[1].value,
                                       getFxMin(stepToSend.fx[0].type, true),
                                       getFxMax(stepToSend.fx[0].type, true));

            randomisedValue = utilMathRandom::random(lowVal, hiVal + 1);
        }

        uint8_t atLeastOneFx = 0;
        uint8_t setBreakPattern = 0;
        uint8_t offsetValue = 0;

        // pętla na potrzeby wszechnadrzędnego FX_TYPE_CHANCE
        bool chanceEvaluated = false;
        for (int8_t fxIndex = 1; fxIndex >= 0; fxIndex--)
        {
            strPlayer::strPlayerTrack::strSendStep::strFx _fx = stepToSend.fx[fxIndex];

            switch (_fx.type)
            {

                case fx.FX_TYPE_CHANCE:
                    killFxOnSlot(row, fxIndex);
                    atLeastOneFx = 1;

                    if (!chanceEvaluated)
                    {
                        chanceEvaluated = true;
                        if (!evaluateChanceFx(row, _fx.value))
                            playerRow.cancelStep = 1;
                    }

                    break;
            }
        }

        if (!playerRow.cancelStep)
        {
            if (isMidiInstrument(stepToSend.instrument))
            {
                if (getInstrumentMidiOffset(stepToSend.instrument) != 0)
                {
                    offsetValue =
                      map(getInstrumentMidiOffset(stepToSend.instrument) + 1, 0, 100, 1, 46);

                    playerRow.micromove.set(offsetValue);
                }
            }

            if (stepToSend.micromove >= 0)
            {
                uint16_t micromoveVal = map(stepToSend.micromove + 1, 0, 100, 1, 48) + offsetValue;
                playerRow.micromove.set(clamp<int>(micromoveVal, 0, 48 * 2));
            }

            if (stepToSend.volume >= 0)
            {
                if (stepToSend.note >= 0)
                {
                    stepSent.volume = stepToSend.volume;
                }
                else if (stepToSend.note == STEP_NOTE_EMPTY && isSampleTrack(row))
                {
                    instrumentPlayer[row].setStepVelocity(stepToSend.volume);
                }
            }
            else if (stepToSend.volume == STEP_VELO_OFF)
            {
                stepToSend.volume = STEP_VELO_DEFAULT;
                stepSent.volume = STEP_VELO_DEFAULT;
                if (stepToSend.note == STEP_NOTE_EMPTY && isSampleTrack(row))
                {
                    instrumentPlayer[row].setStepVelocity(STEP_VELO_DEFAULT);
                }
            }
        }

        for (int8_t fxIndex = 1; fxIndex >= 0; fxIndex--)
        {
            strPlayer::strPlayerTrack::strSendStep::strFx _fx = stepToSend.fx[fxIndex];

            if (playerRow.cancelStep)
                break;

            if (randomisedValue != -1)
            {
                _fx.value = randomisedValue;
                stepToSend.fx[fxIndex].value = _fx.value;
            }
#ifdef FEATURE_SYNTH
            if (isSynthInstrument(stepToSend.instrument) && engine.isSynthFx(_fx.type))
            {
                int synthIdx = instrumentIdxToMidiChannel(stepToSend.instrument) - 1;
                assert(!isOutOfRange(synthIdx, 0, kSynthParts - 1));

                engine.seqSynthFx(_fx.type, _fx.value, fxIndex, synthIdx);

                continue;
            }
#endif

            uint8_t ccIndex = 0;
            switch (_fx.type)
            {

                case fx.FX_TYPE_MICROMOVE:
                {
                    killFxOnSlot(row, fxIndex);
                    atLeastOneFx = 1;

                    uint16_t micromoveVal = map(_fx.value + 1, 0, 100, 1, 48);

                    micromoveVal += offsetValue;

                    micromoveVal = clamp<int>(micromoveVal, 0, 48 * 2);

                    playerRow.micromove.set(micromoveVal);

                    break;
                }
                case fx.FX_TYPE_VOLUME:
                    if (stepToSend.note >= 0)
                    {
                        killFxOnSlot(row, fxIndex);
                        atLeastOneFx = 1;

                        if (!isSampleInstrument(stepToSend.instrument))
                        {
                            stepToSend.volume = _fx.value;
                            stepSent.volume = stepToSend.volume;
                        }
                    }
                    break;
                case fx.FX_TYPE_GATE_LENGTH:
                    gateLength = mapClamped<int>(_fx.value, 0, 100, 0, 47);
                    break;
                case fx.FX_TYPE_OFF:
                    killFxOnSlot(row, fxIndex);
                    atLeastOneFx = 1;

                    if (fxIndex == playerRow.roll.fxSlot)
                    {
                        playerRow.micromove.reset();
                        playerRow.cancelStep = 0;
                        playerRow.roll.reset();
                    }
#ifdef FEATURE_SYNTH
                    if (isSynthInstrument(stepSent.instrument) && engine.isSynthFx(_fx.type))
                    {
                        int synthIdx = instrumentIdxToMidiChannel(stepSent.instrument) - 1;
                        assert(!isOutOfRange(synthIdx, 0, kSynthParts - 1));

                        engine.seqSynthFx(_fx.type, 0, fxIndex, synthIdx);
                    }
                    else
#endif
                    {
                        if (isSampleTrack(row))
                        {
                            instrumentPlayer[row].seqFx(0, 0, fxIndex);
                        }
                    }

                    break;

                case fx.FX_TYPE_SEND_CC_F:
                {
                    ccIndex += 1;
                    [[fallthrough]];
                }
                case fx.FX_TYPE_SEND_CC_E:
                {
                    ccIndex += 1;
                    [[fallthrough]];
                }
                case fx.FX_TYPE_SEND_CC_D:
                {
                    ccIndex += 1;
                    [[fallthrough]];
                }
                case fx.FX_TYPE_SEND_CC_C:
                {
                    ccIndex += 1;
                    [[fallthrough]];
                }
                case fx.FX_TYPE_SEND_CC_B:
                {
                    ccIndex += 1;
                    [[fallthrough]];
                }
                case fx.FX_TYPE_SEND_CC_A:
                {
                    // todo or last used on track?
                    int instrument = stepToSend.instrument;
                    uint8_t ccChannel = 0;
                    ccChannel = instrumentIdxToMidiChannel(instrument);

                    if (ccChannel == kNoChannel)
                    {
                        continue;
                    }
                    bool isSynth = isSynthInstrument(instrument);
                    bool isMidi = isMidiInstrument(instrument);

                    if (isSynth)
                    {
                        // synth macro is handled by engine few lines above
                        assert(false);
                    }
                    else if (isMidi)
                    {
                        assert(ccIndex < std::size(mtProject.values.midiInstrument[0].ccOut));

                        ccIndex = mtProject.values.midiInstrument[ccChannel - 1].ccOut[ccIndex];

                        killFxOnSlot(row, fxIndex);
                        atLeastOneFx = 1;
                        if (ccIndex < 128)
                        {
                            sendCC(ccIndex, _fx.value, ccChannel, isSynth);
                        }
                        else if (ccIndex == MIDI_CC_SPECIAL_PC)
                        {
                            sendProgramChange(_fx.value, ccChannel, isSynth);
                        }
                        else if (ccIndex == MIDI_CC_SPECIAL_CHAN_AT)
                        {
                            sendAfterTouch(_fx.value, ccChannel, isSynth);
                        }
                        else if (ccIndex == MIDI_CC_SPECIAL_POLY_AT)
                        {
                            sendPolyPressure(stepToSend.note, _fx.value, ccChannel, isSynth);
                        }
                    }
                    else
                    {
                        continue;
                    }
                }
                break;
                case fx.FX_TYPE_MICROTUNING:
                {
                    int instrument = stepToSend.instrument;
                    bool isSynth = isSynthInstrument(instrument);
                    bool isMidi = isMidiInstrument(instrument);
                    if (isMidi || isSynth)
                    {
                        const int minPitchbend = isSynth ? kMidiPitchbendMin : -4000;
                        const int maxPitchbend = isSynth ? kMidiPitchbendMax : 4000;

                        atLeastOneFx = 1;
                        killFxOnSlot(row, fxIndex);

                        sendPitchbend(mapClamped<int>((int16_t)_fx.value,
                                                      getFxMin(fx.FX_TYPE_MICROTUNING),
                                                      getFxMax(fx.FX_TYPE_MICROTUNING),
                                                      minPitchbend,
                                                      maxPitchbend),
                                      instrumentIdxToMidiChannel(instrument),
                                      isSynth);
                    }
                }
                break;

                case fx.FX_TYPE_TEMPO:
                {
                    killFxOnSlot(row, fxIndex);
                    atLeastOneFx = 1;

                    float tempoToSet = getFxValueCorrection(fx.FX_TYPE_TEMPO, _fx.value);
                    if (_fx.value <= TEMPO_FX_STP)
                    {
                        player.tempoFx.stopAfterCurrentStep = 1;
                    }
                    else
                    {
                        player.tempoFx.set(tempoToSet);
                    }
                    break;
                }

                case fx.FX_TYPE_SWING:
                    killFxOnSlot(row, fxIndex);
                    atLeastOneFx = 1;

                    player.swing.setSwing(_fx.value);

                    break;

                case fx.FX_TYPE_BREAK_PATTERN:
                    killFxOnSlot(row, fxIndex);
                    atLeastOneFx = 1;

                    setBreakPattern = 1;
                    player.breakPattern = 1;
                    break;

                case fx.FX_TYPE_RANDOM_VOLUME:
                    if (stepToSend.note >= 0)
                    {
                        killFxOnSlot(row, fxIndex);
                        atLeastOneFx = 1;

                        if (!isSampleInstrument(stepToSend.instrument))
                        {
                            stepToSend.volume =
                              clamp<int>(utilMathRandom::random(0, _fx.value + 1), 0, 127);
                            stepSent.volume = stepToSend.volume;
                        }
                    }
                    break;

                default: break;
            }

            if (stepToSend.note == STEP_NOTE_EMPTY && !atLeastOneFx)
            {
                // wysyłam tylko fxa jeśli nie ma nuty
                switch (_fx.type)
                {
                    case fx.FX_TYPE_NONE: break;
                    case fx.FX_TYPE_ROLL:
                        killFxOnSlot(row, fxIndex);

                        playerRow.stepTimer = 1; // trza wyzerować na potrzeby przeliczania volume

                        if (rollValToPeriod(_fx.value) == fx.ROLL_PERIOD_NONE)
                            break;

                        forceFirstRollWhenNoNote = 1;

                        playerRow.roll.isOn = 1;
                        playerRow.roll.fxSlot = fxIndex;
                        playerRow.roll.val = _fx.value;
                        playerRow.roll.type = getRollType(_fx.value);
                        playerRow.roll.period = _fx.value;
                        playerRow.roll.volumeOption = rollValToVolumeOption(_fx.value);
                        playerRow.roll.startingVolume = -1;
                        if (playerRow.roll.volumeOption != rollVolumeOption_const)
                        {
                            playerRow.roll.length = calcStepLength(row, playerRow.actual_pos);

                            if (isSampleInstrument(stepSent.instrument) && isSampleTrack(row))
                            {
                                playerRow.roll.startingVolume =
                                  instrumentPlayer[row].getMostSignificantVolume();
                            }
                            else
                            {
                                playerRow.roll.startingVolume = stepSent.volume;
                            }
                        }

                        break;

                    case fx.FX_TYPE_ARP: playerRow.arp.reset(); break;

                    default: killFxOnSlot(row, fxIndex);
#ifdef FEATURE_SYNTH
                        if (isSynthInstrument(stepSent.instrument) && engine.isSynthFx(_fx.type))
                        {
                            int synthIdx = instrumentIdxToMidiChannel(stepSent.instrument) - 1;
                            assert(!isOutOfRange(synthIdx, 0, kSynthParts - 1));

                            engine.seqSynthFx(_fx.type, _fx.value, fxIndex, synthIdx);
                        }
                        else
#endif
                        {
                            if (isSampleTrack(row))
                            {
                                instrumentPlayer[row].seqFx(_fx.type, _fx.value, fxIndex);
                            }
                        }
                        break;
                }
            }
        }

        if (setBreakPattern && player.songMode)
        {
            loadNextPattern(newFileManager.getNextSongPattern());
        }

        if (!playerRow.cancelStep)
        {
            playerRow.micromove.arm(playerRow.stepToSend);
        }
    }

    // **************************
    // 		odpalamy step?
    // **************************

    if (stepToSend.note != STEP_NOTE_EMPTY)
    {
        // !micromove
        if (!playerRow.micromove.isActive && playerRow.uStep == 1 && !playerRow.cancelStep)
        {
            startStep = 1;
            playerRow.micromove.reset();
        }
    }
    // micromove
    if (playerRow.micromove.isMicromoveReady())
    {
        playerRow.micromove.reset();
        startStep = 1;
        playerRow.stepToSend = playerRow.micromove.stepToMove;
    }

    // **************************
    // 		odpalamy stepa
    // **************************
    if (startStep && !playerRow.recOpen)
    {
        if (playerRow.stepOpen && playerRow.stepToSend.note >= 0)
        {
            // looks weird but keeps logic from commit
            // "Send NOTE OFF to held MIDI notes on track when track muted or sample played"
            if (isMidiInstrument(playerRow.stepToSend.instrument) ||
                isSynthInstrument(playerRow.stepToSend.instrument) ||
                isMidiInstrument(playerRow.stepSent.instrument) ||
                isSynthInstrument(playerRow.stepSent.instrument))
            {
                sendNoteOff(row, &playerRow.stepSent);
            }

            playerRow.stepOpen = 0;
            playerRow.noteOpen = 0;

            playerRow.roll.reset();
            playerRow.arp.reset();
        }

        // EFEKTY WŁAŚCIWE
        for (int8_t fxIndex = 1; fxIndex >= 0; fxIndex--)
        {
            strPlayer::strPlayerTrack::strSendStep::strFx _fx = stepToSend.fx[fxIndex];
            strPlayer::strPlayerTrack::strSendStep::strFx _fxBis = stepToSend.fx[!fxIndex];

            if (randomisedValue != -1)
                _fx.value = randomisedValue;

            switch (_fx.type)
            {
                case fx.FX_TYPE_ROLL:
                    killFxOnSlot(row, fxIndex);

                    if (rollValToPeriod(_fx.value) == fx.ROLL_PERIOD_NONE)
                        break;

                    playerRow.roll.isOn = 1;
                    playerRow.roll.fxSlot = fxIndex;
                    playerRow.roll.val = _fx.value;
                    playerRow.roll.type = getRollType(_fx.value);
                    playerRow.roll.period = _fx.value;
                    playerRow.roll.volumeOption = rollValToVolumeOption(_fx.value);
                    playerRow.roll.startingVolume = -1;
                    if (playerRow.roll.volumeOption != rollVolumeOption_const)
                    {
                        playerRow.roll.length = calcStepLength(row, playerRow.actual_pos);

                        if (_fxBis.type == fx.FX_TYPE_VOLUME)
                        {
                            playerRow.roll.startingVolume = _fxBis.value;
                        }
                        else
                        {
                            playerRow.roll.startingVolume = stepToSend.volume;
                        }
                    }

                    break;

                case fx.FX_TYPE_ARP:
                    if (_fxBis.type == fx.FX_TYPE_MIDI_CHORD && _fx.value > 0)
                    {
                        uint8_t value = _fx.value - 1;

                        playerRow.arp.period = value % fx.arpPeriodsCount;
                        playerRow.arp.option = value / fx.arpPeriodsCount;
                        playerRow.arp.length = arpPeriodToMicrosteps(playerRow.arp.period);
                        playerRow.arp.chord = _fxBis.value;
                        playerRow.arp.baseNote = stepToSend.note;
                        playerRow.arp.start();

                        stepToSend.note = getNextArpNote(&playerRow);

                        //					Serial.printf("arp: val: %d per %d, mode %d\n",
                        //									value,
                        //									playerRow.arp.period,
                        //									playerRow.arp.option);
                    }
                    else
                    {
                        playerRow.arp.reset();
                    }

                    break;
                case fx.FX_TYPE_RANDOM_NOTE:
                    killFxOnSlot(row, fxIndex);
                    for (uint8_t a = 0; a < 100; a++)
                    {
                        stepToSend.note =
                          clamp<int>(utilMathRandom::random(stepToSend.note - _fx.value,
                                                            stepToSend.note + _fx.value + 1),
                                     0,
                                     127);
                        if (isInScale(stepToSend.note,
                                      mtProject.values.padBoardRootNote,
                                      mtProject.values.padBoardScale))
                            break;
                    }
                    break;
                case fx.FX_TYPE_RANDOM_INSTRUMENT:
                    killFxOnSlot(row, fxIndex);
                    if (isSampleInstrument(stepToSend.instrument))
                    {
                        stepToSend.instrument =
                          clamp<int>(utilMathRandom::random(stepToSend.instrument - _fx.value,
                                                            stepToSend.instrument + _fx.value + 1),
                                     0,
                                     kSampleInstrumentsCount - 1);
                    }
                    else if (isMidiInstrument(stepToSend.instrument))
                    {
                        static constexpr int kMidiInstrumentStartIdx = kSampleInstrumentsCount;
                        static constexpr int kMidiInstrumentEndIdx =
                          kMidiInstrumentStartIdx + kMidiInstrumentsCount - 1;

                        stepToSend.instrument =
                          clamp<int>(utilMathRandom::random(stepToSend.instrument - _fx.value,
                                                            stepToSend.instrument + _fx.value + 1),
                                     kMidiInstrumentStartIdx,
                                     kMidiInstrumentEndIdx);
                    }
                    else if (isSynthInstrument(stepToSend.instrument))
                    {
                        static constexpr int kSynthInstrumentStartIdx =
                          kSampleInstrumentsCount + kMidiInstrumentsCount;
                        static constexpr int kSynthInstrumentEndIdx =
                          kSynthInstrumentStartIdx + kSynthInstrumentsCount - 1;

                        stepToSend.instrument =
                          clamp<int>(utilMathRandom::random(stepToSend.instrument - _fx.value,
                                                            stepToSend.instrument + _fx.value + 1),
                                     kSynthInstrumentStartIdx,
                                     kSynthInstrumentEndIdx);
                    }
                    break;

                default: break;
            }
        }

        // ustawiamy całego stepa
        playerRow.stepOpen = 1;
        playerRow.stepTimer = 0; // od tej pory timer liczy w górę

        // ustawiamy nute
        playerRow.noteLength = gateLength; // ustawiany z fxa, -1 to nieskonczonosc
        playerRow.noteTimer = 0;           // od tej pory timer liczy w górę

        if (player.track[row].noteOpen && !player.track[row].recOpen && stepToSend.note >= 0)
        {
            stopOpenNoteOnTrack(row);
        }

        // jeśli rolka to nuty są krótsze od stepa
        if (playerRow.roll.isOn)
        {
            playerRow.roll.period = playerRow.roll.val;
            playerRow.noteLength = rollValToMicroSteps(playerRow.roll.period) / 2;
            playerRow.stepOpen = 1;
        }

        if (stepToSend.note >= 0)
        {
            if (playerRow.roll.type == fx.rollType_noteRandom && playerRow.roll.isOn)
            {
                playerRow.stepToSend.note =
                  clamp<int>(utilMathRandom::random(playerRow.stepToSend.note - 12,
                                                    playerRow.stepToSend.note + 12 + 1),
                             0,
                             127);
            }
            if (playerRow.roll.isOn && playerRow.roll.volumeOption != rollVolumeOption_const)
            {
                uint8_t rollVelo = getRollVelo(row, &stepToSend);
                if (isSampleInstrument(stepToSend.instrument))
                {
                    stepToSend.fx[playerRow.roll.fxSlot].type = fx.FX_TYPE_VOLUME;
                    stepToSend.fx[playerRow.roll.fxSlot].value = rollVelo > 100 ? 100 : rollVelo;
                }
                else
                {
                    stepToSend.volume = rollVelo;
                }
            }

            if ((isMidiInstrument(stepToSend.instrument) ||
                 isSynthInstrument(stepToSend.instrument)) &&
                playerRow.tuneMidi.isActive)
            {
                stepToSend.note = clamp<int>(stepToSend.note + playerRow.tuneMidi.tune, 0, 127);
            }

            sendNoteOn(row, &stepToSend);

            playerRow.stepOpen = 1;
            playerRow.noteOpen = 1;
            playerRow.stepLength = 9999;

            playerRow.stepSent = playerRow.stepToSend;
        }
        else if (stepToSend.note == STEP_NOTE_OFF_DEFAULT || stepToSend.note == STEP_NOTE_OFF_CUT ||
                 stepToSend.note == STEP_NOTE_OFF_FADE)
        {

            sendNoteOff(row, &playerRow.stepSent, stepToSend.note);
            playerRow.noteOpen = 0;
            playerRow.stepOpen = 0;
            playerRow.roll.reset();

            playerRow.stepSent = playerRow.stepToSend;
        }
    }

    // **************************
    // 		kontynuowanie nuty
    // **************************
    if (playerRow.stepSent.note != STEP_NOTE_EMPTY && playerRow.performanceStepRepeater != 0)
    {
        uint16_t period = stutterValToPeriod(playerRow.performanceStepRepeater);
        if (period)
        {
            if ((playerRow.stepTimer % period) == 1)
            {
                playerRow.stepToSend = playerRow.stepSent;

                if ((isMidiInstrument(stepToSend.instrument) ||
                     isSynthInstrument(stepToSend.instrument)) &&
                    playerRow.tuneMidi.isActive)
                {
                    stepToSend.note = clamp<int>(stepToSend.note + playerRow.tuneMidi.tune, 0, 127);
                }

                if (player.track[row].noteOpen)
                {
                    stopOpenNoteOnTrack(row, true);
                }

                sendNoteOn(row, &playerRow.stepToSend);
                playerRow.noteOpen = 1;
                playerRow.noteTimer = 0;

                playerRow.stepSent = playerRow.stepToSend;
            }
        }
    }

    // getChordBitmask(playerRow.arp.chord)
    else if (playerRow.stepOpen && playerRow.arp.isOn)
    {
        if (playerRow.arp.timer >= playerRow.arp.length)
        {
            playerRow.arp.timer = 0;

            playerRow.stepToSend = playerRow.stepSent;
            playerRow.stepToSend.note = playerRow.arp.baseNote;

            playerRow.stepToSend.note = getNextArpNote(&playerRow);

            if (playerRow.noteOpen)
            {
                sendNoteOff(row, &playerRow.stepSent);
            }

            if ((isMidiInstrument(stepToSend.instrument) ||
                 isSynthInstrument(stepToSend.instrument)) &&
                playerRow.tuneMidi.isActive)
            {
                stepToSend.note = clamp<int>(stepToSend.note + playerRow.tuneMidi.tune, 0, 127);
            }

            if (player.track[row].noteOpen)
            {
                stopOpenNoteOnTrack(row, true);
            }
            sendNoteOn(row, &playerRow.stepToSend);
            playerRow.noteOpen = 1;
            playerRow.noteTimer = 0;

            playerRow.stepSent = playerRow.stepToSend;
        }
    }
    // rolka z patternu
    else if (playerRow.stepOpen && playerRow.roll.isOn)
    {
        if (playerRow.roll.period != fx.ROLL_PERIOD_NONE)
        {
            // sprawdzamy timer microstepów, czy jest wielokrotrością rolki
            uint16_t microsteps = rollValToMicroSteps(playerRow.roll.period);
            if (microsteps)
            {
                if (((playerRow.stepTimer % microsteps) == 1) &&
                    (playerRow.stepTimer != 1 || forceFirstRollWhenNoNote))
                {
                    playerRow.stepContRoll = playerRow.stepSent;

                    playerRow.noteLength = rollValToMicroSteps(playerRow.roll.period) / 2;

                    if (playerRow.roll.type == fx.rollType_noteUp)
                    {
                        playerRow.stepContRoll.note =
                          clamp<int>(playerRow.stepContRoll.note + 1, 0, 127);
                    }
                    else if (playerRow.roll.type == fx.rollType_noteDown)
                    {
                        playerRow.stepContRoll.note =
                          clamp<int>(--playerRow.stepContRoll.note, 0, 127);
                    }
                    else if (playerRow.roll.type == fx.rollType_noteRandom)
                    {
                        playerRow.stepContRoll.note =
                          clamp<int>(utilMathRandom::random(playerRow.stepContRoll.note - 12,
                                                            playerRow.stepContRoll.note + 12 + 1),
                                     0,
                                     127);
                    }

                    if (playerRow.roll.isOn &&
                        playerRow.roll.volumeOption != rollVolumeOption_const)
                    {
                        uint8_t rollVelo = getRollVelo(row, &playerRow.stepContRoll);
                        if (isSampleInstrument(playerRow.stepContRoll.instrument))
                        {
                            auto& fxItem = playerRow.stepContRoll.fx[playerRow.roll.fxSlot];
                            fxItem.type = fx.FX_TYPE_VOLUME;
                            fxItem.value = rollVelo > 100 ? 100 : rollVelo;
                        }
                        else
                        {
                            playerRow.stepContRoll.volume = rollVelo > 127 ? 127 : rollVelo;
                        }
                    }

                    if ((isMidiInstrument(stepToSend.instrument) ||
                         isSynthInstrument(stepToSend.instrument)) &&
                        playerRow.tuneMidi.isActive)
                    {
                        stepToSend.note =
                          clamp<int>(stepToSend.note + playerRow.tuneMidi.tune, 0, 127);
                    }

                    if (player.track[row].noteOpen)
                    {
                        stopOpenNoteOnTrack(row, true);
                    }
                    sendNoteOn(row, &playerRow.stepContRoll);
                    playerRow.noteOpen = 1;
                    playerRow.noteTimer = 0;

                    playerRow.stepSent = playerRow.stepContRoll;
                }
            }
        }
    }
}

AUDIO_CORE_RAM uint8_t Sequencer::getNextArpNote(strPlayer::strPlayerTrack* playerRow)
{
    int16_t noteToRet = playerRow->arp.baseNote;
    uint16_t chordBitmask = getChordBitmaskForArp(playerRow->arp.chord);

    switch (playerRow->arp.option)
    {
        case fx.arpOption_up:

            for (uint8_t a = 0; a < 12; a++)
            {
                uint16_t posInBitmaskToCheck = (playerRow->arp.nextPosInBitmask + a) % 12;

                if ((chordBitmask & (0x0001 << posInBitmaskToCheck)))
                {
                    noteToRet = playerRow->arp.baseNote + posInBitmaskToCheck;
                    playerRow->arp.nextPosInBitmask = posInBitmaskToCheck + 1;
                    break;
                }
            }

            break;

        case fx.arpOption_down:

            for (int8_t a = 0; a < 12; a++)
            {
                int16_t posInBitmaskToCheck = (playerRow->arp.nextPosInBitmask - a + 12) % 12;

                if ((chordBitmask & (0x0001 << posInBitmaskToCheck)))
                {
                    noteToRet = playerRow->arp.baseNote + posInBitmaskToCheck;
                    playerRow->arp.nextPosInBitmask = (int16_t)(posInBitmaskToCheck - 1 + 12) % 12;
                    break;
                }
            }

            break;

        case fx.arpOption_random:
        {
            int16_t posInBitmaskToCheck =
              (playerRow->arp.nextPosInBitmask + utilMathRandom::random(0, 11) + 12) % 12;

            for (uint8_t a = 0; a < 12; a++)
            {
                if ((chordBitmask & (0x0001 << posInBitmaskToCheck)))
                {
                    noteToRet = playerRow->arp.baseNote + posInBitmaskToCheck;
                    playerRow->arp.nextPosInBitmask = (int16_t)(posInBitmaskToCheck + 1 + 12) % 12;
                    break;
                }
                posInBitmaskToCheck = (posInBitmaskToCheck + 1) % 12;
            }

            break;
        }

        default: break;
    }

    return clamp<int>(noteToRet, 0, 127);
}

void Sequencer::setStepRepeater(uint8_t track, int8_t stepRepeat)
{
    player.track[track].performanceStepRepeater = stepRepeat;
}
void Sequencer::setPerformancePlayMode(uint8_t track, int8_t playmode)
{
    // The value indexes the custom play orders. Stored performance values are
    // loaded from the project and the config without the range of their effect.
    player.track[track].performancePlayMode =
      clamp<int>(playmode, PLAYMODE_MIN, PLAYMODE_CUSTOM_20);
}

// zamienia fx val na enRollPeriod
AUDIO_CORE_RAM int8_t Sequencer::rollValToPeriod(int8_t rollVal)
{
    return rollVal % (fx.ROLL_PERIOD_MAX + 1);
}

AUDIO_CORE_RAM uint16_t Sequencer::arpPeriodToMicrosteps(int8_t period)
{
    switch (period)
    {
        case fx.arpPeriod_8: return 8 * 48;
        case fx.arpPeriod_6: return 6 * 48;
        case fx.arpPeriod_4: return 4 * 48;
        case fx.arpPeriod_3: return 3 * 48;
        case fx.arpPeriod_2: return 2 * 48;
        case fx.arpPeriod_1: return 1 * 48;

        case fx.arpPeriod_1_2: return 48 / 2;
        case fx.arpPeriod_1_3: return 48 / 3;
        case fx.arpPeriod_1_4: return 48 / 4;
        case fx.arpPeriod_1_6: return 48 / 6;
        case fx.arpPeriod_1_8: return 48 / 8;

        default: return 48;
    }
}
uint16_t Sequencer::rollValToMicroSteps(int8_t rollVal)
{

    rollVal = rollVal % (fx.ROLL_PERIOD_MAX + 1);
    switch (rollVal)
    {
        case fx.ROLL_PERIOD_NONE: return 0;
        case fx.ROLL_PERIOD_1_1: return 48;
        case fx.ROLL_PERIOD_1_2: return 24;
        case fx.ROLL_PERIOD_1_3: return 16;
        case fx.ROLL_PERIOD_1_4: return 12;
        case fx.ROLL_PERIOD_1_6: return 8;
        case fx.ROLL_PERIOD_1_8: return 6;
        case fx.ROLL_PERIOD_1_12: return 4;
        case fx.ROLL_PERIOD_1_16: return 3;
        default: return 0;
    }
    return 0;
}

uint16_t Sequencer::stutterValToPeriod(int8_t rollVal)
{

    rollVal = rollVal % (fx.STUTTER_PERIOD_MAX + 1);
    switch (rollVal)
    {
        case fx.STUTTER_PERIOD_NONE: return 0;

        case fx.STUTTER_PERIOD_16_1: return 16 * 48;
        case fx.STUTTER_PERIOD_12_1: return 12 * 48;
        case fx.STUTTER_PERIOD_8_1: return 8 * 48;
        case fx.STUTTER_PERIOD_6_1: return 6 * 48;
        case fx.STUTTER_PERIOD_4_1: return 4 * 48;
        case fx.STUTTER_PERIOD_3_1: return 3 * 48;
        case fx.STUTTER_PERIOD_2_1: return 2 * 48;

        case fx.STUTTER_PERIOD_1_1: return 48;
        case fx.STUTTER_PERIOD_1_2: return 24;
        case fx.STUTTER_PERIOD_1_3: return 16;
        case fx.STUTTER_PERIOD_1_4: return 12;
        case fx.STUTTER_PERIOD_1_6: return 8;
        case fx.STUTTER_PERIOD_1_8: return 6;
        case fx.STUTTER_PERIOD_1_12: return 4;
        case fx.STUTTER_PERIOD_1_16: return 3;
        default: return 0;
    }
    return 0;
}

void Sequencer::play(void)
{
    play(0);
}
AUDIO_CORE_RAM void Sequencer::play(uint8_t fromPos)
{
    fromPos = clamp<int>(fromPos, 0, MAXSTEP);

    LOGF("sequencer play from step %d (%s mode)\n", fromPos, player.songMode ? "song" : "pattern");

    if (!(isRec() && isPreRollConfigOn()))
    {
        stopAllOpenNotes();
    }

    songTimer.start();

    engine.endAllFx();

    reset_actual_pos();
    resetChanceState();

    player.isStop = 0;
    player.isPlay = 1;
    timerTicks = 1;
    internalTickClock.reset();

    player.uStep = 1;
    player.globalPos = fromPos;
    for (int trackIdx = 0; trackIdx < kAllTracks; trackIdx++)
    {
        player.track[trackIdx].uStep = 1;
        player.track[trackIdx].actual_pos = fromPos;
        player.track[trackIdx].actual_pos_absolute = fromPos;

        if (isCustomPlayMode(trackIdx))
        {
            applyRowCustomFx(trackIdx, fromPos);
        }
    }

    sendMidiStart();
}

void Sequencer::playSelection(bool looped) // potrzebuje aktualnego zaznaczenia
{
    if (player.isPlay && !player.selectionMode)
        return;

    if (!isSelectionCorrect(&selection))
    {
        assert(false);
        return;
    }

    songTimer.start();

    player.loopSelection = selection;

    songTimer.start();
    engine.endAllFx();

    reset_actual_pos();
    resetChanceState();
    for (uint8_t trackIdx = 0; trackIdx < kAllTracks; trackIdx++)
    {
        player.track[trackIdx].uStep = 1;
        player.track[trackIdx].actual_pos = player.loopSelection.firstStep;
        player.track[trackIdx].actual_pos_absolute = player.loopSelection.firstStep;
        player.track[trackIdx].isActive = 0;
    }

    for (uint8_t a = player.loopSelection.firstTrack; a <= player.loopSelection.lastTrack; a++)
    {
        player.track[a].isActive = 1;
    }

    player.songMode = 0;
    player.selectionMode = 1;
    player.selectionModeLooped = looped;
    player.selectionModeLastStep = player.loopSelection.lastStep;

    timerTicks = 1;

    player.uStep = 1;

    player.globalPos = player.loopSelection.firstStep;

    player.isStop = 0;
    player.isPlay = 1;

    if (looped)
    {
        sendMidiStart();
    }
}

float Sequencer::getPlaySelectionProgress(void) // potrzebuje aktualnego zaznaczenia
{
    if (player.selectionMode && isSelectionCorrect(&selection))
    {
        strSelection* s = &selection;
        float retVal =
          (float(player.globalPos - s->firstStep) / float(s->lastStep - s->firstStep)) * 100.0f;
        return clamp(retVal, 0.0f, 100.0f);
    }
    else
    {
        return 0.0f;
    }
}

void Sequencer::playPattern()
{
    playPattern(0);
}

AUDIO_CORE_RAM void Sequencer::playPattern(uint8_t fromPos)
{
    player.songMode = 0;
    play(fromPos);
}

void Sequencer::playSong(void)
{
    playSongFromPosition(0);
}

void Sequencer::playSongFromPosition(uint8_t fromPos)
{
    uint8_t patternToStart = newFileManager.getSongPattern(fromPos);
    uint8_t currentPatternIdx = mtProject.values.actualPattern;

    newFileManager.saveWorkspacePatternNow(currentPatternIdx);

    if (patternToStart != currentPatternIdx)
    {
        newFileManager.loadWorkspacePatternNow(patternToStart);
        DISABLE_GLOBAL_INTERRUPTS();
        newFileManager.setSongPos(fromPos);
        switchRamPatternsNow();
        ENABLE_GLOBAL_INTERRUPTS();
    }
    else
    {
        newFileManager.setSongPos(fromPos);
    }

    player.songMode = 1;
    play();
}

void Sequencer::loopCurrentPattern(uint8_t songPos)
{
    if (player.isPlay == 0)
    {
        playSongFromPosition(songPos);
    }

    DISABLE_GLOBAL_INTERRUPTS();
    newFileManager.setSongPos(songPos);
    player.songMode = 0;
    ENABLE_GLOBAL_INTERRUPTS();
}

void Sequencer::continueSong(uint8_t songPos)
{
    if (player.isPlay == 0)
    {
        playSongFromPosition(songPos);
        return;
    }

    DISABLE_GLOBAL_INTERRUPTS();
    newFileManager.setSongPos(songPos);
    player.songMode = 1;
    ENABLE_GLOBAL_INTERRUPTS();
}

AUDIO_CORE_RAM void Sequencer::stopAllMidiOpenNotes(void)
{
    for (uint8_t trackIdx = 0; trackIdx < kAllTracks; trackIdx++)
    {
        int instrument = player.track[trackIdx].stepSent.instrument;
        if (isMidiInstrument(instrument) || isSynthInstrument(instrument))
        {
            sendNoteOff(trackIdx, &player.track[trackIdx].stepSent);
        }
    }
}

void Sequencer::stopAllOpenNotes(void)
{
    for (int trackIdx = 0; trackIdx < kAllTracks; trackIdx++)
    {
        stopOpenNoteOnTrack(trackIdx);
    }
    liveRecHeldNotesCount = 0;
}

void Sequencer::stopOpenNoteOnTrack(int track, bool leaveStepOpen)
{
    assert(track >= 0);
    assert(track < kAllTracks);

    if (player.track[track].recOpen || player.track[track].stepOpen || player.track[track].noteOpen)
    {
        player.track[track].stepOpen = leaveStepOpen;
        player.track[track].noteOpen = 0;
        player.track[track].recOpen = 0;
        sendNoteOff(track, &player.track[track].stepSent);
    }
}

AUDIO_CORE_RAM void Sequencer::stopManualNotes(void)
{
    for (uint8_t tr = 0; tr < kAllTracks; tr++)
    {
        if (player.track[tr].noteOpen && player.track[tr].recOpen)
        {
            int instrument = player.track[tr].stepSent.instrument;
            bool isMidi = isMidiInstrument(instrument);
            bool isSynth = isSynthInstrument(instrument);

            if (isMidi || isSynth)
            {
                sendMidiNoteOff(player.track[tr].stepSent.note,
                                0,
                                instrumentIdxToMidiChannel(instrument),
                                isSynth);
            }
            else if (isSampleTrack(tr))
            {
                instrumentPlayer[tr].noteOff();
            }
            player.track[tr].noteOpen = 0;
            player.track[tr].recOpen = 0;
            player.track[tr].noteSource = -1;
        }
    }
}

void Sequencer::stop(void)
{
    stop(StopContext::mainLoop);
}

void Sequencer::stop(StopContext context)
{
    LOG("sequencer stop");

    songTimer.stop();
    stopAllOpenNotes();
    cancelFxes();

    player.isPlay = 0;
    player.isStop = 1;
    player.isREC = 0;
    player.startRecOnExtClk = 0;

    player.uStep = 0;
    player.selectionMode = false;
    player.selectionModeLooped = false;

    player.swing.resetSwing();

    player.preRecMetronomeActive = 0;

    player.breakPattern = 0;

    timerTicks = 0;
    internalTickClock.reset();

    if (player.onPatternOrSelectionEnd == cbkTurnOffPerformanceMode)
    {
        player.onPatternOrSelectionEnd();
        if (sequencialSwitch_GetNext() > 0)
        {
            if (context == StopContext::audioCallback)
            {
                player.reloadPatternAfterStop = true;
            }
            else
            {
                newFileManager.loadWorkspacePatternNow(mtProject.values.actualPattern);
                sequencer.switchRamPatternsNow();
            }
        }
    }
    player.onGlobalStep = nullCallback;

    sequencialSwitch_Reset();

    for (int a = 0; a < kAllTracks; a++)
    {
        player.track[a].uStep = 0;

        player.track[a].roll.isOn = 0;
        player.track[a].isActive = 1;

        player.track[a].stepSent.note = STEP_NOTE_EMPTY;
    }

    reset_actual_pos();
    resetChanceState();

    player.tempoFx.reset();

    sendMidiStop();

    patternEditor.refreshEditStateFromOutsideModule();

    for (int trackIdx = 0; trackIdx < kAllTracks; trackIdx++)
    {
        player.track[trackIdx].stepSent.note = STEP_NOTE_EMPTY;
    }
}

void Sequencer::reloadPatternAfterStop()
{
    if (!player.reloadPatternAfterStop)
    {
        return;
    }
    player.reloadPatternAfterStop = false;

    newFileManager.loadWorkspacePatternNow(mtProject.values.actualPattern);
    // Playback may have been started again since the stop.
    DISABLE_GLOBAL_INTERRUPTS();
    switchRamPatternsNow();
    ENABLE_GLOBAL_INTERRUPTS();
}

void Sequencer::updatePatternManagment()
{
    static bool last_isPlay = false;

    if (isPlay() != last_isPlay)
    {
        if (!isPlay())
        {
            for (int trackIdx = 0; trackIdx < kAllTracks; trackIdx++)
            {
                if (isPerformanceMode())
                {
                    if (player.track[trackIdx].performanceSourcePattern != -1)
                    {
                        setTrackToLoadOnSwitch(trackIdx,
                                               player.track[trackIdx].performanceSourcePattern);
                        switchPerformanceTrackNow(trackIdx);
                    }
                }
                else
                {
                    player.track[trackIdx].performanceSourcePattern = -1;
                }
            }
        }

        last_isPlay = isPlay();
    }
}

bool Sequencer::startRecOnExtClk_Active(void)
{
    return player.startRecOnExtClk;
}
void Sequencer::startRecOnExtClk_Reset(void)
{
    player.startRecOnExtClk = 0;
}
void Sequencer::recStart(void)
{
    player.isREC = 1;
    stopAllOpenNotes();

    if (!isSetInternalClock())
    {
        player.startRecOnExtClk = 1;
    }

    if (isPreRollConfigOn() && isSetInternalClock())
    {
        engine.endAllFx();
        reset_actual_pos();
        resetChanceState();
        player.preRecMetronomeActive = 1;
        showPreRollPopup();
    }
    else
    {
        if (isSetInternalClock())
        {
            playPattern();
        }
    }
}

void Sequencer::showPreRollPopup()
{
    uint8_t stepSec = (uint8_t)clamp<>(
      ((float)getMetronomeNumerator() * (float)getMetronomeDenominator() * getStepPeriodMs()) /
        1000.0f,
      0.0f,
      (float)std::numeric_limits<uint8_t>::max());

    mtPopups.show(0, "Counting in", std::max<uint8_t>(1, stepSec));
}

void Sequencer::recEnable(bool enable)
{
    player.isREC = enable;
    if (!enable)
    {
        liveRecHeldNotesCount = 0;
    }
}

AUDIO_CORE_RAM uint8_t Sequencer::isCustomPlayMode(uint8_t row)
{
    return player.performanceMode && player.track[row].performancePlayMode >= PLAYMODE_CUSTOM_1;
}

void Sequencer::switchRowStep(uint8_t row) // przełączamy stepy w zależności od trybu grania
{
    assert(row < kAllTracks);

    int8_t playMode = PLAYMODE_FORWARD;
    // length => lastStepIndex
    uint8_t trackLength = currentPattern->track[0].length;

    if (row == kAllTracks - 1) // operacje dla ostatniego
    {
        player.swing.toogle();

        if (player.tempoFx.stopAfterCurrentStep)
        {
            player.tempoFx.reset();

            // if exporting end export here
            if (player.onPatternOrSelectionEnd == cbkSetCurrentExporterOnLastStep ||
                player.onSongEnd == cbkSetCurrentExporterOnLastStep)
            {
                cbkSetCurrentExporterOnLastStep();
                return;
            }

            stop(StopContext::audioCallback);
            return;
        }
    }

    if (player.performanceMode && isPerformanceTrack(row))
    {
        playMode = player.track[row].performancePlayMode;
        trackLength = player.performance.trackLength[row];
    }

    if (player.performance.patternLength > -1)
    {
        trackLength = player.performance.patternLength;
    }

    player.endOfPattern = false;
    switch (playMode)
    {
        case PLAYMODE_FORWARD:
        {
            int startPos = player.selectionModeLooped ? player.loopSelection.firstStep : 0;
            int lastPos = player.selectionModeLooped
                            ? std::min(player.loopSelection.lastStep, trackLength)
                            : trackLength;

            // check if length was changed meantime
            if (player.selectionModeLooped && lastPos < player.loopSelection.firstStep)
            {
                stop(StopContext::audioCallback);
                return;
            }

            player.track[row].actual_pos_absolute++;

            if ((player.track[row].actual_pos_absolute > lastPos) || (player.breakPattern))
            {
                player.track[row].actual_pos_absolute = startPos;
                if (row == kAllTracks - 1)
                {
                    player.endOfPattern = true;
                }
                player.track[row].actual_pos_absolute = 0;
            }

            player.track[row].actual_pos++;
            if ((player.track[row].actual_pos > lastPos) || (player.breakPattern))
            {
                player.track[row].actual_pos = startPos;
                player.track[row].passCount++;

                if (player.track[row].performanceSourcePattern != -1)
                {
                    switchPerformanceTrackNow(row);
                    cancelFxes(row);

                    player.track[row].performanceSourcePattern = -1;
                }

                if (row == kAllTracks - 1)
                {
                    player.endOfPattern = true;
                }
            }
        }
        break;
        case PLAYMODE_BACKWARD:

            player.track[row].actual_pos_absolute++;
            if ((player.track[row].actual_pos_absolute > trackLength) || (player.breakPattern))
            {
                player.track[row].actual_pos_absolute = 0;
                if (row == kAllTracks - 1)
                {
                    player.endOfPattern = true;
                }
            }

            player.track[row].actual_pos--;
            if ((player.track[row].actual_pos < 0))
            {
                reset_actual_pos(row);
                player.track[row].passCount++;

                if (player.track[row].performanceSourcePattern != -1)
                {
                    switchPerformanceTrackNow(row);
                    cancelFxes(row);
                    player.track[row].performanceSourcePattern = -1;
                }

                if ((player.onPatternOrSelectionEnd != NULL) && (row == kAllTracks - 1))
                    player.onPatternOrSelectionEnd();
            }
            break;

        case PLAYMODE_RANDOM:
            player.track[row].actual_pos = utilMathRandom::random(0, trackLength + 1);

            player.track[row].actual_pos_absolute++;
            if ((player.track[row].actual_pos_absolute > trackLength) || (player.breakPattern))
            {
                player.track[row].actual_pos_absolute = 0;
                player.track[row].passCount++;
                if (row == kAllTracks - 1)
                {
                    player.endOfPattern = true;
                }

                if (player.track[row].performanceSourcePattern != -1)
                {
                    switchPerformanceTrackNow(row);
                    cancelFxes(row);
                    player.track[row].performanceSourcePattern = -1;
                }
            }
            break;

        default:
            if (playMode >= PLAYMODE_CUSTOM_1)
            {
                uint8_t customPlayMode = player.track[row].performancePlayMode - PLAYMODE_CUSTOM_1;

                player.track[row].actual_pos_absolute++;
                if ((player.track[row].actual_pos_absolute > trackLength) || (player.breakPattern))
                {
                    player.track[row].actual_pos_absolute = 0;
                    player.track[row].passCount++;
                    if (row == kAllTracks - 1)
                    {
                        player.endOfPattern = true;
                    }

                    if (player.track[row].performanceSourcePattern != -1)
                    {
                        switchPerformanceTrackNow(row);
                        cancelFxes(row);
                        player.track[row].performanceSourcePattern = -1;
                    }
                }

                uint8_t nextStepInOrder =
                  player.track[row].actual_pos_absolute % playOrderWithFxLength;

                // 0 nie mozna
                if (playOrderWithFx[customPlayMode][nextStepInOrder].stepNumberToPlay != 0)
                {
                    player.track[row].actual_pos =
                      playOrderWithFx[customPlayMode][nextStepInOrder].stepNumberToPlay - 1;
                    player.track[row].actual_pos = player.track[row].actual_pos % (trackLength + 1);
                }
                else
                {
                    player.track[row].actual_pos = 0;
                }

                applyRowCustomFx(row, nextStepInOrder);
            }
            break;
    }

    if (player.endOfPattern)
    {
        bool isNextPatternAvailable = 0; // jeśli 0 to song sie skonczyl
        bool patternSwitched = false;
        player.breakPattern = 0;

        if (player.sequencialSwitch_isArmed && player.sequencialSwitch_loaded)
        {
            if (!newFileManager.isPatternPreloaded(player.sequencialSwitch_pattern))
            {
                stopForMissingNextPattern();
                return;
            }

            player.songMode = 0;

            reset_actual_pos();
            switchRamPatternsNow();
            patternSwitched = true;

            sequencialSwitch_Reset();
        }
        else if (player.songMode)
        {
            if (!newFileManager.isPatternPreloaded(newFileManager.getNextSongPattern()))
            {
                stopForMissingNextPattern();
                return;
            }

            isNextPatternAvailable = newFileManager.switchNextPatternInSong();

            reset_actual_pos();
            switchRamPatternsNow();
            patternSwitched = true;

            if (player.performanceMode)
            {
                for (uint8_t a = 0; a < kPerformanceTracks; a++)
                {
                    player.performance.trackLength[a] = getActualPattern()->track[0].length;
                }
            }
        }

        player.onPatternOrSelectionEnd();
        if (!isNextPatternAvailable)
            player.onSongEnd();

        if (patternSwitched && mtInterface.isPerformanceModeScreenActive())
        {
            for (uint8_t a = 0; a < kPerformanceTracks; a++)
            {
                mtProject.perfTracksPatterns[a] = mtProject.values.actualPattern;
            }
            PM->refreshTrackPattern = 1;
        }
    }
}

// The next pattern is asked for one step before it is needed and loaded by the main loop.
// When it is not in the buffer pattern by then, switching would play whatever the buffer
// holds under the next pattern's number, and a later save would write that into its file.
void Sequencer::stopForMissingNextPattern()
{
    if (player.onPatternOrSelectionEnd == cbkSetCurrentExporterOnLastStep ||
        player.onSongEnd == cbkSetCurrentExporterOnLastStep)
    {
        cbkSetCurrentExporterOnLastStep();
        return;
    }

    stop(StopContext::audioCallback);
}

AUDIO_CORE_FLASH void Sequencer::applyRowCustomFx(uint8_t tr, uint8_t stepInCustomOrder)
{
    uint8_t customPlayMode = player.track[tr].performancePlayMode - PLAYMODE_CUSTOM_1;

    switch (playOrderWithFx[customPlayMode][stepInCustomOrder].fxOption)
    {
        case playModeFx_none:
            player.track[tr].custom_fx = fx.FX_TYPE_NONE;
            player.track[tr].custom_fx_value = 0;
            break;
        case playModeFx_rollFlat_1_4_P30:
            if (utilMathRandom::random(0, 100) < 30)
            {
                player.track[tr].custom_fx = fx.FX_TYPE_ROLL;
                player.track[tr].custom_fx_value =
                  fx.ROLL_PERIOD_1_4 + fx.rollType_const * (fx.ROLL_PERIOD_MAX + 1);
            }
            else
            {
                player.track[tr].custom_fx = fx.FX_TYPE_NONE;
            }
            break;
        case playModeFx_rollFlat_1_4_P60:
            if (utilMathRandom::random(0, 100) < 60)
            {
                player.track[tr].custom_fx = fx.FX_TYPE_ROLL;
                player.track[tr].custom_fx_value =
                  fx.ROLL_PERIOD_1_4 + fx.rollType_const * (fx.ROLL_PERIOD_MAX + 1);
            }
            break;
        case playModeFx_rollFlat_1_4_P100:
            if (utilMathRandom::random(0, 100) < 100)
            {
                player.track[tr].custom_fx = fx.FX_TYPE_ROLL;
                player.track[tr].custom_fx_value =
                  fx.ROLL_PERIOD_1_4 + fx.rollType_const * (fx.ROLL_PERIOD_MAX + 1);
            }
            break;

            //****************

        case playModeFx_rollDecVelo_1_4_P30:
            if (utilMathRandom::random(0, 100) < 30)
            {
                player.track[tr].custom_fx = fx.FX_TYPE_ROLL;
                player.track[tr].custom_fx_value =
                  fx.ROLL_PERIOD_1_4 + fx.rollType_volDown * (fx.ROLL_PERIOD_MAX + 1);
            }
            else
            {
                player.track[tr].custom_fx = fx.FX_TYPE_NONE;
            }
            break;
        case playModeFx_rollDecVelo_1_4_P60:
            if (utilMathRandom::random(0, 100) < 60)
            {
                player.track[tr].custom_fx = fx.FX_TYPE_ROLL;
                player.track[tr].custom_fx_value =
                  fx.ROLL_PERIOD_1_4 + fx.rollType_volDown * (fx.ROLL_PERIOD_MAX + 1);
            }
            else
            {
                player.track[tr].custom_fx = fx.FX_TYPE_NONE;
            }
            break;
        case playModeFx_rollDecVelo_1_4_P100:
            if (utilMathRandom::random(0, 100) < 100)
            {
                player.track[tr].custom_fx = fx.FX_TYPE_ROLL;
                player.track[tr].custom_fx_value =
                  fx.ROLL_PERIOD_1_4 + fx.rollType_volDown * (fx.ROLL_PERIOD_MAX + 1);
            }
            else
            {
                player.track[tr].custom_fx = fx.FX_TYPE_NONE;
            }
            break;

            //****************

        case playModeFx_rollDecVelo_1_2_P30:
            if (utilMathRandom::random(0, 100) < 30)
            {
                player.track[tr].custom_fx = fx.FX_TYPE_ROLL;
                player.track[tr].custom_fx_value =
                  fx.ROLL_PERIOD_1_2 + fx.rollType_volDown * (fx.ROLL_PERIOD_MAX + 1);
            }
            else
            {
                player.track[tr].custom_fx = fx.FX_TYPE_NONE;
            }
            break;
        case playModeFx_rollDecVelo_1_2_P60:
            if (utilMathRandom::random(0, 100) < 60)
            {
                player.track[tr].custom_fx = fx.FX_TYPE_ROLL;
                player.track[tr].custom_fx_value =
                  fx.ROLL_PERIOD_1_2 + fx.rollType_volDown * (fx.ROLL_PERIOD_MAX + 1);
            }
            else
            {
                player.track[tr].custom_fx = fx.FX_TYPE_NONE;
            }
            break;
        case playModeFx_rollDecVelo_1_2_P100:
            if (utilMathRandom::random(0, 100) < 100)
            {
                player.track[tr].custom_fx = fx.FX_TYPE_ROLL;
                player.track[tr].custom_fx_value =
                  fx.ROLL_PERIOD_1_2 + fx.rollType_volDown * (fx.ROLL_PERIOD_MAX + 1);
            }
            else
            {
                player.track[tr].custom_fx = fx.FX_TYPE_NONE;
            }
            break;

            //****************

        case playModeFx_rollFlatVelo_1_1_P30:
            if (utilMathRandom::random(0, 100) < 30)
            {
                player.track[tr].custom_fx = fx.FX_TYPE_ROLL;
                player.track[tr].custom_fx_value =
                  fx.ROLL_PERIOD_1_1 + fx.rollType_const * (fx.ROLL_PERIOD_MAX + 1);
            }
            else
            {
                player.track[tr].custom_fx = fx.FX_TYPE_NONE;
            }
            break;
        case playModeFx_rollFlatVelo_1_1_P60:
            if (utilMathRandom::random(0, 100) < 60)
            {
                player.track[tr].custom_fx = fx.FX_TYPE_ROLL;
                player.track[tr].custom_fx_value =
                  fx.ROLL_PERIOD_1_1 + fx.rollType_const * (fx.ROLL_PERIOD_MAX + 1);
            }
            else
            {
                player.track[tr].custom_fx = fx.FX_TYPE_NONE;
            }
            break;
        case playModeFx_rollFlatVelo_1_1_P100:
            if (utilMathRandom::random(0, 100) < 100)
            {
                player.track[tr].custom_fx = fx.FX_TYPE_ROLL;
                player.track[tr].custom_fx_value =
                  fx.ROLL_PERIOD_1_1 + fx.rollType_const * (fx.ROLL_PERIOD_MAX + 1);
            }
            else
            {
                player.track[tr].custom_fx = fx.FX_TYPE_NONE;
            }
            break;

        case playModeFx_rollDecVelo_1_1_P30:
            if (utilMathRandom::random(0, 100) < 30)
            {
                player.track[tr].custom_fx = fx.FX_TYPE_ROLL;
                player.track[tr].custom_fx_value =
                  fx.ROLL_PERIOD_1_1 + fx.rollType_volDown * (fx.ROLL_PERIOD_MAX + 1);
            }
            else
            {
                player.track[tr].custom_fx = fx.FX_TYPE_NONE;
            }
            break;

        case playModeFx_rollDecVelo_1_1_P60:
            if (utilMathRandom::random(0, 100) < 60)
            {
                player.track[tr].custom_fx = fx.FX_TYPE_ROLL;
                player.track[tr].custom_fx_value =
                  fx.ROLL_PERIOD_1_1 + fx.rollType_volDown * (fx.ROLL_PERIOD_MAX + 1);
            }
            else
            {
                player.track[tr].custom_fx = fx.FX_TYPE_NONE;
            }
            break;

        case playModeFx_rollDecVelo_1_1_P100:
            if (utilMathRandom::random(0, 100) < 100)
            {
                player.track[tr].custom_fx = fx.FX_TYPE_ROLL;
                player.track[tr].custom_fx_value =
                  fx.ROLL_PERIOD_1_1 + fx.rollType_volDown * (fx.ROLL_PERIOD_MAX + 1);
            }
            else
            {
                player.track[tr].custom_fx = fx.FX_TYPE_NONE;
            }
            break;

        default:
            player.track[tr].custom_fx = fx.FX_TYPE_NONE;
            player.track[tr].custom_fx_value = 0;
            break;
    }
}

void Sequencer::alignToGlobalPos(uint8_t row)
{
    player.track[row].actual_pos = player.globalPos;
}

AUDIO_CORE_RAM void Sequencer::reset_actual_pos(void)
{
    for (int row = 0; row < kAllTracks; row++)
    {
        reset_actual_pos(row);
    }

    player.globalPos = 0;
}

AUDIO_CORE_RAM void Sequencer::reset_actual_pos(uint8_t row, uint8_t delta /* = 0 */)
{

    if (player.track[row].performancePlayMode == PLAYMODE_FORWARD)
    {
        player.track[row].actual_pos = MINSTEP + delta;
    }

    else if (player.track[row].performancePlayMode == PLAYMODE_BACKWARD)
    {
        player.track[row].actual_pos = currentPattern->track[row].length - delta;
    }

    else if (player.track[row].performancePlayMode == PLAYMODE_PINGPONG)
    {
        player.track[row].actual_pos = MINSTEP + delta;
    }
}

AUDIO_CORE_RAM void Sequencer::resetChanceState(void)
{
    player.activePatternIdx = mtProject.values.actualPattern;
    for (int row = 0; row < kAllTracks; row++)
    {
        player.track[row].passCount = 0;
        player.track[row].lastCondFired = true;
    }
}

AUDIO_CORE_RAM bool Sequencer::evaluateChanceFx(uint8_t row, uint8_t value)
{
    assert(row < kAllTracks);
    strPlayer::strPlayerTrack& playerRow = player.track[row];

    if (value <= fx.CHANCE_PROB_MAX)
    {
        bool fired = (utilMathRandom::random(0, fx.CHANCE_PROB_MAX) < value);
        playerRow.lastCondFired = fired;
        return fired;
    }
    else if (value == fx.CHANCE_GROUP_WITH_LAST)
    {
        return playerRow.lastCondFired;
    }
    else if (value == fx.CHANCE_NOT_LAST)
    {
        return !playerRow.lastCondFired;
    }
    else if (value >= fx.CHANCE_PLAY_SKIP_START && value <= fx.CHANCE_PLAY_SKIP_END)
    {
        uint8_t idx = value - fx.CHANCE_PLAY_SKIP_START;
        uint8_t n = 1 + idx / fx.CHANCE_CYCLE_STEPS;
        uint8_t m = 1 + idx % fx.CHANCE_CYCLE_STEPS;
        bool fired = (playerRow.passCount % (n + m)) < n;
        playerRow.lastCondFired = fired;
        return fired;
    }
    else if (value >= fx.CHANCE_SKIP_PLAY_START && value <= fx.CHANCE_SKIP_PLAY_END)
    {
        uint8_t idx = value - fx.CHANCE_SKIP_PLAY_START;
        uint8_t n = 1 + idx / fx.CHANCE_CYCLE_STEPS;
        uint8_t m = 1 + idx % fx.CHANCE_CYCLE_STEPS;
        bool fired = (playerRow.passCount % (n + m)) >= n;
        playerRow.lastCondFired = fired;
        return fired;
    }

    return true;
}

float Sequencer::getCurrentTempo()
{
    if (isSetInternalClock() || renderMode || player.selectionMode)
    {
        if (player.tempoFx.isActive)
        {
            return player.tempoFx.tempo;
        }
        else
        {
            return mtProject.values.globalTempo;
        }
    }
    else if (player.tempoFx.isActive)
    {
        return player.tempoFx.tempo;
    }
    else
    {
        return externalTempo;
    }
}

uint32_t Sequencer::getSeqTimer()
{
    return timerTicks;
}

void Sequencer::detachFromTimer(void) // make time abstract
{
    renderMode = 1;
}
void Sequencer::attachToTimer(void) // make time abstract
{
    renderMode = 0;
}

AUDIO_CORE_RAM float Sequencer::getStepPeriodMs(void)
{
    float timerPeriodUs = (312500.0f / getCurrentTempo()) / 12.0f;
    float timerPeriodMs = timerPeriodUs / 1000.0f;

    float stepPeriodMs = 12 * 48 * timerPeriodMs;

    return stepPeriodMs;
}

AUDIO_CORE_RAM float Sequencer::getTimerPeriod_us(void)
{
    if (player.swing.isActive)
    {
        return ((3125.0 * player.swing.getTempoCorrection()) / getCurrentTempo()) / 12.0;
    }
    else
    {
        return (312500.0 / getCurrentTempo()) / 12.0;
    }
}

AUDIO_CORE_RAM void Sequencer::inc_uStepGlobal()
{
    player.uStep++;

    if (player.uStep > 48)
    {
        player.uStep = 1;
        player.globalPos++;

        if (player.selectionMode && !player.selectionModeLooped)
        {
            if (player.globalPos > player.selectionModeLastStep)
            {
                stop(StopContext::audioCallback);
                player.onPatternOrSelectionEnd();
            }
        }

        if (player.globalPos > getPatternLengthWithSel())
        {
            player.globalPos = 0;
        }

        player.onGlobalStep();
    }
}
AUDIO_CORE_RAM void Sequencer::inc_uStep(uint8_t row)
{
    player.track[row].uStep++;

    if (player.track[row].uStep > 48)
    {
        player.track[row].uStep = 1;
        switchRowStep(row);
    }
}

AUDIO_CORE_RAM bool Sequencer::isPlay(void)
{
    return player.isPlay;
}

AUDIO_CORE_RAM bool Sequencer::isRec(void)
{
    return player.isREC;
}

uint8_t Sequencer::isStop(void)
{
    return player.isStop;
}

uint16_t Sequencer::getChordBitmask(uint8_t chord)
{
    if (chord > MAX_CHORD)
        chord = MIN_CHORD;

    return chordDef[chord];
}
uint16_t Sequencer::getChordBitmaskForArp(uint8_t chord)
{
    if (chord > MAX_CHORD)
        chord = MIN_CHORD;

    if (chord == MIN_CHORD)
        return 0b1111111111111111;

    return chordDef[chord];
}

AUDIO_CORE_RAM void Sequencer::sendMidiChordOn(uint8_t track,
                                               strPlayer::strPlayerTrack::strSendStep* step)
{
    uint8_t chordNumber = 0;
    if (step->fx[0].type == fx.FX_TYPE_MIDI_CHORD)
    {
        chordNumber = step->fx[0].value;
    }
    else if (step->fx[1].type == fx.FX_TYPE_MIDI_CHORD)
    {
        chordNumber = step->fx[1].value;
    }
    else
        return;

    uint8_t instrumentVelo = getInstrumentVelo(step->instrument);
    int16_t chordBitmask = getChordBitmask(chordNumber);

    for (uint8_t a = 0; a < CHORD_FOR_MAX; a++)
    {
        int16_t noteToSend = step->note + a;
        if ((chordBitmask & (0x0001 << a)) && noteToSend <= STEP_NOTE_MAX &&
            noteToSend >= STEP_NOTE_MIN)

        {
            int velocity = step->volume == -1
                             ? instrumentVelo
                             : mapClamped<int>(step->volume, 0, 100, 0, kMaxMidiVelocity);

            int deltaPerformanceVelocityFx =
              mapClamped(player.track[track].performanceMidiVolumeFx.value,
                         player.track[track].performanceMidiVolumeFx.minValue,
                         player.track[track].performanceMidiVolumeFx.maxValue,
                         -kMaxMidiVelocity,
                         kMaxMidiVelocity);
            velocity = clamp(velocity + deltaPerformanceVelocityFx, 0, kMaxMidiVelocity);

            sendMidiNoteOn(noteToSend,
                           velocity,
                           instrumentIdxToMidiChannel(step->instrument),
                           isSynthInstrument(step->instrument));
        }
    }
}

void Sequencer::sendMidiChordOff([[maybe_unused]] uint8_t track,
                                 strPlayer::strPlayerTrack::strSendStep* step)
{
    uint8_t chordNumber = 0;
    if (step->fx[0].type == fx.FX_TYPE_MIDI_CHORD)
    {
        chordNumber = step->fx[0].value;
    }
    else if (step->fx[1].type == fx.FX_TYPE_MIDI_CHORD)
    {
        chordNumber = step->fx[1].value;
    }
    else
        return;

    int16_t chordBitmask = getChordBitmask(chordNumber);

    for (uint8_t a = 0; a < CHORD_FOR_MAX; a++)
    {
        int16_t noteToSend = step->note + a;

        if ((chordBitmask & (0x0001 << a)) && noteToSend <= STEP_NOTE_MAX &&
            noteToSend >= STEP_NOTE_MIN)
        {
            sendMidiNoteOff(noteToSend,
                            0,
                            instrumentIdxToMidiChannel(step->instrument),
                            isSynthInstrument(step->instrument));
        }
    }
}

AUDIO_CORE_RAM void Sequencer::sendNoteOn(uint8_t track,
                                          strPlayer::strPlayerTrack::strSendStep* step)
{
    bool toMidi = isMidiInstrument(step->instrument);
    bool toSynth = isSynthInstrument(step->instrument);

    if (toSynth && synthsMuteState == MuteState::kMuted)
    {
        return;
    }

    if (toMidi || toSynth)
    {
        if ((step->fx[0].type == fx.FX_TYPE_MIDI_CHORD && step->fx[1].type != fx.FX_TYPE_ARP) ||
            (step->fx[1].type == fx.FX_TYPE_MIDI_CHORD && step->fx[0].type != fx.FX_TYPE_ARP))
        {
            sendMidiChordOn(track, step);
        }
        else
        {
            uint8_t instrumentVelo = getInstrumentVelo(step->instrument);

            int velocity = step->volume == -1
                             ? instrumentVelo
                             : mapClamped<int>(step->volume, 0, 100, 0, kMaxMidiVelocity);

            int deltaPerformanceVelocityFx =
              mapClamped(player.track[track].performanceMidiVolumeFx.value,
                         player.track[track].performanceMidiVolumeFx.minValue,
                         player.track[track].performanceMidiVolumeFx.maxValue,
                         -kMaxMidiVelocity,
                         kMaxMidiVelocity);

            velocity = clamp(velocity + deltaPerformanceVelocityFx, 0, kMaxMidiVelocity);

            sendMidiNoteOn(
              step->note, velocity, instrumentIdxToMidiChannel(step->instrument), toSynth);
        }
    }
    else if (isSampleInstrument(step->instrument) && isSampleTrack(track))
    {
        instrumentPlayer[track].noteOn(step->instrument,
                                       step->note,
                                       step->volume,
                                       step->fx[0].type,
                                       step->fx[0].value,
                                       step->fx[1].type,
                                       step->fx[1].value);
    }
}

AUDIO_CORE_RAM void Sequencer::sendNoteOff(uint8_t track,
                                           strPlayer::strPlayerTrack::strSendStep* step,
                                           int8_t offOption)
{

    int instrument = step->instrument;
    bool isMidi = isMidiInstrument(instrument);
    bool isSynth = isSynthInstrument(instrument);
    if (isMidi || isSynth)
    {
        if ((step->fx[0].type == fx.FX_TYPE_MIDI_CHORD && step->fx[1].type != fx.FX_TYPE_ARP) ||
            (step->fx[1].type == fx.FX_TYPE_MIDI_CHORD && step->fx[0].type != fx.FX_TYPE_ARP))
        {
            sendMidiChordOff(track, step);
        }
        else
        {

            sendMidiNoteOff(step->note, 0, instrumentIdxToMidiChannel(instrument), isSynth);
        }
    }
    else if (isSampleTrack(track))
    {
        instrumentPlayer[track].noteOff(offOption < 0 ? offOption : step->note);
    }
}

int8_t Sequencer::velocityToVolume(int8_t velo)
{
    if (velo < 0)
        return velo;

    velo = clamp<int>(velo, 0, 127);
    return (int8_t)std::roundf(mapClamped<float>(velo, 0, 127, 0, 100));
}

int8_t Sequencer::volumeToVelocity(int8_t vol)
{
    if (vol < 0)
        return vol;

    vol = clamp<int>(vol, 0, 100);
    return (int8_t)std::roundf(mapClamped<float>(vol, 0, 100, 0, 127));
}

AUDIO_CORE_RAM void Sequencer::loadNextPattern(uint8_t patternNumber)
{
    assert(patternNumber > 0);
    newFileManager.loadWorkspacePattern(patternNumber);
}

uint8_t Sequencer::getDestRecordingStep(uint8_t track, bool quantize, bool noteOff)
{
    int destPos = player.track[track].actual_pos;

    if (quantize && player.track[track].uStep > 24)
    {
        destPos = (destPos + 1) % (getActualPattern()->track[0].length + 1);
    }

    if (noteOff && destPos == player.track[track].recNoteOnStepIdx)
    {
        destPos = (destPos + 1) % (getActualPattern()->track[0].length + 1);
    }

    return destPos;
}

uint8_t Sequencer::recMicrotiming()
{
    return mtConfig.general.recOptions == recOptions_microtimingAndVelocity ||
           mtConfig.general.recOptions == recOptions_microtiming;
}
uint8_t Sequencer::recVelocity()
{
    return mtConfig.general.recOptions == recOptions_microtimingAndVelocity ||
           mtConfig.general.recOptions == recOptions_velocity;
}

AUDIO_CORE_FLASH void Sequencer::handleNoteOn(
  byte channel, // pochodzenie grida np. GRID_OUTSIDE_PATTERN
  byte note,
  int8_t velocity, // 0-127, historyczna wartosc specjalna -1 jest tozsama z 127
  int16_t source)  // nr pada, jesli midi to source = nuta+100,
{
    if (!recVelocity() || velocity < 0)
    {
        velocity = 127;
    }

    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;

    if (!isPlay() && !player.preRecMetronomeActive)
    {
        // TRK-518 synth fxes are not closed on stop(), so we need to end them here
        engine.endAllFx(true);
    }

    bool wasChangeInPattern = false;
    bool padInstrumentEdit = false;

    if (channel == GRID_INSIDE_PATTERN_INSTRUMENT)
    {
        padInstrumentEdit = true;
        channel = GRID_OUTSIDE_PATTERN;
    }

    if (isEditMode() && channel != GRID_OUTSIDE_PATTERN)
    {
        strPattern::strTrack::strStep* step =
          &currentPattern->track[sel->firstTrack].step[sel->firstStep];
        if (!isMultiSelection())
        {
            newFileManager.storePatternUndoRevision();

            if (step->isNoteEmpty())
            {
                // xxx: ignorujemy source?
                step->instrument = changeInstrumentInSpecificOrder(
                  mtProject.values.lastUsedInstrument, 0, !isSampleTrack(sel->firstTrack));
            }
            step->note = note;

            wasChangeInPattern = true;
        }

        velocity = mapClamped<int>(velocity, 0, 127, 0, getInstrumentVelo(step->instrument));

        bool isSynth = isSynthInstrument(step->instrument);

        if (isMidiInstrument(step->instrument) || isSynth)
        {
            sendMidiNoteOn(
              step->note, velocity, instrumentIdxToMidiChannel(step->instrument), isSynth);
        }
        else if (isSampleTrack(sel->firstTrack))
        {
            instrumentPlayer[sel->firstTrack].noteOff();
            instrumentPlayer[sel->firstTrack].noteOn(step->instrument,
                                                     step->note,
                                                     velocityToVolume(velocity),
                                                     0,
                                                     0,
                                                     0,
                                                     0);

            engine.setLastUsedVoice(sel->firstTrack);
        }
    }
    else if (isRec())
    {
        int8_t singleArmedTrack = getSingleArmedTrack();
        int startTrack = (singleArmedTrack >= 0) ? singleArmedTrack : getActualTrack();
        int endTrack = (singleArmedTrack >= 0) ? (singleArmedTrack + 1) : kAllTracks;

        if (singleArmedTrack >= 0)
        {
            bool found = false;
            for (uint8_t i = 0; i < liveRecHeldNotesCount; i++)
            {
                if (liveRecHeldNotes[i].source == source)
                {
                    liveRecHeldNotes[i].note = note;
                    found = true;
                    break;
                }
            }
            if (!found && liveRecHeldNotesCount < kMaxLiveRecHeldNotes)
            {
                liveRecHeldNotes[liveRecHeldNotesCount++] = {source, note};
            }
        }

        bool isAnticipatedBeat1 = false;
        if (player.preRecMetronomeActive && isSetInternalClock())
        {
            uint8_t threshold = std::min<uint8_t>(mtConfig.general.recThreshold, recThreshold_max) * 4;
            if (threshold > 0)
            {
                int preRollLengthStep = getMetronomeDenominator() * getMetronomeNumerator();
                if (player.extRecMetronomeStep == preRollLengthStep)
                {
                    int stepTick = timerTicks % kTicksInStep;
                    int uStep = clamp<int>(stepTick / 12 + 1, 1, 48);
                    int microValue = mapClamped<int>(uStep, 1, 48, 0, 100);
                    if (microValue >= (100 - threshold))
                    {
                        isAnticipatedBeat1 = true;
                    }
                }
            }
        }

        for (int tr = startTrack; tr < endTrack; tr++)
        {
            uint8_t destinationStep = isAnticipatedBeat1 ? 0 : getDestRecordingStep(tr, !recMicrotiming());

            strPattern::strTrack::strStep* step = &currentPattern->track[tr].step[destinationStep];

            bool doPreview = true;

            if (!isPlay() && !player.preRecMetronomeActive)
            {
                doPreview = false;
            }

            /*
             * spr warunek wejscia w track
             */
            bool isPrerollPreviewOnly = player.preRecMetronomeActive && !isAnticipatedBeat1;
            if ((isPrerollPreviewOnly || (step->isNoteEmpty() && !step->hasAnyFx())) && isRecArmedOnTrack(tr))
            {
                if (singleArmedTrack < 0 && player.track[tr].noteOpen && !padInstrumentEdit)
                {
                    // jezeli ten track jest zajety to omijamy go,
                    // chyba, ze nagrywamy instrumenty padami
                    continue;
                }

                if (singleArmedTrack >= 0 && doPreview && player.track[tr].noteOpen)
                {
                    int prevInstrument = player.track[tr].stepSent.instrument;
                    bool isMidi = isMidiInstrument(prevInstrument);
                    bool isSynth = isSynthInstrument(prevInstrument);

                    if (isMidi || isSynth)
                    {
                        sendMidiNoteOff(player.track[tr].stepSent.note,
                                        0,
                                        instrumentIdxToMidiChannel(prevInstrument),
                                        isSynth);
                    }
                    else if (isSampleTrack(tr))
                    {
                        instrumentPlayer[tr].noteOff();
                    }
                }

                int newInstrument =
                  padInstrumentEdit ? source : mtProject.values.lastUsedInstrument;
                int inst =
                  changeInstrumentInSpecificOrder(newInstrument, 0, !isSampleTrack(tr));
                velocity =
                  mapClamped<int>(velocity, 0, 127, 0, getInstrumentVelo(inst));

                if (!player.preRecMetronomeActive || isAnticipatedBeat1)
                {
                    newFileManager.storePatternUndoRevision();
                    step->note = note;
                    step->instrument = inst;
                    wasChangeInPattern = true;

                    player.track[tr].recOpen = 1;
                    player.track[tr].recNoteOnStepIdx = destinationStep;

                    if (recMicrotiming())
                    {
                        step->micromove =
                          isAnticipatedBeat1
                            ? 0
                            : clamp<int>(mapClamped<int>(player.uStep, 1, 48, 0, 100),
                                         getFxMin(fx.FX_TYPE_MICROMOVE),
                                         getFxMax(fx.FX_TYPE_MICROMOVE));

                        wasChangeInPattern = true;
                    }

                    if (recVelocity())
                    {
                        step->volume = velocityToVolume(velocity);
                        wasChangeInPattern = true;
                    }
                }

                player.track[tr].stepSent.note = note;
                player.track[tr].stepSent.instrument = inst;

                if (doPreview)
                {
                    player.track[tr].noteOpen = 1;

                    bool isSynth = isSynthInstrument(inst);

                    if (isMidiInstrument(inst) || isSynth)
                    {
                        sendMidiNoteOn(note,
                                       velocity,
                                       instrumentIdxToMidiChannel(inst),
                                       isSynth);
                    }
                    else if (isSampleTrack(tr))
                    {
                        instrumentPlayer[tr].noteOff();
                        instrumentPlayer[tr].noteOn(inst,
                                                    note,
                                                    velocityToVolume(velocity),
                                                    0,
                                                    0,
                                                    0,
                                                    0);

                        engine.setLastUsedVoice(tr);
                    }
                }
                break;
            }
        }
    }
    else // czyli playMode / dogrywanie / wgrywanie akordów w edit
    {
        int instrument = mtProject.values.lastUsedInstrument;

        for (int tryIdx = 0; tryIdx < kAllTracks; tryIdx++)
        {
            int trackIdx = (sel->firstTrack + tryIdx) % kAllTracks;

            if (isSampleInstrument(instrument) && !isSampleTrack(trackIdx))
            {
                continue;
            }

            if (!isTrackEngineMuted(trackIdx) &&
                ((isEditMode() && !player.track[trackIdx].recOpen) ||
                 !player.track[trackIdx].noteOpen))
            {
                player.track[trackIdx].noteOpen = 1;
                player.track[trackIdx].noteLength = -1;
                player.track[trackIdx].recOpen = true;
                player.track[trackIdx].noteSource = source;

                player.track[trackIdx].stepSent.note = note;
                player.track[trackIdx].stepSent.instrument = instrument;

                velocity = mapClamped<int>(velocity, 0, 127, 0, getInstrumentVelo(instrument));
                bool isMidi = isMidiInstrument(instrument);
                bool isSynth = isSynthInstrument(instrument);

                if (isMidi || isSynth)
                {
                    sendMidiNoteOn(note, velocity, instrumentIdxToMidiChannel(instrument), isSynth);
                }
                else if (isSampleTrack(trackIdx))
                {
                    instrumentPlayer[trackIdx].noteOff();
                    instrumentPlayer[trackIdx].noteOn(mtProject.values.lastUsedInstrument,
                                                      note,
                                                      velocityToVolume(velocity),
                                                      0,
                                                      0,
                                                      0,
                                                      0);

                    engine.setLastUsedVoice(trackIdx);
                }

                if (isEditMode() && trackIdx >= sel->firstTrack && trackIdx <= sel->lastTrack &&
                    mtInterface.isPatternScreenActive() &&
                    mtPopups.getStepPopupState() == stepPopupNone)
                {
                    strPattern::strTrack::strStep* step =
                      &currentPattern->track[trackIdx].step[sel->firstStep];

                    newFileManager.storePatternUndoRevision();

                    step->note = note;
                    step->instrument = changeInstrumentInSpecificOrder(
                      mtProject.values.lastUsedInstrument, 0, !isSampleTrack(trackIdx));

                    wasChangeInPattern = true;

                    if (recVelocity())
                    {
                        step->volume = velocityToVolume(velocity);
                    }
                }
                break;
            }
        }
    }

    if (wasChangeInPattern)
    {
        newFileManager.setActualPatternStructChanged();
    }
}
void Sequencer::handleNoteOff(byte channel, // pochodzenie grida np. GRID_OUTSIDE_PATTERN itd
                              byte note,
                              [[maybe_unused]] byte velocity,
                              int16_t source) // nr pada, jesli midi to source = nuta+100,
{
    strSelection* sel = &selection;
    if (!isSelectionCorrect(sel))
        return;

    bool wasChangeInPattern = false;
    bool padInstrumentEdit = false;

    if (channel == GRID_INSIDE_PATTERN_INSTRUMENT)
    {
        padInstrumentEdit = true;
        channel = GRID_OUTSIDE_PATTERN;
    }

    if (isEditMode() && channel != GRID_OUTSIDE_PATTERN)
    {
        if (!isMultiSelection())
        {
            int instrument = player.track[sel->firstTrack].stepSent.instrument;
            bool isMidi = isMidiInstrument(instrument);
            bool isSynth = isSynthInstrument(instrument);

            if (isMidi || isSynth)
            {
                sendMidiNoteOff(player.track[sel->firstTrack].stepSent.note,
                                0,
                                instrumentIdxToMidiChannel(instrument),
                                isSynth);
            }
            else if (isSampleTrack(sel->firstTrack))
            {
                instrumentPlayer[sel->firstTrack].noteOff();
            }
        }
    }
    else if (isRec())
    {
        int8_t singleArmedTrack = getSingleArmedTrack();
        if (singleArmedTrack >= 0)
        {
            for (uint8_t i = 0; i < liveRecHeldNotesCount; i++)
            {
                if ((source >= 0 && liveRecHeldNotes[i].source == source) ||
                    (source < 0 && liveRecHeldNotes[i].note == note))
                {
                    for (uint8_t j = i; j < liveRecHeldNotesCount - 1; j++)
                    {
                        liveRecHeldNotes[j] = liveRecHeldNotes[j + 1];
                    }
                    liveRecHeldNotesCount--;
                    break;
                }
            }

            // Only write the OFF once the last pad is released
            if (liveRecHeldNotesCount == 0)
            {
                uint8_t tr = singleArmedTrack;
                bool shouldOff = player.track[tr].recOpen || player.track[tr].noteOpen;
                if (shouldOff)
                {
                    bool wasRecOpen = player.track[tr].recOpen;
                    player.track[tr].recOpen = 0;

                    bool doPreview = true;

                    if (!isPlay() && !player.preRecMetronomeActive)
                    {
                        doPreview = false;
                    }

                    if (doPreview)
                    {
                        int instrument = player.track[tr].stepSent.instrument;
                        bool isMidi = isMidiInstrument(instrument);
                        bool isSynth = isSynthInstrument(instrument);

                        if (isMidi || isSynth)
                        {
                            sendMidiNoteOff(player.track[tr].stepSent.note,
                                            0,
                                            instrumentIdxToMidiChannel(instrument),
                                            isSynth);
                        }
                        else if (isSampleTrack(tr))
                        {
                            instrumentPlayer[tr].noteOff();
                        }

                        player.track[tr].noteOpen = 0;
                    }

                    if (wasRecOpen && !padInstrumentEdit && !player.preRecMetronomeActive)
                    {
                        uint8_t destinationStep = getDestRecordingStep(tr, !recMicrotiming(), true);
                        strPattern::strTrack::strStep* step =
                          &currentPattern->track[tr].step[destinationStep];

                        if (step->isNoteEmpty())
                        {
                            wasChangeInPattern = true;

                            step->note = STEP_NOTE_OFF_DEFAULT;

                            if (recMicrotiming() && destinationStep == player.track[tr].actual_pos)
                            {
                                step->micromove =
                                  clamp<int>(mapClamped<int>(player.uStep, 1, 48, 0, 100),
                                             getFxMin(fx.FX_TYPE_MICROMOVE),
                                             getFxMax(fx.FX_TYPE_MICROMOVE));
                            }
                        }
                    }
                }
            }
        }
        else
        {
            liveRecHeldNotesCount = 0;
            for (uint8_t tr = 0; tr < kAllTracks; tr++)
            {
                uint8_t destinationStep = getDestRecordingStep(tr, !recMicrotiming(), true);
                bool shouldOff = (player.track[tr].recOpen || player.track[tr].noteOpen) &&
                                 player.track[tr].stepSent.note == note;

                if (shouldOff)
                {
                    bool wasRecOpen = player.track[tr].recOpen;
                    player.track[tr].recOpen = 0;

                    bool doPreview = true;

                    if (!isPlay() && !player.preRecMetronomeActive)
                    {
                        doPreview = false;
                    }

                    if (doPreview)
                    {
                        int instrument = player.track[tr].stepSent.instrument;
                        bool isMidi = isMidiInstrument(instrument);
                        bool isSynth = isSynthInstrument(instrument);

                        if (isMidi || isSynth)
                        {
                            sendMidiNoteOff(player.track[tr].stepSent.note,
                                            0,
                                            instrumentIdxToMidiChannel(instrument),
                                            isSynth);
                        }
                        else if (isSampleTrack(tr))
                        {
                            instrumentPlayer[tr].noteOff();
                        }

                        player.track[tr].noteOpen = 0;
                    }

                    if (padInstrumentEdit)
                    {
                        // jezeli nagrywamy instrumenty padami to nie dajemy NOTE OFF
                        continue;
                    }

                    if (wasRecOpen && !player.preRecMetronomeActive)
                    {
                        strPattern::strTrack::strStep* step =
                          &currentPattern->track[tr].step[destinationStep];

                        if (step->isNoteEmpty())
                        {
                            wasChangeInPattern = true;

                            step->note = STEP_NOTE_OFF_DEFAULT;

                            if (recMicrotiming() && destinationStep == player.track[tr].actual_pos)
                            {
                                step->micromove =
                                  clamp<int>(mapClamped<int>(player.uStep, 1, 48, 0, 100),
                                             getFxMin(fx.FX_TYPE_MICROMOVE),
                                             getFxMax(fx.FX_TYPE_MICROMOVE));
                            }
                        }
                    }
                    break;
                }
            }
        }
    }
    else // czyli playMode
    {
        for (int tryIdx = 0; tryIdx < kAllTracks; tryIdx++)
        {
            int trackIdx = (sel->firstTrack + tryIdx) % kAllTracks;
            int instrument = player.track[trackIdx].stepSent.instrument;

            if (isSampleInstrument(instrument) && !isSampleTrack(trackIdx))
            {
                continue;
            }

            if (player.track[trackIdx].noteOpen && player.track[trackIdx].recOpen &&
                player.track[trackIdx].noteSource == source)
            {
                bool isMidi = isMidiInstrument(instrument);
                bool isSynth = isSynthInstrument(instrument);

                if (isMidi || isSynth)
                {
                    sendMidiNoteOff(note, 0, instrumentIdxToMidiChannel(instrument), isSynth);
                }
                else if (isSampleTrack(trackIdx))
                {
                    instrumentPlayer[trackIdx].noteOff();
                }
                player.track[trackIdx].noteOpen = 0;
                player.track[trackIdx].recOpen = 0;
                player.track[trackIdx].noteSource = -1;
                break;
            }
        }
    }

    if (wasChangeInPattern)
    {
        newFileManager.setActualPatternStructChanged();
    }
}

void Sequencer::updateTrackLengths(strPattern* pattern)
{
    for (int trackIdx = 0; trackIdx < kAllTracks; trackIdx++)
    {
        pattern->track[trackIdx].length = pattern->track[0].length;
    }
}

// 1 jesli jest najmniej jedna otwarta nuta recOpen
uint8_t Sequencer::noMoreRecOpen()
{
    uint8_t noMore = 1;
    for (uint8_t a = 0; a < 8; a++)
    {
        if (player.track[a].recOpen)
        {
            noMore = 0;
            break;
        }
    }
    return noMore;
}

AUDIO_CORE_RAM void Sequencer::cancelFxes()
{
    player.tempoFx.reset();

    for (uint8_t a = 0; a < kAllTracks; a++)
    {
        player.track[a].roll.reset();
        player.track[a].arp.reset();
    }
}
void Sequencer::cancelFxes(int8_t track)
{
    player.track[track].roll.reset();
}

AUDIO_CORE_RAM void Sequencer::killFxOnSlot(uint8_t row, uint8_t slot)
{

    if (player.track[row].roll.fxSlot == slot)
    {
        player.track[row].roll.reset();
        player.track[row].arp.reset();
    }
}

void Sequencer::enterPerformanceMode()
{
    player.performanceMode = 1;

    for (uint8_t a = 0; a < kAllTracks; a++)
    {
        player.performance.trackLength[a] = getActualPattern()->track[0].length;
    }

    if (player.onPatternOrSelectionEnd == cbkTurnOffPerformanceMode)
    {
        player.onPatternOrSelectionEnd = nullCallback;
    }
}

void Sequencer::setPerformanceMidiTune(uint8_t tr, int8_t value)
{
    assert(tr < kAllTracks);

    if (abs(value) > 48)
        return;

    player.track[tr].tuneMidi.isActive = value != 0;
    player.track[tr].tuneMidi.tune = value;
}

void Sequencer::setPerformanceMidiVolume(int track, int value)
{
    assert(track <= kAllTracks);
    assert(value >= strPlayer::strPlayerTrack::strPerformanceMidiVolumeFx::minValue &&
           value <= strPlayer::strPlayerTrack::strPerformanceMidiVolumeFx::maxValue);

    player.track[track].performanceMidiVolumeFx.value = value;
}

void Sequencer::setPerformancePatternLength(int length)
{
    player.performance.patternLength = length - 1;
}
void Sequencer::setPerformancePatternLengthFromFxVal(int8_t val)
{
    int16_t performancePatternLengthValues[] = { -1, 1, 2, 4, 8, 16, 32, 64, 128 };
    const int lastValue =
      sizeof(performancePatternLengthValues) / sizeof(performancePatternLengthValues[0]) - 1;

    // Stored performance values are loaded without the range of their effect.
    setPerformancePatternLength(performancePatternLengthValues[clamp<int>(val, 0, lastValue)]);
}
// laduje track do bufora i czeka na switch
void Sequencer::setTrackToLoadOnSwitch(uint8_t track, uint8_t sourcePattern)
{
    if (!player.performanceMode)
    {
        enterPerformanceMode();
    }

    newFileManager.loadPerformanceTrackToBuffer(sourcePattern, track);

    player.track[track].performanceSourcePattern = sourcePattern;
}
// laduje track do bufora i przelacza od razu
void Sequencer::setTrackToLoadNow(uint8_t track, uint8_t sourcePattern)
{
    if (!player.performanceMode)
    {
        enterPerformanceMode();
    }

    newFileManager.loadPerformanceTrackToBuffer(sourcePattern, track);

    switchPerformanceTrackNow(track);
}

void Sequencer::setPerformanceTrackLength(uint8_t track, uint8_t length)
{
    assert(track < kAllTracks);

    if (length > MAXSTEP)
        return;

    player.performance.trackLength[track] = length;
}

AUDIO_CORE_RAM uint8_t Sequencer::getRollType(uint8_t value)
{
    return (value / (fx.ROLL_PERIOD_MAX + 1));
}

AUDIO_CORE_RAM uint8_t Sequencer::rollValToVolumeOption(uint8_t value)
{
    switch (getRollType(value))
    {
        case fx.rollType_volUp: return rollVolumeOption_up;
        case fx.rollType_volDown: return rollVolumeOption_down;
        default: return rollVolumeOption_const;
    }
}
AUDIO_CORE_RAM uint8_t
Sequencer::getRollVelo(uint8_t row, const strPlayer::strPlayerTrack::strSendStep* const sendStep)
{
    strPlayer::strPlayerTrack& playerRow = player.track[row];
    int8_t volMax = 1;
    if (playerRow.roll.startingVolume < 0)
    {
        volMax = getInstrumentVolume(sendStep->instrument);
    }
    else
    {
        volMax = playerRow.roll.startingVolume;
    }

    float progress = ((float)playerRow.stepTimer / (playerRow.roll.length * 48));

    if (progress > 1.0)
        progress = 1.0;

    switch (playerRow.roll.volumeOption)
    {
        case rollVolumeOption_up: return volMax * progress;
        case rollVolumeOption_down: return volMax * (1.0f - progress);
        default: break;
    }

    return 127;
}

uint8_t Sequencer::getInstrumentVelo(uint8_t instrumentIdx)
{
    if (isMidiInstrument(instrumentIdx))
    {
        int midiInstrumentIdx = instrumentIdxToMidiChannel(instrumentIdx) - 1;
        return mtProject.values.midiInstrument[midiInstrumentIdx].velocity;
    }
    else if (isSynthInstrument(instrumentIdx))
    {
        // todo synth get volume
        return 100;
    }
    else if (isSampleInstrument(instrumentIdx))
    {
        return volumeToVelocity(mtProject.instrument[instrumentIdx].volume);
    }
    return 0;
}
// todo synth: reduce to one function
uint8_t Sequencer::getInstrumentVolume(uint8_t instrumentIdx)
{
    if (isMidiInstrument(instrumentIdx))
    {
        int midiInstrumentIdx = instrumentIdxToMidiChannel(instrumentIdx) - 1;
        return velocityToVolume(mtProject.values.midiInstrument[midiInstrumentIdx].velocity);
    }
    else if (isSynthInstrument(instrumentIdx))
    {
        // todo synth get volume
        return 100;
    }
    else if (isSampleInstrument(instrumentIdx))
    {
        return mtProject.instrument[instrumentIdx].volume;
    }
    return 0;
}

AUDIO_CORE_RAM uint8_t Sequencer::getInstrumentMidiOffset(uint8_t instrument)
{
    if (isMidiInstrument(instrument))
    {
        int midiInstrumentIdx = instrumentIdxToMidiChannel(instrument) - 1;
        assert(midiInstrumentIdx >= 0);
        return mtProject.values.midiInstrument[midiInstrumentIdx].offset;
    }

    return 0;
}

void Sequencer::switchRamPatternsNow()
{
    cancelFxes();
    std::swap(currentPattern, bufferPattern);

    int bufferPatternIdx = newFileManager.getLoadedPatternIndex();
    assert(bufferPatternIdx >= 0);
    // The buffer pattern now holds what was playing.
    newFileManager.forgetPreloadedPattern();
    mtProject.values.actualPattern = bufferPatternIdx;

    if (bufferPatternIdx != player.activePatternIdx)
    {
        player.activePatternIdx = bufferPatternIdx;
        for (int row = 0; row < kAllTracks; row++)
        {
            player.track[row].passCount = 0;
        }
    }

    int16_t current_song_pos = (int16_t)mtProject.song.playlistPos;
    int16_t newSlotMaybe = findSlotWithPattern();
    if (newSlotMaybe != -1 && current_song_pos != newSlotMaybe)
    {
        mtProject.song.playlistPos = (uint8_t)newSlotMaybe;
    }
}

int16_t Sequencer::findSlotWithPattern()
{
    uint8_t slot_idx = 0;
    uint8_t slot_value = 0;
    uint8_t current_pattern = mtProject.values.actualPattern;
    uint8_t current_song_pos = mtProject.song.playlistPos;

    // try the last used playlist position first
    for (slot_idx = current_song_pos; slot_idx < SONG_MAX; slot_idx++)
    {
        slot_value = mtProject.song.playlist[slot_idx];
        if (slot_value == current_pattern)
        {
            return slot_idx;
        }
        else if (slot_value == 0)
        {
            break; // pattern 0 == end of song
        }
    }

    for (slot_idx = 0; slot_idx < SONG_MAX; slot_idx++)
    {
        slot_value = mtProject.song.playlist[slot_idx];
        if (slot_value == current_pattern)
        {
            return slot_idx;
        }
        else if (slot_idx >= current_song_pos || slot_value == 0) // pattern 0 == end of song
        {
            break;
        }
    }

    return -1;
}

extern Sequencer::strPattern fileManagerPerformancePatternBuffer;

// podmienia zaladowane w performance tracki
void Sequencer::switchPerformanceTrackNow(uint8_t trackToSwitch)
{

    Sequencer::strPattern* patternTo = currentPattern;
    Sequencer::strPattern* patternFrom = &fileManagerPerformancePatternBuffer;

    patternTo->track[trackToSwitch] = patternFrom->track[trackToSwitch];
    uint8_t track_len = patternFrom->track[trackToSwitch].length;

    setPerformanceTrackLength(trackToSwitch, track_len);
    player.track[trackToSwitch].performanceSourcePattern = -1;
    player.track[trackToSwitch].passCount = 0;

    if (player.track[trackToSwitch].actual_pos_absolute > track_len)
    {
        uint8_t delta = player.globalPos % (track_len + 1);
        player.track[trackToSwitch].actual_pos_absolute = delta;
        reset_actual_pos(trackToSwitch, delta);
    }
}

AUDIO_CORE_RAM uint8_t Sequencer::isMetronomeActive()
{
    return mtConfig.metronome.state > 0;
}
uint8_t Sequencer::isPreRollConfigOn()
{
    return mtConfig.metronome.preRoll == 1;
}
AUDIO_CORE_RAM uint8_t Sequencer::getMetronomeNumerator()
{
    return mtConfig.metronome.timeSignatureNumerator + 1;
}
AUDIO_CORE_RAM uint8_t Sequencer::getMetronomeDenominator()
{
    return mtConfig.metronome.timeSignatureDenominator + 1;
}

void Sequencer::setMidiInVoiceMode(enMidiInVoiceMode mode)
{
    player.midiInVoiceMode = mode;
}

uint8_t Sequencer::getMidiInVoiceMode()
{
    return player.midiInVoiceMode;
}

// zmiania wartosc sequencialSwitch, potrzebuje uzbrojenia setReady()
void Sequencer::sequencialSwitch_changeNextPattern(int8_t delta)
{
    if (player.sequencialSwitch_pattern == 0)
    {
        player.sequencialSwitch_pattern = mtProject.values.actualPattern;
    }

    assert(player.sequencialSwitch_pattern > 0);
    player.sequencialSwitch_pattern =
      clamp<int>(player.sequencialSwitch_pattern + delta, PATTERN_INDEX_MIN, PATTERN_INDEX_MAX);
}

// uzbraja sequencialSwitch
void Sequencer::sequencialSwitch_SetReady()
{
    sequencer.setOnGlobalStep(cbkSequencialSwitch_SetReady);
}

void Sequencer::sequencialSwitch_SetReadyNow()
{
    player.sequencialSwitch_isArmed = true;
}

void cbkSequencialSwitch_SetReady()
{
    if (newFileManager.isPatternLoading() || sequencer.isLastPatternStep())
    {
        return; // retry in one step (because we're not unsetting)
    }

    sequencer.sequencialSwitch_SetReadyNow();
    sequencer.setOnGlobalStep(nullCallback);
}

// anuluje sequencialSwitch
AUDIO_CORE_RAM void Sequencer::sequencialSwitch_Reset()
{
    player.sequencialSwitch_pattern = 0;
    player.sequencialSwitch_isArmed = 0;
    player.sequencialSwitch_loaded = 0;
}

// getuje nastepny pattern, jesli 0 to nie ma zmiany
uint16_t Sequencer::sequencialSwitch_GetNext()
{
    return player.sequencialSwitch_pattern;
}

void Sequencer::setExternalClockComp(uint8_t configValue)
{
    externalClockCompensation_clockSignals = mapClamped((int)configValue,
                                                        0,
                                                        kExternalClockCompMaxVal,
                                                        -kExternalClockCompMaxVal / 2,
                                                        kExternalClockCompMaxVal / 2);
}

uint8_t Sequencer::isRecArmedOnTrack(uint8_t track)
{
    assert(track < kAllTracks);
    return player.track[track].isRecordingArmed;
}
void Sequencer::toogleRecArmedOnTrack(uint8_t track)
{
    assert(track < kAllTracks);
    player.track[track].isRecordingArmed = !player.track[track].isRecordingArmed;
    liveRecHeldNotesCount = 0;
}

int8_t Sequencer::getSingleArmedTrack()
{
    int8_t armedTrack = -1;
    for (int tr = 0; tr < kAllTracks; tr++)
    {
        if (player.track[tr].isRecordingArmed)
        {
            if (armedTrack != -1)
            {
                return -1;
            }
            armedTrack = tr;
        }
    }
    return armedTrack;
}

bool Sequencer::isPlayingSelection(void)
{
    return player.isPlay && player.selectionMode;
}

AUDIO_CORE_RAM bool Sequencer::isPlayingSelectionLooped(void)
{
    return player.isPlay && player.selectionMode && player.selectionModeLooped;
}

bool Sequencer::isLoopSelectionSingleRow(void)
{
    return player.loopSelection.firstStep == player.loopSelection.lastStep;
}

uint8_t Sequencer::isRenderMode(void)
{
    return renderMode;
}
/*
 *
 *
 *
 *
 *
 */

void Sequencer::handleMidiInSongPosition(uint16_t beats)
{
    pendingSongPositionBeats = beats;
    hasPendingSongPosition = true;
}

void Sequencer::handleMidiInStart(bool continuePlay, bool songMode)
{
    // Clock 0 (the first 0xF8 right after Start/Continue) immediately plays step uStep 1..8
    externalClockPulses = 0;
    externalClocksToPlay = 0;
    externalClockEventTime = 0;
    externalClockIntervalValid = false;
    externalTempo = lastKnownExternalTempo;

    clockSignalsToCompensate = externalClockCompensation_clockSignals;

    uint8_t startStep = 0;
    if (continuePlay)
    {
        if (hasPendingSongPosition)
        {
            uint8_t patLen = currentPattern->track[0].length + 1;
            startStep = patLen > 0 ? static_cast<uint8_t>(pendingSongPositionBeats % patLen) : 0;
            hasPendingSongPosition = false;
        }
        else
        {
            startStep = lastStoppedStepPos;
        }
    }
    else
    {
        lastStoppedStepPos = 0;
        hasPendingSongPosition = false;
    }

    if (songMode)
    {
        uint8_t currentPattern = mtProject.values.actualPattern;

        auto songPosToStart = newFileManager.getPatternPosInSong(currentPattern);
        if (songPosToStart.has_value())
        {
            // Not playSongFromPosition(): it saves the pattern to the SD card first, which
            // the audio callback must not do. The slot holds the pattern that is loaded,
            // so nothing is replaced yet; the pattern is saved before the song moves on,
            // when the next one is loaded.
            newFileManager.setSongPos(songPosToStart.value());
            player.songMode = 1;
            play();
            if (startStep > 0)
            {
                player.globalPos = startStep;
                for (int trackIdx = 0; trackIdx < kAllTracks; trackIdx++)
                {
                    player.track[trackIdx].actual_pos = startStep;
                    player.track[trackIdx].actual_pos_absolute = startStep;
                }
            }
        }
        else
        {
            mtPopups.show(0, "Load pattern from song first");
        }
    }
    else
    {
        playPattern(startStep);
    }
}

void Sequencer::handleMidiInStop()
{
    if (player.isPlay)
    {
        if (!isSetInternalClock() && !player.selectionMode)
        {
            midiSetClockSubBlockOffsetUs(0U);
            static const int nanoTicksInClockPulse =
              kStepsPerBeat * kuStepsInStep * kNanoStepsInUStep / kExternalClockPpqn;

            if (externalClockPulses > 0)
            {
                externalClocksToPlay += externalClockPulses + clockSignalsToCompensate;
                clockSignalsToCompensate = 0;
                externalClockPulses = 0;
            }

            while (externalClocksToPlay > 0)
            {
                for (int tick = 0; tick < nanoTicksInClockPulse; tick++)
                {
                    tick_uStep();
                }
                externalClocksToPlay--;
            }
        }
        lastStoppedStepPos = player.track[0].actual_pos;
    }
    externalClockPulses = 0;
    externalClocksToPlay = 0;
    stop(StopContext::audioCallback);
}

void Sequencer::handleMidiInClock()
{
    externalClockPulses++;
    timeWithoutClockPulse = 0;
}

AUDIO_CORE_RAM void Sequencer::updateExternalClockTempo(seqtimesec_t deltaTime)
{
    static constexpr seqtimesec_t kMaxTimeWithoutClockPulse = 1;

    externalClockEventTime += deltaTime;
    if (externalClockPulses > 0)
    {
        timeWithoutClockPulse = 0;

        int pulsesToRecord = externalClockPulses;
        if (!externalClockIntervalValid)
        {
            externalClockIntervalValid = true;
            pulsesToRecord = externalClockPulses - 1;
        }

        if (pulsesToRecord > 0)
        {
            seqtimesec_t intervalPerPulse =
              externalClockEventTime / static_cast<seqtimesec_t>(externalClockPulses);
            for (int p = 0; p < pulsesToRecord; p++)
            {
                std::copy(&externalClockEvents[1],
                          &externalClockEvents[kExternalTempoCounterSize],
                          &externalClockEvents[0]);
                externalClockEvents[kExternalTempoCounterSize - 1] = intervalPerPulse;
                if (externalClockEventsCount < kExternalTempoCounterSize)
                {
                    externalClockEventsCount++;
                }
            }
        }

        externalClockEventTime = 0;

        if (externalClockEventsCount > 0)
        {
            size_t startIdx = kExternalTempoCounterSize - externalClockEventsCount;
            seqtimesec_t totalTime = std::accumulate(
              &externalClockEvents[startIdx], &externalClockEvents[kExternalTempoCounterSize], 0.0f);
            if (totalTime > 0.0f)
            {
                externalTempo =
                  (kMinute * static_cast<float>(externalClockEventsCount) / kExternalClockPpqn) /
                  totalTime;
                externalTempo = clamp(externalTempo, kMinExternalBPM, kMaxExternalBPM);
                lastKnownExternalTempo = externalTempo;
            }
        }
    }
    else
    {
        timeWithoutClockPulse += deltaTime;
        if (timeWithoutClockPulse > kMaxTimeWithoutClockPulse)
        {
            externalTempo = kUnknownExternalTempo;
            externalClockIntervalValid = false;
            externalClockEventsCount = 0;
        }
    }
}

bool Sequencer::copyBufferToBufferPattern(uint8_t* srcBuff)
{
    uint8_t* destPattern = (uint8_t*)bufferPattern;

    memcpy(destPattern, srcBuff, sizeof(Sequencer::strPattern));
    newFileManager.checkVersionAndTranslate((Sequencer::strPattern*)destPattern);
    return true;
}

bool Sequencer::copyCurrentPatternToBuff(uint8_t* destBuff)
{
    uint8_t* srcPattern = (uint8_t*)currentPattern;

    memcpy(destBuff, srcPattern, sizeof(Sequencer::strPattern));

    sequencer.setPatternHeader((Sequencer::strPattern*)destBuff);

    ((Sequencer::strPattern*)destBuff)->crc =
      crcCalculator->calculate32bitCrc(destBuff, sizeof(Sequencer::strPattern) - sizeof(uint32_t));

    return true;
}

void Sequencer::setCrcCalculator(CrcCalculator* crcCalculator)
{
    this->crcCalculator = crcCalculator;
}

void Sequencer::setSynthMute(MuteState muteState)
{
    this->synthsMuteState = muteState;
}

AUDIO_CORE_RAM bool isPerformanceTrack(int track)
{
    return track < kPerformanceTracks;
}

AUDIO_CORE_RAM bool isSampleTrack(int track)
{
    assert(track >= 0);
    return (track < kSampleTracks);
}

AUDIO_CORE_RAM bool isSampleInstrument(int instrument)
{
    assert(instrument >= 0);
    assert(instrument < kTotalInstrumentsCount);
    return instrument < kSampleInstrumentsCount;
}

AUDIO_CORE_RAM bool isMidiInstrument(int instrument)
{
    assert(instrument >= 0);
    assert(instrument < kTotalInstrumentsCount);
    return instrument >= kSampleInstrumentsCount &&
           instrument < kSampleInstrumentsCount + kMidiInstrumentsCount;
}

AUDIO_CORE_RAM bool isSynthInstrument(int instrument)
{
    assert(instrument >= 0);
    assert(instrument < kTotalInstrumentsCount);
    return instrument >= kSampleInstrumentsCount + kMidiInstrumentsCount &&
           instrument < kSampleInstrumentsCount + kMidiInstrumentsCount + kSynthInstrumentsCount;
}

AUDIO_CORE_RAM int instrumentIdxToMidiChannel(int instrument)
{
    assert(instrument >= 0);
    if (isSampleInstrument(instrument))
    {
        return kNoChannel;
    }
    else if (isMidiInstrument(instrument))
    {
        return instrument - kSampleInstrumentsCount + 1;
    }
    else if (isSynthInstrument(instrument))
    {
        return instrument - kSampleInstrumentsCount - kMidiInstrumentsCount + 1;
    }

    // instrument out of range
    assert(false);
    return kNoChannel;
}

int instrumentIdxToSynthIdx(int instrumentIdx)
{
    assert(isSynthInstrument(instrumentIdx));
    return instrumentIdxToMidiChannel(instrumentIdx) - 1;
}
