
#ifndef SOURCE_MTAUDIOENGINE_H_
#define SOURCE_MTAUDIOENGINE_H_

#include "Audio.h"
#include "AudioInput.h"
#include "AudioOutput.h"
#include "AudioVoice.h"
#include "ExportQueues.h"
#include "MixerAndFxs.h"
#include "Platform.h"
#include "SdStream.h"
#include "SynthVoice.h"
#include "padToBeatNoteConverter.h"
#include <bitset>
#include <map>
#include <optional>

namespace mtAudioEngineConstans
{
constexpr uint32_t SKIP_MODIFICATION_THIS_VALUE = 1000000;
constexpr uint8_t MUTE_DISABLE = 0;
constexpr uint8_t MUTE_ENABLE = 1;
constexpr float AMP_MUTED = 0.0f;
constexpr uint8_t MOST_SIGNIFICANT_FX = 0;
constexpr uint8_t LEAST_SIGNIFICANT_FX = 1;
constexpr uint8_t ACTIVE_ENVELOPES = envMax;
constexpr uint16_t RELEASE_NOTE_ON_VAL = 1;
constexpr uint8_t OVERDRIVE_DISABLED = 0;
}

class Platform;
class Sgtl5000;
class Radio;
class Timer;

class audioEngine
{
  public:
    static constexpr int kVoices = 8;

    audioEngine();

    void init(Platform* platform, Synth* synth);
    void update();
    void updateSdStream();
    void updateAudioBuffers(void* left, void* right, int length);
    void updateInputAudioBuffers(void* i2sData, void* usbData, int length);
    void connectVoiceToMixer(AudioVoice* voice, MixerAndFxs<kVoices>* mixerAndFxs, int idx);
    void connectInputToMixer(AudioInput* input, MixerAndFxs<kVoices>* mixerAndFxs);
    void connectVoiceToOutput(AudioVoice* voice, AudioOutput* output, int idx);
    void connectInputToOutput(AudioInput* input, AudioOutput* output);
    void connectMixerToOutput(MixerAndFxs<kVoices>* mixerAndFxs, AudioOutput* output);
#ifdef FEATURE_SYNTH
    void connectVoiceToSynth(AudioVoice* voice, SynthVoice* synth, int idx);
    void connectSynthToMixer(MixerAndFxs<kVoices>* mixerAndFxs, SynthVoice* synth);
    void connectSynthToExportQueue(SynthVoice* synth, ExportQueues* exportQueues);
    void connectSynthToOutput(SynthVoice* synth, AudioOutput* output);
#endif
    void connectSpecialInputsToOutput(AudioOutput* output);
    void connectVoiceToExportQueue(AudioVoice* voice, ExportQueues* exportQueues, int idx);
    void connectMixerToExportQueue(MixerAndFxs<kVoices>* mixerAndFxs, ExportQueues* exportQueues);
    void connectOutputToExportQueue(AudioOutput* output, ExportQueues* exportQueues);
#ifdef PLATFORM_IMX
    void refreshOutputSourceOutsideTheRecorder();
    void forceUsbGainOutsideTheRecorder();
#endif
    void refreshTrackVolume();
    void refreshReverbVolume();
    void refreshDelayVolume();
    void refreshAllSends();
    void forceLineInStreamVolume(uint8_t value);
    void refreshLineInStreamVolume();
    void refreshLineInStreamDelaySend();
    void refreshLineInStreamReverbSend();
    void disableLineInImpact();
    void enableLineInImpact();
    void setHeadphonesVolume(uint8_t value);
    void setPreviewVolume(uint8_t value);
    void muteHeadphones();
    void unmuteHeadphones();
    void setDelayFeedback(uint8_t value);
    void setDelayTime(uint16_t value);
    int getDelayTime();
    void setDelayFilterFrequency(uint16_t value);
    void setReverbSize(float value);
    void setReverbDamp(float value);
    void setReverbPredelay(float value);
    void setReverbDiffusion(float value);
    void setDelayParams(uint8_t value);
    void setLimiterAttack(uint16_t attack);
    void setLimiterRelease(float release);
    void setLimiterThreshold(uint16_t threshold);
    void setLimiterSidechain(uint8_t sidechain);
#ifdef FEATURE_SYNTH
    enum SynthValuesSource
    {
        kInstrument,
        kFx1,
        kFx2,
        kPerformance,
        kSynthValuesSourceCount
    };

    enum SynthParam
    {
        kPanning,
        kDelaySend,
        kReverbSend,
        kVolume,
        kMacro1,
        kMacro2,
        kMacro3,
        kMacro4,
        kMacro5,
        kMacro6,
        kSynthParamsCount
    };

    struct SynthParamBoundaries
    {
        float min;
        float max;
    };
    static std::map<uint8_t, SynthValuesSource> fxNumberToSynthValuesSource;
    static std::map<uint8_t, SynthParam> fxIdToSynthParam;
    static const SynthParamBoundaries synthParamsBoundaries[SynthParam::kSynthParamsCount];

    void setSynthParam(SynthParam param, int synthIdx, float value, SynthValuesSource source);
    void cleanSynthParam(SynthParam param, int synthIdx, SynthValuesSource source);
    void refreshSynthParam(SynthParam param);

    bool isSynthFx(uint8_t fx_id);
    void seqSynthFx(uint8_t fx_id, uint8_t fx_val, uint8_t fx_n, uint8_t synthIdx);
    void refreshSynthInstrumentParams(SynthPartSettings& synthSettings, int synthIdx);
    float getSynthVoiceRMS(uint8_t channel, int synthIdx);
#endif
    void muteTrack(uint8_t channel, uint8_t state);
    void soloTrack(uint8_t channel, uint8_t state);
    void muteDelaySend(uint8_t channel, uint8_t state);
    void muteReverbSend(uint8_t channel, uint8_t state);
    void soloDelaySend(uint8_t state);
    void soloReverbSend(uint8_t state);
    void clearDelay();
    void clearReverb();
    void resetLimiter();
    void performanceModeEndAll();
    void startTestSignal(float amp, float freq);
    void stopTestSignal();
    void endAllFx(bool synthOnly = false);
    void cutAllVolumeEnvelopes();
    void setPassEnvelope(uint8_t state);
    void blockDelayRefresh();
    void unblockDelayRefresh();
    void setLastUsedVoice(uint8_t v);
    uint8_t getLastUsedVoice();
    void makeMetronomeTick(uint8_t);
    void setBlockedInstrument(int8_t idx);
    void clearBlockedInstrument(int8_t idx);
    void setBlockedNoteOnFromInstrument(int8_t idx);
    void clearBlockedNoteOnFromInstrument(int8_t idx);
    void blockAllNoteOn();
    void unblockAllNoteOn();
    void setInterpolationEnable(bool value);
    float getDelayAverageRMS();
    float getReverbAverageRMS();
    float getDryMixAverageRMS();
    float getDelayRMS(uint8_t channel);
    float getReverbRMS(uint8_t channel);
    float getLineInRMS(uint8_t channel);
    float getDryMixRMS(uint8_t channel);
    void disconnectMixerImpact();
    void connectMixerImpact();
    void setInputConfigOutsideRecorderInterfaceModule();
    void setLineInOutType(uint8_t type);
    void setHeadroomEnable(bool enabled);
    void setSnapToZeroEnable(bool enabled);
    void setSaturationType(SaturationType type);
#ifdef PLATFORM_IMX
    enum OutputSourceType
    {
        kRegularMix,
        kUsbInput
    };

    void setOutputSource(OutputSourceType source);
#endif
    void detachUpdateTimer();
    void attachUpdateTimer();

    AudioInput* getAudioInput();
    AudioOutput* getAudioOutput();
    ExportQueues* getExportQueues();
    MixerAndFxs<kVoices>* getMixerAndFx();
    AudioPlaySdWav* getSdPlayer();

    friend class playerEngine;

  private:
#ifdef FEATURE_SYNTH
    void updateRoutedTracks();
    void updateTrackDirectGain(uint8_t track);
    void updateSynthParams();
    void updateSynthParam(int synthIdx, SynthParam param);
    void applySynthValue(int synthIdx, SynthParam param, float value);
    float calculateParamValue(int synthIdx, SynthParam param);
    void setSynthDelaySend(int synthIdx, float value);
    void setSynthReverbSend(int synthIdx, float value);
    void setSynthPanning(int synthIdx, float value);
    void setSynthGain(int synthIdx, float value);
    void setSynthMacro(int synthIdx, int macroIdx, float value);
    void endSynthFx(uint8_t fx_id, uint8_t fx_n, uint8_t synthIdx);
#endif
    Sgtl5000* codec;
    Radio* radio;
    Usb* usb;

    uint8_t forceDelaySend;
    uint8_t forceReverbSend;
    float lastTempo[4];
    float currentTempo;
    float lastDelayRefreshTempo;
    uint8_t lastUsedVoice;
    void storeMixerValues();
    void resetMixerValues();
    void restoreMixerValues();
    struct strMixerValuesStore
    {
        uint8_t trackVolume[8];
        uint8_t delayVolume;
        uint8_t reverbVolume;
        uint8_t dryMixVolume;
    } mixerValuesStore;

    SdStream sdStream;
    std::vector<AudioConnection*> connections;
    AudioInput input;
    AudioPlaySdWav playerWavFromSD;
    AudioSynthWaveform testWaveform;
    AudioPlayMemory metronomeTick;
    MixerAndFxs<kVoices> mixerAndFxs;

#ifdef FEATURE_SYNTH
    SynthVoice synthVoice;
    uint8_t routedTracks = 0;
    std::optional<float> synthParams[kSynthParts][SynthParam::kSynthParamsCount]
                                    [SynthValuesSource::kSynthValuesSourceCount] = {};
    std::bitset<SynthParam::kSynthParamsCount>
      isSynthParamDirty[kSynthParts]; // this solution is not optimized (bits alignment) - please
                                      // use something like bitset2d from Play if it grows
    std::optional<uint8_t> lastSynthFxId[kSynthParts][2];
#endif
    ExportQueues exportQueues;
    AudioOutput output;
    AudioAnalyzeRMS* delayRms[(int)MixerAndFxChannelType::kChannelCount];
    AudioAnalyzeRMS* polyverbRms[(int)MixerAndFxChannelType::kChannelCount];
    AudioAnalyzeRMS* dryMixRms[(int)MixerAndFxChannelType::kChannelCount];
    AudioMixer<AudioOutput::FinalMixInputs::kFinalMixerSize>*
      finalMixer[AudioOutput::OutputChannels::kChannelsCount];
    AudioMixer<MixerAndFxs<kVoices>::kTrackAndSendsMixerInputs>*
      tracksAndFxsMixer[(int)MixerAndFxChannelType::kChannelCount];
    AudioEffectLimiter* limiter[AudioOutput::OutputChannels::kChannelsCount];
    AudioEffectPolyverb* polyverb;
    AudioEffectShortDelay* delay;
    AudioMixer<MixerAndFxs<kVoices>::kSendsInputsCount>* preDelayMixer;
    AudioMixer<MixerAndFxs<kVoices>::kSendsInputsCount>* prePolyverbMixer;
};

class playerEngine
{
  public:
    playerEngine();

    static bool isZeroInPointZone(uint16_t position, uint32_t sampleLength, int16_t* sampleAddress);
    void init(Timer* timer,
              AudioMixer<MixerAndFxs<audioEngine::kVoices>::kSendsInputsCount>* preDelayMixer,
              AudioMixer<MixerAndFxs<audioEngine::kVoices>::kSendsInputsCount>* prePolyverbMixer);

    uint8_t noteOn(uint8_t instr_idx, int8_t note, int8_t velocity);
    uint8_t noteOn(uint8_t instr_idx,
                   int8_t note,
                   int8_t velocity,
                   uint8_t fx1_id,
                   uint8_t fx1_val,
                   uint8_t fx2_id,
                   uint8_t fx2_val); // tylko ten note on do nut w sequencerze
    void noteOff(int8_t option = -4);
    void setStepVelocity(int8_t velocity);
    void clean();

    void setInterpolationEnable(bool value);

    void seqFx(uint8_t fx_id, uint8_t fx_val, uint8_t fx_n);
    void endFx(uint8_t fx_id, uint8_t fx_n);
    void slide(int8_t note, uint16_t time);
    void modPitch(float value);
    void modGlide(uint16_t value);
    void modFineTune(int8_t value);

    void modPanning(int16_t value);
    void modVolume(float value);
    void modSP(uint16_t value);
    void modLP1(uint16_t value);
    void modLP2(uint16_t value);
    void modCutoff(float value);
    void modResonance(float value);
    void modOverdrive(uint8_t value);
    void modBitDepth(uint8_t value);

    void modSeqPoints(uint32_t sp, uint32_t ep, uint8_t fx_n);

    void modWavetableWindow(uint16_t value);
    void modTune(int8_t value);
    void modDelaySend(uint8_t value);
    void modReverbSend(uint8_t value);

    void modGranularPosition(uint16_t value);
    void modGranularGrainLength();
    void modGranularGrainLength(uint16_t value);

    void setRefreshBit(uint32_t mask);
    //	void resetMods();
    uint8_t getInterfaceEndReleaseFlag();
    void clearInterfaceEndReleaseFlag();
    uint8_t getInterfacePlayingEndFlag();
    void clearInterfacePlayingEndFlag();

    void setTrackIsDisplayed();
    void clearTrackIsDisplayed();
    bool getEndDisplayTrackFlag();
    void clearEndDisplayTrackFlag();

    uint32_t getEnvelopeGranPosMod();
    uint32_t getEnvelopeGranLenMod();

    uint32_t getEnvelopeWtPosMod();

    uint16_t getWavePosition();
    int16_t* getWaveStartAddressIfPlaying(int channel);
    uint32_t getLengthIfPlaying();
    void update();

    //	This function changes limiter values to "pass through" mode.
    //	After use this function one should reload  limiter values saved in project.
    //  Instrument related parameters will be reloaded when note on appears.
    uint8_t noteOnWithoutInstrument(std::vector<int16_t*> addr,
                                    uint32_t len,
                                    uint8_t type,
                                    uint8_t note = 255,
                                    int8_t velocity = -1);

    uint8_t currentInstrument_idx;

    const uint32_t envTargetRefreshMask[envMax] = {
        0, PANNING_MASK, CUTOFF_MASK, WT_POS_SEND_MASK, GRANULAR_POS_SEND_MASK, FINETUNE_MASK
    };
    const uint8_t envelopesControlValue[envMax] = {
        (uint8_t)parameterList::lfoAmp,
        (uint8_t)parameterList::lfoPanning,
        (uint8_t)parameterList::lfoCutoff,
        (uint8_t)parameterList::lfoWavetablePosition,
        (uint8_t)parameterList::lfoGranularPosition,
        (uint8_t)parameterList::lfoFinetune,
    };
    enum controlType
    {
        sequencerMode,
        sequencerMode2,
        performanceMode,

        controlTypeLength
    };

    enum parameterList
    {
        startPoint,
        loopPoint1,
        loopPoint2,
        endPoint,
        filterCutoff,
        filterType,
        filterEnable,
        delaySend,
        reverbSend,
        overdrive,
        bitDepth,
        panning,
        ampAttack,
        ampRelease,
        filterAttack,
        filterRelease,
        fineTune,
        tune,
        lfoFineTune,
        lfoAmp,
        lfoCutoff,
        lfoWavetablePosition,
        lfoGranularPosition,
        lfoPanning,
        lfoFinetune,
        volume,
        samplePlaybeckDirection,
        sampleSlice,
        glide,
        wavetablePosition,
        granularPosition,
        granularLength,
        slice,

        parameterListLength
    };

    struct strCurrentSeqModValues
    {
        uint16_t startPoint;
        uint16_t loopPoint1;
        uint16_t loopPoint2;
        uint16_t endPoint;
        uint8_t sampleSlice;
        uint8_t reversePlayback;
        float filterCutoff;
        uint8_t filterType;
        uint8_t filterEnable;
        uint8_t delaySend;
        uint8_t reverbSend;
        int16_t panning;
        uint8_t overdrive;
        uint8_t bitDepth;

        uint16_t ampAttack;
        uint16_t ampRelease;
        uint16_t filterAttack;
        uint16_t filterRelease;

        int8_t fineTune;
        uint16_t glide;
        uint32_t wavetablePosition;
        uint16_t granularPosition;
        uint16_t granularLength;
        uint8_t volume;
        uint8_t slice;

        uint8_t lfoAmpEnable;
        uint8_t lfoAmpRate;

        uint8_t lfoCutoffEnable;
        uint8_t lfoCutoffRate;

        uint8_t lfoWavetablePositionEnable;
        uint8_t lfoWavetablePositionRate;

        uint8_t lfoGranularPositionEnable;
        uint8_t lfoGranularPositionRate;

        uint8_t lfoPanningEnable;
        uint8_t lfoPanningRate;

        uint8_t lfoFinetuneEnable;
        uint8_t lfoFinetuneRate;

        int8_t tune;

    } currentSeqModValues;

    struct strCurrentPerformanceValues
    {
        uint16_t startPoint;
        uint16_t loopPoint1;
        uint16_t loopPoint2;
        uint16_t endPoint;
        uint8_t reversePlayback;
        float filterCutoff;
        uint8_t filterType;
        uint8_t filterEnable;
        uint8_t delaySend;
        uint8_t reverbSend;
        int16_t panning;
        int8_t tune;
        uint8_t volume;
        uint32_t wavetablePosition;
        uint16_t granularPosition;

        uint8_t lfoAmpEnable;
        uint8_t lfoAmpRate;

        uint8_t lfoCutoffEnable;
        uint8_t lfoCutoffRate;

        uint8_t lfoWavetablePositionEnable;
        uint8_t lfoWavetablePositionRate;

        uint8_t lfoGranularPositionEnable;
        uint8_t lfoGranularPositionRate;

        uint8_t lfoPanningEnable;
        uint8_t lfoPanningRate;

        uint8_t lfoFinetuneEnable;
        uint8_t lfoFinetuneRate;

        uint8_t bitDepth;
        uint8_t overdrive;

    } currentPerformanceValues;

    struct strPerformanceMod
    {
        int8_t volume;
        int8_t panning;
        int8_t tune;
        int8_t delaySend;
        int8_t reverbSend;
        int8_t cutoff;
        int16_t wavetablePosition;
        int16_t granularPosition;
        uint8_t filterType;
        int32_t startPoint;
        int32_t endPoint;
        uint8_t reversePlayback;
        int8_t lfoAmpRate;
        int8_t lfoCutoffRate;
        int8_t lfoPositionRate;
        int8_t lfoPanningRate;
        int8_t lfoFinetuneRate;

        int8_t bitDepth;
        int8_t overdrive;
    } performanceMod;

    float currentEnvelopeModification[envMax];

    uint8_t trackControlParameter[(int)controlType::controlTypeLength]
                                 [(int)parameterList::parameterListLength];
    //**********************************************************************************************************************************
    // PERFORMANCE MODE
    //*****************************CHANGE
    void changeVolumePerformanceMode(int8_t value);
    void changePanningPerformanceMode(int8_t value);
    void changeBitDepthPerformanceMode(int8_t value);
    void changeOverdrivePerformanceMode(int8_t value);
    void changeTunePerformanceMode(int8_t value);
    void changeDelaySendPerformanceMode(int8_t value);
    void changeReverbSendPerformanceMode(int8_t value);
    void changeStartPointPerformanceMode(int32_t value);
    void changeEndPointPerformanceMode(int32_t value);
    void changeCutoffPerformanceMode(int8_t value);
    void changeFilterTypePerformanceMode(uint8_t mode);
    void changeSamplePlaybackPerformanceMode(uint8_t value);
    void changeWavetableWindowPerformanceMode(int16_t value);
    void changeGranularPositionPerformanceMode(int16_t value);

    void changePositionPerformanceMode(int16_t value);

    void changeAmpLfoRatePerformanceMode(int8_t value);
    void changeCutoffLfoRatePerformanceMode(int8_t value);
    void changePositionLfoRatePerformanceMode(int8_t value);
    void changePanningLfoRatePerformanceMode(int8_t value);
    void changeFinetuneLfoRatePerformanceMode(int8_t value);

    //*****************************END
    void endVolumePerformanceMode();
    void endPanningPerformanceMode();
    void endBitDepthPerformanceMode();
    void endOverdrivePerformanceMode();
    void endTunePerformanceMode();
    void endDelaySendPerformanceMode();
    void endReverbSendPerformanceMode();
    void endStartPointPerformanceMode();
    void endCutoffPerformanceMode();
    void endFilterTypePerformanceMode();
    void endSamplePlaybackPerformanceMode();
    void endEndPointPerformanceMode();
    void endWavetableWindowPerformanceMode();
    void endGranularPositionPerformanceMode();

    void endPositionPerformanceMode();

    void endAmpLfoRatePerformanceMode();
    void endCutoffLfoRatePerformanceMode();
    void endPositionLfoRatePerformanceMode();
    void endPanningLfoRatePerformanceMode();
    void endFinetuneLfoRatePerformanceMode();

    void setPassEnvelope(uint8_t state);
    float getRMSValue();
    AudioVoice* getAudioVoice();

  private:
    friend audioEngine;

    AudioVoice audioVoice;
#ifndef PLATFORM_IMX
    MonoToStereo* monoToStereoPtr;
#else
    std::vector<AudioAmplifier*> panningAmplifierPtr;
#endif
    std::vector<AudioPlayMemory*> playMemPtr;
    std::vector<AudioAmplifier*> ampPtr;
    std::vector<AudioFilterStateVariable*> filterPtr;
    std::vector<AudioBitDepth*> bitDepthPtr;
    std::vector<AudioAnalyzeRMS*> rmsPtr;

    envelopeGenerator* envelopePtr[envMax];
    uint8_t nChannel;
    uint8_t lastSeqFx[2];
    uint8_t lastSeqVal[2];
    int8_t currentNote;
    uint32_t refreshRegister;
    uint8_t interfaceEndReleaseFlag = 0;
    uint8_t interfacePlayingEndFlag = 0;
    uint8_t currentPlayState = 0;
    uint8_t lastPlayState = 0;

    uint8_t muteState = 0;
    uint8_t onlyDelayMuteState = 0;
    uint8_t onlyReverbMuteState = 0;

    uint8_t envelopePassFlag = 0;
    float currentSeqTempo = 0;
    float lastSeqTempo = 0;

    envelopeGenerator::strEnv lfoBasedEnvelope[envMax];
    const envelopeGenerator::strEnv passEnvelope = { .amount = 1.0,
                                                     .delay = 0,
                                                     .attack = 0,
                                                     .hold = 0,
                                                     .decay = 0,
                                                     .sustain = 1.0,
                                                     .release = 0,
                                                     .loop = 0,
                                                     .enable = 0 };

    uint8_t isActiveFlag = 0;

    bool isTrackDisplayed;
    bool onEndDisplay;
    Timer* timer;
    AudioMixer<MixerAndFxs<audioEngine::kVoices>::kSendsInputsCount>* preDelayMixer;
    AudioMixer<MixerAndFxs<audioEngine::kVoices>::kSendsInputsCount>* prePolyverbMixer;
    //**********************FUNKCJE POMOCNICZE
    uint8_t isFxVelocity(uint8_t fx_id);
    float getMostSignificantAmount();
    uint8_t getMostSignificantVolume();
    void initEnvelopesParameters(uint8_t n, envelopeGenerator::strEnv* env);
    void setSyncParamsLFO(uint8_t type);
    void setSyncParamsAmpLFO();
    bool isActiveEnvelope(uint8_t type);

    //**********************NOTE ON/OFF HANDLERS
    //*****note on fx
    void handleInitFxNoteOnEnvelope(uint8_t n);
    void handleInitFxNoteOnAmpEnvelope();
    void handleInitFxNoteOnAllEnvelopes(); // wywolywany w note on fx
    void handleInitFxNoteOnFilter();
    void handleFxNoteOnFilter();
    void handleFxNoteOnGain();
    void handleFxNoteOnPanning();
    void handleFxNoteOnBitDepth();
    void handleFxNoteOnDelaySend();
    void handleFxNoteOnReverbSend();
    //*****note on
    void handleInitNoteOnEnvelope(uint8_t n);
    void handleInitNoteOnAmpEnvelope();
    void handleInitNoteOnAllEnvelopes(); // wywolywany w note on
    void handleNoteOnFilter();
    void handleNoteOnGain();
    void handleNoteOnPanning();
    void handleNoteOnBitCrusher();
    void handleNoteOnDelaySend();
    void handleNoteOnReverbSend();
    //*****note off
    void noteOffFade();
    void noteOffCut();
    void noteOffOrdinary();
    //*************************************************
    //**********************UPDATE HANDLERS
    inline void handleUpdateEndPlayDetect();
    inline void handleUpdateEnvelope(uint8_t type, bool enableCondition);
    inline void handleUpdateEndReleaseAction();
    inline void handleUpdateRefreshLP1();
    inline void handleUpdateRefreshLP2();
    inline void handleUpdateRefreshFinetune();
    inline void handleUpdateRefreshTune();
    inline void handleUpdateRefreshVolume();
    inline void handleUpdateRefreshPanning();
    inline void handleUpdateRefreshCutoff();
    inline void handleUpdateRefreshResonance();
    void handleUpdateRefreshDelay();
    void handleUpdateRefreshReverb();
    inline void handleUpdateRefreshOverdrive();
    inline void handleUpdateRefreshBitDepth();
    inline void handleUpdateRefreshWtPos();
    inline void handleUpdateRefreshGranPos();
    inline void handleUpdateRefreshGranLen();
    inline void handleUpdateRefreshGranWave();
    inline void handleUpdateRefreshGranLoop();
    inline void handleUpdateRefreshAmpLFO();
    inline void handleUpdateRefreshCutoffLFO();
    inline void handleUpdateRefreshWtPosLFO();
    inline void handleUpdateRefreshGranPosLFO();
    inline void handleUpdateRefreshPanningLFO();
    inline void handleUpdateRefreshFinetuneLFO();

    //*************************************************
    //************************* FX HANDLE
    //******* fx start
    void fxFilter(uint8_t fx_val, uint8_t fx_n, uint8_t type);
    void fxGlide(uint8_t fx_val, uint8_t fx_n);
    void fxFinetune(uint8_t fx_val, uint8_t fx_n);
    void fxPanning(uint8_t fx_val, uint8_t fx_n);
    void fxBitDepth(uint8_t fx_val, uint8_t fx_n);
    void fxDelaySend(uint8_t fx_val, uint8_t fx_n);
    void fxReverbSend(uint8_t fx_val, uint8_t fx_n);
    void fxOverdrive(uint8_t fx_val, uint8_t fx_n);
    void fxReversePlayback(uint8_t fx_val, uint8_t fx_n);
    //***position
    void fxPosition(uint8_t fx_val, uint8_t fx_n);
    void fxPositionStartPoint(uint8_t fx_val, uint8_t fx_n);
    void fxPositionWavetable(uint8_t fx_val, uint8_t fx_n);
    void fxPositionGranular(uint8_t fx_val, uint8_t fx_n);
    //***
    void fxSlideUp(uint8_t fx_val);
    void fxSlideDown(uint8_t fx_val);
    void fxTune(uint8_t fx_val, uint8_t fx_n);
    void fxVolume(uint8_t fx_val, uint8_t fx_n);
    void fxRandomVolume(uint8_t fx_val, uint8_t fx_n);
    void fxSampleSlice(uint8_t fx_val, uint8_t fx_n);
    void fxVolumeLFO(uint8_t fx_val, uint8_t fx_n);
    void fxCutoffLFO(uint8_t fx_val, uint8_t fx_n);
    //***position lfo
    void fxPositionLFO(uint8_t fx_val, uint8_t fx_n);
    void fxPositionGranularLFO(uint8_t fx_val, uint8_t fx_n);
    void fxPositionWavetableLFO(uint8_t fx_val, uint8_t fx_n);
    //***
    void fxPanningLFO(uint8_t fx_val, uint8_t fx_n);
    //***
    void fxFinetuneLFO(uint8_t fx_val, uint8_t fx_n);
    void fxAmpEnv(uint8_t fx_val, uint8_t fx_n);
    void fxFilterEnv(uint8_t fx_val, uint8_t fx_n);
    void fxGranularLength(uint8_t fx_val, uint8_t fx_n);
    void endFxAmpEnv(uint8_t fx_n);
    void endFxFilterEnv(uint8_t fx_n);
    void endFxGranularLength(uint8_t fx_n);
    //*************************************************
    //************************* FX END HANDLE
    void endFxFilter(uint8_t fx_n);
    void endFxGlide(uint8_t fx_n);
    void endFxFinetune(uint8_t fx_n);
    void endFxPanning(uint8_t fx_n);
    void endFxBitDepth(uint8_t fx_n);
    void endFxDelaySend(uint8_t fx_n);
    void endFxReverbSend(uint8_t fx_n);
    void endFxOverdrive(uint8_t fx_n);
    void endFxReversePlayback(uint8_t fx_n);
    //****** position
    void endFxPosition(uint8_t fx_n);
    void endFxPositionStartPoint(uint8_t fx_n);
    void endFxPositionGranular(uint8_t fx_n);
    void endFxPositionWavetable(uint8_t fx_n);
    //******
    void endFxTune(uint8_t fx_n);
    void endFxRandomVolume(uint8_t fx_n);
    void endFxVolume(uint8_t fx_n);
    void endFxSlice(uint8_t fx_n);
    void endFxVolumeLFO(uint8_t fx_n);
    void endFxCutoffLFO(uint8_t fx_n);
    //****** position lfo
    void endFxPositionLFO(uint8_t fx_n);
    void endFxPositionGranularLFO(uint8_t fx_n);
    void endFxPositionWavetableLFO(uint8_t fx_n);
    //******
    void endFxPanningLFO(uint8_t fx_n);
    //******
    void endFxFinetuneLFO(uint8_t fx_n);
    //******FUNKCJE NARZUCAJACE WARTOSCI SILNIKOWI

    void setFxBitDepth();
    void clearFxBitDepth();

    void setFxFinetune();
    void clearFxFinetune();

    void setFxGlide();
    void clearFxGlide();

    void setFxReverse();
    void clearFxReverse();

    void setFxPositionGranular();
    void clearFxPositionGranular();

    void setFxGranularLength();
    void clearFxGranularLength();

    void setFxPositionWavetable();
    void clearFxPositionWavetable();

    void setFxTune();
    void clearFxTune();

    void setFxStartPoint(uint8_t fx_n);
    void clearFxStartPoint();

    void setFxSlice();
    void clearFxSlice();

    void setFxOverdrive();
    void clearFxOverdrive();

    void setFxCutoff();
    void clearFxCutoff();
    void setFxFilterType();
    void clearFxFilterType();

    void setFxAmpRateLFO();
    void clearFxAmpRateLFO();
    void syncFxAmpLFO();

    void changeFilterType(uint8_t type);
    void filterConnect();
    void filterDisconnect();

    void changePointsPerformanceMode(int32_t spValue, int32_t epValue);
    void endPointsPerformanceMode();

    void calcLfoBasedEnvelope(envelopeGenerator::strEnv* env,
                              strInstrument::strEnvBasedLfo* lfo,
                              uint8_t rate,
                              uint8_t isAmp,
                              uint8_t loopMode = 1);

    float fmap(float x, float in_min, float in_max, float out_min, float out_max);
    int8_t stepVelocity = -1;
};

extern audioEngine engine;
extern playerEngine instrumentPlayer[8];
extern uint8_t isBlockedInstrument[48];
extern uint8_t isBlockedNoteOn[48];

#endif /* SOURCE_MTAUDIOENGINE_H_ */
