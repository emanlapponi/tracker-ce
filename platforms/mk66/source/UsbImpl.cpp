#include "UsbImpl.h"
#include "usb_audio.h"
#include "usb_desc.h"
#include "usb_dev.h"
#include "usb_msc.h"

void UsbImpl::init()
{
}

void UsbImpl::reset(void)
{
    resetRequested = true;
}

void UsbImpl::update(void)
{
    if (resetRequested)
    {
        resetRequested = false;
        uint8_t flags = 0;
        if (storageEnabled)
        {
            flags = USB_CFG_MSC;
        }
        else
        {
            if (midiEnabled)
                flags |= USB_CFG_MIDI;
            if (serialEnabled)
                flags |= USB_CFG_SERIAL;
            if (rawHidEnabled)
                flags |= USB_CFG_RAWHID;
            if (audioOutEnabled)
                flags |= USB_CFG_AUDIO_OUT;
            if (audioInEnabled)
                flags |= USB_CFG_AUDIO_IN;
        }

        usb_audio_reset_buffers();
        usb_reinit(flags);
        return;
    }

    usb_msc_update();
}

void UsbImpl::setClassState(Usb::ClassType type, bool enable)
{
    switch (type)
    {
        case Usb::ClassType::kMidi:
            midiEnabled = enable;
            break;
        case Usb::ClassType::kAudioIn:
            audioInEnabled = enable;
            break;
        case Usb::ClassType::kAudioOut:
            audioOutEnabled = enable;
            break;
        case Usb::ClassType::kStorage:
            storageEnabled = enable;
            break;
        case Usb::ClassType::kSerial:
            serialEnabled = enable;
            break;
    }
}

bool UsbImpl::getClassState(Usb::ClassType type)
{
    switch (type)
    {
        case Usb::ClassType::kMidi:
            return midiEnabled;
        case Usb::ClassType::kAudioIn:
            return audioInEnabled;
        case Usb::ClassType::kAudioOut:
            return audioOutEnabled;
        case Usb::ClassType::kStorage:
            return storageEnabled;
        case Usb::ClassType::kSerial:
            return serialEnabled;
    }
    return false;
}

bool UsbImpl::isAttached()
{
    return usb_configuration != 0;
}
