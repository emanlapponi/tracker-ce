#ifdef FEATURE_SYNTH

#include "synth_PolySynth.h"
#include "PolySynth/sdk/SynthVoiceSDK.h"

#include "PolySynth/Instrument.h"
#include "PolySynth/Synth.h"
#include "Synth.h"
#include "Platform.h"
#include "Tracker.h"
#include "config.h"
#include "synth_part_clip.h"
#include <span>
//#include "Audio.h"
//#include "dspinst.h'
//#include "util_math.h"
//#include <cr_section_macros.h>

namespace
{
constexpr int kSynthSourceBase = polysynth::Patch::Metadata::Param::kInputSourceSynthBase;
static_assert(kSynthSourceBase == PolySynth::kTrackInputs);
using BusInput = std::array<std::array<polysynth::sample_t, polysynth::kAudioBufferSize>, 2>;
using BusSidechain = std::array<polysynth::sample_t, polysynth::kAudioBufferSize>;

polysynth::AudioBufferStereoRef toStereoRef(BusInput& input)
{
    return { input[0], input[1] };
}

void addToBusInput(BusInput& input,
                   const audio_block_t* left,
                   const audio_block_t* right,
                   float gainLeft = 1.0f,
                   float gainRight = 1.0f)
{
    if (left == NULL)
    {
        return;
    }

    if (right == NULL)
    {
        right = left;
    }

    for (int i = 0; i < polysynth::kAudioBufferSize; i++)
    {
        input[0][i] += left->data[i] * (gainLeft / INT16_MAX);
        input[1][i] += right->data[i] * (gainRight / INT16_MAX);
    }
}

// Downmixed to mono: the sidechain only drives a detector, and SRAM_DTC headroom is tight.
void addToBusSidechain(BusSidechain& sidechain, const audio_block_t* left, const audio_block_t* right)
{
    if (left == NULL)
    {
        return;
    }

    if (right == NULL)
    {
        right = left;
    }

    for (int i = 0; i < polysynth::kAudioBufferSize; i++)
    {
        sidechain[i] += (left->data[i] + right->data[i]) * (0.5f / INT16_MAX);
    }
}
}

AUDIO_CORE_RAM void PolySynth::update(void)
{
    audio_block_t* trackBlocks[2 * kTrackInputs];
    for (int idx = 0; idx < 2 * kTrackInputs; idx++)
    {
        trackBlocks[idx] = receiveReadOnly(idx);
    }

    renderParts(trackBlocks);

    for (audio_block_t* block : trackBlocks)
    {
        if (block != NULL)
        {
            release(block);
        }
    }
}

AUDIO_CORE_RAM void PolySynth::renderParts(audio_block_t* const* trackBlocks)
{
    if (isBlocked)
    {
        return;
    }

    static constexpr uint32_t kMaxSynthRealtimeBudgetUs = 2100;
    static constexpr uint8_t kOverloadCooldownBlocks = 32;

    if (!AudioStream::renderMode && overloadCooldownBlocks > 0)
    {
        --overloadCooldownBlocks;
        return;
    }

    Timer* timer = (!AudioStream::renderMode && Tracker::getInstance() != nullptr &&
                    Tracker::getInstance()->getPlatform() != nullptr)
                     ? Tracker::getInstance()->getPlatform()->getTimer()
                     : nullptr;
    const uint32_t maxCycles =
      timer != nullptr ? kMaxSynthRealtimeBudgetUs * timer->clockCyclesPerUs() : 0;
    const uint32_t startCycles = timer != nullptr ? timer->clockCycles() : 0;

    static_assert(std::is_same<polysynth::sample_t, float>::value);

    static std::array<polysynth::sample_t, polysynth::kAudioBufferSize> tempBufSendsCommonLeft;
    static std::array<polysynth::sample_t, polysynth::kAudioBufferSize> tempBufSendsCommonRight;
    static std::array<polysynth::sample_t, polysynth::kAudioBufferSize> tempBufPostfaderLeft;
    static std::array<polysynth::sample_t, polysynth::kAudioBufferSize> tempBufPostfaderRight;

    static std::array<std::array<polysynth::sample_t, polysynth::kAudioBufferSize>, 2> tempBufSend;

    static std::array<BusInput, kSynthParts> tempBufBus;
    static std::array<BusSidechain, kSynthParts> tempBufSidechain;

    std::array<BusRoute, kSynthParts> routes;
    uint16_t sources = 0;
    for (int partIdx = 0; partIdx < kSynthParts; partIdx++)
    {
        routes[partIdx] = getBusRoute(partIdx);
        sources |= routes[partIdx].sources[kBusInput];
        if (routes[partIdx].isBus)
        {
            tempBufBus[partIdx][0].fill(0.0f);
            tempBufBus[partIdx][1].fill(0.0f);
            tempBufSidechain[partIdx].fill(0.0f);
        }
    }
    routedSources = sources;

    for (int partIdx = 0; partIdx < kSynthParts; partIdx++)
    {
        auto& part = trackerSynth->parts[partIdx];

        if (part.loading.load(std::memory_order_acquire) || part.instrument->getVoiceCount() == 0)
        {
            if (!AudioStream::renderMode)
            {
                continue;
            }

            audio_block_t* blockZero = allocate();
            if (blockZero == NULL)
            {
                return;
            }
            std::fill(std::begin(blockZero->data), std::end(blockZero->data), 0);
            transmit(blockZero, partIdx);
            transmit(blockZero, kRightChannelOffset + partIdx);
            transmit(blockZero, kSend1Offset + partIdx);
            transmit(blockZero, kSend2Offset + partIdx);
            release(blockZero);
            continue;
        }

#ifdef AUDIO_NULL_IS_SILENCE
        if (!AudioStream::renderMode && trackerSynth->synth != nullptr &&
            trackerSynth->synth->prepareSilentInstrument(partIdx))
        {
            continue;
        }
#endif

        if (partIdx > 0 && timer != nullptr && (timer->clockCycles() - startCycles) > maxCycles)
        {
            overloadCooldownBlocks = kOverloadCooldownBlocks;
            return;
        }

        bool isStereo =
          part.patch != nullptr && part.patch->getMetadata().outputChannels ==
                                     polysynth::Patch::Metadata::OutputChannels::kStereo;

        bool hasSends = part.patch != nullptr && part.patch->getMetadata().rendersAuxSends;
        const bool reuseUnmodifiedMono = (!hasSends && !isStereo && gainDB[partIdx] == 0.0f);

        audio_block_t* blockPostFaderLeft = allocate();
        audio_block_t* blockPostFaderRight = isStereo ? allocate() : NULL;
        audio_block_t* blockSendsCommonOrSend1 = allocate();
        audio_block_t* blockSend2 = hasSends ? allocate() : NULL;

        if (blockPostFaderLeft == NULL || (isStereo && blockPostFaderRight == NULL) ||
            blockSendsCommonOrSend1 == NULL || (hasSends && blockSend2 == NULL))
        {
            if (blockPostFaderLeft != NULL)
            {
                release(blockPostFaderLeft);
            }

            if (blockPostFaderRight != NULL)
            {
                release(blockPostFaderRight);
            }

            if (blockSendsCommonOrSend1 != NULL)
            {
                release(blockSendsCommonOrSend1);
            }

            if (blockSend2 != NULL)
            {
                release(blockSend2);
            }

            return;
        }

        std::array<polysynth::AudioBufferRef, 2> tempSendsBufArray = { tempBufSend[0],
                                                                       tempBufSend[1] };

        std::optional<polysynth::AudioBufferStereoRef> busInput;
        std::optional<polysynth::AudioBufferRef> busSidechain;
        if (routes[partIdx].isBus)
        {
            const uint16_t inputSources = routes[partIdx].sources[kBusInput];
            const uint16_t sidechainSources = routes[partIdx].sources[kBusSidechain];
            for (int track = 0; track < kTrackInputs; track++)
            {
                const uint16_t sourceBit = 1u << track;
                if (inputSources & sourceBit)
                {
                    addToBusInput(tempBufBus[partIdx],
                                  trackBlocks[2 * track],
                                  trackBlocks[2 * track + 1]);
                }
                if (sidechainSources & sourceBit)
                {
                    addToBusSidechain(tempBufSidechain[partIdx],
                                      trackBlocks[2 * track],
                                      trackBlocks[2 * track + 1]);
                }
            }

            busInput = toStereoRef(tempBufBus[partIdx]);
            if (sidechainSources != 0)
            {
                busSidechain = tempBufSidechain[partIdx];
            }
        }

        trackerSynth->synth->renderAudioBuffer(
          { tempBufSendsCommonLeft, tempBufSendsCommonRight },
          std::span<polysynth::AudioBufferRef>(tempSendsBufArray),
          partIdx,
          busInput,
          busSidechain);

        bool budgetExceeded =
          (timer != nullptr && (timer->clockCycles() - startCycles) > maxCycles);
        if (budgetExceeded)
        {
            overloadCooldownBlocks = kOverloadCooldownBlocks;
        }

        if (!hasSends && !isStereo)
        {
            static_assert(kSynthParts == 3);
            static part_clip::GainCache<kSynthParts> gainCache = {
                { __builtin_nanf(""), __builtin_nanf(""), __builtin_nanf("") },
                { 0.0f, 0.0f, 0.0f }
            };
            const float postGainAmp =
              reuseUnmodifiedMono
                ? 1.0f
                : gainCache.get(partIdx, gainDB[partIdx], polysynth::sdk::dbToAmplitude);

            part_clip::monoPart<polysynth::kAudioBufferSize>(
              tempBufSendsCommonLeft.data(),
              postGainAmp,
              reuseUnmodifiedMono,
              blockSendsCommonOrSend1->data,
              blockPostFaderLeft->data);

            transmit(blockSendsCommonOrSend1, kSend1Offset + partIdx);
            transmit(blockSendsCommonOrSend1, kSend2Offset + partIdx);
            release(blockSendsCommonOrSend1);
        }
        else
        {
            tempBufPostfaderLeft = tempBufSendsCommonLeft;
            if (isStereo)
            {
                tempBufPostfaderRight = tempBufSendsCommonRight;
            }

            polysynth::AudioBufferRef tempBufRef = tempBufSendsCommonLeft;

            if (hasSends)
            {
                auto* srcSend1 = tempBufSend[0].begin();
                auto* srcSend2 = tempBufSend[1].begin();

                auto* destSend1 = blockSendsCommonOrSend1->data;
                auto* destSend2 = blockSend2->data;

                for (int i = 0; i < polysynth::kAudioBufferSize; i++)
                {
                    *(destSend1++) = static_cast<int16_t>(*(srcSend1++) * INT16_MAX);
                    *(destSend2++) = static_cast<int16_t>(*(srcSend2++) * INT16_MAX);
                }
            }
            else
            {
                if (isStereo)
                {
                    for (int idx = 0; idx < polysynth::kAudioBufferSize; idx++)
                    {
                        tempBufSendsCommonLeft[idx] =
                          (tempBufSendsCommonLeft[idx] + tempBufSendsCommonRight[idx]) / 2.0f;
                    }
                }

                polysynth::sdk::saturateSoftClip(tempBufRef, 0.0f);

                auto* src = tempBufSendsCommonLeft.begin();
                auto* dest = blockSendsCommonOrSend1->data;

                for (int i = 0; i < polysynth::kAudioBufferSize; i++)
                {
                    *(dest++) = static_cast<int16_t>(*(src++) * INT16_MAX);
                }
            }

            transmit(blockSendsCommonOrSend1, kSend1Offset + partIdx);
            transmit(hasSends ? blockSend2 : blockSendsCommonOrSend1, kSend2Offset + partIdx);
            release(blockSendsCommonOrSend1);
            if (blockSend2 != NULL)
            {
                release(blockSend2);
            }

            tempBufRef = tempBufPostfaderLeft;
            polysynth::sdk::saturateSoftClip(tempBufRef, gainDB[partIdx]);

            if (isStereo)
            {
                tempBufRef = tempBufPostfaderRight;
                polysynth::sdk::saturateSoftClip(tempBufRef, gainDB[partIdx]);
            }

            if (isStereo)
            {
                auto* srcL = tempBufPostfaderLeft.begin();
                auto* srcR = tempBufPostfaderRight.begin();

                auto* destL = blockPostFaderLeft->data;
                auto* destR = blockPostFaderRight->data;

                for (int i = 0; i < polysynth::kAudioBufferSize; i++)
                {
                    *(destL++) = static_cast<int16_t>(*(srcL++) * INT16_MAX);
                    *(destR++) = static_cast<int16_t>(*(srcR++) * INT16_MAX);
                }
            }
            else
            {
                auto* src = tempBufPostfaderLeft.begin();
                auto* dest = blockPostFaderLeft->data;

                for (int i = 0; i < polysynth::kAudioBufferSize; i++)
                {
                    *(dest++) = static_cast<int16_t>(*(src++) * INT16_MAX);
                }
            }
        }

        const uint16_t sourceBit = 1u << (kSynthSourceBase + partIdx);
        for (int busIdx = partIdx + 1; busIdx < kSynthParts; busIdx++)
        {
            const audio_block_t* postFaderRight =
              isStereo ? blockPostFaderRight : blockPostFaderLeft;
            if (routes[busIdx].sources[kBusInput] & sourceBit)
            {
                // Tracks are tapped after their pan, so synth sources get their pan too
                addToBusInput(tempBufBus[busIdx],
                              blockPostFaderLeft,
                              postFaderRight,
                              (*panningAmplifiers)[partIdx][0].gain(),
                              (*panningAmplifiers)[partIdx][1].gain());
            }
            if (routes[busIdx].sources[kBusSidechain] & sourceBit)
            {
                addToBusSidechain(tempBufSidechain[busIdx], blockPostFaderLeft, postFaderRight);
            }
        }

        // Always sent: stems and USB still need it. The engine mutes the direct mix of routed parts.
        transmit(blockPostFaderLeft, partIdx);
        transmit(isStereo ? blockPostFaderRight : blockPostFaderLeft,
                 kRightChannelOffset + partIdx);
        release(blockPostFaderLeft);
        if (blockPostFaderRight != NULL)
        {
            release(blockPostFaderRight);
        }
        if (budgetExceeded)
        {
            break;
        }
    }
}

PolySynth::BusRoute PolySynth::getBusRoute(int partIdx) const
{
    BusRoute route;
    const auto& part = trackerSynth->parts[partIdx];
    // A part that is not rendered cannot take its sources, or they would go silent
    if (part.loading.load(std::memory_order_acquire) || part.patch == nullptr ||
        part.instrument->getVoiceCount() == 0)
    {
        return route;
    }

    const auto& params = part.patch->getMetadata().params;
    for (int paramId = 0; paramId < static_cast<int>(params.size()); paramId++)
    {
        const int source = params[paramId].inputSource;
        if (source < 0)
        {
            continue;
        }

        route.isBus = true;
        const bool isAvailable = source < kSynthSourceBase || source - kSynthSourceBase < partIdx;
        if (isAvailable && part.patch->getParamFinalValue(paramId).value_or(0.0f) > 0.5f)
        {
            route.sources[params[paramId].inputSidechain ? kBusSidechain : kBusInput] |=
              1u << source;
        }
    }
    return route;
}

void PolySynth::init(Synth* trackerSynth, const PanningAmplifiers* panningAmplifiers)
{
    PolySynth::trackerSynth = trackerSynth;
    PolySynth::panningAmplifiers = panningAmplifiers;
    isBlocked = false;
}

AUDIO_CORE_FLASH void PolySynth::setGain(int idx, float gainDB)
{
    assert(idx < kSynthParts);
    PolySynth::gainDB[idx] = gainDB;
}

#endif
