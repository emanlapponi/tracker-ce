#pragma once

#include "PolyDSP/synths/SynthBase.h"
#include "PolySynth/Patch.h"
#include "config.h"
#include <fmt/format.h>

#include "PolyDSP/modulators/LFO.h"

#include "PolyUtil/math.h"

#include "enum_util.h"

namespace polysynth
{
using EditorSection = Patch::Metadata::EditorLayout::EditorSection;
using EditorSubsection = Patch::Metadata::EditorLayout::EditorSubsection;
using EditorElement = Patch::Metadata::EditorLayout::EditorElement;
using Param = Patch::Metadata::Param;

enum class MidiCCs
{
    kOscParam0 = 20, // 12 slots
    kBankSelectLSB = 32,
    kUserEnv1Attack = 46,
    kUserEnv1Decay,
    kUserEnv1Sustain,
    kUserEnv1Release,
    kUserEnv2Attack,
    kUserEnv2Decay,
    kUserEnv2Sustain,
    kUserEnv2Release,
    kLfo1Frequency,
    kLfo2Frequency,
    kUndefined56 = 56, // 8 slots
    kSustainPedalEnable = 64,
    kPortamentoEnable,
    kSostenutoEnable,
    kSoftPedalEnable,
    kLegatoEnable,
    kHoldPedal2,
    kFilterType = 70,
    kFilterResonance = 71,
    kAmpEnvDecay = 72,
    kAmpEnvRelease = 73,
    kFilterCutoff = 74,
    kAmpEnvAttack = 75,
    kAmpEnvSustain = 76,
    kFilterEnvAmount = 77,
    kFilterLfoAmount = 78,
    kFilterEnvVelocity = 79,
    kFilterEnvAttack = 80,
    kFilterEnvDecay,
    kFilterEnvSustain,
    kFilterEnvRelease,
    kVelocityVolume = 84,
    kUndefined85 = 85, // 5 slots
    kDelaySend = 90,
    kReverbSend,
    kTremolo,
    kChorus,
    kDetune,
    kPhaser,
    KMacroStart = 102,
    kUndefined108 = KMacroStart + kUserMacroCount,
    kMute = 120,
    kResetAllControllers = 121,
    kLocalEnable = 122,
    kAllNotesOff = 123,
};

// clang-format off
static constexpr std::array kTimeBasePulseCount = {
    96, // 1 note
    72, // 1/2 note (dotted)
    48, // 1/2 note
    36, // 1/4D note (dotted)
    32, // 1 note (triplet)
    24, // 1/4 note
    18, // 1/8D note (dotted)
    16, // 1/2T note (triplet)
    12, // 1/8 note
    9,  // 1/16D note (dotted)
    8,  // 1/4T note (triplet)
    6,  // 1/16 note
    4,  // 1/8T note (triplet)
    3,  // 1/32 note
    2,  // 1/16T note (triplet)
    1,  // 1/32T note (triplet)
};

static constexpr std::array kTimeBaseNames = {
    "1",
    "1/2 D",
    "1/2",
    "1/4 D",
    "1 T",
    "1/4",
    "1/8 D",
    "1/2 T",
    "1/8",
    "1/16 D",
    "1/4 T",
    "1/16",
    "1/8 T",
    "1/32",
    "1/16 T",
    "1/32 T",
};
// clang-format on

static_assert(std::ssize(kTimeBasePulseCount) == std::ssize(kTimeBaseNames));

/**
 * @brief Returns the closest pulse count to the given pulseCount
 * @return
 */
static constexpr int closestDefinedPulseCountIdx(int pulseCount)
{
    auto size = static_cast<int>(kTimeBasePulseCount.size());
    auto it =
      std::lower_bound(kTimeBasePulseCount.rbegin(), kTimeBasePulseCount.rend(), pulseCount);
    if (it == kTimeBasePulseCount.rend())
    {
        return (size - 1 - static_cast<int>(it - kTimeBasePulseCount.rbegin() - 1));
    }
    if (it == kTimeBasePulseCount.rbegin())
    {
        return (size - 1 - static_cast<int>(it - kTimeBasePulseCount.rbegin()));
    }
    auto lower = (it - 1);
    const auto& upper = it;

    auto closestIdx =
      static_cast<int>((std::abs(pulseCount - *lower) < std::abs(pulseCount - *upper))
                         ? (size - (lower - kTimeBasePulseCount.rbegin()) - 1)
                         : (size - (upper - kTimeBasePulseCount.rbegin()) - 1));
    return std::clamp(closestIdx, 0, size - 1);
}

static_assert(*MidiCCs::KMacroStart == Patch::Metadata::Macro::kMacroCCStart);

template<class VoiceType>
class ParamHelper
{
  public:
    ParamHelper(
      Patch::Metadata& metadata,
      std::span<std::function<void(VoiceType& voice, Patch::optional_paramvalue_t newValue)>>
        paramHandlers)
      : metadata(metadata)
      , paramHandlers(paramHandlers){};

    void addParam(
      int paramId,
      Patch::Metadata::Param paramDef,
      std::function<void(VoiceType& voice, Patch::optional_paramvalue_t newValue)> paramHandler)
    {
        assert(paramId < static_cast<int>(paramHandlers.size()));
        metadata.params.at(paramId) = std::move(paramDef);
        if (!paramHandlers[paramId])
        {
            paramHandlers[paramId] = std::move(paramHandler);
        }
    }

  private:
    Patch::Metadata& metadata;
    std::span<std::function<void(VoiceType& voice, Patch::optional_paramvalue_t newValue)>>
      paramHandlers;
};

template<class SynthClassParam, class VoiceClassParam, class ParamId>
class BaseSynthVoice : public InstrumentVoice
{
  public:
    template<typename... Args>
    explicit BaseSynthVoice(Args&&... args)
      : synth(std::forward<Args>(args)...){};

    void onMidiEvent(const InstrumentVoiceContext& ctx, const MidiEvent& event) final
    {
        switch (event.type)
        {
            case MidiEvent::Type::kStart:
            {
                synth.start();
                break;
            }
            case MidiEvent::Type::kClock:
            {
                synth.clockPulse();
                break;
            }
            case MidiEvent::Type::kNoteOn:
            {
                polydsp::RenderContext renderContext{ .samplingRate = kAudioSampleRate,
                                                      .tempo = ctx.tempo };
                synth.noteOn(renderContext,
                             event.data.note.note,
                             scaleLin(static_cast<polydsp::param_t>(event.data.note.velocity),
                                      0,
                                      MidiEvent::Data::Note::kMaxVelocity,
                                      0,
                                      1.0f));

                break;
            }
            case MidiEvent::Type::kNoteOff:
            {
                polydsp::RenderContext renderContext{ .samplingRate = kAudioSampleRate,
                                                      .tempo = ctx.tempo };
                synth.noteOff(renderContext, event.data.note.note);
                break;
            }
            case MidiEvent::Type::kPitchBendChange:
            {
                synth.pitchBendChange(
                  static_cast<float>(event.data.pitchBendChange.value * kPitchBendRangeSemitones) /
                  MidiEvent::Data::PitchBendChange::kMaxValue);
                break;
            }
            default:
                break;
        }
    };

    void onParamChange(int paramId, Patch::optional_paramvalue_t newValue) final
    {
        if (paramHandlers[paramId])
        {
            paramHandlers[paramId](*static_cast<VoiceClassParam*>(this), newValue);
        };
    };

    bool isActive() final { return synth.ampEnv.isActive(); };

    bool handlesNote(note_t note) final { return synth.handlesNote(note); };
    bool handlesAnyNote() final { return synth.handlesAnyNote(); };
    tiny_optional<note_t> getLastNote() final { return synth.getLastNote(); };
    tiny_optional<float> getCurrentNoteVelocity() final { return synth.getCurrentNoteVelocity(); }
    void clearPreviousNotes() final { synth.clearPreviousNotes(); }

    void renderVoiceAudio(const InstrumentVoiceContext& ctx,
                          [[maybe_unused]] std::optional<AudioBufferStereoRef> inputBuffer,
                          AudioBufferStereoRef buffer,
                          std::span<AudioBufferRef> /* auxOutBuffer */) override
    {
        assert(!inputBuffer);

        polydsp::RenderContext renderContext{ .samplingRate = kAudioSampleRate,
                                              .tempo = ctx.tempo };
        synth.renderAudioBuffer(renderContext, buffer[0]);
    };

    void renderPostProcessAudio(const InstrumentVoiceContext& ctx,
                                AudioBufferStereoRef buffer) final
    {
        polydsp::RenderContext renderContext{ .samplingRate = kAudioSampleRate,
                                              .tempo = ctx.tempo };
        synth.renderFx(renderContext, buffer[0]);
    };

    bool hasPostProcess() const final { return true; }

  protected:
    static constexpr int kPitchBendRangeSemitones = 2;

    // NOLINTNEXTLINE(*-non-private-member-variables-in-classes)
    SynthClassParam synth;

    using SynthClass = SynthClassParam;

    static inline boost::container::static_vector<
      std::function<void(VoiceClassParam& voice, Patch::optional_paramvalue_t newValue)>,
      enumutil::EnumMax<ParamId>::value + 1>
      paramHandlers;
};

template<class VoiceClassParam, class ParamId>
class BaseFxVoice : public InstrumentVoice
{
  public:
    bool isActive() final { return true; };
    bool handlesNote(note_t /* note */) final { return false; };
    bool handlesAnyNote() final { return false; };
    tiny_optional<note_t> getLastNote() final { return std::nullopt; };
    tiny_optional<float> getCurrentNoteVelocity() final { return std::nullopt; };

  protected:
    static inline boost::container::static_vector<
      std::function<void(VoiceClassParam& voice, Patch::optional_paramvalue_t newValue)>,
      enumutil::EnumMax<ParamId>::value + 1>
      paramHandlers;
};

inline auto createPercentFormatter(bool showPercentSign = true)
{
    return [showPercentSign](const Patch& /* patch */, polysynth::Patch::paramvalue_t value)
    { return fmt::format(showPercentSign ? "{:.0f} %" : "{:.0f}", value * 100); };
}

inline auto createDetailPercentFormatter(bool showPercentSign = true)
{
    return [showPercentSign](const Patch& /* patch */, polysynth::Patch::paramvalue_t value)
    {
        return fmt::format(value < 0.1f ? (showPercentSign ? "{:.1f} %" : "{:.1f}")
                                        : (showPercentSign ? "{:.0f} %" : "{:.0f}"),
                           value * 100);
    };
}

inline auto createFormatter(std::string_view format, float mult = 1.0f)
{
    return [format, mult](const Patch& /* patch */, polysynth::Patch::paramvalue_t value)
    { return fmt::format(format, value * mult); };
}

inline auto createOnOffFormatter()
{
    return [](const Patch& /* patch */, polysynth::Patch::paramvalue_t value)
    { return (value > 0) ? "On" : "Off"; };
}

inline auto createTimeFormatter()
{
    return [](const Patch& /* patch */, polysynth::Patch::paramvalue_t value)
    { return (value < 1) ? fmt::format("{:.2f} s", value) : fmt::format("{:.1f} s", value); };
}

inline auto createFrequencyFormatter(
  polysynth::Patch::paramvalue_t max = std::numeric_limits<polysynth::Patch::paramvalue_t>::max())
{
    return [max](const Patch& /* patch */, polysynth::Patch::paramvalue_t value)
    {
        if (value >= max)
        {
            return std::string("Off");
        }
        return (value < 10)
                 ? fmt::format("{:.2f} Hz", value)
                 : ((value < 100)
                      ? fmt::format("{:.1f} Hz", value)
                      : ((value < 1000)
                           ? fmt::format("{:.0f} Hz", value)
                           : ((value < 10000) ? fmt::format("{:.2f} kHz", value / 1000)
                                              : fmt::format("{:.1f} kHz", value / 1000))));
    };
}

template<class SynthClass>
inline Patch::Metadata::Param createFilterFreqParam(float defaultValue = 400)
{
    return Patch::Metadata::Param{
        .displayName = "Cutoff",
        .defaultValue = defaultValue,
        .minValue = 20,
        .maxValue = static_cast<int>(SynthClass::kMaxFilterCutoff),
        .editCurve = Patch::Metadata::Param::Curve::kExponentialSteep,
        .midiCC = *MidiCCs::kFilterCutoff,
        .valueFormatter = createFrequencyFormatter(SynthClass::kMaxFilterCutoff),
    };
}

inline Patch::Metadata::Param createFilterResonanceParam(float defaultValue = 0.5f)
{
    return {
        .displayName = "Resonance",
        .defaultValue = defaultValue,
        .minValue = 0,
        .maxValue = 1.5f,
        .midiCC = *MidiCCs::kFilterResonance,
        .valueFormatter = createPercentFormatter(),
    };
}

inline Patch::Metadata::Param createFilterEnvAmountParam(float defaultValue = 0.14f)
{
    return {
        .displayName = "Env Amt",
        .defaultValue = defaultValue,
        .minValue = -1,
        .maxValue = 1,
        .editCurve = Patch::Metadata::Param::Curve::kExponential,
        .midiCC = *MidiCCs::kFilterEnvAmount,
        .valueFormatter =
          [](const Patch& /* patch */, polysynth::Patch::paramvalue_t value)
        {
            return (std::abs(value) < 0.1f) ? fmt::format("{:.1f} %", value * 100)
                                            : fmt::format("{:.0f} %", value * 100);
        },
    };
}

inline Patch::Metadata::Param createFilterLfoAmountParam(float defaultValue = 0)
{
    return {
        .displayName = "LFO Amt",
        .defaultValue = defaultValue,
        .minValue = -1,
        .maxValue = 1,
        .editCurve = Patch::Metadata::Param::Curve::kExponential,
        .midiCC = *MidiCCs::kFilterLfoAmount,
        .valueFormatter =
          [](const Patch& /* patch */, polysynth::Patch::paramvalue_t value)
        {
            return (std::abs(value) < 0.1f) ? fmt::format("{:.1f} %", value * 100)
                                            : fmt::format("{:.0f} %", value * 100);
        },
    };
}

inline Patch::Metadata::Param createEnvTimeParam(std::string name, float defaultValue, int ccId)
{
    return {
        .displayName = std::move(name),
        .defaultValue = defaultValue,
        .minValue = 0,
        .maxValue = 10,
        .editCurve = Patch::Metadata::Param::Curve::kExponential,
        .midiCC = ccId,
        .valueFormatter = createTimeFormatter(),
    };
}

inline Patch::Metadata::Param createLfoWaveformParam()
{
    return Patch::Metadata::Param{
        .displayName = "Waveform",
        .defaultValue = static_cast<int>(polydsp::LFO::Waveform::kSin),
        .minValue = enumutil::EnumMin<polydsp::LFO::Waveform>::value,
        .maxValue = enumutil::EnumMax<polydsp::LFO::Waveform>::value,
        .isEnum = true,
        .valueFormatter =
          [](const Patch& /* patch */, polysynth::Patch::paramvalue_t value)
        {
            constexpr std::array kNames = { "Triangle",  "Sin",    "Ramp Up",
                                            "Ramp Down", "Square", "Random S&H" };
            return kNames.at(static_cast<int>(value));
        },
    };
}

inline Patch::Metadata::Param createLfoRetriggerParam()
{
    return Patch::Metadata::Param{
        .displayName = "Retrigger",
        .defaultValue = static_cast<int>(polydsp::LFO::Mode::kFreeRun),
        .minValue = enumutil::EnumMin<polydsp::LFO::Mode>::value,
        .maxValue = enumutil::EnumMax<polydsp::LFO::Mode>::value,
        .isEnum = true,
        .valueFormatter =
          [](const Patch& /* patch */, polysynth::Patch::paramvalue_t value)
        {
            constexpr std::array kNames = { "Note", "One Shot", "Free Running" };
            return kNames.at(static_cast<int>(value));
        },
    };
}

inline Patch::Metadata::Param createPlayModeParam()
{
    return Patch::Metadata::Param{
        .displayName = "Glide Mode",
        .defaultValue = *polydsp::SynthBase::PlayMode::kNormal,
        .minValue = static_cast<polysynth::Patch::paramvalue_t>(
          enumutil::EnumMin<polydsp::SynthBase::PlayMode>::value),
        .maxValue = static_cast<polysynth::Patch::paramvalue_t>(
          enumutil::EnumMax<polydsp::SynthBase::PlayMode>::value),
        .isEnum = true,
        .valueFormatter =
          [](const Patch& /* patch */, polysynth::Patch::paramvalue_t value)
        {
            constexpr std::array kNames = { "Always", "Overlap", "Legato", "Legato O." };
            return kNames.at(std::lround(value));
        },
    };
}

inline Patch::Metadata::Param createPortamentoTimeParam()
{
    return Patch::Metadata::Param{
        .displayName = "Glide Time",
        .defaultValue = 0,
        .minValue = 0,
        .maxValue = 3,
        .editCurve = Patch::Metadata::Param::Curve::kExponential,
        .valueFormatter = createTimeFormatter(),
    };
}

inline Patch::Metadata::Param createPatchVolumeParam(float defaultValue = 1.0f)
{
    return Patch::Metadata::Param{
        .displayName = "Volume",
        .defaultValue = defaultValue,
        .minValue = 0,
        .maxValue = 2,
        .macroOperation = Patch::Metadata::Param::MacroOperation::kMult,
        .valueFormatter = createPercentFormatter(),
    };
}

inline Patch::Metadata::Param createLfoSyncParam()
{
    return Patch::Metadata::Param{
        .displayName = "Sync",
        .defaultValue = 0,
        .minValue = 0,
        .maxValue = 1,
        .isEnum = true,
        .invalidatesOtherMetadata = true,
        .valueFormatter = [](const Patch& /* patch */, polysynth::Patch::paramvalue_t value)
        { return (value == 0) ? "Off" : "On"; },
    };
}

inline Patch::Metadata::Param createLfoSyncedFreqParam(const Patch& patch, const int kSyncParamId)
{
    bool isSync = patch.getParamValue(kSyncParamId).value_or(0) != 0;
    return Patch::Metadata::Param{
        .displayName = "Ratio",
        .defaultValue = isSync ? 0 : 10,
        .minValue = 0,
        .maxValue =
          isSync ? static_cast<Patch::paramvalue_short_t>(kTimeBasePulseCount.size() - 1) : 100,
        .editCurve = isSync ? Patch::Metadata::Param::Curve::kLinear
                            : Patch::Metadata::Param::Curve::kExponentialSteep,
        .isEnum = isSync,
        .valueFormatter =
          [kSyncParamId](const Patch& patch, polysynth::Patch::paramvalue_t value)
        {
            bool sync = patch.getParamValue(kSyncParamId).value_or(0) != 0;
            return sync ? kTimeBaseNames[std::clamp<int>(static_cast<int>(std::lround(value)),
                                                         0,
                                                         std::ssize(kTimeBasePulseCount) - 1)]
                   : (value < 10) ? fmt::format("{:.2f} Hz", value)
                                  : fmt::format("{:.1f} Hz", value);
        },
    };
}

template<int Freq, int Resonance, int EnvAmount>
inline void setupPlayFilterMacros(std::vector<polysynth::Patch::Metadata::Macro>& macros)
{
    macros[3] = (Patch::Metadata::Macro{
      .displayName = "Filter Cutoff",
      .modifiers{
        { Freq, 1.0f },
      },
      .bipolar = true,
    });
    macros[4] = (Patch::Metadata::Macro{
      .displayName = "Filter Resonance",
      .modifiers{
        { Resonance, 1.0f },
      },
      .bipolar = true,
    });
    macros[5] = (Patch::Metadata::Macro{
      .displayName = "Filter Env Amount",
      .modifiers{
        { EnvAmount, 1.0f },
      },
      .bipolar = true,
    });
}

template<int Volume>
inline void setupVelocityMacro(std::vector<polysynth::Patch::Metadata::Macro>& macros)
{
    macros[*Patch::SystemMacroId::kVelocity].modifiers = { { Volume, 1.0f } };
}

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define SETUP_PATCH_VOLUME()                                                                       \
    helper.addParam(ParamId::kAmpVolume,                                                           \
                    createPatchVolumeParam(),                                                      \
                    [](auto& voice, Patch::optional_paramvalue_t newValue)                         \
                    { voice.synth.setAmpVolumeMult(*newValue); });

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define SETUP_AMP_ENV()                                                                            \
    helper.addParam(ParamId::kAmpEnvAttack,                                                        \
                    {                                                                              \
                      .displayName = "Attack",                                                     \
                      .defaultValue = 0.0f,                                                        \
                      .minValue = 0,                                                               \
                      .maxValue = 10,                                                              \
                      .editCurve = Patch::Metadata::Param::Curve::kExponential,                    \
                      .midiCC = *MidiCCs::kAmpEnvAttack,                                           \
                      .valueFormatter = createTimeFormatter(),                                     \
                    },                                                                             \
                    [](auto& voice, Patch::optional_paramvalue_t newValue)                         \
                    { voice.synth.ampEnv.setAttack(static_cast<polydsp::param_t>(*newValue)); });  \
    helper.addParam(ParamId::kAmpEnvDecay,                                                         \
                    {                                                                              \
                      .displayName = "Decay",                                                      \
                      .defaultValue = 0.5f,                                                        \
                      .minValue = 0,                                                               \
                      .maxValue = 10,                                                              \
                      .editCurve = Patch::Metadata::Param::Curve::kExponential,                    \
                      .midiCC = *MidiCCs::kAmpEnvDecay,                                            \
                      .valueFormatter = createTimeFormatter(),                                     \
                    },                                                                             \
                    [](auto& voice, Patch::optional_paramvalue_t newValue)                         \
                    { voice.synth.ampEnv.setDecay(static_cast<polydsp::param_t>(*newValue)); });   \
    helper.addParam(ParamId::kAmpEnvSustain,                                                       \
                    {                                                                              \
                      .displayName = "Sustain",                                                    \
                      .defaultValue = 0,                                                           \
                      .minValue = 0,                                                               \
                      .maxValue = 1,                                                               \
                      .midiCC = *MidiCCs::kAmpEnvSustain,                                          \
                      .valueFormatter = createPercentFormatter(),                                  \
                    },                                                                             \
                    [](auto& voice, Patch::optional_paramvalue_t newValue)                         \
                    {                                                                              \
                        voice.synth.modMatrix.setBaseValue(                                        \
                          *SynthClass::ModDestinations::kAmpEnvSustain,                            \
                          static_cast<polydsp::param_t>(*newValue));                               \
                    });                                                                            \
    helper.addParam(ParamId::kAmpEnvRelease,                                                       \
                    {                                                                              \
                      .displayName = "Release",                                                    \
                      .defaultValue = 0.5f,                                                        \
                      .minValue = 0,                                                               \
                      .maxValue = 10,                                                              \
                      .editCurve = Patch::Metadata::Param::Curve::kExponential,                    \
                      .midiCC = *MidiCCs::kAmpEnvRelease,                                          \
                      .valueFormatter = createTimeFormatter(),                                     \
                    },                                                                             \
                    [](auto& voice, Patch::optional_paramvalue_t newValue)                         \
                    { voice.synth.ampEnv.setRelease(static_cast<polydsp::param_t>(*newValue)); });

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define SETUP_FILTER_ENV()                                                                         \
    helper.addParam(ParamId::kFilterEnvAttack,                                                     \
                    {                                                                              \
                      .displayName = "Attack",                                                     \
                      .defaultValue = 0.0f,                                                        \
                      .minValue = 0,                                                               \
                      .maxValue = 10,                                                              \
                      .editCurve = Patch::Metadata::Param::Curve::kExponential,                    \
                      .midiCC = *MidiCCs::kFilterEnvAttack,                                        \
                      .valueFormatter = createTimeFormatter(),                                     \
                    },                                                                             \
                    [](auto& voice, Patch::optional_paramvalue_t newValue) {                       \
                        voice.synth.filterEnv.setAttack(static_cast<polydsp::param_t>(*newValue)); \
                    });                                                                            \
    helper.addParam(ParamId::kFilterEnvDecay,                                                      \
                    {                                                                              \
                      .displayName = "Decay",                                                      \
                      .defaultValue = 0.5f,                                                        \
                      .minValue = 0,                                                               \
                      .maxValue = 10,                                                              \
                      .editCurve = Patch::Metadata::Param::Curve::kExponential,                    \
                      .midiCC = *MidiCCs::kFilterEnvDecay,                                         \
                      .valueFormatter = createTimeFormatter(),                                     \
                    },                                                                             \
                    [](auto& voice, Patch::optional_paramvalue_t newValue) {                       \
                        voice.synth.filterEnv.setDecay(static_cast<polydsp::param_t>(*newValue));  \
                    });                                                                            \
    helper.addParam(ParamId::kFilterEnvSustain,                                                    \
                    {                                                                              \
                      .displayName = "Sustain",                                                    \
                      .defaultValue = 0,                                                           \
                      .minValue = 0,                                                               \
                      .maxValue = 1,                                                               \
                      .midiCC = *MidiCCs::kFilterEnvSustain,                                       \
                      .valueFormatter = createPercentFormatter(),                                  \
                    },                                                                             \
                    [](auto& voice, Patch::optional_paramvalue_t newValue)                         \
                    {                                                                              \
                        voice.synth.modMatrix.setBaseValue(                                        \
                          *SynthClass::ModDestinations::kFilterEnvSustain,                         \
                          static_cast<polydsp::param_t>(*newValue));                               \
                    });                                                                            \
    helper.addParam(                                                                               \
      ParamId::kFilterEnvRelease,                                                                  \
      {                                                                                            \
        .displayName = "Release",                                                                  \
        .defaultValue = 0.5f,                                                                      \
        .minValue = 0,                                                                             \
        .maxValue = 10,                                                                            \
        .editCurve = Patch::Metadata::Param::Curve::kExponential,                                  \
        .midiCC = *MidiCCs::kFilterEnvRelease,                                                     \
        .valueFormatter = createTimeFormatter(),                                                   \
      },                                                                                           \
      [](auto& voice, Patch::optional_paramvalue_t newValue)                                       \
      { voice.synth.filterEnv.setRelease(static_cast<polydsp::param_t>(*newValue)); });

}

inline polysynth::Patch::Metadata::Param createModAmtParam(const std::string& name)
{
    return polysynth::Patch::Metadata::Param{
        .displayName = "Amt " + name,
        .defaultValue = 0,
        .minValue = -4,
        .maxValue = 4,
        .editCurve = polysynth::Patch::Metadata::Param::Curve::kPower,
        .valueFormatter =
          [](const polysynth::Patch& /* patch */, polysynth::Patch::paramvalue_t value)
        {
            auto percent = value * 100.f;
            auto absPercent = std::abs(percent);
            if (absPercent < 0.1f)
            {
                return fmt::format("{:.3f} %", percent);
            }
            if (absPercent < 2.f)
            {
                return fmt::format("{:.2f} %", percent);
            }
            if (absPercent < 50.f)
            {
                return fmt::format("{:.1f} %", percent);
            }
            return fmt::format("{:.0f} %", percent);
        },
    };
}