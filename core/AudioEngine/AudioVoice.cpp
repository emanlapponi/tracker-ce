#include "AudioVoice.h"

AUDIO_CORE_RAM void AudioStereoMirror::update()
{
    audio_block_t* left = receiveReadOnly(0);
    audio_block_t* right = stereo ? receiveReadOnly(1) : nullptr;
    if (!stereo)
    {
        if (audio_block_t* stale = receiveReadOnly(1))
        {
            release(stale);
        }
    }

    if (left != nullptr)
    {
        transmit(left, 0);
        if (!stereo)
        {
            transmit(left, 1);
        }
        release(left);
    }

    if (stereo && right != nullptr)
    {
        transmit(right, 1);
        release(right);
    }
}

AudioVoice::AudioVoice()
{
    for (int channel = 0; channel < VoiceChannels::kChannelsCount; channel++)
    {
        connections.push_back(new AudioConnection(&memoryPlayer[channel], &bitDepth[channel]));
        connections.push_back(new AudioConnection(&bitDepth[channel], &filter[channel]));
        connections.push_back(new AudioConnection(&filter[channel], &amplifier[channel]));
        connections.push_back(new AudioConnection(&amplifier[channel], 0, &stereoMirror, channel));
        connections.push_back(
          new AudioConnection(&stereoMirror, channel, &panningAmplifier[channel], 0));
        connections.push_back(new AudioConnection(&stereoMirror, channel, &rms[channel], 0));
    }
}

AudioPlayMemory* AudioVoice::getMemoryPlayer(VoiceChannels channel)
{
    return &memoryPlayer[channel];
}

AudioBitDepth* AudioVoice::getBitDepth(VoiceChannels channel)
{
    return &bitDepth[channel];
}

AudioFilterStateVariable* AudioVoice::getFilter(VoiceChannels channel)
{
    return &filter[channel];
}

AudioAmplifier* AudioVoice::getAmplifier(VoiceChannels channel)
{
    return &amplifier[channel];
}

AudioAmplifier* AudioVoice::getPanningAmplifier(VoiceChannels channel)
{
    return &panningAmplifier[channel];
}

AudioAnalyzeRMS* AudioVoice::getRMS(VoiceChannels channel)
{
    return &rms[channel];
}

envelopeGenerator* AudioVoice::getEnvelopeGenerator(EnvelopeGeneratorType type)
{
    return &envelope[type];
}
