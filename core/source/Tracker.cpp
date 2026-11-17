#include "Platform.h"
#include "Profiler.h"
#include "TrackerImpl.h"
#include "mtWavetableEnvelopes.h"
#include "core/interface.h"
#include "elapsedMillis.h"
#include "fileManager.h"
#include "mtAudioEngine.h"
#include "mtAudioExport.h"
#include "mtConfig.h"
#include "mtHardware.h"
#include "mtMidi.h"
#include "mtPadsBacklight.h"
#include "mtSequencer.h"
#include "mtSleep.h"
#include "mtSliceDetector.h"

#include <cr_section_macros.h>

//-----------------------------------  EXTERNAL RAM CONFIGURATION --------------------------------
// clang-format off
__attribute__((section(".sample_memory"), used)) int16_t externalRamSampleMemory[SAMPLE_MEMORY_SIZE / 2];
__attribute__((section(".delay_memory"), used)) int16_t externalRamDelayBuff[DELAY_MEMORY_SIZE / 2];
__attribute__((section(".reverb_memory"), used)) uint16_t externalRamReverbBuff[REVERB_MEMORY_SIZE / 2];
__attribute__((section(".scratch_memory"), aligned(32), used)) uint8_t externalRamScratchBuff[SHARED_SCRATCH_MEMORY_SIZE];

// clang-format on
int16_t* externalRamEffectsBuff = externalRamSampleMemory + (EDIT_BUFF_OFFSET / 2);
int16_t* externalRamSampleMemoryBegin = externalRamSampleMemory;

//--------------------------------------------------------------------------------------------------------------
static TrackerImpl instance;

extern void onPowerButtonChange(int value);
extern void onSDCardSlotChange(bool state);

void startDisplay(Platform* platform)
{
    [[maybe_unused]] bool powerButtonState = platform->getPowerButtonState();

    display.begin(platform->getDisplayDriver(), platform->getTimer());
#ifdef PLATFORM_IMX
    // delay to hide white screen before driver initialize the screen
    platform->getTimer()->delay(200);
#endif

#ifdef TRACKER_MINI
    // only in release build mode (always start device immediately in debug mode)
#if !defined(DEBUG) && !defined(FORCED_AUTOSTART)
    if (platform->readForceStartUp() == false &&
        (!powerButtonState || platform->getBattery()->isEmpty()))
    {
        Battery* battery = platform->getBattery();
        display.showBatteryScreen(
          battery->isEmpty(), battery->getCharging(), battery->getLevel(), battery->getVoltage());
        platform->getTimer()->delay(100);
        display.turnOn();
        display.showBatteryScreen(
          battery->isEmpty(), battery->getCharging(), battery->getLevel(), battery->getVoltage());
        platform->getTimer()->delay(2000);
        platform->setPowerMode(false);
    }
#endif
#endif

    display.turnOn();
}

Tracker* Tracker::getInstance()
{
    return &instance;
}

TrackerImpl* TrackerImpl::getInstance()
{
    return &instance;
}

Platform* TrackerImpl::getPlatform()
{
    return platform;
}

#ifdef FEATURE_SYNTH
Synth& TrackerImpl::getSynth()
{
    return synth;
}
#endif

const char* Tracker::getVersion()
{
    return "1.0";
}

void TrackerImpl::init(Platform* platform)
{
    this->platform = platform;

    initWavetableEnvelopes();

    Timer* timer = getPlatform()->getTimer();
    elapsedMicros::init(timer);
    elapsedMillis::init(timer);
    elapsedMicrosAudio::init(timer);
    elapsedMillisAudio::init(timer);
    AudioStream::setUsbAudio(platform->getUsbAudio());

    midiInit(platform->getUartMidi(), platform->getUsbMidi());

    platform->setPowerButtonCallback(onPowerButtonChange);
    platform->getFileSystem()->setMountEventCallback(onSDCardSlotChange);

#ifdef FEATURE_SYNTH
    synth.init(platform, [this]() { return sequencer.getCurrentTempo(); });

    engine.init(platform, &synth);
#else
    engine.init(platform, nullptr);
#endif
    sequencer.init();
    sequencer.setCrcCalculator(platform->getCrcCalculator());
    padsBacklight.init(500, platform->getLeds());
    exporter.init(platform->getFileSystem());
    Profiler::getInstance()->init();
    newFileManager.setCrcCalculator(platform->getCrcCalculator());

    initConfig(platform);
    if (mtConfig.common.usbAudioMode != 0)
    {
        extern void usbAudioChange();
        usbAudioChange();
    }
    // UI init at the end
#ifdef FEATURE_SYNTH
    mtInterface.begin(platform, &synth);
#else
    mtInterface.begin(platform, nullptr);
#endif

    bool stereoSampleMode = true;

#ifdef FEATURE_SYNTH
    newFileManager.init(platform->getFileSystem(), &synth, stereoSampleMode);
#else
    newFileManager.init(platform->getFileSystem(), nullptr, stereoSampleMode);
#endif

    interfaceGlobals.devMode = platform->getFileSystem()->exists("/.develop");

#if defined(PLATFORM_IMX) && defined(ENABLE_DEV_CONSOLE)
    if (interfaceGlobals.devMode)
    {
        // The serial port carries the log and the development console.
        Usb* usb = platform->getUsb();
        usb->setClassState(Usb::ClassType::kSerial, true);
        usb->reset();
    }
#endif
}

void TrackerImpl::update()
{
    if (lowPower.getLowPowerState() == shutdownStateSleep)
    {
        return;
    }

    engine.updateSdStream();
    display.update();
    engine.updateSdStream();
    padsBacklight.update();
    exporter.update();
    sliceDetector.update();
    newFileManager.update();
    mtInterface.update();
    engine.updateSdStream();
    sequencer.update();
}

void TrackerImpl::updateAudioBuffers(void* left, void* right, int length)
{
    if (AudioStream::renderMode)
    {
        return;
    }
    if (platform != nullptr)
    {
        midiSetAudioBlockStartUs(platform->getTimer()->micros());
    }
    Profiler::getInstance()->startMeasuringAudio();
    midiUpdate();

    // Exact hardware I2S sample rate:
    // - IMX (Tracker+ and Tracker Mini): PLL4 = 24 MHz * (37 + 15168/24000) / 2 / 4 / 10 / 256 = 44100.0 Hz
    // - MK66 (Original Tracker): F_CPU = 180 MHz * 16 / 255 / 256 = 44117.6470588 Hz
#ifdef PLATFORM_IMX
    static constexpr float kAudioSampleRate = 44100.0f;
#else
    static constexpr float kAudioSampleRate = 44117.6470588f;
#endif

    float updateIntervalSeconds = length / kAudioSampleRate;
    sequencer.tick(updateIntervalSeconds);

    engine.update();

    engine.updateAudioBuffers(left, right, length);
    Profiler::getInstance()->stopMeasuringAudio();
}

void TrackerImpl::updateInputAudioBuffers(void* dataI2s, void* dataUsb, int length)
{
    engine.updateInputAudioBuffers(dataI2s, dataUsb, length);
}

void (*TrackerImpl::getStartDisplayFunc())(Platform*)
{
    return startDisplay;
}
