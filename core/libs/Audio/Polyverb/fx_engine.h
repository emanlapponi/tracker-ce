// Copyright 2014 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Adapted for Polyverb by Wojciech Jakóbczyk (jakobczyk.woj@gmail.com)
//
// -----------------------------------------------------------------------------
//
// Base class for building reverb.

#ifndef POLYVERB_FX_ENGINE_H_
#define POLYVERB_FX_ENGINE_H_

#include <algorithm>

#include "cosine_oscillator.h"
#include "util.h"

#define TAIL , -1

enum Format
{
    FORMAT_12_BIT,
    FORMAT_16_BIT,
    FORMAT_32_BIT
};

enum LFOIndex
{
    LFO_1,
    LFO_2
};

template<Format format>
struct DataType
{
};

template<>
struct DataType<FORMAT_12_BIT>
{
    typedef uint16_t T;

    static inline float Decompress(T value)
    {
        return static_cast<float>(static_cast<int16_t>(value)) / 4096.0f;
    }

    static inline T Compress(float value)
    {
        return static_cast<uint16_t>(Clip16(static_cast<int32_t>(value * 4096.0f)));
    }
};

template<>
struct DataType<FORMAT_16_BIT>
{
    typedef uint16_t T;

    static inline float Decompress(T value)
    {
        return static_cast<float>(static_cast<int16_t>(value)) / 32768.0f;
    }

    static inline T Compress(float value)
    {
        return static_cast<uint16_t>(Clip16(static_cast<int32_t>(value * 32768.0f)));
    }
};

template<>
struct DataType<FORMAT_32_BIT>
{
    typedef float T;

    static inline float Decompress(T value)
    {
        return value;
        ;
    }

    static inline T Compress(float value) { return value; }
};

template<size_t size, Format format = FORMAT_12_BIT>
class FxEngine
{
  public:
    typedef typename DataType<format>::T T;
    FxEngine() {}
    ~FxEngine() {}

    void Init(T* buffer, T* buffer2)
    {
        buffer_ = buffer;
        buffer2_ = buffer2;
        //    std::fill(&buffer_[0], &buffer_[size], 0);
        //    std::fill(&buffer2_[0], &buffer2_[size], 0);
        write_ptr_ = 0;
        write_ptr2_ = 0;
    }

    static constexpr size_t GetSize() { return size; }

    void Reset()
    {
        std::fill(&buffer_[0], &buffer_[size], 0);
        std::fill(&buffer2_[0], &buffer2_[size], 0);
        write_ptr_ = 0;
        write_ptr2_ = 0;
    }

    struct Empty
    {
    };

    template<int32_t l, typename T = Empty>
    struct Reserve
    {
        typedef T Tail;
        enum
        {
            length = l
        };
    };

    template<typename Memory, int32_t index>
    struct DelayLine
    {
        enum
        {
            length = DelayLine<typename Memory::Tail, index - 1>::length,
            base = DelayLine<Memory, index - 1>::base + DelayLine<Memory, index - 1>::length + 1
        };
    };

    template<typename Memory>
    struct DelayLine<Memory, 0>
    {
        enum
        {
            length = Memory::length,
            base = 0
        };
    };

    class Context
    {
        friend class FxEngine;

      public:
        Context() {}
        ~Context() {}

        inline void Load(float value) { accumulator_ = value; }

        inline void Read(float value, float scale) { accumulator_ += value * scale; }

        inline void Read(float value) { accumulator_ += value; }

        inline void Write(float& value) { value = accumulator_; }

        inline void Write(float& value, float scale)
        {
            value = accumulator_;
            accumulator_ *= scale;
        }

        template<typename D>
        inline void Write(D&, int32_t offset, float scale)
        {
            STATIC_ASSERT(D::base + D::length <= size, delay_memory_full);
            T w = DataType<format>::Compress(accumulator_);
            if (offset == -1)
            {
                buffer_[(write_ptr_ + D::base + D::length - 1) % size] = w;
            }
            else
            {
                buffer_[(write_ptr_ + D::base + offset) % size] = w;
            }
            accumulator_ *= scale;
        }

        template<typename D>
        inline void Write2(D&, int32_t offset, float scale)
        {
            STATIC_ASSERT(D::base + D::length <= size, delay_memory_full);
            T w = DataType<format>::Compress(accumulator_);
            if (offset == -1)
            {
                buffer2_[(write_ptr2_ + D::base + D::length - 1) % size] = w;
            }
            else
            {
                buffer2_[(write_ptr2_ + D::base + offset) % size] = w;
            }
            accumulator_ *= scale;
        }

        template<typename D>
        inline void Write(D& d, float scale)
        {
            Write(d, 0, scale);
        }

        template<typename D>
        inline void Write2(D& d, float scale)
        {
            Write2(d, 0, scale);
        }

#ifdef AUDIO_NULL_IS_SILENCE
        // Write(), and the written value is added to the activity flag
        template<typename D>
        inline void WriteQ(D& d, float scale)
        {
            nonzero_ |= static_cast<uint32_t>(DataType<format>::Compress(accumulator_));
            Write(d, 0, scale);
        }
        template<typename D>
        inline void Write2Q(D& d, float scale)
        {
            nonzero_ |= static_cast<uint32_t>(DataType<format>::Compress(accumulator_));
            Write2(d, 0, scale);
        }
#endif

        template<typename D>
        inline void WriteAllPass(D& d, int32_t offset, float scale)
        {
            Write(d, offset, scale);
            accumulator_ += previous_read_;
        }

        template<typename D>
        inline void WriteAllPass2(D& d, int32_t offset, float scale)
        {
            Write2(d, offset, scale);
            accumulator_ += previous_read_;
        }

        template<typename D>
        inline void WriteAllPass(D& d, float scale)
        {
            WriteAllPass(d, 0, scale);
        }

        template<typename D>
        inline void WriteAllPass2(D& d, float scale)
        {
            WriteAllPass2(d, 0, scale);
        }

        template<typename D>
        inline void Read(D&, int32_t offset, float scale)
        {
            STATIC_ASSERT(D::base + D::length <= size, delay_memory_full);
            T r;
            if (offset == -1)
            {
                r = buffer_[(write_ptr_ + D::base + D::length - 1) % size];
            }
            else
            {
                r = buffer_[(write_ptr_ + D::base + offset) % size];
            }
            float r_f = DataType<format>::Decompress(r);
            previous_read_ = r_f;
            accumulator_ += r_f * scale;
        }

        template<typename D>
        inline void Read2(D&, int32_t offset, float scale)
        {
            STATIC_ASSERT(D::base + D::length <= size, delay_memory_full);
            T r;
            if (offset == -1)
            {
                r = buffer2_[(write_ptr2_ + D::base + D::length - 1) % size];
            }
            else
            {
                r = buffer2_[(write_ptr2_ + D::base + offset) % size];
            }
            float r_f = DataType<format>::Decompress(r);
            previous_read_ = r_f;
            accumulator_ += r_f * scale;
        }

        template<typename D>
        inline void Read(D& d, float scale)
        {
            Read(d, 0, scale);
        }

        inline void Lp(float& state, float coefficient)
        {
            state += coefficient * (accumulator_ - state);
            accumulator_ = state;
        }

        inline void Hp(float& state, float coefficient)
        {
            state += coefficient * (accumulator_ - state);
            accumulator_ -= state;
        }

        template<typename D>
        inline void Interpolate(D& d, float offset, float scale)
        {
            STATIC_ASSERT(D::base + D::length <= size, delay_memory_full);
            MAKE_INTEGRAL_FRACTIONAL(offset);
            float a = DataType<format>::Decompress(
              buffer_[(write_ptr_ + offset_integral + D::base) % size]);
            float b = DataType<format>::Decompress(
              buffer_[(write_ptr_ + offset_integral + D::base + 1) % size]);
            float x = a + (b - a) * offset_fractional;
            previous_read_ = x;
            accumulator_ += x * scale;
        }

        template<typename D>
        inline void Interpolate2(D& d, float offset, float scale)
        {
            STATIC_ASSERT(D::base + D::length <= size, delay_memory_full);
            MAKE_INTEGRAL_FRACTIONAL(offset);
            float a = DataType<format>::Decompress(
              buffer2_[(write_ptr2_ + offset_integral + D::base) % size]);
            float b = DataType<format>::Decompress(
              buffer2_[(write_ptr2_ + offset_integral + D::base + 1) % size]);
            float x = a + (b - a) * offset_fractional;
            previous_read_ = x;
            accumulator_ += x * scale;
        }

        template<typename D>
        inline void Interpolate(D& d, float offset, LFOIndex index, float amplitude, float scale)
        {
            STATIC_ASSERT(D::base + D::length <= size, delay_memory_full);
            offset += amplitude * lfo_value_[index];
            MAKE_INTEGRAL_FRACTIONAL(offset);
            float a = DataType<format>::Decompress(
              buffer_[(write_ptr_ + offset_integral + D::base) % size]);
            float b = DataType<format>::Decompress(
              buffer_[(write_ptr_ + offset_integral + D::base + 1) % size]);
            float x = a + (b - a) * offset_fractional;
            previous_read_ = x;
            accumulator_ += x * scale;
        }

        template<typename D>
        inline void Interpolate2(D&, float offset, LFOIndex index, float amplitude, float scale)
        {
            STATIC_ASSERT(D::base + D::length <= size, delay_memory_full);
            offset += amplitude * lfo_value_[index];
            MAKE_INTEGRAL_FRACTIONAL(offset);
            float a = DataType<format>::Decompress(
              buffer2_[(write_ptr2_ + offset_integral + D::base) % size]);
            float b = DataType<format>::Decompress(
              buffer2_[(write_ptr2_ + offset_integral + D::base + 1) % size]);
            float x = a + (b - a) * offset_fractional;
            previous_read_ = x;
            accumulator_ += x * scale;
        }

      public:
#ifdef AUDIO_NULL_IS_SILENCE
        uint32_t nonzero_; // OR of everything written to the line buffers since the caller cleared it
#endif
        float accumulator_;
        float previous_read_;
        float lfo_value_[2];
        T* buffer_;
        T* buffer2_;
        int32_t write_ptr_;
        int32_t write_ptr2_;

        DISALLOW_COPY_AND_ASSIGN(Context);
    };

    inline void SetLFOFrequency(LFOIndex index, float frequency)
    {
        lfo_[index].template Init<COSINE_OSCILLATOR_APPROXIMATE>(frequency * 32.0f);
    }

    inline void Start(Context* c)
    {
        --write_ptr_;
        if (write_ptr_ < 0)
        {
            write_ptr_ += size;
        }
        --write_ptr2_;
        if (write_ptr2_ < 0)
        {
            write_ptr2_ += size;
        }
        c->accumulator_ = 0.0f;
        c->previous_read_ = 0.0f;
        c->buffer_ = buffer_;
        c->buffer2_ = buffer2_;
        c->write_ptr_ = write_ptr_;
        c->write_ptr2_ = write_ptr2_;
        if ((write_ptr_ & 30) == 0)
        {
            // c->lfo_value_[0] = lfo_[0].Next();
            c->lfo_value_[1] = lfo_[1].Next();
        }
        else
        {
            // c->lfo_value_[0] = lfo_[0].value();
            c->lfo_value_[1] = lfo_[1].value();
        }
    }

#ifdef AUDIO_NULL_IS_SILENCE
    // n samples of Start() without a Context: the write pointers and the LFO move as they would (for a block of silence in a reverb
    // whose line buffers are all zero, nothing else changes)
    inline void Skip(size_t n)
    {
        for (size_t i = 0; i < n; i++)
        {
            --write_ptr_;
            if (write_ptr_ < 0)
            {
                write_ptr_ += size;
            }
            --write_ptr2_;
            if (write_ptr2_ < 0)
            {
                write_ptr2_ += size;
            }
            if ((write_ptr_ & 30) == 0)
            {
                lfo_[1].Next();
            }
        }
    }
#endif

    inline void StartNoLFO(Context* c)
    {
        --write_ptr_;
        if (write_ptr_ < 0)
        {
            write_ptr_ += size;
        }
        --write_ptr2_;
        if (write_ptr2_ < 0)
        {
            write_ptr2_ += size;
        }
        c->accumulator_ = 0.0f;
        c->previous_read_ = 0.0f;
        c->buffer_ = buffer_;
        c->buffer2_ = buffer2_;
        c->write_ptr_ = write_ptr_;
        c->write_ptr2_ = write_ptr2_;
    }

  private:
    enum
    {
        MASK = size - 1
    };

    int32_t write_ptr_;
    int32_t write_ptr2_;
    T* buffer_;
    T* buffer2_;
    CosineOscillator lfo_[2];

    DISALLOW_COPY_AND_ASSIGN(FxEngine);
};

#endif // POLYVERB_FX_ENGINE_H_
