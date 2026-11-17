#include "ampLogarythmicValues.h"
#include "mtAudioEngine.h"
#include "mtWavetableEnvelopes.h"
#include "mtSequencer.h"
#include "tempoSyncRateValues.h"

using namespace mtAudioEngineConstans;

// Najpierw przepisywane są wartosci seqa nawet gdy aktywny jest performanceMode gdyz i tak trzeba
// zaktualizowac performanceMode bo bedzie wyliczany na podstawie nowych zmienionych wartosci w sequ
AUDIO_CORE_RAM void playerEngine::seqFx(uint8_t fx_id, uint8_t fx_val, uint8_t fx_n)
{
    if (isBlockedNoteOn[currentInstrument_idx])
        return;

    AudioNoInterrupts();
    //	__disable_irq();

    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setCurrentInstrIdx(
          currentInstrument_idx); // play mem dopiero aktualizuje index na play, a czasem korzysta
                                  // sie wczesniej z funkcji
    }
    const uint8_t prevFx = lastSeqFx[fx_n];
    const bool isPrevFilter =
      (prevFx == Sequencer::strFxConsts::FX_TYPE_FILTER_BANDPASS ||
       prevFx == Sequencer::strFxConsts::FX_TYPE_FILTER_HIGHPASS ||
       prevFx == Sequencer::strFxConsts::FX_TYPE_FILTER_LOWPASS);
    const bool isNewFilter =
      (fx_id == Sequencer::strFxConsts::FX_TYPE_FILTER_BANDPASS ||
       fx_id == Sequencer::strFxConsts::FX_TYPE_FILTER_HIGHPASS ||
       fx_id == Sequencer::strFxConsts::FX_TYPE_FILTER_LOWPASS);
    const bool skipEndFx =
      (isPrevFilter && isNewFilter) ||
      (prevFx != 0 && prevFx == fx_id &&
       (fx_id == Sequencer::strFxConsts::FX_TYPE_GLIDE ||
        fx_id == Sequencer::strFxConsts::FX_TYPE_MICROTUNING ||
        fx_id == Sequencer::strFxConsts::FX_TYPE_PANNING ||
        fx_id == Sequencer::strFxConsts::FX_TYPE_BIT_DEPTH ||
        fx_id == Sequencer::strFxConsts::FX_TYPE_DELAY_SEND ||
        fx_id == Sequencer::strFxConsts::FX_TYPE_REVERB_SEND ||
        fx_id == Sequencer::strFxConsts::FX_TYPE_REVERSE_PLAYBACK ||
        fx_id == Sequencer::strFxConsts::FX_TYPE_OVERDRIVE ||
        fx_id == Sequencer::strFxConsts::FX_TYPE_TUNE ||
        fx_id == Sequencer::strFxConsts::FX_TYPE_GRANULAR_LENGTH ||
        fx_id == Sequencer::strFxConsts::FX_TYPE_VOLUME));
    if (!skipEndFx)
    {
        endFx(prevFx, fx_n);
    }
    switch (fx_id)
    {
        case 0: break; // na 0 mial wywolywac endFx ale wywoluje go zawsze i tak
        case Sequencer::strFxConsts::FX_TYPE_FILTER_BANDPASS:
            fxFilter(fx_val, fx_n, bandPass);
            break;
        case Sequencer::strFxConsts::FX_TYPE_FILTER_HIGHPASS:
            fxFilter(fx_val, fx_n, highPass);
            break;
        case Sequencer::strFxConsts::FX_TYPE_FILTER_LOWPASS: fxFilter(fx_val, fx_n, lowPass); break;
        case Sequencer::strFxConsts::FX_TYPE_GLIDE: fxGlide(fx_val, fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_MICROTUNING: fxFinetune(fx_val, fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_PANNING: fxPanning(fx_val, fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_BIT_DEPTH: fxBitDepth(fx_val, fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_DELAY_SEND: fxDelaySend(fx_val, fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_REVERB_SEND: fxReverbSend(fx_val, fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_REVERSE_PLAYBACK:
            fxReversePlayback(fx_val, fx_n);
            break;
        case Sequencer::strFxConsts::FX_TYPE_OVERDRIVE: fxOverdrive(fx_val, fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_TUNE: fxTune(fx_val, fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_GRANULAR_LENGTH: fxGranularLength(fx_val, fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_R7: break;
        case Sequencer::strFxConsts::FX_TYPE_R8: break;
        case Sequencer::strFxConsts::FX_TYPE_R9: break;
        case Sequencer::strFxConsts::FX_TYPE_R30: fxAmpEnv(fx_val, fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_R31: fxFilterEnv(fx_val, fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_POSITION: fxPosition(fx_val, fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_VOLUME: fxVolume(fx_val, fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_RANDOM_VOLUME: fxRandomVolume(fx_val, fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_SAMPLE_SLICE: fxSampleSlice(fx_val, fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_VOLUME_LFO: fxVolumeLFO(fx_val, fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_FILTER_LFO: fxCutoffLFO(fx_val, fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_POSITION_LFO: fxPositionLFO(fx_val, fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_PANNING_LFO: fxPanningLFO(fx_val, fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_FINETUNE_LFO: fxFinetuneLFO(fx_val, fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_SLIDE_UP: fxSlideUp(fx_val); break;
        case Sequencer::strFxConsts::FX_TYPE_SLIDE_DOWN: fxSlideDown(fx_val); break;
        default: break;
    }

    lastSeqFx[fx_n] = fx_id;
    lastSeqVal[fx_n] = fx_val;

    AudioInterrupts();
    //	__enable_irq();
}
//*** Jeżeli kończy się mniej znaczący efekt mimo, że aktywny jest bardziej znaczący efekt to nie
// wykonujemy żadnej akcji poza wyzerowaniem flag ***/
//*** Jeżeli kończy się bardziej znaczący efekt i aktywny jest mniej znaczący efekt to mniej
// znaczący efekt przejmuje kontrolę lub performanceMode wykonuje
// swoje operacje na podstawie wartości mniej znaczącego efektu ***/
AUDIO_CORE_RAM void playerEngine::endFx(uint8_t fx_id, uint8_t fx_n)
{
    if (isFxVelocity(fx_id))
        fx_id = Sequencer::strFxConsts::FX_TYPE_VOLUME;

    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setCurrentInstrIdx(
          currentInstrument_idx); // play mem dopiero aktualizuje index na play, a czasem korzysta
                                  // sie wczesniej z funkcji
    }

    switch (fx_id)
    {
        case 0: break;
        case Sequencer::strFxConsts::FX_TYPE_FILTER_BANDPASS: endFxFilter(fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_FILTER_HIGHPASS: endFxFilter(fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_FILTER_LOWPASS: endFxFilter(fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_GLIDE: endFxGlide(fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_MICROTUNING: endFxFinetune(fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_PANNING: endFxPanning(fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_BIT_DEPTH: endFxBitDepth(fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_DELAY_SEND: endFxDelaySend(fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_REVERB_SEND: endFxReverbSend(fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_REVERSE_PLAYBACK: endFxReversePlayback(fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_OVERDRIVE: endFxOverdrive(fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_TUNE: endFxTune(fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_GRANULAR_LENGTH: endFxGranularLength(fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_R7: break;
        case Sequencer::strFxConsts::FX_TYPE_R8: break;
        case Sequencer::strFxConsts::FX_TYPE_R9: break;
        case Sequencer::strFxConsts::FX_TYPE_R30: endFxAmpEnv(fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_R31: endFxFilterEnv(fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_POSITION: endFxPosition(fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_RANDOM_VOLUME:
            endFxRandomVolume(fx_n);
            break; // dla porządku - wyzej i tak go nadpisze na zwykle velocity
        case Sequencer::strFxConsts::FX_TYPE_VOLUME: endFxVolume(fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_SAMPLE_SLICE: endFxSlice(fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_VOLUME_LFO: endFxVolumeLFO(fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_FILTER_LFO: endFxCutoffLFO(fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_POSITION_LFO: endFxPositionLFO(fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_PANNING_LFO: endFxPanningLFO(fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_FINETUNE_LFO: endFxFinetuneLFO(fx_n); break;
        case Sequencer::strFxConsts::FX_TYPE_SLIDE_UP: break;
        case Sequencer::strFxConsts::FX_TYPE_SLIDE_DOWN: break;
        default: break;
    }
}

//************************************SEQ FX HANDLE

void playerEngine::fxFilter(uint8_t fx_val, uint8_t fx_n, uint8_t type)
{
    uint8_t otherFx_n = !fx_n;
    // moglbym offsetowac od najmniejszego Sequencer::strFxConsts + type ale zakladam ze predzej cos
    // znajdzie sie pomiedzy typami niz beda dla roznych filtrow rozne zakresy stąd
    // Sequencer::strFxConsts::FX_TYPE_FILTER_BANDPASS
    uint8_t maxFilterFx = sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_FILTER_BANDPASS);
    uint8_t minFilterFx = sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_FILTER_BANDPASS);

    trackControlParameter[(int)controlType::sequencerMode + fx_n]
                         [(int)parameterList::filterCutoff] = 1;
    trackControlParameter[(int)controlType::sequencerMode + fx_n]
                         [(int)parameterList::filterEnable] = 1;
    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::filterType] =
      1;
    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        currentSeqModValues.filterCutoff =
          (fx_val - minFilterFx) / (float)(maxFilterFx - minFilterFx);
        currentSeqModValues.filterType = type;
        currentSeqModValues.filterEnable = 1;
    }
    else if (fx_n == LEAST_SIGNIFICANT_FX)
    {
        if (!trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                  [(int)parameterList::filterCutoff] &&
            !trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                  [(int)parameterList::filterEnable] &&
            !trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                  [(int)parameterList::filterType])
        {
            currentSeqModValues.filterCutoff =
              (fx_val - minFilterFx) / (float)(maxFilterFx - minFilterFx);
            currentSeqModValues.filterType = type;
            currentSeqModValues.filterEnable = 1;
        }
    }

    setFxCutoff();
    setFxFilterType();
}

void playerEngine::fxGlide(uint8_t fx_val, uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    uint8_t maxFxGlide = sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_GLIDE);
    uint8_t minFxGlide = sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_GLIDE);
    uint8_t halfFxGlide = maxFxGlide / 2;

    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        currentSeqModValues.glide =
          (fx_val > halfFxGlide)
            ? mapClamped<int>(fx_val, halfFxGlide, maxFxGlide, 1000, GLIDE_MAX)
            : mapClamped<int>(fx_val, minFxGlide, halfFxGlide, GLIDE_MIN, 1000);
    }
    else if (fx_n == LEAST_SIGNIFICANT_FX)
    {
        if (!trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                  [(int)parameterList::glide])
        {
            currentSeqModValues.glide =
              (fx_val > halfFxGlide)
                ? mapClamped<int>(fx_val, halfFxGlide, maxFxGlide, 1000, GLIDE_MAX)
                : mapClamped<int>(fx_val, minFxGlide, halfFxGlide, GLIDE_MIN, 1000);
        }
    }
    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::glide] = 1;

    setFxGlide();
}

void playerEngine::fxFinetune(uint8_t fx_val, uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;
    uint8_t maxFxFinetune = sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_MICROTUNING);
    uint8_t minFxFinetune = sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_MICROTUNING);

    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        currentSeqModValues.fineTune = mapClamped<int>(
          fx_val, minFxFinetune, maxFxFinetune, MIN_INSTRUMENT_FINETUNE, MAX_INSTRUMENT_FINETUNE);
    }
    else if (fx_n == LEAST_SIGNIFICANT_FX)
    {
        if (!trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                  [(int)parameterList::fineTune])
        {
            currentSeqModValues.fineTune = mapClamped<int>(fx_val,
                                                           minFxFinetune,
                                                           maxFxFinetune,
                                                           MIN_INSTRUMENT_FINETUNE,
                                                           MAX_INSTRUMENT_FINETUNE);
        }
    }
    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::fineTune] = 1;

    setFxFinetune();
}

void playerEngine::fxPanning(uint8_t fx_val, uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    uint8_t maxFxPanning = sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_PANNING);
    uint8_t minFxPanning = sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_PANNING);

    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        currentSeqModValues.panning =
          mapClamped<int>(fx_val, minFxPanning, maxFxPanning, PANNING_MIN, PANNING_MAX);
    }
    else if (fx_n == LEAST_SIGNIFICANT_FX)
    {
        if (!trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                  [(int)parameterList::panning])
        {
            currentSeqModValues.panning =
              mapClamped<int>(fx_val, minFxPanning, maxFxPanning, PANNING_MIN, PANNING_MAX);
        }
    }

    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::panning] = 1;
    setRefreshBit(PANNING_MASK);
}

void playerEngine::fxBitDepth(uint8_t fx_val, uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    uint8_t maxFxBitDepth = sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_BIT_DEPTH);
    uint8_t minFxBitDepth = sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_BIT_DEPTH);

    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        currentSeqModValues.bitDepth =
          mapClamped<int>(fx_val, minFxBitDepth, maxFxBitDepth, BIT_DEPTH_MIN, BIT_DEPTH_MAX);
    }
    else if (fx_n == LEAST_SIGNIFICANT_FX)
    {
        if (!trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                  [(int)parameterList::bitDepth])
        {
            currentSeqModValues.bitDepth =
              mapClamped<int>(fx_val, minFxBitDepth, maxFxBitDepth, BIT_DEPTH_MIN, BIT_DEPTH_MAX);
        }
    }
    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::bitDepth] = 1;

    setFxBitDepth();
}

void playerEngine::fxDelaySend(uint8_t fx_val, uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    uint8_t maxFxDelaySend = sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_DELAY_SEND);
    uint8_t minFxDelaySend = sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_DELAY_SEND);

    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        currentSeqModValues.delaySend =
          mapClamped<int>(fx_val, minFxDelaySend, maxFxDelaySend, SEND_MIN, SEND_MAX);
    }
    else if (fx_n == LEAST_SIGNIFICANT_FX)
    {
        if (!trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                  [(int)parameterList::delaySend])
        {
            currentSeqModValues.delaySend =
              mapClamped<int>(fx_val, minFxDelaySend, maxFxDelaySend, SEND_MIN, SEND_MAX);
        }
    }
    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::delaySend] =
      1;

    setRefreshBit(DELAY_SEND_MASK);
}

void playerEngine::fxReverbSend(uint8_t fx_val, uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    uint8_t maxFxReverbSend = sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_REVERB_SEND);
    uint8_t minFxReverbSend = sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_REVERB_SEND);

    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        currentSeqModValues.reverbSend =
          mapClamped<int>(fx_val, minFxReverbSend, maxFxReverbSend, SEND_MIN, SEND_MAX);
    }
    else if (fx_n == LEAST_SIGNIFICANT_FX)
    {
        if (!trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                  [(int)parameterList::reverbSend])
        {
            currentSeqModValues.reverbSend =
              mapClamped<int>(fx_val, minFxReverbSend, maxFxReverbSend, SEND_MIN, SEND_MAX);
        }
    }
    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::reverbSend] =
      1;

    setRefreshBit(REVERB_SEND_MASK);
}

void playerEngine::fxOverdrive(uint8_t fx_val, uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    uint8_t maxFxOverdrive = sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_OVERDRIVE);
    uint8_t minFxOverdrive = sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_OVERDRIVE);

    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        currentSeqModValues.overdrive =
          mapClamped<int>(fx_val, minFxOverdrive, maxFxOverdrive, SEND_MIN, SEND_MAX);
    }
    else if (fx_n == LEAST_SIGNIFICANT_FX)
    {
        if (!trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                  [(int)parameterList::overdrive])
        {
            currentSeqModValues.overdrive =
              mapClamped<int>(fx_val, minFxOverdrive, maxFxOverdrive, SEND_MIN, SEND_MAX);
        }
    }
    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::overdrive] =
      1;

    setFxOverdrive();
}

void playerEngine::fxReversePlayback(uint8_t fx_val, uint8_t fx_n)
{
    trackControlParameter[(int)controlType::sequencerMode + fx_n]
                         [(int)parameterList::samplePlaybeckDirection] = 1;

    uint8_t otherFx_n = !fx_n;

    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        currentSeqModValues.reversePlayback = fx_val;
        setFxReverse();
    }
    else if (fx_n == LEAST_SIGNIFICANT_FX)
    {
        if (!trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                  [(int)parameterList::samplePlaybeckDirection])
        {
            currentSeqModValues.reversePlayback = fx_val;
            setFxReverse();
        }
    }
}
//******* position
void playerEngine::fxPosition(uint8_t fx_val, uint8_t fx_n)
{
    if ((mtProject.instrument[currentInstrument_idx].sample.type == mtSampleTypeWavetable) &&
        (mtProject.instrument[currentInstrument_idx].playMode == playModeWavetable))
    {
        fxPositionWavetable(fx_val, fx_n);
    }
    else if (mtProject.instrument[currentInstrument_idx].playMode == playModeGranular)
    {
        fxPositionGranular(fx_val, fx_n);
    }
    else if ((mtProject.instrument[currentInstrument_idx].playMode != playModeSlice) &&
             (mtProject.instrument[currentInstrument_idx].playMode != playModeBeatSlice))
    {
        fxPositionStartPoint(fx_val, fx_n);
    }
}
void playerEngine::fxPositionStartPoint(uint8_t fx_val, uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    uint8_t maxFxPosition = sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_POSITION);
    uint8_t minFxPosition = sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_POSITION);

    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::startPoint] =
      1;
    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        currentSeqModValues.startPoint =
          mapClamped<int>(fx_val, minFxPosition, maxFxPosition, 0, MAX_16BIT);
    }
    else if (fx_n == LEAST_SIGNIFICANT_FX)
    {
        if (!trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                  [(int)parameterList::startPoint])
        {
            currentSeqModValues.startPoint =
              mapClamped<int>(fx_val, minFxPosition, maxFxPosition, 0, MAX_16BIT);
        }
    }

    setFxStartPoint(fx_n);
}
void playerEngine::fxPositionWavetable(uint8_t fx_val, uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    uint8_t maxFxPosition = sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_POSITION);
    uint8_t minFxPosition = sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_POSITION);

    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        currentSeqModValues.wavetablePosition =
          mapClamped<int>(fx_val,
                          minFxPosition,
                          maxFxPosition,
                          0,
                          mtProject.instrument[currentInstrument_idx].sample.wavetableWindowNumber);
    }
    else if (fx_n == LEAST_SIGNIFICANT_FX)
    {
        if (!trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                  [(int)parameterList::wavetablePosition])
        {
            currentSeqModValues.wavetablePosition = mapClamped<int>(
              fx_val,
              minFxPosition,
              maxFxPosition,
              0,
              mtProject.instrument[currentInstrument_idx].sample.wavetableWindowNumber);
        }
    }

    trackControlParameter[(int)controlType::sequencerMode + fx_n]
                         [(int)parameterList::wavetablePosition] = 1;

    setFxPositionWavetable();
}

void playerEngine::fxTune(uint8_t fx_val, uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    int8_t maxFxTune = sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_TUNE);
    int8_t minFxTune = sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_TUNE);

    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        currentSeqModValues.tune =
          mapClamped<int>(fx_val, minFxTune, maxFxTune, MIN_INSTRUMENT_TUNE, MAX_INSTRUMENT_TUNE);
    }
    else if (fx_n == LEAST_SIGNIFICANT_FX)
    {
        if (!trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                  [(int)parameterList::tune])
        {
            currentSeqModValues.tune = mapClamped<int>(
              fx_val, minFxTune, maxFxTune, MIN_INSTRUMENT_TUNE, MAX_INSTRUMENT_TUNE);
        }
    }

    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::tune] = 1;

    setFxTune();
}

void playerEngine::fxSlideUp(uint8_t fx_val)
{
    uint8_t maxFxSlideUp = sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_SLIDE_UP);
    uint8_t minFxSlideUp = sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_SLIDE_UP);
    uint8_t fxValue = mapClamped<int>(
      fx_val, minFxSlideUp, maxFxSlideUp, AudioPlayMemory::SLIDE_MIN, AudioPlayMemory::SLIDE_MAX);
    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setSlidePitch(true, fxValue, sequencer.getStepPeriodMs());
    }
}
void playerEngine::fxSlideDown(uint8_t fx_val)
{
    uint8_t maxFxSlideDown = sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_SLIDE_DOWN);
    uint8_t minFxSlideDown = sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_SLIDE_DOWN);
    uint8_t fxValue = mapClamped<int>(fx_val,
                                      minFxSlideDown,
                                      maxFxSlideDown,
                                      AudioPlayMemory::SLIDE_MIN,
                                      AudioPlayMemory::SLIDE_MAX);
    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setSlidePitch(false, fxValue, sequencer.getStepPeriodMs());
    }
}

void playerEngine::fxPositionGranular(uint8_t fx_val, uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    uint8_t maxFxPosition = sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_POSITION);
    uint8_t minFxPosition = sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_POSITION);

    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        currentSeqModValues.granularPosition =
          mapClamped<int>(fx_val, minFxPosition, maxFxPosition, 0, MAX_16BIT);
    }
    else if (fx_n == LEAST_SIGNIFICANT_FX)
    {
        if (!trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                  [(int)parameterList::granularPosition])
        {
            currentSeqModValues.granularPosition =
              mapClamped<int>(fx_val, minFxPosition, maxFxPosition, 0, MAX_16BIT);
        }
    }

    trackControlParameter[(int)controlType::sequencerMode + fx_n]
                         [(int)parameterList::granularPosition] = 1;

    setFxPositionGranular();
}

static uint16_t mapStepFxToGranularLengthSamples(uint8_t fx_val, uint8_t minFx, uint8_t maxFx, uint32_t sampleLength)
{
    uint8_t pct = clamp<uint8_t>(fx_val, minFx, maxFx);
    uint32_t targetSamples = 44;
    if (pct <= 50)
    {
        targetSamples = mapClamped<int>(pct, 0, 50, 44, 4410);
    }
    else
    {
        targetSamples = mapClamped<int>(pct, 50, 100, 4410, 44100);
    }
    if (sampleLength > 0 && targetSamples > sampleLength)
    {
        targetSamples = sampleLength;
    }
    if (targetSamples < 44 && sampleLength >= 44)
    {
        targetSamples = 44;
    }
    else if (targetSamples < 1)
    {
        targetSamples = 1;
    }
    if (targetSamples > MAX_16BIT)
    {
        targetSamples = MAX_16BIT;
    }
    return static_cast<uint16_t>(targetSamples);
}

void playerEngine::fxGranularLength(uint8_t fx_val, uint8_t fx_n)
{
    if (mtProject.instrument[currentInstrument_idx].playMode != playModeGranular)
    {
        return;
    }

    uint8_t otherFx_n = !fx_n;

    uint8_t maxFxGranLen = sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_GRANULAR_LENGTH);
    uint8_t minFxGranLen = sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_GRANULAR_LENGTH);
    uint32_t sampleLen = mtProject.instrument[currentInstrument_idx].sample.length;

    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        currentSeqModValues.granularLength =
          mapStepFxToGranularLengthSamples(fx_val, minFxGranLen, maxFxGranLen, sampleLen);
    }
    else if (fx_n == LEAST_SIGNIFICANT_FX)
    {
        if (!trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                  [(int)parameterList::granularLength])
        {
            currentSeqModValues.granularLength =
              mapStepFxToGranularLengthSamples(fx_val, minFxGranLen, maxFxGranLen, sampleLen);
        }
    }

    trackControlParameter[(int)controlType::sequencerMode + fx_n]
                         [(int)parameterList::granularLength] = 1;

    setFxGranularLength();
}
//*******
void playerEngine::fxRandomVolume(uint8_t fx_val, uint8_t fx_n)
{
    uint8_t localVolume = 0;
    uint8_t otherFx_n = !fx_n;

    if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                             [(int)parameterList::volume])
        localVolume = currentSeqModValues.volume;
    else if (stepVelocity >= 0)
        localVolume = (uint8_t)clamp<int>((int)stepVelocity,
                                          (int)MIN_INSTRUMENT_VOLUME,
                                          (int)MAX_INSTRUMENT_VOLUME);
    else
        localVolume = mtProject.instrument[currentInstrument_idx].volume;

    int8_t maxFxVol = sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_VOLUME);
    int8_t minFxVol = sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_VOLUME);
    uint8_t minRand = 0, maxRand = 0;

    if (localVolume + fx_val > maxFxVol)
        maxRand = maxFxVol;
    else
        maxRand = localVolume + fx_val;

    if (localVolume - fx_val < minFxVol)
        minRand = minFxVol;
    else
        minRand = localVolume - fx_val;

    localVolume = utilMathRandom::random(minRand, maxRand);

    fxVolume(localVolume, fx_n);
}
static constexpr uint16_t kEnvStepFxTimeMs[16] = {
    0, 5, 12, 25, 45, 80, 140, 240, 400, 650, 1000, 1500, 2200, 3300, 5000, 8000
};

void playerEngine::fxAmpEnv(uint8_t fx_val, uint8_t fx_n)
{
    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::ampAttack] = 1;
    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::ampRelease] = 1;

    uint8_t nibA = (fx_val >> 4) & 0x0F;
    uint8_t nibR = fx_val & 0x0F;

    currentSeqModValues.ampAttack = kEnvStepFxTimeMs[nibA];
    currentSeqModValues.ampRelease = kEnvStepFxTimeMs[nibR];

    uint8_t curLoop = mtProject.instrument[currentInstrument_idx].envelope[envAmp].loop;

    if (curLoop == 0)
    {
        for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
        {
            playMemPtr[channel]->envelopeAttack(kEnvStepFxTimeMs[nibA]);
            playMemPtr[channel]->envelopeDecay(kEnvStepFxTimeMs[nibR]);
            playMemPtr[channel]->envelopeRelease(kEnvStepFxTimeMs[nibR]);
            if (nibR <= 6)
            {
                playMemPtr[channel]->envelopeSustain(0.0f);
            }
        }
    }
    else if (curLoop >= 2)
    {
        float durMs = (float)mtProject.instrument[currentInstrument_idx].envelope[envAmp].decay;
        if (durMs <= 1.0f) durMs = 1000.0f;
        if (nibA < 8)
        {
            durMs = durMs * (0.05f + 0.95f * ((float)nibA / 8.0f));
        }
        else if (nibA > 8)
        {
            durMs = durMs * (1.0f + 3.0f * ((float)(nibA - 8) / 7.0f));
        }
        if (durMs < 10.0f) durMs = 10.0f;
        currentSeqModValues.ampAttack = (uint16_t)durMs;
        currentSeqModValues.ampRelease = (uint16_t)durMs;

        int8_t baseWx = unpackWarpX(mtProject.instrument[currentInstrument_idx].lfo[envAmp].wtBend);
        int8_t baseWy = unpackWarpY(mtProject.instrument[currentInstrument_idx].lfo[envAmp].wtBend);
        int stepWx = clamp<int>(baseWx + (int)nibR - 8, -7, 7);

        for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
        {
            playMemPtr[channel]->envelopeAttack(durMs);
            playMemPtr[channel]->envelopeDecay(durMs);
            playMemPtr[channel]->envelopeSetWavetable(
              curLoop,
              mtProject.instrument[currentInstrument_idx].lfo[envAmp].shape,
              packWarpXY((int8_t)stepWx, baseWy),
              mtProject.instrument[currentInstrument_idx].lfo[envAmp].wtSust);
        }
    }
}

void playerEngine::endFxAmpEnv(uint8_t fx_n)
{
    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::ampAttack] = 0;
    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::ampRelease] = 0;

    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->envelopeAttack(mtProject.instrument[currentInstrument_idx].envelope[envAmp].attack);
        playMemPtr[channel]->envelopeDecay(mtProject.instrument[currentInstrument_idx].envelope[envAmp].decay);
        playMemPtr[channel]->envelopeSustain(mtProject.instrument[currentInstrument_idx].envelope[envAmp].sustain);
        playMemPtr[channel]->envelopeRelease(mtProject.instrument[currentInstrument_idx].envelope[envAmp].release);
        playMemPtr[channel]->envelopeSetWavetable(
          mtProject.instrument[currentInstrument_idx].envelope[envAmp].loop,
          mtProject.instrument[currentInstrument_idx].lfo[envAmp].shape,
          mtProject.instrument[currentInstrument_idx].lfo[envAmp].wtBend,
          mtProject.instrument[currentInstrument_idx].lfo[envAmp].wtSust);
    }
}

void playerEngine::fxFilterEnv(uint8_t fx_val, uint8_t fx_n)
{
    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::filterAttack] = 1;
    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::filterRelease] = 1;

    uint8_t nibA = (fx_val >> 4) & 0x0F;
    uint8_t nibR = fx_val & 0x0F;

    currentSeqModValues.filterAttack = kEnvStepFxTimeMs[nibA];
    currentSeqModValues.filterRelease = kEnvStepFxTimeMs[nibR];

    uint8_t curLoop = mtProject.instrument[currentInstrument_idx].envelope[envCutoff].loop;

    if (curLoop == 0)
    {
        envelopePtr[envCutoff]->putMidiParameter(0, 41, nibA * 8);
        envelopePtr[envCutoff]->putMidiParameter(0, 43, nibR * 8);
        envelopePtr[envCutoff]->putMidiParameter(0, 45, nibR * 8);
        if (nibR <= 6)
        {
            envelopePtr[envCutoff]->putMidiParameter(0, 44, 0);
        }
    }
    else if (curLoop >= 2)
    {
        float durMs = (float)mtProject.instrument[currentInstrument_idx].envelope[envCutoff].decay;
        if (durMs <= 1.0f) durMs = 1000.0f;
        if (nibA < 8)
        {
            durMs = durMs * (0.05f + 0.95f * ((float)nibA / 8.0f));
        }
        else if (nibA > 8)
        {
            durMs = durMs * (1.0f + 3.0f * ((float)(nibA - 8) / 7.0f));
        }
        if (durMs < 10.0f) durMs = 10.0f;
        currentSeqModValues.filterAttack = (uint16_t)durMs;

        int8_t baseWx = unpackWarpX(mtProject.instrument[currentInstrument_idx].lfo[envCutoff].wtBend);
        int8_t baseWy = unpackWarpY(mtProject.instrument[currentInstrument_idx].lfo[envCutoff].wtBend);
        int stepWx = clamp<int>(baseWx + (int)nibR - 8, -7, 7);
        envelopePtr[envCutoff]->setWavetableParams(
          mtProject.instrument[currentInstrument_idx].lfo[envCutoff].shape,
          packWarpXY((int8_t)stepWx, baseWy),
          mtProject.instrument[currentInstrument_idx].lfo[envCutoff].wtSust);
        envelopePtr[envCutoff]->setWavetableDurationMs(durMs);
    }
}

void playerEngine::endFxFilterEnv(uint8_t fx_n)
{
    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::filterAttack] = 0;
    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::filterRelease] = 0;

    uint8_t curLoop = mtProject.instrument[currentInstrument_idx].envelope[envCutoff].loop;
    if (curLoop == 1 || curLoop == 3)
    {
        envelopePtr[envCutoff]->init(&lfoBasedEnvelope[envCutoff]);
    }
    else
    {
        envelopePtr[envCutoff]->init(&mtProject.instrument[currentInstrument_idx].envelope[envCutoff]);
    }
    envelopePtr[envCutoff]->setWavetableParams(
      mtProject.instrument[currentInstrument_idx].lfo[envCutoff].shape,
      mtProject.instrument[currentInstrument_idx].lfo[envCutoff].wtBend,
      mtProject.instrument[currentInstrument_idx].lfo[envCutoff].wtSust);
}

void playerEngine::fxVolume(uint8_t fx_val, uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    uint8_t maxFxVolume = sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_VOLUME);
    uint8_t minFxVolume = sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_VOLUME);

    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::volume] = 1;
    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        currentSeqModValues.volume =
          mapClamped<int>(fx_val, minFxVolume, maxFxVolume, 0, MAX_INSTRUMENT_VOLUME);
    }
    else if (fx_n == LEAST_SIGNIFICANT_FX)
    {
        if (!trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                  [(int)parameterList::volume])
        {
            currentSeqModValues.volume =
              mapClamped<int>(fx_val, minFxVolume, maxFxVolume, 0, MAX_INSTRUMENT_VOLUME);
        }
    }

    setRefreshBit(VOLUME_MASK);
}

void playerEngine::fxSampleSlice(uint8_t fx_val, uint8_t fx_n)
{
    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::slice] = 1;

    uint8_t otherFx_n = !fx_n;

    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        currentSeqModValues.slice =
          fx_val % (mtProject.instrument[currentInstrument_idx].sliceNumber);
    }
    else if (fx_n == LEAST_SIGNIFICANT_FX)
    {
        if (!trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                  [(int)parameterList::slice])
        {
            currentSeqModValues.slice =
              fx_val % (mtProject.instrument[currentInstrument_idx].sliceNumber);
        }
    }

    setFxSlice();
}

void playerEngine::fxVolumeLFO(uint8_t fx_val, uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::lfoAmp] = 1;

    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        currentSeqModValues.lfoAmpEnable = 1;
        currentSeqModValues.lfoAmpRate = fx_val;
        setSyncParamsLFO(envAmp);
    }
    else if (fx_n == LEAST_SIGNIFICANT_FX)
    {
        if (!trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                  [(int)parameterList::lfoAmp])
        {
            currentSeqModValues.lfoAmpEnable = 1;
            currentSeqModValues.lfoAmpRate = fx_val;
            setSyncParamsLFO(envAmp);
        }
    }
    setFxAmpRateLFO();
}

void playerEngine::fxCutoffLFO(uint8_t fx_val, uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::lfoCutoff] =
      1;
    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        currentSeqModValues.lfoCutoffEnable = 1;
        currentSeqModValues.lfoCutoffRate = fx_val;
    }
    else if (fx_n == LEAST_SIGNIFICANT_FX)
    {
        if (!trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                  [(int)parameterList::lfoCutoff])
        {
            currentSeqModValues.lfoCutoffEnable = 1;
            currentSeqModValues.lfoCutoffRate = fx_val;
        }
    }

    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::lfoCutoff])
    {
        changeCutoffLfoRatePerformanceMode(performanceMod.lfoCutoffRate);
    }
    else
    {
        uint8_t curLoop = mtProject.instrument[currentInstrument_idx].envelope[envCutoff].loop;
        uint8_t targetLoop = (curLoop == 3) ? 3 : 1;
        calcLfoBasedEnvelope(&lfoBasedEnvelope[envCutoff],
                             &mtProject.instrument[currentInstrument_idx].lfo[envCutoff],
                             currentSeqModValues.lfoCutoffRate,
                             0,
                             targetLoop);
        initEnvelopesParameters(envCutoff, &lfoBasedEnvelope[envCutoff]);
        if ((envelopePtr[envCutoff]->isKeyPressed() != 1) &&
            (envelopePtr[envCutoff]->getPhase() == 0))
        {
            bool isRandom =
              (curLoop == 1) &&
              (mtProject.instrument[currentInstrument_idx].lfo[envCutoff].shape == lfoShapeRandom);
            envelopePtr[envCutoff]->setIsRandom(isRandom);
            envelopePtr[envCutoff]->start();
        }
        else if ((curLoop == 0 || curLoop == 2) &&
                 (!trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                        [(int)parameterList::lfoCutoff]))
        {
            envelopePtr[envCutoff]->kill();
            envelopePtr[envCutoff]->start();
        }
    }

    setSyncParamsLFO(envCutoff);
}
//******* position LFO
void playerEngine::fxPositionLFO(uint8_t fx_val, uint8_t fx_n)
{
    if (mtProject.instrument[currentInstrument_idx].playMode == playModeWavetable)
    {
        fxPositionWavetableLFO(fx_val, fx_n);
    }
    else if (mtProject.instrument[currentInstrument_idx].playMode == playModeGranular)
    {
        fxPositionGranularLFO(fx_val, fx_n);
    }
}
void playerEngine::fxPositionGranularLFO(uint8_t fx_val, uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    trackControlParameter[(int)controlType::sequencerMode + fx_n]
                         [(int)parameterList::lfoGranularPosition] = 1;

    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        currentSeqModValues.lfoGranularPositionEnable = 1;
        currentSeqModValues.lfoGranularPositionRate = fx_val;
    }
    else if (fx_n == LEAST_SIGNIFICANT_FX)
    {
        if (!trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                  [(int)parameterList::lfoGranularPosition])
        {
            currentSeqModValues.lfoGranularPositionEnable = 1;
            currentSeqModValues.lfoGranularPositionRate = fx_val;
        }
    }

    if (trackControlParameter[(int)controlType::performanceMode]
                             [(int)parameterList::lfoGranularPosition])
    {
        changePositionLfoRatePerformanceMode(performanceMod.lfoPositionRate);
    }
    else
    {
        uint8_t curLoop = mtProject.instrument[currentInstrument_idx].envelope[envGranPos].loop;
        uint8_t targetLoop = (curLoop == 3) ? 3 : 1;
        calcLfoBasedEnvelope(&lfoBasedEnvelope[envGranPos],
                             &mtProject.instrument[currentInstrument_idx].lfo[envGranPos],
                             currentSeqModValues.lfoGranularPositionRate,
                             0,
                             targetLoop);
        initEnvelopesParameters(envGranPos, &lfoBasedEnvelope[envGranPos]);
        if ((envelopePtr[envGranPos]->isKeyPressed() != 1) &&
            (envelopePtr[envGranPos]->getPhase() == 0))
        {
            bool isRandom =
              (curLoop == 1) &&
              (mtProject.instrument[currentInstrument_idx].lfo[envGranPos].shape == lfoShapeRandom);
            envelopePtr[envGranPos]->setIsRandom(isRandom);
            envelopePtr[envGranPos]->start();
        }
    }

    setSyncParamsLFO(envGranPos);
}
void playerEngine::fxPositionWavetableLFO(uint8_t fx_val, uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    trackControlParameter[(int)controlType::sequencerMode + fx_n]
                         [(int)parameterList::lfoWavetablePosition] = 1;

    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        currentSeqModValues.lfoWavetablePositionEnable = 1;
        currentSeqModValues.lfoWavetablePositionRate = fx_val;
    }
    else if (fx_n == LEAST_SIGNIFICANT_FX)
    {
        if (!trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                  [(int)parameterList::lfoWavetablePosition])
        {
            currentSeqModValues.lfoWavetablePositionEnable = 1;
            currentSeqModValues.lfoWavetablePositionRate = fx_val;
        }
    }

    if (trackControlParameter[(int)controlType::performanceMode]
                             [(int)parameterList::lfoWavetablePosition])
    {
        changePositionLfoRatePerformanceMode(performanceMod.lfoPositionRate);
    }
    else
    {
        uint8_t curLoop = mtProject.instrument[currentInstrument_idx].envelope[envWtPos].loop;
        uint8_t targetLoop = (curLoop == 3) ? 3 : 1;
        calcLfoBasedEnvelope(&lfoBasedEnvelope[envWtPos],
                             &mtProject.instrument[currentInstrument_idx].lfo[envWtPos],
                             currentSeqModValues.lfoWavetablePositionRate,
                             0,
                             targetLoop);
        initEnvelopesParameters(envWtPos, &lfoBasedEnvelope[envWtPos]);
        if ((envelopePtr[envWtPos]->isKeyPressed() != 1) &&
            (envelopePtr[envWtPos]->getPhase() == 0))
        {
            bool isRandom =
              (curLoop == 1) &&
              (mtProject.instrument[currentInstrument_idx].lfo[envWtPos].shape == lfoShapeRandom);
            envelopePtr[envWtPos]->setIsRandom(isRandom);
            envelopePtr[envWtPos]->start();
        }
    }

    setSyncParamsLFO(envWtPos);
}
//*********
void playerEngine::fxPanningLFO(uint8_t fx_val, uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::lfoPanning] =
      1;
    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        currentSeqModValues.lfoPanningEnable = 1;
        currentSeqModValues.lfoPanningRate = fx_val;
    }
    else if (fx_n == LEAST_SIGNIFICANT_FX)
    {
        if (!trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                  [(int)parameterList::lfoPanning])
        {
            currentSeqModValues.lfoPanningEnable = 1;
            currentSeqModValues.lfoPanningRate = fx_val;
        }
    }

    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::lfoPanning])
    {
        changePanningLfoRatePerformanceMode(performanceMod.lfoPanningRate);
    }
    else
    {
        uint8_t curLoop = mtProject.instrument[currentInstrument_idx].envelope[envPan].loop;
        uint8_t targetLoop = (curLoop == 3) ? 3 : 1;
        calcLfoBasedEnvelope(&lfoBasedEnvelope[envPan],
                             &mtProject.instrument[currentInstrument_idx].lfo[envPan],
                             currentSeqModValues.lfoPanningRate,
                             0,
                             targetLoop);
        initEnvelopesParameters(envPan, &lfoBasedEnvelope[envPan]);
        if ((envelopePtr[envPan]->isKeyPressed() != 1) && (envelopePtr[envPan]->getPhase() == 0))
        {
            bool isRandom =
              (curLoop == 1) &&
              (mtProject.instrument[currentInstrument_idx].lfo[envPan].shape == lfoShapeRandom);
            envelopePtr[envPan]->setIsRandom(isRandom);
            envelopePtr[envPan]->start();
        }
    }

    setSyncParamsLFO(envPan);
}
//************************************************************
void playerEngine::fxFinetuneLFO(uint8_t fx_val, uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::lfoFinetune] =
      1;
    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        currentSeqModValues.lfoFinetuneEnable = 1;
        currentSeqModValues.lfoFinetuneRate = fx_val;
    }
    else if (fx_n == LEAST_SIGNIFICANT_FX)
    {
        if (!trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                  [(int)parameterList::lfoFinetune])
        {
            currentSeqModValues.lfoFinetuneEnable = 1;
            currentSeqModValues.lfoFinetuneRate = fx_val;
        }
    }

    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::lfoFinetune])
    {
        changeFinetuneLfoRatePerformanceMode(performanceMod.lfoFinetuneRate);
    }
    else
    {
        uint8_t curLoop = mtProject.instrument[currentInstrument_idx].envelope[envFinetune].loop;
        uint8_t targetLoop = (curLoop == 3) ? 3 : 1;
        calcLfoBasedEnvelope(&lfoBasedEnvelope[envFinetune],
                             &mtProject.instrument[currentInstrument_idx].lfo[envFinetune],
                             currentSeqModValues.lfoFinetuneRate,
                             0,
                             targetLoop);
        initEnvelopesParameters(envFinetune, &lfoBasedEnvelope[envFinetune]);
        if ((envelopePtr[envFinetune]->isKeyPressed() != 1) &&
            (envelopePtr[envFinetune]->getPhase() == 0))
        {

            bool isRandom =
              (curLoop == 1) &&
              (mtProject.instrument[currentInstrument_idx].lfo[envFinetune].shape ==
               lfoShapeRandom);
            envelopePtr[envFinetune]->setIsRandom(isRandom);
            envelopePtr[envFinetune]->start();
        }
    }

    setSyncParamsLFO(envFinetune);
}

//*********************************** END SEQ FX

void playerEngine::endFxFilter(uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    trackControlParameter[(int)controlType::sequencerMode + fx_n]
                         [(int)parameterList::filterCutoff] = 0;
    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::filterType] =
      0;
    trackControlParameter[(int)controlType::sequencerMode + fx_n]
                         [(int)parameterList::filterEnable] = 0;

    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                 [(int)parameterList::filterCutoff] &&
            trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                 [(int)parameterList::filterEnable] &&
            trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                 [(int)parameterList::filterType])
        {
            uint8_t maxFilterFx =
              sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_FILTER_BANDPASS);
            uint8_t minFilterFx =
              sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_FILTER_BANDPASS);

            currentSeqModValues.filterCutoff =
              (lastSeqVal[otherFx_n] - minFilterFx) / (float)(maxFilterFx - minFilterFx);

            currentSeqModValues.filterType =
              (lastSeqFx[otherFx_n] == Sequencer::strFxConsts::FX_TYPE_FILTER_BANDPASS)
                ? bandPass
                : ((lastSeqFx[otherFx_n] == Sequencer::strFxConsts::FX_TYPE_FILTER_HIGHPASS)
                     ? highPass
                     : lowPass);

            currentSeqModValues.filterEnable = 1;
        }
    }

    if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                             [(int)parameterList::filterCutoff] &&
        trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                             [(int)parameterList::filterEnable])
    {
        setFxCutoff();
    }
    else
    {
        clearFxCutoff();
    }

    if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                             [(int)parameterList::filterType])
    {
        setFxFilterType();
    }
    else
    {
        clearFxFilterType();
    }
}

void playerEngine::endFxGlide(uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::glide] = 0;

    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                 [(int)parameterList::glide])
        {
            uint8_t maxFxGlide = sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_GLIDE);
            uint8_t minFxGlide = sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_GLIDE);
            uint8_t halfFxGlide = maxFxGlide / 2;

            currentSeqModValues.glide =
              (lastSeqVal[otherFx_n] > halfFxGlide)
                ? mapClamped<int>(lastSeqVal[otherFx_n], halfFxGlide, maxFxGlide, 1000, GLIDE_MAX)
                : mapClamped<int>(lastSeqVal[otherFx_n], minFxGlide, halfFxGlide, GLIDE_MIN, 1000);

            setFxGlide();
        }
        else
        {
            clearFxGlide();
        }
    }
    else
    {
        if (!trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                  [(int)parameterList::glide])
        {
            clearFxGlide();
        }
    }
}
void playerEngine::endFxFinetune(uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::fineTune] = 0;

    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                 [(int)parameterList::fineTune])
        {
            uint8_t maxFxFinetune = sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_MICROTUNING);
            uint8_t minFxFinetune = sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_MICROTUNING);

            currentSeqModValues.fineTune = mapClamped<int>(lastSeqVal[otherFx_n],
                                                           minFxFinetune,
                                                           maxFxFinetune,
                                                           MIN_INSTRUMENT_FINETUNE,
                                                           MAX_INSTRUMENT_FINETUNE);

            setFxFinetune();
        }
        else
        {
            clearFxFinetune();
        }
    }
    else
    {
        if (!trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                  [(int)parameterList::fineTune])
        {
            clearFxFinetune();
        }
    }
}

void playerEngine::endFxPanning(uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::panning] = 0;

    if (fx_n == MOST_SIGNIFICANT_FX) // Trzeba zaktualizowac wartosci sequencera
    {
        if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                 [(int)parameterList::panning])
        {
            uint8_t maxFxPanning = sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_PANNING);
            uint8_t minFxPanning = sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_PANNING);

            currentSeqModValues.panning = mapClamped<int>(
              lastSeqVal[otherFx_n], minFxPanning, maxFxPanning, PANNING_MIN, PANNING_MAX);
        }
    }

    setRefreshBit(PANNING_MASK);
}

void playerEngine::endFxBitDepth(uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::bitDepth] = 0;

    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                 [(int)parameterList::bitDepth])
        {
            uint8_t maxFxBitDepth = sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_BIT_DEPTH);
            uint8_t minFxBitDepth = sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_BIT_DEPTH);

            currentSeqModValues.bitDepth = mapClamped<int>(
              lastSeqVal[otherFx_n], minFxBitDepth, maxFxBitDepth, BIT_DEPTH_MIN, BIT_DEPTH_MAX);
        }
    }

    if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                             [(int)parameterList::bitDepth])
    {
        setFxBitDepth();
    }
    else
    {
        clearFxBitDepth();
    }
}

void playerEngine::endFxDelaySend(uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::delaySend] =
      0;

    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                 [(int)parameterList::delaySend])
        {
            uint8_t maxFxDelaySend = sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_DELAY_SEND);
            uint8_t minFxDelaySend = sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_DELAY_SEND);

            currentSeqModValues.delaySend = mapClamped<int>(
              lastSeqVal[otherFx_n], minFxDelaySend, maxFxDelaySend, SEND_MIN, SEND_MAX);
        }
    }

    setRefreshBit(DELAY_SEND_MASK);
}
void playerEngine::endFxReverbSend(uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::reverbSend] =
      0;

    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                 [(int)parameterList::reverbSend])
        {
            uint8_t maxFxReverbSend =
              sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_REVERB_SEND);
            uint8_t minFxReverbSend =
              sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_REVERB_SEND);

            currentSeqModValues.reverbSend = mapClamped<int>(
              lastSeqVal[otherFx_n], minFxReverbSend, maxFxReverbSend, SEND_MIN, SEND_MAX);
        }
    }

    setRefreshBit(REVERB_SEND_MASK);
}

void playerEngine::endFxOverdrive(uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::overdrive] =
      0;

    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                 [(int)parameterList::overdrive])
        {
            uint8_t maxFxOverdrive = sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_OVERDRIVE);
            uint8_t minFxOverdrive = sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_OVERDRIVE);

            currentSeqModValues.overdrive = mapClamped<int>(
              lastSeqVal[otherFx_n], minFxOverdrive, maxFxOverdrive, SEND_MIN, SEND_MAX);
        }
    }

    if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                             [(int)parameterList::overdrive])
    {
        setFxOverdrive();
    }
    else
    {
        clearFxOverdrive();
    }
}

void playerEngine::endFxReversePlayback(uint8_t fx_n)
{
    trackControlParameter[(int)controlType::sequencerMode + fx_n]
                         [(int)parameterList::samplePlaybeckDirection] = 0;

    uint8_t otherFx_n = !fx_n;

    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                 [(int)parameterList::samplePlaybeckDirection])
        {
            currentSeqModValues.reversePlayback = lastSeqVal[otherFx_n];
        }
    }

    if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                             [(int)parameterList::samplePlaybeckDirection])
    {
        setFxReverse();
    }
    else
    {
        clearFxReverse();
    }
}
//******** position
void playerEngine::endFxPosition(uint8_t fx_n)
{
    //	if((mtProject.instrument[currentInstrument_idx].sample.type == mtSampleTypeWavetable) &&
    //(mtProject.instrument[currentInstrument_idx].playMode == playModeWavetable))
    //	{
    //		endFxPositionWavetable(fx_n);
    //	}
    //	else if(mtProject.instrument[currentInstrument_idx].playMode == playModeGranular)
    //	{
    //		endFxPositionGranular(fx_n);
    //	}
    //	else if((mtProject.instrument[currentInstrument_idx].playMode != playModeSlice) &&
    //			(mtProject.instrument[currentInstrument_idx].playMode != playModeBeatSlice))
    //	{
    //		endFxPositionStartPoint(fx_n);
    //	}

    if (trackControlParameter[(int)controlType::sequencerMode + fx_n]
                             [(int)parameterList::wavetablePosition])
    {
        endFxPositionWavetable(fx_n);
    }
    else if (trackControlParameter[(int)controlType::sequencerMode + fx_n]
                                  [(int)parameterList::granularPosition])
    {
        endFxPositionGranular(fx_n);
    }
    else if (trackControlParameter[(int)controlType::sequencerMode + fx_n]
                                  [(int)parameterList::startPoint])
    {
        endFxPositionStartPoint(fx_n);
    }
}
void playerEngine::endFxPositionStartPoint(uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::startPoint] =
      0;
    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::loopPoint1] =
      0;
    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::loopPoint2] =
      0;

    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                 [(int)parameterList::startPoint])
        {
            uint8_t maxFxPosition = sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_POSITION);
            uint8_t minFxPosition = sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_POSITION);

            currentSeqModValues.startPoint =
              mapClamped<int>(lastSeqVal[otherFx_n], minFxPosition, maxFxPosition, 0, MAX_16BIT);
        }
    }

    if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                             [(int)parameterList::startPoint])
    {
        setFxStartPoint(otherFx_n);
    }
    else
    {
        clearFxStartPoint();
    }
}
void playerEngine::endFxPositionGranular(uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    trackControlParameter[(int)controlType::sequencerMode + fx_n]
                         [(int)parameterList::granularPosition] = 0;

    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                 [(int)parameterList::granularPosition])
        {
            uint8_t maxFxPosition = sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_POSITION);
            uint8_t minFxPosition = sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_POSITION);

            currentSeqModValues.granularPosition =
              mapClamped<int>(lastSeqVal[otherFx_n], minFxPosition, maxFxPosition, 0, MAX_16BIT);
        }
    }

    if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                             [(int)parameterList::granularPosition])
    {
        setFxPositionGranular();
    }
    else
    {
        clearFxPositionGranular();
    }
}

void playerEngine::endFxGranularLength(uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    trackControlParameter[(int)controlType::sequencerMode + fx_n]
                         [(int)parameterList::granularLength] = 0;

    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                 [(int)parameterList::granularLength])
        {
            uint8_t maxFxGranLen =
              sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_GRANULAR_LENGTH);
            uint8_t minFxGranLen =
              sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_GRANULAR_LENGTH);
            uint32_t sampleLen = mtProject.instrument[currentInstrument_idx].sample.length;

            currentSeqModValues.granularLength = mapStepFxToGranularLengthSamples(
              lastSeqVal[otherFx_n], minFxGranLen, maxFxGranLen, sampleLen);
        }
    }

    if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                             [(int)parameterList::granularLength])
    {
        setFxGranularLength();
    }
    else
    {
        clearFxGranularLength();
    }
}
void playerEngine::endFxPositionWavetable(uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    trackControlParameter[(int)controlType::sequencerMode + fx_n]
                         [(int)parameterList::wavetablePosition] = 0;
    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                 [(int)parameterList::wavetablePosition])
        {
            uint8_t maxFxPosition = sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_POSITION);
            uint8_t minFxPosition = sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_POSITION);

            currentSeqModValues.wavetablePosition = mapClamped<int>(
              lastSeqVal[otherFx_n],
              minFxPosition,
              maxFxPosition,
              0,
              mtProject.instrument[currentInstrument_idx].sample.wavetableWindowNumber);
        }
    }

    if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                             [(int)parameterList::wavetablePosition])
    {
        setFxPositionWavetable();
    }
    else
    {
        clearFxPositionWavetable();
    }
}

void playerEngine::endFxTune(uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::tune] = 0;
    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                 [(int)parameterList::tune])
        {
            int8_t maxFxTune = sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_TUNE);
            int8_t minFxTune = sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_TUNE);

            currentSeqModValues.tune = mapClamped<int>(lastSeqVal[otherFx_n],
                                                       minFxTune,
                                                       maxFxTune,
                                                       MIN_INSTRUMENT_TUNE,
                                                       MAX_INSTRUMENT_TUNE);
        }
    }

    if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                             [(int)parameterList::tune])
    {
        setFxTune();
    }
    else
    {
        clearFxTune();
    }
}

//*******
void playerEngine::endFxRandomVolume(uint8_t fx_n)
{
    endFxVolume(fx_n);
}

void playerEngine::endFxVolume(uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::volume] = 0;

    if (fx_n == MOST_SIGNIFICANT_FX) // Trzeba zaktualizowac wartosci sequencera
    {
        if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                 [(int)parameterList::volume])
        {
            uint8_t maxFxVolume = sequencer.getFxMax(Sequencer::strFxConsts::FX_TYPE_VOLUME);
            uint8_t minFxVolume = sequencer.getFxMin(Sequencer::strFxConsts::FX_TYPE_VOLUME);

            currentSeqModValues.volume = mapClamped<int>(
              lastSeqVal[otherFx_n], minFxVolume, maxFxVolume, 0, MAX_INSTRUMENT_VOLUME);
        }
    }

    setRefreshBit(VOLUME_MASK);
}

void playerEngine::endFxSlice(uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::slice] = 0;

    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                 [(int)parameterList::slice])
        {
            currentSeqModValues.slice =
              lastSeqVal[otherFx_n] % mtProject.instrument[currentInstrument_idx].sliceNumber;
        }
    }

    if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                             [(int)parameterList::slice])
    {
        setFxSlice();
    }
    else
    {
        clearFxSlice();
    }
}

void playerEngine::endFxVolumeLFO(uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::lfoAmp] = 0;

    if (fx_n == MOST_SIGNIFICANT_FX)
    {
        if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                 [(int)parameterList::lfoAmp])
        {
            syncFxAmpLFO();
            currentSeqModValues.lfoAmpRate = lastSeqVal[otherFx_n];
            currentSeqModValues.lfoAmpEnable = 1;
        }
    }

    if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                             [(int)parameterList::lfoAmp])
    {
        setFxAmpRateLFO();
    }
    else
    {
        clearFxAmpRateLFO();
    }

    setSyncParamsLFO(envAmp);
}

void playerEngine::endFxCutoffLFO(uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::lfoCutoff] =
      0;

    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::lfoCutoff])
    {
        if (fx_n == MOST_SIGNIFICANT_FX)
        {
            if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                     [(int)parameterList::lfoCutoff])
            {
                currentSeqModValues.lfoCutoffRate = lastSeqVal[otherFx_n];
                currentSeqModValues.lfoCutoffEnable = 1;

                changeCutoffLfoRatePerformanceMode(performanceMod.lfoCutoffRate);
            }
        }
    }
    else
    {
        uint8_t curLoop = mtProject.instrument[currentInstrument_idx].envelope[envCutoff].loop;
        if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                 [(int)parameterList::lfoCutoff])
        {
            if (fx_n == MOST_SIGNIFICANT_FX)
            {
                currentSeqModValues.lfoCutoffRate = lastSeqVal[otherFx_n];
                currentSeqModValues.lfoCutoffEnable = 1;

                uint8_t targetLoop = (curLoop == 3) ? 3 : 1;
                calcLfoBasedEnvelope(&lfoBasedEnvelope[envCutoff],
                                     &mtProject.instrument[currentInstrument_idx].lfo[envCutoff],
                                     currentSeqModValues.lfoCutoffRate,
                                     0,
                                     targetLoop);
                initEnvelopesParameters(envCutoff, &lfoBasedEnvelope[envCutoff]);
            }
        }
        else
        {
            if (mtProject.instrument[currentInstrument_idx].envelope[envCutoff].enable)
            {
                if (curLoop == 1 || curLoop == 3)
                {
                    calcLfoBasedEnvelope(
                      &lfoBasedEnvelope[envCutoff],
                      &mtProject.instrument[currentInstrument_idx].lfo[envCutoff],
                      mtProject.instrument[currentInstrument_idx].lfo[envCutoff].speed,
                      0,
                      curLoop);
                    initEnvelopesParameters(envCutoff, &lfoBasedEnvelope[envCutoff]);
                }
                else
                {
                    initEnvelopesParameters(
                      envCutoff, &mtProject.instrument[currentInstrument_idx].envelope[envCutoff]);
                }
            }
        }
    }

    setSyncParamsLFO(envCutoff);
}
//***** position lfo
void playerEngine::endFxPositionLFO(uint8_t fx_n)
{
    if (trackControlParameter[(int)controlType::sequencerMode + fx_n]
                             [(int)parameterList::lfoWavetablePosition])
    {
        endFxPositionWavetableLFO(fx_n);
    }
    else if (trackControlParameter[(int)controlType::sequencerMode + fx_n]
                                  [(int)parameterList::lfoGranularPosition])
    {
        endFxPositionGranularLFO(fx_n);
    }
}
void playerEngine::endFxPositionGranularLFO(uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    trackControlParameter[(int)controlType::sequencerMode + fx_n]
                         [(int)parameterList::lfoGranularPosition] = 0;

    if (trackControlParameter[(int)controlType::performanceMode]
                             [(int)parameterList::lfoGranularPosition])
    {
        if (fx_n == MOST_SIGNIFICANT_FX)
        {
            if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                     [(int)parameterList::lfoGranularPosition])
            {
                currentSeqModValues.lfoGranularPositionRate = lastSeqVal[otherFx_n];
                currentSeqModValues.lfoGranularPositionEnable = 1;

                changePositionLfoRatePerformanceMode(performanceMod.lfoPositionRate);
            }
        }
    }
    else
    {
        uint8_t curLoop = mtProject.instrument[currentInstrument_idx].envelope[envGranPos].loop;
        if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                 [(int)parameterList::lfoGranularPosition])
        {
            if (fx_n == MOST_SIGNIFICANT_FX)
            {
                currentSeqModValues.lfoGranularPositionRate = lastSeqVal[otherFx_n];
                currentSeqModValues.lfoGranularPositionEnable = 1;

                uint8_t targetLoop = (curLoop == 3) ? 3 : 1;
                calcLfoBasedEnvelope(&lfoBasedEnvelope[envGranPos],
                                     &mtProject.instrument[currentInstrument_idx].lfo[envGranPos],
                                     currentSeqModValues.lfoGranularPositionRate,
                                     0,
                                     targetLoop);
                initEnvelopesParameters(envGranPos, &lfoBasedEnvelope[envGranPos]);
            }
        }
        else
        {
            if (mtProject.instrument[currentInstrument_idx].envelope[envGranPos].enable)
            {
                if (curLoop == 1 || curLoop == 3)
                {
                    calcLfoBasedEnvelope(
                      &lfoBasedEnvelope[envGranPos],
                      &mtProject.instrument[currentInstrument_idx].lfo[envGranPos],
                      mtProject.instrument[currentInstrument_idx].lfo[envGranPos].speed,
                      0,
                      curLoop);
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
    }

    setSyncParamsLFO(envGranPos);
}
void playerEngine::endFxPositionWavetableLFO(uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    trackControlParameter[(int)controlType::sequencerMode + fx_n]
                         [(int)parameterList::lfoWavetablePosition] = 0;

    if (trackControlParameter[(int)controlType::performanceMode]
                             [(int)parameterList::lfoWavetablePosition])
    {
        if (fx_n == MOST_SIGNIFICANT_FX)
        {
            if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                     [(int)parameterList::lfoWavetablePosition])
            {
                currentSeqModValues.lfoWavetablePositionRate = lastSeqVal[otherFx_n];
                currentSeqModValues.lfoWavetablePositionEnable = 1;

                changePositionLfoRatePerformanceMode(performanceMod.lfoPositionRate);
            }
        }
    }
    else
    {
        uint8_t curLoop = mtProject.instrument[currentInstrument_idx].envelope[envWtPos].loop;
        if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                 [(int)parameterList::lfoWavetablePosition])
        {
            if (fx_n == MOST_SIGNIFICANT_FX)
            {
                currentSeqModValues.lfoWavetablePositionRate = lastSeqVal[otherFx_n];
                currentSeqModValues.lfoWavetablePositionEnable = 1;

                uint8_t targetLoop = (curLoop == 3) ? 3 : 1;
                calcLfoBasedEnvelope(&lfoBasedEnvelope[envWtPos],
                                     &mtProject.instrument[currentInstrument_idx].lfo[envWtPos],
                                     currentSeqModValues.lfoWavetablePositionRate,
                                     0,
                                     targetLoop);
                initEnvelopesParameters(envWtPos, &lfoBasedEnvelope[envWtPos]);
            }
        }
        else
        {
            if (mtProject.instrument[currentInstrument_idx].isEnvEnabled(envWtPos))
            {
                if (curLoop == 1 || curLoop == 3)
                {
                    calcLfoBasedEnvelope(
                      &lfoBasedEnvelope[envWtPos],
                      &mtProject.instrument[currentInstrument_idx].lfo[envWtPos],
                      mtProject.instrument[currentInstrument_idx].lfo[envWtPos].speed,
                      0,
                      curLoop);
                    initEnvelopesParameters(envWtPos, &lfoBasedEnvelope[envWtPos]);
                }
                else
                {
                    initEnvelopesParameters(
                      envWtPos, &mtProject.instrument[currentInstrument_idx].envelope[envWtPos]);
                }
            }
        }
    }

    setSyncParamsLFO(envWtPos);
}
//********
void playerEngine::endFxPanningLFO(uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::lfoPanning] =
      0;

    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::lfoPanning])
    {
        if (fx_n == MOST_SIGNIFICANT_FX)
        {
            if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                     [(int)parameterList::lfoPanning])
            {
                currentSeqModValues.lfoPanningRate = lastSeqVal[otherFx_n];
                currentSeqModValues.lfoPanningEnable = 1;
            }
        }
        changePanningLfoRatePerformanceMode(performanceMod.lfoPanningRate);
    }
    else
    {
        uint8_t curLoop = mtProject.instrument[currentInstrument_idx].envelope[envPan].loop;
        if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                 [(int)parameterList::lfoPanning])
        {
            if (fx_n == MOST_SIGNIFICANT_FX)
            {
                currentSeqModValues.lfoPanningRate = lastSeqVal[otherFx_n];
                currentSeqModValues.lfoPanningEnable = 1;

                uint8_t targetLoop = (curLoop == 3) ? 3 : 1;
                calcLfoBasedEnvelope(&lfoBasedEnvelope[envPan],
                                     &mtProject.instrument[currentInstrument_idx].lfo[envPan],
                                     currentSeqModValues.lfoPanningRate,
                                     0,
                                     targetLoop);
                initEnvelopesParameters(envPan, &lfoBasedEnvelope[envPan]);
            }
        }
        else
        {
            if (mtProject.instrument[currentInstrument_idx].envelope[envPan].enable)
            {
                if (curLoop == 1 || curLoop == 3)
                {
                    calcLfoBasedEnvelope(
                      &lfoBasedEnvelope[envPan],
                      &mtProject.instrument[currentInstrument_idx].lfo[envPan],
                      mtProject.instrument[currentInstrument_idx].lfo[envPan].speed,
                      0,
                      curLoop);
                    initEnvelopesParameters(envPan, &lfoBasedEnvelope[envPan]);
                }
                else
                {
                    initEnvelopesParameters(
                      envPan, &mtProject.instrument[currentInstrument_idx].envelope[envPan]);
                }
            }
        }
    }

    setSyncParamsLFO(envPan);
}

void playerEngine::endFxFinetuneLFO(uint8_t fx_n)
{
    uint8_t otherFx_n = !fx_n;

    trackControlParameter[(int)controlType::sequencerMode + fx_n][(int)parameterList::lfoFinetune] =
      0;

    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::lfoFinetune])
    {
        if (fx_n == MOST_SIGNIFICANT_FX)
        {
            if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                     [(int)parameterList::lfoFinetune])
            {
                currentSeqModValues.lfoFinetuneRate = lastSeqVal[otherFx_n];
                currentSeqModValues.lfoFinetuneEnable = 1;
            }
        }
        changeFinetuneLfoRatePerformanceMode(performanceMod.lfoFinetuneRate);
    }
    else
    {
        uint8_t curLoop = mtProject.instrument[currentInstrument_idx].envelope[envFinetune].loop;
        if (trackControlParameter[(int)controlType::sequencerMode + otherFx_n]
                                 [(int)parameterList::lfoFinetune])
        {
            if (fx_n == MOST_SIGNIFICANT_FX)
            {
                currentSeqModValues.lfoFinetuneRate = lastSeqVal[otherFx_n];
                currentSeqModValues.lfoFinetuneEnable = 1;

                uint8_t targetLoop = (curLoop == 3) ? 3 : 1;
                calcLfoBasedEnvelope(&lfoBasedEnvelope[envFinetune],
                                     &mtProject.instrument[currentInstrument_idx].lfo[envFinetune],
                                     currentSeqModValues.lfoFinetuneRate,
                                     0,
                                     targetLoop);
                initEnvelopesParameters(envFinetune, &lfoBasedEnvelope[envFinetune]);
            }
        }
        else
        {
            if (mtProject.instrument[currentInstrument_idx].envelope[envFinetune].enable)
            {
                if (curLoop == 1 || curLoop == 3)
                {
                    calcLfoBasedEnvelope(
                      &lfoBasedEnvelope[envFinetune],
                      &mtProject.instrument[currentInstrument_idx].lfo[envFinetune],
                      mtProject.instrument[currentInstrument_idx].lfo[envFinetune].speed,
                      0,
                      curLoop);
                    initEnvelopesParameters(envFinetune, &lfoBasedEnvelope[envFinetune]);
                }
                else
                {
                    initEnvelopesParameters(
                      envFinetune,
                      &mtProject.instrument[currentInstrument_idx].envelope[envFinetune]);
                }
            }
        }
    }

    setSyncParamsLFO(envFinetune);
}

AUDIO_CORE_RAM uint8_t playerEngine::isFxVelocity(uint8_t fx_id)
{
    return (fx_id == Sequencer::strFxConsts::FX_TYPE_VOLUME) ||
           (fx_id == Sequencer::strFxConsts::FX_TYPE_ROLL); // ||
    //			(fx_id == Sequencer::strFxConsts::FX_TYPE_NONE); // jeśli kontynuacja rolki to brak
    // fxow w komunikacie
}
float playerEngine::getMostSignificantAmount()
{
    float localAmount = 0.0f;

    if (trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::lfoAmp] ||
        trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::lfoAmp] ||
        trackControlParameter[(int)controlType::performanceMode][(int)parameterList::lfoAmp])
    {
        localAmount = mtProject.instrument[currentInstrument_idx].lfo[envAmp].amount;
    }
    else
    {
        if (mtProject.instrument[currentInstrument_idx].envelope[envAmp].enable)
        {
            uint8_t curAmpLoop = mtProject.instrument[currentInstrument_idx].envelope[envAmp].loop;
            if (curAmpLoop == 1 || curAmpLoop == 3)
            {
                localAmount = mtProject.instrument[currentInstrument_idx].lfo[envAmp].amount;
            }
            else
            {
                localAmount = mtProject.instrument[currentInstrument_idx].envelope[envAmp].amount;
            }
        }
        else
        {
            localAmount = 1.0f;
        }
    }

    return localAmount;
}

void playerEngine::setStepVelocity(int8_t velocity)
{
    stepVelocity = velocity;
    setRefreshBit(VOLUME_MASK);
}

uint8_t playerEngine::getMostSignificantVolume()
{
    uint8_t localVol = 0;
    if (muteState == MUTE_DISABLE)
    {
        if ((trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::volume]) ||
            (trackControlParameter[(int)controlType::sequencerMode2][(int)parameterList::volume]))
        {
            localVol = currentSeqModValues.volume;
        }
        else if (stepVelocity >= 0)
        {
            localVol = (uint8_t)clamp<int>((int)stepVelocity,
                                           (int)MIN_INSTRUMENT_VOLUME,
                                           (int)MAX_INSTRUMENT_VOLUME);
        }
        else
        {
            localVol = mtProject.instrument[currentInstrument_idx].volume;
        }

        if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::volume])
        {
            localVol = (uint8_t)clamp<int>((int)localVol + (int)performanceMod.volume,
                                           (int)MIN_INSTRUMENT_VOLUME,
                                           (int)MAX_INSTRUMENT_VOLUME);
        }
    }

    return localVol;
}

void playerEngine::initEnvelopesParameters(uint8_t n, envelopeGenerator::strEnv* env)
{
    if (n == envAmp)
    {
        for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
        {
            if (env->enable)
            {
                playMemPtr[channel]->envelopeSetPassFlag(0);
            }
            else
            {
                playMemPtr[channel]->envelopeSetPassFlag(1);
            }

            playMemPtr[channel]->envelopeDelay(env->delay);
            playMemPtr[channel]->envelopeAttack(env->attack);
            playMemPtr[channel]->envelopeHold(env->hold);
            playMemPtr[channel]->envelopeDecay(env->decay);
            playMemPtr[channel]->envelopeSustain(env->sustain);
            playMemPtr[channel]->envelopeRelease(env->release);
            playMemPtr[channel]->envelopeSetLoop(env->loop);
            playMemPtr[channel]->envelopeSetWavetable(
              env->loop,
              mtProject.instrument[currentInstrument_idx].lfo[envAmp].shape,
              mtProject.instrument[currentInstrument_idx].lfo[envAmp].wtBend,
              mtProject.instrument[currentInstrument_idx].lfo[envAmp].wtSust);
        }

        uint8_t localVol = getMostSignificantVolume();

        setRefreshBit(VOLUME_MASK);
    }
    else
    {
        envelopePtr[n]->init(env);
        envelopePtr[n]->setWavetableParams(
          mtProject.instrument[currentInstrument_idx].lfo[n].shape,
          mtProject.instrument[currentInstrument_idx].lfo[n].wtBend,
          mtProject.instrument[currentInstrument_idx].lfo[n].wtSust);
    }
}

AUDIO_CORE_RAM bool playerEngine::isActiveEnvelope(uint8_t type)
{
    return (
      (mtProject.instrument[currentInstrument_idx].envelope[type].enable) ||
      (trackControlParameter[(int)controlType::performanceMode][envelopesControlValue[type]]) ||
      (trackControlParameter[(int)controlType::sequencerMode][envelopesControlValue[type]]) ||
      (trackControlParameter[(int)controlType::sequencerMode2][envelopesControlValue[type]]));
}

void playerEngine::setSyncParamsLFO(uint8_t type)
{
    if (type == envAmp)
    {
        setSyncParamsAmpLFO();
    }
    else
    {
        uint8_t curLoop = mtProject.instrument[currentInstrument_idx].envelope[type].loop;
        if ((curLoop == 1 || curLoop == 3) ||
            (trackControlParameter[(int)controlType::performanceMode]
                                  [envelopesControlValue[type]]) ||
            (trackControlParameter[(int)controlType::sequencerMode][envelopesControlValue[type]]) ||
            (trackControlParameter[(int)controlType::sequencerMode2][envelopesControlValue[type]]))
        {

            uint8_t localRate = mtProject.instrument[currentInstrument_idx].lfo[type].speed;

            if (trackControlParameter[(int)controlType::performanceMode]
                                     [envelopesControlValue[type]])
            {
                switch (type)
                {
                    case envCutoff: localRate = currentPerformanceValues.lfoCutoffRate; break;
                    case envGranPos:
                        localRate = currentPerformanceValues.lfoGranularPositionRate;
                        break;
                    case envPan: localRate = currentPerformanceValues.lfoPanningRate; break;
                    case envWtPos:
                        localRate = currentPerformanceValues.lfoWavetablePositionRate;
                        break;
                    case envFinetune: localRate = currentPerformanceValues.lfoFinetuneRate; break;
                }
            }
            else if ((trackControlParameter[(int)controlType::sequencerMode]
                                           [envelopesControlValue[type]]) ||
                     (trackControlParameter[(int)controlType::sequencerMode2]
                                           [envelopesControlValue[type]]))
            {
                switch (type)
                {
                    case envCutoff: localRate = currentSeqModValues.lfoCutoffRate; break;
                    case envGranPos: localRate = currentSeqModValues.lfoGranularPositionRate; break;
                    case envPan: localRate = currentSeqModValues.lfoPanningRate; break;
                    case envWtPos: localRate = currentSeqModValues.lfoWavetablePositionRate; break;
                    case envFinetune: localRate = currentSeqModValues.lfoFinetuneRate; break;
                }
            }

            envelopePtr[type]->setSyncRate(tempoSyncRatesOthers[(localRate >= ENVELOPE_SPEED_OTHER_COUNT) ? 0 : localRate]);
            envelopePtr[type]->setSyncStartStep(sequencer.getActualPos());
            envelopePtr[type]->setWavetableParams(
              mtProject.instrument[currentInstrument_idx].lfo[type].shape,
              mtProject.instrument[currentInstrument_idx].lfo[type].wtBend,
              mtProject.instrument[currentInstrument_idx].lfo[type].wtSust);

            /*			Magiczne liczby:
                        2 - attack,
                        3 - hold,
                        4 - decay,
                        6 - release
            */

            switch (mtProject.instrument[currentInstrument_idx].lfo[type].shape)
            {
                case (int)lfoShapeType::lfoShapeSaw:
                    envelopePtr[type]->setPhaseNumbers(2, -1);
                    break;
                case (int)lfoShapeType::lfoShapeReverseSaw:
                    envelopePtr[type]->setPhaseNumbers(4, -1);
                    break;
                case (int)lfoShapeType::lfoShapeTriangle:
                    envelopePtr[type]->setPhaseNumbers(2, 4);
                    break;
                case (int)lfoShapeType::lfoShapeSquare:
                    envelopePtr[type]->setPhaseNumbers(3, 6);
                    break;
                case (int)lfoShapeType::lfoShapeRandom:
                    envelopePtr[type]->setPhaseNumbers(3, -1);
                    break;
                default: break;
            }
        }
    }
}

void playerEngine::setSyncParamsAmpLFO()
{
    uint8_t curAmpLoop = mtProject.instrument[currentInstrument_idx].envelope[envAmp].loop;
    if ((curAmpLoop == 1 || curAmpLoop == 3) ||
        (trackControlParameter[(int)controlType::performanceMode][envAmp]) ||
        (trackControlParameter[(int)controlType::sequencerMode][envAmp]) ||
        (trackControlParameter[(int)controlType::sequencerMode2][envAmp]))
    {

        uint8_t localRate = mtProject.instrument[currentInstrument_idx].lfo[envAmp].speed;

        if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::lfoAmp])
        {
            localRate = currentPerformanceValues.lfoAmpRate;
        }
        else if ((trackControlParameter[(int)controlType::sequencerMode]
                                       [(int)parameterList::lfoAmp]) ||
                 (trackControlParameter[(int)controlType::sequencerMode2]
                                       [(int)parameterList::lfoAmp]))
        {
            localRate = currentSeqModValues.lfoAmpRate;
        }

        for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
        {
            playMemPtr[channel]->envelopeSetSyncRate(tempoSyncRatesAmp[(localRate >= ENVELOPE_SPEED_AMP_COUNT) ? 0 : localRate]);
            playMemPtr[channel]->envelopeSetSyncStartStep(sequencer.getActualPos());
            playMemPtr[channel]->envelopeSetWavetable(
              mtProject.instrument[currentInstrument_idx].envelope[envAmp].loop,
              mtProject.instrument[currentInstrument_idx].lfo[envAmp].shape,
              mtProject.instrument[currentInstrument_idx].lfo[envAmp].wtBend,
              mtProject.instrument[currentInstrument_idx].lfo[envAmp].wtSust);

            /*			Magiczne liczby:
                        2 - attack,
                        3 - hold,
                        4 - decay,
                        6 - release
            */
            switch (mtProject.instrument[currentInstrument_idx].lfo[envAmp].shape)
            {
                case (int)lfoShapeType::lfoShapeSaw:
                    playMemPtr[channel]->envelopeSetPhaseNumbers(2, -1);
                    break;
                case (int)lfoShapeType::lfoShapeReverseSaw:
                    playMemPtr[channel]->envelopeSetPhaseNumbers(4, -1);
                    break;
                case (int)lfoShapeType::lfoShapeTriangle:
                    playMemPtr[channel]->envelopeSetPhaseNumbers(2, 4);
                    break;
                case (int)lfoShapeType::lfoShapeSquare:
                    playMemPtr[channel]->envelopeSetPhaseNumbers(3, 6);
                    break;
                case (int)lfoShapeType::lfoShapeRandom:
                    playMemPtr[channel]->envelopeSetPhaseNumbers(3, -1);
                    break;
                default: break;
            }
        }
    }
}
//****************** FUNKCJE NARZUCAJĄCE WARTOSCI W SILNIKU
// Przed wywolaniem tych funkcji nalezy przygotowac wartosci:
// instrumentu, i bieżące z sequencera
// BIT DEPTH
void playerEngine::setFxBitDepth()
{
    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::bitDepth])
    {
        changeBitDepthPerformanceMode(performanceMod.bitDepth);
    }
    else
    {
        modBitDepth(currentSeqModValues.bitDepth);
    }
}
void playerEngine::clearFxBitDepth()
{
    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::bitDepth])
    {
        changeBitDepthPerformanceMode(performanceMod.bitDepth);
    }
    else
    {
        modBitDepth(mtProject.instrument[currentInstrument_idx].bitDepth);
    }
}
// FINETUNE
void playerEngine::setFxFinetune()
{
    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setFineTuneForceFlag();
        playMemPtr[channel]->setForcedFineTune(currentSeqModValues.fineTune);
        playMemPtr[channel]->setFineTune(currentSeqModValues.fineTune, currentNote);
    }
}
void playerEngine::clearFxFinetune()
{
    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->clearFineTuneForceFlag();
        playMemPtr[channel]->setFineTune(mtProject.instrument[currentInstrument_idx].fineTune,
                                         currentNote);
    }
}
// GLIDE
void playerEngine::setFxGlide()
{
    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setGlideForceFlag();
        playMemPtr[channel]->setForcedGlide(currentSeqModValues.glide);
    }
}
void playerEngine::clearFxGlide()
{
    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->clearGlideForceFlag();
        playMemPtr[channel]->setForcedGlide(0);
    }
}
// REVERSE
void playerEngine::setFxReverse()
{
    if (trackControlParameter[(int)controlType::performanceMode]
                             [(int)parameterList::samplePlaybeckDirection])
    {
        changeSamplePlaybackPerformanceMode(performanceMod.reversePlayback);
    }
    else
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
}
void playerEngine::clearFxReverse()
{
    if (trackControlParameter[(int)controlType::performanceMode]
                             [(int)parameterList::samplePlaybeckDirection])
    {
        changeSamplePlaybackPerformanceMode(performanceMod.reversePlayback);
    }
    else
    {
        for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
        {
            playMemPtr[channel]->clearReverse();
        }
    }
}
// POSITION GRANULAR
void playerEngine::setFxPositionGranular()
{
    if (trackControlParameter[(int)controlType::performanceMode]
                             [(int)parameterList::granularPosition])
    {
        changeGranularPositionPerformanceMode(performanceMod.granularPosition);
    }
    else
    {
        for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
        {
            playMemPtr[channel]->setGranularPosForceFlag();
            playMemPtr[channel]->setForcedGranularPos(currentSeqModValues.granularPosition);
            playMemPtr[channel]->setGranularPosition(currentSeqModValues.granularPosition);
        }
    }
}
void playerEngine::clearFxPositionGranular()
{
    if (trackControlParameter[(int)controlType::performanceMode]
                             [(int)parameterList::granularPosition])
    {
        changeWavetableWindowPerformanceMode(performanceMod.granularPosition);
    }
    else
    {
        uint32_t localGranPos =
          mtProject.instrument[currentInstrument_idx].granular.currentPosition;

        int32_t localGranMod = currentEnvelopeModification[envGranPos] * MAX_16BIT;

        if (localGranPos + localGranMod > MAX_16BIT)
            localGranPos = MAX_16BIT;
        else if (localGranPos + localGranMod < 0)
            localGranPos = 0;
        else
            localGranPos += localGranMod;

        for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
        {
            playMemPtr[channel]->clearGranularPosForceFlag();
            playMemPtr[channel]->setGranularPosition(localGranPos);
        }
    }
}
// GRANULAR LENGTH
void playerEngine::setFxGranularLength()
{
    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setGranularLenForceFlag();
        playMemPtr[channel]->setForcedGranularLen(currentSeqModValues.granularLength);
        playMemPtr[channel]->setGranularGrainLength(currentSeqModValues.granularLength);
    }
}
void playerEngine::clearFxGranularLength()
{
    uint32_t sampleLen = mtProject.instrument[currentInstrument_idx].sample.length;
    uint32_t maxGrainLen = (sampleLen > 0 && sampleLen < 44100u) ? sampleLen : 44100u;
    if (maxGrainLen < 44u)
        maxGrainLen = (sampleLen > 0) ? sampleLen : 44u;

    int32_t localGranLen =
      static_cast<int32_t>(mtProject.instrument[currentInstrument_idx].granular.grainLength);
    int32_t localGranLenMod = static_cast<int32_t>(
      currentEnvelopeModification[envGranLen] * static_cast<float>(maxGrainLen));

    int32_t minGrainLen = (sampleLen > 0 && sampleLen < 44u) ? static_cast<int32_t>(sampleLen) : 44;
    if (localGranLen + localGranLenMod > static_cast<int32_t>(maxGrainLen))
        localGranLen = static_cast<int32_t>(maxGrainLen);
    else if (localGranLen + localGranLenMod < minGrainLen)
        localGranLen = minGrainLen;
    else
        localGranLen += localGranLenMod;

    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->clearGranularLenForceFlag();
        playMemPtr[channel]->setGranularGrainLength(static_cast<uint16_t>(localGranLen));
    }
}
// POSITION WAVETABLE
void playerEngine::setFxPositionWavetable()
{
    if (trackControlParameter[(int)controlType::performanceMode]
                             [(int)parameterList::wavetablePosition])
    {
        changeWavetableWindowPerformanceMode(performanceMod.wavetablePosition);
    }
    else
    {
        for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
        {
            playMemPtr[channel]->setWavetableWindowForceFlag();
            playMemPtr[channel]->setForcedWavetableWindow(currentSeqModValues.wavetablePosition);
            playMemPtr[channel]->setWavetableWindow(currentSeqModValues.wavetablePosition);
        }
    }
}
void playerEngine::clearFxPositionWavetable()
{
    if (trackControlParameter[(int)controlType::performanceMode]
                             [(int)parameterList::wavetablePosition])
    {
        changeWavetableWindowPerformanceMode(performanceMod.wavetablePosition);
    }
    else
    {
        uint32_t localWtPos = mtProject.instrument[currentInstrument_idx].wavetableCurrentWindow;
        int32_t localWtMod =
          currentEnvelopeModification[envWtPos] *
          mtProject.instrument[currentInstrument_idx].sample.wavetableWindowNumber;

        if (localWtPos + localWtMod >
            mtProject.instrument[currentInstrument_idx].sample.wavetableWindowNumber - 1)
            localWtPos =
              mtProject.instrument[currentInstrument_idx].sample.wavetableWindowNumber - 1;
        else if (localWtPos + localWtMod < 0)
            localWtPos = 0;
        else
            localWtPos += localWtMod;

        for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
        {
            playMemPtr[channel]->clearWavetableWindowForceFlag();
            playMemPtr[channel]->setWavetableWindow(localWtPos);
        }
    }
}
// TUNE
void playerEngine::setFxTune()
{
    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::tune])
    {
        changeTunePerformanceMode(performanceMod.tune);
    }
    else
    {
        for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
        {
            playMemPtr[channel]->setTuneForceFlag();
            playMemPtr[channel]->setForcedTune(currentSeqModValues.tune);
            playMemPtr[channel]->setTune(currentSeqModValues.tune, currentNote);
        }
    }
}
void playerEngine::clearFxTune()
{
    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::tune])
    {
        changeTunePerformanceMode(performanceMod.tune);
    }
    else
    {
        for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
        {
            playMemPtr[channel]->clearTuneForceFlag();
            playMemPtr[channel]->setTune(mtProject.instrument[currentInstrument_idx].tune,
                                         currentNote);
        }
    }
}
// START POINT
void playerEngine::setFxStartPoint(uint8_t fx_n)
{
    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::startPoint])
    {
        changeStartPointPerformanceMode(performanceMod.startPoint);
    }
    else
    {
        //		if(trackControlParameter[(int)controlType::sequencerMode +
        // otherFx_n][(int)parameterList::endPoint]) modSeqPoints(currentSeqModValues.startPoint,
        // currentSeqModValues.endPoint); 		else modSeqPoints(currentSeqModValues.startPoint ,
        // NOT_MOD_POINTS);
        modSeqPoints(currentSeqModValues.startPoint, SKIP_MODIFICATION_THIS_VALUE, fx_n);
    }
}
void playerEngine::clearFxStartPoint()
{
    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::startPoint])
    {
        changeStartPointPerformanceMode(performanceMod.startPoint);
    }
    else
    {
        for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
        {
            playMemPtr[channel]->clearPointsForceFlag();
            playMemPtr[channel]->setForcedPoints(-1, -1, -1, -1);
        }
    }

    //	if(trackControlParameter[(int)controlType::sequencerMode +
    // otherFx_n][(int)parameterList::endPoint])
    //	{
    //		modSeqPoints(NOT_MOD_POINTS,currentSeqModValues.endPoint);
    //	}
}
// SLICE
void playerEngine::setFxSlice()
{
    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setSliceForcedFlag();
        playMemPtr[channel]->setForcedSlice(currentSeqModValues.slice);
    }
}
void playerEngine::clearFxSlice()
{
    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->clearSliceForcedFlag();
    }
}
// OVERDRIVE
void playerEngine::setFxOverdrive()
{
    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::overdrive])
    {
        changeOverdrivePerformanceMode(performanceMod.overdrive);
    }
    else
    {
        modOverdrive(currentSeqModValues.overdrive);
    }
}
void playerEngine::clearFxOverdrive()
{
    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::overdrive])
    {
        changeOverdrivePerformanceMode(performanceMod.overdrive);
    }
    else
    {
        modOverdrive(mtProject.instrument[currentInstrument_idx].overdrive);
    }
}
// FILTER
void playerEngine::setFxCutoff()
{
    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::filterCutoff])
    {
        changeCutoffPerformanceMode(performanceMod.cutoff);
    }
    else
    {
        if (!isActiveEnvelope(envCutoff))
            modCutoff(currentSeqModValues.filterCutoff);
        filterConnect();
    }
}
void playerEngine::clearFxCutoff()
{
    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::filterCutoff])
    {
        changeCutoffPerformanceMode(performanceMod.cutoff);
    }
    else
    {
        float localCutoff = mtProject.instrument[currentInstrument_idx].cutOff;

        if (currentEnvelopeModification[envCutoff] != 0.0f)
        {
            if (localCutoff + currentEnvelopeModification[envCutoff] > 1.0f)
                localCutoff = 1.0;
            else if (localCutoff + currentEnvelopeModification[envCutoff] < 0.0f)
                localCutoff = 0.0;
            else
                localCutoff += currentEnvelopeModification[envCutoff];
        }

        if (!isActiveEnvelope(envCutoff))
            modCutoff(localCutoff);

        if (!mtProject.instrument[currentInstrument_idx].filterEnable)
            filterDisconnect();
        else
            filterConnect();
    }
}

void playerEngine::setFxFilterType()
{
    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::filterType])
    {
        changeFilterTypePerformanceMode(performanceMod.filterType);
    }
    else
    {
        changeFilterType(currentSeqModValues.filterType);
    }
}
void playerEngine::clearFxFilterType()
{
    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::filterType])
    {
        changeFilterTypePerformanceMode(performanceMod.filterType);
    }
    else
    {
        changeFilterType(mtProject.instrument[currentInstrument_idx].filterType);
    }
}

void playerEngine::setFxAmpRateLFO()
{
    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::lfoAmp])
    {
        changeAmpLfoRatePerformanceMode(performanceMod.lfoAmpRate);
    }
    else
    {
        uint8_t curAmpLoop = mtProject.instrument[currentInstrument_idx].envelope[envAmp].loop;
        uint8_t targetLoop = (curAmpLoop == 3) ? 3 : 1;
        calcLfoBasedEnvelope(&lfoBasedEnvelope[envAmp],
                             &mtProject.instrument[currentInstrument_idx].lfo[envAmp],
                             currentSeqModValues.lfoAmpRate,
                             1,
                             targetLoop);
        initEnvelopesParameters(envAmp, &lfoBasedEnvelope[envAmp]);
    }
}

void playerEngine::clearFxAmpRateLFO()
{
    if (trackControlParameter[(int)controlType::performanceMode][(int)parameterList::lfoAmp])
    {
        changeAmpLfoRatePerformanceMode(performanceMod.lfoAmpRate);
    }
    else
    {
        if (mtProject.instrument[currentInstrument_idx].envelope[envAmp].enable)
        {
            uint8_t curAmpLoop = mtProject.instrument[currentInstrument_idx].envelope[envAmp].loop;
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

                for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
                {
                    if (playMemPtr[channel]->envelopeGetState() != 0)
                    {
                        playMemPtr[channel]->envelopeSetSustain();
                    }
                }
            }
        }
        else
        {
            initEnvelopesParameters(envAmp, (envelopeGenerator::strEnv*)&passEnvelope);

            for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
            {
                if (playMemPtr[channel]->envelopeGetState() != 0)
                {
                    playMemPtr[channel]->envelopeSetSustain();
                }
            }
        }
    }
}

void playerEngine::syncFxAmpLFO()
{
    bool isAmpRandom =
      (mtProject.instrument[currentInstrument_idx].envelope[envAmp].loop == 1) &&
      (mtProject.instrument[currentInstrument_idx].lfo[envAmp].shape == lfoShapeRandom);
    const int activeChannels = voiceIsStereo ? (int)playMemPtr.size() : 1;
    for (int channel = 0; channel < activeChannels; channel++)
    {
        playMemPtr[channel]->envelopeNoteOff();
        playMemPtr[channel]->envelopeSetIdle();
        playMemPtr[channel]->envelopeSetIsRandom(isAmpRandom);
        playMemPtr[channel]->envelopeNoteOn(currentInstrument_idx, currentNote);
    }
    if (!voiceIsStereo)
    {
        playMemPtr[1]->stop();
    }
}
