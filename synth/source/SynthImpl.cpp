#include "SynthImpl.h"
#include "MixerImpl.h"
#include "PolySynth/FileSystem.h"
#include "PolySynth/MidiSink.h"
#include "PolySynth/SampleStore.h"
#include "grid/GridImpl.h"
#include "grid/GridMapperImpl.h"
#include "grid/GridMapperStoreImpl.h"
#include "patch/PatchPoolImpl.h"
#include "patch/PatchStoreFilesystem.h"
#include "sample/SamplePool.h"
#include "sample/SampleStoreImpl.h"
#include "sequencer/impl/SequencerEngineImpl.h"
#include "synths/PsynLoader.h"

#include "PolyDSP/misc/Amplifier.h"
#include "mocks/LambdaSequencerDataProvider.h"
#include "sequencer/impl/TransportSyncImpl.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <ranges>

// Using shared_ptr extensively in the audio thread,
// don't want them to use locks
static_assert(__gnu_cxx::__default_lock_policy == __gnu_cxx::_S_single ||
              __gnu_cxx::__default_lock_policy == __gnu_cxx::_S_atomic);

namespace polysynth
{

std::shared_ptr<Synth> polysynth::Synth::createSynth(Allocator largeBuffersAlloc)
{
    return SynthImpl::createSynth(std::move(largeBuffersAlloc));
}

std::shared_ptr<Synth> SynthImpl::createSynth(Allocator largeBuffersAlloc)
{
    return std::shared_ptr<SynthImpl>(new SynthImpl(std::move(largeBuffersAlloc)));
}

SynthImpl::SynthImpl(Allocator largeBuffersAlloc_)
  : instrumentTypeRegistry(std::make_shared<InstrumentTypeRegistry>())
  , largeBuffersAlloc(std::move(largeBuffersAlloc_))
{
    InstrumentTypeRegistry::registerInstrumentTypes(*instrumentTypeRegistry, largeBuffersAlloc);
}

std::unique_ptr<PatchStore> SynthImpl::createPatchStore(FileSystemId id,
                                                        bool instrumentTypeFolderStructure)
{
    assert(fileSystems.contains(id));

    return std::make_unique<PatchStoreFilesystem>(
      fileSystems[id], instrumentTypeRegistry, samplePool, instrumentTypeFolderStructure);
}

std::unique_ptr<PatchPool> SynthImpl::createPatchPool(
  std::shared_ptr<PatchStore> store,
  int slotCount,
  std::function<tiny::optional<int>(const std::string&)> nameToIndex)
{
    return std::make_unique<PatchPoolImpl>(store, slotCount, nameToIndex);
}

std::shared_ptr<Instrument> SynthImpl::createInstrument(int instrumentId)
{
    return InstrumentImpl::create(instrumentId, instrumentTypeRegistry);
}

std::unique_ptr<Mixer> SynthImpl::createMixer(int channels, int sends)
{
    return std::make_unique<MixerImpl>(channels, sends);
}

std::unique_ptr<GridMapper> SynthImpl::createGridMapper()
{
    auto gridMapper = std::make_unique<GridMapperImpl>(sequencer, largeBuffersAlloc, getShared());
    registerComponent(*gridMapper);
    return gridMapper;
}

std::unique_ptr<GridMapperStore> SynthImpl::createGridMapperStore(FileSystemId id)
{
    assert(fileSystems.contains(id));

    return std::make_unique<GridMapperStoreImpl>(fileSystems[id], largeBuffersAlloc);
}

std::unique_ptr<Grid> SynthImpl::createGrid()
{
    auto grid = std::make_unique<GridImpl>();
    registerComponent(*grid);
    return grid;
}

std::unique_ptr<SampleStore> SynthImpl::createSampleStore(FileSystemId id)
{
    return std::make_unique<SampleStoreImpl>(fileSystems.at(id), largeBuffersAlloc);
}

std::shared_ptr<Sequencer> SynthImpl::createSequencer()
{
    assert(!sequencer);
    sequencer = std::make_unique<SequencerEngineImpl>(largeBuffersAlloc, getShared());
    registerComponent(*sequencer->impl);
    return sequencer;
}

void SynthImpl::setupInstruments(std::span<std::shared_ptr<polysynth::Instrument>> newInstruments,
                                 const InstrumentImpl::InstrumentContext& ctx)
{
    for (int instrumentId = 0; instrumentId < std::ssize(newInstruments); instrumentId++)
    {
        auto& newInstrument = newInstruments[instrumentId];
        if (instrumentId >= std::ssize(instruments) || newInstrument != instruments[instrumentId])
        {
            newInstrument->impl->setup(ctx);
            for (auto [_, source] : midiSources)
            {
                newInstrument->connect(*source);
                if (sequencer)
                {
                    newInstrument->connect(*sequencer->impl->getSyncMidiSource());
                }
            }

            if (instrumentId >= std::ssize(instruments))
            {
                instruments.push_back(std::move(newInstrument));
            }
            else
            {
                instruments[instrumentId] = std::move(newInstrument);
            }
        }
    }
}

void SynthImpl::setupEffects(std::span<std::shared_ptr<polysynth::Instrument>> newEffects,
                             const InstrumentImpl::InstrumentContext& ctx)
{
    for (auto& effect : newEffects)
    {
        effect->impl->setup(ctx);
        if (sequencer)
        {
            effect->connect(*sequencer->impl->getSyncMidiSource());
        };
    }
}

void SynthImpl::setup(std::span<std::shared_ptr<polysynth::Instrument>> newInstruments,
                      std::span<std::shared_ptr<polysynth::Instrument>> newEffects,
                      std::shared_ptr<Mixer> newMixer,
                      TempoProvider tempoProvider)
{
    assert(sequencer || tempoProvider);

    InstrumentImpl::InstrumentContext ctx = { .midiOutputs = midiOutputs,
                                              .tempoProvider = tempoProvider
                                                                 ? std::move(tempoProvider)
                                                                 : sequencer->getTempoProvider() };

    if (sequencer)
    {
        for (auto [_, source] : midiSources)
        {
            sequencer->impl->connect(*source);
        }
    }

    setupInstruments(newInstruments, ctx);
    setupEffects(newEffects, ctx);

    assert(std::ssize(midiOutputs) <= 1);
    if (!midiOutputs.empty())
    {
        sequencer->impl->setSyncMidiOutput(midiOutputs.begin()->second);
    }

    mixer = std::move(newMixer);
}

void SynthImpl::disconnectInstrument(int instrumentId)
{
    instruments[instrumentId] = nullptr;
}

void SynthImpl::renderAudioBuffer(AudioBufferStereoRef buffer)
{
    assert(mixer);

    mixer->impl->renderAudioBuffer(buffer);
}

void SynthImpl::renderAudioBuffer(AudioBufferStereoRef bufferMain,
                                  std::span<AudioBufferRef> bufferAux,
                                  int instrumentId,
                                  std::optional<AudioBufferStereoRef> inputBuffer,
                                  std::optional<AudioBufferRef> sidechainBuffer)
{
    instruments[instrumentId]->impl->renderAudioBuffer(
      inputBuffer, bufferMain, bufferAux, sidechainBuffer);
}

bool SynthImpl::isInstrumentIdle(int instrumentId) const
{
    if (instrumentId < 0 || instrumentId >= std::ssize(instruments) ||
        instruments[instrumentId] == nullptr)
    {
        return true;
    }
    return instruments[instrumentId]->impl->isIdle();
}

bool SynthImpl::prepareSilentInstrument(int instrumentId)
{
    if (instrumentId < 0 || instrumentId >= std::ssize(instruments) ||
        instruments[instrumentId] == nullptr)
    {
        return true;
    }
    return instruments[instrumentId]->impl->prepareSilentBlock();
}

void SynthImpl::updateAudio()
{
    std::ranges::for_each(getComponents(), [](auto& component) { component->updateAudio(); });
}

void SynthImpl::updateMain()
{
    std::ranges::for_each(getComponents(), [](auto& component) { component->updateMain(); });
}

void SynthImpl::addMidiSource(const std::string& id, std::shared_ptr<MidiSource> input)
{
    midiSources[id] = std::move(input);
}

void SynthImpl::addMidiSink(const std::string& id, std::unique_ptr<MidiSink> output)
{
    midiOutputs[id] = std::move(output);
}

void SynthImpl::addFileSystem(FileSystemId id, std::unique_ptr<FileSystem> fileSystem)
{
    assert(!fileSystems.contains(id));
    fileSystems[id] = std::move(fileSystem);
    if (id == FileSystemId::kSynths)
    {
        PsynLoader::scanAndRegister(
          *fileSystems[id], *instrumentTypeRegistry, largeBuffersAlloc, false);
    }
    else if (id == FileSystemId::kSamples)
    {
        PsynLoader::setSamplesFileSystem(*fileSystems[id]);
    }
}

void SynthImpl::initSamplePool(size_t size, std::shared_ptr<SampleStore> sampleStore)
{
    samplePool = std::make_shared<SamplePool>(largeBuffersAlloc, size, std::move(sampleStore));
}

std::shared_ptr<SynthImpl> SynthImpl::getShared()
{
    return std::static_pointer_cast<SynthImpl>(ComponentRegistry::shared_from_this());
}

const Patch::Sequencer& SynthImpl::getSequencerData(int instrumentId) const
{
    return instruments[instrumentId]->impl->getPatchAssigned()->getSequencer();
}

std::string SynthImpl::getInstrumentName(int instrumentTypeId)
{
    auto instrumentType = instrumentTypeRegistry->getInstrumentType(instrumentTypeId);
    if (!instrumentType)
    {
        return {};
    }
    return instrumentType->name;
}

std::vector<std::string_view> SynthImpl::getInstrumentTypeNames()
{
    std::vector<std::string_view> names;

    std::ranges::transform(instrumentTypeRegistry->getInstrumentTypes(),
                           std::back_inserter(names),
                           [](const auto& idType)
                           { return std::string_view{ idType.second->name }; });

    return names;
}

std::vector<int> SynthImpl::getInstrumentTypeIds()
{
    std::vector<int> ids;
    ids.reserve(instrumentTypeRegistry->getInstrumentTypes().size());
    std::ranges::transform(instrumentTypeRegistry->getInstrumentTypes(),
                           std::back_inserter(ids),
                           [](const auto& idType) { return idType.first; });

    return ids;
}
}
