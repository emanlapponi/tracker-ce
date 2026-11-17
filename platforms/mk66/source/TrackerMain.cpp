#include "PlatformImpl.h"
#include "Tracker.h"
#include "config.h"
#include "usb_audio.h"

void initHardware(PlatformImpl* platform,
                  const char* version,
                  void (*updateAudioBuffersFunc)(void* left, void* right),
                  void (*updateAudioInputFunc)(void* data),
                  void (*startDisplayFunc)(Platform*));
extern void updateHardware();

Tracker* trackerInstance;
PlatformImpl platform;

extern "C" void restartMCU()
{
    platform.resetMCU();
}

void updateBuffers(void* bufLeft, void* bufRight)
{
    if (trackerInstance != nullptr)
    {
        UsbAudio* usbAudio = platform.getUsbAudio();
        usbAudio->beginFillData();
        trackerInstance->updateAudioBuffers(bufLeft, bufRight, kAudioBlockSamples);
        usbAudio->finishFillData();
    }
}

static uint32_t s_usbRxBlock[kAudioBlockSamples] __attribute__((section(".noinit.$EXTERNAL_RAM"), aligned(4)));

void updateInputBuffers(void* data)
{
    if (trackerInstance != nullptr)
    {
        const void* usbData = usb_audio_pull_rx_block(s_usbRxBlock, kAudioBlockSamples);
        trackerInstance->updateInputAudioBuffers(
          data, const_cast<void*>(usbData), kAudioBlockSamples);
    }
}

//=======================================================================
void setup()
{
    trackerInstance = Tracker::getInstance();

    initHardware(&platform,
                 Tracker::getVersion(),
                 updateBuffers,
                 updateInputBuffers,
                 trackerInstance->getStartDisplayFunc());

    platform.init();

    trackerInstance->init(&platform);
}

//=======================================================================
void loop()
{
    updateHardware();

    platform.update();

    trackerInstance->update();
}
