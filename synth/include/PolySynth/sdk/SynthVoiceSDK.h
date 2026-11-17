#pragma once

#include "PolySynth/Core.h"
#include "PolySynth/MidiEvent.h"
#include "PolySynth/Patch.h"
#include "PolySynth/sdk/InstrumentType.h"
#include "PolySynth/sdk/InstrumentVoice.h"

#include "enum_util.h"
#include <algorithm>
#include <array>
#include <boost/container/static_vector.hpp>
#include <cmath>
#include <fmt/format.h>
#include <functional>
#include <span>
#include <string>

namespace polysynth::sdk
{

static constexpr float kSampleRate = 44100.0f;
static constexpr float kSilenceGainDb = -90.0f;
static constexpr float kNoteA4Freq = 440.0f;

using EditorSection = Patch::Metadata::EditorLayout::EditorSection;
using EditorSubsection = Patch::Metadata::EditorLayout::EditorSubsection;
using EditorElement = Patch::Metadata::EditorLayout::EditorElement;
using Param = Patch::Metadata::Param;

/**
 * Fast polynomial approximation of sin(2 * pi * phase) for any phase in turns.
 * Accurate to within -124 dB (~6e-7 max absolute error). Safe for per-sample audio callbacks.
 */
inline float fastSine(float phase)
{
    float t = phase - __builtin_roundf(phase);
    if (t > 0.25f)
    {
        t = 0.5f - t;
    }
    else if (t < -0.25f)
    {
        t = -0.5f - t;
    }
    const float t2 = t * t;
    return t * (6.283164f + t2 * (-41.337143f + t2 * (81.340782f + t2 * -70.993568f)));
}

/**
 * Fast minimax polynomial + IEEE-754 exponent approximation of 2^x.
 * Accurate to within 0.0002 cents (~1.1e-7 max relative error). Safe for per-sample audio callbacks.
 */
inline float fastPow2(float x)
{
    x = std::clamp(x, -126.0f, 127.0f);
    const float whole = __builtin_floorf(x);
    const float f = x - whole;
    const float mantissa =
      1.0f + f * (0.69315249f +
                  f * (0.24015281f + f * (0.05583591f + f * (0.0089734f + f * 0.00188529f))));
    const union
    {
        uint32_t bits;
        float value;
    } scale = { static_cast<uint32_t>(static_cast<int32_t>(whole) + 127) << 23 };
    return mantissa * scale.value;
}

/**
 * Convert decibels to linear amplitude multiplier.
 */
constexpr float dbToAmplitude(float db)
{
    return db > kSilenceGainDb ? std::pow(10.0f, db * 0.05f) : 0.0f;
}

/**
 * Convert linear amplitude multiplier to decibels.
 */
constexpr float amplitudeToDb(float amplitude)
{
    return amplitude > 0.0f ? std::log10(amplitude) * 20.0f : kSilenceGainDb;
}

/**
 * Soft-clip saturation (cubic waveshaper: out = in - in^3 / 6.72) with pre-gain in dB.
 */
inline void saturateSoftClip(std::span<sample_t> buf, float gainDb)
{
    const float gainRaw = dbToAmplitude(gainDb);
    for (auto& element : buf)
    {
        float sample = std::clamp(element * gainRaw, -1.5f, 1.5f);
        sample = sample - (sample * sample * sample) * (1.0f / 6.72f);
        element = std::clamp(sample, -1.0f, 1.0f);
    }
}

/**
 * Convert MIDI note number + detune (in semitones) to frequency in Hz.
 */
inline float noteToFreq(note_t note, float detuneSemitones = 0.0f)
{
    return kNoteA4Freq *
           fastPow2((static_cast<float>(note - kNoteA4) + detuneSemitones) *
                    (1.0f / static_cast<float>(kOneOctaveNotes)));
}

/**
 * Frequency ratio multiplier for a given detune in semitones.
 */
inline float detuneMult(float detuneSemitones)
{
    return fastPow2(detuneSemitones * (1.0f / static_cast<float>(kOneOctaveNotes)));
}

enum class MidiCCs : uint8_t
{
    kOscParam0 = 20, // 12 slots (20..31)
    kBankSelectLSB = 32,
    kUserEnv1Attack = 46,
    kUserEnv1Decay = 47,
    kUserEnv1Sustain = 48,
    kUserEnv1Release = 49,
    kUserEnv2Attack = 50,
    kUserEnv2Decay = 51,
    kUserEnv2Sustain = 52,
    kUserEnv2Release = 53,
    kLfo1Frequency = 54,
    kLfo2Frequency = 55,
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
    kFilterEnvDecay = 81,
    kFilterEnvSustain = 82,
    kFilterEnvRelease = 83,
    kVelocityVolume = 84,
    kDelaySend = 90,
    kReverbSend = 91,
    kMacroStart = 102,
};

inline auto createPercentFormatter(bool showPercentSign = true)
{
    return [showPercentSign](const Patch& /* patch */, Patch::paramvalue_t value)
    { return fmt::format(showPercentSign ? "{:.0f} %" : "{:.0f}", value * 100.0f); };
}

inline auto createTimeFormatter()
{
    return [](const Patch& /* patch */, Patch::paramvalue_t value)
    {
        return (value < 1.0f) ? fmt::format("{:.2f} s", value) : fmt::format("{:.1f} s", value);
    };
}

inline auto createFrequencyFormatter(
  Patch::paramvalue_t max = std::numeric_limits<Patch::paramvalue_t>::max())
{
    return [max](const Patch& /* patch */, Patch::paramvalue_t value)
    {
        if (value >= max)
        {
            return std::string("Off");
        }
        return (value < 10.0f)
                 ? fmt::format("{:.2f} Hz", value)
                 : ((value < 100.0f)
                      ? fmt::format("{:.1f} Hz", value)
                      : ((value < 1000.0f)
                           ? fmt::format("{:.0f} Hz", value)
                           : ((value < 10000.0f) ? fmt::format("{:.2f} kHz", value / 1000.0f)
                                                 : fmt::format("{:.1f} kHz", value / 1000.0f))));
    };
}

inline auto createSemitoneFormatter()
{
    return [](const Patch& /* patch */, Patch::paramvalue_t value)
    {
        int semi = static_cast<int>(std::lround(value));
        return semi > 0 ? fmt::format("+{:d} st", semi) : fmt::format("{:d} st", semi);
    };
}

inline auto createCentsFormatter()
{
    return [](const Patch& /* patch */, Patch::paramvalue_t value)
    {
        int cents = static_cast<int>(std::lround(value));
        return cents > 0 ? fmt::format("+{:d} ct", cents) : fmt::format("{:d} ct", cents);
    };
}

template<class VoiceType>
class ParamHelper
{
  public:
    using ParamHandlerFn = void (*)(VoiceType& voice, Patch::optional_paramvalue_t newValue);

    ParamHelper(Patch::Metadata& metadata, std::span<ParamHandlerFn> paramHandlers)
      : metadata(metadata)
      , paramHandlers(paramHandlers)
    {
    }

    template<typename Fn>
    void addParam(int paramId,
                  Patch::Metadata::Param paramDef,
                  [[maybe_unused]] Fn paramHandler)
    {
        static_assert(std::is_empty_v<Fn>, "Param handler lambda must be stateless (no captures)");
        assert(paramId < static_cast<int>(paramHandlers.size()));
        metadata.params.at(paramId) = std::move(paramDef);
        if (!paramHandlers[paramId])
        {
            paramHandlers[paramId] = +[](VoiceType& voice, Patch::optional_paramvalue_t newValue)
            { Fn{}(voice, newValue); };
        }
    }

  private:
    Patch::Metadata& metadata;
    std::span<ParamHandlerFn> paramHandlers;
};

/**
 * Header-only base class for open-source PolySynth voices.
 * Handles parameter dispatch, mono/legato/poly note stack tracking, and MIDI events.
 */
template<class VoiceClass, class ParamIdEnum, size_t kMaxHeldNotes = 8>
class CustomSynthVoice : public InstrumentVoice
{
  public:
    enum class PlayMode : int
    {
        kAlways = 0,
        kOverlap = 1,
        kLegato = 2,
    };

    void onMidiEvent(const InstrumentVoiceContext& ctx, const MidiEvent& event) override
    {
        switch (event.type)
        {
            case MidiEvent::Type::kStart:
                onStart();
                break;
            case MidiEvent::Type::kClock:
                onClock();
                break;
            case MidiEvent::Type::kNoteOn:
            {
                const float vel = std::clamp(
                  static_cast<float>(event.data.note.velocity) /
                    static_cast<float>(MidiEvent::Data::Note::kMaxVelocity),
                  0.0f,
                  1.0f);
                handleNoteOnInternal(ctx, event.data.note.note, vel);
                break;
            }
            case MidiEvent::Type::kNoteOff:
                handleNoteOffInternal(ctx, event.data.note.note);
                break;
            case MidiEvent::Type::kPitchBendChange:
                pitchBendSemitones =
                  (static_cast<float>(event.data.pitchBendChange.value) * kPitchBendRangeSemitones) /
                  static_cast<float>(MidiEvent::Data::PitchBendChange::kMaxValue);
                break;
            default:
                break;
        }
    }

    void onParamChange(int paramId, Patch::optional_paramvalue_t newValue) final
    {
        if (paramId >= 0 && paramId < static_cast<int>(paramHandlers.size()) &&
            paramHandlers[paramId] && newValue.has_value())
        {
            paramHandlers[paramId](*static_cast<VoiceClass*>(this), newValue);
        }
    }

    bool handlesNote(note_t note) final
    {
        for (const auto& held : heldNotes)
        {
            if (held.note == note)
            {
                return true;
            }
        }
        return false;
    }

    bool handlesAnyNote() final { return !heldNotes.empty(); }

    tiny_optional<note_t> getLastNote() final { return lastNote; }

    tiny_optional<float> getCurrentNoteVelocity() final
    {
        if (heldNotes.empty())
        {
            return std::nullopt;
        }
        return heldNotes.back().velocity;
    }

    void clearPreviousNotes() final
    {
        if (heldNotes.size() > 1)
        {
            auto current = heldNotes.back();
            heldNotes.clear();
            heldNotes.push_back(current);
        }
    }

#ifdef PRODUCT_WAVE
    void renderVoiceAudio(const InstrumentVoiceContext& ctx,
                          std::optional<AudioBufferStereoRef> /*inputBuffer*/,
                          AudioBufferStereoRef outputBuffer) override
    {
        renderVoiceAudio(ctx, outputBuffer);
    }

    virtual void renderVoiceAudio(const InstrumentVoiceContext& ctx,
                                  AudioBufferStereoRef outputBuffer) = 0;
#endif

  protected:
    static constexpr float kPitchBendRangeSemitones = 2.0f;

    struct HeldNote
    {
        note_t note;
        float velocity;
    };

    boost::container::static_vector<HeldNote, kMaxHeldNotes> heldNotes;
    tiny_optional<note_t> lastNote = std::nullopt;
    float currentVelocity = 1.0f;
    float pitchBendSemitones = 0.0f;
    PlayMode playMode = PlayMode::kAlways;

    using ParamHandlerFn = void (*)(VoiceClass& voice, Patch::optional_paramvalue_t newValue);

    static inline boost::container::static_vector<ParamHandlerFn,
                                                  enumutil::EnumMax<ParamIdEnum>::value + 1>
      paramHandlers;

    virtual void onNoteOn(const InstrumentVoiceContext& ctx,
                          note_t note,
                          float velocity,
                          bool isLegatoStep) = 0;
    virtual void onNoteOff(const InstrumentVoiceContext& ctx, note_t note) = 0;
    virtual void onStart() {}
    virtual void onClock() {}

  private:
    void handleNoteOnInternal(const InstrumentVoiceContext& ctx, note_t note, float velocity)
    {
        const bool hadHeldNote = !heldNotes.empty();
        // Remove existing entry if same note re-triggered
        for (auto it = heldNotes.begin(); it != heldNotes.end(); ++it)
        {
            if (it->note == note)
            {
                heldNotes.erase(it);
                break;
            }
        }
        if (heldNotes.size() == heldNotes.capacity())
        {
            heldNotes.erase(heldNotes.begin());
        }
        heldNotes.push_back({ note, velocity });
        lastNote = note;
        currentVelocity = velocity;

        const bool isLegato = hadHeldNote && (playMode == PlayMode::kLegato);
        onNoteOn(ctx, note, velocity, isLegato);
    }

    void handleNoteOffInternal(const InstrumentVoiceContext& ctx, note_t note)
    {
        bool wasTopNote = (!heldNotes.empty() && heldNotes.back().note == note);
        for (auto it = heldNotes.begin(); it != heldNotes.end(); ++it)
        {
            if (it->note == note)
            {
                heldNotes.erase(it);
                break;
            }
        }

        if (!heldNotes.empty())
        {
            if (wasTopNote)
            {
                const auto& prev = heldNotes.back();
                lastNote = prev.note;
                currentVelocity = prev.velocity;
                onNoteOn(ctx, prev.note, prev.velocity, playMode == PlayMode::kLegato);
            }
        }
        else
        {
            onNoteOff(ctx, note);
        }
    }
};

} // namespace polysynth::sdk
