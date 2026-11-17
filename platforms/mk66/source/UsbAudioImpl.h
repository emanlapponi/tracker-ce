#ifndef USBAUDIOIMPL_H_
#define USBAUDIOIMPL_H_

#include "Platform.h"
#include "config.h"

class UsbAudioImpl : public UsbAudio
{
  public:
    virtual void beginFillData() override;
    virtual void fillChannelData(void* samples, int count, int channel) override;
    virtual void finishFillData() override;
    virtual bool isOutputActive() const override;

  private:
    static constexpr int kMaxSamplesPerBlock = 128;
    const int16_t* channelPtr[2] = { nullptr, nullptr };
    int sampleCount = 0;
};

#endif /* USBAUDIOIMPL_H_ */
