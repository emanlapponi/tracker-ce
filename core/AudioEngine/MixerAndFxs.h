#ifndef AUDIOENGINE_MIXERANDFXS_H_
#define AUDIOENGINE_MIXERANDFXS_H_
#include "Audio.h"

enum struct MixerAndFxChannelType
{
    kLeft,
    kRight,
    kChannelCount
};

template<int voicesCount>
class MixerAndFxs
{
  public:
    enum FxType
    {
        kDelay,
        kReverb,
        kFxCount
    };

    enum TrackAndFxsChannels
    {
        kDryMixChannel,
        kDelayChannel,
        kReverbChannel,
        kInputChannel
    };

    static constexpr int kExternalInputsCount = 1;
    static constexpr int kFirstSynthSendInput = 2 * kExternalInputsCount + 2 * voicesCount;
    static constexpr int kSendsInputsCount = kFirstSynthSendInput + kSynthParts;
    static constexpr int kTrackMixerOutputsCount = 1;
    static constexpr int kTrackAndSendsMixerInputs =
      kTrackMixerOutputsCount + kExternalInputsCount + FxType::kFxCount;
    static constexpr int kTracksAndSynthVoices = voicesCount + kSynthParts;

    MixerAndFxs();
    AudioMixer<kSendsInputsCount>* getPreFxMixer(FxType type);
    AudioMixer<kTracksAndSynthVoices>* getTracksMixer(MixerAndFxChannelType channel);
    AudioMixer<kTrackAndSendsMixerInputs>* getTracksAndSendsMixer(MixerAndFxChannelType channel);
    AudioEffectShortDelay* getDelay();
    AudioEffectPolyverb* getPolyverb();
    AudioAnalyzeRMS* getDelayRMS(MixerAndFxChannelType channel);
    AudioAnalyzeRMS* getPolyverbRMS(MixerAndFxChannelType channel);
    AudioAnalyzeRMS* getDryMixRMS(MixerAndFxChannelType channel);
#ifdef FEATURE_SYNTH
    AudioAnalyzeRMS* getSynthRMS(int synthIdx, MixerAndFxChannelType channel);
#endif

  private:
    AudioMixer<kSendsInputsCount> preFxMixer[FxType::kFxCount];
    AudioMixer<kTracksAndSynthVoices>
      tracksAndSynthMixer[(int)MixerAndFxChannelType::kChannelCount];
    AudioMixer<kTrackAndSendsMixerInputs>
      tracksAndSendsMixer[(int)MixerAndFxChannelType::kChannelCount];

    AudioAnalyzeRMS delayRMS[(int)MixerAndFxChannelType::kChannelCount];
    AudioAnalyzeRMS polyverbRMS[(int)MixerAndFxChannelType::kChannelCount];
    AudioAnalyzeRMS dryMixRMS[(int)MixerAndFxChannelType::kChannelCount];
#ifdef FEATURE_SYNTH
    AudioAnalyzeRMS synthRMS[kSynthParts][(int)MixerAndFxChannelType::kChannelCount];
#endif

    AudioEffectShortDelay shortDelay;
    AudioEffectPolyverb polyverb;

    std::vector<AudioConnection*> connections;
};

#endif /* AUDIOENGINE_MIXERANDFXS_H_ */
