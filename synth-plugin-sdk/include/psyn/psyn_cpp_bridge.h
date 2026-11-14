#pragma once

/**
 * C++ Bridge for Polyend .psyn Loadable Synth Plugins
 *
 * Allows existing and new C++ synth voices written against polysynth::Patch,
 * polysynth::InstrumentVoice, polysynth::sdk::CustomSynthVoice, or BaseSynthVoice
 * to compile directly into standalone .psyn SD-card binaries with:
 *   - All dynamic allocations (new/delete/malloc/free) routed to the host's
 *     external SDRAM plugin pool (zero internal SRAM_DTC heap usage).
 *   - Lightweight header-only fmt::format replacement (zero libfmt/locale bloat).
 *   - Automatic translation of Patch::Metadata and InstrumentVoice calls across
 *     the pure C ABI defined in psyn_plugin.h.
 */

#include "psyn_plugin.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <string_view>

/* Provide a lightweight fmt::format before SynthVoiceSDK.h or synths_utils.h includes <fmt/format.h> */
#ifndef FMT_FORMAT_H_
#define FMT_FORMAT_H_

namespace fmt
{
namespace detail
{
inline void appendInt(std::string& out, int64_t val, bool forcePlus)
{
    if (val < 0)
    {
        out.push_back('-');
        val = -val;
    }
    else if (forcePlus)
    {
        out.push_back('+');
    }
    char buf[24];
    int pos = 0;
    do
    {
        buf[pos++] = static_cast<char>('0' + (val % 10));
        val /= 10;
    } while (val > 0);
    while (pos > 0)
    {
        out.push_back(buf[--pos]);
    }
}

inline void appendFloat(std::string& out, double val, int precision, bool forcePlus)
{
    if (std::isnan(val))
    {
        out += "NaN";
        return;
    }
    if (val < 0.0)
    {
        out.push_back('-');
        val = -val;
    }
    else if (forcePlus)
    {
        out.push_back('+');
    }

    double scale = 1.0;
    for (int i = 0; i < precision; ++i)
    {
        scale *= 10.0;
    }
    auto scaled = static_cast<uint64_t>(val * scale + 0.5);
    auto intPart = static_cast<uint64_t>(scaled / static_cast<uint64_t>(scale));
    auto fracPart = static_cast<uint64_t>(scaled % static_cast<uint64_t>(scale));

    appendInt(out, static_cast<int64_t>(intPart), false);
    if (precision > 0)
    {
        out.push_back('.');
        char buf[16];
        for (int i = precision - 1; i >= 0; --i)
        {
            buf[i] = static_cast<char>('0' + (fracPart % 10));
            fracPart /= 10;
        }
        out.append(buf, precision);
    }
}

inline void formatOneArg(std::string& out, std::string_view spec, int64_t val)
{
    bool forcePlus = (spec.find('+') != std::string_view::npos);
    appendInt(out, val, forcePlus);
}

inline void formatOneArg(std::string& out, std::string_view spec, double val)
{
    bool forcePlus = (spec.find('+') != std::string_view::npos);
    int precision = 2;
    auto dotPos = spec.find('.');
    if (dotPos != std::string_view::npos && dotPos + 1 < spec.size())
    {
        precision = spec[dotPos + 1] - '0';
        if (precision < 0 || precision > 6)
        {
            precision = 2;
        }
    }
    else if (spec.find('d') != std::string_view::npos)
    {
        appendInt(out, static_cast<int64_t>(std::llround(val)), forcePlus);
        return;
    }
    appendFloat(out, val, precision, forcePlus);
}

inline void formatOneArg(std::string& out, std::string_view /*spec*/, const char* val)
{
    if (val != nullptr)
    {
        out += val;
    }
}

inline void formatOneArg(std::string& out, std::string_view /*spec*/, std::string_view val)
{
    out.append(val.data(), val.size());
}

inline void formatOneArg(std::string& out, std::string_view /*spec*/, const std::string& val)
{
    out += val;
}

template<typename T>
inline void formatDispatch(std::string& out, std::string_view spec, const T& val)
{
    if constexpr (std::is_integral_v<T> || std::is_enum_v<T>)
    {
        formatOneArg(out, spec, static_cast<int64_t>(val));
    }
    else if constexpr (std::is_floating_point_v<T>)
    {
        formatOneArg(out, spec, static_cast<double>(val));
    }
    else
    {
        formatOneArg(out, spec, val);
    }
}

inline void formatImpl(std::string& out, std::string_view fmtStr)
{
    out.append(fmtStr.data(), fmtStr.size());
}

template<typename First, typename... Rest>
inline void formatImpl(std::string& out,
                       std::string_view fmtStr,
                       const First& first,
                       const Rest&... rest)
{
    auto openPos = fmtStr.find('{');
    if (openPos == std::string_view::npos)
    {
        out.append(fmtStr.data(), fmtStr.size());
        return;
    }
    auto closePos = fmtStr.find('}', openPos);
    if (closePos == std::string_view::npos)
    {
        out.append(fmtStr.data(), fmtStr.size());
        return;
    }
    out.append(fmtStr.data(), openPos);
    std::string_view spec = fmtStr.substr(openPos + 1, closePos - openPos - 1);
    formatDispatch(out, spec, first);
    formatImpl(out, fmtStr.substr(closePos + 1), rest...);
}
} // namespace detail

template<typename... Args>
inline std::string format(std::string_view fmtStr, const Args&... args)
{
    std::string out;
    out.reserve(24);
    detail::formatImpl(out, fmtStr, args...);
    return out;
}
} // namespace fmt

#endif // FMT_FORMAT_H_

#include "PolySynth/Patch.h"
#if __has_include("instrument/InstrumentVoice.h")
#include "instrument/InstrumentVoice.h"
#else
#include "PolySynth/sdk/InstrumentVoice.h"
#endif

namespace polysynth
{
#if __has_include("PolySynth/musicology/Musicology.h")
struct Musicology
{
    static inline std::string noteToString(note_t note, bool includeOctave = true)
    {
        static constexpr const char* kSyms[12] = {
            "C ", "C#", "D ", "D#", "E ", "F ", "F#", "G ", "G#", "A ", "A#", "B "
        };
        int noteInOctave = static_cast<int>(note) % kOneOctaveNotes;
        if (noteInOctave < 0)
        {
            noteInOctave += kOneOctaveNotes;
        }
        if (includeOctave)
        {
            int octave = (static_cast<int>(note) - kNoteC0) / kOneOctaveNotes;
            return fmt::format("{}{}", kSyms[noteInOctave], octave);
        }
        return kSyms[noteInOctave];
    }
};
#else
inline std::string Musicology::noteToString(note_t note, bool includeOctave)
{
    static constexpr const char* kSyms[12] = {
        "C ", "C#", "D ", "D#", "E ", "F ", "F#", "G ", "G#", "A ", "A#", "B "
    };
    int noteInOctave = static_cast<int>(note) % kOneOctaveNotes;
    if (noteInOctave < 0)
    {
        noteInOctave += kOneOctaveNotes;
    }
    if (includeOctave)
    {
        int octave = (static_cast<int>(note) - kNoteC0) / kOneOctaveNotes;
        return fmt::format("{}{}", kSyms[noteInOctave], octave);
    }
    return kSyms[noteInOctave];
}
#endif
} // namespace polysynth

namespace psyn_bridge
{
inline const PsynHostApi* g_host = nullptr;
} // namespace psyn_bridge

/* Route all plugin heap allocations into the host's external SDRAM slot pool */
extern "C" {
inline void* psyn_malloc(size_t size)
{
    if (psyn_bridge::g_host != nullptr && psyn_bridge::g_host->slot_alloc != nullptr)
    {
        return psyn_bridge::g_host->slot_alloc(size, 8);
    }
    return nullptr;
}

inline void psyn_free(void* ptr)
{
    if (ptr != nullptr && psyn_bridge::g_host != nullptr &&
        psyn_bridge::g_host->slot_free != nullptr)
    {
        psyn_bridge::g_host->slot_free(ptr);
    }
}

void* __wrap_malloc(size_t size)
{
    return psyn_malloc(size);
}

void __wrap_free(void* ptr)
{
    psyn_free(ptr);
}

void* __wrap_calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void* ptr = psyn_malloc(total);
    if (ptr != nullptr)
    {
        std::memset(ptr, 0, total);
    }
    return ptr;
}

void* __wrap__malloc_r(struct _reent*, size_t size)
{
    return psyn_malloc(size);
}

void __wrap__free_r(struct _reent*, void* ptr)
{
    psyn_free(ptr);
}

void* __wrap__calloc_r(struct _reent*, size_t nmemb, size_t size)
{
    return __wrap_calloc(nmemb, size);
}

void* __dso_handle = nullptr;
int __cxa_atexit(void (*)(void*), void*, void*)
{
    return 0;
}
void __cxa_pure_virtual()
{
    while (true)
    {
    }
}
void _exit(int)
{
    while (true)
    {
    }
}
int _kill(int, int)
{
    return -1;
}
int _getpid()
{
    return 1;
}
void* _sbrk(ptrdiff_t incr)
{
    return psyn_malloc(incr > 0 ? static_cast<size_t>(incr) : 0);
}
void __assert_func(const char*, int, const char*, const char*)
{
    while (true)
    {
    }
}
} // extern "C"

namespace std
{
[[noreturn]] inline void __throw_bad_alloc()
{
    while (true)
    {
    }
}
[[noreturn]] inline void __throw_length_error(const char*)
{
    while (true)
    {
    }
}
[[noreturn]] inline void __throw_logic_error(const char*)
{
    while (true)
    {
    }
}
[[noreturn]] inline void __throw_out_of_range(const char*)
{
    while (true)
    {
    }
}
[[noreturn]] inline void __throw_out_of_range_fmt(const char*, ...)
{
    while (true)
    {
    }
}
[[noreturn]] inline void __throw_bad_function_call()
{
    while (true)
    {
    }
}
} // namespace std

void* operator new(size_t size)
{
    return psyn_malloc(size);
}

void* operator new[](size_t size)
{
    return psyn_malloc(size);
}

void* operator new(size_t size, std::align_val_t al)
{
    if (psyn_bridge::g_host != nullptr && psyn_bridge::g_host->slot_alloc != nullptr)
    {
        return psyn_bridge::g_host->slot_alloc(size, static_cast<size_t>(al));
    }
    return nullptr;
}

void* operator new[](size_t size, std::align_val_t al)
{
    if (psyn_bridge::g_host != nullptr && psyn_bridge::g_host->slot_alloc != nullptr)
    {
        return psyn_bridge::g_host->slot_alloc(size, static_cast<size_t>(al));
    }
    return nullptr;
}

void operator delete(void* ptr) noexcept
{
    psyn_free(ptr);
}

void operator delete[](void* ptr) noexcept
{
    psyn_free(ptr);
}

void operator delete(void* ptr, size_t) noexcept
{
    psyn_free(ptr);
}

void operator delete[](void* ptr, size_t) noexcept
{
    psyn_free(ptr);
}

void operator delete(void* ptr, std::align_val_t) noexcept
{
    psyn_free(ptr);
}

void operator delete[](void* ptr, std::align_val_t) noexcept
{
    psyn_free(ptr);
}

namespace psyn_bridge
{

/**
 * Lightweight proxy implementing polysynth::Patch inside a .psyn plugin
 * by forwarding calls to the host via PsynHostApi.
 */
class PsynPatchProxy final : public polysynth::Patch
{
  public:
    explicit PsynPatchProxy(void* hostPatch = nullptr)
      : hostPatch(hostPatch)
    {
    }

    void setHostPatch(void* hp) { hostPatch = hp; }
    [[nodiscard]] void* getHostPatch() const { return hostPatch; }

    [[nodiscard]] optional_paramvalue_t getParamValue(int paramId) const override
    {
        float val = 0.0f;
        if (g_host != nullptr && hostPatch != nullptr &&
            g_host->patch_get_param_value(hostPatch, paramId, &val))
        {
            return val;
        }
        return std::nullopt;
    }

    [[nodiscard]] tiny_optional<float> getParamValueEditMapped(int) const override
    {
        return std::nullopt;
    }

    void setParamValueEditMapped(int, tiny_optional<float>, ParamChangeSource) override {}

    [[nodiscard]] optional_paramvalue_t getParamFinalValue(int paramId) const override
    {
        float val = 0.0f;
        if (g_host != nullptr && hostPatch != nullptr &&
            g_host->patch_get_param_final_value(hostPatch, paramId, &val))
        {
            return val;
        }
        return std::nullopt;
    }

    void setParamValue(int paramId,
                       optional_paramvalue_t value,
                       ParamChangeSource source) override
    {
        if (g_host != nullptr && hostPatch != nullptr)
        {
            g_host->patch_set_param_value(hostPatch,
                                          paramId,
                                          value.has_value(),
                                          value.value_or(0.0f),
                                          static_cast<int>(source));
        }
    }

    void subscribeParamUpdates(std::weak_ptr<ParamChangeListener>) override {}
    void unsubscribeParamUpdates(const ParamChangeListener&) override {}
    void startBatchUpdate() override {}
    void finishBatchUpdate() override {}

    [[nodiscard]] optional_paramvalue_t getMacroValue(int) const override { return std::nullopt; }
    void setMacroValue(int, optional_paramvalue_t, tiny_optional<int>) override {}
    virtual void resetAllValues() {}
    void setMacroModifier(int, int, tiny_optional<int>) override {}
    void setMacroModifierAmount(int, int, float) override {}
    [[nodiscard]] tiny_optional<float> getMacroModifierAmount(int, int) override
    {
        return std::nullopt;
    }
    void clearMacroModifiers() override {}
    void changeMacroModifierAmount(int, int, paramvalue_t) override {}
    void setMacroBipolar(int, bool) override {}
    void setMacroDisplayName(int, std::string) override {}

    [[nodiscard]] const polysynth::Sample* getSample(int sampleSlotId) const override
    {
        if (g_host == nullptr || hostPatch == nullptr || sampleSlotId < 0 || sampleSlotId >= 4)
        {
            return nullptr;
        }
        PsynSampleData sdata{};
        if (!g_host->patch_get_sample(hostPatch, sampleSlotId, &sdata) || sdata.data == nullptr)
        {
            return nullptr;
        }
        auto& slot = cachedSamples[sampleSlotId];
        slot.data = std::span<polysynth::sample_store_t>(
          const_cast<polysynth::sample_store_t*>(sdata.data), sdata.size);
        slot.format.channels = sdata.channels;
        slot.format.sampleRate = static_cast<int>(sdata.sample_rate);
        slot.ready = ((sdata.ready & 1u) != 0);
        slot.basePitch = (sdata.ready >> 1) != 0
                           ? static_cast<polysynth::note_t>(sdata.ready >> 1)
                           : polysynth::kNoteC4;
        slot.waveSize =
          (sdata.wave_size != 0) ? tiny_optional<int>(sdata.wave_size) : std::nullopt;
        return &slot;
    }

    [[nodiscard]] virtual std::string getSampleFilePath(int) const { return {}; }
    [[nodiscard]] polysynth::LoadStatus loadSample(int, const std::string&) override
    {
        return polysynth::LoadStatus::kSuccess;
    }
    [[nodiscard]] virtual polysynth::Sample* createSample(int, int) { return nullptr; }
    [[nodiscard]] const Metadata& getMetadata() const override { return dummyMeta; }
#if __has_include("PolySynth/musicology/Musicology.h")
    [[nodiscard]] tags_t getTags() const override { return {}; }
    void setTags(tags_t) override {}
#else
    [[nodiscard]] const Sequencer& getSequencer() const override { return dummySeq; }
    void setSequence(const Sequencer::Sequence&, int) override {}
    void setSequencerConfig(const Sequencer::Config&) override {}
#endif

  private:
    void* hostPatch = nullptr;
    mutable polysynth::Sample cachedSamples[4]{};
    Metadata dummyMeta{};
#if !__has_include("PolySynth/musicology/Musicology.h")
    Sequencer dummySeq{};
#endif
};

template<class VoiceClass>
struct VoiceHolder
{
    VoiceClass voice;
    std::shared_ptr<PsynPatchProxy> patchProxy;

    explicit VoiceHolder(int voiceIdx) requires(std::is_constructible_v<VoiceClass, int>)
      : voice(voiceIdx)
      , patchProxy(std::shared_ptr<PsynPatchProxy>(&inlineProxy, [](PsynPatchProxy*) {}))
    {
        voice.setPatch(patchProxy);
    }

    explicit VoiceHolder(int /*voiceIdx*/) requires(!std::is_constructible_v<VoiceClass, int>)
      : voice()
      , patchProxy(std::shared_ptr<PsynPatchProxy>(&inlineProxy, [](PsynPatchProxy*) {}))
    {
        voice.setPatch(patchProxy);
    }

    PsynPatchProxy inlineProxy{};
};

template<class VoiceClass>
class PluginAdapter
{
  public:
    using FormatterFn =
      std::function<std::string(const polysynth::Patch& patch, polysynth::Patch::paramvalue_t val)>;

    static inline std::vector<FormatterFn> s_formatters;

    static void createMetadata(const void* hostPatch, void* metaBuilder)
    {
        if (g_host == nullptr || metaBuilder == nullptr)
        {
            return;
        }
        PsynPatchProxy proxy(const_cast<void*>(hostPatch));
        polysynth::Patch::Metadata meta{};
        VoiceClass::createMetadata(proxy, meta);

        g_host->meta_set_info(metaBuilder,
                              static_cast<int>(meta.outputChannels),
                              meta.macrosAffectParamCC,
                              meta.rendersAuxSends,
                              meta.maxVoices);

        const int paramCount = static_cast<int>(meta.params.size());
        g_host->meta_set_param_count(metaBuilder, paramCount);
        s_formatters.resize(paramCount);

        for (int i = 0; i < paramCount; ++i)
        {
            const auto& p = meta.params[i];
            PsynParamDesc desc{};
            desc.display_name = p.displayName.c_str();
            desc.has_default = p.defaultValue.has_value() ? 1 : 0;
            desc.default_value = p.defaultValue.value_or(0.0f);
            desc.min_value = static_cast<float>(p.minValue);
            desc.max_value = static_cast<float>(p.maxValue);
            desc.curve = static_cast<uint8_t>(p.editCurve);
            desc.macro_op = static_cast<uint8_t>(p.macroOperation);
            desc.is_enum = p.isEnum ? 1 : 0;
            desc.midi_cc = p.midiCC.has_value() ? *p.midiCC : 0xFF;
            desc.invalidates_metadata = p.invalidatesOtherMetadata ? 1 : 0;
            desc.input_source = static_cast<uint8_t>(p.inputSource + 1);
            desc.input_sidechain = p.inputSidechain ? 1 : 0;
            g_host->meta_set_param(metaBuilder, i, &desc);
            s_formatters[i] = p.valueFormatter;
        }

        for (const auto& slot : meta.sampleSlots)
        {
            PsynSampleSlotDesc sdesc{};
            sdesc.display_name = slot.displayName.c_str();
            sdesc.default_sample_path = slot.defaultSamplePath.c_str();
            sdesc.type = static_cast<uint8_t>(slot.type);
            sdesc.section = static_cast<uint8_t>(slot.section);
            sdesc.subsection = static_cast<uint8_t>(slot.subsection);
            g_host->meta_add_sample_slot(metaBuilder, &sdesc);
        }

        std::vector<PsynLayoutElement> elems;
        for (const auto& sec : meta.layoutFlat.sections)
        {
            elems.clear();
            elems.reserve(sec.elements.size());
            for (const auto& el : sec.elements)
            {
                elems.push_back(PsynLayoutElement{
                  .type = static_cast<uint8_t>(el.type),
                  .reserved = 0,
                  .id = static_cast<int16_t>(el.id),
                });
            }
            g_host->meta_add_flat_section(metaBuilder,
                                          sec.name.c_str(),
                                          sec.shortName.c_str(),
                                          elems.data(),
                                          static_cast<int>(elems.size()));
        }

        for (const auto& sec : meta.layout33.sections)
        {
            elems.clear();
            elems.reserve(sec.elements.size());
            for (const auto& el : sec.elements)
            {
                elems.push_back(PsynLayoutElement{
                  .type = static_cast<uint8_t>(el.type),
                  .reserved = 0,
                  .id = static_cast<int16_t>(el.id),
                });
            }
            int secIdx = g_host->meta_add_33_section(metaBuilder,
                                                     sec.name.c_str(),
                                                     sec.shortName.c_str(),
                                                     elems.data(),
                                                     static_cast<int>(elems.size()));
            for (const auto& sub : sec.subsections)
            {
                elems.clear();
                elems.reserve(sub.elements.size());
                for (const auto& el : sub.elements)
                {
                    elems.push_back(PsynLayoutElement{
                      .type = static_cast<uint8_t>(el.type),
                      .reserved = 0,
                      .id = static_cast<int16_t>(el.id),
                    });
                }
                g_host->meta_add_33_subsection(metaBuilder,
                                               secIdx,
                                               sub.name.c_str(),
                                               elems.data(),
                                               static_cast<int>(elems.size()));
            }
        }

        if (meta.initDefaultMacros)
        {
            meta.initDefaultMacros(meta.macros);
            for (size_t m = 0; m < meta.macros.size(); ++m)
            {
                const auto& mac = meta.macros[m];
                if (mac.displayName.empty() && mac.modifiers.empty())
                {
                    continue;
                }
                PsynMacroDesc mdesc{};
                mdesc.display_name = mac.displayName.c_str();
                mdesc.bipolar = mac.bipolar ? 1 : 0;
                mdesc.modifier_count =
                  static_cast<uint8_t>(std::min<size_t>(mac.modifiers.size(), 8));
                for (uint8_t k = 0; k < mdesc.modifier_count; ++k)
                {
                    mdesc.modifiers[k].param_id =
                      mac.modifiers[k].paramId.has_value()
                        ? static_cast<int16_t>(*mac.modifiers[k].paramId)
                        : -1;
                    mdesc.modifiers[k].amount = mac.modifiers[k].amount;
                }
                g_host->meta_set_default_macro(metaBuilder, static_cast<int>(m), &mdesc);
            }
        }
    }

    static int formatParam(const void* hostPatch,
                           int paramId,
                           float value,
                           char* outBuf,
                           int outBufLen)
    {
        if (outBuf == nullptr || outBufLen <= 0)
        {
            return 0;
        }
        if (paramId >= 0 && paramId < static_cast<int>(s_formatters.size()) &&
            s_formatters[paramId])
        {
            PsynPatchProxy proxy(const_cast<void*>(hostPatch));
            std::string str = s_formatters[paramId](proxy, value);
            int copyLen = std::min<int>(static_cast<int>(str.size()), outBufLen - 1);
            std::memcpy(outBuf, str.data(), copyLen);
            outBuf[copyLen] = '\0';
            return copyLen;
        }
        std::string str = fmt::format("{:.0f}", value);
        int copyLen = std::min<int>(static_cast<int>(str.size()), outBufLen - 1);
        std::memcpy(outBuf, str.data(), copyLen);
        outBuf[copyLen] = '\0';
        return copyLen;
    }

    static void* createVoice(int voiceIdx)
    {
        auto* holder = new (std::nothrow) VoiceHolder<VoiceClass>(voiceIdx);
        if (holder == nullptr)
        {
            return nullptr;
        }
        if constexpr (requires(const VoiceClass& v) { v.isValid(); })
        {
            if (!holder->voice.isValid())
            {
                delete holder;
                return nullptr;
            }
        }
        return holder;
    }

    static void destroyVoice(void* v) { delete static_cast<VoiceHolder<VoiceClass>*>(v); }

    static void voiceSetPatch(void* v, void* hostPatch)
    {
        auto* holder = static_cast<VoiceHolder<VoiceClass>*>(v);
        holder->inlineProxy.setHostPatch(hostPatch);
    }

    static void voiceOnMidiEvent(void* v,
                                 const PsynVoiceContext* ctx,
                                 const PsynMidiEvent* ev)
    {
        auto* holder = static_cast<VoiceHolder<VoiceClass>*>(v);
        polysynth::InstrumentVoice::InstrumentVoiceContext vctx{
            .tempo = ctx->tempo,
            .pitchbendRangeSemitones = ctx->pitchbend_range_semitones,
        };
        polysynth::MidiEvent mev{};
        mev.type = static_cast<polysynth::MidiEvent::Type>(ev->type);
        mev.channel = ev->channel;
        if (mev.type == polysynth::MidiEvent::Type::kPitchBendChange)
        {
            mev.data.pitchBendChange.value = ev->pitch_bend;
        }
        else if (mev.type == polysynth::MidiEvent::Type::kControlChange)
        {
            mev.data.controlChange.id = ev->data1;
            mev.data.controlChange.value = ev->data2;
        }
        else if (mev.type == polysynth::MidiEvent::Type::kProgramChange)
        {
            mev.data.programChange.value = ev->data1;
        }
        else
        {
            mev.data.note.note = ev->data1;
            mev.data.note.velocity = ev->data2;
        }
        holder->voice.onMidiEvent(vctx, mev);
    }

    static void voiceOnParamChange(void* v, int paramId, bool hasValue, float value)
    {
        auto* holder = static_cast<VoiceHolder<VoiceClass>*>(v);
        polysynth::Patch::optional_paramvalue_t optVal =
          hasValue ? polysynth::Patch::optional_paramvalue_t(value) : std::nullopt;
        holder->voice.onParamChange(paramId, optVal);
    }

    static void render(void* v,
                       const PsynVoiceContext* ctx,
                       const PsynAudioInput* in,
                       float* outLeft,
                       float* outRight,
                       float* const* auxBuffers,
                       int auxCount)
    {
        auto* holder = static_cast<VoiceHolder<VoiceClass>*>(v);
        polysynth::InstrumentVoice::InstrumentVoiceContext vctx{
            .tempo = ctx->tempo,
            .pitchbendRangeSemitones = ctx->pitchbend_range_semitones,
        };
        polysynth::AudioBufferStereoRef outStereo{
            polysynth::AudioBufferRef(outLeft, polysynth::kAudioBufferSize),
            polysynth::AudioBufferRef(outRight, polysynth::kAudioBufferSize),
        };
        auto toBuffer = [](const float* left, const float* right)
        {
            std::optional<polysynth::AudioBufferStereoRef> buffer;
            if (left != nullptr && right != nullptr)
            {
                buffer = polysynth::AudioBufferStereoRef{
                    polysynth::AudioBufferRef(const_cast<float*>(left),
                                              polysynth::kAudioBufferSize),
                    polysynth::AudioBufferRef(const_cast<float*>(right),
                                              polysynth::kAudioBufferSize),
                };
            }
            return buffer;
        };
        std::optional<polysynth::AudioBufferStereoRef> inBuffer;
        if (in != nullptr)
        {
            inBuffer = toBuffer(in->left, in->right);
            vctx.sidechainBuffer = toBuffer(in->sidechain_left, in->sidechain_right);
        }
        if constexpr (requires {
                          holder->voice.renderVoiceAudio(
                            vctx,
                            std::optional<polysynth::AudioBufferStereoRef>{},
                            outStereo,
                            std::span<polysynth::AudioBufferRef>{});
                      })
        {
            polysynth::AudioBufferRef auxRefs[2] = {
                polysynth::AudioBufferRef(
                  (auxCount > 0 && auxBuffers != nullptr && auxBuffers[0] != nullptr)
                    ? auxBuffers[0]
                    : s_dummyAux[0],
                  polysynth::kAudioBufferSize),
                polysynth::AudioBufferRef(
                  (auxCount > 1 && auxBuffers != nullptr && auxBuffers[1] != nullptr)
                    ? auxBuffers[1]
                    : s_dummyAux[1],
                  polysynth::kAudioBufferSize),
            };
            holder->voice.renderVoiceAudio(
              vctx,
              inBuffer,
              outStereo,
              std::span<polysynth::AudioBufferRef>(auxRefs, 2));
        }
        else
        {
            holder->voice.renderVoiceAudio(vctx, inBuffer, outStereo);
        }
    }

    static void voiceRenderAudio(void* v,
                                 const PsynVoiceContext* ctx,
                                 float* outLeft,
                                 float* outRight,
                                 float* const* auxBuffers,
                                 int auxCount)
    {
        render(v, ctx, nullptr, outLeft, outRight, auxBuffers, auxCount);
    }

    static void voiceRenderAudioIn(void* v,
                                   const PsynVoiceContext* ctx,
                                   const PsynAudioInput* in,
                                   float* outLeft,
                                   float* outRight)
    {
        render(v, ctx, in, outLeft, outRight, nullptr, 0);
    }

    static void voiceRenderPostProcess(void* v,
                                       const PsynVoiceContext* ctx,
                                       float* outLeft,
                                       float* outRight)
    {
        auto* holder = static_cast<VoiceHolder<VoiceClass>*>(v);
        polysynth::InstrumentVoice::InstrumentVoiceContext vctx{
            .tempo = ctx->tempo,
            .pitchbendRangeSemitones = ctx->pitchbend_range_semitones,
        };
        polysynth::AudioBufferStereoRef outStereo{
            polysynth::AudioBufferRef(outLeft, polysynth::kAudioBufferSize),
            polysynth::AudioBufferRef(outRight, polysynth::kAudioBufferSize),
        };
        holder->voice.renderPostProcessAudio(vctx, outStereo);
    }

    static void voiceRenderGraph(void* v, float* outPoints, int count)
    {
        auto* holder = static_cast<VoiceHolder<VoiceClass>*>(v);
        holder->voice.renderGraph(std::span<float>(outPoints, static_cast<size_t>(count)));
    }

    static bool voiceIsActive(void* v)
    {
        return static_cast<VoiceHolder<VoiceClass>*>(v)->voice.isActive();
    }

    static bool voiceHandlesNote(void* v, uint8_t note)
    {
        return static_cast<VoiceHolder<VoiceClass>*>(v)->voice.handlesNote(note);
    }

    static bool voiceHandlesAnyNote(void* v)
    {
        return static_cast<VoiceHolder<VoiceClass>*>(v)->voice.handlesAnyNote();
    }

    static int voiceGetLastNote(void* v)
    {
        auto n = static_cast<VoiceHolder<VoiceClass>*>(v)->voice.getLastNote();
        return n.has_value() ? static_cast<int>(*n) : -1;
    }

    static bool voiceGetCurrentVelocity(void* v, float* outVel)
    {
        auto vel = static_cast<VoiceHolder<VoiceClass>*>(v)->voice.getCurrentNoteVelocity();
        if (vel.has_value())
        {
            *outVel = *vel;
            return true;
        }
        return false;
    }

    static void voiceClearPreviousNotes(void* v)
    {
        static_cast<VoiceHolder<VoiceClass>*>(v)->voice.clearPreviousNotes();
    }

    static void voiceReset(void* v)
    {
        static_cast<VoiceHolder<VoiceClass>*>(v)->voice.reset();
    }

    static const PsynPluginApi* getApi()
    {
        static const PsynPluginApi kApi = {
            .create_metadata = &createMetadata,
            .format_param = &formatParam,
            .create_voice = &createVoice,
            .destroy_voice = &destroyVoice,
            .voice_set_patch = &voiceSetPatch,
            .voice_on_midi_event = &voiceOnMidiEvent,
            .voice_on_param_change = &voiceOnParamChange,
            .voice_render_audio = &voiceRenderAudio,
            .voice_render_post_process = &voiceRenderPostProcess,
            .voice_render_graph = &voiceRenderGraph,
            .voice_is_active = &voiceIsActive,
            .voice_handles_note = &voiceHandlesNote,
            .voice_handles_any_note = &voiceHandlesAnyNote,
            .voice_get_last_note = &voiceGetLastNote,
            .voice_get_current_velocity = &voiceGetCurrentVelocity,
            .voice_clear_previous_notes = &voiceClearPreviousNotes,
            .voice_reset = &voiceReset,
            .voice_render_audio_in = &voiceRenderAudioIn,
        };
        return &kApi;
    }

  private:
    static inline float s_dummyAux[2][polysynth::kAudioBufferSize]{};
};

} // namespace psyn_bridge

#define PSYN_REGISTER_CPP_VOICE(VoiceClass)                                \
    extern "C" const PsynPluginApi* psyn_plugin_entry(const PsynHostApi* host) \
    {                                                                      \
        psyn_bridge::g_host = host;                                        \
        return psyn_bridge::PluginAdapter<VoiceClass>::getApi();           \
    }
