#include "mtAudioEngine.h"
#include "Platform.h"
#include "VoiceStreamManager.h"
#ifdef FEATURE_SYNTH
#include "PolySynth/sdk/SynthVoiceSDK.h"
#endif
#include "SynthVoice.h"
#include "ampLogarythmicValues.h"
#include "cr_section_macros.h"
#include "metronomeBeep.h"
#include "mtMidi.h"
#include "mtMasterEnhancer.h"
#include "mtStructs.h"
#include "sampleRecorder/sampleRecorder.h"
#include "tempoSyncRateValues.h"
#include "util.h"
#include <Recorder.h>

static constexpr int kSdStreamBufferSize = (256 + 64) * 1024;
extern uint8_t externalRamScratchBuff[];
static_assert(kSdStreamBufferSize <= SHARED_SCRATCH_MEMORY_SIZE,
              "sdStreamBuffer exceeds shared scratch size");
#ifdef FEATURE_SYNTH
static_assert(PolySynth::kTrackInputs == audioEngine::kVoices,
              "PolySynth must have an input for each track");
#endif

playerEngine instrumentPlayer[audioEngine::kVoices];
audioEngine engine;

uint8_t isBlockedInstrument[48];
uint8_t isBlockedNoteOn[48];

#ifdef FEATURE_SYNTH
const audioEngine::SynthParamBoundaries
  audioEngine::synthParamsBoundaries[SynthParam::kSynthParamsCount] = {
      { -100.0f, 100.0f },
      { 0.0f, 100.0f },
      { 0.0f, 100.0f },
      { -96.0f, 12.0f },
      { kCCMinValue, kCCMaxValue },
      { kCCMinValue, kCCMaxValue },
      { kCCMinValue, kCCMaxValue },
      { kCCMinValue, kCCMaxValue },
      { kCCMinValue, kCCMaxValue },
      { kCCMinValue, kCCMaxValue }

  };
std::map<uint8_t, audioEngine::SynthValuesSource> audioEngine::fxNumberToSynthValuesSource = {
    { 0, SynthValuesSource::kFx1 },
    { 1, SynthValuesSource::kFx2 }
};

std::map<uint8_t, audioEngine::SynthParam> audioEngine::fxIdToSynthParam = {
    { Sequencer::strFxConsts::FX_TYPE_PANNING, SynthParam::kPanning },
    { Sequencer::strFxConsts::FX_TYPE_DELAY_SEND, SynthParam::kDelaySend },
    { Sequencer::strFxConsts::FX_TYPE_REVERB_SEND, SynthParam::kReverbSend },
    { Sequencer::strFxConsts::FX_TYPE_VOLUME, SynthParam::kVolume },
    { Sequencer::strFxConsts::FX_TYPE_SEND_CC_A, SynthParam::kMacro1 },
    { Sequencer::strFxConsts::FX_TYPE_SEND_CC_B, SynthParam::kMacro2 },
    { Sequencer::strFxConsts::FX_TYPE_SEND_CC_C, SynthParam::kMacro3 },
    { Sequencer::strFxConsts::FX_TYPE_SEND_CC_D, SynthParam::kMacro4 },
    { Sequencer::strFxConsts::FX_TYPE_SEND_CC_E, SynthParam::kMacro5 },
    { Sequencer::strFxConsts::FX_TYPE_SEND_CC_F, SynthParam::kMacro6 }
};

#endif

using namespace mtAudioEngineConstans;

void audioEngine::startTestSignal(float amp, float freq)
{
    testWaveform.amplitude(amp);
    testWaveform.frequency(freq);
}

void audioEngine::stopTestSignal()
{
    testWaveform.amplitude(0.0);
}
// blokowanie odswiezana delaya
void audioEngine::blockDelayRefresh()
{
    delay->blockUpdate();
}
// odblokowanie odswiezana delaya
void audioEngine::unblockDelayRefresh()
{
    delay->unblockUpdate();
}
AUDIO_CORE_RAM void audioEngine::setPassEnvelope(uint8_t state)
{
    for (uint8_t i = 0; i < 8; i++)
    {
        instrumentPlayer[i].setPassEnvelope(state);
    }
}

void audioEngine::detachUpdateTimer()
{
    // @shuler todo: remove or add flag to disable audio processing?
    // used in cFileManager::stopAudioEngine()
}
void audioEngine::attachUpdateTimer()
{
    // @shuler todo: remove or add flag to disable audio processing?
    // used in cFileManager::startAudioEngine()
}

AudioInput* audioEngine::getAudioInput()
{
    return &input;
}

AudioOutput* audioEngine::getAudioOutput()
{
    return &output;
}

ExportQueues* audioEngine::getExportQueues()
{
    return &exportQueues;
}

MixerAndFxs<audioEngine::kVoices>* audioEngine::getMixerAndFx()
{
    return &mixerAndFxs;
}

AudioPlaySdWav* audioEngine::getSdPlayer()
{
    return &playerWavFromSD;
}

audioEngine::audioEngine()
{
    for (int voiceIdx = 0; voiceIdx < kVoices; voiceIdx++)
    {
        connectVoiceToMixer(instrumentPlayer[voiceIdx].getAudioVoice(), &mixerAndFxs, voiceIdx);
        connectVoiceToOutput(instrumentPlayer[voiceIdx].getAudioVoice(), &output, voiceIdx);
        connectVoiceToExportQueue(
          instrumentPlayer[voiceIdx].getAudioVoice(), &exportQueues, voiceIdx);
#ifdef FEATURE_SYNTH
        connectVoiceToSynth(instrumentPlayer[voiceIdx].getAudioVoice(), &synthVoice, voiceIdx);
#endif
    }
#ifdef FEATURE_SYNTH
    connectSynthToMixer(&mixerAndFxs, &synthVoice);
    connectSynthToExportQueue(&synthVoice, &exportQueues);
    connectSynthToOutput(&synthVoice, &output);
#endif

    connectInputToOutput(&input, &output);
    connectInputToMixer(&input, &mixerAndFxs);
    connectMixerToOutput(&mixerAndFxs, &output);
    connectMixerToExportQueue(&mixerAndFxs, &exportQueues);
    connectSpecialInputsToOutput(&output);
    connectOutputToExportQueue(&output, &exportQueues);

    for (int channel = 0; channel < AudioOutput::kChannelsCount; channel++)
    {
        dryMixRms[channel] = mixerAndFxs.getDryMixRMS((MixerAndFxChannelType)channel);
        polyverbRms[channel] = mixerAndFxs.getPolyverbRMS((MixerAndFxChannelType)channel);
        delayRms[channel] = mixerAndFxs.getDelayRMS((MixerAndFxChannelType)channel);

        finalMixer[channel] = output.getFinalMixer((AudioOutput::OutputChannels)channel);
        tracksAndFxsMixer[channel] =
          mixerAndFxs.getTracksAndSendsMixer((MixerAndFxChannelType)channel);
        limiter[channel] = output.getLimiter((AudioOutput::OutputChannels)channel);
    }

    polyverb = mixerAndFxs.getPolyverb();
    delay = mixerAndFxs.getDelay();

    prePolyverbMixer = mixerAndFxs.getPreFxMixer(MixerAndFxs<kVoices>::FxType::kReverb);
    preDelayMixer = mixerAndFxs.getPreFxMixer(MixerAndFxs<kVoices>::FxType::kDelay);

}

void audioEngine::init(Platform* platform, [[maybe_unused]] Synth* synth)
{
    codec = platform->getCodecAudio();
    radio = platform->getRadio();
    usb = platform->getUsb();

#ifdef PLATFORM_IMX
    AudioMemory(80);
#else
    AudioMemory(70);
#endif
    AudioPlayMemory::initGranularShapeLookup();
    AudioPlayMemory::initNotesLookup();
    AudioFilterStateVariable::initCutoffLookup();
#ifdef FEATURE_SYNTH
    synthVoice.init(synth);
#endif

    for (int i = 0; i < 8; i++)
    {
        instrumentPlayer[i].init(platform->getTimer(), preDelayMixer, prePolyverbMixer);
    }
    finalMixer[AudioOutput::OutputChannels::kLeft]->gain(AudioOutput::FinalMixInputs::kStream, 1.0);
    finalMixer[AudioOutput::OutputChannels::kRight]->gain(AudioOutput::FinalMixInputs::kStream,
                                                          1.0);

    for (uint8_t i = 1; i < 4; i++)
    {
        // todo: it looks like legacy. Should we delete it?
        finalMixer[AudioOutput::OutputChannels::kLeft]->gain(i, ampLogValues[50]);
        finalMixer[AudioOutput::OutputChannels::kRight]->gain(i, ampLogValues[50]);

        tracksAndFxsMixer[(int)MixerAndFxChannelType::kLeft]->gain(i, 1.0f);
        tracksAndFxsMixer[(int)MixerAndFxChannelType::kRight]->gain(i, 1.0f);
    }

    for (int channel = 0; channel < kVoices + kSynthParts; channel++)
    {
        mixerAndFxs.getTracksMixer(MixerAndFxChannelType::kLeft)->gain(channel, 1.0f);
        mixerAndFxs.getTracksMixer(MixerAndFxChannelType::kRight)->gain(channel, 1.0f);
    }

    finalMixer[AudioOutput::OutputChannels::kLeft]->gain(
      AudioOutput::FinalMixInputs::kNesEmulatorSound, 0.0f);
    finalMixer[AudioOutput::OutputChannels::kRight]->gain(
      AudioOutput::FinalMixInputs::kNesEmulatorSound, 0.0f);
    //	filterReverbOut.setType(filterType::lowPass);
    //	filterReverbOut.setCutoff(1.0);
    //	filterReverbOut.connect();
    delay->begin(mtProject.values.delayFeedback / 100.0,
                 mtProject.values.delayTime,
                 mtProject.values.delayParams & 0b10000000,
                 mtProject.values.delayParams & 0b01000000,
                 mtProject.values.delayParams & 0b00111111,
                 mtProject.values.delayFilterFrequency);

    AudioPlayMemory::enableHeadroom((bool)mtProject.values.isHeadroomEnabled);
    AudioPlayMemory::enableSnapToZero((bool)mtProject.values.isSnapToZeroEnabled);

    setSaturationType((SaturationType)mtProject.values.saturationType);

    codec->setMasterVolume(mtConfig.audioCodecConfig.volume / 100.0f);
    setPreviewVolume(mtConfig.general.previewVolume);
    mtConfig.audioCodecConfig.inSelect = inputSelectLineIn;
    setInputConfigOutsideRecorderInterfaceModule();

    limiter[AudioOutput::OutputChannels::kLeft]->begin();
    limiter[AudioOutput::OutputChannels::kRight]->begin();

    setLimiterSidechain(mtProject.values.limiterSidechain);

    testWaveform.begin(0.0, 1000, WAVEFORM_SQUARE);

    //	updateTimer.begin(updateAudioEngine,4500);
    //	updateTimer.priority(255);
    attachUpdateTimer();

    polyverb->reset();
    polyverb->setTime(mtProject.values.reverb.size);
    polyverb->setDamp(1.0f - mtProject.values.reverb.damp);
    polyverb->SetPredelayLength(mtProject.values.reverb.predelay);
    polyverb->SetDiffusion(mtProject.values.reverb.diffusion);
    polyverb->unblockUpdate();

    masterEnhancer.setCodec(codec);
    codec->enableHardwareProcessor(true);

    masterEnhancer.fiveBandsEq.enable(mtProject.values.eqBandAdjustment);
    masterEnhancer.enable();

    setLineInOutType(AudioStereoToMono::OutSignalType::stereo);

    input.getGate(AudioInput::InputChannels::kLeft)->gate(GATE_DB);
    input.getGate(AudioInput::InputChannels::kRight)->gate(GATE_DB);

    sdStream.init(platform->getFileSystem(), externalRamScratchBuff, kSdStreamBufferSize);
    playerWavFromSD.setSdStream(&sdStream);
    voiceStreamManager.init(platform->getFileSystem());
}

AUDIO_CORE_RAM void audioEngine::update()
{
    recorder.update();

    if (!recorder.isRecordingActive())
    {
        currentTempo = sequencer.getCurrentTempo();

        lastTempo[3] = lastTempo[2];
        lastTempo[2] = lastTempo[1];
        lastTempo[1] = lastTempo[0];
        lastTempo[0] = currentTempo;

        float currentAveTempo = (lastTempo[0] + lastTempo[1] + lastTempo[2] + lastTempo[3]) / 4.0f;

        if (abs(currentTempo - currentAveTempo) > 0.5f)
        {
            currentTempo = currentAveTempo;
        }

        if (abs(currentTempo - lastDelayRefreshTempo) > 1.0f)
        {
            lastDelayRefreshTempo = currentTempo;
            setDelayParams(mtProject.values.delayParams);
        }

        for (int i = 0; i < 8; i++)
        {
            instrumentPlayer[i].update();
        }

#ifdef FEATURE_SYNTH
        updateSynthParams();
        updateRoutedTracks();
#endif
    }
}

void audioEngine::updateSdStream()
{
    sdStream.update();
    voiceStreamManager.update();
}

AUDIO_CORE_RAM void audioEngine::updateAudioBuffers(void* left, void* right, int length)
{
    (void)length;
    output.getUsbStream()->syncActive();
    output.getStream()->setOutBuffers(left, right);
    AudioStream::update_all();
}

AUDIO_CORE_RAM void audioEngine::updateInputAudioBuffers(void* i2sData,
                                                         void* usbData,
                                                         int length)
{
    (void)length;

    input.getI2sStream()->setInBuffer(i2sData);
    input.getUsbStream()->setInBuffer(usbData);
}

void audioEngine::connectVoiceToMixer(AudioVoice* voice, MixerAndFxs<kVoices>* mixerAndFxs, int idx)
{
    constexpr int kInputsChannels = 2;
    connections.push_back(
      new AudioConnection(voice->getPanningAmplifier(AudioVoice::VoiceChannels::kLeft),
                          0,
                          mixerAndFxs->getTracksMixer(MixerAndFxChannelType::kLeft),
                          idx));
    connections.push_back(
      new AudioConnection(voice->getPanningAmplifier(AudioVoice::VoiceChannels::kRight),
                          0,
                          mixerAndFxs->getTracksMixer(MixerAndFxChannelType::kRight),
                          idx));

    connections.push_back(
      new AudioConnection(voice->getFilter(AudioVoice::VoiceChannels::kLeft),
                          0,
                          mixerAndFxs->getPreFxMixer(MixerAndFxs<kVoices>::FxType::kReverb),
                          idx));
    connections.push_back(
      new AudioConnection(voice->getFilter(AudioVoice::VoiceChannels::kRight),
                          0,
                          mixerAndFxs->getPreFxMixer(MixerAndFxs<kVoices>::FxType::kReverb),
                          kVoices + kInputsChannels + idx));

    connections.push_back(
      new AudioConnection(voice->getFilter(AudioVoice::VoiceChannels::kLeft),
                          0,
                          mixerAndFxs->getPreFxMixer(MixerAndFxs<kVoices>::FxType::kDelay),
                          idx));
    connections.push_back(
      new AudioConnection(voice->getFilter(AudioVoice::VoiceChannels::kRight),
                          0,
                          mixerAndFxs->getPreFxMixer(MixerAndFxs<kVoices>::FxType::kDelay),
                          kVoices + kInputsChannels + idx));
}

void audioEngine::connectInputToMixer(AudioInput* input, MixerAndFxs<kVoices>* mixerAndFxs)
{
    constexpr int kLeftInputMixerChannel = 8;
    constexpr int kRightInputMixerChannel = 9;

    connections.push_back(
      new AudioConnection(input->getGate(AudioInput::InputChannels::kLeft),
                          0,
                          mixerAndFxs->getPreFxMixer(MixerAndFxs<kVoices>::FxType::kDelay),
                          kLeftInputMixerChannel));

    connections.push_back(
      new AudioConnection(input->getGate(AudioInput::InputChannels::kRight),
                          0,
                          mixerAndFxs->getPreFxMixer(MixerAndFxs<kVoices>::FxType::kDelay),
                          kRightInputMixerChannel));

    connections.push_back(
      new AudioConnection(input->getGate(AudioInput::InputChannels::kLeft),
                          0,
                          mixerAndFxs->getPreFxMixer(MixerAndFxs<kVoices>::FxType::kReverb),
                          kLeftInputMixerChannel));

    connections.push_back(
      new AudioConnection(input->getGate(AudioInput::InputChannels::kRight),
                          0,
                          mixerAndFxs->getPreFxMixer(MixerAndFxs<kVoices>::FxType::kReverb),
                          kRightInputMixerChannel));

#ifdef AUDIO_NULL_IS_SILENCE
    input->getGate(AudioInput::InputChannels::kLeft)
      ->setSilenceWatch(mixerAndFxs->getPreFxMixer(MixerAndFxs<kVoices>::FxType::kDelay),
                        kLeftInputMixerChannel,
                        mixerAndFxs->getPreFxMixer(MixerAndFxs<kVoices>::FxType::kReverb),
                        kLeftInputMixerChannel);
    input->getGate(AudioInput::InputChannels::kRight)
      ->setSilenceWatch(mixerAndFxs->getPreFxMixer(MixerAndFxs<kVoices>::FxType::kDelay),
                        kRightInputMixerChannel,
                        mixerAndFxs->getPreFxMixer(MixerAndFxs<kVoices>::FxType::kReverb),
                        kRightInputMixerChannel);
#endif

    constexpr int kTracksAndSendsMixerInputIdx = 3;

    connections.push_back(
      new AudioConnection(input->getStereoToMono(),
                          AudioInput::InputChannels::kLeft,
                          mixerAndFxs->getTracksAndSendsMixer(MixerAndFxChannelType::kLeft),
                          kTracksAndSendsMixerInputIdx));

    connections.push_back(
      new AudioConnection(input->getStereoToMono(),
                          AudioInput::InputChannels::kRight,
                          mixerAndFxs->getTracksAndSendsMixer(MixerAndFxChannelType::kRight),
                          kTracksAndSendsMixerInputIdx));
}

void audioEngine::connectVoiceToOutput(AudioVoice* voice, AudioOutput* output, int idx)
{
    connections.push_back(
      new AudioConnection(voice->getStereoMirror(),
                          0,
                          output->getLimiterSidechainMux(AudioOutput::OutputChannels::kLeft),
                          idx));
    connections.push_back(
      new AudioConnection(voice->getStereoMirror(),
                          1,
                          output->getLimiterSidechainMux(AudioOutput::OutputChannels::kRight),
                          idx));

#ifdef PLATFORM_IMX
    int leftOutAudioUsbIdx = AudioOutput::OutputChannels::kChannelsCount * idx;
    int rightOutAudioUsbIdx = leftOutAudioUsbIdx + 1;

    connections.push_back(
      new AudioConnection(voice->getPanningAmplifier(AudioVoice::VoiceChannels::kLeft),
                          0,
                          output->getUsbStream(),
                          leftOutAudioUsbIdx));
    connections.push_back(
      new AudioConnection(voice->getPanningAmplifier(AudioVoice::VoiceChannels::kRight),
                          0,
                          output->getUsbStream(),
                          rightOutAudioUsbIdx));
#endif
}

void audioEngine::connectInputToOutput(AudioInput* input, AudioOutput* output)
{
    constexpr int kLeftInputMixerChannel = 8;
    constexpr int kRightInputMixerChannel = 9;

    connections.push_back(
      new AudioConnection(input->getStereoToMono(),
                          AudioInput::InputChannels::kLeft,
                          output->getLimiterSidechainMux(AudioOutput::OutputChannels::kLeft),
                          kLeftInputMixerChannel));

    connections.push_back(
      new AudioConnection(input->getStereoToMono(),
                          AudioInput::InputChannels::kRight,
                          output->getLimiterSidechainMux(AudioOutput::OutputChannels::kLeft),
                          kRightInputMixerChannel));

    connections.push_back(
      new AudioConnection(input->getStereoToMono(),
                          AudioInput::InputChannels::kLeft,
                          output->getLimiterSidechainMux(AudioOutput::OutputChannels::kRight),
                          kLeftInputMixerChannel));

    connections.push_back(
      new AudioConnection(input->getStereoToMono(),
                          AudioInput::InputChannels::kRight,
                          output->getLimiterSidechainMux(AudioOutput::OutputChannels::kRight),
                          kRightInputMixerChannel));

    constexpr int kLeftUsbToOutputChannel = 2;
    constexpr int kRightUsbToOutputChannel = 3;

    connections.push_back(
      new AudioConnection(input->getUsbRecordingAmplifier(AudioInput::InputChannels::kLeft),
                          0,
                          output->getStream(),
                          kLeftUsbToOutputChannel));
    connections.push_back(
      new AudioConnection(input->getUsbRecordingAmplifier(AudioInput::InputChannels::kRight),
                          0,
                          output->getStream(),
                          kRightUsbToOutputChannel));
}

void audioEngine::connectMixerToOutput(MixerAndFxs<kVoices>* mixerAndFxs, AudioOutput* output)
{
    connections.push_back(
      new AudioConnection(mixerAndFxs->getTracksAndSendsMixer(MixerAndFxChannelType::kLeft),
                          0,
                          output->getLimiter(AudioOutput::OutputChannels::kLeft),
                          0));

    connections.push_back(
      new AudioConnection(mixerAndFxs->getTracksAndSendsMixer(MixerAndFxChannelType::kRight),
                          0,
                          output->getLimiter(AudioOutput::OutputChannels::kRight),
                          0));

#ifdef PLATFORM_IMX
    static constexpr int kPolyverbLeftUsbStreamIdx = 16;
    static constexpr int kPolyverbRightUsbStreamIdx = 17;
    static constexpr int kDelayLeftUsbStreamIdx = 18;
    static constexpr int kDelayRightUsbStreamIdx = 19;

    connections.push_back(new AudioConnection(mixerAndFxs->getPolyverb(),
                                              AudioOutput::OutputChannels::kLeft,
                                              output->getUsbStream(),
                                              kPolyverbLeftUsbStreamIdx));
    connections.push_back(new AudioConnection(mixerAndFxs->getPolyverb(),
                                              AudioOutput::OutputChannels::kRight,
                                              output->getUsbStream(),
                                              kPolyverbRightUsbStreamIdx));
    connections.push_back(new AudioConnection(mixerAndFxs->getDelay(),
                                              AudioOutput::OutputChannels::kLeft,
                                              output->getUsbStream(),
                                              kDelayLeftUsbStreamIdx));
    connections.push_back(new AudioConnection(mixerAndFxs->getDelay(),
                                              AudioOutput::OutputChannels::kRight,
                                              output->getUsbStream(),
                                              kDelayRightUsbStreamIdx));
#endif
}
#ifdef FEATURE_SYNTH
void audioEngine::connectVoiceToSynth(AudioVoice* voice, SynthVoice* synth, int idx)
{
    connections.push_back(
      new AudioConnection(voice->getPanningAmplifier(AudioVoice::VoiceChannels::kLeft),
                          0,
                          synth->getPolySynth(),
                          2 * idx));
    connections.push_back(
      new AudioConnection(voice->getPanningAmplifier(AudioVoice::VoiceChannels::kRight),
                          0,
                          synth->getPolySynth(),
                          2 * idx + 1));
}

void audioEngine::connectSynthToMixer(MixerAndFxs<kVoices>* mixerAndFxs, SynthVoice* synth)
{
    constexpr int kFirstSynthMixerChannel = 2 + 2 * kVoices;

    for (int synthChannel = 0; synthChannel < kSynthParts; synthChannel++)
    {
        connections.push_back(
          new AudioConnection(synth->getPolySynth(),
                              SynthVoice::kSend1ChannelOffset + synthChannel,
                              mixerAndFxs->getPreFxMixer(MixerAndFxs<kVoices>::FxType::kDelay),
                              kFirstSynthMixerChannel + synthChannel));
        connections.push_back(
          new AudioConnection(synth->getPolySynth(),
                              SynthVoice::kSend2ChannelOffset + synthChannel,
                              mixerAndFxs->getPreFxMixer(MixerAndFxs<kVoices>::FxType::kReverb),
                              kFirstSynthMixerChannel + synthChannel));

        // synth to panningAmplifier
        connections.push_back(new AudioConnection(
          synth->getPolySynth(),
          SynthVoice::kPostFaderLeftChannelOffset + synthChannel,
          synth->getPanningAmplifier(synthChannel, SynthVoice::Channels::kLeft), //
          0));

        connections.push_back(new AudioConnection(
          synth->getPolySynth(),
          SynthVoice::kPostFaderRightChannelOffset + synthChannel,
          synth->getPanningAmplifier(synthChannel, SynthVoice::Channels::kRight),
          0));

        // synth panningAmplifier to master mixer
        connections.push_back(new AudioConnection(
          synth->getPanningAmplifier(synthChannel, SynthVoice::Channels::kLeft), //
          0,
          mixerAndFxs->getTracksMixer(MixerAndFxChannelType::kLeft),
          kVoices + synthChannel));

        connections.push_back(new AudioConnection(
          synth->getPanningAmplifier(synthChannel, SynthVoice::Channels::kRight),
          0,
          mixerAndFxs->getTracksMixer(MixerAndFxChannelType::kRight),
          kVoices + synthChannel));

        connections.push_back(
          new AudioConnection(synth->getPolySynth(), // UI shows only mono
                              SynthVoice::kPostFaderLeftChannelOffset + synthChannel,
                              mixerAndFxs->getSynthRMS(synthChannel, MixerAndFxChannelType::kLeft),
                              0));
        connections.push_back(
          new AudioConnection(synth->getPolySynth(), // UI shows only mono
                              SynthVoice::kPostFaderRightChannelOffset + synthChannel,
                              mixerAndFxs->getSynthRMS(synthChannel, MixerAndFxChannelType::kRight),
                              0));
    }
}

void audioEngine::connectSynthToExportQueue(SynthVoice* synth, ExportQueues* exportQueues)
{
    for (int synthIdx = 0; synthIdx < kSynthParts; synthIdx++)
    {
        connections.push_back(new AudioConnection(
          synth->getPanningAmplifier(synthIdx, SynthVoice::Channels::kRight),
          0,
          exportQueues->getQueue(
            ExportQueues::ExportChannels::kRight,
            (mtAudioExport::enStem)((int)mtAudioExport::stem_synth_1 + synthIdx)),
          0));

        connections.push_back(new AudioConnection(
          synth->getPanningAmplifier(synthIdx, SynthVoice::Channels::kLeft),
          0,
          exportQueues->getQueue(
            ExportQueues::ExportChannels::kLeft,
            (mtAudioExport::enStem)((int)mtAudioExport::stem_synth_1 + synthIdx)),
          0));
    }
}

void audioEngine::connectSynthToOutput(SynthVoice* synth, AudioOutput* output)
{
    for (int synthPartIdx = 0; synthPartIdx < kSynthParts; synthPartIdx++)
    {

        static constexpr int kLimiterSidechainMuxInputOffset = 10;
        int kLimiterSidechainMuxInputIdx = kLimiterSidechainMuxInputOffset + synthPartIdx;

        connections.push_back(
          new AudioConnection(synth->getPolySynth(),
                              SynthVoice::kPostFaderLeftChannelOffset + synthPartIdx,
                              output->getLimiterSidechainMux(AudioOutput::OutputChannels::kLeft),
                              kLimiterSidechainMuxInputIdx));
        connections.push_back(
          new AudioConnection(synth->getPolySynth(),
                              SynthVoice::kPostFaderRightChannelOffset + synthPartIdx,
                              output->getLimiterSidechainMux(AudioOutput::OutputChannels::kRight),
                              kLimiterSidechainMuxInputIdx));

        static constexpr int kSynthUsbOffset = 20;
        int leftSynthUsbIdx =
          kSynthUsbOffset + AudioOutput::OutputChannels::kChannelsCount * synthPartIdx;
        int rightSynthUsbIdx = leftSynthUsbIdx + 1;

        connections.push_back(new AudioConnection(
          synth->getPanningAmplifier(synthPartIdx, SynthVoice::Channels::kLeft), //
          0,
          output->getUsbStream(),
          leftSynthUsbIdx));
        connections.push_back(new AudioConnection(
          synth->getPanningAmplifier(synthPartIdx, SynthVoice::Channels::kRight),
          0,
          output->getUsbStream(),
          rightSynthUsbIdx));
    }
}

#endif
void audioEngine::connectSpecialInputsToOutput(AudioOutput* output)
{
    for (int channel = 0; channel < AudioOutput::OutputChannels::kChannelsCount; channel++)
    {
        connections.push_back(
          new AudioConnection(&playerWavFromSD,
                              channel,
                              output->getFinalMixer((AudioOutput::OutputChannels)channel),
                              AudioOutput::FinalMixInputs::kSdCardPreview));
        connections.push_back(
          new AudioConnection(&testWaveform,
                              0,
                              output->getFinalMixer((AudioOutput::OutputChannels)channel),
                              AudioOutput::FinalMixInputs::kTestWaveform));
        connections.push_back(
          new AudioConnection(&metronomeTick,
                              0,
                              output->getFinalMixer((AudioOutput::OutputChannels)channel),
                              AudioOutput::FinalMixInputs::kMetronomeTick));
    }
}

void audioEngine::connectVoiceToExportQueue(AudioVoice* voice, ExportQueues* exportQueues, int idx)
{
    connections.push_back(new AudioConnection(
      voice->getPanningAmplifier(AudioVoice::VoiceChannels::kLeft),
      0,
      exportQueues->getQueue(ExportQueues::ExportChannels::kLeft,
                             (mtAudioExport::enStem)((int)mtAudioExport::stem_tracks_start + idx)),
      0));

    connections.push_back(new AudioConnection(
      voice->getPanningAmplifier(AudioVoice::VoiceChannels::kRight),
      0,
      exportQueues->getQueue(ExportQueues::ExportChannels::kRight,
                             (mtAudioExport::enStem)((int)mtAudioExport::stem_tracks_start + idx)),
      0));
}

void audioEngine::connectMixerToExportQueue(MixerAndFxs<kVoices>* mixerAndFxs,
                                            ExportQueues* exportQueues)
{

    connections.push_back(
      new AudioConnection(mixerAndFxs->getPolyverb(),
                          ExportQueues::ExportChannels::kLeft,
                          exportQueues->getQueue(ExportQueues::ExportChannels::kLeft,
                                                 mtAudioExport::enStem::stem_reverb),
                          0));

    connections.push_back(
      new AudioConnection(mixerAndFxs->getPolyverb(),
                          ExportQueues::ExportChannels::kRight,
                          exportQueues->getQueue(ExportQueues::ExportChannels::kRight,
                                                 mtAudioExport::enStem::stem_reverb),
                          0));

    connections.push_back(
      new AudioConnection(mixerAndFxs->getDelay(),
                          ExportQueues::ExportChannels::kLeft,
                          exportQueues->getQueue(ExportQueues::ExportChannels::kLeft,
                                                 mtAudioExport::enStem::stem_delay),
                          0));

    connections.push_back(
      new AudioConnection(mixerAndFxs->getDelay(),
                          ExportQueues::ExportChannels::kRight,
                          exportQueues->getQueue(ExportQueues::ExportChannels::kRight,
                                                 mtAudioExport::enStem::stem_delay),
                          0));
}

void audioEngine::connectOutputToExportQueue(AudioOutput* output, ExportQueues* exportQueues)
{
    connections.push_back(
      new AudioConnection(output->getFinalMixer(AudioOutput::OutputChannels::kLeft),
                          exportQueues->getQueue(ExportQueues::ExportChannels::kLeft,
                                                 mtAudioExport::enStem::stem_master)));
    connections.push_back(
      new AudioConnection(output->getFinalMixer(AudioOutput::OutputChannels::kRight),
                          exportQueues->getQueue(ExportQueues::ExportChannels::kRight,
                                                 mtAudioExport::enStem::stem_master)));
    connections.push_back(
      new AudioConnection(output->getFinalMixer(AudioOutput::OutputChannels::kLeft),
                          exportQueues->getChannelRms(ExportQueues::ExportChannels::kLeft)));

    connections.push_back(
      new AudioConnection(output->getFinalMixer(AudioOutput::OutputChannels::kRight),
                          exportQueues->getChannelRms(ExportQueues::ExportChannels::kRight)));
}

AUDIO_CORE_RAM void audioEngine::endAllFx(bool synthOnly)
{
    if (!synthOnly)
    {
        for (uint8_t i = 0; i < 8; i++)
        {
            instrumentPlayer[i].endFx(instrumentPlayer[i].lastSeqFx[0], 0);
            instrumentPlayer[i].endFx(instrumentPlayer[i].lastSeqFx[1], 1);
        }
    }
#ifdef FEATURE_SYNTH
    for (int synthIdx = 0; synthIdx < kSynthParts; synthIdx++)
    {
        for (auto& fxIdAndSynthParam : fxIdToSynthParam)
        {
            endSynthFx(fxIdAndSynthParam.first, 0, synthIdx);
            endSynthFx(fxIdAndSynthParam.first, 1, synthIdx);
        }
    }
#endif
}

void audioEngine::performanceModeEndAll()
{
    for (uint8_t i = 0; i < 8; i++)
    {
        instrumentPlayer[i].endCutoffPerformanceMode();
        instrumentPlayer[i].endEndPointPerformanceMode();
        instrumentPlayer[i].endFilterTypePerformanceMode();
        instrumentPlayer[i].endGranularPositionPerformanceMode();
        instrumentPlayer[i].endPanningPerformanceMode();
        instrumentPlayer[i].endDelaySendPerformanceMode();
        instrumentPlayer[i].endReverbSendPerformanceMode();
        instrumentPlayer[i].endSamplePlaybackPerformanceMode();
        instrumentPlayer[i].endStartPointPerformanceMode();
        instrumentPlayer[i].endTunePerformanceMode();
        instrumentPlayer[i].endVolumePerformanceMode();
        instrumentPlayer[i].endWavetableWindowPerformanceMode();
        instrumentPlayer[i].endAmpLfoRatePerformanceMode();
        instrumentPlayer[i].endCutoffLfoRatePerformanceMode();
        instrumentPlayer[i].endPositionLfoRatePerformanceMode();
        instrumentPlayer[i].endPanningLfoRatePerformanceMode();
        instrumentPlayer[i].endFinetuneLfoRatePerformanceMode();
        instrumentPlayer[i].endBitDepthPerformanceMode();
        instrumentPlayer[i].endOverdrivePerformanceMode();
    }
}

void audioEngine::refreshOutputSourceOutsideTheRecorder()
{
    bool isUsbOutputAndInput = mtConfig.common.usbAudioMode == 3;

    // to handle  usb feedback loop
    if (isUsbOutputAndInput && usb->isAttached())
    {
        setOutputSource(audioEngine::OutputSourceType::kUsbInput);
    }
    else
    {
        setOutputSource(audioEngine::OutputSourceType::kRegularMix);
    }
}

void audioEngine::forceUsbGainOutsideTheRecorder()
{
    input.getUsbRecordingAmplifier(AudioInput::InputChannels::kLeft)->gain(1.0f);
    input.getUsbRecordingAmplifier(AudioInput::InputChannels::kRight)->gain(1.0f);
}

void audioEngine::refreshTrackVolume()
{
    for (uint8_t track = 0; track < 8; track++)
    {
        instrumentPlayer[track].setRefreshBit(VOLUME_MASK);
    }
}
void audioEngine::refreshReverbVolume()
{
    for (uint8_t track = 0; track < 8; track++)
    {
        instrumentPlayer[track].setRefreshBit(REVERB_SEND_MASK);
    }
#ifdef FEATURE_SYNTH
    refreshSynthParam(SynthParam::kReverbSend);
#endif
}
void audioEngine::refreshDelayVolume()
{
    for (uint8_t track = 0; track < 8; track++)
    {
        instrumentPlayer[track].setRefreshBit(DELAY_SEND_MASK);
    }
#ifdef FEATURE_SYNTH
    refreshSynthParam(SynthParam::kDelaySend);
#endif
}

void audioEngine::refreshAllSends()
{
    refreshReverbVolume();
    refreshDelayVolume();
    refreshLineInStreamDelaySend();
    refreshLineInStreamReverbSend();
}

void audioEngine::refreshLineInStreamVolume()
{
    if (mtProject.values.lineInMute)
    {
        tracksAndFxsMixer[(int)MixerAndFxChannelType::kLeft]->gain(3, AMP_MUTED);
        tracksAndFxsMixer[(int)MixerAndFxChannelType::kRight]->gain(3, AMP_MUTED);
    }
    else
    {
        tracksAndFxsMixer[(int)MixerAndFxChannelType::kLeft]->gain(
          3, mixAmpLogValues[mtProject.values.lineInVolume]);
        tracksAndFxsMixer[(int)MixerAndFxChannelType::kRight]->gain(
          3, mixAmpLogValues[mtProject.values.lineInVolume]);
    }
}

void audioEngine::forceLineInStreamVolume(uint8_t value)
{
    tracksAndFxsMixer[(int)MixerAndFxChannelType::kLeft]->gain(3, mixAmpLogValues[value]);
    tracksAndFxsMixer[(int)MixerAndFxChannelType::kRight]->gain(3, mixAmpLogValues[value]);
}

void audioEngine::refreshLineInStreamDelaySend()
{
    float gain = AMP_MUTED;

    if (!mtProject.values.lineInDelaySendMute && !mtProject.values.delayMute)
    {
        if (mtProject.values.sendsMode == sendsModePreFader)
        {
            gain = mixAmpLogValues[mtProject.values.lineInDelaySend] / 2;
        }
        else
        {
            if (!mtProject.values.lineInMute)
            {
                gain = mixAmpLogValues[mtProject.values.lineInVolume] *
                       mixAmpLogValues[mtProject.values.lineInDelaySend] / 2;
            }
        }
    }

    if (gain < 0.01)
    {
        gain = 0;
    }

    preDelayMixer->gain(8, gain);
    preDelayMixer->gain(9, gain);
}

void audioEngine::refreshLineInStreamReverbSend()
{
    float gain = AMP_MUTED;

    if (!mtProject.values.lineInReverbSendMute && !mtProject.values.reverbMute)
    {
        if (mtProject.values.sendsMode == sendsModePreFader)
        {
            gain = mixAmpLogValues[mtProject.values.lineInReverbSend] / 2;
        }
        else
        {
            if (!mtProject.values.lineInMute)
            {
                gain = mixAmpLogValues[mtProject.values.lineInVolume] *
                       mixAmpLogValues[mtProject.values.lineInReverbSend] / 2;
            }
        }
    }

    if (gain < 0.01f)
    {
        gain = 0;
    }

    prePolyverbMixer->gain(8, gain);
    prePolyverbMixer->gain(9, gain);
}

void audioEngine::disableLineInImpact()
{
    tracksAndFxsMixer[(int)MixerAndFxChannelType::kLeft]->gain(3, AMP_MUTED);
    tracksAndFxsMixer[(int)MixerAndFxChannelType::kRight]->gain(3, AMP_MUTED);
    preDelayMixer->gain(8, AMP_MUTED);
    preDelayMixer->gain(9, AMP_MUTED);
    prePolyverbMixer->gain(8, AMP_MUTED);
    prePolyverbMixer->gain(9, AMP_MUTED);
}
void audioEngine::enableLineInImpact()
{
    refreshLineInStreamVolume();
    refreshLineInStreamDelaySend();
    refreshLineInStreamReverbSend();
}

void audioEngine::setHeadphonesVolume(uint8_t value)
{
    codec->setMasterVolume(value / 100.0f);
}
void audioEngine::muteHeadphones()
{
    codec->setMasterVolume(0.0f);
}
void audioEngine::unmuteHeadphones()
{
    codec->setMasterVolume((float)mtConfig.audioCodecConfig.volume / 100.0f);
}

void audioEngine::setPreviewVolume(uint8_t value)
{
    finalMixer[AudioOutput::OutputChannels::kLeft]->gain(
      AudioOutput::FinalMixInputs::kSdCardPreview, mixAmpLogValues[value]);
    finalMixer[AudioOutput::OutputChannels::kRight]->gain(
      AudioOutput::FinalMixInputs::kSdCardPreview, mixAmpLogValues[value]);
}

void audioEngine::setInterpolationEnable(bool value)
{
    for (uint8_t i = 0; i < 8; i++)
    {
        instrumentPlayer[i].setInterpolationEnable(value);
    }
}

void audioEngine::setDelayFeedback(uint8_t value)
{
    delay->setFeedback(value / 100.0);
}

void audioEngine::setDelayTime(uint16_t value)
{
    delay->setTime(value);
}

int audioEngine::getDelayTime()
{
    return delay->getTime();
}

void audioEngine::setDelayFilterFrequency(uint16_t value)
{
    delay->setFilterFrequency(value);
}

AUDIO_CORE_RAM void audioEngine::setDelayParams(uint8_t value)
{
    delay->setPingpongEnable(value & 0b10000000);
    delay->setSyncEnable(value & 0b01000000);
    delay->setRate(value & 0b00111111);
}
void audioEngine::setReverbSize(float value)
{
    polyverb->setTime(value);
}
void audioEngine::setReverbDamp(float value)
{
    polyverb->setDamp(1.0f - value);
}
void audioEngine::setReverbPredelay(float value)
{
    polyverb->SetPredelayLength(value);
}
void audioEngine::setReverbDiffusion(float value)
{
    polyverb->SetDiffusion(value);
}

void audioEngine::setLimiterAttack(uint16_t attack)
{
    limiter[AudioOutput::OutputChannels::kLeft]->setAttack(attack);
    limiter[AudioOutput::OutputChannels::kRight]->setAttack(attack);
}
void audioEngine::setLimiterRelease(float release)
{
    limiter[AudioOutput::OutputChannels::kLeft]->setRelease(release);
    limiter[AudioOutput::OutputChannels::kRight]->setRelease(release);
}
void audioEngine::setLimiterThreshold(uint16_t threshold)
{
    limiter[AudioOutput::OutputChannels::kLeft]->setThreshold(threshold);
    limiter[AudioOutput::OutputChannels::kRight]->setThreshold(threshold);
}

void audioEngine::setLimiterSidechain(uint8_t sidechain)
{
    uint8_t mux = sidechain ? sidechain - 1 : 0;

    output.getLimiterSidechainMux(AudioOutput::OutputChannels::kLeft)->setOut(mux);
    output.getLimiterSidechainMux(AudioOutput::OutputChannels::kLeft)->setEnabled(sidechain != 0);
    output.getLimiterSidechainMux(AudioOutput::OutputChannels::kRight)->setOut(mux);
    output.getLimiterSidechainMux(AudioOutput::OutputChannels::kRight)->setEnabled(sidechain != 0);
    __atomic_signal_fence(__ATOMIC_SEQ_CST);
    limiter[AudioOutput::OutputChannels::kLeft]->setSidechainEnable((bool)sidechain);
    limiter[AudioOutput::OutputChannels::kRight]->setSidechainEnable((bool)sidechain);
}
#ifdef FEATURE_SYNTH

AUDIO_CORE_RAM bool audioEngine::isSynthFx(uint8_t fx_id)
{
    switch (fx_id)
    {
        case Sequencer::strFxConsts::FX_TYPE_OFF:
        case Sequencer::strFxConsts::FX_TYPE_PANNING:
        case Sequencer::strFxConsts::FX_TYPE_DELAY_SEND:
        case Sequencer::strFxConsts::FX_TYPE_SEND_CC_A:
        case Sequencer::strFxConsts::FX_TYPE_SEND_CC_B:
        case Sequencer::strFxConsts::FX_TYPE_SEND_CC_C:
        case Sequencer::strFxConsts::FX_TYPE_SEND_CC_D:
        case Sequencer::strFxConsts::FX_TYPE_SEND_CC_E:
        case Sequencer::strFxConsts::FX_TYPE_SEND_CC_F:
        case Sequencer::strFxConsts::FX_TYPE_REVERB_SEND: return true;
        default: return false;
    }
}

AUDIO_CORE_RAM void audioEngine::seqSynthFx(uint8_t fx_id,
                                            uint8_t fx_val,
                                            uint8_t fx_n,
                                            uint8_t synthIdx)
{

    if (!isSynthFx(fx_id))
    {
        return;
    }

    if (fx_id == Sequencer::strFxConsts::FX_TYPE_OFF)
    {
        if (lastSynthFxId[synthIdx][fx_n].has_value())
        {
            endSynthFx(lastSynthFxId[synthIdx][fx_n].value(), fx_n, synthIdx);
        }

        lastSynthFxId[synthIdx][fx_n].reset();
        return;
    }

    lastSynthFxId[synthIdx][fx_n] = fx_id;

    float max = sequencer.getFxMax(fx_id);
    float min = sequencer.getFxMin(fx_id);

    float value = mapClamped<float>(fx_val,
                                    min,
                                    max,
                                    synthParamsBoundaries[(int)fxIdToSynthParam[fx_id]].min,
                                    synthParamsBoundaries[(int)fxIdToSynthParam[fx_id]].max);

    setSynthParam(fxIdToSynthParam[fx_id], synthIdx, value, fxNumberToSynthValuesSource[fx_n]);
}

AUDIO_CORE_RAM void audioEngine::endSynthFx(uint8_t fx_id, uint8_t fx_n, uint8_t synthIdx)
{
    if (!isSynthFx(fx_id))
    {
        return;
    }

    cleanSynthParam(fxIdToSynthParam[fx_id], synthIdx, fxNumberToSynthValuesSource[fx_n]);
}

void audioEngine::refreshSynthInstrumentParams(SynthPartSettings& synthSettings, int synthIdx)
{
    setSynthParam(
      SynthParam::kVolume, synthIdx, synthSettings.gainDb, SynthValuesSource::kInstrument);
    setSynthParam(
      SynthParam::kPanning, synthIdx, synthSettings.panning, SynthValuesSource::kInstrument);
    setSynthParam(
      SynthParam::kReverbSend, synthIdx, synthSettings.reverbSend, SynthValuesSource::kInstrument);
    setSynthParam(
      SynthParam::kDelaySend, synthIdx, synthSettings.delaySend, SynthValuesSource::kInstrument);

    int macroIdx = 0;
    for (auto macroVal : synthSettings.macroValue)
    {
        SynthParam param = (SynthParam)(SynthParam::kMacro1 + macroIdx);
        setSynthParam(param, synthIdx, macroVal, SynthValuesSource::kInstrument);
        macroIdx++;
    }
}

float audioEngine::getSynthVoiceRMS(uint8_t channel, int synthIdx)
{
    if (channel > 1)
        channel = 1;
    float localVal = -1.0f;
    AudioAnalyzeRMS* rms = mixerAndFxs.getSynthRMS(synthIdx, (MixerAndFxChannelType)channel);
    if (rms->available())
    {
        localVal = rms->read();
        if (localVal < 0.01f)
            localVal = 0.0f;
    }
    return localVal;
}

void audioEngine::setSynthParam(SynthParam param,
                                int synthIdx,
                                float value,
                                SynthValuesSource source)
{
    DISABLE_AUDIO_INTERRUPTS();
    synthParams[synthIdx][(int)param][(int)source] = value;
    isSynthParamDirty[synthIdx][(int)param] = true;
    ENABLE_AUDIO_INTERRUPTS();
}

void audioEngine::refreshSynthParam(SynthParam param)
{
    DISABLE_AUDIO_INTERRUPTS();
    for (int synthIdx = 0; synthIdx < kSynthParts; synthIdx++)
    {
        isSynthParamDirty[synthIdx][(int)param] = true;
    }
    ENABLE_AUDIO_INTERRUPTS();
}

void audioEngine::cleanSynthParam(SynthParam param, int synthIdx, SynthValuesSource source)
{
    DISABLE_AUDIO_INTERRUPTS();
    synthParams[synthIdx][(int)param][(int)source] = std::nullopt;
    isSynthParamDirty[synthIdx][(int)param] = true;
    ENABLE_AUDIO_INTERRUPTS();
}

AUDIO_CORE_RAM void audioEngine::setSynthDelaySend(int synthIdx, float value)
{
    float gain = 1.0f;

    if (mtProject.values.sendsMode == sendsModePreFader)
    {
        gain = mtProject.values.delayMute
                 ? AMP_MUTED
                 : sendsAmpLogValues[(int)value] * mixAmpLogValues[mtProject.values.delayVolume];
    }
    else if (mtProject.values.sendsMode == sendsModePostFader)
    {
        float gainFromDelay =
          sendsAmpLogValues[(int)value] * mixAmpLogValues[mtProject.values.delayVolume];
        float gainFromVolume =
          polysynth::sdk::dbToAmplitude(calculateParamValue(synthIdx, SynthParam::kVolume));

        gain = mtProject.values.delayMute ? AMP_MUTED : gainFromDelay * gainFromVolume;
    }

    int mixerInputIdx = 2 + 2 * kVoices + synthIdx;
    mixerAndFxs.getPreFxMixer(MixerAndFxs<kVoices>::FxType::kDelay)->gain(mixerInputIdx, gain);
}

AUDIO_CORE_RAM void audioEngine::setSynthReverbSend(int synthIdx, float value)
{
    float gain = 1.0f;

    if (mtProject.values.sendsMode == sendsModePreFader)
    {
        gain = mtProject.values.reverbMute
                 ? AMP_MUTED
                 : sendsAmpLogValues[(int)value] * mixAmpLogValues[mtProject.values.reverbVolume];
    }
    else if (mtProject.values.sendsMode == sendsModePostFader)
    {
        float gainFromReverb =
          sendsAmpLogValues[(int)value] * mixAmpLogValues[mtProject.values.reverbVolume];
        float gainFromVolume =
          polysynth::sdk::dbToAmplitude(calculateParamValue(synthIdx, SynthParam::kVolume));

        gain = mtProject.values.reverbMute ? AMP_MUTED : gainFromReverb * gainFromVolume;
    }

    int mixerInputIdx = 2 + 2 * kVoices + synthIdx;
    mixerAndFxs.getPreFxMixer(MixerAndFxs<kVoices>::FxType::kReverb)->gain(mixerInputIdx, gain);
}

AUDIO_CORE_RAM void audioEngine::setSynthPanning(int synthIdx, float value)
{
    float gainR = mapClamped<float>(value, -100.0f, 0.0f, 0.0f, 1.0f);
    float gainL = mapClamped<float>(value, 0.0f, 100.0f, 1.0f, 0.0f);

    synthVoice.getPanningAmplifier(synthIdx, SynthVoice::Channels::kLeft)->gain(gainL);
    synthVoice.getPanningAmplifier(synthIdx, SynthVoice::Channels::kRight)->gain(gainR);
}

AUDIO_CORE_RAM void audioEngine::setSynthGain(int synthIdx, float value)
{
    synthVoice.getPolySynth()->setGain(synthIdx, value);
    if (mtProject.values.sendsMode == sendsModePostFader)
    {
        refreshSynthParam(SynthParam::kReverbSend);
        refreshSynthParam(SynthParam::kDelaySend);
    }
}

AUDIO_CORE_RAM void audioEngine::setSynthMacro(int synthIdx, int macroIdx, float value)
{
    assert(synthIdx < kSynthParts);
    assert(macroIdx < kSynthMacrosCount);

    int ccIdx = SynthVoice::kMacroCCIdxStart + macroIdx;
    sendCC(ccIdx, value, synthIdx + 1, true);
}

AUDIO_CORE_RAM void audioEngine::updateTrackDirectGain(uint8_t track)
{
    const float gain = (routedTracks & (1 << track)) ? 0.0f : 1.0f;
    mixerAndFxs.getTracksMixer(MixerAndFxChannelType::kLeft)->gain(track, gain);
    mixerAndFxs.getTracksMixer(MixerAndFxChannelType::kRight)->gain(track, gain);
}

AUDIO_CORE_RAM void audioEngine::updateRoutedTracks()
{
    const uint8_t routed = synthVoice.getPolySynth()->getRoutedTracks();
    const uint8_t changed = routed ^ routedTracks;
    routedTracks = routed;

    for (int track = 0; track < kVoices; track++)
    {
        if (changed & (1 << track))
        {
            updateTrackDirectGain(track);
        }
    }
}

AUDIO_CORE_RAM void audioEngine::updateSynthParams()
{
    for (int synthIdx = 0; synthIdx < kSynthParts; synthIdx++)
    {
        if (isSynthParamDirty[synthIdx].none())
        {
            continue;
        }
        for (int paramIdx = 0; paramIdx < SynthParam::kSynthParamsCount; paramIdx++)
        {
            updateSynthParam(synthIdx, (SynthParam)paramIdx);
        }
    }
}

AUDIO_CORE_RAM void audioEngine::updateSynthParam(int synthIdx, SynthParam param)
{
    if (isSynthParamDirty[synthIdx][param])
    {
        float result = calculateParamValue(synthIdx, param);

        applySynthValue(synthIdx, param, result);

        isSynthParamDirty[synthIdx][param] = false;
    }
}

AUDIO_CORE_RAM void audioEngine::applySynthValue(int synthIdx, SynthParam param, float value)
{
    switch (param)
    {
        case SynthParam::kDelaySend: setSynthDelaySend(synthIdx, value); break;
        case SynthParam::kReverbSend: setSynthReverbSend(synthIdx, value); break;
        case SynthParam::kPanning: setSynthPanning(synthIdx, value); break;
        case SynthParam::kVolume: setSynthGain(synthIdx, value); break;

        case SynthParam::kMacro1:
        case SynthParam::kMacro2:
        case SynthParam::kMacro3:
        case SynthParam::kMacro4:
        case SynthParam::kMacro5:
        case SynthParam::kMacro6:
        {
            int macroIdx = param - SynthParam::kMacro1;
            setSynthMacro(synthIdx, macroIdx, value);
        }
        break;
        default: assert(false); break;
    }
}

float audioEngine::calculateParamValue(int synthIdx, SynthParam param)
{
    auto& synthParamSource = synthParams[synthIdx][param];

    if (!synthParamSource[SynthValuesSource::kInstrument].has_value())
    {
        return 0.0f;
    }

    float result = synthParamSource[SynthValuesSource::kInstrument].value();

    if (synthParamSource[SynthValuesSource::kFx2].has_value())
    {
        result = synthParamSource[SynthValuesSource::kFx2].value();
    }

    if (synthParamSource[SynthValuesSource::kFx1].has_value())
    {
        result = synthParamSource[SynthValuesSource::kFx1].value();
    }

    if (synthParamSource[SynthValuesSource::kPerformance].has_value())
    {
        result = result + synthParamSource[SynthValuesSource::kPerformance].value();
    }

    assert(synthParamsBoundaries[SynthParam::kSynthParamsCount - 1].max !=
           synthParamsBoundaries[SynthParam::kSynthParamsCount - 1].min);

    result =
      clamp<float>(result, synthParamsBoundaries[param].min, synthParamsBoundaries[param].max);

    return result;
}

#endif

AUDIO_CORE_RAM void audioEngine::setLastUsedVoice(uint8_t v)
{
    lastUsedVoice = v;
}
uint8_t audioEngine::getLastUsedVoice()
{
    return lastUsedVoice;
}

AUDIO_CORE_RAM void audioEngine::makeMetronomeTick(uint8_t accent)
{
    finalMixer[AudioOutput::OutputChannels::kLeft]->gain(
      AudioOutput::FinalMixInputs::kMetronomeTick, sendsAmpLogValues[mtConfig.metronome.volume]);
    finalMixer[AudioOutput::OutputChannels::kRight]->gain(
      AudioOutput::FinalMixInputs::kMetronomeTick, sendsAmpLogValues[mtConfig.metronome.volume]);

    metronomeTick.envelopeSetPassFlag(1);

    metronomeTick.envelopeDelay(0);
    metronomeTick.envelopeAttack(0);
    metronomeTick.envelopeHold(0);
    metronomeTick.envelopeDecay(0);
    metronomeTick.envelopeSustain(1.0f);
    metronomeTick.envelopeRelease(0);
    metronomeTick.envelopeSetLoop(0);

    metronomeTick.envelopeSetIsRandom(false);
    metronomeTick.envelopeNoteOnForPrev(
      (int16_t*)metronomeBeep, sizeof(metronomeBeep) / sizeof(int16_t), 30, 0);

    if (accent)
        metronomeTick.setFineTune(100, 30);
    else
        metronomeTick.setFineTune(45, 30);
    // Pykniecie jest probkowane 8kHz(oszczednosc pamieci), nuta 30 i finetune 45 wynikaja z
    // koniecznosci zamienienia pitcha na odpowiedni dla 44.1 kHz
}

void audioEngine::setBlockedInstrument(int8_t idx)
{
    if ((idx < 0) || (idx >= 48))
        return;
    isBlockedInstrument[idx] = 1;
}
void audioEngine::clearBlockedInstrument(int8_t idx)
{
    if ((idx < 0) || (idx >= 48))
        return;
    isBlockedInstrument[idx] = 0;
}

void audioEngine::setBlockedNoteOnFromInstrument(int8_t idx)
{
    if ((idx < 0) || (idx >= 48))
        return;
    isBlockedNoteOn[idx] = 1;
}
void audioEngine::clearBlockedNoteOnFromInstrument(int8_t idx)
{
    if ((idx < 0) || (idx >= 48))
        return;
    isBlockedNoteOn[idx] = 0;
}

void audioEngine::blockAllNoteOn()
{
    for (uint8_t i = 0; i < kSampleInstrumentsCount; i++)
    {
        setBlockedNoteOnFromInstrument(i);
    }
}
void audioEngine::unblockAllNoteOn()
{
    for (uint8_t i = 0; i < kSampleInstrumentsCount; i++)
    {
        clearBlockedNoteOnFromInstrument(i);
    }
}

float audioEngine::getDelayAverageRMS()
{
    float localVal = -1.0f;

    if (delayRms[(int)MixerAndFxChannelType::kLeft]->available() &&
        delayRms[(int)MixerAndFxChannelType::kRight]->available())
    {
        localVal = (delayRms[(int)MixerAndFxChannelType::kLeft]->read() +
                    delayRms[(int)MixerAndFxChannelType::kRight]->read()) /
                   2.0f;
    }
    return localVal;
}

float audioEngine::getReverbAverageRMS()
{
    float localVal = -1.0f;
    if (polyverbRms[(int)MixerAndFxChannelType::kLeft]->available() &&
        polyverbRms[(int)MixerAndFxChannelType::kRight]->available())
    {
        localVal = (polyverbRms[(int)MixerAndFxChannelType::kLeft]->read() +
                    polyverbRms[(int)MixerAndFxChannelType::kRight]->read()) /
                   2.0f;
    }
    return localVal;
}

float audioEngine::getDryMixAverageRMS()
{
    float localVal = -1.0f;
    if (dryMixRms[(int)MixerAndFxChannelType::kLeft]->available() &&
        dryMixRms[(int)MixerAndFxChannelType::kRight]->available())
    {
        localVal = (dryMixRms[(int)MixerAndFxChannelType::kLeft]->read() +
                    dryMixRms[(int)MixerAndFxChannelType::kRight]->read()) /
                   2.0f;
    }
    return localVal;
}
// 0  - left channel, 1 - right channel
float audioEngine::getDelayRMS(uint8_t channel)
{
    if (channel > 1)
        channel = 1;
    float localVal = -1.0f;

    if (delayRms[channel]->available())
    {
        localVal = delayRms[channel]->read();
    }
    return localVal;
}
// 0  - left channel, 1 - right channel
float audioEngine::getReverbRMS(uint8_t channel)
{
    if (channel > 1)
        channel = 1;
    float localVal = -1.0f;

    if (polyverbRms[channel]->available())
    {
        localVal = polyverbRms[channel]->read();
    }
    return localVal;
}
// 0  - left channel, 1 - right channel
float audioEngine::getLineInRMS(uint8_t channel)
{
    assert(channel <= 1);
    float localVal = -1.0f;
    AudioAnalyzeRMS* rms = input.getStreamRmsI2s((AudioInput::InputChannels)channel);
    if (rms->available())
    {
        localVal = rms->read();
        if (localVal < 0.01f)
        {
            localVal = 0.0f;
        }
    }
    return localVal;
}
// 0  - left channel, 1 - right channel
float audioEngine::getDryMixRMS(uint8_t channel)
{
    float localVal = -1.0f;

    if (dryMixRms[channel]->available())
    {
        localVal = dryMixRms[channel]->read();
    }

    return localVal;
}

void audioEngine::disconnectMixerImpact()
{
    storeMixerValues();
    resetMixerValues();
}
void audioEngine::connectMixerImpact()
{
    restoreMixerValues();
}


void audioEngine::cutAllVolumeEnvelopes()
{
    for (uint8_t player = 0; player < 8; player++)
    {
        instrumentPlayer[player].noteOffCut();
    }
}

void audioEngine::setLineInOutType(uint8_t type)
{
    input.getStereoToMono()->setOutSignalType((AudioStereoToMono::OutSignalType)type);
}

void audioEngine::setHeadroomEnable(bool enabled)
{
    AudioPlayMemory::enableHeadroom(enabled);
}

void audioEngine::setSnapToZeroEnable(bool enabled)
{
    AudioPlayMemory::enableSnapToZero(enabled);
}

void audioEngine::setSaturationType(SaturationType type)
{
    getMixerAndFx()->getTracksMixer(MixerAndFxChannelType::kLeft)->setSaturationType(type);
    getMixerAndFx()->getTracksMixer(MixerAndFxChannelType::kRight)->setSaturationType(type);

    getMixerAndFx()->getTracksAndSendsMixer(MixerAndFxChannelType::kLeft)->setSaturationType(type);
    getMixerAndFx()->getTracksAndSendsMixer(MixerAndFxChannelType::kRight)->setSaturationType(type);
}

void audioEngine::setOutputSource(OutputSourceType source)
{
    getAudioOutput()->getStream()->selectSource(0, source);
    getAudioOutput()->getStream()->selectSource(1, source);
}

void audioEngine::setInputConfigOutsideRecorderInterfaceModule()
{
    codec->setInputType(Sgtl5000::InputType::kLineIn);
    codec->setLineInLevel(0);
#ifndef TRACKER_MINI
    radio->enableLineInput(false);
#endif

    refreshLineInStreamVolume();
}

void audioEngine::storeMixerValues()
{
    for (uint8_t track = 0; track < 8; track++)
    {
        mixerValuesStore.trackVolume[track] = mtProject.values.trackVolume[track];
    }

    mixerValuesStore.dryMixVolume = mtProject.values.dryMixVolume;
    mixerValuesStore.reverbVolume = mtProject.values.reverbVolume;
    mixerValuesStore.delayVolume = mtProject.values.delayVolume;
}

void audioEngine::resetMixerValues()
{
    for (uint8_t track = 0; track < 8; track++)
    {
        mtProject.values.trackVolume[track] = DEFAULT_TRACK_VOLUME;
    }

    mtProject.values.dryMixVolume = DEFAULT_DRY_MIX_VOLUME;
    mtProject.values.reverbVolume = DEFAULT_REVERB_VOLUME;
    mtProject.values.delayVolume = DEFAULT_DELAY_VOLUME;
}

void audioEngine::restoreMixerValues()
{
    for (uint8_t track = 0; track < 8; track++)
    {
        mtProject.values.trackVolume[track] = mixerValuesStore.trackVolume[track];
    }

    mtProject.values.dryMixVolume = mixerValuesStore.dryMixVolume;
    mtProject.values.reverbVolume = mixerValuesStore.reverbVolume;
    mtProject.values.delayVolume = mixerValuesStore.delayVolume;
}

playerEngine::playerEngine()
{
    // bazuje na zadeklarowanych wyzej obiektach silnika i na tym ze obiekty playerEngine beda
    // umieszczone w tablicy(instrumentPlayer) na tej podstawie jest wyznaczany index(nChannel)
    nChannel = ((uint32_t)this - (uint32_t)instrumentPlayer) / sizeof(playerEngine);

    for (int channel = 0; channel < AudioVoice::VoiceChannels::kChannelsCount; channel++)
    {
        playMemPtr.push_back(audioVoice.getMemoryPlayer((AudioVoice::VoiceChannels)channel));
        filterPtr.push_back(audioVoice.getFilter((AudioVoice::VoiceChannels)channel));
        bitDepthPtr.push_back(audioVoice.getBitDepth((AudioVoice::VoiceChannels)channel));
        ampPtr.push_back(audioVoice.getAmplifier((AudioVoice::VoiceChannels)channel));
        panningAmplifierPtr.push_back(
          audioVoice.getPanningAmplifier((AudioVoice::VoiceChannels)channel));
        rmsPtr.push_back(audioVoice.getRMS((AudioVoice::VoiceChannels)channel));
    }
    envelopePtr[envCutoff] =
      audioVoice.getEnvelopeGenerator(AudioVoice::EnvelopeGeneratorType::kFilter);
    envelopePtr[envWtPos] =
      audioVoice.getEnvelopeGenerator(AudioVoice::EnvelopeGeneratorType::kWavetablePosition);
    envelopePtr[envPan] =
      audioVoice.getEnvelopeGenerator(AudioVoice::EnvelopeGeneratorType::kPanning);
    envelopePtr[envGranPos] =
      audioVoice.getEnvelopeGenerator(AudioVoice::EnvelopeGeneratorType::kGranularPosition);
    envelopePtr[envFinetune] =
      audioVoice.getEnvelopeGenerator(AudioVoice::EnvelopeGeneratorType::kFinetune);

    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->envelopeReleaseNoteOn(RELEASE_NOTE_ON_VAL);

        if (channel == 0)
        {
            playMemPtr[channel]->setSamplePointsSource(nullptr);
        }
        else
        {
            playMemPtr[channel]->setSamplePointsSource(playMemPtr[0]);
        }
    }
}

AUDIO_CORE_RAM void playerEngine ::modGlide(uint16_t value)
{
    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setGlide(value, currentNote, currentInstrument_idx);
    }
}

AUDIO_CORE_RAM void playerEngine ::modFineTune(int8_t value)
{
    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setFineTune(value, currentNote);
    }
}

AUDIO_CORE_RAM void playerEngine ::modPanning(int16_t value)
{
    float gainR = 0, gainL = 0;
    if (value < 50)
    {
        gainR = (0 + value) / 50.0;
        gainL = 1.0;
    }
    else if (value > 50)
    {
        gainR = 1.0;
        gainL = (100 - value) / 50.0;
    }
    else if (value == 50)
    {
        gainR = 1.0;
        gainL = 1.0;
    }
    panningAmplifierPtr[(int)MixerAndFxChannelType::kLeft]->gain(gainL);
    panningAmplifierPtr[(int)MixerAndFxChannelType::kRight]->gain(gainR);
}

AUDIO_CORE_RAM void playerEngine::modVolume(float value)
{
    float gain = mtProject.values.dryMixMute
                   ? AMP_MUTED
                   : value * mixAmpLogValues[mtProject.values.trackVolume[nChannel]] *
                       mixAmpLogValues[mtProject.values.dryMixVolume];

    for (int channel = 0; channel < (int)ampPtr.size(); channel++)
    {
        ampPtr[channel]->gain(gain);
    }
}

AUDIO_CORE_RAM void playerEngine ::modLP1(uint16_t value)
{
    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setLP1(value);
    }
}
AUDIO_CORE_RAM void playerEngine ::modLP2(uint16_t value)
{
    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setLP2(value);
    }
}

AUDIO_CORE_RAM void playerEngine ::modCutoff(float value)
{
    for (int channel = 0; channel < (int)filterPtr.size(); channel++)
    {
        filterPtr[channel]->setCutoff(value);
    }
}
AUDIO_CORE_RAM void playerEngine ::modResonance(float value)
{
    if (mtProject.instrument[currentInstrument_idx].filterEnable)
    {
        for (int channel = 0; channel < (int)filterPtr.size(); channel++)
        {
            filterPtr[channel]->resonance(value + RESONANCE_OFFSET);
        }
    }
}
AUDIO_CORE_RAM void playerEngine ::modOverdrive(uint8_t value)
{
    for (int channel = 0; channel < (int)filterPtr.size(); channel++)
    {
        filterPtr[channel]->setOverdriveAmount(value);
    }
}

AUDIO_CORE_RAM void playerEngine ::modBitDepth(uint8_t value)
{
    for (int channel = 0; channel < (int)bitDepthPtr.size(); channel++)
    {
        bitDepthPtr[channel]->setBitDepth(value);
    }
}

AUDIO_CORE_RAM void playerEngine ::modWavetableWindow(uint16_t value)
{
    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setWavetableWindow(value);
    }
}

AUDIO_CORE_RAM void playerEngine ::modGranularPosition(uint16_t value)
{
    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setGranularPosition(value);
    }
}

AUDIO_CORE_RAM void playerEngine ::modGranularGrainLength()
{
    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setGranularGrainLength();
    }
}

AUDIO_CORE_RAM void playerEngine ::modGranularGrainLength(uint16_t value)
{
    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setGranularGrainLength(value);
    }
}

AUDIO_CORE_RAM void playerEngine ::modTune(int8_t value)
{
    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setTune(value, currentNote);
    }
}

AUDIO_CORE_RAM void playerEngine ::modDelaySend(uint8_t value)
{
    static constexpr int kInputsChannels = 2;
    if (mtProject.values.sendsMode == sendsModePreFader)
    {

        float gain = mtProject.values.delayMute
                       ? AMP_MUTED
                       : sendsAmpLogValues[value] * mixAmpLogValues[mtProject.values.delayVolume];
        if (voiceIsStereo)
        {
            preDelayMixer->gain(nChannel, gain / 2.0f);
            preDelayMixer->gain(nChannel + audioEngine::kVoices + kInputsChannels, gain / 2.0f);
        }
        else
        {
            preDelayMixer->gain(nChannel, gain);
            preDelayMixer->gain(nChannel + audioEngine::kVoices + kInputsChannels, 0.0f);
        }
    }
    else if (mtProject.values.sendsMode == sendsModePostFader)
    {
        bool isMute = (mtProject.values.delayMute || mtProject.values.trackMute[nChannel]);
        float gainFromDelay =
          sendsAmpLogValues[value] * mixAmpLogValues[mtProject.values.delayVolume];
        float gainFromVolume = getMostSignificantAmount() *
                               ampLogValues[getMostSignificantVolume()] *
                               mixAmpLogValues[mtProject.values.trackVolume[nChannel]];

        float gain = isMute ? AMP_MUTED : gainFromDelay * gainFromVolume;
        if (voiceIsStereo)
        {
            preDelayMixer->gain(nChannel, gain / 2.0f);
            preDelayMixer->gain(nChannel + audioEngine::kVoices + kInputsChannels, gain / 2.0f);
        }
        else
        {
            preDelayMixer->gain(nChannel, gain);
            preDelayMixer->gain(nChannel + audioEngine::kVoices + kInputsChannels, 0.0f);
        }
    }
}
AUDIO_CORE_RAM void playerEngine ::modReverbSend(uint8_t value)
{
    static constexpr int kInputsChannels = 2;
    if (mtProject.values.sendsMode == sendsModePreFader)
    {
        float gain = mtProject.values.reverbMute
                       ? AMP_MUTED
                       : sendsAmpLogValues[value] * mixAmpLogValues[mtProject.values.reverbVolume];
        if (voiceIsStereo)
        {
            prePolyverbMixer->gain(nChannel, gain / 2.0f);
            prePolyverbMixer->gain(nChannel + audioEngine::kVoices + kInputsChannels, gain / 2.0f);
        }
        else
        {
            prePolyverbMixer->gain(nChannel, gain);
            prePolyverbMixer->gain(nChannel + audioEngine::kVoices + kInputsChannels, 0.0f);
        }
    }
    else if (mtProject.values.sendsMode == sendsModePostFader)
    {
        bool isMute = (mtProject.values.reverbMute || mtProject.values.trackMute[nChannel]);
        float gainFromReverb =
          sendsAmpLogValues[value] * mixAmpLogValues[mtProject.values.reverbVolume];
        float gainFromVolume = getMostSignificantAmount() *
                               ampLogValues[getMostSignificantVolume()] *
                               mixAmpLogValues[mtProject.values.trackVolume[nChannel]];

        float gain = isMute ? AMP_MUTED : gainFromReverb * gainFromVolume;
        if (voiceIsStereo)
        {
            prePolyverbMixer->gain(nChannel, gain / 2.0f);
            prePolyverbMixer->gain(nChannel + audioEngine::kVoices + kInputsChannels, gain / 2.0f);
        }
        else
        {
            prePolyverbMixer->gain(nChannel, gain);
            prePolyverbMixer->gain(nChannel + audioEngine::kVoices + kInputsChannels, 0.0f);
        }
    }
}

void playerEngine::modSeqPoints(uint32_t sp, uint32_t ep, uint8_t fx_n)
{
    if (ep != SKIP_MODIFICATION_THIS_VALUE)
        currentSeqModValues.endPoint = ep;
    if (sp != SKIP_MODIFICATION_THIS_VALUE)
        currentSeqModValues.startPoint = sp;

    int32_t startPoint, loopPoint1, loopPoint2, endPoint;

    startPoint = sp != SKIP_MODIFICATION_THIS_VALUE
                   ? currentSeqModValues.startPoint
                   : mtProject.instrument[currentInstrument_idx].startPoint;
    loopPoint1 = mtProject.instrument[currentInstrument_idx].loopPoint1;
    loopPoint2 = mtProject.instrument[currentInstrument_idx].loopPoint2;
    endPoint = ep != SKIP_MODIFICATION_THIS_VALUE
                 ? currentSeqModValues.endPoint
                 : mtProject.instrument[currentInstrument_idx].endPoint;

    //	if(ep != NOT_MOD_POINTS )
    // trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::endPoint] = 1;
    //	if(sp != NOT_MOD_POINTS )
    // trackControlParameter[(int)controlType::sequencerMode][(int)parameterList::startPoint] = 1;

    if (mtProject.instrument[currentInstrument_idx].playMode != playModeSingleShot)
    {
        if (startPoint < SAMPLE_POINT_POS_MIN)
            startPoint = SAMPLE_POINT_POS_MIN;
        else if (startPoint > SAMPLE_POINT_POS_MAX)
            startPoint = SAMPLE_POINT_POS_MAX;

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
                trackControlParameter[(int)controlType::sequencerMode + fx_n]
                                     [(int)parameterList::loopPoint1] = 1;
                trackControlParameter[(int)controlType::sequencerMode + fx_n]
                                     [(int)parameterList::loopPoint2] = 1;
            }
            else // endPoint > od loopPoint2 , startPoint < od loopPoint1  - nic nie trzeba ruszac
            {
                trackControlParameter[(int)controlType::sequencerMode + fx_n]
                                     [(int)parameterList::loopPoint1] = 0;
                trackControlParameter[(int)controlType::sequencerMode + fx_n]
                                     [(int)parameterList::loopPoint2] = 0;
            }
        }
        else
        {
            if (startPoint > loopPoint1)
            {
                startPoint = loopPoint1 > 0 ? loopPoint1 - 1 : 0;
                endPoint =
                  loopPoint2 < SAMPLE_POINT_POS_MAX ? loopPoint2 + 1 : SAMPLE_POINT_POS_MAX;
                trackControlParameter[(int)controlType::sequencerMode + fx_n]
                                     [(int)parameterList::loopPoint1] = 0;
                trackControlParameter[(int)controlType::sequencerMode + fx_n]
                                     [(int)parameterList::loopPoint2] = 0;
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
                trackControlParameter[(int)controlType::sequencerMode + fx_n]
                                     [(int)parameterList::loopPoint1] = 1;
                trackControlParameter[(int)controlType::sequencerMode + fx_n]
                                     [(int)parameterList::loopPoint2] = 1;
            }
        }
    }
    else
    {
        if (startPoint < SAMPLE_POINT_POS_MIN)
            startPoint = SAMPLE_POINT_POS_MIN;
        else if (startPoint > SAMPLE_POINT_POS_MAX)
            startPoint = SAMPLE_POINT_POS_MAX;

        if (endPoint < SAMPLE_POINT_POS_MIN)
            endPoint = SAMPLE_POINT_POS_MIN;
        else if (endPoint > SAMPLE_POINT_POS_MAX)
            endPoint = SAMPLE_POINT_POS_MAX;
    }

    currentSeqModValues.loopPoint1 = loopPoint1;
    currentSeqModValues.loopPoint2 = loopPoint2;

    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setForcedPoints(startPoint, loopPoint1, loopPoint2, endPoint);
        playMemPtr[channel]->setPointsForceFlag();
    }
}

AUDIO_CORE_RAM void playerEngine::setRefreshBit(uint32_t mask)
{
    refreshRegister |= mask;
}

AUDIO_CORE_RAM void playerEngine ::changeFilterType(uint8_t type)
{
    for (int channel = 0; channel < (int)filterPtr.size(); channel++)
    {
        filterPtr[channel]->setType(type);
    }
}

AUDIO_CORE_RAM void playerEngine ::filterDisconnect()
{
    for (int channel = 0; channel < (int)filterPtr.size(); channel++)
    {
        filterPtr[channel]->disconnect();
    }
}

AUDIO_CORE_RAM void playerEngine ::filterConnect()
{
    for (int channel = 0; channel < (int)filterPtr.size(); channel++)
    {
        filterPtr[channel]->connect();
    }
}

void playerEngine ::clean(void)
{

    currentInstrument_idx = 0;
    currentNote = 0;
    refreshRegister = 0;
    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->clean();
    }
}

void playerEngine::setInterpolationEnable(bool value)
{
    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->setInterpolationEnable(value);
    }
}

float playerEngine ::fmap(float x, float in_min, float in_max, float out_min, float out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

uint8_t playerEngine ::noteOnWithoutInstrument(std::vector<int16_t*> addr,
                                               uint32_t len,
                                               uint8_t type,
                                               uint8_t note,
                                               int8_t velocity)
{

    initEnvelopesParameters(envAmp, (envelopeGenerator::strEnv*)&passEnvelope);

    if (refreshRegister & VOLUME_MASK)
    {
        refreshRegister &= (~VOLUME_MASK);
    }

    for (uint8_t i = envPan; i < ACTIVE_ENVELOPES; i++)
    {
        envelopePtr[i]->kill();
    }

    filterDisconnect();

    if (velocity >= 0 && velocity < AMP_LOG_VALUES)
    {
        modVolume(ampLogValues[velocity]); // wracam z 50 do 100, a teraz ze 100 do 50
    }
    else
    {
        modVolume(ampLogValues[50]);
    }

    modPanning(50);
    modDelaySend(AMP_MUTED);
    modReverbSend(AMP_MUTED);
    modBitDepth(BIT_DEPTH_MAX);
    modOverdrive(OVERDRIVE_DISABLED);

    engine.setLimiterAttack(300);
    engine.setLimiterRelease(10);
    engine.setLimiterThreshold(LIMITER_THRESHOLD_MAX);
    engine.setLimiterSidechain(LIMITER_SIDECHAIN_DEFAULT);

    assert(playMemPtr.size() <= addr.size());

    const bool isStereo = (addr.size() > 1 && addr[1] != nullptr && addr[1] != addr[0]);
    voiceIsStereo = isStereo;
    audioVoice.setStereo(isStereo);

    playMemPtr[0]->envelopeSetIsRandom(false);
    if (note == 255)
    {
        playMemPtr[0]->envelopeNoteOnForPrev(addr[0], len, type);
    }
    else
    {
        playMemPtr[0]->envelopeNoteOnForPrev(addr[0], len, note, type);
    }

    if (isStereo)
    {
        playMemPtr[1]->envelopeSetIsRandom(false);
        if (note == 255)
        {
            playMemPtr[1]->envelopeNoteOnForPrev(addr[1], len, type);
        }
        else
        {
            playMemPtr[1]->envelopeNoteOnForPrev(addr[1], len, note, type);
        }
    }
    else
    {
        playMemPtr[1]->stop();
    }

    return 1;
}
uint8_t playerEngine ::getInterfaceEndReleaseFlag()
{
    return interfaceEndReleaseFlag;
}
void playerEngine ::clearInterfaceEndReleaseFlag()
{
    interfaceEndReleaseFlag = 0;
}

uint8_t playerEngine ::getInterfacePlayingEndFlag()
{
    return interfacePlayingEndFlag;
}
void playerEngine ::clearInterfacePlayingEndFlag()
{
    interfacePlayingEndFlag = 0;
}

void playerEngine::setTrackIsDisplayed()
{
    isTrackDisplayed = true;
}
void playerEngine::clearTrackIsDisplayed()
{
    isTrackDisplayed = false;
}
bool playerEngine::getEndDisplayTrackFlag()
{
    return onEndDisplay;
}
void playerEngine::clearEndDisplayTrackFlag()
{
    onEndDisplay = false;
}

uint16_t playerEngine ::getWavePosition()
{
    return playMemPtr[0]
      ->getPosition(); // should be the same for all channels, so returned the first one
}

int16_t* playerEngine ::getWaveStartAddressIfPlaying(int channel)
{
    return playMemPtr[channel]->getStartAddresIfPlaying();
}

uint32_t playerEngine ::getLengthIfPlaying()
{
    return playMemPtr[0]
      ->getLengthIfPlaying(); // should be the same for all channels, so returned the first one
}
//**************************************************************************************************************************************
// state: 1 - solo ON, 0 - solo OFF
// Na potrzeby eksportu - nie wykorzystywana w mixerze
void audioEngine::soloTrack(uint8_t channel, uint8_t state)
{
    if (channel >= 8)
        return;

    if (state == 1)
    {
        for (uint8_t i = 0; i < 8; i++)
        {
            if (i == channel)
            {
                if (!mtProject.values.trackMute[i])
                {
                    muteDelaySend(i, 1);
                    muteReverbSend(i, 1);
                }
                continue;
            }
            if (!mtProject.values.trackMute[i])
                muteTrack(i, 1);
        }
    }
    else if (state == 0)
    {
        for (uint8_t i = 0; i < 8; i++)
        {
            if (i == channel)
            {
                muteDelaySend(i, 0);
                muteReverbSend(i, 0);
                continue;
            }
            if (!mtProject.values.trackMute[i])
                muteTrack(i, 0);
        }
    }
}

// selectLR :  0 - L, 1- R, state: 1 - solo ON, 0 - solo OFF
void audioEngine::soloDelaySend(uint8_t state)
{
    if (state == 1)
    {
        forceDelaySend = 1;
        for (uint8_t i = 0; i < 8; i++)
        {
            if (!mtProject.values.trackMute[i])
                muteTrack(i, 1);
        }
    }
    else if (state == 0)
    {
        forceDelaySend = 0;
        for (uint8_t i = 0; i < 8; i++)
        {
            if (!mtProject.values.trackMute[i])
                muteTrack(i, 0);
        }
    }
}

void audioEngine::soloReverbSend(uint8_t state)
{
    if (state == 1)
    {
        forceReverbSend = 1;
        for (uint8_t i = 0; i < 8; i++)
        {
            if (!mtProject.values.trackMute[i])
                muteTrack(i, 1);
        }
    }
    else if (state == 0)
    {
        forceReverbSend = 0;
        for (uint8_t i = 0; i < 8; i++)
        {
            if (!mtProject.values.trackMute[i])
                muteTrack(i, 0);
        }
    }
}

void audioEngine::clearDelay()
{
    delay->clear();
    for (uint8_t channel = 0; channel < 8; channel++)
    {
        instrumentPlayer[channel].modDelaySend(AMP_MUTED);
    }
#ifdef FEATURE_SYNTH
    for (int synthIdx = 0; synthIdx < kSynthParts; synthIdx++)
    {
        setSynthDelaySend(synthIdx, AMP_MUTED);
    }
#endif
}
void audioEngine::clearReverb()
{
    polyverb->reset();
    for (uint8_t channel = 0; channel < 8; channel++)
    {
        instrumentPlayer[channel].modReverbSend(AMP_MUTED);
    }
#ifdef FEATURE_SYNTH
    for (int synthIdx = 0; synthIdx < kSynthParts; synthIdx++)
    {
        setSynthReverbSend(synthIdx, AMP_MUTED);
    }
#endif
}

void audioEngine::resetDelay()
{
    if (delay != nullptr)
    {
        delay->clear();
    }
}

void audioEngine::resetLimiter()
{
    limiter[AudioOutput::OutputChannels::kLeft]->begin();
    limiter[AudioOutput::OutputChannels::kRight]->begin();

    const bool scEnabled = mtProject.values.limiterSidechain != 0;
    const uint8_t scMux = mtProject.values.limiterSidechain ? mtProject.values.limiterSidechain - 1 : 0;
    output.getLimiterSidechainMux(AudioOutput::OutputChannels::kLeft)->setOut(scMux);
    output.getLimiterSidechainMux(AudioOutput::OutputChannels::kLeft)->setEnabled(scEnabled);
    output.getLimiterSidechainMux(AudioOutput::OutputChannels::kRight)->setOut(scMux);
    output.getLimiterSidechainMux(AudioOutput::OutputChannels::kRight)->setEnabled(scEnabled);
}

void audioEngine::muteDelaySend(uint8_t channel, uint8_t state)
{
    if (channel >= 8)
    {
        return;
    }

    if (state == 0)
    {
        instrumentPlayer[channel].onlyDelayMuteState = 0;
    }
    else
    {
        instrumentPlayer[channel].onlyDelayMuteState = 1;
    }

    instrumentPlayer[channel].setRefreshBit(DELAY_SEND_MASK);
}

void audioEngine::muteReverbSend(uint8_t channel, uint8_t state)
{
    if (channel >= 8)
    {
        return;
    }

    if (state == 0)
    {
        instrumentPlayer[channel].onlyReverbMuteState = 0;
    }
    else
    {
        instrumentPlayer[channel].onlyReverbMuteState = 1;
    }

    instrumentPlayer[channel].setRefreshBit(REVERB_SEND_MASK);
}

//**************************************************************************************************************************************
// PERFORMANCE MODE

// state = 1 mutuje
void audioEngine::muteTrack(uint8_t channel, uint8_t state)
{
    if (channel >= 8)
    {
        return;
    }

    if (state == MUTE_DISABLE)
    {
        instrumentPlayer[channel].muteState = 0;
    }
    else
    {
        instrumentPlayer[channel].muteState = 1;
    }

    instrumentPlayer[channel].setRefreshBit(VOLUME_MASK);
    instrumentPlayer[channel].setRefreshBit(DELAY_SEND_MASK);
    instrumentPlayer[channel].setRefreshBit(REVERB_SEND_MASK);
}

//************************************************************************************************************
bool playerEngine::isZeroInPointZone(uint16_t position,
                                     uint32_t sampleLength,
                                     int16_t* sampleAddress)
{
    return AudioPlayMemory::isZeroInPointZone(position, sampleLength, sampleAddress);
}

void playerEngine::init(
  Timer* timer,
  AudioMixer<MixerAndFxs<audioEngine::kVoices>::kSendsInputsCount>* preDelayMixer,
  AudioMixer<MixerAndFxs<audioEngine::kVoices>::kSendsInputsCount>* prePolyverbMixer)
{
    playerEngine::timer = timer;
    playerEngine::preDelayMixer = preDelayMixer;
    playerEngine::prePolyverbMixer = prePolyverbMixer;

    for (int envIdx = 1; envIdx < envMax; envIdx++)
    {
        envelopePtr[envIdx]->begin(timer);
        envelopePtr[envIdx]->init(&mtProject.instrument[0].envelope[envIdx]);
    }

    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->init(timer, channel, nChannel);
    }
}

uint32_t playerEngine::getEnvelopeWtPosMod()
{
    if ((envelopePtr[envWtPos]->isKeyPressed() == 1) || (envelopePtr[envWtPos]->getPhase() != 0))
    {
        uint32_t localWTPos = mtProject.instrument[currentInstrument_idx].wavetableCurrentWindow;
        int32_t localWTMod =
          currentEnvelopeModification[envWtPos] *
          mtProject.instrument[currentInstrument_idx].sample.wavetableWindowNumber;

        if ((int)(localWTPos + localWTMod) >
            (int)(mtProject.instrument[currentInstrument_idx].sample.wavetableWindowNumber - 1))
        {
            localWTPos =
              ((int)(mtProject.instrument[currentInstrument_idx].sample.wavetableWindowNumber -
                     1) >= 0)
                ? mtProject.instrument[currentInstrument_idx].sample.wavetableWindowNumber - 1
                : 0;
        }
        else if ((int)(localWTPos + localWTMod) < 0)
        {
            localWTPos = 0;
        }
        else
        {
            localWTPos += localWTMod;
        }

        return localWTPos;
    }
    else
    {
        return mtProject.instrument[currentInstrument_idx].wavetableCurrentWindow;
    }
}

uint32_t playerEngine::getEnvelopeGranPosMod()
{

    if ((envelopePtr[envGranPos]->isKeyPressed() == 1) ||
        (envelopePtr[envGranPos]->getPhase() != 0))
    {
        uint32_t localGranPos =
          mtProject.instrument[currentInstrument_idx].granular.currentPosition;
        int32_t localGranMod = currentEnvelopeModification[envGranPos] * MAX_16BIT;

        if ((int)(localGranPos + localGranMod) > MAX_16BIT)
        {
            localGranPos = MAX_16BIT;
        }
        else if ((int)(localGranPos + localGranMod) < 0)
        {
            localGranPos = 0;
        }
        else
        {
            localGranPos += localGranMod;
        }

        return localGranPos;
    }
    else
    {
        return mtProject.instrument[currentInstrument_idx].granular.currentPosition;
    }
}

uint32_t playerEngine::getEnvelopeGranLenMod()
{
    uint32_t sampleLen = mtProject.instrument[currentInstrument_idx].sample.length;
    int32_t maxGrainLen = (sampleLen > 0 && sampleLen < MAX_GRANULAR_LENGTH)
                            ? static_cast<int32_t>(sampleLen)
                            : static_cast<int32_t>(MAX_GRANULAR_LENGTH);
    if (maxGrainLen < MIN_GRANULAR_LENGTH)
        maxGrainLen = MIN_GRANULAR_LENGTH;

    int32_t localGranLen =
      (trackControlParameter[(int)controlType::sequencerMode]
                            [(int)parameterList::granularLength] ||
       trackControlParameter[(int)controlType::sequencerMode2]
                            [(int)parameterList::granularLength])
        ? static_cast<int32_t>(currentSeqModValues.granularLength)
        : static_cast<int32_t>(mtProject.instrument[currentInstrument_idx].granular.grainLength);

    if (mtProject.instrument[currentInstrument_idx].isGranLenEnvEnabled() &&
        ((envelopePtr[envGranLen]->isKeyPressed() == 1) ||
         (envelopePtr[envGranLen]->getPhase() != 0)))
    {
        int32_t localGranMod =
          static_cast<int32_t>(currentEnvelopeModification[envGranLen] *
                               (maxGrainLen - MIN_GRANULAR_LENGTH));
        localGranLen += localGranMod;
    }

    return static_cast<uint32_t>(clamp<int32_t>(localGranLen, MIN_GRANULAR_LENGTH, maxGrainLen));
}

void playerEngine::calcLfoBasedEnvelope(envelopeGenerator::strEnv* env,
                                        strInstrument::strEnvBasedLfo* lfo,
                                        uint8_t rate,
                                        uint8_t isAmp,
                                        uint8_t loopMode)
{
    env->loop = loopMode;
    env->enable = 1;
    env->amount = lfo->amount;
    env->sustain = lfo->amount;

    uint8_t envelopeConstrain =
      isAmp ? CONSTRAIN_SPEED_AMP_ENVELOPE : CONSTRAIN_SPEED_OTHER_ENVELOPE;
    if (rate > envelopeConstrain)
        rate = envelopeConstrain;

    float lfoFrequency = (sequencer.getCurrentTempo() / 15.0);

    float periodTime =
      (1000 / lfoFrequency) * (isAmp ? tempoSyncRatesAmp[rate] : tempoSyncRatesOthers[rate]);

    if (loopMode == 3)
    {
        env->attack = periodTime;
        env->decay = periodTime;
        env->delay = 0;
        env->hold = 0;
        env->release = periodTime;
        return;
    }

    switch (lfo->shape)
    {
        case lfoShapeReverseSaw:

            env->attack = 0;
            env->decay = periodTime;
            env->delay = 0;
            env->sustain = 0.0f;
            env->hold = 0;
            env->release = 0;

            break;

        case lfoShapeSaw:

            env->attack = periodTime;
            env->decay = 0;
            env->delay = 0;
            env->sustain = 0.0f;
            env->hold = 0;
            env->release = 0;

            break;

        case lfoShapeTriangle:

            env->attack = periodTime / 2;
            env->decay = periodTime / 2;
            ;
            env->delay = 0;
            env->sustain = 0.0f;
            env->hold = 0;
            env->release = 0;

            break;
        case lfoShapeSquare:
            env->attack = 0;
            env->decay = 0;
            env->delay = 0;
            env->sustain = 0.0f;
            env->hold = periodTime / 2;
            env->release = periodTime / 2;
            break;
        case lfoShapeRandom:
            env->attack = 0;
            env->decay = 0;
            env->delay = 0;
            env->sustain = 0.0f;
            env->hold = periodTime;
            env->release = 0;
            break;
    }
}

AUDIO_CORE_FLASH float playerEngine::getRMSValue()
{
    float localVal = -1.0f;
    bool isAvailable = true;
    for (int channel = 0; channel < (int)rmsPtr.size(); channel++)
    {
        isAvailable &= rmsPtr[channel]->available();
    }

    if (isAvailable)
    {
        localVal = 0.0f;
        for (int channel = 0; channel < (int)rmsPtr.size(); channel++)
        {
            localVal += rmsPtr[channel]->read();
        }

        localVal /= (float)rmsPtr.size();
    }

    return localVal;
}

AudioVoice* playerEngine::getAudioVoice()
{
    return &audioVoice;
}

void playerEngine::setPassEnvelope(uint8_t state)
{
    for (int channel = 0; channel < (int)playMemPtr.size(); channel++)
    {
        playMemPtr[channel]->envelopeSetPassFlag(state);
    }
    envelopePassFlag = state;
}
