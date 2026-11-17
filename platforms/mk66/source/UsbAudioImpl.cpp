#include "UsbAudioImpl.h"
#include "usb_audio.h"
#include <algorithm>
#include <cstring>

static int16_t s_zeroBlock[128] = {};

void UsbAudioImpl::beginFillData()
{
    channelPtr[0] = nullptr;
    channelPtr[1] = nullptr;
    sampleCount = 0;
}

void UsbAudioImpl::fillChannelData(void* samples, int count, int channel)
{
    if (channel >= 0 && channel < 2)
    {
        channelPtr[channel] = static_cast<const int16_t*>(samples);
        sampleCount = std::min(count, kMaxSamplesPerBlock);
    }
}

void UsbAudioImpl::finishFillData()
{
    if (sampleCount <= 0 || !usb_audio_is_tx_active())
    {
        return;
    }
    const int16_t* left = channelPtr[0] ? channelPtr[0] : s_zeroBlock;
    const int16_t* right = channelPtr[1] ? channelPtr[1] : s_zeroBlock;
    usb_audio_push_tx_block(left, right, static_cast<unsigned int>(sampleCount));
}

bool UsbAudioImpl::isOutputActive() const
{
    return usb_audio_is_tx_active() != 0;
}
