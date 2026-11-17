#ifndef LIBS_AUDIO_EFFECT_SHORTDELAY_H_
#define LIBS_AUDIO_EFFECT_SHORTDELAY_H_
#include "AudioStream.h"

constexpr uint8_t MAX_SHORT_DELAY_VOICES = 12;
constexpr uint16_t MAX_SHORT_DELAY_TIME = 3500;
constexpr uint32_t MAX_DELAYLINE_LENGTH = 262144;

class AudioEffectShortDelay : public AudioStream
{
  public:
    AudioEffectShortDelay(void)
      : AudioStream(1, inputQueueArray)
    {
        blockUpdate();
    }

    void begin(float feedback,
               uint32_t time,
               bool pingPongEnabled,
               bool syncEnabled,
               uint8_t syncRatioIdx,
               float frequency);
    void setBuffer(int16_t* buffer);
    void setFeedback(float f);
    void setTime(float t);
    int getTime();
    void setRate(uint8_t r);
    void setPingpongEnable(bool pp);
    void setSyncEnable(bool se);
    void setFilterFrequency(float frequency);
    virtual void update(void);
    bool process(int16_t input[], int16_t outputLeft[], int16_t outputRight[], int sampleCount);
    void clear();
    void blockUpdate();
    void unblockUpdate();
    bool isSilent() const
    {
        return silentSamplesCount >= timeInSamples;
    }

  private:
    struct FilterCoeffs
    {
        int32_t a0;
        int32_t a1;
        int32_t b1;
    };

    static constexpr int kFilterCoeffsShift = 10;
    static constexpr int kFeedbackShift = 8;
    static constexpr uint16_t kMaxFilterCoeff = (1 << kFilterCoeffsShift) - 1;
    static constexpr float kMaxFeedback = 0.99f;
    static constexpr uint32_t delaylineLengthMask = MAX_DELAYLINE_LENGTH - 1;
    int16_t* pingpongDelaylineRight;
    int16_t* pingpongDelaylineLeft;
    int16_t* monoDelayline;
    uint32_t headIdx;
    audio_block_t* inputQueueArray[1];
    uint32_t timeInSamples;
    uint8_t feedback;
    volatile bool noRefresh;
    uint8_t
      isPingpong; // uint8_t allows to use conditionally execution instead conditionally branch
    bool isSync;
    FilterCoeffs filterCoeffs;
    int32_t lastFilterInRight;
    int32_t lastFilterInLeft;
    int32_t lastFilterInMono;
    int32_t lastFilterOutRight;
    int32_t lastFilterOutLeft;
    int32_t lastFilterOutMono;
    uint32_t silentSamplesCount = MAX_DELAYLINE_LENGTH;

    // Only the lines of the mode that is heard run (mono: 1 line, ping pong: 2). A line set is "live" while it is written, depth is how
    // many samples back from the head its stored data can be trusted (saturates at MAX_DELAYLINE_LENGTH). On a mode switch the new set
    // goes live with depth 0 and the old one keeps running (output crossfade over kFadeSamples, then the old set stops once the new one has
    // been written for timeInSamples, so a quick switch back loses nothing), reads of the new set older than
    // its depth are derived from the old set's stored lines (mono -> ping pong: left = mono, right = 0, ping pong -> mono: left + right).
    // Steady state is one live set at full weight, both fast loops equal the stock three line loop for that mode bit for bit.
    static constexpr int32_t kFadeOne = 1 << 15;              // crossfade weight one (15 bit, products stay inside int32)
    static constexpr int32_t kFadeSamples = 4096;             // crossfade length of a mode switch (93 ms)
    bool ppLive = false;                                      // ping pong lines (left, right) are written
    bool monoLive = false;                                    // the mono line is written
    int32_t ppWeight = 0;                                     // output weight of the ping pong set, 0..kFadeOne, the mono set has the rest
    uint32_t ppDepth = 0;                                     // valid history of the live sets, in samples back from the head
    uint32_t monoDepth = 0;
    uint32_t ppSeed = 0;                                      // history before activation the other set's stored lines can still supply
    uint32_t monoSeed = 0;
    void startMode(bool pingpong);
    bool prepareBlock(bool wantPingpong);
    template<bool kTrackQuiet>
    int32_t runPingpong(int16_t input[], int16_t outputLeft[], int16_t outputRight[], int sampleCount);
    template<bool kTrackQuiet>
    int32_t runMono(int16_t input[], int16_t outputLeft[], int16_t outputRight[], int sampleCount);
    int32_t runSwitching(int16_t input[], int16_t outputLeft[], int16_t outputRight[], int sampleCount, bool wantPingpong);
    // process() for update(): with kTrackQuiet the block is also checked for quietSamplesCount (silent input only, the loop then has a
    // few more instructions), without it the count is cleared (the input has sound)
    template<bool kTrackQuiet>
    bool processBlock(int16_t input[], int16_t outputLeft[], int16_t outputRight[], int sampleCount);
#ifdef AUDIO_NULL_IS_SILENCE
    void syncIdleMode(bool wantPingpong);
    // Consecutive samples (whole blocks) in which the values written to the lines that run were zero: the last timeInSamples of them
    // fill the delay window. With the filters at rest a block of silent input can be skipped bit exactly.
    uint32_t quietSamplesCount = 0;
    // Consecutive samples back from headIdx where all three lines (mono + ping-pong L/R) are known zero.
    uint32_t zeroedAllSamples = MAX_DELAYLINE_LENGTH;
#endif
};

#endif /* LIBS_AUDIO_EFFECT_SHORTDELAY_H_ */
