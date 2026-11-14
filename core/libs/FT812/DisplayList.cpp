#include "DisplayList.h"

#include <string.h>

namespace displayList
{
uint32_t droppedCommands = 0;
uint32_t overflows = 0;
bool coprocessorDrew = false;

namespace
{
constexpr uint16_t kOptSigned = 256;
constexpr uint16_t kOptCenterX = 512;
constexpr uint16_t kOptCenterY = 1024;
constexpr uint16_t kOptRightX = 2048;
constexpr uint32_t kFontHeightOffset = 140;
// One past the largest coordinate VERTEX2II can carry.
constexpr int32_t kVertex2iiLimit = 512;

uint32_t* first = nullptr;
uint32_t* cursor = nullptr;
uint32_t* limit = nullptr;
bool overflow = false;
FontMetrics fontMetrics = nullptr;
uint32_t numberBase = 10;

void glyphs(int32_t x,
            int32_t y,
            uint8_t handle,
            uint16_t options,
            const uint8_t* characters,
            uint32_t count)
{
    const uint8_t* metrics = fontMetrics != nullptr ? fontMetrics(handle) : nullptr;
    if (metrics == nullptr)
    {
        droppedCommands++;
        return;
    }

    // The chip leaves the last pixel column out of the width it aligns by.
    int32_t width = -1;
    for (uint32_t i = 0; i < count; i++)
    {
        width += metrics[characters[i]];
    }
    if (options & kOptCenterX)
    {
        // An arithmetic shift, so that the empty string moves half a pixel
        // right like on the chip. It draws nothing either way.
        x -= width >> 1;
    }
    if (options & kOptRightX)
    {
        x -= width;
    }
    if (options & kOptCenterY)
    {
        y -= metrics[kFontHeightOffset] / 2;
    }

    const uint8_t bitmapHandle = handle & 31;
    put(0x22000000);                // SAVE_CONTEXT
    put(0x27000002);                // VERTEX_FORMAT(2)
    put(0x05000000 | bitmapHandle); // BITMAP_HANDLE
    put(0x1F000001);                // BEGIN(BITMAPS)
    for (uint32_t i = 0; i < count; i++)
    {
        uint8_t character = characters[i];
        uint32_t cell = character & 127;
        if (x >= 0 && x < kVertex2iiLimit && y >= 0 && y < kVertex2iiLimit)
        {
            put(0x80000000 | ((uint32_t)x << 21) | ((uint32_t)y << 12) |
                ((uint32_t)bitmapHandle << 7) | cell);
        }
        else
        {
            put(0x06000000 | cell);
            put(0x40000000 | (((uint32_t)(x * 4) & 0x7FFF) << 15) | ((uint32_t)(y * 4) & 0x7FFF));
        }
        x += metrics[character];
    }
    put(0x23000000); // RESTORE_CONTEXT
}

uint32_t bitsPerPixel(uint16_t format)
{
    switch (format)
    {
        case 0: return 16; // ARGB1555
        case 1: return 1;  // L1
        case 2: return 4;  // L4
        case 6: return 16; // ARGB4
        case 7: return 16; // RGB565
        case 17: return 2; // L2
        default: return 8;
    }
}
} // namespace

void setFontMetrics(FontMetrics lookup)
{
    fontMetrics = lookup;
}

void capture(uint32_t* buffer, uint32_t capacityWords)
{
    first = buffer;
    cursor = buffer;
    limit = buffer + capacityWords;
    overflow = false;
}

uint32_t finish()
{
    uint32_t words = overflow ? 0 : (uint32_t)(cursor - first);
    if (overflow)
    {
        overflows++;
    }
    first = nullptr;
    cursor = nullptr;
    limit = nullptr;
    return words;
}

bool capturing()
{
    return first != nullptr;
}

void put(uint32_t word)
{
    if (cursor == limit)
    {
        overflow = true;
        return;
    }
    *cursor++ = word;
}

void text(int16_t x, int16_t y, int16_t font, uint16_t options, const char* string, uint32_t length)
{
    // Some controls pass a null text; on the device that reads whatever is
    // at address 0.
    if (string == nullptr)
    {
        string = "";
    }
    uint32_t count = strlen(string);
    if (length != 0 && length < count)
    {
        count = length;
    }
    glyphs(x, y, (uint8_t)font, options, (const uint8_t*)string, count);
}

void number(int16_t x, int16_t y, int16_t font, uint16_t options, int32_t n)
{
    static const char kDigits[] = "0123456789ABCDEF";
    // Enough for 32 binary digits and a sign.
    char buffer[34];
    char* end = buffer + sizeof(buffer);
    char* out = end;

    uint32_t value = (uint32_t)n;
    bool negative = (options & kOptSigned) != 0 && n < 0;
    if (negative)
    {
        value = 0u - value;
    }
    uint32_t base = (numberBase >= 2 && numberBase <= 16) ? numberBase : 10;
    // The low byte of the options is an exact digit count: shorter numbers
    // are padded with zeros, longer ones lose their leading digits.
    uint32_t wanted = options & 255;
    if (wanted > 32)
    {
        wanted = 32;
    }
    uint32_t produced = 0;
    do
    {
        *--out = kDigits[value % base];
        value /= base;
        produced++;
    } while (wanted != 0 ? produced < wanted : (value != 0 && produced < 32));
    if (negative)
    {
        *--out = '-';
    }
    glyphs(x, y, (uint8_t)font, options, (const uint8_t*)out, (uint32_t)(end - out));
}

void setBase(uint32_t base)
{
    numberBase = base;
}

// The gradient is the chip's built-in 256-step ramp bitmap, stretched along
// the line from (x0, y0) to (x1, y1): the first pass writes the ramp to the
// alpha channel and the smaller of the two colours everywhere, the following
// ones add what the end colours have on top of that, weighted by the alpha.
void gradient(int16_t x0, int16_t y0, uint32_t rgb0, int16_t x1, int16_t y1, uint32_t rgb1)
{
    int32_t dx = x1 - x0;
    int32_t dy = y1 - y0;
    int32_t lengthSquared = dx * dx + dy * dy;

    put(0x22000000); // SAVE_CONTEXT
    if (lengthSquared == 0)
    {
        put(0x1501FFFF);
        put(0x1601FFFF);
        put(0x1801FFFF);
        put(0x1901FFFF);
        put(0x17FFFFFF);
        put(0x1AFFFFFF);
    }
    else
    {
        int32_t a = dx * 65536 / lengthSquared;
        int32_t b = dy * 65536 / lengthSquared;
        int64_t dot = (int64_t)x0 * dx + (int64_t)y0 * dy;
        // The ramp starts 128 pixels into the bitmap.
        int32_t c = (int32_t)(((int64_t)0x8000 * lengthSquared - dot * 65536) / lengthSquared);
        put(0x15000000 | ((uint32_t)a & 0x1FFFF));
        put(0x16000000 | ((uint32_t)b & 0x1FFFF));
        put(0x18000000);
        put(0x19000000);
        put(0x17000000 | ((uint32_t)c & 0xFFFFFF));
        put(0x1A000000);
    }

    // The chip compares all four bytes, although only three are colour.
    uint32_t low = 0;
    uint32_t up = 0;
    uint32_t down = 0;
    for (int shift = 24; shift >= 0; shift -= 8)
    {
        uint32_t v0 = (rgb0 >> shift) & 255;
        uint32_t v1 = (rgb1 >> shift) & 255;
        low |= (v0 < v1 ? v0 : v1) << shift;
        up |= (v1 > v0 ? v1 - v0 : 0) << shift;
        down |= (v0 > v1 ? v0 - v1 : 0) << shift;
    }

    put(0x0500000F); // BITMAP_HANDLE(15)
    put(0x01200065); // BITMAP_SOURCE: the ramp in the chip's ROM
    put(0x28000000);
    put(0x071C0001); // BITMAP_LAYOUT(L8, 512, 1)
    put(0x29000000);
    put(0x08040000); // BITMAP_SIZE(NEAREST, BORDER, REPEAT, full screen)
    put(0x04000000 | (low & 0xFFFFFF));
    put(0x100000FF); // COLOR_A(255)
    put(0x0B000008); // BLEND_FUNC(ONE, ZERO)
    put(0x2000000F); // COLOR_MASK(all)
    put(0x1F000001); // BEGIN(BITMAPS)
    put(0x80000780); // VERTEX2II(0, 0, 15, 0)
    put(0x2000000E); // COLOR_MASK(colour only)
    if (up != 0)
    {
        put(0x04000000 | (up & 0xFFFFFF));
        put(0x0B000019); // BLEND_FUNC(DST_ALPHA, ONE)
        put(0x80000780);
    }
    if (down != 0)
    {
        put(0x04000000 | (down & 0xFFFFFF));
        put(0x0B000029); // BLEND_FUNC(ONE_MINUS_DST_ALPHA, ONE)
        put(0x80000780);
    }
    put(0x23000000); // RESTORE_CONTEXT
}

void setBitmap(uint32_t source, uint16_t format, uint16_t width, uint16_t height)
{
    uint32_t stride = (width * bitsPerPixel(format) + 7) / 8;
    put(0x01000000 | (source & 0x3FFFFF));
    put(0x29000000 | ((uint32_t)(width >> 9) << 2) | (height >> 9));
    put(0x08000000 | ((uint32_t)(width & 511) << 9) | (height & 511));
    put(0x28000000 | ((stride >> 10) << 2) | (height >> 9));
    put(0x07000000 | ((uint32_t)format << 19) | ((stride & 1023) << 9) | (height & 511));
}

void rotate270(uint16_t height)
{
    uint32_t side = 2u * (height / 2);
    put(0x1A000000);
    put(0x19000000);
    put(0x180000FF);
    put(0x17000000 | ((side * 256 - 1) & 0xFFFFFF));
    put(0x1601FF00);
    put(0x15000000);
}
} // namespace displayList
