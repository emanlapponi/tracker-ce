#ifdef FEATURE_SYNTH

#include "Synth.h"
#include "FileSystemAdapter.h"
#include "MidiAdapter.h"
#include "config.h"

#include "fileTransfer.h"




#include "PolySynth/Instrument.h"
#include "PolySynth/Mixer.h"
#include "PolySynth/PatchPool.h"
#include "PolySynth/PatchStore.h"
#include "PolySynth/SampleStore.h"
#include "PolySynth/Synth.h"
#include "PolySynth/config.h"

#include "PolyUtil/ScopeExit.h"
#include "PolyUtil/misc.h"
#include "fmt/core.h"

#include "util.h"
#include "util_math.h"

#include "PolySynth/Patch.h"
#include "cr_section_macros.h"
#include "fileManager.h"
#include "interruptsControl.h"
#include "mtStructs.h"
#include <cctype>
#include <cstring>
#include <numeric>

__NOINIT(EXTERNAL_RAM) alignas(32) static uint8_t s_psynRamPool[PSYN_RAM_POOL_SIZE];
static std::size_t s_psynRamPoolOffset = 0;

static const std::array kDefaultSettings = {
    SynthPartSettings{ .gainDb = Synth::kGainDefault,
                       .voiceCount = 1,
                       .synthType = 3,
                       .panning = Synth::kPanningDefault,
                       .patchName = {},
                       .reverbSend = Synth::kSendDefaultValue,
                       .delaySend = Synth::kSendDefaultValue,
                       .macroValue = { 0 } },
    SynthPartSettings{ .gainDb = Synth::kGainDefault,
                       .voiceCount = 1,
                       .synthType = 0,
                       .panning = Synth::kPanningDefault,
                       .patchName = {},
                       .reverbSend = Synth::kSendDefaultValue,
                       .delaySend = Synth::kSendDefaultValue,
                       .macroValue = { 0 } },
    SynthPartSettings{ .gainDb = Synth::kGainDefault,
                       .voiceCount = 1,
                       .synthType = 2,
                       .panning = Synth::kPanningDefault,
                       .patchName = {},
                       .reverbSend = Synth::kSendDefaultValue,
                       .delaySend = Synth::kSendDefaultValue,
                       .macroValue = { 0 } },
    SynthPartSettings{ .gainDb = -95,
                       .voiceCount = 0,
                       .synthType = 0,
                       .panning = 0,
                       .patchName = {},
                       .reverbSend = 0.0f,
                       .delaySend = 0.0f,
                       .macroValue = { 0 } },
};

static constexpr int kFirstSendCC = MidiAdapter::kTrackCCRange.first;
static constexpr std::array<uint8_t, kSynthCCs> defaultCCs = {
    105, 106, 102, 103, kFirstSendCC + 1, kFirstSendCC, 107, 104
};

static constexpr std::array kMacroKnobNames = { "Sample Attack", "Sample Decay",     "Bit Depth",
                                                "Filter Cutoff", "Filter Resonance", "Overdrive",
                                                "Volume" };

static constexpr int kInstrumentTypeMult = 1000;
static constexpr auto kPatchNameFormat = "%02d-%02d "; // TT-PP type-program
static constexpr auto kPatchNameTypeLen = 3;           // 2 chars of type idx + '-'

// required because of forward declarations of polysynth classes in the header
Synth::Synth() = default;
Synth::~Synth() = default;

void Synth::init(Platform* platform, std::function<tiny_optional<float>()> getTempo)
{
    s_psynRamPoolOffset = 0;
    synth = polysynth::Synth::createSynth(
      [](size_t size) -> void*
      {
          std::size_t aligned = (size + 31u) & ~31u;
          if (s_psynRamPoolOffset + aligned > sizeof(s_psynRamPool))
          {
              return nullptr;
          }
          void* ptr = &s_psynRamPool[s_psynRamPoolOffset];
          s_psynRamPoolOffset += aligned;
          return ptr;
      });

    FileSystemAdapter::staticInit();

    filesystem = platform->getFileSystem();
    if (!filesystem->exists("/Synths") && !filesystem->exists("/Synth"))
    {
        filesystem->mkdir(0, "/Synths");
    }
    std::string synthsDir = "/Synths";
    auto synthsAdapter = std::make_unique<FileSystemAdapter>("/Synths", filesystem);
    if (synthsAdapter
          ->listFiles(
            "", [](const std::string& name) { return name.size() > 5; }, std::nullopt)
          .empty() &&
        filesystem->exists("/Synth"))
    {
        synthsDir = "/Synth";
        synthsAdapter = std::make_unique<FileSystemAdapter>("/Synth", filesystem);
    }
    synth->addFileSystem(polysynth::Synth::FileSystemId::kSynths, std::move(synthsAdapter));
    synth->addFileSystem(
      polysynth::Synth::FileSystemId::kPatches,
      std::make_unique<FileSystemAdapter>(std::string(kPatchesFolder) + "/", filesystem));
    synth->addFileSystem(
      polysynth::Synth::FileSystemId::kWorkspace,
      std::make_unique<FileSystemAdapter>(std::string(kWorkspaceFolder) + "/", filesystem));
    synth->addFileSystem(
      polysynth::Synth::FileSystemId::kSamples,
      std::make_unique<FileSystemAdapter>("/SynthSamples/", filesystem));

    sampleStore = synth->createSampleStore(polysynth::Synth::FileSystemId::kSamples);
    synth->initSamplePool(4 * 1024 * 1024, sampleStore);

    synthTypes.clear();
    const auto availableIds = synth->getInstrumentTypeIds();
    for (auto preferredId : kSynthTypes)
    {
        if (std::ranges::find(availableIds, *preferredId) != availableIds.end())
        {
            synthTypes.push_back(preferredId);
        }
    }
    for (int id : availableIds)
    {
        if (id > 0 && id < *polysynth::InstrumentTypeId::kDelay)
        {
            auto typeId = static_cast<polysynth::InstrumentTypeId>(id);
            if (std::ranges::find(synthTypes, typeId) == synthTypes.end())
            {
                synthTypes.push_back(typeId);
            }
        }
    }
    if (synthTypes.empty())
    {
        synthTypes.push_back(static_cast<polysynth::InstrumentTypeId>(1));
    }

    patchStore = synth->createPatchStore(polysynth::Synth::FileSystemId::kPatches, true);
    patchStoreWorkspace =
      synth->createPatchStore(polysynth::Synth::FileSystemId::kWorkspace, false);

    midiAdapter = std::make_unique<MidiAdapter>(nullptr);

    auto instruments = std::array<std::shared_ptr<polysynth::Instrument>, kSynthParts>{};
    for (auto& part : parts)
    {
        int partIdx = std::distance(parts.begin(), &part);
        auto& instrument = part.instrument;

        part.instrumentTypeId = synthTypes[partIdx % synthTypes.size()];
        part.patchName = kInitPatchName;
        setPatch(partIdx, patchStore->createPatch(*part.instrumentTypeId), part.patchName);

        instrument = synth->createInstrument(partIdx);
        instrument->assignPatch(part.patch);
        instrument->connect(*midiAdapter);
        instrument->setVoiceCount(1);
        instrument->setMidiChannel(partIdx);

        instruments[partIdx] = instrument;
    }

    synth->setup(instruments, {}, nullptr, std::move(getTempo));

    // Populate Wave <-> Play/Tracker plugin mapping and metadata counts from /Synths/*.psyn headers.
    pluginTypes.clear();
    struct __attribute__((packed)) PsynRawHeader
    {
        uint32_t magic;
        uint16_t api_version;
        uint16_t flags;
        uint16_t type_id_wave;
        uint16_t type_id_play_tracker;
        char code_name[8];
        char display_name[24];
        char patch_folder[16];
    };
    static_assert(sizeof(PsynRawHeader) == 60);

    struct MetadataCounts
    {
        int paramCount = 0;
        int baseParamCount = 0;
        int sampleSlotCount = 0;
    };
    auto getMetadataCountsForType = [this](int typeId) -> MetadataCounts
    {
        auto countsFromMeta = [](const polysynth::Patch::Metadata& meta) -> MetadataCounts
        {
            const int totalParams = static_cast<int>(meta.params.size());
            const int baseParams =
              (meta.voiceSleepParamId >= 0 && totalParams > 0) ? (totalParams - 1) : totalParams;
            return {
                .paramCount = totalParams,
                .baseParamCount = baseParams,
                .sampleSlotCount = static_cast<int>(meta.sampleSlots.size()),
            };
        };
        for (const auto& part : parts)
        {
            if (part.patch && *part.instrumentTypeId == typeId)
            {
                return countsFromMeta(part.patch->getMetadata());
            }
        }
        auto tmp = patchStore->createPatch(typeId);
        if (tmp)
        {
            return countsFromMeta(tmp->getMetadata());
        }
        return {};
    };

    auto mapPtToWaveId = [](int ptId) -> int32_t
    {
        switch (ptId)
        {
            case 6:
                return 11; // PERC
            case 7:
                return 10; // DIRT
            case 8:
                return 12; // CHIP
            case 9:
                return 6; // PMD
            case 10:
                return 7; // PHZ
            case 11:
                return 8; // GRAIN
            case 12:
                return 9; // DWA
            default:
                return ptId;
        }
    };

    FileSystemAdapter dirListAdapter(synthsDir + "/", filesystem);
    auto psynFiles = dirListAdapter.listFiles(
      "",
      [](const std::string& name)
      {
          return name.size() > 5 &&
                 strcasecmp(name.c_str() + name.size() - 5, ".psyn") == 0;
      },
      std::nullopt);

    for (auto fileName : psynFiles)
    {
        std::string fullPsynPath = synthsDir + "/" + std::string(fileName);
        auto [ok, fh] = filesystem->openFile(fullPsynPath.c_str(), FileSystem::kRead);
        if (!ok)
        {
            continue;
        }
        ScopeExit closePsyn([this, fh] { filesystem->closeFile(fh); });
        PsynRawHeader hdr{};
        int32_t bytesRead = filesystem->readFile(&hdr, sizeof(hdr), fh);
        if (bytesRead != static_cast<int32_t>(sizeof(hdr)) || hdr.magic != 0x4E595350u)
        {
            continue;
        }
        int32_t regId = (hdr.type_id_play_tracker > 0) ? hdr.type_id_play_tracker : hdr.type_id_wave;
        if (std::ranges::find(availableIds, regId) == availableIds.end())
        {
            continue;
        }
        auto existing = std::ranges::find_if(
          pluginTypes,
          [regId](const PluginTypeInfo& info) { return *info.trackerTypeId == regId; });
        if (existing != pluginTypes.end())
        {
            continue;
        }
        std::string codeName(hdr.code_name, strnlen(hdr.code_name, sizeof(hdr.code_name)));
        std::string patchFolder(hdr.patch_folder,
                                strnlen(hdr.patch_folder, sizeof(hdr.patch_folder)));
        if (patchFolder.empty())
        {
            patchFolder = codeName;
        }
        const auto counts = getMetadataCountsForType(regId);
        pluginTypes.push_back(PluginTypeInfo{
          .trackerTypeId = static_cast<polysynth::InstrumentTypeId>(regId),
          .waveTypeId = hdr.type_id_wave,
          .playTrackerTypeId = hdr.type_id_play_tracker,
          .codeName = std::move(codeName),
          .patchFolder = std::move(patchFolder),
          .paramCount = counts.paramCount,
          .baseParamCount = counts.baseParamCount,
          .sampleSlotCount = counts.sampleSlotCount,
        });
    }

    for (auto typeId : synthTypes)
    {
        auto existing = std::ranges::find_if(
          pluginTypes,
          [typeId](const PluginTypeInfo& info) { return info.trackerTypeId == typeId; });
        if (existing != pluginTypes.end())
        {
            continue;
        }
        std::string codeName = synth->getInstrumentName(*typeId);
        const auto counts = getMetadataCountsForType(*typeId);
        pluginTypes.push_back(PluginTypeInfo{
          .trackerTypeId = typeId,
          .waveTypeId = mapPtToWaveId(*typeId),
          .playTrackerTypeId = *typeId,
          .codeName = codeName,
          .patchFolder = codeName,
          .paramCount = counts.paramCount,
          .baseParamCount = counts.baseParamCount,
          .sampleSlotCount = counts.sampleSlotCount,
        });
    }

    for (int part = 0; part < kSynthParts; part++)
    {
        Synth::initSynthPartSettings(part, mtProject.values.synthPart[part]);
    }

    updateToSettings(mtProject.values);
}

::Midi* Synth::getVirtualMidiPort()
{
    return &*midiAdapter;
}

std::vector<std::string_view> Synth::getPatchList(int part)
{
    newFileManager.createPatchesFolder();

    return patchStore->listPatchesByType(*parts[part].instrumentTypeId);
}

bool Synth::loadPatch(int part, const char* name)
{
    assert(part < kSynthParts);

    silencePart(part);
    parts[part].loading.store(true, std::memory_order_release);
    ScopeExit clearLoading(
      [this, part] { parts[part].loading.store(false, std::memory_order_release); });

    if (patchStore->loadPatch(name, *parts[part].patch, true))
    {
        setPatch(part, std::move(parts[part].patch), name);
        return true;
    }

    return false;
}

namespace
{
std::string stripPatchExtension(const std::string& path)
{
    static constexpr std::string_view kPatchExt = ".patch";
    if (path.size() >= kPatchExt.size() &&
        strcasecmp(path.c_str() + path.size() - kPatchExt.size(), kPatchExt.data()) == 0)
    {
        return path.substr(0, path.size() - kPatchExt.size());
    }
    return path;
}

void ensureParentDirsForSdFile(FileSystem* fs, const std::string& fullSdPath)
{
    if (fs == nullptr || fullSdPath.empty())
    {
        return;
    }

    std::string rel = fullSdPath;
    while (!rel.empty() && rel.front() == '/')
    {
        rel.erase(rel.begin());
    }
    auto lastSlash = rel.find_last_of('/');
    if (lastSlash == std::string::npos)
    {
        return;
    }
    rel.resize(lastSlash);

    std::string acc;
    size_t pos = 0;
    while (pos < rel.size())
    {
        size_t nextSlash = rel.find('/', pos);
        std::string seg =
          (nextSlash == std::string::npos) ? rel.substr(pos) : rel.substr(pos, nextSlash - pos);
        if (!seg.empty())
        {
            if (!acc.empty())
            {
                acc += '/';
            }
            acc += seg;
            if (!fs->exists(acc.c_str()))
            {
                fs->mkdir(0, acc.c_str());
            }
        }
        if (nextSlash == std::string::npos)
        {
            break;
        }
        pos = nextSlash + 1;
    }
}

void clearPatchWireCache();
} // namespace

bool Synth::savePatch(int part, const char* name)
{
    assert(part < kSynthParts);
    clearPatchWireCache();

    newFileManager.createPatchesFolder();

    if (name != nullptr)
    {
        parts[part].patchName = name;
    }

    const std::string& pName = parts[part].patchName;
    if (!pName.empty() && pName.front() == '/')
    {
        std::string noExtFull = stripPatchExtension(pName);
        ensureParentDirsForSdFile(filesystem, noExtFull + ".patch");
        return patchStoreWorkspace->savePatch(noExtFull, *parts[part].patch);
    }

    if (pName.find('/') != std::string::npos)
    {
        std::string fullPath = getEnginePatchFolder(part) + "/" + stripPatchExtension(pName) + ".patch";
        ensureParentDirsForSdFile(filesystem, fullPath);
    }

    return patchStore->savePatch(pName, *parts[part].patch);
}

bool Synth::savePatch(int part)
{
    assert(part < kSynthParts);

    return savePatch(part, nullptr);
}

void Synth::initPatch(int part)
{
    assert(part < kSynthParts);

    std::shared_ptr<polysynth::Patch> newPatch =
      patchStore->createPatch(*parts[part].instrumentTypeId);

    DISABLE_AUDIO_INTERRUPTS();
    parts[part].instrument->assignPatch(newPatch);
    ENABLE_AUDIO_INTERRUPTS();

    setPatch(part, std::move(newPatch), kInitPatchName);
    savePatchToWorkspace(part);
}

void Synth::savePatchToWorkspace(int part)
{
    clearPatchWireCache();
    patchStoreWorkspace->savePatch(fmt::format("{}", part), *parts[part].patch);
    parts[part].patchDirty = false;
}

bool Synth::loadPatchFromWorkspace(int part, std::optional<std::string> overrideName)
{
    silencePart(part);
    parts[part].loading.store(true, std::memory_order_release);
    ScopeExit clearLoading(
      [this, part] { parts[part].loading.store(false, std::memory_order_release); });

    bool fileLoaded = true;
    if (overrideName)
    {
        parts[part].patchName = std::move(*overrideName);
    }

    if (!patchStoreWorkspace->loadPatch(fmt::format("{}", part), *parts[part].patch, true))
    {
        if (!loadPatch(part, parts[part].patchName.c_str()))
        {
            initPatch(part);
            fileLoaded = false;
        }
    }

    parts[part].patchDirty = false;
    return fileLoaded;
}

void Synth::saveDirtyWorkspacePatches()
{
    for (int part = 0; part < kSynthParts; part++)
    {
        if (parts[part].patchDirty)
        {
            savePatchToWorkspace(part);
        }
    }
}

void Synth::setVoiceCount(int part, int voiceCount)
{
    if (parts[part].instrument->getVoiceCount() != voiceCount)
    {
        DISABLE_AUDIO_INTERRUPTS();
        parts[part].instrument->setVoiceCount(voiceCount);
        ENABLE_AUDIO_INTERRUPTS();
    }

    assert(std::accumulate(parts.begin(),
                           parts.end(),
                           0,
                           [](int acc, const Part& part)
                           { return acc + part.instrument->getVoiceCount(); }) <= kSynthMaxVoices);
}

int Synth::getMaxVoiceCount(int part)
{
    int otherPartsVoiceCount = std::accumulate(parts.begin(),
                                               parts.end(),
                                               -parts[part].instrument->getVoiceCount(),
                                               [](int acc, const Part& part)
                                               { return acc + part.instrument->getVoiceCount(); });

    assert(parts[part].patch);
    return std::min(kSynthMaxVoices - otherPartsVoiceCount,
                    parts[part].patch->getMetadata().maxVoices);
}

void Synth::setInstrumentType(int partIdx, polysynth::InstrumentTypeId typeId)
{
    auto& part = parts[partIdx];
    if (part.instrumentTypeId != typeId)
    {
        part.instrumentTypeId = typeId;
        setPatch(partIdx, patchStore->createPatch(*typeId), kInitPatchName);

        if (part.instrument->getVoiceCount() > getMaxVoiceCount(partIdx))
        {
            setVoiceCount(partIdx, getMaxVoiceCount(partIdx));
        }

        DISABLE_AUDIO_INTERRUPTS();
        part.instrument->assignPatch(part.patch);
        ENABLE_AUDIO_INTERRUPTS();
    }
}

void Synth::setPatch(int part, std::shared_ptr<polysynth::Patch> patch, std::string name)
{
    parts[part].patch = std::move(patch);
    parts[part].patchName = std::move(name);

    //    updateCCDefaults(part);
}

std::string Synth::getInstrumentTypeName(int typeIdx)
{
    if (synthTypes.empty())
    {
        return "NONE";
    }
    int clamped = std::clamp<int>(typeIdx, 0, static_cast<int>(synthTypes.size()) - 1);
    return synth->getInstrumentName(*synthTypes.at(clamped));
}

int Synth::getInstrumentTypeIdx(int part)
{
    auto it = std::ranges::find(synthTypes, parts[part].instrumentTypeId);
    if (it == synthTypes.end())
    {
        return 0;
    }

    return std::distance(synthTypes.begin(), it);
}

int Synth::getSend(int part, int sendIdx)
{
    return midiAdapter->getTrackedCCValue(part, kFirstSendCC + sendIdx) * kSendMaxValue /
           polysynth::MidiEvent::Data::ControlChange::kMaxValue;
}

polysynth::Patch& Synth::getPatch(int part)
{
    return *parts[part].patch;
}

std::string Synth::getPatchName(int part)
{
    return parts[part].patchName;
}

std::string Synth::getSynthName(int part)
{
    assert(part < kSynthParts);
    std::string synthName = parts[part].instrument->getVoiceCount() > 0
                              ? synth->getInstrumentName(*parts[part].instrumentTypeId)
                              : "Off";
    return fmt::format("Synth {} ({})", part + 1, synthName);
}

std::string Synth::getParamFullName(int part, int paramId)
{
    const auto& metadata = parts[part].patch->getMetadata();
    // The id can come from a macro modifier stored in a patch file.
    if (paramId < 0 || paramId >= std::ssize(metadata.params))
    {
        return "Empty";
    }
    const auto& paramName = !metadata.params[paramId].displayName.empty()
                              ? metadata.params[paramId].displayName
                              : "Empty";

    for (const auto& section : metadata.layoutFlat.sections)
    {
        for (const auto& element : section.elements)
        {
            if (element.id == paramId &&
                element.type ==
                  polysynth::Patch::Metadata::EditorLayout::EditorElement::Type::kParam)
            {
                return (section.shortName.empty() ? section.name : section.shortName) + ' ' +
                       paramName;
            }
        }
        for (const auto& subsection : section.subsections)
        {
            for (const auto& element : subsection.elements)
            {
                if (element.id == paramId &&
                    element.type ==
                      polysynth::Patch::Metadata::EditorLayout::EditorElement::Type::kParam)
                {
                    return section.name + '>' + subsection.name + ' ' + paramName;
                }
            }
        }
    }

    return paramName;
}

const char* Synth::getMacroKnobName(int macroId)
{
    return macroId < std::ssize(kMacroKnobNames) ? kMacroKnobNames[macroId] : nullptr;
}

std::vector<std::string_view> Synth::listSamples(int part)
{
    if (part < 0 || part >= kSynthParts || !sampleStore || !synth)
    {
        return {};
    }
    std::string engineName = synth->getInstrumentName(*parts[part].instrumentTypeId);
    auto samples = sampleStore->listSamples(engineName);
    for (auto& s : samples)
    {
        if (s.size() > 4)
        {
            std::string_view ext = s.substr(s.size() - 4);
            if ((ext[0] == '.') && (ext[1] == 'w' || ext[1] == 'W') &&
                (ext[2] == 'a' || ext[2] == 'A') && (ext[3] == 'v' || ext[3] == 'V'))
            {
                s.remove_suffix(4);
            }
        }
    }
    return samples;
}

std::string Synth::getSampleSlotName(int part, int slotId)
{
    if (part < 0 || part >= kSynthParts || !parts[part].patch)
    {
        return "Sample";
    }
    const auto& slots = parts[part].patch->getMetadata().sampleSlots;
    if (slotId >= 0 && slotId < static_cast<int>(slots.size()) && !slots[slotId].displayName.empty())
    {
        return slots[slotId].displayName;
    }
    return "Sample";
}

std::string Synth::getSampleName(int part, int slotId)
{
    if (part < 0 || part >= kSynthParts || !parts[part].patch)
    {
        return "Empty";
    }
    const auto& slots = parts[part].patch->getMetadata().sampleSlots;
    if (slotId < 0 || slotId >= static_cast<int>(slots.size()))
    {
        return "Empty";
    }

    const auto* sample = parts[part].patch->getSample(slotId);
    if (!sample || sample->name.empty())
    {
        return "Empty";
    }

    const std::string& path = sample->name;
    auto slashPos = path.find_last_of("/\\");
    std::string name = (slashPos != std::string::npos) ? path.substr(slashPos + 1) : path;
    auto dotPos = name.find_last_of('.');
    if (dotPos != std::string::npos)
    {
        name = name.substr(0, dotPos);
    }
    return name.empty() ? "Empty" : name;
}

int Synth::getSampleIndex(int part, int slotId)
{
    auto samples = listSamples(part);
    if (samples.empty())
    {
        return -1;
    }
    std::string currentName = getSampleName(part, slotId);
    if (currentName == "Empty")
    {
        return -1;
    }
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        if (samples[i] == currentName)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool Synth::loadSampleByIndex(int part, int slotId, int idx)
{
    if (part < 0 || part >= kSynthParts || !parts[part].patch || !sampleStore || !synth)
    {
        return false;
    }
    auto samples = listSamples(part);
    if (samples.empty())
    {
        return false;
    }
    int clampedIdx = std::clamp<int>(idx, 0, static_cast<int>(samples.size()) - 1);
    std::string sampleName(samples[clampedIdx]);
    std::string engineName = synth->getInstrumentName(*parts[part].instrumentTypeId);
    std::string fullRelPath = engineName + "/" + sampleName + ".wav";

    if (parts[part].patch->loadSample(slotId, fullRelPath) == polysynth::LoadStatus::kSuccess)
    {
        parts[part].patchDirty = true;
        return true;
    }
    return false;
}

bool Synth::loadSampleByPath(int part, int slotId, const std::string& fullSdPath)
{
    if (part < 0 || part >= kSynthParts || !parts[part].patch || fullSdPath.empty())
    {
        return false;
    }

    std::string storePath = fullSdPath;
    static constexpr std::string_view kSynthSamplesPrefix = "/SynthSamples/";
    if (storePath.rfind(kSynthSamplesPrefix, 0) == 0)
    {
        storePath.erase(0, kSynthSamplesPrefix.size());
    }

    if (parts[part].patch->loadSample(slotId, storePath) == polysynth::LoadStatus::kSuccess)
    {
        parts[part].patchDirty = true;
        return true;
    }
    return false;
}

std::string Synth::getEngineSampleFolder(int part)
{
    if (part < 0 || part >= kSynthParts || !synth)
    {
        return "/SynthSamples";
    }
    std::string engineName = synth->getInstrumentName(*parts[part].instrumentTypeId);
    if (engineName.empty())
    {
        return "/SynthSamples";
    }
    return "/SynthSamples/" + engineName;
}

std::string Synth::getEnginePatchFolder(int part)
{
    clearPatchWireCache();
    newFileManager.createPatchesFolder();
    if (part < 0 || part >= kSynthParts || !synth)
    {
        return "/Patches";
    }
    std::string engineName = synth->getInstrumentName(*parts[part].instrumentTypeId);
    if (engineName.empty())
    {
        return "/Patches";
    }
    return "/Patches/" + engineName;
}

namespace
{
bool caseInsensitiveStartsWith(const std::string& haystack, const std::string& prefix)
{
    if (prefix.empty() || haystack.size() < prefix.size())
    {
        return false;
    }
    return strncasecmp(haystack.c_str(), prefix.c_str(), prefix.size()) == 0;
}

bool caseInsensitivePathHasSegment(const std::string& fullPath, const std::string& seg)
{
    if (seg.empty())
    {
        return false;
    }
    std::string needle = "/" + seg + "/";
    if (fullPath.size() < needle.size())
    {
        return false;
    }
    for (size_t i = 0; i + needle.size() <= fullPath.size(); ++i)
    {
        if (strncasecmp(fullPath.c_str() + i, needle.c_str(), needle.size()) == 0)
        {
            return true;
        }
    }
    return false;
}

struct PatchSampleSlotInfo
{
    std::string filePath;
    bool hasEmbedded = false;
};

struct PatchWireInfo
{
    bool valid = false;
    int32_t rawTypeId = 0;
    int paramCount = 0;
    bool sawEndToken = false;
    std::array<PatchSampleSlotInfo, 4> samples{};
};

class PatchStreamReader
{
  public:
    PatchStreamReader(FileSystem* fs, FileSystem::FileHandle fh)
      : fs(fs)
      , fh(fh)
    {
    }

    bool readByte(uint8_t& out)
    {
        if (atEof)
        {
            return false;
        }
        if (lookaheadLen > 0)
        {
            out = lookahead[0];
            for (size_t i = 1; i < lookaheadLen; ++i)
            {
                lookahead[i - 1] = lookahead[i];
            }
            lookaheadLen--;
            bytesRead++;
            return true;
        }
        if (bufPos < bufLen)
        {
            out = buf[bufPos++];
            bytesRead++;
            return true;
        }
        int32_t n = fs->readFile(buf.data(), buf.size(), fh);
        if (n <= 0)
        {
            atEof = true;
            return false;
        }
        bufLen = static_cast<size_t>(n);
        bufPos = 1;
        out = buf[0];
        bytesRead++;
        return true;
    }

    bool peekEndToken()
    {
        while (!atEof && lookaheadLen < 4)
        {
            uint8_t b = 0;
            if (bufPos < bufLen)
            {
                b = buf[bufPos++];
            }
            else
            {
                int32_t n = fs->readFile(buf.data(), buf.size(), fh);
                if (n <= 0)
                {
                    atEof = true;
                    break;
                }
                bufLen = static_cast<size_t>(n);
                b = buf[0];
                bufPos = 1;
            }
            lookahead[lookaheadLen++] = b;
        }
        if (lookaheadLen == 4)
        {
            uint32_t token = 0;
            std::memcpy(&token, lookahead.data(), sizeof(token));
            if (token == 0x5423526eu)
            {
                lookaheadLen = 0;
                atEof = true;
                return true;
            }
        }
        if (lookaheadLen == 0)
        {
            atEof = true;
        }
        return false;
    }

    bool skip(size_t count)
    {
        while (count > 0)
        {
            uint8_t dummy = 0;
            if (!readByte(dummy))
            {
                return false;
            }
            count--;
        }
        return true;
    }

    bool readVarint(uint64_t& val, size_t maxBytes = SIZE_MAX, size_t* consumed = nullptr)
    {
        val = 0;
        uint32_t shift = 0;
        for (int i = 0; i < 10; ++i)
        {
            if (consumed && *consumed >= maxBytes)
            {
                return false;
            }
            uint8_t b = 0;
            if (!readByte(b))
            {
                return false;
            }
            if (consumed)
            {
                (*consumed)++;
            }
            val |= static_cast<uint64_t>(b & 0x7Fu) << shift;
            if ((b & 0x80u) == 0)
            {
                return true;
            }
            shift += 7;
        }
        return false;
    }

    bool skipField(uint8_t wireType, size_t* consumed = nullptr, size_t limit = SIZE_MAX)
    {
        switch (wireType)
        {
            case 0:
            {
                uint64_t dummy = 0;
                return readVarint(dummy, limit, consumed);
            }
            case 1:
            {
                if (consumed && *consumed + 8 > limit)
                {
                    return false;
                }
                if (!skip(8))
                {
                    return false;
                }
                if (consumed)
                {
                    *consumed += 8;
                }
                return true;
            }
            case 2:
            {
                uint64_t len = 0;
                if (!readVarint(len, limit, consumed))
                {
                    return false;
                }
                if (len > 65536 || (consumed && *consumed + len > limit))
                {
                    return false;
                }
                if (!skip(static_cast<size_t>(len)))
                {
                    return false;
                }
                if (consumed)
                {
                    *consumed += static_cast<size_t>(len);
                }
                return true;
            }
            case 5:
            {
                if (consumed && *consumed + 4 > limit)
                {
                    return false;
                }
                if (!skip(4))
                {
                    return false;
                }
                if (consumed)
                {
                    *consumed += 4;
                }
                return true;
            }
            default:
                return false;
        }
    }

    size_t getBytesRead() const { return bytesRead; }

  private:
    FileSystem* fs;
    FileSystem::FileHandle fh;
    std::array<uint8_t, 256> buf{};
    size_t bufPos = 0;
    size_t bufLen = 0;
    std::array<uint8_t, 4> lookahead{};
    size_t lookaheadLen = 0;
    bool atEof = false;
    size_t bytesRead = 0;
};

void parseSampleSubmessage(PatchStreamReader& reader, size_t totalLen, PatchSampleSlotInfo& outSlot)
{
    size_t pos = 0;
    while (pos < totalLen)
    {
        uint64_t key = 0;
        if (!reader.readVarint(key, totalLen, &pos))
        {
            break;
        }
        uint32_t tag = static_cast<uint32_t>(key >> 3);
        uint8_t wt = static_cast<uint8_t>(key & 7u);
        if (tag == 1 && wt == 2)
        {
            uint64_t slen = 0;
            if (!reader.readVarint(slen, totalLen, &pos) || pos + slen > totalLen)
            {
                break;
            }
            outSlot.filePath.clear();
            for (size_t i = 0; i < static_cast<size_t>(slen); ++i)
            {
                uint8_t c = 0;
                if (!reader.readByte(c))
                {
                    return;
                }
                pos++;
                if (outSlot.filePath.size() < 256)
                {
                    outSlot.filePath.push_back(static_cast<char>(c));
                }
            }
        }
        else if (tag == 2 && wt == 2)
        {
            uint64_t fmtLen = 0;
            if (!reader.readVarint(fmtLen, totalLen, &pos) || pos + fmtLen > totalLen)
            {
                break;
            }
            size_t fmtEnd = pos + static_cast<size_t>(fmtLen);
            while (pos < fmtEnd)
            {
                uint64_t fkey = 0;
                if (!reader.readVarint(fkey, fmtEnd, &pos))
                {
                    break;
                }
                uint32_t ftag = static_cast<uint32_t>(fkey >> 3);
                uint8_t fwt = static_cast<uint8_t>(fkey & 7u);
                if (ftag == 3 && fwt == 0)
                {
                    uint64_t samplesVal = 0;
                    if (!reader.readVarint(samplesVal, fmtEnd, &pos))
                    {
                        break;
                    }
                    if (samplesVal > 0)
                    {
                        outSlot.hasEmbedded = true;
                    }
                }
                else if (!reader.skipField(fwt, &pos, fmtEnd))
                {
                    break;
                }
            }
        }
        else if (!reader.skipField(wt, &pos, totalLen))
        {
            break;
        }
    }
    if (pos < totalLen)
    {
        reader.skip(totalLen - pos);
    }
}

std::string s_cachedWireNoExtPath;
PatchWireInfo s_cachedWireInfo;

void clearPatchWireCache()
{
    s_cachedWireNoExtPath.clear();
    s_cachedWireInfo = {};
}

PatchWireInfo inspectPatchWireFile(FileSystem* fs, const std::string& noExtFull)
{
    PatchWireInfo info;
    if (fs == nullptr || noExtFull.empty())
    {
        return info;
    }

    if (s_cachedWireInfo.valid && s_cachedWireNoExtPath == noExtFull)
    {
        return s_cachedWireInfo;
    }

    std::string path = noExtFull + polysynth::PatchStore::kFileExtension;
    auto [ok, fh] = fs->openFile(path.c_str(), FileSystem::kRead);
    if (!ok)
    {
        return info;
    }
    ScopeExit closeFile([fs, fh] { fs->closeFile(fh); });

    PatchStreamReader reader(fs, fh);
    int sampleSlotIdx = 0;
    int fieldCount = 0;

    while (true)
    {
        if (reader.peekEndToken())
        {
            info.sawEndToken = true;
            break;
        }
        uint64_t key = 0;
        if (!reader.readVarint(key))
        {
            break;
        }
        uint32_t tag = static_cast<uint32_t>(key >> 3);
        uint8_t wt = static_cast<uint8_t>(key & 7u);
        if (tag == 0)
        {
            return {};
        }
        fieldCount++;

        if (tag == 1 && wt == 0)
        {
            uint64_t val = 0;
            if (!reader.readVarint(val))
            {
                return {};
            }
            info.rawTypeId = static_cast<int32_t>(val);
        }
        else if (tag == 6 && wt == 2)
        {
            if (!reader.skipField(wt))
            {
                return {};
            }
            info.paramCount++;
        }
        else if (tag == 7 && wt == 2)
        {
            uint64_t slen = 0;
            if (!reader.readVarint(slen) || slen > 2048)
            {
                return {};
            }
            if (sampleSlotIdx < static_cast<int>(info.samples.size()))
            {
                parseSampleSubmessage(reader, static_cast<size_t>(slen), info.samples[sampleSlotIdx]);
            }
            else if (!reader.skip(static_cast<size_t>(slen)))
            {
                return {};
            }
            sampleSlotIdx++;
        }
        else if (!reader.skipField(wt))
        {
            return {};
        }
    }

    info.valid = (fieldCount > 0 || reader.getBytesRead() > 0);
    if (info.valid)
    {
        s_cachedWireNoExtPath = noExtFull;
        s_cachedWireInfo = info;
    }
    return info;
}

bool sampleExistsOnSd(FileSystem* fs, const std::string& engineCode, const std::string& filePath)
{
    if (fs == nullptr || filePath.empty())
    {
        return false;
    }
    if (filePath.front() == '/' && fs->exists(filePath.c_str()))
    {
        return true;
    }
    std::string rel = filePath;
    if (rel.front() == '/')
    {
        rel.erase(rel.begin());
    }
    if (fs->exists(("/SynthSamples/" + rel).c_str()) || fs->exists(("/Samples/" + rel).c_str()))
    {
        return true;
    }
    if (rel.find('/') == std::string::npos && !engineCode.empty())
    {
        if (fs->exists(("/SynthSamples/" + engineCode + "/" + rel).c_str()) ||
            fs->exists(("/Samples/" + engineCode + "/" + rel).c_str()))
        {
            return true;
        }
    }
    return fs->exists(("/Patches/" + rel).c_str());
}

std::string extractLeafSampleName(const std::string& filePath)
{
    if (filePath.empty())
    {
        return "Empty";
    }
    auto slash = filePath.find_last_of("/\\");
    std::string name = (slash != std::string::npos) ? filePath.substr(slash + 1) : filePath;
    auto dot = name.find_last_of('.');
    if (dot != std::string::npos)
    {
        name.resize(dot);
    }
    return name.empty() ? "Empty" : name;
}
} // namespace

bool Synth::loadPatchByPath(int part, const std::string& fullSdPath)
{
    if (part < 0 || part >= kSynthParts || !parts[part].patch || fullSdPath.empty())
    {
        return false;
    }

    std::string noExtFull = stripPatchExtension(fullSdPath);
    if (noExtFull.front() != '/')
    {
        noExtFull.insert(noExtFull.begin(), '/');
    }

    PatchWireInfo wireInfo = inspectPatchWireFile(filesystem, noExtFull);
    if (!wireInfo.valid)
    {
        return false;
    }

    const PluginTypeInfo* activePlugin = nullptr;
    for (const auto& info : pluginTypes)
    {
        if (info.trackerTypeId == parts[part].instrumentTypeId)
        {
            activePlugin = &info;
            break;
        }
    }

    bool compatible = false;
    if (activePlugin != nullptr)
    {
        if (wireInfo.rawTypeId == activePlugin->playTrackerTypeId ||
            wireInfo.rawTypeId == activePlugin->waveTypeId)
        {
            compatible = true;
        }
        else if (wireInfo.paramCount > 0 &&
                 (wireInfo.paramCount == activePlugin->paramCount ||
                  wireInfo.paramCount == activePlugin->baseParamCount))
        {
            compatible = true;
        }
    }
    else if (wireInfo.rawTypeId == *parts[part].instrumentTypeId)
    {
        compatible = true;
    }

    if (!compatible)
    {
        return false;
    }

    silencePart(part);
    parts[part].loading.store(true, std::memory_order_release);
    ScopeExit clearLoading(
      [this, part] { parts[part].loading.store(false, std::memory_order_release); });

    if (!patchStoreWorkspace->loadPatch(noExtFull, *parts[part].patch, true))
    {
        return false;
    }

    std::string storedName = noExtFull;
    std::string enginePrefix = getEnginePatchFolder(part) + "/";
    if (storedName.rfind(enginePrefix, 0) == 0)
    {
        storedName.erase(0, enginePrefix.size());
    }

    setPatch(part, std::move(parts[part].patch), std::move(storedName));
    return true;
}

bool Synth::savePatchByPath(int part, const std::string& fullSdPath)
{
    if (part < 0 || part >= kSynthParts || !parts[part].patch || fullSdPath.empty())
    {
        return false;
    }

    clearPatchWireCache();
    newFileManager.createPatchesFolder();

    std::string noExtFull = stripPatchExtension(fullSdPath);
    if (noExtFull.front() != '/')
    {
        noExtFull.insert(noExtFull.begin(), '/');
    }

    ensureParentDirsForSdFile(filesystem, noExtFull + ".patch");
    if (!patchStoreWorkspace->savePatch(noExtFull, *parts[part].patch))
    {
        return false;
    }

    std::string storedName = noExtFull;
    std::string enginePrefix = getEnginePatchFolder(part) + "/";
    if (storedName.rfind(enginePrefix, 0) == 0)
    {
        storedName.erase(0, enginePrefix.size());
    }

    parts[part].patchName = std::move(storedName);
    parts[part].patchDirty = false;
    return true;
}

std::string Synth::describePatchFile(int part, const std::string& fullSdPath)
{
    if (fullSdPath.empty() || !patchStoreWorkspace || !synth)
    {
        return {};
    }

    std::string noExtFull = stripPatchExtension(fullSdPath);
    if (noExtFull.front() != '/')
    {
        noExtFull.insert(noExtFull.begin(), '/');
    }

    PatchWireInfo wireInfo = inspectPatchWireFile(filesystem, noExtFull);
    if (!wireInfo.valid)
    {
        return "Invalid Patch";
    }

    const PluginTypeInfo* resolvedPlugin = nullptr;
    int bestScore = -1;
    const auto activeType =
      (part >= 0 && part < kSynthParts) ? parts[part].instrumentTypeId : polysynth::InstrumentTypeId{};

    for (const auto& info : pluginTypes)
    {
        const bool idMatches = (info.playTrackerTypeId == wireInfo.rawTypeId ||
                                info.waveTypeId == wireInfo.rawTypeId);
        const bool exactCountMatches =
          (info.paramCount > 0 && info.paramCount == wireInfo.paramCount);
        const bool baseCountMatches =
          (info.baseParamCount > 0 && info.baseParamCount == wireInfo.paramCount);
        if (!idMatches && !exactCountMatches && !baseCountMatches)
        {
            continue;
        }

        int score = 0;
        if (idMatches)
        {
            score += 100;
        }
        if (exactCountMatches)
        {
            score += 50;
        }
        else if (baseCountMatches)
        {
            score += 40;
        }
        for (const auto& s : wireInfo.samples)
        {
            if (!s.filePath.empty() && caseInsensitiveStartsWith(s.filePath, info.codeName + "/"))
            {
                score += 30;
                break;
            }
        }
        if (caseInsensitivePathHasSegment(noExtFull, info.codeName) ||
            caseInsensitivePathHasSegment(noExtFull, info.patchFolder))
        {
            score += 20;
        }
        if (part >= 0 && part < kSynthParts && info.trackerTypeId == activeType)
        {
            score += 10;
        }
        if (info.playTrackerTypeId == wireInfo.rawTypeId)
        {
            score += 5;
        }

        if (score > bestScore)
        {
            bestScore = score;
            resolvedPlugin = &info;
        }
    }

    if (resolvedPlugin == nullptr)
    {
        std::string eng = synth->getInstrumentName(wireInfo.rawTypeId);
        if (eng.empty())
        {
            return "Invalid Patch";
        }
        bool match = (part >= 0 && part < kSynthParts && wireInfo.rawTypeId == *parts[part].instrumentTypeId);
        return match ? ("Engine: " + eng) : ("Engine: " + eng + " (Incompatible)");
    }

    const bool compatible =
      (part >= 0 && part < kSynthParts && resolvedPlugin->trackerTypeId == activeType);
    std::string desc =
      compatible ? ("Engine: " + resolvedPlugin->codeName)
                 : ("Engine: " + resolvedPlugin->codeName + " (Incompatible)");

    const int maxShownSlots = std::min(2, resolvedPlugin->sampleSlotCount);
    for (int slotIdx = 0; slotIdx < maxShownSlots; ++slotIdx)
    {
        const auto& slot = wireInfo.samples[slotIdx];
        const char* prefix = (resolvedPlugin->sampleSlotCount <= 1)
                               ? "Sample: "
                               : (slotIdx == 0 ? "Slot 1: " : "Slot 2: ");
        desc.push_back('\n');
        desc += prefix;
        if (slot.filePath.empty())
        {
            if (wireInfo.sawEndToken && slot.hasEmbedded)
            {
                desc += "Embedded";
            }
            else
            {
                desc += "Empty";
            }
        }
        else
        {
            desc += extractLeafSampleName(slot.filePath);
            const bool available = (wireInfo.sawEndToken && slot.hasEmbedded) ||
                                   sampleExistsOnSd(filesystem, resolvedPlugin->codeName, slot.filePath);
            if (!available)
            {
                desc += " (Missing)";
            }
        }
    }

    return desc;
}

void Synth::updateToSettings(const strMtValues& settings, bool resetAllVoices)
{
    if (resetAllVoices)
    {
        for (int part = 0; part < kSynthParts; part++)
        {
            setVoiceCount(part, 0);
        }
    }

    for (int part = 0; part < kSynthParts; part++)
    {
        setVoiceCount(part, 0);
        int clampedType = std::clamp<int>(
          settings.synthPart[part].synthType,
          0,
          std::max<int>(0, static_cast<int>(synthTypes.size()) - 1));
        setInstrumentType(part, synthTypes.at(clampedType));
        setVoiceCount(part, settings.synthPart[part].voiceCount);
        parts[part].patchName = settings.synthPart[part].patchName;
    }
}

void Synth::initSynthPartSettings(int part, SynthPartSettings& settings)
{
    settings = kDefaultSettings.at(part);
    if (!synthTypes.empty() && settings.synthType >= static_cast<int>(synthTypes.size()))
    {
        settings.synthType = 0;
    }

    initSynthPartMacrosValues(part, settings);

    snprintf(settings.patchName, std::size(settings.patchName), "%s", kInitPatchName);
}

void Synth::initSynthPartMacrosValues(int part, SynthPartSettings& settings)
{
    for (int macroIdx = 0; macroIdx < kSynthMacrosCount; macroIdx++)
    {
        initSynthPartMacroValue(part, macroIdx, settings);
    }
}

void Synth::initSynthPartMacroValue(int part, int macroIdx, SynthPartSettings& settings)
{
    const polysynth::Patch::Metadata& metadata = getPatch(part).getMetadata();
    settings.macroValue[macroIdx] = metadata.macros[macroIdx].bipolar ? kCCMidValue : kCCMinValue;
}

void Synth::validateSynthPartSettings(int part, SynthPartSettings& settings)
{

    bool anyMacroValueIsOurRange = false;
    for (auto macroValue : settings.macroValue)
    {
        if (isOutOfRange<int>(macroValue, 0, kCCMaxValue))
        {
            anyMacroValueIsOurRange = true;
            break;
        }
    }

    int maxSynthType = std::max<int>(0, static_cast<int>(synthTypes.size()) - 1);
    if (isOutOfRange<int>(settings.synthType, 0, maxSynthType) ||
        isOutOfRange<int>(settings.voiceCount, 0, kSynthMaxVoices) ||
        isOutOfRange<float>(settings.gainDb, kGainSilence, kGainMax) ||
        isOutOfRange<float>(settings.panning, -kPanningMax, kPanningMax) ||
        isOutOfRange<float>(settings.reverbSend, 0, kSendMaxValue) ||
        isOutOfRange<float>(settings.delaySend, 0, kSendMaxValue) || anyMacroValueIsOurRange)
    {
        initSynthPartSettings(part, settings);
    }
}

void Synth::triggerTestNote(int part, int note, bool noteOn)
{
    DISABLE_AUDIO_INTERRUPTS();
    if (noteOn)
    {
        midiAdapter->sendNoteOn(note, 100, part + 1);
    }
    else
    {
        midiAdapter->sendNoteOff(note, 100, part + 1);
    }

    ENABLE_AUDIO_INTERRUPTS();
}

void Synth::copyWorkspacePatches(const char* sourceProjectPath, const char* destinationProjectPath)
{
    for (int part = 0; part < kSynthParts; part++)
    {
        std::string srcFile =
          fmt::format("{}\\{}{}", sourceProjectPath, part, polysynth::PatchStore::kFileExtension);
        std::string dstFile = fmt::format(
          "{}\\{}{}", destinationProjectPath, part, polysynth::PatchStore::kFileExtension);
        cFileTransfer::copyFile(filesystem, srcFile.c_str(), dstFile.c_str());
    }
}

bool Synth::isPatchDirty(int part) const
{
    return (part >= 0 && part < kSynthParts) ? parts[part].patchDirty : false;
}

void Synth::silencePart(int part)
{
    if (part < 0 || part >= kSynthParts || !midiAdapter || !parts[part].instrument)
    {
        return;
    }
    DISABLE_AUDIO_INTERRUPTS();
    midiAdapter->sendControlChange(
      polysynth::MidiEvent::Data::ControlChange::kAllNotesOffCC, 0, part + 1);
    parts[part].instrument->reset();
    ENABLE_AUDIO_INTERRUPTS();
}

bool Synth::isAnyPatchDirty()
{
    for (int part = 0; part < kSynthParts; part++)
    {
        if (parts[part].patchDirty)
        {
            return true;
        }
    }
    return false;
}

#endif
