#ifndef INTERNAL_TICK_CLOCK_H_
#define INTERNAL_TICK_CLOCK_H_

#include <stdint.h>
#include <string.h>

// Fixed-point Q38.26 (1 / 2^26 us) internal sequencer tick clock replacing soft double accumulation.
//
// Both M4F and M7 without double FPU (i.MX RT1051 uses single-precision FPU) otherwise emit softfp
// double helper calls and divisions every audio block / tick. Using integer 2^-26 us units matches
// the stock double accumulator's exact tick timings while evaluating stockPeriodUs() only on
// tempo or swing factor bit-pattern changes (3-entry cache covers non-swing and both swing phases).
class InternalTickClock
{
  public:
    static constexpr int kFracBits = 26;
    static constexpr int64_t kNever = INT64_MAX;

    void reset() { accumulator = 0; }

    // Adds one audio block and invokes onTick(offsetUs) once per tick due in it, where offsetUs
    // is the integer microseconds from the tick to the end of the block (for MIDI clock sub-block offset).
    template<typename StockPeriodFn, typename TickFn>
    inline __attribute__((always_inline)) void advance(float intervalSeconds,
                                                       float tempo,
                                                       float swingFactor,
                                                       StockPeriodFn&& stockPeriodUs,
                                                       TickFn&& onTick)
    {
        const int64_t block = blockFixed(intervalSeconds);
        const int64_t period = periodFixed(tempo, swingFactor, stockPeriodUs);

        accumulator += block;
        while (accumulator >= period)
        {
            accumulator -= period;
            const int64_t offset = block - accumulator;
            onTick(offset > 0 ? static_cast<uint32_t>((offset + kHalf) >> kFracBits) : 0U);
        }
    }

    int64_t accumulatorFixed() const { return accumulator; }

    // period in us -> units of 2^-26 us. Non-positive or non-finite periods never tick.
    static int64_t toFixed(float periodUs)
    {
        const float scaled = periodUs * 67108864.0f; // 2^26, exact
        if (!(scaled > 0.0f) || scaled >= 4.0e18f)
            return kNever;
        const int64_t v = static_cast<int64_t>(scaled);
        return v < 1 ? 1 : v;
    }

  private:
    static constexpr int64_t kHalf = int64_t(1) << (kFracBits - 1);

    inline __attribute__((always_inline)) int64_t blockFixed(float intervalSeconds)
    {
        uint32_t bits;
        memcpy(&bits, &intervalSeconds, sizeof(bits));
        if (!blockValid || bits != blockBits)
        {
            blockBits = bits;
            blockValid = true;
            // (double)intervalSeconds * 1e6 scaled by 2^26
            blockUnits = static_cast<int64_t>(static_cast<double>(intervalSeconds) * 1000000.0 * 67108864.0);
        }
        return blockUnits;
    }

    template<typename StockPeriodFn>
    inline __attribute__((always_inline)) int64_t periodFixed(float tempo, float swingFactor, StockPeriodFn&& stockPeriodUs)
    {
        uint32_t tempoBits, factorBits;
        memcpy(&tempoBits, &tempo, sizeof(tempoBits));
        memcpy(&factorBits, &swingFactor, sizeof(factorBits));
        for (int i = 0; i < kCacheEntries; i++)
        {
            if (cache[i].valid && cache[i].tempoBits == tempoBits && cache[i].factorBits == factorBits)
                return cache[i].period;
        }
        CacheEntry& e = cache[nextVictim];
        nextVictim = (nextVictim + 1) % kCacheEntries;
        e.tempoBits = tempoBits;
        e.factorBits = factorBits;
        e.period = toFixed(stockPeriodUs());
        e.valid = true;
        return e.period;
    }

    static constexpr int kCacheEntries = 3; // no swing, swing step off, swing step on

    struct CacheEntry
    {
        uint32_t tempoBits = 0;
        uint32_t factorBits = 0;
        int64_t period = 0;
        bool valid = false;
    };

    int64_t accumulator = 0; // units of 2^-26 us
    CacheEntry cache[kCacheEntries];
    int nextVictim = 0;
    uint32_t blockBits = 0;
    int64_t blockUnits = 0;
    bool blockValid = false;
};

#endif
