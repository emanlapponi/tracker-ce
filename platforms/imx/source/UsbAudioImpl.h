#ifndef USBAUDIOIMPL_H_
#define USBAUDIOIMPL_H_

#include "Platform.h"

class UsbAudioImpl : public UsbAudio
{
  public:
    virtual void beginFillData() override;
    virtual void fillChannelData(void* samples, int count, int channel) override;
    virtual void finishFillData() override;
    virtual bool isOutputActive() const override;
};

#endif /* USBAUDIOIMPL_H_ */
