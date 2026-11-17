#include "psyn/psyn_cpp_bridge.h"
#include "PolySynth/sdk/SynthVoiceSDK.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace polysynth
{

namespace
{

enum class ChipParamId : int
{
    kModel = 0,
    kTune,
    kFine,
    kShape,
    kPwmRate,
    kPwmDepth,
    kSubMix,
    kArpChord,
    kArpSpeed,
    kPitchSweep,
    kSweepTime,
    kVibRate,
    kVibDepth,
    kAmpAttack,
    kAmpDecay,
    kAmpSustain,
    kAmpRelease,
    kFilterCutoff,
    kFilterResonance,
    kFilterEnvAmount,
    kBitcrush,
    kDecimate,
    kPlayMode,
    kGlideTime,
    kVolume,
};

enum class ChipModel : int
{
    kPulse2A03 = 0,
    kSidSawTri = 1,
    kNoiseLfsr = 2,
    kFmBell = 3,
    kSuperSquare = 4,
    kZapLaser = 5,
    kArpChord = 6,
    kChipDrum = 7,
};

static constexpr std::array<const char*, 8> kModelNames = {
    "2A03 Pulse",   "SID Saw/Tri", "Noise / LFSR", "FM Bell",
    "Super Square", "Zap / Laser", "Arp Chord",    "Chip Drum",
};

static constexpr std::array<const char*, 8> kArpNames = {
    "Off", "Octave", "Fifth", "Major", "Minor", "Sus4", "Maj7", "Min7",
};

// Semitone offsets for 4-step chiptune chord arpeggiator
static constexpr int8_t kArpChords[8][4] = {
    { 0, 0, 0, 0 },   // Off
    { 0, 12, 0, 12 }, // Octave
    { 0, 7, 12, 7 },  // Fifth
    { 0, 4, 7, 12 },  // Major
    { 0, 3, 7, 12 },  // Minor
    { 0, 5, 7, 12 },  // Sus4
    { 0, 4, 7, 11 },  // Maj7
    { 0, 3, 7, 10 },  // Min7
};

inline float polyBlep(float t, float dt)
{
    if (dt <= 0.0f)
    {
        return 0.0f;
    }
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt)
    {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

using sdk::fastPow2;
using sdk::fastSine;

inline float wrapPhase(float x)
{
    return (x >= 1.0f) ? x - 1.0f : x;
}

inline float nesSteppedTriangle(float phase01)
{
    const float tri = (phase01 < 0.5f) ? (phase01 * 4.0f - 1.0f) : (3.0f - phase01 * 4.0f);
    return std::round(tri * 7.5f) * (1.0f / 7.5f);
}

inline uint16_t stepLfsr(uint16_t lfsr, bool metallicShortMode)
{
    const uint16_t tap = metallicShortMode ? ((lfsr >> 6) & 1u) : ((lfsr >> 1) & 1u);
    const uint16_t bit = ((lfsr >> 0) ^ tap) & 1u;
    return static_cast<uint16_t>((lfsr >> 1) | (bit << 14));
}

} // namespace

class ChiptuneSynthVoice : public sdk::CustomSynthVoice<ChiptuneSynthVoice, ChipParamId>
{
  public:
    ChiptuneSynthVoice() = default;

    bool isActive() final { return envStage != EnvStage::kIdle; }

    void tickIdle(const InstrumentVoiceContext& /*ctx*/) final
    {
        const float blockAdvance = static_cast<float>(kAudioBufferSize) / sdk::kSampleRate;
        pwmPhase += pwmRateHz * blockAdvance;
        while (pwmPhase >= 1.0f)
        {
            pwmPhase -= 1.0f;
        }
        vibPhase += vibRateHz * blockAdvance;
        while (vibPhase >= 1.0f)
        {
            vibPhase -= 1.0f;
        }
    }

    void onNoteOn(const InstrumentVoiceContext& /*ctx*/,
                  note_t note,
                  float /*velocity*/,
                  bool isLegatoStep) final
    {
        const float targetNote = static_cast<float>(note);
        if (!hasPlayedAnyNote || glideTimeSec <= 0.001f ||
            (playMode == PlayMode::kLegato && !isLegatoStep))
        {
            currentGlideNote = targetNote;
        }
        targetGlideNote = targetNote;
        hasPlayedAnyNote = true;

        if (!isLegatoStep || envStage == EnvStage::kIdle || envStage == EnvStage::kRelease)
        {
            envStage = EnvStage::kAttack;
            noteAgeSamples = 0;
            arpStepCounter = 0;
            arpSampleCounter = 0;
            lfsrState = 0x7FFFu;
        }
    }

    void onNoteOff(const InstrumentVoiceContext& /*ctx*/, note_t /*note*/) final
    {
        if (envStage != EnvStage::kIdle)
        {
            envStage = EnvStage::kRelease;
        }
    }

    void renderVoiceAudio(const InstrumentVoiceContext& ctx,
                          AudioBufferStereoRef outputBuffer)
    {
        auto out = outputBuffer[0];
        if (envStage == EnvStage::kIdle)
        {
            tickIdle(ctx);
            std::fill(out.begin(), out.end(), 0.0f);
            std::fill(lastGraphSamples.begin(), lastGraphSamples.end(), 0.0f);
            return;
        }

        const float sr = sdk::kSampleRate;
        const float invSr = 1.0f / sr;

        const float attackInc =
          (ampAttackSec > 0.001f) ? (1.0f / (ampAttackSec * sr)) : 1.0f;
        const float decayDec =
          (ampDecaySec > 0.002f) ? ((1.0f - ampSustainLevel) / (ampDecaySec * sr)) : 1.0f;
        const float releaseDec =
          (ampReleaseSec > 0.002f) ? (1.0f / (ampReleaseSec * sr)) : 1.0f;

        const float glideStep =
          (glideTimeSec > 0.001f) ? (1.0f / (glideTimeSec * sr)) : 1.0f;

        const int effArpIdx =
          (model == ChipModel::kArpChord && arpChordIdx == 0) ? 3 : arpChordIdx;
        const uint32_t arpPeriodSamples =
          std::max<uint32_t>(64u, static_cast<uint32_t>(sr / std::max(5.0f, arpSpeedHz)));

        const float sweepSamples = std::max(64.0f, sweepTimeSec * sr);
        const float invSweepSamples = 1.0f / sweepSamples;
        const float zapSweepScale = 12.0f + 24.0f * shape;

        const bool crushActive = (bitcrushBits < 15.5f);
        const float crushScale = crushActive ? fastPow2(bitcrushBits - 1.0f) : 32768.0f;
        const float invCrushScale = 1.0f / crushScale;
        const int decimateFactor = std::clamp(static_cast<int>(decimateRate + 0.5f), 1, 32);

        const float qDamper = std::clamp(1.0f - 0.85f * filterResonance, 0.12f, 1.0f);

        const float baseNoteOffset = tuneSemitones + (fineCents * 0.01f) + pitchBendSemitones;
        const float pwmPhaseInc = pwmRateHz * invSr;
        const float vibPhaseInc = vibRateHz * invSr;
        const float vibDepthSemi = vibDepth * 1.5f;
        const bool hasVibrato = (vibDepth > 0.0f);
        const float baseDuty = 0.125f + 0.75f * shape;
        const float pwmDutyScale = 0.38f * pwmDepth;
        const float lfsrRateMult = 2.0f + 14.0f * (1.0f - pwmDepth);
        const bool lfsrShortMode = (shape >= 0.5f);
        static constexpr std::array<float, 8> kFmRatios = {
            1.0f, 2.0f, 3.0f, 3.5f, 4.0f, 5.0f, 7.0f, 9.0f
        };
        const float fmRatio = kFmRatios[std::clamp(static_cast<int>(shape * 7.99f), 0, 7)];
        const float fmModScale = 0.4f + 3.5f * (0.35f + 0.65f * pwmDepth);
        const float superSquareSpread = 0.002f + 0.018f * shape;
        const float superSquareMult2 = 1.0f + superSquareSpread;
        const float superSquareMult3 = 1.0f - superSquareSpread;

        static constexpr float kDrumDecayStep = 0.99971659f;
        float drumDecay = (model == ChipModel::kChipDrum)
                            ? fastPow2(-1.4426950f * static_cast<float>(noteAgeSamples) *
                                       (1.0f / (0.08f * sr)))
                            : 0.0f;
        const float drumSweepScale = 3.5f * (1.0f - shape * 0.7f);
        const bool drumMetallicMode = (shape > 0.75f);
        const float drumSustainedNoiseMix = std::clamp(shape * 1.25f, 0.0f, 1.0f);

        const bool hasSubMix = (subMix > 0.001f);
        const float subMainGain = 1.0f - 0.35f * subMix;
        const float subOscGain = 0.75f * subMix;

        const float filterEnvScale = filterEnvAmount * 4.0f;
        const bool hasFilterEnv = (std::abs(filterEnvAmount) > 0.0001f);
        float cachedFilterEnvLevel = -1.0f;
        float cachedEnvCutoff = std::clamp(filterCutoffHz, 40.0f, 18000.0f);
        bool cachedFilterActive = (cachedEnvCutoff < 17500.0f || filterResonance > 0.02f);
        float cachedSvfF =
          (!hasFilterEnv && cachedFilterActive)
            ? std::min(0.85f, 2.0f * fastSine(0.5f * cachedEnvCutoff * invSr))
            : 0.0f;

        for (size_t i = 0; i < out.size(); ++i)
        {
            switch (envStage)
            {
                case EnvStage::kAttack:
                    envLevel += attackInc;
                    if (envLevel >= 1.0f)
                    {
                        envLevel = 1.0f;
                        envStage = EnvStage::kDecay;
                    }
                    break;
                case EnvStage::kDecay:
                    envLevel -= decayDec;
                    if (envLevel <= ampSustainLevel)
                    {
                        envLevel = ampSustainLevel;
                        envStage = (ampSustainLevel <= 0.0001f) ? EnvStage::kIdle
                                                                : EnvStage::kSustain;
                    }
                    break;
                case EnvStage::kSustain:
                    envLevel = ampSustainLevel;
                    break;
                case EnvStage::kRelease:
                    envLevel -= releaseDec;
                    if (envLevel <= 0.0001f)
                    {
                        envLevel = 0.0f;
                        envStage = EnvStage::kIdle;
                    }
                    break;
                case EnvStage::kIdle:
                    out[i] = 0.0f;
                    lastGraphSamples[i] = 0.0f;
                    continue;
            }

            if (std::abs(currentGlideNote - targetGlideNote) > 0.001f)
            {
                const float diff = targetGlideNote - currentGlideNote;
                const float step = std::min(std::abs(diff), glideStep * 24.0f);
                currentGlideNote += (diff > 0.0f) ? step : -step;
            }
            else
            {
                currentGlideNote = targetGlideNote;
            }

            int8_t arpSemi = 0;
            if (effArpIdx > 0 && effArpIdx < 8)
            {
                if (++arpSampleCounter >= arpPeriodSamples)
                {
                    arpSampleCounter = 0;
                    arpStepCounter = (arpStepCounter + 1u) & 3u;
                }
                arpSemi = kArpChords[effArpIdx][arpStepCounter & 3u];
            }

            pwmPhase += pwmPhaseInc;
            if (pwmPhase >= 1.0f)
            {
                pwmPhase -= 1.0f;
            }
            const float pwmLfo =
              (pwmPhase < 0.5f) ? (pwmPhase * 4.0f - 1.0f) : (3.0f - pwmPhase * 4.0f);

            vibPhase += vibPhaseInc;
            if (vibPhase >= 1.0f)
            {
                vibPhase -= 1.0f;
            }
            const float vibSemitones = hasVibrato ? (fastSine(vibPhase) * vibDepthSemi) : 0.0f;

            const float sweepProgress =
              std::min(1.0f, static_cast<float>(noteAgeSamples) * invSweepSamples);
            float activeSweepSemi = pitchSweepSemitones * sweepProgress;
            if (model == ChipModel::kZapLaser)
            {
                const float zapEnv = 1.0f - sweepProgress;
                activeSweepSemi += zapSweepScale * (zapEnv * zapEnv * zapEnv - 1.0f);
            }

            const float totalNote = currentGlideNote + baseNoteOffset +
                                    static_cast<float>(arpSemi) + vibSemitones + activeSweepSemi;
            const float freq =
              std::clamp(8.1757989f * fastPow2(totalNote * (1.0f / 12.0f)), 16.0f, 18000.0f);
            const float dt = freq * invSr;

            const float duty = std::clamp(baseDuty + pwmDutyScale * pwmLfo, 0.06f, 0.94f);

            float sig = 0.0f;
            phase += dt;
            bool wrapped = false;
            if (phase >= 1.0f)
            {
                phase -= 1.0f;
                wrapped = true;
            }

            switch (model)
            {
                case ChipModel::kPulse2A03:
                case ChipModel::kArpChord:
                case ChipModel::kZapLaser:
                {
                    float pulse = (phase < duty) ? 1.0f : -1.0f;
                    pulse += polyBlep(phase, dt);
                    pulse -= polyBlep(wrapPhase(phase - duty + 1.0f), dt);
                    sig = pulse * 0.75f;
                    break;
                }

                case ChipModel::kSidSawTri:
                {
                    float saw = 2.0f * phase - 1.0f;
                    saw -= polyBlep(phase, dt);
                    const float tri =
                      (phase < 0.5f) ? (phase * 4.0f - 1.0f) : (3.0f - phase * 4.0f);
                    const float ring = saw * ((subPhase < 0.5f) ? 1.0f : -1.0f);
                    sig = ((1.0f - duty) * tri + duty * (0.65f * saw + 0.35f * ring)) * 0.8f;
                    break;
                }

                case ChipModel::kNoiseLfsr:
                {
                    lfsrPhase += dt * lfsrRateMult;
                    while (lfsrPhase >= 1.0f)
                    {
                        lfsrPhase -= 1.0f;
                        lfsrState = stepLfsr(lfsrState, lfsrShortMode);
                    }
                    sig = ((lfsrState & 1u) != 0u ? 0.8f : -0.8f);
                    break;
                }

                case ChipModel::kFmBell:
                {
                    const float modDt = dt * fmRatio;
                    osc2Phase += modDt;
                    if (osc2Phase >= 1.0f)
                    {
                        osc2Phase -= std::floor(osc2Phase);
                    }
                    const float modIndex = fmModScale * envLevel;
                    const float modSig = fastSine(osc2Phase + 0.15f * pwmLfo);
                    const float rawCarrier = fastSine(phase + modIndex * modSig * 0.5f);
                    sig = std::round(rawCarrier * 31.0f) * (0.85f / 31.0f);
                    break;
                }

                case ChipModel::kSuperSquare:
                {
                    const float dt2 = dt * superSquareMult2;
                    const float dt3 = dt * superSquareMult3;
                    osc2Phase += dt2;
                    if (osc2Phase >= 1.0f)
                    {
                        osc2Phase -= 1.0f;
                    }
                    osc3Phase += dt3;
                    if (osc3Phase >= 1.0f)
                    {
                        osc3Phase -= 1.0f;
                    }
                    float p1 = (phase < duty) ? 1.0f : -1.0f;
                    p1 += polyBlep(phase, dt) -
                          polyBlep(wrapPhase(phase - duty + 1.0f), dt);
                    float p2 = (osc2Phase < duty) ? 1.0f : -1.0f;
                    p2 += polyBlep(osc2Phase, dt2) -
                          polyBlep(wrapPhase(osc2Phase - duty + 1.0f), dt2);
                    float p3 = (osc3Phase < duty) ? 1.0f : -1.0f;
                    p3 += polyBlep(osc3Phase, dt3) -
                          polyBlep(wrapPhase(osc3Phase - duty + 1.0f), dt3);
                    sig = (p1 + 0.8f * p2 + 0.8f * p3) * 0.34f;
                    break;
                }

                case ChipModel::kChipDrum:
                {
                    const float drumFreq =
                      freq * (1.0f + drumSweepScale * drumDecay * drumDecay);
                    drumDecay *= kDrumDecayStep;
                    osc2Phase += drumFreq * invSr;
                    if (osc2Phase >= 1.0f)
                    {
                        osc2Phase -= std::floor(osc2Phase);
                    }
                    if (wrapped || (noteAgeSamples & 1u) == 0u)
                    {
                        lfsrState = stepLfsr(lfsrState, drumMetallicMode);
                    }
                    const float body = nesSteppedTriangle(osc2Phase);
                    const float noise = ((lfsrState & 1u) != 0u) ? 0.85f : -0.85f;
                    const float noiseMix = (noteAgeSamples < 180u) ? 0.7f : drumSustainedNoiseMix;
                    sig = (1.0f - noiseMix) * body + noiseMix * noise;
                    break;
                }
            }

            subPhase += dt * 0.5f;
            if (subPhase >= 1.0f)
            {
                subPhase -= 1.0f;
            }
            if (hasSubMix)
            {
                sig = sig * subMainGain + nesSteppedTriangle(subPhase) * subOscGain;
            }

            if (++decimateCounter >= decimateFactor)
            {
                decimateCounter = 0;
                if (crushActive)
                {
                    heldSample = std::round(sig * crushScale) * invCrushScale;
                }
                else
                {
                    heldSample = sig;
                }
            }
            float postCrush = heldSample;

            if (hasFilterEnv && envLevel != cachedFilterEnvLevel)
            {
                cachedFilterEnvLevel = envLevel;
                cachedEnvCutoff = std::clamp(
                  filterCutoffHz * fastPow2(filterEnvScale * envLevel), 40.0f, 18000.0f);
                cachedFilterActive = (cachedEnvCutoff < 17500.0f || filterResonance > 0.02f);
                if (cachedFilterActive)
                {
                    cachedSvfF = std::min(0.85f, 2.0f * fastSine(0.5f * cachedEnvCutoff * invSr));
                }
            }
            if (cachedFilterActive)
            {
                svfLow += cachedSvfF * svfBand;
                const float high = postCrush - svfLow - qDamper * svfBand;
                svfBand += cachedSvfF * high;
                svfBand = std::clamp(svfBand, -2.0f, 2.0f);
                svfLow = std::clamp(svfLow, -2.0f, 2.0f);
                postCrush = svfLow;
            }

            const float sampleOut = postCrush * envLevel * volumeMult;
            out[i] = sampleOut;
            lastGraphSamples[i] = sampleOut;
            ++noteAgeSamples;
        }
    }

    void renderVoiceAudio(const InstrumentVoiceContext& ctx,
                          [[maybe_unused]] std::optional<AudioBufferStereoRef> inputBuffer,
                          AudioBufferStereoRef outputBuffer,
                          std::span<AudioBufferRef> /*auxOutBuffer*/)
    {
        renderVoiceAudio(ctx, outputBuffer);
    }

    void renderGraph(std::span<float> points) override
    {
        for (size_t i = 0; i < points.size(); ++i)
        {
            const size_t srcIdx = (i * lastGraphSamples.size()) / std::max<size_t>(1, points.size());
            points[i] = std::clamp(lastGraphSamples[srcIdx], -1.0f, 1.0f);
        }
    }

    static void createMetadata(const Patch& /*patch*/, Patch::Metadata& metadata)
    {
        using namespace polysynth::sdk;

        metadata.params.resize(enumutil::EnumMax<ChipParamId>::value + 1);
        metadata.params.shrink_to_fit();

        if (paramHandlers.empty())
        {
            paramHandlers.resize(enumutil::EnumMax<ChipParamId>::value + 1);
        }

        ParamHelper<ChiptuneSynthVoice> helper(metadata, paramHandlers);

        helper.addParam(
          *ChipParamId::kModel,
          {
            .displayName = "Model",
            .defaultValue = 0,
            .minValue = 0,
            .maxValue = static_cast<Patch::paramvalue_short_t>(kModelNames.size() - 1),
            .isEnum = true,
            .midiCC = *MidiCCs::kOscParam0,
            .valueFormatter =
              [](const Patch& /*patch*/, Patch::paramvalue_t value)
            {
                const int idx =
                  std::clamp<int>(static_cast<int>(std::lround(value)), 0, kModelNames.size() - 1);
                return std::string(kModelNames[idx]);
            },
          },
          [](auto& voice, Patch::optional_paramvalue_t val)
          {
              voice.model = static_cast<ChipModel>(
                std::clamp<int>(static_cast<int>(std::lround(*val)), 0, kModelNames.size() - 1));
          });

        helper.addParam(
          *ChipParamId::kTune,
          {
            .displayName = "Tune",
            .defaultValue = 0.0f,
            .minValue = -24,
            .maxValue = 24,
            .midiCC = *MidiCCs::kOscParam0 + 1,
            .valueFormatter = createSemitoneFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val)
          { voice.tuneSemitones = std::round(*val); });

        helper.addParam(
          *ChipParamId::kFine,
          {
            .displayName = "Fine",
            .defaultValue = 0.0f,
            .minValue = -100,
            .maxValue = 100,
            .midiCC = *MidiCCs::kOscParam0 + 2,
            .valueFormatter = createCentsFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.fineCents = *val; });

        helper.addParam(
          *ChipParamId::kShape,
          {
            .displayName = "Duty / Shape",
            .defaultValue = 0.35f,
            .minValue = 0,
            .maxValue = 1,
            .midiCC = *MidiCCs::kOscParam0 + 3,
            .valueFormatter = createPercentFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.shape = *val; });

        helper.addParam(
          *ChipParamId::kPwmRate,
          {
            .displayName = "PWM Rate",
            .defaultValue = 2.5f,
            .minValue = 0.1f,
            .maxValue = 40.0f,
            .editCurve = Patch::Metadata::Param::Curve::kExponential,
            .midiCC = *MidiCCs::kOscParam0 + 4,
            .valueFormatter = createFrequencyFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.pwmRateHz = *val; });

        helper.addParam(
          *ChipParamId::kPwmDepth,
          {
            .displayName = "PWM Depth",
            .defaultValue = 0.2f,
            .minValue = 0,
            .maxValue = 1,
            .midiCC = *MidiCCs::kOscParam0 + 5,
            .valueFormatter = createPercentFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.pwmDepth = *val; });

        helper.addParam(
          *ChipParamId::kSubMix,
          {
            .displayName = "Sub Osc",
            .defaultValue = 0.25f,
            .minValue = 0,
            .maxValue = 1,
            .midiCC = *MidiCCs::kOscParam0 + 6,
            .valueFormatter = createPercentFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.subMix = *val; });

        helper.addParam(
          *ChipParamId::kArpChord,
          {
            .displayName = "Chord Arp",
            .defaultValue = 0,
            .minValue = 0,
            .maxValue = static_cast<Patch::paramvalue_short_t>(kArpNames.size() - 1),
            .isEnum = true,
            .midiCC = *MidiCCs::kOscParam0 + 7,
            .valueFormatter =
              [](const Patch& /*patch*/, Patch::paramvalue_t value)
            {
                const int idx =
                  std::clamp<int>(static_cast<int>(std::lround(value)), 0, kArpNames.size() - 1);
                return std::string(kArpNames[idx]);
            },
          },
          [](auto& voice, Patch::optional_paramvalue_t val)
          {
              voice.arpChordIdx =
                std::clamp<int>(static_cast<int>(std::lround(*val)), 0, kArpNames.size() - 1);
          });

        helper.addParam(
          *ChipParamId::kArpSpeed,
          {
            .displayName = "Arp Speed",
            .defaultValue = 50.0f,
            .minValue = 5.0f,
            .maxValue = 75.0f,
            .midiCC = *MidiCCs::kOscParam0 + 8,
            .valueFormatter = createFrequencyFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.arpSpeedHz = *val; });

        helper.addParam(
          *ChipParamId::kPitchSweep,
          {
            .displayName = "Pitch Sweep",
            .defaultValue = 0.0f,
            .minValue = -36.0f,
            .maxValue = 36.0f,
            .midiCC = *MidiCCs::kOscParam0 + 9,
            .valueFormatter = createSemitoneFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val)
          { voice.pitchSweepSemitones = *val; });

        helper.addParam(
          *ChipParamId::kSweepTime,
          {
            .displayName = "Sweep Time",
            .defaultValue = 0.25f,
            .minValue = 0.02f,
            .maxValue = 2.0f,
            .editCurve = Patch::Metadata::Param::Curve::kExponential,
            .midiCC = *MidiCCs::kOscParam0 + 10,
            .valueFormatter = createTimeFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.sweepTimeSec = *val; });

        helper.addParam(
          *ChipParamId::kVibRate,
          {
            .displayName = "Vibrato Rate",
            .defaultValue = 6.0f,
            .minValue = 0.5f,
            .maxValue = 25.0f,
            .editCurve = Patch::Metadata::Param::Curve::kExponential,
            .midiCC = *MidiCCs::kLfo1Frequency,
            .valueFormatter = createFrequencyFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.vibRateHz = *val; });

        helper.addParam(
          *ChipParamId::kVibDepth,
          {
            .displayName = "Vibrato Depth",
            .defaultValue = 0.0f,
            .minValue = 0.0f,
            .maxValue = 1.0f,
            .midiCC = *MidiCCs::kOscParam0 + 11,
            .valueFormatter = createPercentFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.vibDepth = *val; });

        helper.addParam(
          *ChipParamId::kAmpAttack,
          {
            .displayName = "Attack",
            .defaultValue = 0.0f,
            .minValue = 0.0f,
            .maxValue = 5.0f,
            .editCurve = Patch::Metadata::Param::Curve::kExponential,
            .midiCC = *MidiCCs::kAmpEnvAttack,
            .valueFormatter = createTimeFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.ampAttackSec = *val; });

        helper.addParam(
          *ChipParamId::kAmpDecay,
          {
            .displayName = "Decay",
            .defaultValue = 0.35f,
            .minValue = 0.01f,
            .maxValue = 5.0f,
            .editCurve = Patch::Metadata::Param::Curve::kExponential,
            .midiCC = *MidiCCs::kAmpEnvDecay,
            .valueFormatter = createTimeFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.ampDecaySec = *val; });

        helper.addParam(
          *ChipParamId::kAmpSustain,
          {
            .displayName = "Sustain",
            .defaultValue = 0.4f,
            .minValue = 0.0f,
            .maxValue = 1.0f,
            .midiCC = *MidiCCs::kAmpEnvSustain,
            .valueFormatter = createPercentFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.ampSustainLevel = *val; });

        helper.addParam(
          *ChipParamId::kAmpRelease,
          {
            .displayName = "Release",
            .defaultValue = 0.15f,
            .minValue = 0.01f,
            .maxValue = 5.0f,
            .editCurve = Patch::Metadata::Param::Curve::kExponential,
            .midiCC = *MidiCCs::kAmpEnvRelease,
            .valueFormatter = createTimeFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.ampReleaseSec = *val; });

        helper.addParam(
          *ChipParamId::kFilterCutoff,
          {
            .displayName = "Cutoff",
            .defaultValue = 12000.0f,
            .minValue = 80.0f,
            .maxValue = 18000.0f,
            .editCurve = Patch::Metadata::Param::Curve::kExponentialSteep,
            .midiCC = *MidiCCs::kFilterCutoff,
            .valueFormatter = createFrequencyFormatter(18000.0f),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.filterCutoffHz = *val; });

        helper.addParam(
          *ChipParamId::kFilterResonance,
          {
            .displayName = "Resonance",
            .defaultValue = 0.15f,
            .minValue = 0.0f,
            .maxValue = 1.0f,
            .midiCC = *MidiCCs::kFilterResonance,
            .valueFormatter = createPercentFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.filterResonance = *val; });

        helper.addParam(
          *ChipParamId::kFilterEnvAmount,
          {
            .displayName = "Env Amt",
            .defaultValue = 0.2f,
            .minValue = -1.0f,
            .maxValue = 1.0f,
            .midiCC = *MidiCCs::kFilterEnvAmount,
            .valueFormatter = createPercentFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.filterEnvAmount = *val; });

        helper.addParam(
          *ChipParamId::kBitcrush,
          {
            .displayName = "Bitcrush",
            .defaultValue = 8.0f,
            .minValue = 2.0f,
            .maxValue = 16.0f,
            .valueFormatter =
              [](const Patch& /*patch*/, Patch::paramvalue_t value)
            { return fmt::format("{:.0f} bit", value); },
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.bitcrushBits = *val; });

        helper.addParam(
          *ChipParamId::kDecimate,
          {
            .displayName = "Decimate",
            .defaultValue = 1.0f,
            .minValue = 1.0f,
            .maxValue = 32.0f,
            .valueFormatter =
              [](const Patch& /*patch*/, Patch::paramvalue_t value)
            { return fmt::format("{:.0f}x", std::round(value)); },
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.decimateRate = *val; });

        helper.addParam(
          *ChipParamId::kPlayMode,
          {
            .displayName = "Glide Mode",
            .defaultValue = 0,
            .minValue = 0,
            .maxValue = 2,
            .isEnum = true,
            .valueFormatter =
              [](const Patch& /*patch*/, Patch::paramvalue_t value)
            {
                constexpr std::array kModes = { "Always", "Overlap", "Legato" };
                return std::string(kModes.at(std::clamp<int>(std::lround(value), 0, 2)));
            },
          },
          [](auto& voice, Patch::optional_paramvalue_t val)
          { voice.playMode = static_cast<PlayMode>(std::clamp<int>(std::lround(*val), 0, 2)); });

        helper.addParam(
          *ChipParamId::kGlideTime,
          {
            .displayName = "Glide Time",
            .defaultValue = 0.0f,
            .minValue = 0.0f,
            .maxValue = 2.0f,
            .editCurve = Patch::Metadata::Param::Curve::kExponential,
            .valueFormatter = createTimeFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.glideTimeSec = *val; });

        helper.addParam(
          *ChipParamId::kVolume,
          {
            .displayName = "Volume",
            .defaultValue = 1.0f,
            .minValue = 0.0f,
            .maxValue = 2.0f,
            .macroOperation = Patch::Metadata::Param::MacroOperation::kMult,
            .valueFormatter = createPercentFormatter(),
          },
          [](auto& voice, Patch::optional_paramvalue_t val) { voice.volumeMult = *val; });

        metadata.layout33 = Patch::Metadata::EditorLayout{
            .sections = {
              {
                .name = "Oscillator",
                .shortName = "Osc",
                .subsections = {
                  {
                    .elements = {
                      { .type = EditorElement::Type::kParam, .id = *ChipParamId::kModel },
                      { .type = EditorElement::Type::kParam, .id = *ChipParamId::kShape },
                      { .type = EditorElement::Type::kParam, .id = *ChipParamId::kSubMix },
                      { .type = EditorElement::Type::kParam, .id = *ChipParamId::kTune },
                      { .type = EditorElement::Type::kParam, .id = *ChipParamId::kFine },
                      { .type = EditorElement::Type::kEmpty },
                      { .type = EditorElement::Type::kParam, .id = *ChipParamId::kPwmRate },
                      { .type = EditorElement::Type::kParam, .id = *ChipParamId::kPwmDepth },
                    },
                  },
                  {
                    .elements = {
                      { .type = EditorElement::Type::kParam, .id = *ChipParamId::kPlayMode },
                      { .type = EditorElement::Type::kParam, .id = *ChipParamId::kGlideTime },
                      { .type = EditorElement::Type::kParam, .id = *ChipParamId::kVolume },
                    },
                  },
                },
              },
              {
                .name = "Filter",
                .shortName = "Filter",
                .subsections = {
                  {
                    .elements = {
                      { .type = EditorElement::Type::kParam, .id = *ChipParamId::kFilterCutoff },
                      { .type = EditorElement::Type::kParam, .id = *ChipParamId::kFilterResonance },
                      { .type = EditorElement::Type::kParam, .id = *ChipParamId::kFilterEnvAmount },
                      { .type = EditorElement::Type::kParam, .id = *ChipParamId::kBitcrush },
                      { .type = EditorElement::Type::kParam, .id = *ChipParamId::kDecimate },
                    },
                  },
                },
              },
              {
                .name = "Env",
                .shortName = "Env",
                .subsections = {
                  {
                    .name = "Amp",
                    .elements = {
                      { .type = EditorElement::Type::kParam, .id = *ChipParamId::kAmpAttack },
                      { .type = EditorElement::Type::kParam, .id = *ChipParamId::kAmpDecay },
                      { .type = EditorElement::Type::kParam, .id = *ChipParamId::kAmpSustain },
                      { .type = EditorElement::Type::kParam, .id = *ChipParamId::kAmpRelease },
                    },
                  },
                  {
                    .name = "Sweep",
                    .elements = {
                      { .type = EditorElement::Type::kParam, .id = *ChipParamId::kPitchSweep },
                      { .type = EditorElement::Type::kParam, .id = *ChipParamId::kSweepTime },
                    },
                  },
                },
              },
              {
                .name = "LFO",
                .shortName = "LFO",
                .subsections = {
                  {
                    .elements = {
                      { .type = EditorElement::Type::kParam, .id = *ChipParamId::kVibRate },
                      { .type = EditorElement::Type::kParam, .id = *ChipParamId::kVibDepth },
                      { .type = EditorElement::Type::kEmpty },
                      { .type = EditorElement::Type::kParam, .id = *ChipParamId::kPwmRate },
                      { .type = EditorElement::Type::kParam, .id = *ChipParamId::kPwmDepth },
                    },
                  },
                },
              },
              {
                .name = "Mod",
                .shortName = "Mod",
                .subsections = {
                  {
                    .elements = {
                      { .type = EditorElement::Type::kParam, .id = *ChipParamId::kArpChord },
                      { .type = EditorElement::Type::kParam, .id = *ChipParamId::kArpSpeed },
                      { .type = EditorElement::Type::kEmpty },
                      { .type = EditorElement::Type::kParam, .id = *ChipParamId::kPitchSweep },
                      { .type = EditorElement::Type::kParam, .id = *ChipParamId::kSweepTime },
                    },
                  },
                },
              },
            },
        };

        metadata.layoutFlat = Patch::Metadata::EditorLayout{
            .sections = {
              {
                .name = "Oscillator",
                .shortName = "Osc",
                .elements = {
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kModel },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kTune },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kFine },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kShape },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kPwmRate },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kPwmDepth },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kSubMix },
                },
              },
              {
                .name = "Modulation",
                .shortName = "Mod",
                .elements = {
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kArpChord },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kArpSpeed },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kPitchSweep },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kSweepTime },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kVibRate },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kVibDepth },
                },
              },
              {
                .name = "Amplifier",
                .shortName = "Amp",
                .elements = {
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kAmpAttack },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kAmpDecay },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kAmpSustain },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kAmpRelease },
                },
              },
              {
                .name = "Filter",
                .shortName = "Filter",
                .elements = {
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kFilterCutoff },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kFilterResonance },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kFilterEnvAmount },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kBitcrush },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kDecimate },
                },
              },
              {
                .name = "Voice",
                .shortName = "Voice",
                .elements = {
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kPlayMode },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kGlideTime },
                  { .type = EditorElement::Type::kParam, .id = *ChipParamId::kVolume },
                },
              },
            },
        };

        metadata.layout23 = metadata.layoutFlat;

        metadata.initDefaultMacros = [](auto& macros)
        {
            macros[0] = Patch::Metadata::Macro{
                .displayName = "Timbre / PWM",
                .modifiers = {
                  { *ChipParamId::kShape, 0.7f },
                  { *ChipParamId::kPwmDepth, 0.5f },
                },
                .bipolar = true,
            };
            macros[1] = Patch::Metadata::Macro{
                .displayName = "Arp / Chord",
                .modifiers = {
                  { *ChipParamId::kArpChord, 1.0f },
                },
                .bipolar = false,
            };
            macros[2] = Patch::Metadata::Macro{
                .displayName = "Sweep / Vibrato",
                .modifiers = {
                  { *ChipParamId::kVibDepth, 0.6f },
                  { *ChipParamId::kPitchSweep, 0.5f },
                },
                .bipolar = true,
            };
            macros[3] = Patch::Metadata::Macro{
                .displayName = "Env Decay / Rel",
                .modifiers = {
                  { *ChipParamId::kAmpDecay, 0.8f },
                  { *ChipParamId::kAmpRelease, 0.6f },
                },
                .bipolar = true,
            };
            macros[4] = Patch::Metadata::Macro{
                .displayName = "Crush / Lo-Fi",
                .modifiers = {
                  { *ChipParamId::kBitcrush, -0.6f },
                  { *ChipParamId::kDecimate, 0.4f },
                },
                .bipolar = false,
            };
            macros[5] = Patch::Metadata::Macro{
                .displayName = "Filter Cutoff",
                .modifiers = {
                  { *ChipParamId::kFilterCutoff, 1.0f },
                  { *ChipParamId::kFilterResonance, 0.35f },
                },
                .bipolar = true,
            };

            macros[*Patch::SystemMacroId::kVelocity].modifiers = {
                { *ChipParamId::kVolume, 1.0f },
            };
        };

        metadata.instrumentTypeId = static_cast<int>(InstrumentTypeId::kChiptuneSynth);
        metadata.outputChannels = Patch::Metadata::OutputChannels::kMono;
    }

  private:
    enum class EnvStage : uint8_t
    {
        kIdle = 0,
        kAttack,
        kDecay,
        kSustain,
        kRelease,
    };

    ChipModel model = ChipModel::kPulse2A03;
    float tuneSemitones = 0.0f;
    float fineCents = 0.0f;
    float shape = 0.35f;
    float pwmRateHz = 2.5f;
    float pwmDepth = 0.2f;
    float subMix = 0.25f;

    int arpChordIdx = 0;
    float arpSpeedHz = 50.0f;
    float pitchSweepSemitones = 0.0f;
    float sweepTimeSec = 0.25f;
    float vibRateHz = 6.0f;
    float vibDepth = 0.0f;

    float ampAttackSec = 0.0f;
    float ampDecaySec = 0.35f;
    float ampSustainLevel = 0.4f;
    float ampReleaseSec = 0.15f;

    float filterCutoffHz = 12000.0f;
    float filterResonance = 0.15f;
    float filterEnvAmount = 0.2f;
    float bitcrushBits = 8.0f;
    float decimateRate = 1.0f;

    float glideTimeSec = 0.0f;
    float volumeMult = 1.0f;

    EnvStage envStage = EnvStage::kIdle;
    float envLevel = 0.0f;
    bool hasPlayedAnyNote = false;
    float currentGlideNote = 60.0f;
    float targetGlideNote = 60.0f;

    float phase = 0.0f;
    float osc2Phase = 0.0f;
    float osc3Phase = 0.0f;
    float subPhase = 0.0f;
    float pwmPhase = 0.0f;
    float vibPhase = 0.0f;
    float lfsrPhase = 0.0f;
    uint16_t lfsrState = 0x7FFFu;

    uint32_t noteAgeSamples = 0;
    uint32_t arpSampleCounter = 0;
    uint32_t arpStepCounter = 0;

    int decimateCounter = 0;
    float heldSample = 0.0f;
    float svfLow = 0.0f;
    float svfBand = 0.0f;
    std::array<float, kAudioBufferSize> lastGraphSamples{};
};

} // namespace polysynth

PSYN_REGISTER_CPP_VOICE(polysynth::ChiptuneSynthVoice)
