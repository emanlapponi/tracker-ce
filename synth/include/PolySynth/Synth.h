#pragma once

#include <functional>
#include <memory>
#include <span>
#include <string>

#include "config.h"

#include "PolyUtil/misc.h"

#include "PolySynth/Musicology.h"
#include "PolySynth/Sequencer.h"

namespace polysynth
{
class PatchPool;
class PatchStore;
class Instrument;
class Grid;
class GridMapper;
class GridMapperStore;
class MidiSource;
class MidiSink;
class FileSystem;
class Mixer;
class SampleStore;
class Sequencer;

/**
 * Synth is the entry point of the PolySynth.
 *
 * Responsible for creation and management of all components
 * (instruments, grid, ...).
 * Also handles and dispatches main loop update events for
 * both audio and main threads.
 *
 * Examples of the API usage: see docs/design.md#examples
 */
class Synth
{
  public:
    enum class FileSystemId
    {
        kWorkspace,
        kGrid,
        kPatches,
        kSamples,
        kSynths,
    };

    using Allocator = std::function<void*(size_t bytes)>;

    static std::shared_ptr<Synth> createSynth(Allocator largeBuffersAlloc);

    virtual std::unique_ptr<PatchStore> createPatchStore(FileSystemId id,
                                                         bool instrumentTypeFolderStructure) = 0;

    /**
     * Creates PatchPool that keeps patches in memory.
     * Size of the pool (maximum amount of patches stored) is limited
     * and the pool releases patches when needed based on
     * longest-not-used logic.
     *
     * @param store store to load patches from
     * @param slotCount size of the pool
     * @param nameToIndex mapping of patch name to pool index
     */
    virtual std::unique_ptr<PatchPool> createPatchPool(
      std::shared_ptr<PatchStore> store,
      int slotCount,
      std::function<tiny::optional<int>(const std::string&)> nameToIndex) = 0;
    virtual std::shared_ptr<Instrument> createInstrument(int instrumentId) = 0;
    virtual std::unique_ptr<Mixer> createMixer(int channels, int sends) = 0;
    virtual std::unique_ptr<Grid> createGrid() = 0;
    virtual std::unique_ptr<GridMapperStore> createGridMapperStore(FileSystemId id) = 0;
    virtual std::unique_ptr<GridMapper> createGridMapper() = 0;
    virtual std::unique_ptr<SampleStore> createSampleStore(FileSystemId id) = 0;
    virtual std::shared_ptr<Sequencer> createSequencer() = 0;

    /**
     * Provide midi input that can be used by the synth.
     */
    virtual void addMidiSource(const std::string& id, std::shared_ptr<MidiSource> source) = 0;

    /**
     * Provide midi output that can be used by the synth.
     */
    virtual void addMidiSink(const std::string& id, std::unique_ptr<MidiSink> sink) = 0;

    /**
     * Provide file system implementation to be used by the synth.
     * There can be multiple instances (differentiated by id)
     * for the client to manage multiple locations in a flexible way
     * (e.g. workspace vs global configuration).
     */
    virtual void addFileSystem(FileSystemId id, std::unique_ptr<FileSystem> fileSystem) = 0;

    virtual void initSamplePool(size_t size, std::shared_ptr<SampleStore> sampleStore) = 0;

    virtual std::string getInstrumentName(int instrumentTypeId) = 0;

    virtual std::vector<std::string_view> getInstrumentTypeNames() = 0;

    virtual std::vector<int> getInstrumentTypeIds() = 0;

#ifdef __cpp_lib_span // workaround c++17 in PlatformImx
    /**
     * Setup the synth using instruments and mixer.
     */
    virtual void setup(std::span<std::shared_ptr<polysynth::Instrument>> newInstruments,
                       std::span<std::shared_ptr<polysynth::Instrument>> newEffects,
                       std::shared_ptr<Mixer> newMixer,
                       TempoProvider tempoProvider) = 0;

    /**
     * Disconnect an instrument previously connected in setup().
     */
    virtual void disconnectInstrument(int instrumentId) = 0;

    /**
     * Fill audio buffer with synth output.
     * Called directly from the audio thread.
     */
    virtual void renderAudioBuffer(AudioBufferStereoRef buffer) = 0;

    /**
     * Fill audio buffer with single instrument output.
     * Instruments that consume audio (see Patch::Metadata::Param::inputSource) receive inputBuffer
     * and sidechainBuffer.
     * Called directly from the audio thread.
     */
    virtual void renderAudioBuffer(AudioBufferStereoRef bufferMain,
                                   std::span<AudioBufferRef> bufferAux,
                                   int instrumentId,
                                   std::optional<AudioBufferStereoRef> inputBuffer = std::nullopt,
                                   std::optional<AudioBufferStereoRef> sidechainBuffer = std::nullopt) = 0;
#endif

    /**
     * Update the synth in audio thread.
     */
    virtual void updateAudio() = 0;

    /**
     * Update the synth in main/UI thread.
     * Useful for operations that cannot be performed in the audio thread,
     * e.g. involving memory alloc/free or file I/O.
     */
    virtual void updateMain() = 0;

    virtual ~Synth() = default;
};

} // namespace polysynth
