#pragma once

#include "PolySynth/Core.h"
#include "PolySynth/Musicology.h"

#include <boost/container/static_vector.hpp>
#include <boost/core/noncopyable.hpp>
#include <boost/predef/architecture/arm.h>
#include <boost/predef/architecture/x86.h>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>

namespace polysynth
{

/**
 * @brief Contains all the parameters and data of a specific instrument type used for its audio
 * rendering.
 *
 * Per-voice macros:
 * - calculated separately for each voice of the instrument, used for velocity and pressure
 *   modulation
 * - do not affect getParamFinalValue(int paramId)
 * - std::nullopt passed in voiceId argument can be used only on !perVoice macros
 *
 */
class Patch : private boost::noncopyable
{
  public:
    using paramvalue_t = float;
    using optional_paramvalue_t = tiny_optional<paramvalue_t>;
#if BOOST_ARCH_ARM
    using paramvalue_short_t = __fp16;
#elif BOOST_ARCH_X86
    using paramvalue_short_t = float;
#endif

    static constexpr paramvalue_t kMaxMacroValue = 100;

    enum class SystemMacroId
    {
        kVelocity = kUserMacroCount,
        kPressure,
    };

    struct Sequencer
    {
        struct Config
        {
            enum class Mode
            {
                kOff,
                kArp,
                kSeq
            };
            struct Arp
            {
                enum class ArpStyle
                {
                    kUp,
                    kDown,
                    kPlayOrder,
                    kRandom
                };
                ArpStyle arpStyle = ArpStyle::kRandom;
                int octaveRange = 3;

                static constexpr int kMinOctaveRange = 1;
                static constexpr int kMaxOctaveRange = 8;

                bool operator==(const Arp& rhs) const
                {
                    return arpStyle == rhs.arpStyle && octaveRange == rhs.octaveRange;
                }
                bool operator!=(const Arp& rhs) const { return !(rhs == *this); }
            };

            Mode mode = Mode::kOff;
            Arp arp;
        };

        static constexpr int kSeqMaxLength = kMaxSequenceLength;
        static constexpr int kSeqsPerInstrument = 1;
        static constexpr int kDefaultVelocity = 100;
        struct Step
        {
            Musicology::Chord chord{};
            int velocity = kDefaultVelocity;
            int length = 1;
            bool operator==(const Step& rhs) const = default;
        };

        struct Sequence
        {
            using Steps = boost::container::static_vector<Step, kSeqMaxLength>;
            note_t rootNote = kNoteC4;
            scalescheme_t scaleScheme = Musicology::kChromaticScale;
            Steps steps;
        };
        using Sequences = std::array<Sequence, kSeqsPerInstrument>;

        Sequences sequences;
        Config config;
    };

    struct Metadata
#ifndef UNIT_TESTS
      : private boost::noncopyable
#endif
    {
        struct Param
        {
            // https://www.desmos.com/calculator/ejiaclvt5b
            enum class Curve : uint8_t
            {
                kLinear,
                kExponential,
                kExponentialSteep,
                kPower,
            };

            enum class MacroOperation : uint8_t
            {
                kAdd,
                kMult,
            };

            std::string displayName;
            optional_paramvalue_t defaultValue = std::nullopt;
            paramvalue_short_t minValue = std::numeric_limits<paramvalue_short_t>::min();
            paramvalue_short_t maxValue = std::numeric_limits<paramvalue_short_t>::max();
            Curve editCurve = Curve::kLinear;
            MacroOperation macroOperation = MacroOperation::kAdd;
            bool isEnum = false;
            tiny::optional<uint8_t, 0xff> midiCC = std::nullopt;
            bool invalidatesOtherMetadata = false;
            std::function<std::string(const Patch& patch, paramvalue_t value)> valueFormatter = {};

            /**
             * Host input sources are numbered 0..kInputSourceSynthBase - 1 for sample tracks,
             * followed by synth parts. A param with a source routes it into the voice input while
             * its value is above 0.5. Only synth parts before the voice's own part are available.
             */
            static constexpr int kInputSourceSynthBase = 8;
            int inputSource = -1;
        };

        struct Macro
        {
            /**
             * Macros are mapped to Midi CC starting from this CC id.
             */
            static constexpr int kMacroCCStart = 102;
            static constexpr int kMaxDisplayNameLen = 20;

            struct ParamModifier
            {
                static constexpr float kMaxAmount = 2.f;
                tiny_optional<int> paramId;
                /**
                 * factor that determines the strength of the modifier in relation to range of the
                 * corresponding param. 1.0 means whole range ascending -0.5 means half range
                 * descending
                 */
                float amount;
            };

            std::string displayName;
            std::vector<ParamModifier> modifiers = {};
            bool bipolar = false;
            bool perVoice = false; // see note in Patch comment
            bool system = false;   // special macro, user can edit only modifiers
        };

        struct SampleSlot
        {
            std::string displayName;
            enum Type
            {
                kSample,
                kWaveTable,
            } type;
            int section = 0;
            int subsection = 0;
            std::string defaultSamplePath = {};
        };

        enum class OutputChannels
        {
            kNone,
            kMono,
            kStereo,
        };

        struct EditorLayout
        {
            struct EditorElement
            {
                enum class Type
                {
                    kEmpty,
                    kParam,
                    kSampleSlot
                } type = Type::kEmpty;
                int id = -1;
            };

            struct EditorSubsection
            {
                std::string name{};
                std::vector<EditorElement> elements;
            };

            struct EditorSection
            {
                std::string name{};
                std::string shortName{};
                std::vector<EditorSubsection> subsections{};
                std::vector<EditorElement> elements{};
            };

            std::vector<EditorSection> sections;
        };

        std::vector<Param> params = {};
        std::vector<Macro> macros = { kMacroCount, Macro{} };
        std::vector<SampleSlot> sampleSlots = {};
        int instrumentTypeId = -1;
        OutputChannels outputChannels = OutputChannels::kNone;
        std::function<void(std::vector<Macro>& macros)> initDefaultMacros{ [](auto&) {} };
        // if macro values should be included in CC updates of controlled parameters
        bool macrosAffectParamCC = false;
        bool rendersAuxSends = false;
        int maxVoices = std::numeric_limits<int>::max();

        // 3 columns / 3 rows editor layout
        EditorLayout layout33 = {};

        // 2 columns / 3 rows editor layout
        EditorLayout layout23 = {};

        // Layout with sections with any number of parameters, no subsections
        EditorLayout layoutFlat = {};
    };

    enum class ParamChangeSource
    {
        kKnob,
        kMidiPort,
        kPatchAssign,
        kMacro,
    };

    class ParamChangeListener
    {
      public:
        virtual void onParamChange(int paramId,
                                   optional_paramvalue_t newValue,
                                   ParamChangeSource source,
                                   tiny_optional<int> voiceId) = 0;

        virtual void onPatchReset() {}

        [[nodiscard]] virtual int getVoiceCount() const = 0;

        virtual ~ParamChangeListener() = default;
    };

    /**
     * Get param value directly as set (excluding macros).
     */
    [[nodiscard]] virtual optional_paramvalue_t getParamValue(int paramId) const = 0;

    /**
     * Get param value mapped to characteristic curve specified in Metadata::Param::editCurve.
     *
     * @param paramId
     * @return float [0, 1]
     */
    [[nodiscard]] virtual tiny_optional<float> getParamValueEditMapped(int paramId) const = 0;

    /**
     * Set param value mapped to characteristic curve specified in Metadata::Param::editCurve.
     *
     * @param value [0, 1]
     */
    virtual void setParamValueEditMapped(int paramId,
                                         tiny_optional<float> value,
                                         ParamChangeSource source) = 0;

    /**
     * Get param value including macros.
     */
    [[nodiscard]] virtual Patch::optional_paramvalue_t getParamFinalValue(int paramId) const = 0;
    virtual void setParamValue(int paramId,
                               optional_paramvalue_t value,
                               ParamChangeSource source) = 0;

    virtual void subscribeParamUpdates(std::weak_ptr<ParamChangeListener> listener) = 0;
    virtual void unsubscribeParamUpdates(const ParamChangeListener& listener) = 0;

    /**
     * Start an parameters update batch, until end of the batch no parameter
     * updates will be sent to voices.
     * This allows to group an update
     * and deal with param dependencies in param update handlers.
     */
    virtual void startBatchUpdate() = 0;

    /**
     * End current parameters update batch and send update notification
     * to subscribers for all params in the patch.
     */
    virtual void finishBatchUpdate() = 0;

    [[nodiscard]] virtual optional_paramvalue_t getMacroValue(int macroId) const = 0;
    virtual void setMacroValue(int macroId,
                               optional_paramvalue_t value,
                               tiny_optional<int> voiceId) = 0;

    /**
     * Set, add or delete macro modifier with given paramId.
     * @param macroId macro ID
     * @param modifierIdx index of the modifier. Will add a new modifier if this is equal to
     * modifier count
     * @param paramId id of the modifier param.
     * If the optional is null then this function will delete a modifier by given modifierIdx
     */
    virtual void setMacroModifier(int macroId, int modifierIdx, tiny_optional<int> paramId) = 0;

    /**
     * Sets amount only of given macro modifier
     * @param macroId macro ID
     * @param modifierIdx index of the modifier
     * @param amount amount to be set
     */
    virtual void setMacroModifierAmount(int macroId, int modifierIdx, float amount) = 0;

    /**
     * Gets amount only of given macro modifier
     * @param macroId macro ID
     * @param modifierIdx index of the modifier
     */
    [[nodiscard]] virtual tiny_optional<float> getMacroModifierAmount(int macroId,
                                                                      int modifierIdx) = 0;

    /**
     * Removes all modifiers in all macros and resets internal structures.
     */
    virtual void clearMacroModifiers() = 0;

    /**
     * Changes macro modifier amount by given a delta param value.
     * @param macroId macro ID
     * @param modifierIdx index of the modifier
     * @param value delta value that corresponds to range of underlying parameter.
     */
    virtual void changeMacroModifierAmount(int macroId, int modifierIdx, paramvalue_t value) = 0;
    virtual void setMacroBipolar(int macroId, bool bipolar) = 0;
    virtual void setMacroDisplayName(int macroId, std::string name) = 0;

    [[nodiscard]] virtual const Sample* getSample(int sampleSlotId) const = 0;

    // Do not call this function in a critical section (long time).
    // It synchronizes with audio thread internally.
    [[nodiscard]] virtual LoadStatus loadSample(int sampleSlotId, const std::string& filePath) = 0;

    [[nodiscard]] virtual const Metadata& getMetadata() const = 0;
    [[nodiscard]] virtual const Sequencer& getSequencer() const = 0;

    virtual void setSequence(const Sequencer::Sequence& steps, int idx) = 0;
    virtual void setSequencerConfig(const Sequencer::Config& config) = 0;

    virtual ~Patch() = default;
};
}
