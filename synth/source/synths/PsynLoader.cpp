#include "PsynLoader.h"

#include "PolySynth/Core.h"
#include "PolySynth/Patch.h"
#include "PolySynth/config.h"
#include "PolySynth/psyn_plugin.h"
#include "PolySynth/sdk/InstrumentType.h"
#include "PolySynth/sdk/InstrumentVoice.h"
#include "PolyUtil/ScopeExit.h"
#include "patch/PatchImpl.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

namespace polysynth
{

namespace
{

// -----------------------------------------------------------------------------
// External SDRAM Arena Allocator (32-byte aligned blocks with coalescing)
// -----------------------------------------------------------------------------
class ArenaAllocator
{
  public:
    struct alignas(32) BlockHeader
    {
        size_t size; // Usable payload size (always a multiple of 32)
        BlockHeader* next;
        BlockHeader* prev;
        uint32_t used;
        uint32_t magic;
        int32_t ownerPluginIdx;
        uint32_t pad;
    };
    static_assert(sizeof(BlockHeader) == 32, "BlockHeader must be 32 bytes");

    static constexpr uint32_t kBlockMagic = 0x5053424Cu; // "PSBL"

    void init(void* base, size_t totalBytes)
    {
        if (base == nullptr || totalBytes <= sizeof(BlockHeader) * 2)
        {
            head_ = nullptr;
            totalBytes_ = 0;
            usedBytes_ = 0;
            return;
        }
        uintptr_t rawAddr = reinterpret_cast<uintptr_t>(base);
        uintptr_t alignedAddr = (rawAddr + 31u) & ~static_cast<uintptr_t>(31u);
        size_t usableTotal = (totalBytes - (alignedAddr - rawAddr)) & ~static_cast<size_t>(31u);

        head_ = reinterpret_cast<BlockHeader*>(alignedAddr);
        head_->size = usableTotal - sizeof(BlockHeader);
        head_->next = nullptr;
        head_->prev = nullptr;
        head_->used = 0;
        head_->magic = kBlockMagic;
        head_->ownerPluginIdx = -1;
        head_->pad = 0;

        totalBytes_ = usableTotal;
        usedBytes_ = 0;
    }

    bool isInitialized() const { return head_ != nullptr; }
    size_t usedBytes() const { return usedBytes_; }
    size_t totalBytes() const { return totalBytes_; }

    void* alloc(size_t size, size_t alignment = 32, int32_t ownerPluginIdx = -1)
    {
        if (head_ == nullptr || size == 0)
        {
            return nullptr;
        }
        (void)alignment; // All blocks and payloads are 32-byte aligned
        size_t alignedSize = (size + 31u) & ~static_cast<size_t>(31u);

        for (BlockHeader* cur = head_; cur != nullptr; cur = cur->next)
        {
            if (!cur->used && cur->size >= alignedSize)
            {
                if (cur->size >= alignedSize + sizeof(BlockHeader) + 32u)
                {
                    auto* split = reinterpret_cast<BlockHeader*>(
                      reinterpret_cast<uint8_t*>(cur) + sizeof(BlockHeader) + alignedSize);
                    split->size = cur->size - alignedSize - sizeof(BlockHeader);
                    split->next = cur->next;
                    split->prev = cur;
                    split->used = 0;
                    split->magic = kBlockMagic;
                    split->ownerPluginIdx = -1;
                    split->pad = 0;
                    if (cur->next != nullptr)
                    {
                        cur->next->prev = split;
                    }
                    cur->next = split;
                    cur->size = alignedSize;
                }
                cur->used = 1;
                cur->ownerPluginIdx = ownerPluginIdx;
                usedBytes_ += cur->size + sizeof(BlockHeader);
                void* payload = reinterpret_cast<uint8_t*>(cur) + sizeof(BlockHeader);
                std::memset(payload, 0, cur->size);
                return payload;
            }
        }
        return nullptr;
    }

    void free(void* ptr)
    {
        if (ptr == nullptr || head_ == nullptr)
        {
            return;
        }
        auto* blk = reinterpret_cast<BlockHeader*>(static_cast<uint8_t*>(ptr) - sizeof(BlockHeader));
        if (blk->magic != kBlockMagic || !blk->used)
        {
            return;
        }
        blk->used = 0;
        blk->ownerPluginIdx = -1;
        if (usedBytes_ >= blk->size + sizeof(BlockHeader))
        {
            usedBytes_ -= blk->size + sizeof(BlockHeader);
        }

        // Coalesce with next block if free
        if (blk->next != nullptr && !blk->next->used)
        {
            BlockHeader* nextBlk = blk->next;
            blk->size += sizeof(BlockHeader) + nextBlk->size;
            blk->next = nextBlk->next;
            if (blk->next != nullptr)
            {
                blk->next->prev = blk;
            }
        }
        // Coalesce with prev block if free
        if (blk->prev != nullptr && !blk->prev->used)
        {
            BlockHeader* prevBlk = blk->prev;
            prevBlk->size += sizeof(BlockHeader) + blk->size;
            prevBlk->next = blk->next;
            if (prevBlk->next != nullptr)
            {
                prevBlk->next->prev = prevBlk;
            }
        }
    }

    void freeAllOwnedBy(int32_t ownerPluginIdx)
    {
        if (head_ == nullptr || ownerPluginIdx < 0)
        {
            return;
        }
        for (BlockHeader* cur = head_; cur != nullptr;)
        {
            BlockHeader* next = cur->next;
            if (cur->used && cur->ownerPluginIdx == ownerPluginIdx)
            {
                free(reinterpret_cast<uint8_t*>(cur) + sizeof(BlockHeader));
                // Coalescing may merge `cur` with `prev` or `next`; restart cleanly from head_.
                cur = head_;
                continue;
            }
            cur = next;
        }
    }

  private:
    BlockHeader* head_ = nullptr;
    size_t totalBytes_ = 0;
    size_t usedBytes_ = 0;
};

struct PluginRecord
{
    bool valid = false;
    bool hasSampleSlots = false;
    bool metadataPrimedOnce = false;
    int index = -1;
    int typeId = 0;
    char filename[48]{};
    PsynHeader header{};
    uint8_t* imageBase = nullptr;
    size_t imageSize = 0;
    const PsynPluginApi* pluginApi = nullptr;
    int activeVoices = 0;
    uint32_t lastUsedTick = 0;
};

ArenaAllocator s_arena;
std::deque<PluginRecord> s_plugins{};
size_t s_pluginCount = 0;
FileSystem* s_synthsFs = nullptr;
FileSystem* s_samplesFs = nullptr;
uint32_t s_tickCounter = 0;
PluginRecord* s_currentPlugin = nullptr;

struct ScopedPluginContext
{
    PluginRecord* prev = nullptr;
    explicit ScopedPluginContext(PluginRecord* cur)
      : prev(s_currentPlugin)
    {
        s_currentPlugin = cur;
    }
    ~ScopedPluginContext() { s_currentPlugin = prev; }
};

void ensurePluginSampleFolder(const PluginRecord& rec)
{
    if (s_samplesFs == nullptr)
    {
        return;
    }
    const char* folder =
      rec.header.patch_folder[0] != '\0' ? rec.header.patch_folder : rec.header.code_name;
    if (folder[0] != '\0')
    {
        s_samplesFs->createFolder(folder);
    }
}

// -----------------------------------------------------------------------------
// ARM Thumb-2 Relocation & Instruction Cache Helpers
// -----------------------------------------------------------------------------
void patchThumbMovwMovt(uint8_t* loc, uint16_t imm16)
{
    uint16_t hw1 = 0;
    uint16_t hw2 = 0;
    std::memcpy(&hw1, loc, 2);
    std::memcpy(&hw2, loc + 2, 2);

    uint16_t imm4 = (imm16 >> 12) & 0xFu;
    uint16_t iBit = (imm16 >> 11) & 0x1u;
    uint16_t imm3 = (imm16 >> 8) & 0x7u;
    uint16_t imm8 = imm16 & 0xFFu;

    hw1 = static_cast<uint16_t>((hw1 & ~(0x040Fu)) | (iBit << 10) | imm4);
    hw2 = static_cast<uint16_t>((hw2 & ~(0x70FFu)) | (imm3 << 12) | imm8);

    std::memcpy(loc, &hw1, 2);
    std::memcpy(loc + 2, &hw2, 2);
}

void syncInstructionCache([[maybe_unused]] void* addr, [[maybe_unused]] size_t size)
{
#if defined(__arm__) || defined(__thumb__)
    asm volatile("dsb 0xf" ::: "memory");
    asm volatile("isb 0xf" ::: "memory");

    // Cortex-M7 SCB->DCCIMVAC (0xE000EF70) and SCB->ICIALLU (0xE000EF50)
    volatile uint32_t* const scb_dccimvac = reinterpret_cast<volatile uint32_t*>(0xE000EF70u);
    volatile uint32_t* const scb_iciallu = reinterpret_cast<volatile uint32_t*>(0xE000EF50u);

    uintptr_t p = reinterpret_cast<uintptr_t>(addr) & ~static_cast<uintptr_t>(0x1Fu);
    uintptr_t end = reinterpret_cast<uintptr_t>(addr) + size;
    while (p < end)
    {
        *scb_dccimvac = static_cast<uint32_t>(p);
        p += 32u;
    }

    asm volatile("dsb 0xf" ::: "memory");
    *scb_iciallu = 0u;
    asm volatile("dsb 0xf" ::: "memory");
    asm volatile("isb 0xf" ::: "memory");
#endif
}

bool evictOneIdlePlugin(const PluginRecord* exclude)
{
    PluginRecord* victim = nullptr;
    for (size_t i = 0; i < s_pluginCount; ++i)
    {
        auto& p = s_plugins[i];
        if (!p.valid || p.imageBase == nullptr || p.activeVoices > 0 || &p == exclude)
        {
            continue;
        }
        if (victim == nullptr || p.lastUsedTick < victim->lastUsedTick)
        {
            victim = &p;
        }
    }
    if (victim == nullptr)
    {
        return false;
    }
    s_arena.free(victim->imageBase);
    victim->imageBase = nullptr;
    victim->imageSize = 0;
    victim->pluginApi = nullptr;
    s_arena.freeAllOwnedBy(victim->index);
    return true;
}

void* allocWithEviction(size_t size,
                        size_t alignment,
                        const PluginRecord* exclude,
                        int32_t ownerPluginIdx = -1)
{
    void* ptr = s_arena.alloc(size, alignment, ownerPluginIdx);
    while (ptr == nullptr && evictOneIdlePlugin(exclude))
    {
        ptr = s_arena.alloc(size, alignment, ownerPluginIdx);
    }
    return ptr;
}

// -----------------------------------------------------------------------------
// Host API Callbacks Passed to .psyn Plugins
// -----------------------------------------------------------------------------
void* hostSlotAlloc(size_t size, size_t alignment)
{
    int32_t owner = (s_currentPlugin != nullptr) ? s_currentPlugin->index : -1;
    return allocWithEviction(size, alignment, s_currentPlugin, owner);
}

void hostSlotFree(void* ptr)
{
    s_arena.free(ptr);
}

bool hostPatchGetParamValue(const void* hostPatch, int paramId, float* outVal)
{
    if (hostPatch == nullptr || outVal == nullptr || paramId < 0)
    {
        return false;
    }
    const auto* patch = static_cast<const Patch*>(hostPatch);
    auto val = patch->getParamValue(paramId);
    if (val.has_value())
    {
        *outVal = *val;
        return true;
    }
    return false;
}

bool hostPatchGetParamFinalValue(const void* hostPatch, int paramId, float* outVal)
{
    if (hostPatch == nullptr || outVal == nullptr || paramId < 0)
    {
        return false;
    }
    const auto* patch = static_cast<const Patch*>(hostPatch);
    auto val = patch->getParamFinalValue(paramId);
    if (val.has_value())
    {
        *outVal = *val;
        return true;
    }
    return false;
}

void hostPatchSetParamValue(void* hostPatch, int paramId, bool hasVal, float val, int source)
{
    if (hostPatch == nullptr || paramId < 0)
    {
        return;
    }
    auto* patch = static_cast<Patch*>(hostPatch);
    Patch::optional_paramvalue_t optVal =
      hasVal ? Patch::optional_paramvalue_t(val) : std::nullopt;
    patch->setParamValue(paramId, optVal, static_cast<Patch::ParamChangeSource>(source));
}

bool hostPatchGetSample(const void* hostPatch, int slotId, PsynSampleData* outSample)
{
    if (hostPatch == nullptr || outSample == nullptr)
    {
        return false;
    }
    const auto* patch = static_cast<const Patch*>(hostPatch);
    if (slotId < 0 || slotId >= static_cast<int>(patch->getMetadata().sampleSlots.size()))
    {
        return false;
    }
    auto sample = patch->getSample(slotId);
    if (!sample)
    {
        return false;
    }
    outSample->data = sample->data.data();
    outSample->size = static_cast<uint32_t>(sample->data.size());
    outSample->channels = static_cast<uint8_t>(sample->format.channels);
    outSample->ready =
      (sample->ready ? 1u : 0u) |
      (sample->ready
         ? static_cast<uint8_t>((std::clamp<int>(sample->basePitch, 1, 127) & 0x7F) << 1)
         : 0u);
    outSample->wave_size = static_cast<uint16_t>(sample->waveSize.value_or(0));
    outSample->sample_rate = static_cast<uint32_t>(sample->format.sampleRate);
    return true;
}

struct MetaBuilderContext
{
    Patch::Metadata* metadata = nullptr;
    int pluginIdx = -1;
    std::array<Patch::Metadata::Macro, kMacroCount> defaultMacros{};
    bool hasDefaultMacros = false;
};

const PsynPluginApi* ensurePluginLoaded(PluginRecord& rec);

void hostMetaSetInfo(void* metaBuilder,
                     int outputChannels,
                     bool macrosAffectCc,
                     bool rendersAuxSends,
                     int maxVoices)
{
    auto* ctx = static_cast<MetaBuilderContext*>(metaBuilder);
    if (ctx == nullptr || ctx->metadata == nullptr)
    {
        return;
    }
    ctx->metadata->outputChannels =
      static_cast<Patch::Metadata::OutputChannels>(std::clamp(outputChannels, 0, 2));
    ctx->metadata->macrosAffectParamCC = macrosAffectCc;
    ctx->metadata->rendersAuxSends = rendersAuxSends;
    if (maxVoices > 0)
    {
        ctx->metadata->maxVoices = maxVoices;
    }
}

void hostMetaSetParamCount(void* metaBuilder, int count)
{
    auto* ctx = static_cast<MetaBuilderContext*>(metaBuilder);
    if (ctx == nullptr || ctx->metadata == nullptr || count < 0)
    {
        return;
    }
    ctx->metadata->params.resize(static_cast<size_t>(count));
}

void hostMetaSetParam(void* metaBuilder, int paramId, const PsynParamDesc* desc)
{
    auto* ctx = static_cast<MetaBuilderContext*>(metaBuilder);
    if (ctx == nullptr || ctx->metadata == nullptr || desc == nullptr || paramId < 0)
    {
        return;
    }
    if (paramId >= static_cast<int>(ctx->metadata->params.size()))
    {
        ctx->metadata->params.resize(static_cast<size_t>(paramId + 1));
    }

    auto& p = ctx->metadata->params[paramId];
    p.displayName = desc->display_name != nullptr ? desc->display_name : "";
    p.defaultValue =
      desc->has_default ? Patch::optional_paramvalue_t(desc->default_value) : std::nullopt;
    p.minValue = desc->min_value;
    p.maxValue = desc->max_value;
    p.editCurve = static_cast<Patch::Metadata::Param::Curve>(desc->curve);
    p.macroOperation = static_cast<Patch::Metadata::Param::MacroOperation>(desc->macro_op);
    p.isEnum = (desc->is_enum != 0);
    p.midiCC = (desc->midi_cc != 0xFFu) ? tiny::optional<uint8_t, 0xff>(desc->midi_cc)
                                        : std::nullopt;
    p.invalidatesOtherMetadata = (desc->invalidates_metadata != 0);
    p.inputSource = static_cast<int>(desc->input_source) - 1;
    p.inputSidechain = (desc->input_sidechain != 0);

    int pluginIdx = ctx->pluginIdx;
    p.valueFormatter = [pluginIdx, paramId](const Patch& patch,
                                            Patch::paramvalue_t value) -> std::string
    {
        if (pluginIdx < 0 || pluginIdx >= static_cast<int>(s_pluginCount))
        {
            return {};
        }
        auto& rec = s_plugins[pluginIdx];
        const PsynPluginApi* api = ensurePluginLoaded(rec);
        if (api == nullptr || api->format_param == nullptr)
        {
            return {};
        }
        ScopedPluginContext pluginScope(&rec);
        char buf[32]{};
        int len = api->format_param(&patch, paramId, value, buf, sizeof(buf));
        if (len <= 0)
        {
            return {};
        }
        return std::string(buf, static_cast<size_t>(std::min<int>(len, sizeof(buf) - 1)));
    };
}

void hostMetaAddSampleSlot(void* metaBuilder, const PsynSampleSlotDesc* desc)
{
    auto* ctx = static_cast<MetaBuilderContext*>(metaBuilder);
    if (ctx == nullptr || ctx->metadata == nullptr || desc == nullptr)
    {
        return;
    }
    Patch::Metadata::SampleSlot slot{};
    slot.displayName = desc->display_name != nullptr ? desc->display_name : "";
    slot.defaultSamplePath = desc->default_sample_path != nullptr ? desc->default_sample_path : "";
    slot.type = static_cast<Patch::Metadata::SampleSlot::Type>(desc->type);
    slot.section = desc->section;
    slot.subsection = desc->subsection;
    ctx->metadata->sampleSlots.push_back(std::move(slot));

    if (s_currentPlugin != nullptr)
    {
        s_currentPlugin->hasSampleSlots = true;
        ensurePluginSampleFolder(*s_currentPlugin);
    }
    if (s_samplesFs != nullptr && desc->default_sample_path != nullptr)
    {
        std::string_view defPath(desc->default_sample_path);
        auto slashPos = defPath.find('/');
        if (slashPos != std::string_view::npos && slashPos > 0)
        {
            s_samplesFs->createFolder(std::string(defPath.substr(0, slashPos)));
        }
    }
}

void hostMetaAddFlatSection(void* metaBuilder,
                            const char* name,
                            const char* shortName,
                            const PsynLayoutElement* elements,
                            int count)
{
    auto* ctx = static_cast<MetaBuilderContext*>(metaBuilder);
    if (ctx == nullptr || ctx->metadata == nullptr || count < 0)
    {
        return;
    }
    Patch::Metadata::EditorLayout::EditorSection sec{};
    sec.name = name != nullptr ? name : "";
    sec.shortName = shortName != nullptr ? shortName : sec.name;
#ifdef PRODUCT_TRACKER_PLUS
    if (sec.name == "Kick (C4)")
    {
        sec.name = "Kick (C5)";
    }
    else if (sec.name == "Tom (C0-B3,C5+)")
    {
        sec.name = "Tom (C1-B4,C6+)";
    }
    else if (sec.name == "Snare (D4)")
    {
        sec.name = "Snare (D5)";
    }
    else if (sec.name == "Hihat (E4-G4)")
    {
        sec.name = "Hihat (E5-G5)";
    }
    else if (sec.name == "Cymbal (A4)")
    {
        sec.name = "Cymbal (A5)";
    }
    else if (sec.name == "Percussion (B4)")
    {
        sec.name = "Percussion (B5)";
    }
#endif
    if (elements != nullptr && count > 0)
    {
        sec.elements.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            sec.elements.push_back(Patch::Metadata::EditorLayout::EditorElement{
                .type =
                  static_cast<Patch::Metadata::EditorLayout::EditorElement::Type>(elements[i].type),
                .id = elements[i].id,
            });
        }
    }
    ctx->metadata->layoutFlat.sections.push_back(std::move(sec));
}

int hostMetaAdd33Section(void* metaBuilder,
                         const char* name,
                         const char* shortName,
                         const PsynLayoutElement* elements,
                         int count)
{
    auto* ctx = static_cast<MetaBuilderContext*>(metaBuilder);
    if (ctx == nullptr || ctx->metadata == nullptr)
    {
        return -1;
    }
    Patch::Metadata::EditorLayout::EditorSection sec{};
    sec.name = name != nullptr ? name : "";
    sec.shortName = shortName != nullptr ? shortName : sec.name;
#ifdef PRODUCT_TRACKER_PLUS
    if (sec.name == "Kick (C4)")
    {
        sec.name = "Kick (C5)";
    }
    else if (sec.name == "Tom (C0-B3,C5+)")
    {
        sec.name = "Tom (C1-B4,C6+)";
    }
    else if (sec.name == "Snare (D4)")
    {
        sec.name = "Snare (D5)";
    }
    else if (sec.name == "Hihat (E4-G4)")
    {
        sec.name = "Hihat (E5-G5)";
    }
    else if (sec.name == "Cymbal (A4)")
    {
        sec.name = "Cymbal (A5)";
    }
    else if (sec.name == "Percussion (B4)")
    {
        sec.name = "Percussion (B5)";
    }
#endif
    if (elements != nullptr && count > 0)
    {
        sec.elements.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            sec.elements.push_back(Patch::Metadata::EditorLayout::EditorElement{
                .type =
                  static_cast<Patch::Metadata::EditorLayout::EditorElement::Type>(elements[i].type),
                .id = elements[i].id,
            });
        }
    }
    ctx->metadata->layout33.sections.push_back(std::move(sec));
    return static_cast<int>(ctx->metadata->layout33.sections.size()) - 1;
}

void hostMetaAdd33Subsection(void* metaBuilder,
                             int sectionIdx,
                             const char* name,
                             const PsynLayoutElement* elements,
                             int count)
{
    auto* ctx = static_cast<MetaBuilderContext*>(metaBuilder);
    if (ctx == nullptr || ctx->metadata == nullptr || sectionIdx < 0 ||
        sectionIdx >= static_cast<int>(ctx->metadata->layout33.sections.size()))
    {
        return;
    }
    Patch::Metadata::EditorLayout::EditorSubsection sub{};
    sub.name = name != nullptr ? name : "";
    if (elements != nullptr && count > 0)
    {
        sub.elements.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            sub.elements.push_back(Patch::Metadata::EditorLayout::EditorElement{
                .type =
                  static_cast<Patch::Metadata::EditorLayout::EditorElement::Type>(elements[i].type),
                .id = elements[i].id,
            });
        }
    }
    ctx->metadata->layout33.sections[sectionIdx].subsections.push_back(std::move(sub));
}

void hostMetaSetDefaultMacro(void* metaBuilder, int macroIdx, const PsynMacroDesc* desc)
{
    auto* ctx = static_cast<MetaBuilderContext*>(metaBuilder);
    if (ctx == nullptr || ctx->metadata == nullptr || desc == nullptr || macroIdx < 0 ||
        macroIdx >= static_cast<int>(kMacroCount))
    {
        return;
    }
    ctx->hasDefaultMacros = true;
    auto& m = ctx->defaultMacros[macroIdx];
    m.displayName = desc->display_name != nullptr ? desc->display_name : "";
    m.bipolar = (desc->bipolar != 0);
    m.modifiers.clear();
    int modCount = std::min<int>(desc->modifier_count, 8);
    for (int i = 0; i < modCount; ++i)
    {
        if (desc->modifiers[i].param_id >= 0)
        {
            m.modifiers.push_back(Patch::Metadata::Macro::ParamModifier{
                .paramId = desc->modifiers[i].param_id,
                .amount = desc->modifiers[i].amount,
            });
        }
    }
}

int32_t hostReadFile(const char* path, void* buffer, uint32_t maxLen)
{
    FileSystem* fs = s_samplesFs != nullptr ? s_samplesFs : s_synthsFs;
    if (fs == nullptr || path == nullptr || buffer == nullptr || maxLen == 0)
    {
        return -1;
    }
    auto fhOpt = fs->open(path, FileSystem::OpenOptions::kNone);
    if (!fhOpt)
    {
        return -1;
    }
    FileSystem::FileHandle fh = *fhOpt;
    ScopeExit closeGuard([fs, fh]() { fs->close(fh); });
    size_t readBytes =
      static_cast<size_t>(fs->read(fh, std::span<uint8_t>(static_cast<uint8_t*>(buffer), maxLen)));
    return static_cast<int32_t>(readBytes);
}

int32_t hostWriteFile(const char* path, const void* data, uint32_t len)
{
    FileSystem* fs = s_samplesFs != nullptr ? s_samplesFs : s_synthsFs;
    if (fs == nullptr || path == nullptr || (data == nullptr && len > 0))
    {
        return -1;
    }
    auto fhOpt = fs->open(path, FileSystem::OpenOptions::kWrite);
    if (!fhOpt)
    {
        return -1;
    }
    FileSystem::FileHandle fh = *fhOpt;
    ScopeExit closeGuard([fs, fh]() { fs->close(fh); });
    if (len == 0)
    {
        return 0;
    }
    bool ok = fs->write(fh, std::span<const uint8_t>(static_cast<const uint8_t*>(data), len));
    return ok ? static_cast<int32_t>(len) : -1;
}

bool hostLoadWavFile(const char* path,
                     int16_t** outPcmData,
                     uint32_t* outSampleCount,
                     uint8_t* outChannels,
                     uint32_t* outSampleRate)
{
    FileSystem* fs = s_samplesFs != nullptr ? s_samplesFs : s_synthsFs;
    if (fs == nullptr || path == nullptr || outPcmData == nullptr || outSampleCount == nullptr)
    {
        return false;
    }
    *outPcmData = nullptr;
    *outSampleCount = 0;

    auto fhOpt = fs->open(path, FileSystem::OpenOptions::kNone);
    if (!fhOpt)
    {
        return false;
    }
    FileSystem::FileHandle fh = *fhOpt;
    ScopeExit closeGuard([fs, fh]() { fs->close(fh); });

    uint8_t riffHdr[12]{};
    if (static_cast<size_t>(fs->read(fh, std::span<uint8_t>(riffHdr, 12))) != 12 ||
        std::memcmp(riffHdr, "RIFF", 4) != 0 || std::memcmp(riffHdr + 8, "WAVE", 4) != 0)
    {
        return false;
    }

    uint16_t audioFormat = 0;
    uint16_t numChannels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    uint32_t dataSize = 0;
    bool foundFmt = false;
    bool foundData = false;

    for (int chunkIter = 0; !foundData && chunkIter < 32; ++chunkIter)
    {
        uint8_t chunkHdr[8]{};
        if (static_cast<size_t>(fs->read(fh, std::span<uint8_t>(chunkHdr, 8))) != 8)
        {
            break;
        }
        uint32_t chunkSize = 0;
        std::memcpy(&chunkSize, chunkHdr + 4, 4);

        if (std::memcmp(chunkHdr, "fmt ", 4) == 0)
        {
            if (chunkSize < 16 || chunkSize > 0x7FFFFFFEu)
            {
                return false;
            }
            uint8_t fmtBuf[16]{};
            if (static_cast<size_t>(fs->read(fh, std::span<uint8_t>(fmtBuf, 16))) != 16)
            {
                return false;
            }
            std::memcpy(&audioFormat, fmtBuf + 0, 2);
            std::memcpy(&numChannels, fmtBuf + 2, 2);
            std::memcpy(&sampleRate, fmtBuf + 4, 4);
            std::memcpy(&bitsPerSample, fmtBuf + 14, 2);
            foundFmt = true;
            uint32_t rem = ((chunkSize + 1u) & ~1u) - 16u;
            if (rem > 0 && !fs->seek(fh, static_cast<int>(rem)))
            {
                return false;
            }
        }
        else if (std::memcmp(chunkHdr, "data", 4) == 0)
        {
            dataSize = chunkSize;
            foundData = true;
        }
        else
        {
            if (chunkSize > 0x7FFFFFFEu)
            {
                return false;
            }
            uint32_t paddedSize = (chunkSize + 1u) & ~1u;
            if (paddedSize > 0 && !fs->seek(fh, static_cast<int>(paddedSize)))
            {
                return false;
            }
        }
    }

    const bool supportedFormat =
      (audioFormat == 1 && (bitsPerSample == 8 || bitsPerSample == 16 || bitsPerSample == 24 ||
                            bitsPerSample == 32)) ||
      (audioFormat == 3 && bitsPerSample == 32);
    if (!foundFmt || !foundData || !supportedFormat || numChannels == 0 || dataSize == 0)
    {
        return false;
    }
    uint32_t bytesPerSample = (bitsPerSample / 8u) * numChannels;
    if (bytesPerSample == 0)
    {
        return false;
    }
    uint32_t frameCount = dataSize / bytesPerSample;
    constexpr uint32_t kMaxWavFrames = (PsynLoader::kArenaSize / 2u) / sizeof(int16_t);
    if (frameCount == 0 || frameCount > kMaxWavFrames)
    {
        return false;
    }

    auto* dst = static_cast<int16_t*>(hostSlotAlloc(frameCount * sizeof(int16_t), 4));
    if (dst == nullptr)
    {
        return false;
    }

    if (audioFormat == 1 && bitsPerSample == 16 && numChannels == 1)
    {
        size_t bytesToRead = frameCount * sizeof(int16_t);
        if (static_cast<size_t>(
              fs->read(fh, std::span<uint8_t>(reinterpret_cast<uint8_t*>(dst), bytesToRead))) !=
            bytesToRead)
        {
            hostSlotFree(dst);
            return false;
        }
    }
    else
    {
        uint8_t tmp[512];
        uint32_t framesPerChunk = static_cast<uint32_t>(sizeof(tmp)) / bytesPerSample;
        if (framesPerChunk == 0)
        {
            hostSlotFree(dst);
            return false;
        }
        uint32_t framesRead = 0;
        while (framesRead < frameCount)
        {
            uint32_t toRead = std::min(frameCount - framesRead, framesPerChunk);
            size_t chunkBytes = toRead * bytesPerSample;
            if (static_cast<size_t>(fs->read(fh, std::span<uint8_t>(tmp, chunkBytes))) != chunkBytes)
            {
                break;
            }
            for (uint32_t f = 0; f < toRead; ++f)
            {
                const uint8_t* p = tmp + f * bytesPerSample;
                int32_t sample = 0;
                if (audioFormat == 1 && bitsPerSample == 8)
                {
                    sample = (static_cast<int32_t>(p[0]) - 128) << 8;
                }
                else if (audioFormat == 1 && bitsPerSample == 16)
                {
                    int16_t s16 = 0;
                    std::memcpy(&s16, p, 2);
                    sample = s16;
                }
                else if (audioFormat == 1 && bitsPerSample == 24)
                {
                    int32_t s24 = (static_cast<int32_t>(p[2]) << 24) |
                                  (static_cast<int32_t>(p[1]) << 16) |
                                  (static_cast<int32_t>(p[0]) << 8);
                    sample = s24 >> 16;
                }
                else if (audioFormat == 1 && bitsPerSample == 32)
                {
                    int32_t s32 = 0;
                    std::memcpy(&s32, p, 4);
                    sample = s32 >> 16;
                }
                else if (audioFormat == 3 && bitsPerSample == 32)
                {
                    float flt = 0.0f;
                    std::memcpy(&flt, p, 4);
                    sample = static_cast<int32_t>(std::clamp(flt, -1.0f, 1.0f) * 32767.0f);
                }
                dst[framesRead + f] = static_cast<int16_t>(std::clamp<int32_t>(sample, -32768, 32767));
            }
            framesRead += toRead;
        }
        frameCount = framesRead;
    }

    *outPcmData = dst;
    *outSampleCount = frameCount;
    if (outChannels != nullptr)
    {
        *outChannels = 1;
    }
    if (outSampleRate != nullptr)
    {
        *outSampleRate = sampleRate;
    }
    return true;
}

void hostFreeWavFile(int16_t* pcmData)
{
    if (pcmData != nullptr)
    {
        hostSlotFree(pcmData);
    }
}

const PsynHostApi s_hostApi = {
    .api_version = PSYN_API_VERSION,
    .sample_rate = static_cast<float>(kAudioSampleRate),
    .block_size = static_cast<uint32_t>(kAudioBufferSize),
    .slot_alloc = &hostSlotAlloc,
    .slot_free = &hostSlotFree,
    .patch_get_param_value = &hostPatchGetParamValue,
    .patch_get_param_final_value = &hostPatchGetParamFinalValue,
    .patch_set_param_value = &hostPatchSetParamValue,
    .patch_get_sample = &hostPatchGetSample,
    .meta_set_info = &hostMetaSetInfo,
    .meta_set_param_count = &hostMetaSetParamCount,
    .meta_set_param = &hostMetaSetParam,
    .meta_add_sample_slot = &hostMetaAddSampleSlot,
    .meta_add_flat_section = &hostMetaAddFlatSection,
    .meta_add_33_section = &hostMetaAdd33Section,
    .meta_add_33_subsection = &hostMetaAdd33Subsection,
    .meta_set_default_macro = &hostMetaSetDefaultMacro,
    .read_file = &hostReadFile,
    .write_file = &hostWriteFile,
    .load_wav_file = &hostLoadWavFile,
    .free_wav_file = &hostFreeWavFile,
};

// -----------------------------------------------------------------------------
// .psyn Image Loader & Relocator
// -----------------------------------------------------------------------------
std::optional<FileSystem::FileHandle> openPsynFile(FileSystem& fs, const char* filename)
{
    std::string withSlash = std::string("/") + filename;
    auto fh = fs.open(withSlash, FileSystem::OpenOptions::kNone);
    if (fh)
    {
        return fh;
    }
    return fs.open(filename, FileSystem::OpenOptions::kNone);
}

const PsynPluginApi* ensurePluginLoaded(PluginRecord& rec)
{
    rec.lastUsedTick = ++s_tickCounter;
    if (rec.pluginApi != nullptr)
    {
        return rec.pluginApi;
    }
    if (!rec.valid || s_synthsFs == nullptr || !s_arena.isInitialized())
    {
        return nullptr;
    }

    size_t loadedSize =
      rec.header.code_size + rec.header.rodata_size + rec.header.data_size;
    size_t totalMemSize = loadedSize + rec.header.bss_size;
    if (loadedSize == 0 || totalMemSize > PsynLoader::kArenaSize)
    {
        return nullptr;
    }

    auto* imageBase = static_cast<uint8_t*>(allocWithEviction(totalMemSize, 32, &rec, rec.index));
    if (imageBase == nullptr)
    {
        return nullptr;
    }

    auto fhOpt = openPsynFile(*s_synthsFs, rec.filename);
    if (!fhOpt)
    {
        s_arena.free(imageBase);
        return nullptr;
    }
    FileSystem::FileHandle fh = *fhOpt;
    ScopeExit closeGuard([fh]() { s_synthsFs->close(fh); });

    // Skip 128-byte header
    uint8_t hdrSkip[sizeof(PsynHeader)];
    if (static_cast<size_t>(
          s_synthsFs->read(fh, std::span<uint8_t>(hdrSkip, sizeof(PsynHeader)))) !=
        sizeof(PsynHeader))
    {
        s_arena.free(imageBase);
        return nullptr;
    }

    // Read code + rodata + data
    if (static_cast<size_t>(s_synthsFs->read(fh, std::span<uint8_t>(imageBase, loadedSize))) !=
        loadedSize)
    {
        s_arena.free(imageBase);
        return nullptr;
    }

    // Zero .bss
    if (rec.header.bss_size > 0)
    {
        std::memset(imageBase + loadedSize, 0, rec.header.bss_size);
    }

    // Apply relocations in buffered chunks
    uintptr_t baseAddr = reinterpret_cast<uintptr_t>(imageBase);
    uint32_t relocsRemaining = rec.header.reloc_count;
    uint32_t relocBuf[64];
    size_t bufPos = 0;
    size_t bufCount = 0;

    auto nextRelocWord = [&](uint32_t& outWord) -> bool
    {
        if (bufPos >= bufCount)
        {
            if (relocsRemaining == 0)
            {
                return false;
            }
            uint32_t toRead = std::min<uint32_t>(relocsRemaining, 64u);
            size_t bytesToRead = toRead * sizeof(uint32_t);
            if (static_cast<size_t>(s_synthsFs->read(
                  fh,
                  std::span<uint8_t>(reinterpret_cast<uint8_t*>(relocBuf), bytesToRead))) !=
                bytesToRead)
            {
                return false;
            }
            relocsRemaining -= toRead;
            bufPos = 0;
            bufCount = toRead;
        }
        outWord = relocBuf[bufPos++];
        return true;
    };

    while (relocsRemaining > 0 || bufPos < bufCount)
    {
        uint32_t word = 0;
        if (!nextRelocWord(word))
        {
            s_arena.free(imageBase);
            return nullptr;
        }
        uint32_t tag = word & 0xC0000000u;
        uint32_t offset = word & 0x3FFFFFFFu;

        if (tag == 0x00000000u) // R_ARM_ABS32 / R_ARM_TARGET1
        {
            if (offset + 4u <= loadedSize)
            {
                uint32_t val = 0;
                std::memcpy(&val, imageBase + offset, 4);
                val += static_cast<uint32_t>(baseAddr);
                std::memcpy(imageBase + offset, &val, 4);
            }
        }
        else if (tag == 0x40000000u || tag == 0x80000000u) // MOVW / MOVT
        {
            uint32_t symOffset = 0;
            if (!nextRelocWord(symOffset))
            {
                s_arena.free(imageBase);
                return nullptr;
            }
            if (offset + 4u <= loadedSize)
            {
                uint32_t targetAddr = symOffset + static_cast<uint32_t>(baseAddr);
                uint16_t imm16 = (tag == 0x40000000u)
                                   ? static_cast<uint16_t>(targetAddr & 0xFFFFu)
                                   : static_cast<uint16_t>((targetAddr >> 16) & 0xFFFFu);
                patchThumbMovwMovt(imageBase + offset, imm16);
            }
        }
    }

    // Flush D-Cache and invalidate I-Cache before executing code in SDRAM
    syncInstructionCache(imageBase, totalMemSize);

    {
        ScopedPluginContext pluginScope(&rec);

        // Call psyn_plugin_entry(&s_hostApi) first so psyn_bridge::g_host is initialized
        // before any .init_array static constructors run.
        auto entryFn = reinterpret_cast<PsynEntryFn>((baseAddr + rec.header.entry_offset) | 1u);
        const PsynPluginApi* api = entryFn(&s_hostApi);
        if (api == nullptr)
        {
            s_arena.free(imageBase);
            s_arena.freeAllOwnedBy(rec.index);
            return nullptr;
        }

        // Run .init_array static constructors
        if (rec.header.init_array_count > 0 &&
            rec.header.init_array_offset + rec.header.init_array_count * 4u <= loadedSize)
        {
            using InitFn = void (*)();
            auto* initTable =
              reinterpret_cast<const uint32_t*>(imageBase + rec.header.init_array_offset);
            for (uint32_t i = 0; i < rec.header.init_array_count; ++i)
            {
                uintptr_t fnAddr = initTable[i];
                if (fnAddr != 0)
                {
                    auto fn = reinterpret_cast<InitFn>(fnAddr | 1u);
                    fn();
                }
            }
        }

        rec.imageBase = imageBase;
        rec.imageSize = totalMemSize;
        rec.pluginApi = api;

        // Re-prime plugin-local static lookup tables (such as paramHandlers) that plugins
        // populate inside create_metadata() when an evicted plugin is reloaded directly
        // for voice creation.
        if (rec.metadataPrimedOnce && api->create_metadata != nullptr)
        {
            Patch::Metadata dummyMeta{};
            MetaBuilderContext dummyCtx{
                .metadata = &dummyMeta,
                .pluginIdx = rec.index,
            };
            api->create_metadata(nullptr, &dummyCtx);
        }

        return api;
    }
}

// -----------------------------------------------------------------------------
// InstrumentVoice Wrapper for a Loaded .psyn Engine
// -----------------------------------------------------------------------------
class PsynVoiceWrapper : public InstrumentVoice
{
  public:
    PsynVoiceWrapper(int pluginIdx, int voiceIdx)
      : pluginIdx_(pluginIdx)
    {
        if (pluginIdx_ >= 0 && pluginIdx_ < static_cast<int>(s_pluginCount))
        {
            auto& rec = s_plugins[pluginIdx_];
            const PsynPluginApi* api = ensurePluginLoaded(rec);
            if (api != nullptr && api->create_voice != nullptr)
            {
                rec.activeVoices++;
                ScopedPluginContext pluginScope(&rec);
                pluginVoice_ = api->create_voice(voiceIdx);
                if (pluginVoice_ == nullptr)
                {
                    rec.activeVoices--;
                }
            }
        }
    }

    ~PsynVoiceWrapper() override
    {
        if (pluginVoice_ != nullptr && pluginIdx_ >= 0 &&
            pluginIdx_ < static_cast<int>(s_pluginCount))
        {
            auto& rec = s_plugins[pluginIdx_];
            if (rec.pluginApi != nullptr && rec.pluginApi->destroy_voice != nullptr)
            {
                ScopedPluginContext pluginScope(&rec);
                rec.pluginApi->destroy_voice(pluginVoice_);
            }
            pluginVoice_ = nullptr;
            if (rec.activeVoices > 0)
            {
                rec.activeVoices--;
            }
        }
    }

    void setPatch(std::shared_ptr<Patch> patch) override
    {
        InstrumentVoice::setPatch(std::move(patch));
        ScopedPluginContext pluginScope(pluginRecord());
        syncPatchPointer();
    }

    void onMidiEvent(const InstrumentVoiceContext& ctx, const MidiEvent& event) override
    {
        const PsynPluginApi* api = getActiveApi();
        if (api == nullptr || api->voice_on_midi_event == nullptr)
        {
            return;
        }
        ScopedPluginContext pluginScope(pluginRecord());
        syncPatchPointer();
        PsynVoiceContext pctx{
            .tempo = ctx.tempo,
            .pitchbend_range_semitones = ctx.pitchbendRangeSemitones,
        };
        PsynMidiEvent pev{};
        pev.type = static_cast<uint8_t>(event.type);
        pev.channel = event.channel;
        if (event.type == MidiEvent::Type::kPitchBendChange)
        {
            pev.pitch_bend = event.data.pitchBendChange.value;
        }
        else if (event.type == MidiEvent::Type::kControlChange)
        {
            pev.data1 = event.data.controlChange.id;
            pev.data2 = event.data.controlChange.value;
        }
        else if (event.type == MidiEvent::Type::kProgramChange)
        {
            pev.data1 = event.data.programChange.value;
        }
        else
        {
            pev.data1 = event.data.note.note;
            pev.data2 = event.data.note.velocity;
        }
        api->voice_on_midi_event(pluginVoice_, &pctx, &pev);
    }

    void onParamChange(int paramId, Patch::optional_paramvalue_t newValue) override
    {
        const PsynPluginApi* api = getActiveApi();
        if (api == nullptr || api->voice_on_param_change == nullptr)
        {
            return;
        }
        ScopedPluginContext pluginScope(pluginRecord());
        syncPatchPointer();
        api->voice_on_param_change(
          pluginVoice_, paramId, newValue.has_value(), newValue.value_or(0.0f));
    }

    void renderVoiceAudio(const InstrumentVoiceContext& ctx,
                          std::optional<AudioBufferStereoRef> /*inputBuffer*/,
                          AudioBufferStereoRef outputBuffer)
    {
        const PsynPluginApi* api = getActiveApi();
        if (api == nullptr || api->voice_render_audio == nullptr)
        {
            std::ranges::fill(outputBuffer[0], 0.0f);
            std::ranges::fill(outputBuffer[1], 0.0f);
            return;
        }
        ScopedPluginContext pluginScope(pluginRecord());
        syncPatchPointer();
        PsynVoiceContext pctx{
            .tempo = ctx.tempo,
            .pitchbend_range_semitones = ctx.pitchbendRangeSemitones,
        };
        api->voice_render_audio(
          pluginVoice_, &pctx, outputBuffer[0].data(), outputBuffer[1].data(), nullptr, 0);
    }

    void renderVoiceAudio(const InstrumentVoiceContext& ctx,
                          std::optional<AudioBufferStereoRef> inputBuffer,
                          AudioBufferStereoRef outputBuffer,
                          std::span<AudioBufferRef> auxOutBuffer)
    {
        const PsynPluginApi* api = getActiveApi();
        if (api == nullptr || api->voice_render_audio == nullptr)
        {
            std::ranges::fill(outputBuffer[0], 0.0f);
            std::ranges::fill(outputBuffer[1], 0.0f);
            for (auto buf : auxOutBuffer)
            {
                std::ranges::fill(buf, 0.0f);
            }
            return;
        }
        ScopedPluginContext pluginScope(pluginRecord());
        syncPatchPointer();
        PsynVoiceContext pctx{
            .tempo = ctx.tempo,
            .pitchbend_range_semitones = ctx.pitchbendRangeSemitones,
        };
        if ((pluginRecord()->header.flags & PSYN_FLAG_AUDIO_INPUT) != 0 &&
            api->voice_render_audio_in != nullptr)
        {
            PsynAudioInput in{};
            if (inputBuffer)
            {
                in.left = (*inputBuffer)[0].data();
                in.right = (*inputBuffer)[1].data();
            }
            if (ctx.sidechainBuffer)
            {
                in.sidechain_left = (*ctx.sidechainBuffer)[0].data();
                in.sidechain_right = (*ctx.sidechainBuffer)[1].data();
            }
            api->voice_render_audio_in(
              pluginVoice_, &pctx, &in, outputBuffer[0].data(), outputBuffer[1].data());
            return;
        }
        float* auxPtrs[2] = {
            auxOutBuffer.size() > 0 ? auxOutBuffer[0].data() : nullptr,
            auxOutBuffer.size() > 1 ? auxOutBuffer[1].data() : nullptr,
        };
        api->voice_render_audio(pluginVoice_,
                                &pctx,
                                outputBuffer[0].data(),
                                outputBuffer[1].data(),
                                auxPtrs,
                                static_cast<int>(std::min<size_t>(auxOutBuffer.size(), 2)));
    }

    void renderPostProcessAudio(const InstrumentVoiceContext& ctx,
                                AudioBufferStereoRef outputBuffer) override
    {
        const PsynPluginApi* api = getActiveApi();
        if (api == nullptr || api->voice_render_post_process == nullptr)
        {
            return;
        }
        ScopedPluginContext pluginScope(pluginRecord());
        syncPatchPointer();
        PsynVoiceContext pctx{
            .tempo = ctx.tempo,
            .pitchbend_range_semitones = ctx.pitchbendRangeSemitones,
        };
        api->voice_render_post_process(
          pluginVoice_, &pctx, outputBuffer[0].data(), outputBuffer[1].data());
    }

    void renderGraph(std::span<float> points) override
    {
        const PsynPluginApi* api = getActiveApi();
        if (api == nullptr || api->voice_render_graph == nullptr)
        {
            std::ranges::fill(points, 0.0f);
            return;
        }
        ScopedPluginContext pluginScope(pluginRecord());
        api->voice_render_graph(pluginVoice_, points.data(), static_cast<int>(points.size()));
    }

    bool isActive() override
    {
        const PsynPluginApi* api = getActiveApi();
        if (api == nullptr || api->voice_is_active == nullptr)
        {
            return false;
        }
        ScopedPluginContext pluginScope(pluginRecord());
        return api->voice_is_active(pluginVoice_);
    }

    bool handlesNote(note_t note) override
    {
        const PsynPluginApi* api = getActiveApi();
        if (api == nullptr || api->voice_handles_note == nullptr)
        {
            return false;
        }
        ScopedPluginContext pluginScope(pluginRecord());
        return api->voice_handles_note(pluginVoice_, note);
    }

    bool handlesAnyNote() override
    {
        const PsynPluginApi* api = getActiveApi();
        if (api == nullptr || api->voice_handles_any_note == nullptr)
        {
            return false;
        }
        ScopedPluginContext pluginScope(pluginRecord());
        return api->voice_handles_any_note(pluginVoice_);
    }

    tiny_optional<note_t> getLastNote() override
    {
        const PsynPluginApi* api = getActiveApi();
        if (api != nullptr && api->voice_get_last_note != nullptr)
        {
            ScopedPluginContext pluginScope(pluginRecord());
            int n = api->voice_get_last_note(pluginVoice_);
            if (n >= 0 && n <= 127)
            {
                return static_cast<note_t>(n);
            }
        }
        return {};
    }

    tiny_optional<float> getCurrentNoteVelocity() override
    {
        const PsynPluginApi* api = getActiveApi();
        if (api != nullptr && api->voice_get_current_velocity != nullptr)
        {
            ScopedPluginContext pluginScope(pluginRecord());
            float vel = 0.0f;
            if (api->voice_get_current_velocity(pluginVoice_, &vel))
            {
                return vel;
            }
        }
        return {};
    }

    void clearPreviousNotes() override
    {
        const PsynPluginApi* api = getActiveApi();
        if (api != nullptr && api->voice_clear_previous_notes != nullptr)
        {
            ScopedPluginContext pluginScope(pluginRecord());
            api->voice_clear_previous_notes(pluginVoice_);
        }
    }

    void reset() override
    {
        const PsynPluginApi* api = getActiveApi();
        if (api != nullptr && api->voice_reset != nullptr)
        {
            ScopedPluginContext pluginScope(pluginRecord());
            api->voice_reset(pluginVoice_);
        }
    }

  private:
    PluginRecord* pluginRecord() const
    {
        return (pluginIdx_ >= 0 && pluginIdx_ < static_cast<int>(s_pluginCount))
                 ? &s_plugins[pluginIdx_]
                 : nullptr;
    }
    const PsynPluginApi* getActiveApi() const
    {
        if (pluginVoice_ == nullptr || pluginIdx_ < 0 ||
            pluginIdx_ >= static_cast<int>(s_pluginCount))
        {
            return nullptr;
        }
        return s_plugins[pluginIdx_].pluginApi;
    }

    void syncPatchPointer()
    {
        const PsynPluginApi* api = getActiveApi();
        Patch* cur = getPatch();
        if (api != nullptr && api->voice_set_patch != nullptr && cur != lastPatch_)
        {
            lastPatch_ = cur;
            api->voice_set_patch(pluginVoice_, cur);
        }
    }

    int pluginIdx_ = -1;
    void* pluginVoice_ = nullptr;
    Patch* lastPatch_ = nullptr;
};

// -----------------------------------------------------------------------------
// Silent Fallback Synth Voice (used only when /Synths/ has no .psyn files)
// -----------------------------------------------------------------------------
class SilentFallbackVoice : public InstrumentVoice
{
  public:
    void onMidiEvent(const InstrumentVoiceContext&, const MidiEvent&) override {}
    void onParamChange(int, Patch::optional_paramvalue_t) override {}
    void renderVoiceAudio(const InstrumentVoiceContext&,
                          std::optional<AudioBufferStereoRef>,
                          AudioBufferStereoRef)
    {
    }
    void renderVoiceAudio(const InstrumentVoiceContext&,
                          std::optional<AudioBufferStereoRef>,
                          AudioBufferStereoRef,
                          std::span<AudioBufferRef>)
    {
    }
    bool isActive() override { return false; }
    bool handlesNote(note_t) override { return false; }
    bool handlesAnyNote() override { return false; }
    tiny_optional<note_t> getLastNote() override { return {}; }
    tiny_optional<float> getCurrentNoteVelocity() override { return {}; }

    static void createMetadata(const Patch&, Patch::Metadata& metadata)
    {
        metadata.instrumentTypeId = 1;
        metadata.outputChannels = Patch::Metadata::OutputChannels::kMono;
        metadata.maxVoices = 8;
        metadata.params.clear();
        metadata.params.push_back(Patch::Metadata::Param{
            .displayName = "Gain",
            .defaultValue = 0.0f,
            .minValue = -95.0f,
            .maxValue = 6.0f,
        });
        metadata.layout33.sections.clear();
        metadata.layout23.sections.clear();
        metadata.layoutFlat.sections.clear();
        Patch::Metadata::EditorLayout::EditorSection sec{};
        sec.name = "Main";
        sec.shortName = "Main";
        sec.elements.push_back(
          { Patch::Metadata::EditorLayout::EditorElement::Type::kParam, 0 });
        metadata.layoutFlat.sections.push_back(std::move(sec));
    }
};

std::unique_ptr<InstrumentType> createPsynInstrumentType(int pluginIdx)
{
    const auto& rec = s_plugins[pluginIdx];
    return std::make_unique<InstrumentType>(InstrumentType{
        .name = rec.header.code_name,
        .createMetadata =
          [pluginIdx](const Patch& patch, Patch::Metadata& metadata)
        {
            if (pluginIdx < 0 || pluginIdx >= static_cast<int>(s_pluginCount))
            {
                return;
            }
            auto& p = s_plugins[pluginIdx];
            metadata.instrumentTypeId = p.typeId;
            metadata.outputChannels = static_cast<Patch::Metadata::OutputChannels>(
              std::clamp<int>(p.header.output_channels, 0, 2));
            metadata.rendersAuxSends = (p.header.renders_aux_sends != 0);
            metadata.maxVoices = p.header.max_voices > 0 ? p.header.max_voices : 8;
            metadata.sampleSlots.clear();
            metadata.layout33.sections.clear();
            metadata.layout23.sections.clear();
            metadata.layoutFlat.sections.clear();

            const PsynPluginApi* api = ensurePluginLoaded(p);
            if (api != nullptr && api->create_metadata != nullptr)
            {
                MetaBuilderContext ctx{
                    .metadata = &metadata,
                    .pluginIdx = pluginIdx,
                };
                {
                    ScopedPluginContext pluginScope(&p);
                    api->create_metadata(&patch, &ctx);
                    p.metadataPrimedOnce = true;
                }
                metadata.instrumentTypeId = p.typeId;
                const int headerMaxVoices = p.header.max_voices > 0 ? p.header.max_voices : 8;
                metadata.maxVoices = std::clamp<int>(metadata.maxVoices, 1, headerMaxVoices);

                // Ensure every declared SampleSlot is represented in layoutFlat or layout33
                // before synthesizing the complementary layout.
                for (int slotId = static_cast<int>(metadata.sampleSlots.size()) - 1; slotId >= 0;
                     --slotId)
                {
                    const auto& slot = metadata.sampleSlots[slotId];
                    if (!metadata.layoutFlat.sections.empty())
                    {
                        bool foundInFlat = false;
                        for (const auto& sec : metadata.layoutFlat.sections)
                        {
                            for (const auto& el : sec.elements)
                            {
                                if (el.type ==
                                      Patch::Metadata::EditorLayout::EditorElement::Type::kSampleSlot &&
                                    el.id == slotId)
                                {
                                    foundInFlat = true;
                                    break;
                                }
                            }
                            if (foundInFlat)
                            {
                                break;
                            }
                        }
                        if (!foundInFlat)
                        {
                            size_t secIdx = std::min<size_t>(
                              slot.section, metadata.layoutFlat.sections.size() - 1);
                            auto& elems = metadata.layoutFlat.sections[secIdx].elements;
                            elems.insert(elems.begin(),
                                         Patch::Metadata::EditorLayout::EditorElement{
                                             .type = Patch::Metadata::EditorLayout::EditorElement::
                                               Type::kSampleSlot,
                                             .id = slotId,
                                         });
                        }
                    }
                    if (!metadata.layout33.sections.empty())
                    {
                        bool foundIn33 = false;
                        for (const auto& sec : metadata.layout33.sections)
                        {
                            for (const auto& el : sec.elements)
                            {
                                if (el.type ==
                                      Patch::Metadata::EditorLayout::EditorElement::Type::kSampleSlot &&
                                    el.id == slotId)
                                {
                                    foundIn33 = true;
                                    break;
                                }
                            }
                            for (const auto& sub : sec.subsections)
                            {
                                for (const auto& el : sub.elements)
                                {
                                    if (el.type == Patch::Metadata::EditorLayout::EditorElement::
                                                     Type::kSampleSlot &&
                                        el.id == slotId)
                                    {
                                        foundIn33 = true;
                                        break;
                                    }
                                }
                            }
                            if (foundIn33)
                            {
                                break;
                            }
                        }
                        if (!foundIn33)
                        {
                            size_t secIdx = std::min<size_t>(
                              slot.section, metadata.layout33.sections.size() - 1);
                            auto& sec33 = metadata.layout33.sections[secIdx];
                            if (sec33.subsections.empty())
                            {
                                sec33.subsections.push_back(
                                  Patch::Metadata::EditorLayout::EditorSubsection{
                                      .name = sec33.name,
                                      .elements = {},
                                  });
                            }
                            size_t subIdx = std::min<size_t>(
                              slot.subsection, sec33.subsections.size() - 1);
                            auto& elems = sec33.subsections[subIdx].elements;
                            elems.insert(elems.begin(),
                                         Patch::Metadata::EditorLayout::EditorElement{
                                             .type = Patch::Metadata::EditorLayout::
                                               EditorElement::Type::kSampleSlot,
                                             .id = slotId,
                                         });
                        }
                    }
                }

                if (ctx.hasDefaultMacros)
                {
                    metadata.initDefaultMacros =
                      [defaultMacros = std::move(ctx.defaultMacros)](
                        std::vector<Patch::Metadata::Macro>& macros)
                    {
                        for (size_t i = 0; i < std::min(macros.size(), defaultMacros.size()); ++i)
                        {
                            if (!defaultMacros[i].displayName.empty() ||
                                !defaultMacros[i].modifiers.empty())
                            {
                                macros[i] = defaultMacros[i];
                            }
                        }
                    };
                }
            }
        },
        .createVoice =
          [pluginIdx](int voiceIdx) -> std::unique_ptr<InstrumentVoice>
        { return std::make_unique<PsynVoiceWrapper>(pluginIdx, voiceIdx); },
    });
}

bool hasPsynExtension(std::string_view name)
{
    if (name.size() <= 5 || name[0] == '.')
    {
        return false;
    }
    std::string_view ext = name.substr(name.size() - 5);
    return (ext[0] == '.') && (std::tolower(static_cast<unsigned char>(ext[1])) == 'p') &&
           (std::tolower(static_cast<unsigned char>(ext[2])) == 's') &&
           (std::tolower(static_cast<unsigned char>(ext[3])) == 'y') &&
           (std::tolower(static_cast<unsigned char>(ext[4])) == 'n');
}

} // namespace

std::unique_ptr<InstrumentType> PsynLoader::createFallbackSynth()
{
    return std::make_unique<InstrumentType>(InstrumentType{
        .name = "NONE",
        .createMetadata = &SilentFallbackVoice::createMetadata,
        .createVoice = [](int) -> std::unique_ptr<InstrumentVoice>
        { return std::make_unique<SilentFallbackVoice>(); },
    });
}

void PsynLoader::scanAndRegister(FileSystem& synthsFs,
                                 InstrumentTypeRegistry& registry,
                                 const Synth::Allocator& largeBuffersAlloc,
                                 bool isWaveDevice)
{
    s_synthsFs = &synthsFs;

    if (!s_arena.isInitialized() && largeBuffersAlloc)
    {
        void* poolMem = largeBuffersAlloc(kArenaSize);
        if (poolMem != nullptr)
        {
            s_arena.init(poolMem, kArenaSize);
        }
    }

    // Ensure /Synths folder exists on SD card
    synthsFs.createFolder("");

    auto fileViews = synthsFs.listFiles(
      "", [](const std::string& name) { return hasPsynExtension(name); }, std::nullopt);

    std::vector<std::string> filenames;
    filenames.reserve(fileViews.size());
    for (auto sv : fileViews)
    {
        if (hasPsynExtension(sv))
        {
            filenames.emplace_back(sv);
        }
    }
    std::ranges::sort(filenames);

    s_plugins.clear();
    s_pluginCount = 0;
    bool replacedFallbackId1 = false;
    int nextDynamicId = 100;

    for (const auto& fname : filenames)
    {
        auto fhOpt = openPsynFile(synthsFs, fname.c_str());
        if (!fhOpt)
        {
            continue;
        }
        FileSystem::FileHandle fh = *fhOpt;
        PsynHeader hdr{};
        size_t bytesRead = static_cast<size_t>(
          synthsFs.read(fh, std::span<uint8_t>(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr))));
        synthsFs.close(fh);

        if (bytesRead != sizeof(PsynHeader) || hdr.magic != PSYN_MAGIC ||
            hdr.api_version != PSYN_API_VERSION || hdr.code_size == 0)
        {
            continue;
        }

        hdr.code_name[sizeof(hdr.code_name) - 1] = '\0';
        hdr.display_name[sizeof(hdr.display_name) - 1] = '\0';
        hdr.patch_folder[sizeof(hdr.patch_folder) - 1] = '\0';

        int requestedId = isWaveDevice ? static_cast<int>(hdr.type_id_wave)
                                       : static_cast<int>(hdr.type_id_play_tracker);
        int assignedId = requestedId;

        bool alreadyUsedByPlugin = false;
        for (size_t i = 0; i < s_pluginCount; ++i)
        {
            if (s_plugins[i].typeId == assignedId)
            {
                alreadyUsedByPlugin = true;
                break;
            }
        }

        if (assignedId <= 0 || alreadyUsedByPlugin)
        {
            while (registry.getInstrumentType(nextDynamicId) != nullptr && nextDynamicId < 999)
            {
                ++nextDynamicId;
            }
            assignedId = nextDynamicId++;
        }

        if (assignedId == 1)
        {
            replacedFallbackId1 = true;
        }

        int pluginIdx = static_cast<int>(s_pluginCount++);
        s_plugins.emplace_back();
        auto& rec = s_plugins[pluginIdx];
        rec.valid = true;
        rec.hasSampleSlots = ((hdr.flags & PSYN_FLAG_HAS_SAMPLE_SLOTS) != 0) ||
                             (std::strcmp(hdr.code_name, "WAVS") == 0) ||
                             (std::strcmp(hdr.code_name, "GRAIN") == 0) ||
                             (std::strcmp(hdr.code_name, "DWA") == 0);
        rec.index = pluginIdx;
        rec.typeId = assignedId;
        std::strncpy(rec.filename, fname.c_str(), sizeof(rec.filename) - 1);
        rec.header = hdr;
        if (rec.hasSampleSlots)
        {
            ensurePluginSampleFolder(rec);
        }

        registry.addInstrumentType(assignedId, createPsynInstrumentType(pluginIdx));

        SynthEngineDescriptor desc{};
        desc.typeId = assignedId;
        desc.codeName = rec.header.code_name;
        desc.displayName =
          rec.header.display_name[0] != '\0' ? rec.header.display_name : rec.header.code_name;
        desc.patchFolder =
          rec.header.patch_folder[0] != '\0' ? rec.header.patch_folder : rec.header.code_name;
        desc.defaultVoices = rec.header.default_voices > 0 ? rec.header.default_voices : 4;
        desc.maxVoices = rec.header.max_voices > 0 ? rec.header.max_voices : 8;
        desc.createInstrumentType = nullptr;
        registerSynthEngine(desc);
    }

    // If at least one .psyn plugin was discovered and ID 1 is still the silent "NONE" fallback,
    // remove the silent fallback so only real SD-card .psyn engines appear in the UI.
    if (s_pluginCount > 0 && !replacedFallbackId1)
    {
        registry.removeInstrumentType(1);
    }

    // Preload .psyn images into the SDRAM arena up to kMaxPreloadBytes so runtime engine
    // switching and preset browsing require zero SD card I/O.
    for (size_t i = 0; i < s_pluginCount; ++i)
    {
        auto& rec = s_plugins[i];
        size_t needed = rec.header.code_size + rec.header.rodata_size + rec.header.data_size +
                        rec.header.bss_size;
        if (s_arena.usedBytes() + needed <= kMaxPreloadBytes)
        {
            ensurePluginLoaded(rec);
        }
    }
}

void PsynLoader::setSamplesFileSystem(FileSystem& samplesFs)
{
    s_samplesFs = &samplesFs;
    s_samplesFs->createFolder("");
    s_samplesFs->createFolder("WAVS");
    s_samplesFs->createFolder("GRAIN");
    s_samplesFs->createFolder("DWA");
    for (size_t i = 0; i < s_pluginCount; ++i)
    {
        if (s_plugins[i].hasSampleSlots)
        {
            ensurePluginSampleFolder(s_plugins[i]);
        }
    }
}

} // namespace polysynth
