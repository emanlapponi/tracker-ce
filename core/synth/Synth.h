#pragma once
#include "config.h"
#ifdef FEATURE_SYNTH
#include "AudioStream.h"
#include "Platform.h"
#include "PolySynth/Patch.h"
#include "PolySynth/config.h"
#include "PolyUtil/misc.h"

#include "mtStructs.h"
#include <array>
#include <atomic>
#include <cassert>
#include <memory>
#include <span>

namespace polysynth
{
class Synth;
class PatchStore;
class SampleStore;
class Instrument;
class Mixer;
class PatchPool;
};

class MidiAdapter;

class Synth
{
  public:
    static constexpr float kGainSilence = -96;
    static constexpr float kGainMax = 12;
    static constexpr float kGainDefault = -9;
    static constexpr int kPanningMax = 100;
    static constexpr int kPanningDefault = 0;
    static constexpr int kSendMaxValue = 100;
    static constexpr float kSendDefaultValue = 0.0f;
    static constexpr std::array kSynthTypes = { polysynth::InstrumentTypeId::kSimpleAnalogSynth,
                                                polysynth::InstrumentTypeId::kFatAnalogSynth,
                                                polysynth::InstrumentTypeId::kVASynth,
                                                polysynth::InstrumentTypeId::kFMSynth,
                                                polysynth::InstrumentTypeId::kDrumSynth,
                                                polysynth::InstrumentTypeId::kShapingSynth, polysynth::InstrumentTypeId::kChiptuneSynth };
    static constexpr auto kInitPatchName = "Init";

    Synth();
    ~Synth();

    void init(Platform* platform, std::function<tiny::optional<float>()> getTempo);

    ::Midi* getVirtualMidiPort();

    std::vector<std::string_view> getPatchList(int part);
    [[nodiscard]] bool loadPatch(int part, const char* name);
    bool savePatch(int part, const char* name);
    bool savePatch(int part);
    void initPatch(int part);
    bool loadPatchFromWorkspace(int part, std::optional<std::string> overrideName = std::nullopt);
    void savePatchToWorkspace(int part);
    void saveDirtyWorkspacePatches();
    void copyWorkspacePatches(const char* sourceProjectPath, const char* destinationProjectPath);
    void setPatchDirty(int part) { parts[part].patchDirty = true; };
    [[nodiscard]] bool isPatchDirty(int part) const;
    bool isAnyPatchDirty();
    void silencePart(int part);

    void setGain(int part, float gainDb);
    void setPanning(int part, float panning);
    void setVoiceCount(int part, int voiceCount);
    int getMaxVoiceCount(int part);
    void setInstrumentType(int part, polysynth::InstrumentTypeId typeId);
    const std::vector<polysynth::InstrumentTypeId>& getSynthTypes() const { return synthTypes; }

    std::string getInstrumentTypeName(int typeIdx);
    int getInstrumentTypeIdx(int part);
    std::string getPatchName(int part);
    std::string getSynthName(int part);
    std::string getParamFullName(int part, int paramId);
    const char* getMacroKnobName(int macroId);
    const char* getDefault(int macroId);

    std::vector<std::string_view> listSamples(int part);
    std::string getSampleSlotName(int part, int slotId);
    std::string getSampleName(int part, int slotId);
    int getSampleIndex(int part, int slotId);
    bool loadSampleByIndex(int part, int slotId, int idx);
    bool loadSampleByPath(int part, int slotId, const std::string& fullSdPath);
    std::string getEngineSampleFolder(int part);

    std::string getEnginePatchFolder(int part);
    [[nodiscard]] bool loadPatchByPath(int part, const std::string& fullSdPath);
    bool savePatchByPath(int part, const std::string& fullSdPath);
    std::string describePatchFile(int part, const std::string& fullSdPath);

    int getSend(int part, int sendIdx); // in percent
    float getPanning(int part);         // +- percent

    // use AudioCS when modyfing the patch object
    polysynth::Patch& getPatch(int part);

    void triggerTestNote(int part, int note, bool noteOn);

    void updateToSettings(const strMtValues& settings, bool resetAllVoices = false);

    void initSynthPartSettings(int part, SynthPartSettings& settings);
    void initSynthPartMacrosValues(int part, SynthPartSettings& settings);
    void initSynthPartMacroValue(int part, int macroIdx, SynthPartSettings& settings);
    void validateSynthPartSettings(int part, SynthPartSettings& settings);

  private:
    struct Part
    {
        polysynth::InstrumentTypeId instrumentTypeId = {};
        std::shared_ptr<polysynth::Instrument> instrument = nullptr;
        std::shared_ptr<polysynth::Patch> patch = nullptr;
        std::string patchName = {};
        bool patchDirty = false;
        std::atomic<bool> loading{ false };
    };
    struct PluginTypeInfo
    {
        polysynth::InstrumentTypeId trackerTypeId = {};
        int32_t waveTypeId = 0;
        int32_t playTrackerTypeId = 0;
        std::string codeName = {};
        std::string patchFolder = {};
        int paramCount = 0;
        int baseParamCount = 0;
        int sampleSlotCount = 0;
    };
    std::array<Part, kSynthParts> parts;
    std::shared_ptr<polysynth::Synth> synth;
    std::unique_ptr<polysynth::PatchPool> patchPool;
    std::shared_ptr<polysynth::SampleStore> sampleStore;
    std::shared_ptr<polysynth::PatchStore> patchStore;
    std::shared_ptr<polysynth::PatchStore> patchStoreWorkspace;
    std::unique_ptr<MidiAdapter> midiAdapter;
    FileSystem* filesystem;
    std::vector<polysynth::InstrumentTypeId> synthTypes;
    std::vector<PluginTypeInfo> pluginTypes;

    void setPatch(int part, std::shared_ptr<polysynth::Patch> patch, std::string name);

    friend class PolySynth;
};
#endif
