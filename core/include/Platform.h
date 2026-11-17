#ifndef PLATFORM_H_
#define PLATFORM_H_
#include "stdio.h"

#include <functional>
#include <stdint.h>
#include <tuple>

class Sgtl5000;
class Radio;
class SwitchGroup;
class Leds;
class Battery;
class Timer;
class Encoder;
class DisplayDriver;
class ConfigManager;
class UartMidi;
class UsbMidi;
class UsbAudio;
class FileSystem;
class CrcCalculator;
class Usb;

class Platform
{
  public:
    struct SystemInfo
    {
        const char* buildId;
    };

    typedef void (*PowerButtonCallback)(int);

    virtual void setPowerMode(bool mode) = 0;
    virtual void resetMCU() = 0;
    virtual void runBootloader(char* fileName) = 0;
    virtual void setPowerButtonCallback(PowerButtonCallback) = 0;
    virtual bool getPowerButtonState() = 0;
    virtual bool readForceStartUp() = 0;

    virtual Sgtl5000* getCodecAudio() = 0;
    virtual Radio* getRadio() = 0;
    virtual SwitchGroup* getButtons() = 0;
    virtual SwitchGroup* getPads() = 0;
    virtual Leds* getLeds() = 0;
    virtual Battery* getBattery() = 0;
    virtual Timer* getTimer() = 0;
    virtual Encoder* getEncoder() = 0;
    virtual DisplayDriver* getDisplayDriver() = 0;
    virtual UsbAudio* getUsbAudio() = 0;
    virtual UartMidi* getUartMidi() = 0;
    virtual UsbMidi* getUsbMidi() = 0;
    virtual ConfigManager* getConfigManager() = 0;
    virtual FileSystem* getFileSystem() = 0;
    virtual CrcCalculator* getCrcCalculator() = 0;
    virtual SystemInfo* getSystemInfo() = 0;
    virtual Usb* getUsb() = 0;
};

class CrcCalculator
{
  public:
    virtual uint32_t calculate32bitCrc(const uint8_t* data, const uint16_t datalen) = 0;
};

class Midi
{
  public:
    enum MessageType : uint8_t
    {
        kInvalidType = 0x00,
        kNoteOff = 0x80,
        kNoteOn = 0x90,
        kAfterTouchPoly = 0xA0,
        kControlChange = 0xB0,
        kProgramChange = 0xC0,
        kAfterTouchChannel = 0xD0,
        kPitchBend = 0xE0,
        kSystemExclusive = 0xF0,
        kSystemExclusiveStart = kSystemExclusive,
        kTimeCodeQuarterFrame = 0xF1,
        kSongPosition = 0xF2,
        kSongSelect = 0xF3,
        kUndefined_F4 = 0xF4,
        kUndefined_F5 = 0xF5,
        kTuneRequest = 0xF6,
        kSystemExclusiveEnd = 0xF7,
        kClock = 0xF8,
        kUndefined_F9 = 0xF9,
        kTick = kUndefined_F9,
        kStart = 0xFA,
        kContinue = 0xFB,
        kStop = 0xFC,
        kUndefined_FD = 0xFD,
        kActiveSensing = 0xFE,
        kSystemReset = 0xFF,
    };

    virtual bool isReadAvailable() = 0;
    virtual void read() = 0;
    virtual void sendRealTime(MessageType messageType) = 0;
    virtual void sendControlChange(uint8_t index, uint8_t value, uint8_t channel) = 0;
    virtual void sendProgramChange(uint8_t programNumber, uint8_t channel) = 0;
    virtual void sendAfterTouch(uint8_t velocity, uint8_t channel) = 0;
    virtual void sendPolyPressure(uint8_t note, uint8_t value, uint8_t channel) = 0;
    virtual void sendPitchBend(int value, uint8_t channel) = 0;
    virtual void sendNoteOn(uint8_t note, uint8_t velocity, uint8_t channel) = 0;
    virtual void sendNoteOff(uint8_t note, uint8_t velocity, uint8_t channel) = 0;
};

class UsbAudio
{
  public:
    virtual void beginFillData() = 0;
    virtual void fillChannelData(void* samples, int count, int channel) = 0;
    virtual void finishFillData() = 0;
    virtual bool isOutputActive() const { return false; }
};

class UsbMidi : public Midi
{
  public:
    struct MidiReceiveActions
    {
        void (*realTime)(uint8_t);
        void (*noteOn)(uint8_t, uint8_t, uint8_t);
        void (*noteOff)(uint8_t, uint8_t, uint8_t);
        void (*songPosition)(uint16_t);
        void (*controlChange)(uint8_t, uint8_t, uint8_t);
    };

    virtual void setReceiveActions(MidiReceiveActions* receiveActions) = 0;
};

class UartMidi : public Midi
{
  public:
    struct MidiReceiveActions
    {
        void (*noteOn)(uint8_t, uint8_t, uint8_t);
        void (*noteOff)(uint8_t, uint8_t, uint8_t);
        void (*clock)(void);
        void (*start)(void);
        void (*stop)(void);
        void (*songPosition)(unsigned);
        void (*continueMessage)(void);
        void (*controlChange)(uint8_t, uint8_t, uint8_t);
    };
    virtual void init() = 0;
    virtual void setReceiveActions(MidiReceiveActions* receiveActions) = 0;
    virtual void setReceiveNoteOnAction(void (*action)(uint8_t, uint8_t, uint8_t)) = 0;
};

class Encoder
{
  public:
    virtual void setAcceleration(int acceleration) = 0;
    virtual void setResolution(int resolution) = 0;
};

class Timer
{
  public:
    virtual uint32_t millis() = 0;
    virtual uint32_t micros() = 0;
    virtual void delay(int ms) = 0;
    virtual uint32_t clockCycles() = 0;
    virtual uint32_t clockCyclesPerUs() = 0;
    virtual void scheduleMidiClockUs(uint32_t delayUs, void (*callback)())
    {
        (void)delayUs;
        if (callback)
            callback();
    }
    virtual void cancelMidiClock() {}
};

class Battery
{
  public:
    virtual void enableMonitoring() = 0;
    virtual int getLevel() = 0;
    virtual float getVoltage() = 0;
    virtual bool getCharging() = 0;
    virtual int getTimeTo() = 0;
    virtual bool getState() = 0;
    virtual bool isEmpty() = 0;
};

class Leds
{
  public:
    virtual void setLed(int idx, bool enabled, int brightness) = 0;
};

class SwitchGroup
{
  public:
    enum ActionType
    {
        kPress,
        kRelease,
        kHold
    };

    virtual void setHoldTime(int timeMs) = 0;
    virtual bool isButtonPressed(int buttonIdx) = 0;
    virtual void setActionCallback(ActionType action, void (*callback)(uint8_t)) = 0;
};

class Radio
{
  public:
    static constexpr int kMaxAudioFrequencyMHz = 108;

    enum SeekDirection
    {
        kUp,
        kDown
    };

    enum Region
    {
        kEurope,
        kUSA,
        kAustralia,
        kJapan,

    };

    virtual void enableLineInput(
      bool enabled) = 0; // true enables for LineInput for radio, false for jack input
    virtual void enable(bool enabled) = 0;
    virtual bool setFrequency(float freq) = 0; // true if success
    virtual void setSeekCallback(void (*seekCallback)(void)) = 0;
    virtual void seek(SeekDirection direction) = 0;
    virtual void updateSeek() = 0;
    virtual bool updateRDS() = 0;
    virtual float getFrequency() = 0;
    virtual float getBaseFrequency() = 0;
    virtual float getSpacing() = 0;
    virtual bool isinitialized() = 0;
    virtual void clearRDS() = 0;
    virtual void setRegion(Region region) = 0;
    virtual bool isRegionChanged() = 0;
    virtual char* getStationName() = 0;
};

class Sgtl5000
{
  public:
    enum InputType
    {
        kLineIn,
        kMic
    };

    enum HeadphoneSource
    {
        kAudioOutput,
        kAudioInput
    };

    enum EqualizerType
    {
        kFlatFrequency,
        kParametricEqualizer,
        kToneControls,
        kGraphicEqualizer
    };

    struct AutoVolumeParams
    {
        int maxExpanderGainType; // 0 - 2
        int responseTimeType;    // 0 - 3
        int limiterType;         // 0 - 1
        float threshold;         // 96.0f - 0.0f
        float attack;            // 2.0f - 32.0f;
        float decay;             // 2.0f - 32.0f
    };

    struct EnhanceBassParams
    {
        float inputEnhanceLevel; // 0.0f - 1.0f
        float bassEnhanceLevel;  // 0.0f - 1.0f
        int cutoffEnabled;       // 0 (enabled) - 1(disabled)
        int cutoffFrequency;     // 0 - 6
    };

    struct SurroundSoundParams
    {
        int width;     // 0 - 7
        int selection; // 2 - 3
    };

    virtual void init() = 0;
    virtual void setInputType(InputType input) = 0;
    virtual void setHeadphoneSource(HeadphoneSource source) = 0;
    virtual void setLineInLevel(int level) = 0; // 0 - 15
    virtual void setMicGain(int gainDb) = 0;    // 0 - 63
    virtual void setHeadphoneMuted(bool muted) = 0;
    virtual void setMasterVolume(float volume) = 0; // 0.0f - 1.0f
    virtual void setAutoVolumeEnabled(bool enabled) = 0;
    virtual void configureAutoVolume(AutoVolumeParams* params) = 0;
    virtual void setEnhanceBassEnabled(bool enabled) = 0;
    virtual void configureEnhanceBass(EnhanceBassParams* params) = 0;
    virtual void setEqualizerType(EqualizerType type) = 0;
    virtual void configureFiveBandEqualizer(float* values) = 0; // 5 bands, each : -1.0f - 1.0f
    virtual void enableSurroundSound(bool enabled) = 0;
    virtual void configureSurroundSound(SurroundSoundParams* params) = 0;
    virtual void enableHardwareProcessor(bool enabled) = 0;
};

class FileSystem
{
  public:
    typedef void (*CardStateChangeCallback)(bool);
    typedef void* FileHandle;
    static constexpr int kListError = -1;

    enum DirContentType
    {
        kFolders,
        kFiles
    };

    // must be consistent with SdDir::enListFileFilter
    enum enListFileFilter
    {
        enListFileFilter_all,
        enListFileFilter_folder,
        enListFileFilter_wave,
        enListFileFilter_wave_inst,
        enListFileFilter_firmware,
        enListFileFilter_mod,
        enListFileFilter_game,
    };

    enum FileOpenMode
    {
        kRead,
        kWriteOpen,
        kWriteAppend,
        kWriteCreateNew,
    };

    static constexpr char kPathSeparator = '/';

    virtual bool isSdCardInserted() = 0;
    virtual void setMountEventCallback(CardStateChangeCallback funct) = 0;
    // Reads the volume from the card again, for after something else wrote to
    // it. Every open file becomes invalid. False if no volume got mounted.
    virtual bool remount() = 0;

    virtual bool exists(const char* path) = 0;
    virtual bool rename(const char* oldPath, const char* newPath) = 0;
    virtual bool mkdir(uint8_t hidden, const char* path, bool pFlag = true) = 0;
    virtual bool remove(const char* path) = 0;
    virtual bool removeDirWithFiles(const char* path) = 0;
    virtual bool removeOnlyDirFiles(const char* path) = 0;

    virtual size_t getFileSize(const char* path) = 0;
    virtual size_t getFileSize(FileHandle fileHandle) = 0; // file needs to be open
    virtual size_t getFileAvailable(FileHandle fileHandle) = 0;

    // todo refator to openFile(filepath, enum openOption(open/openWrite/write), useCache,
    // fileHandle)
    virtual std::tuple<bool, FileHandle> openFile(
      const char* filepath,
      FileOpenMode openMode = kRead,
      // non-null file handle means that fileHandle is already created
      FileHandle* fileHandle = nullptr) = 0;
    virtual int32_t readFile(void* destBuf, size_t bufLen, FileHandle file) = 0;
    virtual int32_t writeFile(const void* data, size_t dataSize, FileHandle fileHandle) = 0;
    virtual bool seekFileCur(int offset, FileHandle fileHandle) = 0;
    virtual bool seekFile(uint32_t pos, FileHandle fileHandle) = 0;
    virtual void closeFile(FileHandle file, bool deleteOnClose = true) = 0;
    virtual void endFile(FileHandle file) = 0;
    virtual bool isFileOpen(FileHandle file) = 0;
    virtual bool enableFastSeek(FileHandle fileHandle,
                                uint32_t* clmtBuffer,
                                uint32_t clmtElementCount,
                                uint32_t* outDatabaseSector = nullptr,
                                uint32_t* outSectorsPerCluster = nullptr) = 0;
    virtual bool readSectorsAsync(uint8_t* buffer, uint32_t startSector, uint32_t sectorCount) = 0;
    virtual bool pollAsyncRead(bool* outSuccess = nullptr) = 0;
    virtual bool waitAsyncRead() = 0;
    virtual bool isAsyncReadBusy() = 0;

    virtual int createInstrumentsList(const char* path,
                                      char** list,
                                      uint8_t list_length,
                                      uint32_t* sizeOut) = 0;
    virtual int countContainedFiles(const char* path) = 0;

    virtual int createFilesList(const char* path,
                                uint8_t start_line,
                                char** list,
                                uint8_t list_length,
                                uint16_t max_used_memory,
                                enListFileFilter filter = enListFileFilter_all,
                                uint16_t skip_count = 0,
                                uint16_t max_files_in_subdir = 0,
                                enListFileFilter folderSizeFilter = enListFileFilter_all) = 0;

    virtual void listDirContents(const char* dirpath,
                                 DirContentType contentType,
                                 std::function<bool(const char* name)> nameFilter,
                                 char* namesBuffer,
                                 int namesBufferSize,
                                 int maxNameLen,
                                 int& outNamesCount,
                                 char** fileList) = 0;

    // todo: refactor below...
    virtual int readNbFiles(const char* romPath) = 0;

    virtual bool nesDir_open(const char* path) = 0;
    virtual bool nesDir_readItem(char* path, uint8_t* isDir, uint8_t* isHid, uint32_t* size) = 0;
    virtual void nesDir_close() = 0;
};

class Usb
{
  public:
    enum class ClassType
    {
        kMidi,
        kAudioIn,  // Audio input into Tracker (USB OUT: Host -> Device, 2ch)
        kAudioOut, // Audio output from Tracker (USB IN: Device -> Host, 28ch)
        kStorage,
        kSerial
    };
    virtual void init(void) = 0;
    virtual void reset(void) = 0;
    virtual void update(void) = 0;
    virtual void setClassState(ClassType type, bool enable) = 0;
    virtual bool getClassState(ClassType type) = 0;
    virtual bool isAttached() = 0;
};

#endif /* PLATFORM_H_ */
