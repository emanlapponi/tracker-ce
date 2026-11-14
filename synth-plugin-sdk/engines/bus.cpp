#include "psyn/psyn_cpp_bridge.h"
#include "PolySynth/sdk/SynthVoiceSDK.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace polysynth
{

namespace
{

enum class BusParamId : int
{
    kSynth1 = 0,
    kSynth2,
    kSynth3,
    kLimiter,
    kThreshold,
    kAttack,
    kRelease,
    kTrack1,
    kTrack2,
    kTrack3,
    kTrack4,
    kTrack5,
    kTrack6,
    kTrack7,
    kTrack8,
};

static constexpr int kSynthSources = 3;
static constexpr int kTrackSources = 8;

std::string formatOnOff(const Patch& /*patch*/, Patch::paramvalue_t value)
{
    return value > 0.5f ? "On" : "Off";
}

float msToCoeff(float ms)
{
    return 1.0f - std::exp(-2.2f * 1000.0f / (sdk::kSampleRate * ms));
}

} // namespace

/**
 * Receives the tracks and synth parts routed through its source params and runs them through a
 * limiter (the master limiter algorithm, in float).
 */
class BusVoice : public sdk::CustomSynthVoice<BusVoice, BusParamId>
{
  public:
    BusVoice() = default;

    bool isActive() final { return true; }

    void onNoteOn(const InstrumentVoiceContext& /*ctx*/,
                  note_t /*note*/,
                  float /*velocity*/,
                  bool /*isLegatoStep*/) final
    {
    }

    void onNoteOff(const InstrumentVoiceContext& /*ctx*/, note_t /*note*/) final {}

    void renderVoiceAudio(const InstrumentVoiceContext& ctx, AudioBufferStereoRef outputBuffer)
    {
        renderVoiceAudio(ctx, std::nullopt, outputBuffer, {});
    }

    void renderVoiceAudio(const InstrumentVoiceContext& /*ctx*/,
                          std::optional<AudioBufferStereoRef> inputBuffer,
                          AudioBufferStereoRef outputBuffer,
                          std::span<AudioBufferRef> /*auxOutBuffer*/)
    {
        for (size_t i = 0; i < kAudioBufferSize; ++i)
        {
            const std::array<float, 2> in = { inputBuffer ? (*inputBuffer)[0][i] : 0.0f,
                                              inputBuffer ? (*inputBuffer)[1][i] : 0.0f };
            if (!limiterEnabled)
            {
                outputBuffer[0][i] = in[0];
                outputBuffer[1][i] = in[1];
                continue;
            }

            const float peak = std::max(std::fabs(in[0]), std::fabs(in[1]));
            peakFollower += (peak > peakFollower ? attackCoeff : releaseCoeff) *
                            (peak - peakFollower);

            const float target = peakFollower > thresholdAmp ? thresholdAmp / peakFollower : 1.0f;
            gain += (target < gain ? attackCoeff : releaseCoeff) * (target - gain);

            outputBuffer[0][i] = gain * delayLine[delayPos][0];
            outputBuffer[1][i] = gain * delayLine[delayPos][1];
            delayLine[delayPos] = in;
            delayPos = (delayPos + 1) % kLimiterDelay;
        }
    }

    static void createMetadata(const Patch& /*patch*/, Patch::Metadata& metadata)
    {
        using namespace polysynth::sdk;

        metadata.params.resize(enumutil::EnumMax<BusParamId>::value + 1);
        metadata.params.shrink_to_fit();

        if (paramHandlers.empty())
        {
            paramHandlers.resize(enumutil::EnumMax<BusParamId>::value + 1);
        }

        ParamHelper<BusVoice> helper(metadata, paramHandlers);

        for (int i = 0; i < kSynthSources; ++i)
        {
            addSourceParam(helper,
                           *BusParamId::kSynth1 + i,
                           fmt::format("Synth {}", i + 1),
                           Patch::Metadata::Param::kInputSourceSynthBase + i);
        }

        for (int i = 0; i < kTrackSources; ++i)
        {
            addSourceParam(
              helper, *BusParamId::kTrack1 + i, fmt::format("Track {}", i + 1), i);
        }

        helper.addParam(
          *BusParamId::kLimiter,
          {
            .displayName = "Limiter",
            .defaultValue = 1,
            .minValue = 0,
            .maxValue = 1,
            .isEnum = true,
            .valueFormatter = formatOnOff,
          },
          [](auto& voice, Patch::optional_paramvalue_t val)
          { voice.limiterEnabled = *val > 0.5f; });

        helper.addParam(
          *BusParamId::kThreshold,
          {
            .displayName = "Threshold",
            .defaultValue = -6.0f,
            .minValue = -40.0f,
            .maxValue = 0.0f,
            .valueFormatter =
              [](const Patch& /*patch*/, Patch::paramvalue_t value)
            { return fmt::format("{:.1f} dB", value); },
          },
          [](auto& voice, Patch::optional_paramvalue_t val)
          { voice.thresholdAmp = dbToAmplitude(*val); });

        helper.addParam(
          *BusParamId::kAttack,
          {
            .displayName = "Attack",
            .defaultValue = 5.0f,
            .minValue = 1.0f,
            .maxValue = 100.0f,
            .editCurve = Patch::Metadata::Param::Curve::kExponential,
            .valueFormatter =
              [](const Patch& /*patch*/, Patch::paramvalue_t value)
            { return fmt::format("{:.0f} ms", value); },
          },
          [](auto& voice, Patch::optional_paramvalue_t val)
          { voice.attackCoeff = msToCoeff(*val); });

        helper.addParam(
          *BusParamId::kRelease,
          {
            .displayName = "Release",
            .defaultValue = 100.0f,
            .minValue = 1.0f,
            .maxValue = 1000.0f,
            .editCurve = Patch::Metadata::Param::Curve::kExponential,
            .valueFormatter =
              [](const Patch& /*patch*/, Patch::paramvalue_t value)
            { return fmt::format("{:.0f} ms", value); },
          },
          [](auto& voice, Patch::optional_paramvalue_t val)
          { voice.releaseCoeff = msToCoeff(*val); });

        Patch::Metadata::EditorLayout::EditorSection mixer{ .name = "Mixer", .shortName = "Mixer" };
        for (int i = 0; i < kTrackSources; ++i)
        {
            mixer.elements.push_back(
              { .type = EditorElement::Type::kParam, .id = *BusParamId::kTrack1 + i });
        }
        for (int i = 0; i < kSynthSources; ++i)
        {
            mixer.elements.push_back(
              { .type = EditorElement::Type::kParam, .id = *BusParamId::kSynth1 + i });
        }

        metadata.layoutFlat = Patch::Metadata::EditorLayout{
            .sections = {
              std::move(mixer),
              {
                .name = "FX",
                .shortName = "FX",
                .elements = {
                  { .type = EditorElement::Type::kParam, .id = *BusParamId::kLimiter },
                  { .type = EditorElement::Type::kParam, .id = *BusParamId::kThreshold },
                  { .type = EditorElement::Type::kParam, .id = *BusParamId::kAttack },
                  { .type = EditorElement::Type::kParam, .id = *BusParamId::kRelease },
                },
              },
            },
        };

        metadata.layout23 = metadata.layoutFlat;

        metadata.outputChannels = Patch::Metadata::OutputChannels::kStereo;
        metadata.maxVoices = 1;
    }

  private:
    static void addSourceParam(sdk::ParamHelper<BusVoice>& helper,
                               int paramId,
                               std::string name,
                               int inputSource)
    {
        helper.addParam(
          paramId,
          {
            .displayName = std::move(name),
            .defaultValue = 0,
            .minValue = 0,
            .maxValue = 1,
            .isEnum = true,
            .valueFormatter = formatOnOff,
            .inputSource = inputSource,
          },
          [](auto& /*voice*/, Patch::optional_paramvalue_t /*val*/) {});
    }

    static constexpr size_t kLimiterDelay = 5;

    bool limiterEnabled = true;
    float thresholdAmp = sdk::dbToAmplitude(-6.0f);
    float attackCoeff = msToCoeff(5.0f);
    float releaseCoeff = msToCoeff(100.0f);

    float peakFollower = 0.0f;
    float gain = 1.0f;
    std::array<std::array<float, 2>, kLimiterDelay> delayLine{};
    size_t delayPos = 0;
};

} // namespace polysynth

PSYN_REGISTER_CPP_VOICE(polysynth::BusVoice)
