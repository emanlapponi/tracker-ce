#ifdef FEATURE_SYNTH
#include "SynthVoice.h"

SynthVoice::SynthVoice()
{
}

void SynthVoice::init(Synth* synth)
{
    polySynth.init(synth, &panningAmplifier);
}

PolySynth* SynthVoice::getPolySynth()
{
    return &polySynth;
}

AudioAmplifier* SynthVoice::getPanningAmplifier(int synthIdx, Channels channel)
{
    assert(channel < Channels::kChannelsCount);
    assert(synthIdx < kSynthParts);

    return &panningAmplifier[synthIdx][channel];
}

#endif
