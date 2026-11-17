#include "Cpu.h"
#include "DevConsole.h"
#include "ExternalSdram.h"
#include "MIMXRT1051.h"
#include "TimeControl.h"
#include "board.h"
#include "clock_config.h"
#include "fsl_debug_console.h"
#include "log.h"
#include "peripherals.h"
#include "pin_mux.h"

#include "PlatformImpl.h"
#include "Tracker.h"

#include "config.h"
#include <stdio.h>

#define BLINK_LED_ENABLED

void initHardware(PlatformImpl* platform,
                  void (*outputFunct)(void*, void*),
                  void (*inputFunct)(void*),
                  void (*startDisplayFunc)(Platform*));
void updateHardware(void* unusedParam);

Tracker* trackerInstance;
PlatformImpl platform;

void _debuglog(const char* msg)
{
    DevConsole_Printf("%s\n", msg);
}

void _debuglogf(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    DevConsole_Vprintf(format, args);
    va_end(args);
}

void assert_func_play(const char*, int)
{
    asm("bkpt #0");
    while (1)
    {
    }
}

void __assert_func(const char* file, int line, const char*, const char*)
{
    assert_func_play(file, line);
    while (1)
    {
    }
}

int16_t leftBuffer[128];
int16_t rightBuffer[128];

extern "C" void updateUsbOutputBuffers(void* left, void* right, int count);
extern "C" bool updateUsbInputBuffers(void* data, int count);

extern void* _malloc_r(struct _reent* r, size_t sz);

bool enableMallocCheck = false;

void* malloc(size_t size)
{
    auto ptr = _malloc_r(0, size);
    if (ptr == nullptr && enableMallocCheck) [[unlikely]]
    {
        assert(false);
        while (1)
        {
        }
    }
    return ptr;
}

void updateBuffers(void* bufLeft, void* bufRight)
{
    UsbAudio* usbAudio = platform.getUsbAudio();

    if (trackerInstance != nullptr)
    {
        usbAudio->beginFillData();
        trackerInstance->updateAudioBuffers(bufLeft, bufRight, kAudioBlockSamples);
        usbAudio->finishFillData();
        // updateUsbOutputBuffers(bufLeft, bufRight, kAudioBlockSamples);
    }
}

void updateInputBuffers(void* data)
{
    if (trackerInstance != nullptr)
    {
        static uint32_t usb_rx_buffer[kAudioBlockSamples]{};
        const bool hasUsbRx = updateUsbInputBuffers(usb_rx_buffer, kAudioBlockSamples);
        trackerInstance->updateInputAudioBuffers(
          data, hasUsbRx ? usb_rx_buffer : nullptr, kAudioBlockSamples);
    }
}

TimeControl timeControl;
Cpu mainCpu;

// dummy bytes at end of the file (see linker script)
// workaround for a bug in the bootloader that is not
// erasing last sector if it contains less than 256 bytes
const uint32_t end_dummy[4096] __attribute__((section(".end")))
__attribute__((used)) = { 0xdeadbeef };

int main(void)
{
    /* Init board hardware. */
    BOARD_ConfigMPU();
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitBootPeripherals();

    mainCpu.setFrequency(CPU_FREQ);

    Usb* usb = platform.getUsb();

    usb->init();

    DbgConsole_Init(kSerialManager_UsbVirtualControllerEhci0, 0, kSerialPort_Virtual, 0);

    LOG("Started successfully.\n");

    timeControl.init();
#ifdef BLINK_LED_ENABLED
    int output = 0;
    constexpr uint32_t kLedTestBlinkTimeMs = 1000;
    uint32_t timeCounterMs = timeControl.millis();
#endif

    trackerInstance = Tracker::getInstance();

    initHardware(
      &platform, updateBuffers, updateInputBuffers, trackerInstance->getStartDisplayFunc());

    platform.init();
    trackerInstance->init(&platform);
    enableMallocCheck = true;
    DevConsole_Init(&platform);

    while (1)
    {
        usb->update();
        DevConsole_Update();
#ifdef BLINK_LED_ENABLED
        if (timeControl.millis() > timeCounterMs + kLedTestBlinkTimeMs)
        {
            timeCounterMs = timeControl.millis();
            GPIO_PinWrite(
              BOARD_INITPINS_BLINK_LED_PERIPHERAL, BOARD_INITPINS_BLINK_LED_CHANNEL, output);
            output = !output;
        }

        updateHardware(&trackerInstance);

        platform.update();
        trackerInstance->update();
#endif
    }
    return 0;
}
