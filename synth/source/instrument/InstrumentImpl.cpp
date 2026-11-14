#include "InstrumentImpl.h"

#include "PolyUtil/math.h"
#include "PolyUtil/misc.h"

#include "grid/MidiUtils.h"

#include <algorithm>
#include <boost/range/algorithm_ext.hpp>
#include <cassert>
#include <cmath>
#include <ranges>
#include <type_traits>

namespace polysynth
{

std::shared_ptr<InstrumentImpl> InstrumentImpl::create(
  int instrumentId,
  std::shared_ptr<InstrumentTypeRegistry> instrumentTypeRegistry)
{
    if (instrumentTypeRegistry->getInstrumentType(instrumentId) == nullptr)
    {
        instrumentId = instrumentTypeRegistry->getFallbackSynthTypeId();
    }
    return std::shared_ptr<InstrumentImpl>(
      new InstrumentImpl(instrumentId, std::move(instrumentTypeRegistry)));
}

InstrumentImpl::InstrumentImpl(int instrumentId,
                               std::shared_ptr<InstrumentTypeRegistry> instrumentTypeRegistry_)
  : instrumentId(instrumentId)
  , instrumentTypeRegistry(std::move(instrumentTypeRegistry_))
{
    this->impl = this;
}

InstrumentImpl::~InstrumentImpl()
{
    if (patchAssigned != nullptr)
    {
        patchAssigned->unsubscribeParamUpdates(*this);
    }
}

int InstrumentImpl::getVoiceCount() const
{
    return static_cast<int>(voices.size());
};

void InstrumentImpl::setVoiceCount(int voiceCount)
{
    recreateVoices(voiceCount);
}

/**
 * Recreate AudioVoice instances as needed, based on provided voiceCount
 * and patch assigned (its instrument type).
 */
bool InstrumentImpl::recreateVoices(int voiceCount)
{
    assert(voiceCount <= kMaxInstrumentVoices);
    assert(patchAssigned != nullptr);

    auto newInstrumentType =
      instrumentTypeRegistry->getInstrumentType(patchAssigned->getMetadata().instrumentTypeId);
    if (newInstrumentType == nullptr)
    {
        newInstrumentType =
          instrumentTypeRegistry->getInstrumentType(instrumentTypeRegistry->getFallbackSynthTypeId());
    }
    assert(newInstrumentType != nullptr);

    if (voicesInstrumentTypeId != patchAssigned->getMetadata().instrumentTypeId)
    {
        voices.clear();
        voicesInstrumentTypeId = patchAssigned->getMetadata().instrumentTypeId;
    }

    auto delta = voiceCount - std::ssize(voices);

    if (delta > 0)
    {
        for (; delta > 0; delta--)
        {
            auto voice = newInstrumentType->createVoice(static_cast<int>(this->voices.size()));
            if (patchAssigned != nullptr)
            {
                voice->setPatch(patchAssigned);
            }
            this->voices.emplace_back(Voice{ .voice = std::move(voice) });
        }

        if (patchAssigned != nullptr)
        {
            updateAllPatchParams(Patch::ParamChangeSource::kPatchAssign);
        }

        return true;
    }

    if (delta < 0)
    {
        this->voices.resize(voiceCount);
        return true;
    }

    return false;
}

template<class Func>
void InstrumentImpl::executeOnSourceVoices(int sourceId, Func func)
{
    std::ranges::for_each(voices,
                          [sourceId, &func](auto& voice)
                          {
                              if (voice.voice->handlesAnyNote() && !voice.noteSourceIds.empty() &&
                                  sourceId == voice.noteSourceIds.back())
                              {
                                  func(voice);
                              }
                          });
}

template<class Func>
void InstrumentImpl::executeOnNoteVoice(uint8_t note, Func func)
{
    std::ranges::for_each(voices,
                          [note, &func](auto& voice)
                          {
                              if (voice.voice->handlesNote(note))
                              {
                                  func(voice);
                              }
                          });
}

template<class Func>
void InstrumentImpl::executeOnAllVoices(Func func)
{
    std::ranges::for_each(voices, [&func](auto& voice) { func(voice); });
}

void InstrumentImpl::onPatchReset()
{
    for (auto& voice : voices)
    {
        voice.voice->reset();
        voice.noteSourceIds.clear();
        voice.ticksSinceNoteOn = 0;
    }
}

void InstrumentImpl::reset()
{
    onPatchReset();
    if (patchAssigned != nullptr)
    {
        updateAllPatchParams(Patch::ParamChangeSource::kPatchAssign);
    }
}

void InstrumentImpl::assignPatch(std::shared_ptr<Patch> patch)
{
    if (patchAssigned != nullptr)
    {
        patchAssigned->unsubscribeParamUpdates(*this);
    }

    patch->subscribeParamUpdates(shared_from_this());

    patchAssigned = std::move(patch);

    auto voicesRecreated = recreateVoices(static_cast<int>(std::ssize(voices)));

    for (auto& voice : voices)
    {
        voice.voice->setPatch(patchAssigned);
    }

    if (!voicesRecreated)
    {
        // otherwise it was done in recreateVoices
        updateAllPatchParams(Patch::ParamChangeSource::kPatchAssign);
    }
}

void InstrumentImpl::updateAllPatchParams(Patch::ParamChangeSource source)
{
    assert(patchAssigned != nullptr);
#ifdef __clang__
    // placeholder for clang-tidy because of a bug/incompatibility in clang
    (void)patchAssigned.get();
    (void)source;
#else
    std::ranges::for_each(
      std::views::iota(0, std::ssize(patchAssigned->getMetadata().params)),
      [this, source](auto paramId) {
          onParamChange(paramId, patchAssigned->getParamFinalValue(paramId), source, std::nullopt);
      });
#endif
}

void InstrumentImpl::connect(MidiSource& midiSource)
{
    auto midiEventsCallback = std::make_shared<MidiSource::MidiEventCallback>(
      [this, inputType = midiSource.getType()](const MidiEvent& event, int sourceId)
      {
          if (midiChannel == MidiEvent::kChannelOmni || event.channel == MidiEvent::kChannelOmni ||
              midiChannel == event.channel)
          {
              this->onMidiEvent(event, inputType, sourceId);
          }
      });
    midiSource.subscribeMidiEvents(instrumentId, midiEventsCallback);
    midiEventsCallbacks.emplace_back(std::move(midiEventsCallback));
}

void InstrumentImpl::setMidiChannel(int channel)
{
    assert(channel >= 0);
    midiChannel = channel;
}

int InstrumentImpl::getMidiChannel() const
{
    return midiChannel;
}

void InstrumentImpl::setLocalMode(LocalMode mode)
{
    localMode = mode;
}

void InstrumentImpl::setup(const InstrumentContext& context)
{
    midiSink = !context.midiOutputs.empty() ? context.midiOutputs.begin()->second : nullptr;
    tempoProvider = context.tempoProvider;
}

/**
 * Voice allocation algorithm, searches for a voice based on criteria,
 * executes in steps until success:
 * 1. not playing - amp envelope finished release stage
 * 2. received note off, longest time since note on
 *   * priority for the same note value
 * 3. longest time since note on
 */
InstrumentImpl::Voice& InstrumentImpl::findVoiceForNote(uint8_t newNote)
{
#ifdef __clang__
    // placeholder for clang-tidy because of a bug/incompatibility in clang
    (void)newNote;
    return voices[0];
#else
    {
        auto voiceIt =
          std::ranges::find_if(voices, [](const auto& voice) { return !voice.voice->isActive(); });

        if (voiceIt != voices.end())
        {
            return *voiceIt;
        }

        voiceIt = std::ranges::find_if(voices,
                                       [newNote](const auto& voice) {
                                           return !voice.voice->handlesAnyNote() &&
                                                  voice.voice->getLastNote() == newNote;
                                       });

        if (voiceIt != voices.end())
        {
            return *voiceIt;
        }
    }

    auto inactiveVoices =
      voices |
      std::ranges::views::filter([](const auto& voice) { return !voice.voice->handlesAnyNote(); });
    if (!inactiveVoices.empty())
    {
        auto voiceIt = std::ranges::max_element(
          inactiveVoices,
          [](const auto& v1, const auto& v2) { return v1.ticksSinceNoteOn < v2.ticksSinceNoteOn; });

        assert(voiceIt != inactiveVoices.end());
        return *voiceIt;
    }

    auto voiceIt = std::ranges::max_element(voices,
                                            [](const auto& v1, const auto& v2)
                                            { return v1.ticksSinceNoteOn < v2.ticksSinceNoteOn; });

    assert(voiceIt != voices.end());
    return *voiceIt;
#endif
}

void InstrumentImpl::onMidiEvent(const MidiEvent& event, MidiSource::Type sourceType, int sourceId)
{
    auto isSyncMessage = MidiUtils::isSyncMessage(event.type);
    if (midiSink != nullptr && sourceType == MidiSource::Type::kInternalKeyboard && !isSyncMessage)
    {
        MidiEvent eventOut = event;
        eventOut.channel = midiChannel;
        midiSink->onMidiEvent(eventOut);
    }

    // ignore sync messages from external sources. They come always from sequencer (internal)
    if (isSyncMessage && sourceType == MidiSource::Type::kExternalPort)
    {
        return;
    }

    if (!isSyncMessage && sourceType == MidiSource::Type::kInternalKeyboard &&
        getLocalMode() == LocalMode::kOff)
    {
        return;
    }

    if (voices.empty())
    {
        return;
    }

    switch (event.type)
    {
        case MidiEvent::Type::kStart:
            executeOnAllVoices([&event, this](auto& voice)
                               { voice.voice->onMidiEvent({ getTempo() }, event); });
            return;
        case MidiEvent::Type::kClock:
            executeOnAllVoices([&event, this](auto& voice)
                               { voice.voice->onMidiEvent({ getTempo() }, event); });
            return;
        case MidiEvent::Type::kNoteOn:
            handleNoteOn(event, sourceId);
            break;
        case MidiEvent::Type::kNoteOff:
            handleNoteOff(event, sourceId);
            break;
        case MidiEvent::Type::kControlChange:
            handleMidiCC(event.data.controlChange);
            break;
        case MidiEvent::Type::kPolyAftertouch:
            handleAftertouch(event.data.note, sourceId);
            break;
        case MidiEvent::Type::kChannelAftertouch:
            break;
        case MidiEvent::Type::kPitchBendChange:
            executeOnAllVoices([&event, this](auto& voice)
                               { voice.voice->onMidiEvent({ getTempo() }, event); });
            break;
        case MidiEvent::Type::kProgramChange:
            executeOnSourceVoices(sourceId,
                                  [&event, this](auto& voice)
                                  { voice.voice->onMidiEvent({ getTempo() }, event); });
            break;
        default:
            break;
    }
}

void InstrumentImpl::onParamChange(int paramId,
                                   Patch::optional_paramvalue_t newValue,
                                   Patch::ParamChangeSource source,
                                   tiny_optional<int> voiceId)
{
    if (voiceId)
    {
        assert(voiceId < std::ssize(voices));
        voices[*voiceId].voice->onParamChange(paramId, newValue);
    }
    else
    {
        for (auto& voice : voices)
        {
            voice.voice->onParamChange(paramId, newValue);
        };
    }

    // if update is sent per voice send CC only once
    if (patchAssigned != nullptr && (!voiceId || voiceId == 0))
    {
        const auto macrosAffectParamCC = patchAssigned->getMetadata().macrosAffectParamCC;

        if (midiSink != nullptr && newValue &&
            (source == Patch::ParamChangeSource::kKnob ||
             (source == Patch::ParamChangeSource::kMacro && macrosAffectParamCC)))
        {
            sendParamCC(paramId,
                        macrosAffectParamCC ? *newValue : *patchAssigned->getParamValue(paramId));
        }
    }
}

void InstrumentImpl::sendParamCC(int paramId, Patch::paramvalue_t newValue)
{
    if (!midiSink)
    {
        return;
    }

    const auto& metadata = patchAssigned->getMetadata().params[paramId];

    if (!metadata.midiCC)
    {
        return;
    }

    auto value = scaleLin(newValue,
                          metadata.minValue,
                          metadata.maxValue,
                          MidiEvent::Data::ControlChange::kMinValue,
                          MidiEvent::Data::ControlChange::kMaxValue);

    MidiEvent event = { .type = MidiEvent::Type::kControlChange,
                        .channel = static_cast<uint8_t>(midiChannel),
                        .data = MidiEvent::Data{
                          .controlChange = MidiEvent::Data::ControlChange{
                            .id = *metadata.midiCC, .value = static_cast<uint8_t>(value) } } };

    midiSink->onMidiEvent(event);
}

void InstrumentImpl::handleMidiCC(const MidiEvent::Data::ControlChange& controlChange)
{
    if (patchAssigned == nullptr)
    {
        return;
    }

    if (controlChange.id == MidiEvent::Data::ControlChange::kAllNotesOffCC)
    {
        executeOnAllVoices(
          [this](auto& voice)
          {
              if (voice.voice->handlesAnyNote())
              {
                  voice.voice->clearPreviousNotes();
                  auto note = voice.voice->getLastNote();
                  assert(note);
                  if (note)
                  {
                      auto event = MidiEvent{ .type = MidiEvent::Type::kNoteOff,
                                              .channel = static_cast<uint8_t>(midiChannel),
                                              .data = MidiEvent::Data{
                                                .note = MidiEvent::Data::Note{
                                                  .note = static_cast<uint8_t>(*note),
                                                  .velocity = 0 } } };

                      voice.voice->onMidiEvent({ getTempo() }, event);
                  }
                  voice.noteSourceIds.clear();
              }
          });

        return;
    }

    if (controlChange.id >= Patch::Metadata::Macro::kMacroCCStart &&
        controlChange.id < Patch::Metadata::Macro::kMacroCCStart + kUserMacroCount)
    {
        int macroId = controlChange.id - Patch::Metadata::Macro::kMacroCCStart;
        const auto& macroDef = patchAssigned->getMetadata().macros[macroId];

        // for bipolar macros, kMidValue of CC lands with 0 macro value
        auto ccValue = macroDef.bipolar ? std::min(static_cast<int>(controlChange.value),
                                                   MidiEvent::Data::ControlChange::kMidValue * 2)
                                        : static_cast<int>(controlChange.value);

        auto value = scaleLin(ccValue,
                              MidiEvent::Data::ControlChange::kMinValue,
                              macroDef.bipolar ? MidiEvent::Data::ControlChange::kMidValue * 2
                                               : MidiEvent::Data::ControlChange::kMaxValue,
                              macroDef.bipolar ? -Patch::kMaxMacroValue : 0,
                              Patch::kMaxMacroValue);

        patchAssigned->setMacroValue(macroId, value, std::nullopt);

        return;
    }

    const auto& param =
      std::ranges::find_if(patchAssigned->getMetadata().params,
                           [&controlChange](const auto& param)
                           { return param.midiCC && *param.midiCC == controlChange.id; });

    if (param != patchAssigned->getMetadata().params.end())
    {
        auto value = scaleLin(controlChange.value,
                              MidiEvent::Data::ControlChange::kMinValue,
                              MidiEvent::Data::ControlChange::kMaxValue,
                              0.0f,
                              1.0f);

        patchAssigned->setParamValueEditMapped(
          static_cast<int>(param - patchAssigned->getMetadata().params.begin()),
          value,
          Patch::ParamChangeSource::kMidiPort);
    }
}

void InstrumentImpl::handleAftertouch(const MidiEvent::Data::Note& note, int sourceId)
{
    if (patchAssigned)
    {
        executeOnSourceVoices(sourceId,
                              [&note, this](auto& voice)
                              {
                                  patchAssigned->setMacroValue(
                                    *Patch::SystemMacroId::kPressure,
                                    static_cast<Patch::paramvalue_t>(note.velocity) *
                                      Patch::kMaxMacroValue / MidiEvent::Data::Note::kMaxVelocity,
                                    &voice - voices.data());
                              });
    }
}

void InstrumentImpl::handleNoteOn(const MidiEvent& event, int sourceId)
{
    auto& voice = findVoiceForNote(event.data.note.note);
    int voiceIdx = static_cast<int>(std::distance(voices.data(), &voice));

    if (patchAssigned)
    {
        patchAssigned->setMacroValue(*Patch::SystemMacroId::kVelocity,
                                     static_cast<Patch::paramvalue_t>(event.data.note.velocity) *
                                       Patch::kMaxMacroValue / MidiEvent::Data::Note::kMaxVelocity,
                                     voiceIdx);
    }

    voice.voice->onMidiEvent({ getTempo() }, event);

    auto& noteSourceIds = voice.noteSourceIds;
    if (noteSourceIds.size() == decltype(voice.noteSourceIds)::capacity())
    {
        noteSourceIds.erase(noteSourceIds.begin());
    }
    noteSourceIds.push_back(sourceId);
    voice.ticksSinceNoteOn = 0;

    for (auto& voice : voices)
    {
        voice.ticksSinceNoteOn++;
    }
}

void InstrumentImpl::handleNoteOff(const MidiEvent& event, int sourceId)
{
    executeOnNoteVoice(event.data.note.note,
                       [&event, sourceId, this](auto& voice)
                       {
                           voice.voice->onMidiEvent({ getTempo() }, event);
                           boost::range::remove_erase(voice.noteSourceIds, sourceId);

                           auto velocity = voice.voice->getCurrentNoteVelocity();
                           if (velocity && patchAssigned)
                           {
                               int voiceIdx =
                                 static_cast<int>(std::distance(voices.data(), &voice));
                               patchAssigned->setMacroValue(*Patch::SystemMacroId::kVelocity,
                                                            *velocity * Patch::kMaxMacroValue,
                                                            voiceIdx);
                           }
                       });
}

void InstrumentImpl::renderAudioBuffer(std::optional<AudioBufferStereoRef> inputBuffer,
                                       AudioBufferStereoRef outputBuffer,
                                       std::span<AudioBufferRef> auxOutBuffer,
                                       std::optional<AudioBufferStereoRef> sidechainBuffer)
{
    InstrumentVoice::InstrumentVoiceContext ctx = { getTempo() };
    ctx.sidechainBuffer = sidechainBuffer;

    for (auto& auxBuf : auxOutBuffer)
    {
        std::ranges::fill(auxBuf, 0.0f);
    }

    if (voices.size() == 1)
    {
        voices[0].voice->renderVoiceAudio(ctx, inputBuffer, outputBuffer, auxOutBuffer);
    }
    else
    {
        std::ranges::fill(outputBuffer.at(0), 0.0f);
        std::ranges::fill(outputBuffer.at(1), 0.0f);

        for (auto& voice : voices)
        {
            voice.voice->renderVoiceAudio(
              ctx,
              inputBuffer,
              std::array{ AudioBufferRef(tempBuffer[0]), AudioBufferRef(tempBuffer[1]) },
              auxOutBuffer);

            for (const auto& pair : { std::make_pair(outputBuffer.at(0), tempBuffer[0]),
                                      std::make_pair(outputBuffer.at(1), tempBuffer[1]) })
            {
                const auto* it = pair.second.begin();
                for (auto& value : pair.first)
                {
                    value += *it++; // NOLINT(*-pro-bounds-pointer-arithmetic)
                };
            }
        };
    }
    if (!voices.empty())
    {
        voices[0].voice->renderPostProcessAudio(ctx, outputBuffer);
    }
}
float InstrumentImpl::getTempo() const
{
    if (tempoProvider != nullptr)
    {
        return tempoProvider().value_or(kDefaultTempo);
    }
    return kDefaultTempo;
}

}
