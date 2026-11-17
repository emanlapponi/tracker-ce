#include "UsbAudioImpl.h"
#include "audio_unified.h"

void UsbAudioImpl::beginFillData()
{
    beginUsbAudioFill();
}

void UsbAudioImpl::fillChannelData(void* samples, int count, int channel)
{
    fillUsbAudioChannelData(samples, count, channel);
}

void UsbAudioImpl::finishFillData()
{
    finishUsbAudioFill();
}

bool UsbAudioImpl::isOutputActive() const
{
    return isUsbAudioOutputActive();
}
