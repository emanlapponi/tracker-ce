#include "AudioOutput.h"

AudioOutput::AudioOutput()
{
    constexpr int kLimiterSidechainChannelIdx = 1;

    connections.push_back(new AudioConnection(&limiterSidechainMux[OutputChannels::kLeft],
                                              0,
                                              &limiter[OutputChannels::kLeft],
                                              kLimiterSidechainChannelIdx));
    connections.push_back(new AudioConnection(&limiterSidechainMux[OutputChannels::kRight],
                                              0,
                                              &limiter[OutputChannels::kRight],
                                              kLimiterSidechainChannelIdx));

    constexpr int kStreamInputIdx = 0;
    connections.push_back(new AudioConnection(
      &limiter[OutputChannels::kLeft], 0, &finalMixer[OutputChannels::kLeft], kStreamInputIdx));
    connections.push_back(new AudioConnection(
      &limiter[OutputChannels::kRight], 0, &finalMixer[OutputChannels::kRight], kStreamInputIdx));

    connections.push_back(new AudioConnection(
      &finalMixer[OutputChannels::kLeft], 0, &outputStream, OutputChannels::kLeft));
    connections.push_back(new AudioConnection(
      &finalMixer[OutputChannels::kRight], 0, &outputStream, OutputChannels::kRight));

#ifdef PLATFORM_IMX
    constexpr int kMasterLeftUsbStreamIdx =
      UsbOutputStream::AudioSource::kMaster * UsbOutputStream::SourceChannel::kChannelsCount;
    constexpr int kMasterRightUsbStreamIdx = kMasterLeftUsbStreamIdx + 1;

    connections.push_back(new AudioConnection(
      &finalMixer[OutputChannels::kLeft], 0, &usbOutput, kMasterLeftUsbStreamIdx));
    connections.push_back(new AudioConnection(
      &finalMixer[OutputChannels::kRight], 0, &usbOutput, kMasterRightUsbStreamIdx));

    usbOutput.connectInputToUsbChannel(
      UsbOutputStream::kMaster, UsbOutputStream::SourceChannel::kLeft, 0);
    usbOutput.connectInputToUsbChannel(
      UsbOutputStream::kMaster, UsbOutputStream::SourceChannel::kRight, 1);

    for (int idx = UsbOutputStream::AudioSource::kFirstTrack;
         idx < UsbOutputStream::AudioSource::kMaster;
         idx++)
    {
        constexpr int channelsOffset = 1;
        int usbChannelLeft = (idx + channelsOffset) * 2;
        int usbChannelRight = usbChannelLeft + 1;

        usbOutput.connectInputToUsbChannel(
          idx, UsbOutputStream::SourceChannel::kLeft, usbChannelLeft);
        usbOutput.connectInputToUsbChannel(
          idx, UsbOutputStream::SourceChannel::kRight, usbChannelRight);
    }
#else
    connections.push_back(
      new AudioConnection(&finalMixer[OutputChannels::kLeft], 0, &usbOutput, 0));
    connections.push_back(
      new AudioConnection(&finalMixer[OutputChannels::kRight], 0, &usbOutput, 1));

    usbOutput.connectInputToUsbChannel(
      UsbOutputStream::kMaster, UsbOutputStream::SourceChannel::kLeft, 0);
    usbOutput.connectInputToUsbChannel(
      UsbOutputStream::kMaster, UsbOutputStream::SourceChannel::kRight, 1);
#endif
}

AudioMux* AudioOutput::getLimiterSidechainMux(OutputChannels channel)
{
    return &limiterSidechainMux[channel];
}

AudioEffectLimiter* AudioOutput::getLimiter(OutputChannels channel)
{
    return &limiter[channel];
}

AudioMixer<AudioOutput::FinalMixInputs::kFinalMixerSize>* AudioOutput::getFinalMixer(
  OutputChannels channel)
{
    return &finalMixer[channel];
}

AudioOutputStream* AudioOutput::getStream()
{
    return &outputStream;
}

UsbOutputStream* AudioOutput::getUsbStream()
{
    return &usbOutput;
}
