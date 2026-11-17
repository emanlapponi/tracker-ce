#include "Audio.h"
#include "TrackerImpl.h"
#include "polyassert.h"
#include <Recorder.h>

uint32_t Recorder::DestinationChannelConfig::maxLength = 0;
uint32_t Recorder::DestinationChannelConfig::length = 0;

Recorder::Recorder()
{
    inputStereoToMono = engine.getAudioInput()->getStereoToMono();
}

void Recorder::setConfig(Config config)
{
    Recorder::config = config;

    channelConfig.clear();

    uint32_t channelMaxLength = (uint32_t)((int)config.destinationBufferSize /
                                           ((int)sizeof(int16_t) * config.destinationChannels));
    DestinationChannelConfig::maxLength = channelMaxLength;

    for (int channel = 0; channel < config.destinationChannels; channel++)
    {
        DestinationChannelConfig tempChannelConfig;
        tempChannelConfig.initialAddress =
          config.destinationBufferAddress + channel * channelMaxLength;
        tempChannelConfig.addresToSave = tempChannelConfig.initialAddress;
        channelConfig.push_back(tempChannelConfig);
    }
}

// todo: SR->fullMemoryDuringRecordFlag this flag should be set
void Recorder::setFullMemoryCallback(std::function<void(void)> fullMemoryCallback)
{
    assert(fullMemoryCallback != nullptr);
    Recorder::fullMemoryCallback = fullMemoryCallback;
}

bool Recorder::isRecordingActive()
{
    return isActive;
}

void Recorder::record()
{

    if (config.source == Config::Source::kUsb)
    {
        recordingQueue[AudioInput::InputChannels::kLeft] =
          engine.getAudioInput()->getRecordingQueueUsb(AudioInput::InputChannels::kLeft);
        recordingQueue[AudioInput::InputChannels::kRight] =
          engine.getAudioInput()->getRecordingQueueUsb(AudioInput::InputChannels::kRight);
    }
    else if (config.source == Config::Source::kI2s)
    {
        recordingQueue[AudioInput::InputChannels::kLeft] =
          engine.getAudioInput()->getRecordingQueueI2s(AudioInput::InputChannels::kLeft);
        recordingQueue[AudioInput::InputChannels::kRight] =
          engine.getAudioInput()->getRecordingQueueI2s(AudioInput::InputChannels::kRight);
    }

    for (int sourceChannel = 0; sourceChannel < config.sourceChannels; sourceChannel++)
    {
        recordingQueue[sourceChannel]->begin();
    }

    if (config.sourceChannels == 1)
    {
        inputStereoToMono->setOutSignalType(config.useRightChannelForMono
                                              ? AudioStereoToMono::OutSignalType::monoR
                                              : AudioStereoToMono::OutSignalType::monoL);
    }
    else if (config.sourceChannels == 2)
    {
        inputStereoToMono->setOutSignalType(AudioStereoToMono::OutSignalType::stereo);
    }

    isActive = true;
    recordingHeadIdx = 0;
}

void Recorder::update()
{
    if (isActive)
    {
        bool isDestinationStereo = config.destinationChannels == 2;
        bool isDestinationMono = config.destinationChannels == 1;

        if (isDestinationStereo)
        { // SourceChannels == 1 would be handled by copying 0 channel to 1 channel but it seems to
          // be waste of memory.
            assert(config.sourceChannels == 2);

            bool areChannelsAvailable =
              std::all_of(recordingQueue,
                          recordingQueue + config.sourceChannels,
                          [](auto& queue) { return queue->available() > 0; });

            if (areChannelsAvailable)
            {
                for (int sourceChannel = 0; sourceChannel < config.sourceChannels; sourceChannel++)
                {
                    int16_t* destInitialAddress = channelConfig[sourceChannel].initialAddress;
                    memcpy(&destInitialAddress[recordingHeadIdx],
                           recordingQueue[sourceChannel]->readBuffer(),
                           sizeof(int16_t) * AUDIO_BLOCK_SAMPLES);
                    recordingQueue[sourceChannel]->freeBuffer();
                }

                recordingHeadIdx += AUDIO_BLOCK_SAMPLES;
            }
        }
        else if (isDestinationMono)
        {
            bool isSourceStereo = config.sourceChannels == 2;
            bool isSourceMono = config.sourceChannels == 1;

            if (isSourceStereo)
            {
                bool areChannelsAvailable =
                  std::all_of(recordingQueue,
                              recordingQueue + config.sourceChannels,
                              [](auto& queue) { return queue->available() > 0; });

                if (areChannelsAvailable)
                {

                    int16_t* dataLeft =
                      recordingQueue[AudioInput::InputChannels::kLeft]->readBuffer();
                    int16_t* dataRight =
                      recordingQueue[AudioInput::InputChannels::kRight]->readBuffer();
                    for (int idx = 0; idx < AUDIO_BLOCK_SAMPLES; idx++)
                    {
                        channelConfig[0].initialAddress[recordingHeadIdx++] =
                          ((int)*dataLeft++ + (int)*dataRight++) / 2;
                    }

                    recordingQueue[AudioInput::InputChannels::kLeft]->freeBuffer();
                    recordingQueue[AudioInput::InputChannels::kRight]->freeBuffer();
                }
            }
            else if (isSourceMono)
            {
                if (recordingQueue[0]->available() > 0)
                {
                    memcpy(&channelConfig[0].initialAddress[recordingHeadIdx],
                           recordingQueue[0]->readBuffer(),
                           sizeof(int16_t) * AUDIO_BLOCK_SAMPLES);
                    recordingQueue[0]->freeBuffer();

                    recordingHeadIdx += AUDIO_BLOCK_SAMPLES;
                }
            }
            else
            {
                assert(false);
            }
        }
        else
        {
            assert(false);
        }

        if (recordingHeadIdx >= DestinationChannelConfig::maxLength - AUDIO_BLOCK_SAMPLES)
        {
            for (int sourceChannel = 0; sourceChannel < config.sourceChannels; sourceChannel++)
            {
                recordingQueue[sourceChannel]->end();
            }

            finishRecording();

            if (fullMemoryCallback != nullptr)
            {
                fullMemoryCallback();
            }
        }

        DestinationChannelConfig::length = recordingHeadIdx;
    }
}

void Recorder::stop()
{
    for (int sourceChannel = 0; sourceChannel < config.sourceChannels; sourceChannel++)
    {
        recordingQueue[sourceChannel]->end();
    }

    if (isActive)
    {
        if (config.sourceChannels == 1)
        {
            while (isActive && recordingQueue[AudioInput::InputChannels::kLeft]->available() > 0)
            {
                update();
            }
        }
        else if (config.sourceChannels == 2)
        {
            while (isActive && recordingQueue[AudioInput::InputChannels::kLeft]->available() > 0 &&
                   recordingQueue[AudioInput::InputChannels::kRight]->available() > 0)
            {
                update();
            }
        }
        else
        {
            assert(false);
        }
    }

    for (int sourceChannel = 0; sourceChannel < config.sourceChannels; sourceChannel++)
    {
        recordingQueue[sourceChannel]->clear();
    }

    finishRecording();
}

void Recorder::crop(uint16_t startPoint, uint16_t endPoint)
{
    uint32_t startPointShift =
      (uint32_t)((uint32_t)startPoint * (float)(DestinationChannelConfig::length) / MAX_16BIT);
    uint32_t endPointShift =
      DestinationChannelConfig::length -
      (uint32_t)((uint32_t)endPoint * (float)(DestinationChannelConfig::length) / MAX_16BIT);

    for (auto& config : channelConfig)
    {
        config.addresToSave += startPointShift;
    }

    DestinationChannelConfig::length -= (startPointShift + endPointShift);
}

void Recorder::undo(std::vector<int16_t*> address, uint32_t length)
{
    assert(channelConfig.size() <= address.size());

    for (uint32_t channel = 0; channel < channelConfig.size(); channel++)
    {
        channelConfig[channel].addresToSave = address[channel];
    }

    DestinationChannelConfig::length = length;
}

int16_t* Recorder::getAddress(AudioInput::InputChannels channel)
{
    if (channelConfig.size() == 0)
    {
        return nullptr;
    }

    int16_t* address = channelConfig[AudioInput::InputChannels::kLeft].addresToSave;

    if (channel == AudioInput::InputChannels::kRight && channelConfig.size() == 2)
    {
        address = channelConfig[AudioInput::InputChannels::kRight].addresToSave;
    }

    return address;
}

uint32_t Recorder::getLength()
{
    return DestinationChannelConfig::length;
}

uint32_t Recorder::getSize()
{
    return DestinationChannelConfig::length * sizeof(int16_t) * channelConfig.size();
}

void Recorder::finishRecording()
{
    if (!isActive)
    {
        return;
    }

    isActive = false;
    inputStereoToMono->setOutSignalType(AudioStereoToMono::OutSignalType::stereo);
}

Recorder recorder;
