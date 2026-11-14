#pragma once

#include "AudioStream.h"

class Synth;

class PolySynth : public AudioStream
{
  public:
    PolySynth(void)
      : AudioStream(0, NULL)
    {
    }

    void init(Synth* trackerSynth);
    void setGain(int idx, float gainDB);
    virtual void update(void) override;

    static constexpr int kSend1Offset = 2 * kSynthParts;
    static constexpr int kSend2Offset = 3 * kSynthParts;
    static constexpr int kRightChannelOffset = kSynthParts;

  private:
    struct BusRoute
    {
        bool isBus = false;
        uint16_t sources = 0;
    };

    BusRoute getBusRoute(int partIdx) const;

    Synth* trackerSynth = nullptr;
    bool isBlocked = true;
    float gainDB[kSynthParts] = {};
    uint8_t overloadCooldownBlocks = 0;
};
