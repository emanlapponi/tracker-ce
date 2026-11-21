#include "play_memory.h"
#include "Audio.h"
#include "ampLogarythmicValues.h"
#include "dspinst.h"
#include "mtAudioEngine.h"
#include "util_math.h"
#include <cr_section_macros.h>

float AudioPlayMemory::notes[NOTE_LOOKUP_SIZE];
float AudioPlayMemory::wt32Note[NOTE_LOOKUP_SIZE];
float AudioPlayMemory::wt64Note[NOTE_LOOKUP_SIZE];
float AudioPlayMemory::wt128Note[NOTE_LOOKUP_SIZE];
float AudioPlayMemory::wt256Note[NOTE_LOOKUP_SIZE];
float AudioPlayMemory::wt512Note[NOTE_LOOKUP_SIZE];
float AudioPlayMemory::wt1024Note[NOTE_LOOKUP_SIZE];
float AudioPlayMemory::wt2048Note[NOTE_LOOKUP_SIZE];

bool AudioPlayMemory::isHeadroomEnabled = false;
bool AudioPlayMemory::isSnapToZeroEnabled = false;

// Keep note + tune lookups inside the note table across note-on, glide, tune, and fine tune
// paths, computing in int before clamping so sums >= 128 do not wrap in int8_t.
static int tunedNoteIdx(int note, int tune)
{
    return clamp<int>(note + tune, MIN_NOTE, MAX_NOTE - 1);
}

void AudioPlayMemory::initNotesLookup(bool applyClockCorrection)
{
    constexpr int kMiddleNoteIdx = 60; // C5
    constexpr int kA4NoteIdx = 57;
    constexpr float kWtExpectedFreqA4 = 440.0f; // A4
    const float kWtExpectedFreqMiddleNote =
      kWtExpectedFreqA4 * powf(2.0f, (kMiddleNoteIdx - kA4NoteIdx) / 12.0f);

    constexpr int kWtSizesCount = 7;
    constexpr float kWtSizes[kWtSizesCount] = { 2048.0f, 1024.0f, 512.0f, 256.0f,
                                                128.0f,  64.0f,   32.0f };
    float* kWtTables[kWtSizesCount] = { wt2048Note, wt1024Note, wt512Note, wt256Note,
                                        wt128Note,  wt64Note,   wt32Note };
    float kWtNaturalFreq[kWtSizesCount];
    float kWtPitchMiddleNote[kWtSizesCount];

    for (int wtFreqIdx = 0; wtFreqIdx < kWtSizesCount; wtFreqIdx++)
    {
        kWtNaturalFreq[wtFreqIdx] = 44100.0f / kWtSizes[wtFreqIdx];
        kWtPitchMiddleNote[wtFreqIdx] = kWtExpectedFreqMiddleNote / kWtNaturalFreq[wtFreqIdx];
    }

    const float noteCorrection = applyClockCorrection ? NOTE_CORRECTION_COEFFICIENT : 1.0f;

    for (int noteIdx = 0; noteIdx < NOTE_LOOKUP_SIZE; noteIdx++)
    {
        notes[noteIdx] =
          powf(2.0f, (noteIdx - kMiddleNoteIdx) / 12.0f) * noteCorrection;

        for (int wtFreqIdx = 0; wtFreqIdx < kWtSizesCount; wtFreqIdx++)
        {
            float* wtTable = kWtTables[wtFreqIdx];
            wtTable[noteIdx] = kWtPitchMiddleNote[wtFreqIdx] * notes[noteIdx];
        }
    }
}

AUDIO_CORE_FLASH void AudioPlayMemory::enableHeadroom(bool enabled)
{
    isHeadroomEnabled = enabled;
}

AUDIO_CORE_FLASH void AudioPlayMemory::enableSnapToZero(bool enabled)
{
    isSnapToZeroEnabled = enabled;
}

static uint32_t getMaxReadableFramesForAddress(const int16_t* sampleAddress, uint32_t sampleLength)
{
    if (sampleAddress == nullptr)
    {
        return 0;
    }
    for (size_t i = 0; i < kSampleInstrumentsCount; ++i)
    {
        if (mtProject.sampleMemory[i].isStreamed &&
            (mtProject.sampleMemory[i].address[0] == sampleAddress ||
             mtProject.sampleMemory[i].address[1] == sampleAddress))
        {
            return mtProject.sampleMemory[i].preCacheFrames;
        }
    }
    return sampleLength;
}

AUDIO_CORE_FLASH bool AudioPlayMemory::isZeroInPointZone(uint16_t position,
                                                         uint32_t sampleLength,
                                                         int16_t* sampleAddress)
{
    uint32_t maxReadable = getMaxReadableFramesForAddress(sampleAddress, sampleLength);
    if (maxReadable == 0)
    {
        return false;
    }

    if ((bool)isSnapToZeroEnabled == false)
    {
        uint32_t sampleIdx = (uint32_t)((float)position * ((float)sampleLength / MAX_16BIT));
        if (sampleIdx >= maxReadable)
        {
            return false;
        }

        return sampleAddress[sampleIdx] == 0;
    }

    auto [lowerAddress, initialAddress, upperAddress] =
      calculatePositionZoneAddresses(position, sampleLength, sampleAddress);

    (void)initialAddress;

    if ((uint32_t)(upperAddress - sampleAddress) >= maxReadable)
    {
        return false;
    }

    auto detectZeroCrossing = [sampleAddress, maxReadable](int16_t& value)
    { return isSampleInZeroCrossing(value, sampleAddress, sampleAddress + maxReadable); };

    int16_t* result = std::find_if(lowerAddress, upperAddress + 1, detectZeroCrossing);

    return result != upperAddress + 1;
}

//**************************************************************************************UPDATE START
AUDIO_CORE_RAM void AudioPlayMemory::update(void)
{
    if (isBlockedInstrument[currentInstrIdx])
        return;

    if (envelope.state == envelopePhaseIdle && !stackedPlay.enable)
    {
        if (playing)
        {
            stop();
        }
        envelopeUpdate(nullptr);
        return;
    }

    audio_block_t* block = nullptr;
    switch (currentPlayMode)
    {
        case playModeSingleShot: block = updateSingleShot(); break;
        case playModeLoopForward: block = updateLoopForward(); break;
        case playModeLoopBackward: block = updateLoopBackward(); break;
        case playModePingpong: block = updateLoopPingpong(); break;
        case playModeSlice: block = updateSlice(); break;
        case playModeBeatSlice: block = updateBeatSlice(); break;
        case playModeGranular: block = updateGranular(); break;
        case playModeWavetable: block = updateWavetable(); break;
        default: break;
    }

    if (isHeadroomEnabled)
    {
        lowerVolume3dB(block);
    }

    envelopeUpdate(block);
}

AUDIO_CORE_RAM void AudioPlayMemory::lowerVolume3dB(audio_block_t* block)
{
    if (nullptr == block)
    {
        return;
    }

    int32_t* data = (int32_t*)block->data;
    for (int idx = 0; idx < AUDIO_BLOCK_SAMPLES / 2; idx++)
    {
        data[idx] = signed_halving_add_16_and_16(data[idx], 0);
    }
}

void AudioPlayMemory::init(Timer* timer, int channel, int voiceIdx)
{
    AudioPlayMemory::timer = timer;
    AudioPlayMemory::channel = channel;
    AudioPlayMemory::voiceIndex = (int8_t)voiceIdx;
}

//**************************************************************************************UPDATE END
//**************************************************************************************PLAY
AUDIO_CORE_FLASH uint8_t AudioPlayMemory::play(uint8_t instrIdx, int8_t note)
{
    // The play functions limit the note at the top only, and index the note table with it.
    if (note < MIN_NOTE)
        note = MIN_NOTE;

    switch (mtProject.instrument[instrIdx].playMode)
    {
        case playModeSingleShot: playSingleShot(instrIdx, note); break;
        case playModeLoopForward: playLoopForward(instrIdx, note); break;
        case playModeLoopBackward: playLoopBackward(instrIdx, note); break;
        case playModePingpong: playLoopPingpong(instrIdx, note); break;
        case playModeSlice: playSlice(instrIdx, note); break;
        case playModeBeatSlice: playBeatSlice(instrIdx, note); break;
        case playModeGranular: playGranular(instrIdx, note); break;
        case playModeWavetable: playWavetable(instrIdx, note); break;
        default: return 0;
    }

    if (stackedSlide.isSlideOnPlay)
    {
        setSlidePitch(stackedSlide.upOrDown, stackedSlide.dstValue, stackedSlide.time);
        stackedSlide.isSlideOnPlay = false;
    }

    return 1;
}

AUDIO_CORE_FLASH void AudioPlayMemory::setSlideOnPlay(bool upOrDown, uint8_t dstValue, float time)
{
    stackedSlide.isSlideOnPlay = true;
    stackedSlide.upOrDown = upOrDown;
    stackedSlide.dstValue = dstValue;
    stackedSlide.time = time;
}

uint8_t AudioPlayMemory::playForPrev(uint8_t instr_idx, int8_t n)
{
    tuneForceFlag = 0;
    pointsForceFlag = 0;
    glideForceFlag = 0;
    fineTuneForceFlag = 0;
    wavetableWindowForceFlag = 0;
    sliceForcedFlag = 0;
    granularForcedFlag = 0;
    reverseDirectionFlag = 0;
    constrainsInSamples.pitchSmoothing = 0;

    play(instr_idx, n);

    lastNote = -1;
    return 1;
}

uint8_t AudioPlayMemory::playForPrev(int16_t* addr, uint32_t len, uint8_t type)
{
    uint32_t startPoint, endPoint;
    int8_t note = 60;
    playing = 0;
    loopBackwardFlag = 0;
    iPitchCounter = 0;
    fPitchCounter = 0;
    pitchSmoothingControlCounter = 0;

    currentGlide = 0;
    currentTune = 0;
    lastNote = -1;

    tuneForceFlag = 0;
    pointsForceFlag = 0;
    glideForceFlag = 0;
    fineTuneForceFlag = 0;
    wavetableWindowForceFlag = 0;
    sliceForcedFlag = 0;
    granularForcedFlag = 0;
    reverseDirectionFlag = 0;
    constrainsInSamples.pitchSmoothing = 0;

    currentPlayMode = playModeSingleShot;

    currentSampleLength = len;
    currentStartAddress = addr;
    currentIsStreamed = false;
    currentPreCacheFrames = 0;
    if (channel == 0 && voiceIndex >= 0)
    {
        voiceStreamManager.notifyNoteOff((uint8_t)voiceIndex);
    }
    if ((note + currentTune) > (MAX_NOTE - 1))
    {
        if (lastNote > note)
            currentTune = (MAX_NOTE - 1) - lastNote;
        else
            currentTune = (MAX_NOTE - 1) - note;
    }
    if ((note + currentTune) < MIN_NOTE)
    {
        if ((lastNote >= 0) && (lastNote < note))
            currentTune = MIN_NOTE - lastNote;
        else
            currentTune = MIN_NOTE - note;
    }

    if (currentPlayMode == playModeWavetable)
    {
        switch (mtProject.instrument[currentInstrIdx].sample.wavetable_window_size)
        {
            case 32: wt_notes = wt32Note; break;
            case 64: wt_notes = wt64Note; break;
            case 128: wt_notes = wt128Note; break;
            case 256: wt_notes = wt256Note; break;
            case 512: wt_notes = wt512Note; break;
            case 1024: wt_notes = wt1024Note; break;
            case 2048: wt_notes = wt2048Note; break;
            default: break;
        }
    }
    if (lastNote >= 0)
        pitchControl = (currentPlayMode != playModeWavetable)
                         ? (float)notes[lastNote + currentTune]
                         : (float)wt_notes[lastNote + currentTune];
    else
        pitchControl = (currentPlayMode != playModeWavetable) ? (float)notes[note + currentTune]
                                                              : (float)wt_notes[note + currentTune];

    int16_t* data = addr;

    if (currentPlayMode == playModeWavetable)
    {
        wavetableWindowSize = SERUM_WAVETABLE_WINDOW_LEN;
        if (wavetableWindowForceFlag)
            currentWindow = forcedWavetableWindow;
        else
            currentWindow = 0;
        constrainsInSamples.endPoint = wavetableWindowSize * 256; // nie ma znaczenia
        constrainsInSamples.loopPoint1 = 0; // currentWindow*wavetableWindowSize;
        constrainsInSamples.loopPoint2 =
          wavetableWindowSize; // (currentWindow+1)*wavetableWindowSize;
        constrainsInSamples.loopLength = wavetableWindowSize;
        pointsInSamples.start = 0;
    }

    startPoint = 0;
    endPoint = MAX_16BIT;
    currentFineTune = 0;
    finetunePitchControl = 0;

    pointsInSamples.start =
      (uint32_t)((float)startPoint * ((float)currentSampleLength / MAX_16BIT));
    pointsInSamples.end = (uint32_t)((float)endPoint * ((float)currentSampleLength / MAX_16BIT));
    constrainsInSamples.endPoint = pointsInSamples.end - pointsInSamples.start;

    if (currentPlayMode != playModeWavetable)
    {
        next = data + pointsInSamples.start;
        length = currentSampleLength - pointsInSamples.start;
    }
    else
    {
        next = data;
        length = currentSampleLength;
    }

    playing = 1;

    return successInit;
}

uint8_t AudioPlayMemory::playForPrev(int16_t* addr, uint32_t len, uint8_t n, uint8_t type)
{
    uint32_t startPoint, endPoint;
    int8_t note = n;
    playing = 0;
    loopBackwardFlag = 0;
    iPitchCounter = 0;
    fPitchCounter = 0;
    pitchSmoothingControlCounter = 0;

    currentGlide = 0;
    currentTune = 0;
    lastNote = -1;
    currentStartAddress = addr;
    currentPlayMode = playModeSingleShot;
    currentSampleLength = len;
    currentIsStreamed = false;
    currentPreCacheFrames = 0;
    if (channel == 0 && voiceIndex >= 0)
    {
        voiceStreamManager.notifyNoteOff((uint8_t)voiceIndex);
    }

    tuneForceFlag = 0;
    pointsForceFlag = 0;
    glideForceFlag = 0;
    fineTuneForceFlag = 0;
    wavetableWindowForceFlag = 0;
    sliceForcedFlag = 0;
    granularForcedFlag = 0;
    reverseDirectionFlag = 0;

    constrainsInSamples.pitchSmoothing = 0;

    if ((note + currentTune) > (MAX_NOTE - 1))
    {
        if (lastNote > note)
            currentTune = (MAX_NOTE - 1) - lastNote;
        else
            currentTune = (MAX_NOTE - 1) - note;
    }
    if ((note + currentTune) < MIN_NOTE)
    {
        if ((lastNote >= 0) && (lastNote < note))
            currentTune = MIN_NOTE - lastNote;
        else
            currentTune = MIN_NOTE - note;
    }

    if (type == mtSampleTypeWavetable)
    {
        switch (mtProject.instrument[currentInstrIdx].sample.wavetable_window_size)
        {
            case 32: wt_notes = wt32Note; break;
            case 64: wt_notes = wt64Note; break;
            case 128: wt_notes = wt128Note; break;
            case 256: wt_notes = wt256Note; break;
            case 512: wt_notes = wt512Note; break;
            case 1024: wt_notes = wt1024Note; break;
            case 2048: wt_notes = wt2048Note; break;
            default: break;
        }
    }

    if (lastNote >= 0)
        pitchControl = (currentPlayMode != playModeWavetable)
                         ? (float)notes[lastNote + currentTune]
                         : (float)wt_notes[lastNote + currentTune];
    else
        pitchControl = (currentPlayMode != playModeWavetable) ? (float)notes[note + currentTune]
                                                              : (float)wt_notes[note + currentTune];

    int16_t* data = addr;

    startPoint = 0;
    endPoint = MAX_16BIT;
    currentFineTune = 0;
    finetunePitchControl = 0;
    if (currentPlayMode == playModeWavetable)
    {
        wavetableWindowSize = SERUM_WAVETABLE_WINDOW_LEN;
        if (wavetableWindowForceFlag)
            currentWindow = forcedWavetableWindow;
        else
            currentWindow = 0;
        constrainsInSamples.endPoint = wavetableWindowSize * 256; // nie ma znaczenia
        constrainsInSamples.loopPoint1 = 0; // currentWindow*wavetableWindowSize;
        constrainsInSamples.loopPoint2 =
          wavetableWindowSize; // (currentWindow+1)*wavetableWindowSize;
        constrainsInSamples.loopLength = wavetableWindowSize;
        pointsInSamples.start = 0;
    }

    pointsInSamples.start =
      (uint32_t)((float)startPoint * ((float)currentSampleLength / MAX_16BIT));
    pointsInSamples.end = (uint32_t)((float)endPoint * ((float)currentSampleLength / MAX_16BIT));
    constrainsInSamples.endPoint = pointsInSamples.end - pointsInSamples.start;

    if (currentPlayMode != playModeWavetable)
    {
        next = data + pointsInSamples.start;
        length = currentSampleLength - pointsInSamples.start;
    }
    else
    {
        next = data;
        length = currentSampleLength;
    }

    playing = 1;

    return successInit;
}

//*******************************************************************************************************************

void AudioPlayMemory::stop(void)
{
    //	__disable_irq();
    AudioNoInterrupts();
    playing = 0;
    if (channel == 0 && voiceIndex >= 0)
    {
        voiceStreamManager.notifyNoteOff((uint8_t)voiceIndex);
    }
    AudioInterrupts();
    //	__enable_irq();
}

void AudioPlayMemory::setWavetableWindow(int16_t value)
{
    if (mtProject.instrument[currentInstrIdx].playMode != playModeWavetable)
        return;

    if ((uint32_t)value >= mtProject.instrument[currentInstrIdx].sample.wavetableWindowNumber)
        currentWindow = mtProject.instrument[currentInstrIdx].sample.wavetableWindowNumber - 1;
    else if (value < 0)
        currentWindow = 0;
    else
        currentWindow = value;
}

void AudioPlayMemory::setLP1(
  uint16_t value) // w audio engine zadba zeby zapodac odpowiednia wartosc gdy force
{
    if (!((currentPlayMode == playModeLoopForward) || (currentPlayMode == playModeLoopBackward) ||
          (currentPlayMode == playModePingpong)))
        return;

    uint32_t lastSamplePointsLoop1 = pointsInSamples.loop1;

    if (samplePointsSource != nullptr)
    {
        pointsInSamples.loop1 = samplePointsSource->pointsInSamples.loop1;
    }
    else
    {
        pointsInSamples.loop1 = calculatePointPositionAndSnapToZero(
          value, (uint32_t)currentSampleLength, (int16_t*)currentStartAddress);
    }

    if ((pointsInSamples.loop1 < pointsInSamples.start) ||
        (pointsInSamples.loop1 > pointsInSamples.loop2) ||
        (pointsInSamples.loop1 > pointsInSamples.end))
    {
        pointsInSamples.loop1 = lastSamplePointsLoop1;
        return;
    }

    constrainsInSamples.loopPoint1 = pointsInSamples.loop1 - pointsInSamples.start;
    constrainsInSamples.loopLength = pointsInSamples.loop2 - pointsInSamples.loop1;

    //	next = mtProject.instrument[currentInstr_idx].sample.address + samplePoints.start;
}
void AudioPlayMemory::setLP2(
  uint16_t value) // w audio engine zadba zeby zapodac odpowiednia wartosc gdy force
{
    if (!((currentPlayMode == playModeLoopForward) || (currentPlayMode == playModeLoopBackward) ||
          (currentPlayMode == playModePingpong)))
        return;

    uint32_t lastSamplePointsLoop2 = pointsInSamples.loop2;

    if (samplePointsSource != nullptr)
    {
        pointsInSamples.loop2 = samplePointsSource->pointsInSamples.loop2;
    }
    else
    {
        pointsInSamples.loop2 = calculatePointPositionAndSnapToZero(
          value, (uint32_t)currentSampleLength, (int16_t*)currentStartAddress);
    }

    if (pointsInSamples.loop2 >= pointsInSamples.end)
    {
        assert(pointsInSamples.end != 0);
        pointsInSamples.loop2 = pointsInSamples.end - 1;
    }

    if ((pointsInSamples.loop2 < pointsInSamples.start) ||
        (pointsInSamples.loop2 < pointsInSamples.loop1) ||
        (pointsInSamples.loop1 > pointsInSamples.end))
    {
        pointsInSamples.loop2 = lastSamplePointsLoop2;
        return;
    }

    constrainsInSamples.loopPoint2 = pointsInSamples.loop2 - pointsInSamples.start;
    constrainsInSamples.loopLength = pointsInSamples.loop2 - pointsInSamples.loop1;

    //	int16_t * lastNext = next;
    //	next = mtProject.instrument[currentInstr_idx].sample.address + samplePoints.start;
    //	int32_t localShift = (int32_t)(lastNext - next);
    //	iPitchCounter += localShift;
}

void AudioPlayMemory::setGlide(uint16_t value, int8_t currentNote, uint8_t instr_idx)
{
    uint32_t localSampleConstrainsGlide = (uint32_t)(value * 44.1);
    int8_t tune = mtProject.instrument[instr_idx].tune;
    float localGlideControl = ((float)notes[tunedNoteIdx(currentNote, tune)] -
                               (float)notes[tunedNoteIdx(lastNote, tune)]) /
                              localSampleConstrainsGlide;
    AudioNoInterrupts();
    constrainsInSamples.pitchSmoothing = localSampleConstrainsGlide;
    if ((lastNote >= 0) && (lastNote != currentNote))
        pitchSmoothingControl = localGlideControl;
    else
        pitchSmoothingControl = 0;
    AudioInterrupts();
}

void AudioPlayMemory::setFineTune(int8_t value, int8_t currentNote)
{
    if (currentPlayMode == playModeBeatSlice)
        currentNote = 60;

    float localPitchControl = pitchControl;
    float localFinetunePitchControl = finetunePitchControl;
    const float* localNoteTable =
      (mtProject.instrument[currentInstrIdx].playMode == playModeWavetable) ? wt_notes : notes;
    int noteAndTuneIdx = tunedNoteIdx(currentNote, currentTune);

    localPitchControl -= localFinetunePitchControl;

    if (value >= 0)
    {
        bool isNotExceedUpperNoteRange = ((noteAndTuneIdx + 1) < MAX_NOTE);

        if (isNotExceedUpperNoteRange)
        {
            float maxFinetuneShiftPitch = localNoteTable[noteAndTuneIdx + 1];
            float currentNotePitch = localNoteTable[noteAndTuneIdx];

            localFinetunePitchControl =
              value * ((maxFinetuneShiftPitch - currentNotePitch) / MAX_INSTRUMENT_FINETUNE);
        }
        else
            localFinetunePitchControl = 0;
    }
    else
    {
        bool isNotExceedLowerNoteRange = ((noteAndTuneIdx - 1) >= MIN_NOTE);

        if (isNotExceedLowerNoteRange)
        {
            float maxFinetuneShiftPitch = localNoteTable[noteAndTuneIdx - 1];
            float currentNotePitch = localNoteTable[noteAndTuneIdx];

            localFinetunePitchControl =
              (-value) * ((maxFinetuneShiftPitch - currentNotePitch) / MAX_INSTRUMENT_FINETUNE);
        }
        else
            localFinetunePitchControl = 0;
    }

    localPitchControl += localFinetunePitchControl;

    AudioNoInterrupts();
    pitchControl = localPitchControl;
    finetunePitchControl = localFinetunePitchControl;
    currentFineTune = value;
    AudioInterrupts();
}

void AudioPlayMemory::refreshGranularPosition()
{

    if (mtProject.instrument[currentInstrIdx].playMode != playModeGranular)
    {
        granularPositionRefreshFlag = 0;
        return;
    }

    uint16_t grainLengthToUse =
      (currentGranularLength >= MIN_GRANULAR_LENGTH)
        ? currentGranularLength
        : (granularLengthForcedFlag ? forcedGranularLength
                                    : mtProject.instrument[currentInstrIdx].granular.grainLength);

    auto [startGranular, loopPoint1Granular, loopPoint2Granular, endGranular] =
      calculateGranularPoints(currentGranularPosition,
                              grainLengthToUse,
                              mtProject.instrument[currentInstrIdx].sample.length);

    pointsInSamples.start =
      (uint32_t)((float)startGranular * ((float)currentSampleLength / MAX_16BIT));
    pointsInSamples.end = (uint32_t)((float)endGranular * ((float)currentSampleLength / MAX_16BIT));

    pointsInSamples.loop1 =
      (uint32_t)((float)loopPoint1Granular * ((float)currentSampleLength / MAX_16BIT));
    pointsInSamples.loop2 =
      (uint32_t)((float)loopPoint2Granular * ((float)currentSampleLength / MAX_16BIT));

    length = currentSampleLength - pointsInSamples.start;

    constrainsInSamples.loopPoint1 = pointsInSamples.loop1 - pointsInSamples.start;
    constrainsInSamples.loopPoint2 = pointsInSamples.loop2 - pointsInSamples.start;
    constrainsInSamples.loopLength = pointsInSamples.loop2 - pointsInSamples.loop1;
    constrainsInSamples.endPoint = pointsInSamples.end - pointsInSamples.start;

    granularPositionRefreshFlag = 0;
}

std::tuple<uint16_t, uint16_t, uint16_t, uint16_t> AudioPlayMemory::calculateGranularPoints(
  uint16_t granularPosition,
  uint16_t grainLength,
  uint32_t sampleLength)
{
    // An empty instrument has no sample to measure the grain against.
    // Keep the quotient in uint32_t before clamping so grainLength > sampleLength (e.g. a sample
    // shorter than MIN_GRANULAR_LENGTH) saturates at uint16_t::max() instead of wrapping.
    // The loop points lie one unit inside the zone on either side. A zone of fewer than four
    // units, which is all that a short grain of a long sample comes to, leaves a loop without
    // length or with its points swapped.
    constexpr uint32_t kMinGranularLength = 4;
    constexpr uint32_t kMaxGranularLength = std::numeric_limits<uint16_t>::max();
    uint32_t rawGranularLength =
      sampleLength > 0 ? ((uint32_t)grainLength * kMaxGranularLength) / sampleLength : 0;
    uint16_t granularLength =
      (uint16_t)clamp<uint32_t>(rawGranularLength, kMinGranularLength, kMaxGranularLength);

    int upperZoneLength = granularLength / 2 + (granularLength / 2 - std::min((int)granularPosition,
                                                                              granularLength / 2));
    int lowerZoneLength =
      granularLength / 2 +
      (granularLength / 2 -
       std::min((int)(std::numeric_limits<uint16_t>::max() - (int)granularPosition),
                granularLength / 2));

    int32_t granularDownConstrain = granularPosition - lowerZoneLength;
    int32_t granularUpConstrain = granularPosition + upperZoneLength;

    uint16_t startGranular = (granularDownConstrain > 0) ? (uint16_t)granularDownConstrain : 0;
    uint16_t loopPoint1Granular = startGranular + 1;
    uint16_t endGranular =
      (granularUpConstrain < MAX_16BIT) ? (uint16_t)granularUpConstrain : MAX_16BIT;
    uint16_t loopPoint2Granular = endGranular - 1;

    return std::make_tuple(startGranular, loopPoint1Granular, loopPoint2Granular, endGranular);
}

uint32_t AudioPlayMemory::calculatePointPositionAndSnapToZero(uint16_t position,
                                                              uint32_t sampleLength,
                                                              int16_t* sampleAddress)
{
    uint32_t maxReadable = getMaxReadableFramesForAddress(sampleAddress, sampleLength);
    if ((bool)isSnapToZeroEnabled == false || maxReadable == 0)
    {
        return (uint32_t)((float)position * ((float)sampleLength / MAX_16BIT));
    }

    auto [lowerBoundaryAddress, initialAddress, upperBoundaryAddress] =
      calculatePositionZoneAddresses(position, sampleLength, sampleAddress);

    if ((uint32_t)(upperBoundaryAddress - sampleAddress) >= maxReadable)
    {
        return (uint32_t)((float)position * ((float)sampleLength / MAX_16BIT));
    }

    auto detectZeroCrossing = [sampleAddress, maxReadable](int16_t& value)
    { return isSampleInZeroCrossing(value, sampleAddress, sampleAddress + maxReadable); };

    int16_t* lowerBoundaryResult =
      std::find_if(lowerBoundaryAddress, initialAddress + 1, detectZeroCrossing);

    static constexpr int kNotFound = std::numeric_limits<int>::min();
    int lowerBoundaryShift = (lowerBoundaryResult == initialAddress + 1)
                               ? kNotFound
                               : initialAddress - lowerBoundaryResult;
    int16_t* upperBoundaryResult =
      std::find_if(initialAddress, upperBoundaryAddress + 1, detectZeroCrossing);
    int upperBoundaryShift = (upperBoundaryResult == upperBoundaryAddress + 1)
                               ? kNotFound
                               : upperBoundaryResult - initialAddress;

    int shift = 0;
    if (lowerBoundaryShift != kNotFound && upperBoundaryShift != kNotFound)
    {
        if (upperBoundaryShift <= lowerBoundaryShift)
        {
            shift = upperBoundaryShift;
        }
        else
        {
            shift = -lowerBoundaryShift;
        }
    }
    else if (lowerBoundaryShift == kNotFound && upperBoundaryShift != kNotFound)
    {
        shift = upperBoundaryShift;
    }
    else if (lowerBoundaryShift != kNotFound && upperBoundaryShift == kNotFound)
    {
        shift -= lowerBoundaryShift;
    }

    uint32_t initialPosition =
      (uint32_t)((float)position * ((float)sampleLength / (float)MAX_16BIT));

    return (uint32_t)((int)initialPosition + shift);
}

std::tuple<int16_t*, int16_t*, int16_t*> AudioPlayMemory::calculatePositionZoneAddresses(
  uint16_t position,
  uint32_t sampleLength,
  int16_t* sampleAddress)
{
    int kSnapZoneLength = 128;
    int upperZoneLength =
      std::min(kSnapZoneLength / 2, std::numeric_limits<uint16_t>::max() - position);

    int lowerZoneLength = std::min(kSnapZoneLength / 2, (int)position);

    if (upperZoneLength < kSnapZoneLength / 2)
    {
        int dif = kSnapZoneLength / 2 - upperZoneLength;
        lowerZoneLength += dif;
    }
    else if (lowerZoneLength < kSnapZoneLength / 2)
    {
        int dif = kSnapZoneLength / 2 - lowerZoneLength;
        upperZoneLength += dif;
    }

    assert(kSnapZoneLength == (lowerZoneLength + upperZoneLength));
    int upperBoundary =
      clamp((int)position + upperZoneLength, 0, (int)std::numeric_limits<uint16_t>::max());
    int lowerBoundary =
      clamp((int)position - lowerZoneLength, 0, (int)std::numeric_limits<uint16_t>::max());

    uint32_t upperBoundaryPosition =
      (uint32_t)((float)upperBoundary * ((float)sampleLength / (float)MAX_16BIT));

    uint32_t lowerBoundaryPosition =
      (uint32_t)((float)lowerBoundary * ((float)sampleLength / (float)MAX_16BIT));

    uint32_t initialPosition =
      (uint32_t)((float)position * ((float)sampleLength / (float)MAX_16BIT));

    return std::make_tuple(sampleAddress + lowerBoundaryPosition,
                           sampleAddress + initialPosition,
                           sampleAddress + upperBoundaryPosition);
}

bool AudioPlayMemory::isSampleInZeroCrossing(int16_t& value,
                                             const int16_t* sampleBeginAddress,
                                             const int16_t* sampleEndAddress)
{
    if (value == 0)
    {
        return true;
    }

    if (value <= kSnapToZeroThreshold && value >= -kSnapToZeroThreshold)
    {
        const int16_t* address = &value;
        const int16_t* previousAddress = clamp(address - 1, sampleBeginAddress, sampleEndAddress);
        const int16_t* nextAddress = clamp(address + 1, sampleBeginAddress, sampleEndAddress);

        // if product of two values is less than 0 it means that the values have opposite sign
        int multWithPrevious = (int)value * (int)(*previousAddress);
        int multWithNext = (int)value * (int)(*nextAddress);

        return multWithPrevious < 0 || multWithNext < 0;
    }

    return false;
}

void AudioPlayMemory::setGranularPosition(uint16_t val)
{
    if (mtProject.instrument[currentInstrIdx].playMode != playModeGranular)
        return;

    currentGranularPosition = val;

    granularPositionRefreshFlag = 1;
}
void AudioPlayMemory::setGranularGrainLength()
{
    if (mtProject.instrument[currentInstrIdx].playMode != playModeGranular)
        return;

    currentGranularLength = granularLengthForcedFlag
                              ? forcedGranularLength
                              : mtProject.instrument[currentInstrIdx].granular.grainLength;
    granularPositionRefreshFlag = 1;
}
void AudioPlayMemory::setGranularGrainLength(uint16_t val)
{
    if (mtProject.instrument[currentInstrIdx].playMode != playModeGranular)
        return;

    currentGranularLength = val;
    granularPositionRefreshFlag = 1;
}
void AudioPlayMemory::setGranularWave(uint8_t type)
{
    if (mtProject.instrument[currentInstrIdx].playMode != playModeGranular)
        return;
    switch (type)
    {
        case granularShapeSquare: granularEnvelopeTab = squareTab; break;
        case granularShapeGauss: granularEnvelopeTab = gaussTab; break;
        case granularShapeTriangle: granularEnvelopeTab = triangleTab; break;
        default: break;
    }
}
void AudioPlayMemory::setGranularLoopMode(uint8_t type)
{
    if (mtProject.instrument[currentInstrIdx].playMode != playModeGranular)
        return;
    granularLoopType = type;
}

void AudioPlayMemory::setTune(int8_t value, int8_t currentNote)
{
    value += mtProject.instrument[currentInstrIdx].tuneResample;

    if ((currentNote + value) > (MAX_NOTE - 1))
        value = (MAX_NOTE - 1) - currentNote;
    if ((currentNote + value) < MIN_NOTE)
        value = MIN_NOTE - currentNote;

    uint8_t localPlaymode = mtProject.instrument[currentInstrIdx].playMode;

    float localPitchControl = pitchControl;

    int lastNoteAndTuneIdx = tunedNoteIdx(currentNote, currentTune);

    const float* local_notes = (localPlaymode != playModeWavetable) ? notes : wt_notes;

    localPitchControl -= local_notes[lastNoteAndTuneIdx];
    localPitchControl += local_notes[currentNote + value];
    AudioNoInterrupts();
    pitchControl = localPitchControl;
    currentTune = value;

    if (pitchControl > local_notes[MAX_NOTE - 1])
    {
        pitchControl = local_notes[MAX_NOTE - 1];
    }
    else if (pitchControl < local_notes[MIN_NOTE])
    {
        pitchControl = local_notes[MIN_NOTE];
    }
    AudioInterrupts();
    setFineTune(currentFineTune, currentNote);
}

void AudioPlayMemory::setTuneForceFlag()
{
    tuneForceFlag = 1;
}
void AudioPlayMemory::clearTuneForceFlag()
{
    tuneForceFlag = 0;
}
void AudioPlayMemory::setForcedTune(int8_t value)
{
    forcedTune = value;
}

void AudioPlayMemory::setPointsForceFlag()
{
    pointsForceFlag = 1;
}
void AudioPlayMemory::clearPointsForceFlag()
{
    pointsForceFlag = 0;
}
void AudioPlayMemory::setGlideForceFlag()
{
    glideForceFlag = 1;
}
void AudioPlayMemory::clearGlideForceFlag()
{
    glideForceFlag = 0;
}

void AudioPlayMemory::setForcedGlide(uint16_t value)
{
    forcedGlide = value;
}

void AudioPlayMemory::setFineTuneForceFlag()
{
    fineTuneForceFlag = 1;
}
void AudioPlayMemory::clearFineTuneForceFlag()
{
    fineTuneForceFlag = 0;
}
void AudioPlayMemory::setForcedFineTune(int8_t value)
{
    forcedFineTune = value;
}

void AudioPlayMemory::setForcedPoints(int32_t sp, int32_t lp1, int32_t lp2, int32_t ep)
{
    if (sp != -1)
        forcedStartPoint = sp;
    else
        forcedStartPoint = mtProject.instrument[currentInstrIdx].startPoint;
    if (lp1 != -1)
        forcedLoopPoint1 = lp1;
    else
        forcedLoopPoint1 = mtProject.instrument[currentInstrIdx].loopPoint1;
    if (lp2 != -1)
        forcedLoopPoint2 = lp2;
    else
        forcedLoopPoint2 = mtProject.instrument[currentInstrIdx].loopPoint2;
    if (ep != -1)
        forcedEndPoint = ep;
    else
        forcedEndPoint = mtProject.instrument[currentInstrIdx].endPoint;
}

void AudioPlayMemory::setWavetableWindowForceFlag()
{
    wavetableWindowForceFlag = 1;
}
void AudioPlayMemory::clearWavetableWindowForceFlag()
{
    wavetableWindowForceFlag = 0;
}
void AudioPlayMemory::setForcedWavetableWindow(uint32_t val)
{
    if (val >= mtProject.instrument[currentInstrIdx].sample.wavetableWindowNumber)
        forcedWavetableWindow =
          mtProject.instrument[currentInstrIdx].sample.wavetableWindowNumber - 1;
    else
        forcedWavetableWindow = val;
}

void AudioPlayMemory::setGranularPosForceFlag()
{
    granularForcedFlag = 1;
}
void AudioPlayMemory::clearGranularPosForceFlag()
{
    granularForcedFlag = 0;
}
void AudioPlayMemory::setForcedGranularPos(uint16_t value)
{
    forcedGranularPosition = value;
}

void AudioPlayMemory::setGranularLenForceFlag()
{
    granularLengthForcedFlag = 1;
}
void AudioPlayMemory::clearGranularLenForceFlag()
{
    granularLengthForcedFlag = 0;
}
void AudioPlayMemory::setForcedGranularLen(uint16_t value)
{
    forcedGranularLength = value;
}

void AudioPlayMemory::setReverse()
{
    reverseDirectionFlag = 1;
}
void AudioPlayMemory::clearReverse()
{
    reverseDirectionFlag = 0;
}
void AudioPlayMemory::setInterpolationEnable(bool value)
{
    enableInterpolation = value;
}
uint16_t AudioPlayMemory::getPosition()
{
    return (uint16_t)(65535 *
                      ((pointsInSamples.start + iPitchCounter) / (float)currentSampleLength));
}
int16_t* AudioPlayMemory::getStartAddresIfPlaying()
{
    return playing ? (int16_t*)currentStartAddress : nullptr;
}
uint32_t AudioPlayMemory::getLengthIfPlaying()
{
    return playing ? length : 0;
}

void AudioPlayMemory::setForcedSlice(uint8_t value)
{
    if (mtProject.instrument[currentInstrIdx].sliceNumber == 0)
        forcedSlice = 0;
    else if (value > mtProject.instrument[currentInstrIdx].sliceNumber - 1)
        forcedSlice = mtProject.instrument[currentInstrIdx].sliceNumber - 1;
    else
        forcedSlice = value;
}
void AudioPlayMemory::setSliceForcedFlag()
{
    sliceForcedFlag = 1;
}
void AudioPlayMemory::clearSliceForcedFlag()
{
    sliceForcedFlag = 0;
}
void AudioPlayMemory::setCurrentInstrIdx(uint8_t n)
{
    currentInstrIdx = n;
}

void AudioPlayMemory::clean(void)
{
    if (!playing)
    {
        next = NULL;
        length = 0;
        pitchControl = 1;
        iPitchCounter = 0;
        fPitchCounter = 0;
        currentPlayMode = 0;
        playing = 0;
        loopBackwardFlag = 0;
        lastNote = -1;
        currentGlide = 0;
        pitchSmoothingControlCounter = 0;
        pitchSmoothingControl = 0;
        finetunePitchControl = 0;
        currentTune = 0;
        wavetableWindowSize = 0;
        currentWindow = 0;
        waveTablePosition = 0;
        //	    wavetableSync=0;
        //	    wavetablePWM=0;
        //	    wavetableFlip=0;
        //	    wavetableQuantize=0;
        currentInstrIdx = 0;
        currentFineTune = 0;

        pointsInSamples.start = 0;
        pointsInSamples.end = 0;
        pointsInSamples.loop1 = 0;
        pointsInSamples.loop2 = 0;

        constrainsInSamples.loopLength = 0;
        constrainsInSamples.loopPoint1 = 0;
        constrainsInSamples.loopPoint2 = 0;
        constrainsInSamples.endPoint = 0;

        constrainsInSamples.pitchSmoothing = 0;

        currentSampleLength = 0;
        currentIsStreamed = false;
        currentPreCacheFrames = 0;
        currentSliceCacheAddress = nullptr;
        currentSliceCacheFrames = 0;
        if (channel == 0 && voiceIndex >= 0)
        {
            voiceStreamManager.notifyNoteOff((uint8_t)voiceIndex);
        }
    }
}
// PLAY OBSÓGA OGÓLNA
void AudioPlayMemory::refreshStartParameters()
{
    if (playing == 1)
        needSmoothingFlag = 1;
    playing = 0;
    loopBackwardFlag = 0;
    iPitchCounter = 0;
    fPitchCounter = 0;
    pitchSmoothingControlCounter = 0;

    currentGlide = glideForceFlag ? forcedGlide : mtProject.instrument[currentInstrIdx].glide;

    currentTune = (tuneForceFlag ? forcedTune : mtProject.instrument[currentInstrIdx].tune) +
                  mtProject.instrument[currentInstrIdx].tuneResample;

    currentFineTune =
      fineTuneForceFlag ? forcedFineTune : mtProject.instrument[currentInstrIdx].fineTune;

    currentStartAddress = mtProject.sampleMemory[currentInstrIdx].address[channel];

    currentPlayMode = mtProject.instrument[currentInstrIdx].playMode;

    currentSampleLength = mtProject.instrument[currentInstrIdx].sample.length;

    currentIsStreamed = (currentPlayMode != playModeWavetable &&
                         currentPlayMode != playModeGranular) &&
                        mtProject.sampleMemory[currentInstrIdx].isStreamed;
    currentPreCacheFrames = mtProject.sampleMemory[currentInstrIdx].preCacheFrames;
    currentSliceCacheAddress = nullptr;
    currentSliceCacheFrames = 0;
    if (!currentIsStreamed && channel == 0 && voiceIndex >= 0)
    {
        voiceStreamManager.notifyNoteOff((uint8_t)voiceIndex);
    }
}

void AudioPlayMemory::constrainCurrentTune(uint8_t note)
{
    if ((note + currentTune) > (MAX_NOTE - 1))
    {
        if (lastNote > note)
            currentTune = (MAX_NOTE - 1) - lastNote;
        else
            currentTune = (MAX_NOTE - 1) - note;
    }
    else if ((note + currentTune) < MIN_NOTE)
    {
        if ((lastNote >= 0) && (lastNote < note))
            currentTune = MIN_NOTE - lastNote;
        else
            currentTune = MIN_NOTE - note;
    }
}

// note + tune
void AudioPlayMemory::setStartPitch(uint8_t note)
{
    if (currentPlayMode == playModeWavetable)
    {
        if ((lastNote >= 0) && (currentGlide != 0))
            pitchControl = (float)wt_notes[tunedNoteIdx(lastNote, currentTune)];
        else
            pitchControl = (float)wt_notes[tunedNoteIdx(note, currentTune)];
    }
    else
    {
        if ((lastNote >= 0) && (currentGlide != 0))
            pitchControl = (float)notes[tunedNoteIdx(lastNote, currentTune)];
        else
            pitchControl = (float)notes[tunedNoteIdx(note, currentTune)];
    }
}
void AudioPlayMemory::applyFinetuneOnPitch(uint8_t note)
{
    if (currentPlayMode == playModeWavetable)
    {
        if (currentFineTune >= 0)
        {
            if ((note + currentTune + 1) <= (MAX_NOTE - 1))
            {
                float localPitch1 = (float)wt_notes[note + currentTune + 1];
                float localPitch2 = (float)wt_notes[note + currentTune];

                finetunePitchControl =
                  currentFineTune * ((localPitch1 - localPitch2) / MAX_INSTRUMENT_FINETUNE);
            }
            else
                finetunePitchControl = 0;
        }
        else
        {
            if ((note + currentTune - 1) >= MIN_NOTE)
            {
                float localPitch1 = (float)wt_notes[note + currentTune - 1];
                float localPitch2 = (float)wt_notes[note + currentTune];

                finetunePitchControl =
                  (-currentFineTune) * ((localPitch1 - localPitch2) / MAX_INSTRUMENT_FINETUNE);
            }
            else
                finetunePitchControl = 0;
        }

        pitchControl += finetunePitchControl;
    }
    else
    {
        if (currentFineTune >= 0)
        {
            if ((note + currentTune + 1) <= (MAX_NOTE - 1))
            {
                float localPitch1 = (float)notes[note + currentTune + 1];
                float localPitch2 = (float)notes[note + currentTune];

                finetunePitchControl =
                  currentFineTune * ((localPitch1 - localPitch2) / MAX_INSTRUMENT_FINETUNE);
            }
            else
                finetunePitchControl = 0;
        }
        else
        {
            if ((note + currentTune - 1) >= MIN_NOTE)
            {
                float localPitch1 = (float)notes[note + currentTune - 1];
                float localPitch2 = (float)notes[note + currentTune];

                finetunePitchControl =
                  (-currentFineTune) * ((localPitch1 - localPitch2) / MAX_INSTRUMENT_FINETUNE);
            }
            else
                finetunePitchControl = 0;
        }

        pitchControl += finetunePitchControl;
    }
}
void AudioPlayMemory::calculateGlidePitch(uint8_t note)
{
    if (currentPlayMode == playModeWavetable)
    {
        if (currentGlide)
        {
            constrainsInSamples.pitchSmoothing = (uint32_t)((float)currentGlide * 44.1);

            float localPitch1 = (float)wt_notes[tunedNoteIdx(note, currentTune)];
            float localPitch2 = (float)wt_notes[tunedNoteIdx(lastNote, currentTune)];
            if ((lastNote >= 0) && (lastNote != note))
                pitchSmoothingControl =
                  (localPitch1 - localPitch2) / constrainsInSamples.pitchSmoothing;
            else
                pitchSmoothingControl = 0;
        }
        else
        {
            constrainsInSamples.pitchSmoothing = 0;
            pitchSmoothingControl = 0;
        }
    }
    else
    {
        if (currentGlide)
        {
            constrainsInSamples.pitchSmoothing = (uint32_t)((float)currentGlide * 44.1);

            float localPitch1 = (float)notes[tunedNoteIdx(note, currentTune)];
            float localPitch2 = (float)notes[tunedNoteIdx(lastNote, currentTune)];
            if ((lastNote >= 0) && (lastNote != note))
                pitchSmoothingControl =
                  (localPitch1 - localPitch2) / constrainsInSamples.pitchSmoothing;
            else
                pitchSmoothingControl = 0;
        }
        else
        {
            constrainsInSamples.pitchSmoothing = 0;
            pitchSmoothingControl = 0;
        }
    }
}

// up true, down false
AUDIO_CORE_FLASH void AudioPlayMemory::setSlidePitch(bool upOrDown, uint8_t dstValue, float time_ms)
{
    if (dstValue)
    {
        float octaveChangeValue = upOrDown ? 192.0f : -192.0f;
        float inToOutPitchRatio =
          powf(2.0f, (float)dstValue / octaveChangeValue); // todo: czy wplynie na wydajnosc?
        float localPitch1 = pitchControl * inToOutPitchRatio;
        float localPitch2 = pitchControl;

        float maxPitch =
          (currentPlayMode == playModeWavetable) ? wt_notes[MAX_NOTE - 1] : notes[MAX_NOTE - 1];
        clamp(localPitch1, localPitch2, maxPitch);

        AudioNoInterrupts();
        constrainsInSamples.pitchSmoothing = (uint32_t)((float)time_ms * 44.117f);
        pitchSmoothingControl = (localPitch1 - localPitch2) / constrainsInSamples.pitchSmoothing;
        pitchSmoothingControlCounter = 0;
        AudioInterrupts();
    }
    else
    {
        AudioNoInterrupts();
        constrainsInSamples.pitchSmoothing = 0;
        pitchSmoothingControl = 0;
        pitchSmoothingControlCounter = 0;
        AudioInterrupts();
    }
}

void AudioPlayMemory::setSamplePointsSource(AudioPlayMemory* sourceObject)
{
    samplePointsSource = sourceObject;
}

float AudioPlayMemory::getChangeVolumeCoef()
{
    if (lastInstrIdxInPlay == -1)
        return 1.0f;

    float currentVol = ampLogValues[mtProject.instrument[currentInstrIdxInPlay].volume];
    float lastVol = ampLogValues[mtProject.instrument[lastInstrIdxInPlay].volume];
    float volCoef = lastVol / currentVol;

    return volCoef;
}
