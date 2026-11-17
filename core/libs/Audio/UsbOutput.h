#ifndef LIBS_AUDIO_USBOUTPUT_H_
#define LIBS_AUDIO_USBOUTPUT_H_

#include "AudioStream.h"
#include <algorithm>
#include <vector>

class UsbOutputStream : public AudioStream
{
  protected:
#ifdef PLATFORM_IMX
    static constexpr int kInputChannelsCount = 28;
    static constexpr int kUsbChannelsCount = 28;
#else
    static constexpr int kInputChannelsCount = 2;
    static constexpr int kUsbChannelsCount = 2;
#endif
    static constexpr int kChannelDisconnected = -1;
    audio_block_t* inputQueueArray[kInputChannelsCount];
    int usbChannelToInput[kUsbChannelsCount];

  public:
    enum AudioSource
    {
        kFirstTrack,
        kLastTrack = 7,
        kReverb,
        kDelay,
#ifdef FEATURE_SYNTH
        kFirstSynth,
        kLastSynth = kFirstSynth + kSynthParts - 1,
#endif
        kMaster,

    };

    enum SourceChannel
    {
        kLeft,
        kRight,
        kChannelsCount
    };

    void connectInputToUsbChannel(int source, SourceChannel channel, int usbChannel);

    UsbOutputStream()
      : AudioStream(kInputChannelsCount, inputQueueArray)
    {
        std::fill(std::begin(usbChannelToInput), std::end(usbChannelToInput), kChannelDisconnected);
    }
    virtual void update(void);
    void syncActive();
};

#endif /* LIBS_AUDIO_USBOUTPUT_H_ */
