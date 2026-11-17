#pragma once

#include "AudioStream.h"
#include "amplifier.h"
#include <array>

class Synth;

class PolySynth : public AudioStream
{
  public:
    static constexpr int kTrackInputs = 8;

    PolySynth(void)
      : AudioStream(2 * kTrackInputs, inputQueue)
    {
    }

    using PanningAmplifiers = AudioAmplifier[kSynthParts][2]; // [part][left, right]

    void init(Synth* trackerSynth, const PanningAmplifiers* panningAmplifiers);
    void setGain(int idx, float gainDB);
    virtual void update(void) override;
    uint8_t getRoutedTracks() const { return routedTracks; }

    static constexpr int kSend1Offset = 2 * kSynthParts;
    static constexpr int kSend2Offset = 3 * kSynthParts;
    static constexpr int kRightChannelOffset = kSynthParts;

  private:
    enum BusChannel
    {
        kBusInput,
        kBusSidechain,

        kBusChannelCount
    };

    struct BusRoute
    {
        bool isBus = false;
        std::array<uint16_t, kBusChannelCount> sources = {};
    };

    BusRoute getBusRoute(int partIdx) const;
    void renderParts(audio_block_t* const* trackBlocks);

    Synth* trackerSynth = nullptr;
    const PanningAmplifiers* panningAmplifiers = nullptr;
    bool isBlocked = true;
    float gainDB[kSynthParts] = {};
    uint8_t overloadCooldownBlocks = 0;
    uint8_t routedTracks = 0;
    audio_block_t* inputQueue[2 * kTrackInputs];
};
