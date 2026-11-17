#ifndef MTRECORDER_H_
#define MTRECORDER_H_

#include "mtAudioEngine.h"

class Recorder
{
  public:
    Recorder();

    struct Config
    {
        enum Source
        {
            kUsb,
            kI2s
        };
        int16_t* destinationBufferAddress;
        uint32_t destinationBufferSize;
        int destinationChannels;
        int sourceChannels;
        Source source = Source::kI2s;
        bool useRightChannelForMono = false;
    };
    int16_t* getAddress(AudioInput::InputChannels channel);
    uint32_t getLength();
    uint32_t getSize();
    bool isRecordingActive();

    void update();
    void setConfig(Config config);
    void setFullMemoryCallback(std::function<void(void)> fullMemoryCallback);

    void record();
    void stop();
    void crop(uint16_t startPoint, uint16_t endPoint);
    void undo(std::vector<int16_t*> address, uint32_t length);

  private:
    struct DestinationChannelConfig
    {
        int16_t* initialAddress;
        int16_t* addresToSave;
        static uint32_t length;
        static uint32_t maxLength;
    };

    struct DataToSave
    {
        std::vector<int16_t*> addressToSave;
        uint32_t lengthOffset;
    };

    void finishRecording();

    Config config;
    std::vector<DestinationChannelConfig> channelConfig;

    AudioRecordQueue* recordingQueue[AudioInput::InputChannels::kChannelsCount] = {};
    AudioStereoToMono* inputStereoToMono = nullptr;

    bool isActive = false;
    uint32_t recordingHeadIdx;
    std::function<void(void)> fullMemoryCallback = nullptr;
};

extern Recorder recorder;
extern int16_t externalRamSampleMemory[];

#endif /* SOURCE_MTRECORDER_H_ */
