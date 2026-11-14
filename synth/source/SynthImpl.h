#pragma once

#include "Component.h"
#include "PolySynth/MidiSource.h"
#include "PolySynth/Synth.h"
#include "SequencerDataProvider.h"
#include "instrument/InstrumentImpl.h"
#include "instrument/InstrumentTypeRegistry.h"
#include "sequencer/SequencerEngine.h"

#include <memory>
#include <unordered_map>

namespace polysynth
{
class SamplePool;
class SampleStore;

class SynthImpl
  : public Synth
  , public ComponentRegistry
  , public SequencerDataProvider
{
  public:
    static std::shared_ptr<Synth> createSynth(Allocator largeBuffersAlloc);

    std::unique_ptr<PatchPool> createPatchPool(
      std::shared_ptr<PatchStore> store,
      int slotCount,
      std::function<tiny::optional<int>(const std::string&)> nameToIndex) override;
    std::unique_ptr<PatchStore> createPatchStore(FileSystemId id,
                                                 bool instrumentTypeFolderStructure) override;
    std::shared_ptr<Instrument> createInstrument(int instrumentId) override;
    std::unique_ptr<Mixer> createMixer(int channels, int sends) override;
    std::unique_ptr<GridMapper> createGridMapper() override;
    std::unique_ptr<GridMapperStore> createGridMapperStore(FileSystemId id) override;
    std::unique_ptr<Grid> createGrid() override;
    std::unique_ptr<SampleStore> createSampleStore(FileSystemId id) override;
    std::shared_ptr<Sequencer> createSequencer() override;

    void addMidiSource(const std::string& id, std::shared_ptr<MidiSource> input) override;
    void addMidiSink(const std::string& id, std::unique_ptr<MidiSink> output) override;
    void addFileSystem(FileSystemId id, std::unique_ptr<FileSystem> fileSystem) override;

    void initSamplePool(size_t size, std::shared_ptr<SampleStore> sampleStore) override;

    std::string getInstrumentName(int instrumentTypeId) override;

    std::vector<std::string_view> getInstrumentTypeNames() override;
    std::vector<int> getInstrumentTypeIds() override;

    void setup(std::span<std::shared_ptr<polysynth::Instrument>> newInstruments,
               std::span<std::shared_ptr<polysynth::Instrument>> newEffects,
               std::shared_ptr<Mixer> newMixer,
               TempoProvider tempoProvider) override;
    void disconnectInstrument(int instrumentId) override;

    void renderAudioBuffer(AudioBufferStereoRef buffer) override;
    void renderAudioBuffer(AudioBufferStereoRef bufferMain,
                           std::span<AudioBufferRef> bufferAux,
                           int instrumentId,
                           std::optional<AudioBufferStereoRef> inputBuffer,
                           std::optional<AudioBufferRef> sidechainBuffer) override;

    void updateAudio() override;
    void updateMain() override;

    [[nodiscard]] const Patch::Sequencer& getSequencerData(int instrumentId) const override;

  private:
    uint32_t sampleCounter = 0;

    std::unordered_map<std::string, std::shared_ptr<MidiSource>> midiSources;
    std::unordered_map<std::string, std::shared_ptr<MidiSink>> midiOutputs;
    std::unordered_map<FileSystemId, std::shared_ptr<FileSystem>> fileSystems;

    std::shared_ptr<InstrumentTypeRegistry> instrumentTypeRegistry;

    std::vector<std::shared_ptr<Instrument>> instruments;

    std::shared_ptr<Mixer> mixer = nullptr;
    std::shared_ptr<Sequencer> sequencer;

    std::shared_ptr<SamplePool> samplePool;

    Allocator largeBuffersAlloc;

    explicit SynthImpl(Allocator largeBuffersAlloc);

    std::shared_ptr<SynthImpl> getShared();

    void setupInstruments(std::span<std::shared_ptr<polysynth::Instrument>> newInstruments,
                          const InstrumentImpl::InstrumentContext& ctx);
    void setupEffects(std::span<std::shared_ptr<polysynth::Instrument>> newEffects,
                      const InstrumentImpl::InstrumentContext& ctx);
};
}
